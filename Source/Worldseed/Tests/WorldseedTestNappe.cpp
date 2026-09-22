// Worldseed - la nappe vue ne doit jamais noyer une terre ni elever un sommet.

#if WITH_DEV_AUTOMATION_TESTS

#include "Procedural/WorldseedGroundProxy.h"

#include "Misc/AutomationTest.h"

/**
 * POURQUOI CE TEST EXISTE, ET CE QU'IL AURAIT ATTRAPE.
 *
 * LA NAPPE VUE NOYAIT UN SIXIEME DES TERRES, ET RIEN NE LE DISAIT. Elle
 * s'enfonce de cent vingt-cinq metres pour passer sous la bande creusable et
 * ne boucher aucune cavite ; mais elle porte le relief du monde ENTIER, et
 * tout ce qui culmine plus bas passait alors sous le niveau de la mer, ou
 * l'ocean le recouvrait. Mesure du 22 septembre : **398 839 sommets de terre
 * sur 2 449 474**, soit 16,3 %. Vu depuis un sommet, une vallee verte avec sa
 * plage se lisait comme une baie.
 *
 * AUCUNE SONDE NE POUVAIT LE VOIR. Elles mesurent des pentes, des parts de
 * biomes, des comptes de chunks -- jamais ce que le DECOR montre. Le defaut
 * n'est apparu qu'en cadrant l'horizon a l'image, et il a fallu un temoin
 * (`-WorldseedNappeVue=0`) pour l'attribuer.
 *
 * CE QUE CE TEST GARDE EST L'INVARIANT, PAS LE CHIFFRE. Le pourcentage depend
 * du monde et bougera a chaque regeneration ; la propriete, elle, ne doit
 * jamais bouger : le plafond ne descend pas sous la marge de mer, et il
 * n'eleve jamais un sommet.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestNappePlafond,
	"Worldseed.Nappe.PlafondDeMer",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestNappePlafond::RunTest(const FString& Parameters)
{
	// Les valeurs du monde de reference : bande creusable 100 m plus 25 de
	// marge, et la marge de mer des cavites.
	constexpr double Enfoncement = 125.0;
	constexpr double MargeMer = 5.0;

	using WorldseedNappe::PlafondDEnfoncement;

	// --- 1. LE RELIEF HAUT S'ENFONCE ENTIEREMENT ----------------------------
	//
	// C'est la raison d'etre de l'enfoncement : sur les hauteurs, ou vivent
	// TOUTES les cavites, la nappe doit passer sous la bande creusable. Un
	// plafond qui morderait ici rouvrirait le decor dans les grottes.
	TestEqual(TEXT("a 500 m, l'enfoncement est plein"),
		PlafondDEnfoncement(500.0, MargeMer, Enfoncement), Enfoncement);
	TestEqual(TEXT("a 130 m, l'enfoncement est plein"),
		PlafondDEnfoncement(130.0, MargeMer, Enfoncement), Enfoncement);

	// --- 2. LE RIVAGE EST EPINGLE -------------------------------------------
	//
	// C'EST CE QUI REND LA RAMPE SURE. L'enfoncement suit la camera, donc le
	// relief lointain change d'altitude quand le joueur marche ; sur un trait
	// de cote cela se verrait. A zero d'enfoncement, il n'y a rien a faire
	// bouger.
	TestEqual(TEXT("au niveau de la marge, on n'enfonce plus du tout"),
		PlafondDEnfoncement(MargeMer, MargeMer, Enfoncement), 0.0);
	TestEqual(TEXT("sous la marge, on n'enfonce pas davantage"),
		PlafondDEnfoncement(2.0, MargeMer, Enfoncement), 0.0);

	// --- 3. ON N'ELEVE JAMAIS UN SOMMET -------------------------------------
	//
	// L'ASSERTION QUI REPOND A LA QUESTION POSEE PAR LE PROPRIETAIRE : « si
	// cela fait apparaitre des terres censees etre sous l'eau, cela ne me
	// convient pas ». Un enfoncement negatif REMONTERAIT le decor et ferait
	// emerger du fond marin. Il ne peut pas etre negatif, et le fond marin le
	// prouve : sous zero, le plafond vaut zero.
	TestEqual(TEXT("un fond marin ne s'enfonce pas, donc il ne remonte pas"),
		PlafondDEnfoncement(-50.0, MargeMer, Enfoncement), 0.0);
	TestEqual(TEXT("le plancher du monde non plus"),
		PlafondDEnfoncement(-371.0, MargeMer, Enfoncement), 0.0);

	// --- 4. LE BALAYAGE, PARCE QUE TROIS POINTS NE SONT PAS UNE PROPRIETE ---
	//
	// Les cas ci-dessus sont des points choisis ; ils passeraient encore avec
	// une formule fausse entre eux. On balaie donc toute la plage d'altitudes
	// du monde de reference et l'on verifie les DEUX invariants a chaque pas.
	int32 Eleves = 0;
	int32 SousLaMarge = 0;
	int32 TropEnfonces = 0;
	double PireAltitude = 0.0;

	for (double Alt = -400.0; Alt <= 1800.0; Alt += 0.5)
	{
		const double E = PlafondDEnfoncement(Alt, MargeMer, Enfoncement);

		if (E < 0.0) { ++Eleves; }
		if (E > Enfoncement) { ++TropEnfonces; }

		// L'altitude APRES enfoncement ne descend jamais sous la marge -- sauf
		// la ou elle etait deja dessous, auquel cas elle ne bouge pas.
		const double Apres = Alt - E;
		if (Alt > MargeMer && Apres < MargeMer - KINDA_SMALL_NUMBER)
		{
			++SousLaMarge;
			PireAltitude = Alt;
		}
	}

	TestEqual(TEXT("aucun sommet n'est ELEVE sur toute la plage"), Eleves, 0);
	TestEqual(TEXT("aucun sommet n'est enfonce au-dela du plein"), TropEnfonces, 0);
	TestEqual(TEXT("aucune terre ne passe sous la marge de mer"), SousLaMarge, 0);

	if (SousLaMarge > 0)
	{
		AddError(FString::Printf(
			TEXT("derniere altitude fautive : %.1f m"), PireAltitude));
	}

	// --- 5. ET LA FIXTURE DOIT MORDRE ---------------------------------------
	//
	// Sans ceci, tout ce qui precede passerait avec un plafond qui ne plafonne
	// rien -- il suffirait que la plage balayee soit entierement au-dessus de
	// l'enfoncement. Le depot a deja paye quatre assertions muettes de cette
	// famille en une journee : deux `nullptr` compares egaux, un comblement
	// sans cuvette, une rampe uniforme sans plat, un registre non charge.
	TestTrue(TEXT("le plafond MORD quelque part dans la plage balayee"),
		PlafondDEnfoncement(60.0, MargeMer, Enfoncement) < Enfoncement);
	TestEqual(TEXT("et il mord de la bonne quantite"),
		PlafondDEnfoncement(60.0, MargeMer, Enfoncement), 55.0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
