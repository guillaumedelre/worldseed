// Worldseed - construction des maillages de nappes et de cours d'eau.

#pragma once

#include "CoreMinimal.h"

/** Un maillage en cours d'assemblage, dans la forme attendue par ProceduralMesh. */
struct WORLDSEED_API FWorldseedMeshBuffer
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;

	void Reset()
	{
		Vertices.Reset();
		Triangles.Reset();
		Normals.Reset();
		UVs.Reset();
	}

	bool IsEmpty() const { return Triangles.Num() == 0; }
};

/**
 * Maillages d'eau.
 *
 * TROIS FORMES, ET SEULEMENT TROIS. L'ocean est un plan ; un lac est une nappe
 * horizontale epousant un masque de cellules ; une riviere est un ruban qui
 * suit une polyligne en variant de largeur. Rien ici ne sait d'ou viennent ces
 * donnees — c'est ce qui permet de tester la geometrie sans monde.
 */
namespace WorldseedWaterMesh
{
	/** Plan horizontal centre sur l'origine locale. */
	WORLDSEED_API void BuildPlane(float WidthCm, float HeightCm, float AltitudeCm,
		int32 Subdivisions, FWorldseedMeshBuffer& Out);

	/**
	 * Nappe horizontale sur les cellules marquees d'un masque.
	 *
	 * POURQUOI UN MAILLAGE DE CELLULES ET NON UN POLYGONE TRIANGULE. Trianguler
	 * le contour demanderait de decouper un polygone concave, a trous, parfois
	 * non simple — et une seule erreur y produit une nappe percee ou repliee.
	 * Le masque, lui, est exact par construction : deux triangles par cellule,
	 * aucun cas particulier, et les iles se decoupent toutes seules puisqu'elles
	 * ne sont tout simplement pas dans le masque.
	 *
	 * Mask est indexe Row * NX + Col sur la grille du monde ; seules les
	 * cellules portant Label sont retenues.
	 */
	WORLDSEED_API void BuildSurfaceFromMask(const TArray<int32>& Labels, int32 Label,
		int32 NX, int32 NY, const FIntRect& Bounds,
		float CellSizeCm, float AltitudeCm, const FVector2D& WorldOriginCm,
		FWorldseedMeshBuffer& Out);

	/**
	 * Ruban le long d'une polyligne, de largeur variable.
	 *
	 * Points sont en centimetres monde (X, Y) ; Altitudes donne le Z de chaque
	 * point, HalfWidths sa demi-largeur. Les trois tableaux vont de pair.
	 *
	 * LE RUBAN EST ORIENTE PAR LA BISSECTRICE aux sommets intermediaires, et non
	 * par le segment suivant : sinon les bords se pincent dans les virages
	 * serres, ce qui se voit comme un etranglement du cours d'eau.
	 */
	WORLDSEED_API void BuildRibbon(const TArray<FVector>& Points,
		const TArray<float>& HalfWidthsCm, FWorldseedMeshBuffer& Out);
}
