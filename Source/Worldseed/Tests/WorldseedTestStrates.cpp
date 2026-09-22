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
	R.bPileCyclique = false;   // ce test-ci decrit la pile BORNEE
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

/**
 * LA PILE ENROULEE DOIT RAYER PARTOUT, ET SURTOUT AU-DESSUS DU TOIT.
 *
 * C'est la raison d'etre de l'enroulement : bornee, la pile rend le banc
 * sommital pour TOUT ce qui depasse le datum -- mesure, 92 % des sommets qui la
 * lisent -- donc toute paroi haute est unie. Or c'est exactement la que le
 * relief montre de la roche : les canyons de ce monde sont a 556 et 580 m pour
 * un toit a 520.
 *
 * CE TEST GARDE LA PROPRIETE, PAS LE REGLAGE. Ou poser le toit et quelle
 * epaisseur donner aux bancs se recalibrent ; qu'une paroi traverse plusieurs
 * bancs A TOUTE ALTITUDE, non.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestStratesCyclique,
	"Worldseed.Strates.PileCyclique",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestStratesCyclique::RunTest(const FString& Parameters)
{
	FWorldseedStratRules R;
	R.DatumM = 100.0f;
	R.WarpAmplitudeM = 0.0f;
	R.bPileCyclique = true;
	R.Serie.Empty();
	R.Serie.Add({ 1, 10.0f });
	R.Serie.Add({ 2, 20.0f });
	R.Serie.Add({ 3, 30.0f });
	R.TotalThicknessM = 60.0f;

	using WorldseedStrata::BancAt;

	// --- 1. PLUS JAMAIS DE SOCLE, NI EN HAUT NI EN BAS ----------------------
	//
	// Une pile qui se repete n'a ni sommet ni fond : tout point est dans un
	// banc. C'est ce qui remplace le banc zero partout au-dessus du toit.
	int32 HorsPile = 0;
	TSet<int32> VusHaut;
	TSet<int32> VusBas;

	for (double Z = -400.0; Z <= 1700.0; Z += 0.5)
	{
		const int32 B = BancAt(0, 0, Z, R, 1);
		if (!R.Serie.IsValidIndex(B)) { ++HorsPile; }
		else if (Z > R.DatumM) { VusHaut.Add(B); }
		else { VusBas.Add(B); }
	}

	TestEqual(TEXT("aucun point du monde ne tombe hors de la pile"), HorsPile, 0);

	// --- 2. ET ELLE RAYE DES DEUX COTES DU TOIT -----------------------------
	//
	// L'assertion qui porte tout : SOUS le toit la pile bornee rayait deja ;
	// c'est AU-DESSUS qu'elle rendait le banc zero pour tout. Les deux
	// ensembles doivent etre complets.
	TestEqual(TEXT("les trois bancs se voient SOUS le toit"), VusBas.Num(), 3);
	TestEqual(TEXT("les trois bancs se voient AU-DESSUS du toit"), VusHaut.Num(), 3);

	// --- 3. LA PERIODE EST CELLE DE LA PILE ---------------------------------
	//
	// Ce qui distingue un enroulement d'un bruit : deux altitudes separees d'une
	// epaisseur totale portent le MEME banc, partout et exactement.
	int32 Ecarts = 0;
	for (double Z = -300.0; Z <= 1600.0; Z += 1.0)
	{
		if (BancAt(0, 0, Z, R, 1) != BancAt(0, 0, Z + R.TotalThicknessM, R, 1))
		{
			++Ecarts;
		}
	}
	TestEqual(TEXT("la pile est periodique, de periode son epaisseur"), Ecarts, 0);

	// --- 4. LE TEMOIN : LA PILE BORNEE NE PASSE PAS CE TEST ----------------
	//
	// Sans lui, rien ne prouverait que ces assertions distinguent les deux
	// modes -- elles pourraient etre vraies des deux cotes et ne rien garder.
	FWorldseedStratRules Bornee = R;
	Bornee.bPileCyclique = false;

	TSet<int32> VusHautBornee;
	for (double Z = R.DatumM + 1.0; Z <= 1700.0; Z += 0.5)
	{
		VusHautBornee.Add(BancAt(0, 0, Z, Bornee, 1));
	}
	TestEqual(TEXT("temoin : bornee, TOUT le dessus du toit est un seul banc"),
		VusHautBornee.Num(), 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
