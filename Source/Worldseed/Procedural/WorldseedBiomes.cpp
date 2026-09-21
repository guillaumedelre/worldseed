// Worldseed - etape 5 : classification des biomes.

#include "Procedural/WorldseedBiomes.h"

#include "Procedural/WorldseedClimate.h"
#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedPerlin.h"
#include "Procedural/WorldseedWind.h"

#include "Async/ParallelFor.h"

#include <atomic>
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	const TCHAR* BIO = TEXT("biomes");

	/**
	 * Le SUBSTRAT a sa propre section, et ce n'est pas cosmetique : la roche a
	 * nu est posee par la PENTE, l'estran par l'ALTITUDE. Ni l'une ni l'autre
	 * ne sort du diagramme de Whittaker, et les laisser sous "biomes"
	 * entretenait exactement la confusion qu'on a corrigee dans le code.
	 */

	constexpr int32 BiomeCount = static_cast<int32>(EWorldseedBiome::Count);

	/**
	 * LE REGISTRE PUBLIE, source unique des noms, couleurs et matieres.
	 *
	 * Il remplace quatre tables qui etaient recopiees a la main ici. Il est
	 * rempli UNE FOIS, a la lecture des regles, et lu ensuite sans verrou : les
	 * fils de maillage l'interrogent pour peindre leurs sommets, et ils ne
	 * demarrent jamais avant la classification. C'est la seule facon de le
	 * rendre lisible depuis les fonctions libres Colour(), Name() et
	 * SlotWeights() sans changer la signature de leurs quarante appelants.
	 *
	 * Tant qu'il n'est pas publie, les fonctions rendent des valeurs neutres
	 * plutot qu'un tableau code en dur : un gris et un nom vide se voient, une
	 * valeur par defaut plausible se cache.
	 */
	TArray<FWorldseedBiomeEntry> RegistrePublie;
	std::atomic<bool> bRegistrePret{ false };

	const FWorldseedBiomeEntry& EntreeDe(int32 Id)
	{
		static const FWorldseedBiomeEntry Neutre;
		if (!bRegistrePret.load(std::memory_order_acquire)
			|| !RegistrePublie.IsValidIndex(Id))
		{
			return Neutre;
		}
		return RegistrePublie[Id];
	}

	EWorldseedBiome BiomeFromKey(const TArray<FWorldseedBiomeEntry>& Registre,
		const FString& Key)
	{
		for (int32 I = 0; I < Registre.Num(); ++I)
		{
			if (Key == Registre[I].Key)
			{
				return static_cast<EWorldseedBiome>(I);
			}
		}
		return EWorldseedBiome::Grassland;
	}

	/** Dilatation binaire a quatre voisins, longitude enroulee. */
	void Dilate(TArray<bool>& Mask, int32 NX, int32 NY, int32 Iterations)
	{
		if (Iterations <= 0)
		{
			return;
		}

		TArray<bool> Source;
		for (int32 Pass = 0; Pass < Iterations; ++Pass)
		{
			Source = Mask;
			ParallelFor(NY, [&](int32 Row)
			{
				for (int32 Col = 0; Col < NX; ++Col)
				{
					const int32 Index = Row * NX + Col;
					if (Source[Index])
					{
						continue;
					}

					const int32 Left = ((Col - 1 + NX) % NX);
					const int32 Right = ((Col + 1) % NX);

					const bool bNear =
						Source[Row * NX + Left] || Source[Row * NX + Right]
						|| (Row > 0 && Source[(Row - 1) * NX + Col])
						|| (Row < NY - 1 && Source[(Row + 1) * NX + Col]);

					if (bNear)
					{
						Mask[Index] = true;
					}
				}
			});
		}
	}
}

FWorldseedBiomeRules FWorldseedBiomeRules::FromRules(const UWorldseedRules& Rules,
	const FWorldseedGeometry& Geo)
{
	FWorldseedBiomeRules Out;

	auto Num = [&Rules](const TCHAR* Key, double Fallback)
	{
		return static_cast<float>(Rules.Num(BIO, Key, Fallback));
	};

	// --- le registre, EN PREMIER ----------------------------------------------
	// Le diagramme de Whittaker nomme ses biomes par leur CLE : sans registre,
	// BiomeFromKey ne saurait pas les resoudre et tout retomberait sur la
	// prairie, en silence.
	Out.Registre.SetNum(BiomeCount);
	if (const TArray<TSharedPtr<FJsonValue>>* Entrees =
		Rules.Array(BIO, TEXT("registre")))
	{
		for (const TSharedPtr<FJsonValue>& Valeur : *Entrees)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Valeur.IsValid() || !Valeur->TryGetObject(Obj) || !Obj) { continue; }

			int32 Id = -1;
			if (!(*Obj)->TryGetNumberField(TEXT("id"), Id)
				|| !Out.Registre.IsValidIndex(Id))
			{
				continue;
			}

			FWorldseedBiomeEntry& E = Out.Registre[Id];
			(*Obj)->TryGetStringField(TEXT("cle"), E.Key);
			(*Obj)->TryGetStringField(TEXT("libelle"), E.Label);
			(*Obj)->TryGetBoolField(TEXT("attribue"), E.bAssigned);

			const TArray<TSharedPtr<FJsonValue>>* Couleur = nullptr;
			if ((*Obj)->TryGetArrayField(TEXT("couleur"), Couleur) && Couleur->Num() >= 3)
			{
				E.Colour = FLinearColor(FColor(
					static_cast<uint8>((*Couleur)[0]->AsNumber()),
					static_cast<uint8>((*Couleur)[1]->AsNumber()),
					static_cast<uint8>((*Couleur)[2]->AsNumber()), 255));
			}

			const TArray<TSharedPtr<FJsonValue>>* Matieres = nullptr;
			if ((*Obj)->TryGetArrayField(TEXT("matieres"), Matieres) && Matieres->Num() >= 4)
			{
				E.Slots = FLinearColor(
					static_cast<float>((*Matieres)[0]->AsNumber()),
					static_cast<float>((*Matieres)[1]->AsNumber()),
					static_cast<float>((*Matieres)[2]->AsNumber()),
					static_cast<float>((*Matieres)[3]->AsNumber()));
			}
		}
	}

	// PUBLICATION, une fois pour toutes. Les fils de maillage liront ce tableau
	// sans verrou ; ils ne demarrent jamais avant la classification, qui passe
	// forcement par ici.
	RegistrePublie = Out.Registre;
	bRegistrePret.store(true, std::memory_order_release);

	Out.TreeLineTempC = Num(TEXT("treeLineTempC"), 4.0);
	Out.TreeLineWarmestMonthC = Num(TEXT("treeLineWarmestMonthC"), 10.0);
	Out.TaigaMinPrecipMm = Num(TEXT("taigaMinPrecipMm"), 350.0);
	Out.ColdDesertMaxTempC = Num(TEXT("coldDesertMaxTempC"), 18.0);
	// --- LE SEUIL ALPIN SUIT L'ECHELLE VERTICALE ---------------------------
	//
	// IL EST COMPARE A UNE ALTITUDE, DONC IL DOIT SE MESURER DANS LA MEME
	// UNITE QU'ELLE. WorldseedTectonics met a l'echelle tout ce qui fabrique le
	// relief -- oceanDepthM, continentBaseM, poleContinentBonusM,
	// mountainHeightM -- par WorldseedVerticalScale. Ce seuil-la ne l'etait
	// pas : il restait a 212,5 m pendant que le relief passait a 1609 m.
	//
	// CE QUE CELA COUTAIT, mesure par ProbeZonal sur la graine 1337 en
	// 64 x 32 km : l'altitude MOYENNE des terres vaut 420 a 530 m selon la
	// bande, donc le seuil etait franchi presque partout, et l'etage alpin
	// confisquait 70,3 % des terres a 50-60 degres et 47,9 % a 40-50 -- la ou
	// devraient se trouver la taiga et la foret temperee, qui tombaient a 14,9
	// et 1,7 %. Le froid n'y manquait pas : il portait la mauvaise etiquette.
	//
	// C'est le piege que le depot a deja paye deux fois et consigne : « une
	// constante metrique en dur suffit a fausser un monde entier », « une
	// valeur d'auteur JUSTE devient fausse quand on change l'echelle du
	// monde ». La valeur du fichier garde son sens -- le seuil d'un monde de
	// REFERENCE -- et suit desormais toute taille de carte.
	Out.AlpineMinElevationM = Num(TEXT("alpineMinElevationM"), 212.5)
		* WorldseedVerticalScale(Rules, Geo.HeightM);
	Out.PermanentIceTempC = Num(TEXT("permanentIceTempC"), 0.0);
	Out.SeaIceTempC = static_cast<float>(
		Rules.Num(TEXT("glace"), TEXT("banquiseTempC"), 0.0));
	auto Sub = [&Rules](const TCHAR* Key, double Fallback)
	{
		return static_cast<float>(Rules.Num(WorldseedSection::Substrat, Key, Fallback));
	};

	Out.BareRockSlopeDeg = Sub(TEXT("bareRockSlopeDeg"), 55.0);

	Out.BeachElevationM = Sub(TEXT("beachElevationM"), 5.5);
	Out.BeachWidthM = Sub(TEXT("beachWidthM"), 110.0);
	Out.BeachSlopeFlatDeg = Sub(TEXT("beachSlopeFlatDeg"), 8.0);
	Out.BeachSlopeSteepDeg = Sub(TEXT("beachSlopeSteepDeg"), 30.0);
	Out.BeachWindwardBonus = Sub(TEXT("beachWindwardBonus"), 0.6);

	Out.MarshMaxSlopeDeg = Num(TEXT("marshMaxSlopeDeg"), 2.0);
	Out.MarshMinPrecipMm = Num(TEXT("marshMinPrecipMm"), 900.0);

	Out.MediterraneanSummerFracMax = Num(TEXT("mediterraneanSummerFracMax"), 0.38);
	Out.MediterraneanMinTempC = Num(TEXT("mediterraneanMinTempC"), 6.0);
	Out.MediterraneanMaxTempC = Num(TEXT("mediterraneanMaxTempC"), 20.0);
	Out.MediterraneanMinPrecipMm = Num(TEXT("mediterraneanMinPrecipMm"), 300.0);
	Out.MediterraneanMaxPrecipMm = Num(TEXT("mediterraneanMaxPrecipMm"), 1000.0);

	// La saisonnalite ne depend que de la latitude : une valeur par LIGNE
	// suffit, et c'est WorldseedClimate qui la calcule -- la formule de la
	// circulation ne doit exister qu'a un seul endroit.
	Out.SummerRainFracByRow.SetNumUninitialized(FMath::Max(Geo.NY, 0));
	for (int32 Row = 0; Row < Geo.NY; ++Row)
	{
		Out.SummerRainFracByRow[Row] = WorldseedClimate::SummerRainFraction(
			Rules, Geo, Geo.LatitudeDegForRow(Row));
	}

	// --- diagramme de Whittaker ---------------------------------------------
	if (const TArray<TSharedPtr<FJsonValue>>* Bands =
		Rules.Array(BIO, TEXT("whittakerBands")))
	{
		for (const TSharedPtr<FJsonValue>& Entry : *Bands)
		{
			const TSharedPtr<FJsonObject>* BandObj = nullptr;
			if (!Entry.IsValid() || !Entry->TryGetObject(BandObj) || !BandObj->IsValid())
			{
				continue;   // les cles de commentaire ne sont pas des bandes
			}

			FWorldseedWhittakerBand Band;
			if (!(*BandObj)->TryGetNumberField(TEXT("tMax"), Band.MaxTempC))
			{
				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>* Cuts = nullptr;
			if (!(*BandObj)->TryGetArrayField(TEXT("cuts"), Cuts))
			{
				continue;
			}

			for (const TSharedPtr<FJsonValue>& CutValue : *Cuts)
			{
				const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
				if (!CutValue.IsValid() || !CutValue->TryGetArray(Pair) || Pair->Num() < 2)
				{
					continue;
				}

				FWorldseedWhittakerCut Cut;
				Cut.MaxPrecipMm = static_cast<float>((*Pair)[0]->AsNumber());
				Cut.Biome = BiomeFromKey(Out.Registre, (*Pair)[1]->AsString());
				Band.Cuts.Add(Cut);
			}

			if (Band.Cuts.Num() > 0)
			{
				Out.Bands.Add(MoveTemp(Band));
			}
		}
	}

	return Out;
}

namespace WorldseedBiomes
{
	FLinearColor Colour(EWorldseedBiome Biome)
	{
		return EntreeDe(FMath::Clamp(static_cast<int32>(Biome), 0, BiomeCount - 1)).Colour;
	}

	FLinearColor CoverColour(EWorldseedCover Cover)
	{
		// Les trois teintes d'eau de debugColors, a leur place d'origine dans la
		// table : la couverture et le biome partagent la meme palette.
		switch (Cover)
		{
		case EWorldseedCover::Ocean: return Colour(EWorldseedBiome::Ocean);
		case EWorldseedCover::Lake:  return Colour(EWorldseedBiome::Lake);
		case EWorldseedCover::River: return Colour(EWorldseedBiome::River);
		case EWorldseedCover::Rock:  return Colour(EWorldseedBiome::BareRock);
		case EWorldseedCover::Beach: return Colour(EWorldseedBiome::Beach);

		// LA BANQUISE N'EST PAS LA CALOTTE, et elle ne doit pas lui ressembler
		// tout a fait : un blanc legerement bleute, un peu plus sombre, dit
		// qu'on est sur de la glace FLOTTANTE et non sur un continent.
		case EWorldseedCover::SeaIce: return FLinearColor(0.82f, 0.88f, 0.94f);

		default:                     return FLinearColor::Transparent;
		}
	}

	const TCHAR* CoverName(EWorldseedCover Cover)
	{
		switch (Cover)
		{
		case EWorldseedCover::Ocean: return TEXT("ocean");
		case EWorldseedCover::Lake:  return TEXT("lac");
		case EWorldseedCover::River: return TEXT("riviere");
		case EWorldseedCover::Rock:  return TEXT("roche nue");
		case EWorldseedCover::Beach: return TEXT("plage");
		default:                     return TEXT("a decouvert");
		}
	}

	EWorldseedBiome AppearanceBiome(uint8 BiomeIndex, uint8 Cover)
	{
		switch (static_cast<EWorldseedCover>(Cover))
		{
		case EWorldseedCover::Rock:  return EWorldseedBiome::BareRock;
		case EWorldseedCover::Beach: return EWorldseedBiome::Beach;
		default:
			return static_cast<EWorldseedBiome>(
				FMath::Clamp<int32>(BiomeIndex, 0, BiomeCount - 1));
		}
	}

	const TCHAR* Name(EWorldseedBiome Biome)
	{
		// LA CHAINE DOIT SURVIVRE A L'APPELANT : les journaux la passent par %s
		// longtemps apres le retour. Le registre, lui, ne bouge plus une fois
		// publie, donc sa chaine est stable.
		return *EntreeDe(FMath::Clamp(static_cast<int32>(Biome), 0, BiomeCount - 1)).Label;
	}

	FLinearColor SlotWeights(EWorldseedBiome Biome)
	{
		// LES MELANGES NE SONT PAS DECORATIFS, et ils vivent maintenant dans le
		// registre. Une savane est de l'herbe qui laisse voir la terre, une
		// steppe l'inverse ; un etage alpin est de la roche que la mousse
		// colonise par plaques. Ce sont ces proportions qui font qu'on
		// reconnait un biome sans lire son nom -- raison de plus pour qu'elles
		// soient reglables sans recompiler.
		return EntreeDe(FMath::Clamp(static_cast<int32>(Biome), 0, BiomeCount - 1)).Slots;
	}

	void SlopeDegrees(const TArray<float>& ElevationM, const FWorldseedGeometry& Geometry,
		TArray<float>& OutSlopeDeg)
	{
		const int32 Count = Geometry.CellCount();
		OutSlopeDeg.SetNumZeroed(Count);
		if (ElevationM.Num() != Count)
		{
			return;
		}

		TArray<float> DY;
		TArray<float> DX;
		WorldseedGrid::Gradient(ElevationM, Geometry.NX, Geometry.NY,
			Geometry.MetersPerPixel(), DY, DX);

		ParallelFor(Count, [&](int32 I)
		{
			OutSlopeDeg[I] = FMath::RadiansToDegrees(
				FMath::Atan(FMath::Sqrt(DX[I] * DX[I] + DY[I] * DY[I])));
		});
	}

	/**
	 * Le biome que donne un CLIMAT seul. Voir la declaration pour le pourquoi.
	 */
	EWorldseedBiome FromClimate(float T, float P, float TempMaxC,
		float SummerFrac, bool bTempMaxConnu, const FWorldseedBiomeRules& Rules)
	{
		if (Rules.Bands.Num() == 0 || Rules.Bands.Last().Cuts.Num() == 0)
		{
			return EWorldseedBiome::Ocean;
		}

		const bool bSummerFrac = true;

		// --- diagramme de Whittaker ---------------------------------------
		// Bandes de la plus froide a la plus chaude ; la premiere dont
		// tMax depasse la temperature l'emporte, puis le premier seuil
		// de pluie depasse a l'interieur.
		EWorldseedBiome Biome = Rules.Bands.Last().Cuts.Last().Biome;
		for (const FWorldseedWhittakerBand& Band : Rules.Bands)
		{
			if (T > Band.MaxTempC)
			{
				continue;
			}
			for (const FWorldseedWhittakerCut& Cut : Band.Cuts)
			{
				if (P <= Cut.MaxPrecipMm)
				{
					Biome = Cut.Biome;
					break;
				}
			}
			break;
		}

		// --- la limite des arbres se joue sur l'ETE, pas sur l'annee -------
		//
		// C'est le critere de Koppen (isotherme 10 degres du mois le plus
		// chaud) et c'est le seul qui marche : un arbre a besoin d'une
		// SAISON DE CROISSANCE. Le diagramme, qui ne connait que la
		// moyenne annuelle, ne peut pas le voir -- le releve reel de la
		// toundra polaire est PLUS CHAUD en moyenne (-8,4 C) que celui du
		// subarctique a hiver severe (-11,6 C), qui porte pourtant de la
		// taiga. Aucun seuil sur l'annee ne separe ces deux-la.
		//
		// Le critere joue DANS LES DEUX SENS, sans quoi il ne ferait que
		// deshabiller les terres froides : sous la limite, une foret
		// redevient toundra ; au-dessus, une toundra assez arrosee devient
		// de la taiga.
		if (bTempMaxConnu)
		{
			const bool bArbresPossibles = TempMaxC >= Rules.TreeLineWarmestMonthC;

			if (!bArbresPossibles)
			{
				if (Biome == EWorldseedBiome::Taiga
					|| Biome == EWorldseedBiome::TemperateForest
					|| Biome == EWorldseedBiome::TemperateRainforest)
				{
					Biome = EWorldseedBiome::Tundra;
				}
			}
			else if (Biome == EWorldseedBiome::Tundra
				&& P > Rules.TaigaMinPrecipMm)
			{
				Biome = EWorldseedBiome::Taiga;
			}
		}

		// --- le climat mediterraneen : un ETE SEC sous une annee qui ne l'est pas
		//
		// C'est le seul grand biome terrestre que le diagramme ne pouvait
		// pas produire, parce qu'il ne connaissait que le CUMUL annuel.
		// Trois releves reels le montraient : Mediterranean_Cool_Summer
		// (13,2 C, 809 mm) tombait en foret temperee, alors qu'il n'y
		// pousse ni la meme foret ni la meme chose.
		//
		// IL NE PREND QUE CE QUI LUI REVIENT : seules les cases que le
		// diagramme donne a une vegetation temperee ou herbacee peuvent
		// basculer. Une foret tropicale a mousson a elle aussi une saison
		// seche, et elle n'est pas mediterraneenne pour autant.
		if (bSummerFrac
			&& (Biome == EWorldseedBiome::TemperateForest
				|| Biome == EWorldseedBiome::Grassland
				|| Biome == EWorldseedBiome::Steppe)
			&& SummerFrac < Rules.MediterraneanSummerFracMax
			&& T >= Rules.MediterraneanMinTempC
			&& T <= Rules.MediterraneanMaxTempC
			&& P >= Rules.MediterraneanMinPrecipMm
			&& P <= Rules.MediterraneanMaxPrecipMm)
		{
			Biome = EWorldseedBiome::Mediterranean;
		}

		// --- un desert FROID se definit par son hiver -----------------------
		// 18 degres de moyenne annuelle est la frontiere k/h de Koppen.
		// Le diagramme ne pouvait pas trancher : le releve reel du
		// Cold_Desert est a +17 C de moyenne, donc dans la bande chaude.
		if (Biome == EWorldseedBiome::HotDesert && T < Rules.ColdDesertMaxTempC)
		{
			Biome = EWorldseedBiome::ColdDesert;
		}
		return Biome;
	}


	void Classify(const FWorldseedGeometry& Geometry, const TArray<float>& ElevationM,
		const TArray<float>& TempMeanC, const TArray<float>& TempMaxC,
		const TArray<float>& PrecipMm, const TArray<bool>& LakeMask,
		const TArray<bool>& RiverMask, const FWorldseedBiomeRules& Rules,
		FWorldseedBiomeMap& Out)
	{
		const int32 NX = Geometry.NX;
		const int32 NY = Geometry.NY;
		const int32 Count = Geometry.CellCount();

		Out.Index.Init(static_cast<uint8>(EWorldseedBiome::Grassland), Count);
		Out.Cover.Init(static_cast<uint8>(EWorldseedCover::None), Count);
		FMemory::Memzero(Out.LandSharePct, sizeof(Out.LandSharePct));
		FMemory::Memzero(Out.CoverSharePct, sizeof(Out.CoverSharePct));

		if (ElevationM.Num() != Count || TempMeanC.Num() != Count
			|| PrecipMm.Num() != Count || Rules.Bands.Num() == 0)
		{
			return;
		}

		const double StartTime = FPlatformTime::Seconds();

		SlopeDegrees(ElevationM, Geometry, Out.SlopeDeg);

		const bool bHasTempMax = (TempMaxC.Num() == Count);
		const bool bHasLakes = (LakeMask.Num() == Count);
		const bool bHasRivers = (RiverMask.Num() == Count);

		// --- distance a l'ocean, pour l'estran -----------------------------------
		TArray<uint8> LandMask;
		LandMask.SetNumUninitialized(Count);
		for (int32 I = 0; I < Count; ++I)
		{
			LandMask[I] = (ElevationM[I] > 0.0f) ? 1 : 0;
		}

		TArray<float> DistanceToOceanPx;
		WorldseedGrid::DistanceTransform(LandMask, NX, NY, DistanceToOceanPx);

		// Le gradient de cette distance pointe vers l'INTERIEUR des terres : un
		// vent aligne avec lui souffle de la mer vers la cote, donc au vent.
		TArray<float> DistDY;
		TArray<float> DistDX;
		const float MetresPerPixel = Geometry.MetersPerPixel();
		{
			TArray<float> DistanceM;
			DistanceM.SetNumUninitialized(Count);
			for (int32 I = 0; I < Count; ++I)
			{
				DistanceM[I] = DistanceToOceanPx[I] * MetresPerPixel;
			}
			WorldseedGrid::Gradient(DistanceM, NX, NY, MetresPerPixel, DistDY, DistDX);
		}

		// --- eau douce dilatee, pour le marais -----------------------------------
		//
		// LE TABLEAU RESTE, LE CALCUL NON. L'hydrologie a ete retiree du
		// generateur le 18 septembre 2026 : les deux masques arrivent vides, le
		// marais devient inatteignable, et dilater deux millions de cellules
		// toutes fausses coute trois passes pour rien. La garde laisse la regle
		// en place pour le jour ou une source d'eau douce reviendra.
		TArray<bool> NearFresh;
		NearFresh.Init(false, Count);
		if (bHasLakes || bHasRivers)
		{
			for (int32 I = 0; I < Count; ++I)
			{
				NearFresh[I] = (bHasLakes && LakeMask[I]) || (bHasRivers && RiverMask[I]);
			}
			Dilate(NearFresh, NX, NY, 3);
		}

		// --- classification ------------------------------------------------------
		const bool bSummerFrac = (Rules.SummerRainFracByRow.Num() == NY);

		ParallelFor(NY, [&](int32 Row)
		{
			const float LatitudeDeg = Geometry.LatitudeDegForRow(Row);
			const float SummerFrac = bSummerFrac ? Rules.SummerRainFracByRow[Row] : 0.5f;

			float WindEast = 0.0f;
			float WindNorth = 0.0f;
			WorldseedWind::Prevailing(LatitudeDeg, Geometry.LatSpanDeg, WindEast, WindNorth);

			for (int32 Col = 0; Col < NX; ++Col)
			{
				const int32 I = Row * NX + Col;
				const bool bLand = (ElevationM[I] > 0.0f);

				const float T = TempMeanC[I];
				const float P = PrecipMm[I];
				const float Slope = Out.SlopeDeg[I];

				EWorldseedBiome Biome = FromClimate(T, P,
					bHasTempMax ? TempMaxC[I] : 0.0f, SummerFrac, bHasTempMax, Rules);

				// --- surcharges, de la moins a la plus prioritaire -----------------
				// ELLES NE VALENT QU'A TERRE : sous la mer, le diagramme dit la
				// bande climatique de l'eau, et ni la pente du fond ni la
				// distance au rivage n'ont de sens a lui appliquer.
				//
				// DEUX SORTES DE SURCHARGE, ET ELLES NE VONT PLUS AU MEME
				// ENDROIT. Ce qui releve du CLIMAT -- l'etage alpin, la calotte
				// -- continue d'ecrire le biome. Ce qui releve de la FORME DU
				// RELIEF -- la roche a nu, l'estran -- ecrit desormais le
				// substrat, et laisse le climat intact dessous.
				EWorldseedCover Substrate = EWorldseedCover::None;
				if (!bLand)
				{
					// LA MER PREND EN GLACE quand meme son mois le plus chaud
					// reste sous le seuil -- le meme critere que la calotte, et
					// pour la meme raison : ce qui fond en ete ne tient pas
					// l'annee. C'est ce qui donne un pole NORD blanc, lui qui
					// n'a aucune terre et n'en aura jamais.
					const bool bGelee = bHasTempMax
						&& (TempMaxC[I] < Rules.SeaIceTempC);

					Out.Index[I] = static_cast<uint8>(Biome);
					Out.Cover[I] = static_cast<uint8>(bGelee
						? EWorldseedCover::SeaIce
						: EWorldseedCover::Ocean);
					continue;
				}

				// Etage alpin : au-dessus de la limite des arbres ET en altitude
				// REELLE. Sans le second critere, toute la toundra polaire de
				// bord de mer basculerait en alpin, ce qui n'a aucun sens.
				if (T < Rules.TreeLineTempC && ElevationM[I] > Rules.AlpineMinElevationM)
				{
					Biome = EWorldseedBiome::Alpine;
				}

				// Au-dela de l'angle de tenue, aucune terre ne reste, quel que
				// soit le climat. C'est bien ce que dit la phrase : QUEL QUE
				// SOIT LE CLIMAT -- donc le climat, lui, ne change pas, et il
				// n'y a aucune raison de l'effacer.
				if (Slope > Rules.BareRockSlopeDeg)
				{
					Substrate = EWorldseedCover::Rock;
				}

				// Calotte : meme le mois le PLUS CHAUD reste sous le gel.
				if (bHasTempMax && TempMaxC[I] < Rules.PermanentIceTempC)
				{
					Biome = EWorldseedBiome::IceCap;
				}

				// --- estran, de largeur VARIABLE ----------------------------------
				// LA PENTE NE DOIT PAS TRANCHER MAIS MODULER. Une regle en tout
				// ou rien a 12 degres reduisait la plage a 0,2 % des terres — un
				// a deux pixels — parce que la bande cotiere de ce monde a une
				// pente MEDIANE de 31 degres : ces cotes sont escarpees.
				if (ElevationM[I] < Rules.BeachElevationM)
				{
					const float FlatFactor = 1.0f - WorldseedPerlin::Smoothstep(
						Rules.BeachSlopeFlatDeg, Rules.BeachSlopeSteepDeg, Slope);

					const float GradX = DistDX[I];
					const float GradY = DistDY[I];
					const float GradNorm = FMath::Max(
						FMath::Sqrt(GradX * GradX + GradY * GradY), 1e-6f);
					const float Exposure = FMath::Clamp(
						(WindEast * GradX + WindNorth * GradY) / GradNorm, 0.0f, 1.0f);

					const float WidthM = Rules.BeachWidthM * FlatFactor
						* (1.0f + Rules.BeachWindwardBonus * Exposure);

					if (DistanceToOceanPx[I] * MetresPerPixel <= WidthM)
					{
						// L'estran passe APRES la roche et la remplace : une
						// paroi qui plonge dans la mer se lit comme une plage
						// de galets, pas comme une falaise. C'est l'ordre
						// qu'avait deja l'ancienne cascade, garde tel quel.
						Substrate = EWorldseedCover::Beach;
					}
				}

				// Marais : plat, humide, au contact de l'eau douce.
				if (NearFresh[I] && Slope < Rules.MarshMaxSlopeDeg && P > Rules.MarshMinPrecipMm)
				{
					Biome = EWorldseedBiome::Marsh;
				}

				Out.Index[I] = static_cast<uint8>(Biome);

				// L'EAU N'ECRASE PLUS RIEN : elle se range dans son propre axe.
				// Une berge reste de la savane, et le sait.
				// L'EAU PASSE AVANT LE SUBSTRAT : sous une nappe, ce qu'on voit
				// est la nappe, et le fond n'a plus a dire s'il est rocheux.
				EWorldseedCover Cover = Substrate;
				if (bHasRivers && RiverMask[I]) { Cover = EWorldseedCover::River; }
				if (bHasLakes && LakeMask[I]) { Cover = EWorldseedCover::Lake; }
				Out.Cover[I] = static_cast<uint8>(Cover);
			}
		});

		// --- releve ---------------------------------------------------------------
		constexpr int32 CoverCount = static_cast<int32>(EWorldseedCover::Count);

		int32 LandTotal = 0;
		int32 Tally[BiomeCount] = {};
		int32 CoverTally[CoverCount] = {};
		for (int32 I = 0; I < Count; ++I)
		{
			if (ElevationM[I] > 0.0f)
			{
				++LandTotal;
				++Tally[FMath::Min<int32>(Out.Index[I], BiomeCount - 1)];
				++CoverTally[FMath::Min<int32>(Out.Cover[I], CoverCount - 1)];
			}
		}

		if (LandTotal > 0)
		{
			for (int32 B = 0; B < BiomeCount; ++B)
			{
				Out.LandSharePct[B] = 100.0f * Tally[B] / LandTotal;
			}
			for (int32 C = 0; C < CoverCount; ++C)
			{
				Out.CoverSharePct[C] = 100.0f * CoverTally[C] / LandTotal;
			}
		}

		// Les trois plus repandus : de quoi juger d'un coup si la carte est
		// plausible, sans noyer le journal.
		TArray<int32> Ranked;
		for (int32 B = 0; B < BiomeCount; ++B) { Ranked.Add(B); }
		Ranked.Sort([&Out](int32 A, int32 B) { return Out.LandSharePct[A] > Out.LandSharePct[B]; });

		// La couverture d'eau douce ne figure plus au journal : sans hydrologie
		// elle vaudrait zero a chaque generation, et un zero perpetuel se lit
		// comme une panne. L'ocean, lui, n'apparait pas dans CoverSharePct, qui
		// ne compte que les cellules emergees.
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] biomes : %s %.1f %%, %s %.1f %%, %s %.1f %%  (%.0f ms)"),
			Name(static_cast<EWorldseedBiome>(Ranked[0])), Out.LandSharePct[Ranked[0]],
			Name(static_cast<EWorldseedBiome>(Ranked[1])), Out.LandSharePct[Ranked[1]],
			Name(static_cast<EWorldseedBiome>(Ranked[2])), Out.LandSharePct[Ranked[2]],
			(FPlatformTime::Seconds() - StartTime) * 1000.0);

		// LE SUBSTRAT A SA PROPRE LIGNE, et il la merite : depuis qu'il ne
		// mange plus l'index des biomes, la part de roche a nu ne se lit plus
		// dans le tableau des biomes. La voir baisser ou exploser reste
		// pourtant le meilleur controle du seuil de pente.
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] substrat : roche a nu %.1f %% des terres, estran %.1f %%"),
			Out.CoverSharePct[static_cast<int32>(EWorldseedCover::Rock)],
			Out.CoverSharePct[static_cast<int32>(EWorldseedCover::Beach)]);
	}
}
