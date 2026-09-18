// Worldseed - operations de grille equivalentes a celles de scipy.ndimage.

#include "Procedural/WorldseedGrid.h"
#include "Async/ParallelFor.h"

#include <algorithm>

namespace WorldseedGrid
{
	namespace
	{
		/** Transformee de distance 1D au carre, enveloppe inferieure de paraboles. */
		void DistanceTransform1D(const float* F, float* D, int32 Count, int32 Stride,
			TArray<int32>& V, TArray<float>& Z)
		{
			V.SetNumUninitialized(Count);
			Z.SetNumUninitialized(Count + 1);

			auto At = [F, Stride](int32 Q) -> float { return F[Q * Stride]; };

			int32 K = 0;
			V[0] = 0;
			Z[0] = -BIG_NUMBER;
			Z[1] = BIG_NUMBER;

			for (int32 Q = 1; Q < Count; ++Q)
			{
				float S = ((At(Q) + static_cast<float>(Q * Q))
					- (At(V[K]) + static_cast<float>(V[K] * V[K])))
					/ static_cast<float>(2 * Q - 2 * V[K]);

				while (K > 0 && S <= Z[K])
				{
					--K;
					S = ((At(Q) + static_cast<float>(Q * Q))
						- (At(V[K]) + static_cast<float>(V[K] * V[K])))
						/ static_cast<float>(2 * Q - 2 * V[K]);
				}

				++K;
				V[K] = Q;
				Z[K] = S;
				Z[K + 1] = BIG_NUMBER;
			}

			K = 0;
			for (int32 Q = 0; Q < Count; ++Q)
			{
				while (Z[K + 1] < static_cast<float>(Q))
				{
					++K;
				}
				const float Delta = static_cast<float>(Q - V[K]);
				D[Q] = Delta * Delta + At(V[K]);
			}
		}

		/**
		 * FLOU PAR BOITES ITEREES.
		 *
		 * Trois moyennes glissantes successives convergent vers une gaussienne —
		 * c est le theoreme central limite applique au noyau. L interet est le
		 * COUT CONSTANT : une somme glissante ne touche chaque valeur que deux
		 * fois, quel que soit sigma, la ou la convolution directe en lit 2*4*sigma.
		 *
		 * Mesure qui a motive le changement : a 2000x1000 avec sigma 55,6 px, le
		 * noyau direct faisait 445 valeurs par cellule, et les 400 passes
		 * d advection coutaient 57 secondes PAR passe de climat. Ici on tombe a
		 * douze operations par cellule.
		 *
		 * Largeurs choisies par la methode de Kovesi, qui repartit les trois
		 * boites entre deux largeurs voisines pour coller au sigma demande.
		 */
		void BoxSizesForGaussian(float Sigma, int32 Passes, TArray<int32>& OutRadii)
		{
			OutRadii.SetNumUninitialized(Passes);

			const float Variance = 12.0f * Sigma * Sigma;
			const float WIdeal = FMath::Sqrt(Variance / Passes + 1.0f);

			int32 WL = FMath::FloorToInt32(WIdeal);
			if (WL % 2 == 0) { --WL; }
			WL = FMath::Max(WL, 1);
			const int32 WU = WL + 2;

			const float Denom = -4.0f * WL - 4.0f;
			const float MIdeal = (Variance - Passes * WL * WL
				- 4.0f * Passes * WL - 3.0f * Passes) / (FMath::Abs(Denom) > KINDA_SMALL_NUMBER ? Denom : -4.0f);
			const int32 M = FMath::Clamp(FMath::RoundToInt32(MIdeal), 0, Passes);

			for (int32 I = 0; I < Passes; ++I)
			{
				OutRadii[I] = ((I < M) ? WL : WU) / 2;
			}
		}

		/** Une moyenne glissante horizontale, bords en mode "nearest". */
		void BoxPassX(const TArray<float>& Src, TArray<float>& Dst,
			int32 NX, int32 NY, int32 R)
		{
			const float Inv = 1.0f / static_cast<float>(2 * R + 1);
			ParallelFor(NY, [&](int32 Y)
			{
				const int32 Row = Y * NX;

				// La fenetre de la premiere cellule deborde a gauche : le mode
				// "nearest" revient a repeter la valeur de bord.
				float Acc = Src[Row] * static_cast<float>(R + 1);
				for (int32 K = 1; K <= R; ++K)
				{
					Acc += Src[Row + FMath::Min(K, NX - 1)];
				}

				for (int32 X = 0; X < NX; ++X)
				{
					Dst[Row + X] = Acc * Inv;
					Acc += Src[Row + FMath::Min(X + R + 1, NX - 1)]
						 - Src[Row + FMath::Max(X - R, 0)];
				}
			});
		}

		/** Meme chose sur l axe des lignes. */
		void BoxPassY(const TArray<float>& Src, TArray<float>& Dst,
			int32 NX, int32 NY, int32 R)
		{
			const float Inv = 1.0f / static_cast<float>(2 * R + 1);
			ParallelFor(NX, [&](int32 X)
			{
				float Acc = Src[X] * static_cast<float>(R + 1);
				for (int32 K = 1; K <= R; ++K)
				{
					Acc += Src[FMath::Min(K, NY - 1) * NX + X];
				}

				for (int32 Y = 0; Y < NY; ++Y)
				{
					Dst[Y * NX + X] = Acc * Inv;
					Acc += Src[FMath::Min(Y + R + 1, NY - 1) * NX + X]
						 - Src[FMath::Max(Y - R, 0) * NX + X];
				}
			});
		}

		/** Applique les trois boites sur un axe. */
		void BlurAxis(TArray<float>& InOut, int32 NX, int32 NY, float Sigma, bool bAlongRows)
		{
			if (Sigma <= 0.0f)
			{
				return;
			}

			TArray<int32> Radii;
			BoxSizesForGaussian(Sigma, 3, Radii);

			TArray<float> Temp;
			Temp.SetNumUninitialized(NX * NY);

			for (const int32 R : Radii)
			{
				if (R <= 0)
				{
					continue;
				}
				if (bAlongRows)
				{
					BoxPassY(InOut, Temp, NX, NY, R);
				}
				else
				{
					BoxPassX(InOut, Temp, NX, NY, R);
				}
				InOut = Temp;
			}
		}
	}

	void GaussianFilter(TArray<float>& InOut, int32 NX, int32 NY, float SigmaPixels)
	{
		if (SigmaPixels <= 0.0f || NX < 2 || NY < 2 || InOut.Num() != NX * NY)
		{
			return;
		}

		BlurAxis(InOut, NX, NY, SigmaPixels, false);
		BlurAxis(InOut, NX, NY, SigmaPixels, true);
	}

	void GaussianFilter1D(TArray<float>& InOut, int32 NX, int32 NY,
		float SigmaPixels, bool bAlongRows)
	{
		if (SigmaPixels <= 0.0f || NX < 2 || NY < 2 || InOut.Num() != NX * NY)
		{
			return;
		}

		BlurAxis(InOut, NX, NY, SigmaPixels, bAlongRows);
	}

	void DistanceTransform(const TArray<uint8>& Mask, int32 NX, int32 NY,
		TArray<float>& OutDistancePixels)
	{
		OutDistancePixels.SetNumUninitialized(NX * NY);
		if (NX < 2 || NY < 2 || Mask.Num() != NX * NY)
		{
			return;
		}

		TArray<float> Squared;
		Squared.SetNumUninitialized(NX * NY);
		for (int32 I = 0; I < NX * NY; ++I)
		{
			Squared[I] = Mask[I] ? BIG_NUMBER : 0.0f;
		}

		// Passe sur les colonnes (pas NX entre deux lignes), puis sur les lignes.
		ParallelFor(NX, [&](int32 X)
		{
			TArray<float> Result;
			TArray<int32> V;
			TArray<float> Z;
			Result.SetNumUninitialized(NY);

			DistanceTransform1D(&Squared[X], Result.GetData(), NY, NX, V, Z);
			for (int32 Y = 0; Y < NY; ++Y)
			{
				Squared[Y * NX + X] = Result[Y];
			}
		});

		ParallelFor(NY, [&](int32 Y)
		{
			TArray<float> Result;
			TArray<int32> V;
			TArray<float> Z;
			Result.SetNumUninitialized(NX);

			DistanceTransform1D(&Squared[Y * NX], Result.GetData(), NX, 1, V, Z);
			for (int32 X = 0; X < NX; ++X)
			{
				OutDistancePixels[Y * NX + X] = FMath::Sqrt(FMath::Max(0.0f, Result[X]));
			}
		});
	}

	void Gradient(const TArray<float>& Field, int32 NX, int32 NY, float SpacingM,
		TArray<float>& OutDY, TArray<float>& OutDX)
	{
		OutDY.SetNumUninitialized(NX * NY);
		OutDX.SetNumUninitialized(NX * NY);
		if (NX < 2 || NY < 2 || Field.Num() != NX * NY)
		{
			return;
		}

		const float Inv2 = 1.0f / (2.0f * SpacingM);
		const float Inv1 = 1.0f / SpacingM;

		ParallelFor(NY, [&](int32 Y)
		{
			for (int32 X = 0; X < NX; ++X)
			{
				const int32 Index = Y * NX + X;

				// np.gradient : centrees a l'interieur, unilaterales au bord.
				if (Y == 0)
				{
					OutDY[Index] = (Field[(Y + 1) * NX + X] - Field[Index]) * Inv1;
				}
				else if (Y == NY - 1)
				{
					OutDY[Index] = (Field[Index] - Field[(Y - 1) * NX + X]) * Inv1;
				}
				else
				{
					OutDY[Index] = (Field[(Y + 1) * NX + X] - Field[(Y - 1) * NX + X]) * Inv2;
				}

				if (X == 0)
				{
					OutDX[Index] = (Field[Index + 1] - Field[Index]) * Inv1;
				}
				else if (X == NX - 1)
				{
					OutDX[Index] = (Field[Index] - Field[Index - 1]) * Inv1;
				}
				else
				{
					OutDX[Index] = (Field[Index + 1] - Field[Index - 1]) * Inv2;
				}
			}
		});
	}

	float SampleBilinearClamped(const TArray<float>& Field, int32 NX, int32 NY,
		float PosJ, float PosI)
	{
		const float CJ = FMath::Clamp(PosJ, 0.0f, static_cast<float>(NY - 1));
		const float CI = FMath::Clamp(PosI, 0.0f, static_cast<float>(NX - 1));

		const int32 J0 = FMath::Clamp(static_cast<int32>(CJ), 0, NY - 2);
		const int32 I0 = FMath::Clamp(static_cast<int32>(CI), 0, NX - 2);
		const float TJ = CJ - static_cast<float>(J0);
		const float TI = CI - static_cast<float>(I0);

		const float V00 = Field[J0 * NX + I0];
		const float V01 = Field[J0 * NX + I0 + 1];
		const float V10 = Field[(J0 + 1) * NX + I0];
		const float V11 = Field[(J0 + 1) * NX + I0 + 1];

		return V00 * (1 - TI) * (1 - TJ)
			 + V01 * TI * (1 - TJ)
			 + V10 * (1 - TI) * TJ
			 + V11 * TI * TJ;
	}

	float SampleUV(const TArray<float>& Field, int32 NX, int32 NY, float U, float V)
	{
		if (NX < 2 || NY < 2 || Field.Num() != NX * NY)
		{
			return 0.0f;
		}

		// U s'enroule : la carte fait le tour de la sphere, la derniere colonne
		// est voisine de la premiere. Sans cela, une couture noire apparaitrait
		// sur le globe au meridien d'origine.
		const float WrappedU = U - FMath::FloorToFloat(U);
		const float FX = WrappedU * static_cast<float>(NX);
		const int32 I0 = FMath::Clamp(static_cast<int32>(FX), 0, NX - 1);
		const int32 I1 = (I0 + 1) % NX;
		const float TI = FX - static_cast<float>(I0);

		// V se borne : un pole n'a pas de voisin au-dela.
		const float FY = FMath::Clamp(V, 0.0f, 1.0f) * static_cast<float>(NY - 1);
		const int32 J0 = FMath::Clamp(static_cast<int32>(FY), 0, NY - 2);
		const int32 J1 = J0 + 1;
		const float TJ = FY - static_cast<float>(J0);

		const float V00 = Field[J0 * NX + I0];
		const float V01 = Field[J0 * NX + I1];
		const float V10 = Field[J1 * NX + I0];
		const float V11 = Field[J1 * NX + I1];

		return V00 * (1 - TI) * (1 - TJ)
			 + V01 * TI * (1 - TJ)
			 + V10 * (1 - TI) * TJ
			 + V11 * TI * TJ;
	}

	float Quantile(const TArray<float>& Values, float Q)
	{
		if (Values.Num() == 0)
		{
			return 0.0f;
		}

		// SELECTION PARTIELLE plutot que tri complet. On n a besoin que des deux
		// valeurs qui encadrent la position cherchee, pas de l ordre total :
		// nth_element les place en O(n) la ou un tri coute O(n log n). L erosion
		// appelle cette fonction 120 fois par generation, sur des millions de
		// valeurs — la difference se compte en secondes.
		TArray<float> Work = Values;
		float* Data = Work.GetData();
		const int32 Num = Work.Num();

		const double Position = FMath::Clamp(static_cast<double>(Q), 0.0, 1.0)
			* static_cast<double>(Num - 1);
		const int32 Low = FMath::FloorToInt32(Position);
		const int32 High = FMath::Min(Low + 1, Num - 1);
		const double T = Position - static_cast<double>(Low);

		std::nth_element(Data, Data + Low, Data + Num);
		const float LowValue = Data[Low];

		float HighValue = LowValue;
		if (High > Low)
		{
			// Le partitionnement precedent garantit que tout ce qui suit Low lui
			// est superieur ou egal : la seconde selection ne travaille que sur
			// cette moitie.
			std::nth_element(Data + Low + 1, Data + High, Data + Num);
			HighValue = Data[High];
		}

		return static_cast<float>(LowValue + T * (HighValue - LowValue));
	}

	void Downsample(const TArray<float>& Source, int32 SrcNX, int32 SrcNY,
		int32 DstNX, int32 DstNY, TArray<float>& Out)
	{
		if (SrcNX < 1 || SrcNY < 1 || Source.Num() != SrcNX * SrcNY
			|| DstNX >= SrcNX || DstNY >= SrcNY || DstNX < 1 || DstNY < 1)
		{
			Out = Source;
			return;
		}

		Out.SetNumUninitialized(DstNX * DstNY);

		ParallelFor(DstNY, [&](int32 DstRow)
		{
			const int32 Row0 = DstRow * SrcNY / DstNY;
			const int32 Row1 = FMath::Max((DstRow + 1) * SrcNY / DstNY, Row0 + 1);

			for (int32 DstCol = 0; DstCol < DstNX; ++DstCol)
			{
				const int32 Col0 = DstCol * SrcNX / DstNX;
				const int32 Col1 = FMath::Max((DstCol + 1) * SrcNX / DstNX, Col0 + 1);

				double Sum = 0.0;
				for (int32 Row = Row0; Row < Row1; ++Row)
				{
					const int32 Base = Row * SrcNX;
					for (int32 Col = Col0; Col < Col1; ++Col)
					{
						Sum += Source[Base + Col];
					}
				}

				const int32 Samples = (Row1 - Row0) * (Col1 - Col0);
				Out[DstRow * DstNX + DstCol] = static_cast<float>(Sum / Samples);
			}
		});
	}

}
