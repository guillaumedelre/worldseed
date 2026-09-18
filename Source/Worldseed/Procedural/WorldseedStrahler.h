// Worldseed - hierarchie du reseau hydrographique.

#pragma once

#include "CoreMinimal.h"

/**
 * Ordres de Strahler et cours principal.
 *
 * A QUOI CELA SERT ICI. Un reseau de drainage brut compte des dizaines de
 * milliers de filets d'eau : les tracer tous noierait la carte et la memoire.
 * L'ordre de Strahler donne un critere OBJECTIF de hierarchie — deux affluents
 * de meme rang qui confluent donnent le rang suivant — et permet de ne garder
 * que les axes qui meritent d'exister en jeu.
 */
namespace WorldseedStrahler
{
	/**
	 * Pour chaque cellule, le tributaire de plus fort debit (-1 si aucun).
	 *
	 * C'est ce qui permet de remonter le COURS PRINCIPAL d'un fleuve depuis son
	 * embouchure : a chaque confluence on suit le plus gros apport, comme on
	 * nomme un fleuve dans la realite.
	 */
	WORLDSEED_API void BestDonors(const TArray<int32>& Receivers,
		const TArray<float>& Accumulation, const TArray<bool>& Valid,
		TArray<int32>& OutDonors);

	/**
	 * Ordre de Strahler sur le reseau de chenaux.
	 *
	 * Order doit lister les cellules de l'amont vers l'aval : c'est l'ordre que
	 * WorldseedFlow produit deja.
	 */
	WORLDSEED_API void Orders(const TArray<int32>& Receivers,
		const TArray<int32>& Order, const TArray<bool>& IsChannel,
		TArray<int32>& OutOrders);
}
