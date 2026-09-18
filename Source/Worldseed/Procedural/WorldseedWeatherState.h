// Worldseed - etat meteo instantane, depuis le climat et le temps qui passe.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedClimatePreset.h"

/** Ce qu'on ecrit dans UDS. Temperatures en Celsius : la conversion est au bord. */
struct WORLDSEED_API FWorldseedWeather
{
	float Rain = 0.0f;              // 0..1
	float Snow = 0.0f;              // 0..1
	float Fog = 1.0f;               // echelle UDS, 1 = normal
	float Dust = 0.0f;              // 0..1
	float CloudCoverage = 3.0f;     // echelle UDS, ~4 = ciel ordinaire
	float WindIntensity = 2.0f;
	float WindDirectionDeg = 180.0f;

	/** Plages jour/nuit par saison, en Celsius : (minimum, maximum). */
	FVector2D SeasonMinMaxC[FWorldseedClimatePreset::SeasonCount] = {
		FVector2D(2.0, 10.0), FVector2D(8.0, 18.0),
		FVector2D(16.0, 26.0), FVector2D(8.0, 18.0)
	};

	/** Nom du regime dominant, pour l'affichage. */
	FString DescribeRegime() const;
};

/** Ce qui gouverne la traduction, au-dela du prereglage climatique. */
struct WORLDSEED_API FWorldseedWeatherParams
{
	/** Duree d'un cycle meteo complet, en secondes. */
	float VariationPeriodS = 180.0f;

	/** Position dans l'annee : 0 au coeur de l'hiver, 1 un tour plus tard. */
	float SeasonPhase = 0.5f;

	/** Etendue de latitude de la carte, pour placer les ceintures de vent. */
	float LatSpanDeg = 180.0f;

	/** Graine du monde : le signal temporel en derive. */
	int32 Seed = 0;

	float TimeSeconds = 0.0f;
};

namespace WorldseedWeatherState
{
	/**
	 * Traduit un climat en meteo instantanee.
	 *
	 * LE POURCENTAGE DE CIEL COUVERT DU PREREGLAGE EST UNE FREQUENCE, pas une
	 * quantite : la part du temps ou le ciel est charge. Il fait donc un seuil
	 * naturel que le signal d'agitation doit franchir. Un climat couvert 93 % du
	 * temps le franchit presque toujours, un climat couvert 15 % presque jamais
	 * — et le desert garde ainsi son orage rare sans qu'on ait a l'inventer.
	 */
	WORLDSEED_API FWorldseedWeather Evaluate(const FWorldseedClimateSample& Sample,
		const FWorldseedClimatePresetRules& PresetRules,
		const FWorldseedWeatherParams& Params);

	/**
	 * Fondu vers une cible.
	 *
	 * Franchir une frontiere climatique ne doit pas commuter le ciel d'un coup :
	 * on tend vers la cible au lieu de s'y poser. Alpha est la part de chemin
	 * parcourue sur ce pas.
	 */
	WORLDSEED_API void BlendTowards(FWorldseedWeather& Current,
		const FWorldseedWeather& Target, float Alpha);
}
