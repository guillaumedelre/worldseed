// Worldseed - construction du maillage de la surface d'ocean.

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
 * Maillage d'eau.
 *
 * UNE SEULE FORME : l'ocean est un plan. Les nappes de lac et les rubans de
 * riviere ont disparu avec l'hydrologie, le 18 septembre 2026. Rien ici ne
 * sait d'ou viennent ces donnees — c'est ce qui permet de tester la geometrie
 * sans monde.
 */
namespace WorldseedWaterMesh
{
	/** Plan horizontal centre sur l'origine locale. */
	WORLDSEED_API void BuildPlane(float WidthCm, float HeightCm, float AltitudeCm,
		int32 Subdivisions, FWorldseedMeshBuffer& Out);
}
