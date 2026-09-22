// Worldseed - l'apparence d'un sommet : roche, neige, plage, et la mer du decor.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedApparence.h"
#include "Procedural/WorldseedBiomes.h"

#include "Misc/AutomationTest.h"

/**
 * CE CALCUL NE SE JUGE QU'A L'IMAGE, ET C'EST BIEN LE PROBLEME.
 *
 * Il decide de ce que porte chaque sommet du sol de fond : la roche qui perce
 * sur les pentes, la neige qui suit la temperature et non l'altitude, le sable
 * au bord de l'eau, et la couleur de mer qui masque la couture du decor
 * lointain. Rien de tout cela ne leve d'erreur quand c'est faux -- on voit
 * simplement un monde un peu different, et l'on ne sait pas de quoi.
 *
 * IL ETAIT INTESTABLE JUSQU'A CE MATIN. Methode privee d'un acteur de deux
 * mille lignes, clouee la parce qu'elle avait DEUX consommateurs dont un
 * mailleur legataire. Celui-ci supprime, elle a pu sortir -- et ces controles
 * deviennent possibles.
 */
namespace
{
	/** Un monde fictif et des reglages de surface aux valeurs par defaut. */
	struct FDecor
	{
		FWorldseedWorldData Monde;
		FWorldseedSurfaceRegles Regles;
		FWorldseedAppearance Mode;

		FDecor()
		{
			Monde = WorldseedTest::Monde(16);

			// LE MODE DU SOL DE FOND : couleur de biome, climat present.
			Mode.bColourByBiome = true;
			Mode.bHasClimate = true;
			Mode.bHasCover = true;
		}

		/** Normale d'une pente donnee, inclinee dans le plan XZ. */
		static FVector NormaleDePente(float Degres)
		{
			const float R = FMath::DegreesToRadians(Degres);
			return FVector(FMath::Sin(R), 0.0, FMath::Cos(R));
		}

		FLinearColor Couleur(int32 Cell, float HeightM, float PenteDeg) const
		{
			FLinearColor C = FLinearColor::Black;
			FVector2D RG = FVector2D::ZeroVector;
			FVector2D B = FVector2D::ZeroVector;
			WorldseedApparence::Sommet(Monde, Regles, Cell, HeightM,
				NormaleDePente(PenteDeg), Mode, C, RG, B);
			return C;
		}
	};
}

/**
 * LA MER DU DECOR : SOUS ZERO, ET SEULEMENT LA.
 *
 * SIGNALE EN JEU : « je vois nettement un carre d'ocean autour de moi ». Le
 * bord du carre est la fenetre glissante du plugin Water ; au-dela il n'y a
 * AUCUNE eau rendue, donc le plateau cotier s'y dessinait a sec et son biome le
 * peignait en PLAGE -- une bande de sable pale coupee par un trait DROIT.
 *
 * CE TERME N'EST PAS DEMONTRE A L'IMAGE, et le registre le dit : trois points
 * de vue eprouves, aucune difference visible, parce que le relief au-dela de la
 * fenetre etait au-dessus du niveau de la mer. Ce test est donc le SEUL
 * controle qui etablisse que la regle est juste -- a defaut de prouver qu'elle
 * serve souvent.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestApparenceMer,
	"Worldseed.Apparence.MerDuDecor",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestApparenceMer::RunTest(const FString& Parameters)
{
	FDecor D;
	const int32 Cell = 100;
	const FLinearColor Magenta(1.0f, 0.0f, 1.0f, 1.0f);

	// LE VERDICT SE LIT SUR LE TEMOIN MAGENTA, PAS SUR LA COULEUR D'OCEAN, ET
	// C'EST LA FIXTURE QUI L'IMPOSE.
	//
	// Premier jet : comparer la sortie a `WorldseedBiomes::Colour(Ocean)`. Les
	// trois assertions negatives ont echoue -- un sommet a DIX METRES
	// D'ALTITUDE sortait « de la mer ». Ce n'etait pas la regle qui debordait :
	// les couleurs de biome viennent du REGISTRE, charge depuis
	// `world_rules.json`, qu'un monde fictif ne charge pas. Toutes les teintes
	// valaient donc la meme valeur par defaut, et l'egalite etait vraie
	// partout. La comparaison ne discriminait rien.
	//
	// Le magenta, lui, est ECRIT EN DUR dans le calcul. Il ne doit rien au
	// registre, donc il dit exactement ce qu'on veut savoir : quelle BRANCHE a
	// ete prise. C'est d'ailleurs pour cette raison qu'il existe en jeu --
	// « une couleur franche ne se compare a rien, elle est la ou elle n'est
	// pas ».
	D.Mode.bMerTemoin = true;

	// --- SANS LE DRAPEAU, LA BRANCHE N'EST PAS PRISE ------------------------
	//
	// C'est la garde qui protege le terrain SOUS LES PIEDS : on y voit le fond
	// a travers l'eau -- le degrade turquoise du rivage -- et le peindre en
	// bleu opaque detruirait ce que la nappe du plugin rend bien.
	D.Mode.bMerOpaque = false;
	TestFalse(TEXT("sans le drapeau, un sommet sous zero n'est pas repeint"),
		D.Couleur(Cell, -50.0f, 0.0f).Equals(Magenta, 1e-4f));

	// --- AVEC LE DRAPEAU, SOUS ZERO EST REPEINT -----------------------------
	D.Mode.bMerOpaque = true;
	TestTrue(TEXT("sous zero, la branche de mer est prise"),
		D.Couleur(Cell, -50.0f, 0.0f).Equals(Magenta, 1e-4f));
	TestTrue(TEXT("juste sous zero aussi"),
		D.Couleur(Cell, -0.1f, 0.0f).Equals(Magenta, 1e-4f));

	// --- ET AU-DESSUS DE ZERO, JAMAIS ---------------------------------------
	//
	// La borne compte : un terme qui deborderait au-dessus du niveau de la mer
	// peindrait en bleu des plages parfaitement emergees.
	TestFalse(TEXT("juste au-dessus de zero, la branche n'est plus prise"),
		D.Couleur(Cell, 0.1f, 0.0f).Equals(Magenta, 1e-4f));
	TestFalse(TEXT("et a dix metres non plus"),
		D.Couleur(Cell, 10.0f, 0.0f).Equals(Magenta, 1e-4f));

	// --- LA BRANCHE EXIGE AUSSI LE MODE COULEUR DE BIOME --------------------
	//
	// Elle ne doit pas mordre en mode POIDS DE COUCHES : la couleur y porte des
	// poids de matiere, et y ecrire une teinte de mer donnerait un sol qui se
	// croit fait de roche et de neige dans des proportions absurdes.
	D.Mode.bColourByBiome = false;
	TestFalse(TEXT("en mode poids de couches, la branche ne mord pas"),
		D.Couleur(Cell, -50.0f, 0.0f).Equals(Magenta, 1e-4f));

	return true;
}

/**
 * LA ROCHE PERCE AVEC LA PENTE, ET LA NEIGE SUIT LA TEMPERATURE.
 *
 * DEUX RAMPES QU'AUCUNE ERREUR NE SIGNALE. Inverser les bornes d'une
 * `GetMappedRangeValueClamped` donne un monde ou la roche pousse dans les
 * plaines et l'herbe sur les falaises : c'est parfaitement silencieux, et cela
 * ne se voit qu'en regardant un versant.
 *
 * ON JUGE EN MODE POIDS DE COUCHES, ou la couleur EST le vecteur des poids --
 * le rouge porte la roche. En couleur de biome, la pente n'entre que par
 * l'etiquette et la rampe serait invisible.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestApparenceRampes,
	"Worldseed.Apparence.Rampes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestApparenceRampes::RunTest(const FString& Parameters)
{
	FDecor D;
	D.Mode.bColourByBiome = false;   // poids de couches
	D.Mode.bTexturePack = false;

	const int32 Cell = 100;
	const float H = 500.0f;          // bien au-dessus de la plage

	// --- LA ROCHE EST MONOTONE EN PENTE, ET BORNEE AUX DEUX BOUTS -----------
	const float Plat = D.Couleur(Cell, H, 0.0f).R;
	const float Debut = D.Couleur(Cell, H, D.Regles.RockSlopeStartDeg).R;
	const float Milieu = D.Couleur(Cell, H,
		0.5f * (D.Regles.RockSlopeStartDeg + D.Regles.RockSlopeFullDeg)).R;
	const float Plein = D.Couleur(Cell, H, D.Regles.RockSlopeFullDeg).R;
	const float Paroi = D.Couleur(Cell, H, 80.0f).R;

	AddInfo(FString::Printf(
		TEXT("roche : plat %.3f, debut %.3f, milieu %.3f, plein %.3f, paroi %.3f"),
		Plat, Debut, Milieu, Plein, Paroi));

	TestTrue(TEXT("aucune roche sur le plat"), Plat < 0.01f);
	TestTrue(TEXT("aucune roche au seuil de depart"), Debut < 0.01f);
	TestTrue(TEXT("de la roche a mi-rampe"), Milieu > 0.3f && Milieu < 0.7f);
	TestTrue(TEXT("tout en roche au seuil plein"), Plein > 0.99f);
	TestTrue(TEXT("et une paroi reste tout en roche"), Paroi > 0.99f);

	// LA MONOTONIE, sur toute la rampe : c'est elle qu'une inversion de bornes
	// casserait, et aucun des points ci-dessus ne la verrait a lui seul.
	float Precedent = -1.0f;
	int32 Reculs = 0;
	for (float P = 0.0f; P <= 90.0f; P += 2.0f)
	{
		const float R = D.Couleur(Cell, H, P).R;
		if (R < Precedent - 1e-4f) { ++Reculs; }
		Precedent = R;
	}
	TestEqual(TEXT("la roche ne recule jamais quand la pente monte"), Reculs, 0);

	// --- LA NEIGE SUIT LA TEMPERATURE DE LA CELLULE, PAS L'ALTITUDE ---------
	//
	// Un sommet equatorial et une plaine polaire peuvent etre a la meme
	// altitude sans avoir le meme climat : c'est tout l'interet de lire le
	// champ plutot que le relief.
	//
	// La fixture donne a chaque cellule sa propre temperature -- `Serie(N,
	// -18, 0.11)` -- donc deux cellules a la MEME altitude repondent
	// differemment. Sans cela, ce controle ne distinguerait pas les deux
	// lectures.
	int32 Froide = INDEX_NONE;
	int32 Chaude = INDEX_NONE;
	for (int32 C = 0; C < D.Monde.CellCount(); ++C)
	{
		if (D.Monde.TempC[C] < D.Regles.SnowTempFullC && Froide == INDEX_NONE)
		{
			Froide = C;
		}
		if (D.Monde.TempC[C] > D.Regles.SnowTempC + 10.0f && Chaude == INDEX_NONE)
		{
			Chaude = C;
		}
	}

	if (!TestTrue(TEXT("la fixture porte une cellule froide et une chaude"),
		Froide != INDEX_NONE && Chaude != INDEX_NONE))
	{
		return false;
	}

	// Le canal ALPHA porte la neige dans le vecteur des poids.
	const float NeigeFroide = D.Couleur(Froide, H, 0.0f).A;
	const float NeigeChaude = D.Couleur(Chaude, H, 0.0f).A;

	AddInfo(FString::Printf(
		TEXT("neige : cellule a %.1f C -> %.3f, cellule a %.1f C -> %.3f"),
		D.Monde.TempC[Froide], NeigeFroide, D.Monde.TempC[Chaude], NeigeChaude));

	TestTrue(TEXT("il neige sur la cellule froide"), NeigeFroide > 0.99f);
	TestTrue(TEXT("et pas sur la chaude, A ALTITUDE EGALE"), NeigeChaude < 0.01f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
