// Worldseed - les prereglages climatiques.
//
// L'ORIGINAL PYTHON A ETE SUPPRIME : ce fichier est la SEULE
// implementation. Il ne faut plus chercher de reference ailleurs, ni supposer
// qu'un autre fichier dit la meme chose autrement.
//
// Ce fichier Python a ete cale sur les 23 prereglages LIVRES par Ultra Dynamic
// Sky, lus dans l'editeur : ses formules sont des mesures, pas des suppositions.
// Toutes les constantes vivent dans la section "uds" de world_rules.json.

#pragma once

#include "CoreMinimal.h"

class UWorldseedRules;

/** Index des saisons, dans l'ordre du Python. */
enum class EWorldseedSeason : uint8
{
	Winter = 0,
	Spring = 1,
	Summer = 2,
	Autumn = 3,
	Count = 4
};

/** Le climat lu en un point du monde. Cinq mesures suffisent au prereglage. */
struct WORLDSEED_API FWorldseedClimateSample
{
	/** Moyenne annuelle au sol, en degres Celsius. */
	float TempMeanC = 15.0f;

	/** Cumul annuel de precipitations, en millimetres. */
	float PrecipMm = 700.0f;

	/** Ecart entre le mois le plus froid et le plus chaud, en degres. */
	float SeasonalAmpC = 12.0f;

	/** 0 au bord de mer, 1 loin des cotes. Ouvre l'ecart jour/nuit. */
	float Continentality = 0.5f;

	/** Latitude signee, en degres. */
	float LatitudeDeg = 0.0f;
};

/**
 * Les 21 cases d'un UDS_Climate_Preset.
 *
 * DEUX PRECISIONS QUE LA LECTURE DES PRESETS LIVRES A CORRIGEES, et qu'on
 * aurait ratees en raisonnant de tete :
 *   - la pluie est un cumul MENSUEL, pas saisonnier (Tropical_Rainforest porte
 *     145 a 184 mm) ;
 *   - la neige est un EQUIVALENT-EAU, pas une hauteur : un meme mois d'hiver
 *     peut porter 36 mm de pluie ET 45 mm de neige.
 */
struct WORLDSEED_API FWorldseedClimatePreset
{
	static constexpr int32 SeasonCount = 4;

	float HighTempC[SeasonCount] = { 10.0f, 18.0f, 26.0f, 18.0f };
	float LowTempC[SeasonCount] = { 2.0f, 8.0f, 16.0f, 8.0f };
	float CloudyPct[SeasonCount] = { 40.0f, 40.0f, 40.0f, 40.0f };
	float RainfallMm[SeasonCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float SnowfallMm[SeasonCount] = { 0.0f, 0.0f, 0.0f, 0.0f };

	bool bDustPresent = false;

	float Get(const float (&Values)[SeasonCount], EWorldseedSeason S) const
	{
		return Values[static_cast<int32>(S)];
	}
};

/** Section "uds" de world_rules.json, plus world.tropicDeg. */
struct WORLDSEED_API FWorldseedClimatePresetRules
{
	float DiurnalBaseC = 4.0f;
	float DiurnalContinentalC = 3.0f;
	float DiurnalAridC = 2.0f;
	float AridPrecipRefMm = 1000.0f;

	float CloudyFloorPct = 15.0f;
	float CloudySpanPct = 78.0f;
	float CloudyPrecipScaleMm = 60.0f;

	float SnowThresholdC = 2.0f;
	float SnowBandC = 4.0f;

	float ItczSummerFactor = 1.8f;
	float ItczWinterFactor = 0.2f;
	float MediterraneanWinterFactor = 1.5f;
	float MediterraneanSummerFactor = 0.5f;
	float MediterraneanLatMinDeg = 30.0f;
	float MediterraneanLatMaxDeg = 45.0f;

	float DustPrecipMaxMm = 250.0f;

	/** world.tropicDeg : borne de la bande ou la ZCIT module les pluies. */
	float TropicLatDeg = 23.44f;

	static FWorldseedClimatePresetRules FromRules(const UWorldseedRules& Rules);
};

namespace WorldseedClimatePreset
{
	/**
	 * Les 21 cases, depuis cinq mesures du monde.
	 *
	 * DEUX HEMISPHERES, UNE SEULE SAISON. UDS n'a qu'une saison globale, or le
	 * monde va d'un pole a l'autre : quand c'est l'ete au nord, c'est l'hiver au
	 * sud. Un echantillon austral rend donc son prereglage avec hiver/ete et
	 * printemps/automne echanges, pour tomber juste dans les saisons d'UDS.
	 */
	WORLDSEED_API FWorldseedClimatePreset Build(const FWorldseedClimateSample& Sample,
		const FWorldseedClimatePresetRules& Rules);

	/**
	 * Valeur continue au sein de l'annee.
	 *
	 * Les saisons sont echantillonnees au quart de tour ; Phase va de 0 (coeur
	 * de l'hiver) a 1.
	 */
	WORLDSEED_API float SeasonLerp(const float Values[FWorldseedClimatePreset::SeasonCount],
		float Phase);
}
