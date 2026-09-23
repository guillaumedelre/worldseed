// Worldseed - l'erosion : elle ENLEVE de la matiere, et la roche dure en perd
// moins. Deux proprietes physiques, donc independantes du calage.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedErosion.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedRules.h"

#include "Misc/AutomationTest.h"

namespace
{
	/** Un jeu d'entrees coherent : relief, pluie uniforme, pas de soulevement. */
	struct FBanc
	{
		FWorldseedGeometry G;
		TArray<float> Relief;
		TArray<float> Pluie;
		TArray<float> Erodabilite;
		TArray<float> Soulevement;

		explicit FBanc(int32 NY = 48, float PluieMm = 800.0f, float K = 1.0f)
		{
			G = WorldseedTest::Geometrie(NY);
			Relief = WorldseedTest::Relief(G);
			const int32 N = G.CellCount();
			Pluie.Init(PluieMm, N);
			Erodabilite.Init(K, N);
			Soulevement.Init(0.0f, N);
		}
	};

	float Maximum(const TArray<float>& A)
	{
		float M = -BIG_NUMBER;
		for (const float V : A) { M = FMath::Max(M, V); }
		return M;
	}

	/** Somme des ecarts absolus, pour dire si DEUX reliefs different. */
	double Ecart(const TArray<float>& A, const TArray<float>& B)
	{
		if (A.Num() != B.Num()) { return BIG_NUMBER; }
		double S = 0.0;
		for (int32 I = 0; I < A.Num(); ++I) { S += FMath::Abs(A[I] - B[I]); }
		return S;
	}
}

/**
 * LA PLUIE REPARTIT L'EROSION, ELLE N'EN FIXE PAS LA QUANTITE.
 *
 * MA PREMIERE VERSION DE CE TEST ETAIT FAUSSE, ET C'EST LE CODE QUI M'A
 * DETROMPE. Je comparais zero millimetre a huit cents, en attendant que le
 * second erode davantage : mesure, **103387,297 contre 103387,305** -- le meme
 * chiffre a sept decimales. La cause n'est pas un defaut, c'est une decision
 * d'ingenierie : `RainWeight[I] = PrecipMm[I] / mediane des terres`. La pluie
 * est NORMALISEE PAR SA PROPRE MEDIANE, donc un champ UNIFORME ne porte
 * aucune information quel que soit son niveau, et la quantite totale d'erosion
 * ne depend pas du calage absolu des precipitations. C'est ce qu'on veut :
 * regler `targetMeanLandMm` ne doit pas changer l'amplitude du relief.
 *
 * CE QUI COMPTE EST DONC LE CONTRASTE, et c'est ce que ce test eprouve : « un
 * bassin humide se creuse en vallees, un bassin aride reste en plateau ». Sans
 * ce terme, le desert se creuserait comme la foret, et le fleuve allogene du
 * Grand Canyon -- qui ramasse son eau en montagne et traverse un desert --
 * n'existerait pas.
 *
 * L'A/B ECHANGE LA PLUIE, IL NE CHANGE PAS DE MOITIE. Comparer la moitie
 * gauche a la droite dans un seul monde ne prouverait rien : le relief de la
 * fixture est antisymetrique en X, donc les deux moities n'ont pas la meme
 * pente au depart et n'ont aucune raison de perdre autant. On compare donc LA
 * MEME moitie entre deux mondes ou seule la pluie a ete echangee -- meme
 * regle que pour la durete, et meme lecon que le depot a payee sur l'altitude
 * moyenne par roche, dominee par la position et non par le terme teste.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestErosionPluie,
	"Worldseed.Erosion.LaPluieRepartitLErosion",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestErosionPluie::RunTest(const FString& Parameters)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("world_rules.json se lit"), R))
	{
		AddError(Erreur);
		return false;
	}

	// Perte d'altitude cumulee sur la moitie GAUCHE de la carte.
	const auto PerteAGauche = [](const FWorldseedGeometry& G,
		const TArray<float>& Avant, const TArray<float>& Apres) -> double
	{
		double S = 0.0;
		for (int32 J = 0; J < G.NY; ++J)
		{
			for (int32 I = 0; I < G.NX / 2; ++I)
			{
				const int32 K = J * G.NX + I;
				S += FMath::Max(0.0f, Avant[K] - Apres[K]);
			}
		}
		return S;
	};

	// --- monde A : la gauche est ARROSEE ------------------------------------
	FBanc A(48);
	for (int32 J = 0; J < A.G.NY; ++J)
	{
		for (int32 I = 0; I < A.G.NX; ++I)
		{
			A.Pluie[J * A.G.NX + I] = (I < A.G.NX / 2) ? 2000.0f : 100.0f;
		}
	}
	const TArray<float> AvantA = A.Relief;
	FWorldseedErosionReport RA;
	TestTrue(TEXT("l'erosion s'execute (gauche arrosee)"),
		WorldseedErosion::Run(*R, A.G, A.Pluie, A.Erodabilite, A.Soulevement,
			nullptr, A.Relief, RA));

	// --- monde B : la gauche est ARIDE, tout le reste identique -------------
	FBanc B(48);
	for (int32 J = 0; J < B.G.NY; ++J)
	{
		for (int32 I = 0; I < B.G.NX; ++I)
		{
			B.Pluie[J * B.G.NX + I] = (I < B.G.NX / 2) ? 100.0f : 2000.0f;
		}
	}
	const TArray<float> AvantB = B.Relief;
	FWorldseedErosionReport RB;
	TestTrue(TEXT("l'erosion s'execute (gauche aride)"),
		WorldseedErosion::Run(*R, B.G, B.Pluie, B.Erodabilite, B.Soulevement,
			nullptr, B.Relief, RB));

	const double GaucheArrosee = PerteAGauche(A.G, AvantA, A.Relief);
	const double GaucheAride = PerteAGauche(B.G, AvantB, B.Relief);

	AddInfo(FString::Printf(
		TEXT("meme moitie gauche -- arrosee %.1f m cumules, aride %.1f m"),
		GaucheArrosee, GaucheAride));

	TestTrue(TEXT("TEMOIN : la moitie aride s'erode quand meme un peu"),
		GaucheAride > 0.0);
	TestTrue(TEXT("la moitie ARROSEE perd davantage que la meme moitie aride"),
		GaucheArrosee > GaucheAride * 1.1);

	return true;
}

/**
 * ET LE NIVEAU ABSOLU DE LA PLUIE NE CHANGE RIEN, ce qui est la contrepartie
 * du test precedent et merite d'etre GRAVE plutot que redecouvert.
 *
 * C'est ce qui protege le calage : `precipitation.targetMeanLandMm` a deja ete
 * corrige une fois -- 715 mm est une MOYENNE et non une mediane, le monde
 * etait 70 % trop humide -- et cette correction n'a pas eu a toucher au
 * relief. Si la quantite d'erosion se mettait a suivre le niveau absolu, tout
 * reglage de pluie deplacerait les montagnes.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestErosionNiveauPluie,
	"Worldseed.Erosion.LeNiveauAbsoluDePluieEstSansEffet",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestErosionNiveauPluie::RunTest(const FString& Parameters)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("world_rules.json se lit"), R))
	{
		AddError(Erreur);
		return false;
	}

	FBanc Faible(48, 200.0f), Forte(48, 2000.0f);
	FWorldseedErosionReport RF, RG;

	WorldseedErosion::Run(*R, Faible.G, Faible.Pluie, Faible.Erodabilite,
		Faible.Soulevement, nullptr, Faible.Relief, RF);
	WorldseedErosion::Run(*R, Forte.G, Forte.Pluie, Forte.Erodabilite,
		Forte.Soulevement, nullptr, Forte.Relief, RG);

	const double EcartEntreLesDeux = Ecart(Faible.Relief, Forte.Relief);

	AddInfo(FString::Printf(
		TEXT("200 mm uniformes contre 2000 -- incision %.1f contre %.1f, ecart de relief %.4f"),
		RF.TotalIncisionM, RG.TotalIncisionM, EcartEntreLesDeux));

	TestTrue(TEXT("TEMOIN : l'erosion a bien travaille"), RF.TotalIncisionM > 0.0f);
	TestTrue(TEXT("un champ de pluie UNIFORME donne le meme relief a tout niveau"),
		EcartEntreLesDeux < 1.0);

	return true;
}

/**
 * L'EROSION N'INVENTE NI MATIERE NI ALTITUDE.
 *
 * Sans soulevement, le point le plus haut du monde ne peut pas monter : il n'y
 * a aucun apport. Un signe inverse dans la loi d'incision -- une confusion
 * entre le receveur et le donneur, par exemple -- ferait CROITRE le relief, et
 * le depot ne s'en apercevrait qu'au bulletin terrestre, des dizaines de
 * minutes de generation plus tard.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestErosionNInventeRien,
	"Worldseed.Erosion.NInventeRien",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestErosionNInventeRien::RunTest(const FString& Parameters)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("world_rules.json se lit"), R))
	{
		AddError(Erreur);
		return false;
	}

	FBanc B(48, 1200.0f);
	const float SommetAvant = Maximum(B.Relief);

	FWorldseedErosionReport Rapport;
	TestTrue(TEXT("l'erosion s'execute"),
		WorldseedErosion::Run(*R, B.G, B.Pluie, B.Erodabilite, B.Soulevement,
			nullptr, B.Relief, Rapport));

	const float SommetApres = Maximum(B.Relief);

	AddInfo(FString::Printf(TEXT("sommet %.1f -> %.1f m (%d passes)"),
		SommetAvant, SommetApres, Rapport.Iterations));

	TestEqual(TEXT("aucune altitude NaN ou infinie"),
		WorldseedTest::CompterNonFinis(B.Relief), 0);

	// La tolerance couvre le DEPOT, qui peut remonter une cellule de fond de
	// vallee -- mais jamais le sommet du monde, et jamais de beaucoup.
	TestTrue(TEXT("sans soulevement, le sommet du monde ne monte pas"),
		SommetApres <= SommetAvant + 1.0f);

	TestTrue(TEXT("l'incision totale est positive ou nulle"),
		Rapport.TotalIncisionM >= 0.0f);
	TestTrue(TEXT("le depot total est positif ou nul"),
		Rapport.TotalDepositionM >= 0.0f);
	TestTrue(TEXT("TEMOIN : des passes ont bien eu lieu"), Rapport.Iterations > 0);

	return true;
}

/**
 * LA ROCHE DURE PERD MOINS QUE LA TENDRE.
 *
 * C'est `S = (U / K.A^m)^(1/n)` : l'erodabilite K est LE terme qui porte la
 * resistance du substrat, et c'est ce qui donne au granite 25 degres de pente
 * contre 9,5 au calcaire. Le depot a deja passe une session a brancher ce
 * terme pour decouvrir qu'il ne deplacait que quatre centimetres -- faute de
 * boucle soulevement/erosion. Le signe, lui, doit etre juste QUOI QU'IL
 * ARRIVE : s'il s'inversait, la roche dure formerait les vallees et la tendre
 * les corniches, et aucune mesure agregee ne le dirait.
 *
 * ON COMPARE DEUX MONDES IDENTIQUES A L'ERODABILITE PRES, ce qui isole le
 * terme -- le depot a appris a ses depens que ni l'altitude moyenne par roche
 * ni la perte brute ne mesurent l'erosion differentielle, toutes deux etant
 * dominees par la POSITION que la regle d'attribution donne a chaque roche.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestErosionDurete,
	"Worldseed.Erosion.LaDureteMordDansLeBonSens",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestErosionDurete::RunTest(const FString& Parameters)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("world_rules.json se lit"), R))
	{
		AddError(Erreur);
		return false;
	}

	// Meme relief, meme pluie, meme geometrie, meme graine implicite : SEULE
	// l'erodabilite change.
	FBanc Tendre(48, 1000.0f, 2.0f);
	FBanc Dure(48, 1000.0f, 0.25f);

	FWorldseedErosionReport RT, RD;
	WorldseedErosion::Run(*R, Tendre.G, Tendre.Pluie, Tendre.Erodabilite,
		Tendre.Soulevement, nullptr, Tendre.Relief, RT);
	WorldseedErosion::Run(*R, Dure.G, Dure.Pluie, Dure.Erodabilite,
		Dure.Soulevement, nullptr, Dure.Relief, RD);

	AddInfo(FString::Printf(
		TEXT("incision totale -- tendre (K=2,0) %.2f m, dure (K=0,25) %.2f m"),
		RT.TotalIncisionM, RD.TotalIncisionM));

	TestTrue(TEXT("TEMOIN : la roche tendre s'erode reellement"),
		RT.TotalIncisionM > 0.0f);
	TestTrue(TEXT("la roche DURE perd moins que la tendre"),
		RD.TotalIncisionM < RT.TotalIncisionM);

	return true;
}

/**
 * LA MEME ENTREE REND LE MEME RELIEF.
 *
 * L'erosion est la passe la plus chere de la chaine -- une quarantaine de
 * secondes -- donc son resultat part au CACHE. Une passe non deterministe
 * rendrait un monde different du monde cache, et le depot a deja mis une
 * soiree a comprendre qu'un releve identique au chiffre pres apres une
 * correction reelle signalait un cache, pas une absence d'effet.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestErosionDeterminisme,
	"Worldseed.Erosion.Determinisme",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestErosionDeterminisme::RunTest(const FString& Parameters)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("world_rules.json se lit"), R))
	{
		AddError(Erreur);
		return false;
	}

	FBanc A(48, 900.0f), B(48, 900.0f);
	FWorldseedErosionReport RA, RB;

	WorldseedErosion::Run(*R, A.G, A.Pluie, A.Erodabilite, A.Soulevement,
		nullptr, A.Relief, RA);
	WorldseedErosion::Run(*R, B.G, B.Pluie, B.Erodabilite, B.Soulevement,
		nullptr, B.Relief, RB);

	int32 Ecarts = 0;
	for (int32 I = 0; I < A.Relief.Num() && I < B.Relief.Num(); ++I)
	{
		if (A.Relief[I] != B.Relief[I]) { ++Ecarts; }
	}

	TestEqual(TEXT("deux passes identiques rendent le meme relief, au bit pres"),
		Ecarts, 0);
	TestEqual(TEXT("et le meme nombre d'iterations"),
		RA.Iterations, RB.Iterations);

	// TEMOIN : si l'erosion ne faisait rien, l'egalite serait triviale.
	TestTrue(TEXT("TEMOIN : l'erosion a bien travaille"),
		RA.TotalIncisionM > 0.0f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
