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

#include "ImageUtils.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedFins.h"
#include "Procedural/WorldseedGlobe.h"
#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedPerlin.h"
#include "Procedural/WorldseedTectonics.h"
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
	// Symetriques faute de mieux : la Terre ne l'est pas tout a fait -- son
	// hemisphere nord est plus chaud, parce qu'il porte plus de terres. On ne
	// compare donc que des ordres de grandeur.
	static const float TerreMoyenneC[9] = {
		26.0f, 25.0f, 21.0f, 16.0f, 9.0f, 2.0f, -6.0f, -14.0f, -22.0f
	};
	// Pluie zonale terrestre sur les TERRES, en millimetres par an. Ordres de
	// grandeur : le creux subtropical vers 20-30 degres, le maximum equatorial,
	// et le desert polaire. C'est la colonne qui manquait pour comprendre
	// pourquoi la bande temperee se remplit de steppe plutot que de foret.
	static const float TerrePluieMm[9] = {
		2000.0f, 1200.0f, 500.0f, 600.0f, 700.0f, 600.0f, 450.0f, 250.0f, 150.0f
	};

	struct FBande
	{
		int32 Cellules = 0;
		int32 Terres = 0;
		double SommeT = 0.0;
		double SommeTMax = 0.0;
		double SommeAlt = 0.0;
		double SommePluie = 0.0;
		int32 Calotte = 0;
		int32 Toundra = 0;
		int32 Taiga = 0;
		int32 Alpin = 0;
		int32 SousZeroEte = 0;   // TempMax < 0 : eligible a la calotte
		int32 Mer = 0;
		int32 Banquise = 0;      // mer prise en glace
	};
	// --- LES HEMISPHERES NE SE MELANGENT PAS -------------------------------
	//
	// La premiere version agregeait |latitude|. Elle melangeait donc le pole
	// NORD, force en OCEAN a dessein -- northPole vaut "ocean", comme
	// l''Arctique -- et le pole SUD, force en CONTINENT comme l''Antarctique.
	// La ligne 80-90 annoncait « 0,0 % emerge » pour les deux, et l''on pouvait
	// en conclure que le forcage continental ne marchait pas alors que la
	// moitie de la mesure ne le concernait pas.
	//
	// C''est la faute que ce depot a deja payee sur le routage des galeries :
	// une mesure qui melange deux populations ne se corrige pas, elle se
	// DECOMPOSE. Dix-huit bandes signees, du nord au sud.
	FBande Bandes[18];

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
			static_cast<int32>((Lat + 90.0f) / 10.0f), 0, 17);

		for (int32 I = 0; I < NX; ++I)
		{
			const int32 Idx = J * NX + I;
			FBande& Ba = Bandes[B];
			++Ba.Cellules;

			const float Elev = World.ElevationM[Idx];
			if (Elev < 0.0f)
			{
				// LA MER SE COMPTE A PART. La banquise n'est pas un biome : on
				// ne peut donc pas la lire dans Index, et la ranger avec les
				// terres gonflerait la calotte d'une surface qui n'en est pas.
				++Ba.Mer;
				if (World.Biomes.Cover.IsValidIndex(Idx)
					&& static_cast<EWorldseedCover>(World.Biomes.Cover[Idx])
						== EWorldseedCover::SeaIce)
				{
					++Ba.Banquise;
				}
				continue;
			}

			++Ba.Terres;
			++TerresTotal;
			Ba.SommeT += T.IsValidIndex(Idx) ? T[Idx] : 0.0f;
			Ba.SommeAlt += Elev;
			Ba.SommePluie += World.Climate.PrecipMm.IsValidIndex(Idx)
				? World.Climate.PrecipMm[Idx] : 0.0f;
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
		TEXT("[Worldseed]   lat       surface   emerge   part des    alt     T an    T ete   Terre    pluie   Terre    calotte  toundra   taiga   alpin"));
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]            du monde   local     terres      m        C        C      C an     %%       %%       %%      %%"));

	int32 EligibleCalotte = 0;

	for (int32 B = 17; B >= 0; --B)
	{
		const FBande& Ba = Bandes[B];
		if (Ba.Cellules == 0) { continue; }

		const float PartSurface = 100.0f * Ba.Cellules / static_cast<float>(NX * NY);
		const float Emerge = 100.0f * Ba.Terres / static_cast<float>(Ba.Cellules);
		const float PartTerres = TerresTotal > 0
			? 100.0f * Ba.Terres / static_cast<float>(TerresTotal) : 0.0f;

		EligibleCalotte += Ba.SousZeroEte;

		// Bornes SIGNEES de la bande, et l''index de la reference terrestre,
		// qui est donnee en valeur absolue de latitude.
		const int32 LatBas = B * 10 - 90;
		const int32 IdxTerre = (B <= 8) ? (8 - B) : (B - 9);

		if (Ba.Terres == 0)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]   %2d-%2d  %8.2f  %6.1f      --        --       --       --  %6.1f       --       --      --      --"),
				LatBas, LatBas + 10, PartSurface, Emerge, TerreMoyenneC[IdxTerre]);
			continue;
		}

		const float MoyT = static_cast<float>(Ba.SommeT / Ba.Terres);
		const float MoyTMax = static_cast<float>(Ba.SommeTMax / Ba.Terres);
		const float MoyAlt = static_cast<float>(Ba.SommeAlt / Ba.Terres);
		const float MoyPluie = static_cast<float>(Ba.SommePluie / Ba.Terres);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   %2d-%2d  %8.2f  %6.1f  %8.2f  %6.0f  %7.1f  %7.1f  %6.1f  %6.0f  %6.0f  %7.1f  %7.1f %7.1f %7.1f"),
			LatBas, LatBas + 10, PartSurface, Emerge, PartTerres, MoyAlt,
			MoyT, MoyTMax, TerreMoyenneC[IdxTerre], MoyPluie, TerrePluieMm[IdxTerre],
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

	// LA BANQUISE, PAR BANDE ET SUR LA MER. Elle ne peut pas se lire dans le
	// tableau ci-dessus, dont toutes les colonnes portent sur les TERRES : au
	// pole nord il n'y en a aucune, et c'est justement la que la banquise doit
	// exister.
	int32 MerTotale = 0;
	int32 BanquiseTotale = 0;
	for (int32 B = 17; B >= 0; --B)
	{
		MerTotale += Bandes[B].Mer;
		BanquiseTotale += Bandes[B].Banquise;
		if (Bandes[B].Banquise > 0)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]   banquise %3d a %3d deg : %.1f %% de la mer de la bande"),
				B * 10 - 90, B * 10 - 80,
				100.0f * Bandes[B].Banquise / FMath::Max(Bandes[B].Mer, 1));
		}
	}
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   banquise : %.2f %% de la mer du monde (Terre : ~3 a 5 selon la saison)"),
		MerTotale > 0 ? 100.0f * BanquiseTotale / MerTotale : 0.0f);
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

namespace
{
	/**
	 * Dimension fractale d'un trait de cote, par comptage de boites.
	 *
	 * ELLE EST FACTORISEE PARCE QU'ON LA MESURE DEUX FOIS -- a la sortie de la
	 * tectonique et sur le relief fini -- et que le depot a une regle contre la
	 * formule recopiee : deux copies divergent a la premiere retouche, et la
	 * comparaison ne voudrait plus rien dire.
	 *
	 * Rend la dimension moyenne et journalise le detail par echelle.
	 */
	float DimensionDuTrait(const TArray<float>& ElevationM,
		const FWorldseedGeometry& Geo, const TCHAR* Etiquette)
	{
		const int32 NX = Geo.NX;
		const int32 NY = Geo.NY;

		// Une terre qui touche la mer par un cote. La carte boucle en longitude :
		// un continent coupe par le bord aurait sinon deux fausses cotes droites.
		TArray<uint8> Cote;
		Cote.SetNumZeroed(NX * NY);
		int32 Cellules = 0;
		for (int32 J = 0; J < NY; ++J)
		{
			for (int32 I = 0; I < NX; ++I)
			{
				const int32 Idx = J * NX + I;
				if (ElevationM[Idx] < 0.0f) { continue; }
				const int32 IG = (I + NX - 1) % NX;
				const int32 ID = (I + 1) % NX;
				const bool bMer =
					(ElevationM[J * NX + IG] < 0.0f)
					|| (ElevationM[J * NX + ID] < 0.0f)
					|| (J > 0 && ElevationM[(J - 1) * NX + I] < 0.0f)
					|| (J < NY - 1 && ElevationM[(J + 1) * NX + I] < 0.0f);
				if (bMer) { Cote[Idx] = 1; ++Cellules; }
			}
		}
		if (Cellules == 0) { return 0.0f; }

		const int32 Tailles[] = { 1, 2, 4, 8, 16, 32 };
		constexpr int32 NbTailles = UE_ARRAY_COUNT(Tailles);

		UE_LOG(LogTemp, Log, TEXT("[Worldseed]   --- %s ---"), Etiquette);
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]     regle      boites   cote de la regle   dimension locale"));

		double SommeX = 0.0, SommeY = 0.0, SommeXY = 0.0, SommeXX = 0.0;
		int32 NbPoints = 0;
		int32 DernierN = 0;
		const float MetresParPixel = FMath::Max(Geo.MetersPerPixel(), 1e-3f);

		for (int32 T = 0; T < NbTailles; ++T)
		{
			const int32 R = Tailles[T];
			const int32 BX = (NX + R - 1) / R;
			const int32 BY = (NY + R - 1) / R;

			TArray<uint8> Boite;
			Boite.SetNumZeroed(BX * BY);
			for (int32 J = 0; J < NY; ++J)
			{
				for (int32 I = 0; I < NX; ++I)
				{
					if (Cote[J * NX + I] != 0) { Boite[(J / R) * BX + (I / R)] = 1; }
				}
			}

			int32 N = 0;
			for (const uint8 B : Boite) { N += B; }
			if (N <= 0) { continue; }

			// DernierN est le compte a la regle DEUX FOIS PLUS FINE, donc plus
			// grand : il va au numerateur, sinon la dimension sort negative.
			if (DernierN > 0)
			{
				const float DimLocale = FMath::Loge(
					static_cast<float>(DernierN) / static_cast<float>(N)) / FMath::Loge(2.0f);
				UE_LOG(LogTemp, Log,
					TEXT("[Worldseed]     %3d px  %8d   %7.0f m        %.2f"),
					R, N, R * MetresParPixel, DimLocale);
			}
			else
			{
				UE_LOG(LogTemp, Log,
					TEXT("[Worldseed]     %3d px  %8d   %7.0f m          --"),
					R, N, R * MetresParPixel);
			}
			DernierN = N;

			const double X = FMath::Loge(1.0 / static_cast<double>(R));
			const double Y = FMath::Loge(static_cast<double>(N));
			SommeX += X; SommeY += Y; SommeXY += X * Y; SommeXX += X * X;
			++NbPoints;
		}

		if (NbPoints < 2) { return 0.0f; }

		const double Denom = NbPoints * SommeXX - SommeX * SommeX;
		const float Dim = (FMath::Abs(Denom) > 1e-12)
			? static_cast<float>((NbPoints * SommeXY - SommeX * SommeY) / Denom)
			: 0.0f;

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]     dimension %.3f   %d cellules de littoral"),
			Dim, Cellules);
		return Dim;
	}
}

FString UWorldseedProbeLibrary::ProbeCotes(int32 Seed, float HeightMeters,
	int32 ResolutionY)
{
	WorldseedPipeline::ReloadRules();

	FString Error;
	const UWorldseedRules* Rules = WorldseedPipeline::GetRules(Error);
	if (!Rules)
	{
		return FString::Printf(TEXT("regles illisibles : %s"), *Error);
	}

	WorldseedPipeline::FResult World;
	if (!WorldseedPipeline::Generate(Seed, HeightMeters, ResolutionY, World, Error))
	{
		return FString::Printf(TEXT("generation impossible : %s"), *Error);
	}

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] === TRAIT DE COTE ==="));

	// --- DEUX TEMOINS, AVANT TOUTE CONCLUSION -------------------------------
	//
	// UNE METRIQUE NON VALIDEE NE MESURE RIEN, et ce depot l'a deja paye : le
	// comptage des composants d'herbe de Landscape rendait zero, on en a
	// conclu a un defaut du materiau, et la demo du pack -- qui a pourtant un
	// tapis visible -- rendait le meme zero. Le compteur ne mesurait rien.
	//
	// Ici cinq reglages tres differents ont rendu la meme dimension a deux
	// millemes pres. Avant d'en conclure que le monde est plat, il faut savoir
	// si la sonde SAIT voir autre chose. On lui donne donc deux formes dont la
	// reponse est connue : un disque, qui vaut 1,00 par definition, et le
	// contour d'un bruit fractal de gain 0,7, qui vaut 2 - H = 1,49.
	{
		const int32 NX = World.Geometry.NX;
		const int32 NY = World.Geometry.NY;

		TArray<float> Disque;
		Disque.SetNumUninitialized(NX * NY);
		const float CX = NX * 0.5f;
		const float CY = NY * 0.5f;
		const float Rayon = NY * 0.35f;
		for (int32 J = 0; J < NY; ++J)
		{
			for (int32 I = 0; I < NX; ++I)
			{
				const float DX = I - CX;
				const float DY = J - CY;
				// Positif dedans : la sonde lit « terre » au-dessus de zero.
				Disque[J * NX + I] = Rayon - FMath::Sqrt(DX * DX + DY * DY);
			}
		}
		DimensionDuTrait(Disque, World.Geometry, TEXT("TEMOIN : un disque (attendu 1,00)"));

		TArray<float> Bruit;
		WorldseedPerlin::FBMSphere(Bruit, World.Geometry, 6.0f, 8, Seed + 1234, 2.0f, 0.7f);
		// Centre sur zero pour que le contour coupe au milieu du champ.
		float Somme = 0.0f;
		for (const float V : Bruit) { Somme += V; }
		const float Moyenne = Somme / FMath::Max(Bruit.Num(), 1);
		for (float& V : Bruit) { V -= Moyenne; }
		DimensionDuTrait(Bruit, World.Geometry,
			TEXT("TEMOIN : contour d'un fBm de gain 0,7 (attendu ~1,49)"));
	}

	// --- LA MESURE QUI TRANCHE ---------------------------------------------
	//
	// La cote NAIT-ELLE lisse de la tectonique, ou le DEVIENT-elle ensuite ?
	// Quatre reglages du bruit de cote rendaient le meme nombre de cellules de
	// littoral a un demi pour cent pres, et couper la diffusion de l'erosion
	// n'y changeait rien non plus : quelque chose plafonne le trait en amont.
	// On mesure donc la MEME grandeur aux deux bouts de la chaine.
	//
	// La tectonique recale elle-meme le niveau de la mer sur la part de terres
	// visee, donc son relief porte deja un trait de cote comparable.
	FWorldseedTectonicResult Tecto;
	if (WorldseedTectonics::Generate(*Rules, World.Geometry, Seed, Tecto)
		&& Tecto.ElevationM.Num() == World.ElevationM.Num())
	{
		const float DimAvant = DimensionDuTrait(Tecto.ElevationM, World.Geometry,
			TEXT("A LA SORTIE DE LA TECTONIQUE (avant erosion)"));
		const float DimApres = DimensionDuTrait(World.ElevationM, World.Geometry,
			TEXT("SUR LE RELIEF FINI (apres erosion et littoral)"));

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   VERDICT : %.3f a la naissance, %.3f a l'arrivee ")
			TEXT("(ecart %+.3f)"),
			DimAvant, DimApres, DimApres - DimAvant);
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   reperes : droite 1,00 ; Afrique du Sud 1,05 ; ")
			TEXT("Grande-Bretagne 1,25 ; Norvege 1,52"));

		return FString::Printf(
			TEXT("trait de cote : %.3f avant erosion, %.3f apres (Grande-Bretagne 1,25)"),
			DimAvant, DimApres);
	}

	const float Dim = DimensionDuTrait(World.ElevationM, World.Geometry,
		TEXT("SUR LE RELIEF FINI"));
	return FString::Printf(TEXT("dimension fractale : %.3f"), Dim);
}

/**
 * Ecrire la carte du monde A PLAT, pour REGARDER la forme des continents.
 *
 * POURQUOI ELLE NE FAIT PAS DOUBLE EMPLOI AVEC ProbeCotes. La dimension
 * fractale quantifie la rugosite FINE du trait. Un continent peut avoir une
 * silhouette parfaitement polygonale -- des segments de plusieurs kilometres
 * se rejoignant a angles nets -- et mesurer 1,01 quand meme : le chiffre ne
 * voit pas ce qui se juge a l'oeil. Et ce depot a une regle, payee plusieurs
 * fois, selon laquelle une forme qui n'a pas ete VUE n'est pas validee.
 *
 * POURQUOI PAS LE GLOBE DU MENU. Il montre deja les biomes, mais sur une
 * SPHERE : on n'en voit qu'une face, elle tourne, et deux reglages ne s'y
 * comparent donc pas image contre image. La carte equivalente-aire, elle, se
 * superpose d'un essai a l'autre.
 */
FString UWorldseedProbeLibrary::ProbeCarte(int32 Seed, float HeightMeters,
	int32 ResolutionY, const FString& Etiquette)
{
	WorldseedPipeline::ReloadRules();

	FString Error;
	WorldseedPipeline::FResult World;
	if (!WorldseedPipeline::Generate(Seed, HeightMeters, ResolutionY, World, Error))
	{
		return FString::Printf(TEXT("generation impossible : %s"), *Error);
	}

	const FWorldseedGeometry& Geo = World.Geometry;
	const int32 NX = Geo.NX;
	const int32 NY = Geo.NY;
	const int32 Total = Geo.CellCount();

	// Les biomes peuvent manquer -- une generation de secours n'en produit
	// pas. On le DIT plutot que de rendre une carte grise sans explication.
	const bool bBiomes = World.Biomes.Index.Num() == Total
		&& World.Biomes.Cover.Num() == Total;

	// Le fond marin porte un degrade de profondeur : sans lui le plateau
	// continental disparait, et l'on ne voit plus POURQUOI une cote est la.
	float Fond = 0.0f;
	for (int32 I = 0; I < Total; ++I)
	{
		Fond = FMath::Min(Fond, World.ElevationM[I]);
	}
	Fond = FMath::Min(Fond, -1.0f);

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Total);

	for (int32 I = 0; I < Total; ++I)
	{
		const float Z = World.ElevationM[I];
		FLinearColor C;

		if (Z > 0.0f)
		{
			C = bBiomes
				? WorldseedBiomes::Colour(WorldseedBiomes::AppearanceBiome(
					World.Biomes.Index[I], World.Biomes.Cover[I]))
				: FLinearColor(0.45f, 0.42f, 0.36f);
		}
		else
		{
			// Clair sur le plateau, sombre dans l'abysse.
			const float T = FMath::Clamp(Z / Fond, 0.0f, 1.0f);
			C = FMath::Lerp(FLinearColor(0.40f, 0.60f, 0.76f),
				FLinearColor(0.03f, 0.08f, 0.20f), T);
		}

		Pixels[I] = C.ToFColor(true);
	}

	// LE TRAIT DE COTE EST SOULIGNE, parce que c'est lui qu'on vient juger.
	// Sans ce lisere, un biome cotier pale contre une mer peu profonde rend la
	// frontiere illisible a l'echelle ou l'on regarde la FORME. On ecrit dans
	// une COPIE : souligner en place propagerait le trait de proche en proche.
	TArray<FColor> Trait = Pixels;
	for (int32 J = 0; J < NY; ++J)
	{
		for (int32 I = 0; I < NX; ++I)
		{
			const int32 K = J * NX + I;
			if (World.ElevationM[K] <= 0.0f)
			{
				continue;
			}

			// Le monde est cyclique en longitude : le trait ne doit pas se
			// couper au meridien de bordure.
			const int32 IG = (I + NX - 1) % NX;
			const int32 ID = (I + 1) % NX;
			const int32 JB = FMath::Max(J - 1, 0);
			const int32 JH = FMath::Min(J + 1, NY - 1);

			const bool bBord = World.ElevationM[J * NX + IG] <= 0.0f
				|| World.ElevationM[J * NX + ID] <= 0.0f
				|| World.ElevationM[JB * NX + I] <= 0.0f
				|| World.ElevationM[JH * NX + I] <= 0.0f;

			if (bBord)
			{
				Trait[K] = FColor(18, 18, 24);
			}
		}
	}

	const FString Nom = Etiquette.IsEmpty() ? TEXT("carte") : Etiquette;
	const FString Chemin = FPaths::Combine(FPaths::ProjectSavedDir(),
		TEXT("Worldseed"), TEXT("Cartes"), Nom + TEXT(".png"));

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Chemin), true);

	const FImageView Image(Trait.GetData(), NX, NY);
	if (!FImageUtils::SaveImageAutoFormat(*Chemin, Image))
	{
		return FString::Printf(TEXT("ecriture impossible : %s"), *Chemin);
	}

	const FString Bilan = FString::Printf(
		TEXT("carte ecrite : %s (%d x %d, terres %.2f %%, biomes %s)"),
		*Chemin, NX, NY, World.LandRatio * 100.0f,
		bBiomes ? TEXT("oui") : TEXT("NON -- altitude seule"));

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] %s"), *Bilan);
	return Bilan;
}


// --------------------------------------------- le pointage sur le globe

/**
 * Controle ALLER-RETOUR de la projection du globe.
 *
 * POURQUOI ELLE EXISTE. Partager une formule entre le rendu, le pointage et
 * le repere garantit qu'ils sont D'ACCORD -- pas qu'ils ont RAISON. Une
 * projection partagee mais fausse est partagee et fausse. Le seul controle
 * qui tranche est l'aller-retour : une latitude et une longitude connues,
 * projetees vers l'image puis reinversees, doivent revenir sur elles-memes.
 *
 * Elle ne genere AUCUN monde : la projection ne depend que de l'orientation
 * du globe. Elle tourne donc en une fraction de seconde, ce qui est la
 * condition pour qu'on la relance apres chaque retouche.
 */
FString UWorldseedProbeLibrary::ProbePointage()
{
	UE_LOG(LogTemp, Log, TEXT("[Worldseed] === POINTAGE DU GLOBE ==="));

	// PLUSIEURS ORIENTATIONS, et les bornes en font partie. Le menu laisse
	// l'inclinaison aller de -80 a +80 degres ; une formule juste au repos
	// peut se tromper de signe des qu'on bascule de l'autre cote, et c'est
	// exactement le genre de faute qu'un seul cas d'essai ne voit pas.
	const float Inclinaisons[] = { 0.0f, 18.0f, -45.0f, 80.0f, -80.0f };
	const float Rotations[] = { 0.0f, 37.0f, 180.0f, 355.0f };

	double PireLat = 0.0;
	double PireLon = 0.0;
	double PireLatFranc = 0.0;
	double PireLonFranc = 0.0;
	double PirePixel = 0.0;
	int32 Testes = 0;
	int32 Caches = 0;
	int32 Incoherents = 0;

	constexpr int32 Res = 1024;

	for (const float Tilt : Inclinaisons)
	{
		for (const float Rot : Rotations)
		{
			WorldseedGlobe::FGlobeSettings Reglages;
			Reglages.TiltDeg = Tilt;
			Reglages.LongitudeOffsetDeg = Rot;
			const WorldseedGlobe::FCadreGlobe Cadre = WorldseedGlobe::CadreGlobe(Reglages);

			for (float Lat = -85.0f; Lat <= 85.0f; Lat += 5.0f)
			{
				for (float Lon = 0.0f; Lon < 360.0f; Lon += 5.0f)
				{
					float X = 0.0f;
					float Y = 0.0f;
					if (!WorldseedGlobe::CadreDepuisLatLon(Lat, Lon, Cadre, X, Y))
					{
						++Caches;
						continue;
					}

					const WorldseedGlobe::FPointeGlobe P =
						WorldseedGlobe::PointerCadre(X, Y, Cadre);

					// UN POINT DE LA FACE VISIBLE DOIT TOMBER DANS LE DISQUE.
					// Si les deux sens ne s'accordent pas la-dessus, inutile
					// de regarder les degres : c'est la geometrie qui est
					// fausse, pas la precision.
					if (!P.bSurLeGlobe)
					{
						++Incoherents;
						continue;
					}

					++Testes;

					const double DLat = FMath::Abs(P.LatitudeDeg - Lat);
					double DLon = FMath::Abs(P.LongitudeDeg - Lon);
					if (DLon > 180.0) { DLon = 360.0 - DLon; }

					// Aux poles la longitude n'a plus de sens : tous les
					// meridiens s'y rejoignent, et un ecart d'un pixel y vaut
					// des dizaines de degres. On la pondere par le cosinus,
					// c'est-a-dire par la distance reelle sur la sphere.
					DLon *= FMath::Cos(FMath::DegreesToRadians(Lat));

					PireLat = FMath::Max(PireLat, DLat);
					PireLon = FMath::Max(PireLon, DLon);

					// LE LIMBE EST MAL CONDITIONNE PAR NATURE, et le taire
					// serait malhonnete dans les deux sens : on rend donc
					// DEUX chiffres, celui de tout le disque et celui du
					// disque franc. Pres du bord, la derivee de l'arc sinus
					// diverge et un demi-pixel y vaut plusieurs degres -- ce
					// n'est pas un defaut de la formule, c'est la projection.
					if (P.Rayon01 < 0.98f)
					{
						PireLatFranc = FMath::Max(PireLatFranc, DLat);
						PireLonFranc = FMath::Max(PireLonFranc, DLon);
					}

					// Le second aller-retour : cadre -> pixel -> coordonnees
					// de texture -> cadre. C'est ce chemin-la que le clic
					// emprunte, et l'inversion de l'axe vertical n'y est
					// ecrite qu'une fois -- raison de plus pour la verifier.
					float PX = 0.0f;
					float PY = 0.0f;
					WorldseedGlobe::PixelDepuisCadre(X, Y, Res, PX, PY);

					float RX = 0.0f;
					float RY = 0.0f;
					WorldseedGlobe::CadreDepuisUV(
						PX / static_cast<float>(Res - 1),
						PY / static_cast<float>(Res - 1), RX, RY);

					PirePixel = FMath::Max(PirePixel,
						FMath::Max(FMath::Abs(RX - X), FMath::Abs(RY - Y)));
				}
			}
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   %d points sur la face visible, %d sur la face cachee ")
		TEXT("(non testables), %d incoherents"),
		Testes, Caches, Incoherents);
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   disque entier : latitude %.6f deg, longitude %.6f deg"),
		PireLat, PireLon);
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   hors limbe    : latitude %.6f deg, longitude %.6f deg"),
		PireLatFranc, PireLonFranc);
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   cadre -> pixel -> cadre : %.8f"), PirePixel);

	// LE SEUIL PORTE SUR LE DISQUE FRANC, et il est large a dessein : on
	// cherche une faute de FORMULE -- un signe, un axe, une transposee -- qui
	// se compte en degres, pas une derive de virgule flottante.
	const bool bBon = (Incoherents == 0)
		&& (PireLatFranc < 0.01) && (PireLonFranc < 0.01) && (PirePixel < 1e-4);

	const FString Verdict = FString::Printf(
		TEXT("VERDICT : %s -- %d points, pire ecart hors limbe %.6f deg"),
		bBon ? TEXT("la projection et son inverse s'accordent")
			: TEXT("DESACCORD, la projection est fausse"),
		Testes, FMath::Max(PireLatFranc, PireLonFranc));

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] %s"), *Verdict);
	return Verdict;
}
