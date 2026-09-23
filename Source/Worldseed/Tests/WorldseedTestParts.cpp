// Worldseed - tenir des proportions demandees sur une distribution qui n'est
// pas uniforme. C'est la faute `diaclaseZonePct`, et elle a coute un reglage
// qui gardait 1,59 % la ou il en promettait 16.

#if WITH_DEV_AUTOMATION_TESTS

#include "Procedural/WorldseedParts.h"

#include "Misc/AutomationTest.h"

namespace
{
	/**
	 * Un echantillon FRANCHEMENT dissymetrique, et c'est tout l'objet.
	 *
	 * Sur une distribution uniforme, un seuil pose « au tiers de l'etendue »
	 * garde bien un tiers, et le test passerait avec ou sans quantile : il ne
	 * mesurerait rien. Le cube tasse les valeurs vers zero exactement comme un
	 * Perlin les masse autour de sa moyenne -- c'est le cas qui a mordu.
	 */
	TArray<float> WorldseedPartsEchantillonTordu(int32 N)
	{
		TArray<float> V;
		V.Reserve(N);
		for (int32 I = 0; I < N; ++I)
		{
			const float U = static_cast<float>(I) / static_cast<float>(N - 1);
			V.Add(U * U * U);
		}
		return V;
	}
}

/**
 * LES PROPORTIONS DEMANDEES SONT TENUES, QUELLE QUE SOIT LA DISTRIBUTION.
 *
 * LE DEFAUT QUE CE TEST GARDE. Le reglage des diaclases s'appelait
 * `diaclaseZonePct` et valait 0,16 ; le code en tirait un seuil en supposant le
 * bruit UNIFORME sur [-1..1]. Un Perlin ne l'est pas : le seuil 0,68 cense
 * garder 16 % n'en gardait que **1,59**. Meme famille d'erreur que les 715 mm
 * pris pour une mediane -- on confond une VALEUR et un RANG.
 *
 * ET LE TEMOIN EST LA MOITIE QUI COMPTE : il pose des seuils « a l'intuition »
 * sur le MEME echantillon et montre qu'ils ne tiennent rien. Sans lui, « les
 * parts sont tenues » pourrait etre vrai simplement parce que l'echantillon est
 * gentil, et le test ne prouverait rien sur la methode.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestPartsProportions,
	"Worldseed.Parts.LesProportionsSontTenues",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestPartsProportions::RunTest(const FString& Parameters)
{
	constexpr int32 N = 10000;
	const TArray<float> Echantillon = WorldseedPartsEchantillonTordu(N);

	// Les proportions du calage historique du socle, et ce n'est pas un hasard :
	// c'est sur elles que le depot a mesure 40,8 / 38,1 / 21,1 pour 45 / 35 / 20
	// demandes, faute d'echantillonner le bon domaine.
	const TArray<float> Parts = { 45.0f, 35.0f, 20.0f };
	const float Attendu[3] = { 45.0f, 35.0f, 20.0f };

	TArray<float> Seuils;
	WorldseedParts::Seuils(Echantillon, Parts, 3, Seuils);

	TestEqual(TEXT("trois classes demandent deux seuils"), Seuils.Num(), 2);

	int32 Compte[3] = { 0, 0, 0 };
	for (const float V : Echantillon)
	{
		++Compte[FMath::Min(WorldseedParts::Classe(V, Seuils), 2)];
	}

	float PireEcart = 0.0f;
	for (int32 K = 0; K < 3; ++K)
	{
		const float Part = 100.0f * Compte[K] / N;
		AddInfo(FString::Printf(TEXT("classe %d : %.2f %% pour %.0f demandes"),
			K, Part, Attendu[K]));
		PireEcart = FMath::Max(PireEcart, FMath::Abs(Part - Attendu[K]));
	}

	TestTrue(FString::Printf(
		TEXT("les parts sont tenues a %.2f point pres sur un bruit tres dissymetrique"),
		PireEcart), PireEcart < 0.5f);

	// --- LE TEMOIN : les memes parts posees comme des VALEURS -----------------
	//
	// C'est exactement ce que faisait `diaclaseZonePct`. On coupe l'etendue aux
	// memes fractions -- 45 % puis 80 % du chemin entre le min et le max -- et
	// l'on compte ce que cela garde REELLEMENT.
	const float Min = Echantillon[0];
	const float Max = Echantillon.Last();
	const TArray<float> SeuilsNaifs = {
		Min + 0.45f * (Max - Min),
		Min + 0.80f * (Max - Min)
	};

	int32 CompteNaif[3] = { 0, 0, 0 };
	for (const float V : Echantillon)
	{
		++CompteNaif[FMath::Min(WorldseedParts::Classe(V, SeuilsNaifs), 2)];
	}

	float PireEcartNaif = 0.0f;
	for (int32 K = 0; K < 3; ++K)
	{
		const float Part = 100.0f * CompteNaif[K] / N;
		AddInfo(FString::Printf(
			TEXT("TEMOIN, seuil pose a l'intuition -- classe %d : %.2f %% pour %.0f demandes"),
			K, Part, Attendu[K]));
		PireEcartNaif = FMath::Max(PireEcartNaif, FMath::Abs(Part - Attendu[K]));
	}

	TestTrue(FString::Printf(
		TEXT("TEMOIN : des seuils poses a l'intuition se trompent de %.1f points, ")
		TEXT("donc le quantile n'est pas decoratif"), PireEcartNaif),
		PireEcartNaif > 15.0f);

	return true;
}

/**
 * UN DECOUPAGE IMPOSSIBLE DEGENERE, IL NE LIT PAS HORS BORNES.
 *
 * Ces cas ne sont pas theoriques : un domaine de depot peut ne contenir AUCUNE
 * cellule sur un monde donne -- c'est meme ce que le releve de reference montre
 * pour les tables, « 0 sites retenus sur 0 candidats ». Et un fichier de regles
 * peut decrire moins de parts que de roches ; l'original indexait alors `Parts[K]`
 * sans garde, donc faisait tomber la generation entiere.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestPartsDegenere,
	"Worldseed.Parts.UnDecoupageImpossibleDegenere",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestPartsDegenere::RunTest(const FString& Parameters)
{
	const TArray<float> Parts = { 45.0f, 35.0f, 20.0f };
	TArray<float> Seuils;

	// --- un domaine vide ------------------------------------------------------
	WorldseedParts::Seuils(TArray<float>(), Parts, 3, Seuils);
	TestEqual(TEXT("un echantillon vide ne rend aucun seuil"), Seuils.Num(), 0);
	TestEqual(TEXT("et tout retombe alors dans la premiere classe"),
		WorldseedParts::Classe(1234.0f, Seuils), 0);

	// --- une seule classe -----------------------------------------------------
	const TArray<float> Ech = WorldseedPartsEchantillonTordu(100);
	WorldseedParts::Seuils(Ech, Parts, 1, Seuils);
	TestEqual(TEXT("une classe unique ne demande aucun seuil"), Seuils.Num(), 0);

	// --- MOINS DE PARTS QUE DE CLASSES ---------------------------------------
	//
	// L'original lisait `Parts[K]` sans garde et tombait. Ici la classe sans
	// part recoit une tranche VIDE, ce qui est exactement ce qu'un fichier muet
	// demande -- et le compte le verifie plutot que de se fier a l'absence de
	// plantage.
	const TArray<float> PartsCourtes = { 60.0f, 40.0f };
	WorldseedParts::Seuils(Ech, PartsCourtes, 4, Seuils);
	TestEqual(TEXT("quatre classes demandent trois seuils, meme avec deux parts"),
		Seuils.Num(), 3);

	int32 Compte[4] = { 0, 0, 0, 0 };
	for (const float V : Ech)
	{
		++Compte[FMath::Min(WorldseedParts::Classe(V, Seuils), 3)];
	}
	AddInfo(FString::Printf(TEXT("deux parts pour quatre classes -> %d / %d / %d / %d"),
		Compte[0], Compte[1], Compte[2], Compte[3]));

	TestTrue(TEXT("les deux classes decrites se partagent tout l'echantillon"),
		Compte[0] > 0 && Compte[1] > 0);
	TestEqual(TEXT("la troisieme classe, sans part, est vide"), Compte[2], 0);
	TestEqual(TEXT("la quatrieme aussi"), Compte[3], 0);

	// --- des parts toutes nulles ---------------------------------------------
	const TArray<float> PartsNulles = { 0.0f, 0.0f, 0.0f };
	WorldseedParts::Seuils(Ech, PartsNulles, 3, Seuils);
	TestEqual(TEXT("des parts nulles ne divisent pas par zero"), Seuils.Num(), 2);

	return true;
}

/**
 * LES SEUILS SONT CROISSANTS, ET LA CLASSE LES SUIT.
 *
 * Un ordre rompu ferait sauter des classes sans qu'aucun compte global ne s'en
 * apercoive -- les parts resteraient justes en somme, et la roche tomberait au
 * mauvais endroit. C'est precisement ce qu'on ne veut pas : c'est la ROCHE qui
 * decide des formes, et une craie en haute montagne est une falaise au mauvais
 * endroit.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestPartsOrdre,
	"Worldseed.Parts.LeChoixSuitLesSeuils",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestPartsOrdre::RunTest(const FString& Parameters)
{
	const TArray<float> Ech = WorldseedPartsEchantillonTordu(5000);
	const TArray<float> Parts = { 10.0f, 20.0f, 30.0f, 40.0f };

	TArray<float> Seuils;
	WorldseedParts::Seuils(Ech, Parts, 4, Seuils);

	for (int32 K = 1; K < Seuils.Num(); ++K)
	{
		TestTrue(FString::Printf(TEXT("le seuil %d ne recule pas sur le precedent"), K),
			Seuils[K] >= Seuils[K - 1]);
	}

	// La classe ne recule jamais quand la valeur monte : c'est la propriete que
	// l'attribution suppose, et elle n'est vraie que si les seuils sont tries.
	int32 Precedente = 0;
	int32 Reculs = 0;
	for (const float V : Ech)
	{
		const int32 K = WorldseedParts::Classe(V, Seuils);
		if (K < Precedente) { ++Reculs; }
		Precedente = K;
	}
	TestEqual(TEXT("sur un echantillon croissant, la classe ne recule jamais"),
		Reculs, 0);

	// Et les bornes : sous le premier seuil on est dans la classe 0, au-dessus
	// du dernier dans la derniere.
	TestEqual(TEXT("bien en dessous, classe 0"),
		WorldseedParts::Classe(-1.0e6f, Seuils), 0);
	TestEqual(TEXT("bien au-dessus, derniere classe"),
		WorldseedParts::Classe(1.0e6f, Seuils), Seuils.Num());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
