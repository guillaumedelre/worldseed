// Worldseed - drainage : comblement, directions D8, accumulation.

#pragma once

#include "CoreMinimal.h"

/** Etat de drainage d'un relief. Indexation J * NX + I. */
struct WORLDSEED_API FWorldseedFlow
{
	/** Relief comble, sans cuvette ni plat. */
	TArray<float> FilledM;

	/** Index du voisin receveur, ou soi-meme si exutoire. */
	TArray<int32> Receivers;

	/** Cellules triees par altitude decroissante : amont avant aval. */
	TArray<int32> Order;

	/** Flux accumule, dans l'unite des poids fournis. */
	TArray<float> Accumulation;

	/**
	 * Hauteur comblee au-dessus du relief brut, en metres.
	 *
	 * ELLE NE DESIGNE PAS UN LAC. Combler une cuvette est une operation de
	 * ROUTAGE : elle rend le terrain traversable par l'ecoulement, et ne dit
	 * rien de la presence d'eau libre. C'est WorldseedLakes qui decide, par
	 * composante connexe et sur des criteres de profondeur et d'etendue, ce qui
	 * merite de devenir une nappe.
	 */
	TArray<float> LakeDepthM;
};

/**
 * Chaine de drainage.
 *
 * COMBLEMENT PAR PRIORITY-FLOOD + EPSILON, la ou le generateur Python enchaine
 * trois etapes distinctes — reconstruction morphologique, pente epsilon, puis
 * percage iteratif des bassins fermes. Priority-Flood obtient le meme resultat
 * en UNE passe : chaque cellule recoit un chemin strictement descendant vers un
 * exutoire, donc il ne reste ni cuvette ni plat, et le percage n'a plus d'objet.
 * Les etapes separees du Python servent surtout l'extraction des rivieres, qui
 * ne fait pas partie de ce portage.
 *
 * LA LONGITUDE S'ENROULE : la colonne 0 est voisine de la derniere. Les lignes,
 * elles, se bornent — un pole n'a pas de voisin au-dela.
 */
namespace WorldseedFlow
{
	/**
	 * Weight est l'apport de chaque cellule (pluie ponderee, aire...).
	 * SeaLevelM et les lignes polaires servent d'exutoires.
	 */
	WORLDSEED_API void Compute(const TArray<float>& DemM, const TArray<float>& Weight,
		int32 NX, int32 NY, float SeaLevelM, float EpsilonM, FWorldseedFlow& Out);

	/** Pente sans dimension vers le voisin le plus bas, sur le relief donne. */
	WORLDSEED_API void SlopeToReceiver(const TArray<float>& DemM, int32 NX, int32 NY,
		float SpacingM, TArray<float>& OutSlope);
}
