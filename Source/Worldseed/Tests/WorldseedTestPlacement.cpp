// Worldseed - ou le joueur nait : pente, sol plein, et la borne d'altitude.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedDensity.h"
#include "Procedural/WorldseedPlacement.h"
#include "Procedural/WorldseedRules.h"

#include "Misc/AutomationTest.h"

/**
 * LE PREMIER ENDROIT QUE LE JOUEUR VOIT, ET IL N'ETAIT GARDE PAR RIEN.
 *
 * LE DEFAUT MESURE : qui visait un sommet naissait a son pied. Sur deux parties
 * independantes -- latitudes 73,1 et 15,3 degres, donc sans rapport de terrain
 * -- le joueur est ne 89 et 92 metres SOUS le point choisi ; une autre fois
 * 357 metres plus bas, et une autre encore 914, au niveau de la mer.
 *
 * LA CAUSE N'ETAIT PAS L'ABSENCE DE BORNE -- elle existait -- mais une FALAISE
 * DE POLITIQUE : quarante metres, puis l'infini, en un cran. Quand la passe
 * bornee echouait, le repli repartait sans aucune contrainte d'altitude et
 * prenait la plaine.
 *
 * CES TESTS N'ONT PAS BESOIN D'UN MONDE. Le champ se bat sur un relief
 * FABRIQUE dont on connait la pente au degre pres -- c'est la seule facon de
 * prouver une regle de placement, le relief d'un vrai monde changeant a chaque
 * regeneration. Le releve de reference du registre le montre bien : ses
 * coordonnees pointent aujourd'hui trois cent metres sous la mer.
 */
namespace
{
	/**
	 * Un champ de densite sur un relief ANALYTIQUE, dont la pente est connue.
	 *
	 * Le relief est une rampe le long de X : `Pente` metres de montee par
	 * metre parcouru. La pente vraie vaut donc `atan(Pente)`, et c'est contre
	 * cette valeur-la que la mesure se juge -- pas contre une autre mesure.
	 */
	struct FRampe
	{
		FWorldseedGeometry Geo;
		TArray<float> Relief;
		FWorldseedDensityRules Regles;
		FWorldseedDensity Champ;

		FRampe(double Pente, double AltitudeAuCentreM)
		{
			Geo = WorldseedTest::Geometrie(64);

			const int32 NX = Geo.NX;
			const int32 NY = Geo.NY;
			Relief.SetNumUninitialized(Geo.CellCount());

			const double LargeurM = Geo.WidthM();
			for (int32 J = 0; J < NY; ++J)
			{
				for (int32 I = 0; I < NX; ++I)
				{
					// X du centre de la cellule, dans le repere du monde.
					const double XM = (static_cast<double>(I) / NX - 0.5) * LargeurM;
					Relief[J * NX + I] = static_cast<float>(
						AltitudeAuCentreM + Pente * XM);
				}
			}

			// LE BRUIT EST COUPE : ce test porte sur la REGLE de placement, pas
			// sur le grain du terrain. Un surplomb ou un detail de deux metres
			// et demi rendrait la pente locale impredictible, et l'on ne
			// saurait plus si un ecart vient de la regle ou du bruit.
			Regles.OverhangAmplitudeM = 0.0f;
			Regles.DetailAmplitudeM = 0.0f;

			Champ.Init(Geo, Relief, 1.0f, 4242, Regles);
		}
	};

	/**
	 * UNE COLLINE : une plaine, un versant raide, un sommet plat.
	 *
	 * ELLE EXISTE PARCE QUE LA RAMPE UNIFORME NE CONTENAIT PAS LE CAS. Le
	 * premier jet du test de borne tournait sur une rampe a 31 degres partout :
	 * la recherche NON bornee n'y trouvait rien non plus -- « rien trouve dans
	 * les 384 m » -- donc le temoin ne s'executait pas et le test passait sans
	 * jamais comparer quoi que ce soit.
	 *
	 * C'est la troisieme forme de la meme faute dans ce depot, apres les biomes
	 * vides et le comblement sans cuvette : **une fixture qui ne contient pas
	 * le cas ne rend pas le test indulgent, elle le rend MUET**.
	 *
	 * Le defaut reel demande trois choses a la fois -- un versant trop raide
	 * pour qu'on y naisse, du PLAT plus bas, et le tout dans les 384 metres que
	 * la spirale fouille. C'est exactement cette colline.
	 *
	 * La maille fait ici quinze metres, contre cent vingt-cinq pour la grille
	 * de la rampe : un versant de deux cents metres n'y tiendrait pas
	 * autrement, et l'interpolation bicubique l'arrondirait en pente douce.
	 */
	struct FColline
	{
		static constexpr double PlaineM = 20.0;
		static constexpr double SommetM = 260.0;
		static constexpr double DemiVersantM = 200.0;

		FWorldseedGeometry Geo;
		TArray<float> Relief;
		FWorldseedDensityRules Regles;
		FWorldseedDensity Champ;

		FColline()
		{
			Geo = WorldseedTest::Geometrie(512);

			const int32 NX = Geo.NX;
			const int32 NY = Geo.NY;
			const double LargeurM = Geo.WidthM();
			Relief.SetNumUninitialized(Geo.CellCount());

			for (int32 J = 0; J < NY; ++J)
			{
				for (int32 I = 0; I < NX; ++I)
				{
					const double XM = (static_cast<double>(I) / NX - 0.5) * LargeurM;
					Relief[J * NX + I] = static_cast<float>(Altitude(XM));
				}
			}

			Regles.OverhangAmplitudeM = 0.0f;
			Regles.DetailAmplitudeM = 0.0f;
			Champ.Init(Geo, Relief, 1.0f, 4242, Regles);
		}

		static double Altitude(double XM)
		{
			if (XM <= -DemiVersantM) { return PlaineM; }
			if (XM >= DemiVersantM) { return SommetM; }
			return PlaineM + (SommetM - PlaineM)
				* (XM + DemiVersantM) / (2.0 * DemiVersantM);
		}
	};
}

/**
 * LA PENTE MESUREE EST LA PENTE VRAIE.
 *
 * Elle se juge contre `atan` de la rampe analytique, donc contre une valeur qui
 * ne doit rien a une autre mesure. C'est ce que `ProbeTransvoxel` n'avait pas
 * fait -- elle comparait un mailleur a ses PROPRES normales, ne pouvait que se
 * donner raison, et a pose la mauvaise valeur pendant des jours.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestPlacementPente,
	"Worldseed.Placement.Pente",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestPlacementPente::RunTest(const FString& Parameters)
{
	// Des pentes franches et bien separees, de part et d'autre des seuils qui
	// comptent : douze degres pour une naissance libre, vingt-cinq pour un
	// depart choisi, quarante-cinq pour la limite de marche du pion.
	const double Pentes[] = { 0.0, 0.1, 0.3, 0.6, 1.0 };

	for (const double P : Pentes)
	{
		const FRampe R(P, 500.0);
		const float Attendu = FMath::RadiansToDegrees(FMath::Atan(P));
		const float Mesure = WorldseedPlacement::PenteDeg(R.Champ, 0.0, 0.0);

		AddInfo(FString::Printf(TEXT("rampe %.1f m/m : attendu %.2f deg, mesure %.2f"),
			P, Attendu, Mesure));

		TestTrue(FString::Printf(TEXT("la pente de la rampe %.1f est juste"), P),
			FMath::Abs(Mesure - Attendu) < 0.5f);
	}

	return true;
}

/**
 * LA BORNE D'ALTITUDE TIENT, ET SANS ELLE LA RECHERCHE DESCEND.
 *
 * C'EST LE TEMOIN QUI DONNE SON SENS AU TEST. La meme rampe, la meme recherche,
 * et seule la borne change : sans elle on part chercher le plat en bas, avec
 * elle on reste sur le relief vise. Sans ce couple, « la recherche a rendu un
 * point proche en altitude » ne prouverait rien -- elle aurait pu le rendre
 * parce que la rampe est douce.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestPlacementBorne,
	"Worldseed.Placement.BorneAltitude",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestPlacementBorne::RunTest(const FString& Parameters)
{
	const FColline C;

	// ON SE PLACE A MI-VERSANT : trop raide pour naitre, avec du plat a deux
	// cents metres en contrebas -- donc DANS les 384 que la spirale fouille.
	const FVector2D Vise(0.0, 0.0);
	const float Reference = C.Champ.SurfaceHeightM(Vise.X, Vise.Y);
	const float PenteVersant = WorldseedPlacement::PenteDeg(C.Champ, Vise.X, Vise.Y);

	AddInfo(FString::Printf(
		TEXT("versant a %.1f deg, altitude visee %.1f m (plaine %.0f, sommet %.0f)"),
		PenteVersant, Reference, FColline::PlaineM, FColline::SommetM));

	if (!TestTrue(TEXT("le versant est plus raide que les deux seuils"),
		PenteVersant > 25.0f))
	{
		return false;
	}

	// --- SANS BORNE : elle trouve, et elle trouve BIEN PLUS BAS -------------
	double SX = 0.0, SY = 0.0;
	float SSurface = 0.0f, SPente = 0.0f;
	const bool bSans = WorldseedPlacement::SolPlat(C.Champ, Vise,
		/*PenteMaxDeg=*/12.0f, /*EcartAltitudeMaxM=*/0.0f, /*AltitudeRefM=*/0.0f,
		SX, SY, SSurface, SPente);

	if (!TestTrue(TEXT("le temoin TROUVE quelque chose, sinon il ne compare rien"),
		bSans))
	{
		return false;
	}

	AddInfo(FString::Printf(
		TEXT("sans borne : (%.0f, %.0f) m, altitude %.1f m, soit %+.0f m"),
		SX, SY, SSurface, SSurface - Reference));

	// C'EST LE DEFAUT, REPRODUIT. Sans borne, la recherche quitte le relief
	// vise et descend chercher le plat : c'est ainsi que le joueur naissait 89,
	// 92, 357 puis 914 metres sous le point qu'il avait choisi.
	TestTrue(TEXT("sans borne, la recherche descend franchement"),
		FMath::Abs(SSurface - Reference) > 40.0f);

	// --- AVEC LA BORNE : elle refuse plutot que de descendre ----------------
	double BX = 0.0, BY = 0.0;
	float BSurface = 0.0f, BPente = 0.0f;
	const bool bAvec = WorldseedPlacement::SolPlat(C.Champ, Vise,
		/*PenteMaxDeg=*/25.0f, /*EcartAltitudeMaxM=*/40.0f, Reference,
		BX, BY, BSurface, BPente);

	AddInfo(FString::Printf(TEXT("avec borne : %s"),
		bAvec ? *FString::Printf(TEXT("(%.0f, %.0f) m, %+.0f m"),
			BX, BY, BSurface - Reference)
			: TEXT("rien trouve -- c'est l'echec franc, et le point vise est tenu")));

	// LA BORNE EST UNE PROMESSE : si la recherche rend quelque chose, l'ecart
	// est tenu. Qu'elle rende ou non depend du terrain, et c'est precisement
	// pourquoi la politique d'echec se decide ailleurs -- ici elle refuse, et
	// l'acteur tient alors le point vise.
	if (bAvec)
	{
		TestTrue(TEXT("l'ecart d'altitude reste sous la borne"),
			FMath::Abs(BSurface - Reference) <= 40.0f);
		TestTrue(TEXT("la pente rendue respecte son plafond"), BPente <= 25.0f);
	}
	else
	{
		AddInfo(TEXT("sur ce versant, la borne ne laisse rien passer : ")
			TEXT("c'est le cas ou l'acteur tient le point vise"));
	}

	// --- ET SUR LA PLAINE, LA MEME BORNE LAISSE PASSER ----------------------
	//
	// Sans ce troisieme cas, « la borne refuse » pourrait vouloir dire « la
	// borne refuse TOUJOURS », et le test ne distinguerait pas une regle d'une
	// panne.
	const FVector2D SurLaPlaine(-FColline::DemiVersantM - 300.0, 0.0);
	const float RefPlaine = C.Champ.SurfaceHeightM(SurLaPlaine.X, SurLaPlaine.Y);

	double PX = 0.0, PY = 0.0;
	float PSurface = 0.0f, PPente = 0.0f;
	const bool bPlaine = WorldseedPlacement::SolPlat(C.Champ, SurLaPlaine,
		25.0f, 40.0f, RefPlaine, PX, PY, PSurface, PPente);

	if (TestTrue(TEXT("sur la plaine, la borne laisse passer"), bPlaine))
	{
		AddInfo(FString::Printf(
			TEXT("sur la plaine : (%.0f, %.0f) m, %+.1f m, pente %.1f deg"),
			PX, PY, PSurface - RefPlaine, PPente));
		TestTrue(TEXT("et l'ecart y est quasi nul"),
			FMath::Abs(PSurface - RefPlaine) < 5.0f);
	}

	return true;
}

/**
 * LA RECHERCHE REFUSE LA MER, ET LE VIDE SOUS LES PIEDS.
 *
 * Deux refus qui ne se voient jamais quand ils marchent, et qui coutent cher
 * quand ils tombent : le depot a mesure « joueur tenu a (0, 0) m, surface
 * -153,8 m » -- cent cinquante metres sous le niveau de la mer -- et une
 * colonne sur huit de ce monde porte une galerie.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestPlacementRefus,
	"Worldseed.Placement.Refus",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestPlacementRefus::RunTest(const FString& Parameters)
{
	// --- TOUT SOUS LA MER : rien ne doit convenir, nulle part ---------------
	{
		const FRampe Noyee(0.0, -50.0);

		double X = 0.0, Y = 0.0;
		float Surface = 0.0f, Pente = 0.0f;
		const bool bTrouve = WorldseedPlacement::SolPlat(Noyee.Champ,
			FVector2D::ZeroVector, 12.0f, 0.0f, 0.0f, X, Y, Surface, Pente);

		TestFalse(TEXT("aucun sol praticable sur un monde entierement noye"),
			bTrouve);
	}

	// --- ET LE TEMOIN : le MEME relief remonte au-dessus de zero convient ---
	//
	// Sans lui, le refus ci-dessus pourrait venir de n'importe quoi -- une
	// rampe mal construite, un champ vide -- et l'on croirait avoir eprouve la
	// garde de la mer alors qu'on aurait eprouve sa propre fixture.
	{
		const FRampe Emergee(0.0, 200.0);

		double X = 0.0, Y = 0.0;
		float Surface = 0.0f, Pente = 0.0f;
		const bool bTrouve = WorldseedPlacement::SolPlat(Emergee.Champ,
			FVector2D::ZeroVector, 12.0f, 0.0f, 0.0f, X, Y, Surface, Pente);

		if (TestTrue(TEXT("le meme relief emerge, lui, convient"), bTrouve))
		{
			AddInfo(FString::Printf(TEXT("plat trouve a (%.0f, %.0f) m, %.1f m, %.1f deg"),
				X, Y, Surface, Pente));
			TestTrue(TEXT("et il est bien au-dessus de la mer"), Surface > 2.0f);

			// LA MEME FONCTION QUE LA RECHERCHE EMPLOIE, appelee a part : si
			// elle disait le contraire de ce que la recherche a retenu, l'une
			// des deux mentirait.
			TestTrue(TEXT("le sol retenu est plein sous les pieds"),
				WorldseedPlacement::SolPlein(Emergee.Champ, X, Y, Surface));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
