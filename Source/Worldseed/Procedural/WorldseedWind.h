// Worldseed - circulation atmospherique dominante.

#pragma once

#include "CoreMinimal.h"

/**
 * Le vent dominant a une latitude.
 *
 * REJOUE LA CIRCULATION DU MODELE CLIMATIQUE plutot que d'inventer un vent
 * decoratif : trois cellules par hemisphere, alizes vers l'ouest, westerlies
 * vers l'est, est polaires vers l'ouest. Ce sont ces memes vents qui ont
 * transporte l'humidite pendant la generation, donc le vent qu'on sent en jeu
 * est celui qui a dessine les deserts et les faces au vent des montagnes.
 *
 * HORS GIGUE, LE CHAMP NE DEPEND QUE DE LA LATITUDE. C'est ce qui permet de le
 * retrouver ici sans transporter la grille complete a travers le cache.
 */
namespace WorldseedWind
{
	/**
	 * Direction unitaire du vent dominant.
	 *
	 * LatSpanDeg est l'etendue de latitude de la carte : les ceintures se
	 * placent en proportion de cette etendue, exactement comme dans le modele.
	 */
	WORLDSEED_API void Prevailing(float LatitudeDeg, float LatSpanDeg,
		float& OutEast, float& OutNorth);

	/** Lacet monde du vent dominant, en degres. +X vers l'est, +Y vers le nord. */
	WORLDSEED_API float PrevailingYawDeg(float LatitudeDeg, float LatSpanDeg);
}
