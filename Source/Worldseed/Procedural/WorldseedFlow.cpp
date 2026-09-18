// Worldseed - drainage : comblement, directions D8, accumulation.

#include "Procedural/WorldseedFlow.h"
#include "Async/ParallelFor.h"

namespace WorldseedFlow
{
	namespace
	{
		/** Huit voisins, avec la distance diagonale. */
		struct FNeighbour { int32 DI; int32 DJ; float Dist; };
		const FNeighbour Neighbours[8] = {
			{ -1, -1, 1.41421356f }, { 0, -1, 1.0f }, { 1, -1, 1.41421356f },
			{ -1,  0, 1.0f },                          { 1,  0, 1.0f },
			{ -1,  1, 1.41421356f }, { 0,  1, 1.0f }, { 1,  1, 1.41421356f },
		};

		/** Entree du tas : altitude comblee, puis index. */
		struct FCell
		{
			float Elevation;
			int32 Index;

			bool operator<(const FCell& Other) const
			{
				// TArray::HeapPop rend le PLUS PETIT selon cet operateur.
				return Elevation < Other.Elevation;
			}
		};

		/** Voisin (I, J) avec enroulement en longitude et bornage en latitude. */
		FORCEINLINE bool NeighbourIndex(int32 I, int32 J, const FNeighbour& N,
			int32 NX, int32 NY, int32& OutIndex)
		{
			const int32 NJ = J + N.DJ;
			if (NJ < 0 || NJ >= NY)
			{
				return false;   // au-dela d'un pole, il n'y a rien
			}
			const int32 NI = ((I + N.DI) % NX + NX) % NX;
			OutIndex = NJ * NX + NI;
			return true;
		}
	}

	void Compute(const TArray<float>& DemM, const TArray<float>& Weight,
		int32 NX, int32 NY, float SeaLevelM, float EpsilonM, FWorldseedFlow& Out)
	{
		const int32 Count = NX * NY;
		if (NX < 2 || NY < 2 || DemM.Num() != Count)
		{
			return;
		}

		const double StartTime = FPlatformTime::Seconds();

		// --- comblement Priority-Flood + epsilon -----------------------------
		Out.FilledM.Init(TNumericLimits<float>::Max(), Count);

		TArray<FCell> Heap;
		Heap.Reserve(Count / 4);

		auto Seed = [&](int32 Index)
		{
			if (Out.FilledM[Index] > DemM[Index])
			{
				Out.FilledM[Index] = DemM[Index];
				Heap.Add({ DemM[Index], Index });
			}
		};

		// Exutoires : la mer, et les deux lignes polaires. Sans ces dernieres,
		// un continent polaire sans littoral n'aurait aucune sortie et tout son
		// bassin serait comble jusqu'au bord.
		for (int32 I = 0; I < Count; ++I)
		{
			if (DemM[I] <= SeaLevelM)
			{
				Seed(I);
			}
		}
		for (int32 I = 0; I < NX; ++I)
		{
			Seed(I);
			Seed((NY - 1) * NX + I);
		}

		Heap.Heapify();

		while (Heap.Num() > 0)
		{
			FCell Current;
			Heap.HeapPop(Current, EAllowShrinking::No);

			const int32 CI = Current.Index % NX;
			const int32 CJ = Current.Index / NX;

			for (const FNeighbour& N : Neighbours)
			{
				int32 NIndex = 0;
				if (!NeighbourIndex(CI, CJ, N, NX, NY, NIndex))
				{
					continue;
				}
				if (Out.FilledM[NIndex] < TNumericLimits<float>::Max())
				{
					continue;   // deja traite
				}

				// L'epsilon garantit une pente strictement descendante : c'est
				// lui qui supprime les plats, ou D8 n'aurait aucun receveur.
				Out.FilledM[NIndex] = FMath::Max(DemM[NIndex], Current.Elevation + EpsilonM);
				Heap.HeapPush({ Out.FilledM[NIndex], NIndex });
			}
		}

		// --- directions D8 ----------------------------------------------------
		Out.Receivers.SetNumUninitialized(Count);

		ParallelFor(NY, [&](int32 J)
		{
			for (int32 I = 0; I < NX; ++I)
			{
				const int32 Index = J * NX + I;
				const float Here = Out.FilledM[Index];

				float BestDrop = 0.0f;
				int32 BestRec = Index;

				for (const FNeighbour& N : Neighbours)
				{
					int32 NIndex = 0;
					if (!NeighbourIndex(I, J, N, NX, NY, NIndex))
					{
						continue;
					}
					const float Drop = (Here - Out.FilledM[NIndex]) / N.Dist;
					if (Drop > BestDrop)
					{
						BestDrop = Drop;
						BestRec = NIndex;
					}
				}

				Out.Receivers[Index] = BestRec;
			}
		});

		// --- ordre topologique -------------------------------------------------
		// Trier par altitude decroissante suffit : toute cellule est traitee
		// avant son receveur, qui est par construction plus bas.
		Out.Order.SetNumUninitialized(Count);
		for (int32 I = 0; I < Count; ++I)
		{
			Out.Order[I] = I;
		}
		const TArray<float>& Filled = Out.FilledM;
		Out.Order.Sort([&Filled](int32 A, int32 B) { return Filled[A] > Filled[B]; });

		// --- accumulation -------------------------------------------------------
		Out.Accumulation.SetNumUninitialized(Count);
		const bool bHasWeight = (Weight.Num() == Count);
		for (int32 I = 0; I < Count; ++I)
		{
			Out.Accumulation[I] = bHasWeight ? Weight[I] : 1.0f;
		}

		// Sequentiel par nature : chaque cellule verse dans son receveur.
		for (const int32 Cell : Out.Order)
		{
			const int32 Rec = Out.Receivers[Cell];
			if (Rec != Cell)
			{
				Out.Accumulation[Rec] += Out.Accumulation[Cell];
			}
		}

		// Ce que le comblement a ajoute : matiere premiere des lacs.
		Out.LakeDepthM.SetNumUninitialized(Count);
		for (int32 I = 0; I < Count; ++I)
		{
			Out.LakeDepthM[I] = FMath::Max(Out.FilledM[I] - DemM[I], 0.0f);
		}

		UE_LOG(LogTemp, Verbose, TEXT("[Worldseed] drainage %dx%d en %.0f ms"),
			NX, NY, (FPlatformTime::Seconds() - StartTime) * 1000.0);
	}

	void SlopeToReceiver(const TArray<float>& DemM, int32 NX, int32 NY,
		float SpacingM, TArray<float>& OutSlope)
	{
		const int32 Count = NX * NY;
		OutSlope.SetNumZeroed(Count);
		if (NX < 2 || NY < 2 || DemM.Num() != Count || SpacingM <= 0.0f)
		{
			return;
		}

		ParallelFor(NY, [&](int32 J)
		{
			for (int32 I = 0; I < NX; ++I)
			{
				const int32 Index = J * NX + I;
				const float Here = DemM[Index];

				float Best = 0.0f;
				for (const FNeighbour& N : Neighbours)
				{
					int32 NIndex = 0;
					if (!NeighbourIndex(I, J, N, NX, NY, NIndex))
					{
						continue;
					}
					Best = FMath::Max(Best, (Here - DemM[NIndex]) / (N.Dist * SpacingM));
				}

				OutSlope[Index] = Best;
			}
		});
	}
}
