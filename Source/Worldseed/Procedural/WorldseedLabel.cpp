// Worldseed - etiquetage en composantes connexes.

#include "Procedural/WorldseedLabel.h"

namespace WorldseedLabel
{
	int32 Components(const TArray<bool>& Mask, int32 NX, int32 NY,
		TArray<int32>& OutLabels)
	{
		const int32 Count = NX * NY;
		OutLabels.Init(0, Count);

		if (NX < 1 || NY < 1 || Mask.Num() != Count)
		{
			return 0;
		}

		int32 Next = 0;

		// Parcours en largeur depuis chaque graine non encore etiquetee. Une
		// pile explicite plutot qu'une recursion : une nappe peut couvrir des
		// centaines de milliers de cellules.
		TArray<int32> Stack;

		for (int32 Seed = 0; Seed < Count; ++Seed)
		{
			if (!Mask[Seed] || OutLabels[Seed] != 0)
			{
				continue;
			}

			++Next;
			Stack.Reset();
			Stack.Add(Seed);
			OutLabels[Seed] = Next;

			while (Stack.Num() > 0)
			{
				const int32 Cell = Stack.Pop(EAllowShrinking::No);
				const int32 Row = Cell / NX;
				const int32 Col = Cell % NX;

				auto Visit = [&](int32 NeighbourCol, int32 NeighbourRow)
				{
					if (NeighbourRow < 0 || NeighbourRow >= NY)
					{
						return;
					}
					const int32 WrappedCol = ((NeighbourCol % NX) + NX) % NX;
					const int32 Index = NeighbourRow * NX + WrappedCol;

					if (Mask[Index] && OutLabels[Index] == 0)
					{
						OutLabels[Index] = Next;
						Stack.Add(Index);
					}
				};

				Visit(Col - 1, Row);
				Visit(Col + 1, Row);
				Visit(Col, Row - 1);
				Visit(Col, Row + 1);
			}
		}

		return Next;
	}
}
