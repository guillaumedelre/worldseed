// Worldseed - la tectonique : elle pose les plaques et le trait de cote, donc
// elle decide de TOUT ce qui suit. Une derive ici ne casse rien : elle rend un
// autre monde.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedTectonics.h"

#include "Misc/AutomationTest.h"

namespace
{
	/**
	 * Une grille assez grande pour que la statistique ait un sens.
	 *
	 * 48 par axe donnerait 4608 cellules : la part de terres y varierait de
	 * plusieurs points par pur echantillonnage, et le test deviendrait
	 * instable sans qu'un defaut soit en cause. A 96, on a 18 432 cellules et
	 * la part se tient au demi-point.
	 */
	constexpr int32 NYBanc = 96;

	int32 CompterNonFinis(const TArray<float>& A)
	{
		int32 N = 0;
		for (const float V : A)
		{
			if (FMath::IsNaN(V) || !FMath::IsFinite(V)) { ++N; }
		}
		return N;
	}

	bool Generer(FAutomationTestBase& Test, int32 Seed,
		FWorldseedTectonicResult& Out, FWorldseedGeometry& OutGeo)
	{
		FString Erreur;
		const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
		if (!Test.TestNotNull(TEXT("world_rules.json se lit"), R))
		{
			Test.AddError(Erreur);
			return false;
		}

		OutGeo = WorldseedTest::Geometrie(NYBanc);
		return WorldseedTectonics::Generate(*R, OutGeo, Seed, Out);
	}
}

/**
 * LA PART DE TERRES EST TENUE, ET C'EST L'INVARIANT SACRE DU PROJET.
 *
 * Tout le calage terrestre en depend : les parts de biomes sont un jeu a SOMME
 * NULLE -- agrandir l'Antarctique retire de la savane -- donc une part emergee
 * qui deriverait invaliderait d'un coup le bulletin des vingt-trois climats
 * reels, l'ecart absolu moyen aux huit biomes, et toute comparaison avec un
 * releve anterieur. Le depot verifie ce 29,2 % apres chaque chantier de
 * relief ; ici il devient un oracle qui echoue tout seul.
 *
 * LE NIVEAU DE LA MER EST UN QUANTILE, pas une constante : la tectonique le
 * DEPLACE pour atteindre la cible. C'est pourquoi la part tenue est une
 * propriete du code et non un heureux hasard du bruit.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestTectoniquePartDeTerres,
	"Worldseed.Tectonique.PartDeTerres",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestTectoniquePartDeTerres::RunTest(const FString& Parameters)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("world_rules.json se lit"), R))
	{
		AddError(Erreur);
		return false;
	}

	const float Cible = static_cast<float>(
		R->Num(TEXT("tectonics"), TEXT("landRatio"), 0.292));

	// PLUSIEURS GRAINES, parce qu'une seule ne dirait rien : la cible doit
	// etre tenue pour TOUT monde, pas pour celui qui tombe bien.
	const int32 Graines[] = { 20260909, 1337, 7, 999983 };

	for (const int32 Seed : Graines)
	{
		FWorldseedTectonicResult T;
		FWorldseedGeometry G;
		if (!TestTrue(TEXT("la tectonique s'execute"), Generer(*this, Seed, T, G)))
		{
			return false;
		}

		AddInfo(FString::Printf(
			TEXT("graine %d : terres %.2f %% (cible %.2f), niveau de mer %.1f m"),
			Seed, T.MeasuredLandRatio * 100.0f, Cible * 100.0f, T.SeaLevelShiftM));

		TestTrue(FString::Printf(
			TEXT("graine %d : la part de terres tient la cible a un point pres"), Seed),
			FMath::Abs(T.MeasuredLandRatio - Cible) < 0.01f);
	}

	return true;
}

/**
 * LES PLAQUES PARTITIONNENT LE MONDE, ET LA CROUTE CONTINENTALE EST PLUS HAUTE.
 *
 * C'est la signature PHYSIQUE de l'isostasie : une croute continentale est
 * moins dense et flotte plus haut. Si elle se perdait, le trait de cote
 * cesserait de suivre les plaques et deviendrait un simple seuillage de bruit
 * -- exactement ce que le depot a corrige le 21 septembre en decouvrant que
 * des continents peuvent etre anguleux tout en ayant une dimension fractale
 * parfaite. Aucune mesure agregee ne l'aurait dit.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestTectoniquePlaques,
	"Worldseed.Tectonique.PlaquesEtIsostasie",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestTectoniquePlaques::RunTest(const FString& Parameters)
{
	FWorldseedTectonicResult T;
	FWorldseedGeometry G;
	if (!TestTrue(TEXT("la tectonique s'execute"), Generer(*this, 20260909, T, G)))
	{
		return false;
	}

	const int32 N = G.CellCount();
	TestEqual(TEXT("une altitude par cellule"), T.ElevationM.Num(), N);
	TestEqual(TEXT("une plaque par cellule"), T.PlateId.Num(), N);
	TestEqual(TEXT("un drapeau continental par cellule"), T.IsContinental.Num(), N);
	TestEqual(TEXT("une convergence par cellule"), T.Convergence.Num(), N);
	TestEqual(TEXT("aucune altitude NaN"), CompterNonFinis(T.ElevationM), 0);
	TestEqual(TEXT("aucune convergence NaN"), CompterNonFinis(T.Convergence), 0);

	// --- les plaques ---------------------------------------------------------
	TSet<int32> Plaques;
	int32 Invalides = 0;
	for (const int32 Id : T.PlateId)
	{
		if (Id < 0) { ++Invalides; } else { Plaques.Add(Id); }
	}

	AddInfo(FString::Printf(TEXT("%d plaques distinctes, %d cellules sans plaque"),
		Plaques.Num(), Invalides));

	TestEqual(TEXT("chaque cellule appartient a une plaque"), Invalides, 0);
	TestTrue(TEXT("il y a plusieurs plaques"), Plaques.Num() >= 2);

	// --- l'isostasie ---------------------------------------------------------
	double SommeCont = 0.0, SommeOcean = 0.0;
	int32 NCont = 0, NOcean = 0;
	for (int32 I = 0; I < N; ++I)
	{
		if (T.IsContinental[I] != 0) { SommeCont += T.ElevationM[I]; ++NCont; }
		else { SommeOcean += T.ElevationM[I]; ++NOcean; }
	}

	if (!TestTrue(TEXT("TEMOIN : les deux croutes existent"),
		NCont > 0 && NOcean > 0))
	{
		return false;
	}

	const double MoyCont = SommeCont / NCont;
	const double MoyOcean = SommeOcean / NOcean;

	AddInfo(FString::Printf(
		TEXT("altitude moyenne -- continentale %.1f m (%d cellules), oceanique %.1f m (%d)"),
		MoyCont, NCont, MoyOcean, NOcean));

	TestTrue(TEXT("la croute continentale flotte plus haut que l'oceanique"),
		MoyCont > MoyOcean);

	return true;
}

/**
 * LA MEME GRAINE REND LE MEME MONDE, DEUX GRAINES EN RENDENT DEUX.
 *
 * Le menu laisse choisir une graine et le cache l'emploie comme cle : si deux
 * graines se rejoignaient, chaque partie rendrait le meme monde, et la panne
 * serait TOTALE et muette. Si la meme graine divergeait, le cache rendrait un
 * monde etranger a celui qu'il pretend porter.
 *
 * ON COMPARE DES GRAINES VOISINES, parce que c'est le cas dur : un hachage
 * faible les melange, et c'est precisement ce qu'un joueur fait -- il
 * incremente.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestTectoniqueGraine,
	"Worldseed.Tectonique.LaGraineDecide",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestTectoniqueGraine::RunTest(const FString& Parameters)
{
	FWorldseedTectonicResult A, ABis, B;
	FWorldseedGeometry G;

	if (!TestTrue(TEXT("premiere generation"), Generer(*this, 20260909, A, G))
		|| !TestTrue(TEXT("seconde, meme graine"), Generer(*this, 20260909, ABis, G))
		|| !TestTrue(TEXT("troisieme, graine voisine"), Generer(*this, 20260910, B, G)))
	{
		return false;
	}

	int32 EcartsMemeGraine = 0;
	for (int32 I = 0; I < A.ElevationM.Num() && I < ABis.ElevationM.Num(); ++I)
	{
		if (A.ElevationM[I] != ABis.ElevationM[I]) { ++EcartsMemeGraine; }
	}
	TestEqual(TEXT("la meme graine rend le meme relief, au bit pres"),
		EcartsMemeGraine, 0);

	// Combien de cellules changent de cote du trait de cote d'une graine a
	// l'autre ? C'est la mesure qui parle : deux mondes differents ne partagent
	// pas leur geographie.
	int32 Bascules = 0;
	const int32 N = FMath::Min(A.ElevationM.Num(), B.ElevationM.Num());
	for (int32 I = 0; I < N; ++I)
	{
		const bool TerreA = A.ElevationM[I] > A.SeaLevelShiftM;
		const bool TerreB = B.ElevationM[I] > B.SeaLevelShiftM;
		if (TerreA != TerreB) { ++Bascules; }
	}

	const double Part = (N > 0) ? static_cast<double>(Bascules) / N : 0.0;
	AddInfo(FString::Printf(
		TEXT("graines voisines : %.1f %% des cellules changent de terre a mer"),
		Part * 100.0));

	// La part de terres etant tenue a 29,2 %, deux geographies independantes
	// different sur environ 2 x 0,292 x 0,708 = 41 % des cellules. On exige
	// beaucoup moins pour ne pas etre fragile, mais bien plus que zero.
	TestTrue(TEXT("deux graines voisines donnent deux geographies"), Part > 0.10);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
