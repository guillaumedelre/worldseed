// Worldseed - les prereglages climatiques.
//
// L'ORIGINAL PYTHON A ETE SUPPRIME : ce fichier est la SEULE
// implementation. Il ne faut plus chercher de reference ailleurs, ni supposer
// qu'un autre fichier dit la meme chose autrement.

#include "Procedural/WorldseedClimatePreset.h"

#include "Procedural/WorldseedKoppen.h"
#include "Procedural/WorldseedPerlin.h"
#include "Procedural/WorldseedRules.h"

namespace
{
	const TCHAR* UDS = TEXT("uds");
	const TCHAR* WORLD = TEXT("world");
}

FWorldseedClimatePresetRules FWorldseedClimatePresetRules::FromRules(
	const UWorldseedRules& Rules)
{
	FWorldseedClimatePresetRules Out;

	auto Num = [&Rules](const TCHAR* Key, double Fallback)
	{
		return static_cast<float>(Rules.Num(UDS, Key, Fallback));
	};

	Out.DiurnalBaseC = Num(TEXT("diurnalBaseC"), 4.0);
	Out.DiurnalContinentalC = Num(TEXT("diurnalContinentalC"), 3.0);
	Out.DiurnalAridC = Num(TEXT("diurnalAridC"), 2.0);
	Out.AridPrecipRefMm = Num(TEXT("aridPrecipRefMm"), 1000.0);

	Out.CloudyFloorPct = Num(TEXT("cloudyFloorPct"), 15.0);
	Out.CloudySpanPct = Num(TEXT("cloudySpanPct"), 78.0);
	Out.CloudyPrecipScaleMm = Num(TEXT("cloudyPrecipScaleMm"), 60.0);

	Out.SnowThresholdC = Num(TEXT("snowThresholdC"), 2.0);
	Out.SnowBandC = Num(TEXT("snowBandC"), 4.0);

	Out.ItczSummerFactor = Num(TEXT("itczSummerFactor"), 1.8);
	Out.ItczWinterFactor = Num(TEXT("itczWinterFactor"), 0.2);
	Out.MediterraneanWinterFactor = Num(TEXT("mediterraneanWinterFactor"), 1.5);
	Out.MediterraneanSummerFactor = Num(TEXT("mediterraneanSummerFactor"), 0.5);
	Out.MediterraneanLatMinDeg = Num(TEXT("mediterraneanLatMinDeg"), 30.0);
	Out.MediterraneanLatMaxDeg = Num(TEXT("mediterraneanLatMaxDeg"), 45.0);

	Out.PoussierePleinePluieMm = Num(TEXT("poussierePleinePluieMm"), 120.0);
	Out.PoussiereNullePluieMm = Num(TEXT("poussiereNullePluieMm"), 350.0);
	Out.PoussiereGelNulleC = Num(TEXT("poussiereGelNulleC"), -8.0);
	Out.PoussiereGelPleineC = Num(TEXT("poussiereGelPleineC"), 0.0);
	Out.PoussiereVoile = Num(TEXT("poussiereVoile"), 2.0);
	Out.PoussiereVentSeuil = Num(TEXT("poussiereVentSeuil"), 6.5);
	Out.PoussiereVentMordant = Num(TEXT("poussiereVentMordant"), 3.0);
	Out.PoussierePeriodeFacteur = Num(TEXT("poussierePeriodeFacteur"), 3.0);

	Out.VentCalme = Num(TEXT("ventCalme"), 1.0);
	Out.VentMordantAgitation = Num(TEXT("ventMordantAgitation"), 9.0);
	Out.VentPluie = Num(TEXT("ventPluie"), 2.0);
	Out.VentMaxUds = Num(TEXT("ventMaxUds"), 10.0);
	Out.VentForme = Num(TEXT("ventForme"), 3.0);
	Out.BlizzardVisibiliteMax = Num(TEXT("blizzardVisibiliteMax"), 10.0);

	Out.BrumePlancher = Num(TEXT("brumePlancher"), 0.4);
	Out.BrumeMax = Num(TEXT("brumeMax"), 10.0);
	Out.BrumePoidsFraicheur = Num(TEXT("brumePoidsFraicheur"), 0.7);
	Out.BrumePoidsLittoral = Num(TEXT("brumePoidsLittoral"), 0.55);
	Out.BrumeHumiditeCotiere = Num(TEXT("brumeHumiditeCotiere"), 0.45);
	Out.BrumeVentNul = Num(TEXT("brumeVentNul"), 4.0);
	Out.BrumeVentPlein = Num(TEXT("brumeVentPlein"), 0.35);
	Out.BrumeHeureMax = Num(TEXT("brumeHeureMax"), 6.0);
	Out.BrumePoidsHeure = Num(TEXT("brumePoidsHeure"), 0.6);
	Out.BrumePeriodeFacteur = Num(TEXT("brumePeriodeFacteur"), 5.0);

	Out.PluieFrequenceEchelleMm = Num(TEXT("pluieFrequenceEchelleMm"), 625.0);
	Out.PluieIntensitePlancher = Num(TEXT("pluieIntensitePlancher"), 0.5);

	Out.OrageTempMinC = Num(TEXT("orageTempMinC"), 8.0);
	Out.OrageTempMaxC = Num(TEXT("orageTempMaxC"), 24.0);
	Out.AuroreLatitudePicDeg = Num(TEXT("auroreLatitudePicDeg"), 67.0);
	Out.AuroreLargeurDeg = Num(TEXT("auroreLargeurDeg"), 12.0);
	Out.AuroreIntensiteMax = Num(TEXT("auroreIntensiteMax"), 1.0);

	Out.TropicLatDeg = static_cast<float>(Rules.Num(WORLD, TEXT("tropicDeg"), 23.44));
	Out.SeasonContrastExponent = Num(TEXT("saisonExposant"), 1.9);
	Out.bCouvertureDepuisReleves =
		(Rules.Num(UDS, TEXT("couvertureDepuisReleves"), 1.0) >= 0.5);
	Out.KoppenPointeMensuelleC = Num(TEXT("koppenPointeMensuelleC"), 0.5);

	// LES RELEVES SONT CHARGES ICI, avec le reste, et une seule fois. Un echec
	// de lecture laisse le tableau vide, et `Build` retombe alors sur la courbe :
	// une donnee absente doit rester sans effet, jamais faire echouer le ciel.
	if (Out.bCouvertureDepuisReleves)
	{
		FString ErreurReleves;
		if (!WorldseedClimatsReels::Charger(Out.Releves, ErreurReleves))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] ciel : releves illisibles (%s) -- la couverture ")
				TEXT("nuageuse restera calculee depuis la pluie"), *ErreurReleves);
			Out.Releves.Reset();
		}
	}

	return Out;
}

namespace WorldseedClimatePreset
{
	namespace
	{
		constexpr int32 Winter = static_cast<int32>(EWorldseedSeason::Winter);
		constexpr int32 Spring = static_cast<int32>(EWorldseedSeason::Spring);
		constexpr int32 Summer = static_cast<int32>(EWorldseedSeason::Summer);
		constexpr int32 Autumn = static_cast<int32>(EWorldseedSeason::Autumn);
		constexpr int32 NumSeasons = FWorldseedClimatePreset::SeasonCount;

		/**
		 * Modulation saisonniere de la pluie, par bande de latitude.
		 *
		 * Deux motifs reels, et seulement deux :
		 *   - sous les tropiques la ZCIT suit le soleil : ete humide, hiver sec.
		 *     C'est ce qui donne a la savane sa saison seche, son trait
		 *     definitoire ;
		 *   - entre 30 et 45 degres le front polaire descend en hiver : hiver
		 *     humide et ete sec, le regime mediterraneen.
		 * Ailleurs rien : le generateur ne calcule qu'un cumul annuel, et
		 * inventer une saisonnalite serait de l'ornement.
		 *
		 * LE POIDS DE LA ZCIT EST NUL A L'EQUATEUR, et c'est contre-intuitif.
		 * L'equateur est humide toute l'annee parce que la ZCIT y passe DEUX
		 * fois par an ; la saison seche marquee est vers 10 a 20 degres, ou elle
		 * ne passe qu'une fois. Le poids suit donc un demi-sinus, nul a
		 * l'equateur comme au tropique et maximal a mi-chemin.
		 */
		/**
		 * REPARTIR LA PLUIE D'APRES LA PART QUI TOMBE AU SEMESTRE CHAUD.
		 *
		 * POURQUOI C'EST MIEUX QUE LA LATITUDE SEULE. La fonction du dessous
		 * REDERIVE la saisonnalite depuis la latitude, alors que la chaine
		 * climatique la calcule deja par cellule et mieux. Mesure sur les
		 * vingt-trois releves de stations reelles : 25,7 mm par mois d'ecart
		 * par la latitude, 21,8 par cette voie. Le temoin -- repartir
		 * uniformement -- en fait 32,3.
		 *
		 * L'EXPOSANT EST CE QUI DONNE SON MORDANT AU CONTRASTE. A un, les
		 * quatre saisons suivent lineairement la fraction, et le monde reel est
		 * bien plus tranche : une savane a hiver sec recoit deux millimetres en
		 * hiver et cent cinquante-cinq en automne, un rapport de CINQUANTE,
		 * quand une repartition lineaire plafonne vers dix. Cale a 1,7 sur les
		 * releves, compte tenu de l'erreur qui reste sur notre propre fraction.
		 *
		 * LES EQUINOXES VALENT UN DEMI, entre les deux extremes : ils sont a
		 * cheval sur les deux semestres par construction, et c'est deja la
		 * convention qu'emploie le calcul de la fraction elle-meme.
		 */
		void FacteursDepuisFractionEstivale(float SummerFrac,
			const FWorldseedClimatePresetRules& Rules, float OutFactor[NumSeasons])
		{
			const float Fe = FMath::Clamp(SummerFrac, 0.0f, 1.0f);
			const float K = FMath::Max(Rules.SeasonContrastExponent, 0.01f);

			float Base[NumSeasons];
			Base[Winter] = 1.0f - Fe;
			Base[Spring] = 0.5f;
			Base[Summer] = Fe;
			Base[Autumn] = 0.5f;

			float Somme = 0.0f;
			for (int32 S = 0; S < NumSeasons; ++S)
			{
				OutFactor[S] = FMath::Pow(FMath::Max(Base[S], 1e-3f), K);
				Somme += OutFactor[S];
			}

			// LA MOYENNE ANNUELLE DOIT RESTER LA MOYENNE ANNUELLE : repartir ne
			// doit ni ajouter ni retirer d'eau sur l'annee. Meme garde que la
			// fonction du dessous, et pour la meme raison.
			const float Moyenne = Somme / static_cast<float>(NumSeasons);
			if (Moyenne > 1e-6f)
			{
				for (int32 S = 0; S < NumSeasons; ++S)
				{
					OutFactor[S] /= Moyenne;
				}
			}
		}

		void SeasonalRainFactors(float LatitudeDeg,
			const FWorldseedClimatePresetRules& Rules, float OutFactor[NumSeasons])
		{
			for (int32 S = 0; S < NumSeasons; ++S)
			{
				OutFactor[S] = 1.0f;
			}

			const float AbsLat = FMath::Abs(LatitudeDeg);
			const float Tropic = FMath::Max(Rules.TropicLatDeg, 1e-6f);

			if (AbsLat <= Tropic)
			{
				const float Weight = FMath::Sin(PI * AbsLat / Tropic);
				OutFactor[Summer] = 1.0f + (Rules.ItczSummerFactor - 1.0f) * Weight;
				OutFactor[Winter] = 1.0f + (Rules.ItczWinterFactor - 1.0f) * Weight;
			}
			else if (AbsLat >= Rules.MediterraneanLatMinDeg
				&& AbsLat <= Rules.MediterraneanLatMaxDeg)
			{
				const float Mid = 0.5f
					* (Rules.MediterraneanLatMinDeg + Rules.MediterraneanLatMaxDeg);
				const float HalfWidth = FMath::Max(0.5f
					* (Rules.MediterraneanLatMaxDeg - Rules.MediterraneanLatMinDeg), 1e-6f);
				const float Weight = FMath::Max(0.0f,
					1.0f - FMath::Abs(AbsLat - Mid) / HalfWidth);

				OutFactor[Winter] = 1.0f + (Rules.MediterraneanWinterFactor - 1.0f) * Weight;
				OutFactor[Summer] = 1.0f + (Rules.MediterraneanSummerFactor - 1.0f) * Weight;
			}

			// La moyenne annuelle doit rester la moyenne annuelle : moduler ne
			// doit ni ajouter ni retirer d'eau sur l'annee.
			float Mean = 0.0f;
			for (int32 S = 0; S < NumSeasons; ++S)
			{
				Mean += OutFactor[S];
			}
			Mean /= static_cast<float>(NumSeasons);

			if (Mean > 1e-6f)
			{
				for (int32 S = 0; S < NumSeasons; ++S)
				{
					OutFactor[S] /= Mean;
				}
			}
		}

		/** Echange hiver/ete et printemps/automne, en place. */
		void SwapHemisphere(FWorldseedClimatePreset& P)
		{
			auto SwapSeasons = [](float V[NumSeasons])
			{
				Swap(V[Winter], V[Summer]);
				Swap(V[Spring], V[Autumn]);
			};

			SwapSeasons(P.HighTempC);
			SwapSeasons(P.LowTempC);
			SwapSeasons(P.CloudyPct);
			SwapSeasons(P.RainfallMm);
			SwapSeasons(P.SnowfallMm);
		}
	}

	FWorldseedClimatePreset Build(const FWorldseedClimateSample& Sample,
		const FWorldseedClimatePresetRules& Rules)
	{
		FWorldseedClimatePreset Out;

		// --- ecart diurne -------------------------------------------------------
		// Il ne fait que 4 a 9 degres dans les presets livres : ce sont des
		// MOYENNES haute et basse, pas des extremes. Un interieur de continent
		// et un climat aride l'ouvrent, faute de mer et de vapeur pour tamponner.
		const float Aridity = 1.0f - FMath::Min(
			Sample.PrecipMm / FMath::Max(Rules.AridPrecipRefMm, 1e-6f), 1.0f);
		const float Diurnal = Rules.DiurnalBaseC
			+ Rules.DiurnalContinentalC * Sample.Continentality
			+ Rules.DiurnalAridC * Aridity;

		const float T = Sample.TempMeanC;
		const float HalfAmp = Sample.SeasonalAmpC * 0.5f;

		float SeasonMeanC[NumSeasons];
		SeasonMeanC[Winter] = T - HalfAmp;
		SeasonMeanC[Spring] = T;
		SeasonMeanC[Summer] = T + HalfAmp;
		SeasonMeanC[Autumn] = T;

		float RainFactor[NumSeasons];
		if (Sample.SummerRainFrac >= 0.0f)
		{
			FacteursDepuisFractionEstivale(Sample.SummerRainFrac, Rules, RainFactor);
		}
		else
		{
			SeasonalRainFactors(Sample.LatitudeDeg, Rules, RainFactor);
		}

		// La pluie des presets est un cumul MENSUEL.
		const float MonthlyMm = Sample.PrecipMm / 12.0f;

		for (int32 S = 0; S < NumSeasons; ++S)
		{
			const float SeasonT = SeasonMeanC[S];

			Out.HighTempC[S] = SeasonT + Diurnal * 0.5f;
			Out.LowTempC[S] = SeasonT - Diurnal * 0.5f;

			const float Mm = MonthlyMm * RainFactor[S];

			// Part tombant en neige : tout sous le seuil, rien au-dessus de la
			// bande. La neige etant un EQUIVALENT-EAU, les deux lignes se
			// partagent un meme cumul.
			const float SnowPart = FMath::Clamp(
				(Rules.SnowThresholdC - SeasonT) / FMath::Max(Rules.SnowBandC, 1e-6f),
				0.0f, 1.0f);

			Out.RainfallMm[S] = Mm * (1.0f - SnowPart);
			Out.SnowfallMm[S] = Mm * SnowPart;

			Out.CloudyPct[S] = Rules.CloudyFloorPct + Rules.CloudySpanPct
				* (1.0f - FMath::Exp(-Mm / FMath::Max(Rules.CloudyPrecipScaleMm, 1e-6f)));
		}

		// --- LA COUVERTURE D'UNE VRAIE STATION, QUAND ON SAIT LAQUELLE -------
		//
		// ON NE COPIE QUE LE CIEL, PAS LE CLIMAT. Les temperatures et le cumul
		// de pluie viennent du MONDE -- c'est lui qui decide -- et seule la part
		// du temps ou le ciel est charge vient du releve. La raison est qu'elle
		// ne se deduit PAS de la pluie : deux climats a quarante millimetres par
		// mois portent 35 ou 78 pour cent de couverture selon que la pluie est
		// convective ou frontale, et la quantite ne dit pas le type. C'est le
		// seul endroit ou une donnee exterieure apporte ce qu'aucun calcul
		// n'atteint.
		if (Rules.bCouvertureDepuisReleves && Rules.Releves.Num() > 0)
		{
			const FWorldseedKoppenEntree Entree = WorldseedKoppen::DepuisChamps(
				Sample.TempMeanC, Sample.PrecipMm, Sample.SeasonalAmpC,
				(Sample.SummerRainFrac >= 0.0f) ? Sample.SummerRainFrac : 0.5f,
				Rules.SeasonContrastExponent);

			const EWorldseedKoppen Classe = WorldseedKoppen::Classer(
				Entree, Rules.KoppenPointeMensuelleC);

			for (const FWorldseedReleveReel& R : Rules.Releves)
			{
				if (R.Koppen != Classe) { continue; }
				for (int32 S = 0; S < NumSeasons; ++S)
				{
					Out.CloudyPct[S] = R.CouvertPct[S];
				}
				break;
			}
		}

		// LA POUSSIERE N'EST PAS UNE AFFAIRE DE CHALEUR : le `T > 5.0f` en dur
		// qui vivait ici excluait le Gobi et la Patagonie. Voir `DustPart`.
		//
		// MAIS ELLE EST UNE AFFAIRE DE GEL, et le retrait du seuil d'origine
		// avait fait POUDROYER LA CALOTTE -- 99,9 % de voile a -18,8 C. Les deux
		// termes se multiplient : il faut un sol SEC **et** DEGELE.
		const float PartAridite = 1.0f - WorldseedPerlin::Smoothstep(
			Rules.PoussierePleinePluieMm, Rules.PoussiereNullePluieMm, Sample.PrecipMm);
		const float PartDegel = WorldseedPerlin::Smoothstep(
			Rules.PoussiereGelNulleC, Rules.PoussiereGelPleineC, T);

		Out.DustPart = PartAridite * PartDegel;

		if (Sample.LatitudeDeg < 0.0f)
		{
			SwapHemisphere(Out);
		}

		return Out;
	}

	float SeasonLerp(const float Values[NumSeasons], float Phase)
	{
		const float P = Phase - FMath::FloorToFloat(Phase);
		const float Scaled = P * static_cast<float>(NumSeasons);
		const float Floor = FMath::FloorToFloat(Scaled);

		const int32 A = static_cast<int32>(Floor) & (NumSeasons - 1);
		const int32 B = (A + 1) & (NumSeasons - 1);

		return FMath::Lerp(Values[A], Values[B], Scaled - Floor);
	}
}

