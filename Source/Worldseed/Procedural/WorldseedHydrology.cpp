// Worldseed - etape 4b : de l'ecoulement aux rivieres, lacs et cascades.

#include "Procedural/WorldseedHydrology.h"

#include "Procedural/WorldseedFlow.h"

namespace
{
	const TCHAR* HYD = TEXT("hydrology");
}

FWorldseedHydrologyRules FWorldseedHydrologyRules::FromRules(const UWorldseedRules& Rules)
{
	FWorldseedHydrologyRules Out;

	auto Num = [&Rules](const TCHAR* Key, double Fallback)
	{
		return static_cast<float>(Rules.Num(HYD, Key, Fallback));
	};
	auto Int = [&Rules](const TCHAR* Key, int32 Fallback)
	{
		return Rules.Int(HYD, Key, Fallback);
	};

	Out.RiverDischargeThreshold = Num(TEXT("riverDischargeThreshold"), 0.00025);
	Out.MinStrahlerOrder = Int(TEXT("minStrahlerOrder"), 2);
	Out.RunoffCoefficient = Num(TEXT("runoffCoefficient"), 0.35);

	Out.WidthCoefA = Num(TEXT("widthCoefA"), 2.0);
	Out.WidthExponent = Num(TEXT("widthExponent"), 0.5);
	Out.DepthCoefB = Num(TEXT("depthCoefB"), 0.3);
	Out.DepthExponent = Num(TEXT("depthExponent"), 0.4);
	Out.MinRiverWidthM = Num(TEXT("minRiverWidthM"), 1.0);
	Out.MaxRiverWidthM = Num(TEXT("maxRiverWidthM"), 48.0);
	Out.MinRiverDepthM = Num(TEXT("minRiverDepthM"), 0.4);
	Out.GeometryExaggeration = Num(TEXT("geometryExaggeration"), 8000.0);

	Out.SimplifyToleranceM = Num(TEXT("simplifyToleranceM"), 5.0);
	Out.MaxPointSpacingM = Num(TEXT("maxPointSpacingM"), 40.0);
	Out.MaxPointsPerRiver = Int(TEXT("maxPointsPerRiver"), 120);
	Out.MinRiverLengthM = Num(TEXT("minRiverLengthM"), 225.0);
	Out.MaxRiverActors = Int(TEXT("maxRiverActors"), 60);

	Out.MinLakeDepthM = Num(TEXT("minLakeDepthM"), 0.5);
	Out.MinLakeAreaHa = Num(TEXT("minLakeAreaHa"), 9.375);
	Out.MaxLakeActors = Int(TEXT("maxLakeActors"), 40);
	Out.MaxPointsPerLake = Int(TEXT("maxPointsPerLake"), 300);
	Out.RiparianBandM = Num(TEXT("riparianBandM"), 30.0);
	Out.RiparianCells = Int(TEXT("riparianCells"), 3);

	return Out;
}

namespace WorldseedHydrology
{
	namespace
	{
		/** Dilatation binaire a quatre voisins, longitude enroulee. */
		void Dilate(TArray<bool>& Mask, int32 NX, int32 NY, int32 Iterations)
		{
			TArray<bool> Source;
			for (int32 Pass = 0; Pass < Iterations; ++Pass)
			{
				Source = Mask;
				for (int32 Row = 0; Row < NY; ++Row)
				{
					for (int32 Col = 0; Col < NX; ++Col)
					{
						const int32 Index = Row * NX + Col;
						if (Source[Index])
						{
							continue;
						}

						const int32 Left = (Col - 1 + NX) % NX;
						const int32 Right = (Col + 1) % NX;

						if (Source[Row * NX + Left] || Source[Row * NX + Right]
							|| (Row > 0 && Source[(Row - 1) * NX + Col])
							|| (Row < NY - 1 && Source[(Row + 1) * NX + Col]))
						{
							Mask[Index] = true;
						}
					}
				}
			}
		}

		void BuildRiverMask(const FWorldseedFlow& Flow, const TArray<float>& ElevationM,
			const FWorldseedGeometry& Geometry, const FWorldseedHydrologyRules& Rules,
			TArray<bool>& Out)
		{
			const int32 Count = Geometry.CellCount();
			Out.Init(false, Count);

			for (int32 I = 0; I < Count; ++I)
			{
				Out[I] = (Flow.Accumulation[I] >= Rules.RiverDischargeThreshold)
					&& (ElevationM[I] > 0.0f);
			}

			Dilate(Out, Geometry.NX, Geometry.NY,
				Rules.RiparianIterations(Geometry.MetersPerPixel()));

			// La dilatation deborde sur la mer : on la ramene a terre.
			for (int32 I = 0; I < Count; ++I)
			{
				Out[I] = Out[I] && (ElevationM[I] > 0.0f);
			}
		}
	}

	void Generate(const TArray<float>& ElevationM, const TArray<float>& PrecipMm,
		const FWorldseedGeometry& Geometry, const FWorldseedHydrologyRules& Rules,
		FWorldseedHydrology& Out)
	{
		Out = FWorldseedHydrology();

		const int32 Count = Geometry.CellCount();
		if (Geometry.NX < 2 || ElevationM.Num() != Count)
		{
			return;
		}

		const double StartTime = FPlatformTime::Seconds();

		// --- debit ---------------------------------------------------------------
		TArray<float> Weights;
		WorldseedRivers::DischargeWeights(PrecipMm, Geometry, Rules.RunoffCoefficient, Weights);

		FWorldseedFlow Flow;
		WorldseedFlow::Compute(ElevationM, Weights, Geometry.NX, Geometry.NY,
			0.0f, 1e-4f, Flow);

		// --- lacs AVANT rivieres -------------------------------------------------
		// L'ordre n'est pas indifferent : le classement de l'embouchure d'un
		// cours d'eau a besoin de savoir ou sont les nappes, faute de quoi tout
		// cours finissant dans un lac serait declare endoreique.
		WorldseedLakes::BuildMask(Flow.LakeDepthM, ElevationM, Geometry, Rules, Out.LakeMask);
		WorldseedLakes::Extract(Flow, ElevationM, Geometry, Rules, Out.Lakes, Out.LakeLabels);

		WorldseedRivers::Extract(Flow, ElevationM, Out.LakeMask, Geometry, Rules, Out.Rivers);

		// Masque du reseau COMPLET, berges comprises : les polylignes ne gardent
		// que les axes principaux, mais un biome de riviere doit suivre jusqu'au
		// moindre filet.
		BuildRiverMask(Flow, ElevationM, Geometry, Rules, Out.RiverMask);

		WorldseedWaterfalls::Detect(Out.Rivers, Geometry,
			DefaultWaterfallSlopeDeg, DefaultWaterfallMinDropM, Out.Waterfalls);

		// --- releve ---------------------------------------------------------------
		int32 ToOcean = 0;
		int32 ToLake = 0;
		int32 Endorheic = 0;
		float LongestM = 0.0f;
		for (const FWorldseedRiver& River : Out.Rivers)
		{
			LongestM = FMath::Max(LongestM, River.LengthM);
			switch (River.Mouth)
			{
			case EWorldseedMouth::Ocean:     ++ToOcean; break;
			case EWorldseedMouth::Lake:      ++ToLake; break;
			case EWorldseedMouth::Endorheic: ++Endorheic; break;
			default: break;
			}
		}

		float LakeAreaHa = 0.0f;
		for (const FWorldseedLake& Lake : Out.Lakes)
		{
			LakeAreaHa += Lake.AreaHa;
		}

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] hydrologie : %d rivieres (%d vers l'ocean, %d vers un lac, %d endoreiques), ")
			TEXT("la plus longue %.1f km  |  %d lacs, %.0f ha  |  %d cascades  (%.0f ms)"),
			Out.Rivers.Num(), ToOcean, ToLake, Endorheic, LongestM / 1000.0f,
			Out.Lakes.Num(), LakeAreaHa, Out.Waterfalls.Num(),
			(FPlatformTime::Seconds() - StartTime) * 1000.0);
	}
}
