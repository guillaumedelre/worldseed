// Worldseed - etiquetage en composantes connexes.

#pragma once

#include "CoreMinimal.h"

/**
 * Composantes connexes d'un masque binaire.
 *
 * LA LONGITUDE S'ENROULE : la colonne 0 est voisine de la derniere, faute de
 * quoi une nappe d'eau a cheval sur l'antimeridien serait comptee comme deux
 * lacs distincts et chacun pourrait tomber sous le seuil de surface. Les lignes,
 * elles, se bornent : un pole n'a pas de voisin au-dela.
 */
namespace WorldseedLabel
{
	/**
	 * Etiquette les cellules vraies, de 1 a N ; 0 pour le fond.
	 *
	 * Rend le nombre de composantes. Connexite a quatre voisins, comme le
	 * generateur Python.
	 */
	WORLDSEED_API int32 Components(const TArray<bool>& Mask, int32 NX, int32 NY,
		TArray<int32>& OutLabels);
}
