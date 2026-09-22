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

	// UNE PORTE POUR L'A/B, parce que c'est un arbitrage A L'IMAGE. Sans elle
	// il faudrait recompiler entre les deux moities, et le depot a une regle
	// contre les A/B qui rouvrent le fichier de regles.
	float Active = 1.0f;
	{
		FString Val;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedRampe="), Val))
		{
			Active = FMath::Clamp(FCString::Atof(*Val), 0.0f, 1.0f);
		}
	}

	const float Facteur = FMath::Max(GroundProxyRampFactor, 1.1f);
	const float DebutCm = RayonM * WorldseedMetersToCm;
	const float FinCm = RayonM * Facteur * WorldseedMetersToCm;

	MatiereNappeVue->SetScalarParameterValue(TEXT("NappeRampeDebutCm"), DebutCm);
	MatiereNappeVue->SetScalarParameterValue(TEXT("NappeRampeFinCm"), FinCm);
	MatiereNappeVue->SetScalarParameterValue(TEXT("NappeRampeActive"), Active);

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] sol de fond : rampe %s -- entiere jusqu'a %.0f m, ")
		TEXT("nulle au-dela de %.0f m (rayon de vue %.0f m%s)"),
		Active > 0.0f ? TEXT("ARMEE") : TEXT("COUPEE"),
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

	if (!bBuildGroundProxy || Geometry.NX < 2 || HeightsM().Num() != Geometry.CellCount())
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
		&& (Biomes().Index.Num() == Geometry.CellCount());
	Mode.bColourByBiome = (Colouring == EWorldseedTerrainColouring::BiomeColour)
		&& (Biomes().Index.Num() == Geometry.CellCount());
	Mode.bHasClimate = (TempC().Num() == Geometry.CellCount())
		&& (PrecipMm().Num() == Geometry.CellCount());
	Mode.bHasCover = (Biomes().Cover.Num() == Geometry.CellCount());

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

	// --- CE QUE L'ENFONCEMENT DE LA NAPPE VUE VA NOYER -------------------
	//
	// LE CALCUL EST HISSE ICI POUR AVOIR UN SEUL PROPRIETAIRE. Il servait plus
	// bas a enfoncer la nappe de l'IMAGE ; il sert aussi a compter ce que cet
	// enfoncement coute, et deux copies d'un meme seuil divergent a la premiere
	// retouche.
	//
	// POURQUOI CE COMPTE EXISTE. La nappe vue passe sous la bande creusable
	// pour ne boucher aucune cavite -- cent vingt-cinq metres. Mais elle porte
	// le relief du monde ENTIER, et tout ce qui culmine sous cette valeur passe
	// alors SOUS LE NIVEAU DE LA MER : l'ocean, qui est un plan a l'altitude
	// zero, le recouvre. Au-dela du rayon de chargement, une plaine cotiere ou
	// une vallee basse ne se lit donc plus comme une terre un peu affaissee,
	// elle DISPARAIT sous l'eau. Vu depuis un sommet, une vallee verte devient
	// une baie.
	//
	// C'est le vrai cout de l'enfoncement, et il ne se voyait nulle part : le
	// registre ne parlait que d'« une marche de 125 m ».
	double SurEnfoncementM = GroundProxyHorizonExtraDropM;

	// LA MARGE DE MER EST CELLE DES CAVITES, ET CE N'EST PAS UNE COINCIDENCE.
	// C'est elle qui interdit a une chambre d'exister trop bas ; c'est donc
	// elle, et pas un nombre choisi ici, qui borne par le bas ce que la nappe
	// doit passer sous les cavites. La recopier les ferait diverger.
	double MargeMerM = 5.0;
	{
		FString RulesError;
		if (const UWorldseedRules* R = WorldseedPipeline::GetRules(RulesError))
		{
			const FWorldseedDensityRules DR = FWorldseedDensityRules::FromRules(*R);
			SurEnfoncementM = FMath::Max<double>(
				SurEnfoncementM, DR.BandDepthM + GroundProxyHorizonMarginM);
			MargeMerM = FWorldseedCaveRules::FromRules(*R).SeaMarginM;
		}
	}
	{
		FString Val;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedNappeVue="), Val))
		{
			SurEnfoncementM = FMath::Max(0.0, FCString::Atod(*Val));
		}

		// LA MARGE EST SURCHARGEABLE PARCE QU'ELLE EST EN QUESTION. Posee a la
		// valeur des cavites -- cinq metres -- elle laisse la terre sauvee du
		// noyage affleurer AU RAS de l'eau, donc indiscernable de la mer a
		// trois kilometres. La regler demande un A/B, et le depot a une regle
		// contre les A/B qui rouvrent le fichier de regles.
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedNappeMarge="), Val))
		{
			MargeMerM = FMath::Max(0.0, FCString::Atod(*Val));
		}
	}

	int32 SommetsEmerges = 0;
	int32 SommetsNoyesParLaVue = 0;

	// ET LA COURBE, PAS SEULEMENT LE POINT. « 16 % des terres noyees a 125 m »
	// ne dit pas ce qu'on regagnerait en enfoncant moins : la distribution des
	// altitudes basses n'est pas uniforme, et c'est elle qui decide si ramener
	// l'enfoncement a quatre-vingts metres rend beaucoup ou presque rien.
	constexpr int32 NbPaliers = 6;
	constexpr double Paliers[NbPaliers] = { 25.0, 50.0, 75.0, 100.0, 125.0, 150.0 };
	int32 ParPalier[NbPaliers] = {};

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
				Lowest = FMath::Min(Lowest, HeightsM()[Base + Col]);
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

			const float HereM = HeightsM()[Cell];

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
				(HeightsM()[SY * Geometry.NX + PX] - HeightsM()[SY * Geometry.NX + MX]) / SpanX;
			const float DZDY =
				(HeightsM()[PY * Geometry.NX + SX] - HeightsM()[MY * Geometry.NX + SX]) / SpanY;

			const FVector N = FVector(-DZDX, -DZDY, 1.0f).GetSafeNormal();
			Normals[Index] = N;
			Tangents[Index] = FProcMeshTangent(
				FVector(1.0f, 0.0f, DZDX).GetSafeNormal(), false);

			UVs[Index] = FVector2D(
				static_cast<float>(SX) / static_cast<float>(Geometry.NX),
				static_cast<float>(SY) / static_cast<float>(Geometry.NY));

			WorldseedApparence::Sommet(*Monde, ReglesSurface(), Cell, HereM, N,
				Mode, Colors[Index], TintRG[Index], TintB[Index]);

			SousZero[Index] = (HereM < 0.0f) ? 1 : 0;
			if (HereM < 0.0f) { ++SommetsSousZero; }
			else
			{
				++SommetsEmerges;
				if (HereM < SurEnfoncementM) { ++SommetsNoyesParLaVue; }
				for (int32 P = 0; P < NbPaliers; ++P)
				{
					if (HereM < Paliers[P]) { ++ParPalier[P]; }
				}
			}
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] sol de fond : %d sommets sur %d sous le niveau zero (%.1f %%)"),
		SommetsSousZero, Verts,
		Verts > 0 ? 100.0f * SommetsSousZero / Verts : 0.0f);

	// LE COMPTE QUI DIT CE QUE LE PLAFOND SAUVE, et il se lit en part des
	// TERRES, pas du monde : c'est la terre emergee qui etait en jeu.
	//
	// IL COMPTE CE QUI EST APLATI, PAS CE QUI EST NOYE, parce que depuis le
	// plafonnement plus rien ne se noie. Le jour ou ce chiffre remonterait, ce
	// serait que le plafond a saute -- et un compte le dit, la ou un
	// chronometre ne dirait rien.
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] sol de fond : enfoncement de la vue %.0f m, PLAFONNE a ")
		TEXT("%.0f m au-dessus de la mer -- %d sommets de terre sur %d (%.1f %%) ")
		TEXT("sont aplatis au lieu d'etre noyes"),
		SurEnfoncementM, MargeMerM, SommetsNoyesParLaVue, SommetsEmerges,
		SommetsEmerges > 0 ? 100.0f * SommetsNoyesParLaVue / SommetsEmerges : 0.0f);

	{
		FString Courbe;
		for (int32 P = 0; P < NbPaliers; ++P)
		{
			Courbe += FString::Printf(TEXT("  %.0f m -> %.1f %%"), Paliers[P],
				SommetsEmerges > 0 ? 100.0f * ParPalier[P] / SommetsEmerges : 0.0f);
		}
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] sol de fond : part des terres noyee selon l'enfoncement --%s"),
			*Courbe);
	}

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
		//
		// LA VALEUR EST CALCULEE PLUS HAUT, avec le compte des terres qu'elle
		// noie : c'est la meme grandeur, et la recopier ici l'aurait fait
		// diverger de ce que le releve annonce.
		//
		// `-WorldseedNappeVue=` la surcharge, parce que c'est un arbitrage A
		// L'IMAGE. Plus le decor est bas, moins il bouche -- mais plus il noie
		// de terres basses. Aucun calcul ne tranche cela, et un A/B qui
		// demanderait de rouvrir le fichier de regles en changerait
		// l'empreinte, donc regenererait le monde entre les deux moities.

		// L'ENFONCEMENT SUIT L'EXAGERATION VERTICALE, comme celui de la nappe :
		// un monde etire verticalement etire aussi la hauteur des ouvertures
		// qu'il s'agit de ne plus boucher. Il s'applique SOMMET PAR SOMMET
		// depuis que la mer le plafonne -- voir la boucle ci-dessous.

		TArray<FVector> HVertices;
		TArray<FVector> HNormals;
		TArray<FVector2D> HUVs;
		TArray<FVector2D> HTintRG;
		TArray<FVector2D> HTintB;
		TArray<FVector2D> HCapCm;
		TArray<FProcMeshTangent> HTangents;
		TArray<FLinearColor> HColors;
		TArray<int32> HTriangles;

		HVertices.SetNumUninitialized(HVerts);
		HNormals.SetNumUninitialized(HVerts);
		HUVs.SetNumUninitialized(HVerts);
		HTintRG.SetNumUninitialized(HVerts);
		HTintB.SetNumUninitialized(HVerts);
		HCapCm.SetNumUninitialized(HVerts);
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

				// --- LE SOMMET RESTE A SON ALTITUDE, IL PORTE SON PLAFOND ---
				//
				// L'ENFONCEMENT NE SE CUIT PLUS DANS LA GEOMETRIE. Il depend
				// desormais de la distance a la CAMERA -- entier pres du
				// joueur, nul au loin -- donc il change a chaque image et ne
				// peut pas vivre dans des sommets qu'on ne rebatit jamais. Il
				// est applique par deplacement de sommets dans le materiau, et
				// ce canal lui porte la seule chose que le materiau ne sait pas
				// calculer : de combien CE sommet a le droit de descendre.
				//
				// --- L'ENFONCEMENT NE CREUSE JAMAIS SOUS LA MER -------------
				//
				// SANS CE PLAFOND, IL NOIE UN SIXIEME DES TERRES. Mesure sur le
				// monde de reference : 398 839 sommets de terre sur 2 449 474,
				// soit 16,3 %, passaient sous l'altitude zero -- et l'ocean,
				// qui est un plan a zero, les recouvrait. Au-dela du rayon de
				// chargement, une vallee verte avec sa plage se lisait comme
				// une BAIE. Vu a l'image depuis le massif de l'est, et confirme
				// par un temoin a `-WorldseedNappeVue=0` ou la meme vallee est
				// verte : 28 a 30 % du cadre changeait entre les deux moities.
				//
				// ET LE PLAFOND NE ROUVRE RIEN, par construction. L'enfoncement
				// existe pour que la nappe passe sous toute cavite ; or aucune
				// chambre n'existe sous `profondeurMax + rayonMax + margeMer`
				// d'altitude -- soixante-six metres ici -- donc tout plancher de
				// cavite se trouve AU-DESSUS de la marge de mer, partout. Une
				// nappe posee a cette marge reste dessous sans avoir a creuser
				// davantage. Les deux contraintes se rejoignent exactement.
				//
				// CE QU'ON ECHANGE, ET IL FAUT LE DIRE : les terres qui
				// culminent sous l'enfoncement ne sont plus noyees, elles sont
				// APLATIES -- elles se lisent au loin comme une plate-forme a
				// hauteur de rivage au lieu d'un relief. On troque une mer
				// fausse contre une plaine faussement plate, ce qui est le bon
				// sens de l'echange : une terre reste une terre.
				const double FloorM =
					(HVertices[Dst].Z + DropCm)
					/ (WorldseedMetersToCm * HeightExaggeration);
				const double EnfonceM = WorldseedNappe::PlafondDEnfoncement(
					FloorM, MargeMerM, SurEnfoncementM);

				// ET LE PLAFOND DE MER EPINGLE LE RIVAGE, ce qui supprime le
				// seul risque serieux de la rampe. Un enfoncement qui suit la
				// camera fait « respirer » le relief lointain : un point voit
				// son altitude dessinee changer d'environ un metre par dix
				// metres parcourus. Sur une crete c'est invisible ; sur un
				// TRAIT DE COTE cela se verrait, le rivage avancant et reculant
				// au rythme des pas. Or le plafond vaut zero des qu'on approche
				// du niveau de la mer : le rivage ne bouge pas, par
				// construction.
				HCapCm[Dst] = FVector2D(
					EnfonceM * WorldseedMetersToCm * HeightExaggeration, 0.0);
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
			TArray<FVector2D> S2; TArray<FVector2D> S3; TArray<FVector2D> S4;
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
					S4.Add(HCapCm[Ind]);
					ST.Add(HTangents[Ind]); SC.Add(HColors[Ind]);
				}
				SI.Add(Remap[Ind]);
			}

			// LE QUATRIEME CANAL PORTE LE PLAFOND D'ENFONCEMENT, et il etait
			// libre : la nappe n'en posait que trois. Les chunks du terrain,
			// eux, n'en posent AUCUN -- leur section se cree sans tableau
			// d'UV -- donc ils ne peuvent pas lire ce canal par accident.
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

		// L'INSTANCE DYNAMIQUE NE VA QUE SUR LA SECTION TERRE. La section MER
		// porte le materiau du plugin Water, etranger a notre rampe -- et elle
		// n'en a aucun besoin : son plafond vaut zero partout, un fond marin
		// n'ayant jamais eu le droit de descendre.
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
