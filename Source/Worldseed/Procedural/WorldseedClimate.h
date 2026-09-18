// Worldseed - portage de Tools/WorldGen/worldgen/climate.py.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedJob.h"
#include "Procedural/WorldseedRules.h"

/** Sortie des etapes 2 et 3 : temperature, circulation, precipitations. */
struct WORLDSEED_API FWorldseedClimateResult
{
	TArray<float> TempMeanC;        // moyenne annuelle au sol
	TArray<float> TempMinC;         // moyenne du mois le plus froid
	TArray<float> TempMaxC;         // moyenne du mois le plus chaud
	TArray<float> SeasonalAmpC;     // amplitude saisonniere
	TArray<float> PrecipMm;         // cumul annuel
	TArray<float> WindU;            // composante est
	TArray<float> WindV;            // composante nord
	TArray<float> Continentality;   // 0 au bord de mer -> 1 loin des cotes
	TArray<float> OrographicUplift; // soulevement force par le relief
	TArray<float> Convergence;      // convergence du vent (ZCIT)

	/** Moyenne des precipitations sur les terres emergees, en mm/an. */
	float MeanLandPrecipMm = 0.0f;
};

/**
 * Etapes 2 et 3 - temperature, circulation atmospherique, precipitations.
 *
 * Rien n'est peint. Le desert apparait vers 30 degres parce que l'air y arrive
 * deja essore, l'ombre pluviometrique parce qu'une chaine coupe la route de
 * l'ocean, la foret equatoriale parce que la circulation y converge.
 *
 * Trois cellules de circulation par hemisphere, comme sur Terre — alizes,
 * westerlies, est polaires — puis advection semi-lagrangienne de l'humidite.
 */
namespace WorldseedClimate
{
	/**
	 * Amplitude saisonniere. Exposee a dessein : l'export des prereglages
	 * Ultra Dynamic Sky a besoin exactement de cette valeur, et la formule ne
	 * doit exister qu'a UN endroit — la dupliquer fabrique des divergences
	 * invisibles.
	 */
	WORLDSEED_API float SeasonalAmplitude(const UWorldseedRules& Rules,
		float LatitudeNormalized, float Continentality);

	/** Rend faux si le calcul a ete interrompu. */
	WORLDSEED_API bool Generate(const UWorldseedRules& Rules,
		const FWorldseedGeometry& Geometry, int32 Seed,
		const TArray<float>& ElevationM, FWorldseedClimateResult& Out,
		const FWorldseedProgressScope& Progress = FWorldseedProgressScope());
}
