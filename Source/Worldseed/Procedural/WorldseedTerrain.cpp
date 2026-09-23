// Worldseed - terrain runtime decoupe en chunks, depuis le monde spherique.

#include "Procedural/WorldseedTerrain.h"
#include "Procedural/WorldseedGameInstance.h"
#include "Procedural/WorldseedSkyDriverComponent.h"
#include "Procedural/WorldseedWaterComponent.h"
#include "Procedural/WorldseedGroundProxy.h"
#include "Procedural/WorldseedNappe.h"
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

	// LE MINUTEUR DE MAILLAGE ET LE PLACEMENT DU JOUEUR SONT PARTIS AVEC LE
	// MAILLEUR LEGATAIRE. Ils appartenaient au mailleur, donc au voxel depuis
	// qu'il tient le relief : `AWorldseedVoxelTerrain` a son propre minuteur de
	// diffusion, et c'est lui qui pose le joueur -- « surface + 150 cm » n'a
	// d'ailleurs aucun sens dans une grotte.

	// LE SOL DE FOND SE SURVEILLE TOUJOURS : il traverse les cavites.
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
		// ON PREND UNE REFERENCE, ON NE COPIE PLUS. C'etait ici que le monde se
		// dedoublait une premiere fois -- une copie pour sortir de l'instance
		// de jeu, puis les `MoveTemp` vers les membres -- avant qu'`AdoptWorld`
		// n'en fasse une seconde vers l'acteur voxel.
		if (FWorldseedMondePtr Partage = GI->MondePartage())
		{
			Monde = Partage;
			WorldSeed = Monde->Seed;
			Geometry = Monde->Geometry;

			// LA LITHOLOGIE RESTE COPIEE, ET C'EST ASSUME. Son tableau
			// d'identifiants pese huit megaoctets sur les cent
			// quatre-vingt-treize du monde ; la partager demanderait de rendre
			// `FWorldseedLithology::Id` non proprietaire, donc de toucher a
			// une structure que la generation REMPLIT. On ne deplace que ce
			// qui pese, et l'on dit ce qu'on laisse.
			Lithology.Id = Monde->LithologyId;
			TexturePack = Monde->TexturePack;

			// Le point de depart choisi dans le menu. Ce terrain ne s'en sert
			// pas lui-meme -- c'est l'acteur voxel qui place le joueur -- il
			// ne fait que le convoyer jusqu'a lui.
			bDepartDemande = Monde->bHasSpawn;
			DepartXYM = Monde->SpawnXYM;
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
				WorldSeed, Geometry.NX, Geometry.NY, SeasonalAmpC().Num());
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

	// MEME CHEMIN QUE LE MENU : on batit UN monde partage, et l'on s'y
	// refere. Les `MoveTemp` vident le resultat de la chaine plutot que de le
	// recopier -- il ne sert a rien apres.
	{
		FWorldseedWorldData Bati;
		Bati.Seed = FallbackSeed;
		Bati.Geometry = World.Geometry;
		Bati.ElevationM = MoveTemp(World.ElevationM);
		Bati.TempC = MoveTemp(World.Climate.TempMeanC);
		Bati.PrecipMm = MoveTemp(World.Climate.PrecipMm);
		Bati.SeasonalAmpC = MoveTemp(World.Climate.SeasonalAmpC);
		Bati.Continentality = MoveTemp(World.Climate.Continentality);
		Bati.Biomes = MoveTemp(World.Biomes);
		Bati.Tables = MoveTemp(World.Tables);
		Bati.Canyons = MoveTemp(World.Canyons);

		Monde = MakeShared<const FWorldseedWorldData, ESPMode::ThreadSafe>(MoveTemp(Bati));
	}

	WorldSeed = FallbackSeed;
	Geometry = World.Geometry;
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

	WorldseedCaves::Build(Geometry, HeightsM(), PrecipMm(), Lithology,
		FWorldseedLithologyRules::FromRules(*Rules),
		FWorldseedCaveRules::FromRules(*Rules), HeightExaggeration, WorldSeed, Caves);
}

void AWorldseedTerrain::Rebuild()
{
	if (!AcquireWorld() || Geometry.NX < 2 || HeightsM().Num() != Geometry.CellCount())
	{
		return;
	}

	// LE DECOUPAGE EN CHUNKS DE CELLULES A DISPARU AVEC LE MAILLEUR QUI S'EN
	// SERVAIT. Il decrivait une grille de chunks calee sur la grille 2D ; le
	// voxel, lui, decoupe l'espace en cubes de trente-deux metres et n'a que
	// faire du pas de simulation. Le journal l'annoncait encore.
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] terrain %.1f x %.1f km  grille %dx%d"),
		Geometry.WidthM() / 1000.0f, Geometry.HeightM / 1000.0f,
		Geometry.NX, Geometry.NY);

	// LE SOL DE FOND AVANT LE VOXEL ET AVANT L'EAU : c'est lui qui donne au
	// systeme d'eau un sol sur TOUTE la carte, la ou le voxel n'en couvre
	// qu'un rayon autour du joueur.
	BuildGroundProxy();

	// LE VOXEL NE REMPLACE QUE LE MAILLAGE. Le sol de fond vient d'etre bati,
	// l'ocean suit, le ciel et les questions de climat restent ici : seule la
	// geometrie proche change de main.
	SpawnVoxelTerrain();

	// L'ocean vient APRES le relief : il se pose sur un terrain deja connu.
	if (Water)
	{
		Water->Build(Geometry, HeightsM(), HeightExaggeration);
	}
}

void AWorldseedTerrain::ReglerRampeDeLaNappe()
{
	if (!MatiereNappeVue) { return; }

	// LE RAYON VIENT DE L'ACTEUR VOXEL, pas d'une copie. C'est lui qui decide
	// jusqu'ou le terrain detaille existe, et donc jusqu'ou la nappe doit
	// rester enfoncee. Tant qu'il n'est pas pondu, on pose le defaut de sa
	// classe -- corrige au second appel.
	float RayonM = AWorldseedVoxelTerrain::StaticClass()
		->GetDefaultObject<AWorldseedVoxelTerrain>()->LoadRadiusM;
	if (VoxelTerrain)
	{
		RayonM = VoxelTerrain->LoadRadiusM;
	}

	const float Facteur = FMath::Max(GroundProxyRampFactor, 1.1f);
	const float DebutCm = RayonM * WorldseedMetersToCm;
	const float FinCm = RayonM * Facteur * WorldseedMetersToCm;

	MatiereNappeVue->SetScalarParameterValue(TEXT("NappeRampeDebutCm"), DebutCm);
	MatiereNappeVue->SetScalarParameterValue(TEXT("NappeRampeFinCm"), FinCm);
	MatiereNappeVue->SetScalarParameterValue(TEXT("NappeRampeActive"), 1.0f);

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] sol de fond : rampe ARMEE -- entiere jusqu'a %.0f m, ")
		TEXT("nulle au-dela de %.0f m (rayon de vue %.0f m%s)"),
		RayonM, RayonM * Facteur, RayonM,
		VoxelTerrain ? TEXT("") : TEXT(", voxel pas encore pondu"));
}

void AWorldseedTerrain::SpawnVoxelTerrain()
{
	UWorld* World = GetWorld();
	if (!World || VoxelTerrain)
	{
		return;
	}

	// SANS MONDE, PAS DE VOXEL -- ET ON LE DIT. `Rebuild` ne nous appelle
	// qu'apres avoir verifie que le relief a la taille de la grille, donc le
	// cas ne se presente pas aujourd'hui ; mais la garde qui le tient est deux
	// fonctions plus haut, et `ToSharedRef` sur un pointeur nul n'echoue qu'en
	// developpement -- en livraison elle dereferencerait zero. Une ligne de
	// journal vaut mieux qu'un plantage dont personne ne saura la cause.
	if (!Monde.IsValid())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Worldseed] voxel : pas de monde a adopter, acteur non pose"));
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
		// LE MONDE PASSE ENTIER ET PAR REFERENCE. Il y avait ici neuf arguments
		// puis un second appel pour deux champs de plus ; il en reste quatre,
		// et plus une seule copie. Ce qui accompagne le monde ne lui appartient
		// pas : les grottes se rebatissent, la lithologie porte un catalogue
		// issu des regles, et l'exageration verticale est un reglage de CE
		// terrain -- elle doit etre la meme des deux cotes, sans quoi le sol de
		// fond et le relief proche decriraient deux echelles differentes.
		VoxelTerrain->AdoptWorld(Monde.ToSharedRef(), HeightExaggeration,
			Caves, Lithology);

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
			&& (Biomes().Index.Num() == Geometry.CellCount());
		Mode.bColourByBiome = (Colouring == EWorldseedTerrainColouring::BiomeColour)
			&& (Biomes().Index.Num() == Geometry.CellCount());

		if (UMaterialInterface* Material = ChooseTerrainMaterial(Mode))
		{
			VoxelTerrain->TerrainMaterial = Material;
		}

		VoxelTerrain->FinishSpawning(GetActorTransform());

		// LE RAYON DE VUE N'EST CONNU QU'ICI. La nappe s'est batie avant cette
		// ponte, donc sa rampe a ete calee sur le defaut de classe ; on la
		// recale maintenant sur le rayon REEL, surcharges de ligne de commande
		// comprises.
		ReglerRampeDeLaNappe();
	}

	if (!VoxelTerrain)
	{
		// IL N'Y A PLUS DE REPLI, ET C'EST UN CHOIX. Le mailleur en carte
		// d'altitude vivait ici pour ce cas ; il n'etait joignable par aucune
		// ligne de commande, rien ne l'avait exerce depuis le 18 septembre, et
		// un filet qu'on n'eprouve pas n'est pas un filet. L'echec se dit donc
		// franchement au lieu de se rattraper en silence sur un chemin dont on
		// ignorait s'il fonctionnait encore.
		UE_LOG(LogTemp, Error,
			TEXT("[Worldseed] terrain : le mailleur voxel n'a pas pu etre pose "
				 "-- il n'y aura AUCUN relief proche"));
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
	if (Geometry.NX < 2 || TempC().Num() != Count || PrecipMm().Num() != Count)
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

	OutSample.TempMeanC = WorldseedGrid::SampleUV(TempC(), Geometry.NX, Geometry.NY, U, V);
	OutSample.PrecipMm = WorldseedGrid::SampleUV(PrecipMm(), Geometry.NX, Geometry.NY, U, V);

	// Amplitude et continentalite arrivent du modele climatique. Un monde
	// d'avant leur transport n'en porte pas : on garde alors les valeurs par
	// defaut de l'echantillon plutot que d'inventer des saisons.
	if (SeasonalAmpC().Num() == Count)
	{
		OutSample.SeasonalAmpC = WorldseedGrid::SampleUV(
			SeasonalAmpC(), Geometry.NX, Geometry.NY, U, V);
	}
	if (ContinentalityGrid().Num() == Count)
	{
		OutSample.Continentality = WorldseedGrid::SampleUV(
			ContinentalityGrid(), Geometry.NX, Geometry.NY, U, V);
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

	if (!bBuildGroundProxy || Geometry.NX < 2 || HeightsM().Num() != Geometry.CellCount())
	{
		return;
	}

	const double StartTime = FPlatformTime::Seconds();

	// --- CE QUE L'ACTEUR SAIT, ET QUE LA NAPPE N'A PAS A RELIRE --------------
	//
	// LE RETRAIT SUIT LE CHAMP, PAS UN NOMBRE EN DUR. La nappe doit passer sous
	// la surface REELLE, que le voxel deplace de `overhangAmplitudeM +
	// detailAmplitudeM` par rapport au relief macro. Un retrait ecrit en dur
	// laisserait la nappe affleurer des qu'un de ces deux reglages augmente --
	// c'est l'angle mort que `archeMargeSommetM` corrige deja ailleurs.
	//
	// LA MARGE DE MER EST CELLE DES CAVITES, ET CE N'EST PAS UNE COINCIDENCE :
	// c'est elle qui interdit a une chambre d'exister trop bas, donc c'est elle,
	// et pas un nombre choisi ici, qui borne par le bas ce que la nappe doit
	// passer sous les cavites. La recopier les ferait diverger.
	FWorldseedNappeRegles NR;
	NR.Largeur = GroundProxyWidth;
	NR.RetraitM = GroundProxyDropM;
	NR.ExagerationZ = HeightExaggeration;
	NR.SurEnfoncementM = GroundProxyHorizonExtraDropM;
	NR.MargeMerM = 5.0;
	NR.Pas = GroundProxyHorizonStride;
	NR.bInverserEnroulement = bFlipWinding;
	{
		FString RulesError;
		if (const UWorldseedRules* R = WorldseedPipeline::GetRules(RulesError))
		{
			const FWorldseedDensityRules DR = FWorldseedDensityRules::FromRules(*R);
			NR.RetraitM = FMath::Max<double>(NR.RetraitM,
				DR.OverhangAmplitudeM + DR.DetailAmplitudeM);
			NR.SurEnfoncementM = FMath::Max<double>(
				NR.SurEnfoncementM, DR.BandDepthM + GroundProxyHorizonMarginM);
			NR.MargeMerM = FWorldseedCaveRules::FromRules(*R).SeaMarginM;
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] sol de fond : retrait %.1f m (plancher du reglage %.1f, ")
		TEXT("deplacement du champ compris)"),
		NR.RetraitM, GroundProxyDropM);

	FWorldseedAppearance Mode;
	Mode.bTexturePack = (Colouring == EWorldseedTerrainColouring::TexturePack)
		&& (TexturePack != EWorldseedTexturePack::BiomeColour)
		&& (Biomes().Index.Num() == Geometry.CellCount());
	Mode.bColourByBiome = (Colouring == EWorldseedTerrainColouring::BiomeColour)
		&& (Biomes().Index.Num() == Geometry.CellCount());
	Mode.bHasClimate = (TempC().Num() == Geometry.CellCount())
		&& (PrecipMm().Num() == Geometry.CellCount());
	Mode.bHasCover = (Biomes().Cover.Num() == Geometry.CellCount());

	// LA MER DU DECOR EST OPAQUE, CELLE DES PIEDS NE L'EST PAS. Le seul endroit
	// du projet ou ce drapeau s'arme : voir son commentaire dans
	// `FWorldseedAppearance`.
	Mode.bMerOpaque = true;

	// --- LA NAPPE SE BATIT AILLEURS, ET C'EST TOUT L'INTERET -----------------
	//
	// Elle ne demande ni acteur ni composant : un monde, une geometrie, des
	// altitudes, une apparence et sept reglages. Ce qui reste ici est le CYCLE
	// DE VIE -- poser les acteurs, creer les sections, choisir les materiaux --
	// et c'est bien le travail d'un acteur.
	FWorldseedNappeMaillage Pleine;
	FWorldseedNappeReleve Releve;
	WorldseedNappe::Batir(*Monde, Geometry, HeightsM(), ReglesSurface(), Mode,
		NR, Pleine, Releve);

	const int32 CountX = Pleine.CountX;
	const int32 CountY = Pleine.CountY;

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] sol de fond : %d sommets sur %d sous le niveau zero (%.1f %%)"),
		Releve.SommetsSousZero, Releve.Sommets,
		Releve.Sommets > 0 ? 100.0f * Releve.SommetsSousZero / Releve.Sommets : 0.0f);

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] sol de fond : enfoncement de la vue %.0f m, PLAFONNE a ")
		TEXT("%.0f m au-dessus de la mer -- %d sommets de terre sur %d (%.1f %%) ")
		TEXT("sont aplatis au lieu d'etre noyes"),
		NR.SurEnfoncementM, NR.MargeMerM, Releve.SommetsNoyesParLaVue,
		Releve.SommetsEmerges,
		Releve.SommetsEmerges > 0
			? 100.0f * Releve.SommetsNoyesParLaVue / Releve.SommetsEmerges : 0.0f);

	{
		static constexpr double Paliers[FWorldseedNappeReleve::NbPaliers] =
			{ 25.0, 50.0, 75.0, 100.0, 125.0, 150.0 };
		FString Courbe;
		for (int32 P = 0; P < FWorldseedNappeReleve::NbPaliers; ++P)
		{
			Courbe += FString::Printf(TEXT("  %.0f m -> %.1f %%"), Paliers[P],
				Releve.SommetsEmerges > 0
					? 100.0f * Releve.ParPalier[P] / Releve.SommetsEmerges : 0.0f);
		}
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] sol de fond : part des terres noyee selon l'enfoncement --%s"),
			*Courbe);
	}

	// --- LA NAPPE QUE L'EAU LIT ----------------------------------------------
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
		0, Pleine.Positions, Pleine.Triangles, Pleine.Normales, Pleine.UV0,
		Pleine.TeinteRG, Pleine.TeinteB, TArray<FVector2D>(),
		Pleine.Couleurs, Pleine.Tangentes, false);

	if (UMaterialInterface* Material = ChooseTerrainMaterial(Mode))
	{
		Mesh->SetMaterial(0, Material);
	}

	// CELLE-CI NE SE VOIT PLUS JAMAIS : elle ne sert QU'A l'eau. La sortir de la
	// passe principale supprime l'a-coup de 520 ms que sa bascule coutait, et la
	// garder dans la passe de PROFONDEUR est ce qui evite le piege `[0 .. 0]` --
	// l'ocean qui cesse de se dessiner sans le moindre avertissement.
	Mesh->SetRenderInDepthPass(true);
	Mesh->SetMeshSectionVisible(0, true);
	Mesh->SetVisibleInRayTracing(false);
	Mesh->SetRenderInMainPass(false);

	// --- LE SOL QU'ON VOIT, DECIME DEPUIS CELUI QUE L'EAU LIT ----------------
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

	const float StepX = (CountX > 1)
		? static_cast<float>(Geometry.NX - 1) / static_cast<float>(CountX - 1) : 0.0f;
	const float StepY = (CountY > 1)
		? static_cast<float>(Geometry.NY - 1) / static_cast<float>(CountY - 1) : 0.0f;

	if (UProceduralMeshComponent* const Vue =
		HorizonProxy ? HorizonProxy->GetMesh() : nullptr)
	{
		FWorldseedNappeMaillage VueMaillage;
		TArray<FVector2D> PlafondCm;
		TArray<int32> TriTerre;
		TArray<int32> TriMer;
		WorldseedNappe::Decimer(Pleine, NR, VueMaillage, PlafondCm, TriTerre, TriMer);

		const int32 HX = VueMaillage.CountX;
		const int32 HY = VueMaillage.CountY;
		const int32 HVerts = HX * HY;

		Vue->ClearAllMeshSections();

		int32 SommetsTerre = 0;
		int32 SommetsMer = 0;

		// LE REMAPPAGE RESTE ICI PARCE QU'IL FINIT EN SECTION : decouper la
		// liste d'indices et creer le composant sont le meme geste, et les
		// separer demanderait une structure intermediaire pour rien.
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
			TArray<FVector2D> S2; TArray<FVector2D> S3; TArray<FVector2D> S4;
			TArray<FProcMeshTangent> ST; TArray<FLinearColor> SC;
			TArray<int32> SI;
			SI.Reserve(Source.Num());

			for (int32 Ind : Source)
			{
				if (Remap[Ind] == INDEX_NONE)
				{
					Remap[Ind] = SV.Num();
					SV.Add(VueMaillage.Positions[Ind]);
					SN.Add(VueMaillage.Normales[Ind]);
					SU.Add(VueMaillage.UV0[Ind]);
					S2.Add(VueMaillage.TeinteRG[Ind]);
					S3.Add(VueMaillage.TeinteB[Ind]);
					S4.Add(PlafondCm[Ind]);
					ST.Add(VueMaillage.Tangentes[Ind]);
					SC.Add(VueMaillage.Couleurs[Ind]);
				}
				SI.Add(Remap[Ind]);
			}

			Vue->CreateMeshSection_LinearColor(
				Index, SV, SI, SN, SU, S2, S3, S4, SC, ST, false);
			if (Mat)
			{
				Vue->SetMaterial(Index, Mat);
			}
			Vue->SetMeshSectionVisible(Index, true);
			return SV.Num();
		};

		UMaterialInterface* const MatTerre = ChooseTerrainMaterial(Mode);
		UMaterialInterface* const MatMer = ChoisirMateriauMerDecor(MatTerre);

		MatiereNappeVue = MatTerre
			? UMaterialInstanceDynamic::Create(MatTerre, this)
			: nullptr;

		SommetsTerre = PoserSection(
			0, TriTerre, MatiereNappeVue ? MatiereNappeVue : MatTerre);
		SommetsMer = PoserSection(1, TriMer, MatMer);

		ReglerRampeDeLaNappe();

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] sol de fond : section terre %d sommets / %d triangles, ")
			TEXT("section mer %d sommets / %d triangles, materiau de mer %s"),
			SommetsTerre, TriTerre.Num() / 3,
			SommetsMer, TriMer.Num() / 3,
			MatMer ? *MatMer->GetName() : TEXT("AUCUN"));

		Vue->SetVisibleInRayTracing(false);

		ProxyVueSommets = HVerts;

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] sol de fond : vue %dx%d sommets (pas %.0f m, ")
			TEXT("enfoncee de %.0f m en plus), eau %dx%d sommets"),
			HX, HY, StepX * Geometry.MetersPerPixel() * FMath::Clamp(NR.Pas, 1, 8),
			NR.SurEnfoncementM, CountX, CountY);
	}

	const float SpanKmX = (CountX > 1)
		? FMath::Min(FMath::RoundToInt((CountX - 1) * StepX), Geometry.NX - 1)
			* Geometry.MetersPerPixel() / 1000.0f
		: 0.0f;
	const float SpanKmY = (CountY > 1)
		? FMath::Min(FMath::RoundToInt((CountY - 1) * StepY), Geometry.NY - 1)
			* Geometry.MetersPerPixel() / 1000.0f
		: 0.0f;

	ProxySommets = CountX * CountY;

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] sol de fond : %dx%d sommets couvrant %.1f x %.1f km ")
		TEXT("sur %.1f x %.1f km  |  pas %.0f m, depassement evite %.0f m  (%.0f ms)"),
		CountX, CountY, SpanKmX, SpanKmY,
		Geometry.WidthM() / 1000.0f, Geometry.HeightM / 1000.0f,
		StepX * Geometry.MetersPerPixel(), Releve.PireDepassementM,
		(FPlatformTime::Seconds() - StartTime) * 1000.0);
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
	if (Geometry.NX < 2 || HeightsM().Num() != Geometry.CellCount())
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

	const float HeightM = WorldseedGrid::SampleUV(HeightsM(), Geometry.NX, Geometry.NY, U, V);
	return GetActorLocation().Z + HeightM * WorldseedMetersToCm * HeightExaggeration;
}

const TArray<float>& AWorldseedTerrain::FloatsVides()
{
	// UN TABLEAU VIDE PARTAGE, PLUTOT QU'UN DEREFERENCEMENT NU. Les accesseurs
	// du monde sont appeles depuis des chemins qui tournent AVANT qu'un monde
	// soit charge -- le releve d'ecran, les gardes de validite -- et ceux-ci
	// testent `Num() == CellCount()`. Leur rendre un tableau vide les laisse
	// repondre « pas de monde » comme avant ; leur rendre un pointeur nul les
	// ferait tomber.
	static const TArray<float> Vide;
	return Vide;
}
