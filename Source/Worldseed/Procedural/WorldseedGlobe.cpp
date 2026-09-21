// Worldseed - rendu du monde sous forme de globe, pour l'ecran d'entree.

#include "Procedural/WorldseedGlobe.h"
#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedGrid.h"

#include "Async/ParallelFor.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"

namespace WorldseedGlobe
{
	namespace
	{
		/** Fond : un gris tres sombre, neutre, qui ne concurrence pas le globe. */
		const FLinearColor SpaceColor(0.035f, 0.040f, 0.055f);

		/** Bathymetrie : du bleu profond au turquoise cotier. */
		FLinearColor OceanTint(float Depth01)
		{
			const FLinearColor Deep(0.03f, 0.09f, 0.24f);
			const FLinearColor Shallow(0.13f, 0.42f, 0.58f);
			return FMath::Lerp(Deep, Shallow, FMath::Clamp(Depth01, 0.0f, 1.0f));
		}

		/** Teinte des terres, du vert des plaines au blanc des sommets. */
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
			constexpr int32 Count = UE_ARRAY_COUNT(Stops);

			const float T = FMath::Clamp(Height01, 0.0f, 1.0f);
			for (int32 I = 0; I < Count - 1; ++I)
			{
				if (T <= Stops[I + 1].At)
				{
					const float Span = FMath::Max(Stops[I + 1].At - Stops[I].At, KINDA_SMALL_NUMBER);
					return FMath::Lerp(Stops[I].Color, Stops[I + 1].Color, (T - Stops[I].At) / Span);
				}
			}
			return Stops[Count - 1].Color;
		}
	}

	namespace
	{
	void FillPixels(uint8* Pixels, int32 Res, const TArray<float>& Heights,
		const FWorldseedGeometry& Geometry,
		const FGlobeSettings& Settings, const TArray<uint8>* BiomeIndex)
	{
		// Les biomes ne servent que s'ils decrivent LA MEME grille : une carte
		// d'une autre resolution peindrait des couleurs decalees, et rien ne
		// le signalerait.
		const bool bHasBiomes = (BiomeIndex != nullptr)
			&& (BiomeIndex->Num() == Heights.Num());

		// Altitude maximale reelle : apres erosion le sommet n'est plus la
		// valeur theorique, et normaliser dessus ecraserait tout le relief.
		float MaxLand = KINDA_SMALL_NUMBER;
		for (const float H : Heights)
		{
			MaxLand = FMath::Max(MaxLand, H);
		}
		const float SnowScale = FMath::Max(Settings.SnowStartM, MaxLand * 0.75f);

		// Lumiere haute a gauche : convention des globes, elle laisse le
		// terminateur sur la droite et donne du volume sans noyer de face.
		const FVector LightDirection = FVector(-0.45f, 0.35f, 0.82f).GetSafeNormal();

		const float TiltRad = FMath::DegreesToRadians(Settings.TiltDeg);
		const float CosTilt = FMath::Cos(TiltRad);
		const float SinTilt = FMath::Sin(TiltRad);

		// Axe polaire exprime dans le repere de la camera : image de (0,1,0)
		// par l inverse de la bascule. Sert a construire le repere tangent.
		const FVector PolarView(0.0f, CosTilt, -SinTilt);

		// Cote d une cellule de carte, en metres. Identique sur les deux axes
		// par construction (NX = 2 NY, largeur = 2 hauteur).
		const float CellMeters = FMath::Max(Geometry.MetersPerPixel(), 0.01f);

		// Pas d'echantillonnage exprime separement sur chaque axe : une cellule
		// couvre le meme nombre de METRES dans les deux directions, mais pas le
		// meme nombre de degres, ni la meme fraction de carte.
		const float StepU = 1.0f / static_cast<float>(Geometry.NX);
		const float StepV = 1.0f / FMath::Max(static_cast<float>(Geometry.NY - 1), 1.0f);
		const float InvHalf = 2.0f / static_cast<float>(Res - 1);

		ParallelFor(Res, [&](int32 PY)
		{
			// Ecran : Y vers le haut, donc on inverse la ligne.
			const float ScreenY = 1.0f - static_cast<float>(PY) * InvHalf;

			for (int32 PX = 0; PX < Res; ++PX)
			{
				const float ScreenX = static_cast<float>(PX) * InvHalf - 1.0f;

				// Marge pour que le disque ne touche pas les bords.
				const float R = 0.92f;
				const float NX = ScreenX / R;
				const float NY = ScreenY / R;
				const float R2 = NX * NX + NY * NY;

				FLinearColor Color = SpaceColor;

				if (R2 <= 1.0f)
				{
					// Point de la sphere unite face a l'observateur.
					const float NZ = FMath::Sqrt(FMath::Max(0.0f, 1.0f - R2));
					const FVector Normal(NX, NY, NZ);

					// On bascule l'axe polaire de TiltDeg vers l'observateur :
					// sans cela on ne verrait jamais un pole, donc jamais la
					// calotte, qui est justement ce qu'on veut verifier.
					const float AxisY = Normal.Y * CosTilt - Normal.Z * SinTilt;
					const float AxisZ = Normal.Y * SinTilt + Normal.Z * CosTilt;

					const float LatitudeDeg = FMath::RadiansToDegrees(
						FMath::Asin(FMath::Clamp(AxisY, -1.0f, 1.0f)));

					float LongitudeDeg = FMath::RadiansToDegrees(
						FMath::Atan2(Normal.X, AxisZ)) + Settings.LongitudeOffsetDeg;
					LongitudeDeg = FMath::Fmod(FMath::Fmod(LongitudeDeg, 360.0f) + 360.0f, 360.0f);

					const float U = LongitudeDeg / 360.0f;
					const float V = Geometry.VForLatitudeDeg(LatitudeDeg);

					const float Height = WorldseedGrid::SampleUV(Heights, Geometry.NX, Geometry.NY, U, V);

					// --- teinte -------------------------------------------
					if (Height < 0.0f)
					{
						const float Depth01 = 1.0f - FMath::Clamp(
							Height / FMath::Min(Settings.DeepOceanM, -1.0f), 0.0f, 1.0f);
						Color = OceanTint(Depth01);
					}
					else
					{
						// LA COULEUR VIENT DU BIOME QUAND ON L'A. L'ombrage,
						// lui, est applique plus bas dans les deux cas : c'est
						// le relief qui rend le globe lisible.
						if (bHasBiomes)
						{
							// PLUS PROCHE VOISIN, jamais d'interpolation : la
							// moyenne de deux identifiants est un biome qui
							// n'existe nulle part.
							const int32 CellX = FMath::Clamp(
								static_cast<int32>(U * Geometry.NX), 0, Geometry.NX - 1);
							const int32 CellY = FMath::Clamp(
								static_cast<int32>(V * Geometry.NY), 0, Geometry.NY - 1);
							Color = WorldseedBiomes::Colour(static_cast<EWorldseedBiome>(
								(*BiomeIndex)[CellY * Geometry.NX + CellX]));
						}
						else
						{
							Color = LandTint(Height / SnowScale);
						}
					}

					// --- normale du terrain, en distances REELLES ---------
					//
					// La projection equivalente deforme les deux axes en sens
					// inverse : un pas de longitude couvre cos(phi) fois MOINS
					// de metres (les meridiens convergent), un pas de latitude
					// 1/cos(phi) fois PLUS (l equivalent-aire comprime les
					// hautes latitudes). Ignorer ces facteurs — ou pire, sommer
					// les deux differences avec un coefficient arbitraire —
					// donne un ombrage dont l intensite varie avec la latitude
					// et qui traine des stries.
					const float HL = WorldseedGrid::SampleUV(Heights, Geometry.NX, Geometry.NY, U - StepU, V);
					const float HR = WorldseedGrid::SampleUV(Heights, Geometry.NX, Geometry.NY, U + StepU, V);
					const float HD = WorldseedGrid::SampleUV(Heights, Geometry.NX, Geometry.NY, U, V - StepV);
					const float HU = WorldseedGrid::SampleUV(Heights, Geometry.NX, Geometry.NY, U, V + StepV);

					// Borne au pole : cos(phi) tend vers zero et la pente
					// zonale divergerait.
					const float CosLat = FMath::Max(
						FMath::Cos(FMath::DegreesToRadians(LatitudeDeg)), 0.05f);

					const float SpanEastM = CellMeters * CosLat;
					const float SpanNorthM = CellMeters / CosLat;

					// Pentes vraies, sans dimension : metres par metre.
					const float SlopeEast = (HR - HL) / (2.0f * SpanEastM) * Settings.ReliefStrength;
					const float SlopeNorth = (HU - HD) / (2.0f * SpanNorthM) * Settings.ReliefStrength;

					// Repere tangent local. L axe polaire vu dans le repere de
					// la camera : c est l inverse de la bascule appliquee plus
					// haut pour lire la latitude.
					const FVector Up = Normal;
					FVector East = FVector::CrossProduct(PolarView, Up).GetSafeNormal();
					if (East.IsNearlyZero())
					{
						East = FVector(1.0f, 0.0f, 0.0f);   // exactement au pole
					}
					const FVector North = FVector::CrossProduct(Up, East);

					// La normale du terrain contient DEJA la courbure de la
					// sphere, par sa composante Up : un seul Lambert suffit,
					// la modulation separee d avant faisait double emploi.
					const FVector TerrainNormal =
						(East * -SlopeEast + North * -SlopeNorth + Up).GetSafeNormal();

					const float Lambert = FMath::Max(
						FVector::DotProduct(TerrainNormal, LightDirection), 0.0f);
					Color = Color * (0.22f + 0.88f * Lambert);

					// --- limbe : assombrit le bord, donne la rondeur -------
					Color = Color * (0.55f + 0.45f * NZ);

					// --- reperes de latitude ------------------------------
					if (Settings.bShowLatitudeLines)
					{
						// Epaisseur constante a l'ecran : on la mesure en
						// degres rapportes a la derivee locale de la latitude.
						const float Thickness = 0.9f
							/ FMath::Max(static_cast<float>(Res) * 0.5f * FMath::Abs(CosTilt), 1.0f)
							* 180.0f;

						auto NearLine = [LatitudeDeg, Thickness](float Target) -> bool
						{
							return FMath::Abs(FMath::Abs(LatitudeDeg) - Target) < Thickness;
						};

						if (FMath::Abs(LatitudeDeg) < Thickness)
						{
							Color = FMath::Lerp(Color, FLinearColor(1.0f, 0.85f, 0.35f), 0.65f);
						}
						else if (NearLine(Settings.TropicDeg))
						{
							Color = FMath::Lerp(Color, FLinearColor(1.0f, 0.65f, 0.30f), 0.45f);
						}
						else if (NearLine(Settings.PolarCircleDeg))
						{
							Color = FMath::Lerp(Color, FLinearColor(0.55f, 0.80f, 1.0f), 0.45f);
						}
					}
				}

				// --- L'ESPACE EST TRANSPARENT, PAS SOMBRE -----------------
				//
				// L'alpha etait fige a 255 : la texture portait donc un CARRE
				// opaque autour du disque. Cela ne se voyait pas tant que le
				// globe tenait dans 420 pixels au milieu d'un panneau de meme
				// teinte ; des qu'il a pris tout le corps de l'ecran, le carre
				// est devenu une boite posee sur le fond.
				//
				// LE BORD EST FONDU SUR UN PIXEL. Un disque de sept cents
				// pixels coupe net montre son escalier, et c'est la premiere
				// chose qu'on voit sur une forme ronde. `InvHalf` vaut deux
				// sur la resolution, donc un pixel vaut `InvHalf / R` dans les
				// coordonnees normalisees ou le rayon vaut 1.
				const float Rayon = FMath::Sqrt(R2);
				const float Pixel = InvHalf / R;
				const float Opacite = FMath::Clamp((1.0f - Rayon) / Pixel, 0.0f, 1.0f);

				const int32 Index = (PY * Res + PX) * 4;
				Pixels[Index + 0] = static_cast<uint8>(FMath::Clamp(Color.B, 0.0f, 1.0f) * 255.0f);
				Pixels[Index + 1] = static_cast<uint8>(FMath::Clamp(Color.G, 0.0f, 1.0f) * 255.0f);
				Pixels[Index + 2] = static_cast<uint8>(FMath::Clamp(Color.R, 0.0f, 1.0f) * 255.0f);
				Pixels[Index + 3] = static_cast<uint8>(Opacite * 255.0f);
			}
		});

	}
	}

	UTexture2D* Render(const TArray<float>& Heights,
		const FWorldseedGeometry& Geometry, const FGlobeSettings& Settings,
		int32 PreviewResolution, const TArray<uint8>* BiomeIndex)
	{
		const int32 Res = FMath::Clamp(PreviewResolution, 32, 2048);
		if (Geometry.NX < 2 || Heights.Num() != Geometry.CellCount())
		{
			return nullptr;
		}

		UTexture2D* Texture = UTexture2D::CreateTransient(Res, Res, PF_B8G8R8A8);
		if (!Texture)
		{
			return nullptr;
		}

		Texture->SRGB = true;
		Texture->CompressionSettings = TC_EditorIcon;
		Texture->Filter = TF_Bilinear;

		// Elle sera reecrite en place a chaque image : le systeme de streaming
		// n'a rien a faire ici, et pourrait relacher le mip sous nos pieds.
		Texture->NeverStream = true;

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		uint8* Pixels = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		FillPixels(Pixels, Res, Heights, Geometry, Settings, BiomeIndex);
		Mip.BulkData.Unlock();
		Texture->UpdateResource();

		return Texture;
	}

	bool RenderInto(UTexture2D* Texture, const TArray<float>& Heights,
		const FWorldseedGeometry& Geometry, const FGlobeSettings& Settings,
		const TArray<uint8>* BiomeIndex)
	{
		if (!Texture || Geometry.NX < 2 || Heights.Num() != Geometry.CellCount())
		{
			return false;
		}

		FTexturePlatformData* Data = Texture->GetPlatformData();
		if (!Data || Data->Mips.Num() == 0)
		{
			return false;
		}

		const int32 Res = Data->Mips[0].SizeX;
		if (Res != Data->Mips[0].SizeY)
		{
			return false;
		}

		// ON MET A JOUR LES PIXELS, PAS LA RESSOURCE.
		//
		// UpdateResource() ne pousse pas des pixels : il DETRUIT la ressource
		// RHI et en recree une. A trente images par seconde, cela fabriquait
		// trente textures GPU et trente destructions differees par seconde. Ces
		// destructions s'empilent dans une file que le moteur vide par a-coups
		// — d'ou une rotation fluide, puis une saccade, puis de nouveau fluide.
		// Le symptome empirait avec la taille du monde parce que le processus
		// portait alors des centaines de megaoctets de grilles, ce qui rend
		// chaque passe de nettoyage plus chere.
		//
		// UpdateTextureRegions, lui, se contente de televerser un rectangle
		// dans la texture DEJA EXISTANTE. Aucune allocation GPU, aucun dechet.
		const int32 BytesPerPixel = 4;
		const int32 Bytes = Res * Res * BytesPerPixel;

		// Le thread de rendu lira ce tampon APRES le retour de cette fonction :
		// il doit donc lui survivre, et c'est le rappel de nettoyage qui le
		// libere une fois le televersement fait.
		uint8* Pixels = new uint8[Bytes];
		FillPixels(Pixels, Res, Heights, Geometry, Settings, BiomeIndex);

		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Res, Res);

		Texture->UpdateTextureRegions(0, 1, Region,
			Res * BytesPerPixel, BytesPerPixel, Pixels,
			[](uint8* Data, const FUpdateTextureRegion2D* Regions)
			{
				delete[] Data;
				delete Regions;
			});

		return true;
	}
}
