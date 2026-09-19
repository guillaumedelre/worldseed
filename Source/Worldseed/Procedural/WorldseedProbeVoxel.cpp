// Worldseed - sonde du terrain voxel : ce que coute un chunk.
//
// LES SONDES SONT ECLATEES PAR DOMAINE, une question par fichier. Elles
// vivaient dans un fourre-tout de 1661 lignes ou l'on ne trouvait rien, et ou
// chacune recopiait le meme preambule de vingt lignes -- ce que le depot a
// deja paye : probe_voxel avait OUBLIE SetLithology, et annoncait "le bruit
// est gratuit" en mesurant le monde d'avant. FWorldseedSonde rend l'oubli
// impossible.

#include "Procedural/WorldseedProbeLibrary.h"

#include "Procedural/WorldseedProbeCommun.h"

#include "Procedural/WorldseedFins.h"
#include "Procedural/WorldseedGlobe.h"
#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedPerlin.h"
#include "Procedural/WorldseedVoxelChunk.h"

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

	// LA LITHOLOGIE EST INDISPENSABLE A CETTE MESURE, et l'oublier la rend
	// silencieusement fausse. Sans elle, KarstifiableAt rend 1 partout, le
	// terme de diaclase sort au premier test et le releve de cout ne mesure
	// plus que le champ d'avant -- 2,67 ms/chunk annonces, identiques aux 2,62
	// d'avant l'ajout, ce qui donnait a croire que le Worley etait gratuit.
	Density.SetLithology(World.Lithology, FWorldseedLithologyRules::FromRules(*Rules));

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
				Density, nullptr, Boite, DensityRules.VoxelSizeM, Maillage, Stats);
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
