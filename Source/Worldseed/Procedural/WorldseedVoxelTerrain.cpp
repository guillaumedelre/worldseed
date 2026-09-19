// Worldseed - terrain voxel : diffusion des chunks autour du joueur.

#include "Procedural/WorldseedVoxelTerrain.h"

#include "Procedural/WorldseedGameInstance.h"
#include "Procedural/WorldseedPipeline.h"

#include "Async/Async.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
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
	const FWorldseedCaveNetwork& InCaves, const FWorldseedLithology& InLithology)
{
	CaveNetwork = InCaves;
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

	if (UWorld* const W = GetWorld())
	{
		W->GetTimerManager().SetTimer(UpdateTimer, this,
			&AWorldseedVoxelTerrain::UpdateChunks,
			FMath::Max(UpdatePeriod, 0.05f), true);
	}

	// --- LA TOURNEE PHOTO SE DECLENCHE EN LIGNE DE COMMANDE ------------------
	//
	// PAS PAR UN APPEL EXTERNE, ET C'EST TOUT L'INTERET. Piloter la prise de
	// vue depuis l'exterieur suppose un lien d'outillage vivant ; quand il
	// tombe -- ce qui arrive des que l'editeur est tue et relance plusieurs
	// fois, donc a chaque compilation -- on redevient aveugle. Lue au
	// demarrage, l'option rend la verification visuelle possible dans tous les
	// cas, y compris en build final.
	if (FParse::Param(FCommandLine::Get(), TEXT("WorldseedPhotos")))
	{
		bQuitterApresTournee =
			FParse::Param(FCommandLine::Get(), TEXT("WorldseedQuitter"));

		// LES FALAISES D'ABORD : c'est ce que la passe littorale vient de
		// creer, et c'est la condition des arches marines. Les arches
		// ensuite, pour voir si elles ont gagne un endroit d'ou se regarder.
		Tournee.Reset();
		EtapeTournee = INDEX_NONE;
		TourneeDesFalaises(6);
		TourneeDesArches();
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
	for (TPair<FIntVector, FWorldseedVoxelChunkState>& Pair : Chunks)
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

FBox AWorldseedVoxelTerrain::ChunkBoundsM(const FIntVector& Key) const
{
	const double Side = ChunkSideM;
	const FVector Min(Key.X * Side, Key.Y * Side, Key.Z * Side);
	return FBox(Min, Min + FVector(Side, Side, Side));
}

FVector AWorldseedVoxelTerrain::ChunkCentreCm(const FIntVector& Key) const
{
	const FBox B = ChunkBoundsM(Key);
	const FVector CentreM = B.GetCenter();
	return GetActorLocation() + CentreM * WorldseedMetersToCm;
}

// ---------------------------------------------------------------- diffusion

void AWorldseedVoxelTerrain::UpdateChunks()
{
	if (!bWorldReady)
	{
		return;
	}

	// LA TOURNEE PHOTO AVANCE SUR CE MEME MINUTEUR, et c'est ce qui la rend
	// possible sans outillage externe : elle a besoin d'attendre que les
	// chunks arrivent, donc d'un fil du temps -- exactement ce que ce minuteur
	// fournit deja.
	AvancerTournee();

	const FVector OriginCm = StreamingOriginCm() - GetActorLocation();
	const FVector OriginM = OriginCm / WorldseedMetersToCm;
	const double Side = ChunkSideM;

	// --- 1. relacher ce qui est trop loin -----------------------------------
	TArray<FIntVector> ARelacher;
	for (const TPair<FIntVector, FWorldseedVoxelChunkState>& Pair : Chunks)
	{
		const FVector CentreM = ChunkBoundsM(Pair.Key).GetCenter();
		if (FVector::Dist(CentreM, OriginM) > UnloadRadiusM)
		{
			ARelacher.Add(Pair.Key);
		}
	}
	for (const FIntVector& Key : ARelacher)
	{
		ReleaseChunk(Key);
	}

	// --- 2. recolter les travaux termines -----------------------------------
	int32 Televerses = 0;
	for (TPair<FIntVector, FWorldseedVoxelChunkState>& Pair : Chunks)
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
	for (const TPair<FIntVector, FWorldseedVoxelChunkState>& Pair : Chunks)
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
		TArray<FIntVector> ARelancer;
		for (const TPair<FIntVector, FWorldseedVoxelChunkState>& Pair : Chunks)
		{
			const FWorldseedVoxelChunkState& S = Pair.Value;
			if (!S.bEmpty && !S.Mesh && !S.Job.IsValid())
			{
				ARelancer.Add(Pair.Key);
			}
		}
		for (const FIntVector& Key : ARelancer)
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
	const int32 Portee = FMath::CeilToInt(LoadRadiusM / Side);
	const int32 CX0 = FMath::FloorToInt(OriginM.X / Side);
	const int32 CY0 = FMath::FloorToInt(OriginM.Y / Side);

	struct FCandidat
	{
		FIntVector Key;
		double DistM;
	};
	TArray<FCandidat> Candidats;

	for (int32 DY = -Portee; DY <= Portee; ++DY)
	{
		for (int32 DX = -Portee; DX <= Portee; ++DX)
		{
			const int32 CX = CX0 + DX;
			const int32 CY = CY0 + DY;

			const double MinX = CX * Side;
			const double MinY = CY * Side;

			float SurfaceMin = 0.0f;
			float SurfaceMax = 0.0f;
			Density.SurfaceRangeM(MinX, MinY, MinX + Side, MinY + Side,
				SurfaceMin, SurfaceMax);

			const int32 ZBas = FMath::FloorToInt(
				(SurfaceMin - DensityRules.BandDepthM) / Side);
			const int32 ZHaut = FMath::FloorToInt(SurfaceMax / Side);

			for (int32 CZ = ZBas; CZ <= ZHaut; ++CZ)
			{
				const FIntVector Key(CX, CY, CZ);
				if (Chunks.Contains(Key))
				{
					continue;
				}

				const FVector CentreM = ChunkBoundsM(Key).GetCenter();
				const double DistM = FVector::Dist(CentreM, OriginM);
				if (DistM > LoadRadiusM)
				{
					continue;
				}

				Candidats.Add({ Key, DistM });
			}
		}
	}

	// Les plus proches d'abord : c'est ce que le joueur voit en premier.
	Candidats.Sort([](const FCandidat& A, const FCandidat& B)
	{
		return A.DistM < B.DistM;
	});

	for (const FCandidat& C : Candidats)
	{
		if (EnVol >= MaxJobsInFlight)
		{
			break;
		}
		LaunchJob(C.Key);
		++EnVol;
	}

	HoldOrReleasePlayer();
}

void AWorldseedVoxelTerrain::LaunchJob(const FIntVector& Key)
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
	const float VoxelSizeM = DensityRules.VoxelSizeM;

	// L'EXTRACTION SE FAIT ICI, SUR LE FIL DE JEU, ET UNE SEULE FOIS. Le chunk
	// est elargi du rayon de raccordement : une capsule qui ne touche pas la
	// boite peut quand meme arrondir une arete a l'interieur.
	if (CaveNetwork.IsValid())
	{
		CaveNetwork.Query(Job->BoundsM.ExpandBy(DensityRules.CaveBlendM + 4.0f), Job->Caves);
	}

	Async(EAsyncExecution::ThreadPool, [Job, Champ, VoxelSizeM]()
	{
		if (!Job->bCancel.load(std::memory_order_acquire))
		{
			Job->bHasSurface = WorldseedVoxelChunk::Build(
				*Champ, &Job->Caves, Job->BoundsM, VoxelSizeM, Job->Mesh, Job->Stats,
				[Job]() { return Job->bCancel.load(std::memory_order_acquire); });
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
		const FIntVector Key(CX, CY, CZ);
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
				const uint8 Id = Lithology.Id[Cell];
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

void AWorldseedVoxelTerrain::UploadChunk(const FIntVector& Key,
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

	if (!State.Mesh)
	{
		const FName Nom(*FString::Printf(TEXT("Voxel_%d_%d_%d"),
			Key.X, Key.Y, Key.Z));
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

	++BuiltChunks;
	TotalTriangles += Job->Mesh.TriangleCount();

	if (FirstFillSeconds <= 0.0 && BuiltChunks >= 8)
	{
		FirstFillSeconds = FPlatformTime::Seconds() - StartSeconds;
	}
}

void AWorldseedVoxelTerrain::ReleaseChunk(const FIntVector& Key)
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

FIntVector AWorldseedVoxelTerrain::KeyForPoint(double X, double Y, double Z) const
{
	return FIntVector(
		FMath::FloorToInt(X / ChunkSideM),
		FMath::FloorToInt(Y / ChunkSideM),
		FMath::FloorToInt(Z / ChunkSideM));
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
	const FIntVector Key = KeyForPoint(X, Y, SurfaceM);
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
		TEXT("[Worldseed] voxel : joueur rendu a la gravite, chunk %d,%d,%d solide"),
		Key.X, Key.Y, Key.Z);
}

FString AWorldseedVoxelTerrain::ReportState() const
{
	int32 EnVol = 0;
	int32 AvecCollision = 0;
	for (const TPair<FIntVector, FWorldseedVoxelChunkState>& Pair : Chunks)
	{
		if (Pair.Value.Job.IsValid()) { ++EnVol; }
		if (Pair.Value.bHasCollision) { ++AvecCollision; }
	}

	const double Moyenne = (BuiltChunks + EmptyChunks) > 0
		? TotalMeshMs / (BuiltChunks + EmptyChunks) : 0.0;

	const FString Resume = FString::Printf(
		TEXT("%d chunks suivis (%d mailles, %d vides, %d en vol, %d avec collision)  |  ")
		TEXT("%d triangles  |  %.2f ms/chunk en moyenne, %.2f au pire  |  ")
		TEXT("premier remplissage %.1f s"),
		Chunks.Num(), BuiltChunks, EmptyChunks, EnVol, AvecCollision,
		TotalTriangles, Moyenne, WorstMeshMs, FirstFillSeconds);

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

	return Sortie;
}

// -------------------------------------------------------- la tournee photo

void AWorldseedVoxelTerrain::Photographier(double XMetres, double YMetres,
	const FString& Nom)
{
	FWorldseedPhotoStop Etape;
	Etape.Nom = Nom.IsEmpty() ? TEXT("vue") : Nom;
	Etape.CibleM = FVector(XMetres, YMetres, Density.SurfaceHeightM(XMetres, YMetres));
	Etape.DepuisM = FVector2D(1.0, 0.0);
	Tournee.Add(Etape);

	if (EtapeTournee == INDEX_NONE)
	{
		EtapeTournee = Tournee.Num() - 1;
		AttenteTournee = 0;
		TeleporterJoueur(
			Etape.CibleM.X + Etape.DepuisM.X * Etape.DistanceM,
			Etape.CibleM.Y + Etape.DepuisM.Y * Etape.DistanceM);
	}
}

int32 AWorldseedVoxelTerrain::TourneeDesArches()
{
	// ELLE AJOUTE, ELLE NE REMET PAS A ZERO. Premiere version : un Reset en
	// tete, qui effacait en silence les etapes deja posees par la tournee des
	// falaises -- cinq falaises trouvees, journalisees, et aucune photo. Une
	// fonction qui construit une liste ne doit pas decider a la place de son
	// appelant ce qu'il advient de ce qui s'y trouvait.
	const int32 Depart = Tournee.Num();

	for (int32 I = 0; I < CaveNetwork.Arches.Num(); ++I)
	{
		const FWorldseedCaveArch& A = CaveNetwork.Arches[I];

		// DEUX VUES, PARCE QU'ELLES REPONDENT A DEUX QUESTIONS DIFFERENTES.
		// La vue dans l'axe dit si le trou traverse ; la vue large dit si le
		// PAYSAGE existe -- des fentes de cinquante metres devraient se voir
		// de loin, et si elles ne se voient pas, le probleme n'est pas
		// l'arche.
		if (I < 3)
		{
			FWorldseedPhotoStop Large;
			Large.Nom = FString::Printf(TEXT("paysage%02d"), I + 1);
			Large.CibleM = A.CentreM;
			Large.DepuisM = A.TraversM.GetSafeNormal();
			Large.DistanceM = 320.0f;
			Large.HauteurM = 170.0f;
			Tournee.Add(Large);
		}

		FWorldseedPhotoStop Etape;
		Etape.Nom = FString::Printf(TEXT("arche%02d"), I + 1);
		Etape.CibleM = A.CentreM;

		// ON REGARDE DANS L'AXE DU PERCEMENT, sans quoi on photographie une
		// paroi pleine et l'on conclut a tort que l'arche n'existe pas.
		Etape.DepuisM = A.TraversM.GetSafeNormal();
		Etape.DistanceM = FMath::Max(70.0f, A.EpaisseurM * 1.6f);
		Tournee.Add(Etape);
	}

	if (Tournee.Num() > 0 && EtapeTournee == INDEX_NONE)
	{
		EtapeTournee = 0;
		AttenteTournee = 0;
		const FWorldseedPhotoStop& E = Tournee[0];
		TeleporterJoueur(E.CibleM.X + E.DepuisM.X * E.DistanceM,
			E.CibleM.Y + E.DepuisM.Y * E.DistanceM);
	}

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] photo : %d arches ajoutees a la tournee"),
		Tournee.Num() - Depart);
	return Tournee.Num() - Depart;
}

int32 AWorldseedVoxelTerrain::TourneeDesFalaises(int32 Combien)
{
	if (HeightsM.Num() != Geometry.CellCount())
	{
		return 0;
	}

	const int32 NX = Geometry.NX;
	const int32 NY = Geometry.NY;
	const double MailleM = FMath::Max(Geometry.MetersPerPixel(), 1e-3f);

	// --- LES PLUS HAUTES FALAISES, ET ELLES DOIVENT ETRE PRES DE LA MER ------
	//
	// Un ressaut au fond d'une vallee n'est pas une falaise littorale : ce
	// qu'on veut juger est la paroi qui tombe DANS l'eau, parce que c'est elle
	// qui porte les arches marines et c'est elle que cette passe a creee.
	struct FCandidat
	{
		int32 Cellule = 0;
		float Chute = 0.0f;
		FVector2D VersLeBas = FVector2D::ZeroVector;
	};
	TArray<FCandidat> Candidats;

	for (int32 J = 2; J < NY - 2; ++J)
	{
		for (int32 I = 2; I < NX - 2; ++I)
		{
			const int32 C = J * NX + I;
			const float H = HeightsM[C];
			if (H <= 5.0f) { continue; }

			float Chute = 0.0f;
			FIntPoint Vers = FIntPoint::ZeroValue;
			bool bMerProche = false;

			for (int32 DJ = -2; DJ <= 2; ++DJ)
			{
				for (int32 DI = -2; DI <= 2; ++DI)
				{
					const int32 V = (J + DJ) * NX + (I + DI);
					if (HeightsM[V] <= 0.0f) { bMerProche = true; }
					const float D = H - HeightsM[V];
					if (D > Chute) { Chute = D; Vers = FIntPoint(DI, DJ); }
				}
			}

			if (!bMerProche || Chute < 25.0f) { continue; }

			FCandidat K;
			K.Cellule = C;
			K.Chute = Chute;
			K.VersLeBas = FVector2D(Vers.X, Vers.Y).GetSafeNormal();
			Candidats.Add(K);
		}
	}

	Candidats.Sort([](const FCandidat& A, const FCandidat& B)
	{
		return A.Chute > B.Chute;
	});

	int32 Ajoutees = 0;
	TArray<FVector2D> Prises;
	for (const FCandidat& K : Candidats)
	{
		if (Ajoutees >= Combien) { break; }

		const double X = (static_cast<double>(K.Cellule % NX) / NX - 0.5)
			* Geometry.WidthM();
		const double Y = (static_cast<double>(K.Cellule / NX) / NY - 0.5)
			* Geometry.HeightM;

		// Deux falaises voisines sont la MEME falaise.
		bool bVoisine = false;
		for (const FVector2D& P : Prises)
		{
			if (FVector2D::DistSquared(FVector2D(X, Y), P) < 3000.0 * 3000.0)
			{
				bVoisine = true;
				break;
			}
		}
		if (bVoisine) { continue; }
		Prises.Emplace(X, Y);

		FWorldseedPhotoStop E;
		E.Nom = FString::Printf(TEXT("falaise%02d"), Ajoutees + 1);
		E.CibleM = FVector(X, Y, HeightsM[K.Cellule] * 0.5);

		// ON SE MET DU COTE DE LA MER, sinon on photographie le plateau et la
		// falaise est hors champ -- elle est DERRIERE la camera.
		E.DepuisM = K.VersLeBas;
		// DANS LE RAYON DE CHARGEMENT, SINON ON PHOTOGRAPHIE LE SOL DE FOND.
		// Les chunks ne se batissent que dans 250 m autour du pion ; au-dela
		// c'est la nappe d'horizon qu'on voit, qui fait 512 sommets pour tout
		// le monde -- soit 125 m par maille a 64 km. Mes premieres photos
		// etaient prises a 420-580 m : elles montraient les facettes de CETTE
		// nappe, et aucun travail sur le champ de densite ne pouvait les
		// changer. Une heure perdue faute d'avoir verifie ce que le cadre
		// contenait.
		E.DistanceM = FMath::Clamp(K.Chute * 2.2f, 90.0f, 170.0f);
		E.HauteurM = 10.0f;
		Tournee.Add(E);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] photo : falaise%02d a (%.0f, %.0f) m, ")
			TEXT("sommet %.0f m, chute %.0f m sur %.0f m"),
			Ajoutees + 1, X, Y, HeightsM[K.Cellule], K.Chute, MailleM * 2.0);
		++Ajoutees;
	}

	if (Ajoutees > 0 && EtapeTournee == INDEX_NONE)
	{
		EtapeTournee = 0;
		AttenteTournee = 0;
		const FWorldseedPhotoStop& S = Tournee[0];
		TeleporterJoueur(S.CibleM.X + S.DepuisM.X * S.DistanceM,
			S.CibleM.Y + S.DepuisM.Y * S.DistanceM);
	}
	return Ajoutees;
}

void AWorldseedVoxelTerrain::AvancerTournee()
{
	if (!Tournee.IsValidIndex(EtapeTournee))
	{
		return;
	}

	UWorld* const W = GetWorld();
	APawn* const Pion = W ? UGameplayStatics::GetPlayerPawn(W, 0) : nullptr;
	APlayerController* const PC = W ? UGameplayStatics::GetPlayerController(W, 0) : nullptr;
	if (!Pion || !PC)
	{
		return;
	}

	const FWorldseedPhotoStop& E = Tournee[EtapeTournee];

	// ON VISE A CHAQUE PASSE, PAS UNE SEULE FOIS. Le pion pivote quand il
	// retombe sur le sol, et une orientation posee avant l'atterrissage est
	// perdue sans le moindre signe.
	const FVector CibleCm = GetActorLocation()
		+ FVector(E.CibleM.X, E.CibleM.Y, E.CibleM.Z) * WorldseedMetersToCm;
	FVector OeilCm = Pion->GetActorLocation();
	if (const APlayerCameraManager* const Cam = PC->PlayerCameraManager)
	{
		OeilCm = Cam->GetCameraLocation();
	}
	PC->SetControlRotation((CibleCm - OeilCm).Rotation());

	// LE POINT DE VUE SE TIENT A LA HAUTEUR DE LA CIBLE, PAS A CELLE DU SOL.
	//
	// Premiere version : on calait le pion sur le sol local. Mesure -- l'arche
	// etait a 119 m et le sol du point de vue a 239 ; la camera visait donc
	// cinquante-cinq degres vers le bas et photographiait le dos du
	// personnage. Une arche se regarde DE SON NIVEAU, dans l'axe du
	// percement. On part donc de l'altitude de la cible et l'on ne remonte que
	// si l'on se trouve DANS la roche -- ce que le champ sait dire.
	if (UCharacterMovementComponent* const Move =
		Pion->FindComponentByClass<UCharacterMovementComponent>())
	{
		if (bPlayerReleased)
		{
			Move->SetMovementMode(MOVE_Flying);
			Move->Velocity = FVector::ZeroVector;

			const FVector P = Pion->GetActorLocation() - GetActorLocation();
			const double VX = P.X / WorldseedMetersToCm;
			const double VY = P.Y / WorldseedMetersToCm;

			FWorldseedCaveLocal Local;
			CaveNetwork.Query(FBox(FVector(VX - 30.0, VY - 30.0, E.CibleM.Z - 20.0),
				FVector(VX + 30.0, VY + 30.0, E.CibleM.Z + 240.0)), Local);

			double ZM = E.CibleM.Z + E.HauteurM;
			while (ZM < E.CibleM.Z + 220.0
				&& Density.At(FVector(VX, VY, ZM), &Local) <= 0.0)
			{
				ZM += 2.0;
			}

			FVector Pose = Pion->GetActorLocation();
			Pose.Z = GetActorLocation().Z + (ZM + 2.0) * WorldseedMetersToCm;
			Pion->SetActorLocation(Pose, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	if (!bPlayerReleased)
	{
		// Le sol du point de vue n'est pas encore solide : on attend, et on ne
		// compte pas ce temps -- sinon on declenche avant que le monde existe.
		return;
	}

	++AttenteTournee;

	// LAISSER LE MONDE SE BATIR AVANT DE TIRER. Les chunks arrivent par
	// travaux asynchrones ; une photo prise des l'arrivee montre un paysage
	// troue, et l'on croit a un defaut de generation. Mesure du projet : il
	// faut compter une quinzaine de secondes pour que le rayon se remplisse.
	const int32 TicksAvantPhoto = 60;
	const int32 TicksApresPhoto = TicksAvantPhoto + 12;

	if (AttenteTournee == TicksAvantPhoto)
	{
		const FString Fichier = FPaths::Combine(
			FPaths::ProjectSavedDir(), TEXT("Photos"), E.Nom + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Fichier, false, false);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] photo : %s -- cible (%.0f, %.0f, %.0f) m, ")
			TEXT("depuis %.0f m dans l'axe"),
			*E.Nom, E.CibleM.X, E.CibleM.Y, E.CibleM.Z, E.DistanceM);
	}

	if (AttenteTournee >= TicksApresPhoto)
	{
		++EtapeTournee;
		AttenteTournee = 0;

		if (Tournee.IsValidIndex(EtapeTournee))
		{
			const FWorldseedPhotoStop& S = Tournee[EtapeTournee];
			TeleporterJoueur(S.CibleM.X + S.DepuisM.X * S.DistanceM,
				S.CibleM.Y + S.DepuisM.Y * S.DistanceM);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[Worldseed] photo : tournee terminee"));
			EtapeTournee = INDEX_NONE;
			if (bQuitterApresTournee)
			{
				FPlatformMisc::RequestExit(false);
			}
		}
	}
}
