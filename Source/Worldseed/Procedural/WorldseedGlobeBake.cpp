// Worldseed - cuisson du monde en textures equirectangulaires pour le globe.

#include "Procedural/WorldseedGlobeBake.h"

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedGrid.h"

#include "Async/ParallelFor.h"
#include "Engine/Texture2D.h"

namespace WorldseedGlobeBake
{
	namespace
	{
		constexpr int32 BytesPerPixel = 4;

		/** Degrade d'altitude, quand le monde n'a pas encore de biomes. */
		FLinearColor LandTint(float Height01)
		{
			struct FStop { float At; FLinearColor Color; };
			static const FStop Stops[] = {
				{ 0.00f, FLinearColor(0.18f, 0.34f, 0.18f) },
				{ 0.25f, FLinearColor(0.36f, 0.47f, 0.21f) },
				{ 0.50f, FLinearColor(0.55f, 0.48f, 0.29f) },
				{ 0.75f, FLinearColor(0.53f, 0.47f, 0.44f) },
				{ 1.00f, FLinearColor(0.97f, 0.97f, 1.00f) },
			};
			constexpr int32 StopCount = UE_ARRAY_COUNT(Stops);

			const float T = FMath::Clamp(Height01, 0.0f, 1.0f);
			for (int32 I = 0; I < StopCount - 1; ++I)
			{
				if (T <= Stops[I + 1].At)
				{
					const float Span = FMath::Max(
						Stops[I + 1].At - Stops[I].At, KINDA_SMALL_NUMBER);
					return FMath::Lerp(Stops[I].Color, Stops[I + 1].Color,
						(T - Stops[I].At) / Span);
				}
			}
			return Stops[StopCount - 1].Color;
		}

		FLinearColor OceanTint(float Depth01)
		{
			return FMath::Lerp(FLinearColor(0.05f, 0.14f, 0.30f),
				FLinearColor(0.16f, 0.38f, 0.58f),
				FMath::Clamp(Depth01, 0.0f, 1.0f));
		}

		UTexture2D* MakeTexture(UObject* Outer, const TCHAR* Name, int32 Width, int32 Height,
			bool bSrgb, const TArray<uint8>& Pixels)
		{
			UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
			if (!Texture)
			{
				return nullptr;
			}

			Texture->SRGB = bSrgb;
			Texture->CompressionSettings = bSrgb ? TC_EditorIcon : TC_VectorDisplacementmap;
			Texture->Filter = TF_Bilinear;

			// La longitude s'enroule, la latitude non : un globe coupe au
			// meridien montrerait une couture, et un globe qui reboucle aux
			// poles y melerait les deux calottes.
			Texture->AddressX = TA_Wrap;
			Texture->AddressY = TA_Clamp;
			Texture->NeverStream = true;

			FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
			void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num());
			Mip.BulkData.Unlock();

			Texture->UpdateResource();
			return Texture;
		}
	}

	FWorldseedGlobeTextures Build(UObject* Outer, const TArray<float>& Heights,
		const TArray<uint8>& BiomeIndex, const TArray<uint8>& Cover,
		const FWorldseedGeometry& Geometry, const FWorldseedGlobeBakeSettings& Settings)
	{
		FWorldseedGlobeTextures Out;

		const int32 Cells = Geometry.CellCount();
		if (Geometry.NX < 2 || Heights.Num() != Cells)
		{
			return Out;
		}

		const double StartTime = FPlatformTime::Seconds();

		const int32 Width = FMath::Clamp(Settings.Width, 64, 4096);
		const int32 Height = FMath::Max(Width / 2, 32);

		// Altitude maximale REELLE : apres erosion le sommet n'est plus la
		// valeur theorique, et normaliser dessus ecraserait tout le relief.
		float MaxLand = KINDA_SMALL_NUMBER;
		for (const float H : Heights)
		{
			MaxLand = FMath::Max(MaxLand, H);
		}
		const float SnowScale = FMath::Max(Settings.SnowStartM, MaxLand * 0.75f);

		const bool bHasBiomes = (BiomeIndex.Num() == Cells);
		const bool bHasCover = (Cover.Num() == Cells);

		TArray<uint8> AlbedoPixels;
		TArray<uint8> NormalPixels;
		AlbedoPixels.SetNumUninitialized(Width * Height * BytesPerPixel);
		NormalPixels.SetNumUninitialized(Width * Height * BytesPerPixel);

		const float CellMeters = FMath::Max(Geometry.MetersPerPixel(), 0.01f);
		const float StepU = 1.0f / static_cast<float>(Geometry.NX);
		const float StepV = 1.0f / FMath::Max(static_cast<float>(Geometry.NY - 1), 1.0f);

		// LA TEXTURE EST RANGEE EN LATITUDE LINEAIRE, pas dans la projection du
		// generateur. C'est deliberé : le materiau n'a alors qu'une division a
		// faire pour retrouver sa ligne, la ou rejouer la projection
		// equivalente de Lambert en HLSL dupliquerait une formule qui ne doit
		// exister qu'a un seul endroit — et une divergence entre les deux
		// versions decalerait silencieusement toutes les bandes climatiques.
		//
		// La conversion se fait donc ICI, en C++, une fois par ligne.
		const float HalfSpanDeg = FMath::Max(Geometry.LatSpanDeg * 0.5f, 1e-3f);

		ParallelFor(Height, [&](int32 Row)
		{
			const float Along = static_cast<float>(Row) / FMath::Max(Height - 1, 1);
			const float LatitudeDeg = FMath::Lerp(-HalfSpanDeg, HalfSpanDeg, Along);

			// ... et c'est la geometrie qui sait ou lire cette latitude.
			const float V = Geometry.VForLatitudeDeg(LatitudeDeg);

			// Borne au pole : cos(phi) tend vers zero et la pente zonale
			// divergerait.
			const float CosLat = FMath::Max(
				FMath::Cos(FMath::DegreesToRadians(LatitudeDeg)), 0.05f);

			// LA PROJECTION EQUIVALENTE DEFORME LES DEUX AXES EN SENS INVERSE :
			// un pas de longitude couvre cos(phi) fois MOINS de metres (les
			// meridiens convergent), un pas de latitude 1/cos(phi) fois PLUS
			// (l'equivalent-aire comprime les hautes latitudes). Ignorer ces
			// facteurs donne un ombrage dont l'intensite varie avec la latitude
			// et qui traine des stries.
			const float SpanEastM = CellMeters * CosLat;
			const float SpanNorthM = CellMeters / CosLat;

			for (int32 Col = 0; Col < Width; ++Col)
			{
				const float U = static_cast<float>(Col) / static_cast<float>(Width);

				const float H = WorldseedGrid::SampleUV(
					Heights, Geometry.NX, Geometry.NY, U, V);

				// --- couleur ------------------------------------------------
				FLinearColor Colour;
				if (bHasBiomes)
				{
					const int32 Col0 = FMath::Clamp(
						FMath::RoundToInt(U * Geometry.NX), 0, Geometry.NX - 1);
					const int32 Row0 = FMath::Clamp(
						FMath::RoundToInt(V * (Geometry.NY - 1)), 0, Geometry.NY - 1);
					const int32 Cell = Row0 * Geometry.NX + Col0;

					Colour = WorldseedBiomes::Colour(
						static_cast<EWorldseedBiome>(BiomeIndex[Cell]));

					if (bHasCover)
					{
						const EWorldseedCover C = static_cast<EWorldseedCover>(Cover[Cell]);
						if (C != EWorldseedCover::None)
						{
							Colour = FMath::Lerp(Colour,
								WorldseedBiomes::CoverColour(C), 0.75f);
						}
					}
				}
				else if (H < 0.0f)
				{
					const float Depth01 = 1.0f - FMath::Clamp(
						H / FMath::Min(Settings.DeepOceanM, -1.0f), 0.0f, 1.0f);
					Colour = OceanTint(Depth01);
				}
				else
				{
					Colour = LandTint(H / SnowScale);
				}

				// --- normale, en distances REELLES --------------------------
				const float HL = WorldseedGrid::SampleUV(Heights, Geometry.NX, Geometry.NY, U - StepU, V);
				const float HR = WorldseedGrid::SampleUV(Heights, Geometry.NX, Geometry.NY, U + StepU, V);
				const float HD = WorldseedGrid::SampleUV(Heights, Geometry.NX, Geometry.NY, U, V - StepV);
				const float HU = WorldseedGrid::SampleUV(Heights, Geometry.NX, Geometry.NY, U, V + StepV);

				const float SlopeEast =
					(HR - HL) / (2.0f * SpanEastM) * Settings.ReliefStrength;
				const float SlopeNorth =
					(HU - HD) / (2.0f * SpanNorthM) * Settings.ReliefStrength;

				// Repere tangent : X vers l'est, Y vers le nord, Z vers le haut.
				const FVector Normal =
					FVector(-SlopeEast, -SlopeNorth, 1.0f).GetSafeNormal();

				const int32 Index = (Row * Width + Col) * BytesPerPixel;

				// Ordre BGRA, impose par PF_B8G8R8A8.
				AlbedoPixels[Index + 0] = static_cast<uint8>(FMath::Clamp(Colour.B, 0.0f, 1.0f) * 255.0f);
				AlbedoPixels[Index + 1] = static_cast<uint8>(FMath::Clamp(Colour.G, 0.0f, 1.0f) * 255.0f);
				AlbedoPixels[Index + 2] = static_cast<uint8>(FMath::Clamp(Colour.R, 0.0f, 1.0f) * 255.0f);

				// L'alpha porte l'appartenance a la TERRE : le materiau en tire
				// le trait de cote sans avoir a relire une altitude.
				AlbedoPixels[Index + 3] = (H > 0.0f) ? 255 : 0;

				NormalPixels[Index + 0] = static_cast<uint8>((Normal.Z * 0.5 + 0.5) * 255.0);
				NormalPixels[Index + 1] = static_cast<uint8>((Normal.Y * 0.5 + 0.5) * 255.0);
				NormalPixels[Index + 2] = static_cast<uint8>((Normal.X * 0.5 + 0.5) * 255.0);
				NormalPixels[Index + 3] = 255;
			}
		});

		Out.Albedo = MakeTexture(Outer, TEXT("GlobeAlbedo"), Width, Height, true, AlbedoPixels);
		Out.Normal = MakeTexture(Outer, TEXT("GlobeNormal"), Width, Height, false, NormalPixels);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] globe cuit : %dx%d depuis une grille %dx%d, biomes=%d  (%.0f ms)"),
			Width, Height, Geometry.NX, Geometry.NY, bHasBiomes ? 1 : 0,
			(FPlatformTime::Seconds() - StartTime) * 1000.0);

		return Out;
	}
}
