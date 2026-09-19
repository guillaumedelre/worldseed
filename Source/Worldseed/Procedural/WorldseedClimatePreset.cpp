// Worldseed - les prereglages climatiques.
//
// L'ORIGINAL PYTHON A ETE SUPPRIME : ce fichier est la SEULE
// implementation. Il ne faut plus chercher de reference ailleurs, ni supposer
// qu'un autre fichier dit la meme chose autrement.

#include "Procedural/WorldseedClimatePreset.h"
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

	Out.DustPrecipMaxMm = Num(TEXT("dustPrecipMaxMm"), 250.0);

	Out.TropicLatDeg = static_cast<float>(Rules.Num(WORLD, TEXT("tropicDeg"), 23.44));

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
		SeasonalRainFactors(Sample.LatitudeDeg, Rules, RainFactor);

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

		Out.bDustPresent = (Sample.PrecipMm < Rules.DustPrecipMaxMm) && (T > 5.0f);

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
