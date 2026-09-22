// Worldseed - la pile sedimentaire doit CHANGER DE BANC quand on descend.

#if WITH_DEV_AUTOMATION_TESTS

#include "Procedural/WorldseedStrata.h"

#include "Misc/AutomationTest.h"

/**
 * POURQUOI CE TEST EXISTE.
 *
 * LES PAROIS NE SONT PAS RAYEES, ET LA STRATIGRAPHIE EST POURTANT BRANCHEE.
 * Mesure du 22 septembre sur une paroi de canyon : la teinte de roche est
 * PLEINEMENT engagee -- la couper change 99,5 % des pixels de la paroi -- et
 * deplacer le toit de la pile en change 99 %, donc les bancs sont bien lus. Et
 * pourtant la paroi ne porte aucune bande : couper la teinte AUGMENTE la
 * variation de teinte au lieu de la reduire (ecart-type 12,1 contre 10,0).
 *
 * Une paroi de canyon fait une centaine de metres ; un banc en fait dix-huit a
 * trente-cinq. Elle devrait donc en traverser trois ou quatre. Ce test verifie
 * la seule chose qui puisse etre verifiee sans image : que `BancAt` CHANGE
 * quand on descend, et de la bonne quantite.
 *
 * IL NE PEUT PAS DIRE POURQUOI LES PAROIS SONT UNIES. Il peut dire si la pile
 * est en cause, et c'est ce qui manquait : tant qu'on ne l'avait pas, chaque
 * hypothese sur la peinture reposait sur l'idee non verifiee que la pile, elle,
 * fonctionnait.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestStratesPile,
	"Worldseed.Strates.LaPileChangeDeBanc",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestStratesPile::RunTest(const FString& Parameters)
{
	// UNE PILE FABRIQUEE, PAS CELLE DES REGLES. Le test doit echouer quand le
	// CODE casse, pas quand une epaisseur est recalibree -- c'est la regle du
	// depot, et la seule exception admise est un test de CALAGE, ce que
	// celui-ci n'est pas.
	//
	// LE GAUCHISSEMENT EST COUPE : il fait varier le toit avec X et Y, ce qui
	// est voulu en jeu mais rendrait ici les attentes dependantes du bruit.
	FWorldseedStratRules R;
	R.DatumM = 100.0f;
	R.WarpAmplitudeM = 0.0f;
	R.Serie.Empty();
	R.Serie.Add({ /*RockId*/ 1, /*ThicknessM*/ 10.0f });
	R.Serie.Add({ 2, 20.0f });
	R.Serie.Add({ 3, 30.0f });
	R.TotalThicknessM = 60.0f;

	TestTrue(TEXT("la pile fabriquee est active"), R.IsActive());

	using WorldseedStrata::BancAt;

	// --- 1. AU-DESSUS DU TOIT, C'EST LE BANC SOMMITAL -----------------------
	//
	// Rendre INDEX_NONE ferait apparaitre le SOCLE en altitude, ce qui est
	// l'inverse de la realite : un relief plus haut que le datum est fait de la
	// roche du sommet de la pile.
	TestEqual(TEXT("bien au-dessus du toit"), BancAt(0, 0, 500.0, R, 1), 0);
	TestEqual(TEXT("juste au toit"), BancAt(0, 0, 100.0, R, 1), 0);

	// --- 2. ON DESCEND, ON CHANGE DE BANC -----------------------------------
	TestEqual(TEXT("5 m sous le toit -> banc 0"), BancAt(0, 0, 95.0, R, 1), 0);
	TestEqual(TEXT("15 m sous le toit -> banc 1"), BancAt(0, 0, 85.0, R, 1), 1);
	TestEqual(TEXT("35 m sous le toit -> banc 2"), BancAt(0, 0, 65.0, R, 1), 2);

	// --- 3. SOUS LA PILE, C'EST LE SOCLE ------------------------------------
	TestEqual(TEXT("65 m sous le toit -> le socle"),
		BancAt(0, 0, 35.0, R, 1), static_cast<int32>(INDEX_NONE));

	// --- 4. LE BALAYAGE, ET C'EST LUI QUI REPOND A LA QUESTION POSEE --------
	//
	// Une paroi de cent metres doit traverser PLUSIEURS bancs. Trois points
	// choisis ne le prouvent pas : ils passeraient encore avec une fonction
	// qui rendrait le bon banc a ces trois altitudes et n'importe quoi entre.
	int32 Precedent = -2;
	int32 Changements = 0;
	int32 Reculs = 0;
	TSet<int32> Vus;

	for (double Z = 100.0; Z >= 30.0; Z -= 0.5)
	{
		const int32 B = BancAt(0, 0, Z, R, 1);
		Vus.Add(B);
		if (B != Precedent)
		{
			if (Precedent != -2 && B != INDEX_NONE && B < Precedent) { ++Reculs; }
			++Changements;
			Precedent = B;
		}
	}

	// Trois bancs plus le socle, donc trois frontieres franchies apres le
	// premier relevé.
	TestEqual(TEXT("la pile traverse 4 etats sur 70 m de descente"), Vus.Num(), 4);
	TestEqual(TEXT("et elle ne remonte jamais un banc en descendant"), Reculs, 0);
	TestTrue(TEXT("elle change au moins trois fois"), Changements >= 4);

	// --- 5. ET LE TEMOIN QUI FAIT MORDRE LA FIXTURE -------------------------
	//
	// Sans lui, tout ce qui precede passerait avec une pile d'un seul banc
	// epais : le balayage ne verrait qu'un etat et le test n'aurait rien dit.
	// Ce depot a deja paye cinq assertions muettes de cette famille en deux
	// jours.
	TestTrue(TEXT("la fixture porte bien plusieurs bancs"), R.Serie.Num() >= 3);
	TestNotEqual(TEXT("et deux altitudes d'une meme paroi donnent des bancs differents"),
		BancAt(0, 0, 95.0, R, 1), BancAt(0, 0, 65.0, R, 1));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
