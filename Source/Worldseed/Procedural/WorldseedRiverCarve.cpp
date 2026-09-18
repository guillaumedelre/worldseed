// Worldseed - creusement du lit des cours d'eau dans le relief.

#include "Procedural/WorldseedRiverCarve.h"

#include "Procedural/WorldseedRivers.h"
#include "Procedural/WorldseedRules.h"

namespace
{
	/** Pente d'un segment, en degres, prise sur la surface d'eau. */
	float SegmentSlopeDeg(const FWorldseedRiver& River, int32 I, float MetresPerCell)
	{
		const float RunM =
			(River.PointsPx[I + 1] - River.PointsPx[I]).Size() * MetresPerCell;
		const float DropM = River.SurfaceM[I] - River.SurfaceM[I + 1];

		if (RunM <= KINDA_SMALL_NUMBER)
		{
			return (DropM > KINDA_SMALL_NUMBER) ? 90.0f : 0.0f;
		}
		return FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(DropM), RunM));
	}

	/** Un trace exploitable a des tableaux paralleles qui concordent. */
	bool IsUsable(const FWorldseedRiver& River)
	{
		const int32 Count = River.PointsPx.Num();
		return (Count >= 2) && (River.SurfaceM.Num() == Count)
			&& (River.WidthM.Num() == Count);
	}
}

namespace WorldseedRiverCarve
{
	void Apply(TArray<FWorldseedRiver>& Rivers, const TArray<bool>& LakeMask,
		const FWorldseedGeometry& Geometry, const FWorldseedCarveRules& Rules,
		TArray<float>& ElevationM)
	{
		const int32 NX = Geometry.NX;
		const int32 NY = Geometry.NY;
		if (NX < 2 || ElevationM.Num() != Geometry.CellCount())
		{
			return;
		}

		const double StartTime = FPlatformTime::Seconds();
		const float MetresPerCell = Geometry.MetersPerPixel();
		const bool bHasLakes = (LakeMask.Num() == ElevationM.Num());

		// --- le profil du lit, calcule une fois et MONOTONE ------------------
		//
		// Creuser point par point puis rattraper la decroissance apres coup ne
		// marche pas : le rattrapage est un minimum courant, et un creux en
		// amont tire toute la suite vers le bas. L'eau se retrouve alors sous
		// le sol des que le terrain remonte, et le cours se coupe.
		//
		// En imposant la monotonie AU PROFIL avant de creuser, le fond descend
		// par construction. Une surface posee dessus descend aussi, et ne peut
		// plus passer sous le sol du chenal.
		TArray<TArray<float>> Profiles;
		Profiles.SetNum(Rivers.Num());

		int32 Tapered = 0;

		for (int32 R = 0; R < Rivers.Num(); ++R)
		{
			const FWorldseedRiver& River = Rivers[R];
			if (!IsUsable(River))
			{
				continue;
			}

			const int32 Num = River.PointsPx.Num();
			TArray<float>& Profile = Profiles[R];
			Profile.SetNumUninitialized(Num);

			for (int32 I = 0; I < Num; ++I)
			{
				const float SlopeDeg = FMath::Max(
					(I + 1 < Num) ? SegmentSlopeDeg(River, I, MetresPerCell) : 0.0f,
					(I > 0) ? SegmentSlopeDeg(River, I - 1, MetresPerCell) : 0.0f);

				// UN RESSAUT S'ESTOMPE, IL NE SE SAUTE PAS. Pleine profondeur
				// jusqu'au seuil, nulle au double, fondu entre les deux : le
				// chenal se referme a l'approche d'une chute au lieu d'y ouvrir
				// un trou dans lequel le cours disparaitrait.
				const float Taper = 1.0f - FMath::SmoothStep(
					Rules.MaxSlopeDeg, Rules.MaxSlopeDeg * 2.0f, SlopeDeg);
				Tapered += (Taper < 0.99f) ? 1 : 0;

				// Le fond descend de la lame d'eau PLUS la berge : l'eau
				// remplit alors exactement ce qu'on creuse pour elle.
				const float DepthM = River.DepthM.IsValidIndex(I)
					? FMath::Clamp(River.DepthM[I], Rules.MinWaterDepthM,
						Rules.MaxWaterDepthM)
					: Rules.MinWaterDepthM;

				Profile[I] = River.SurfaceM[I]
					- (DepthM + Rules.FreeboardM) * Taper;
				if (I > 0)
				{
					Profile[I] = FMath::Min(Profile[I], Profile[I - 1]);
				}
			}
		}

		// --- le creusement, par passes successives ---------------------------
		int32 Stamped = 0;
		double SumCutM = 0.0;
		float DeepestCutM = 0.0f;

		for (int32 Pass = 0; Pass < FMath::Max(Rules.Passes, 1); ++Pass)
		{
			// LE MELANGE VISE LE RELIEF DE DEBUT DE PASSE, jamais celui qu'on
			// modifie. Les empreintes se recouvrent : viser le resultat du
			// voisin ferait cumuler le creusement, et le long d'une falaise il
			// rongerait le rebord.
			const TArray<float> Original = ElevationM;

			for (int32 R = 0; R < Rivers.Num(); ++R)
			{
				const FWorldseedRiver& River = Rivers[R];
				const TArray<float>& Profile = Profiles[R];
				if (!IsUsable(River) || Profile.Num() != River.PointsPx.Num())
				{
					continue;
				}

				const int32 Num = River.PointsPx.Num();

				// ON ESTAMPILLE LE SEGMENT, ET NON SES EXTREMITES.
				//
				// Les points du trace sont espaces d'une cinquantaine de metres
				// et le chenal fait vingt-quatre metres de rayon : deux disques
				// consecutifs ne se touchent pas. On creusait un CHAPELET DE
				// PERLES, avec du terrain intact entre elles, et l'eau s'y
				// interrompait. Mesurer la distance au SEGMENT revient a
				// densifier le trace a l'infini, sans en payer le cout.
				for (int32 I = 0; I + 1 < Num; ++I)
				{
					const FVector2D A = River.PointsPx[I];
					const FVector2D B = River.PointsPx[I + 1];
					const FVector2D AB = B - A;
					const float LengthSq = static_cast<float>(AB.SizeSquared());

					// LE CHENAL EST ETROIT, ET C'EST TOUT L'ENJEU. Hors de lui
					// le terrain reste intact, donc au-dessus de l'eau, et c'est
					// la que le cours s'arrete. Ce n'est pas la largeur du
					// creusement qui confine l'eau, c'est la hauteur du fond.
					const float HalfBedM = FMath::Max(
						FMath::Max(River.WidthM[I], River.WidthM[I + 1]) * 0.5f,
						MetresPerCell);
					const float ReachM = HalfBedM + Rules.MinBankM;
					const int32 Radius = FMath::CeilToInt(ReachM / MetresPerCell);

					const int32 Col0 = FMath::Clamp(
						FMath::FloorToInt(FMath::Min(A.X, B.X)) - Radius, 0, NX - 1);
					const int32 Col1 = FMath::Clamp(
						FMath::CeilToInt(FMath::Max(A.X, B.X)) + Radius, 0, NX - 1);
					const int32 Row0 = FMath::Clamp(
						FMath::FloorToInt(FMath::Min(A.Y, B.Y)) - Radius, 0, NY - 1);
					const int32 Row1 = FMath::Clamp(
						FMath::CeilToInt(FMath::Max(A.Y, B.Y)) + Radius, 0, NY - 1);

					for (int32 Row = Row0; Row <= Row1; ++Row)
					{
						for (int32 Col = Col0; Col <= Col1; ++Col)
						{
							const int32 Cell = Row * NX + Col;

							// Une nappe deja comblee ne se perce pas : le chenal
							// la viderait par le fond.
							if (bHasLakes && LakeMask[Cell])
							{
								continue;
							}

							const FVector2D C(static_cast<float>(Col),
								static_cast<float>(Row));

							// Projection sur le segment, bornee a ses bouts.
							const float T = (LengthSq > KINDA_SMALL_NUMBER)
								? FMath::Clamp(static_cast<float>(
									FVector2D::DotProduct(C - A, AB)) / LengthSq,
									0.0f, 1.0f)
								: 0.0f;

							const float DistM =
								FVector2D::Distance(C, A + AB * T) * MetresPerCell;
							if (DistM > ReachM)
							{
								continue;
							}

							// Le fond glisse le long du segment, sur le profil.
							const float BedM =
								FMath::Lerp(Profile[I], Profile[I + 1], T);

							// Profil en U : fond plat sur le lit, puis berge qui
							// rejoint le terrain au bord du chenal.
							float Target = BedM;
							if (DistM > HalfBedM)
							{
								const float U = (DistM - HalfBedM)
									/ FMath::Max(ReachM - HalfBedM, KINDA_SMALL_NUMBER);
								Target = FMath::Lerp(BedM, Original[Cell],
									FMath::SmoothStep(0.0f, 1.0f, U));
							}

							// ON NE FAIT QUE DESCENDRE : rejouer le creusement
							// ne change alors plus rien, et aucune passe ne peut
							// remonter le relief.
							if (Target < ElevationM[Cell])
							{
								const float CutM = ElevationM[Cell] - Target;
								SumCutM += CutM;
								DeepestCutM = FMath::Max(DeepestCutM, CutM);
								++Stamped;

								ElevationM[Cell] = Target;
							}
						}
					}
				}
			}
		}

		// --- la surface d'eau se pose sur le profil --------------------------
		//
		// Le profil est monotone et le sol du chenal y a ete descendu : une
		// surface posee dessus descend aussi, et ne peut pas passer sous le
		// sol. Plus besoin de rattraper la decroissance apres coup — c'est ce
		// rattrapage qui coupait le cours.
		//
		// LA HAUTEUR D'EAU N'EST PAS UNE CONSTANTE. Un ruisseau et un fleuve
		// ne portent pas la meme lame : l'hydrologie calcule deja DepthM depuis
		// le debit, et c'est elle qui doit se lire dans le chenal.
		double SumDepthM = 0.0;
		int32 Fitted = 0;

		for (int32 R = 0; R < Rivers.Num(); ++R)
		{
			FWorldseedRiver& River = Rivers[R];
			const TArray<float>& Profile = Profiles[R];
			if (!IsUsable(River) || Profile.Num() != River.PointsPx.Num())
			{
				continue;
			}

			for (int32 I = 0; I < River.SurfaceM.Num(); ++I)
			{
				const float DepthM = River.DepthM.IsValidIndex(I)
					? FMath::Clamp(River.DepthM[I], Rules.MinWaterDepthM,
						Rules.MaxWaterDepthM)
					: Rules.MinWaterDepthM;

				River.SurfaceM[I] = Profile[I] + DepthM;
				SumDepthM += DepthM;
				++Fitted;
			}
		}

		// DEUX PASSES QUI GARANTISSENT LES DEUX PROPRIETES A LA FOIS.
		//
		// Le profil suppose que chaque point a ete creuse. Ceux qu'on epargne —
		// les cellules de nappe, qu'un chenal viderait par le fond — gardent
		// donc un sol intact sous une eau calculee comme si on l'avait baisse.
		// Le minimum courant du profil les tire en plus vers le bas. Mesure sur
		// la graine 20260909 : 32 centres a sec, tous a exactement -1,5 m, soit
		// la hauteur de berge au chiffre pres.
		//
		// On impose donc les deux proprietes separement, et dans cet ordre :
		//
		//   1. L'eau ne passe JAMAIS sous le sol. On la remonte au besoin.
		//   2. L'amont n'est jamais plus bas que l'aval. On remonte encore, en
		//      remontant le cours — jamais en abaissant, sinon la premiere
		//      propriete serait perdue.
		//
		// Remonter l'amont est physiquement juste : une eau retenue derriere un
		// seuil forme une retenue, et c'est exactement ce que le relief comble
		// modelisait deja.
		for (int32 R = 0; R < Rivers.Num(); ++R)
		{
			FWorldseedRiver& River = Rivers[R];
			if (!IsUsable(River))
			{
				continue;
			}

			auto GroundAt = [&](const FVector2D& Px)
			{
				const int32 Col = FMath::Clamp(FMath::RoundToInt(Px.X), 0, NX - 1);
				const int32 Row = FMath::Clamp(FMath::RoundToInt(Px.Y), 0, NY - 1);
				return ElevationM[Row * NX + Col];
			};

			const int32 Num = River.PointsPx.Num();
			for (int32 I = 0; I < Num; ++I)
			{
				River.SurfaceM[I] = FMath::Max(River.SurfaceM[I],
					GroundAt(River.PointsPx[I]) + Rules.MinWaterDepthM);
			}
			for (int32 I = Num - 2; I >= 0; --I)
			{
				River.SurfaceM[I] =
					FMath::Max(River.SurfaceM[I], River.SurfaceM[I + 1]);
			}
		}

		// LA MESURE FINALE EST CELLE QUE LE CORPS D'EAU PORTERA : elle doit
		// decrire le chenal creuse, et non la vallee d'avant.
		WorldseedRivers::MeasureBasins(ElevationM, Geometry,
			Rules.MaxBasinWidthM, Rivers);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] lits creuses : %d cellules abaissees de %.2f m en moyenne ")
			TEXT("(%.1f m au plus), berge de %.1f m, lame de %.1f m en moyenne, ")
			TEXT("%d points adoucis par la pente  (%.0f ms)"),
			Stamped, (Stamped > 0) ? static_cast<float>(SumCutM / Stamped) : 0.0f,
			DeepestCutM, Rules.FreeboardM,
			(Fitted > 0) ? static_cast<float>(SumDepthM / Fitted) : 0.0f,
			Tapered, (FPlatformTime::Seconds() - StartTime) * 1000.0);
	}
}
