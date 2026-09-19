// Worldseed - les lames de gres, et les fentes qui les decoupent.

#include "Procedural/WorldseedFins.h"

#include "Procedural/WorldseedPerlin.h"
#include "Procedural/WorldseedRules.h"

FWorldseedFinRules FWorldseedFinRules::FromRules(const UWorldseedRules& Rules)
{
	FWorldseedFinRules Out;

	const TCHAR* LAM = TEXT("lames");
	auto Num = [&Rules, LAM](const TCHAR* Key, double Fallback)
	{
		return static_cast<float>(Rules.Num(LAM, Key, Fallback));
	};

	Out.SpacingM = Num(TEXT("espacementM"), 70.0);
	Out.SlotM = Num(TEXT("fenteM"), 16.0);
	Out.DepthM = Num(TEXT("profondeurM"), 50.0);
	Out.WanderM = Num(TEXT("ondulationM"), 12.0);
	Out.TurnCellM = Num(TEXT("orientationCaseM"), 16000.0);
	Out.TurnFrequency = Num(TEXT("rotationFrequence"), 0.00011);
	Out.ZoneFrequency = Num(TEXT("zoneFrequence"), 0.00025);
	Out.ZoneThreshold = Num(TEXT("zoneSeuil"), 0.45);
	Out.HardnessMin = Num(TEXT("dureteMin"), 0.50);
	Out.HardnessMax = Num(TEXT("dureteMax"), 0.70);
	Out.SeaMarginM = Num(TEXT("niveauMerMargeM"), 5.0);
	return Out;
}

namespace
{
	/** Centre de la case d'orientation qui contient ce point. */
	void CaseDe(double X, double Y, const FWorldseedFinRules& Rules,
		double& OutCx, double& OutCy)
	{
		const double C = FMath::Max(1000.0f, Rules.TurnCellM);
		OutCx = (FMath::FloorToDouble(X / C) + 0.5) * C;
		OutCy = (FMath::FloorToDouble(Y / C) + 0.5) * C;
	}
}

namespace WorldseedFins
{
	double Direction(double X, double Y, const FWorldseedFinRules& Rules, int32 Seed)
	{
		// LA DIRECTION EST CONSTANTE PAR MORCEAUX, ET C'EST OBLIGATOIRE.
		//
		// Premiere version : theta variait continument et l'on ecrivait
		// U = X.cos(theta(X,Y)) + Y.sin(theta(X,Y)). Cela PARAIT donner des
		// bandes qui tournent doucement. C'est faux, et spectaculairement :
		// theta multiplie la coordonnee ABSOLUE du monde, qui monte a 32 000 m,
		// si bien que le gradient de U vaut 1 + X.d(theta)/ds -- environ CINQ
		// loin de l'origine. L'espacement REEL des fentes valait donc quatorze
		// metres pour soixante-dix demandes, et il ne restait aucune lame.
		//
		// LA VERIFICATION L'A DIT D'UN SEUL COUP : pont MESURE a 0 m, au meme
		// offset de -22 m, sur les DIX arches. Un chiffre identique partout
		// n'est jamais un hasard de terrain -- c'est une erreur de formule. Et
		// le booleen precedent ne pouvait pas le montrer : il fallait mesurer
		// une EPAISSEUR pour que l'anomalie saute aux yeux.
		//
		// Avec theta constant dans une case, |grad U| vaut exactement 1 et
		// l'espacement est celui qu'on demande. La discontinuite au bord des
		// cases ne se voit pas : la largeur de fente est multipliee par la
		// ZONE, qui s'annule hors des taches, et les cases sont bien plus
		// larges que les taches.
		double Cx = 0.0;
		double Cy = 0.0;
		CaseDe(X, Y, Rules, Cx, Cy);

		// UN DEMI-TOUR SUFFIT : une famille de fentes a theta et la meme a
		// theta + pi sont le meme reseau.
		const float B = WorldseedPerlin::Perlin(
			static_cast<float>(Cx) * Rules.TurnFrequency,
			static_cast<float>(Cy) * Rules.TurnFrequency,
			Seed + 8821);
		return static_cast<double>(B) * (UE_DOUBLE_PI * 0.5);
	}

	double Across(double X, double Y, const FWorldseedFinRules& Rules, int32 Seed)
	{
		const double Theta = Direction(X, Y, Rules, Seed);

		// ON MESURE DEPUIS LE CENTRE DE LA CASE, pas depuis l'origine du monde.
		// Cela ne change pas le gradient -- theta est constant ici -- mais cela
		// garde U petit, donc a l'abri des pertes de precision qu'une
		// coordonnee a cinq chiffres provoque sur une periode de 70 m.
		double Cx = 0.0;
		double Cy = 0.0;
		CaseDe(X, Y, Rules, Cx, Cy);

		// L'ONDULATION PORTE SUR LA COORDONNEE, PAS SUR LA LARGEUR. Deformer la
		// largeur amincirait la lame par endroits -- et une lame qui s'amincit
		// se perce toute seule, ce qui ferait des trous partout au lieu d'une
		// arche choisie. Deplacer la coordonnee courbe la fente sans changer
		// son calibre, et sa distorsion reste bornee : le gradient de ce terme
		// vaut ondulationM x 2.pi x 0,0013, soit un dixieme.
		const double Onde = Rules.WanderM * WorldseedPerlin::Perlin(
			static_cast<float>(X) * 0.0013f,
			static_cast<float>(Y) * 0.0013f,
			Seed + 8831);

		return (X - Cx) * FMath::Cos(Theta) + (Y - Cy) * FMath::Sin(Theta) + Onde;
	}

	float Zone(double X, double Y, const FWorldseedFinRules& Rules, int32 Seed)
	{
		// UN PERLIN 2D A UNE OCTAVE, comme la garde des diaclases, et pour la
		// meme raison de cout : ce test s'evalue sur tout le gres de la bande
		// creusable, donc des millions de fois par chunk.
		const float B = WorldseedPerlin::Perlin(
			static_cast<float>(X) * Rules.ZoneFrequency,
			static_cast<float>(Y) * Rules.ZoneFrequency,
			Seed + 8841);

		if (B <= Rules.ZoneThreshold)
		{
			return 0.0f;
		}
		return FMath::Min(1.0f, (B - Rules.ZoneThreshold) / 0.2f);
	}

	double SlotAt(double X, double Y, double DepthM,
		const FWorldseedFinRules& Rules, int32 Seed)
	{
		if (!Rules.IsActive() || DepthM > Rules.DepthM)
		{
			return -1.0;
		}

		const float Z = Zone(X, Y, Rules, Seed);
		if (Z <= 0.0f)
		{
			return -1.0;
		}

		// La fente se referme avec la profondeur. Cinquante metres de chute
		// pour huit de demi-largeur : les parois sont tres redressees, ce qui
		// est bien ce qu'on veut -- un V evase ne laisserait pas de lame.
		const double Fondu = 1.0 - FMath::Max(0.0, DepthM) / Rules.DepthM;

		const double U = Across(X, Y, Rules, Seed);
		const double Axe = Rules.SpacingM * FMath::RoundToDouble(U / Rules.SpacingM);
		const double Distance = FMath::Abs(U - Axe);

		return 0.5 * Rules.SlotM * Z * Fondu - Distance;
	}

	double DecalageVersCentre(double X, double Y,
		const FWorldseedFinRules& Rules, int32 Seed)
	{
		if (!Rules.IsActive())
		{
			return 0.0;
		}

		// Le centre d'une lame est a mi-chemin de deux fentes, donc decale d'un
		// demi-espacement par rapport a l'axe d'une fente.
		const double U = Across(X, Y, Rules, Seed);
		const double Demi = 0.5 * Rules.SpacingM;
		const double Centre = Rules.SpacingM
			* FMath::RoundToDouble((U - Demi) / Rules.SpacingM) + Demi;
		return Centre - U;
	}
}
