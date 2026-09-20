// Worldseed - sonde des trous : le defaut est-il dans le CHAMP ou dans le MAILLEUR ?

#include "Procedural/WorldseedProbeLibrary.h"

#include "Procedural/WorldseedProbeCommun.h"

#include "Procedural/WorldseedCaves.h"
#include "Procedural/WorldseedVoxelChunk.h"

FString UWorldseedProbeLibrary::ProbeTrous(int32 Seed, float HeightMeters,
	int32 ResolutionY, float CoteM, float PasM)
{
	FWorldseedSonde S;
	if (!S.Preparer(Seed, HeightMeters, ResolutionY))
	{
		UE_LOG(LogTemp, Error, TEXT("[Sonde] %s"), *S.Erreur);
		return S.Erreur;
	}

	const FWorldseedGeometry& Geo = S.World.Geometry;

	// --- se placer sur de la TERRE, et la plus accidentee -------------------
	int32 Meilleure = INDEX_NONE;
	float PlusHaut = 0.0f;
	for (int32 C = 0; C < S.World.ElevationM.Num(); ++C)
	{
		if (S.World.ElevationM[C] > PlusHaut) { PlusHaut = S.World.ElevationM[C]; Meilleure = C; }
	}
	if (Meilleure == INDEX_NONE) { return TEXT("aucune terre emergee"); }

	const double MpP = Geo.MetersPerPixel();
	const double CX = (static_cast<double>(Meilleure % Geo.NX) - Geo.NX * 0.5) * MpP;
	const double CY = (static_cast<double>(Meilleure / Geo.NX) - Geo.NY * 0.5) * MpP;

	const double Demi = FMath::Max(CoteM, 64.0f) * 0.5;
	const double Pas = FMath::Max(PasM, 1.0f);

	// LA PREMIERE BIFURCATION, ET LA MOINS CHERE. « Trou » recouvre au moins
	// trois choses qui se ressemblent vues de l'exterieur : un chunk jamais
	// considere, un chunk declare vide, et de la geometrie dechiree. Elles
	// appellent trois corrections opposees. Mais AVANT de les departager, il
	// faut savoir si le defaut est seulement dans le MAILLAGE : si le CHAMP
	// lui-meme n'a pas de surface a cet endroit, aucun mailleur du monde n'en
	// produira, et c'est la generation qu'il faut regarder.
	int32 Colonnes = 0;
	int32 SansSurface = 0;
	int32 AvecSurface = 0;

	// Et la ou il y a une surface, on la maille VRAIMENT, comme le jeu le fait,
	// pour voir si le mailleur la rend.
	int32 ChunksTestes = 0;
	int32 ChunksVides = 0;
	int32 ChunksSansTraversee = 0;
	int32 ChunksMaillageVide = 0;
	int64 TrianglesTotal = 0;

	TArray<double> ProfondeurTrou;

	for (double Y = CY - Demi; Y <= CY + Demi; Y += Pas)
	{
		for (double X = CX - Demi; X <= CX + Demi; X += Pas)
		{
			const double Macro = S.Densite.SurfaceHeightM(X, Y);
			if (Macro <= 1.0) { continue; }
			++Colonnes;

			// ON CHERCHE LE PREMIER CHANGEMENT DE SIGNE EN DESCENDANT, et non
			// une valeur au relief macro : le champ deplace la surface de
			// plusieurs metres, et partir du macro compte de l'air ordinaire.
			// Le depot a deja paye cette lecon sur la sonde des diaclases.
			bool bTrouve = false;
			const double Haut = Macro + 30.0;
			const double Bas = Macro - S.DensiteRegles.BandDepthM;
			for (double Z = Haut; Z >= Bas; Z -= 1.0)
			{
				if (S.Densite.At(FVector(X, Y, Z)) <= 0.0) { bTrouve = true; break; }
			}

			if (bTrouve) { ++AvecSurface; }
			else
			{
				++SansSurface;
				ProfondeurTrou.Add(Macro);
			}
		}
	}

	// --- et le mailleur, sur la meme emprise --------------------------------
	//
	// MEME CHAMP, MEMES BOITES QUE LE JEU. Un mailleur interroge a cote de ce
	// que la diffusion lui donne ne prouverait rien.
	const double Cote = 32.0;
	FWorldseedVoxelMesh Maillage;
	FWorldseedVoxelStats Stats;

	const int32 Portee = FMath::CeilToInt(Demi / Cote);
	for (int32 DY = -Portee; DY <= Portee; ++DY)
	{
		for (int32 DX = -Portee; DX <= Portee; ++DX)
		{
			const double MinX = FMath::FloorToDouble((CX + DX * Cote) / Cote) * Cote;
			const double MinY = FMath::FloorToDouble((CY + DY * Cote) / Cote) * Cote;

			float SMin = 0.0f;
			float SMax = 0.0f;
			S.Densite.SurfaceRangeM(MinX, MinY, MinX + Cote, MinY + Cote, SMin, SMax);
			if (SMax <= 1.0f) { continue; }

			const int32 ZBas = FMath::FloorToInt((SMin - S.DensiteRegles.BandDepthM) / Cote);
			const int32 ZHaut = FMath::FloorToInt(SMax / Cote);

			for (int32 CZ = ZBas; CZ <= ZHaut; ++CZ)
			{
				const FBox Boite(FVector(MinX, MinY, CZ * Cote),
					FVector(MinX + Cote, MinY + Cote, (CZ + 1) * Cote));

				++ChunksTestes;
				const bool bPlein = WorldseedVoxelChunk::Build(
					S.Densite, nullptr, Boite, S.DensiteRegles.VoxelSizeM,
					Maillage, Stats, nullptr, S.DensiteRegles.bTransvoxel);

				if (bPlein) { TrianglesTotal += Maillage.TriangleCount(); }
				else
				{
					++ChunksVides;
					switch (Stats.Cause)
					{
					case FWorldseedVoxelStats::ECause::SansTraversee:
						++ChunksSansTraversee; break;
					case FWorldseedVoxelStats::ECause::MaillageVide:
						++ChunksMaillageVide; break;
					default: break;
					}
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Sonde] === LES TROUS : CHAMP OU MAILLEUR ? ==="));
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   zone de %.0f m au pas de %.1f m, centree sur (%.0f, %.0f) m"),
		Demi * 2.0, Pas, CX, CY);
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   LE CHAMP : %d colonnes de terre, %d avec une surface, ")
		TEXT("%d SANS (%.2f %%)"),
		Colonnes, AvecSurface, SansSurface,
		Colonnes > 0 ? 100.0 * SansSurface / Colonnes : 0.0);
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   LE MAILLEUR : %d chunks, %d rendus vides dont %d sans ")
		TEXT("traversee et %d au maillage vide  |  %lld triangles"),
		ChunksTestes, ChunksVides, ChunksSansTraversee, ChunksMaillageVide,
		TrianglesTotal);
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   Lecture : une colonne SANS surface est un trou du CHAMP -- ")
		TEXT("aucun mailleur n'en produira de sol. Un chunk au MAILLAGE VIDE est ")
		TEXT("l'inverse : le champ y a une surface et le mailleur n'a rien rendu. ")
		TEXT("« Sans traversee » est normal et attendu : ce sont les chunks tout ")
		TEXT("air ou tout roche, les deux tiers du volume."));

	const FString Resume = FString::Printf(
		TEXT("champ : %d/%d colonnes sans surface (%.2f %%) ; mailleur : %d chunks, ")
		TEXT("%d au maillage vide"),
		SansSurface, Colonnes, Colonnes > 0 ? 100.0 * SansSurface / Colonnes : 0.0,
		ChunksTestes, ChunksMaillageVide);

	UE_LOG(LogTemp, Log, TEXT("[Sonde] %s"), *Resume);
	return Resume;
}
