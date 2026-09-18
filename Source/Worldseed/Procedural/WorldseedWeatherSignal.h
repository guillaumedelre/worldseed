// Worldseed - signal temporel deterministe qui fait varier la meteo.

#pragma once

#include "CoreMinimal.h"

/**
 * L'agitation atmospherique, en fonction du temps.
 *
 * POURQUOI UN SIGNAL PLUTOT QU'UN TIRAGE. Le climat dit ce qu'on peut ATTENDRE
 * d'un lieu, pas le temps qu'il y fait a l'instant : une foret equatoriale
 * recoit 2500 mm par an sans pleuvoir en continu, un desert connait son orage.
 * Il faut donc une deuxieme dimension, le temps — et elle doit etre PARTAGEE
 * par tout le monde, sinon deux lieux voisins auraient des averses sans rapport.
 *
 * Le signal derive de la graine du monde : deux parties lancees sur la meme
 * graine voient la meme meteo au meme instant, ce qui est la regle du projet.
 */
namespace WorldseedWeatherSignal
{
	/**
	 * Agitation a l'instant donne, de 0 (calme plat) a 1 (regime perturbe).
	 *
	 * PeriodS est la duree d'un cycle complet.
	 */
	WORLDSEED_API float Storminess(float TimeSeconds, float PeriodS, int32 Seed);

	/**
	 * Deuxieme signal, independant du premier.
	 *
	 * Le brouillard ne suit pas les fronts : le lier a Storminess le ferait
	 * arriver avec la pluie, alors qu'il se leve justement quand elle cesse.
	 */
	WORLDSEED_API float Haze(float TimeSeconds, float PeriodS, int32 Seed);
}
