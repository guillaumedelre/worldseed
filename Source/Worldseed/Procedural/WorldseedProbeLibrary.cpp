// Worldseed - points d'entree de verification, appelables sans lancer le jeu.

#include "Procedural/WorldseedProbeLibrary.h"

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedGlobe.h"
#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedPipeline.h"

namespace
{
	/**
	 * Remontee admise le long d'un cours d'eau, en metres.
	 *
	 * ELLE N'EST PAS NULLE A DESSEIN : le trace est SIMPLIFIE, donc un sommet
	 * conserve peut se trouver quelques centimetres au-dessus du precedent quand
	 * la corde coupe un meandre. Au-dela, c'est le reseau lui-meme qui remonte
	 * une pente — et ca, c'est un defaut.
	 */
	constexpr float DescentToleranceM = 0.5f;

	float ElevationAt(const WorldseedPipeline::FResult& World, const FVector2D& Px)
	{
		const int32 NX = World.Geometry.NX;
		const int32 NY = World.Geometry.NY;
		const int32 Col = ((FMath::RoundToInt(Px.X) % NX) + NX) % NX;
		const int32 Row = FMath::Clamp(FMath::RoundToInt(Px.Y), 0, NY - 1);
		return World.ElevationM[Row * NX + Col];
	}
}

FString UWorldseedProbeLibrary::ProbeGlobe(int32 Seed, float HeightMeters,
	int32 ResolutionY, int32 Frames)
{
	WorldseedPipeline::FResult World;
	FString Error;

	if (!WorldseedPipeline::Generate(Seed, HeightMeters, ResolutionY, World, Error))
	{
		return FString::Printf(TEXT("generation impossible : %s"), *Error);
	}

	WorldseedGlobe::FGlobeSettings Settings;

	// On chronometre la MEME scene sur deux heightfields : l'original et sa
	// reduction. Tout le reste est identique — meme texture, meme nombre de
	// pixels, meme rotation — donc l'ecart ne peut venir que des acces memoire.
	auto Chrono = [&](const TArray<float>& Heights, const FWorldseedGeometry& Geo) -> float
	{
		UTexture2D* Texture = WorldseedGlobe::Render(Heights, Geo, Settings, 512);
		if (!Texture)
		{
			return -1.0f;
		}

		// Une image hors chronometre : elle paie les defauts de cache
		// obligatoires du premier passage, qui ne se reproduisent pas.
		WorldseedGlobe::RenderInto(Texture, Heights, Geo, Settings);

		const int32 Count = FMath::Max(Frames, 1);
		const double Start = FPlatformTime::Seconds();
		for (int32 F = 0; F < Count; ++F)
		{
			// On tourne reellement : garder la meme orientation laisserait le
			// cache chaud sur une seule bande du monde et flatterait la mesure.
			Settings.LongitudeOffsetDeg = 360.0f * F / Count;
			WorldseedGlobe::RenderInto(Texture, Heights, Geo, Settings);
		}
		return static_cast<float>((FPlatformTime::Seconds() - Start) * 1000.0 / Count);
	};

	const float FullMs = Chrono(World.ElevationM, World.Geometry);

	// La reduction que le menu applique desormais.
	constexpr int32 PreviewNX = 1024;
	float PreviewMs = FullMs;
	int32 PreviewCells = World.Geometry.CellCount();

	if (World.Geometry.NX > PreviewNX)
	{
		TArray<float> Reduced;
		WorldseedGrid::Downsample(World.ElevationM, World.Geometry.NX, World.Geometry.NY,
			PreviewNX, PreviewNX / 2, Reduced);

		FWorldseedGeometry ReducedGeo = World.Geometry;
		ReducedGeo.NX = PreviewNX;
		ReducedGeo.NY = PreviewNX / 2;

		PreviewMs = Chrono(Reduced, ReducedGeo);
		PreviewCells = Reduced.Num();
	}

	const float FullMb = World.Geometry.CellCount() * sizeof(float) / (1024.0f * 1024.0f);
	const float PreviewMb = PreviewCells * sizeof(float) / (1024.0f * 1024.0f);

	const FString Summary = FString::Printf(
		TEXT("%dx%d (%.1f Mo) : %.2f ms   ->   reduit %.1f Mo : %.2f ms   (x%.1f plus rapide)"),
		World.Geometry.NX, World.Geometry.NY, FullMb, FullMs, PreviewMb, PreviewMs,
		PreviewMs > 0.0f ? FullMs / PreviewMs : 1.0f);

	UE_LOG(LogTemp, Log, TEXT("[Sonde] %s"), *Summary);
	return Summary;
}

FString UWorldseedProbeLibrary::ProbeHydrology(int32 Seed, float HeightMeters,
	int32 ResolutionY)
{
	WorldseedPipeline::FResult World;
	FString Error;

	if (!WorldseedPipeline::Generate(Seed, HeightMeters, ResolutionY, World, Error))
	{
		const FString Message = FString::Printf(TEXT("generation impossible : %s"), *Error);
		UE_LOG(LogTemp, Error, TEXT("[Sonde] %s"), *Message);
		return Message;
	}

	const FWorldseedHydrology& Hydro = World.Hydrology;
	const int32 NX = World.Geometry.NX;
	const int32 Cells = World.Geometry.CellCount();

	// --- les plus longs cours, en detail -------------------------------------
	TArray<int32> ByLength;
	for (int32 I = 0; I < Hydro.Rivers.Num(); ++I)
	{
		ByLength.Add(I);
	}
	ByLength.Sort([&Hydro](int32 A, int32 B)
	{
		return Hydro.Rivers[A].LengthM > Hydro.Rivers[B].LengthM;
	});

	for (int32 Rank = 0; Rank < FMath::Min(ByLength.Num(), 5); ++Rank)
	{
		const FWorldseedRiver& River = Hydro.Rivers[ByLength[Rank]];
		UE_LOG(LogTemp, Log,
			TEXT("[Sonde]   riviere %d : %.2f km  ordre %d  %d points  largeur %.1f-%.1f m  vers %s"),
			Rank + 1, River.LengthM / 1000.0f, River.StrahlerOrder, River.PointsPx.Num(),
			River.WidthM.Num() > 0 ? River.WidthM[0] : 0.0f,
			River.WidthM.Num() > 0 ? River.WidthM.Last() : 0.0f,
			WorldseedRivers::MouthName(River.Mouth));
	}

	// --- CONTROLE 1 : un cours d'eau DESCEND ---------------------------------
	// Le test le plus severe du reseau : si un trace remonte, c'est que le
	// routage ou la simplification a faute quelque part.
	int32 RiversThatClimb = 0;
	float WorstClimbM = 0.0f;
	for (const FWorldseedRiver& River : Hydro.Rivers)
	{
		// On controle LA SURFACE D'EAU, qui est ce que le joueur verra couler,
		// et non le relief brut : celui-ci plonge dans les cuvettes comblees,
		// ou l'eau, elle, reste a niveau.
		const bool bHasSurface = (River.SurfaceM.Num() == River.PointsPx.Num());

		float WorstHere = 0.0f;
		for (int32 I = 1; I < River.PointsPx.Num(); ++I)
		{
			const float Rise = bHasSurface
				? (River.SurfaceM[I] - River.SurfaceM[I - 1])
				: (ElevationAt(World, River.PointsPx[I])
					- ElevationAt(World, River.PointsPx[I - 1]));
			WorstHere = FMath::Max(WorstHere, Rise);
		}
		if (WorstHere > DescentToleranceM)
		{
			++RiversThatClimb;
			WorstClimbM = FMath::Max(WorstClimbM, WorstHere);
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Sonde] DESCENTE : %d cours sur %d remontent de plus de %.1f m (pire : %.1f m)"),
		RiversThatClimb, Hydro.Rivers.Num(), DescentToleranceM, WorstClimbM);

	// --- CONTROLE 2 : un cours d'eau reste a terre ---------------------------
	// Seul le dernier point a le droit de toucher la mer : c'est l'embouchure.
	int32 PointsAtSea = 0;
	for (const FWorldseedRiver& River : Hydro.Rivers)
	{
		for (int32 I = 0; I < River.PointsPx.Num() - 1; ++I)
		{
			if (ElevationAt(World, River.PointsPx[I]) <= 0.0f)
			{
				++PointsAtSea;
			}
		}
	}
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde] TERRE : %d points de trace sous le niveau de la mer"), PointsAtSea);

	// --- CONTROLE 3 : pourquoi aucun cours ne finit dans un lac ? ------------
	// Un lac n'attrape un cours d'eau que s'il est POSE SUR le reseau. On mesure
	// donc combien de cellules de nappe portent aussi un chenal.
	int32 LakeCells = 0;
	int32 LakeCellsOnChannel = 0;
	if (Hydro.LakeMask.Num() == Cells && Hydro.RiverMask.Num() == Cells)
	{
		for (int32 I = 0; I < Cells; ++I)
		{
			if (!Hydro.LakeMask[I])
			{
				continue;
			}
			++LakeCells;
			if (Hydro.RiverMask[I])
			{
				++LakeCellsOnChannel;
			}
		}
	}
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde] LACS : %d cellules de nappe, dont %d portant un chenal (%.1f %%)"),
		LakeCells, LakeCellsOnChannel,
		LakeCells > 0 ? 100.0f * LakeCellsOnChannel / LakeCells : 0.0f);

	for (int32 Rank = 0; Rank < FMath::Min(Hydro.Lakes.Num(), 5); ++Rank)
	{
		const FWorldseedLake& Lake = Hydro.Lakes[Rank];
		UE_LOG(LogTemp, Log,
			TEXT("[Sonde]   lac %d : %.1f ha  surface a %.1f m  contour de %d points"),
			Rank + 1, Lake.AreaHa, Lake.SurfaceM, Lake.OutlinePx.Num());
	}

	// --- CONTROLE 4 : les embouchures ----------------------------------------
	int32 Mouths[4] = {};
	for (const FWorldseedRiver& River : Hydro.Rivers)
	{
		++Mouths[static_cast<int32>(River.Mouth)];
	}
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde] EMBOUCHURES : %d ocean, %d lac, %d bord, %d endoreique"),
		Mouths[0], Mouths[1], Mouths[2], Mouths[3]);

	// --- CONTROLE 5 : les cascades -------------------------------------------
	float HighestDropM = 0.0f;
	float SteepestDeg = 0.0f;
	for (const FWorldseedWaterfall& Fall : Hydro.Waterfalls)
	{
		HighestDropM = FMath::Max(HighestDropM, Fall.DropM);
		SteepestDeg = FMath::Max(SteepestDeg, Fall.SlopeDeg);
	}
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde] CASCADES : %d, plus haute %.1f m, plus raide %.0f degres"),
		Hydro.Waterfalls.Num(), HighestDropM, SteepestDeg);

	// --- CONTROLE 6 : repartition des biomes ---------------------------------
	if (World.Biomes.Index.Num() == Cells)
	{
		TArray<int32> Ranked;
		for (int32 B = 0; B < static_cast<int32>(EWorldseedBiome::Count); ++B)
		{
			Ranked.Add(B);
		}
		Ranked.Sort([&World](int32 A, int32 B)
		{
			return World.Biomes.LandSharePct[A] > World.Biomes.LandSharePct[B];
		});

		for (int32 Rank = 0; Rank < FMath::Min(Ranked.Num(), 10); ++Rank)
		{
			const int32 B = Ranked[Rank];
			if (World.Biomes.LandSharePct[B] < 0.05f)
			{
				break;
			}
			UE_LOG(LogTemp, Log, TEXT("[Sonde]   biome %-24s %5.1f %% des terres"),
				WorldseedBiomes::Name(static_cast<EWorldseedBiome>(B)),
				World.Biomes.LandSharePct[B]);
		}

		// LE SECOND AXE. Les parts de couverture NE S'AJOUTENT PAS aux parts de
		// biome : elles se superposent. Un total qui depasse cent pour cent est
		// donc normal — c'est meme le signe que la separation fonctionne.
		for (int32 C = 1; C < static_cast<int32>(EWorldseedCover::Count); ++C)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Sonde]   couverture %-18s %5.1f %% des terres (par-dessus le biome)"),
				WorldseedBiomes::CoverName(static_cast<EWorldseedCover>(C)),
				World.Biomes.CoverSharePct[C]);
		}
	}

	const FString Summary = FString::Printf(
		TEXT("seed %d  %dx%d  terres %.1f %%  |  %d rivieres, %d lacs, %d cascades"),
		Seed, NX, World.Geometry.NY, World.LandRatio * 100.0f,
		Hydro.Rivers.Num(), Hydro.Lakes.Num(), Hydro.Waterfalls.Num());

	UE_LOG(LogTemp, Log, TEXT("[Sonde] %s"), *Summary);
	return Summary;
}
