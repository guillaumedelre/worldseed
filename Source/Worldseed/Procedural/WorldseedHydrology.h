// Worldseed - etape 4b : de l'ecoulement aux rivieres, lacs et cascades.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedHydrologyRules.h"
#include "Procedural/WorldseedLakes.h"
#include "Procedural/WorldseedRivers.h"
#include "Procedural/WorldseedWaterfalls.h"

/** Tout ce que l'hydrologie produit pour un monde. */
struct WORLDSEED_API FWorldseedHydrology
{
	TArray<FWorldseedRiver> Rivers;
	TArray<FWorldseedLake> Lakes;
	TArray<FWorldseedWaterfall> Waterfalls;

	/** Vrai la ou une nappe interieure couvre le sol. Indexe J * NX + I. */
	TArray<bool> LakeMask;

	/**
	 * Etiquette de nappe par cellule, 0 hors de l'eau.
	 *
	 * Chaque lac a SA surface libre : les mailler separement demande de savoir
	 * quelles cellules appartiennent a quelle cuvette, ce qu'un masque booleen
	 * ne dit pas.
	 */
	TArray<int32> LakeLabels;

	/**
	 * Vrai sur les chenaux et leurs berges.
	 *
	 * Distinct des polylignes de Rivers : celles-ci ne gardent que les axes
	 * principaux, quand ce masque couvre TOUT le reseau, jusqu'aux plus petits
	 * filets. C'est lui qui donne aux biomes leur trame hydrographique.
	 */
	TArray<bool> RiverMask;

	bool IsEmpty() const { return Rivers.Num() == 0 && Lakes.Num() == 0; }
};

/**
 * Chaine hydrologique.
 *
 * LE DEBIT EST PONDERE PAR LA CARTE DE PLUIE, et c'est ce qui fait tout :
 * un bassin equatorial porte un fleuve, un bassin desertique n'atteint jamais
 * le seuil et ne produit aucune riviere. C'est le climat qui decide du reseau
 * hydrographique, pas le relief seul.
 */
namespace WorldseedHydrology
{
	/**
	 * Pente au-dela de laquelle un troncon devient une cascade.
	 *
	 * Absente de world_rules.json : le generateur Python ne modelisait pas les
	 * cascades. La valeur vient de la mesure faite sur l'ancien projet, ou les
	 * cours d'eau au-dela de 30 degres refusaient de se laisser modeliser comme
	 * des lits.
	 */
	constexpr float DefaultWaterfallSlopeDeg = 30.0f;
	constexpr float DefaultWaterfallMinDropM = 3.0f;

	WORLDSEED_API void Generate(const TArray<float>& ElevationM,
		const TArray<float>& PrecipMm, const FWorldseedGeometry& Geometry,
		const FWorldseedHydrologyRules& Rules, FWorldseedHydrology& Out);
}
