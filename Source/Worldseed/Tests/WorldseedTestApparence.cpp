// Worldseed - l'apparence d'un sommet : roche, neige, plage, et la mer du decor.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedApparence.h"
#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedRules.h"

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
			float N = 0.0f;
			WorldseedApparence::Sommet(Monde, Regles, Cell, HeightM,
				NormaleDePente(PenteDeg), Mode, C, RG, B, N);
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


/**
 * LA RAISON D'ETRE DU CANAL D'ESTRAN, ET C'EST ELLE QU'IL FAUT GARDER.
 *
 * La plage ne peut pas avoir sa propre texture de sable par les poids : il
 * n'y a que QUATRE matieres -- elles voyagent dans les quatre canaux RGBA du
 * sommet -- et la plage partage la matiere ARIDE avec le desert chaud, au
 * poids pres. Tant que c'est vrai, le fondu par UV2.Y est necessaire.
 *
 * SI CE TEST TOMBE UN JOUR, C'EST UNE BONNE NOUVELLE : cela voudra dire que
 * la plage a recu une matiere a elle, et que le transport peut disparaitre.
 * Il est ecrit pour dire cela, pas pour interdire le changement.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedEstranPartageLaMatiereDuDesert,
	"Worldseed.Apparence.EstranPartageLaMatiereDuDesert",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedEstranPartageLaMatiereDuDesert::RunTest(const FString&)
{
	// LE REGISTRE DOIT ETRE PUBLIE, SANS QUOI CE TEST NE MESURE RIEN. Sans
	// lui, `SlotWeights` rend une entree NEUTRE -- la meme pour tous les
	// biomes -- et l'egalite serait vraie quoi qu'il arrive. C'est la fixture
	// muette que ce depot a payee quatre fois en une journee, et la premiere
	// version de ce test y est tombee : elle passait en comparant deux valeurs
	// par defaut. `FromRules` est ce qui publie le registre.
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("les regles se chargent"), R))
	{
		AddError(Erreur);
		return false;
	}
	FWorldseedBiomeRules::FromRules(*R, WorldseedTest::Geometrie(16));

	const FLinearColor Plage = WorldseedBiomes::SlotWeights(EWorldseedBiome::Beach);
	const FLinearColor Desert = WorldseedBiomes::SlotWeights(EWorldseedBiome::HotDesert);

	AddInfo(FString::Printf(TEXT("plage  %s"), *Plage.ToString()));
	AddInfo(FString::Printf(TEXT("desert %s"), *Desert.ToString()));

	// LE TEMOIN QUI PROUVE QUE LE REGISTRE A PARLE. Une entree neutre rend
	// (1, 0, 0, 0) ; les deux biomes, eux, sont a cent pour cent de matiere
	// ARIDE, donc sur le second canal. Sans ce controle, on ne saurait pas
	// distinguer « les poids sont egaux » de « les poids n'ont pas ete lus ».
	TestTrue(TEXT("le registre est bien charge : la plage est 100 % aride"),
		Plage.G > 0.99f && Plage.R < 0.01f);

	TestTrue(TEXT("plage et desert chaud portent les MEMES poids de matiere"),
		Plage.Equals(Desert, 1e-4f));

	return true;
}


/**
 * `PartEstran` DISTINGUE, et on le verifie sur les trois cas qui comptent.
 *
 * Une fonction qui rendrait zero partout passerait « l'estran vaut zero hors
 * de la plage » sans rien prouver -- c'est la fixture muette que ce depot a
 * payee quatre fois en une journee. On exige donc AUSSI qu'elle rende un sur
 * la plage.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedPartEstranDistingue,
	"Worldseed.Apparence.PartEstranDistingue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedPartEstranDistingue::RunTest(const FString&)
{
	TestEqual(TEXT("la plage vaut un"),
		WorldseedApparence::PartEstran(EWorldseedBiome::Beach), 1.0f);
	TestEqual(TEXT("le desert chaud vaut zero"),
		WorldseedApparence::PartEstran(EWorldseedBiome::HotDesert), 0.0f);
	TestEqual(TEXT("la foret temperee vaut zero"),
		WorldseedApparence::PartEstran(EWorldseedBiome::TemperateForest), 0.0f);
	TestEqual(TEXT("la roche a nu vaut zero"),
		WorldseedApparence::PartEstran(EWorldseedBiome::BareRock), 0.0f);

	return true;
}


/**
 * LE CANAL ARRIVE JUSQU'AU SOMMET, et c'est le seul controle qui le dise.
 *
 * `PartEstran` peut etre juste et n'atteindre jamais le maillage : le depot a
 * exactement ce precedent avec la rampe du decor, dont les trois ecritures de
 * parametre etaient devenues des no-op sans qu'une ligne de journal ne bouge.
 * On appelle donc `Sommet` comme le terrain l'appelle, et l'on relit UV2.Y.
 *
 * LA FIXTURE EST GARNIE A LA MAIN : `WorldseedTest::Monde` pose des
 * couvertures de 0 a 3, donc AUCUN estran. Sans cette ligne, le test dirait
 * « l'estran vaut zero » partout et serait muet sur ce qu'il verifie.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedEstranArriveDansLeSommet,
	"Worldseed.Apparence.EstranArriveDansLeSommet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedEstranArriveDansLeSommet::RunTest(const FString&)
{
	FDecor D;

	// LE MODE QUI PORTE LES POIDS : c'est le seul ou la teinte -- donc UV2 --
	// sert a quelque chose. En couleur de biome, les quatre canaux portent la
	// couleur et les UV restent a zero.
	D.Mode.bColourByBiome = false;
	D.Mode.bTexturePack = true;

	const int32 Estran = 3;
	const int32 Terre = 4;
	D.Monde.Biomes.Cover[Estran] = static_cast<uint8>(EWorldseedCover::Beach);
	D.Monde.Biomes.Cover[Terre] = static_cast<uint8>(EWorldseedCover::None);
	D.Monde.Biomes.Index[Terre] = static_cast<uint8>(EWorldseedBiome::HotDesert);

	auto Lire = [&D](int32 Cell)
	{
		FLinearColor C = FLinearColor::Black;
		FVector2D RG = FVector2D::ZeroVector;
		FVector2D B = FVector2D::ZeroVector;
		float N = 0.0f;
		WorldseedApparence::Sommet(D.Monde, D.Regles, Cell, 2.0f,
			FVector(0.0, 0.0, 1.0), D.Mode, C, RG, B, N);
		return B.Y;
	};

	const float SurLaPlage = Lire(Estran);
	const float SurLeDesert = Lire(Terre);

	AddInfo(FString::Printf(TEXT("UV2.Y : estran %.3f, desert chaud %.3f"),
		SurLaPlage, SurLeDesert));

	TestEqual(TEXT("UV2.Y vaut un sur l'estran"), SurLaPlage, 1.0f);
	TestEqual(TEXT("et zero sur le desert, MEME MATIERE POURTANT"),
		SurLeDesert, 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedNeigeArriveDansLeSommet,
	"Worldseed.Apparence.NeigeArriveDansLeSommet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * LE DEFAUT QU'IL GARDE : la calotte glaciaire rendait GRISE en jeu pendant
 * que le globe la peignait blanche. Le registre lui donne des matieres
 * [0, 0, 1, 0] -- cent pour cent de roche -- et sa couleur blanche ne pouvait
 * pas la sauver, `TeinteNormalisee` divisant par la luminance.
 *
 * IL PORTE SON PROPRE TEMOIN, et il le faut : un `PartNeige` qui rendrait
 * zero PARTOUT passerait "la calotte est blanche" si l'on ne testait que la
 * calotte -- le depot a paye quatre fixtures muettes en une seule journee. On
 * exige donc aussi qu'un biome tempere n'ait AUCUNE neige, sans quoi la
 * mesure ne discrimine rien.
 */
bool FWorldseedNeigeArriveDansLeSommet::RunTest(const FString&)
{
	FDecor D;

	D.Mode.bColourByBiome = false;
	D.Mode.bTexturePack = true;

	const int32 Calotte = 3;
	const int32 Foret = 4;
	D.Monde.Biomes.Cover[Calotte] = static_cast<uint8>(EWorldseedCover::None);
	D.Monde.Biomes.Cover[Foret] = static_cast<uint8>(EWorldseedCover::None);
	D.Monde.Biomes.Index[Calotte] = static_cast<uint8>(EWorldseedBiome::IceCap);
	D.Monde.Biomes.Index[Foret] =
		static_cast<uint8>(EWorldseedBiome::TemperateForest);

	auto Lire = [&D](int32 Cell)
	{
		FLinearColor C = FLinearColor::Black;
		FVector2D RG = FVector2D::ZeroVector;
		FVector2D B = FVector2D::ZeroVector;
		float N = -1.0f;
		WorldseedApparence::Sommet(D.Monde, D.Regles, Cell, 2.0f,
			FVector(0.0, 0.0, 1.0), D.Mode, C, RG, B, N);
		return N;
	};

	const float SurLaCalotte = Lire(Calotte);
	const float SurLaForet = Lire(Foret);

	AddInfo(FString::Printf(TEXT("UV3.Y : calotte %.3f, foret temperee %.3f"),
		SurLaCalotte, SurLaForet));

	TestEqual(TEXT("UV3.Y vaut un sur la calotte glaciaire"),
		SurLaCalotte, 1.0f);
	TestEqual(TEXT("et zero sur une foret temperee -- SANS QUOI LA MESURE NE "
		"DISCRIMINE RIEN"), SurLaForet, 0.0f);

	// LA MEME PART, LUE PAR LES DEUX CONSOMMATEURS. La nappe d'horizon passe
	// par `Sommet`, le terrain voxel par `WorldseedPeinture` : les deux
	// appellent `PartNeige`, et le depot interdit de recopier une formule
	// dans deux fichiers. Une divergence se verrait exactement la ou les deux
	// maillages se rencontrent -- au rayon de vue, en cercle autour du joueur.
	TestEqual(TEXT("PartNeige rend la meme chose que le sommet, calotte"),
		WorldseedApparence::PartNeige(EWorldseedBiome::IceCap), SurLaCalotte);
	TestEqual(TEXT("PartNeige rend la meme chose que le sommet, foret"),
		WorldseedApparence::PartNeige(EWorldseedBiome::TemperateForest),
		SurLaForet);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
