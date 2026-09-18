// Worldseed - decoupe des cours d'eau en troncons qu'une spline peut porter.

#include "Procedural/WorldseedRiverReaches.h"

#include "Procedural/WorldseedRivers.h"
#include "Procedural/WorldseedRules.h"

namespace
{
	/**
	 * Pente d'un segment, en degres, prise sur la SURFACE D'EAU.
	 *
	 * Sur la surface et non sur le relief brut : c'est elle que la spline
	 * portera, et c'est donc elle qui decide si la spline s'en sort. Un cours
	 * qui traverse une nappe garde une surface plate au-dessus d'un fond
	 * accidente — le decouper la n'aurait aucun sens.
	 */
	float SegmentSlopeDeg(const FWorldseedRiver& River, int32 I, float MetresPerCell)
	{
		const FVector2D Step = River.PointsPx[I + 1] - River.PointsPx[I];
		const float RunM = Step.Size() * MetresPerCell;
		const float DropM = River.SurfaceM[I] - River.SurfaceM[I + 1];

		if (RunM <= KINDA_SMALL_NUMBER)
		{
			// Deux points confondus : un ressaut vertical s'il descend, rien
			// du tout sinon. Dans les deux cas la spline n'a pas a s'y risquer.
			return (DropM > KINDA_SMALL_NUMBER) ? 90.0f : 0.0f;
		}

		return FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(DropM), RunM));
	}

	/** Le trace est-il exploitable ? Les tableaux paralleles doivent concorder. */
	bool IsUsable(const FWorldseedRiver& River)
	{
		const int32 Count = River.PointsPx.Num();
		return (Count >= 2) && (River.SurfaceM.Num() == Count)
			&& (River.WidthM.Num() == Count);
	}
}

namespace WorldseedRiverReaches
{
	void Split(const TArray<FWorldseedRiver>& Rivers,
		const FWorldseedGeometry& Geometry, float MaxSlopeDeg, int32 MinPoints,
		TArray<FWorldseedReach>& OutReaches)
	{
		OutReaches.Reset();

		const float MetresPerCell = Geometry.MetersPerPixel();
		const int32 Floor = FMath::Max(MinPoints, 2);

		for (int32 R = 0; R < Rivers.Num(); ++R)
		{
			const FWorldseedRiver& River = Rivers[R];
			if (!IsUsable(River))
			{
				continue;
			}

			// Un bief court tant que les segments restent doux. Le premier
			// ressaut le ferme, et le suivant en rouvre un APRES lui : le
			// segment de chute n'appartient a aucun bief, il reste au maillage
			// procedural.
			int32 First = 0;
			for (int32 I = 0; I + 1 < River.PointsPx.Num(); ++I)
			{
				if (SegmentSlopeDeg(River, I, MetresPerCell) <= MaxSlopeDeg)
				{
					continue;
				}

				if (I - First + 1 >= Floor)
				{
					OutReaches.Add({ R, First, I });
				}
				First = I + 1;
			}

			const int32 Last = River.PointsPx.Num() - 1;
			if (Last - First + 1 >= Floor)
			{
				OutReaches.Add({ R, First, Last });
			}
		}
	}

	void MarkTakenSegments(const TArray<FWorldseedRiver>& Rivers,
		const TArray<FWorldseedReach>& Reaches, TArray<TArray<bool>>& OutTaken)
	{
		OutTaken.Reset();
		OutTaken.SetNum(Rivers.Num());

		for (int32 R = 0; R < Rivers.Num(); ++R)
		{
			OutTaken[R].Init(false, FMath::Max(Rivers[R].PointsPx.Num() - 1, 0));
		}

		for (const FWorldseedReach& Reach : Reaches)
		{
			if (!OutTaken.IsValidIndex(Reach.RiverIndex))
			{
				continue;
			}

			TArray<bool>& Taken = OutTaken[Reach.RiverIndex];
			for (int32 I = Reach.First; I < Reach.Last && Taken.IsValidIndex(I); ++I)
			{
				Taken[I] = true;
			}
		}
	}
}
