// Worldseed - extraction des lacs depuis les cuvettes comblees.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedHydrologyRules.h"
#include "Procedural/WorldseedRules.h"

struct FWorldseedFlow;

/** Une nappe d'eau interieure. */
struct WORLDSEED_API FWorldseedLake
{
	/** Altitude de la surface libre, en metres. */
	float SurfaceM = 0.0f;

	float AreaHa = 0.0f;
	int32 CellCount = 0;

	/** Point le plus profond, en cellules (X colonne, Y ligne). */
	FVector2D DeepestPx = FVector2D::ZeroVector;

	/**
	 * Hauteur d'eau au point le plus profond, en metres.
	 *
	 * C'EST L'EPAISSEUR DE LA NAPPE, PAS SON ALTITUDE. Le plugin Water en tire
	 * la hauteur du volume de collision du lac ; a defaut ce volume est PLAT,
	 * et tout ce qui depend d'etre DANS l'eau — le voile sous-marin, la nage,
	 * la flottabilite — cesse des qu'on descend d'un metre.
	 */
	float MaxDepthM = 0.0f;

	/** Rivage ferme et ordonne, en cellules. */
	TArray<FVector2D> OutlinePx;

	/** Etiquette de la composante, pour retrouver ses cellules dans la grille. */
	int32 Label = 0;

	/** Emprise en cellules, bornes incluses. Evite de balayer toute la carte. */
	FIntRect BoundsPx = FIntRect(0, 0, 0, 0);
};

namespace WorldseedLakes
{
	/**
	 * Masque des VRAIS lacs, distinct du simple comblement numerique.
	 *
	 * COMBLER UNE CUVETTE EST UNE OPERATION DE ROUTAGE : elle rend le terrain
	 * traversable par l'ecoulement et ne dit rien de la presence d'eau libre.
	 * Traiter toute cellule comblee comme un lac — ce que faisait une premiere
	 * version du generateur avec un seuil commun de 5 cm — couvrait 20,5 % des
	 * terres de lacs et arretait neuf rivieres sur dix au premier bassin.
	 *
	 * Un lac n'existe donc que si la depression est a la fois assez PROFONDE
	 * sous son point de deversement et assez ETENDUE. Le critere s'evalue par
	 * COMPOSANTE CONNEXE et non par cellule : c'est la cuvette entiere qui est
	 * un lac, ou ne l'est pas.
	 */
	WORLDSEED_API void BuildMask(const TArray<float>& LakeDepthM,
		const TArray<float>& ElevationM, const FWorldseedGeometry& Geometry,
		const FWorldseedHydrologyRules& Rules, TArray<bool>& OutMask);

	/** Les cuvettes qui satisfont le critere deviennent des nappes. */
	WORLDSEED_API void Extract(const FWorldseedFlow& Flow,
		const TArray<float>& ElevationM, const FWorldseedGeometry& Geometry,
		const FWorldseedHydrologyRules& Rules, TArray<FWorldseedLake>& OutLakes,
		TArray<int32>& OutLabels);
}
