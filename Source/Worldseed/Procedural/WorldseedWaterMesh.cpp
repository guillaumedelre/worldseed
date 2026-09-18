// Worldseed - construction du maillage de la surface d'ocean.

#include "Procedural/WorldseedWaterMesh.h"

namespace WorldseedWaterMesh
{
	namespace
	{
		const FVector Up(0.0, 0.0, 1.0);

		/** Repere UV en metres, pour que l'echelle des vagues suive le monde. */
		constexpr float UvPerCm = 0.01f;

		void AddQuad(FWorldseedMeshBuffer& Out, int32 A, int32 B, int32 C, int32 D)
		{
			Out.Triangles.Add(A); Out.Triangles.Add(B); Out.Triangles.Add(C);
			Out.Triangles.Add(A); Out.Triangles.Add(C); Out.Triangles.Add(D);
		}
	}

	void BuildPlane(float WidthCm, float HeightCm, float AltitudeCm,
		int32 Subdivisions, FWorldseedMeshBuffer& Out)
	{
		Out.Reset();

		const int32 Steps = FMath::Max(Subdivisions, 1);
		const int32 Side = Steps + 1;

		const float HalfW = WidthCm * 0.5f;
		const float HalfH = HeightCm * 0.5f;

		Out.Vertices.Reserve(Side * Side);
		Out.Normals.Reserve(Side * Side);
		Out.UVs.Reserve(Side * Side);

		for (int32 Row = 0; Row < Side; ++Row)
		{
			const float V = static_cast<float>(Row) / Steps;
			for (int32 Col = 0; Col < Side; ++Col)
			{
				const float U = static_cast<float>(Col) / Steps;

				const float X = FMath::Lerp(-HalfW, HalfW, U);
				const float Y = FMath::Lerp(-HalfH, HalfH, V);

				Out.Vertices.Emplace(X, Y, AltitudeCm);
				Out.Normals.Add(Up);
				Out.UVs.Emplace(X * UvPerCm, Y * UvPerCm);
			}
		}

		Out.Triangles.Reserve(Steps * Steps * 6);
		for (int32 Row = 0; Row < Steps; ++Row)
		{
			for (int32 Col = 0; Col < Steps; ++Col)
			{
				const int32 A = Row * Side + Col;
				AddQuad(Out, A, A + Side, A + Side + 1, A + 1);
			}
		}
	}
}
