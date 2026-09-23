// Worldseed - peindre une fenetre de la carte du monde.

#include "Procedural/WorldseedCarte.h"

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedTrace.h"

#include "Async/ParallelFor.h"

namespace WorldseedCarte
{

// ------------------------------------------------------------ la projection

void MetresDuPixel(const FParamsFenetre& P, int32 PX, int32 PY,
	double& OutXm, double& OutYm)
{
	const double Pas = P.MetresParPixel();
	const double Demi = static_cast<double>(P.Res) * 0.5;

	// Le centre du pixel, et non son coin : sans le demi-pixel, la fenetre
	// serait decalee d'un demi-pas et le joueur ne tomberait pas au milieu.
	const double CX = static_cast<double>(PX) + 0.5 - Demi;
	const double CY = static_cast<double>(PY) + 0.5 - Demi;

	OutXm = P.CentreXm + CX * Pas;

	// LE SIGNE EST ICI, ET NULLE PART AILLEURS. L'axe des lignes d'une texture
	// descend ; celui des Y du monde monte vers le nord. La ligne 0 doit donc
	// rendre le Y le PLUS GRAND, faute de quoi la carte a le nord en bas.
	OutYm = P.CentreYm - CY * Pas;
}


// --------------------------------------------------------------- la couleur

float FondDuMonde(const TArray<float>& ElevationM)
{
	float Fond = 0.0f;
	for (const float Z : ElevationM)
	{
		Fond = FMath::Min(Fond, Z);
	}

	// Jamais zero : il divise le degrade de profondeur.
	return FMath::Min(Fond, -1.0f);
}


FLinearColor CouleurCellule(float AltitudeM, uint8 BiomeIndex, uint8 Cover,
	float FondM)
{
	if (AltitudeM > 0.0f)
	{
		return WorldseedBiomes::Colour(
			WorldseedBiomes::AppearanceBiome(BiomeIndex, Cover));
	}

	// LE FOND MARIN PORTE UN DEGRADE, et ce n'est pas une coquetterie : sans
	// lui le plateau continental disparait, et l'on ne voit plus POURQUOI une
	// cote est la. Clair sur le plateau, sombre dans l'abysse.
	const float T = FMath::Clamp(AltitudeM / FondM, 0.0f, 1.0f);
	return FMath::Lerp(FLinearColor(0.40f, 0.60f, 0.76f),
		FLinearColor(0.03f, 0.08f, 0.20f), T);
}


namespace
{
	/** Le vide au-dela d'un pole : ni terre, ni mer, et cela doit se voir. */
	const FLinearColor CouleurHorsMonde(0.06f, 0.06f, 0.08f);

	/** Le trait de cote, assez sombre pour tenir sur une plage comme sur une mer. */
	const FColor CouleurLisere(18, 18, 24, 255);

	/**
	 * LUMIERE RASANTE DU NORD-OUEST -- l'eclairage conventionnel des cartes en
	 * relief, celui qui fait ressortir les vallees. Meme direction que
	 * l'apercu de `WorldseedNoise`, dans le repere du monde ou +X est l'est et
	 * +Y le nord.
	 */
	const FVector DirectionLumiere = FVector(-0.6, 0.6, 0.53).GetSafeNormal();

	/**
	 * Une bande qui vaut plein au centre et s'eteint sur un pixel.
	 *
	 * Reprise du globe : un bord coupe net montre son escalier, et sur la
	 * seule forme ronde de l'ecran un escalier se voit tout de suite.
	 */
	FORCEINLINE float Fondu(float Distance)
	{
		return FMath::Clamp(Distance, 0.0f, 1.0f);
	}

	FORCEINLINE void EcrireBGRA(uint8* P, const FLinearColor& C, float Alpha)
	{
		const FColor Q = C.ToFColor(true);
		P[0] = Q.B;
		P[1] = Q.G;
		P[2] = Q.R;
		P[3] = static_cast<uint8>(FMath::Clamp(Alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
	}
}


// ----------------------------------------------------------------- le fond

void PeindreFenetre(const FWorldseedGeometry& Geo,
	const TArray<float>& ElevationM, const FWorldseedBiomeMap& Biomes,
	const FParamsFenetre& P, uint8* PixelsBGRA)
{
	WORLDSEED_TRACE(Carte);

	if (!PixelsBGRA || P.Res <= 0)
	{
		return;
	}

	const int32 Total = Geo.CellCount();
	const bool bRelief = ElevationM.Num() == Total && Total > 0;
	const bool bBiomes = Biomes.Index.Num() == Total
		&& Biomes.Cover.Num() == Total;

	const int32 Res = P.Res;
	const double DemiHauteurM = static_cast<double>(Geo.HeightM) * 0.5;

	// Le rayon du disque, en pixels, moins un demi pour que le fondu tienne
	// dans le tampon.
	const float RayonPx = static_cast<float>(Res) * 0.5f - 0.5f;
	const float CentrePx = static_cast<float>(Res) * 0.5f;

	// L'ALTITUDE EST RETENUE PAR PIXEL, pour que le lisere de cote ne
	// re-echantillonne pas. Elle sert aussi de marqueur de hors-monde.
	TArray<float> Altitudes;
	Altitudes.SetNumUninitialized(Res * Res);

	// Le pas d'ombrage, en metres, sur la grille du monde.
	const double PasOmbrageM = static_cast<double>(
		FMath::Max(P.PasOmbrageCellules, 1)) * static_cast<double>(Geo.MetersPerPixel());

	ParallelFor(Res, [&](int32 PY)
	{
		for (int32 PX = 0; PX < Res; ++PX)
		{
			const int32 Index = PY * Res + PX;
			uint8* const Pixel = PixelsBGRA + Index * 4;

			double Xm = 0.0;
			double Ym = 0.0;
			MetresDuPixel(P, PX, PY, Xm, Ym);

			// --- le disque -------------------------------------------------
			float Alpha = 1.0f;
			if (P.bDisque)
			{
				const float DX = static_cast<float>(PX) + 0.5f - CentrePx;
				const float DY = static_cast<float>(PY) + 0.5f - CentrePx;
				Alpha = Fondu(RayonPx - FMath::Sqrt(DX * DX + DY * DY));
				if (Alpha <= 0.0f)
				{
					Altitudes[Index] = 0.0f;
					EcrireBGRA(Pixel, FLinearColor::Black, 0.0f);
					continue;
				}
			}

			// --- hors monde ------------------------------------------------
			//
			// Y NE S'ENROULE PAS, et il ne se borne pas non plus : un pole n'a
			// pas de voisin au-dela. Borner y dessinerait un terrain raye qui
			// laisse croire que le monde continue. On peint donc un VIDE, etat
			// distinct du hors-disque -- qui, lui, est transparent.
			if (FMath::Abs(Ym) > DemiHauteurM || !bRelief)
			{
				Altitudes[Index] = NAN;
				EcrireBGRA(Pixel, CouleurHorsMonde, Alpha);
				continue;
			}

			double U = 0.0;
			double V = 0.0;
			Geo.UVDepuisMetres(Xm, Ym, U, V);

			// L'ALTITUDE S'INTERPOLE, L'IDENTIFIANT NON. Le relief est un champ
			// continu echantillonne aux noeuds ; un biome est une CATEGORIE, et
			// la moyenne de « desert » et de « toundra » n'est pas un biome
			// intermediaire, c'est un biome qui n'existe nulle part. Ce depot a
			// paye ce piege sur la carte lue par PCG -- 307 points faux sur
			// 17956, du type « plage » lu comme « alpin ».
			const float Z = WorldseedGrid::SampleUV(ElevationM, Geo.NX, Geo.NY,
				static_cast<float>(U), static_cast<float>(V));
			Altitudes[Index] = Z;

			// SANS BIOMES, LA TERRE EST GRISE ET LA MER GARDE SON DEGRADE.
			// Passer l'identifiant 0 rendrait la couleur du PREMIER biome du
			// registre, ce qui peindrait un monde entier dans une teinte
			// arbitraire sans que rien ne signale qu'il manque une carte.
			const int32 Cellule = Geo.CelluleDepuisMetres(Xm, Ym);
			FLinearColor C = bBiomes
				? CouleurCellule(Z, Biomes.Index[Cellule],
					Biomes.Cover[Cellule], P.FondM)
				: (Z > 0.0f ? FLinearColor(0.45f, 0.42f, 0.36f)
					: CouleurCellule(Z, 0, 0, P.FondM));

			// --- l'ombrage -------------------------------------------------
			if (P.bOmbrage && Z > 0.0f)
			{
				double Ug = 0.0;
				double Vg = 0.0;

				Geo.UVDepuisMetres(Xm - PasOmbrageM, Ym, Ug, Vg);
				const float ZO = WorldseedGrid::SampleUV(ElevationM, Geo.NX,
					Geo.NY, static_cast<float>(Ug), static_cast<float>(Vg));

				Geo.UVDepuisMetres(Xm + PasOmbrageM, Ym, Ug, Vg);
				const float ZE = WorldseedGrid::SampleUV(ElevationM, Geo.NX,
					Geo.NY, static_cast<float>(Ug), static_cast<float>(Vg));

				Geo.UVDepuisMetres(Xm, Ym - PasOmbrageM, Ug, Vg);
				const float ZS = WorldseedGrid::SampleUV(ElevationM, Geo.NX,
					Geo.NY, static_cast<float>(Ug), static_cast<float>(Vg));

				Geo.UVDepuisMetres(Xm, Ym + PasOmbrageM, Ug, Vg);
				const float ZN = WorldseedGrid::SampleUV(ElevationM, Geo.NX,
					Geo.NY, static_cast<float>(Ug), static_cast<float>(Vg));

				// Pente en metres par metre, donc une VRAIE pente : l'ombrage
				// ne depend pas de la resolution de la fenetre.
				const double DZdX = (ZE - ZO) / (2.0 * PasOmbrageM);
				const double DZdY = (ZN - ZS) / (2.0 * PasOmbrageM);

				const FVector Normale = FVector(-DZdX, -DZdY, 1.0).GetSafeNormal();
				const double Lambert = FMath::Max(
					FVector::DotProduct(Normale, DirectionLumiere), 0.0);

				// Doux : on module, on ne remplace pas. Un ombrage qui va
				// jusqu'au noir mangerait les biomes qu'on vient de peindre.
				C *= static_cast<float>(0.72 + 0.56 * Lambert);
			}

			EcrireBGRA(Pixel, C, Alpha);
		}
	});

	// --- le lisere de cote, EN SECONDE PASSE ---------------------------------
	//
	// SUR LES ALTITUDES, JAMAIS SUR LES PIXELS DEJA ECRITS : souligner en place
	// propagerait le trait de proche en proche, chaque pixel noirci devenant a
	// son tour une frontiere. `ProbeCarte` travaille sur une copie pour cette
	// raison exacte ; ici la copie est le tableau d'altitudes, qui existe deja.
	if (P.bLisereCote)
	{
		for (int32 PY = 0; PY < Res; ++PY)
		{
			for (int32 PX = 0; PX < Res; ++PX)
			{
				const int32 Index = PY * Res + PX;
				const float Z = Altitudes[Index];
				if (!FMath::IsFinite(Z) || Z <= 0.0f)
				{
					continue;
				}

				const int32 XG = FMath::Max(PX - 1, 0);
				const int32 XD = FMath::Min(PX + 1, Res - 1);
				const int32 YH = FMath::Max(PY - 1, 0);
				const int32 YB = FMath::Min(PY + 1, Res - 1);

				auto EstMer = [&Altitudes, Res](int32 X, int32 Y) -> bool
				{
					const float A = Altitudes[Y * Res + X];
					return FMath::IsFinite(A) && A <= 0.0f;
				};

				if (EstMer(XG, PY) || EstMer(XD, PY)
					|| EstMer(PX, YH) || EstMer(PX, YB))
				{
					uint8* const Pixel = PixelsBGRA + Index * 4;
					Pixel[0] = CouleurLisere.B;
					Pixel[1] = CouleurLisere.G;
					Pixel[2] = CouleurLisere.R;
					// L'alpha du disque est conserve : un lisere opaque
					// depasserait du fondu de bord.
				}
			}
		}
	}
}


// ------------------------------------------------------------------ le cone

void PeindreCone(uint8* PixelsBGRA, int32 Res, float AzimutDeg,
	float DemiAngleDeg, float RayonFraction)
{
	if (!PixelsBGRA || Res <= 0)
	{
		return;
	}

	FMemory::Memzero(PixelsBGRA, static_cast<SIZE_T>(Res) * Res * 4);

	const float Centre = static_cast<float>(Res) * 0.5f;
	const float Rayon = FMath::Max(Centre * FMath::Clamp(RayonFraction, 0.05f, 1.0f), 2.0f);
	const float DemiAngle = FMath::DegreesToRadians(
		FMath::Clamp(DemiAngleDeg, 1.0f, 89.0f));

	// LA DIRECTION A L'ECRAN, ET ELLE S'ECRIT AU LIEU DE SE DEDUIRE. L'azimut
	// vaut zero au NORD et croit vers l'EST ; la ligne 0 de la texture est au
	// nord et l'axe des lignes DESCEND. Le nord est donc (0, -1) et l'est
	// (+1, 0), d'ou (sin, -cos) en (colonne, ligne).
	const float Az = FMath::DegreesToRadians(AzimutDeg);
	const float DirX = FMath::Sin(Az);
	const float DirY = -FMath::Cos(Az);

	for (int32 PY = 0; PY < Res; ++PY)
	{
		for (int32 PX = 0; PX < Res; ++PX)
		{
			const float DX = static_cast<float>(PX) + 0.5f - Centre;
			const float DY = static_cast<float>(PY) + 0.5f - Centre;
			const float D = FMath::Sqrt(DX * DX + DY * DY);

			if (D > Rayon)
			{
				continue;
			}

			// LA MARQUE « VOUS ETES ICI ». Sans elle, a un pixel par cellule,
			// le centre du disque n'est pas identifiable a l'oeil -- et un
			// cone tres etroit ne suffit pas a le designer.
			if (D <= 2.5f)
			{
				uint8* const Pixel = PixelsBGRA + (PY * Res + PX) * 4;
				Pixel[0] = 255;
				Pixel[1] = 255;
				Pixel[2] = 255;
				Pixel[3] = static_cast<uint8>(255.0f * Fondu(2.5f - D + 1.0f));
				continue;
			}

			// L'angle au sommet, par le produit scalaire avec la direction.
			const float CosA = FMath::Clamp((DX * DirX + DY * DirY) / D, -1.0f, 1.0f);
			const float Angle = FMath::Acos(CosA);

			// Le fondu angulaire vaut UN PIXEL D'ARC, donc il se resserre en
			// s'eloignant du centre : un fondu constant en angle serait large
            // au bord et invisible au sommet.
			const float FonduAngulaire = 1.0f / FMath::Max(D, 1.0f);
			const float Couverture = Fondu((DemiAngle - Angle) / FonduAngulaire);
			if (Couverture <= 0.0f)
			{
				continue;
			}

			// L'ALPHA NE S'ETEINT QUE SUR LE DERNIER TIERS. Premiere version :
			// il decroissait lineairement depuis le centre, si bien que le
			// cone etait le plus PALE la ou il est le plus LARGE -- donc
			// invisible en pratique. Verifie a l'image sur la planche-contact.
			const float Radial = FMath::Clamp((1.0f - D / Rayon) / 0.40f, 0.0f, 1.0f);

			// Les flancs portent la lisibilite : sombres et nets, ils tiennent
			// sur une calotte comme sur un ocean profond, la ou un aplat clair
			// disparaitrait sur l'un des deux. C'est la lecon du reticule du
			// globe, ou le halo sombre vient AVANT le trait clair.
			const float Flanc = 1.0f - FMath::Clamp(
				(DemiAngle - Angle) / (DemiAngle * 0.30f), 0.0f, 1.0f);

			// Le corps reste discret : on veut lire une DIRECTION a travers la
			// carte, pas poser une part de tarte dessus.
			const float Alpha = Couverture * Radial
				* FMath::Lerp(0.28f, 0.85f, Flanc);
			const uint8 Ton = static_cast<uint8>(
				255.0f * FMath::Lerp(1.0f, 0.10f, Flanc));

			uint8* const Pixel = PixelsBGRA + (PY * Res + PX) * 4;
			Pixel[0] = Ton;
			Pixel[1] = Ton;
			Pixel[2] = Ton;
			Pixel[3] = static_cast<uint8>(FMath::Clamp(Alpha, 0.0f, 1.0f) * 255.0f);
		}
	}
}

} // namespace WorldseedCarte
