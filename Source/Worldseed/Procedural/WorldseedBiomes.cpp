// Worldseed - etape 5 : classification des biomes.

#include "Procedural/WorldseedBiomes.h"

#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedPerlin.h"
#include "Procedural/WorldseedWind.h"

#include "Async/ParallelFor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	const TCHAR* BIO = TEXT("biomes");

	constexpr int32 BiomeCount = static_cast<int32>(EWorldseedBiome::Count);

	/** Les noms des identifiants, dans l'ordre de world_rules.json. */
	const TCHAR* BiomeKeys[BiomeCount] = {
		TEXT("ocean"), TEXT("lac"), TEXT("riviere"), TEXT("calotte"),
		TEXT("toundra"), TEXT("taiga"), TEXT("foret_temperee"),
		TEXT("foret_temperee_humide"), TEXT("prairie"), TEXT("steppe"),
		TEXT("desert_froid"), TEXT("desert_chaud"), TEXT("savane"),
		TEXT("foret_tropicale_seche"), TEXT("foret_tropicale_humide"),
		TEXT("alpin"), TEXT("roche_nue"), TEXT("plage"), TEXT("marais"),
	};

	const TCHAR* BiomeNames[BiomeCount] = {
		TEXT("ocean"), TEXT("lac"), TEXT("riviere"), TEXT("calotte glaciaire"),
		TEXT("toundra"), TEXT("taiga"), TEXT("foret temperee"),
		TEXT("foret temperee humide"), TEXT("prairie"), TEXT("steppe"),
		TEXT("desert froid"), TEXT("desert chaud"), TEXT("savane"),
		TEXT("foret tropicale seche"), TEXT("foret tropicale humide"),
		TEXT("alpin"), TEXT("roche nue"), TEXT("plage"), TEXT("marais"),
	};

	/** debugColors de world_rules.json, en octets. */
	const uint8 BiomeRgb[BiomeCount][3] = {
		{  24,  62, 122 }, {  46, 116, 181 }, {  86, 164, 214 }, { 242, 246, 250 },
		{ 166, 176, 152 }, {  46,  84,  62 }, {  70, 128,  66 }, {  42, 104,  54 },
		{ 150, 168,  92 }, { 178, 172, 108 }, { 176, 176, 166 }, { 214, 190, 126 },
		{ 196, 172,  92 }, { 128, 152,  70 }, {  30, 110,  58 }, { 150, 146, 140 },
		{ 112, 108, 104 }, { 226, 212, 172 }, {  96, 118,  84 },
	};

	EWorldseedBiome BiomeFromKey(const FString& Key)
	{
		for (int32 I = 0; I < BiomeCount; ++I)
		{
			if (Key == BiomeKeys[I])
			{
				return static_cast<EWorldseedBiome>(I);
			}
		}
		return EWorldseedBiome::Grassland;
	}

	/** Dilatation binaire a quatre voisins, longitude enroulee. */
	void Dilate(TArray<bool>& Mask, int32 NX, int32 NY, int32 Iterations)
	{
		if (Iterations <= 0)
		{
			return;
		}

		TArray<bool> Source;
		for (int32 Pass = 0; Pass < Iterations; ++Pass)
		{
			Source = Mask;
			ParallelFor(NY, [&](int32 Row)
			{
				for (int32 Col = 0; Col < NX; ++Col)
				{
					const int32 Index = Row * NX + Col;
					if (Source[Index])
					{
						continue;
					}

					const int32 Left = ((Col - 1 + NX) % NX);
					const int32 Right = ((Col + 1) % NX);

					const bool bNear =
						Source[Row * NX + Left] || Source[Row * NX + Right]
						|| (Row > 0 && Source[(Row - 1) * NX + Col])
						|| (Row < NY - 1 && Source[(Row + 1) * NX + Col]);

					if (bNear)
					{
						Mask[Index] = true;
					}
				}
			});
		}
	}
}

FWorldseedBiomeRules FWorldseedBiomeRules::FromRules(const UWorldseedRules& Rules)
{
	FWorldseedBiomeRules Out;

	auto Num = [&Rules](const TCHAR* Key, double Fallback)
	{
		return static_cast<float>(Rules.Num(BIO, Key, Fallback));
	};

	Out.TreeLineTempC = Num(TEXT("treeLineTempC"), 4.0);
	Out.AlpineMinElevationM = Num(TEXT("alpineMinElevationM"), 212.5);
	Out.PermanentIceTempC = Num(TEXT("permanentIceTempC"), 0.0);
	Out.BareRockSlopeDeg = Num(TEXT("bareRockSlopeDeg"), 55.0);

	Out.BeachElevationM = Num(TEXT("beachElevationM"), 3.75);
	Out.BeachWidthM = Num(TEXT("beachWidthM"), 37.5);
	Out.BeachSlopeFlatDeg = Num(TEXT("beachSlopeFlatDeg"), 8.0);
	Out.BeachSlopeSteepDeg = Num(TEXT("beachSlopeSteepDeg"), 30.0);
	Out.BeachWindwardBonus = Num(TEXT("beachWindwardBonus"), 0.6);

	Out.MarshMaxSlopeDeg = Num(TEXT("marshMaxSlopeDeg"), 2.0);
	Out.MarshMinPrecipMm = Num(TEXT("marshMinPrecipMm"), 900.0);

	// --- diagramme de Whittaker ---------------------------------------------
	if (const TArray<TSharedPtr<FJsonValue>>* Bands =
		Rules.Array(BIO, TEXT("whittakerBands")))
	{
		for (const TSharedPtr<FJsonValue>& Entry : *Bands)
		{
			const TSharedPtr<FJsonObject>* BandObj = nullptr;
			if (!Entry.IsValid() || !Entry->TryGetObject(BandObj) || !BandObj->IsValid())
			{
				continue;   // les cles de commentaire ne sont pas des bandes
			}

			FWorldseedWhittakerBand Band;
			if (!(*BandObj)->TryGetNumberField(TEXT("tMax"), Band.MaxTempC))
			{
				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>* Cuts = nullptr;
			if (!(*BandObj)->TryGetArrayField(TEXT("cuts"), Cuts))
			{
				continue;
			}

			for (const TSharedPtr<FJsonValue>& CutValue : *Cuts)
			{
				const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
				if (!CutValue.IsValid() || !CutValue->TryGetArray(Pair) || Pair->Num() < 2)
				{
					continue;
				}

				FWorldseedWhittakerCut Cut;
				Cut.MaxPrecipMm = static_cast<float>((*Pair)[0]->AsNumber());
				Cut.Biome = BiomeFromKey((*Pair)[1]->AsString());
				Band.Cuts.Add(Cut);
			}

			if (Band.Cuts.Num() > 0)
			{
				Out.Bands.Add(MoveTemp(Band));
			}
		}
	}

	return Out;
}

namespace WorldseedBiomes
{
	FLinearColor Colour(EWorldseedBiome Biome)
	{
		const int32 I = FMath::Clamp(static_cast<int32>(Biome), 0, BiomeCount - 1);
		return FLinearColor(FColor(BiomeRgb[I][0], BiomeRgb[I][1], BiomeRgb[I][2], 255));
	}

	FLinearColor CoverColour(EWorldseedCover Cover)
	{
		// Les trois teintes d'eau de debugColors, a leur place d'origine dans la
		// table : la couverture et le biome partagent la meme palette.
		switch (Cover)
		{
		case EWorldseedCover::Ocean: return Colour(EWorldseedBiome::Ocean);
		case EWorldseedCover::Lake:  return Colour(EWorldseedBiome::Lake);
		case EWorldseedCover::River: return Colour(EWorldseedBiome::River);
		default:                     return FLinearColor::Transparent;
		}
	}

	const TCHAR* CoverName(EWorldseedCover Cover)
	{
		switch (Cover)
		{
		case EWorldseedCover::Ocean: return TEXT("ocean");
		case EWorldseedCover::Lake:  return TEXT("lac");
		case EWorldseedCover::River: return TEXT("riviere");
		default:                     return TEXT("a decouvert");
		}
	}

	const TCHAR* Name(EWorldseedBiome Biome)
	{
		const int32 I = FMath::Clamp(static_cast<int32>(Biome), 0, BiomeCount - 1);
		return BiomeNames[I];
	}

	FLinearColor SlotWeights(EWorldseedBiome Biome)
	{
		// Ordre : herbe, aride, roche, mousse.
		//
		// LES MELANGES NE SONT PAS DECORATIFS. Une savane est de l'herbe qui
		// laisse voir la terre, une steppe l'inverse ; un etage alpin est de la
		// roche que la mousse colonise par plaques. Ce sont ces proportions qui
		// font qu'on reconnait un biome sans lire son nom.
		switch (Biome)
		{
		case EWorldseedBiome::IceCap:              return FLinearColor(0.00f, 0.00f, 1.00f, 0.00f);
		case EWorldseedBiome::Tundra:              return FLinearColor(0.00f, 0.10f, 0.25f, 0.65f);
		case EWorldseedBiome::Taiga:               return FLinearColor(0.70f, 0.00f, 0.10f, 0.20f);
		case EWorldseedBiome::TemperateForest:     return FLinearColor(0.90f, 0.00f, 0.05f, 0.05f);
		case EWorldseedBiome::TemperateRainforest: return FLinearColor(0.70f, 0.00f, 0.00f, 0.30f);
		case EWorldseedBiome::Grassland:           return FLinearColor(1.00f, 0.00f, 0.00f, 0.00f);
		case EWorldseedBiome::Steppe:              return FLinearColor(0.45f, 0.50f, 0.05f, 0.00f);
		case EWorldseedBiome::ColdDesert:          return FLinearColor(0.05f, 0.60f, 0.35f, 0.00f);
		case EWorldseedBiome::HotDesert:           return FLinearColor(0.00f, 1.00f, 0.00f, 0.00f);
		case EWorldseedBiome::Savanna:             return FLinearColor(0.40f, 0.55f, 0.05f, 0.00f);
		case EWorldseedBiome::TropicalDryForest:   return FLinearColor(0.60f, 0.35f, 0.05f, 0.00f);
		case EWorldseedBiome::TropicalRainforest:  return FLinearColor(0.80f, 0.00f, 0.00f, 0.20f);
		case EWorldseedBiome::Alpine:              return FLinearColor(0.05f, 0.10f, 0.65f, 0.20f);
		case EWorldseedBiome::BareRock:            return FLinearColor(0.00f, 0.05f, 0.95f, 0.00f);
		case EWorldseedBiome::Beach:               return FLinearColor(0.00f, 1.00f, 0.00f, 0.00f);
		case EWorldseedBiome::Marsh:               return FLinearColor(0.20f, 0.00f, 0.00f, 0.80f);

		// L'eau : le composant d'eau pose sa nappe par-dessus, mais le fond
		// doit tout de meme ressembler a quelque chose.
		case EWorldseedBiome::Ocean:
		case EWorldseedBiome::Lake:                return FLinearColor(0.00f, 0.55f, 0.45f, 0.00f);
		case EWorldseedBiome::River:               return FLinearColor(0.10f, 0.45f, 0.35f, 0.10f);

		default:                                   return FLinearColor(1.00f, 0.00f, 0.00f, 0.00f);
		}
	}

	void SlopeDegrees(const TArray<float>& ElevationM, const FWorldseedGeometry& Geometry,
		TArray<float>& OutSlopeDeg)
	{
		const int32 Count = Geometry.CellCount();
		OutSlopeDeg.SetNumZeroed(Count);
		if (ElevationM.Num() != Count)
		{
			return;
		}

		TArray<float> DY;
		TArray<float> DX;
		WorldseedGrid::Gradient(ElevationM, Geometry.NX, Geometry.NY,
			Geometry.MetersPerPixel(), DY, DX);

		ParallelFor(Count, [&](int32 I)
		{
			OutSlopeDeg[I] = FMath::RadiansToDegrees(
				FMath::Atan(FMath::Sqrt(DX[I] * DX[I] + DY[I] * DY[I])));
		});
	}

	void Classify(const FWorldseedGeometry& Geometry, const TArray<float>& ElevationM,
		const TArray<float>& TempMeanC, const TArray<float>& TempMaxC,
		const TArray<float>& PrecipMm, const TArray<bool>& LakeMask,
		const TArray<bool>& RiverMask, const FWorldseedBiomeRules& Rules,
		FWorldseedBiomeMap& Out)
	{
		const int32 NX = Geometry.NX;
		const int32 NY = Geometry.NY;
		const int32 Count = Geometry.CellCount();

		Out.Index.Init(static_cast<uint8>(EWorldseedBiome::Grassland), Count);
		Out.Cover.Init(static_cast<uint8>(EWorldseedCover::None), Count);
		FMemory::Memzero(Out.LandSharePct, sizeof(Out.LandSharePct));
		FMemory::Memzero(Out.CoverSharePct, sizeof(Out.CoverSharePct));

		if (ElevationM.Num() != Count || TempMeanC.Num() != Count
			|| PrecipMm.Num() != Count || Rules.Bands.Num() == 0)
		{
			return;
		}

		const double StartTime = FPlatformTime::Seconds();

		SlopeDegrees(ElevationM, Geometry, Out.SlopeDeg);

		const bool bHasTempMax = (TempMaxC.Num() == Count);
		const bool bHasLakes = (LakeMask.Num() == Count);
		const bool bHasRivers = (RiverMask.Num() == Count);

		// --- distance a l'ocean, pour l'estran -----------------------------------
		TArray<uint8> LandMask;
		LandMask.SetNumUninitialized(Count);
		for (int32 I = 0; I < Count; ++I)
		{
			LandMask[I] = (ElevationM[I] > 0.0f) ? 1 : 0;
		}

		TArray<float> DistanceToOceanPx;
		WorldseedGrid::DistanceTransform(LandMask, NX, NY, DistanceToOceanPx);

		// Le gradient de cette distance pointe vers l'INTERIEUR des terres : un
		// vent aligne avec lui souffle de la mer vers la cote, donc au vent.
		TArray<float> DistDY;
		TArray<float> DistDX;
		const float MetresPerPixel = Geometry.MetersPerPixel();
		{
			TArray<float> DistanceM;
			DistanceM.SetNumUninitialized(Count);
			for (int32 I = 0; I < Count; ++I)
			{
				DistanceM[I] = DistanceToOceanPx[I] * MetresPerPixel;
			}
			WorldseedGrid::Gradient(DistanceM, NX, NY, MetresPerPixel, DistDY, DistDX);
		}

		// --- eau douce dilatee, pour le marais -----------------------------------
		TArray<bool> NearFresh;
		NearFresh.Init(false, Count);
		for (int32 I = 0; I < Count; ++I)
		{
			NearFresh[I] = (bHasLakes && LakeMask[I]) || (bHasRivers && RiverMask[I]);
		}
		Dilate(NearFresh, NX, NY, 3);

		// --- classification ------------------------------------------------------
		ParallelFor(NY, [&](int32 Row)
		{
			const float LatitudeDeg = Geometry.LatitudeDegForRow(Row);

			float WindEast = 0.0f;
			float WindNorth = 0.0f;
			WorldseedWind::Prevailing(LatitudeDeg, Geometry.LatSpanDeg, WindEast, WindNorth);

			for (int32 Col = 0; Col < NX; ++Col)
			{
				const int32 I = Row * NX + Col;
				const bool bLand = (ElevationM[I] > 0.0f);

				const float T = TempMeanC[I];
				const float P = PrecipMm[I];
				const float Slope = Out.SlopeDeg[I];

				// --- diagramme de Whittaker ---------------------------------------
				// Bandes de la plus froide a la plus chaude ; la premiere dont
				// tMax depasse la temperature l'emporte, puis le premier seuil
				// de pluie depasse a l'interieur.
				EWorldseedBiome Biome = Rules.Bands.Last().Cuts.Last().Biome;
				for (const FWorldseedWhittakerBand& Band : Rules.Bands)
				{
					if (T > Band.MaxTempC)
					{
						continue;
					}
					for (const FWorldseedWhittakerCut& Cut : Band.Cuts)
					{
						if (P <= Cut.MaxPrecipMm)
						{
							Biome = Cut.Biome;
							break;
						}
					}
					break;
				}

				// --- surcharges, de la moins a la plus prioritaire -----------------
				// ELLES NE VALENT QU'A TERRE : sous la mer, le diagramme dit la
				// bande climatique de l'eau, et ni la pente du fond ni la
				// distance au rivage n'ont de sens a lui appliquer.
				if (!bLand)
				{
					Out.Index[I] = static_cast<uint8>(Biome);
					Out.Cover[I] = static_cast<uint8>(EWorldseedCover::Ocean);
					continue;
				}

				// Etage alpin : au-dessus de la limite des arbres ET en altitude
				// REELLE. Sans le second critere, toute la toundra polaire de
				// bord de mer basculerait en alpin, ce qui n'a aucun sens.
				if (T < Rules.TreeLineTempC && ElevationM[I] > Rules.AlpineMinElevationM)
				{
					Biome = EWorldseedBiome::Alpine;
				}

				// Au-dela de l'angle de tenue, aucune terre ne reste, quel que
				// soit le climat.
				if (Slope > Rules.BareRockSlopeDeg)
				{
					Biome = EWorldseedBiome::BareRock;
				}

				// Calotte : meme le mois le PLUS CHAUD reste sous le gel.
				if (bHasTempMax && TempMaxC[I] < Rules.PermanentIceTempC)
				{
					Biome = EWorldseedBiome::IceCap;
				}

				// --- estran, de largeur VARIABLE ----------------------------------
				// LA PENTE NE DOIT PAS TRANCHER MAIS MODULER. Une regle en tout
				// ou rien a 12 degres reduisait la plage a 0,2 % des terres — un
				// a deux pixels — parce que la bande cotiere de ce monde a une
				// pente MEDIANE de 31 degres : ces cotes sont escarpees.
				if (ElevationM[I] < Rules.BeachElevationM)
				{
					const float FlatFactor = 1.0f - WorldseedPerlin::Smoothstep(
						Rules.BeachSlopeFlatDeg, Rules.BeachSlopeSteepDeg, Slope);

					const float GradX = DistDX[I];
					const float GradY = DistDY[I];
					const float GradNorm = FMath::Max(
						FMath::Sqrt(GradX * GradX + GradY * GradY), 1e-6f);
					const float Exposure = FMath::Clamp(
						(WindEast * GradX + WindNorth * GradY) / GradNorm, 0.0f, 1.0f);

					const float WidthM = Rules.BeachWidthM * FlatFactor
						* (1.0f + Rules.BeachWindwardBonus * Exposure);

					if (DistanceToOceanPx[I] * MetresPerPixel <= WidthM)
					{
						Biome = EWorldseedBiome::Beach;
					}
				}

				// Marais : plat, humide, au contact de l'eau douce.
				if (NearFresh[I] && Slope < Rules.MarshMaxSlopeDeg && P > Rules.MarshMinPrecipMm)
				{
					Biome = EWorldseedBiome::Marsh;
				}

				Out.Index[I] = static_cast<uint8>(Biome);

				// L'EAU N'ECRASE PLUS RIEN : elle se range dans son propre axe.
				// Une berge reste de la savane, et le sait.
				EWorldseedCover Cover = EWorldseedCover::None;
				if (bHasRivers && RiverMask[I]) { Cover = EWorldseedCover::River; }
				if (bHasLakes && LakeMask[I]) { Cover = EWorldseedCover::Lake; }
				Out.Cover[I] = static_cast<uint8>(Cover);
			}
		});

		// --- releve ---------------------------------------------------------------
		constexpr int32 CoverCount = static_cast<int32>(EWorldseedCover::Count);

		int32 LandTotal = 0;
		int32 Tally[BiomeCount] = {};
		int32 CoverTally[CoverCount] = {};
		for (int32 I = 0; I < Count; ++I)
		{
			if (ElevationM[I] > 0.0f)
			{
				++LandTotal;
				++Tally[FMath::Min<int32>(Out.Index[I], BiomeCount - 1)];
				++CoverTally[FMath::Min<int32>(Out.Cover[I], CoverCount - 1)];
			}
		}

		if (LandTotal > 0)
		{
			for (int32 B = 0; B < BiomeCount; ++B)
			{
				Out.LandSharePct[B] = 100.0f * Tally[B] / LandTotal;
			}
			for (int32 C = 0; C < CoverCount; ++C)
			{
				Out.CoverSharePct[C] = 100.0f * CoverTally[C] / LandTotal;
			}
		}

		// Les trois plus repandus : de quoi juger d'un coup si la carte est
		// plausible, sans noyer le journal.
		TArray<int32> Ranked;
		for (int32 B = 0; B < BiomeCount; ++B) { Ranked.Add(B); }
		Ranked.Sort([&Out](int32 A, int32 B) { return Out.LandSharePct[A] > Out.LandSharePct[B]; });

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] biomes : %s %.1f %%, %s %.1f %%, %s %.1f %%")
			TEXT("  |  couverture : lac %.1f %%, riviere %.1f %%  (%.0f ms)"),
			Name(static_cast<EWorldseedBiome>(Ranked[0])), Out.LandSharePct[Ranked[0]],
			Name(static_cast<EWorldseedBiome>(Ranked[1])), Out.LandSharePct[Ranked[1]],
			Name(static_cast<EWorldseedBiome>(Ranked[2])), Out.LandSharePct[Ranked[2]],
			Out.CoverSharePct[static_cast<int32>(EWorldseedCover::Lake)],
			Out.CoverSharePct[static_cast<int32>(EWorldseedCover::River)],
			(FPlatformTime::Seconds() - StartTime) * 1000.0);
	}
}
