// Worldseed - affichage du releve meteo pendant le jeu.

#include "Procedural/WorldseedWeatherReadout.h"
#include "Procedural/WorldseedClimatePreset.h"
#include "Procedural/WorldseedWeatherState.h"

#include "Engine/Engine.h"

namespace WorldseedWeatherReadout
{
	namespace
	{
		/** Cles stables : chaque ligne se remplace au lieu de s'empiler. */
		constexpr int32 KeyRegime = 9001;
		constexpr int32 KeyValues = 9002;
		constexpr int32 KeyClimate = 9003;
		constexpr int32 KeyPlace = 9004;

		const TCHAR* SeasonName(float Phase)
		{
			const float P = Phase - FMath::FloorToFloat(Phase);
			if (P < 0.125f || P >= 0.875f) { return TEXT("hiver"); }
			if (P < 0.375f) { return TEXT("printemps"); }
			if (P < 0.625f) { return TEXT("ete"); }
			return TEXT("automne");
		}
	}

	void Draw(const FWorldseedWeather& Weather, const FWorldseedClimateSample& Sample,
		float LongitudeDeg, float AltitudeM, float SeasonPhase,
		float PeriodS, float HoldSeconds)
	{
		if (!GEngine)
		{
			return;
		}

		auto Line = [HoldSeconds](int32 Key, const FColor& Colour, const FString& Text)
		{
			GEngine->AddOnScreenDebugMessage(Key, HoldSeconds, Colour, Text);
		};

		Line(KeyRegime, FColor::White, FString::Printf(
			TEXT("  %s   nuages %.1f   vent %.1f a %.0f deg"),
			*Weather.DescribeRegime(), Weather.CloudCoverage,
			Weather.WindIntensity, Weather.WindDirectionDeg));

		Line(KeyValues, FColor::Cyan, FString::Printf(
			TEXT("  pluie %.2f   neige %.2f   brouillard %.2f   poussiere %.2f"),
			Weather.Rain, Weather.Snow, Weather.Fog, Weather.Dust));

		Line(KeyClimate, FColor::Yellow, FString::Printf(
			TEXT("  CLIMAT ICI : %.1f C   %.0f mm/an   saisons +/-%.1f C   continentalite %.2f"),
			Sample.TempMeanC, Sample.PrecipMm,
			Sample.SeasonalAmpC * 0.5f, Sample.Continentality));

		// L'altitude se compte depuis le NIVEAU DE LA MER, qui est le zero de
		// toute la chaine de generation : une valeur negative est donc sous
		// l'eau, pas une anomalie.
		Line(KeyPlace, FColor::Green, FString::Printf(
			TEXT("WORLDSEED  lat %.1f  lon %.1f  altitude %.0f m  %s  (cycle %.0f s)"),
			Sample.LatitudeDeg, LongitudeDeg, AltitudeM,
			SeasonName(SeasonPhase), PeriodS));
	}
}
