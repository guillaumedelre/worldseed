// Worldseed - le sol de fond : il passe SOUS le relief partout, et son plafond
// n'eleve jamais un sommet. Deux proprietes dont depend l'ocean.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedNappe.h"
#include "Procedural/WorldseedWorldData.h"

#include "Misc/AutomationTest.h"

namespace
{
	/** Un metre du monde vaut cent unites Unreal. */
	constexpr double CmParMetre = 100.0;

	/** La nappe pleine, batie sur le monde de la fixture. */
	struct FBancNappe
	{
		FWorldseedWorldData Monde;
		FWorldseedNappeRegles R;
		FWorldseedNappeMaillage Pleine;
		FWorldseedNappeReleve Releve;

		explicit FBancNappe(int32 NY = 64, int32 Largeur = 64)
		{
			Monde = WorldseedTest::Monde(NY);

			R.Largeur = Largeur;
			R.RetraitM = 12.0;
			R.ExagerationZ = 1.0f;
			R.SurEnfoncementM = 125.0;
			R.MargeMerM = 5.0;
			R.Pas = 2;

			FWorldseedAppearance Mode;
			Mode.bColourByBiome = true;
			Mode.bMerOpaque = true;

			WorldseedNappe::Batir(Monde, Monde.Geometry, Monde.ElevationM,
				FWorldseedSurfaceRegles(), Mode, R, Pleine, Releve);
		}

		/** L'altitude d'un sommet de la nappe, en metres du monde. */
		double AltitudeM(int32 Index) const
		{
			return Pleine.Positions[Index].Z / (CmParMetre * R.ExagerationZ);
		}
	};
}

/**
 * LA NAPPE PASSE SOUS LE RELIEF, PARTOUT.
 *
 * C'EST SA RAISON D'ETRE, ET LE DEFAUT QU'ELLE EVITE SE VOIT EN JEU : une
 * nappe qui affleurerait entre deux de ses sommets apparaitrait AU MILIEU des
 * chunks detailles, comme un plan gris coupant le terrain. Elle echantillonne
 * bien plus grossierement que la grille -- 64 sommets pour 128 cellules ici --
 * donc elle ne peut pas se contenter de lire le relief a son propre sommet :
 * elle prend le PLANCHER de son voisinage, puis se retire encore.
 *
 * ON COMPARE AU MINIMUM DU VOISINAGE COUVERT, pas au relief du sommet : c'est
 * ce que la passe promet, et la comparer a autre chose reviendrait a tester
 * une propriete qu'elle n'a jamais annoncee.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestNappeSousLeRelief,
	"Worldseed.SolDeFond.PasseSousLeRelief",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestNappeSousLeRelief::RunTest(const FString& Parameters)
{
	const FBancNappe B;
	const FWorldseedGeometry& G = B.Monde.Geometry;

	if (!TestTrue(TEXT("TEMOIN : la nappe a des sommets"), B.Pleine.Sommets() > 100))
	{
		return false;
	}

	// Le pas d'echantillonnage, et donc l'empreinte dont la passe prend le
	// plancher. On le recalcule ici PARCE QU'IL EST TRIVIAL -- deux divisions --
	// et non parce qu'on reimplemente le critere : ce que le test verifie est
	// le MINIMUM sur cette empreinte, ce que la passe ne redit nulle part.
	const float StepX = (B.Pleine.CountX > 1)
		? static_cast<float>(G.NX - 1) / static_cast<float>(B.Pleine.CountX - 1) : 0.0f;
	const float StepY = (B.Pleine.CountY > 1)
		? static_cast<float>(G.NY - 1) / static_cast<float>(B.Pleine.CountY - 1) : 0.0f;
	const int32 FootX = FMath::Max(FMath::Max(FMath::RoundToInt(StepX), 1) / 2, 1);
	const int32 FootY = FMath::Max(FMath::Max(FMath::RoundToInt(StepY), 1) / 2, 1);

	int32 AuDessus = 0;
	double PireEcartM = 0.0;
	double PlusPetitRetraitM = BIG_NUMBER;

	for (int32 Y = 0; Y < B.Pleine.CountY; ++Y)
	{
		const int32 SY = FMath::Min(FMath::RoundToInt(Y * StepY), G.NY - 1);
		for (int32 X = 0; X < B.Pleine.CountX; ++X)
		{
			const int32 SX = FMath::Min(FMath::RoundToInt(X * StepX), G.NX - 1);

			double PlancherM = BIG_NUMBER;
			for (int32 Row = FMath::Max(SY - FootY, 0);
				Row <= FMath::Min(SY + FootY, G.NY - 1); ++Row)
			{
				for (int32 Col = FMath::Max(SX - FootX, 0);
					Col <= FMath::Min(SX + FootX, G.NX - 1); ++Col)
				{
					PlancherM = FMath::Min<double>(PlancherM,
						B.Monde.ElevationM[Row * G.NX + Col]);
				}
			}

			const double NappeM = B.AltitudeM(Y * B.Pleine.CountX + X);
			const double Retrait = PlancherM - NappeM;

			PlusPetitRetraitM = FMath::Min(PlusPetitRetraitM, Retrait);
			if (NappeM > PlancherM + 1.0e-3)
			{
				++AuDessus;
				PireEcartM = FMath::Max(PireEcartM, NappeM - PlancherM);
			}
		}
	}

	AddInfo(FString::Printf(
		TEXT("%dx%d sommets ; retrait le plus faible sous le plancher %.3f m ")
		TEXT("(demande %.1f) ; %d sommets au-dessus, au pire de %.3f m"),
		B.Pleine.CountX, B.Pleine.CountY, PlusPetitRetraitM, B.R.RetraitM,
		AuDessus, PireEcartM));

	TestEqual(TEXT("aucun sommet ne passe au-dessus du plancher de son voisinage"),
		AuDessus, 0);
	TestTrue(TEXT("et le retrait demande est bien applique"),
		PlusPetitRetraitM >= B.R.RetraitM - 1.0e-3);

	return true;
}

/**
 * LE MAILLAGE EST COHERENT, ET LE RELEVE DIT LA VERITE.
 *
 * Le releve n'est pas decoratif : c'est lui que l'acteur journalise, et c'est
 * par lui que le depot a decouvert que l'enfoncement de la vue noyait 16,3 %
 * des terres. Un compte faux ferait prendre une decision de rendu sur un
 * chiffre invente.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestNappeCoherence,
	"Worldseed.SolDeFond.MaillageEtReleveCoherents",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestNappeCoherence::RunTest(const FString& Parameters)
{
	const FBancNappe B;
	const int32 N = B.Pleine.Sommets();

	// --- les tableaux couvrent tous le maillage -----------------------------
	TestEqual(TEXT("une position par sommet"), B.Pleine.Positions.Num(), N);
	TestEqual(TEXT("une normale par sommet"), B.Pleine.Normales.Num(), N);
	TestEqual(TEXT("un UV par sommet"), B.Pleine.UV0.Num(), N);
	TestEqual(TEXT("une couleur par sommet"), B.Pleine.Couleurs.Num(), N);
	TestEqual(TEXT("une tangente par sommet"), B.Pleine.Tangentes.Num(), N);
	TestEqual(TEXT("un drapeau sous-zero par sommet"), B.Pleine.SousZero.Num(), N);

	// --- les triangles sont une grille complete et valide --------------------
	TestEqual(TEXT("deux triangles par maille"),
		B.Pleine.Triangles.Num(),
		(B.Pleine.CountX - 1) * (B.Pleine.CountY - 1) * 6);

	int32 HorsBornes = 0;
	for (const int32 I : B.Pleine.Triangles)
	{
		if (I < 0 || I >= N) { ++HorsBornes; }
	}
	TestEqual(TEXT("aucun indice hors du maillage"), HorsBornes, 0);

	// --- aucune valeur aberrante ---------------------------------------------
	int32 NonFinis = 0;
	for (const FVector& P : B.Pleine.Positions)
	{
		if (P.ContainsNaN()) { ++NonFinis; }
	}
	TestEqual(TEXT("aucune position NaN"), NonFinis, 0);

	// --- le releve compte ce qui existe --------------------------------------
	int32 SousZeroReels = 0;
	for (const uint8 S : B.Pleine.SousZero) { if (S) { ++SousZeroReels; } }

	AddInfo(FString::Printf(
		TEXT("%d sommets : %d sous zero, %d emerges, %d que la vue aplatirait"),
		B.Releve.Sommets, B.Releve.SommetsSousZero, B.Releve.SommetsEmerges,
		B.Releve.SommetsNoyesParLaVue));

	TestEqual(TEXT("le releve compte autant de sommets que le maillage"),
		B.Releve.Sommets, N);
	TestEqual(TEXT("et autant sous zero que le maillage en marque"),
		B.Releve.SommetsSousZero, SousZeroReels);
	TestEqual(TEXT("sous zero et emerges partitionnent les sommets"),
		B.Releve.SommetsSousZero + B.Releve.SommetsEmerges, N);

	// LES DEUX TEMOINS : un monde tout emerge ou tout immerge passerait les
	// trois lignes ci-dessus sans rien prouver de la classification.
	TestTrue(TEXT("TEMOIN : il y a de la mer"), B.Releve.SommetsSousZero > 0);
	TestTrue(TEXT("TEMOIN : et de la terre"), B.Releve.SommetsEmerges > 0);

	// LA COURBE EST CROISSANTE PAR CONSTRUCTION : plus on enfonce, plus on noie.
	int32 Reculs = 0;
	for (int32 P = 1; P < FWorldseedNappeReleve::NbPaliers; ++P)
	{
		if (B.Releve.ParPalier[P] < B.Releve.ParPalier[P - 1]) { ++Reculs; }
	}
	TestEqual(TEXT("la courbe du noyage ne recule jamais"), Reculs, 0);

	return true;
}

/**
 * LA DECIMATION GARDE TOUT LE MONDE, ET LE PLAFOND EPINGLE LE RIVAGE.
 *
 * DEUX SECTIONS, PAS UNE : la mer du decor a son propre materiau, parce qu'au
 * dela de la fenetre du plugin Water ce qu'on voit n'est plus de l'eau mais le
 * sol de fond -- le peindre en plage laisse un trait droit en travers des
 * dunes, constate a l'image. La coupe doit donc etre EXACTE : chaque triangle
 * part d'un cote et d'un seul.
 *
 * ET LE PLAFOND N'ELEVE JAMAIS UN SOMMET. C'est ce qui interdit a un fond
 * marin d'emerger et a une terre basse de disparaitre sous l'ocean -- la part
 * emergee est un invariant du projet, et l'enfoncement de la vue en noyait
 * 16,3 % avant que ce plafond n'existe.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestNappeDecimation,
	"Worldseed.SolDeFond.DecimationEtCoupure",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestNappeDecimation::RunTest(const FString& Parameters)
{
	const FBancNappe B;

	FWorldseedNappeMaillage Vue;
	TArray<FVector2D> PlafondCm;
	TArray<int32> TriTerre;
	TArray<int32> TriMer;
	WorldseedNappe::Decimer(B.Pleine, B.R, Vue, PlafondCm, TriTerre, TriMer);

	// --- la taille suit le pas ----------------------------------------------
	const int32 Pas = FMath::Clamp(B.R.Pas, 1, 8);
	TestEqual(TEXT("la largeur decimee suit le pas"),
		Vue.CountX, (B.Pleine.CountX - 1) / Pas + 1);
	TestEqual(TEXT("la hauteur aussi"),
		Vue.CountY, (B.Pleine.CountY - 1) / Pas + 1);
	TestEqual(TEXT("un plafond par sommet"), PlafondCm.Num(), Vue.Sommets());

	// --- la coupe est une PARTITION des triangles ---------------------------
	TestEqual(TEXT("terre et mer se partagent tous les triangles"),
		TriTerre.Num() + TriMer.Num(), Vue.Triangles.Num());

	int32 MalClasses = 0;
	for (int32 I = 0; I + 2 < TriMer.Num(); I += 3)
	{
		// Un triangle n'est de la MER que si ses TROIS sommets sont sous zero :
		// un seul sommet emerge suffit a en faire une cote, qui doit garder le
		// materiau de terrain.
		if (!(Vue.SousZero[TriMer[I]] && Vue.SousZero[TriMer[I + 1]]
			&& Vue.SousZero[TriMer[I + 2]]))
		{
			++MalClasses;
		}
	}
	TestEqual(TEXT("un triangle de mer a ses trois sommets sous zero"),
		MalClasses, 0);

	AddInfo(FString::Printf(
		TEXT("%dx%d sommets decimes ; %d triangles de terre, %d de mer"),
		Vue.CountX, Vue.CountY, TriTerre.Num() / 3, TriMer.Num() / 3));

	// LES DEUX TEMOINS : une coupe qui mettrait tout d'un cote passerait la
	// partition et la classification sans rien decouper.
	TestTrue(TEXT("TEMOIN : il y a des triangles de terre"), TriTerre.Num() > 0);
	TestTrue(TEXT("TEMOIN : et des triangles de mer"), TriMer.Num() > 0);

	// --- le plafond n'eleve jamais, et epingle le rivage ---------------------
	int32 Negatifs = 0, TropGrands = 0, Epingles = 0, Enfonces = 0;
	for (int32 I = 0; I < PlafondCm.Num(); ++I)
	{
		const double EnfonceM = PlafondCm[I].X / (CmParMetre * B.R.ExagerationZ);

		if (EnfonceM < -1.0e-6) { ++Negatifs; }
		if (EnfonceM > B.R.SurEnfoncementM + 1.0e-6) { ++TropGrands; }
		if (EnfonceM <= 1.0e-6) { ++Epingles; }
		if (EnfonceM > 1.0) { ++Enfonces; }
	}

	AddInfo(FString::Printf(
		TEXT("plafond : %d sommets epingles a zero, %d reellement enfonces"),
		Epingles, Enfonces));

	TestEqual(TEXT("le plafond n'eleve JAMAIS un sommet"), Negatifs, 0);
	TestEqual(TEXT("et ne depasse jamais l'enfoncement demande"), TropGrands, 0);

	// LE TEMOIN QUI DONNE SON SENS AU PLAFOND : s'il epinglait tout, la nappe
	// ne s'enfoncerait plus et boucherait les cavites ; s'il n'epinglait rien,
	// le rivage respirerait quand le joueur marche.
	TestTrue(TEXT("TEMOIN : des sommets sont epingles au rivage"), Epingles > 0);
	TestTrue(TEXT("TEMOIN : et d'autres s'enfoncent vraiment"), Enfonces > 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
