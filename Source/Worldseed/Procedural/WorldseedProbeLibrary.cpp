// Worldseed - points d'entree de verification, appelables sans lancer le jeu.

#include "Procedural/WorldseedProbeLibrary.h"

#include "Procedural/WorldseedDensity.h"
#include "Procedural/WorldseedGlobe.h"
#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedVoxelChunk.h"


FString UWorldseedProbeLibrary::ProbeGlobe(int32 Seed, float HeightMeters,
	int32 ResolutionY, int32 Frames)
{
	WorldseedPipeline::FResult World;
	FString Error;

	if (!WorldseedPipeline::Generate(Seed, HeightMeters, ResolutionY, World, Error))
	{
		return FString::Printf(TEXT("generation impossible : %s"), *Error);
	}

	WorldseedGlobe::FGlobeSettings Settings;

	// On chronometre la MEME scene sur deux heightfields : l'original et sa
	// reduction. Tout le reste est identique — meme texture, meme nombre de
	// pixels, meme rotation — donc l'ecart ne peut venir que des acces memoire.
	auto Chrono = [&](const TArray<float>& Heights, const FWorldseedGeometry& Geo) -> float
	{
		UTexture2D* Texture = WorldseedGlobe::Render(Heights, Geo, Settings, 512);
		if (!Texture)
		{
			return -1.0f;
		}

		// Une image hors chronometre : elle paie les defauts de cache
		// obligatoires du premier passage, qui ne se reproduisent pas.
		WorldseedGlobe::RenderInto(Texture, Heights, Geo, Settings);

		const int32 Count = FMath::Max(Frames, 1);
		const double Start = FPlatformTime::Seconds();
		for (int32 F = 0; F < Count; ++F)
		{
			// On tourne reellement : garder la meme orientation laisserait le
			// cache chaud sur une seule bande du monde et flatterait la mesure.
			Settings.LongitudeOffsetDeg = 360.0f * F / Count;
			WorldseedGlobe::RenderInto(Texture, Heights, Geo, Settings);
		}
		return static_cast<float>((FPlatformTime::Seconds() - Start) * 1000.0 / Count);
	};

	const float FullMs = Chrono(World.ElevationM, World.Geometry);

	// La reduction que le menu applique desormais.
	constexpr int32 PreviewNX = 1024;
	float PreviewMs = FullMs;
	int32 PreviewCells = World.Geometry.CellCount();

	if (World.Geometry.NX > PreviewNX)
	{
		TArray<float> Reduced;
		WorldseedGrid::Downsample(World.ElevationM, World.Geometry.NX, World.Geometry.NY,
			PreviewNX, PreviewNX / 2, Reduced);

		FWorldseedGeometry ReducedGeo = World.Geometry;
		ReducedGeo.NX = PreviewNX;
		ReducedGeo.NY = PreviewNX / 2;

		PreviewMs = Chrono(Reduced, ReducedGeo);
		PreviewCells = Reduced.Num();
	}

	const float FullMb = World.Geometry.CellCount() * sizeof(float) / (1024.0f * 1024.0f);
	const float PreviewMb = PreviewCells * sizeof(float) / (1024.0f * 1024.0f);

	const FString Summary = FString::Printf(
		TEXT("%dx%d (%.1f Mo) : %.2f ms   ->   reduit %.1f Mo : %.2f ms   (x%.1f plus rapide)"),
		World.Geometry.NX, World.Geometry.NY, FullMb, FullMs, PreviewMb, PreviewMs,
		PreviewMs > 0.0f ? FullMs / PreviewMs : 1.0f);

	UE_LOG(LogTemp, Log, TEXT("[Sonde] %s"), *Summary);
	return Summary;
}


FString UWorldseedProbeLibrary::ProbeVoxel(int32 Seed, float HeightMeters,
	int32 ResolutionY, int32 ChunkSideM, int32 ChunksPerSide)
{
	WorldseedPipeline::FResult World;
	FString Error;

	if (!WorldseedPipeline::Generate(Seed, HeightMeters, ResolutionY, World, Error))
	{
		const FString Message = FString::Printf(TEXT("generation impossible : %s"), *Error);
		UE_LOG(LogTemp, Error, TEXT("[Sonde] %s"), *Message);
		return Message;
	}

	const UWorldseedRules* Rules = WorldseedPipeline::GetRules(Error);
	if (!Rules)
	{
		return FString::Printf(TEXT("regles illisibles : %s"), *Error);
	}

	const FWorldseedDensityRules DensityRules = FWorldseedDensityRules::FromRules(*Rules);

	FWorldseedDensity Density;
	Density.Init(World.Geometry, World.ElevationM, 1.0f, Seed, DensityRules);

	// --- trouver de la TERRE ------------------------------------------------
	// Mailler au-dessus de l'ocean ne couterait rien et ne prouverait rien : il
	// n'y a pas de surface la ou le champ ne change pas de signe. On cherche
	// donc la cellule emergee la plus haute, qui est aussi la plus accidentee
	// donc la plus chere : le releve est ainsi un MAJORANT, pas une moyenne
	// flatteuse.
	const int32 NX = World.Geometry.NX;
	const int32 NY = World.Geometry.NY;
	int32 BestCell = INDEX_NONE;
	float BestH = 0.0f;
	for (int32 Cell = 0; Cell < World.ElevationM.Num(); ++Cell)
	{
		if (World.ElevationM[Cell] > BestH)
		{
			BestH = World.ElevationM[Cell];
			BestCell = Cell;
		}
	}
	if (BestCell == INDEX_NONE)
	{
		return TEXT("aucune terre emergee dans ce monde");
	}

	const double MetresParCellule = World.Geometry.MetersPerPixel();
	const double CentreX = (static_cast<double>(BestCell % NX) - NX * 0.5) * MetresParCellule;
	const double CentreY = (static_cast<double>(BestCell / NX) - NY * 0.5) * MetresParCellule;

	// --- mailler la tuile ---------------------------------------------------
	const double Cote = FMath::Max(ChunkSideM, 4);
	const int32 Colonnes = FMath::Clamp(ChunksPerSide, 1, 16);
	const double Depart = -Cote * Colonnes * 0.5;

	int32 MaillesPleines = 0;
	int32 MaillesVides = 0;
	int32 Triangles = 0;
	int32 Sommets = 0;
	int64 Evaluations = 0;
	int32 Octets = 0;
	double TotalMs = 0.0;
	double PireMs = 0.0;
	double NormalesMs = 0.0;

	FWorldseedVoxelMesh Maillage;
	FWorldseedVoxelStats Stats;

	for (int32 CY = 0; CY < Colonnes; ++CY)
	{
		for (int32 CX = 0; CX < Colonnes; ++CX)
		{
			const double MinX = CentreX + Depart + CX * Cote;
			const double MinY = CentreY + Depart + CY * Cote;

			// La grille 2D dit ou peut se trouver la surface : on ne maille que
			// les etages qu'elle traverse, plus la bande creusable dessous.
			float SurfaceMin = 0.0f;
			float SurfaceMax = 0.0f;
			Density.SurfaceRangeM(MinX, MinY, MinX + Cote, MinY + Cote,
				SurfaceMin, SurfaceMax);

			const double BasM = SurfaceMin - DensityRules.BandDepthM;
			const int32 EtageBas = FMath::FloorToInt(BasM / Cote);
			const int32 EtageHaut = FMath::FloorToInt(SurfaceMax / Cote);

			for (int32 CZ = EtageBas; CZ <= EtageHaut; ++CZ)
			{
				const FBox Boite(
					FVector(MinX, MinY, CZ * Cote),
					FVector(MinX + Cote, MinY + Cote, (CZ + 1) * Cote));

				const double Debut = FPlatformTime::Seconds();
				const bool bPlein = WorldseedVoxelChunk::Build(
					Density, Boite, DensityRules.VoxelSizeM, Maillage, Stats);
				const double Ms = (FPlatformTime::Seconds() - Debut) * 1000.0;

				TotalMs += Ms;
				PireMs = FMath::Max(PireMs, Ms);
				Evaluations += Stats.FieldSamples;
				NormalesMs += Stats.NormalMs;

				if (bPlein)
				{
					++MaillesPleines;
					Triangles += Maillage.TriangleCount();
					Sommets += Maillage.Positions.Num();
					Octets += Maillage.BytesUsed();
				}
				else
				{
					++MaillesVides;
				}
			}
		}
	}

	const int32 Total = MaillesPleines + MaillesVides;
	const double MoyenneMs = Total > 0 ? TotalMs / Total : 0.0;

	UE_LOG(LogTemp, Log,
		TEXT("[Sonde] voxel : %d chunks de %.0f m (%d avec surface, %d vides)  |  ")
		TEXT("voxel %.2f m, bande %.0f m"),
		Total, Cote, MaillesPleines, MaillesVides,
		DensityRules.VoxelSizeM, DensityRules.BandDepthM);
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde] voxel : %.2f ms par chunk en moyenne, %.2f ms au pire  |  ")
		TEXT("%.0f ms au total, dont %.0f ms de normales"),
		MoyenneMs, PireMs, TotalMs, NormalesMs);
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde] voxel : %d triangles, %d sommets, %.2f Mo de maillage  |  ")
		TEXT("%lld evaluations du champ (%.0f par chunk)"),
		Triangles, Sommets, Octets / (1024.0 * 1024.0), Evaluations,
		Total > 0 ? static_cast<double>(Evaluations) / Total : 0.0);

	const FString Summary = FString::Printf(
		TEXT("%d chunks de %.0f m : %.2f ms/chunk (pire %.2f), %d triangles, %.2f Mo"),
		Total, Cote, MoyenneMs, PireMs, Triangles, Octets / (1024.0 * 1024.0));

	UE_LOG(LogTemp, Log, TEXT("[Sonde] %s"), *Summary);
	return Summary;
}
