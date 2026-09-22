// Worldseed - terrain runtime decoupe en chunks, depuis le monde spherique.

#include "Procedural/WorldseedTerrain.h"
#include "Procedural/WorldseedGameInstance.h"
#include "Procedural/WorldseedSkyDriverComponent.h"
#include "Procedural/WorldseedWaterComponent.h"
#include "Procedural/WorldseedGroundProxy.h"
#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedTrace.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"
#include "UObject/UnrealType.h"

#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "TimerManager.h"


AWorldseedTerrain::AWorldseedTerrain()
{
	PrimaryActorTick.bCanEverTick = false;

	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(RootScene);

	SkyDriver = CreateDefaultSubobject<UWorldseedSkyDriverComponent>(TEXT("SkyDriver"));
	Water = CreateDefaultSubobject<UWorldseedWaterComponent>(TEXT("Water"));
}

void AWorldseedTerrain::BeginPlay()
{
	Super::BeginPlay();

	CielDInspection();

	Rebuild();

	// LE MINUTEUR ET LE PLACEMENT APPARTIENNENT AU MAILLEUR, donc au voxel
	// quand c'est lui qui tient le relief. Les laisser tourner ferait defiler
	// des chunks de carte d'altitude sous ceux du voxel, et poserait le joueur
	// sur une surface qui n'est plus celle qu'il voit -- "surface + 150 cm" n'a
	// d'ailleurs aucun sens dans une grotte.
	if (!bUseVoxelMesher)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(UpdateTimer, this,
				&AWorldseedTerrain::UpdateChunks, FMath::Max(UpdatePeriod, 0.05f), true);
		}

		if (bPlacePlayerAfterGenerate)
		{
			GetWorldTimerManager().SetTimerForNextTick(
				this, &AWorldseedTerrain::PlacePlayerOnTerrain);
		}
	}

	// LE SOL DE FOND SE SURVEILLE SUR LES DEUX CHEMINS, voxel comme carte
	// d'altitude : il traverse les cavites dans les deux cas.
	if (bHideGroundProxyUnderground)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(ProxyTimer, this,
				&AWorldseedTerrain::UpdateGroundProxyVisibility, 0.1f, true);
		}
	}
}

void AWorldseedTerrain::UpdateGroundProxyVisibility()
{
	WORLDSEED_TRACE(VisibiliteSolDeFond);

	if (!GroundProxy)
	{
		return;
	}
	// C'EST LA NAPPE QU'ON VOIT QU'ON BASCULE, PAS CELLE QUE L'EAU LIT.
	UProceduralMeshComponent* const Vue =
		HorizonProxy ? HorizonProxy->GetMesh() : nullptr;
	UWorld* const World = GetWorld();
	if (!Vue || !World)
	{
		return;
	}

	// C'EST LA CAMERA QUI DECIDE, PAS LE PION. En vue a la troisieme personne
	// le bras place l'oeil jusqu'a quatre metres derriere le personnage : il
	// peut etre dehors quand le personnage est dedans, et c'est l'oeil qui
	// voit le decor.
	const APlayerCameraManager* const Cam =
		UGameplayStatics::GetPlayerCameraManager(World, 0);
	if (!Cam)
	{
		return;
	}
	const FVector OeilCm = Cam->GetCameraLocation();

	// --- Y A-T-IL DE LA ROCHE ENTRE L'OEIL ET LE CIEL ? ----------------------
	//
	// PREMIERE VERSION FAUTIVE, ET ELLE SE VOYAIT EN PLEIN JOUR. Elle comparait
	// l'altitude de l'oeil a celle de la NAPPE A SON APLOMB : sous la nappe,
	// on cachait. Or sur une pente, la camera est derriere ET plus bas que le
	// personnage, donc son aplomb tombe souvent sur du terrain PLUS HAUT
	// qu'elle. Mesure : camera en plein air a 83,5 m, surface a son aplomb
	// 97,7 m -- « 14,2 m sous la nappe », decor masque, horizon disparu et
	// terrain reduit au disque de chunks charges au milieu de l'ocean.
	//
	// La vraie question n'est pas une altitude, c'est une OCCULTATION : un
	// decor d'horizon ne gene que s'il s'interpose, et il ne peut s'interposer
	// que si l'on est sous un plafond. Un sondage vertical repond exactement,
	// sur la GEOMETRIE REELLE plutot que sur une approximation du relief. Le
	// sol de fond n'a pas de collision : il ne peut pas se sonder lui-meme.
	FHitResult Touche;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WorldseedProxyRoof), false);
	if (const APawn* const Pion = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		Params.AddIgnoredActor(Pion);
	}

	const bool bSousPlafond = World->LineTraceSingleByChannel(
		Touche, OeilCm,
		OeilCm + FVector(0.0, 0.0, GroundProxyRoofProbeM * WorldseedMetersToCm),
		ECC_Visibility, Params);

	// ANTI-REBOND : au bord d'un plafond, deux sondages voisins peuvent ne pas
	// dire la meme chose. On n'accepte une bascule qu'apres deux mesures
	// concordantes, sans quoi le decor clignote au franchissement d'un seuil.
	if (bSousPlafond == bGroundProxyHidden)
	{
		ProxyVotes = 0;
		return;
	}
	if (++ProxyVotes < 2)
	{
		return;
	}
	ProxyVotes = 0;
	bGroundProxyHidden = bSousPlafond;

	// ON SORT DU RENDU PRINCIPAL, PAS DE LA PASSE DE PROFONDEUR, et la nuance
	// est vitale : ce sol de fond porte le UWaterTerrainComponent, c'est meme
	// toute sa raison d'etre. Le plugin Water lit le relief dans sa passe de
	// PROFONDEUR, et le moteur decide d'y entrer par
	// `ShouldRenderInDepthPass() = bRenderInMainPass || bRenderInDepthPass`
	// (PrimitiveSceneProxy.h:804). Couper le premier en armant le second
	// retire donc la nappe de l'image SANS la retirer de l'eau. La masquer
	// entierement -- SetActorHiddenInGame -- l'aurait sortie des deux, et
	// l'ocean cesse de se dessiner sans le moindre avertissement.
	// --- LA BASCULE PASSE PAR LA SECTION, PAS PAR LE DRAPEAU DE RENDU --------
	//
	// MESURE QUI A TRANCHE, et elle est sans appel. Sur toute une partie il y a
	// eu EXACTEMENT quatre trames au-dessus de 300 ms, et EXACTEMENT quatre
	// bascules : trames 390, 396, 804 et 94, les numeros memes des bascules.
	// Rien d'autre n'a jamais depasse 300 ms.
	//
	//     cout de l'appel SetRenderInMainPass  ...   0,02 ms
	//     cout de la trame qui suit            ...   508 a 528 ms
	//     budget                               ...   16,67 ms
	//
	// L'ECART ENTRE CES DEUX CHIFFRES EST TOUTE L'EXPLICATION.
	// `SetRenderInMainPass` ne fait que MARQUER l'etat de rendu sale ; la
	// recreation du proxy de scene -- 8 388 608 sommets pour cette nappe -- a
	// lieu en fin de trame. Chronometrer l'appel seul concluait « la bascule
	// est gratuite », ce qui est vrai et sans le moindre interet.
	//
	// `SetMeshSectionVisible` n'appelle PAS MarkRenderStateDirty : il pousse
	// une commande de rendu qui bascule un booleen dans le proxy existant
	// (ProceduralMeshComponent.cpp:789). Mesure : 0,00 ms contre 508 a 528.
	//
	// IL AVAIT ETE ESSAYE PUIS RENDU, ET IL FAUT SAVOIR POURQUOI IL REVIENT.
	// La visibilite de section retire la geometrie de TOUTES les passes, celle
	// de PROFONDEUR comprise. Applique a la nappe QUE L'EAU LIT, elle coupait
	// l'ocean -- signale en jeu des la premiere traversee : « avant j'avais de
	// l'eau dans l'arche et maintenant elle est coupee ».
	//
	// ELLE PORTE DESORMAIS SUR LA NAPPE QU'ON VOIT, qui ne nourrit rien. La
	// nappe de l'eau, elle, est sortie du rendu principal une fois pour toutes
	// a la construction et n'est plus jamais basculee : son cout est nul par
	// construction, et sa passe de profondeur reste armee.
	//
	// C'est la separation en deux maillages qui rend les deux compatibles ;
	// tant qu'un seul servait les deux maitres, il fallait choisir entre un
	// a-coup de 520 ms et un ocean coupe.
	const double Depart = FPlatformTime::Seconds();

	// LES DEUX SECTIONS BASCULENT ENSEMBLE. La mer du decor vit en section 1
	// depuis qu'elle a son propre materiau ; n'en basculer qu'une laisserait
	// l'horizon marin visible depuis l'interieur d'une grotte -- exactement le
	// defaut que cette bascule existe pour empecher, et il ne se signalerait
	// pas puisque la section terre, elle, disparaitrait bien.
	Vue->SetMeshSectionVisible(0, !bSousPlafond);
	Vue->SetMeshSectionVisible(1, !bSousPlafond);

	const double BasculeMs = (FPlatformTime::Seconds() - Depart) * 1000.0;

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] sol de fond : %s (plafond %s au-dessus de l'oeil) ")
		TEXT("-- bascule %.2f ms sur %d sommets vus"),
		bSousPlafond ? TEXT("retire") : TEXT("rendu"),
		bSousPlafond ? TEXT("trouve") : TEXT("absent"),
		BasculeMs, ProxyVueSommets);
}

void AWorldseedTerrain::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ProxyTimer);
		World->GetTimerManager().ClearTimer(UpdateTimer);
	}

	// Le sol de fond est un acteur a part : il ne part pas avec le terrain.
	if (GroundProxy)
	{
		GroundProxy->Destroy();
		GroundProxy = nullptr;
	}

	if (VoxelTerrain)
	{
		VoxelTerrain->Destroy();
		VoxelTerrain = nullptr;
	}

	Super::EndPlay(Reason);
}

bool AWorldseedTerrain::AcquireWorld()
{
	// Le menu a deja paye la chaine complete, tectonique et climat compris.
	if (UWorldseedGameInstance* GI =
		UWorldseedGameInstance::GetWorldseedGameInstance(this))
	{
		FWorldseedWorldData Loaded;
		if (GI->TryGetWorld(Loaded))
		{
			WorldSeed = Loaded.Seed;
			Geometry = Loaded.Geometry;
			HeightsM = MoveTemp(Loaded.ElevationM);
			TempC = MoveTemp(Loaded.TempC);
			PrecipMm = MoveTemp(Loaded.PrecipMm);
			SeasonalAmpC = MoveTemp(Loaded.SeasonalAmpC);
			ContinentalityGrid = MoveTemp(Loaded.Continentality);
			Biomes = MoveTemp(Loaded.Biomes);
			Lithology.Id = MoveTemp(Loaded.LithologyId);
			TexturePack = Loaded.TexturePack;

			// Le point de depart choisi dans le menu. Ce terrain ne s'en sert
			// pas lui-meme -- c'est l'acteur voxel qui place le joueur -- il
			// ne fait que le convoyer jusqu'a lui.
			bDepartDemande = Loaded.bHasSpawn;
			DepartXYM = Loaded.SpawnXYM;
			Colouring = (TexturePack == EWorldseedTexturePack::BiomeColour)
				? EWorldseedTerrainColouring::BiomeColour
				: EWorldseedTerrainColouring::TexturePack;

			// LE RESEAU DE GROTTES N'EST PAS TRANSPORTE PAR LE MENU, et il ne
			// doit pas l'etre : il se rebatit a l'identique depuis la graine,
			// la lithologie et le climat. Le transporter doublerait une donnee
			// deterministe. En revanche il FAUT le rebatir, sans quoi une
			// partie lancee depuis le menu n'aurait aucune grotte la ou une
			// partie lancee en PIE en a.
			RebuildCaveNetwork();

			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] monde repris du menu : seed=%d  %dx%d  saisons=%d"),
				WorldSeed, Geometry.NX, Geometry.NY, SeasonalAmpC.Num());
			return true;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] aucun monde en attente, generation de secours"));

	WorldseedPipeline::FResult World;
	FString Error;
	if (!WorldseedPipeline::Generate(FallbackSeed, FallbackHeightMeters,
		FallbackResolutionY, World, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("[Worldseed] generation de secours impossible : %s"), *Error);
		return false;
	}

	WorldSeed = FallbackSeed;
	Geometry = World.Geometry;
	HeightsM = MoveTemp(World.ElevationM);
	TempC = MoveTemp(World.Climate.TempMeanC);
	PrecipMm = MoveTemp(World.Climate.PrecipMm);
	SeasonalAmpC = MoveTemp(World.Climate.SeasonalAmpC);
	ContinentalityGrid = MoveTemp(World.Climate.Continentality);
	Biomes = MoveTemp(World.Biomes);
	Caves = MoveTemp(World.Caves);
	Lithology = MoveTemp(World.Lithology);
	return true;
}

void AWorldseedTerrain::RebuildCaveNetwork()
{
	Caves.Reset();

	FString Error;
	const UWorldseedRules* Rules = WorldseedPipeline::GetRules(Error);
	if (!Rules)
	{
		return;
	}

	// LA LITHOLOGIE EST DESORMAIS TRANSPORTEE, ET CE COMMENTAIRE DISAIT
	// L'INVERSE. Il affirmait qu'elle « se recalcule depuis la tectonique,
	// qu'on n'a pas ici », et concluait a un reseau sans contrainte de roche --
	// « plutot que pas de reseau du tout ». La conclusion etait raisonnable, la
	// premisse fausse : le champ `LithologyId` existait deja dans les donnees
	// transportees, le CACHE l'ecrivait et le relisait, et seul le passage du
	// menu au niveau le laissait tomber.
	//
	// CE QUE CELA COUTAIT, MESURE SUR LE PARCOURS COMPLET (graine 20260909,
	// monde de 2 x 1 km) : le pipeline calculait UN reseau sous contrainte de
	// roche -- 1 chambre -- et le terrain en rebatissait un AUTRE sans elle --
	// 9 chambres. Le joueur ne jouait donc pas le monde que le menu lui avait
	// montre, et rien ne le signalait. S'y ajoutait « PAS de lithologie » cote
	// voxel, donc des parois a la couleur du biome de surface au lieu de celle
	// de la roche.
	if (!Lithology.IsValid(Geometry.CellCount()))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] grottes : lithologie absente (%d identifiants pour ")
			TEXT("%d cellules) -- reseau rebati SANS contrainte de roche"),
			Lithology.Id.Num(), Geometry.CellCount());
	}

	WorldseedCaves::Build(Geometry, HeightsM, PrecipMm, Lithology,
		FWorldseedLithologyRules::FromRules(*Rules),
		FWorldseedCaveRules::FromRules(*Rules), HeightExaggeration, WorldSeed, Caves);
}

void AWorldseedTerrain::Rebuild()
{
	for (const TPair<FIntPoint, FWorldseedChunk>& Pair : Chunks)
	{
		if (Pair.Value.Mesh)
		{
			Pair.Value.Mesh->DestroyComponent();
		}
	}
	Chunks.Empty();

	if (!AcquireWorld() || Geometry.NX < 2 || HeightsM.Num() != Geometry.CellCount())
	{
		return;
	}

	const int32 Cells = FMath::Max(ChunkCells, 16);
	ChunksX = FMath::DivideAndRoundUp(Geometry.NX, Cells);
	ChunksY = FMath::DivideAndRoundUp(Geometry.NY, Cells);

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] terrain %.1f x %.1f km  grille %dx%d  decoupe %dx%d chunks de %d cellules"),
		Geometry.WidthM() / 1000.0f, Geometry.HeightM / 1000.0f,
		Geometry.NX, Geometry.NY, ChunksX, ChunksY, Cells);

	// LE SOL DE FOND AVANT LES CHUNKS ET AVANT L'EAU : c'est lui qui donne au
	// systeme d'eau un sol sur TOUTE la carte, la ou les chunks n'en couvrent
	// qu'un rayon autour du joueur.
	BuildGroundProxy();

	if (bUseVoxelMesher)
	{
		// LE VOXEL NE REMPLACE QUE LE MAILLAGE. Le sol de fond vient d'etre
		// bati, l'ocean suit, le ciel et les questions de climat restent ici :
		// seule la geometrie proche change de main.
		SpawnVoxelTerrain();
	}
	else
	{
		UpdateChunks();
	}

	// L'ocean vient APRES les chunks : il se pose sur un relief deja connu, et
	// n'a pas besoin d'etre refaite quand les chunks changent de resolution.
	if (Water)
	{
		Water->Build(Geometry, HeightsM, HeightExaggeration);
	}
}

void AWorldseedTerrain::SpawnVoxelTerrain()
{
	UWorld* World = GetWorld();
	if (!World || VoxelTerrain)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// MEME TRANSFORM QUE CE TERRAIN : les deux travaillent dans le meme repere,
	// et le champ de densite est exprime en metres dans celui de l'acteur.
	UClass* Classe = VoxelTerrainClass ? VoxelTerrainClass.Get()
		: AWorldseedVoxelTerrain::StaticClass();
	// POSE DIFFEREE, ET C'EST INDISPENSABLE. BeginPlay charge le monde ; il faut
	// donc lui donner le notre AVANT, sans quoi il en genere un autre -- et sans
	// monde en attente dans l'instance de jeu, sa generation de secours ne
	// tourne pas a la meme resolution que la notre. Meme graine, relief
	// different, et le sol de fond decrivait alors un autre monde que celui
	// qu'on a sous les pieds.
	VoxelTerrain = World->SpawnActorDeferred<AWorldseedVoxelTerrain>(
		Classe, GetActorTransform(), this, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (VoxelTerrain)
	{
		VoxelTerrain->AdoptWorld(WorldSeed, Geometry, HeightsM, Biomes,
			HeightExaggeration, Caves, Lithology, PrecipMm, TempC);

		// LES DEUX CHAMPS QUI N'EXPLIQUENT, ET NE GENERENT RIEN. Ils passent a
		// part parce qu'AdoptWorld transmet le MONDE -- ce qui sert a mailler
		// -- alors que ceux-ci ne servent qu'au releve de jeu. Les melanger
		// inviterait a croire que le champ de densite les lit.
		VoxelTerrain->AdoptChampsClimat(ContinentalityGrid, SeasonalAmpC);

		// LE DEPART CHOISI DANS LE MENU PASSE PAR ICI, ET C'EST LE SEUL
		// CHEMIN. Le voxel lit bien lui-meme l'instance de jeu, mais seulement
		// quand personne ne lui donne de monde -- un PIE lance depuis
		// l'editeur, sans passer par le menu. Des que L_Menu a joue, c'est ce
		// terrain-ci qui a consomme l'instance et qui transmet ; le depart
		// pose sur l'autre chemin ne servait donc jamais. Mesure : le journal
		// disait « monde repris du TERRAIN » puis « terre emergee la plus
		// proche a (8, 8) m », c'est-a-dire une recherche partie de l'origine
		// alors que le menu avait demande (-2766, 2297).
		if (bDepartDemande)
		{
			VoxelTerrain->DemanderDepart(DepartXYM);
		}

		// LE MATERIAU AUSSI SE TRANSMET. Sans lui les chunks voxel prennent le
		// gris par defaut, qui ne lit pas la couleur de sommet : le relief
		// proche devenait uniformement gris pendant que le sol de fond, lui,
		// gardait ses couleurs de biome. La difference se voyait a l'horizon.
		FWorldseedAppearance Mode;
		Mode.bTexturePack = (Colouring == EWorldseedTerrainColouring::TexturePack)
			&& (TexturePack != EWorldseedTexturePack::BiomeColour)
			&& (Biomes.Index.Num() == Geometry.CellCount());
		Mode.bColourByBiome = (Colouring == EWorldseedTerrainColouring::BiomeColour)
			&& (Biomes.Index.Num() == Geometry.CellCount());

		if (UMaterialInterface* Material = ChooseTerrainMaterial(Mode))
		{
			VoxelTerrain->TerrainMaterial = Material;
		}

		VoxelTerrain->FinishSpawning(GetActorTransform());
	}

	if (!VoxelTerrain)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] terrain : le mailleur voxel n'a pas pu etre pose, "
				 "retour a la carte d'altitude"));
		bUseVoxelMesher = false;
		UpdateChunks();
		return;
	}

	VoxelTerrain->SetActorLabel(TEXT("Worldseed_Voxel"));
	UE_LOG(LogTemp, Log, TEXT("[Worldseed] terrain : le relief proche passe au VOXEL"));
}

FVector AWorldseedTerrain::GetStreamingOrigin() const
{
	if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		return Pawn->GetActorLocation();
	}
	return GetActorLocation();
}

int32 AWorldseedTerrain::StrideForDistance(float DistanceM) const
{
	int32 Stride = 1;
	for (const float Threshold : LodDistancesM)
	{
		if (DistanceM > Threshold)
		{
			Stride *= 2;
		}
	}
	// Au-dela du cote du chunk, le maillage n'aurait plus de quoi former un quad.
	return FMath::Clamp(Stride, 1, FMath::Max(ChunkCells / 2, 1));
}

FVector2D AWorldseedTerrain::ChunkCenterCm(const FIntPoint& Key) const
{
	const float CellCm = Geometry.MetersPerPixel() * WorldseedMetersToCm;
	const float OriginX = -Geometry.WidthM() * WorldseedMetersToCm * 0.5f;
	const float OriginY = -Geometry.HeightM * WorldseedMetersToCm * 0.5f;
	const int32 Cells = FMath::Max(ChunkCells, 16);

	return FVector2D(
		OriginX + (Key.X * Cells + Cells * 0.5f) * CellCm,
		OriginY + (Key.Y * Cells + Cells * 0.5f) * CellCm);
}

void AWorldseedTerrain::UpdateChunks()
{
	if (Geometry.NX < 2 || HeightsM.Num() != Geometry.CellCount())
	{
		return;
	}

	FeedSky(FMath::Max(UpdatePeriod, 0.05f));

	const FVector Origin = GetStreamingOrigin();
	const FVector2D OriginXY(Origin.X, Origin.Y);

	// --- ce qui doit disparaitre -------------------------------------------
	// L'hysteresis entre les deux rayons evite qu'un pas en avant puis en
	// arriere sur la frontiere fasse construire et detruire en boucle.
	TArray<FIntPoint> ToRelease;
	for (const TPair<FIntPoint, FWorldseedChunk>& Pair : Chunks)
	{
		const float DistM = FVector2D::Distance(OriginXY, ChunkCenterCm(Pair.Key)) / WorldseedMetersToCm;
		if (DistM > UnloadRadiusM)
		{
			ToRelease.Add(Pair.Key);
		}
	}
	for (const FIntPoint& Key : ToRelease)
	{
		ReleaseChunk(Key);
	}

	// --- ce qui doit exister ------------------------------------------------
	struct FCandidate { FIntPoint Key; int32 Stride; float DistM; };
	TArray<FCandidate> Candidates;

	for (int32 CY = 0; CY < ChunksY; ++CY)
	{
		for (int32 CX = 0; CX < ChunksX; ++CX)
		{
			const FIntPoint Key(CX, CY);
			const float DistM = FVector2D::Distance(OriginXY, ChunkCenterCm(Key)) / WorldseedMetersToCm;
			if (DistM > LoadRadiusM)
			{
				continue;
			}

			const int32 Stride = StrideForDistance(DistM);
			const FWorldseedChunk* Existing = Chunks.Find(Key);
			if (!Existing || Existing->Stride != Stride)
			{
				Candidates.Add({ Key, Stride, DistM });
			}
		}
	}

	// Les plus proches d'abord : le joueur voit d'abord ce qui l'entoure.
	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.DistM < B.DistM; });

	const int32 Budget = FMath::Max(ChunkBuildBudget, 1);
	const int32 Built = FMath::Min(Candidates.Num(), Budget);
	for (int32 I = 0; I < Built; ++I)
	{
		BuildChunk(Candidates[I].Key, Candidates[I].Stride);
	}

	// ON NE REDEMANDE PLUS LA TEXTURE D'INFORMATION A CHAQUE CHUNK.
	//
	// C'etait la cause du clignotement : l'eau disparaissait le temps du
	// redessin, une fois par chunk traverse. Deux changements l'ont rendue
	// inutile — le sol de fond donne desormais un sol sur toute la carte des la
	// generation, et la zone d'eau est passee en fenetre glissante, que le
	// MOTEUR regenere lui-meme quand la camera avance.
}

void AWorldseedTerrain::ReleaseChunk(const FIntPoint& Key)
{
	if (FWorldseedChunk* Chunk = Chunks.Find(Key))
	{
		if (Chunk->Mesh)
		{
			Chunk->Mesh->DestroyComponent();
		}
		Chunks.Remove(Key);
	}
}

void AWorldseedTerrain::BuildChunk(const FIntPoint& Key, int32 Stride)
{
	const int32 Cells = FMath::Max(ChunkCells, 16);
	const int32 StartI = Key.X * Cells;
	const int32 StartJ = Key.Y * Cells;

	// Un sommet de plus sur chaque bord : les chunks voisins partagent ainsi
	// leur rangee frontiere quand ils sont au meme niveau de detail.
	const int32 CountX = Cells / Stride + 1;
	const int32 CountY = Cells / Stride + 1;
	if (CountX < 2 || CountY < 2)
	{
		return;
	}

	const float CellCm = Geometry.MetersPerPixel() * WorldseedMetersToCm;
	const float StepCm = CellCm * Stride;
	const float OriginX = -Geometry.WidthM() * WorldseedMetersToCm * 0.5f;
	const float OriginY = -Geometry.HeightM * WorldseedMetersToCm * 0.5f;

	// X s'enroule : la carte fait le tour de la sphere, le chunk du bord est
	// est voisin de celui du bord ouest. Y se borne, un pole n ayant pas de
	// voisin au-dela.
	auto CellIndex = [this](int32 SX, int32 SY) -> int32
	{
		const int32 CX = ((SX % Geometry.NX) + Geometry.NX) % Geometry.NX;
		const int32 CY = FMath::Clamp(SY, 0, Geometry.NY - 1);
		return CY * Geometry.NX + CX;
	};

	auto SampleM = [this, &CellIndex](int32 SX, int32 SY) -> float
	{
		return HeightsM[CellIndex(SX, SY)];
	};

	// --- couches, decidees ICI et non dans le shader -----------------------
	//
	// Le materiau ne connait ni le climat ni la carte : il ne verrait que la
	// position et la normale. Or un desert et une prairie peuvent partager
	// exactement la meme altitude et la meme pente — seule la pluie les
	// separe. On calcule donc les poids au sommet, ou toutes les donnees sont
	// disponibles, et on les transporte en COULEUR DE SOMMET :
	//
	//     R = roche      G = vegetation      B = sable      A = neige
	//
	// Le shader n a plus qu a melanger quatre teintes, ce qui le garde lisible
	// et permet de lui substituer des textures sans rien recalculer.
	// Le mode biome demande une carte de biomes : sans elle on retombe sur les
	// poids de couches plutot que de peindre tout en noir.
	const bool bColourByBiome =
		(Colouring == EWorldseedTerrainColouring::BiomeColour)
		&& (Biomes.Index.Num() == Geometry.CellCount());

	const bool bHasCover = (Biomes.Cover.Num() == Geometry.CellCount());

	// Le mode pack demande a la fois une carte de biomes et un materiau : sans
	// l'un ou l'autre on retombe sur la couleur de biome, jamais sur du noir.
	const TObjectPtr<UMaterialInterface>* PackMaterial = PackMaterials.Find(TexturePack);
	const bool bTexturePack =
		(Colouring == EWorldseedTerrainColouring::TexturePack)
		&& (TexturePack != EWorldseedTexturePack::BiomeColour)
		&& (Biomes.Index.Num() == Geometry.CellCount())
		&& PackMaterial && PackMaterial->Get();

	const bool bHasClimate = (TempC.Num() == Geometry.CellCount())
		&& (PrecipMm.Num() == Geometry.CellCount());

	const float CosRockStart = FMath::Cos(FMath::DegreesToRadians(RockSlopeStartDeg));
	const float CosRockFull = FMath::Cos(FMath::DegreesToRadians(RockSlopeFullDeg));

	const int32 GridVerts = CountX * CountY;
	const int32 SkirtVerts = (SkirtDepthM > 0.0f) ? 2 * (CountX + CountY) : 0;

	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;

	// Canaux supplementaires : la teinte du biome, que RGBA ne peut plus porter
	// des lors qu'il transporte les poids de matiere.
	TArray<FVector2D> TintRG;
	TArray<FVector2D> TintB;

	TArray<FProcMeshTangent> Tangents;
	TArray<FLinearColor> Colors;
	TArray<int32> Triangles;

	Vertices.Reserve(GridVerts + SkirtVerts);
	Normals.Reserve(GridVerts + SkirtVerts);
	UVs.Reserve(GridVerts + SkirtVerts);
	Tangents.Reserve(GridVerts + SkirtVerts);
	Triangles.Reserve((CountX - 1) * (CountY - 1) * 6 + SkirtVerts * 3);

	Vertices.SetNumUninitialized(GridVerts);
	Normals.SetNumUninitialized(GridVerts);
	UVs.SetNumUninitialized(GridVerts);
	TintRG.SetNumUninitialized(GridVerts);
	TintB.SetNumUninitialized(GridVerts);
	Tangents.SetNumUninitialized(GridVerts);
	Colors.SetNumUninitialized(GridVerts);

	for (int32 Y = 0; Y < CountY; ++Y)
	{
		const int32 SY = StartJ + Y * Stride;
		for (int32 X = 0; X < CountX; ++X)
		{
			const int32 SX = StartI + X * Stride;
			const int32 Index = Y * CountX + X;

			Vertices[Index] = FVector(
				OriginX + SX * CellCm,
				OriginY + SY * CellCm,
				SampleM(SX, SY) * WorldseedMetersToCm * HeightExaggeration);

			UVs[Index] = FVector2D(
				static_cast<float>(SX) / static_cast<float>(Geometry.NX),
				static_cast<float>(SY) / static_cast<float>(Geometry.NY));

			// Normales par differences centrees, au PAS DU CHUNK : les prendre a
			// la resolution pleine ferait apparaitre un eclairage different entre
			// deux niveaux de detail voisins, plus visible que la fissure.
			const float HL = SampleM(SX - Stride, SY);
			const float HR = SampleM(SX + Stride, SY);
			const float HD = SampleM(SX, SY - Stride);
			const float HU = SampleM(SX, SY + Stride);

			const float DZDX = (HR - HL) * WorldseedMetersToCm * HeightExaggeration / (2.0f * StepCm);
			const float DZDY = (HU - HD) * WorldseedMetersToCm * HeightExaggeration / (2.0f * StepCm);

			const FVector N = FVector(-DZDX, -DZDY, 1.0f).GetSafeNormal();
			Normals[Index] = N;
			Tangents[Index] = FProcMeshTangent(
				FVector(1.0f, 0.0f, DZDX).GetSafeNormal(), false);

			FWorldseedAppearance Mode;
			Mode.bTexturePack = bTexturePack;
			Mode.bColourByBiome = bColourByBiome;
			Mode.bHasClimate = bHasClimate;
			Mode.bHasCover = bHasCover;

			ComputeVertexAppearance(CellIndex(SX, SY), SampleM(SX, SY), N, Mode,
				Colors[Index], TintRG[Index], TintB[Index]);
		}
	}

	for (int32 Y = 0; Y < CountY - 1; ++Y)
	{
		for (int32 X = 0; X < CountX - 1; ++X)
		{
			const int32 I = Y * CountX + X;
			if (bFlipWinding)
			{
				Triangles.Add(I); Triangles.Add(I + 1);          Triangles.Add(I + CountX + 1);
				Triangles.Add(I); Triangles.Add(I + CountX + 1); Triangles.Add(I + CountX);
			}
			else
			{
				Triangles.Add(I); Triangles.Add(I + CountX);     Triangles.Add(I + CountX + 1);
				Triangles.Add(I); Triangles.Add(I + CountX + 1); Triangles.Add(I + 1);
			}
		}
	}

	// --- jupe de bordure ----------------------------------------------------
	// Deux chunks de niveaux differents ne partagent pas leurs sommets de bord :
	// une fissure apparait, et on voit le ciel au travers. Une jupe verticale la
	// bouche sans avoir a raccorder les maillages entre eux.
	if (SkirtDepthM > 0.0f)
	{
		const float Drop = SkirtDepthM * WorldseedMetersToCm;

		auto AddSkirt = [&](int32 EdgeIndex)
		{
			// ON COPIE D ABORD, ON AJOUTE ENSUITE. Passer Array[i] a Array.Add()
			// remet a la fonction une reference INTERNE au tableau qu elle
			// modifie : une reallocation la ferait pendre en pleine copie. UE le
			// verifie systematiquement, meme quand la capacite suffirait.
			const FVector EdgeVertex = Vertices[EdgeIndex];
			const FVector EdgeNormal = Normals[EdgeIndex];
			const FVector2D EdgeUV = UVs[EdgeIndex];
			const FVector2D EdgeTintRG = TintRG[EdgeIndex];
			const FVector2D EdgeTintB = TintB[EdgeIndex];
			const FProcMeshTangent EdgeTangent = Tangents[EdgeIndex];
			const FLinearColor EdgeColor = Colors[EdgeIndex];

			const int32 New = Vertices.Num();
			Vertices.Add(EdgeVertex - FVector(0.0f, 0.0f, Drop));
			Normals.Add(EdgeNormal);
			UVs.Add(EdgeUV);
			TintRG.Add(EdgeTintRG);
			TintB.Add(EdgeTintB);
			Tangents.Add(EdgeTangent);
			Colors.Add(EdgeColor);
			return New;
		};

		auto Quad = [&](int32 A, int32 B, int32 LowA, int32 LowB)
		{
			if (bFlipWinding)
			{
				Triangles.Add(A); Triangles.Add(LowA); Triangles.Add(LowB);
				Triangles.Add(A); Triangles.Add(LowB); Triangles.Add(B);
			}
			else
			{
				Triangles.Add(A); Triangles.Add(LowB); Triangles.Add(LowA);
				Triangles.Add(A); Triangles.Add(B);    Triangles.Add(LowB);
			}
		};

		// Bord sud puis nord.
		for (int32 X = 0; X < CountX - 1; ++X)
		{
			const int32 A = X;
			const int32 B = X + 1;
			Quad(A, B, AddSkirt(A), AddSkirt(B));

			const int32 C = (CountY - 1) * CountX + X;
			const int32 D = C + 1;
			Quad(D, C, AddSkirt(D), AddSkirt(C));
		}
		// Bord ouest puis est.
		for (int32 Y = 0; Y < CountY - 1; ++Y)
		{
			const int32 A = Y * CountX;
			const int32 B = (Y + 1) * CountX;
			Quad(B, A, AddSkirt(B), AddSkirt(A));

			const int32 C = Y * CountX + (CountX - 1);
			const int32 D = (Y + 1) * CountX + (CountX - 1);
			Quad(C, D, AddSkirt(C), AddSkirt(D));
		}
	}

	// --- composant ----------------------------------------------------------
	FWorldseedChunk& Chunk = Chunks.FindOrAdd(Key);
	if (!Chunk.Mesh)
	{
		Chunk.Mesh = NewObject<UProceduralMeshComponent>(this,
			*FString::Printf(TEXT("Chunk_%d_%d"), Key.X, Key.Y));
		Chunk.Mesh->SetupAttachment(RootScene);
		Chunk.Mesh->bUseAsyncCooking = false;
		Chunk.Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

		// --- ombres ---------------------------------------------------------
		// LE RELIEF LOINTAIN DOIT PORTER SON OMBRE. Sans bCastFarShadow, une
		// primitive est ecartee des cascades lointaines du soleil : sur un
		// monde de 16 km, les massifs de l'horizon n'assombrissent plus rien
		// et le paysage s'aplatit des que le soleil rase. C'est le reglage qui
		// se voit le plus une fois UDS en place.
		Chunk.Mesh->SetCastShadow(true);
		Chunk.Mesh->bCastFarShadow = true;

		// Un maillage procedural n'a PAS de champ de distance : celui-ci se
		// construit a la cuisson, et rien ne le calcule au runtime. L'occlusion
		// ambiante et les ombres par champ de distance ne peuvent donc pas
		// prendre le terrain en compte ; le declarer ferait porter le cout sans
		// le benefice. Les ombres viennent des Virtual Shadow Maps, qui
		// travaillent, elles, sur la geometrie reelle.
		Chunk.Mesh->bAffectDistanceFieldLighting = false;

		Chunk.Mesh->RegisterComponent();
	}

	Chunk.Stride = Stride;

	// La collision ne sert que de pres : la cuire pour les chunks lointains
	// couterait plus cher que tout le reste du maillage.
	const bool bCollide = bCreateCollision && (Stride == 1);

	// Releve sur le premier chunk seulement : de quoi savoir si le probleme est
	// dans le calcul des poids ou dans leur transport jusqu au shader.
	if (Chunks.Num() <= 1)
	{
		FLinearColor Sum(0.0f, 0.0f, 0.0f, 0.0f);
		FLinearColor Peak(0.0f, 0.0f, 0.0f, 0.0f);
		for (const FLinearColor& C : Colors)
		{
			Sum += C;
			Peak.R = FMath::Max(Peak.R, C.R);
			Peak.G = FMath::Max(Peak.G, C.G);
			Peak.B = FMath::Max(Peak.B, C.B);
			Peak.A = FMath::Max(Peak.A, C.A);
		}
		const float Inv = 1.0f / FMath::Max(Colors.Num(), 1);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] couches chunk %d,%d : climat=%d  %d couleurs pour %d sommets"),
			Key.X, Key.Y, bHasClimate ? 1 : 0, Colors.Num(), Vertices.Num());
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   moyennes  roche %.2f  vegetation %.2f  sable %.2f  neige %.2f"),
			Sum.R * Inv, Sum.G * Inv, Sum.B * Inv, Sum.A * Inv);
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   maxima    roche %.2f  vegetation %.2f  sable %.2f  neige %.2f"),
			Peak.R, Peak.G, Peak.B, Peak.A);
	}

	Chunk.Mesh->ClearAllMeshSections();

	// UV1 et UV2 transportent la teinte du biome. Ils restent a zero hors du
	// mode pack : le materiau ne les lit pas dans les autres modes.
	// LA REPETITION DU SOL NE PASSE PAS PAR UN CANAL UV : le materiau la tire de
	// la position monde. Un canal de sommet n'en porterait pas la valeur — le
	// maillage procedural stocke ses UV en demi-precision, ou le pas atteint
	// seize metres a l'echelle d'une carte de seize kilometres.
	static const TArray<FVector2D> NoUV;
	Chunk.Mesh->CreateMeshSection_LinearColor(
		0, Vertices, Triangles, Normals, UVs, TintRG, TintB, NoUV,
		Colors, Tangents, bCollide);

	FWorldseedAppearance MaterialMode;
	MaterialMode.bTexturePack = bTexturePack;
	MaterialMode.bColourByBiome = bColourByBiome;
	UMaterialInterface* Material = ChooseTerrainMaterial(MaterialMode);
	if (Material)
	{
		Chunk.Mesh->SetMaterial(0, Material);
	}

	// Les canaux de teinte doivent avoir EXACTEMENT autant d'entrees que de
	// sommets : ProceduralMesh rejette en silence un canal UV de la mauvaise
	// taille, et le materiau recoit alors des zeros — donc une teinte noire.
	if (Chunks.Num() <= 1 && bTexturePack)
	{
		FLinearColor SumW(0.0f, 0.0f, 0.0f, 0.0f);
		FVector2D SumRG = FVector2D::ZeroVector;
		float SumB = 0.0f;
		for (int32 I = 0; I < Vertices.Num(); ++I)
		{
			SumW += Colors[I];
			SumRG += TintRG[I];
			SumB += static_cast<float>(TintB[I].X);
		}
		const float Inv = 1.0f / FMath::Max(Vertices.Num(), 1);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] canaux : %d sommets, %d couleurs, %d TintRG, %d TintB"),
			Vertices.Num(), Colors.Num(), TintRG.Num(), TintB.Num());
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   poids moyens  herbe %.2f  aride %.2f  roche %.2f  mousse %.2f"),
			SumW.R * Inv, SumW.G * Inv, SumW.B * Inv, SumW.A * Inv);
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   teinte moyenne  R %.2f  G %.2f  B %.2f"),
			SumRG.X * Inv, SumRG.Y * Inv, SumB * Inv);
	}

	// UN MATERIAU ABSENT NE SE VOIT PAS COMME UNE ERREUR : le maillage tombe sur
	// le WorldGridMaterial du moteur, un damier gris qu'on prend facilement pour
	// une texture mal reglee. On dit donc a voix haute ce qui a ete pose.
	if (Chunks.Num() <= 1)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] habillage : mode=%s  pack=%s  materiau=%s"),
			bTexturePack ? TEXT("pack") : (bColourByBiome ? TEXT("biome") : TEXT("couches")),
			*UEnum::GetDisplayValueAsText(TexturePack).ToString(),
			Material ? *Material->GetName() : TEXT("AUCUN (damier du moteur)"));
	}
}

void AWorldseedTerrain::FeedSky(float DeltaSeconds)
{
	if (!SkyDriver || Geometry.NX < 2)
	{
		return;
	}

	const FVector Origin = GetStreamingOrigin();

	FWorldseedClimateSample Sample;
	if (!SampleClimateAtWorldXY(Origin.X, Origin.Y, Sample))
	{
		// Un monde sans climat n'a pas de meteo a suivre.
		return;
	}

	float LongitudeDeg = 0.0f;
	float LatitudeDeg = 0.0f;
	GetLonLatAtWorldXY(Origin.X, Origin.Y, LongitudeDeg, LatitudeDeg);
	Sample.LatitudeDeg = LatitudeDeg;

	// L'altitude du joueur, en metres depuis le niveau de la mer.
	const float AltitudeM =
		static_cast<float>(Origin.Z - GetActorLocation().Z) / WorldseedMetersToCm
		/ FMath::Max(HeightExaggeration, KINDA_SMALL_NUMBER);

	SkyDriver->Drive(Sample, LongitudeDeg, AltitudeM, Geometry.LatSpanDeg,
		WorldSeed, DeltaSeconds);
}

bool AWorldseedTerrain::SampleClimateAtWorldXY(float WorldX, float WorldY,
	FWorldseedClimateSample& OutSample) const
{

	const int32 Count = Geometry.CellCount();
	if (Geometry.NX < 2 || TempC.Num() != Count || PrecipMm.Num() != Count)
	{
		return false;
	}

	const FVector Local = GetActorTransform().InverseTransformPosition(
		FVector(WorldX, WorldY, 0.0f));

	const float WidthCm = Geometry.WidthM() * WorldseedMetersToCm;
	const float HeightCm = Geometry.HeightM * WorldseedMetersToCm;

	float U = static_cast<float>(Local.X) / WidthCm + 0.5f;
	U -= FMath::FloorToFloat(U);
	const float V = FMath::Clamp(static_cast<float>(Local.Y) / HeightCm + 0.5f, 0.0f, 1.0f);

	OutSample.TempMeanC = WorldseedGrid::SampleUV(TempC, Geometry.NX, Geometry.NY, U, V);
	OutSample.PrecipMm = WorldseedGrid::SampleUV(PrecipMm, Geometry.NX, Geometry.NY, U, V);

	// Amplitude et continentalite arrivent du modele climatique. Un monde
	// d'avant leur transport n'en porte pas : on garde alors les valeurs par
	// defaut de l'echantillon plutot que d'inventer des saisons.
	if (SeasonalAmpC.Num() == Count)
	{
		OutSample.SeasonalAmpC = WorldseedGrid::SampleUV(
			SeasonalAmpC, Geometry.NX, Geometry.NY, U, V);
	}
	if (ContinentalityGrid.Num() == Count)
	{
		OutSample.Continentality = WorldseedGrid::SampleUV(
			ContinentalityGrid, Geometry.NX, Geometry.NY, U, V);
	}
	return true;
}

void AWorldseedTerrain::CielDInspection()
{
	// --- UN CIEL QUI NE BOUGE PAS, POUR POUVOIR COMPARER -------------------
	//
	// SIGNALE : « trop de nuage pour confirmer, il faudrait faire des tests
	// sans couverture nuageuse ». C'est juste, et cela repare DEUX defauts de
	// methode a la fois.
	//
	// LES NUAGES CACHENT CE QU'ON VIENT REGARDER. La couture de l'eau se lit
	// a la jonction de deux teintes ; un banc de nuages a hauteur d'oeil la
	// coupe en morceaux et l'on conclut « aucune couture » faute de la voir.
	//
	// ET L'HORLOGE D'UDS REND TOUT A/B PAR LANCEMENTS SUCCESSIFS INUTILISABLE.
	// `Animate Time of Day` tourne : trente secondes d'ecart au chargement
	// font douze minutes de jeu. Mesure prise aujourd'hui sur un temoin hors
	// d'atteinte du traitement -- une crete rocheuse au-dessus du niveau de la
	// mer -- **101 sur 765** de derive entre les deux moities, quand la zone
	// testee bougeait de 15. Le bruit valait sept fois le signal, et trois
	// A/B de la journee sont partis a la poubelle pour cette seule raison.
	//
	// ON NE TOUCHE RIEN EN JEU NORMAL : tout est derriere un drapeau, et
	// l'absence de drapeau laisse le ciel exactement comme avant.
	if (!FParse::Param(FCommandLine::Get(), TEXT("WorldseedCielClair")))
	{
		return;
	}

	// LES NUAGES SE COUPENT PAR UNE VARIABLE DE CONSOLE DU MOTEUR, pas en
	// pilotant UDS. `r.VolumetricCloud` est un interrupteur du RENDU : il ne
	// touche ni a la meteo, ni a l'etat du ciel, ni au climat, donc il ne peut
	// rien deregler qu'on aurait ensuite a remettre. Piloter la couverture
	// nuageuse d'UDS demanderait d'atteindre un Blueprint par reflexion, et ce
	// depot a deja fait tomber l'editeur en ecrivant dans ses collections de
	// parametres.
	if (IConsoleVariable* const Nuages = IConsoleManager::Get()
			.FindConsoleVariable(TEXT("r.VolumetricCloud")))
	{
		Nuages->Set(0, ECVF_SetByCode);
	}

	// L'HORLOGE : on la fige par reflexion sur l'acteur d'UDS.
	//
	// LE NOM DE LA VARIABLE PORTE DES ESPACES -- c'est une variable Blueprint,
	// et le depot a deja paye de les deviner (`AsUltra Dynamic Sky`). On
	// cherche donc les deux ecritures et l'on JOURNALISE celle qui a pris :
	// une reflexion qui echoue est silencieuse, et l'on croirait le ciel fige
	// alors qu'il continue de tourner.
	int32 Figes = 0;
	if (UWorld* const World = GetWorld())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* const Acteur = *It;
			if (!Acteur || !Acteur->GetClass()->GetName().Contains(TEXT("Ultra_Dynamic_Sky")))
			{
				continue;
			}

			for (const TCHAR* Nom : { TEXT("Animate Time of Day"), TEXT("AnimateTimeOfDay") })
			{
				if (FBoolProperty* const Prop = CastField<FBoolProperty>(
						Acteur->GetClass()->FindPropertyByName(FName(Nom))))
				{
					Prop->SetPropertyValue_InContainer(Acteur, false);
					++Figes;
				}
			}
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[Worldseed] ciel d'inspection : nuages volumetriques coupes, ")
		TEXT("horloge figee sur %d acteur(s) UDS"),
		Figes);

	if (Figes == 0)
	{
		// ON LE DIT PLUTOT QUE DE LAISSER CROIRE. Sans horloge figee, deux
		// lancements n'ont pas la meme lumiere et l'A/B ne vaut rien.
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] ciel d'inspection : AUCUNE horloge figee -- ")
			TEXT("la lumiere derivera entre deux lancements, tout A/B est a lire avec ")
			TEXT("un temoin hors traitement"));
	}
}

UMaterialInterface* AWorldseedTerrain::ChoisirMateriauMerDecor(
	UMaterialInterface* Repli) const
{
	// UN MATERIAU MAISON POURRAIT VIVRE DANS LE DEPOT, contrairement a ce que
	// j'ai d'abord ecrit ici. `.gitignore` exclut `Content/*` mais ROUVRE
	// `!Content/Worldseed/` : soixante-dix-sept fichiers y sont versionnes,
	// dont `Content/Worldseed/Materials/`. C'est d'ailleurs la que vivent
	// `BP_WorldseedClimat` et `CAL_Worldseed`, et le registre le dit.
	// **Ce qui n'est PAS versionne, c'est `Content/__ExternalActors__/`** --
	// d'ou les reglages perdus (PlayerStart, acteurs d'eclairage, les 40 km de
	// nappe lointaine) : ils etaient poses sur des ACTEURS de niveau, pas dans
	// un asset.
	//
	// On prend quand meme celui du PLUGIN Water pour commencer : il existe,
	// il est fait pour ce role exact, et il n'exige pas d'ouvrir l'editeur.
	// Un materiau maison sous `Content/Worldseed/Materials/` reste la bonne
	// reponse le jour ou celui-ci ne suffira pas.
	//
	// CE QU'ON LUI DEMANDE N'EST PAS DE LA VAGUE. A six kilometres et au-dela,
	// vu de 930 m, l'angle d'incidence vaut huit degres : une vraie mer y est
	// un MIROIR, elle prend la couleur du ciel. Ce qui manque au decor n'est
	// donc pas du relief de surface mais une reponse speculaire et un fresnel.
	//
	// ET SI CE MATERIAU NE CONVIENT PAS, LE JOURNAL LE DIRA. Un materiau du
	// domaine de l'eau peut refuser de se compiler pour le facteur de sommets
	// d'un maillage procedural ; le moteur retombe alors sur son materiau par
	// defaut, en gris, sans erreur bloquante. On journalise donc ce qui est
	// REELLEMENT pose, et `-WorldseedMerMateriau=0` rend le materiau de
	// terrain pour comparer sur le meme binaire.
	int32 Actif = 1;
	if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedMerMateriau="), Actif)
		&& Actif == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] sol de fond : materiau de mer COUPE par la ligne de commande"));
		return Repli;
	}

	if (HorizonSeaMaterial)
	{
		return HorizonSeaMaterial.Get();
	}

	if (UMaterialInterface* const Lointain = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Water/Materials/WaterSurface/Water_FarMesh.Water_FarMesh")))
	{
		return Lointain;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[Worldseed] sol de fond : aucun materiau de mer, on garde celui du terrain"));
	return Repli;
}

UMaterialInterface* AWorldseedTerrain::ChooseTerrainMaterial(
	const FWorldseedAppearance& Mode) const
{
	// CHAQUE MODE A SON MATERIAU, et ils ne sont pas interchangeables : l'un lit
	// des poids de matiere, l'autre des poids de couches, le troisieme une
	// couleur. Poser le mauvais donnerait un terrain absurde plutot qu'une
	// simple difference de rendu.
	if (Mode.bTexturePack)
	{
		if (const TObjectPtr<UMaterialInterface>* Found = PackMaterials.Find(TexturePack))
		{
			if (Found->Get())
			{
				return Found->Get();
			}
		}
	}

	if (Mode.bColourByBiome)
	{
		return BiomeMaterial ? BiomeMaterial.Get() : TerrainMaterial.Get();
	}

	return TerrainMaterial ? TerrainMaterial.Get() : BiomeMaterial.Get();
}

// LE CONTROLE QUI TRANCHE EST DE MASQUER LA NAPPE ET DE RECAPTURER, et le
// depot le dit deja en toutes lettres : elle n-a pas de collision, donc les
// sondages la traversent et ne peuvent pas la voir. On mesure du sol la ou
// l-on voit un trou, et inversement. `-WorldseedSansNappe` la supprime pour
// une session, le temps d-un A/B a l-image.
void AWorldseedTerrain::BuildGroundProxy()
{
	WORLDSEED_TRACE(SolDeFond);

	if (FParse::Param(FCommandLine::Get(), TEXT("WorldseedSansNappe")))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] sol de fond : SUPPRIME par -WorldseedSansNappe ")
			TEXT("-- l-horizon sera vide, et l-ocean risque de ne plus se dessiner"));
		return;
	}

	if (!bBuildGroundProxy || Geometry.NX < 2 || HeightsM.Num() != Geometry.CellCount())
	{
		return;
	}

	const double StartTime = FPlatformTime::Seconds();

	// Jamais plus fin que la grille elle-meme : au-dela on interpolerait du
	// detail qui n'existe pas.
	const int32 CountX = FMath::Clamp(GroundProxyWidth, 32, Geometry.NX);
	// La hauteur suit la FORME DE LA GRILLE, et non un demi arbitraire : le
	// monde n'est pas toujours deux fois plus large que haut, et un maillage
	// qui ne respecterait pas son rapport donnerait des mailles etirees.
	const int32 CountY = FMath::Clamp(
		FMath::RoundToInt(CountX * static_cast<float>(Geometry.NY)
			/ static_cast<float>(FMath::Max(Geometry.NX, 1))),
		16, Geometry.NY);

	const float CellCm = Geometry.MetersPerPixel() * WorldseedMetersToCm;
	const float OriginX = -Geometry.WidthM() * WorldseedMetersToCm * 0.5f;
	const float OriginY = -Geometry.HeightM * WorldseedMetersToCm * 0.5f;
	// --- LE RETRAIT SUIT LE CHAMP, PAS UN NOMBRE EN DUR --------------------
	//
	// LA PREMISSE DU REGLAGE ETAIT FAUSSE. Il valait UN METRE, avec ce motif :
	// « les deux maillages decrivent le meme relief, sans ce retrait ils se
	// disputeraient le meme plan et scintilleraient ». Or ils ne decrivent PAS
	// le meme relief : la nappe est batie sur la grille macro, le terrain voxel
	// maille l'isovaleur zero du CHAMP, qui ajoute a cette grille un
	// deplacement vertical 3D -- jusqu'a overhangAmplitudeM plus
	// detailAmplitudeM vers le bas.
	//
	// MESURE, graine 20260909, 10 637 colonnes de terre : la nappe flotte
	// AU-DESSUS du sol reel sur 38,8 % d'entre elles. Ecart moyen 0,44 m,
	// mediane 0,50, p90 2,50, p99 7,00. Un metre couvrait donc la mediane et
	// rien de plus, et le joueur -- qui marche sur le voxel et traverse la
	// nappe, laquelle est dessinee mais sans collision -- paraissait enfonce
	// dedans jusqu'a la taille. Signale sur capture par le proprietaire.
	//
	// C'est exactement l'angle mort que `archeMargeSommetM` corrige deja
	// ailleurs : « le champ de densite deplace la surface et la passe des
	// cavites ne le sait pas ». La nappe avait la meme cecite.
	//
	// On lit donc les deux amplitudes plutot que d'ecrire douze : un reglage
	// qui change ne doit pas laisser ce retrait en arriere.
	double RetraitM = GroundProxyDropM;
	{
		FString RulesError;
		if (const UWorldseedRules* R = WorldseedPipeline::GetRules(RulesError))
		{
			const FWorldseedDensityRules DR = FWorldseedDensityRules::FromRules(*R);
			RetraitM = FMath::Max<double>(RetraitM,
				DR.OverhangAmplitudeM + DR.DetailAmplitudeM);
		}
	}
	const float DropCm = RetraitM * WorldseedMetersToCm * HeightExaggeration;

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] sol de fond : retrait %.1f m (plancher du reglage %.1f, ")
		TEXT("deplacement du champ compris)"),
		RetraitM, GroundProxyDropM);

	const int32 Verts = CountX * CountY;

	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FVector2D> TintRG;
	TArray<FVector2D> TintB;
	TArray<FProcMeshTangent> Tangents;
	TArray<FLinearColor> Colors;
	TArray<int32> Triangles;

	Vertices.SetNumUninitialized(Verts);
	Normals.SetNumUninitialized(Verts);
	UVs.SetNumUninitialized(Verts);
	TintRG.SetNumUninitialized(Verts);
	TintB.SetNumUninitialized(Verts);
	Tangents.SetNumUninitialized(Verts);
	Colors.SetNumUninitialized(Verts);

	FWorldseedAppearance Mode;
	Mode.bTexturePack = (Colouring == EWorldseedTerrainColouring::TexturePack)
		&& (TexturePack != EWorldseedTexturePack::BiomeColour)
		&& (Biomes.Index.Num() == Geometry.CellCount());
	Mode.bColourByBiome = (Colouring == EWorldseedTerrainColouring::BiomeColour)
		&& (Biomes.Index.Num() == Geometry.CellCount());
	Mode.bHasClimate = (TempC.Num() == Geometry.CellCount())
		&& (PrecipMm.Num() == Geometry.CellCount());
	Mode.bHasCover = (Biomes.Cover.Num() == Geometry.CellCount());

	// LA MER DU DECOR EST OPAQUE, CELLE DES PIEDS NE L'EST PAS. Le seul
	// endroit du projet ou ce drapeau s'arme : voir son commentaire dans
	// `FWorldseedAppearance`.
	//
	// La surcharge existe pour l'A/B : la couture que ce drapeau masque ne se
	// voit qu'a fenetre d'eau ETROITE, donc les deux corrections se cachent
	// l'une l'autre et une capture « apres » ne prouverait rien toute seule.
	// A combiner avec `-WorldseedEauFenetre=4`.
	// 0 = coupee, 1 = couleur de mer, 2 = MAGENTA de controle.
	Mode.bMerOpaque = true;
	int32 MerOpaque = 1;
	if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedMerOpaque="), MerOpaque))
	{
		Mode.bMerOpaque = (MerOpaque != 0);
		Mode.bMerTemoin = (MerOpaque == 2);
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] sol de fond : mer opaque %s par la ligne de commande"),
			Mode.bMerTemoin ? TEXT("en MAGENTA de controle")
				: (Mode.bMerOpaque ? TEXT("ARMEE") : TEXT("COUPEE")));
	}

	// COMPTER CE QU'ON PEINT. Une couleur qui ne se voit pas a deux causes
	// opposees -- le terme ne s'evalue jamais, ou il s'evalue et rien ne
	// l'affiche -- et elles n'appellent pas du tout le meme remede. Le compte
	// separe les deux avant meme de regarder l'image.
	int32 SommetsSousZero = 0;

	// LE MARQUAGE VOYAGE AVEC LE SOMMET, il ne se rededuit pas. La nappe vue
	// est ENFONCEE sous le niveau de la mer, donc son Z ne dit plus rien de
	// l'altitude reelle du terrain : relire « ce sommet est-il submerge » sur
	// la geometrie decimee donnerait « tout est submerge ». On retient donc le
	// verdict ici, ou `HereM` est l'altitude vraie.
	TArray<uint8> SousZero;
	SousZero.SetNumUninitialized(Verts);

	// LE PAS SE PREND EN REEL, ET LE DERNIER SOMMET TOMBE SUR LA DERNIERE
	// CELLULE.
	//
	// Un pas ENTIER tronque le sol de fond sans le dire : NX / CountX vaut 1
	// des que la grille fait moins du double de la largeur demandee, et le
	// maillage s'arrete alors a la colonne CountX au lieu de NX. A 512 sommets
	// sur une grille de 1000, il ne couvrait plus que la moitie de la carte —
	// le paysage se terminait par une arete parfaitement droite a vingt metres
	// du joueur, et le ciel commencait derriere.
	const float StepX = (CountX > 1)
		? static_cast<float>(Geometry.NX - 1) / static_cast<float>(CountX - 1) : 0.0f;
	const float StepY = (CountY > 1)
		? static_cast<float>(Geometry.NY - 1) / static_cast<float>(CountY - 1) : 0.0f;

	// Voisins pour la normale : a un pas du sol de fond, pas a une cellule.
	const int32 NeighbourX = FMath::Max(FMath::RoundToInt(StepX), 1);
	const int32 NeighbourY = FMath::Max(FMath::RoundToInt(StepY), 1);

	// LE SOL DE FOND DOIT PASSER SOUS LE RELIEF, PAS AU TRAVERS.
	//
	// Un sommet pris au POINT ne dit rien de ce qui se passe entre deux
	// sommets. Sur cette carte l'ecart entre eux atteint trente metres : le
	// maillage y tend une corde droite, qui monte au-dessus du terrain dans le
	// moindre creux. Les chunks detailles le traversent alors par en dessous,
	// et l'oeil voit deux reliefs superposes — exactement ce qu'on croit etre
	// un bug de maillage. L'enfoncement d'un metre n'y peut rien : il est deux
	// ordres de grandeur trop petit.
	//
	// On prend donc le MINIMUM sur l'empreinte du sommet. Une corde tendue
	// entre deux minima passe sous le relief au lieu de le couper, et
	// l'enfoncement ne sert plus que de marge.
	const int32 FootX = FMath::Max(NeighbourX / 2, 1);
	const int32 FootY = FMath::Max(NeighbourY / 2, 1);

	auto FloorAt = [&](int32 CX, int32 CY) -> float
	{
		const int32 X0 = FMath::Max(CX - FootX, 0);
		const int32 X1 = FMath::Min(CX + FootX, Geometry.NX - 1);
		const int32 Y0 = FMath::Max(CY - FootY, 0);
		const int32 Y1 = FMath::Min(CY + FootY, Geometry.NY - 1);

		float Lowest = TNumericLimits<float>::Max();
		for (int32 Row = Y0; Row <= Y1; ++Row)
		{
			const int32 Base = Row * Geometry.NX;
			for (int32 Col = X0; Col <= X1; ++Col)
			{
				Lowest = FMath::Min(Lowest, HeightsM[Base + Col]);
			}
		}
		return Lowest;
	};

	// De combien le point depassait-il son empreinte ? C'est la hauteur dont le
	// sol de fond sortait du relief, et elle n'avait aucune raison d'etre
	// visible autrement qu'a l'ecran.
	float WorstOvershootM = 0.0f;

	for (int32 Y = 0; Y < CountY; ++Y)
	{
		const int32 SY = FMath::Min(FMath::RoundToInt(Y * StepY), Geometry.NY - 1);
		for (int32 X = 0; X < CountX; ++X)
		{
			const int32 SX = FMath::Min(FMath::RoundToInt(X * StepX), Geometry.NX - 1);
			const int32 Index = Y * CountX + X;
			const int32 Cell = SY * Geometry.NX + SX;

			const float HereM = HeightsM[Cell];

			// La GEOMETRIE prend le plancher ; l'APPARENCE garde la hauteur du
			// point, qui est celle du biome reellement present ici.
			const float FloorM = FloorAt(SX, SY);
			WorstOvershootM = FMath::Max(WorstOvershootM, HereM - FloorM);

			Vertices[Index] = FVector(
				OriginX + SX * CellCm,
				OriginY + SY * CellCm,
				FloorM * WorldseedMetersToCm * HeightExaggeration - DropCm);


			// Normale au PAS DU SOL DE FOND, pas a celui de la grille : prise
			// sur des voisins immediats, elle porterait un detail que ce
			// maillage ne represente pas, et l'ombrage jurerait avec sa forme.
			const int32 PX = FMath::Min(SX + NeighbourX, Geometry.NX - 1);
			const int32 MX = FMath::Max(SX - NeighbourX, 0);
			const int32 PY = FMath::Min(SY + NeighbourY, Geometry.NY - 1);
			const int32 MY = FMath::Max(SY - NeighbourY, 0);

			const float SpanX = FMath::Max((PX - MX), 1) * Geometry.MetersPerPixel();
			const float SpanY = FMath::Max((PY - MY), 1) * Geometry.MetersPerPixel();

			const float DZDX =
				(HeightsM[SY * Geometry.NX + PX] - HeightsM[SY * Geometry.NX + MX]) / SpanX;
			const float DZDY =
				(HeightsM[PY * Geometry.NX + SX] - HeightsM[MY * Geometry.NX + SX]) / SpanY;

			const FVector N = FVector(-DZDX, -DZDY, 1.0f).GetSafeNormal();
			Normals[Index] = N;
			Tangents[Index] = FProcMeshTangent(
				FVector(1.0f, 0.0f, DZDX).GetSafeNormal(), false);

			UVs[Index] = FVector2D(
				static_cast<float>(SX) / static_cast<float>(Geometry.NX),
				static_cast<float>(SY) / static_cast<float>(Geometry.NY));

			ComputeVertexAppearance(Cell, HereM, N, Mode,
				Colors[Index], TintRG[Index], TintB[Index]);

			SousZero[Index] = (HereM < 0.0f) ? 1 : 0;
			if (HereM < 0.0f) { ++SommetsSousZero; }
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] sol de fond : %d sommets sur %d sous le niveau zero (%.1f %%)"),
		SommetsSousZero, Verts,
		Verts > 0 ? 100.0f * SommetsSousZero / Verts : 0.0f);

	Triangles.Reserve((CountX - 1) * (CountY - 1) * 6);
	for (int32 Y = 0; Y < CountY - 1; ++Y)
	{
		for (int32 X = 0; X < CountX - 1; ++X)
		{
			const int32 A = Y * CountX + X;
			const int32 C = A + CountX;

			if (bFlipWinding)
			{
				Triangles.Add(A); Triangles.Add(A + 1); Triangles.Add(C);
				Triangles.Add(A + 1); Triangles.Add(C + 1); Triangles.Add(C);
			}
			else
			{
				Triangles.Add(A); Triangles.Add(C); Triangles.Add(A + 1);
				Triangles.Add(A + 1); Triangles.Add(C); Triangles.Add(C + 1);
			}
		}
	}

	// L'ACTEUR DEDIE, ET NON UN COMPOSANT DE PLUS SUR LE TERRAIN.
	//
	// C'est lui qui porte le UWaterTerrainComponent, et il doit pour cela etre
	// le seul de son acteur — sans quoi chaque chunk du terrain fait redemander
	// au plugin Water la texture d'information du monde entier. Le pourquoi
	// tient dans WorldseedGroundProxy.h.
	if (!GroundProxy)
	{
		UWorld* const World = GetWorld();
		if (!World)
		{
			return;
		}

		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		GroundProxy = World->SpawnActor<AWorldseedGroundProxy>(
			AWorldseedGroundProxy::StaticClass(), GetActorTransform(), Params);
		if (GroundProxy)
		{
			// Les chunks appartiennent a CE terrain : voir DetailedTerrain.
			GroundProxy->SetDetailedTerrain(this);
		}
		if (!GroundProxy)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] sol de fond non pose : l'eau ne verra que les chunks"));
			return;
		}
	}

	UProceduralMeshComponent* const Mesh = GroundProxy->GetMesh();

	Mesh->ClearAllMeshSections();
	Mesh->CreateMeshSection_LinearColor(
		0, Vertices, Triangles, Normals, UVs, TintRG, TintB, TArray<FVector2D>(),
		Colors, Tangents, false);

	if (UMaterialInterface* Material = ChooseTerrainMaterial(Mode))
	{
		Mesh->SetMaterial(0, Material);
	}

	// LE DRAPEAU DE PROFONDEUR SE POSE ICI, UNE FOIS. Il vivait dans la
	// bascule, qui le reecrivait a l'identique a chaque passage -- et une
	// ecriture de drapeau de rendu, meme sans changement de valeur, est
	// exactement ce qu'on cherche a ne plus faire.
	//
	// LE PRINCIPAL SORT, LA PROFONDEUR RESTE, et la nuance est vitale : cette
	// nappe nourrit le plugin Water, qui la lit dans la passe de PROFONDEUR --
	// `ShouldRenderInDepthPass() = bRenderInMainPass || bRenderInDepthPass`
	// (PrimitiveSceneProxy.h:804). La masquer entierement la sortirait des
	// deux, et l'ocean cesserait de se dessiner sans le moindre avertissement.
	Mesh->SetRenderInDepthPass(true);

	// LA SECTION EST RENDUE VISIBLE EXPLICITEMENT. Un essai de bascule par
	// visibilite de section a ete fait puis annule (voir le commentaire de
	// UpdateGroundProxyVisibility) ; si l'etat etait reste a « cachee », une
	// nappe reconstruite plus tard le reprendrait sans que rien le dise.
	Mesh->SetMeshSectionVisible(0, true);

	// --- LE DECOR D'HORIZON SORT DU RAY TRACING -----------------------------
	//
	// SIGNALE EN JEU DES LE PASSAGE A 4096 : « RAY TRACING GEOMETRY REQUESTED
	// MEMORY OVER BUDGET 900 MB / 400 MB ». La cause est directe -- cette
	// nappe est UN SEUL maillage de plusieurs millions de sommets, et le
	// moteur lui batit une structure d'acceleration a la mesure.
	//
	// ET ELLE N'A RIEN A Y FAIRE. C'est un decor de FOND : on ne le voit qu'au
	// dela du rayon de chargement, le terrain voxel le couvre entierement en
	// deca, et il est deja retire du rendu principal des qu'on passe sous un
	// plafond. Le sortir des reflets et des ombres tracees ne se voit donc
	// nulle part, et rend au budget ce qui lui manquait.
	//
	// MEME FAMILLE QUE LE POOL DE TEXTURES : le moteur ne plante pas, il
	// DEGRADE en silence une fois le budget depasse -- et l'on cherche ensuite
	// la cause d'un rendu terne du mauvais cote.
	Mesh->SetVisibleInRayTracing(false);

	// --- CELUI QUE L'EAU LIT NE SE VOIT PLUS JAMAIS -------------------------
	//
	// POSE UNE FOIS, ICI, ET PLUS JAMAIS TOUCHE. C'est toute la raison d'etre
	// de la separation : ce drapeau est celui qui coute 520 ms quand on le
	// bascule, parce qu'il marque l'etat de rendu sale et fait recreer un
	// proxy de 8,4 millions de sommets. Ne jamais le basculer rend ce cout NUL
	// par construction, et la profondeur reste armee juste au-dessus, donc
	// l'ocean garde son sol.
	Mesh->SetRenderInMainPass(false);

	// --- LE SOL QU'ON VOIT, DECIME DEPUIS CELUI QUE L'EAU LIT ---------------
	//
	// ON DECIME AU LIEU DE RECALCULER : les sommets sont deja la, avec leur
	// normale, leur teinte et leur apparence de biome. Les refaire couterait
	// une seconde passe sur toute la grille pour un resultat identique au
	// sommet pres -- et surtout, deux codes de construction finiraient par
	// diverger, ce que ce depot a deja paye plus d'une fois.
	//
	// ON NE PEUT PAS LE DUPLIQUER A L'IDENTIQUE : un FProcMeshVertex pese de
	// l'ordre de cent cinquante octets, donc une nappe pleine depasse le
	// gigaoctet en copie processeur.
	// L'HORIZON NAIT SUR SON PROPRE ACTEUR, et il ne peut pas en etre
	// autrement : le plugin Water borne sa zone sur la boite de TOUS les
	// composants du sol de fond, et `GetTerrainPrimitives` enumere toutes les
	// primitives du terrain. Partout ailleurs, cette nappe enfoncee mentirait
	// a l'eau.
	if (!HorizonProxy)
	{
		if (UWorld* const World = GetWorld())
		{
			FActorSpawnParameters Params;
			Params.Owner = this;
			Params.SpawnCollisionHandlingOverride =
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			HorizonProxy = World->SpawnActor<AWorldseedHorizonProxy>(
				AWorldseedHorizonProxy::StaticClass(), GetActorTransform(), Params);
		}
	}

	if (UProceduralMeshComponent* const Vue =
		HorizonProxy ? HorizonProxy->GetMesh() : nullptr)
	{
		const int32 Pas = FMath::Clamp(GroundProxyHorizonStride, 1, 8);
		const int32 HX = (CountX - 1) / Pas + 1;
		const int32 HY = (CountY - 1) / Pas + 1;
		const int32 HVerts = HX * HY;

		// --- LE DECOR DOIT PASSER SOUS TOUT CE QUE LE VOXEL PEUT CREUSER ----
		//
		// L'ENFONCEMENT NE SE CHOISIT PAS, IL SE DEDUIT. Le terrain voxel ne
		// creuse que dans une bande de `bandeM` sous la surface : rien, ni
		// galerie, ni chambre, ni arche, ni gouffre, n'existe plus bas. Un
		// decor de fond pose SOUS cette bande ne peut donc boucher aucune
		// ouverture, ou qu'on soit.
		//
		// ET C'EST CE QUI FAIT DISPARAITRE L'APPARITION BRUTALE. Signale en
		// jeu : « quand je sors de l'arche le paysage en fond apparait d'un
		// coup ». C'etait la bascule qui se voyait enfin -- avant, le gel de
		// 520 ms la masquait. Sous la bande, la nappe vue n'est visible de
		// NULLE PART ou l'on pourrait se trouver sous un plafond : la cacher
		// puis la rendre ne change plus rien a l'image, et il n'y a plus rien
		// a faire apparaitre.
		//
		// La bascule est GARDEE malgre tout -- elle ne coute rien et reste un
		// filet si un creusement futur sortait de la bande.
		double SurEnfoncementM = GroundProxyHorizonExtraDropM;
		{
			FString RulesError;
			if (const UWorldseedRules* R = WorldseedPipeline::GetRules(RulesError))
			{
				const FWorldseedDensityRules DR = FWorldseedDensityRules::FromRules(*R);
				SurEnfoncementM = FMath::Max<double>(
					SurEnfoncementM, DR.BandDepthM + GroundProxyHorizonMarginM);
			}
		}

		// UNE SURCHARGE, PARCE QUE C'EST UN ARBITRAGE A L'IMAGE. Plus le decor
		// est bas, moins il bouche -- mais plus la marche qu'il laisse a la
		// limite du terrain charge est haute. Aucun calcul ne tranche cela, et
		// un A/B qui demanderait de rouvrir le fichier de regles en changerait
		// l'empreinte, donc regenererait le monde entre les deux moities.
		{
			FString Val;
			if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedNappeVue="), Val))
			{
				SurEnfoncementM = FMath::Max(0.0, FCString::Atod(*Val));
			}
		}

		// L'ENFONCEMENT SUIT L'EXAGERATION VERTICALE, comme celui de la nappe :
		// un monde etire verticalement etire aussi la hauteur des ouvertures
		// qu'il s'agit de ne plus boucher.
		const float SurEnfoncementCm =
			SurEnfoncementM * WorldseedMetersToCm * HeightExaggeration;

		TArray<FVector> HVertices;
		TArray<FVector> HNormals;
		TArray<FVector2D> HUVs;
		TArray<FVector2D> HTintRG;
		TArray<FVector2D> HTintB;
		TArray<FProcMeshTangent> HTangents;
		TArray<FLinearColor> HColors;
		TArray<int32> HTriangles;

		HVertices.SetNumUninitialized(HVerts);
		HNormals.SetNumUninitialized(HVerts);
		HUVs.SetNumUninitialized(HVerts);
		HTintRG.SetNumUninitialized(HVerts);
		HTintB.SetNumUninitialized(HVerts);
		HTangents.SetNumUninitialized(HVerts);
		HColors.SetNumUninitialized(HVerts);

		TArray<uint8> HSousZero;
		HSousZero.SetNumUninitialized(HVerts);

		for (int32 Y = 0; Y < HY; ++Y)
		{
			// LE DERNIER SOMMET TOMBE SUR LE DERNIER, et pas sur un multiple
			// du pas : sans ce bornage la nappe vue s'arreterait avant le bord
			// du monde et le paysage se terminerait par une arete droite --
			// defaut deja rencontre sur la nappe elle-meme.
			const int32 SY = FMath::Min(Y * Pas, CountY - 1);
			for (int32 X = 0; X < HX; ++X)
			{
				const int32 SX = FMath::Min(X * Pas, CountX - 1);
				const int32 Src = SY * CountX + SX;
				const int32 Dst = Y * HX + X;

				HVertices[Dst] = Vertices[Src];
				HVertices[Dst].Z -= SurEnfoncementCm;
				HNormals[Dst] = Normals[Src];
				HUVs[Dst] = UVs[Src];
				HTintRG[Dst] = TintRG[Src];
				HTintB[Dst] = TintB[Src];
				HTangents[Dst] = Tangents[Src];
				HColors[Dst] = Colors[Src];
				HSousZero[Dst] = SousZero[Src];
			}
		}

		HTriangles.Reserve((HX - 1) * (HY - 1) * 6);
		for (int32 Y = 0; Y < HY - 1; ++Y)
		{
			for (int32 X = 0; X < HX - 1; ++X)
			{
				const int32 A = Y * HX + X;
				const int32 C = A + HX;

				if (bFlipWinding)
				{
					HTriangles.Add(A); HTriangles.Add(A + 1); HTriangles.Add(C);
					HTriangles.Add(A + 1); HTriangles.Add(C + 1); HTriangles.Add(C);
				}
				else
				{
					HTriangles.Add(A); HTriangles.Add(C); HTriangles.Add(A + 1);
					HTriangles.Add(A + 1); HTriangles.Add(C); HTriangles.Add(C + 1);
				}
			}
		}

		// --- LA MER DU DECOR PREND SA PROPRE SECTION ------------------------
		//
		// SIGNALE EN JEU DEPUIS 930 M : « on voit le shader de la mer detaille
		// sur la moitie gauche, une coupure nette, puis du bleu plat a
		// droite ». Le bleu plat est CE maillage-ci : il joue deja le role du
		// `Water_FarMesh` du plugin -- dont la jupe, elle, s'accroche aux
		// bornes de la ZONE (64 x 32 km) et reste donc hors d'atteinte depuis
		// l'interieur du monde. Il porte simplement le materiau de TERRAIN,
		// mat, sans speculaire ni reflet de ciel.
		//
		// POURQUOI CELA NE SE VOYAIT PAS PLUS TOT, et c'est de la geometrie
		// pure : le bord de la fenetre est a 6 144 m, donc son angle sous
		// l'horizontale vaut `atan(altitude / 6144)` -- 0,6 degre a 69 m,
		// 2,9 a 307, mais 8,6 a 930. Mes trois points de vue etaient sous
		// l'horizon. **Une couture a distance fixe se juge a l'ALTITUDE, pas
		// a la distance.**
		//
		// LE PARTAGE EST CONSERVATEUR : un triangle ne passe a la mer que si
		// ses TROIS sommets sont submerges. La bande du rivage reste donc avec
		// la terre, et aucun materiau de mer ne deborde sur une plage.
		//
		// ET LES SOMMETS SONT REMAPPES, PAS DUPLIQUES. Donner le tableau
		// complet aux deux sections doublerait deux millions de sommets pour
		// n'en dessiner qu'une part dans chacune.
		TArray<int32> TriTerre;
		TArray<int32> TriMer;
		TriTerre.Reserve(HTriangles.Num());
		TriMer.Reserve(HTriangles.Num());
		for (int32 I = 0; I + 2 < HTriangles.Num(); I += 3)
		{
			const int32 A = HTriangles[I];
			const int32 B = HTriangles[I + 1];
			const int32 C = HTriangles[I + 2];
			TArray<int32>& Cible =
				(HSousZero[A] && HSousZero[B] && HSousZero[C]) ? TriMer : TriTerre;
			Cible.Add(A); Cible.Add(B); Cible.Add(C);
		}

		Vue->ClearAllMeshSections();

		int32 SommetsTerre = 0;
		int32 SommetsMer = 0;

		auto PoserSection =
			[&](int32 Index, const TArray<int32>& Source, UMaterialInterface* Mat) -> int32
		{
			if (Source.Num() == 0)
			{
				return 0;
			}

			TArray<int32> Remap;
			Remap.Init(INDEX_NONE, HVerts);

			TArray<FVector> SV; TArray<FVector> SN; TArray<FVector2D> SU;
			TArray<FVector2D> S2; TArray<FVector2D> S3;
			TArray<FProcMeshTangent> ST; TArray<FLinearColor> SC;
			TArray<int32> SI;
			SI.Reserve(Source.Num());

			for (int32 Ind : Source)
			{
				if (Remap[Ind] == INDEX_NONE)
				{
					Remap[Ind] = SV.Num();
					SV.Add(HVertices[Ind]); SN.Add(HNormals[Ind]);
					SU.Add(HUVs[Ind]); S2.Add(HTintRG[Ind]); S3.Add(HTintB[Ind]);
					ST.Add(HTangents[Ind]); SC.Add(HColors[Ind]);
				}
				SI.Add(Remap[Ind]);
			}

			Vue->CreateMeshSection_LinearColor(
				Index, SV, SI, SN, SU, S2, S3, TArray<FVector2D>(), SC, ST, false);
			if (Mat)
			{
				Vue->SetMaterial(Index, Mat);
			}
			Vue->SetMeshSectionVisible(Index, true);
			return SV.Num();
		};

		UMaterialInterface* const MatTerre = ChooseTerrainMaterial(Mode);
		UMaterialInterface* const MatMer = ChoisirMateriauMerDecor(MatTerre);

		SommetsTerre = PoserSection(0, TriTerre, MatTerre);
		SommetsMer = PoserSection(1, TriMer, MatMer);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] sol de fond : section terre %d sommets / %d triangles, ")
			TEXT("section mer %d sommets / %d triangles, materiau de mer %s"),
			SommetsTerre, TriTerre.Num() / 3,
			SommetsMer, TriMer.Num() / 3,
			MatMer ? *MatMer->GetName() : TEXT("AUCUN"));

		// Meme argument que pour la nappe : un decor de fond n'a rien a faire
		// dans les reflets ni dans les ombres tracees, et le budget de ray
		// tracing se degrade en SILENCE une fois depasse.
		Vue->SetVisibleInRayTracing(false);

		ProxyVueSommets = HVerts;

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] sol de fond : vue %dx%d sommets (pas %.0f m, ")
			TEXT("enfoncee de %.0f m en plus), eau %dx%d sommets"),
			HX, HY, StepX * Geometry.MetersPerPixel() * Pas,
			SurEnfoncementM, CountX, CountY);
	}

	// LE RELEVE DIT CE QUI EST COUVERT, PAS CE QUI A ETE DEMANDE.
	//
	// Un sol de fond tronque ne ressemble pas a une panne : le paysage se
	// termine par une arete droite, ce qu'on prend pour un bord de monde. La
	// seule facon de le voir sans y aller est de comparer ici l'etendue
	// atteinte a celle de la grille.
	const float SpanKmX = (CountX > 1)
		? FMath::Min(FMath::RoundToInt((CountX - 1) * StepX), Geometry.NX - 1)
			* Geometry.MetersPerPixel() / 1000.0f : 0.0f;
	const float SpanKmY = (CountY > 1)
		? FMath::Min(FMath::RoundToInt((CountY - 1) * StepY), Geometry.NY - 1)
			* Geometry.MetersPerPixel() / 1000.0f : 0.0f;

	// Retenu pour que le chronometre de la bascule sache sur COMBIEN il porte :
	// « 180 ms » ne dit rien, « 180 ms sur 8,4 millions de sommets » dit tout.
	ProxySommets = CountX * CountY;

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] sol de fond : %dx%d sommets couvrant %.1f x %.1f km ")
		TEXT("sur %.1f x %.1f km  |  pas %.0f m, depassement evite %.0f m  (%.0f ms)"),
		CountX, CountY, SpanKmX, SpanKmY,
		Geometry.WidthM() / 1000.0f, Geometry.HeightM / 1000.0f,
		StepX * Geometry.MetersPerPixel(), WorstOvershootM,
		(FPlatformTime::Seconds() - StartTime) * 1000.0);
}

void AWorldseedTerrain::ComputeVertexAppearance(int32 Cell, float HeightM,
	const FVector& Normal, const FWorldseedAppearance& Mode,
	FLinearColor& OutColour, FVector2D& OutTintRG, FVector2D& OutTintB) const
{
	// La pente se lit directement sur la composante verticale de la normale :
	// cos(pente), sans arc cosinus.
	const float CosRockStart = FMath::Cos(FMath::DegreesToRadians(RockSlopeStartDeg));
	const float CosRockFull = FMath::Cos(FMath::DegreesToRadians(RockSlopeFullDeg));

	const bool bTexturePack = Mode.bTexturePack;
	const bool bColourByBiome = Mode.bColourByBiome;
	const bool bHasClimate = Mode.bHasClimate;
	const bool bHasCover = Mode.bHasCover;

	// --- poids des couches ---------------------------------------
	const float HereM = HeightM;

	// La pente se lit directement sur la composante verticale de la
	// normale : cos(pente). Pas besoin d arc cosinus.
	const float Rock = 1.0f - FMath::GetMappedRangeValueClamped(
		FVector2D(CosRockFull, CosRockStart), FVector2D(0.0f, 1.0f),
		static_cast<float>(Normal.Z));

	float Snow = 0.0f;
	float Vegetation = 0.0f;
	if (bHasClimate)
	{
		const int32 CI = Cell;

		// La neige suit la TEMPERATURE, pas l altitude : un sommet
		// equatorial et une plaine polaire peuvent etre a la meme
		// altitude sans avoir le meme climat.
		Snow = FMath::GetMappedRangeValueClamped(
			FVector2D(SnowTempC, SnowTempFullC), FVector2D(0.0f, 1.0f), TempC[Cell]);

		Vegetation = FMath::GetMappedRangeValueClamped(
			FVector2D(AridMm, LushMm), FVector2D(0.0f, 1.0f), PrecipMm[Cell]);
	}
	else
	{
		// Sans climat on retombe sur l altitude seule, faute de mieux.
		Snow = FMath::GetMappedRangeValueClamped(
			FVector2D(150.0f, 320.0f), FVector2D(0.0f, 1.0f), HereM);
		Vegetation = 1.0f - Snow;
	}

	// Le sable ne tient qu au bord de l eau, et pas sur une falaise.
	const float Beach = FMath::GetMappedRangeValueClamped(
		FVector2D(BeachTopM, 0.0f), FVector2D(0.0f, 1.0f), HereM)
		* (1.0f - Rock) * (1.0f - Snow);

	// La roche gagne partout ou ca penche : rien ne pousse ni ne tient
	// sur une paroi.
	Vegetation *= (1.0f - Rock) * (1.0f - Snow) * (1.0f - Beach);

	// Par defaut la teinte ne sert pas : le mode poids de couches n'en
	// a que faire, et le mode couleur de biome la met dans RGBA.
	OutTintRG = FVector2D::ZeroVector;
	OutTintB = FVector2D::ZeroVector;

	// --- LE DECOR LOINTAIN DOIT LIRE COMME UNE MER SOUS ZERO --------------
	//
	// SIGNALE EN JEU : « je vois nettement un carre d'ocean autour de moi ».
	// Le bord du carre est la fenetre glissante du plugin Water ; au-dela, il
	// n'y a AUCUNE eau rendue. Le plateau cotier s'y dessinait donc a sec, et
	// son biome le peint en PLAGE -- d'ou une bande de sable pale, coupee par
	// un trait DROIT qui traverse les dunes sans les suivre. Verifie a
	// l'echantillon sur `balayage/4_centre.png` : eau R78 G125 B153, puis
	// plateau R196 G188 B174 de l'autre cote du trait.
	//
	// AGRANDIR LA FENETRE NE SUPPRIME PAS CETTE COUTURE, IL LA DEPLACE. Elle
	// reapparait des qu'un haut-fond clair s'etend au-dela. Peindre en mer ce
	// qui est sous zero la rend invisible PAR CONSTRUCTION, a n'importe quelle
	// distance et depuis n'importe quelle altitude, pour zero appel de dessin.
	//
	// CE QUE CE N'EST PAS : de l'eau. C'est du DECOR qui a la couleur de
	// l'eau. Il n'a ni vague, ni reflet, ni profondeur -- exactement comme le
	// `Water_FarMesh` du plugin, qui remplit le meme role et que nous ne
	// pouvons pas employer ici (sa jupe s'accroche aux bornes de la ZONE,
	// 64 x 32 km, donc hors d'atteinte depuis l'interieur du monde).
	//
	// LA GEOMETRIE N'EST PAS TOUCHEE, seulement la couleur. Le sol de fond est
	// deja ENFONCE sous la bande creusable pour ne rien boucher ; le remonter
	// a zero le ferait ressortir au travers du relief.
	if (Mode.bMerOpaque && bColourByBiome && HereM < 0.0f)
	{
		OutColour = Mode.bMerTemoin
			? FLinearColor(1.0f, 0.0f, 1.0f, 1.0f)
			: WorldseedBiomes::Colour(EWorldseedBiome::Ocean);
		return;
	}

	if (bTexturePack)
	{
		const int32 CI = Cell;

		// LE SUBSTRAT DECIDE DE LA MATIERE, PAS LE CLIMAT. Une paroi en
		// foret tropicale porte desormais "foret tropicale" dans l'axe
		// des biomes ; si on lisait cet axe pour choisir les textures,
		// la falaise se couvrirait d'herbe. AppearanceBiome rend
		// l'identifiant d'avant la separation des deux axes, donc cette
		// ligne peint rigoureusement la meme chose qu'avant.
		const EWorldseedBiome Biome = bHasCover
			? WorldseedBiomes::AppearanceBiome(Biomes.Index[Cell], Biomes.Cover[Cell])
			: static_cast<EWorldseedBiome>(Biomes.Index[Cell]);

		// RGBA porte les POIDS DE MATIERE, jamais une couleur : c'est
		// le materiau qui melange les quatre textures avec.
		OutColour = WorldseedBiomes::SlotWeights(Biome);

		FLinearColor Tint = WorldseedBiomes::Colour(Biome);
		if (bHasCover)
		{
			const EWorldseedCover C = static_cast<EWorldseedCover>(Biomes.Cover[Cell]);
			if (C != EWorldseedCover::None)
			{
				Tint = FMath::Lerp(Tint,
					WorldseedBiomes::CoverColour(C), CoverTint);
			}
		}

		// LA TEINTE PART DEJA NORMALISEE EN LUMINANCE.
		//
		// Le materiau se contente alors d'un multiplie : c'est ici, en
		// C++, que le calcul delicat se lit et se verifie, pas dans un
		// graphe de shader. Sans cette normalisation, multiplier par la
		// couleur de reference d'un biome — qui vaut autour de 0,4 —
		// assombrirait le sol : une savane aurait une herbe plus sombre
		// qu'une prairie, alors qu'elle doit seulement etre plus jaune.
		const float Luminance = FMath::Max(
			0.299f * Tint.R + 0.587f * Tint.G + 0.114f * Tint.B, 0.01f);

		// Borne haute : un biome tres sombre et tres sature donnerait
		// sinon un facteur enorme sur un seul canal, et un sol fluo.
		const FLinearColor Normalised(
			FMath::Min(Tint.R / Luminance, 2.5f),
			FMath::Min(Tint.G / Luminance, 2.5f),
			FMath::Min(Tint.B / Luminance, 2.5f));

		OutTintRG = FVector2D(Normalised.R, Normalised.G);
		OutTintB = FVector2D(Normalised.B, 0.0f);
	}
	else if (bColourByBiome)
	{
		// LA COULEUR DU BIOME REMPLACE LES POIDS, elle ne s'y ajoute
		// pas : les deux occupent les memes quatre canaux, et un
		// materiau ne peut pas deviner lequel il recoit.
		const int32 CI = Cell;
		FLinearColor Tint = WorldseedBiomes::Colour(bHasCover
			? WorldseedBiomes::AppearanceBiome(Biomes.Index[Cell], Biomes.Cover[Cell])
			: static_cast<EWorldseedBiome>(Biomes.Index[Cell]));

		// L'eau se MELE au biome au lieu de l'effacer : c'est tout le
		// benefice de la separation des deux axes.
		if (bHasCover && CoverTint > 0.0f)
		{
			const EWorldseedCover Cover =
				static_cast<EWorldseedCover>(Biomes.Cover[Cell]);
			if (Cover != EWorldseedCover::None)
			{
				Tint = FMath::Lerp(Tint,
					WorldseedBiomes::CoverColour(Cover), CoverTint);
			}
		}

		OutColour = Tint;
	}
	else
	{
		OutColour = FLinearColor(Rock, Vegetation, Beach, Snow);
	}
}

void AWorldseedTerrain::PlacePlayerOnTerrain()
{
	APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Pawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Worldseed] aucun pion a reposer"));
		return;
	}

	const FVector Current = Pawn->GetActorLocation();
	const float GroundZ = GetHeightAtWorldXY(Current.X, Current.Y);
	Pawn->SetActorLocation(FVector(Current.X, Current.Y, GroundZ + PlayerClearanceCm),
		false, nullptr, ETeleportType::TeleportPhysics);

	float Lon = 0.0f;
	float Lat = 0.0f;
	GetLonLatAtWorldXY(Current.X, Current.Y, Lon, Lat);

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] joueur repose : z %.0f -> %.0f  lon %.1f  lat %.1f  (%d chunks)"),
		Current.Z, GroundZ + PlayerClearanceCm, Lon, Lat, Chunks.Num());
}

void AWorldseedTerrain::GetLonLatAtWorldXY(float WorldX, float WorldY,
	float& OutLongitudeDeg, float& OutLatitudeDeg) const
{
	OutLongitudeDeg = 0.0f;
	OutLatitudeDeg = 0.0f;
	if (Geometry.NX < 2)
	{
		return;
	}

	const FVector Local = GetActorTransform().InverseTransformPosition(
		FVector(WorldX, WorldY, 0.0f));

	const float WidthCm = Geometry.WidthM() * WorldseedMetersToCm;
	const float HeightCm = Geometry.HeightM * WorldseedMetersToCm;
	if (WidthCm <= SMALL_NUMBER || HeightCm <= SMALL_NUMBER)
	{
		return;
	}

	float U = static_cast<float>(Local.X) / WidthCm + 0.5f;
	U -= FMath::FloorToFloat(U);
	const float V = FMath::Clamp(static_cast<float>(Local.Y) / HeightCm + 0.5f, 0.0f, 1.0f);

	// Convention geographique : -180 a l'ouest, +180 a l'est. La grille, elle,
	// numerote de 0 a 1 depuis son bord gauche — et UDS attend la convention
	// geographique, pas celle de la texture.
	OutLongitudeDeg = U * 360.0f - 180.0f;
	OutLatitudeDeg = Geometry.LatitudeDegForV(V);
}

float AWorldseedTerrain::GetHeightAtWorldXY(float WorldX, float WorldY) const
{
	if (Geometry.NX < 2 || HeightsM.Num() != Geometry.CellCount())
	{
		return GetActorLocation().Z;
	}

	const FVector Local = GetActorTransform().InverseTransformPosition(
		FVector(WorldX, WorldY, 0.0f));

	const float WidthCm = Geometry.WidthM() * WorldseedMetersToCm;
	const float HeightCm = Geometry.HeightM * WorldseedMetersToCm;

	float U = static_cast<float>(Local.X) / WidthCm + 0.5f;
	U -= FMath::FloorToFloat(U);
	const float V = FMath::Clamp(static_cast<float>(Local.Y) / HeightCm + 0.5f, 0.0f, 1.0f);

	const float HeightM = WorldseedGrid::SampleUV(HeightsM, Geometry.NX, Geometry.NY, U, V);
	return GetActorLocation().Z + HeightM * WorldseedMetersToCm * HeightExaggeration;
}
