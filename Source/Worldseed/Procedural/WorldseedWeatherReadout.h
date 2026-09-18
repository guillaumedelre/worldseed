// Worldseed - affichage du releve meteo pendant le jeu.

#pragma once

#include "CoreMinimal.h"

struct FWorldseedClimateSample;
struct FWorldseedWeather;

/**
 * Le releve a l'ecran.
 *
 * POURQUOI IL EXISTE. La meteo evolue sur plusieurs minutes et se melange en
 * douceur : sans chiffres, impossible de dire si elle suit vraiment le climat
 * ou si elle est figee. Le releve donne la lecture directe — ou on se trouve,
 * ce que le climat y prevoit, et ce qui est reellement ecrit dans le ciel.
 *
 * Separe du pilotage : c'est de la PRESENTATION, et le ciel doit marcher
 * exactement pareil qu'on l'affiche ou non.
 */
namespace WorldseedWeatherReadout
{
	/** AltitudeM est celle du joueur, zero au niveau de la mer. */
	WORLDSEED_API void Draw(const FWorldseedWeather& Weather,
		const FWorldseedClimateSample& Sample,
		float LongitudeDeg, float AltitudeM, float SeasonPhase,
		float PeriodS, float HoldSeconds);
}
