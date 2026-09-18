// Worldseed - section "hydrology" de world_rules.json.

#pragma once

#include "CoreMinimal.h"

class UWorldseedRules;

/**
 * Tous les seuils de l'hydrologie, lus une fois.
 *
 * Aucune de ces valeurs n'est ecrite en dur ailleurs : c'est la regle du
 * projet, et elle evite que les deux moities — le generateur Python et le
 * runtime C++ — divergent a la premiere retouche.
 */
struct WORLDSEED_API FWorldseedHydrologyRules
{
	// --- reseau --------------------------------------------------------------
	float RiverDischargeThreshold = 0.00025f;
	int32 MinStrahlerOrder = 2;
	float RunoffCoefficient = 0.35f;

	// --- geometrie hydraulique ----------------------------------------------
	float WidthCoefA = 2.0f;
	float WidthExponent = 0.5f;
	float DepthCoefB = 0.3f;
	float DepthExponent = 0.4f;
	float MinRiverWidthM = 1.0f;
	float MaxRiverWidthM = 48.0f;
	float MinRiverDepthM = 0.4f;

	/**
	 * Exageration ASSUMEE du debit apparent.
	 *
	 * Les coefficients ci-dessus sont ceux de la geometrie hydraulique reelle.
	 * Appliques tels quels a un monde de 32 km, ils donnent des chenaux de 50 cm
	 * — ce qui est PHYSIQUEMENT JUSTE, un tel monde ne pouvant pas porter de
	 * fleuve : son plus grand bassin fait quelques dizaines de km2.
	 *
	 * On exagere donc sciemment, et on le nomme. L'exageration porte sur le
	 * DEBIT APPARENT et non sur la largeur seule, si bien que largeur et
	 * profondeur gardent leur proportion physique : on obtient une vraie
	 * riviere portant N fois son debit, et non un ruban large et plat.
	 */
	float GeometryExaggeration = 8000.0f;

	// --- trace ---------------------------------------------------------------
	float SimplifyToleranceM = 5.0f;
	float MaxPointSpacingM = 40.0f;
	int32 MaxPointsPerRiver = 120;
	float MinRiverLengthM = 225.0f;
	int32 MaxRiverActors = 60;

	// --- lacs ----------------------------------------------------------------
	float MinLakeDepthM = 0.5f;
	float MinLakeAreaHa = 9.375f;
	int32 MaxLakeActors = 40;
	int32 MaxPointsPerLake = 300;

	/**
	 * Largeur de la ripisylve de part et d'autre d'un chenal, en METRES.
	 *
	 * COMPTER EN CELLULES IGNORERAIT LA RESOLUTION, et c'est le piege. A 31 m
	 * par cellule, le chenal fait DEJA 31 m de large alors que la riviere qui
	 * l'occupe en fait 3 a 25 : la berge y est donc deja comprise, et ajouter
	 * deux cellules portait le corridor a 155 m — un quart des terres emergees.
	 * A resolution fine le meme reglage serait au contraire trop etroit.
	 *
	 * On mesure donc en metres, et on retranche ce que le chenal couvre deja.
	 */
	float RiparianBandM = 30.0f;

	/**
	 * Plafond en cellules, contre une resolution tres fine.
	 *
	 * Une grille a 2 m la cellule demanderait quinze dilatations pour trente
	 * metres de berge : correct, mais couteux pour un detail qu'on ne verrait
	 * pas. Le plafond borne la depense.
	 */
	int32 RiparianCells = 3;

	/** Dilatations effectives a cette resolution. */
	int32 RiparianIterations(float MetresPerPixel) const
	{
		// Le chenal occupe deja une cellule : seule la largeur EN PLUS compte.
		const float ExtraM = FMath::Max(RiparianBandM - MetresPerPixel * 0.5f, 0.0f);
		const int32 Wanted = FMath::RoundToInt(ExtraM / FMath::Max(MetresPerPixel, 1e-3f));
		return FMath::Clamp(Wanted, 0, FMath::Max(RiparianCells, 0));
	}

	static FWorldseedHydrologyRules FromRules(const UWorldseedRules& Rules);
};
