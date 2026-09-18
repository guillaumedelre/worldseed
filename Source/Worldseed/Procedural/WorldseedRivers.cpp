// Worldseed - extraction du reseau hydrographique en polylignes.

#include "Procedural/WorldseedRivers.h"

#include "Algo/Reverse.h"
#include "Procedural/WorldseedFlow.h"
#include "Procedural/WorldseedPolyline.h"
#include "Procedural/WorldseedStrahler.h"

namespace WorldseedRivers
{
	namespace
	{
		constexpr double SecondsPerYear = 365.25 * 24.0 * 3600.0;
		constexpr float MillimetresPerMetre = 1000.0f;

		/**
		 * Pas maximum suivis au-dela du dernier troncon pour trouver l'exutoire.
		 *
		 * GENEREUX A DESSEIN : la descente finale vers la mer traverse souvent
		 * une plaine cotiere inclinee au seul epsilon, ou le chemin serpente sur
		 * des centaines de cellules. Avec une borne trop courte, un cours d'eau
		 * se jetant dans la mer restait classe endoreique.
		 */
		constexpr int32 MaxMouthSteps = 8192;
	}

	const TCHAR* MouthName(EWorldseedMouth Mouth)
	{
		switch (Mouth)
		{
		case EWorldseedMouth::Ocean:     return TEXT("ocean");
		case EWorldseedMouth::Lake:      return TEXT("lac");
		case EWorldseedMouth::Border:    return TEXT("bord");
		case EWorldseedMouth::Endorheic: return TEXT("endoreique");
		default:                         return TEXT("?");
		}
	}

	void DischargeWeights(const TArray<float>& PrecipMm,
		const FWorldseedGeometry& Geometry, float RunoffCoefficient,
		TArray<float>& OutWeights)
	{
		const int32 Count = Geometry.CellCount();
		OutWeights.SetNumUninitialized(Count);

		const float MetresPerPixel = Geometry.MetersPerPixel();
		const double CellAreaM2 = static_cast<double>(MetresPerPixel) * MetresPerPixel;
		const float Scale = static_cast<float>(
			CellAreaM2 * RunoffCoefficient / SecondsPerYear);

		const bool bHasPrecip = (PrecipMm.Num() == Count);

		for (int32 I = 0; I < Count; ++I)
		{
			const float MetresPerYear =
				(bHasPrecip ? PrecipMm[I] : 700.0f) / MillimetresPerMetre;
			OutWeights[I] = MetresPerYear * Scale;
		}
	}

	void Extract(const FWorldseedFlow& Flow, const TArray<float>& ElevationM,
		const TArray<bool>& LakeMask, const FWorldseedGeometry& Geometry,
		const FWorldseedHydrologyRules& Rules, TArray<FWorldseedRiver>& OutRivers)
	{
		OutRivers.Reset();

		const int32 NX = Geometry.NX;
		const int32 NY = Geometry.NY;
		const int32 Count = Geometry.CellCount();
		if (Flow.Accumulation.Num() != Count || Flow.Receivers.Num() != Count
			|| ElevationM.Num() != Count)
		{
			return;
		}

		// --- chenaux -------------------------------------------------------------
		TArray<bool> IsChannel;
		IsChannel.Init(false, Count);
		bool bAnyChannel = false;
		for (int32 I = 0; I < Count; ++I)
		{
			if (Flow.Accumulation[I] >= Rules.RiverDischargeThreshold && ElevationM[I] > 0.0f)
			{
				IsChannel[I] = true;
				bAnyChannel = true;
			}
		}
		if (!bAnyChannel)
		{
			return;
		}

		TArray<int32> Orders;
		WorldseedStrahler::Orders(Flow.Receivers, Flow.Order, IsChannel, Orders);

		TArray<bool> Keep;
		Keep.Init(false, Count);
		bool bAnyKept = false;
		for (int32 I = 0; I < Count; ++I)
		{
			if (IsChannel[I] && Orders[I] >= Rules.MinStrahlerOrder)
			{
				Keep[I] = true;
				bAnyKept = true;
			}
		}
		if (!bAnyKept)
		{
			// Reseau trop maigre pour le seuil demande : on retombe sur les
			// chenaux bruts plutot que de ne rien produire du tout.
			Keep = IsChannel;
		}

		// --- embouchures ---------------------------------------------------------
		// Une embouchure est un troncon dont le receveur n'est plus un troncon,
		// ou qui se recoit lui-meme (puits).
		TArray<int32> Outlets;
		for (int32 I = 0; I < Count; ++I)
		{
			if (!Keep[I])
			{
				continue;
			}
			const int32 Rec = Flow.Receivers[I];
			if (Rec == I || !Keep[Rec])
			{
				Outlets.Add(I);
			}
		}

		// Les plus gros debits d'abord : si le quota tranche, il garde les
		// fleuves et non les ruisseaux.
		Outlets.Sort([&Flow](int32 A, int32 B)
		{
			return Flow.Accumulation[A] > Flow.Accumulation[B];
		});

		TArray<int32> Donors;
		WorldseedStrahler::BestDonors(Flow.Receivers, Flow.Accumulation, Keep, Donors);

		const float MetresPerPixel = Geometry.MetersPerPixel();
		const float TolerancePx = Rules.SimplifyToleranceM / FMath::Max(MetresPerPixel, 1e-3f);
		const float MaxSpacingPx = Rules.MaxPointSpacingM / FMath::Max(MetresPerPixel, 1e-3f);
		const bool bHasLakes = (LakeMask.Num() == Count);

		TArray<bool> Visited;
		Visited.Init(false, Count);

		TArray<int32> Path;

		for (const int32 Outlet : Outlets)
		{
			if (OutRivers.Num() >= Rules.MaxRiverActors)
			{
				break;
			}
			if (Visited[Outlet])
			{
				continue;
			}

			// --- remontee du cours principal : embouchure vers source ------------
			Path.Reset();
			int32 Cell = Outlet;
			while (Cell >= 0 && !Visited[Cell])
			{
				Path.Add(Cell);
				Visited[Cell] = true;
				Cell = Donors[Cell];
			}

			if (Path.Num() < 3)
			{
				continue;
			}

			Algo::Reverse(Path);

			// Prolonge d'une cellule pour que le trace touche vraiment l'eau.
			const int32 Tail = Flow.Receivers[Path.Last()];
			if (Tail != Path.Last() && !Keep[Tail])
			{
				Path.Add(Tail);
			}

			TArray<FVector2D> Points;
			Points.Reserve(Path.Num());
			for (const int32 Index : Path)
			{
				Points.Emplace(static_cast<float>(Index % NX), static_cast<float>(Index / NX));
			}

			const float LengthM = WorldseedPolyline::Length(Points) * MetresPerPixel;
			if (LengthM < Rules.MinRiverLengthM)
			{
				continue;
			}

			// Decimation a PAS FIXE et non par tolerance croissante : Simplify
			// vient de reimposer un point tous les MaxPointSpacing metres pour
			// garder le cours au fond de la vallee, et elargir Douglas-Peucker
			// supprimerait en priorite ces points-la.
			TArray<FVector2D> Simplified =
				WorldseedPolyline::Simplify(Points, TolerancePx, MaxSpacingPx);
			Simplified = WorldseedPolyline::Decimate(Simplified, Rules.MaxPointsPerRiver);

			// --- geometrie hydraulique -------------------------------------------
			FWorldseedRiver River;
			River.PointsPx = Simplified;
			River.LengthM = LengthM;
			River.StrahlerOrder = Orders[Outlet];

			River.DischargeM3s.Reserve(Simplified.Num());
			River.WidthM.Reserve(Simplified.Num());
			River.DepthM.Reserve(Simplified.Num());
			River.SurfaceM.Reserve(Simplified.Num());

			for (const FVector2D& P : Simplified)
			{
				const int32 Col = FMath::Clamp(FMath::RoundToInt(P.X), 0, NX - 1);
				const int32 Row = FMath::Clamp(FMath::RoundToInt(P.Y), 0, NY - 1);
				const int32 Sample = Row * NX + Col;
				const float Q = Flow.Accumulation[Sample];

				River.SurfaceM.Add(Flow.FilledM[Sample]);

				// L'exageration porte sur le DEBIT APPARENT, jamais sur la
				// largeur seule : largeur et profondeur gardent ainsi leur
				// proportion physique l'une par rapport a l'autre. Le debit
				// conserve, lui, reste le debit VRAI.
				const float Apparent = Q * Rules.GeometryExaggeration;

				River.DischargeM3s.Add(Q);
				River.WidthM.Add(FMath::Clamp(
					Rules.WidthCoefA * FMath::Pow(Apparent, Rules.WidthExponent),
					Rules.MinRiverWidthM, Rules.MaxRiverWidthM));
				River.DepthM.Add(FMath::Max(Rules.MinRiverDepthM,
					Rules.DepthCoefB * FMath::Pow(Apparent, Rules.DepthExponent)));
			}

			// --- classement de l'embouchure --------------------------------------
			// ON SUIT L'ECOULEMENT au-dela du dernier troncon, au lieu de juger
			// la derniere cellule de terre. Sans cela, un cours qui se jette
			// dans la mer mais dont le dernier pixel terrestre est a 0,75 m
			// d'altitude n'etait ni ocean, ni lac, ni bord : il se retrouvait
			// declare endoreique par elimination.
			int32 End = Path.Last();
			for (int32 Step = 0; Step < MaxMouthSteps; ++Step)
			{
				if (ElevationM[End] <= 0.0f)
				{
					break;
				}
				const int32 Next = Flow.Receivers[End];
				if (Next == End)
				{
					break;   // puits : cuvette reellement fermee
				}
				End = Next;
			}

			const int32 EndRow = End / NX;
			const bool bOnBorder = (EndRow == 0) || (EndRow == NY - 1);

			if (ElevationM[End] <= 0.0f)
			{
				River.Mouth = EWorldseedMouth::Ocean;
			}
			else if (bHasLakes && LakeMask[End])
			{
				River.Mouth = EWorldseedMouth::Lake;
			}
			else if (bOnBorder)
			{
				River.Mouth = EWorldseedMouth::Border;
			}
			else
			{
				River.Mouth = EWorldseedMouth::Endorheic;
			}

			OutRivers.Add(MoveTemp(River));
		}
	}
}

namespace WorldseedRivers
{
	void MeasureBasins(const TArray<float>& ElevationM,
		const FWorldseedGeometry& Geometry, float MaxWidthM,
		TArray<FWorldseedRiver>& Rivers)
	{
		const int32 NX = Geometry.NX;
		const int32 NY = Geometry.NY;
		if (NX < 2 || ElevationM.Num() != Geometry.CellCount())
		{
			return;
		}

		const float MetresPerCell = Geometry.MetersPerPixel();
		// DEUX CELLULES SUFFISENT A CONFIRMER UNE BERGE.
		//
		// Trois etait trop exigeant : mesure sur la graine 20260909, coupe de
		// la riviere 8, la sonde trouvait deux cellules seches a -23 et -31 m,
		// ne les tenait pas pour une berge, traversait, et allait mesurer un
		// creux voisin a soixante metres. Elle annoncait alors une cuvette de
		// 328 m pour un chenal de 39, et le corps d eau posait sa nappe dessus.
		//
		// Deux cellules font seize metres : c est deja un relief, pas un
		// caillou du detail fractal.
		constexpr int32 BankConfirmCells = 2;
		constexpr int32 BankMarginCells = 2;

		// Portee du maximum glissant le long du cours, en points.
		constexpr int32 BasinSpreadPoints = 2;

		int32 Probed = 0;
		int32 Saturated = 0;

		const int32 MaxProbe = FMath::Max(
			FMath::RoundToInt(MaxWidthM * 0.5f / MetresPerCell), 1);

		auto GroundAt = [&](const FVector2D& P)
		{
			const int32 Col = FMath::Clamp(FMath::RoundToInt(P.X), 0, NX - 1);
			const int32 Row = FMath::Clamp(FMath::RoundToInt(P.Y), 0, NY - 1);
			return ElevationM[Row * NX + Col];
		};

		for (FWorldseedRiver& River : Rivers)
		{
			const int32 Num = River.PointsPx.Num();
			River.BasinWidthM.Init(0.0f, Num);
			if (Num < 3 || River.SurfaceM.Num() != Num)
			{
				continue;
			}

			for (int32 I = 1; I + 1 < Num; ++I)
			{
				const FVector2D& P = River.PointsPx[I];
				const float SurfaceM = River.SurfaceM[I];

				// Sous le relief : pas de cuvette, le lit suffit.
				if (SurfaceM <= GroundAt(P))
				{
					continue;
				}

				const FVector2D Along =
					(River.PointsPx[I + 1] - River.PointsPx[I - 1]).GetSafeNormal();
				const FVector2D Side(-Along.Y, Along.X);

				// UNE BOSSE N'EST PAS UNE BERGE.
				//
				// S'arreter a la premiere cellule qui atteint le niveau de
				// l'eau, c'est prendre le premier caillou du detail fractal
				// pour le bord de la cuvette — et le mur d'eau revient, deux
				// metres plus loin qu'avant. Une berge se CONFIRME : il faut
				// plusieurs cellules de suite au-dessus du niveau.
				//
				// Et l'on peut se permettre d'etre genereux : l'eau n'est
				// dessinee que la ou elle depasse le sol, donc une emprise trop
				// large ne produit rien de visible. C'est le meme raisonnement
				// que l'enveloppe des lacs.
				bool bHitCap = false;
				auto Reach = [&](float Sign)
				{
					int32 Confirmed = 0;
					int32 Last = 0;
					int32 Steps = 0;
					for (; Steps < MaxProbe; ++Steps)
					{
						if (GroundAt(P + Side * (Sign * (Steps + 1))) >= SurfaceM)
						{
							if (++Confirmed >= BankConfirmCells)
							{
								break;
							}
						}
						else
						{
							Confirmed = 0;
							Last = Steps + 1;
						}
					}

					// LE PLAFOND DOIT SE SIGNALER, PAS TRANCHER EN SILENCE.
					// Une constante en metres marche sur une graine et casse sur
					// la suivante ; le seul moyen de le savoir est de compter
					// combien de points l'atteignent, graine par graine.
					bHitCap = bHitCap || (Steps >= MaxProbe);
					return FMath::Min(Last + BankMarginCells, MaxProbe);
				};

				River.BasinWidthM[I] = (Reach(-1.0f) + Reach(1.0f)) * MetresPerCell;
				++Probed;
				Saturated += bHitCap ? 1 : 0;
			}


			// L'EMPRISE NE DOIT PAS S'EFFONDRER ENTRE DEUX POINTS.
			//
			// Une mare ne s'arrete pas net entre deux echantillons distants de
			// cinquante metres. Sans cela l'emprise alterne entre le lit et la
			// cuvette d'un point a l'autre, et l'eau se dessine en PLAQUES
			// DISJOINTES le long du cours au lieu d'un plan continu.
			//
			// Un maximum glissant, et non une moyenne : il garantit que la
			// partie large couvre toute la mare, la ou une moyenne la
			// retrecirait a ses deux bouts — exactement la ou le mur d'eau
			// reapparait.
			if (Num >= 3)
			{
				TArray<float> Spread = River.BasinWidthM;
				for (int32 I = 0; I < Num; ++I)
				{
					float Widest = 0.0f;
					for (int32 K = -BasinSpreadPoints; K <= BasinSpreadPoints; ++K)
					{
						if (River.BasinWidthM.IsValidIndex(I + K))
						{
							Widest = FMath::Max(Widest, River.BasinWidthM[I + K]);
						}
					}
					Spread[I] = Widest;
				}
				River.BasinWidthM = MoveTemp(Spread);
			}
			// Les extremites heritent de leur voisin : sonder sur un seul
			// voisin donnerait une direction fausse, et un elargissement de
			// travers se verrait a l'embouchure.
			if (Num >= 3)
			{
				River.BasinWidthM[0] = River.BasinWidthM[1];
				River.BasinWidthM[Num - 1] = River.BasinWidthM[Num - 2];
			}
		}

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] cuvettes sondees : %d points, %d a la butee de %.0f m (%.0f %%)"),
			Probed, Saturated, MaxWidthM,
			(Probed > 0) ? 100.0f * Saturated / Probed : 0.0f);
	}
}
