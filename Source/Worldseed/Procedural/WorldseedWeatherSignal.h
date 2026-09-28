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

	/**
	 * Rend UNIFORME la sortie de `Storminess`, pour qu'un seuil soit une part.
	 *
	 * POURQUOI C'EST NECESSAIRE. `Storminess` somme trois octaves : sa loi est
	 * une cloche resserree autour d'un demi, pas une loi uniforme. Seuiller
	 * directement dessus ne rend donc PAS la fraction de temps qu'on demande --
	 * c'est le piege du « seuil n'est pas une part » que ce depot a paye quatre
	 * fois. Apres cette transformee, demander les cinq pour cent du haut donne
	 * bien cinq pour cent du temps.
	 *
	 * ⚠ SA TABLE EST UNE MESURE, PAS UN CALCUL, et c'est tout le point. Une
	 * premiere version posait une cloche d'ecart-type 0,186, obtenu par
	 * l'algebre d'une somme de trois lois uniformes. Deux choses y etaient
	 * fausses : `ValueNoise` n'est pas uniforme -- il interpole deux tirages par
	 * un smoothstep, ce qui resserre la loi -- et la somme a un SUPPORT BORNE,
	 * donc ses queues tombent bien plus vite qu'une cloche. La taiga voyait
	 * alors 0,5 % de precipitation pour les 6 % visees, un facteur douze.
	 *
	 * `Worldseed.Meteo.LeSignalEstUniforme` releve la loi reelle et verifie que
	 * cette table lui correspond encore : changer les poids des octaves fait
	 * donc tomber un test, au lieu de deregler le ciel en silence.
	 */
	WORLDSEED_API float Uniformiser(float Storm);
}
