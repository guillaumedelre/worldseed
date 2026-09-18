// Worldseed - extraction des lacs depuis les cuvettes comblees.

#include "Procedural/WorldseedLakes.h"

#include "Procedural/WorldseedFlow.h"
#include "Procedural/WorldseedLabel.h"
#include "Procedural/WorldseedPolyline.h"

namespace WorldseedLakes
{
	namespace
	{
		constexpr float SquareMetresPerHectare = 10000.0f;

		/** Mediane par selection : moins cher qu'un tri complet. */
		float Median(TArray<float>& Values)
		{
			if (Values.Num() == 0)
			{
				return 0.0f;
			}

			const int32 Middle = Values.Num() / 2;
			Values.Sort();
			return Values[Middle];
		}
	}

	void BuildMask(const TArray<float>& LakeDepthM, const TArray<float>& ElevationM,
		const FWorldseedGeometry& Geometry, const FWorldseedHydrologyRules& Rules,
		TArray<bool>& OutMask)
	{
		const int32 Count = Geometry.CellCount();
		OutMask.Init(false, Count);

		if (LakeDepthM.Num() != Count || ElevationM.Num() != Count)
		{
			return;
		}

		// Candidats : toute terre emergee que le comblement a relevee.
		TArray<bool> Filled;
		Filled.Init(false, Count);
		bool bAny = false;
		for (int32 I = 0; I < Count; ++I)
		{
			if (LakeDepthM[I] > 0.0f && ElevationM[I] > 0.0f)
			{
				Filled[I] = true;
				bAny = true;
			}
		}
		if (!bAny)
		{
			return;
		}

		TArray<int32> Labels;
		const int32 NumComponents = WorldseedLabel::Components(
			Filled, Geometry.NX, Geometry.NY, Labels);
		if (NumComponents == 0)
		{
			return;
		}

		// Profondeur maximale et etendue, par composante.
		TArray<float> MaxDepth;
		TArray<int32> Cells;
		MaxDepth.Init(0.0f, NumComponents + 1);
		Cells.Init(0, NumComponents + 1);

		for (int32 I = 0; I < Count; ++I)
		{
			const int32 Label = Labels[I];
			if (Label > 0)
			{
				MaxDepth[Label] = FMath::Max(MaxDepth[Label], LakeDepthM[I]);
				++Cells[Label];
			}
		}

		const float MetresPerPixel = Geometry.MetersPerPixel();
		const float HectaresPerCell =
			(MetresPerPixel * MetresPerPixel) / SquareMetresPerHectare;

		TArray<bool> Keep;
		Keep.Init(false, NumComponents + 1);
		for (int32 L = 1; L <= NumComponents; ++L)
		{
			Keep[L] = (MaxDepth[L] >= Rules.MinLakeDepthM)
				&& (Cells[L] * HectaresPerCell >= Rules.MinLakeAreaHa);
		}

		for (int32 I = 0; I < Count; ++I)
		{
			OutMask[I] = Keep[Labels[I]];
		}
	}

	void Extract(const FWorldseedFlow& Flow, const TArray<float>& ElevationM,
		const FWorldseedGeometry& Geometry, const FWorldseedHydrologyRules& Rules,
		TArray<FWorldseedLake>& OutLakes, TArray<int32>& OutLabels)
	{
		OutLakes.Reset();
		OutLabels.Reset();

		const int32 NX = Geometry.NX;
		const int32 NY = Geometry.NY;
		const int32 Count = Geometry.CellCount();
		if (Flow.LakeDepthM.Num() != Count || Flow.FilledM.Num() != Count)
		{
			return;
		}

		TArray<bool> Mask;
		BuildMask(Flow.LakeDepthM, ElevationM, Geometry, Rules, Mask);

		TArray<int32>& Labels = OutLabels;
		const int32 NumComponents = WorldseedLabel::Components(Mask, NX, NY, Labels);
		if (NumComponents == 0)
		{
			return;
		}

		// --- inventaire par composante ------------------------------------------
		struct FBasin
		{
			int32 Cells = 0;
			float DeepestValue = -BIG_NUMBER;
			int32 DeepestIndex = 0;
			int32 MinCol = TNumericLimits<int32>::Max();
			int32 MaxCol = TNumericLimits<int32>::Min();
			int32 MinRow = TNumericLimits<int32>::Max();
			int32 MaxRow = TNumericLimits<int32>::Min();
		};

		TArray<FBasin> Basins;
		Basins.SetNum(NumComponents + 1);

		for (int32 I = 0; I < Count; ++I)
		{
			const int32 Label = Labels[I];
			if (Label == 0)
			{
				continue;
			}

			FBasin& B = Basins[Label];
			++B.Cells;

			if (Flow.LakeDepthM[I] > B.DeepestValue)
			{
				B.DeepestValue = Flow.LakeDepthM[I];
				B.DeepestIndex = I;
			}

			const int32 Row = I / NX;
			const int32 Col = I % NX;
			B.MinCol = FMath::Min(B.MinCol, Col);
			B.MaxCol = FMath::Max(B.MaxCol, Col);
			B.MinRow = FMath::Min(B.MinRow, Row);
			B.MaxRow = FMath::Max(B.MaxRow, Row);
		}

		// Les plus grandes d'abord : si le quota tranche, il doit garder ce qui
		// se voit.
		TArray<int32> Ordered;
		Ordered.Reserve(NumComponents);
		for (int32 L = 1; L <= NumComponents; ++L)
		{
			if (Basins[L].Cells > 0)
			{
				Ordered.Add(L);
			}
		}
		Ordered.Sort([&Basins](int32 A, int32 B) { return Basins[A].Cells > Basins[B].Cells; });

		const int32 Limit = FMath::Min(Ordered.Num(), FMath::Max(Rules.MaxLakeActors, 0));
		const float MetresPerPixel = Geometry.MetersPerPixel();
		int32 PinchesFilled = 0;
		const float HectaresPerCell =
			(MetresPerPixel * MetresPerPixel) / SquareMetresPerHectare;

		for (int32 Rank = 0; Rank < Limit; ++Rank)
		{
			const int32 Label = Ordered[Rank];
			const FBasin& B = Basins[Label];

			const int32 SubWidth = B.MaxCol - B.MinCol + 1;
			const int32 SubHeight = B.MaxRow - B.MinRow + 1;

			TArray<bool> SubMask;
			SubMask.Init(false, SubWidth * SubHeight);

			TArray<float> Surfaces;
			Surfaces.Reserve(B.Cells);

			for (int32 Row = B.MinRow; Row <= B.MaxRow; ++Row)
			{
				for (int32 Col = B.MinCol; Col <= B.MaxCol; ++Col)
				{
					const int32 Index = Row * NX + Col;
					if (Labels[Index] != Label)
					{
						continue;
					}

					SubMask[(Row - B.MinRow) * SubWidth + (Col - B.MinCol)] = true;
					Surfaces.Add(Flow.FilledM[Index]);
				}
			}

			FWorldseedLake Lake;
			Lake.Label = Label;
			Lake.BoundsPx = FIntRect(B.MinCol, B.MinRow, B.MaxCol, B.MaxRow);
			Lake.CellCount = B.Cells;
			Lake.AreaHa = B.Cells * HectaresPerCell;

			// LA MEDIANE, PAS LA MOYENNE. Le relief comble est plat par
			// construction sur presque toute la cuvette, mais ses bords portent
			// la pente epsilon du routage : une moyenne s'y laisserait tirer.
			Lake.SurfaceM = Median(Surfaces);

			Lake.DeepestPx = FVector2D(
				static_cast<float>(B.DeepestIndex % NX),
				static_cast<float>(B.DeepestIndex / NX));

			// La cuvette la plus creuse donne l'epaisseur de la nappe.
			Lake.MaxDepthM = FMath::Max(B.DeepestValue, 0.0f);

			// Le traceur suppose une region connexe par les ARETES : on lui la
			// donne. Voir FillDiagonalPinches.
			PinchesFilled += WorldseedPolyline::FillDiagonalPinches(
				SubMask, SubWidth, SubHeight);

			TArray<FVector2D> Outline = WorldseedPolyline::TraceOutline(
				SubMask, SubWidth, SubHeight);
			Outline = WorldseedPolyline::Reduce(Outline, Rules.MaxPointsPerLake);

			Lake.OutlinePx.Reserve(Outline.Num());
			for (const FVector2D& P : Outline)
			{
				Lake.OutlinePx.Emplace(P.X + B.MinCol, P.Y + B.MinRow);
			}

			OutLakes.Add(MoveTemp(Lake));
		}

		if (PinchesFilled > 0)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] lacs : %d pincements diagonaux combles avant tracage"),
				PinchesFilled);
		}
	}
}
