// Worldseed - sondes du monde 2D : globe, sol, biomes, Whittaker.
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

FString UWorldseedProbeLibrary::ProbeZonal(int32 Seed, float HeightMeters,
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
	if (!Rules) { return FString::Printf(TEXT("regles illisibles : %s"), *Error); }
	const FWorldseedBiomeRules Bio = FWorldseedBiomeRules::FromRules(*Rules, World.Geometry);
	if (!World.bHasClimate) { return TEXT("le climat n'a pas tourne"); }

	const FWorldseedGeometry& Geo = World.Geometry;
	const int32 NX = Geo.NX;
	const int32 NY = Geo.NY;

	// LA REFERENCE TERRESTRE, moyenne annuelle par bande de latitude.
	// Valeurs zonales classiques, TOUTES SURFACES confondues : c'est la
	// grandeur que notre TempMeanC decrit. On ne compare donc pas des terres
	// a des terres, et la ligne de comparaison le dit.
	static const float TerreMoyenneC[9] = {
		26.0f, 25.0f, 21.0f, 16.0f, 9.0f, 2.0f, -6.0f, -14.0f, -22.0f
	};

	struct FBande
	{
		int32 Cellules = 0;
		int32 Terres = 0;
		double SommeT = 0.0;
		double SommeTMax = 0.0;
		double SommeAlt = 0.0;
		int32 Calotte = 0;
		int32 Toundra = 0;
		int32 Taiga = 0;
		int32 Alpin = 0;
		int32 SousZeroEte = 0;   // TempMax < 0 : eligible a la calotte
	};
	FBande Bandes[9];

	const TArray<float>& T = World.Climate.TempMeanC;

	// TempMaxC N'EST PAS DANS LE CACHE, et le pipeline le RECONSTRUIT lui-meme
	// avant de classer (WorldseedPipeline.cpp:144). Une sonde qui lirait
	// World.Climate.TempMaxC mesurerait donc un tableau VIDE sur tout monde
	// repris du cache -- et afficherait 0,0 C partout sans rien signaler.
	// C'est exactement le piege que le socle des sondes existe pour eviter :
	// « un temoin non branche mesure le monde d'avant ». Ici le zero n'etait
	// pas plausible, ce qui l'a trahi ; il aurait pu l'etre.
	const int32 Cells = World.ElevationM.Num();
	TArray<float> TMax;
	bool bMaxReconstruit = false;
	if (World.Climate.TempMaxC.Num() == Cells)
	{
		TMax = World.Climate.TempMaxC;
	}
	else if (World.Climate.TempMeanC.Num() == Cells
		&& World.Climate.SeasonalAmpC.Num() == Cells)
	{
		TMax.SetNumUninitialized(Cells);
		for (int32 I = 0; I < Cells; ++I)
		{
			TMax[I] = World.Climate.TempMeanC[I]
				+ World.Climate.SeasonalAmpC[I] * 0.5f;
		}
		bMaxReconstruit = true;
	}
	const bool bHasMax = (TMax.Num() == Cells);

	int32 TerresTotal = 0;

	for (int32 J = 0; J < NY; ++J)
	{
		const float Lat = Geo.LatitudeDegForRow(J);
		const int32 B = FMath::Clamp(
			static_cast<int32>(FMath::Abs(Lat) / 10.0f), 0, 8);

		for (int32 I = 0; I < NX; ++I)
		{
			const int32 Idx = J * NX + I;
			FBande& Ba = Bandes[B];
			++Ba.Cellules;

			const float Elev = World.ElevationM[Idx];
			if (Elev < 0.0f)
			{
				continue;
			}

			++Ba.Terres;
			++TerresTotal;
			Ba.SommeT += T.IsValidIndex(Idx) ? T[Idx] : 0.0f;
			Ba.SommeAlt += Elev;
			if (bHasMax)
			{
				Ba.SommeTMax += TMax[Idx];
				if (TMax[Idx] < 0.0f) { ++Ba.SousZeroEte; }
			}

			if (World.Biomes.Index.IsValidIndex(Idx))
			{
				switch (static_cast<EWorldseedBiome>(World.Biomes.Index[Idx]))
				{
				case EWorldseedBiome::IceCap: ++Ba.Calotte; break;
				case EWorldseedBiome::Tundra: ++Ba.Toundra; break;
				case EWorldseedBiome::Taiga:  ++Ba.Taiga;   break;
				case EWorldseedBiome::Alpine: ++Ba.Alpin;   break;
				default: break;
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] === PROFIL ZONAL ==="));
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   |lat|     surface   emerge   part des    alt     T an    T ete   Terre    calotte  toundra   taiga   alpin"));
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]            du monde   local     terres      m        C        C      C an     %%       %%       %%      %%"));

	int32 EligibleCalotte = 0;

	for (int32 B = 0; B < 9; ++B)
	{
		const FBande& Ba = Bandes[B];
		if (Ba.Cellules == 0) { continue; }

		const float PartSurface = 100.0f * Ba.Cellules / static_cast<float>(NX * NY);
		const float Emerge = 100.0f * Ba.Terres / static_cast<float>(Ba.Cellules);
		const float PartTerres = TerresTotal > 0
			? 100.0f * Ba.Terres / static_cast<float>(TerresTotal) : 0.0f;

		EligibleCalotte += Ba.SousZeroEte;

		if (Ba.Terres == 0)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]   %2d-%2d  %8.2f  %6.1f      --        --       --       --  %6.1f       --       --      --      --"),
				B * 10, B * 10 + 10, PartSurface, Emerge, TerreMoyenneC[B]);
			continue;
		}

		const float MoyT = static_cast<float>(Ba.SommeT / Ba.Terres);
		const float MoyTMax = static_cast<float>(Ba.SommeTMax / Ba.Terres);
		const float MoyAlt = static_cast<float>(Ba.SommeAlt / Ba.Terres);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   %2d-%2d  %8.2f  %6.1f  %8.2f  %6.0f  %7.1f  %7.1f  %6.1f  %7.1f  %7.1f %7.1f %7.1f"),
			B * 10, B * 10 + 10, PartSurface, Emerge, PartTerres, MoyAlt,
			MoyT, MoyTMax, TerreMoyenneC[B],
			100.0f * Ba.Calotte / Ba.Terres,
			100.0f * Ba.Toundra / Ba.Terres,
			100.0f * Ba.Taiga / Ba.Terres,
			100.0f * Ba.Alpin / Ba.Terres);
	}

	// LE CRITERE DE LA CALOTTE, ISOLE. Elle exige que le mois le PLUS CHAUD
	// reste sous le gel ; c'est donc cette colonne-la qu'il faut regarder, et
	// non la moyenne annuelle. Une bande a -10 C de moyenne mais +5 C en ete
	// ne portera jamais de glace permanente.
	const float PartEligible = TerresTotal > 0
		? 100.0f * EligibleCalotte / static_cast<float>(TerresTotal) : 0.0f;

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   terres dont le mois le plus chaud reste sous 0 C : %.2f %% (Terre : ~10)"),
		PartEligible);
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   altitudes %.0f .. %.0f m ; seuil alpin %.1f m ; ")
		TEXT("mois le plus chaud %s"),
		World.MinElevationM, World.MaxElevationM,
		Bio.AlpineMinElevationM,
		bHasMax
			? (bMaxReconstruit ? TEXT("RECONSTRUIT (absent du cache)") : TEXT("lu"))
			: TEXT("INDISPONIBLE -- la colonne T ete ne vaut rien"));

	return FString::Printf(
		TEXT("profil zonal : %.2f %% des terres eligibles a la calotte (Terre ~10) ; detail au journal"),
		PartEligible);
}
