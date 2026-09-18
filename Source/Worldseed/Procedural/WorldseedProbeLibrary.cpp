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

FString UWorldseedProbeLibrary::ProbeCaves(int32 Seed, float HeightMeters,
	int32 ResolutionY, float AreaM, float StepM)
{
	// Les regles sont relues a chaque appel : c'est ce qui permet d'essayer une
	// valeur, de mesurer, et de recommencer sans redemarrer l'editeur.
	WorldseedPipeline::ReloadRules();

	WorldseedPipeline::FResult World;
	FString Error;
	if (!WorldseedPipeline::Generate(Seed, HeightMeters, ResolutionY, World, Error))
	{
		return FString::Printf(TEXT("generation impossible : %s"), *Error);
	}

	const UWorldseedRules* Rules = WorldseedPipeline::GetRules(Error);
	if (!Rules)
	{
		return FString::Printf(TEXT("regles illisibles : %s"), *Error);
	}

	const FWorldseedDensityRules R = FWorldseedDensityRules::FromRules(*Rules);

	FWorldseedDensity Density;
	Density.Init(World.Geometry, World.ElevationM, 1.0f, Seed, R);

	// --- une zone de TERRE, et une zone ordinaire -----------------------------
	// Le point le plus haut du monde est le plus accidente : il flatterait les
	// surplombs. On prend la mediane des terres, c'est-a-dire un relief banal,
	// celui que le joueur verra le plus souvent.
	TArray<float> Terres;
	Terres.Reserve(World.ElevationM.Num() / 4);
	for (const float H : World.ElevationM)
	{
		if (H > 0.0f) { Terres.Add(H); }
	}
	if (Terres.Num() == 0)
	{
		return TEXT("aucune terre emergee");
	}
	const float Mediane = WorldseedGrid::Quantile(Terres, 0.5f);

	const int32 NX = World.Geometry.NX;
	int32 Cellule = INDEX_NONE;
	float Meilleur = TNumericLimits<float>::Max();
	for (int32 C = 0; C < World.ElevationM.Num(); ++C)
	{
		const float H = World.ElevationM[C];
		if (H <= 0.0f) { continue; }
		const float Ecart = FMath::Abs(H - Mediane);
		if (Ecart < Meilleur)
		{
			Meilleur = Ecart;
			Cellule = C;
		}
	}

	const double MetresParCellule = World.Geometry.MetersPerPixel();
	const double CentreX = (static_cast<double>(Cellule % NX) - NX * 0.5) * MetresParCellule;
	const double CentreY = (static_cast<double>(Cellule / NX) - World.Geometry.NY * 0.5)
		* MetresParCellule;

	// --- balayage -------------------------------------------------------------
	const double Pas = FMath::Max(StepM, 0.5f);
	const double Demi = FMath::Max(AreaM, Pas * 4.0) * 0.5;
	const double PasZ = FMath::Max(Pas * 0.5, 0.5);

	int64 PointsBande = 0;
	int64 PointsAir = 0;
	int32 Colonnes = 0;
	int32 ColonnesSurplomb = 0;
	int32 ColonnesGalerie = 0;
	int32 ColonnesFranchissables = 0;
	int32 ColonnesDebout = 0;
	int32 TraverseesMax = 0;
	double PlusGrandVideMonde = 0.0;

	/** Hauteur de chaque vide rencontre, pour en donner la distribution. */
	TArray<double> Hauteurs;

	TArray<FVector> Exemples;

	for (double Y = CentreY - Demi; Y <= CentreY + Demi; Y += Pas)
	{
		for (double X = CentreX - Demi; X <= CentreX + Demi; X += Pas)
		{
			const float Surface = Density.SurfaceHeightM(X, Y);
			if (Surface <= 0.0f)
			{
				continue;   // on ne mesure pas sous la mer
			}

			++Colonnes;

			const double Haut = Surface + R.OverhangAmplitudeM * 1.5;
			const double Bas = Surface - R.BandDepthM;

			// ON MESURE LA HAUTEUR DES VIDES, PAS LEUR NOMBRE.
			//
			// Compter les traversees revenait a compter les rides : une
			// ondulation de quelques centimetres pesait autant qu'une arche de
			// dix metres. Le premier releve annoncait ainsi 43,8 % de colonnes
			// « a surplomb » sur un terrain qui, a l'image, paraissait lisse --
			// les deux etaient vrais, c'est l'indicateur qui ne disait rien.
			//
			// Ce qui compte est la HAUTEUR LIBRE sous de la roche : c'est elle
			// qui decide si l'on peut passer dessous, entrer dedans, s'y tenir
			// debout.
			int32 Traversees = 0;
			bool bAirPrecedent = Density.At(FVector(X, Y, Haut)) > 0.0;
			bool bGalerie = false;
			bool bRocheVue = false;
			double HauteurVide = 0.0;
			double PlusGrandVide = 0.0;

			for (double Z = Haut - PasZ; Z >= Bas; Z -= PasZ)
			{
				const bool bAir = Density.At(FVector(X, Y, Z)) > 0.0;
				if (bAir != bAirPrecedent)
				{
					++Traversees;
					bAirPrecedent = bAir;
				}

				if (!bAir)
				{
					bRocheVue = true;
					if (HauteurVide > 0.0)
					{
						PlusGrandVide = FMath::Max(PlusGrandVide, HauteurVide);
						Hauteurs.Add(HauteurVide);
						HauteurVide = 0.0;
					}
				}
				else if (bRocheVue)
				{
					// De l'air SOUS de la roche : un vide, et non le ciel.
					HauteurVide += PasZ;
				}

				if (Z < Surface)
				{
					++PointsBande;
					if (bAir)
					{
						++PointsAir;
						bGalerie = true;

						// Quelques adresses pour aller voir, prises en
						// profondeur : une poche a un metre sous l'herbe ne
						// prouverait rien.
						if (Exemples.Num() < 6 && (Surface - Z) > 15.0
							&& Exemples.Num() * 97 % 7 == PointsAir % 7)
						{
							Exemples.Add(FVector(X, Y, Z));
						}
					}
				}
			}
			if (HauteurVide > 0.0)
			{
				PlusGrandVide = FMath::Max(PlusGrandVide, HauteurVide);
				Hauteurs.Add(HauteurVide);
			}

			TraverseesMax = FMath::Max(TraverseesMax, Traversees);
			if (Traversees > 1) { ++ColonnesSurplomb; }
			if (bGalerie) { ++ColonnesGalerie; }
			if (PlusGrandVide >= 2.0) { ++ColonnesFranchissables; }
			if (PlusGrandVide >= 2.5) { ++ColonnesDebout; }
			PlusGrandVideMonde = FMath::Max(PlusGrandVideMonde, PlusGrandVide);
		}
	}

	const double PartSurplomb = Colonnes > 0
		? 100.0 * ColonnesSurplomb / Colonnes : 0.0;
	const double PartGalerieColonnes = Colonnes > 0
		? 100.0 * ColonnesGalerie / Colonnes : 0.0;
	const double PartAir = PointsBande > 0
		? 100.0 * static_cast<double>(PointsAir) / PointsBande : 0.0;

	UE_LOG(LogTemp, Log,
		TEXT("[Sonde] formes : relief median %.1f m, zone %.0f m au pas de %.1f m, ")
		TEXT("%d colonnes de terre"),
		Mediane, AreaM, Pas, Colonnes);
	Hauteurs.Sort();
	const double Mediane2 = Hauteurs.Num() > 0 ? Hauteurs[Hauteurs.Num() / 2] : 0.0;
	const double PartFranchissable = Colonnes > 0
		? 100.0 * ColonnesFranchissables / Colonnes : 0.0;
	const double PartDebout = Colonnes > 0
		? 100.0 * ColonnesDebout / Colonnes : 0.0;

	UE_LOG(LogTemp, Log,
		TEXT("[Sonde] surplombs : %.2f %% des colonnes traversees plus d'une fois ")
		TEXT("(%d au plus)  |  amplitude %.1f m, frequence %.4f"),
		PartSurplomb, TraverseesMax, R.OverhangAmplitudeM, R.OverhangFrequency);
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde] vides : %d mesures, hauteur mediane %.1f m, la plus grande ")
		TEXT("%.1f m  |  %.2f %% des colonnes ont 2 m de libre, %.2f %% en ont 2,5"),
		Hauteurs.Num(), Mediane2, PlusGrandVideMonde, PartFranchissable, PartDebout);
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde] galeries : %.2f %% du volume de la bande est creuse, ")
		TEXT("%.2f %% des colonnes en rencontrent une  |  seuil %.3f, rayon %.1f m, ")
		TEXT("frequence %.4f"),
		PartAir, PartGalerieColonnes, R.CaveThreshold, R.CaveRadiusM, R.CaveFrequency);

	for (const FVector& P : Exemples)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Sonde]   galerie a (%.0f, %.0f, %.0f) m, soit %.0f cm monde"),
			P.X, P.Y, P.Z, P.Z * 100.0);
	}

	const FString Resume = FString::Printf(
		TEXT("vides : %.2f %% des colonnes ont 2 m de libre (%.2f %% en ont 2,5), ")
		TEXT("mediane %.1f m, max %.1f m | air %.2f %% de la bande | ")
		TEXT("ampl %.1f seuil %.3f rayon %.1f"),
		PartFranchissable, PartDebout, Mediane2, PlusGrandVideMonde, PartAir,
		R.OverhangAmplitudeM, R.CaveThreshold, R.CaveRadiusM);

	UE_LOG(LogTemp, Log, TEXT("[Sonde] %s"), *Resume);
	return Resume;
}
