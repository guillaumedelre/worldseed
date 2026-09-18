// Worldseed - reperage des cascades sur le reseau hydrographique.

#include "Procedural/WorldseedWaterfalls.h"
#include "Procedural/WorldseedRivers.h"

namespace WorldseedWaterfalls
{
	void Detect(const TArray<FWorldseedRiver>& Rivers, const FWorldseedGeometry& Geometry,
		float MinSlopeDeg, float MinDropM, TArray<FWorldseedWaterfall>& OutFalls)
	{
		OutFalls.Reset();

		const float MetresPerPixel = Geometry.MetersPerPixel();
		const float MinSlopeTangent = FMath::Tan(FMath::DegreesToRadians(MinSlopeDeg));

		for (int32 R = 0; R < Rivers.Num(); ++R)
		{
			const FWorldseedRiver& River = Rivers[R];

			// UNE CHUTE EST UNE RUPTURE DE LA SURFACE D'EAU, pas du sol. Mesuree
			// sur le relief brut, elle se declencherait aussi au bord d'une
			// cuvette comblee — la ou l'eau, justement, reste a niveau.
			if (River.SurfaceM.Num() != River.PointsPx.Num())
			{
				continue;
			}

			for (int32 I = 1; I < River.PointsPx.Num(); ++I)
			{
				const FVector2D& Top = River.PointsPx[I - 1];
				const FVector2D& Bottom = River.PointsPx[I];

				const float RunM = static_cast<float>((Bottom - Top).Size()) * MetresPerPixel;
				if (RunM < 1e-3f)
				{
					continue;
				}

				const float TopM = River.SurfaceM[I - 1];
				const float BottomM = River.SurfaceM[I];
				const float DropM = TopM - BottomM;
				if (DropM < MinDropM)
				{
					continue;
				}

				const float Tangent = DropM / RunM;
				if (Tangent < MinSlopeTangent)
				{
					continue;
				}

				FWorldseedWaterfall Fall;
				Fall.TopPx = Top;
				Fall.BottomPx = Bottom;
				Fall.TopM = TopM;
				Fall.BottomM = BottomM;
				Fall.DropM = DropM;
				Fall.SlopeDeg = FMath::RadiansToDegrees(FMath::Atan(Tangent));
				Fall.WidthM = River.WidthM.IsValidIndex(I) ? River.WidthM[I] : 1.0f;
				Fall.RiverIndex = R;

				OutFalls.Add(Fall);
			}
		}
	}
}
