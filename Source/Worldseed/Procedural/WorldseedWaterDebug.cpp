// Worldseed - visualisation du reseau hydrographique.

#include "Procedural/WorldseedWaterDebug.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Procedural/WorldseedHydrology.h"
#include "Procedural/WorldseedLakes.h"
#include "Procedural/WorldseedRivers.h"
#include "Procedural/WorldseedRules.h"

namespace
{
	constexpr float MetersToCm = 100.0f;

	/** Epaisseurs, en pixels. Le trace doit se lire de loin. */
	constexpr float CourseThickness = 12.0f;
	constexpr float WidthThickness = 4.0f;
	constexpr float GapThickness = 6.0f;

	/** Une barre de largeur un point sur combien : au-dela c'est illisible. */
	constexpr int32 WidthEvery = 4;

}

namespace WorldseedWaterDebug
{
	void DrawRivers(UWorld* World, const FWorldseedGeometry& Geometry,
		const FWorldseedHydrology& Hydrology, const TArray<float>& ElevationM,
		float HeightExaggeration, const TArray<TArray<bool>>& Taken)
	{
		if (!World || Geometry.NX < 2 || ElevationM.Num() != Geometry.CellCount())
		{
			return;
		}

		// On efface le trace precedent : sans cela, deux generations se
		// superposent et l'on croit voir deux reseaux.
		FlushPersistentDebugLines(World);

		const int32 NX = Geometry.NX;
		const int32 NY = Geometry.NY;
		const float CellCm = Geometry.MetersPerPixel() * MetersToCm;
		const float OriginX = -Geometry.WidthM() * MetersToCm * 0.5f;
		const float OriginY = -Geometry.HeightM * MetersToCm * 0.5f;

		auto GroundM = [&](const FVector2D& P)
		{
			const int32 Col = FMath::Clamp(FMath::RoundToInt(P.X), 0, NX - 1);
			const int32 Row = FMath::Clamp(FMath::RoundToInt(P.Y), 0, NY - 1);
			return ElevationM[Row * NX + Col];
		};

		auto WorldAt = [&](const FVector2D& P, float AltitudeM)
		{
			return FVector(OriginX + P.X * CellCm, OriginY + P.Y * CellCm,
				AltitudeM * MetersToCm * HeightExaggeration);
		};

		// Le releve chiffre ce que le trace montre : de combien, en moyenne et
		// au pire, la surface d'eau passe au-dessus du terrain.
		double SumAboveM = 0.0;
		int32 CountAbove = 0;
		int32 CountPoints = 0;
		float WorstAboveM = 0.0f;
		int32 CountBuried = 0;
		float DeepestBuriedM = 0.0f;
		float SumWidthM = 0.0f;
		double SumBasinM = 0.0;
		int32 CountBasin = 0;
		float WidestBasinM = 0.0f;

		for (int32 R = 0; R < Hydrology.Rivers.Num(); ++R)
		{
			const FWorldseedRiver& River = Hydrology.Rivers[R];
			const int32 Num = River.PointsPx.Num();
			if (Num < 2 || River.SurfaceM.Num() != Num || River.WidthM.Num() != Num)
			{
				continue;
			}

			const TArray<bool>* const Mask = Taken.IsValidIndex(R) ? &Taken[R] : nullptr;

			for (int32 I = 0; I < Num; ++I)
			{
				const FVector2D& P = River.PointsPx[I];
				const float SurfaceM = River.SurfaceM[I];
				const float TerrainM = GroundM(P);
				const float AboveM = SurfaceM - TerrainM;

				++CountPoints;
				SumWidthM += River.WidthM[I];
				if (AboveM > 0.0f)
				{
					++CountAbove;
					SumAboveM += AboveM;
					WorstAboveM = FMath::Max(WorstAboveM, AboveM);
				}

				const FVector Surface = WorldAt(P, SurfaceM);
				const FVector Terrain = WorldAt(P, TerrainM);

				// --- l'ecart au terrain, la question qui decide -------------
				if (FMath::Abs(AboveM) > 0.05f)
				{
					DrawDebugLine(World, Terrain, Surface,
						(AboveM > 0.0f) ? FColor::Red : FColor::Green,
						true, -1.0f, 0, GapThickness);
				}

				// LA MEME MESURE QUE CELLE QUI PILOTE L'EAU, ET NON UNE COPIE.
				//
				// Ce trace avait sa propre sonde. Corriger l'une sans l'autre a
				// produit un instrument qui affichait une largeur pendant que le
				// corps d'eau en utilisait une autre : toute comparaison entre
				// la barre jaune et l'eau rendue devenait fausse. On lit
				// desormais la valeur que l'hydrologie a calculee, celle-la
				// meme que le plugin recoit.
				const float BasinM = River.BasinWidthM.IsValidIndex(I)
					? River.BasinWidthM[I] : 0.0f;

				if (BasinM > 0.0f && I > 0 && I + 1 < Num)
				{
					const FVector2D Along =
						(River.PointsPx[I + 1] - River.PointsPx[I - 1]).GetSafeNormal();
					const FVector2D Side(-Along.Y, Along.X);

					SumBasinM += BasinM;
					++CountBasin;
					WidestBasinM = FMath::Max(WidestBasinM, BasinM);

					// JAUNE : l'emprise que le corps d'eau porte reellement ici.
					const float HalfCm = BasinM * MetersToCm * 0.5f;
					const FVector Offset(Side.X * HalfCm, Side.Y * HalfCm, 0.0f);
					DrawDebugLine(World, Surface - Offset, Surface + Offset,
						FColor::Yellow, true, -1.0f, 0, WidthThickness);
				}


				// --- la largeur du lit --------------------------------------
				if ((I % WidthEvery) == 0 && I + 1 < Num)
				{
					const FVector2D Step = (River.PointsPx[I + 1] - P).GetSafeNormal();
					const FVector2D Side(-Step.Y, Step.X);
					const float HalfCm = River.WidthM[I] * MetersToCm * 0.5f;
					const FVector Offset(Side.X * HalfCm, Side.Y * HalfCm, 0.0f);

					DrawDebugLine(World, Surface - Offset, Surface + Offset,
						FColor::White, true, -1.0f, 0, WidthThickness);
				}

				// --- le cours lui-meme --------------------------------------
				if (I + 1 < Num)
				{
					const bool bTaken = Mask && Mask->IsValidIndex(I) && (*Mask)[I];
					DrawDebugLine(World, Surface,
						WorldAt(River.PointsPx[I + 1], River.SurfaceM[I + 1]),
						bTaken ? FColor::Cyan : FColor::Orange,
						true, -1.0f, 0, CourseThickness);
				}
			}
		}

		const float MeanAboveM = (CountAbove > 0)
			? static_cast<float>(SumAboveM / CountAbove) : 0.0f;
		const float MeanWidthM = (CountPoints > 0) ? SumWidthM / CountPoints : 0.0f;

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] trace du reseau : %d points, lit %.1f m en moyenne  |  ")
			TEXT("surface au-dessus du terrain sur %.0f %% des points, ")
			TEXT("de %.1f m en moyenne, %.1f m au pire"),
			CountPoints, MeanWidthM,
			(CountPoints > 0) ? 100.0f * CountAbove / CountPoints : 0.0f,
			MeanAboveM, WorstAboveM);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] lits enterres : %d points sur %d (%.0f %%), %.1f m au pire"),
			CountBuried, CountPoints,
			(CountPoints > 0) ? 100.0f * CountBuried / CountPoints : 0.0f,
			DeepestBuriedM);

	// LA COMPARAISON QUI DECIDE : largeur de cuvette contre largeur de lit.
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] cuvettes : %d points noyes, cuvette %.0f m de large en ")
		TEXT("moyenne (%.0f m au plus), pour un lit de %.1f m"),
		CountBasin,
		(CountBasin > 0) ? static_cast<float>(SumBasinM / CountBasin) : 0.0f,
		WidestBasinM, MeanWidthM);
	}
}

namespace WorldseedWaterDebug
{
	void DrawLakes(UWorld* World, const FWorldseedGeometry& Geometry,
		const FWorldseedHydrology& Hydrology, const TArray<float>& ElevationM,
		float HeightExaggeration)
	{
		if (!World || Geometry.NX < 2 || ElevationM.Num() != Geometry.CellCount())
		{
			return;
		}

		const int32 NX = Geometry.NX;
		const int32 NY = Geometry.NY;
		const float CellCm = Geometry.MetersPerPixel() * MetersToCm;
		const float OriginX = -Geometry.WidthM() * MetersToCm * 0.5f;
		const float OriginY = -Geometry.HeightM * MetersToCm * 0.5f;

		auto WorldAt = [&](const FVector2D& P, float AltitudeM)
		{
			return FVector(OriginX + P.X * CellCm, OriginY + P.Y * CellCm,
				AltitudeM * MetersToCm * HeightExaggeration);
		};

		for (int32 L = 0; L < Hydrology.Lakes.Num(); ++L)
		{
			const FWorldseedLake& Lake = Hydrology.Lakes[L];
			const int32 Num = Lake.OutlinePx.Num();
			if (Num < 3)
			{
				continue;
			}

			int32 Below = 0;
			float DeepestM = 0.0f;

			for (int32 I = 0; I < Num; ++I)
			{
				const FVector2D& P = Lake.OutlinePx[I];
				const int32 Col = FMath::Clamp(FMath::RoundToInt(P.X), 0, NX - 1);
				const int32 Row = FMath::Clamp(FMath::RoundToInt(P.Y), 0, NY - 1);
				const float TerrainM = ElevationM[Row * NX + Col];
				const float AboveM = Lake.SurfaceM - TerrainM;

				const FVector Surface = WorldAt(P, Lake.SurfaceM);

				// Le rivage attendu, par-dessus le rendu.
				DrawDebugLine(World, Surface, WorldAt(Lake.OutlinePx[(I + 1) % Num],
					Lake.SurfaceM), FColor::Magenta, true, -1.0f, 0, CourseThickness);

				// LA QUESTION QUE LA CAPTURE NE TRANCHE PAS : sous le rivage,
				// le terrain est-il au-dessus ou au-dessous de la surface ?
				// Au-dessus, la nappe s'arrete la. Au-dessous, elle continue,
				// et le rivage extrait n'est pas sa vraie limite.
				if (FMath::Abs(AboveM) > 0.05f)
				{
					DrawDebugLine(World, WorldAt(P, TerrainM), Surface,
						(AboveM > 0.0f) ? FColor::Red : FColor::Green,
						true, -1.0f, 0, GapThickness);
				}

				if (AboveM > 0.0f)
				{
					++Below;
					DeepestM = FMath::Max(DeepestM, AboveM);
				}
			}

			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] nappe %d : surface %.0f m, %d points de rivage, ")
				TEXT("terrain sous la surface sur %.0f %% d'entre eux, %.1f m au pire"),
				L, Lake.SurfaceM, Num, 100.0f * Below / Num, DeepestM);
		}
	}
}
