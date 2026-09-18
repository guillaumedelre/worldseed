// Worldseed - portage de Tools/WorldGen/worldgen/tectonics.py.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedJob.h"
#include "Procedural/WorldseedRules.h"

/**
 * Sortie de l'etape tectonique. Les altitudes sont en METRES avec 0 au niveau
 * de la mer : c'est le recalage par quantile qui le garantit, et c'est lui qui
 * fixe la part de terres emergees a la valeur terrestre.
 */
struct WORLDSEED_API FWorldseedTectonicResult
{
	/** Altitude en metres, 0 = niveau de la mer. Indexe J * N + I. */
	TArray<float> ElevationM;

	/** Plaque la plus proche pour chaque cellule. */
	TArray<int32> PlateId;

	/** Cellule au-dessus du seuil continental (apres bruitage du masque). */
	TArray<uint8> IsContinental;

	/** > 0 collision, < 0 rift. */
	TArray<float> Convergence;

	/** Decalage applique pour amener le niveau marin a zero. */
	float SeaLevelShiftM = 0.0f;

	/** Part de cellules au-dessus du niveau marin, pour verification. */
	float MeasuredLandRatio = 0.0f;
};

/**
 * Etape 1 - tectonique et relief brut.
 *
 * Des plaques de Voronoi derivent les unes contre les autres : les frontieres
 * convergentes soulevent des chaines, les divergentes ouvrent des rifts.
 *
 * Pourquoi des plaques plutot qu'un simple bruit fractal : les chaines doivent
 * etre LINEAIRES et ORIENTEES, car c'est ce qui produit de vraies ombres
 * pluviometriques a l'etape climat. Un fBm seul donne des bosses isotropes,
 * donc un climat sans structure.
 */
namespace WorldseedTectonics
{
	/** Rend faux si le calcul a ete interrompu. */
	WORLDSEED_API bool Generate(const UWorldseedRules& Rules,
		const FWorldseedGeometry& Geometry, int32 Seed,
		FWorldseedTectonicResult& Out,
		const FWorldseedProgressScope& Progress = FWorldseedProgressScope());
}
