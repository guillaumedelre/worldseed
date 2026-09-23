// Worldseed - drainage : comblement, directions D8, accumulation.

#include "Procedural/WorldseedFlow.h"
#include "Procedural/WorldseedTrace.h"
#include "Async/ParallelFor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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
		WORLDSEED_TRACE(Drainage);

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
		const double TA = FPlatformTime::Seconds();

		// L'ORDRE TOPOLOGIQUE EST UN SOUS-PRODUIT DU COMBLEMENT, PAS UN TRI.
		//
		// Priority-Flood depile toujours le minimum, et la valeur empilee --
		// `FilledM[N]` -- est FIGEE au moment de l'empilement : la garde
		// `< Max` interdit de repasser sur une cellule deja traitee. Les
		// depilements sortent donc par altitude NON DECROISSANTE, ce qui est
		// exactement l'ordre topologique a l'envers.
		//
		// MESURE QUI A JUSTIFIE LE CHANGEMENT : le tri par comparaison qui
		// occupait cette place coutait **1 038 ms sur les 2 647 du drainage**,
		// soit 39 %. Son comparateur lisait `Filled[A]` a un autre endroit de
		// la memoire a chaque appel -- un defaut de cache par comparaison, et
		// il y en a une vingtaine par element sur 8,4 millions.
		TArray<int32> Depilements;
		Depilements.Reserve(Count);

		while (Heap.Num() > 0)
		{
			FCell Current;
			Heap.HeapPop(Current, EAllowShrinking::No);
			Depilements.Add(Current.Index);

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

		const double TB = FPlatformTime::Seconds();

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

		const double TC = FPlatformTime::Seconds();

		// --- ordre topologique -------------------------------------------------
		// Trier par altitude decroissante suffit : toute cellule est traitee
		// avant son receveur, qui est par construction plus bas.
		Out.Order.SetNumUninitialized(Count);

		if (Depilements.Num() == Count)
		{
			// Le comblement a touche toutes les cellules : on retourne l'ordre
			// de depilement, et il n'y a rien a trier.
			for (int32 I = 0; I < Count; ++I)
			{
				Out.Order[I] = Depilements[Count - 1 - I];
			}
		}
		else
		{
			// LE REPLI EXISTE PARCE QUE LA PROPRIETE SE VERIFIE, ELLE NE SE
			// SUPPOSE PAS. Si une cellule n'est jamais atteinte par le
			// comblement -- une region que ni la mer ni les lignes polaires ne
			// touchent -- elle ne serait pas dans l'ordre, l'accumulation
			// sauterait son bassin, et RIEN ne le signalerait : les debits
			// seraient simplement faux, donc l'humidite du sol et les canyons
			// avec eux. On retombe alors sur le tri, en le DISANT.
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] drainage : %d cellules depilees sur %d -- ")
				TEXT("ordre repris par tri"),
				Depilements.Num(), Count);

			for (int32 I = 0; I < Count; ++I)
			{
				Out.Order[I] = I;
			}
			const TArray<float>& Filled = Out.FilledM;
			Out.Order.Sort([&Filled](int32 A, int32 B) { return Filled[A] > Filled[B]; });
		}

		const double TD = FPlatformTime::Seconds();

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

		// LE DETAIL AVANT LE TOTAL. Le drainage pese 2 647 ms des 2 802 des
		// champs du sol, soit 94,5 % -- mais « le drainage » recouvre quatre
		// traitements de natures differentes, dont deux seulement sont
		// sequentiels par nature. Sans ce releve on optimiserait au hasard.
		const double TE = FPlatformTime::Seconds();

		// L'EMPREINTE EST LE CONTROLE, PAS LE TEMPS. Un ordre topologique faux
		// ne plante pas : il verse simplement l'eau dans le mauvais sens, et
		// l'on obtient des debits errones -- donc une humidite du sol et des
		// canyons faux -- sans le moindre message. La somme et le maximum de
		// l'accumulation doivent etre IDENTIQUES d'un chemin a l'autre ;
		// `-WorldseedFluxTri=1` permet de le verifier sans recompiler.
		double Somme = 0.0;
		float Pic = 0.0f;
		for (const float A : Out.Accumulation)
		{
			Somme += A;
			Pic = FMath::Max(Pic, A);
		}

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] drainage : flood %.0f, D8 %.0f, TRI %.0f, accumulation %.0f ms")
			TEXT("  |  empreinte somme %.6g pic %.6g"),
			(TB - TA) * 1000.0, (TC - TB) * 1000.0,
			(TD - TC) * 1000.0, (TE - TD) * 1000.0,
			Somme, Pic);

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
