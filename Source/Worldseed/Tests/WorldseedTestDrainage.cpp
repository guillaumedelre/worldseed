// Worldseed - le drainage : ordre topologique, conservation, enroulement.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedFlow.h"

#include "Misc/AutomationTest.h"

/**
 * CE FICHIER PROTEGE UNE OPTIMISATION QUI NE PEUT PAS ECHOUER BRUYAMMENT.
 *
 * Le 22 septembre, le tri de l'ordre topologique a ete SUPPRIME : Priority-Flood
 * depile deja par altitude non decroissante, donc l'ordre sortait trie et on le
 * retriait pour rien -- mille millisecondes sur deux mille huit cents, et un
 * comparateur qui lisait ailleurs en memoire a chaque appel.
 *
 * OR UN ORDRE TOPOLOGIQUE FAUX NE PLANTE PAS. Il verse l'eau dans le mauvais
 * sens et rend des debits errones -- donc humidite du sol, canyons et lacs faux
 * -- sans un message. Et supprimer un tri produit MECANIQUEMENT un meilleur
 * temps, y compris quand l'ordre obtenu est mauvais : le chronometre ne peut
 * donc rien valider ici.
 *
 * L'equivalence avait ete prouvee par une empreinte journalisee et un A/B sur
 * le meme binaire. Ces tests remplacent l'empreinte -- un nombre d'or qu'il
 * faudrait remettre a jour a chaque reglage -- par les INVARIANTS dont elle
 * n'etait qu'un symptome : l'ordre est valide, le flux se conserve, et le
 * relief comble n'a plus de cuvette. Ils ne dependent ni de la graine, ni des
 * reglages, ni de la taille du monde.
 */
namespace
{
	/** Le relief de la fixture, et des poids tous differents les uns des autres. */
	struct FBassin
	{
		FWorldseedGeometry Geo;
		TArray<float> Relief;
		TArray<float> Poids;
		FWorldseedFlow Flux;

		FBassin()
		{
			Geo = WorldseedTest::Geometrie(16);
			Relief = WorldseedTest::Relief(Geo);

			// DES POIDS NON UNIFORMES, SANS QUOI LA CONSERVATION EST TRIVIALE.
			// A un par cellule, la somme vaut le nombre de cellules et un
			// melange d'indices passerait inapercu.
			Poids = WorldseedTest::Serie(Geo.CellCount(), 0.5f, 0.013f);

			WorldseedFlow::Compute(Relief, Poids, Geo.NX, Geo.NY,
				/*SeaLevelM=*/0.0f, /*EpsilonM=*/0.001f, Flux);
		}
	};
}

/**
 * L'ORDRE EST UN ORDRE TOPOLOGIQUE, ET C'EST CE QUE LE TRI SUPPRIME GARANTISSAIT.
 *
 * L'accumulation parcourt `Order` en avant et verse chaque cellule dans son
 * receveur. Le receveur doit donc arriver APRES : s'il est deja passe, sa
 * contribution est perdue et le debit aval est sous-estime, en silence.
 *
 * Priority-Flood le garantit sans trier parce que la valeur empilee est FIGEE
 * au moment de l'empilement -- la garde interdit de repasser sur une cellule, et
 * toute valeur poussee vaut au moins celle qu'on vient de depiler. Les
 * depilements sortent donc par altitude non decroissante. Ce test constate la
 * propriete au lieu de faire confiance a ce raisonnement.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestDrainageOrdre,
	"Worldseed.Drainage.OrdreTopologique",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestDrainageOrdre::RunTest(const FString& Parameters)
{
	const FBassin B;
	const int32 N = B.Geo.CellCount();

	if (!TestEqual(TEXT("l'ordre porte toutes les cellules"), B.Flux.Order.Num(), N))
	{
		return false;
	}
	TestEqual(TEXT("un receveur par cellule"), B.Flux.Receivers.Num(), N);

	// L'ORDRE EST UNE PERMUTATION, pas seulement un tableau de la bonne taille.
	// Une cellule citee deux fois verserait son apport deux fois.
	TArray<int32> Rang;
	Rang.Init(INDEX_NONE, N);
	int32 Doublons = 0;
	for (int32 I = 0; I < N; ++I)
	{
		const int32 C = B.Flux.Order[I];
		if (!TestTrue(TEXT("l'ordre ne cite que des cellules valides"),
			C >= 0 && C < N))
		{
			return false;
		}
		if (Rang[C] != INDEX_NONE) { ++Doublons; }
		Rang[C] = I;
	}
	TestEqual(TEXT("aucune cellule citee deux fois"), Doublons, 0);

	// LE TEST LUI-MEME : le receveur arrive APRES celui qui verse.
	int32 Inversions = 0;
	int32 Exutoires = 0;
	for (int32 I = 0; I < N; ++I)
	{
		const int32 C = B.Flux.Order[I];
		const int32 Rec = B.Flux.Receivers[C];
		if (Rec == C) { ++Exutoires; continue; }
		if (Rang[Rec] < I) { ++Inversions; }
	}

	AddInfo(FString::Printf(TEXT("%d cellules, %d exutoires, %d inversions"),
		N, Exutoires, Inversions));

	TestTrue(TEXT("il existe au moins un exutoire"), Exutoires > 0);
	TestEqual(TEXT("aucun receveur ne passe avant celui qui verse"), Inversions, 0);

	return true;
}

/**
 * LE FLUX SE CONSERVE : ce qui entre par les poids ressort par les exutoires.
 *
 * C'est une loi EXACTE, pas une tolerance : l'accumulation d'un exutoire vaut
 * le poids total de son bassin, et les bassins partitionnent la grille. Elle
 * attrape d'un coup un ordre invalide, un receveur qui pointe sur lui-meme a
 * tort, et une cellule oubliee -- trois defauts qu'aucun d'eux ne signale.
 *
 * ELLE REMPLACE L'EMPREINTE. Le releve journalise une somme et un pic, ce qui
 * exigeait de comparer a un nombre releve la veille ; celle-ci se verifie
 * contre les ENTREES, donc elle survit a tout changement de relief, de graine
 * ou de resolution.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestDrainageConservation,
	"Worldseed.Drainage.Conservation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestDrainageConservation::RunTest(const FString& Parameters)
{
	const FBassin B;
	const int32 N = B.Geo.CellCount();

	if (!TestEqual(TEXT("une accumulation par cellule"), B.Flux.Accumulation.Num(), N))
	{
		return false;
	}

	double Entre = 0.0;
	double Sorti = 0.0;
	double Pic = 0.0;
	for (int32 C = 0; C < N; ++C)
	{
		Entre += B.Poids[C];
		Pic = FMath::Max(Pic, static_cast<double>(B.Flux.Accumulation[C]));
		if (B.Flux.Receivers[C] == C) { Sorti += B.Flux.Accumulation[C]; }
	}

	AddInfo(FString::Printf(TEXT("entre %.3f, sorti %.3f, pic %.3f"),
		Entre, Sorti, Pic));

	// La tolerance est celle du SIMPLE PRECISION accumule sur des milliers
	// d'additions, pas une marge de confort : les accumulations sont des float.
	TestTrue(TEXT("le flux se conserve"),
		FMath::Abs(Sorti - Entre) < Entre * 1e-4);

	// Chaque cellule porte au moins son propre apport : l'accumulation ne peut
	// pas etre inferieure au poids, les poids etant positifs.
	int32 SousSonPoids = 0;
	for (int32 C = 0; C < N; ++C)
	{
		if (B.Flux.Accumulation[C] < B.Poids[C] * 0.999f) { ++SousSonPoids; }
	}
	TestEqual(TEXT("aucune cellule sous son propre apport"), SousSonPoids, 0);

	return true;
}

/**
 * LE RELIEF COMBLE N'A NI CUVETTE NI PLAT, ET IL N'ABAISSE JAMAIS.
 *
 * C'est la raison d'etre de Priority-Flood : donner a chaque cellule un chemin
 * STRICTEMENT descendant vers un exutoire, pour que l'erosion ait un terrain
 * drainable. Une cuvette residuelle ne se verrait nulle part ailleurs -- elle
 * ferait simplement stagner l'incision a cet endroit.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestDrainageComblement,
	"Worldseed.Drainage.Comblement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestDrainageComblement::RunTest(const FString& Parameters)
{
	// --- UN RELIEF QUI A VRAIMENT UNE CUVETTE --------------------------------
	//
	// LA PREMIERE VERSION DE CE TEST TOURNAIT SUR LE RELIEF DE LA FIXTURE, ET
	// ELLE NE VERIFIAIT RIEN. Celui-ci est une somme d'harmoniques lisses : il
	// n'a aucun bassin ferme, donc Priority-Flood n'y comble RIEN -- « plus
	// forte hauteur comblee 0,00 m ». Les deux assertions passaient alors
	// trivialement, `FilledM` valant `Relief` partout.
	//
	// Une fixture incomplete ne rend pas un test indulgent, elle le rend MUET
	// sur ce qu'il pretend verifier : c'est la meme faute que les biomes vides
	// du test de partage, ou deux `nullptr` se comparaient egaux.
	//
	// D'ou une cuvette FABRIQUEE : un versant qui descend vers la ligne
	// polaire, et dedans un trou que rien ne relie au bas du versant sans
	// franchir une levre. Priority-Flood doit le remplir jusqu'a cette levre.
	const int32 NX = 32;
	const int32 NY = 16;
	const int32 N = NX * NY;
	const int32 CuvetteI = 16;
	const int32 CuvetteJ = 8;
	const float FondM = 50.0f;

	TArray<float> Relief;
	Relief.SetNumUninitialized(N);
	for (int32 J = 0; J < NY; ++J)
	{
		for (int32 I = 0; I < NX; ++I)
		{
			Relief[J * NX + I] = 100.0f - static_cast<float>(J);
		}
	}
	for (int32 DJ = -2; DJ <= 2; ++DJ)
	{
		for (int32 DI = -2; DI <= 2; ++DI)
		{
			if (DI * DI + DJ * DJ > 4) { continue; }
			Relief[(CuvetteJ + DJ) * NX + (CuvetteI + DI)] = FondM;
		}
	}

	const TArray<float> Poids = WorldseedTest::Serie(N, 0.5f, 0.013f);

	FWorldseedFlow Flux;
	WorldseedFlow::Compute(Relief, Poids, NX, NY, 0.0f, 0.001f, Flux);

	struct FVue
	{
		const TArray<float>& Relief;
		const FWorldseedFlow& Flux;
	};
	const FVue B{ Relief, Flux };

	if (!TestEqual(TEXT("un relief comble par cellule"), B.Flux.FilledM.Num(), N))
	{
		return false;
	}

	// LE COMBLEMENT AJOUTE, IL NE RETIRE PAS.
	int32 Abaissees = 0;
	int32 ProfondeurNegative = 0;
	double PlusComblee = 0.0;
	for (int32 C = 0; C < N; ++C)
	{
		if (B.Flux.FilledM[C] < B.Relief[C] - 1e-3f) { ++Abaissees; }
		PlusComblee = FMath::Max(PlusComblee,
			static_cast<double>(B.Flux.FilledM[C] - B.Relief[C]));
		if (B.Flux.LakeDepthM.Num() == N && B.Flux.LakeDepthM[C] < -1e-3f)
		{
			++ProfondeurNegative;
		}
	}
	TestEqual(TEXT("le comblement n'abaisse aucune cellule"), Abaissees, 0);
	TestEqual(TEXT("aucune hauteur comblee negative"), ProfondeurNegative, 0);

	// CHAQUE CELLULE NON EXUTOIRE DESCEND VERS SON RECEVEUR.
	//
	// C'est la propriete que le comblement existe pour produire, et elle se lit
	// sur le RELIEF COMBLE -- le relief brut, lui, a des cuvettes par
	// construction, et les y chercher mesurerait le monde au lieu du code.
	int32 NeDescendPas = 0;
	for (int32 C = 0; C < N; ++C)
	{
		const int32 Rec = B.Flux.Receivers[C];
		if (Rec == C) { continue; }
		if (B.Flux.FilledM[Rec] >= B.Flux.FilledM[C]) { ++NeDescendPas; }
	}

	AddInfo(FString::Printf(
		TEXT("grille %dx%d, cuvette de %.0f m, plus forte hauteur comblee %.2f m, %d qui ne descendent pas"),
		NX, NY, 100.0f - CuvetteJ - FondM, PlusComblee, NeDescendPas));

	TestEqual(TEXT("toute cellule descend vers son receveur"), NeDescendPas, 0);

	// ET LA MESURE MONTRE QU'ELLE A COMBLE. Sans cette ligne, les deux
	// assertions ci-dessus passent sur un relief qui n'a rien a combler, et
	// l'on croit avoir eprouve Priority-Flood alors qu'on l'a contourne.
	//
	// Le fond est a 50 m sous un versant qui vaut 92 m a cet endroit : la
	// cuvette fait donc 42 m, et le comblement doit la remonter jusqu'a sa
	// levre la plus basse. On exige une remontee SUBSTANTIELLE plutot qu'une
	// valeur exacte -- celle-ci depend de l'epsilon de pente, qui est un
	// reglage et non une propriete.
	TestTrue(TEXT("la cuvette a reellement ete comblee"), PlusComblee > 30.0);

	const int32 Fond = CuvetteJ * NX + CuvetteI;
	TestTrue(TEXT("le fond de la cuvette est remonte au-dessus de son relief"),
		B.Flux.FilledM[Fond] > Relief[Fond] + 30.0f);
	TestTrue(TEXT("et il reste sous la levre"),
		B.Flux.FilledM[Fond] < 100.0f - static_cast<float>(CuvetteJ) + 1.0f);

	return true;
}

/**
 * LA LONGITUDE S'ENROULE : LA COLONNE 0 EST VOISINE DE LA DERNIERE.
 *
 * Le monde est une sphere deroulee. Un drainage qui bornerait la colonne au
 * lieu de l'enrouler poserait une ligne de partage des eaux ARTIFICIELLE sur
 * l'antimeridien, et comme elle tomberait exactement au bord du domaine on ne
 * la verrait qu'en traversant ce meridien -- c'est-a-dire nulle part, sur une
 * carte qu'on regarde de face.
 *
 * LE RELIEF EST FABRIQUE POUR CETTE QUESTION, et non repris de la fixture. Il
 * ne depend que de la colonne et descend vers la colonne 0 des DEUX cotes :
 * depuis la derniere colonne, le seul voisin plus bas est donc de l'autre cote
 * de la couture. Sans enroulement, cette cellule n'aurait aucun voisin
 * strictement plus bas et serait comblee -- le test discrimine par
 * construction.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestDrainageEnroulement,
	"Worldseed.Drainage.Enroulement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestDrainageEnroulement::RunTest(const FString& Parameters)
{
	const int32 NX = 32;
	const int32 NY = 16;
	const int32 N = NX * NY;

	TArray<float> Dem;
	Dem.SetNumUninitialized(N);
	for (int32 J = 0; J < NY; ++J)
	{
		for (int32 I = 0; I < NX; ++I)
		{
			// Minimal en colonne 0, croissant vers le milieu des deux cotes.
			Dem[J * NX + I] = 10.0f + static_cast<float>(FMath::Min(I, NX - I));
		}
	}

	FWorldseedFlow Flux;
	// Niveau de la mer SOUS le relief : aucun exutoire marin, donc le routage
	// ne peut pas court-circuiter la question par le bas.
	WorldseedFlow::Compute(Dem, TArray<float>(), NX, NY, 0.0f, 0.001f, Flux);

	if (!TestEqual(TEXT("un receveur par cellule"), Flux.Receivers.Num(), N))
	{
		return false;
	}

	// On interroge une LIGNE INTERIEURE : les lignes polaires sont des
	// exutoires par construction et ne diraient rien de l'enroulement.
	const int32 J = NY / 2;
	const int32 Derniere = J * NX + (NX - 1);
	const int32 Rec = Flux.Receivers[Derniere];

	const int32 ColonneRec = Rec % NX;
	AddInfo(FString::Printf(
		TEXT("colonne %d (altitude %.0f) -> colonne %d (altitude %.0f)"),
		NX - 1, Dem[Derniere], ColonneRec, Dem[Rec]));

	TestNotEqual(TEXT("la derniere colonne n'est pas son propre exutoire"),
		Rec, Derniere);
	TestEqual(TEXT("elle s'ecoule par-dessus la couture, vers la colonne 0"),
		ColonneRec, 0);

	// ET RIEN N'A ETE COMBLE SUR LA COUTURE : si l'enroulement manquait, cette
	// colonne serait une cuvette et Priority-Flood l'aurait remontee.
	TestTrue(TEXT("la derniere colonne n'a pas ete comblee"),
		Flux.FilledM[Derniere] < Dem[Derniere] + 1e-3f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
