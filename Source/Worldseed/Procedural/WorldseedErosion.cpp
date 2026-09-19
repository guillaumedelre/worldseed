// Worldseed - portage de Tools/WorldGen/worldgen/erosion.py.

#include "Procedural/WorldseedErosion.h"
#include "Procedural/WorldseedFlow.h"
#include "Procedural/WorldseedGrid.h"

#include "Async/ParallelFor.h"

namespace WorldseedErosion
{
	namespace
	{
		const TCHAR* ERO = TEXT("erosion");

		struct FNeighbour { int32 DI; int32 DJ; float Dist; };
		const FNeighbour Neighbours[8] = {
			{ -1, -1, 1.41421356f }, { 0, -1, 1.0f }, { 1, -1, 1.41421356f },
			{ -1,  0, 1.0f },                          { 1,  0, 1.0f },
			{ -1,  1, 1.41421356f }, { 0,  1, 1.0f }, { 1,  1, 1.41421356f },
		};

		FORCEINLINE bool NeighbourIndex(int32 I, int32 J, const FNeighbour& N,
			int32 NX, int32 NY, int32& OutIndex)
		{
			const int32 NJ = J + N.DJ;
			if (NJ < 0 || NJ >= NY)
			{
				return false;
			}
			const int32 NI = ((I + N.DI) % NX + NX) % NX;
			OutIndex = NJ * NX + NI;
			return true;
		}

		/** Au-dela de l'angle de talus, la matiere glisse vers les voisins bas. */
		void ThermalErosion(TArray<float>& Dem, int32 NX, int32 NY,
			float TalusAngleDeg, float SpacingM, int32 Iterations)
		{
			if (Iterations <= 0)
			{
				return;
			}

			const float MaxDrop = FMath::Tan(FMath::DegreesToRadians(TalusAngleDeg)) * SpacingM;
			const int32 Count = NX * NY;

			// FORMULATION EN COLLECTE, ET NON EN DEPOT.
			//
			// Ecrire chez ses voisins impose une passe sequentielle : deux
			// cellules adjacentes viseraient la meme case. En demandant plutot a
			// chaque cellule ce qu elle DONNE et ce qu elle RECOIT, tout se lit
			// dans un tableau fige et s ecrit chez soi : aucune course, et la
			// passe se parallelise telle quelle.
			//
			// Le resultat est identique au bit pres : ce que A donne a B dans un
			// sens est exactement ce que B recoit de A dans l autre.
			TArray<float> Source;

			for (int32 Iter = 0; Iter < Iterations; ++Iter)
			{
				Source = Dem;

				ParallelFor(NY, [&](int32 J)
				{
					for (int32 I = 0; I < NX; ++I)
					{
						const int32 Index = J * NX + I;
						const float Here = Source[Index];
						float Net = 0.0f;

						for (const FNeighbour& N : Neighbours)
						{
							int32 NIndex = 0;
							if (!NeighbourIndex(I, J, N, NX, NY, NIndex))
							{
								continue;
							}

							const float There = Source[NIndex];
							const float Threshold = MaxDrop * N.Dist;

							// 1/8 des voisins puis moitie : le facteur du Python,
							// qui borne le deplacement et garde la passe stable.
							const float Given = FMath::Max(Here - There - Threshold, 0.0f)
								* (0.125f * 0.5f);
							const float Received = FMath::Max(There - Here - Threshold, 0.0f)
								* (0.125f * 0.5f);

							Net += Received - Given;
						}

						Dem[Index] = Here + Net;
					}
				});
			}
		}

		/** Creep : lissage isotrope leger, qui arrondit les interfluves. */
		void HillslopeDiffusion(TArray<float>& Dem, int32 NX, int32 NY,
			float Kappa, int32 Iterations)
		{
			if (Iterations <= 0 || Kappa <= 0.0f)
			{
				return;
			}

			const int32 Count = NX * NY;
			const float K = Kappa / 6.0f;

			// Noyau laplacien du Python : diagonales a 0,5, orthogonaux a 1,
			// centre a -6.
			TArray<float> Source;
			for (int32 Iter = 0; Iter < Iterations; ++Iter)
			{
				Source = Dem;
				ParallelFor(NY, [&](int32 J)
				{
					for (int32 I = 0; I < NX; ++I)
					{
						const int32 Index = J * NX + I;
						float Lap = -6.0f * Source[Index];

						for (const FNeighbour& N : Neighbours)
						{
							int32 NIndex = 0;
							if (!NeighbourIndex(I, J, N, NX, NY, NIndex))
							{
								NIndex = Index;   // mode "nearest" au pole
							}
							Lap += (N.Dist > 1.0f ? 0.5f : 1.0f) * Source[NIndex];
						}

						Dem[Index] = Source[Index] + K * Lap;
					}
				});
			}
		}
	}

	bool Run(const UWorldseedRules& Rules, const FWorldseedGeometry& Geo,
		const TArray<float>& PrecipMm, const TArray<float>& Erodibility,
		const TArray<float>& UpliftM,
		TArray<float>& Dem,
		FWorldseedErosionReport& OutReport, const FWorldseedProgressScope& Progress)
	{
		const int32 NX = Geo.NX;
		const int32 NY = Geo.NY;
		const int32 Count = NX * NY;
		if (Dem.Num() != Count)
		{
			return false;
		}

		// Tableau vide : comportement d'avant, a l'identique. C'est ce qui
		// permet de revenir en arriere sans toucher au code.
		const bool bAvecRoche = (Erodibility.Num() == Count);

		// SATURER N'EST PAS ANODIN : la ou le plafond mord, la loi cesse de
		// s'appliquer et tout terme qu'elle porte -- la roche comprise -- est
		// efface. Sans ce compte au journal, on regle un coefficient qui ne
		// sert a rien sur la moitie du monde sans jamais le savoir.
		int64 SaturesTotal = 0;
		int32 PassesComptees = 0;

		const bool bAvecSoulevement = (UpliftM.Num() == Count);
		double SoulevementTotal = 0.0;

		const double StartTime = FPlatformTime::Seconds();
		const float Spacing = Geo.MetersPerPixel();
		const float CellArea = Spacing * Spacing;
		constexpr float SeaLevel = 0.0f;

		const int32 Iterations = Rules.Int(ERO, TEXT("iterations"), 60);
		const int32 Every = FMath::Max(Rules.Int(ERO, TEXT("flowUpdateEvery"), 5), 1);
		const float M = static_cast<float>(Rules.Num(ERO, TEXT("areaExponent"), 0.5));
		const float NExp = static_cast<float>(Rules.Num(ERO, TEXT("slopeExponent"), 1.0));
		const float Rate = static_cast<float>(Rules.Num(ERO, TEXT("incisionRateM"), 0.25));
		const float MaxStep = static_cast<float>(Rules.Num(ERO, TEXT("maxIncisionPerStepM"), 1.5));
		const float Deposition = static_cast<float>(Rules.Num(ERO, TEXT("depositionRate"), 0.25));
		const float Talus = static_cast<float>(Rules.Num(ERO, TEXT("talusAngleDeg"), 34.0));
		const int32 ThermalIters = Rules.Int(ERO, TEXT("thermalIterationsPerStep"), 1);
		const float Kappa = static_cast<float>(Rules.Num(ERO, TEXT("hillslopeDiffusion"), 0.06));
		const int32 HillslopeIters = Rules.Int(ERO, TEXT("hillslopeIterations"), 2);

		// --- poids de pluie ---------------------------------------------------
		// 1,0 pour une cellule de pluviometrie mediane sur les terres emergees.
		// C'est CE terme qui relie le climat a la forme du relief : un bassin
		// humide se creuse en vallees, un bassin aride reste en plateau.
		TArray<float> RainWeight;
		RainWeight.SetNumUninitialized(Count);
		{
			const bool bHasPrecip = (PrecipMm.Num() == Count);

			float MedianP = 1.0f;
			if (bHasPrecip)
			{
				TArray<float> LandPrecip;
				LandPrecip.Reserve(Count / 4);
				for (int32 I = 0; I < Count; ++I)
				{
					if (Dem[I] > SeaLevel)
					{
						LandPrecip.Add(PrecipMm[I]);
					}
				}
				if (LandPrecip.Num() > 0)
				{
					MedianP = WorldseedGrid::Quantile(LandPrecip, 0.5f);
				}
			}

			const float InvMedian = 1.0f / FMath::Max(MedianP, 1e-6f);
			for (int32 I = 0; I < Count; ++I)
			{
				const float P = bHasPrecip ? PrecipMm[I] : MedianP;
				RainWeight[I] = FMath::Max(P * InvMedian, 0.02f) * CellArea;
			}
		}

		TArray<float> AreaNorm;
		TArray<float> Slope;
		TArray<float> Incision;
		AreaNorm.SetNumZeroed(Count);
		Incision.SetNumUninitialized(Count);

		FWorldseedFlow Flow;
		OutReport = FWorldseedErosionReport();
		OutReport.Iterations = Iterations;

		for (int32 It = 0; It < Iterations; ++It)
		{
			if (Progress.Step(static_cast<float>(It) / FMath::Max(Iterations, 1)))
			{
				return false;
			}

			if (It % Every == 0)
			{
				WorldseedFlow::Compute(Dem, RainWeight, NX, NY, SeaLevel, 1e-4f, Flow);
				++OutReport.FlowUpdates;

				// Normalisation par centile : c'est elle qui rend le resultat
				// independant de la resolution.
				const float AccRef = FMath::Max(
					WorldseedGrid::Quantile(Flow.Accumulation, 0.999f), 1e-9f);
				for (int32 I = 0; I < Count; ++I)
				{
					AreaNorm[I] = FMath::Clamp(Flow.Accumulation[I] / AccRef, 0.0f, 4.0f);
				}
			}

			WorldseedFlow::SlopeToReceiver(Dem, NX, NY, Spacing, Slope);

			float SlopeRef = 1.0f;
			{
				TArray<float> LandSlope;
				LandSlope.Reserve(Count / 4);
				for (int32 I = 0; I < Count; ++I)
				{
					if (Dem[I] > SeaLevel)
					{
						LandSlope.Add(Slope[I]);
					}
				}
				if (LandSlope.Num() > 0)
				{
					SlopeRef = FMath::Max(WorldseedGrid::Quantile(LandSlope, 0.90f), 1e-9f);
				}
			}

			// --- SOULEVEMENT, AVANT L'INCISION -------------------------------
			//
			// L'ordre compte : la matiere doit etre la avant qu'on l'arrache.
			// Soulever apres reviendrait a eroder le relief de la passe
			// precedente puis a le remonter, ce qui n'est pas la meme chose.
			if (bAvecSoulevement)
			{
				for (int32 I = 0; I < Count; ++I)
				{
					Dem[I] += UpliftM[I];
					SoulevementTotal += UpliftM[I];
				}
			}

			// --- incision par puissance de courant ---------------------------
			double ErodedTotal = 0.0;
			float MaxIncisionThisStep = 0.0f;
			int32 Satures = 0;
			for (int32 I = 0; I < Count; ++I)
			{
				if (Dem[I] <= SeaLevel)
				{
					Incision[I] = 0.0f;
					continue;
				}
				const float SlopeNorm = Slope[I] / SlopeRef;

				// LA ROCHE ENTRE ICI, ET NULLE PART AILLEURS. Dans la loi de
				// puissance de courant `E = K . A^m . S^n`, c'est K qui porte
				// la resistance du substrat : un granite s'use moins vite
				// qu'un schiste sous le meme debit et la meme pente. C'est de
				// cette difference que naissent les escarpements, les
				// corniches et les marges raides de plateau. Le mettre
				// ailleurs -- dans l'exposant, ou en post-traitement --
				// reviendrait a bricoler un resultat au lieu de decrire une
				// cause.
				const float K = bAvecRoche ? Erodibility[I] : 1.0f;

				// LE PLAFOND SUIT LA ROCHE, LUI AUSSI. Premiere version : un
				// plafond FIXE. Or il sature precisement sur les cellules
				// raides et bien drainees -- celles qui font le relief -- et la
				// ou il sature, l'erodabilite est purement et simplement
				// effacee. Mesure : le terme etait branche au bon endroit et ne
				// deplacait que quatre a sept CENTIMETRES sur une erosion
				// entiere. Un garde-fou numerique ne doit pas manger la
				// physique qu'il protege : il se met a l'echelle avec elle.
				const float Brut = Rate * K
					* FMath::Pow(AreaNorm[I], M) * FMath::Pow(SlopeNorm, NExp);
				const float Plafond = MaxStep * K;
				if (Brut > Plafond) { ++Satures; }

				const float Value = FMath::Clamp(Brut, 0.0f, Plafond);

				Incision[I] = Value;
				ErodedTotal += Value;
				MaxIncisionThisStep = FMath::Max(MaxIncisionThisStep, Value);
			}

			for (int32 I = 0; I < Count; ++I)
			{
				Dem[I] -= Incision[I];
			}
			SaturesTotal += Satures;
			PassesComptees += 1;

			// --- depot ---------------------------------------------------------
			// La matiere arrachee se redepose dans les fonds de vallee, la ou la
			// pente est faible et le drainage important : cones alluviaux, plaines.
			if (Deposition > 0.0f && ErodedTotal > 0.0)
			{
				double TotalWeight = 0.0;
				TArray<float> DepositWeight;
				DepositWeight.SetNumUninitialized(Count);

				for (int32 I = 0; I < Count; ++I)
				{
					if (Dem[I] <= SeaLevel)
					{
						DepositWeight[I] = 0.0f;
						continue;
					}
					const float SlopeNorm = Slope[I] / SlopeRef;
					DepositWeight[I] = FMath::Clamp(1.0f - SlopeNorm, 0.0f, 1.0f) * AreaNorm[I];
					TotalWeight += DepositWeight[I];
				}

				if (TotalWeight > 1e-9)
				{
					const float Scale = static_cast<float>(
						ErodedTotal * Deposition / TotalWeight);
					for (int32 I = 0; I < Count; ++I)
					{
						const float D = DepositWeight[I] * Scale;
						Dem[I] += D;
						OutReport.TotalDepositionM += D;
					}
				}
			}

			ThermalErosion(Dem, NX, NY, Talus, Spacing, ThermalIters);

			OutReport.TotalIncisionM += static_cast<float>(ErodedTotal);
			OutReport.MaxIncisionM = FMath::Max(OutReport.MaxIncisionM, MaxIncisionThisStep);
		}

		if (bAvecSoulevement)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] erosion : soulevement cumule %.1f m en moyenne ")
				TEXT("sur %d passes"),
				SoulevementTotal / FMath::Max(Count, 1), Iterations);
		}

		if (PassesComptees > 0)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] erosion : %.2f %% des cellules saturent le plafond ")
				TEXT("par passe (%lld sur %lld)"),
				100.0 * SaturesTotal / FMath::Max<int64>(int64(PassesComptees) * Count, 1),
				SaturesTotal, int64(PassesComptees) * Count);
		}

		HillslopeDiffusion(Dem, NX, NY, Kappa, HillslopeIters);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] erosion %dx%d  %d passes  %d recalculs de drainage  incision max %.1f m  (%.0f ms)"),
			NX, NY, Iterations, OutReport.FlowUpdates, OutReport.MaxIncisionM,
			(FPlatformTime::Seconds() - StartTime) * 1000.0);

		Progress.Step(1.0f);
		return true;
	}
}
