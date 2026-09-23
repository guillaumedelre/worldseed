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
	const double PasX = P.MetresParPixelX();
	const double PasY = P.MetresParPixelY();
	const double DemiX = static_cast<double>(P.ResX) * 0.5;
	const double DemiY = static_cast<double>(P.ResY) * 0.5;

	// Le centre du pixel, et non son coin : sans le demi-pixel, la fenetre
	// serait decalee d'un demi-pas et le joueur ne tomberait pas au milieu.
	const double CX = static_cast<double>(PX) + 0.5 - DemiX;
	const double CY = static_cast<double>(PY) + 0.5 - DemiY;

	OutXm = P.CentreXm + CX * PasX;

	// LE SIGNE EST ICI, ET NULLE PART AILLEURS. L'axe des lignes d'une texture
	// descend ; celui des Y du monde monte vers le nord. La ligne 0 doit donc
	// rendre le Y le PLUS GRAND, faute de quoi la carte a le nord en bas.
	OutYm = P.CentreYm - CY * PasY;
}


bool PixelDuMetre(const FParamsFenetre& P, double LargeurMondeM,
	double Xm, double Ym, double& OutPX, double& OutPY)
{
	const double PasX = P.MetresParPixelX();
	const double PasY = P.MetresParPixelY();

	// LE REPRESENTANT LE PLUS PROCHE, et non le point tel qu'il est donne. Le
	// monde reboucle : un point a +31 km peut etre a 1 km a l'OUEST d'une vue
	// centree sur -32 km. Sans ce repli, le marqueur du joueur disparaitrait de
	// la carte des qu'on approche le meridien de bordure.
	double X = Xm;
	if (LargeurMondeM > 0.0)
	{
		const double Ecart = X - P.CentreXm;
		X = P.CentreXm + Ecart
			- LargeurMondeM * FMath::RoundToDouble(Ecart / LargeurMondeM);
	}

	// L'inverse terme a terme de `MetresDuPixel`, signe compris.
	OutPX = (X - P.CentreXm) / PasX + static_cast<double>(P.ResX) * 0.5 - 0.5;
	OutPY = (P.CentreYm - Ym) / PasY + static_cast<double>(P.ResY) * 0.5 - 0.5;

	// Les bornes sont celles des CENTRES de pixels : un point a -0,5 tombe sur
	// le bord exact de la fenetre, donc encore dedans.
	return OutPX >= -0.5 && OutPX <= static_cast<double>(P.ResX) - 0.5
		&& OutPY >= -0.5 && OutPY <= static_cast<double>(P.ResY) - 0.5;
}


double PasOmbrageMetres(const FParamsFenetre& P, const FWorldseedGeometry& Geo)
{
	const double Cellules = static_cast<double>(FMath::Max(P.PasOmbrageCellules, 1))
		* static_cast<double>(Geo.MetersPerPixel());

	// EN AGRANDISSEMENT, ON RESTE A LA CELLULE. Suivre le pixel quand il est
	// plus FIN que la cellule ne gagnerait rien -- le relief n'a pas ce
	// detail -- et rapprocherait les deux points jusqu'a rendre la pente
	// bruyante. On ne borne donc que vers le haut.
	return FMath::Max(Cellules, P.MetresParPixelX());
}


// ------------------------------------------------------------ l'agregation

double CellulesParPixel(const FParamsFenetre& P, const FWorldseedGeometry& Geo)
{
	// LA TAILLE D'UNE CELLULE N'EST PAS `MetersPerPixel()`, ET LE PIEGE A ETE
	// PAYE ICI MEME. `MetersPerPixel` vaut `HeightM / (NY - 1)` : c'est
	// l'espacement des NOEUDS, celui qu'interpole `SampleUV`. Une CELLULE, au
	// sens de `CelluleDepuisMetres` -- la convention unique du sol, qui divise
	// par NY -- vaut `HeightM / NY`. L'ecart est de 0,05 % sur la grille du jeu
	// et de 6,7 % sur la petite grille des tests : assez pour qu'un bloc de
	// deux cellules en lise quatre, ce que l'oracle a vu tout de suite.
	const double Cellule = (Geo.NX > 0)
		? static_cast<double>(Geo.WidthM()) / static_cast<double>(Geo.NX) : 0.0;
	return (Cellule > 0.0) ? P.MetresParPixelX() / Cellule : 1.0;
}


FBlocCellule AgregerBloc(const FWorldseedGeometry& Geo,
	const TArray<float>& ElevationM, const FWorldseedBiomeMap& Biomes,
	double CentreXm, double CentreYm, double LargeurM, double HauteurM)
{
	FBlocCellule Out;

	const int32 NX = Geo.NX;
	const int32 NY = Geo.NY;
	const int32 Total = NX * NY;
	if (NX <= 0 || NY <= 0 || ElevationM.Num() != Total)
	{
		return Out;
	}

	const double LargeurMonde = static_cast<double>(Geo.WidthM());
	const double HauteurMonde = static_cast<double>(Geo.HeightM);

	// Les bornes en indices, NON ENROULEES : l'enroulement se fait a la lecture,
	// sans quoi un bloc a cheval sur le meridien aurait ses bornes inversees.
	const double FI0 = ((CentreXm - LargeurM * 0.5) / LargeurMonde + 0.5) * NX;
	const double FI1 = ((CentreXm + LargeurM * 0.5) / LargeurMonde + 0.5) * NX;
	const double FJ0 = ((CentreYm - HauteurM * 0.5) / HauteurMonde + 0.5) * NY;
	const double FJ1 = ((CentreYm + HauteurM * 0.5) / HauteurMonde + 0.5) * NY;

	int32 I0 = FMath::FloorToInt(FI0);
	int32 I1 = FMath::CeilToInt(FI1) - 1;
	int32 J0 = FMath::FloorToInt(FJ0);
	int32 J1 = FMath::CeilToInt(FJ1) - 1;

	// Au moins une cellule : un pixel plus fin qu'une maille lit quand meme
	// celle sous son centre.
	I1 = FMath::Max(I1, I0);
	J1 = FMath::Max(J1, J0);

	// Y NE S'ENROULE PAS : un pole n'a pas de voisin au-dela. Un bloc
	// entierement hors du monde rend zero cellule, et l'appelant peint le vide.
	J0 = FMath::Clamp(J0, 0, NY - 1);
	J1 = FMath::Clamp(J1, 0, NY - 1);
	if (J1 < J0)
	{
		return Out;
	}

	// AU-DELA DE HUIT CELLULES PAR AXE, ON ECHANTILLONNE. Un pixel de carte
	// tres dezoomee peut couvrir des centaines de cellules, et les lire toutes
	// couterait le monde entier par image ; soixante-quatre points suffisent
	// largement a designer une moyenne et une majorite. En deca -- donc dans
	// tous les cas qui nous occupent -- le bloc est lu EN ENTIER, et c'est ce
	// que l'oracle epingle a la moyenne de `WorldseedGrid::Downsample`.
	constexpr int32 MaxParAxe = 8;
	const int32 PasI = FMath::Max(1, FMath::DivideAndRoundUp(I1 - I0 + 1, MaxParAxe));
	const int32 PasJ = FMath::Max(1, FMath::DivideAndRoundUp(J1 - J0 + 1, MaxParAxe));

	const bool bBiomes = Biomes.Index.Num() == Total && Biomes.Cover.Num() == Total;

	// LE VOTE, SUR LE COUPLE EMPAQUETE : biome en octet haut, couverture en bas.
	// Un petit tableau de pile balaye lineairement -- au plus soixante-quatre
	// entrees, donc jamais plus de soixante-quatre comparaisons, et aucune
	// allocation par pixel.
	uint16 Cles[MaxParAxe * MaxParAxe];
	int32 Comptes[MaxParAxe * MaxParAxe];
	int32 NbCles = 0;

	double Somme = 0.0;
	int32 Lues = 0;

	for (int32 J = J0; J <= J1; J += PasJ)
	{
		for (int32 I = I0; I <= I1; I += PasI)
		{
			// L'enroulement est ici, et il porte sur la COLONNE seulement.
			const int32 IE = ((I % NX) + NX) % NX;
			const int32 Index = J * NX + IE;

			const float Z = ElevationM[Index];
			Somme += static_cast<double>(Z);
			++Lues;

			// ON NE VOTE QUE POUR CE QUI EMERGE. Sous l'eau la couleur vient du
			// degrade de profondeur, pas du biome ; laisser voter les cellules
			// noyees pourrait donner un biome marin a un pixel dont la moyenne
			// dit « terre », et l'on peindrait de l'ocean sur une cote.
			if (!bBiomes || Z <= 0.0f)
			{
				continue;
			}

			const uint16 Cle = static_cast<uint16>(Biomes.Index[Index]) << 8
				| static_cast<uint16>(Biomes.Cover[Index]);

			int32 K = 0;
			for (; K < NbCles; ++K)
			{
				if (Cles[K] == Cle) { ++Comptes[K]; break; }
			}
			if (K == NbCles && NbCles < UE_ARRAY_COUNT(Cles))
			{
				Cles[NbCles] = Cle;
				Comptes[NbCles] = 1;
				++NbCles;
			}
		}
	}

	if (Lues == 0)
	{
		return Out;
	}

	Out.NbCellules = Lues;
	Out.AltitudeMoyenneM = static_cast<float>(Somme / static_cast<double>(Lues));

	// LE DEPARTAGE EST DETERMINISTE -- la cle la plus BASSE gagne. Sans regle,
	// deux biomes a egalite dependraient de l'ordre de parcours, et la carte
	// changerait d'une cuisson a l'autre sur le meme monde.
	int32 Meilleur = INDEX_NONE;
	for (int32 K = 0; K < NbCles; ++K)
	{
		if (Meilleur == INDEX_NONE
			|| Comptes[K] > Comptes[Meilleur]
			|| (Comptes[K] == Comptes[Meilleur] && Cles[K] < Cles[Meilleur]))
		{
			Meilleur = K;
		}
	}

	if (Meilleur != INDEX_NONE)
	{
		Out.BiomeMajoritaire = static_cast<uint8>(Cles[Meilleur] >> 8);
		Out.CoverMajoritaire = static_cast<uint8>(Cles[Meilleur] & 0xFF);
	}

	return Out;
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

	if (!PixelsBGRA || P.ResX <= 0 || P.ResY <= 0)
	{
		return;
	}

	// LES DEUX PAS DOIVENT ETRE EGAUX : une fenetre etiree fausserait l'ombrage
	// et rendrait le disque ovale. Les deux fabriques le garantissent ; ceci
	// attrape un remplissage a la main.
	ensureMsgf(P.PixelsCarres(),
		TEXT("fenetre etiree : %.3f m/px en X contre %.3f en Y -- passer par ")
		TEXT("FParamsFenetre::Carree ou ::Rectangle"),
		P.MetresParPixelX(), P.MetresParPixelY());

	const int32 Total = Geo.CellCount();
	const bool bRelief = ElevationM.Num() == Total && Total > 0;
	const bool bBiomes = Biomes.Index.Num() == Total
		&& Biomes.Cover.Num() == Total;

	const int32 ResX = P.ResX;
	const int32 ResY = P.ResY;
	const double DemiHauteurM = static_cast<double>(Geo.HeightM) * 0.5;

	// LE DISQUE EST INSCRIT, donc porte par le plus PETIT cote : un « disque »
	// dans un rectangle n'a de sens que la. Moins un demi pixel pour que le
	// fondu tienne dans le tampon.
	const float RayonPx = static_cast<float>(FMath::Min(ResX, ResY)) * 0.5f - 0.5f;
	const float CentreXPx = static_cast<float>(ResX) * 0.5f;
	const float CentreYPx = static_cast<float>(ResY) * 0.5f;

	// L'ALTITUDE EST RETENUE PAR PIXEL, pour que le lisere de cote ne
	// re-echantillonne pas. Elle sert aussi de marqueur de hors-monde.
	TArray<float> Altitudes;
	Altitudes.SetNumUninitialized(ResX * ResY);

	// Le pas d'ombrage, borne par le pixel : voir `PasOmbrageMetres`.
	const double PasOmbrageM = PasOmbrageMetres(P, Geo);

	// LE SEUIL EST A DEUX CELLULES PAR PIXEL, ET PAS A UNE. Les deux lectures
	// ne partagent pas exactement la meme convention -- `SampleUV` interpole
	// entre noeuds, `CelluleDepuisMetres` designe une cellule par son centre --
	// si bien qu'un basculement a `k > 1` ferait sauter l'image d'un demi-pixel
	// pile a l'echelle ou l'on regarde. En dessous de deux, la bilineaire reste
	// de toute facon le bon outil : il n'y a rien a moyenner.
	const bool bAgreger =
		(P.Agregation == FParamsFenetre::EAgregation::Toujours)
		|| (P.Agregation == FParamsFenetre::EAgregation::Auto
			&& CellulesParPixel(P, Geo) >= 2.0);

	ParallelFor(ResY, [&](int32 PY)
	{
		for (int32 PX = 0; PX < ResX; ++PX)
		{
			const int32 Index = PY * ResX + PX;
			uint8* const Pixel = PixelsBGRA + Index * 4;

			double Xm = 0.0;
			double Ym = 0.0;
			MetresDuPixel(P, PX, PY, Xm, Ym);

			// --- le disque -------------------------------------------------
			float Alpha = 1.0f;
			if (P.bDisque)
			{
				const float DX = static_cast<float>(PX) + 0.5f - CentreXPx;
				const float DY = static_cast<float>(PY) + 0.5f - CentreYPx;
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
			// QUAND UN PIXEL COUVRE PLUSIEURS CELLULES, il les agrege : la
			// moyenne pour l'altitude, le vote pour l'identifiant. Voir
			// `AgregerBloc` -- et c'est le cran de six kilometres de la minimap
			// que cela repare, pas seulement la carte du monde.
			float Z = 0.0f;
			uint8 IdBiome = 0;
			uint8 IdCover = 0;

			if (bAgreger)
			{
				const FBlocCellule Bloc = AgregerBloc(Geo, ElevationM, Biomes,
					Xm, Ym, P.MetresParPixelX(), P.MetresParPixelY());
				Z = Bloc.AltitudeMoyenneM;
				IdBiome = Bloc.BiomeMajoritaire;
				IdCover = Bloc.CoverMajoritaire;
			}
			else
			{
				Z = WorldseedGrid::SampleUV(ElevationM, Geo.NX, Geo.NY,
					static_cast<float>(U), static_cast<float>(V));

				const int32 Cellule = Geo.CelluleDepuisMetres(Xm, Ym);
				if (bBiomes)
				{
					IdBiome = Biomes.Index[Cellule];
					IdCover = Biomes.Cover[Cellule];
				}
			}

			Altitudes[Index] = Z;

			// SANS BIOMES, LA TERRE EST GRISE ET LA MER GARDE SON DEGRADE.
			// Passer l'identifiant 0 rendrait la couleur du PREMIER biome du
			// registre, ce qui peindrait un monde entier dans une teinte
			// arbitraire sans que rien ne signale qu'il manque une carte.
			FLinearColor C = bBiomes
				? CouleurCellule(Z, IdBiome, IdCover, P.FondM)
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
	//
	// ET ELLE EST PARALLELE, ce qu'elle n'etait pas. Elle lit `Altitudes`,
	// qu'elle n'ecrit jamais, et n'ecrit que les trois octets de SON pixel :
	// aucune dependance entre lignes. A soixante-cinq mille pixels l'asymetrie
	// ne se voyait pas ; sur une carte du monde entier -- huit millions et demi
	// de pixels -- cette seconde passe couterait plus cher que la premiere.
	if (P.bLisereCote)
	{
		// LE MONDE REBOUCLE EN LONGITUDE, mais une fenetre PARTIELLE, non : ses
		// deux bords sont deux endroits differents, et les relier tracerait un
		// trait de cote entre deux rives qui ne se touchent pas. On n'enroule
		// donc que lorsque la fenetre fait reellement le tour.
		const bool bEnroule = 2.0 * P.DemiPorteeXm
			>= static_cast<double>(Geo.WidthM()) - P.MetresParPixelX();

		ParallelFor(ResY, [&](int32 PY)
		{
			for (int32 PX = 0; PX < ResX; ++PX)
			{
				const int32 Index = PY * ResX + PX;
				const float Z = Altitudes[Index];
				if (!FMath::IsFinite(Z) || Z <= 0.0f)
				{
					continue;
				}

				const int32 XG = bEnroule ? (PX + ResX - 1) % ResX
					: FMath::Max(PX - 1, 0);
				const int32 XD = bEnroule ? (PX + 1) % ResX
					: FMath::Min(PX + 1, ResX - 1);
				const int32 YH = FMath::Max(PY - 1, 0);
				const int32 YB = FMath::Min(PY + 1, ResY - 1);

				auto EstMer = [&Altitudes, ResX](int32 X, int32 Y) -> bool
				{
					const float A = Altitudes[Y * ResX + X];
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
		});
	}
}


// ------------------------------------------------------------- la pyramide

void CuireReductions(const uint8* BaseBGRA, int32 BaseX, int32 BaseY,
	TArray<TArray<uint8>>& OutNiveaux)
{
	WORLDSEED_TRACE(CarteMips);

	OutNiveaux.Reset();
	if (!BaseBGRA || BaseX <= 0 || BaseY <= 0)
	{
		return;
	}

	const uint8* Source = BaseBGRA;
	int32 SX = BaseX;
	int32 SY = BaseY;

	while (SX > 1 || SY > 1)
	{
		const int32 DX = FMath::Max(SX >> 1, 1);
		const int32 DY = FMath::Max(SY >> 1, 1);

		TArray<uint8> Niveau;
		Niveau.SetNumUninitialized(static_cast<SIZE_T>(DX) * DY * 4);

		for (int32 Y = 0; Y < DY; ++Y)
		{
			// Une source impaire laisse une derniere ligne ou colonne sans
			// paire : on la borne plutot que de sortir du tampon.
			const int32 Y0 = FMath::Min(Y * 2, SY - 1);
			const int32 Y1 = FMath::Min(Y * 2 + 1, SY - 1);

			for (int32 X = 0; X < DX; ++X)
			{
				const int32 X0 = FMath::Min(X * 2, SX - 1);
				const int32 X1 = FMath::Min(X * 2 + 1, SX - 1);

				const uint8* const A = Source + (static_cast<SIZE_T>(Y0) * SX + X0) * 4;
				const uint8* const B = Source + (static_cast<SIZE_T>(Y0) * SX + X1) * 4;
				const uint8* const C = Source + (static_cast<SIZE_T>(Y1) * SX + X0) * 4;
				const uint8* const D = Source + (static_cast<SIZE_T>(Y1) * SX + X1) * 4;

				uint8* const Dst = Niveau.GetData() + (static_cast<SIZE_T>(Y) * DX + X) * 4;
				for (int32 K = 0; K < 4; ++K)
				{
					Dst[K] = static_cast<uint8>(
						(static_cast<int32>(A[K]) + B[K] + C[K] + D[K] + 2) / 4);
				}
			}
		}

		OutNiveaux.Add(MoveTemp(Niveau));

		// Le niveau qu'on vient d'ecrire devient la source du suivant : la
		// pyramide se batit par reductions successives, jamais depuis la base
		// -- c'est ce qui la rend progressive et bon marche.
		Source = OutNiveaux.Last().GetData();
		SX = DX;
		SY = DY;
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

			// LE LISERE SE MESURE EN PIXELS, PAS EN FRACTION D'ANGLE. Premiere
			// version : une bande de 30 % du demi-angle. A quarante-cinq
			// degres de demi-angle cela faisait treize degres de chaque cote,
			// si bien que le CORPS du cone ne commencait qu'au tiers et
			// disparaissait -- vu en jeu, on ne lisait plus deux aretes au lieu
			// d'un cone. Deux pixels d'epaisseur, et le liseré reste un liseré
			// quelle que soit l'ouverture.
			const float BordPx = (DemiAngle - Angle) * D;
			const float Flanc = 1.0f - FMath::Clamp(BordPx - 1.0f, 0.0f, 1.0f);

			// LE CORPS DOIT SE VOIR SUR DU SABLE COMME SUR DE L'EAU CLAIRE.
			// A vingt-huit pour cent de blanc il etait invisible sur les deux.
			const float Alpha = Couverture * Radial
				* FMath::Lerp(0.42f, 0.92f, Flanc);
			const uint8 Ton = static_cast<uint8>(
				255.0f * FMath::Lerp(1.0f, 0.08f, Flanc));

			uint8* const Pixel = PixelsBGRA + (PY * Res + PX) * 4;
			Pixel[0] = Ton;
			Pixel[1] = Ton;
			Pixel[2] = Ton;
			Pixel[3] = static_cast<uint8>(FMath::Clamp(Alpha, 0.0f, 1.0f) * 255.0f);
		}
	}
}

} // namespace WorldseedCarte
