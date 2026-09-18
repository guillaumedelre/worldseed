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

bool AWorldseedVoxelTerrain::LoadWorld()
{
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

	Density.Init(Geometry, HeightsM, 1.0f, WorldSeed, DensityRules);
	bWorldReady = Density.IsValid();

	if (!bWorldReady)
	{
		UE_LOG(LogTemp, Error, TEXT("[Worldseed] voxel : champ de densite invalide"));
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] voxel : chunks de %.0f m, voxel %.2f m, rayon %.0f m ")
		TEXT("(collision %.0f m), %d travaux simultanes"),
		ChunkSideM, DensityRules.VoxelSizeM, LoadRadiusM, CollisionRadiusM,
		MaxJobsInFlight);

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

	Async(EAsyncExecution::ThreadPool, [Job, Champ, VoxelSizeM]()
	{
		if (!Job->bCancel.load(std::memory_order_acquire))
		{
			Job->bHasSurface = WorldseedVoxelChunk::Build(
				*Champ, Job->BoundsM, VoxelSizeM, Job->Mesh, Job->Stats,
				[Job]() { return Job->bCancel.load(std::memory_order_acquire); });
		}

		// EN DERNIER, ET EN LIBERATION : tout ce qui precede doit etre visible
		// du fil de jeu avant qu'il ne voie ce drapeau.
		Job->bDone.store(true, std::memory_order_release);
	});
}

void AWorldseedVoxelTerrain::PaintVertices(FWorldseedVoxelMesh& Mesh) const
{
	const int32 Count = Mesh.Positions.Num();
	Mesh.Colours.SetNumUninitialized(Count);

	const bool bHasBiomes = (Biomes.Index.Num() == Geometry.CellCount());
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

		const EWorldseedBiome Biome =
			static_cast<EWorldseedBiome>(Biomes.Index[Row * Geometry.NX + Col]);
		Mesh.Colours[I] = WorldseedBiomes::Colour(Biome);
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

	if (!Job->bHasSurface || Job->Mesh.IsEmpty())
	{
		// AUCUNE SURFACE : on s'en souvient au lieu de l'oublier. Sans cette
		// marque, la meme boite serait relancee a chaque passe du minuteur.
		State.bEmpty = true;
		++EmptyChunks;
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

	const FVector CentreM = ChunkBoundsM(Key).GetCenter();
	const FVector OriginM = (StreamingOriginCm() - GetActorLocation())
		/ WorldseedMetersToCm;
	const bool bCollide = FVector::Dist(CentreM, OriginM) <= CollisionRadiusM;

	State.Mesh->CreateMeshSection_LinearColor(0, Job->Mesh.Positions,
		Job->Mesh.Triangles, Job->Mesh.Normals, TArray<FVector2D>(),
		Job->Mesh.Colours, TArray<FProcMeshTangent>(), bCollide);

	State.bHasCollision = bCollide;

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
	if (!bHoldPlayer || bPlayerReleased || !bWorldReady)
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

	if (!bPlayerHeld)
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
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] voxel : joueur tenu a z %.0f cm, surface %.1f m"),
			PoseZCm, SurfaceM);
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
