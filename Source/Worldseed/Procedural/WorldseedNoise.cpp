// Worldseed - generation de relief deterministe.

#include "Procedural/WorldseedNoise.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"

namespace
{
	/** Etale les decalages assez loin pour que deux octaves ne se correlent pas. */
	constexpr float OffsetSpread = 8192.0f;

	/** fBm classique, renvoie approximativement [-1..1]. */
	float FBM(const FWorldseedTerrainParams& P, const FWorldseedNoiseContext& Ctx,
		float U, float V)
	{
		float Amplitude = 1.0f;
		float Frequency = P.BaseFrequency;
		float Sum = 0.0f;
		float Normalisation = 0.0f;

		for (int32 Octave = 0; Octave < P.Octaves; ++Octave)
		{
			const FVector2D& Offset = Ctx.OctaveOffsets[Octave];
			const FVector2D Position(U * Frequency + Offset.X, V * Frequency + Offset.Y);

			Sum += Amplitude * FMath::PerlinNoise2D(Position);
			Normalisation += Amplitude;

			Amplitude *= P.Gain;
			Frequency *= P.Lacunarity;
		}

		return (Normalisation > SMALL_NUMBER) ? (Sum / Normalisation) : 0.0f;
	}

	/**
	 * Bruit ridged : on replie le bruit autour de zero puis on l'eleve au carre.
	 * Produit des cretes nettes, typiques des chaines de montagnes.
	 */
	float Ridged(const FWorldseedTerrainParams& P, const FWorldseedNoiseContext& Ctx,
		float U, float V)
	{
		float Amplitude = 1.0f;
		float Frequency = P.BaseFrequency;
		float Sum = 0.0f;
		float Normalisation = 0.0f;

		for (int32 Octave = 0; Octave < P.Octaves; ++Octave)
		{
			const FVector2D& Offset = Ctx.OctaveOffsets[Octave];
			const FVector2D Position(U * Frequency + Offset.Y, V * Frequency + Offset.X);

			float Value = 1.0f - FMath::Abs(FMath::PerlinNoise2D(Position));
			Value *= Value;

			Sum += Amplitude * Value;
			Normalisation += Amplitude;

			Amplitude *= P.Gain;
			Frequency *= P.Lacunarity;
		}

		return (Normalisation > SMALL_NUMBER) ? (Sum / Normalisation) * 2.0f - 1.0f : 0.0f;
	}

	/**
	 * Soulevement tectonique : distance aux deux plaques les plus proches.
	 * Quand elles sont a egale distance on est sur une frontiere, donc sur une
	 * chaine. Cela produit des reliefs LINEAIRES et arques, ce que le fBm seul
	 * ne sait pas faire : il ne fabrique que des taches.
	 */
	float TectonicUplift(const FWorldseedTerrainParams& P,
		const FWorldseedNoiseContext& Ctx, float U, float V)
	{
		if (Ctx.PlateCenters.Num() < 2)
		{
			return 0.0f;
		}

		const FVector2D Position(U, V);
		float Nearest = BIG_NUMBER;
		float SecondNearest = BIG_NUMBER;

		for (const FVector2D& Center : Ctx.PlateCenters)
		{
			const float Distance = FVector2D::Distance(Position, Center);
			if (Distance < Nearest)
			{
				SecondNearest = Nearest;
				Nearest = Distance;
			}
			else if (Distance < SecondNearest)
			{
				SecondNearest = Distance;
			}
		}

		// Ecart faible => frontiere => soulevement fort.
		const float EdgeDistance = SecondNearest - Nearest;
		return FMath::Exp(-EdgeDistance * P.TectonicSharpness);
	}
}

bool FWorldseedTerrainParams::operator==(const FWorldseedTerrainParams& O) const
{
	return Seed == O.Seed
		&& FMath::IsNearlyEqual(MapSizeMeters, O.MapSizeMeters)
		&& Resolution == O.Resolution
		&& FMath::IsNearlyEqual(HeightScale, O.HeightScale)
		&& Octaves == O.Octaves
		&& FMath::IsNearlyEqual(BaseFrequency, O.BaseFrequency)
		&& FMath::IsNearlyEqual(Lacunarity, O.Lacunarity)
		&& FMath::IsNearlyEqual(Gain, O.Gain)
		&& FMath::IsNearlyEqual(MountainAmount, O.MountainAmount)
		&& FMath::IsNearlyEqual(ContinentFalloff, O.ContinentFalloff)
		&& FMath::IsNearlyEqual(WarpStrength, O.WarpStrength)
		&& FMath::IsNearlyEqual(WarpFrequency, O.WarpFrequency)
		&& PlateCount == O.PlateCount
		&& FMath::IsNearlyEqual(TectonicAmount, O.TectonicAmount)
		&& FMath::IsNearlyEqual(TectonicSharpness, O.TectonicSharpness)
		&& ThermalIterations == O.ThermalIterations
		&& FMath::IsNearlyEqual(TalusAngleDegrees, O.TalusAngleDegrees)
		&& FMath::IsNearlyEqual(ThermalRate, O.ThermalRate)
		&& DropletCount == O.DropletCount
		&& DropletLifetime == O.DropletLifetime
		&& FMath::IsNearlyEqual(Inertia, O.Inertia)
		&& FMath::IsNearlyEqual(SedimentCapacityFactor, O.SedimentCapacityFactor)
		&& FMath::IsNearlyEqual(MinSedimentCapacity, O.MinSedimentCapacity)
		&& FMath::IsNearlyEqual(ErodeSpeed, O.ErodeSpeed)
		&& FMath::IsNearlyEqual(DepositSpeed, O.DepositSpeed)
		&& FMath::IsNearlyEqual(EvaporateSpeed, O.EvaporateSpeed)
		&& FMath::IsNearlyEqual(Gravity, O.Gravity)
		&& ErosionBrushRadius == O.ErosionBrushRadius;
}

void FWorldseedNoiseContext::Init(const FWorldseedTerrainParams& Params)
{
	// FRandomStream est deterministe et sans etat global, contrairement a
	// FMath::Rand : indispensable pour que le seed soit reellement reproductible.
	FRandomStream Stream(Params.Seed);

	const int32 OctaveCount = FMath::Max(1, Params.Octaves);
	OctaveOffsets.Reset(OctaveCount);
	for (int32 Octave = 0; Octave < OctaveCount; ++Octave)
	{
		OctaveOffsets.Emplace(
			Stream.FRandRange(-OffsetSpread, OffsetSpread),
			Stream.FRandRange(-OffsetSpread, OffsetSpread));
	}

	MountainMaskOffset.Set(
		Stream.FRandRange(-OffsetSpread, OffsetSpread),
		Stream.FRandRange(-OffsetSpread, OffsetSpread));

	ContinentOffset.Set(
		Stream.FRandRange(-OffsetSpread, OffsetSpread),
		Stream.FRandRange(-OffsetSpread, OffsetSpread));

	WarpOffsetA.Set(
		Stream.FRandRange(-OffsetSpread, OffsetSpread),
		Stream.FRandRange(-OffsetSpread, OffsetSpread));

	WarpOffsetB.Set(
		Stream.FRandRange(-OffsetSpread, OffsetSpread),
		Stream.FRandRange(-OffsetSpread, OffsetSpread));

	const int32 Plates = FMath::Max(2, Params.PlateCount);
	PlateCenters.Reset(Plates);
	for (int32 Plate = 0; Plate < Plates; ++Plate)
	{
		PlateCenters.Emplace(Stream.FRand(), Stream.FRand());
	}
}

float UWorldseedNoise::SampleHeight01Fast(const FWorldseedTerrainParams& Params,
	const FWorldseedNoiseContext& Ctx, float U, float V)
{
	// --- domain warping ---------------------------------------------------
	// On echantillonne le relief a des coordonnees elles-memes deplacees par un
	// second champ de bruit. Vingt lignes qui transforment des taches rondes en
	// plis et coulees d'allure geologique.
	if (Params.WarpStrength > KINDA_SMALL_NUMBER)
	{
		const float F = Params.WarpFrequency;
		const FVector2D PosA(U * F + Ctx.WarpOffsetA.X, V * F + Ctx.WarpOffsetA.Y);
		const FVector2D PosB(U * F + Ctx.WarpOffsetB.X, V * F + Ctx.WarpOffsetB.Y);

		U += FMath::PerlinNoise2D(PosA) * Params.WarpStrength;
		V += FMath::PerlinNoise2D(PosB) * Params.WarpStrength;
	}

	const float Plains = FBM(Params, Ctx, U, V);
	const float Mountains = Ridged(Params, Ctx, U, V);

	// Masque basse frequence : decide OU se trouvent les massifs, pour eviter
	// un relief uniformement montagneux.
	const FVector2D MaskPos(U * 1.3f + Ctx.MountainMaskOffset.X,
		V * 1.3f + Ctx.MountainMaskOffset.Y);
	const float Mask = FMath::Clamp(FMath::PerlinNoise2D(MaskPos) * 0.5f + 0.5f, 0.0f, 1.0f);

	const float Blend = FMath::Clamp(Mask * Params.MountainAmount, 0.0f, 1.0f);
	float Height = FMath::Lerp(Plains, Mountains, Blend);

	// Normalise vers [0..1] avant les etapes suivantes.
	Height = FMath::Clamp(Height * 0.5f + 0.5f, 0.0f, 1.0f);

	// --- tectonique -------------------------------------------------------
	if (Params.TectonicAmount > KINDA_SMALL_NUMBER)
	{
		const float Uplift = TectonicUplift(Params, Ctx, U, V);
		Height = FMath::Clamp(Height + Uplift * Params.TectonicAmount, 0.0f, 1.0f);
	}

	// --- attenuation continentale ----------------------------------------
	if (Params.ContinentFalloff > KINDA_SMALL_NUMBER)
	{
		// Distance de Tchebychev au centre : attenue les bords de facon carree,
		// ce qui suit la forme de la carte plutot qu'un disque.
		const float DX = FMath::Abs(U - 0.5f) * 2.0f;
		const float DY = FMath::Abs(V - 0.5f) * 2.0f;
		const float Edge = FMath::Max(DX, DY);

		// Bord adouci par un bruit, sinon la cote est parfaitement rectiligne.
		const FVector2D CoastPos(U * 3.0f + Ctx.ContinentOffset.X,
			V * 3.0f + Ctx.ContinentOffset.Y);
		const float Jitter = FMath::PerlinNoise2D(CoastPos) * 0.12f;

		const float Falloff = FMath::Clamp(1.0f - FMath::Pow(
			FMath::Clamp(Edge + Jitter, 0.0f, 1.0f), 3.0f), 0.0f, 1.0f);

		Height = FMath::Lerp(Height, Height * Falloff, Params.ContinentFalloff);
	}

	return FMath::Clamp(Height, 0.0f, 1.0f);
}

float UWorldseedNoise::SampleHeight01(const FWorldseedTerrainParams& Params, float U, float V)
{
	FWorldseedNoiseContext Ctx;
	Ctx.Init(Params);
	return SampleHeight01Fast(Params, Ctx, U, V);
}

void UWorldseedNoise::GenerateHeightfield(const FWorldseedTerrainParams& Params,
	TArray<float>& OutHeightsMeters)
{
	const int32 Res = FMath::Max(2, Params.Resolution);
	const double StartTime = FPlatformTime::Seconds();

	FWorldseedNoiseContext Ctx;
	Ctx.Init(Params);

	OutHeightsMeters.SetNumUninitialized(Res * Res);

	const float InvRes = 1.0f / static_cast<float>(Res - 1);

	for (int32 Y = 0; Y < Res; ++Y)
	{
		const float V = Y * InvRes;
		for (int32 X = 0; X < Res; ++X)
		{
			const float U = X * InvRes;
			OutHeightsMeters[Y * Res + X] =
				SampleHeight01Fast(Params, Ctx, U, V) * Params.HeightScale;
		}
	}

	// REPLI SEULEMENT. La vraie chaine — tectonique, climat, erosion — vit dans
	// WorldseedPipeline et s appuie sur world_rules.json. Cette fonction ne sert
	// plus que si les regles sont introuvables.
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] heightfield de repli %dx%d  seed=%d  (%.0f ms)"),
		Res, Res, Params.Seed, (FPlatformTime::Seconds() - StartTime) * 1000.0);
}

float UWorldseedNoise::SampleHeightfieldBilinear(const TArray<float>& Heights,
	int32 Resolution, float U, float V)
{
	if (Resolution < 2 || Heights.Num() != Resolution * Resolution)
	{
		return 0.0f;
	}

	const float FX = FMath::Clamp(U, 0.0f, 1.0f) * (Resolution - 1);
	const float FY = FMath::Clamp(V, 0.0f, 1.0f) * (Resolution - 1);

	const int32 X0 = FMath::Clamp(static_cast<int32>(FX), 0, Resolution - 2);
	const int32 Y0 = FMath::Clamp(static_cast<int32>(FY), 0, Resolution - 2);
	const float TX = FX - X0;
	const float TY = FY - Y0;

	const float H00 = Heights[Y0 * Resolution + X0];
	const float H10 = Heights[Y0 * Resolution + X0 + 1];
	const float H01 = Heights[(Y0 + 1) * Resolution + X0];
	const float H11 = Heights[(Y0 + 1) * Resolution + X0 + 1];

	return H00 * (1 - TX) * (1 - TY)
		 + H10 * TX * (1 - TY)
		 + H01 * (1 - TX) * TY
		 + H11 * TX * TY;
}

namespace
{
	/**
	 * Teinte hypsometrique : convention cartographique classique, du vert des
	 * plaines au blanc des sommets. Rend l'altitude lisible sans legende.
	 */
	FLinearColor HypsometricTint(float Height01)
	{
		struct FStop { float At; FLinearColor Color; };
		static const FStop Stops[] = {
			{ 0.00f, FLinearColor(0.16f, 0.30f, 0.16f) },   // plaines basses
			{ 0.30f, FLinearColor(0.34f, 0.46f, 0.20f) },   // collines
			{ 0.52f, FLinearColor(0.54f, 0.47f, 0.28f) },   // contreforts
			{ 0.72f, FLinearColor(0.52f, 0.46f, 0.44f) },   // roche
			{ 0.88f, FLinearColor(0.72f, 0.70f, 0.70f) },   // haute montagne
			{ 1.00f, FLinearColor(0.98f, 0.98f, 1.00f) },   // neige
		};
		constexpr int32 StopCount = UE_ARRAY_COUNT(Stops);

		const float T = FMath::Clamp(Height01, 0.0f, 1.0f);
		for (int32 I = 0; I < StopCount - 1; ++I)
		{
			if (T <= Stops[I + 1].At)
			{
				const float Span = FMath::Max(Stops[I + 1].At - Stops[I].At, KINDA_SMALL_NUMBER);
				const float Alpha = (T - Stops[I].At) / Span;
				return FMath::Lerp(Stops[I].Color, Stops[I + 1].Color, Alpha);
			}
		}
		return Stops[StopCount - 1].Color;
	}
}

UTexture2D* UWorldseedNoise::CreatePreviewTextureFromHeightfield(
	const TArray<float>& Heights, int32 Resolution, float MapSizeMeters,
	int32 PreviewResolution)
{
	const int32 Res = FMath::Clamp(PreviewResolution, 16, 2048);

	UTexture2D* Texture = UTexture2D::CreateTransient(Res, Res, PF_B8G8R8A8);
	if (!Texture)
	{
		return nullptr;
	}

	Texture->SRGB = true;
	Texture->CompressionSettings = TC_EditorIcon;
	Texture->Filter = TF_Bilinear;
	// Pas d'AddToRoot : l'appelant conserve la texture dans une UPROPERTY, ce
	// qui suffit au GC. L'enraciner ici fuirait un apercu a chaque changement
	// de seed, sans jamais le liberer.

	// Normalise sur l'amplitude reelle du terrain : apres erosion le maximum
	// n'est plus HeightScale, et un apercu tout gris ne servirait a rien.
	float MinHeight = BIG_NUMBER;
	float MaxHeight = -BIG_NUMBER;
	for (const float H : Heights)
	{
		MinHeight = FMath::Min(MinHeight, H);
		MaxHeight = FMath::Max(MaxHeight, H);
	}
	const float Range = FMath::Max(MaxHeight - MinHeight, KINDA_SMALL_NUMBER);

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	uint8* Pixels = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));

	const float InvRes = 1.0f / static_cast<float>(Res - 1);

	// Pas d'echantillonnage pour le gradient : une cellule du heightfield, pas
	// une cellule d'apercu, sinon le relief serait lisse par la reduction.
	const float StepUV = 1.0f / FMath::Max(static_cast<float>(Resolution - 1), 1.0f);
	const float CellMeters = FMath::Max(
		MapSizeMeters / FMath::Max(static_cast<float>(Resolution - 1), 1.0f),
		KINDA_SMALL_NUMBER);

	// Lumiere rasante venant du nord-ouest : c'est l'eclairage conventionnel
	// des cartes en relief, celui qui fait ressortir les vallees.
	const FVector LightDirection = FVector(-0.6f, 0.6f, 0.53f).GetSafeNormal();

	for (int32 Y = 0; Y < Res; ++Y)
	{
		const float V = Y * InvRes;
		for (int32 X = 0; X < Res; ++X)
		{
			const float U = X * InvRes;

			const float Height = SampleHeightfieldBilinear(Heights, Resolution, U, V);
			const float Height01 = FMath::Clamp((Height - MinHeight) / Range, 0.0f, 1.0f);

			// --- ombrage de relief ---------------------------------------
			const float HL = SampleHeightfieldBilinear(Heights, Resolution, U - StepUV, V);
			const float HR = SampleHeightfieldBilinear(Heights, Resolution, U + StepUV, V);
			const float HD = SampleHeightfieldBilinear(Heights, Resolution, U, V - StepUV);
			const float HU = SampleHeightfieldBilinear(Heights, Resolution, U, V + StepUV);

			const FVector Normal = FVector(
				-(HR - HL) / (2.0f * CellMeters),
				-(HU - HD) / (2.0f * CellMeters),
				1.0f).GetSafeNormal();

			// Ambiante volontairement haute : on veut lire la forme, pas
			// perdre les versants a l'ombre dans du noir.
			const float Lambert = FMath::Max(FVector::DotProduct(Normal, LightDirection), 0.0f);
			const float Shade = 0.35f + 0.65f * Lambert;

			const FLinearColor Color = HypsometricTint(Height01) * Shade;

			const int32 Index = (Y * Res + X) * 4;
			Pixels[Index + 0] = static_cast<uint8>(FMath::Clamp(Color.B, 0.0f, 1.0f) * 255.0f);
			Pixels[Index + 1] = static_cast<uint8>(FMath::Clamp(Color.G, 0.0f, 1.0f) * 255.0f);
			Pixels[Index + 2] = static_cast<uint8>(FMath::Clamp(Color.R, 0.0f, 1.0f) * 255.0f);
			Pixels[Index + 3] = 255;
		}
	}

	Mip.BulkData.Unlock();
	Texture->UpdateResource();

	return Texture;
}

UTexture2D* UWorldseedNoise::CreatePreviewTexture(const FWorldseedTerrainParams& Params,
	int32 PreviewResolution)
{
	TArray<float> Heights;
	GenerateHeightfield(Params, Heights);
	return CreatePreviewTextureFromHeightfield(
		Heights, Params.Resolution, Params.MapSizeMeters, PreviewResolution);
}

float UWorldseedNoise::MapVToLatitude(float V, float LatitudeSpanDegrees)
{
	// V = 0 en haut de carte (nord), V = 1 en bas (sud).
	return (0.5f - FMath::Clamp(V, 0.0f, 1.0f)) * LatitudeSpanDegrees;
}
