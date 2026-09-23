// Worldseed - la passe littorale : elle sape la falaise AU RIVAGE, et ne
// touche a rien d'autre.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedCoast.h"
#include "Procedural/WorldseedLithology.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedRules.h"

#include "Misc/AutomationTest.h"

namespace
{
	/**
	 * Un banc ou le rivage est a une colonne CONNUE, et ou la maille est CELLE
	 * DU MONDE REEL.
	 *
	 * LE RIVAGE EST POSE, PAS DEDUIT. Le relief de la fixture commune est une
	 * somme d'harmoniques : on ne saurait pas dire ou passe son trait de cote
	 * sans le recalculer, et un test qui reimplemente le critere de la passe
	 * qu'il mesure valide une COPIE du mecanisme -- regle de ce depot, payee
	 * au portage de `terre.py`. D'ou une rampe dont le zero tombe, par
	 * construction, sur la colonne qu'on s'est donnee.
	 *
	 * ET LA MAILLE COMPTE AUTANT QUE LA FORME, ce que la premiere version de ce
	 * banc ignorait. La passe compare une distance au rivage EN PIXELS,
	 * convertie en metres, a un recul de 260 m :
	 * `T = Distance_px * MetresParPixel / Recul`, et la plate-forme n'existe
	 * que pour `T < PlatformFraction`, soit 0,35. A 125 m par maille -- ce que
	 * donne `Geometrie(64)` sur 8000 m de hauteur -- le PREMIER pixel de terre
	 * est deja a T = 0,48 : la passe ne peut RIEN faire, et les deux temoins
	 * du fichier ont echoue en le disant. Le monde reel travaille a 15,6 m par
	 * maille (32 km sur 2048), ou la plate-forme couvre cinq a six pixels.
	 * On reproduit donc cette maille, et non une grille commode.
	 */
	struct FRivage
	{
		FWorldseedGeometry G;
		TArray<float> Relief;
		FWorldseedLithology Litho;
		int32 ColonneDuRivage = 0;

		FRivage()
		{
			G.NY = 128;
			G.NX = 256;
			G.HeightM = 2000.0f;          // 15,6 m par maille, comme le monde
			G.LatSpanDeg = 180.0f;
			G.LatitudeMapping = TEXT("equalArea");
			G.LatitudeEqualAreaBlend = 1.0f;

			const int32 N = G.CellCount();
			ColonneDuRivage = G.NX / 4;

			Relief.SetNumUninitialized(N);
			for (int32 J = 0; J < G.NY; ++J)
			{
				for (int32 I = 0; I < G.NX; ++I)
				{
					// Huit metres par colonne : le zero tombe EXACTEMENT sur
					// `ColonneDuRivage`, et le plateau derriere monte assez
					// haut pour qu'une falaise ait de quoi se tailler.
					Relief[J * G.NX + I] =
						8.0f * static_cast<float>(I - ColonneDuRivage);
				}
			}

			Litho.Id.Init(0, N);
		}
	};
}

/**
 * LE LITTORAL NE TOUCHE QUE LE LITTORAL.
 *
 * La portee est un reglage en metres -- `ReachM` -- et la passe doit s'y
 * tenir : si elle debordait, elle remodelerait l'interieur des continents et
 * la part emergee avec, or celle-ci est un invariant du projet a 29,2 %. Le
 * commentaire de la passe le dit d'ailleurs : « LA FACE DOIT TENIR DANS UNE
 * MAILLE DE SIMULATION, sinon la falaise n'est qu'une rampe. »
 *
 * ON MESURE EN COLONNES, parce que le banc est une rampe : la distance au
 * rivage est alors une fonction connue de I, et le test n'a pas a
 * reimplementer le critere de la passe.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestLittoralPortee,
	"Worldseed.Littoral.NeTouchePasLInterieur",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestLittoralPortee::RunTest(const FString& Parameters)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("world_rules.json se lit"), R))
	{
		AddError(Erreur);
		return false;
	}

	const FWorldseedCoastRules CR = FWorldseedCoastRules::FromRules(*R);
	if (!TestTrue(TEXT("la passe littorale est active dans les regles"),
		CR.IsActive()))
	{
		return false;
	}

	FRivage B;
	const TArray<float> Avant = B.Relief;
	const FWorldseedLithologyRules LR = FWorldseedLithologyRules::FromRules(*R);

	WorldseedCoast::Build(B.G, B.Litho, LR, CR, 20260909, B.Relief);

	TestEqual(TEXT("aucune altitude NaN"), WorldseedTest::CompterNonFinis(B.Relief), 0);

	// Combien de METRES represente une colonne, et donc quelle est la derniere
	// colonne que la portee peut atteindre ?
	const float MetresParColonne = FMath::Max(B.G.MetersPerPixel(), 1.0f);
	const int32 ColonnesDePortee =
		FMath::CeilToInt(CR.ReachM / MetresParColonne) + 2;  // +2 de marge

	int32 LoinModifiees = 0;
	int32 PresModifiees = 0;
	int32 ColonnePlusLointaine = -1;

	for (int32 J = 0; J < B.G.NY; ++J)
	{
		for (int32 I = 0; I < B.G.NX; ++I)
		{
			const int32 K = J * B.G.NX + I;
			if (FMath::Abs(B.Relief[K] - Avant[K]) <= 1.0e-3f) { continue; }

			const int32 Distance = FMath::Abs(I - B.ColonneDuRivage);
			ColonnePlusLointaine = FMath::Max(ColonnePlusLointaine, Distance);

			if (Distance > ColonnesDePortee) { ++LoinModifiees; }
			else { ++PresModifiees; }
		}
	}

	AddInfo(FString::Printf(
		TEXT("portee %.0f m = %d colonnes ; modifiees jusqu'a %d colonnes du rivage ")
		TEXT("(%d pres, %d au-dela)"),
		CR.ReachM, ColonnesDePortee, ColonnePlusLointaine,
		PresModifiees, LoinModifiees));

	TestEqual(TEXT("rien n'est modifie au-dela de la portee"), LoinModifiees, 0);

	// TEMOIN : une passe qui ne ferait RIEN passerait la ligne ci-dessus.
	TestTrue(TEXT("TEMOIN : la passe modifie bien le rivage"), PresModifiees > 0);

	return true;
}

/**
 * ELLE EST INERTE QUAND SA PORTEE EST NULLE, ET DETERMINISTE SINON.
 *
 * `IsActive()` est la porte que le depot emploie pour eteindre un terme sans
 * le supprimer -- « une donnee absente doit rester sans effet » -- et c'est ce
 * qui rend les A/B possibles sur le MEME binaire. Le determinisme, lui, est
 * exige par le cache : la passe tourne avant l'ecriture du monde.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestLittoralInerteEtStable,
	"Worldseed.Littoral.InerteEteinteEtStableAllumee",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestLittoralInerteEtStable::RunTest(const FString& Parameters)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("world_rules.json se lit"), R))
	{
		AddError(Erreur);
		return false;
	}

	const FWorldseedLithologyRules LR = FWorldseedLithologyRules::FromRules(*R);
	FWorldseedCoastRules CR = FWorldseedCoastRules::FromRules(*R);

	// --- portee nulle -------------------------------------------------------
	{
		FRivage B;
		const TArray<float> Avant = B.Relief;
		FWorldseedCoastRules Eteinte = CR;
		Eteinte.ReachM = 0.0f;

		TestFalse(TEXT("portee nulle : la passe se declare inactive"),
			Eteinte.IsActive());
		WorldseedCoast::Build(B.G, B.Litho, LR, Eteinte, 20260909, B.Relief);
		TestTrue(TEXT("portee nulle : le relief est intact"), B.Relief == Avant);
	}

	// --- deux fois la meme chose --------------------------------------------
	FRivage A, C;
	const TArray<float> Reference = A.Relief;
	WorldseedCoast::Build(A.G, A.Litho, LR, CR, 20260909, A.Relief);
	WorldseedCoast::Build(C.G, C.Litho, LR, CR, 20260909, C.Relief);

	TestTrue(TEXT("deux passes identiques rendent le meme relief"),
		A.Relief == C.Relief);

	// TEMOIN : sans lui, deux reliefs intacts seraient egaux aussi.
	TestFalse(TEXT("TEMOIN : la passe a bien travaille"), A.Relief == Reference);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
