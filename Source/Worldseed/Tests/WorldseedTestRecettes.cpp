// Worldseed - les recettes de vegetation, et le hachage qui les seme.
//
// DEUX SUJETS, ET ILS SE TIENNENT. Le fichier de recettes decide de l'aspect
// du monde -- c'est lui-meme qui le dit : « fait pour etre RETOUCHE A LA MAIN :
// c'est lui qui decide de l'aspect du monde, pas le code ». Et le hachage
// decide d'ou chaque plante tombe. Un defaut dans l'un ou l'autre ne leve
// aucune erreur : le monde sort simplement nu, ou aligne.

#if WITH_DEV_AUTOMATION_TESTS

#include "Procedural/WorldseedParois.h"
#include "Procedural/WorldseedRecettes.h"

#include "Misc/AutomationTest.h"

/**
 * LE FICHIER DE RECETTES DU PROJET SE LIT, ET IL N'EST PAS VIDE.
 *
 * C'EST UN TEST DE CALAGE, COMME LE BULLETIN TERRESTRE, et il faut l'assumer :
 * il depend d'un fichier versionne qu'on retouche, donc il echouera si ce
 * fichier casse. C'est precisement ce qu'on veut. La regle du depot -- « un
 * test doit echouer quand le CODE casse, pas quand une valeur physique bouge »
 * -- vise les valeurs PHYSIQUES ; ici on ne verifie aucune densite ni aucune
 * espece, seulement que le fichier se PARSE et qu'il porte de la matiere.
 *
 * CE QU'IL AURAIT ATTRAPE. Le chargeur refuse deja un fichier dont toutes les
 * couches sont vides -- « le fichier est peut-etre une vue filtree par la
 * palette » -- ce qui veut dire que ce cas s'est deja produit. Un JSON casse
 * par une virgule, une cle renommee, ou une palette appliquee de travers
 * laissent tous un monde NU, et le jeu reste jouable : rien ne crie.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedRecettesFichierTest,
	"Worldseed.Recettes.LeFichierDuProjetSeLit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedRecettesFichierTest::RunTest(const FString& Parameters)
{
	FWorldseedRecettes R;
	FString Erreur;
	if (!TestTrue(FString::Printf(
		TEXT("le fichier de recettes se charge (%s)"), *Erreur), R.Charger(Erreur)))
	{
		return false;
	}

	TestFalse(TEXT("des recettes ont ete lues"), R.EstVide());
	TestTrue(TEXT("plusieurs biomes sont couverts"), R.ParBiome.Num() >= 10);
	TestTrue(TEXT("le catalogue porte des maillages"), R.Catalogue.Num() > 20);
	TestTrue(TEXT("les couches sont comptees"), R.NbCouches > 20);

	// AUCUNE COUCHE NE DOIT ETRE VIDE NI SANS POIDS. Une couche sans espece
	// traverse tout le semis pour ne rien poser, et une couche dont les poids
	// somment a zero fait un tirage sur un intervalle nul -- deux facons
	// silencieuses de perdre une strate entiere du decor.
	int32 CouchesVides = 0;
	int32 PoidsNuls = 0;
	int32 IndexHorsCatalogue = 0;
	for (const TPair<int32, FWorldseedBiomeRecette>& B : R.ParBiome)
	{
		for (const FWorldseedCoucheRecette& C : B.Value.Couches)
		{
			if (C.Especes.Num() == 0) { ++CouchesVides; }
			if (C.PoidsTotal <= 0.0f) { ++PoidsNuls; }
			for (const FWorldseedEspece& E : C.Especes)
			{
				if (!R.Catalogue.IsValidIndex(E.IndexCatalogue))
				{
					++IndexHorsCatalogue;
				}
			}
		}
	}
	TestEqual(TEXT("aucune couche sans espece"), CouchesVides, 0);
	TestEqual(TEXT("aucune couche de poids nul"), PoidsNuls, 0);

	// L'INDEX EST PRECALCULE A LA LECTURE, et c'est lui qui designe le
	// composant d'instances. Un index hors du catalogue poserait la plante sur
	// la MAUVAISE espece -- « un defaut qui ne se voit qu'a l'image, et
	// seulement si l'on connait les especes attendues ».
	TestEqual(TEXT("chaque espece pointe dans le catalogue"), IndexHorsCatalogue, 0);
	return true;
}

/**
 * L'ESTRAN EST LU, ET IL NE PORTE PAS D'HERBE.
 *
 * La section `estran` est FACULTATIVE -- un fichier qui l'ignore garde le
 * comportement d'avant -- donc son absence ne leve rien. Sans ce test, la
 * disparition de la section ramenerait l'herbe sur le sable sans qu'aucune
 * mesure ne bouge : le semis continuerait, avec la recette du biome.
 *
 * ON VERIFIE AUSSI QU'ELLE RESTE MAIGRE. Le point de la demande etait « sur
 * les plages le foliage beaucoup moins dense, de l'herbe ne pousse pas, on y
 * trouve quelques roches ». Une couche d'estran au pas du tapis rendrait une
 * plage aussi fournie qu'une prairie, ce qu'aucun compte global ne dirait.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedRecettesEstranTest,
	"Worldseed.Recettes.LEstranEstMaigre",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedRecettesEstranTest::RunTest(const FString& Parameters)
{
	FWorldseedRecettes R;
	FString Erreur;
	if (!TestTrue(TEXT("le fichier de recettes se charge"), R.Charger(Erreur)))
	{
		return false;
	}

	if (!TestTrue(TEXT("la section estran est lue"), R.Estran.Couches.Num() > 0))
	{
		return false;
	}

	// LE PAS LE PLUS FIN DE L'ESTRAN, compare a celui d'un tapis. Le tapis du
	// projet est a 75 cm ; l'estran doit etre d'un tout autre ordre.
	float PasMin = TNumericLimits<float>::Max();
	for (const FWorldseedCoucheRecette& C : R.Estran.Couches)
	{
		PasMin = FMath::Min(PasMin, C.PasCm);
		TestTrue(FString::Printf(
			TEXT("la couche d'estran « %s » porte des especes"), *C.Nom),
			C.Especes.Num() > 0);
	}

	// 2000 cm, soit vingt metres : plus de vingt-cinq fois le pas du tapis.
	// Le seuil est LARGE a dessein -- on garde contre une couche semee a la
	// densite d'une prairie, pas contre un reglage fin.
	TestTrue(FString::Printf(
		TEXT("l'estran est seme clair (pas le plus fin : %.0f cm)"), PasMin),
		PasMin >= 2000.0f);
	return true;
}

/**
 * LE HACHAGE DU SEMIS : REPRODUCTIBLE, ET SES CANAUX INDEPENDANTS.
 *
 * IL EST PARTAGE PAR LES DEUX SEMIS -- la vegetation l'importe explicitement,
 * « le hachage de tirage, partage » -- donc un defaut ici touche les plantes
 * ET les pans de falaise.
 *
 * L'EN-TETE DIT POURQUOI LES CANAUX COMPTENT : « les derouler d'un seul
 * hachage correlerait l'echelle au yaw, et l'on verrait les gros pans tous
 * tournes du meme cote ». C'est un defaut VISIBLE mais qu'aucun compte ne
 * donne -- il faut le regarder, ou le mesurer ici.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedParoisTirageTest,
	"Worldseed.Parois.LeTirageEstStableEtDecorrele",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedParoisTirageTest::RunTest(const FString& Parameters)
{
	constexpr int32 N = 4000;

	// --- REPRODUCTIBLE -----------------------------------------------------
	//
	// Sans cela, rien de ce projet ne tient : « deux parties sur la meme graine
	// doivent semer a l'identique ».
	int32 Instables = 0;
	int32 HorsBornes = 0;
	for (int32 I = 0; I < N; ++I)
	{
		const int32 MX = (I % 97) - 48;
		const int32 MY = ((I / 97) % 89) - 44;
		const float A = WorldseedParois::Tirage(MX, MY, 3, 20260909);
		const float B = WorldseedParois::Tirage(MX, MY, 3, 20260909);
		if (A != B) { ++Instables; }
		if (A < 0.0f || A >= 1.0f) { ++HorsBornes; }
	}
	TestEqual(TEXT("le meme tirage rend toujours la meme valeur"), Instables, 0);
	TestEqual(TEXT("le tirage reste dans [0..1)"), HorsBornes, 0);

	// --- LA GRAINE SEPARE --------------------------------------------------
	//
	// TEMOIN : sans lui, un hachage qui ignorerait la graine passerait le test
	// de reproductibilite haut la main. Deux mondes differents porteraient
	// alors la meme vegetation au metre pres.
	int32 Identiques = 0;
	for (int32 I = 0; I < N; ++I)
	{
		const int32 MX = (I % 97) - 48;
		const int32 MY = ((I / 97) % 89) - 44;
		if (WorldseedParois::Tirage(MX, MY, 3, 20260909)
			== WorldseedParois::Tirage(MX, MY, 3, 1337))
		{
			++Identiques;
		}
	}
	TestTrue(FString::Printf(
		TEXT("deux graines donnent deux semis (%d collisions sur %d)"),
		Identiques, N),
		Identiques < N / 20);

	// --- LES CANAUX SONT DECORRELES ----------------------------------------
	//
	// On mesure le coefficient de correlation lineaire entre deux canaux sur
	// les memes mailles. Deux suites independantes le rendent proche de zero ;
	// deux suites derivees l'une de l'autre -- par exemple par un simple
	// decalage -- le rendent proche de un, et c'est exactement le defaut que
	// l'en-tete decrit.
	auto Correlation = [&](int32 CanalA, int32 CanalB)
	{
		double SA = 0.0, SB = 0.0, SAA = 0.0, SBB = 0.0, SAB = 0.0;
		for (int32 I = 0; I < N; ++I)
		{
			const int32 MX = (I % 97) - 48;
			const int32 MY = ((I / 97) % 89) - 44;
			const double A = WorldseedParois::Tirage(MX, MY, CanalA, 20260909);
			const double B = WorldseedParois::Tirage(MX, MY, CanalB, 20260909);
			SA += A; SB += B; SAA += A * A; SBB += B * B; SAB += A * B;
		}
		const double Cov = N * SAB - SA * SB;
		const double VA = FMath::Sqrt(FMath::Max(1e-9, N * SAA - SA * SA));
		const double VB = FMath::Sqrt(FMath::Max(1e-9, N * SBB - SB * SB));
		return FMath::Abs(Cov / (VA * VB));
	};

	for (int32 C = 0; C < 4; ++C)
	{
		const double R = Correlation(C, C + 1);
		TestTrue(FString::Printf(
			TEXT("les canaux %d et %d sont decorreles (|r| = %.3f)"), C, C + 1, R),
			R < 0.1);
	}

	// TEMOIN DE LA MESURE ELLE-MEME : un canal contre LUI-MEME doit rendre 1.
	// Sans ce controle, une correlation qui rendrait zero par construction --
	// une division ratee, un tableau vide -- ferait passer les quatre lignes
	// ci-dessus sans rien prouver.
	TestTrue(TEXT("la correlation vaut 1 pour un canal contre lui-meme"),
		Correlation(2, 2) > 0.99);
	return true;
}

#endif
