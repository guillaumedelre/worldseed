// Worldseed - etat meteo instantane, depuis le climat et le temps qui passe.

#include "Procedural/WorldseedWeatherState.h"
#include "Procedural/WorldseedPerlin.h"
#include "Procedural/WorldseedWeatherSignal.h"
#include "Procedural/WorldseedWind.h"

namespace
{
	/**
	 * Bornes de l'echelle de nebulosite d'UDS.
	 *
	 * Elles decrivent L'ECHELLE D'UDS, pas le climat : leur place n'est donc pas
	 * dans world_rules.json, qui ne parle que du monde.
	 */
	constexpr float UdsClearCoverage = 0.8f;
	constexpr float UdsOvercastCoverage = 8.5f;

	/** Seuils d'affichage du regime dominant. */
	constexpr float VisibleFall = 0.05f;
	constexpr float VisibleDust = 0.05f;
	constexpr float ThickFog = 1.6f;
	constexpr float OvercastSky = 5.0f;
}

FString FWorldseedWeather::DescribeRegime() const
{
	// L'ordre suit ce qui domine le regard : une averse se voit plus qu'un ciel
	// couvert, et la neige plus qu'une pluie.
	if (Snow > VisibleFall) { return TEXT("NEIGE"); }
	if (Rain > VisibleFall) { return TEXT("PLUIE"); }
	if (Dust > VisibleDust) { return TEXT("POUSSIERE"); }
	if (Fog > ThickFog) { return TEXT("BROUILLARD"); }
	if (CloudCoverage > OvercastSky) { return TEXT("couvert"); }
	return TEXT("degage");
}

namespace WorldseedWeatherState
{
	FWorldseedWeather Evaluate(const FWorldseedClimateSample& Sample,
		const FWorldseedClimatePresetRules& PresetRules,
		const FWorldseedWeatherParams& Params)
	{
		const FWorldseedClimatePreset Preset =
			WorldseedClimatePreset::Build(Sample, PresetRules);

		FWorldseedWeather Out;

		for (int32 S = 0; S < FWorldseedClimatePreset::SeasonCount; ++S)
		{
			Out.SeasonMinMaxC[S] = FVector2D(Preset.LowTempC[S], Preset.HighTempC[S]);
		}

		// --- ce que le climat prevoit ici, a cette saison -----------------------
		const float CloudyPct = WorldseedClimatePreset::SeasonLerp(
			Preset.CloudyPct, Params.SeasonPhase);
		const float RainMm = WorldseedClimatePreset::SeasonLerp(
			Preset.RainfallMm, Params.SeasonPhase);
		const float SnowMm = WorldseedClimatePreset::SeasonLerp(
			Preset.SnowfallMm, Params.SeasonPhase);
		const float TotalMm = RainMm + SnowMm;

		const float Storm = WorldseedWeatherSignal::Storminess(
			Params.TimeSeconds, Params.VariationPeriodS, Params.Seed);

		// --- occurrence ---------------------------------------------------------
		// La nebulosite est une FREQUENCE : elle fait le seuil a franchir.
		const float CloudyFraction = FMath::Clamp(CloudyPct * 0.01f, 0.0f, 1.0f);
		const float Threshold = 1.0f - CloudyFraction;
		const float Occurrence = FMath::Clamp(
			(Storm - Threshold) / FMath::Max(1.0f - Threshold, 1e-3f), 0.0f, 1.0f);

		// --- intensite ----------------------------------------------------------
		// Meme courbe de saturation que la nebulosite, sur la meme echelle de
		// reference : un mois a 180 mm tombe fort, un mois a 10 mm bruine.
		const float Intensity = 1.0f - FMath::Exp(
			-TotalMm / FMath::Max(PresetRules.CloudyPrecipScaleMm, 1e-6f));

		// Le partage pluie/neige est deja fait par le prereglage : on le
		// respecte plutot que de le recalculer avec d'autres seuils.
		const float SnowShare = (TotalMm > 1e-6f) ? (SnowMm / TotalMm) : 0.0f;
		const float Fall = Occurrence * Intensity;

		Out.Rain = Fall * (1.0f - SnowShare);
		Out.Snow = Fall * SnowShare;

		// --- poussiere ----------------------------------------------------------
		// Le prereglage dit seulement si le climat en connait ; l'agitation dit
		// quand elle se leve.
		Out.Dust = Preset.bDustPresent
			? FMath::Clamp(FMath::Max(Storm - 0.62f, 0.0f) * 2.6f, 0.0f, 1.0f)
			: 0.0f;

		// --- brouillard ---------------------------------------------------------
		// LE PYTHON N'EN MODELISE PAS : les prereglages d'UDS n'ont pas de case
		// pour lui. On le deduit donc du reste sans pretendre le mesurer — il
		// demande de l'humidite et de la fraicheur, et il se leve quand il ne
		// pleut PAS, une averse lessivant l'air.
		const float Coolness = 1.0f - WorldseedPerlin::Smoothstep(5.0f, 25.0f, Sample.TempMeanC);
		const float HazeNoise = WorldseedWeatherSignal::Haze(
			Params.TimeSeconds, Params.VariationPeriodS, Params.Seed);

		Out.Fog = FMath::Clamp(
			0.4f + 2.0f * CloudyFraction * Coolness * (1.0f - Occurrence) * HazeNoise,
			0.4f, 2.5f);

		// --- nuages -------------------------------------------------------------
		Out.CloudCoverage = FMath::Lerp(UdsClearCoverage, UdsOvercastCoverage,
			FMath::Clamp(CloudyFraction * 0.5f + Occurrence * 0.8f, 0.0f, 1.0f));

		// --- vent ---------------------------------------------------------------
		Out.WindDirectionDeg = WorldseedWind::PrevailingYawDeg(
			Sample.LatitudeDeg, Params.LatSpanDeg);

		Out.WindIntensity = FMath::Clamp(
			1.0f + 3.0f * Storm + 2.0f * Occurrence, 0.5f, 8.0f);

		return Out;
	}

	void BlendTowards(FWorldseedWeather& Current, const FWorldseedWeather& Target,
		float Alpha)
	{
		const float A = FMath::Clamp(Alpha, 0.0f, 1.0f);

		Current.Rain = FMath::Lerp(Current.Rain, Target.Rain, A);
		Current.Snow = FMath::Lerp(Current.Snow, Target.Snow, A);
		Current.Fog = FMath::Lerp(Current.Fog, Target.Fog, A);
		Current.Dust = FMath::Lerp(Current.Dust, Target.Dust, A);
		Current.CloudCoverage = FMath::Lerp(Current.CloudCoverage, Target.CloudCoverage, A);
		Current.WindIntensity = FMath::Lerp(Current.WindIntensity, Target.WindIntensity, A);

		// LE VENT EST UN ANGLE : interpoler 350 vers 10 en ligne droite ferait
		// faire un tour complet a la girouette. On passe par l'ecart signe le
		// plus court.
		const float Delta = FMath::UnwindDegrees(
			Target.WindDirectionDeg - Current.WindDirectionDeg);
		Current.WindDirectionDeg = FMath::UnwindDegrees(
			Current.WindDirectionDeg + Delta * A);

		for (int32 S = 0; S < FWorldseedClimatePreset::SeasonCount; ++S)
		{
			Current.SeasonMinMaxC[S] = FMath::Lerp(
				Current.SeasonMinMaxC[S], Target.SeasonMinMaxC[S], A);
		}
	}
}
