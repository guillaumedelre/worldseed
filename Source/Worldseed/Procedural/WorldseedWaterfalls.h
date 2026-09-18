// Worldseed - reperage des cascades sur le reseau hydrographique.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedRules.h"

struct FWorldseedRiver;

/** Une rupture de pente franchie par un cours d'eau. */
struct WORLDSEED_API FWorldseedWaterfall
{
	/** Position en cellules (X colonne, Y ligne), au sommet de la chute. */
	FVector2D TopPx = FVector2D::ZeroVector;
	FVector2D BottomPx = FVector2D::ZeroVector;

	/** Altitude de la surface d'eau en haut et en bas de la chute, en metres. */
	float TopM = 0.0f;
	float BottomM = 0.0f;

	/** Denivele franchi, en metres. */
	float DropM = 0.0f;

	/** Pente du troncon, en degres. */
	float SlopeDeg = 0.0f;

	/** Largeur du cours a cet endroit, en metres. */
	float WidthM = 1.0f;

	/** Index du cours d'eau porteur. */
	int32 RiverIndex = 0;
};

namespace WorldseedWaterfalls
{
	/**
	 * Les troncons trop raides pour etre un lit de riviere.
	 *
	 * UNE CASCADE N'EST PAS UN OBJET A PLACER, C'EST UN CONSTAT. Le reseau
	 * hydrographique suit la ligne de plus grande pente ; la ou cette pente
	 * depasse ce qu'un lit peut tenir, l'eau tombe. On lit donc les cascades
	 * dans le trace existant au lieu de les semer.
	 *
	 * L'ancien projet avait bute ici : ses rivieres depassaient 30 degres sur
	 * huit cours sur seize, ce que le plugin Water refusait de modeliser. En
	 * maillage procedural la contrainte disparait — encore faut-il savoir OU
	 * changer de representation, et c'est ce que ce module repond.
	 */
	WORLDSEED_API void Detect(const TArray<FWorldseedRiver>& Rivers,
		const FWorldseedGeometry& Geometry, float MinSlopeDeg, float MinDropM,
		TArray<FWorldseedWaterfall>& OutFalls);
}
