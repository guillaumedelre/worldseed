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
	/**
	 * Estampe le reticule du point choisi, APRES le remplissage.
	 *
	 * Apres, et non pendant : demander a chaque pixel « suis-je dans le
	 * reticule ? » couterait la projection a un million de points pour en
	 * toucher deux cents. On projette le point UNE fois, puis on peint son
	 * voisinage -- c'est le meme renversement de sens de lecture qui avait
	 * fait passer le semis PCG de 32 Go a 22.
	 */
	void EstamperRepere(uint8* Pixels, int32 Res, const FGlobeSettings& Settings)
	{
		if (!Settings.Repere.bActif)
		{
			return;
		}

		const FCadreGlobe Cadre = CadreGlobe(Settings);
		float CadreX = 0.0f;
		float CadreY = 0.0f;

		// LA FACE CACHEE NE SE DESSINE PAS. Sans ce test, un point choisi de
		// l'autre cote de la planete se peindrait par-dessus le relief qui est
		// cense le masquer, et l'on conclurait a une projection fausse alors
		// qu'elle serait juste.
		if (!CadreDepuisLatLon(Settings.Repere.LatitudeDeg,
			Settings.Repere.LongitudeDeg, Cadre, CadreX, CadreY))
		{
			return;
		}

		float CX = 0.0f;
		float CY = 0.0f;
		PixelDepuisCadre(CadreX, CadreY, Res, CX, CY);

		// Le reticule DESIGNE un point, il n'en mesure pas l'etendue : sa
		// taille suit donc la texture et non le monde.
		const float RayonBlanc = FMath::Max(4.0f, Res * 0.014f);
		const float RayonSombre = RayonBlanc + FMath::Max(2.0f, Res * 0.004f);
		const int32 Portee = FMath::CeilToInt(RayonSombre) + 2;

		const int32 X0 = FMath::Max(0, FMath::FloorToInt(CX) - Portee);
		const int32 X1 = FMath::Min(Res - 1, FMath::CeilToInt(CX) + Portee);
		const int32 Y0 = FMath::Max(0, FMath::FloorToInt(CY) - Portee);
		const int32 Y1 = FMath::Min(Res - 1, FMath::CeilToInt(CY) + Portee);

		const float InvHalf = 2.0f / static_cast<float>(Res - 1);

		// Une bande vaut plein au centre et s'eteint sur un pixel de part et
		// d'autre : un anneau coupe net montre son escalier, comme le bord du
		// disque le montrait avant qu'on ne le fonde.
		auto Bande = [](float D, float Cible, float DemiLargeur) -> float
		{
			return FMath::Clamp(1.0f - (FMath::Abs(D - Cible) - DemiLargeur), 0.0f, 1.0f);
		};

		for (int32 PY = Y0; PY <= Y1; ++PY)
		{
			const float ScreenY = 1.0f - static_cast<float>(PY) * InvHalf;

			for (int32 PX = X0; PX <= X1; ++PX)
			{
				const float ScreenX = static_cast<float>(PX) * InvHalf - 1.0f;

				// LE RETICULE NE DEBORDE PAS DANS L'ESPACE. Pres du limbe,
				// l'anneau sortirait du disque et abimerait la silhouette --
				// la seule forme ronde de l'ecran, donc celle dont un defaut
				// se voit le plus.
				if (PointerCadre(ScreenX, ScreenY, Cadre).Rayon01 > 1.0f)
				{
					continue;
				}

				const float DX = static_cast<float>(PX) - CX;
				const float DY = static_cast<float>(PY) - CY;
				const float D = FMath::Sqrt(DX * DX + DY * DY);

				// Le halo sombre vient AVANT, pour que le blanc se detache
				// aussi bien sur une calotte que sur un ocean profond.
				const float Sombre = Bande(D, RayonSombre, 0.8f);
				const float Blanc = FMath::Max(
					Bande(D, RayonBlanc, 0.7f),
					FMath::Clamp(2.0f - D, 0.0f, 1.0f));

				if (Sombre <= 0.0f && Blanc <= 0.0f)
				{
					continue;
				}

				const int32 Index = (PY * Res + PX) * 4;
				auto Melanger = [&](int32 Canal, float Valeur, float Poids)
				{
					const float Avant = static_cast<float>(Pixels[Index + Canal]);
					Pixels[Index + Canal] = static_cast<uint8>(
						FMath::Clamp(FMath::Lerp(Avant, Valeur, Poids), 0.0f, 255.0f));
				};

				if (Sombre > 0.0f)
				{
					Melanger(0, 12.0f, Sombre);
					Melanger(1, 12.0f, Sombre);
					Melanger(2, 16.0f, Sombre);
				}
				if (Blanc > 0.0f)
				{
					Melanger(0, 235.0f, Blanc);
					Melanger(1, 245.0f, Blanc);
					Melanger(2, 255.0f, Blanc);
				}

				// Le reticule est OPAQUE meme la ou le bord du disque est
				// fondu : sinon il palit en approchant du limbe, precisement
				// la ou il est deja le plus difficile a viser.
				const float Couvre = FMath::Max(Sombre, Blanc);
				Pixels[Index + 3] = static_cast<uint8>(FMath::Max(
					static_cast<float>(Pixels[Index + 3]), Couvre * 255.0f));
			}
		}
	}

	void FillPixels(uint8* Pixels, int32 Res, const TArray<float>& Heights,
		const FWorldseedGeometry& Geometry,
		const FGlobeSettings& Settings, const TArray<uint8>* BiomeIndex,
		const TArray<uint8>* CoverIndex)
	{
		// Les biomes ne servent que s'ils decrivent LA MEME grille : une carte
		// d'une autre resolution peindrait des couleurs decalees, et rien ne
		// le signalerait.
		const bool bHasBiomes = (BiomeIndex != nullptr)
			&& (BiomeIndex->Num() == Heights.Num());
		const bool bHasCover = (CoverIndex != nullptr)
			&& (CoverIndex->Num() == Heights.Num());

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

		// LA BASCULE EST CALCULEE UNE FOIS ET PARTAGEE. C'est le meme objet que
		// le pointage a la souris et le repere emploient : la formule de
		// projection n'existe qu'a un seul endroit, dans l'en-tete.
		const FCadreGlobe Cadre = CadreGlobe(Settings);
		const float CosTilt = Cadre.CosTilt;
		const float SinTilt = Cadre.SinTilt;

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

				// TOUTE LA GEOMETRIE TIENT DANS CET APPEL. Le pointage a la
				// souris appelle la meme fonction, et le repere son inverse.
				const FPointeGlobe Pointe = PointerCadre(ScreenX, ScreenY, Cadre);
				const FVector& Normal = Pointe.Normale;

				FLinearColor Color = SpaceColor;

				if (Pointe.bSurLeGlobe)
				{
					const float LatitudeDeg = Pointe.LatitudeDeg;
					const float LongitudeDeg = Pointe.LongitudeDeg;

					const float U = LongitudeDeg / 360.0f;
					const float V = Geometry.VForLatitudeDeg(LatitudeDeg);

					const float Height = WorldseedGrid::SampleUV(Heights, Geometry.NX, Geometry.NY, U, V);

					// La cellule de carte, au PLUS PROCHE VOISIN : biomes et
					// couvertures sont des IDENTIFIANTS, et la moyenne de deux
					// identifiants designe ce qui n'existe nulle part.
					const int32 CellX = FMath::Clamp(
						static_cast<int32>(U * Geometry.NX), 0, Geometry.NX - 1);
					const int32 CellY = FMath::Clamp(
						static_cast<int32>(V * Geometry.NY), 0, Geometry.NY - 1);
					const int32 Cell = CellY * Geometry.NX + CellX;

					// --- teinte -------------------------------------------
					if (Height < 0.0f)
					{
						// LA MER PRISE EN GLACE. C'est la seule facon d'avoir du
						// blanc au pole NORD, qui n'a aucune terre par
						// construction -- northPole vaut "ocean", comme
						// l'Arctique -- et n'en aura jamais.
						const bool bGelee = bHasCover
							&& static_cast<EWorldseedCover>((*CoverIndex)[Cell])
								== EWorldseedCover::SeaIce;

						if (bGelee)
						{
							Color = WorldseedBiomes::CoverColour(EWorldseedCover::SeaIce);
						}
						else
						{
							const float Depth01 = 1.0f - FMath::Clamp(
								Height / FMath::Min(Settings.DeepOceanM, -1.0f), 0.0f, 1.0f);
							Color = OceanTint(Depth01);
						}
					}
					else if (bHasBiomes)
					{
						// LA COULEUR VIENT DU BIOME QUAND ON L'A. L'ombrage,
						// lui, est applique plus bas dans tous les cas : c'est
						// le relief qui rend le globe lisible.
						Color = WorldseedBiomes::Colour(
							static_cast<EWorldseedBiome>((*BiomeIndex)[Cell]));
					}
					else
					{
						Color = LandTint(Height / SnowScale);
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
					Color = Color * (0.55f + 0.45f * static_cast<float>(Normal.Z));

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
				const float Pixel = InvHalf / RayonDisque;
				const float Opacite = FMath::Clamp((1.0f - Pointe.Rayon01) / Pixel, 0.0f, 1.0f);

				const int32 Index = (PY * Res + PX) * 4;
				Pixels[Index + 0] = static_cast<uint8>(FMath::Clamp(Color.B, 0.0f, 1.0f) * 255.0f);
				Pixels[Index + 1] = static_cast<uint8>(FMath::Clamp(Color.G, 0.0f, 1.0f) * 255.0f);
				Pixels[Index + 2] = static_cast<uint8>(FMath::Clamp(Color.R, 0.0f, 1.0f) * 255.0f);
				Pixels[Index + 3] = static_cast<uint8>(Opacite * 255.0f);
			}
		});

		// HORS DE LA BOUCLE PARALLELE : le reticule ecrit dans des pixels que
		// plusieurs lignes se partagent, et il est bien trop petit pour que le
		// paralleliser rapporte quoi que ce soit.
		EstamperRepere(Pixels, Res, Settings);
	}
	}

	UTexture2D* Render(const TArray<float>& Heights,
		const FWorldseedGeometry& Geometry, const FGlobeSettings& Settings,
		int32 PreviewResolution, const TArray<uint8>* BiomeIndex,
		const TArray<uint8>* CoverIndex)
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
		FillPixels(Pixels, Res, Heights, Geometry, Settings, BiomeIndex, CoverIndex);
		Mip.BulkData.Unlock();
		Texture->UpdateResource();

		return Texture;
	}

	bool RenderInto(UTexture2D* Texture, const TArray<float>& Heights,
		const FWorldseedGeometry& Geometry, const FGlobeSettings& Settings,
		const TArray<uint8>* BiomeIndex, const TArray<uint8>* CoverIndex)
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
		FillPixels(Pixels, Res, Heights, Geometry, Settings, BiomeIndex, CoverIndex);

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
