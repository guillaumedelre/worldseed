// Worldseed - points d'entree de verification, appelables sans lancer le jeu.

#include "Procedural/WorldseedProbeLibrary.h"

#include "Procedural/WorldseedDensity.h"
#include "Procedural/WorldseedGlobe.h"
#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedLithology.h"
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

FString UWorldseedProbeLibrary::ProbeLithology(int32 Seed, float HeightMeters,
	int32 ResolutionY)
{
	WorldseedPipeline::ReloadRules();

	WorldseedPipeline::FResult World;
	FString Error;
	if (!WorldseedPipeline::Generate(Seed, HeightMeters, ResolutionY, World, Error))
	{
		return FString::Printf(TEXT("generation impossible : %s"), *Error);
	}

	const int32 Count = World.Geometry.CellCount();
	if (!World.Lithology.IsValid(Count))
	{
		return TEXT("la lithologie n'a pas ete calculee");
	}

	const UWorldseedRules* Rules = WorldseedPipeline::GetRules(Error);
	if (!Rules)
	{
		return FString::Printf(TEXT("regles illisibles : %s"), *Error);
	}
	const FWorldseedLithologyRules Litho = FWorldseedLithologyRules::FromRules(*Rules);
	const int32 Roches = Litho.Catalogue.Num();

	TArray<int32> TotalMer;   TotalMer.Init(0, Roches);
	TArray<int32> TotalTerre; TotalTerre.Init(0, Roches);
	TArray<double> SommeAlt;  SommeAlt.Init(0.0, Roches);
	int32 Mer = 0;
	int32 Terre = 0;

	for (int32 I = 0; I < Count; ++I)
	{
		const int32 R = World.Lithology.Id[I];
		if (!TotalMer.IsValidIndex(R)) { continue; }
		if (World.ElevationM[I] > 0.0f)
		{
			++TotalTerre[R]; ++Terre;
			SommeAlt[R] += World.ElevationM[I];
		}
		else
		{
			++TotalMer[R]; ++Mer;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] --- lithologie ---"));
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   %-10s %10s %10s %14s %10s"),
		TEXT("roche"), TEXT("des terres"), TEXT("des mers"),
		TEXT("altitude moy."), TEXT("karst"));

	for (int32 R = 0; R < Roches; ++R)
	{
		if (TotalTerre[R] == 0 && TotalMer[R] == 0) { continue; }
		const double Alt = (TotalTerre[R] > 0) ? SommeAlt[R] / TotalTerre[R] : 0.0;
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   %-10s %9.2f %% %9.2f %% %11.0f m %10.2f"),
			WorldseedLithology::Name(Litho, static_cast<uint8>(R)),
			100.0 * TotalTerre[R] / FMath::Max(Terre, 1),
			100.0 * TotalMer[R] / FMath::Max(Mer, 1),
			Alt, Litho.Catalogue[R].Karstifiable);
	}

	// LA PART KARSTIFIABLE DES TERRES : c'est elle qui dira, quand les grottes
	// arriveront, quelle fraction du monde peut porter un reseau de dissolution.
	double Karst = 0.0;
	for (int32 R = 0; R < Roches; ++R)
	{
		Karst += Litho.Catalogue[R].Karstifiable * TotalTerre[R];
	}
	const double KarstPct = 100.0 * Karst / FMath::Max(Terre, 1);
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   terres karstifiables : %.2f %%"), KarstPct);

	return FString::Printf(TEXT("%d roches ; %.2f %% des terres karstifiables ; detail au journal"),
		Roches, KarstPct);
}

FString UWorldseedProbeLibrary::ProbeGroundFields(int32 Seed, float HeightMeters,
	int32 ResolutionY)
{
	WorldseedPipeline::ReloadRules();

	WorldseedPipeline::FResult World;
	FString Error;
	if (!WorldseedPipeline::Generate(Seed, HeightMeters, ResolutionY, World, Error))
	{
		return FString::Printf(TEXT("generation impossible : %s"), *Error);
	}

	const int32 Count = World.Geometry.CellCount();
	if (!World.Ground.IsValid(Count))
	{
		return TEXT("les champs du sol n'ont pas ete calcules");
	}

	const int32 NX = World.Geometry.NX;
	const float SpacingM = FMath::Max(World.Geometry.MetersPerPixel(), 1e-3f);

	TArray<float> DY;
	TArray<float> DX;
	WorldseedGrid::Gradient(World.ElevationM, NX, World.Geometry.NY, SpacingM, DY, DX);

	TArray<float> Humidite;
	TArray<float> Soleil;

	// Versants tournes vers le nord et vers le sud, par hemisphere. Seules les
	// pentes FRANCHES comptent : sur du plat il n'y a pas d'adret.
	double SudNord[2] = { 0.0, 0.0 };     // hemisphere nord : vers le nord, vers le sud
	int32 SudNordN[2] = { 0, 0 };
	double SudSud[2] = { 0.0, 0.0 };      // hemisphere sud
	int32 SudSudN[2] = { 0, 0 };

	for (int32 Row = 0; Row < World.Geometry.NY; ++Row)
	{
		const float Lat = World.Geometry.LatitudeDegForRow(Row);
		for (int32 Col = 0; Col < NX; ++Col)
		{
			const int32 I = Row * NX + Col;
			if (World.ElevationM[I] <= 0.0f)
			{
				continue;
			}
			Humidite.Add(World.Ground.SoilMoisture01[I]);
			Soleil.Add(World.Ground.SunExposure01[I]);

			// dZ/dNord positif : le terrain MONTE vers le nord, donc la pente
			// regarde vers le SUD. C'est la definition, et c'est elle qu'une
			// erreur de signe fait basculer.
			if (FMath::Abs(DY[I]) < 0.2f || FMath::Abs(Lat) < 15.0f)
			{
				continue;
			}
			const int32 Face = (DY[I] > 0.0f) ? 1 : 0;   // 1 = vers le sud
			if (Lat > 0.0f) { SudNord[Face] += World.Ground.SunExposure01[I]; ++SudNordN[Face]; }
			else            { SudSud[Face] += World.Ground.SunExposure01[I]; ++SudSudN[Face]; }
		}
	}

	if (Humidite.Num() == 0)
	{
		return TEXT("aucune terre emergee");
	}

	auto Moy = [](double S, int32 N) { return (N > 0) ? S / N : 0.0; };

	const double NordVersNord = Moy(SudNord[0], SudNordN[0]);
	const double NordVersSud = Moy(SudNord[1], SudNordN[1]);
	const double SudVersNord = Moy(SudSud[0], SudSudN[0]);
	const double SudVersSud = Moy(SudSud[1], SudSudN[1]);

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] --- champs continus du sol ---"));
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   humidite  : p10 %.2f  mediane %.2f  p90 %.2f"),
		WorldseedGrid::Quantile(Humidite, 0.10f),
		WorldseedGrid::Quantile(Humidite, 0.50f),
		WorldseedGrid::Quantile(Humidite, 0.90f));
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   soleil    : p10 %.2f  mediane %.2f  p90 %.2f  (0,50 = terrain plat)"),
		WorldseedGrid::Quantile(Soleil, 0.10f),
		WorldseedGrid::Quantile(Soleil, 0.50f),
		WorldseedGrid::Quantile(Soleil, 0.90f));

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   hemisphere NORD : versant au sud %.3f, versant au nord %.3f  (ecart %+.3f)"),
		NordVersSud, NordVersNord, NordVersSud - NordVersNord);
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   hemisphere SUD  : versant au nord %.3f, versant au sud %.3f  (ecart %+.3f)"),
		SudVersNord, SudVersSud, SudVersNord - SudVersSud);

	// LE VERDICT. Au nord l'adret regarde le sud, au sud il regarde le nord.
	// Les deux ecarts doivent donc etre POSITIFS. Si l'un est negatif, le signe
	// du soleil est faux, et aucun autre chiffre ne l'aurait dit.
	const bool bNord = (NordVersSud - NordVersNord) > 0.0;
	const bool bSud = (SudVersNord - SudVersSud) > 0.0;

	return FString::Printf(
		TEXT("adret au sud dans l'hemisphere nord : %s ; adret au nord dans l'hemisphere sud : %s"),
		bNord ? TEXT("OUI") : TEXT("NON -- SIGNE FAUX"),
		bSud ? TEXT("OUI") : TEXT("NON -- SIGNE FAUX"));
}

FString UWorldseedProbeLibrary::ProbeBiomes(int32 Seed, float HeightMeters,
	int32 ResolutionY)
{
	WorldseedPipeline::ReloadRules();

	WorldseedPipeline::FResult World;
	FString Error;
	if (!WorldseedPipeline::Generate(Seed, HeightMeters, ResolutionY, World, Error))
	{
		return FString::Printf(TEXT("generation impossible : %s"), *Error);
	}

	// LA REFERENCE TERRESTRE, pour les biomes qui en ont une. Ce sont les huit
	// grands biomes du bulletin de terre.py ; les autres n'ont pas de cible
	// publiee et se lisent seuls. Un tiret vaut mieux qu'un chiffre invente.
	struct FCible { EWorldseedBiome Biome; float TerrePct; };
	static const FCible Cibles[] = {
		{ EWorldseedBiome::Tundra,             8.0f },
		{ EWorldseedBiome::Taiga,             10.0f },
		{ EWorldseedBiome::TemperateForest,   13.0f },
		{ EWorldseedBiome::Grassland,          8.0f },
		{ EWorldseedBiome::HotDesert,         21.0f },
		{ EWorldseedBiome::Savanna,           13.0f },
		{ EWorldseedBiome::TropicalRainforest, 11.0f },
		{ EWorldseedBiome::IceCap,            10.0f },
	};

	auto CibleDe = [](EWorldseedBiome B) -> float
	{
		for (const FCible& C : Cibles)
		{
			if (C.Biome == B) { return C.TerrePct; }
		}
		return -1.0f;
	};

	TArray<int32> Ordre;
	for (int32 B = 0; B < static_cast<int32>(EWorldseedBiome::Count); ++B)
	{
		Ordre.Add(B);
	}
	Ordre.Sort([&World](int32 A, int32 B)
	{
		return World.Biomes.LandSharePct[A] > World.Biomes.LandSharePct[B];
	});

	float Ecart = 0.0f;
	int32 Comptes = 0;

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] --- part des terres par biome ---"));
	for (const int32 B : Ordre)
	{
		const float Part = World.Biomes.LandSharePct[B];
		if (Part <= 0.0f)
		{
			continue;
		}
		const float Cible = CibleDe(static_cast<EWorldseedBiome>(B));
		if (Cible > 0.0f)
		{
			Ecart += FMath::Abs(Part - Cible) / Cible;
			++Comptes;
			UE_LOG(LogTemp, Log, TEXT("[Worldseed]   %-26s %6.2f %%   Terre %5.1f %%   %+6.1f"),
				WorldseedBiomes::Name(static_cast<EWorldseedBiome>(B)), Part, Cible, Part - Cible);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[Worldseed]   %-26s %6.2f %%"),
				WorldseedBiomes::Name(static_cast<EWorldseedBiome>(B)), Part);
		}
	}

	// --- POURQUOI LA LIMITE DES ARBRES NE MORD PAS ---------------------------
	// Le critere de Koppen porte sur le mois le PLUS CHAUD. Si l'amplitude
	// saisonniere est trop faible, ce mois reste froid meme sous une moyenne
	// annuelle clemente, et le critere deshabille des terres qui devraient
	// porter une foret. Ces trois chiffres disent si c'est le cas, et
	// pourquoi -- sans eux on regle un seuil a l'aveugle.
	// bHasClimate ne promet PAS que chaque tableau du climat est rempli : la
	// sonde a plante la premiere fois sur un Continentality vide. On verifie
	// chaque tableau qu'on lit, un par un.
	const int32 Cells = World.Geometry.CellCount();
	const bool bCont = (World.Climate.Continentality.Num() == Cells);
	const bool bAmp = (World.Climate.SeasonalAmpC.Num() == Cells);
	const bool bTMoy = (World.Climate.TempMeanC.Num() == Cells);

	// LE MOIS LE PLUS CHAUD SE RECONSTRUIT, IL NE SE LIT PAS. Sur le chemin
	// "repris du cache", Climate.TempMaxC revient VIDE : seules la moyenne et
	// l'amplitude sont transportees. C'est d'ailleurs pour cela que la chaine
	// le reconstruit elle-meme avant d'appeler Classify. Premiere version de
	// cette sonde silencieuse pour cette raison.
	if (World.bHasClimate && bAmp && bTMoy)
	{
		TArray<float> Cont;
		TArray<float> Amp;
		TArray<float> AmpHaute;   // entre 50 et 70 degres, la ou vit la taiga
		int32 Terres = 0;
		int32 EteFroid = 0;
		int32 EteFroidMaisDoux = 0;

		for (int32 J = 0; J < World.Geometry.NY; ++J)
		{
			const float Lat = FMath::Abs(World.Geometry.LatitudeDegForRow(J));
			for (int32 I = 0; I < World.Geometry.NX; ++I)
			{
				const int32 Idx = J * World.Geometry.NX + I;
				if (Idx >= Cells || World.ElevationM[Idx] <= 0.0f)
				{
					continue;
				}
				++Terres;
				if (bCont) { Cont.Add(World.Climate.Continentality[Idx]); }
				Amp.Add(World.Climate.SeasonalAmpC[Idx]);
				if (Lat >= 50.0f && Lat <= 70.0f)
				{
					AmpHaute.Add(World.Climate.SeasonalAmpC[Idx]);
				}
				const float TMax = World.Climate.TempMeanC[Idx]
					+ World.Climate.SeasonalAmpC[Idx] * 0.5f;
				if (TMax < 10.0f)
				{
					++EteFroid;
					// Le cas qui fait mal : une annee assez douce pour une foret,
					// mais un ete trop froid pour un arbre.
					if (World.Climate.TempMeanC[Idx] > -5.0f) { ++EteFroidMaisDoux; }
				}
			}
		}

		if (Terres > 0)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]   continentalite mediane %.2f | amplitude mediane %.1f C")
				TEXT(" | amplitude 50-70 deg : mediane %.1f C, p90 %.1f C"),
				Cont.Num() ? WorldseedGrid::Quantile(Cont, 0.5f) : -1.0f,
				WorldseedGrid::Quantile(Amp, 0.5f),
				AmpHaute.Num() ? WorldseedGrid::Quantile(AmpHaute, 0.5f) : 0.0f,
				AmpHaute.Num() ? WorldseedGrid::Quantile(AmpHaute, 0.9f) : 0.0f);
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]   ete sous 10 C : %.1f %% des terres, dont %.1f %% ")
				TEXT("sous une moyenne annuelle superieure a -5 C"),
				100.0f * EteFroid / Terres, 100.0f * EteFroidMaisDoux / Terres);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Worldseed]   substrat : roche a nu %.2f %%, estran %.2f %%"),
		World.Biomes.CoverSharePct[static_cast<int32>(EWorldseedCover::Rock)],
		World.Biomes.CoverSharePct[static_cast<int32>(EWorldseedCover::Beach)]);

	const float Moyen = (Comptes > 0) ? 100.0f * Ecart / Comptes : 0.0f;
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   ecart absolu moyen aux %d biomes de reference : %.1f %%"),
		Comptes, Moyen);

	return FString::Printf(TEXT("ecart absolu moyen %.1f %% sur %d biomes ; detail au journal"),
		Moyen, Comptes);
}

FString UWorldseedProbeLibrary::ProbeWhittaker(int32 Seed, float HeightMeters,
	int32 ResolutionY)
{
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

	const FWorldseedBiomeRules Bio = FWorldseedBiomeRules::FromRules(*Rules, World.Geometry);
	if (Bio.Bands.Num() == 0 || !World.bHasClimate)
	{
		return TEXT("pas de climat ou pas de diagramme");
	}

	const int32 Count = World.Geometry.CellCount();

	// LA TEMPERATURE DU MOIS LE PLUS CHAUD SE RECONSTRUIT COMME DANS LA CHAINE :
	// moyenne annuelle plus la moitie de l'amplitude saisonniere. On ne s'en sert
	// que pour ecarter la calotte, qui n'est pas une case du diagramme.
	const bool bHasAmp = (World.Climate.SeasonalAmpC.Num() == Count);

	// Un compteur par case, a plat : bande * plus grand nombre de coupes + coupe.
	int32 MaxCuts = 0;
	for (const FWorldseedWhittakerBand& B : Bio.Bands)
	{
		MaxCuts = FMath::Max(MaxCuts, B.Cuts.Num());
	}

	TArray<int32> Tally;
	Tally.Init(0, Bio.Bands.Num() * MaxCuts);
	int32 LandTotal = 0;
	int32 IceCap = 0;
	int32 Alpine = 0;

	for (int32 I = 0; I < Count; ++I)
	{
		if (World.ElevationM[I] <= 0.0f)
		{
			continue;
		}
		++LandTotal;

		const float T = World.Climate.TempMeanC[I];
		const float P = World.Climate.PrecipMm[I];

		// LES SURCHARGES CLIMATIQUES SONT COMPTEES A PART, pas dans une case :
		// elles ne viennent pas du diagramme, et les melanger ferait croire que
		// la case qu'elles recouvrent est vide alors qu'elle ne l'est pas.
		const float TMax = T + (bHasAmp ? World.Climate.SeasonalAmpC[I] * 0.5f : 0.0f);
		if (TMax < Bio.PermanentIceTempC)
		{
			++IceCap;
			continue;
		}
		if (T < Bio.TreeLineTempC && World.ElevationM[I] > Bio.AlpineMinElevationM)
		{
			++Alpine;
			continue;
		}

		for (int32 B = 0; B < Bio.Bands.Num(); ++B)
		{
			if (T > Bio.Bands[B].MaxTempC && B + 1 < Bio.Bands.Num())
			{
				continue;
			}
			const TArray<FWorldseedWhittakerCut>& Cuts = Bio.Bands[B].Cuts;
			for (int32 C = 0; C < Cuts.Num(); ++C)
			{
				if (P <= Cuts[C].MaxPrecipMm || C + 1 == Cuts.Num())
				{
					++Tally[B * MaxCuts + C];
					break;
				}
			}
			break;
		}
	}

	if (LandTotal == 0)
	{
		return TEXT("aucune terre emergee");
	}

	auto Pct = [LandTotal](int32 N) { return 100.0f * N / LandTotal; };

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] --- cases du diagramme, part des terres ---"));
	for (int32 B = 0; B < Bio.Bands.Num(); ++B)
	{
		const float TLo = (B == 0) ? -100.0f : Bio.Bands[B - 1].MaxTempC;
		const TArray<FWorldseedWhittakerCut>& Cuts = Bio.Bands[B].Cuts;
		float PLo = 0.0f;
		for (int32 C = 0; C < Cuts.Num(); ++C)
		{
			const float PHi = Cuts[C].MaxPrecipMm;
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]   T %6.1f..%6.1f C  P %6.0f..%-8.0f mm  %-24s %5.2f %%"),
				TLo, Bio.Bands[B].MaxTempC, PLo, FMath::Min(PHi, 99999.0f),
				WorldseedBiomes::Name(Cuts[C].Biome), Pct(Tally[B * MaxCuts + C]));
			PLo = PHi;
		}
	}
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   hors diagramme : calotte %.2f %%, alpin %.2f %%"),
		Pct(IceCap), Pct(Alpine));

	return FString::Printf(
		TEXT("%d cases mesurees sur %d cellules de terre ; le detail est au journal"),
		Tally.Num(), LandTotal);
}

FString UWorldseedProbeLibrary::ProbeCaves(int32 Seed, float HeightMeters,
	int32 ResolutionY, float AreaM, float StepM, bool bSteepest)
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
	const int32 NYG = World.Geometry.NY;
	int32 Cellule = INDEX_NONE;

	if (bSteepest)
	{
		// LE DEPLACEMENT HORIZONTAL N'AGIT QUE SUR LE RAIDE, par construction :
		// sur du plat, lire le relief vingt metres plus loin donne la meme
		// altitude. Le mesurer sur un relief median reviendrait donc a conclure
		// qu'il ne fait rien -- ce qui serait vrai, et sans interet.
		float PlusRaide = -1.0f;
		for (int32 J = 1; J < NYG - 1; ++J)
		{
			for (int32 I = 1; I < NX - 1; ++I)
			{
				const int32 C = J * NX + I;
				if (World.ElevationM[C] <= 0.0f) { continue; }
				const float DX = World.ElevationM[C + 1] - World.ElevationM[C - 1];
				const float DY = World.ElevationM[C + NX] - World.ElevationM[C - NX];
				const float G = DX * DX + DY * DY;
				if (G > PlusRaide)
				{
					PlusRaide = G;
					Cellule = C;
				}
			}
		}
	}
	else
	{
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
	}
	if (Cellule == INDEX_NONE)
	{
		return TEXT("aucune cellule de terre retenue");
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
		TEXT("[Sonde] formes : relief median %.1f m, zone %.0f m au pas de %.1f m ")
		TEXT("centree sur (%.0f, %.0f) m [%s], %d colonnes de terre"),
		Mediane, AreaM, Pas, CentreX, CentreY,
		bSteepest ? TEXT("le plus raide") : TEXT("relief median"), Colonnes);
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
