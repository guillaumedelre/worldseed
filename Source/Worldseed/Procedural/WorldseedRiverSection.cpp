// Worldseed - coupe transversale d'un cours d'eau, sur demande.

#include "Procedural/WorldseedRiverSection.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Procedural/WorldseedHydrology.h"
#include "Procedural/WorldseedRivers.h"
#include "Procedural/WorldseedRules.h"

namespace
{
	constexpr float MetersToCm = 100.0f;

	/** Portee de la marche du rayon, en metres. */
	constexpr float MaxTraceM = 8000.0f;

	/** Demi-largeur de la coupe, en metres. */
	constexpr float SectionHalfM = 60.0f;

	/** Pas de la coupe, en cellules. Un pas plus fin n'aurait pas de donnee. */
	constexpr float SectionStepCells = 1.0f;
}

namespace WorldseedRiverSection
{
	bool TraceGround(const TArray<float>& ElevationM,
		const FWorldseedGeometry& Geometry, float HeightExaggeration,
		const FVector& From, const FVector& Direction, FVector& OutHit)
	{
		const int32 NX = Geometry.NX;
		const int32 NY = Geometry.NY;
		if (NX < 2 || ElevationM.Num() != Geometry.CellCount())
		{
			return false;
		}

		const float CellCm = Geometry.MetersPerPixel() * MetersToCm;
		const float OriginX = -Geometry.WidthM() * MetersToCm * 0.5f;
		const float OriginY = -Geometry.HeightM * MetersToCm * 0.5f;

		auto GroundCm = [&](const FVector& P)
		{
			const int32 Col = FMath::Clamp(
				FMath::RoundToInt((P.X - OriginX) / CellCm), 0, NX - 1);
			const int32 Row = FMath::Clamp(
				FMath::RoundToInt((P.Y - OriginY) / CellCm), 0, NY - 1);
			return ElevationM[Row * NX + Col] * MetersToCm * HeightExaggeration;
		};

		// UN PAS DE LA TAILLE D'UNE CELLULE : plus fin ne lirait pas de donnee
		// nouvelle, plus grossier sauterait une crete.
		const FVector Step = Direction.GetSafeNormal() * CellCm;
		const int32 MaxSteps = FMath::CeilToInt(MaxTraceM * MetersToCm / CellCm);

		FVector Here = From;
		bool bWasAbove = (Here.Z > GroundCm(Here));

		for (int32 I = 0; I < MaxSteps; ++I)
		{
			const FVector Next = Here + Step;
			const bool bAbove = (Next.Z > GroundCm(Next));

			// La traversee se fait entre ces deux pas : on la raffine par
			// dichotomie plutot que de rendre le pas entier.
			if (bWasAbove && !bAbove)
			{
				FVector Lo = Here;
				FVector Hi = Next;
				for (int32 K = 0; K < 12; ++K)
				{
					const FVector Mid = (Lo + Hi) * 0.5;
					if (Mid.Z > GroundCm(Mid)) { Lo = Mid; } else { Hi = Mid; }
				}
				OutHit = Hi;
				return true;
			}

			Here = Next;
			bWasAbove = bAbove;
		}

		return false;
	}

	void Probe(UWorld* World, const TArray<float>& ElevationM,
		const FWorldseedGeometry& Geometry, const FWorldseedHydrology& Hydrology,
		float HeightExaggeration, const FVector& WorldPoint)
	{
		const int32 NX = Geometry.NX;
		const int32 NY = Geometry.NY;
		if (!World || NX < 2 || ElevationM.Num() != Geometry.CellCount())
		{
			return;
		}

		const float MetresPerCell = Geometry.MetersPerPixel();
		const float CellCm = MetresPerCell * MetersToCm;
		const float OriginX = -Geometry.WidthM() * MetersToCm * 0.5f;
		const float OriginY = -Geometry.HeightM * MetersToCm * 0.5f;

		const FVector2D TargetPx(
			(WorldPoint.X - OriginX) / CellCm, (WorldPoint.Y - OriginY) / CellCm);

		// --- le cours le plus proche, et le point le plus proche dessus ------
		int32 BestRiver = INDEX_NONE;
		int32 BestPoint = INDEX_NONE;
		float BestDistCells = TNumericLimits<float>::Max();

		for (int32 R = 0; R < Hydrology.Rivers.Num(); ++R)
		{
			const FWorldseedRiver& River = Hydrology.Rivers[R];
			for (int32 I = 0; I < River.PointsPx.Num(); ++I)
			{
				const float D = FVector2D::Distance(River.PointsPx[I], TargetPx);
				if (D < BestDistCells)
				{
					BestDistCells = D;
					BestRiver = R;
					BestPoint = I;
				}
			}
		}

		if (BestRiver == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] coupe : aucun cours d'eau dans ce monde"));
			return;
		}

		const FWorldseedRiver& River = Hydrology.Rivers[BestRiver];
		const int32 Num = River.PointsPx.Num();
		const int32 I = BestPoint;

		const FVector2D Centre = River.PointsPx[I];
		const FVector2D Prev = River.PointsPx[FMath::Max(I - 1, 0)];
		const FVector2D Next = River.PointsPx[FMath::Min(I + 1, Num - 1)];
		const FVector2D Along = (Next - Prev).GetSafeNormal();
		const FVector2D Side(-Along.Y, Along.X);

		const float SurfaceM = River.SurfaceM.IsValidIndex(I) ? River.SurfaceM[I] : 0.0f;
		const float WidthM = River.WidthM.IsValidIndex(I) ? River.WidthM[I] : 0.0f;
		const float BasinM = River.BasinWidthM.IsValidIndex(I)
			? River.BasinWidthM[I] : 0.0f;

		auto GroundM = [&](const FVector2D& Px)
		{
			const int32 Col = FMath::Clamp(FMath::RoundToInt(Px.X), 0, NX - 1);
			const int32 Row = FMath::Clamp(FMath::RoundToInt(Px.Y), 0, NY - 1);
			return ElevationM[Row * NX + Col];
		};


		// LA LAME REELLEMENT POSEE, et non la profondeur hydraulique brute.
		//
		// Afficher l'une pour l'autre a deja fait chercher un defaut la ou il
		// n'y en avait pas : le releve annoncait 0,6 m alors que le plancher en
		// posait 1,0. Un instrument qui ne dit pas ce que le monde fait n'est
		// pas un instrument.
		const float DepthM = SurfaceM - GroundM(Centre);
		auto WorldAt = [&](const FVector2D& Px, float AltitudeM)
		{
			return FVector(OriginX + Px.X * CellCm, OriginY + Px.Y * CellCm,
				AltitudeM * MetersToCm * HeightExaggeration);
		};

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] coupe en (%.0f, %.0f) | riviere %d, point %d/%d, ")
			TEXT("a %.0f m du trace"),
			WorldPoint.X, WorldPoint.Y, BestRiver, I, Num - 1,
			BestDistCells * MetresPerCell);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   eau %.1f m  lit %.1f m de large  lame %.1f m  ")
			TEXT("cuvette %.0f m"),
			SurfaceM, WidthM, DepthM, BasinM);

		// --- la coupe, cellule par cellule ----------------------------------
		const int32 Steps = FMath::CeilToInt(SectionHalfM / MetresPerCell);
		FString Profil;
		int32 Wet = 0;
		float LowestM = TNumericLimits<float>::Max();
		float FirstDryM = 0.0f;
		bool bFoundDry = false;

		for (int32 K = -Steps; K <= Steps; ++K)
		{
			const FVector2D Px = Centre + Side * (K * SectionStepCells);
			const float OffsetM = K * SectionStepCells * MetresPerCell;
			const float TerrainM = GroundM(Px);
			const bool bWet = (TerrainM < SurfaceM);

			LowestM = FMath::Min(LowestM, TerrainM);
			Wet += bWet ? 1 : 0;

			// LE PREMIER POINT SEC EN PARTANT DU CENTRE : c'est la berge, et
			// c'est elle qui dit si le chenal contient le cours.
			if (!bWet && !bFoundDry && K >= 0)
			{
				FirstDryM = OffsetM;
				bFoundDry = true;
			}

			Profil += FString::Printf(TEXT("%+.0fm %.1f  "), OffsetM, TerrainM);

			// Le relief, et la lame d'eau par-dessus quand il y en a.
			DrawDebugLine(World, WorldAt(Px, TerrainM - 2.0f), WorldAt(Px, TerrainM),
				FColor(120, 120, 120), true, -1.0f, 0, 6.0f);
			if (bWet)
			{
				DrawDebugLine(World, WorldAt(Px, TerrainM), WorldAt(Px, SurfaceM),
					FColor::Blue, true, -1.0f, 0, 8.0f);
			}
			else
			{
				DrawDebugLine(World, WorldAt(Px, SurfaceM), WorldAt(Px, TerrainM),
					FColor::Red, true, -1.0f, 0, 8.0f);
			}
		}

		UE_LOG(LogTemp, Log, TEXT("[Worldseed]   %s"), *Profil);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   VERDICT : %d cellules mouillees sur %d (%.0f m de large), ")
			TEXT("fond a %.1f m soit %.1f m sous l'eau, berge a %+.0f m"),
			Wet, 2 * Steps + 1, Wet * MetresPerCell,
			LowestM, SurfaceM - LowestM,
			bFoundDry ? FirstDryM : SectionHalfM);
	}
}

namespace WorldseedRiverSection
{
	void Sweep(const TArray<float>& ElevationM, const FWorldseedGeometry& Geometry,
		const FWorldseedHydrology& Hydrology)
	{
		const int32 NX = Geometry.NX;
		const int32 NY = Geometry.NY;
		if (NX < 2 || ElevationM.Num() != Geometry.CellCount())
		{
			return;
		}

		const double StartTime = FPlatformTime::Seconds();
		const float MetresPerCell = Geometry.MetersPerPixel();
		const float CellCm = MetresPerCell * MetersToCm;
		const float OriginX = -Geometry.WidthM() * MetersToCm * 0.5f;
		const float OriginY = -Geometry.HeightM * MetersToCm * 0.5f;
		const int32 Steps = FMath::CeilToInt(SectionHalfM / MetresPerCell);

		auto GroundM = [&](const FVector2D& Px)
		{
			const int32 Col = FMath::Clamp(FMath::RoundToInt(Px.X), 0, NX - 1);
			const int32 Row = FMath::Clamp(FMath::RoundToInt(Px.Y), 0, NY - 1);
			return ElevationM[Row * NX + Col];
		};

		/** Un point du reseau, resume par ce qui decide s'il va bien. */
		struct FSection
		{
			int32 River = 0;
			int32 Point = 0;
			FVector2D Px = FVector2D::ZeroVector;
			float LameM = 0.0f;
			float WetM = 0.0f;
			bool bDryCentre = false;
		};

		TArray<FSection> Sections;
		int32 DryCentres = 0;
		double SumLameM = 0.0;
		double SumWetM = 0.0;
		float MinLameM = TNumericLimits<float>::Max();
		float MaxWetM = 0.0f;

		for (int32 R = 0; R < Hydrology.Rivers.Num(); ++R)
		{
			const FWorldseedRiver& River = Hydrology.Rivers[R];
			const int32 Num = River.PointsPx.Num();
			if (Num < 3 || River.SurfaceM.Num() != Num)
			{
				continue;
			}

			for (int32 I = 1; I + 1 < Num; ++I)
			{
				const FVector2D Centre = River.PointsPx[I];
				const FVector2D Along =
					(River.PointsPx[I + 1] - River.PointsPx[I - 1]).GetSafeNormal();
				const FVector2D Side(-Along.Y, Along.X);
				const float SurfaceM = River.SurfaceM[I];

				FSection S;
				S.River = R;
				S.Point = I;
				S.Px = Centre;
				S.LameM = SurfaceM - GroundM(Centre);
				S.bDryCentre = (S.LameM <= 0.0f);

				int32 Wet = 0;
				for (int32 K = -Steps; K <= Steps; ++K)
				{
					Wet += (GroundM(Centre + Side * K) < SurfaceM) ? 1 : 0;
				}
				S.WetM = Wet * MetresPerCell;

				DryCentres += S.bDryCentre ? 1 : 0;
				SumLameM += S.LameM;
				SumWetM += S.WetM;
				MinLameM = FMath::Min(MinLameM, S.LameM);
				MaxWetM = FMath::Max(MaxWetM, S.WetM);

				Sections.Add(S);
			}
		}

		if (Sections.Num() == 0)
		{
			return;
		}

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] balayage : %d coupes  |  lame %.1f m en moyenne ")
			TEXT("(%.1f m au minimum)  |  mouille %.0f m en moyenne (%.0f m au plus)  |  ")
			TEXT("%d centres a sec  (%.0f ms)"),
			Sections.Num(),
			static_cast<float>(SumLameM / Sections.Num()), MinLameM,
			static_cast<float>(SumWetM / Sections.Num()), MaxWetM,
			DryCentres, (FPlatformTime::Seconds() - StartTime) * 1000.0);

		// LES PIRES CAS SONT NOMMES, AVEC LEURS COORDONNEES. Une moyenne ne
		// designe aucun endroit ou aller voir ; ces lignes-la, si.
		auto Report = [&](const TCHAR* Titre, TFunctionRef<bool(const FSection&,
			const FSection&)> Pire)
		{
			Sections.Sort(Pire);
			for (int32 K = 0; K < FMath::Min(Sections.Num(), 4); ++K)
			{
				const FSection& S = Sections[K];
				UE_LOG(LogTemp, Log,
					TEXT("[Worldseed]   %s : (%.0f, %.0f) riviere %d point %d  ")
					TEXT("lame %.1f m, mouille %.0f m"),
					Titre, OriginX + S.Px.X * CellCm, OriginY + S.Px.Y * CellCm,
					S.River, S.Point, S.LameM, S.WetM);
			}
		};

		Report(TEXT("plus sec  "), [](const FSection& A, const FSection& B)
			{ return A.LameM < B.LameM; });
		Report(TEXT("plus etale"), [](const FSection& A, const FSection& B)
			{ return A.WetM > B.WetM; });
	}
}
