// Worldseed - construction des maillages de nappes et de cours d'eau.

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

	void BuildSurfaceFromMask(const TArray<int32>& Labels, int32 Label,
		int32 NX, int32 NY, const FIntRect& Bounds,
		float CellSizeCm, float AltitudeCm, const FVector2D& WorldOriginCm,
		FWorldseedMeshBuffer& Out)
	{
		Out.Reset();

		if (Labels.Num() != NX * NY)
		{
			return;
		}

		const int32 MinCol = FMath::Clamp(Bounds.Min.X, 0, NX - 1);
		const int32 MaxCol = FMath::Clamp(Bounds.Max.X, 0, NX - 1);
		const int32 MinRow = FMath::Clamp(Bounds.Min.Y, 0, NY - 1);
		const int32 MaxRow = FMath::Clamp(Bounds.Max.Y, 0, NY - 1);

		// Une entree par COIN de la fenetre : les cellules voisines partagent
		// leurs sommets, ce qui evite les fissures entre deux quads adjacents.
		const int32 CornerCols = (MaxCol - MinCol) + 2;
		const int32 CornerRows = (MaxRow - MinRow) + 2;

		TArray<int32> CornerIndex;
		CornerIndex.Init(INDEX_NONE, CornerCols * CornerRows);

		auto CornerAt = [&](int32 Col, int32 Row) -> int32
		{
			const int32 Slot = (Row - MinRow) * CornerCols + (Col - MinCol);
			if (CornerIndex[Slot] != INDEX_NONE)
			{
				return CornerIndex[Slot];
			}

			const float X = WorldOriginCm.X + Col * CellSizeCm;
			const float Y = WorldOriginCm.Y + Row * CellSizeCm;

			const int32 New = Out.Vertices.Num();
			Out.Vertices.Emplace(X, Y, AltitudeCm);
			Out.Normals.Add(Up);
			Out.UVs.Emplace(X * UvPerCm, Y * UvPerCm);

			CornerIndex[Slot] = New;
			return New;
		};

		for (int32 Row = MinRow; Row <= MaxRow; ++Row)
		{
			for (int32 Col = MinCol; Col <= MaxCol; ++Col)
			{
				if (Labels[Row * NX + Col] != Label)
				{
					continue;
				}

				AddQuad(Out,
					CornerAt(Col, Row),
					CornerAt(Col, Row + 1),
					CornerAt(Col + 1, Row + 1),
					CornerAt(Col + 1, Row));
			}
		}
	}

	void BuildRibbon(const TArray<FVector>& Points, const TArray<float>& HalfWidthsCm,
		FWorldseedMeshBuffer& Out)
	{
		Out.Reset();

		const int32 Num = Points.Num();
		if (Num < 2 || HalfWidthsCm.Num() != Num)
		{
			return;
		}

		Out.Vertices.Reserve(Num * 2);
		Out.Normals.Reserve(Num * 2);
		Out.UVs.Reserve(Num * 2);

		float Travelled = 0.0f;

		for (int32 I = 0; I < Num; ++I)
		{
			// LA BISSECTRICE, PAS LE SEGMENT SUIVANT. Orienter chaque section
			// sur le segment qui la suit pincerait les bords dans les virages
			// serres : le cours d'eau s'y etranglerait visiblement.
			FVector Forward;
			if (I == 0)
			{
				Forward = Points[1] - Points[0];
			}
			else if (I == Num - 1)
			{
				Forward = Points[Num - 1] - Points[Num - 2];
			}
			else
			{
				const FVector In = (Points[I] - Points[I - 1]).GetSafeNormal();
				const FVector OutDir = (Points[I + 1] - Points[I]).GetSafeNormal();
				Forward = In + OutDir;
			}

			Forward.Z = 0.0;
			Forward = Forward.GetSafeNormal();
			if (Forward.IsNearlyZero())
			{
				Forward = FVector(1.0, 0.0, 0.0);
			}

			const FVector Side = FVector::CrossProduct(Up, Forward).GetSafeNormal();
			const float Half = FMath::Max(HalfWidthsCm[I], 1.0f);

			if (I > 0)
			{
				Travelled += static_cast<float>((Points[I] - Points[I - 1]).Size());
			}
			const float V = Travelled * UvPerCm;

			Out.Vertices.Add(Points[I] - Side * Half);
			Out.Normals.Add(Up);
			Out.UVs.Emplace(0.0f, V);

			Out.Vertices.Add(Points[I] + Side * Half);
			Out.Normals.Add(Up);
			Out.UVs.Emplace(1.0f, V);
		}

		Out.Triangles.Reserve((Num - 1) * 6);
		for (int32 I = 0; I < Num - 1; ++I)
		{
			const int32 A = I * 2;
			AddQuad(Out, A, A + 2, A + 3, A + 1);
		}
	}
}
