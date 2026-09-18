// Worldseed - hierarchie du reseau hydrographique.

#include "Procedural/WorldseedStrahler.h"

#include "Algo/StableSort.h"

namespace WorldseedStrahler
{
	void BestDonors(const TArray<int32>& Receivers, const TArray<float>& Accumulation,
		const TArray<bool>& Valid, TArray<int32>& OutDonors)
	{
		const int32 Count = Receivers.Num();
		OutDonors.Init(-1, Count);

		if (Accumulation.Num() != Count || Valid.Num() != Count)
		{
			return;
		}

		TArray<int32> Cells;
		Cells.Reserve(Count / 8 + 8);
		for (int32 I = 0; I < Count; ++I)
		{
			if (Valid[I] && Receivers[I] != I)
			{
				Cells.Add(I);
			}
		}

		// ASTUCE DU PORTAGE : on ecrit les donneurs par debit CROISSANT, donc la
		// DERNIERE ecriture sur une cellule est son plus gros affluent. Un tri
		// stable garantit que deux debits egaux gardent un ordre reproductible.
		Algo::StableSort(Cells, [&Accumulation](int32 A, int32 B)
		{
			return Accumulation[A] < Accumulation[B];
		});

		for (const int32 Cell : Cells)
		{
			OutDonors[Receivers[Cell]] = Cell;
		}
	}

	void Orders(const TArray<int32>& Receivers, const TArray<int32>& Order,
		const TArray<bool>& IsChannel, TArray<int32>& OutOrders)
	{
		const int32 Count = Receivers.Num();
		OutOrders.Init(1, Count);

		if (IsChannel.Num() != Count)
		{
			return;
		}

		TArray<int32> Second;
		Second.Init(0, Count);

		// Amont vers aval : quand on atteint une cellule, tous ses affluents ont
		// deja livre leur rang.
		for (const int32 Cell : Order)
		{
			if (!IsChannel[Cell])
			{
				continue;
			}

			const int32 Rec = Receivers[Cell];
			if (Rec == Cell || !IsChannel[Rec])
			{
				continue;
			}

			const int32 Rank = OutOrders[Cell];
			if (Rank > OutOrders[Rec])
			{
				Second[Rec] = OutOrders[Rec];
				OutOrders[Rec] = Rank;
			}
			else if (Rank > Second[Rec])
			{
				Second[Rec] = Rank;
			}
		}

		// Le rang monte d'un cran la ou DEUX branches de meme rang confluent :
		// c'est toute la definition de Strahler.
		for (int32 I = 0; I < Count; ++I)
		{
			if (Second[I] >= OutOrders[I] && Second[I] > 0)
			{
				OutOrders[I] += 1;
			}
		}
	}
}
