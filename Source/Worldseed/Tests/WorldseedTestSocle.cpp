// Worldseed - ou le socle affleure. Le defaut garde ici -- le bassin plat
// declare socle par la seule convergence -- a coute 0,00 % de gres, donc
// AUCUNE table dans tout le monde, et aucune sonde ne le voyait.

#if WITH_DEV_AUTOMATION_TESTS

#include "Procedural/WorldseedSocle.h"
#include "Procedural/WorldseedRules.h"

#include "Misc/AutomationTest.h"

namespace
{
	/**
	 * UNE CHAINE ET SON BASSIN D'AVANT-PAYS, dans la MEME bande convergente.
	 *
	 * LA FIXTURE DOIT CONTENIR LE CAS, et ce depot a paye trois fois de suite
	 * l'oubli -- un test de comblement sur un relief sans cuvette, un test de
	 * borne d'altitude sur une rampe uniforme, un test d'apparence sur un
	 * registre non charge. Les trois passaient sans rien mesurer.
	 *
	 * Le cas ici demande TROIS choses a la fois : une convergence qui declare
	 * TOUT orogenique -- sans quoi le relief n'aurait rien a trancher --, un
	 * massif reellement accidente, et un bassin reellement PLAT assez loin du
	 * massif pour que la dilatation du relief ne l'atteigne pas.
	 *
	 * LA GEOMETRIE EST CALCULEE, PAS DEVINEE. Maille 8000/(129-1) = 62,5 m ;
	 * le relief se mesure sur une grille grossie de 8, donc 32 x 16 cellules de
	 * 500 m ; un rayon de 1000 m dilate donc de DEUX cellules grossies. Le
	 * massif occupe les colonnes fines 0..102, soit grossies 0..12 ; dilatees,
	 * 0..14. Le bassin profond commence donc a la colonne grossie 16, c'est-a-
	 * dire la colonne fine 128 -- et c'est la qu'on l'interroge.
	 */
	constexpr int32 WorldseedSocleFinDuMassif = 103;   // colonnes fines
	constexpr int32 WorldseedSocleDebutDuBassin = 128; // apres dilatation

	FWorldseedGeometry WorldseedSocleGeometrie()
	{
		FWorldseedGeometry G;
		G.NY = 129;
		G.NX = 258;
		G.HeightM = 8000.0f;
		G.LatSpanDeg = 180.0f;
		G.LatitudeMapping = TEXT("equalArea");
		G.LatitudeEqualAreaBlend = 1.0f;
		return G;
	}

	TArray<float> WorldseedSocleRelief(const FWorldseedGeometry& G)
	{
		TArray<float> H;
		H.SetNumUninitialized(G.CellCount());
		for (int32 J = 0; J < G.NY; ++J)
		{
			for (int32 I = 0; I < G.NX; ++I)
			{
				// LE MASSIF alterne d'une cellule a l'autre : chaque cellule
				// grossie voit donc 200 comme 1200, et porte mille metres de
				// relief. LE BASSIN est DEAD FLAT a 300 m -- plus haut que le
				// pied du massif, pour qu'aucun test d'altitude ne puisse le
				// distinguer : seul le RELIEF le separe, et c'est la question.
				H[J * G.NX + I] = (I < WorldseedSocleFinDuMassif)
					? ((I % 2 == 0) ? 200.0f : 1200.0f)
					: 300.0f;
			}
		}
		return H;
	}

	FWorldseedSocleRegles WorldseedSocleRegles(float PartAccidentee)
	{
		FWorldseedSocleRegles R;
		R.Convergence = 0.35f;
		R.ElevationM = 100000.0f;   // hors d'atteinte : seule la convergence parle
		R.PartHaute = 0.0f;         // donc pas de quantile d'altitude non plus
		R.PartAccidentee = PartAccidentee;
		R.ReliefRayonM = 1000.0f;
		return R;
	}
}

/**
 * LE BASSIN D'AVANT-PAYS N'EST PAS UN SOCLE.
 *
 * LA REGLE EST GEOLOGIQUE : « un orogene expose son socle -- soulevement et
 * DECAPAGE emportent la couverture sedimentaire ». Le soulevement seul ne
 * decape rien ; ce qui decape est le RELIEF. En ne testant que la convergence,
 * on declarait socle TOUTE la bande convergente, bassin plat compris.
 *
 * OR UN BASSIN D'AVANT-PAYS EST L'INVERSE D'UN SOCLE : il est PLEIN des
 * sediments arraches a la chaine voisine, et c'est litteralement le decor des
 * mesas reelles. Mesure du defaut sur le monde entier, avant correction :
 * 70,80 % de granite et 29,13 % de basalte pour **0,00 % de gres** sur le
 * terrain que les tables demandent -- donc pas une seule mesa.
 *
 * LE TEMOIN EST LA MOITIE QUI PROUVE : il rejoue le monde d'avant, sans tri par
 * le relief, et montre le bassin declare socle. Sans lui, « le bassin n'est pas
 * socle » pourrait tenir a la fixture plutot qu'a la regle.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestSocleAvantPays,
	"Worldseed.Socle.LeBassinDAvantPaysNestPasUnSocle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestSocleAvantPays::RunTest(const FString& Parameters)
{
	const FWorldseedGeometry G = WorldseedSocleGeometrie();
	const TArray<float> H = WorldseedSocleRelief(G);

	// TOUTE la carte converge : c'est ce qui isole la question. Si le relief ne
	// tranchait rien, tout serait socle.
	TArray<float> Convergence;
	Convergence.Init(0.50f, G.CellCount());

	auto PartSocle = [&](int32 IDebut, int32 IFin, const TArray<uint8>& Masque)
	{
		int32 N = 0, S = 0;
		for (int32 J = 0; J < G.NY; ++J)
		{
			for (int32 I = IDebut; I < IFin; ++I)
			{
				++N;
				if (Masque[J * G.NX + I] != 0) { ++S; }
			}
		}
		return (N > 0) ? 100.0f * S / N : 0.0f;
	};

	// --- LE TEMOIN D'ABORD : le monde d'avant la correction -------------------
	TArray<uint8> SansTri;
	FWorldseedSocleReleve ReleveSansTri;
	WorldseedSocle::Marquer(G, H, Convergence, WorldseedSocleRegles(1.0f),
		SansTri, ReleveSansTri);

	const float BassinSansTri =
		PartSocle(WorldseedSocleDebutDuBassin, G.NX, SansTri);
	AddInfo(FString::Printf(
		TEXT("TEMOIN, sans tri par le relief : %.1f %% du bassin declare SOCLE ")
		TEXT("(%d orogeniques, %d socles)"),
		BassinSansTri, ReleveSansTri.Orogenes, ReleveSansTri.Socles));

	TestTrue(TEXT("TEMOIN : sans le decapage, le bassin PLAT est declare socle ")
		TEXT("-- c'est le defaut que ce test garde"),
		BassinSansTri > 99.0f);

	// --- ET LE COMPORTEMENT VOULU ---------------------------------------------
	TArray<uint8> AvecTri;
	FWorldseedSocleReleve Releve;
	WorldseedSocle::Marquer(G, H, Convergence, WorldseedSocleRegles(0.40f),
		AvecTri, Releve);

	const float BassinAvecTri =
		PartSocle(WorldseedSocleDebutDuBassin, G.NX, AvecTri);
	const float MassifAvecTri = PartSocle(0, 80, AvecTri);

	AddInfo(FString::Printf(
		TEXT("avec le decapage : massif %.1f %% socle, bassin %.1f %% socle ")
		TEXT("| relief exige %.0f m | %d orogeniques, %d gardes"),
		MassifAvecTri, BassinAvecTri, Releve.SeuilReliefM,
		Releve.Orogenes, Releve.Socles));

	TestTrue(TEXT("le bassin plat est RENDU au bassin"), BassinAvecTri < 1.0f);
	TestTrue(TEXT("et le massif, lui, reste socle"), MassifAvecTri > 99.0f);

	// LE RELEVE DOIT LE DIRE, sinon le journal du jeu ne pourrait pas le
	// rapporter -- et c'est le COMPTE qui tranche, jamais le temps.
	TestTrue(TEXT("le releve compte des orogeniques"), Releve.Orogenes > 0);
	TestTrue(TEXT("et en rend une part au bassin"),
		Releve.Socles < Releve.Orogenes);
	TestTrue(TEXT("le relief exige est strictement positif"),
		Releve.SeuilReliefM > 0.0f);

	return true;
}

/**
 * LE SEUIL D'ALTITUDE NE REGARDE QUE LES TERRES.
 *
 * Y inclure les fonds marins reviendrait a mesurer la part haute d'une
 * distribution que LA MER domine -- soixante-dix pour cent de l'echantillon
 * sous zero sur un monde ordinaire, donc un seuil qui tombe au niveau de la
 * mer et declare socle tout ce qui emerge.
 *
 * ET C'EST UN SEUIL PAR QUANTILE, PAS EN METRES : « une constante metrique en
 * dur suffit a fausser un monde entier », et un seuil cale sur un monde de 8 km
 * avale tout le relief d'un monde de 64.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestSocleSeuil,
	"Worldseed.Socle.LeSeuilNeRegardeQueLesTerres",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestSocleSeuil::RunTest(const FString& Parameters)
{
	// Cent terres de 1 a 100 m, NEUF CENTS cellules de mer : la mer ecrase
	// l'echantillon si on la compte, et c'est tout l'objet.
	TArray<float> H;
	TArray<float> TerresSeules;
	for (int32 I = 1; I <= 100; ++I)
	{
		H.Add(static_cast<float>(I));
		TerresSeules.Add(static_cast<float>(I));
	}
	for (int32 I = 0; I < 900; ++I)
	{
		H.Add(-500.0f);
	}

	const float Seuil = WorldseedSocle::SeuilElevationM(H, 0.10f, 12345.0f);
	AddInfo(FString::Printf(
		TEXT("100 terres (1..100 m) noyees dans 900 cellules de mer -> seuil %.1f m"),
		Seuil));

	TestTrue(TEXT("le seuil tombe dans le haut des TERRES, vers 90 m"),
		Seuil > 85.0f && Seuil < 95.0f);

	// LE TEMOIN QUI DONNE SON SENS AU TEST : compter la mer donnerait un seuil
	// tout autre. On le calcule pour le dire, plutot que de l'affirmer.
	const float SeuilAvecLaMer = [&H]
	{
		TArray<float> Tout = H;
		Tout.Sort();
		const float Pos = 0.90f * (Tout.Num() - 1);
		const int32 K = FMath::FloorToInt(Pos);
		return FMath::Lerp(Tout[K], Tout[FMath::Min(K + 1, Tout.Num() - 1)], Pos - K);
	}();
	AddInfo(FString::Printf(
		TEXT("TEMOIN : le meme quantile en comptant la mer donnerait %.1f m"),
		SeuilAvecLaMer));
	TestTrue(TEXT("TEMOIN : compter la mer donnerait un seuil tout autre"),
		FMath::Abs(SeuilAvecLaMer - Seuil) > 50.0f);

	// --- le repli, quand la part est hors de ]0..1[ ---------------------------
	TestEqual(TEXT("une part nulle retombe sur la valeur metrique"),
		WorldseedSocle::SeuilElevationM(H, 0.0f, 12345.0f), 12345.0f);
	TestEqual(TEXT("une part de 1 aussi"),
		WorldseedSocle::SeuilElevationM(H, 1.0f, 12345.0f), 12345.0f);

	// --- et un monde SANS terres ---------------------------------------------
	TArray<float> ToutEnMer;
	ToutEnMer.Init(-300.0f, 500);
	TestEqual(TEXT("un monde sans terre emergee retombe sur la valeur metrique"),
		WorldseedSocle::SeuilElevationM(ToutEnMer, 0.15f, 777.0f), 777.0f);

	return true;
}

/**
 * LE RELIEF LOCAL MESURE UN RELIEF, ET IL EST VALIDE SUR DES CAS CONNUS.
 *
 * L'INSTRUMENT SE VALIDE AVANT DE S'EN SERVIR. Ce depot a conclu a un defaut de
 * materiau sur un compteur qui rendait zero sur le cas TEMOIN comme sur le sien
 * -- l'herbe de Landscape -- et perdu une heure. Une plaine doit rendre zero,
 * un massif son amplitude : si ces deux-la ne sortent pas, la decision du socle
 * ne vaut rien quoi qu'elle reponde.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestSocleRelief,
	"Worldseed.Socle.LeReliefLocalMesureUnRelief",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestSocleRelief::RunTest(const FString& Parameters)
{
	const FWorldseedGeometry G = WorldseedSocleGeometrie();

	// --- une plaine parfaite --------------------------------------------------
	TArray<float> Plaine;
	Plaine.Init(300.0f, G.CellCount());

	TArray<float> Bas, Haut;
	int32 PX = 0, PY = 0;
	WorldseedSocle::ReliefLocal(G, Plaine, 1000.0f, 8, Bas, Haut, PX, PY);

	TestEqual(TEXT("la grille grossie fait NX/8 colonnes"), PX, G.NX / 8);
	TestEqual(TEXT("et NY/8 lignes"), PY, G.NY / 8);

	float PireReliefDePlaine = 0.0f;
	for (int32 P = 0; P < PX * PY; ++P)
	{
		PireReliefDePlaine = FMath::Max(PireReliefDePlaine, Haut[P] - Bas[P]);
	}
	AddInfo(FString::Printf(TEXT("plaine parfaite -> relief maximal %.3f m"),
		PireReliefDePlaine));
	TestTrue(TEXT("une plaine ne porte aucun relief"),
		PireReliefDePlaine < 1.0e-3f);

	// --- puis le massif de la fixture ----------------------------------------
	const TArray<float> H = WorldseedSocleRelief(G);
	WorldseedSocle::ReliefLocal(G, H, 1000.0f, 8, Bas, Haut, PX, PY);

	// Le coeur du massif porte les mille metres qu'on y a mis...
	const int32 CoeurDuMassif = (PY / 2) * PX + 4;
	const float ReliefDuMassif = Haut[CoeurDuMassif] - Bas[CoeurDuMassif];

	// ...et le bassin profond, rien. Colonne grossie 20 : bien au-dela des deux
	// cellules de dilatation qui suivent la fin du massif.
	const int32 CoeurDuBassin = (PY / 2) * PX + 20;
	const float ReliefDuBassin = Haut[CoeurDuBassin] - Bas[CoeurDuBassin];

	AddInfo(FString::Printf(TEXT("massif %.0f m de relief, bassin %.0f m"),
		ReliefDuMassif, ReliefDuBassin));

	TestTrue(TEXT("le massif porte les mille metres qu'on y a poses"),
		FMath::IsNearlyEqual(ReliefDuMassif, 1000.0f, 1.0f));
	TestTrue(TEXT("et le bassin profond n'en porte aucun"),
		ReliefDuBassin < 1.0e-3f);

	// --- UN RAYON PLUS LARGE ETALE PLUS LOIN, et c'est la propriete qui compte -
	//
	// Si le rayon n'etait pas lu, massif et bassin garderaient les memes
	// chiffres quel que soit le reglage -- et ce depot a rencontre SIX FOIS le
	// signe « deux mesures identiques pour deux reglages differents ».
	WorldseedSocle::ReliefLocal(G, H, 6000.0f, 8, Bas, Haut, PX, PY);
	const float BassinLarge = Haut[CoeurDuBassin] - Bas[CoeurDuBassin];
	AddInfo(FString::Printf(
		TEXT("a 6000 m de rayon, le bassin voit %.0f m -- le massif l'atteint"),
		BassinLarge));
	TestTrue(TEXT("le rayon est REELLEMENT lu : a 6000 m le massif atteint le bassin"),
		BassinLarge > 900.0f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
