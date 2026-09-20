// Worldseed - terrain voxel : diffusion des chunks autour du joueur.

#include "Procedural/WorldseedVoxelTerrain.h"
#include "Procedural/WorldseedPlateau.h"
#include "Procedural/WorldseedStrata.h"

#include "Procedural/WorldseedGameInstance.h"
#include "Procedural/WorldseedPipeline.h"

#include "Async/Async.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "TimerManager.h"

AWorldseedVoxelTerrain::AWorldseedVoxelTerrain()
{
	PrimaryActorTick.bCanEverTick = false;

	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(RootScene);
}

// ---------------------------------------------------------------- chargement

void AWorldseedVoxelTerrain::AdoptWorld(int32 InSeed,
	const FWorldseedGeometry& InGeometry, const TArray<float>& InHeightsM,
	const FWorldseedBiomeMap& InBiomes, float InHeightExaggeration,
	const FWorldseedCaveNetwork& InCaves, const FWorldseedLithology& InLithology,
	const TArray<float>& InPrecipMm)
{
	CaveNetwork = InCaves;
	PrecipMm = InPrecipMm;
	Lithology = InLithology;
	WorldSeed = InSeed;
	Geometry = InGeometry;
	HeightsM = InHeightsM;
	Biomes = InBiomes;
	HeightExaggeration = InHeightExaggeration;
	bWorldAdopted = true;
}

bool AWorldseedVoxelTerrain::LoadWorld()
{
	// UN MONDE ADOPTE NE SE RECHARGE PAS. C'est celui de l'acteur qui a pose
	// celui-ci, donc celui que le sol de fond et l'ocean decrivent deja.
	if (bWorldAdopted && Geometry.NX >= 2 && HeightsM.Num() == Geometry.CellCount())
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] voxel : monde repris du terrain, seed=%d  %dx%d"),
			WorldSeed, Geometry.NX, Geometry.NY);
		return true;
	}

	if (const UWorldseedGameInstance* GI =
		UWorldseedGameInstance::GetWorldseedGameInstance(this))
	{
		FWorldseedWorldData Loaded;
		if (GI->TryGetWorld(Loaded))
		{
			WorldSeed = Loaded.Seed;
			Geometry = Loaded.Geometry;
			HeightsM = MoveTemp(Loaded.ElevationM);
			Biomes = MoveTemp(Loaded.Biomes);
			// Le reseau n'est pas transporte par le menu : il se rebatit ici.
			CaveNetwork.Reset();

			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : monde repris du menu, seed=%d  %dx%d"),
				WorldSeed, Geometry.NX, Geometry.NY);
			return true;
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] voxel : aucun monde en attente, generation de secours"));

	WorldseedPipeline::FResult World;
	FString Error;
	if (!WorldseedPipeline::Generate(FallbackSeed, FallbackHeightMeters,
		FallbackResolutionY, World, Error))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Worldseed] voxel : generation de secours impossible : %s"), *Error);
		return false;
	}

	WorldSeed = FallbackSeed;
	Geometry = World.Geometry;
	HeightsM = MoveTemp(World.ElevationM);
	Biomes = MoveTemp(World.Biomes);
	return true;
}

void AWorldseedVoxelTerrain::BeginPlay()
{
	Super::BeginPlay();

	StartSeconds = FPlatformTime::Seconds();

	// Ce que la ligne de commande a impose : les regles ne le recouvriront pas.
	bool bRayonForce = false;
	bool bNiveauxForce = false;
	bool bAnneau0Force = false;

	// LE RAYON SE PILOTE DEPUIS LA LIGNE DE COMMANDE, pour le banc.
	// Sans ce levier, comparer deux rayons demanderait de recompiler entre
	// les deux mesures -- et le depot a une regle contre les A/B dont les
	// deux moities ne sont pas montees a l'identique.
	{
		float Rayon = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedRayon="), Rayon)
			&& Rayon > 32.0f)
		{
			// LE RAYON DE DECHARGEMENT SUIT, ET C'EST OBLIGATOIRE.
			//
			// DEFAUT DE BANC PAYE COMPTANT. La premiere version ne forcait que
			// LoadRadiusM : au-dela de `UnloadRadiusM`, fige a 350 m, les
			// chunks etaient batis puis DETRUITS aussitot. A 400 comme a 600 m
			// le monde tournait donc en boucle sur le meme millier de chunks,
			// et le releve rendait EXACTEMENT 1679 des deux cotes -- la
			// cinquieme fois dans ce depot qu'un chiffre identique au chiffre
			// pres trahit un plafond cache. Toutes les conclusions tirees de
			// ces mesures, dont « le debit est le mur », portaient sur une
			// configuration cassee.
			//
			// L'hysteresis d'origine -- 350 pour 250, soit 1,4 fois -- est
			// conservee : sans elle, un pas en avant et un pas en arriere sur
			// la frontiere feraient construire et detruire le meme chunk en
			// boucle.
			const float Hysteresis = (LoadRadiusM > 0.0f)
				? (UnloadRadiusM / LoadRadiusM) : 1.4f;
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : rayons forces a %.0f m de chargement ")
				TEXT("et %.0f de dechargement (defauts %.0f / %.0f)"),
				Rayon, Rayon * Hysteresis, LoadRadiusM, UnloadRadiusM);
			LoadRadiusM = Rayon;
			UnloadRadiusM = Rayon * Hysteresis;
			bRayonForce = true;
		}
	}

	// LES ANNEAUX AUSSI SE PILOTENT DEPUIS LA LIGNE DE COMMANDE, et pour la
	// meme raison : un A/B dont les deux moities demandent une recompilation
	// n'est pas un A/B. C'est ce qui permet de verifier, sur la MEME binaire,
	// que `NiveauMax = 0` rend exactement la diffusion d'avant les anneaux.
	{
		int32 Niveaux = -1;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedNiveaux="), Niveaux)
			&& Niveaux >= 0)
		{
			NiveauMax = FMath::Clamp(Niveaux, 0, 4);
			bNiveauxForce = true;
		}
		float Anneau0 = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedAnneau0="), Anneau0)
			&& Anneau0 > 32.0f)
		{
			RayonAnneau0M = Anneau0;
			bAnneau0Force = true;
		}
	}

	if (!LoadWorld())
	{
		return;
	}

	FString Error;
	if (const UWorldseedRules* Rules = WorldseedPipeline::GetRules(Error))
	{
		DensityRules = FWorldseedDensityRules::FromRules(*Rules);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] voxel : regles illisibles (%s), valeurs par defaut"), *Error);
	}

	// --- LES REGLES FIXENT LES ANNEAUX, LA LIGNE DE COMMANDE GARDE LA MAIN ---
	//
	// Les seuils vivent dans world_rules.json, c'est la regle du depot. Mais le
	// banc doit pouvoir comparer deux configurations sur le MEME binaire, sans
	// quoi les deux moities d'un A/B ne sont pas montees a l'identique -- et ce
	// depot a deja bati une journee de conclusions sur un harnais fausse. Les
	// surcharges de ligne de commande sont donc appliquees APRES les regles et
	// l'emportent ; c'est pour cela qu'elles sont relues ici plutot que plus
	// haut, avant que les regles n'existent.
	if (!bRayonForce && DensityRules.LoadRadiusM > 32.0f)
	{
		const float Hysteresis = (LoadRadiusM > 0.0f)
			? (UnloadRadiusM / LoadRadiusM) : 1.4f;
		LoadRadiusM = DensityRules.LoadRadiusM;
		UnloadRadiusM = LoadRadiusM * Hysteresis;
	}
	if (!bNiveauxForce)
	{
		NiveauMax = FMath::Clamp(DensityRules.NiveauMax, 0, 4);
	}
	if (!bAnneau0Force)
	{
		RayonAnneau0M = DensityRules.RayonAnneau0M;
	}
	LargeurTransition = DensityRules.LargeurTransition;

	// ET LE MAILLEUR AUSSI SE DEBRANCHE EN LIGNE DE COMMANDE. Un A/B qui
	// demande de rouvrir le fichier de regles change son empreinte, donc
	// regenere le monde entre les deux moities : ce ne serait plus le meme
	// monde, et le depot a une regle contre les A/B mal montes.
	{
		int32 Tv = -1;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedTransvoxel="), Tv)
			&& Tv >= 0)
		{
			DensityRules.bTransvoxel = (Tv > 0);
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : mailleur force -- %s"),
				DensityRules.bTransvoxel ? TEXT("Transvoxel") : TEXT("FMarchingCubes"));
		}
	}

	// ET LA TAILLE DU VOXEL, QUI EST LA DENSITE DE MAILLAGE ELLE-MEME.
	//
	// C.EST LE SEUL LEVIER QUI CHANGE LE NOMBRE DE TRIANGLES SANS TOUCHER A
	// RIEN D.AUTRE : le decoupage en chunks suit ChunkSideM, donc doubler le
	// voxel divise par quatre les triangles a nombre de chunks, de composants
	// et d.emprise RIGOUREUSEMENT identiques. C.est ce qui permet de repondre
	// a « le cout du voxel, est-ce les triangles ? » par une mesure et non par
	// une intuition -- le depot a deja cru que le cout etait la ou il n.etait
	// pas, sur le debit de streaming comme sur le bridage de l.editeur.
	{
		float Vx = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedVoxel="), Vx) && Vx > 0.0f)
		{
			DensityRules.VoxelSizeM = Vx;
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : taille forcee -- %.2f m, soit %d cellules par chunk"),
				Vx, FMath::RoundToInt(ChunkSideM / Vx));
		}
	}

	if (NiveauMax > 0)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] voxel : %d anneaux -- %.0f / %.0f / %.0f m, vue %.0f m, ")
			TEXT("dalle de transition %.2f cellule"),
			NiveauMax + 1, RayonAnneauM(0), RayonAnneauM(1), RayonAnneauM(2),
			LoadRadiusM, LargeurTransition);
	}

	Density.Init(Geometry, HeightsM, HeightExaggeration, WorldSeed, DensityRules);

	// LA LITHOLOGIE EST BRANCHEE APRES Init, ET SEULEMENT SI ELLE EXISTE. Sans
	// elle le champ reste evaluable et ne creuse aucune diaclase : une donnee
	// absente doit rester sans effet, jamais produire un effet arbitraire.
	if (!Lithology.IsValid(Geometry.CellCount()))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] voxel : PAS de lithologie (%d identifiants pour %d cellules) ")
			TEXT("-- les parois garderont la couleur du biome de surface"),
			Lithology.Id.Num(), Geometry.CellCount());
	}
	if (Lithology.IsValid(Geometry.CellCount()))
	{
		FString LithoError;
		if (const UWorldseedRules* LithoRules = WorldseedPipeline::GetRules(LithoError))
		{
			const FWorldseedLithologyRules LR = FWorldseedLithologyRules::FromRules(*LithoRules);
			Density.SetLithology(Lithology, LR);

			StratRules = FWorldseedStratRules::FromRules(*LithoRules, LR);
			DureteParId.Reset();
			for (const FWorldseedLithologyEntry& E : LR.Catalogue)
			{
				DureteParId.Add(E.Hardness);
			}
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : strates -- %d bancs, %.0f m de serie, datum %.0f m"),
				StratRules.Serie.Num(), StratRules.TotalThicknessM, StratRules.DatumM);

			CouleurParRoche.Reset();
			NomParRoche.Reset();
			for (const FWorldseedLithologyEntry& E : LR.Catalogue)
			{
				CouleurParRoche.Add(E.Colour);
				NomParRoche.Add(E.Label.IsEmpty() ? E.Key : E.Label);
			}

			// SANS CETTE LIGNE ON NE SAIT PAS SI LA ROCHE EST BRANCHEE, et la
			// difference ne se voit pas : une paroi peut etre creme parce
			// qu'elle est du calcaire, ou parce que le biome au-dessus est du
			// desert. Deux causes, une seule image.
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : lithologie branchee, %d couleurs de roche, ")
				TEXT("fondu %.0f m"),
				CouleurParRoche.Num(), DensityRules.RockColourFadeM);
		}
	}
	bWorldReady = Density.IsValid();

	if (!bWorldReady)
	{
		UE_LOG(LogTemp, Error, TEXT("[Worldseed] voxel : champ de densite invalide"));
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] voxel : chunks de %.0f m, voxel %.2f m, rayon %.0f m, ")
		TEXT("collision partout, %d travaux simultanes"),
		ChunkSideM, DensityRules.VoxelSizeM, LoadRadiusM, MaxJobsInFlight);

	// LES SITES DE TABLES SE REBATISSENT, ILS NE SE TRANSPORTENT PAS. Meme
	// raisonnement que pour le reseau de grottes, que le menu ne transporte pas
	// non plus : la recherche est une fonction PURE du relief, des regles et de
	// la graine, donc la transporter doublerait une donnee deterministe. Et il
	// FAUT la faire ici, sans quoi une partie lancee depuis le menu n'aurait
	// aucun lieu remarquable la ou une partie lancee en PIE en a.
	{
		FString Err;
		if (const UWorldseedRules* const R = WorldseedPipeline::GetRules(Err))
		{
			WorldseedPlateau::Sites(Geometry, HeightsM,
				FWorldseedPlateauRules::FromRules(*R), Lithology,
				FWorldseedLithologyRules::FromRules(*R), PrecipMm,
				WorldSeed, Tables, &Canyons);
		}
	}

	if (UWorld* const W = GetWorld())
	{
		W->GetTimerManager().SetTimer(UpdateTimer, this,
			&AWorldseedVoxelTerrain::UpdateChunks,
			FMath::Max(UpdatePeriod, 0.05f), true);
	}

	// Une passe tout de suite : sans elle le monde reste vide le temps du
	// premier reveil du minuteur, ce qui se voit au demarrage.
	UpdateChunks();
}

void AWorldseedVoxelTerrain::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* const W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(UpdateTimer);
	}

	// ON ANNULE, ON N'ATTEND PAS. Les travaux en vol detiennent un pointeur
	// partage sur leur propre structure, jamais sur l'acteur : ils peuvent donc
	// finir dans le vide sans rien toucher de mort. Attendre les bloquerait le
	// fil de jeu pendant la fermeture du PIE, pour rien.
	for (TPair<FWorldseedChunkKey, FWorldseedVoxelChunkState>& Pair : Chunks)
	{
		if (Pair.Value.Job.IsValid())
		{
			Pair.Value.Job->bCancel.store(true, std::memory_order_release);
		}
	}
	Chunks.Reset();

	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------- geometrie

FVector AWorldseedVoxelTerrain::StreamingOriginCm() const
{
	if (const UWorld* const W = GetWorld())
	{
		if (const APawn* const Pawn = UGameplayStatics::GetPlayerPawn(W, 0))
		{
			return Pawn->GetActorLocation();
		}
	}
	return GetActorLocation();
}

FBox AWorldseedVoxelTerrain::ChunkBoundsM(const FWorldseedChunkKey& Key) const
{
	const double Side = CoteM(Key.Niveau);
	const FVector Min(Key.C.X * Side, Key.C.Y * Side, Key.C.Z * Side);
	return FBox(Min, Min + FVector(Side, Side, Side));
}

int32 AWorldseedVoxelTerrain::NiveauEn(const FVector& PointM,
	const FVector& OrigineM) const
{
	int32 Niveau = FMath::Max(NiveauMax, 0);
	while (Niveau > 0)
	{
		const double Cote = CoteM(Niveau);
		const FWorldseedChunkKey Contenant{
			FIntVector(
				FMath::FloorToInt(PointM.X / Cote),
				FMath::FloorToInt(PointM.Y / Cote),
				FMath::FloorToInt(PointM.Z / Cote)),
			Niveau };

		// MEME PREDICAT QUE LA DIFFUSION, et c'est toute la raison d'etre de
		// cette fonction : un masque calcule avec un autre critere que celui qui
		// decide des niveaux armerait des faces de transition la ou il n'y a pas
		// de changement de resolution, et en oublierait ailleurs.
		const FVector Centre = ChunkBoundsM(Contenant).GetCenter();
		if (FVector::Dist(Centre, OrigineM) < RayonAnneauM(Niveau - 1))
		{
			--Niveau;
		}
		else
		{
			break;
		}
	}
	return Niveau;
}

uint8 AWorldseedVoxelTerrain::MasqueDe(const FWorldseedChunkKey& Key,
	const FVector& OrigineM) const
{
	// Le niveau le plus fin ne peut pas avoir de voisin plus fin.
	if (Key.Niveau <= 0) { return 0; }

	const double Cote = CoteM(Key.Niveau);
	const FVector Centre = ChunkBoundsM(Key).GetCenter();

	// Meme ordre que WorldseedTransvoxel::EFace : -X, +X, -Y, +Y, -Z, +Z.
	static const FVector Normales[6] =
	{
		FVector(-1, 0, 0), FVector(1, 0, 0),
		FVector(0, -1, 0), FVector(0, 1, 0),
		FVector(0, 0, -1), FVector(0, 0, 1),
	};

	uint8 Masque = 0;
	for (int32 F = 0; F < 6; ++F)
	{
		const FVector Voisin = Centre + Normales[F] * Cote;
		if (NiveauEn(Voisin, OrigineM) < Key.Niveau)
		{
			Masque |= static_cast<uint8>(1 << F);
		}
	}
	return Masque;
}

void AWorldseedVoxelTerrain::PlageSurface(int32 CX, int32 CY, int32 Niveau,
	float& OutMinM, float& OutMaxM) const
{
	const FIntVector Cle(CX, CY, Niveau);
	if (const FVector2D* Deja = CacheSurface.Find(Cle))
	{
		OutMinM = static_cast<float>(Deja->X);
		OutMaxM = static_cast<float>(Deja->Y);
		return;
	}

	const double Cote = CoteM(Niveau);
	Density.SurfaceRangeM(CX * Cote, CY * Cote,
		(CX + 1) * Cote, (CY + 1) * Cote, OutMinM, OutMaxM);

	// LE CACHE SE VIDE PLUTOT QUE DE GONFLER SANS FIN. Un joueur qui traverse
	// le monde finirait par l'emplir ; le repartir de zero coute une passe de
	// recalcul et rien d'autre, puisque la donnee est deterministe.
	if (CacheSurface.Num() > 200000)
	{
		CacheSurface.Reset();
	}
	CacheSurface.Add(Cle, FVector2D(OutMinM, OutMaxM));
}

void AWorldseedVoxelTerrain::Enumerer(const FWorldseedChunkKey& Key,
	const FVector& OrigineM,
	TArray<TPair<FWorldseedChunkKey, double>>& Sortie) const
{
	const FBox Boite = ChunkBoundsM(Key);

	// ON ECARTE PAR LA DISTANCE A LA BOITE, PAS AU CENTRE, et seulement ici.
	// Un noeud grossier dont le CENTRE est hors du rayon peut tres bien avoir
	// des enfants dedans : l'ecarter sur son centre creuserait un trou. La
	// distance a la boite, elle, ne peut que diminuer en descendant.
	if (Boite.ComputeSquaredDistanceToPoint(OrigineM) >
		static_cast<double>(LoadRadiusM) * LoadRadiusM)
	{
		return;
	}

	// LA GRILLE 2D DECIDE DE LA VERTICALE : on ne descend pas dans un noeud que
	// la surface ne traverse pas, ni dans la bande creusable sous elle.
	float SurfaceMin = 0.0f;
	float SurfaceMax = 0.0f;
	PlageSurface(Key.C.X, Key.C.Y, Key.Niveau, SurfaceMin, SurfaceMax);
	if (Boite.Min.Z > SurfaceMax ||
		Boite.Max.Z < SurfaceMin - DensityRules.BandDepthM)
	{
		return;
	}

	const FVector Centre = Boite.GetCenter();
	const double Dist = FVector::Dist(Centre, OrigineM);

	// SUBDIVISER OU EMETTRE, JAMAIS LES DEUX. C'est ce qui fait de la diffusion
	// une partition : ni recouvrement, ni trou, quelle que soit la facon dont
	// les rayons sont choisis.
	if (Key.Niveau > 0 && Dist < RayonAnneauM(Key.Niveau - 1))
	{
		for (int32 I = 0; I < 8; ++I)
		{
			const FWorldseedChunkKey Enfant{
				FIntVector(
					Key.C.X * 2 + (I & 1),
					Key.C.Y * 2 + ((I >> 1) & 1),
					Key.C.Z * 2 + ((I >> 2) & 1)),
				Key.Niveau - 1 };
			Enumerer(Enfant, OrigineM, Sortie);
		}
		return;
	}

	if (Dist > LoadRadiusM)
	{
		return;
	}
	Sortie.Emplace(Key, Dist);
}

FVector AWorldseedVoxelTerrain::ChunkCentreCm(const FWorldseedChunkKey& Key) const
{
	const FBox B = ChunkBoundsM(Key);
	const FVector CentreM = B.GetCenter();
	return GetActorLocation() + CentreM * WorldseedMetersToCm;
}

// ---------------------------------------------------------------- diffusion

void AWorldseedVoxelTerrain::UpdateChunks()
{
	// ON MESURE AU LIEU DE SUPPOSER. Le banc a montre un pic periodique a 2400 m
	// -- p95 a 14 ms pour une moyenne de 6,6 -- et ma premiere explication,
	// « c-est le balayage des masques », a ete DEMENTIE : le borner n-a pas
	// deplace le chiffre d-un dixieme. Plutot que d-essayer une deuxieme
	// hypothese a l-aveugle, on chronometre la passe elle-meme : si le pic n-est
	// pas ici, il est ailleurs, et ce sera dit.
	const double DebutPasse = FPlatformTime::Seconds();
	UpdateChunksInterne();
	const double Ms = (FPlatformTime::Seconds() - DebutPasse) * 1000.0;
	TotalUpdateMs += Ms;
	++UpdateCount;
	WorstUpdateMs = FMath::Max(WorstUpdateMs, Ms);
}

void AWorldseedVoxelTerrain::UpdateChunksInterne()
{
	if (!bWorldReady)
	{
		return;
	}

	const FVector OriginCm = StreamingOriginCm() - GetActorLocation();
	const FVector OriginM = OriginCm / WorldseedMetersToCm;
	const double Side = ChunkSideM;

	// --- 1. relacher ce qui est trop loin -----------------------------------
	TArray<FWorldseedChunkKey> ARelacher;
	for (const TPair<FWorldseedChunkKey, FWorldseedVoxelChunkState>& Pair : Chunks)
	{
		const FVector CentreM = ChunkBoundsM(Pair.Key).GetCenter();
		if (FVector::Dist(CentreM, OriginM) > UnloadRadiusM)
		{
			ARelacher.Add(Pair.Key);
		}
	}
	for (const FWorldseedChunkKey& Key : ARelacher)
	{
		ReleaseChunk(Key);
	}

	// --- 2. recolter les travaux termines -----------------------------------
	int32 Televerses = 0;
	for (TPair<FWorldseedChunkKey, FWorldseedVoxelChunkState>& Pair : Chunks)
	{
		FWorldseedVoxelChunkState& State = Pair.Value;
		if (!State.Job.IsValid())
		{
			continue;
		}
		if (!State.Job->bDone.load(std::memory_order_acquire))
		{
			continue;
		}
		if (Televerses >= UploadsPerPass)
		{
			break;
		}

		UploadChunk(Pair.Key, State);
		++Televerses;
	}

	// --- 3. lancer ce qui manque --------------------------------------------
	int32 EnVol = 0;
	for (const TPair<FWorldseedChunkKey, FWorldseedVoxelChunkState>& Pair : Chunks)
	{
		if (Pair.Value.Job.IsValid())
		{
			++EnVol;
		}
	}
	if (EnVol >= MaxJobsInFlight)
	{
		HoldOrReleasePlayer();
		return;
	}

	// --- 3 bis. REPRENDRE CE QUI A ETE ABANDONNE ----------------------------
	//
	// Un chunk dont le travail a ete annule reste dans la table SANS maillage,
	// SANS travail et SANS marque de vide. La boucle des candidats ci-dessous
	// saute toute cle deja presente, donc il ne reviendrait jamais tout seul :
	// il faut le relancer explicitement. C'est le pendant indispensable de la
	// distinction faite a la reception -- sans lui, on a seulement remplace un
	// trou marque "vide" par un trou sans marque.
	{
		TArray<FWorldseedChunkKey> ARelancer;
		for (const TPair<FWorldseedChunkKey, FWorldseedVoxelChunkState>& Pair : Chunks)
		{
			const FWorldseedVoxelChunkState& S = Pair.Value;
			if (!S.bEmpty && !S.Mesh && !S.Job.IsValid())
			{
				ARelancer.Add(Pair.Key);
			}
		}
		for (const FWorldseedChunkKey& Key : ARelancer)
		{
			if (EnVol >= MaxJobsInFlight) { break; }
			LaunchJob(Key);
			++EnVol;
		}
	}

	// LES COLONNES D'ABORD, LA VERTICALE ENSUITE, et c'est la grille 2D qui la
	// donne : on ne considere que les etages ou la surface peut se trouver,
	// plus la bande creusable dessous. Les etages de socle n'existent meme pas
	// dans cette enumeration.
	//
	// ON PART DU NIVEAU LE PLUS GROSSIER ET L'ON DESCEND. Avec `NiveauMax` a
	// zero, il n'y a qu'un niveau et la descente ne fait rien : l'enumeration
	// est alors RIGOUREUSEMENT celle d'avant les anneaux, ce qui est la
	// propriete de surete de ce chantier.
	const int32 NiveauHaut = FMath::Max(NiveauMax, 0);
	const double CoteHaute = CoteM(NiveauHaut);
	const int32 Portee = FMath::CeilToInt(LoadRadiusM / CoteHaute) + 1;
	const int32 CX0 = FMath::FloorToInt(OriginM.X / CoteHaute);
	const int32 CY0 = FMath::FloorToInt(OriginM.Y / CoteHaute);

	TArray<TPair<FWorldseedChunkKey, double>> Feuilles;

	for (int32 DY = -Portee; DY <= Portee; ++DY)
	{
		for (int32 DX = -Portee; DX <= Portee; ++DX)
		{
			const int32 CX = CX0 + DX;
			const int32 CY = CY0 + DY;

			float SurfaceMin = 0.0f;
			float SurfaceMax = 0.0f;
			PlageSurface(CX, CY, NiveauHaut, SurfaceMin, SurfaceMax);

			const int32 ZBas = FMath::FloorToInt(
				(SurfaceMin - DensityRules.BandDepthM) / CoteHaute);
			const int32 ZHaut = FMath::FloorToInt(SurfaceMax / CoteHaute);

			for (int32 CZ = ZBas; CZ <= ZHaut; ++CZ)
			{
				Enumerer(FWorldseedChunkKey{ FIntVector(CX, CY, CZ), NiveauHaut },
					OriginM, Feuilles);
			}
		}
	}

	// --- 3 ter. LES MASQUES PERIMES ------------------------------------------
	//
	// Le masque d'un chunk depend du niveau de ses VOISINS, donc de la position
	// du joueur : il change sans que le chunk change de niveau. Un chunk qui
	// garde un masque perime rouvre exactement la fissure que la cellule de
	// transition etait censee fermer -- et rien ne le signalerait. On remaille
	// donc, en gardant l'ancien maillage visible jusqu'au televersement du neuf.
	//
	// LE BALAYAGE EST BORNE PAR PASSE, ET LA MESURE L'A EXIGE. La premiere
	// version repassait sur TOUTES les feuilles a chaque mise a jour, ce qui
	// coute `feuilles x 6 x niveaux` descentes de NiveauEn -- et cela se voyait :
	// a 2400 m, p95 a 14 ms pour une moyenne de 6,6, soit un pic periodique a la
	// cadence exacte de la passe. Le controle qui a designe le coupable : deux
	// anneaux a 2400 m (3 934 feuilles, deux niveaux) donnent le MEME pic que
	// trois anneaux (2 436 feuilles, trois niveaux). C'est donc le PRODUIT qui
	// compte et non le nombre de chunks -- le niveau 3, un temps soupconne, est
	// innocent.
	//
	// Un balayage tournant etale le cout : la fissure eventuelle ne dure que le
	// temps d'un tour, soit quelques dixiemes de seconde, et le pic disparait.
	if (NiveauMax > 0 && Feuilles.Num() > 0)
	{
		constexpr int32 BudgetParPasse = 512;

		TArray<FWorldseedChunkKey> ARemailler;
		const int32 Nombre = Feuilles.Num();
		const int32 Examen = FMath::Min(BudgetParPasse, Nombre);

		for (int32 I = 0; I < Examen; ++I)
		{
			if (CurseurMasque >= Nombre) { CurseurMasque = 0; }
			const FWorldseedChunkKey& Cle = Feuilles[CurseurMasque].Key;
			++CurseurMasque;

			const FWorldseedVoxelChunkState* const S = Chunks.Find(Cle);
			if (!S || S->Job.IsValid() || S->bEmpty) { continue; }
			if (S->Masque != MasqueDe(Cle, OriginM))
			{
				ARemailler.Add(Cle);
			}
		}
		for (const FWorldseedChunkKey& Key : ARemailler)
		{
			if (EnVol >= MaxJobsInFlight) { break; }
			LaunchJob(Key);
			++EnVol;
		}
	}

	// Les plus proches d'abord : c'est ce que le joueur voit en premier.
	Feuilles.Sort([](const TPair<FWorldseedChunkKey, double>& A,
		const TPair<FWorldseedChunkKey, double>& B)
	{
		return A.Value < B.Value;
	});

	for (const TPair<FWorldseedChunkKey, double>& F : Feuilles)
	{
		if (EnVol >= MaxJobsInFlight)
		{
			break;
		}
		if (Chunks.Contains(F.Key))
		{
			continue;
		}
		LaunchJob(F.Key);
		++EnVol;
	}

	HoldOrReleasePlayer();
}

void AWorldseedVoxelTerrain::LaunchJob(const FWorldseedChunkKey& Key)
{
	FWorldseedVoxelChunkState& State = Chunks.FindOrAdd(Key);

	FWorldseedVoxelJobPtr Job = MakeShared<FWorldseedVoxelJob, ESPMode::ThreadSafe>();
	Job->Key = Key;
	Job->BoundsM = ChunkBoundsM(Key);
	State.Job = Job;

	// LE CHAMP EST CAPTURE PAR ADRESSE, ET C'EST SUR : il ne contient que des
	// nombres et une reference sur le relief, tous deux immuables pendant la
	// partie, et il n'a aucun etat mutable. Plusieurs fils l'interrogent donc
	// en meme temps sans verrou. Ce qui NE serait pas sur, c'est de capturer
	// l'acteur : il peut mourir avant la fin du travail.
	const FWorldseedDensity* const Champ = &Density;
	const float VoxelSizeM = VoxelM(Key.Niveau);
	const float Largeur = LargeurTransition;

	// LE MASQUE EST CALCULE ICI, SUR LE FIL DE JEU, ET RETENU DANS L'ETAT. Il
	// depend de l'origine de diffusion, qui bouge : le retenir est ce qui permet
	// de savoir, a la passe suivante, qu'il a change et qu'il faut remailler.
	const FVector OrigineM =
		(StreamingOriginCm() - GetActorLocation()) / WorldseedMetersToCm;
	const uint8 Masque = MasqueDe(Key, OrigineM);
	State.Masque = Masque;

	// LES CELLULES DE TRANSITION IMPLIQUENT LE MAILLEUR MAISON. Le marching
	// cubes du moteur ne sait pas les produire : demander un raccord tout en
	// maillant avec lui donnerait une fissure silencieuse. Des qu'un masque est
	// arme, on passe donc par Transvoxel, quel que soit le reglage.
	const bool bTransvoxel = DensityRules.bTransvoxel || (Masque != 0);

	// L'EXTRACTION SE FAIT ICI, SUR LE FIL DE JEU, ET UNE SEULE FOIS. Le chunk
	// est elargi du rayon de raccordement : une capsule qui ne touche pas la
	// boite peut quand meme arrondir une arete a l'interieur.
	if (CaveNetwork.IsValid())
	{
		CaveNetwork.Query(Job->BoundsM.ExpandBy(DensityRules.CaveBlendM + 4.0f), Job->Caves);
	}

	Async(EAsyncExecution::ThreadPool,
		[Job, Champ, VoxelSizeM, bTransvoxel, Masque, Largeur]()
	{
		if (!Job->bCancel.load(std::memory_order_acquire))
		{
			Job->bHasSurface = WorldseedVoxelChunk::Build(
				*Champ, &Job->Caves, Job->BoundsM, VoxelSizeM, Job->Mesh, Job->Stats,
				[Job]() { return Job->bCancel.load(std::memory_order_acquire); },
				bTransvoxel, Masque, Largeur);
		}

		// EN DERNIER, ET EN LIBERATION : tout ce qui precede doit etre visible
		// du fil de jeu avant qu'il ne voie ce drapeau.
		Job->bDone.store(true, std::memory_order_release);
	});
}

FString AWorldseedVoxelTerrain::DiagnostiquerColonne(FVector MondeCm) const
{
	if (!bWorldReady)
	{
		return TEXT("monde pas pret");
	}

	const FVector LocalM = (MondeCm - GetActorLocation()) / WorldseedMetersToCm;
	const double Side = ChunkSideM;
	const int32 CX = FMath::FloorToInt(LocalM.X / Side);
	const int32 CY = FMath::FloorToInt(LocalM.Y / Side);

	float SurfMin = 0.0f, SurfMax = 0.0f;
	Density.SurfaceRangeM(CX * Side, CY * Side, CX * Side + Side, CY * Side + Side,
		SurfMin, SurfMax);
	const int32 ZBas = FMath::FloorToInt((SurfMin - DensityRules.BandDepthM) / Side);
	const int32 ZHaut = FMath::FloorToInt(SurfMax / Side);

	const FVector OriginM = (StreamingOriginCm() - GetActorLocation()) / WorldseedMetersToCm;

	FString R = FString::Printf(
		TEXT("colonne (%d, %d) : surface macro %.1f a %.1f m, etages %d a %d"),
		CX, CY, SurfMin, SurfMax, ZBas, ZHaut);

	// La surface REELLE, echantillonnee au centre de la colonne : c'est elle
	// que le mailleur voit, et elle differe de la macro par le bruit.
	const double MX = CX * Side + Side * 0.5;
	const double MY = CY * Side + Side * 0.5;
	R += FString::Printf(TEXT("\n  champ au centre, tous les 4 m :"));
	for (double Z = ZBas * Side; Z <= (ZHaut + 1) * Side; Z += 4.0)
	{
		R += FString::Printf(TEXT(" %.0f:%+.1f"), Z, Density.At(FVector(MX, MY, Z)));
	}

	for (int32 CZ = ZBas; CZ <= ZHaut; ++CZ)
	{
		const FWorldseedChunkKey Key{ FIntVector(CX, CY, CZ), 0 };
		const FVector CentreM = ChunkBoundsM(Key).GetCenter();
		const double DistM = FVector::Dist(CentreM, OriginM);
		const FWorldseedVoxelChunkState* S = Chunks.Find(Key);

		R += FString::Printf(TEXT("\n  etage %+d  d=%.0f m  "), CZ, DistM);
		if (!S)
		{
			R += (DistM > LoadRadiusM)
				? TEXT("ABSENT (hors rayon)")
				: TEXT("ABSENT ALORS QU'IL EST DANS LE RAYON");
			continue;
		}
		R += FString::Printf(TEXT("present : vide=%s travail=%s maillage=%s"),
			S->bEmpty ? TEXT("OUI") : TEXT("non"),
			S->Job.IsValid() ? TEXT("en cours") : TEXT("aucun"),
			S->Mesh ? TEXT("OUI") : TEXT("AUCUN"));

		// ON REJOUE LE BALAYAGE PAR GERMES, a l'identique, pour savoir s'il
		// voit la traversee. C'est le seul moyen de distinguer un chunk que le
		// balayage a manque d'un chunk que le mailleur n'a pas su remplir :
		// vus du dehors, les deux sont un trou.
		{
			const FBox B = ChunkBoundsM(Key);
			const double PasM = DensityRules.VoxelSizeM * 4.0;
			const int32 N = FMath::Max(FMath::CeilToInt(ChunkSideM / PasM), 1);
			// AVEC LES GROTTES, comme le mailleur. Mon premier balayage de
			// diagnostic appelait le champ SANS elles et voyait donc une
			// traversee nette la ou le mailleur n'en voyait aucune : la sonde
			// et le code mesuraient deux champs differents.
			FWorldseedCaveLocal Local;
			CaveNetwork.Query(B.ExpandBy(DensityRules.CaveBlendM + 4.0f), Local);

			int32 Dedans = 0, Dehors = 0;
			for (int32 K = 0; K <= N; ++K)
			for (int32 J = 0; J <= N; ++J)
			for (int32 I = 0; I <= N; ++I)
			{
				const FVector P(
					FMath::Min(B.Min.X + I * PasM, B.Max.X),
					FMath::Min(B.Min.Y + J * PasM, B.Max.Y),
					FMath::Min(B.Min.Z + K * PasM, B.Max.Z));
				(Density.At(P, &Local) < 0.0 ? Dedans : Dehors)++;
			}
			static const TCHAR* NomCause[] = {
				TEXT("maille"), TEXT("sans traversee"), TEXT("annule"), TEXT("maillage vide") };
			R += FString::Printf(
				TEXT("  [balayage %dx%d : %d roche, %d air | cause : %s | %d capsules]"),
				N + 1, N + 1, Dedans, Dehors,
				NomCause[static_cast<int32>(S->Cause)],
				Local.Chambers.Num() + Local.Segments.Num());
			R += FString::Printf(TEXT(" [germes %d, triangles %d]"), S->Seeds, S->Tris);
		}
	}
	return R;
}

void AWorldseedVoxelTerrain::PaintVertices(FWorldseedVoxelMesh& Mesh) const
{
	const int32 Count = Mesh.Positions.Num();
	Mesh.Colours.SetNumUninitialized(Count);

	const bool bHasBiomes = (Biomes.Index.Num() == Geometry.CellCount());
	const bool bHasCover = (Biomes.Cover.Num() == Geometry.CellCount());
	const bool bAvecRoche = Lithology.IsValid(Geometry.CellCount())
		&& CouleurParRoche.Num() > 0;
	const FWorldseedDensityRules& Rules = DensityRules;

	const double WidthM = Geometry.WidthM();
	const double HeightM = Geometry.HeightM;

	for (int32 I = 0; I < Count; ++I)
	{
		if (!bHasBiomes)
		{
			Mesh.Colours[I] = FLinearColor::Gray;
			continue;
		}

		// Le sommet est en centimetres dans le repere de l'acteur ; la carte
		// des biomes est une grille 2D en longitude/latitude.
		const double X = Mesh.Positions[I].X / WorldseedMetersToCm;
		const double Y = Mesh.Positions[I].Y / WorldseedMetersToCm;

		double U = X / WidthM + 0.5;
		U -= FMath::FloorToDouble(U);
		const double V = FMath::Clamp(Y / HeightM + 0.5, 0.0, 1.0);

		const int32 Col = FMath::Clamp(
			FMath::FloorToInt(U * Geometry.NX), 0, Geometry.NX - 1);
		const int32 Row = FMath::Clamp(
			FMath::FloorToInt(V * Geometry.NY), 0, Geometry.NY - 1);

		// LA COULEUR SUIT LE SUBSTRAT QUAND IL Y EN A UN. Depuis que la roche
		// a nu et l'estran ont quitte l'axe des biomes, l'index porte le climat
		// meme sur une paroi : le lire seul peindrait la falaise en vert.
		const int32 Cell = Row * Geometry.NX + Col;
		const EWorldseedBiome Biome = bHasCover
			? WorldseedBiomes::AppearanceBiome(Biomes.Index[Cell], Biomes.Cover[Cell])
			: static_cast<EWorldseedBiome>(Biomes.Index[Cell]);
		FLinearColor Teinte = WorldseedBiomes::Colour(Biome);

		// --- SOUS TERRE, C'EST LA ROCHE QUI HABILLE -------------------------
		//
		// Ce calcul etait purement 2D : on lisait le biome de la colonne et on
		// peignait, sans aucune notion de profondeur. Une paroi de grotte a
		// quarante metres sous une prairie rendait donc VERTE -- constate a
		// l'image dans la salle sous le gouffre, et c'est ce qui rendait les
		// cavites illisibles meme une fois eclairees.
		//
		// La couleur d'une paroi est celle de sa ROCHE, et la lithologie la
		// porte deja : calcaire creme, granite gris rose, basalte sombre. Meme
		// doctrine que partout ailleurs dans cette passe -- la roche decide.
		if (bAvecRoche && Rules.RockColourFadeM > 0.0f)
		{
			const double Z = Mesh.Positions[I].Z / WorldseedMetersToCm;
			const double Profondeur = Density.SurfaceHeightM(X, Y) - Z;
			if (Profondeur > 0.0)
			{
				// --- LA ROCHE SE LIT EN TROIS DIMENSIONS --------------------
				//
				// C'ETAIT UNE ROCHE PAR COLONNE, donc une paroi d'une seule
				// teinte du sommet au pied. Or un vrai sous-sol est FEUILLETE,
				// et c'est exactement ce qui donne au Grand Canyon ses rayures :
				// les bancs durs font les corniches, les tendres les talus, et
				// chacun a sa couleur.
				//
				// La serie ne recouvre que le SEDIMENTAIRE : sur du granite ou
				// du basalte, la garde de durete ne passe pas et l'on retombe
				// sur la roche 2D, c'est-a-dire le comportement d'avant a
				// l'identique. Une donnee absente doit rester sans effet.
				uint8 Id = Lithology.Id[Cell];
				if (StratRules.IsActive())
				{
					const float DureteSocle = DureteParId.IsValidIndex(Id)
						? DureteParId[Id] : 1.0f;
					if (DureteSocle >= StratRules.SocleHardnessMin
						&& DureteSocle <= StratRules.SocleHardnessMax)
					{
						const int32 Banc = WorldseedStrata::BancAt(
							X, Y, Z, StratRules, WorldSeed);
						if (StratRules.Serie.IsValidIndex(Banc))
						{
							Id = StratRules.Serie[Banc].RockId;
						}
					}
				}
				if (CouleurParRoche.IsValidIndex(Id))
				{
					const float T = FMath::Clamp(
						static_cast<float>(Profondeur) / Rules.RockColourFadeM, 0.0f, 1.0f);
					Teinte = FMath::Lerp(Teinte, CouleurParRoche[Id], T);
				}
			}
		}

		Mesh.Colours[I] = Teinte;
	}
}

void AWorldseedVoxelTerrain::UploadChunk(const FWorldseedChunkKey& Key,
	FWorldseedVoxelChunkState& State)
{
	FWorldseedVoxelJobPtr Job = State.Job;
	State.Job.Reset();

	if (!Job.IsValid())
	{
		return;
	}

	TotalMeshMs += Job->Stats.MeshMs + Job->Stats.NormalMs;
	WorstMeshMs = FMath::Max(WorstMeshMs, Job->Stats.MeshMs + Job->Stats.NormalMs);

	State.Cause = Job->Stats.Cause;
	State.Seeds = Job->Stats.Seeds;
	State.Tris = Job->Stats.Triangles;

	if (!Job->bHasSurface || Job->Mesh.IsEmpty())
	{
		// UN TRAVAIL ANNULE N'EST PAS UN CHUNK VIDE, et les confondre laissait
		// un TROU DEFINITIF dans le sol.
		//
		// Ce test marquait "vide" les trois causes d'echec du mailleur : pas de
		// traversee, travail annule, maillage sorti vide. Seule la premiere est
		// une propriete du monde ; les deux autres sont des accidents. Et comme
		// un chunk marque vide n'est JAMAIS repropose -- la boucle des
		// candidats saute toute cle deja presente -- l'accident devenait
		// permanent. Mesure chez le proprietaire : 2 colonnes sans aucun sol
		// sur 135 chargees, soit 1,5 %, et le sol de fond les masquait en se
		// dessinant un metre plus bas, ce qui donnait les "zones bizarres".
		// On tombait au travers.
		if (Job->Stats.Cause == FWorldseedVoxelStats::ECause::Annule)
		{
			// Ni maillage ni marque : la passe suivante le reprendra.
			++AbandonedChunks;
			return;
		}

		// AUCUNE SURFACE : on s'en souvient au lieu de l'oublier. Sans cette
		// marque, la meme boite serait relancee a chaque passe du minuteur.
		State.bEmpty = true;
		++EmptyChunks;
		if (Job->Stats.Cause == FWorldseedVoxelStats::ECause::MaillageVide)
		{
			++DegenerateChunks;
		}
		return;
	}

	PaintVertices(Job->Mesh);

	const double DebutUpload = FPlatformTime::Seconds();

	if (!State.Mesh)
	{
		// LE NIVEAU ENTRE DANS LE NOM : sans lui, deux chunks de niveaux
		// differents mais de memes indices porteraient le meme nom de composant.
		const FName Nom(*FString::Printf(TEXT("Voxel_L%d_%d_%d_%d"),
			Key.Niveau, Key.C.X, Key.C.Y, Key.C.Z));
		State.Mesh = NewObject<UProceduralMeshComponent>(this, Nom);
		State.Mesh->SetupAttachment(RootScene);
		State.Mesh->bUseAsyncCooking = true;
		State.Mesh->SetCastShadow(true);
		State.Mesh->bAffectDistanceFieldLighting = false;
		State.Mesh->RegisterComponent();

		if (TerrainMaterial)
		{
			State.Mesh->SetMaterial(0, TerrainMaterial);
		}
	}

	// TOUT CHUNK MAILLE EST SOLIDE, SANS EXCEPTION.
	//
	// Il y avait ici un rayon de collision, plus court que le rayon de
	// chargement : les chunks lointains etaient poses SANS collision, pour
	// economiser la cuisson. Le defaut est qu'il n'existe aucune facon de
	// donner la collision a une section deja creee -- ProceduralMeshComponent
	// n'expose rien de tel -- donc la decision, prise UNE FOIS au televersement,
	// etait definitive. Un chunk pose a plus de 120 m n'en recevait jamais, et
	// le joueur qui marchait jusqu'a lui passait AU TRAVERS DU SOL.
	//
	// Mesure du defaut, sondes verticales tous les dix metres depuis le pion :
	// sol present de 0 a 110 m, PLUS RIEN de 120 a 250 m. La frontiere tombait
	// exactement sur l'ancien rayon.
	//
	// Un chunk qu'on voit est un chunk qu'on peut atteindre, et le rayon de
	// CHARGEMENT borne deja le travail. Si la cuisson coute trop cher, la
	// reponse est de la faire de facon asynchrone, pas de laisser un trou.
	State.Mesh->CreateMeshSection_LinearColor(0, Job->Mesh.Positions,
		Job->Mesh.Triangles, Job->Mesh.Normals, TArray<FVector2D>(),
		Job->Mesh.Colours, TArray<FProcMeshTangent>(), true);

	State.bHasCollision = true;

	const double UploadMs = (FPlatformTime::Seconds() - DebutUpload) * 1000.0;
	TotalUploadMs += UploadMs;
	WorstUploadMs = FMath::Max(WorstUploadMs, UploadMs);
	++UploadCount;

	++BuiltChunks;
	TotalTriangles += Job->Mesh.TriangleCount();

	if (FirstFillSeconds <= 0.0 && BuiltChunks >= 8)
	{
		FirstFillSeconds = FPlatformTime::Seconds() - StartSeconds;
	}
}

void AWorldseedVoxelTerrain::ReleaseChunk(const FWorldseedChunkKey& Key)
{
	FWorldseedVoxelChunkState* const State = Chunks.Find(Key);
	if (!State)
	{
		return;
	}

	if (State->Job.IsValid())
	{
		State->Job->bCancel.store(true, std::memory_order_release);
	}
	if (State->Mesh)
	{
		State->Mesh->DestroyComponent();
	}
	Chunks.Remove(Key);
}

FWorldseedChunkKey AWorldseedVoxelTerrain::KeyForPoint(double X, double Y, double Z) const
{
	// LE NIVEAU SE DEDUIT, IL NE SE SUPPOSE PAS. Le chunk qui couvre un point
	// n-est au niveau le plus fin que si le point est dans le premier anneau ;
	// chercher ailleurs une cle de niveau zero ne trouverait rien, et le filet
	// du joueur conclurait que le sol n-existe pas.
	const FVector PointM(X, Y, Z);
	const FVector OrigineM =
		(StreamingOriginCm() - GetActorLocation()) / WorldseedMetersToCm;
	const int32 Niveau = NiveauEn(PointM, OrigineM);
	const double Cote = CoteM(Niveau);

	return FWorldseedChunkKey{
		FIntVector(
			FMath::FloorToInt(X / Cote),
			FMath::FloorToInt(Y / Cote),
			FMath::FloorToInt(Z / Cote)),
		Niveau };
}

bool AWorldseedVoxelTerrain::FindFlatGround(const FVector2D& AroundM,
	double& OutX, double& OutY, float& OutSurfaceM, float& OutSlopeDeg) const
{
	// Spirale carree autour du point demande : on prend le PREMIER endroit
	// acceptable, donc le plus proche, et non le meilleur du monde.
	constexpr double PasM = 16.0;
	constexpr int32 Anneaux = 24;
	constexpr double SondeM = 6.0;        // ecart pour estimer la pente
	constexpr float PenteMaxDeg = 12.0f;

	auto Convient = [this](double X, double Y, float& Surface, float& PenteDeg) -> bool
	{
		Surface = Density.SurfaceHeightM(X, Y);
		if (Surface < 2.0f)
		{
			return false;   // sous la mer, ou tout juste au bord
		}

		const float HX = Density.SurfaceHeightM(X + SondeM, Y)
			- Density.SurfaceHeightM(X - SondeM, Y);
		const float HY = Density.SurfaceHeightM(X, Y + SondeM)
			- Density.SurfaceHeightM(X, Y - SondeM);
		const float Pente = FMath::Sqrt(HX * HX + HY * HY) / (2.0f * SondeM);
		PenteDeg = FMath::RadiansToDegrees(FMath::Atan(Pente));
		if (PenteDeg > PenteMaxDeg)
		{
			return false;
		}

		// PLEIN SOUS LES PIEDS, sur toute la hauteur d'une galerie typique :
		// un plancher de deux metres au-dessus d'un vide ne tient pas.
		for (double Profondeur = 1.0; Profondeur <= 12.0; Profondeur += 2.0)
		{
			if (Density.At(FVector(X, Y, Surface - Profondeur)) > 0.0)
			{
				return false;
			}
		}
		return true;
	};

	for (int32 Anneau = 0; Anneau <= Anneaux; ++Anneau)
	{
		for (int32 DY = -Anneau; DY <= Anneau; ++DY)
		{
			for (int32 DX = -Anneau; DX <= Anneau; ++DX)
			{
				// Seulement le bord de l'anneau : l'interieur a deja ete vu.
				if (Anneau > 0 && FMath::Abs(DX) != Anneau && FMath::Abs(DY) != Anneau)
				{
					continue;
				}

				const double X = AroundM.X + DX * PasM;
				const double Y = AroundM.Y + DY * PasM;

				float Surface = 0.0f;
				float PenteDeg = 0.0f;
				if (Convient(X, Y, Surface, PenteDeg))
				{
					OutX = X;
					OutY = Y;
					OutSurfaceM = Surface;
					OutSlopeDeg = PenteDeg;
					return true;
				}
			}
		}
	}
	return false;
}

void AWorldseedVoxelTerrain::HoldOrReleasePlayer()
{
	if (!bHoldPlayer || !bWorldReady)
	{
		return;
	}

	UWorld* const W = GetWorld();
	APawn* const Pawn = W ? UGameplayStatics::GetPlayerPawn(W, 0) : nullptr;
	if (!Pawn)
	{
		// Le pion n'existe pas encore : le mode de jeu le posera, et la passe
		// suivante s'en occupera.
		return;
	}

	UCharacterMovementComponent* const Move =
		Pawn->FindComponentByClass<UCharacterMovementComponent>();

	const FVector PosCm = Pawn->GetActorLocation() - GetActorLocation();
	double X = PosCm.X / WorldseedMetersToCm;
	double Y = PosCm.Y / WorldseedMetersToCm;
	float SurfaceM = Density.SurfaceHeightM(X, Y);

	// --- LE FILET EST PERMANENT, IL NE JOUE PAS QU'UNE FOIS ------------------
	//
	// Cette fonction sortait immediatement des que bPlayerReleased etait pose,
	// donc apres la mise en place initiale il n'y avait PLUS AUCUN filet. Un
	// pion qui passe sous la bande de terrain tombe alors indefiniment :
	// mesure, -4745 m a quarante metres par seconde, et rien ne le ramene.
	//
	// LE CRITERE N'EST PAS ARBITRAIRE, IL VIENT DE LA BANDE ELLE-MEME. Le
	// terrain n'est maille que de la surface a bandeM en dessous ; plus bas,
	// aucun chunk n'existe et il ne peut RIEN y avoir. Un pion qu'on y trouve
	// n'est pas en train de tomber dans un trou, il est HORS DU MONDE.
	if (bPlayerReleased)
	{
		const double SousM = SurfaceM - PosCm.Z / WorldseedMetersToCm;
		if (SousM <= DensityRules.BandDepthM + PlayerRescueMarginM)
		{
			return;
		}

		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] voxel : joueur a %.0f m SOUS la bande de terrain ")
			TEXT("-- hors du monde, on le remonte"),
			SousM - DensityRules.BandDepthM);

		// On rearme la mise en place initiale, qui sait deja poser le pion et
		// ne le relacher qu'une fois le chunk SOLIDE. Refaire ce travail ici
		// serait le dupliquer, et les deux moities divergeraient.
		bPlayerReleased = false;
		bPlayerHeld = false;
	}

	if (!bPlayerHeld && bTeleportPose)
	{
		// UNE DESTINATION DEMANDEE NE SE CORRIGE PAS. FindFlatGround fouille un
		// voisinage pour trouver du plat : tres bien au depart, ou l'endroit
		// n'a aucune importance, mais il deplacerait de plusieurs centaines de
		// metres un joueur venu voir UN sommet precis.
		X = TeleportXYM.X;
		Y = TeleportXYM.Y;
		SurfaceM = Density.SurfaceHeightM(X, Y);
	}
	else if (!bPlayerHeld)
	{
		// ON CHOISIT L'ENDROIT, ON NE SE CONTENTE PAS DE CELUI DU PlayerStart.
		// Il faut du plat, de l'emerge, et du plein dessous : une colonne sur
		// huit porte une galerie, et naitre au-dessus revient a tomber dedans.
		float PenteDeg = 0.0f;
		double FX = X;
		double FY = Y;
		float FSurface = SurfaceM;
		if (FindFlatGround(FVector2D(X, Y), FX, FY, FSurface, PenteDeg))
		{
			X = FX;
			Y = FY;
			SurfaceM = FSurface;
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : sol plat trouve a (%.0f, %.0f) m, ")
				TEXT("altitude %.1f m, pente %.1f deg"),
				X, Y, SurfaceM, PenteDeg);
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] voxel : aucun sol plat autour du depart, ")
				TEXT("pose sur place"));
		}
	}

	// Marge au-dessus du relief : le deplacement 3D peut avoir remonte la
	// surface, et on ne veut pas naitre a l'interieur de la roche.
	const double PoseZCm = GetActorLocation().Z
		+ (SurfaceM + DensityRules.OverhangAmplitudeM + 3.0) * WorldseedMetersToCm;

	if (!bPlayerHeld)
	{
		Pawn->SetActorLocation(
			FVector(GetActorLocation().X + X * WorldseedMetersToCm,
				GetActorLocation().Y + Y * WorldseedMetersToCm, PoseZCm),
			false, nullptr, ETeleportType::TeleportPhysics);

		if (Move)
		{
			// EN VOL, PAS EN CHUTE : c'est le seul mode qui ne consomme pas la
			// gravite. Sans lui le pion descend pendant qu'on maille, et comme
			// c'est lui qui donne l'origine de la diffusion, il emmene la
			// fenetre de chunks avec lui.
			Move->SetMovementMode(MOVE_Flying);
			Move->Velocity = FVector::ZeroVector;
		}

		bPlayerHeld = true;
		bTeleportPose = false;
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] voxel : joueur tenu a (%.0f, %.0f) m, z %.0f cm, ")
			TEXT("surface %.1f m"),
			X, Y, PoseZCm, SurfaceM);
		return;
	}

	// --- relacher, quand le sol existe VRAIMENT -----------------------------
	const FWorldseedChunkKey Key = KeyForPoint(X, Y, SurfaceM);
	const FWorldseedVoxelChunkState* const State = Chunks.Find(Key);
	if (!State || !State->bHasCollision || State->Job.IsValid())
	{
		// On maintient la position : un pion en vol derive s'il a de l'inertie.
		if (Move)
		{
			Move->Velocity = FVector::ZeroVector;
		}
		return;
	}

	if (Move)
	{
		Move->SetMovementMode(MOVE_Falling);
	}
	bPlayerReleased = true;

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] voxel : joueur rendu a la gravite, chunk %d,%d,%d ")
		TEXT("de niveau %d solide"),
		Key.C.X, Key.C.Y, Key.C.Z, Key.Niveau);
}

int32 AWorldseedVoxelTerrain::TravauxEnVol() const
{
	int32 N = 0;
	for (const TPair<FWorldseedChunkKey, FWorldseedVoxelChunkState>& Pair : Chunks)
	{
		if (Pair.Value.Job.IsValid()) { ++N; }
	}
	return N;
}

FString AWorldseedVoxelTerrain::ReportState() const
{
	int32 EnVol = 0;
	int32 AvecCollision = 0;
	int32 ParNiveau[5] = { 0, 0, 0, 0, 0 };
	int32 AvecTransition = 0;
	for (const TPair<FWorldseedChunkKey, FWorldseedVoxelChunkState>& Pair : Chunks)
	{
		if (Pair.Value.Job.IsValid()) { ++EnVol; }
		if (Pair.Value.bHasCollision) { ++AvecCollision; }
		if (Pair.Value.Masque != 0) { ++AvecTransition; }
		++ParNiveau[FMath::Clamp(Pair.Key.Niveau, 0, 4)];
	}

	// LA REPARTITION PAR NIVEAU EST CE QUI DIT SI LES ANNEAUX MORDENT. Le compte
	// total seul ne le dit pas : il baisse aussi quand le rayon baisse, et l'on
	// croirait a un gain des anneaux la ou l'on n'a fait que voir moins loin.
	if (NiveauMax > 0)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] voxel : chunks par niveau  %d / %d / %d / %d / %d")
			TEXT("  |  %d portent une face de transition"),
			ParNiveau[0], ParNiveau[1], ParNiveau[2], ParNiveau[3], ParNiveau[4],
			AvecTransition);
	}

	const double Moyenne = (BuiltChunks + EmptyChunks) > 0
		? TotalMeshMs / (BuiltChunks + EmptyChunks) : 0.0;

	const FString Resume = FString::Printf(
		TEXT("%d chunks suivis (%d mailles, %d vides, %d en vol, %d avec collision)  |  ")
		TEXT("%d triangles  |  maillage %.2f ms/chunk, %.2f au pire  |  ")
		TEXT("PASSE DE DIFFUSION %.2f ms en moyenne, %.2f au pire  |  ")
		TEXT("TELEVERSEMENT sur le fil de jeu %.2f ms/chunk, %.2f au pire, ")
		TEXT("%.1f s cumulees  |  premier remplissage %.1f s"),
		Chunks.Num(), BuiltChunks, EmptyChunks, EnVol, AvecCollision,
		TotalTriangles, Moyenne, WorstMeshMs,
		(UpdateCount > 0) ? TotalUpdateMs / UpdateCount : 0.0, WorstUpdateMs,
		(UploadCount > 0) ? TotalUploadMs / UploadCount : 0.0, WorstUploadMs,
		TotalUploadMs / 1000.0, FirstFillSeconds);

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] voxel : %s"), *Resume);
	return Resume;
}


// ------------------------------------------------------- aller, et savoir ou

void AWorldseedVoxelTerrain::TeleporterJoueur(double XMetres, double YMetres)
{
	if (!bWorldReady)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] aller : le monde n'est pas encore charge"));
		return;
	}

	TeleportXYM = FVector2D(XMetres, YMetres);
	bTeleportPose = true;

	// ON REARME LA MISE EN PLACE, ON NE DEPLACE PAS LE PION A LA MAIN. Elle
	// sait tenir en vol, attendre la collision et relacher ; la dupliquer ici
	// ferait deux codes a maintenir pour une seule regle.
	bPlayerHeld = false;
	bPlayerReleased = false;

	const float SurfaceM = Density.SurfaceHeightM(XMetres, YMetres);
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] aller : (%.0f, %.0f) m, surface %.0f m -- ")
		TEXT("le joueur est tenu en vol jusqu'a ce que le sol soit solide"),
		XMetres, YMetres, SurfaceM);

	HoldOrReleasePlayer();
}

FString AWorldseedVoxelTerrain::OuSuisJe() const
{
	UWorld* const W = GetWorld();
	APawn* const Pawn = W ? UGameplayStatics::GetPlayerPawn(W, 0) : nullptr;
	if (!Pawn || !bWorldReady)
	{
		return TEXT("pas de joueur, ou monde pas encore charge");
	}

	const FVector PosCm = Pawn->GetActorLocation() - GetActorLocation();
	const double X = PosCm.X / WorldseedMetersToCm;
	const double Y = PosCm.Y / WorldseedMetersToCm;
	const double Z = PosCm.Z / WorldseedMetersToCm;
	const float SurfaceM = Density.SurfaceHeightM(X, Y);

	// La roche se lit au PLUS PROCHE VOISIN : un identifiant est une categorie,
	// et interpoler entre du granite et du calcaire donnerait du gres.
	FString Roche = TEXT("inconnue");
	if (Lithology.IsValid(Geometry.CellCount()))
	{
		const double U = FMath::Frac((X / Geometry.WidthM()) + 0.5);
		const double V = FMath::Clamp((Y / Geometry.HeightM) + 0.5, 0.0, 1.0);
		const int32 I = FMath::Clamp(FMath::RoundToInt(U * Geometry.NX),
			0, Geometry.NX - 1);
		const int32 J = FMath::Clamp(FMath::RoundToInt(V * Geometry.NY),
			0, Geometry.NY - 1);
		const uint8 Id = Lithology.Id[J * Geometry.NX + I];
		Roche = NomParRoche.IsValidIndex(Id) ? NomParRoche[Id]
			: FString::Printf(TEXT("roche %d"), Id);
	}

	// La pente se mesure sur le champ lui-meme, pas sur la grille : c'est le
	// relief qu'on a REELLEMENT sous les pieds, deplacement 3D compris.
	const double Pas = 4.0;
	const double DX = Density.SurfaceHeightM(X + Pas, Y)
		- Density.SurfaceHeightM(X - Pas, Y);
	const double DY = Density.SurfaceHeightM(X, Y + Pas)
		- Density.SurfaceHeightM(X, Y - Pas);
	const double PenteDeg = FMath::RadiansToDegrees(
		FMath::Atan(FMath::Sqrt(DX * DX + DY * DY) / (2.0 * Pas)));

	const FString Resume = FString::Printf(
		TEXT("(%.0f, %.0f) m -- altitude %.0f m, surface %.0f m (%+.0f m), ")
		TEXT("pente %.0f deg, roche %s"),
		X, Y, Z, SurfaceM, Z - SurfaceM, PenteDeg, *Roche);

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] ou : %s"), *Resume);
	return Resume;
}

FString AWorldseedVoxelTerrain::LieuxRemarquables() const
{
	if (!bWorldReady || HeightsM.Num() != Geometry.CellCount())
	{
		return TEXT("monde pas encore charge");
	}

	const bool bRoche = Lithology.IsValid(Geometry.CellCount());

	int32 Sommet = INDEX_NONE;
	float ZMax = -1e9f;
	int32 Dure = INDEX_NONE;
	float ZDure = -1e9f;
	int32 Tendre = INDEX_NONE;
	float ZTendre = 1e9f;
	int32 Cote = INDEX_NONE;

	for (int32 I = 0; I < HeightsM.Num(); ++I)
	{
		const float Z = HeightsM[I];
		if (Z > ZMax) { ZMax = Z; Sommet = I; }
		if (Z <= 0.0f) { continue; }
		if (Cote == INDEX_NONE && Z > 2.0f && Z < 12.0f) { Cote = I; }

		if (!bRoche) { continue; }
		const uint8 R = Lithology.Id[I];

		// Le contraste se lit entre la roche qui PORTE les hauteurs et celle
		// qui reste en plaine -- c'est tout l'objet de l'erosion differentielle.
		if (NomParRoche.IsValidIndex(R))
		{
			if (Z > ZDure && R == Lithology.Id[Sommet]) { ZDure = Z; Dure = I; }
			if (Z < ZTendre && Z > 20.0f && R != Lithology.Id[Sommet])
			{
				ZTendre = Z; Tendre = I;
			}
		}
	}

	FString Sortie;
	auto Ligne = [&](const TCHAR* Nom, int32 I, const TCHAR* Pourquoi)
	{
		if (I == INDEX_NONE) { return; }
		const double X = (static_cast<double>(I % Geometry.NX) / Geometry.NX - 0.5)
			* Geometry.WidthM();
		const double Y = (static_cast<double>(I / Geometry.NX) / Geometry.NY - 0.5)
			* Geometry.HeightM;
		const uint8 R = bRoche ? Lithology.Id[I] : 0;
		const FString Roche = (bRoche && NomParRoche.IsValidIndex(R))
			? NomParRoche[R] : FString(TEXT("?"));
		const FString L = FString::Printf(
			TEXT("Worldseed.Aller %.0f %.0f   %-18s %5.0f m, %-10s -- %s"),
			X, Y, Nom, HeightsM[I], *Roche, Pourquoi);
		UE_LOG(LogTemp, Log, TEXT("[Worldseed] lieu : %s"), *L);
		Sortie += L + LINE_TERMINATOR;
	};

	Ligne(TEXT("le sommet"), Sommet, TEXT("l'amplitude du relief"));
	Ligne(TEXT("roche dure"), Dure, TEXT("le versant que la roche tient"));
	Ligne(TEXT("roche tendre"), Tendre, TEXT("la plaine decapee, a comparer"));
	Ligne(TEXT("la cote"), Cote, TEXT("le fond marin et l'eau"));

	// LES ARCHES, ELLES, SONT CONSERVEES PAR LE RESEAU. C'est l'arbitrage B8 --
	// la sortie de la passe macro est interrogeable au runtime -- et c'est ce
	// qui permet d'y aller sans relire un journal. Les bouches, gouffres et
	// dolines ne le sont pas encore : elles restent au journal de generation.
	for (int32 I = 0; I < CaveNetwork.Arches.Num(); ++I)
	{
		const FWorldseedCaveArch& A = CaveNetwork.Arches[I];
		const FString L = FString::Printf(
			TEXT("Worldseed.Aller %.0f %.0f   arche %-12d %5.0f m, ")
			TEXT("ouverture %.0f m sous un pont, lame de %.0f m"),
			A.CentreM.X, A.CentreM.Y, I + 1, A.CentreM.Z,
			2.6f * A.RayonM, A.EpaisseurM);
		UE_LOG(LogTemp, Log, TEXT("[Worldseed] lieu : %s"), *L);
		Sortie += L + LINE_TERMINATOR;
	}

	if (CaveNetwork.IsValid())
	{
		Sortie += FString::Printf(
			TEXT("%d chambres dans le monde ; les bouches, gouffres et dolines ")
			TEXT("sont au journal de generation.") LINE_TERMINATOR,
			CaveNetwork.Chambers.Num());
	}

	// LES TABLES SONT INTROUVABLES AU HASARD, et c'est la raison d'etre de ces
	// lignes : elles couvrent environ un pour cent des terres sur 64 x 32 km.
	// Meme motif que pour les arches -- une forme qu'on ne sait pas trouver
	// n'existe pas pour le joueur.
	for (int32 I = 0; I < Tables.Num() && I < 10; ++I)
	{
		const FWorldseedPlateauSite& S = Tables[I];
		const FString L = FString::Printf(
			TEXT("Worldseed.Aller %.0f %.0f   table %-12d sommet %5.0f m, ")
			TEXT("paroi de %.0f m"),
			S.CentreM.X, S.CentreM.Y, I + 1, S.AltitudeM, S.EscarpementM);
		UE_LOG(LogTemp, Log, TEXT("[Worldseed] lieu : %s"), *L);
		Sortie += L + LINE_TERMINATOR;
	}

	return Sortie;
}

