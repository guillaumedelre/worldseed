// Worldseed - le socle : tout le monde sort de ces fonctions, et une derive
// ici ne casse RIEN -- elle deplace le monde en silence.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedPerlin.h"

#include "Misc/AutomationTest.h"

namespace
{
	/** Un balayage reproductible, volontairement hors des noeuds entiers. */
	template <typename F>
	void Balayer(int32 N, F&& Visiteur)
	{
		for (int32 I = 0; I < N; ++I)
		{
			// Pas irrationnels : on ne retombe jamais sur la grille, et deux
			// echantillons successifs ne sont pas voisins. Un pas entier
			// ferait passer le test A COTE du defaut qu'il cherche, puisque
			// Perlin vaut zero aux noeuds par construction.
			const float X = static_cast<float>(I) * 0.6180339887f * 13.0f;
			const float Y = static_cast<float>(I) * 0.4142135624f * 7.0f + 3.25f;
			const float Z = static_cast<float>(I) * 0.3027756377f * 11.0f - 2.5f;
			Visiteur(X, Y, Z);
		}
	}

	/**
	 * Le meme balayage, mais BORNE -- et ce n'est pas un detail de confort.
	 *
	 * UNE DIFFERENCE FINIE EXIGE QUE LE PAS DEPASSE LA PRECISION DU TYPE. Un
	 * `float` a 24 bits de mantisse, donc son ULP vaut la magnitude fois
	 * 6e-8 : a X ~ 16 400, l'ULP vaut 9,8e-4, c'est-a-dire exactement le pas
	 * de 1e-3 qu'on croit appliquer. `X + E` tombe alors sur X ou sur
	 * X + 2 ULP, le pas REEL est indetermine, et la pente mesuree est
	 * gonflee d'un facteur allant jusqu'a deux.
	 *
	 * MESURE : la premiere version de ce fichier balayait jusqu'a X ~ 16 400
	 * et rendait 1,666 pour la pente de F1 de Worley, qui est 1-lipschitzien
	 * par construction -- un minimum de distances. Le test a donc echoue sur
	 * un defaut qui etait le SIEN. Borne a 256, l'ULP tombe a 1,5e-5, soit
	 * soixante-cinq fois sous le pas, et le bruit de mesure disparait.
	 *
	 * Les tests qui ne font PAS de difference finie -- bornes, graines,
	 * determinisme -- gardent le balayage large a dessein : il couvre bien
	 * plus de cellules de hachage.
	 */
	template <typename F>
	void BalayerProche(int32 N, F&& Visiteur)
	{
		for (int32 I = 0; I < N; ++I)
		{
			const float X = FMath::Fmod(static_cast<float>(I) * 8.0299f, 256.0f);
			const float Y = FMath::Fmod(static_cast<float>(I) * 2.8995f, 256.0f) + 3.25f;
			const float Z = FMath::Fmod(static_cast<float>(I) * 3.3305f, 256.0f) - 2.5f;
			Visiteur(X, Y, Z);
		}
	}

	bool EstFini(float V)
	{
		return !FMath::IsNaN(V) && FMath::IsFinite(V);
	}

	/** Correlation de Pearson entre deux echantillons apparies. */
	double Correlation(const TArray<float>& A, const TArray<float>& B)
	{
		const int32 N = A.Num();
		if (N < 2 || B.Num() != N) { return 1.0; }

		double MA = 0.0, MB = 0.0;
		for (int32 I = 0; I < N; ++I) { MA += A[I]; MB += B[I]; }
		MA /= N; MB /= N;

		double SAB = 0.0, SAA = 0.0, SBB = 0.0;
		for (int32 I = 0; I < N; ++I)
		{
			const double DA = A[I] - MA;
			const double DB = B[I] - MB;
			SAB += DA * DB; SAA += DA * DA; SBB += DB * DB;
		}
		if (SAA <= 0.0 || SBB <= 0.0) { return 1.0; }
		return SAB / FMath::Sqrt(SAA * SBB);
	}
}

/**
 * LE MEME APPEL REND LA MEME VALEUR.
 *
 * C'est la propriete dont TOUT depend dans ce depot, et elle ne se voit pas
 * quand elle casse : le cache du monde est indexe par graine et par empreinte
 * de regles, donc un bruit non deterministe rendrait un monde different du
 * monde mis en cache SANS que rien ne le signale -- et tous les A/B du depot,
 * qui comparent deux lancements, mesureraient du bruit d'execution.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestBruitDeterminisme,
	"Worldseed.Bruit.Determinisme",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestBruitDeterminisme::RunTest(const FString& Parameters)
{
	constexpr int32 Seed = 20260909;

	Balayer(512, [this](float X, float Y, float Z)
	{
		TestEqual(TEXT("Perlin 2D rend deux fois la meme valeur"),
			WorldseedPerlin::Perlin(X, Y, Seed),
			WorldseedPerlin::Perlin(X, Y, Seed));

		TestEqual(TEXT("Perlin 3D rend deux fois la meme valeur"),
			WorldseedPerlin::Perlin3D(X, Y, Z, Seed),
			WorldseedPerlin::Perlin3D(X, Y, Z, Seed));

		TestEqual(TEXT("Fbm3D rend deux fois la meme valeur"),
			WorldseedPerlin::Fbm3D(X, Y, Z, 0.01f, 4, Seed),
			WorldseedPerlin::Fbm3D(X, Y, Z, 0.01f, 4, Seed));
	});

	// ET LE CHEMIN PAR TABLEAU AUSSI, parce que ce n'est pas le meme code :
	// `FBM` remplit une grille entiere et c'est lui que la chaine emploie.
	TArray<float> A, B;
	WorldseedPerlin::FBM(A, 64, 32, 0.05f, 4, Seed);
	WorldseedPerlin::FBM(B, 64, 32, 0.05f, 4, Seed);
	TestEqual(TEXT("FBM rend une grille de meme taille"), A.Num(), B.Num());

	int32 Ecarts = 0;
	for (int32 I = 0; I < A.Num() && I < B.Num(); ++I)
	{
		if (A[I] != B[I]) { ++Ecarts; }
	}
	TestEqual(TEXT("FBM rend deux fois la meme grille, au bit pres"), Ecarts, 0);

	return true;
}

/**
 * PERLIN EST UN BRUIT DE GRADIENT, DONC IL VAUT EXACTEMENT ZERO AUX NOEUDS.
 *
 * C'est la propriete qui le DISTINGUE d'un bruit de valeur, et elle n'est pas
 * decorative : le relief macro est un fBm de Perlin, et un bruit de valeur au
 * meme endroit donnerait des plateaux carres a la maille du bruit au lieu de
 * collines. Un refactor du hachage qui perdrait la structure de gradient
 * passerait tous les autres controles -- bornes, continuite, determinisme --
 * et se verrait seulement A L'IMAGE, des mois plus tard.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestBruitZeroAuxNoeuds,
	"Worldseed.Bruit.ZeroAuxNoeuds",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestBruitZeroAuxNoeuds::RunTest(const FString& Parameters)
{
	constexpr int32 Seed = 1337;

	float PireAuNoeud = 0.0f;
	for (int32 IY = -8; IY <= 8; ++IY)
	{
		for (int32 IX = -8; IX <= 8; ++IX)
		{
			PireAuNoeud = FMath::Max(PireAuNoeud, FMath::Abs(
				WorldseedPerlin::Perlin(static_cast<float>(IX),
					static_cast<float>(IY), Seed)));
		}
	}

	AddInfo(FString::Printf(TEXT("Perlin aux 289 noeuds entiers : pire %.3e"),
		PireAuNoeud));
	TestTrue(TEXT("Perlin s'annule aux noeuds entiers"), PireAuNoeud < 1.0e-5f);

	// LE TEMOIN, SANS QUOI LE TEST NE PROUVE RIEN. Un bruit identiquement nul
	// passerait la ligne ci-dessus. Il faut donc montrer qu'ENTRE les noeuds il
	// se passe quelque chose -- c'est le piege de la fixture muette, paye
	// quatre fois dans ce depot en une seule journee.
	float PlusGrandEntreLesNoeuds = 0.0f;
	Balayer(512, [&PlusGrandEntreLesNoeuds](float X, float Y, float)
	{
		PlusGrandEntreLesNoeuds = FMath::Max(PlusGrandEntreLesNoeuds,
			FMath::Abs(WorldseedPerlin::Perlin(X, Y, Seed)));
	});

	AddInfo(FString::Printf(TEXT("et entre les noeuds : %.3f"),
		PlusGrandEntreLesNoeuds));
	TestTrue(TEXT("mais il ne s'annule pas partout"),
		PlusGrandEntreLesNoeuds > 0.2f);

	return true;
}

/**
 * LES VALEURS RESTENT BORNEES, ET AUCUNE N'EST NaN.
 *
 * Un NaN dans le relief ne plante pas : il se propage, le marching cubes le
 * compare, la comparaison rend faux des deux cotes, et le chunk sort VIDE. Ce
 * depot a deja passe une soiree sur des trous de terrain dont la cause etait
 * ailleurs ; celle-la serait indiscernable a l'oeil.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestBruitBornes,
	"Worldseed.Bruit.Bornes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestBruitBornes::RunTest(const FString& Parameters)
{
	constexpr int32 Seed = 4242;

	float Pire2D = 0.0f, Pire3D = 0.0f, PireFbm = 0.0f;
	int32 NonFinis = 0;

	Balayer(4096, [&](float X, float Y, float Z)
	{
		const float P2 = WorldseedPerlin::Perlin(X, Y, Seed);
		const float P3 = WorldseedPerlin::Perlin3D(X, Y, Z, Seed);
		const float FB = WorldseedPerlin::Fbm3D(X, Y, Z, 0.02f, 5, Seed);

		if (!EstFini(P2) || !EstFini(P3) || !EstFini(FB)) { ++NonFinis; }

		Pire2D = FMath::Max(Pire2D, FMath::Abs(P2));
		Pire3D = FMath::Max(Pire3D, FMath::Abs(P3));
		PireFbm = FMath::Max(PireFbm, FMath::Abs(FB));
	});

	AddInfo(FString::Printf(
		TEXT("amplitudes sur 4096 points -- Perlin2D %.3f, Perlin3D %.3f, fBm %.3f"),
		Pire2D, Pire3D, PireFbm));

	TestEqual(TEXT("aucune valeur NaN ou infinie"), NonFinis, 0);

	// La borne est GENEREUSE A DESSEIN : ce test garde l'ordre de grandeur, pas
	// une normalisation exacte. Un bruit qui sortirait a 10 aurait multiplie
	// tout le relief par dix ; c'est cela qu'on attrape, pas un centieme.
	TestTrue(TEXT("Perlin 2D reste dans [-1,5 .. 1,5]"), Pire2D <= 1.5f);
	TestTrue(TEXT("Perlin 3D reste dans [-1,5 .. 1,5]"), Pire3D <= 1.5f);
	TestTrue(TEXT("le fBm reste borne"), PireFbm <= 2.0f);

	// ET ILS NE SONT PAS PLATS -- meme temoin que ci-dessus.
	TestTrue(TEXT("Perlin 2D varie reellement"), Pire2D > 0.2f);
	TestTrue(TEXT("le fBm varie reellement"), PireFbm > 0.05f);

	return true;
}

/**
 * DEUX GRAINES DONNENT DEUX MONDES, ET C'EST TOUT LE PRODUIT.
 *
 * Le menu laisse choisir une graine ; si elles se rejoignaient, chaque partie
 * rendrait le meme monde. La panne serait TOTALE et parfaitement muette --
 * aucune mesure de ce depot ne compare deux graines.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestBruitGraine,
	"Worldseed.Bruit.LaGraineSepare",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestBruitGraine::RunTest(const FString& Parameters)
{
	TArray<float> A, B, C;
	A.Reserve(4096); B.Reserve(4096); C.Reserve(4096);

	Balayer(4096, [&](float X, float Y, float Z)
	{
		A.Add(WorldseedPerlin::Perlin3D(X, Y, Z, 20260909));
		B.Add(WorldseedPerlin::Perlin3D(X, Y, Z, 20260910));  // graine VOISINE
		C.Add(WorldseedPerlin::Perlin3D(X, Y, Z, 1337));
	});

	const double RVoisine = Correlation(A, B);
	const double RLointaine = Correlation(A, C);

	AddInfo(FString::Printf(
		TEXT("correlation graine voisine %.4f, graine lointaine %.4f"),
		RVoisine, RLointaine));

	// DEUX GRAINES VOISINES SONT LE CAS DUR : un hachage faible les melange,
	// et c'est precisement ce qu'un joueur fait -- il incremente la graine.
	TestTrue(TEXT("deux graines voisines donnent des champs decorreles"),
		FMath::Abs(RVoisine) < 0.15);
	TestTrue(TEXT("deux graines lointaines aussi"),
		FMath::Abs(RLointaine) < 0.15);

	// LE TEMOIN QUI VALIDE LA METRIQUE : la meme graine contre elle-meme doit
	// rendre EXACTEMENT 1. Sans lui, une correlation qui rendrait zero par
	// construction -- un bug de calcul -- ferait passer le test sur un bruit
	// pourtant identique.
	TestTrue(TEXT("temoin : la meme graine se correle a elle-meme"),
		Correlation(A, A) > 0.999);

	return true;
}

/**
 * LE BRUIT EST CONTINU : PAS DE MARCHE ENTRE DEUX POINTS VOISINS.
 *
 * Une discontinuite du champ se lit comme une FALAISE d'un voxel, partout ou
 * elle passe -- et le depot sait depuis le 22 septembre qu'une marche dans le
 * terrain ne se distingue pas a l'oeil d'une marche voulue par la geologie.
 * On mesure la pente maximale observee : une discontinuite la fait exploser,
 * une courbure ordinaire non.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestBruitContinuite,
	"Worldseed.Bruit.Continuite",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestBruitContinuite::RunTest(const FString& Parameters)
{
	constexpr int32 Seed = 7717;
	constexpr float E = 1.0e-3f;

	float PenteMax2D = 0.0f, PenteMax3D = 0.0f;

	BalayerProche(2048, [&](float X, float Y, float Z)
	{
		const float D2 = FMath::Abs(
			WorldseedPerlin::Perlin(X + E, Y, Seed)
			- WorldseedPerlin::Perlin(X, Y, Seed)) / E;
		const float D3 = FMath::Abs(
			WorldseedPerlin::Perlin3D(X, Y + E, Z, Seed)
			- WorldseedPerlin::Perlin3D(X, Y, Z, Seed)) / E;

		PenteMax2D = FMath::Max(PenteMax2D, D2);
		PenteMax3D = FMath::Max(PenteMax3D, D3);
	});

	AddInfo(FString::Printf(TEXT("pente maximale -- 2D %.3f, 3D %.3f"),
		PenteMax2D, PenteMax3D));

	// UN BRUIT DE GRADIENT A SA DERIVEE BORNEE PAR CONSTRUCTION. Une
	// discontinuite rendrait ici des milliers, pas des unites : le seuil n'a
	// pas besoin d'etre fin pour trancher.
	TestTrue(TEXT("Perlin 2D est continu"), PenteMax2D < 20.0f);
	TestTrue(TEXT("Perlin 3D est continu"), PenteMax3D < 20.0f);

	// ET LA PENTE N'EST PAS NULLE : un bruit constant serait continu aussi.
	TestTrue(TEXT("temoin : la pente n'est pas nulle"), PenteMax2D > 0.1f);

	return true;
}

/**
 * WORLEY REND SES DEUX DISTANCES DANS L'ORDRE, ET C'EST CE QUI FAIT LES
 * DIACLASES.
 *
 * Le reseau de fractures est le seuillage de `F2 - F1`, qui s'annule
 * exactement sur la frontiere de Voronoi. Si l'ordre s'inversait, la
 * difference deviendrait negative et le seuil garderait l'INTERIEUR des
 * cellules au lieu de leurs parois -- des bulles au lieu de fentes, sans une
 * erreur.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestBruitWorley,
	"Worldseed.Bruit.Worley",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestBruitWorley::RunTest(const FString& Parameters)
{
	constexpr int32 Seed = 9001;
	constexpr float E = 1.0e-3f;

	int32 Desordre = 0, Negatives = 0, NonFinies = 0;
	float PireEcartF1 = 0.0f;
	float PlusPetitEcart = BIG_NUMBER;
	float PlusGrandEcart = 0.0f;

	BalayerProche(2048, [&](float X, float Y, float Z)
	{
		float F1 = 0.0f, F2 = 0.0f;
		WorldseedPerlin::Worley3D(X, Y, Z, Seed, F1, F2);

		if (!EstFini(F1) || !EstFini(F2)) { ++NonFinies; }
		if (F2 < F1) { ++Desordre; }
		if (F1 < 0.0f) { ++Negatives; }

		PlusPetitEcart = FMath::Min(PlusPetitEcart, F2 - F1);
		PlusGrandEcart = FMath::Max(PlusGrandEcart, F2 - F1);

		// F1 est un MINIMUM de distances, donc 1-lipschitzien : un deplacement
		// de E ne peut pas le changer de plus de E.
		float G1 = 0.0f, G2 = 0.0f;
		WorldseedPerlin::Worley3D(X + E, Y, Z, Seed, G1, G2);
		PireEcartF1 = FMath::Max(PireEcartF1, FMath::Abs(G1 - F1) / E);
	});

	AddInfo(FString::Printf(
		TEXT("F2-F1 de %.4f a %.4f, pente de F1 au pire %.3f"),
		PlusPetitEcart, PlusGrandEcart, PireEcartF1));

	TestEqual(TEXT("aucune distance NaN ou infinie"), NonFinies, 0);
	TestEqual(TEXT("F1 est toujours la plus petite"), Desordre, 0);
	TestEqual(TEXT("aucune distance negative"), Negatives, 0);
	TestTrue(TEXT("F1 est 1-lipschitzien"), PireEcartF1 < 1.5f);

	// LE TEMOIN : la difference doit s'APPROCHER de zero quelque part, sinon
	// aucune diaclase ne peut s'ouvrir -- le seuil ne mordrait jamais.
	TestTrue(TEXT("F2-F1 approche zero sur les frontieres"),
		PlusPetitEcart < 0.1f);
	TestTrue(TEXT("et s'en ecarte ailleurs"), PlusGrandEcart > 0.3f);

	return true;
}

/**
 * AJOUTER DES OCTAVES AFFINE LE BRUIT, ELLES NE L'AMPLIFIENT PAS.
 *
 * Avec un gain de 0,5 la somme des amplitudes converge vers deux fois la
 * premiere. Si une octave cessait d'etre attenuee, tout le relief gagnerait de
 * l'amplitude a chaque octave -- et comme le depot cale ses altitudes par
 * ecretage, l'ecretage mordrait au lieu du relief : un monde de plateaux, sans
 * un message.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestBruitOctaves,
	"Worldseed.Bruit.Octaves",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestBruitOctaves::RunTest(const FString& Parameters)
{
	constexpr int32 Seed = 555;

	float PireParOctaves[7] = {};
	for (int32 O = 1; O <= 6; ++O)
	{
		float Pire = 0.0f;
		Balayer(1024, [&](float X, float Y, float Z)
		{
			Pire = FMath::Max(Pire, FMath::Abs(
				WorldseedPerlin::Fbm3D(X, Y, Z, 0.02f, O, Seed)));
		});
		PireParOctaves[O] = Pire;
		AddInfo(FString::Printf(TEXT("%d octave(s) : amplitude %.4f"), O, Pire));
	}

	// L'amplitude ne doit pas croitre sans fin : entre une octave et six, le
	// rapport d'une somme geometrique de raison 0,5 vaut au plus deux.
	const float Rapport = (PireParOctaves[1] > 0.0f)
		? PireParOctaves[6] / PireParOctaves[1] : 0.0f;

	AddInfo(FString::Printf(TEXT("rapport six octaves / une : %.3f"), Rapport));
	TestTrue(TEXT("six octaves n'amplifient pas plus que deux fois"),
		Rapport < 2.5f);
	TestTrue(TEXT("temoin : une octave produit bien quelque chose"),
		PireParOctaves[1] > 0.05f);

	return true;
}

/**
 * `Smoothstep` EST BORNE, MONOTONE, ET EXACT AUX DEUX BOUTS.
 *
 * Il sert de FONDU partout dans la chaine -- detail pres des cotes, ouverture
 * des diaclases, teinte de roche, rampe de la nappe. Un fondu qui depasserait
 * [0,1] multiplierait un terme par une valeur negative ou superieure a un, ce
 * qui se lit comme une inversion locale et non comme un bug.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestBruitSmoothstep,
	"Worldseed.Bruit.Smoothstep",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestBruitSmoothstep::RunTest(const FString& Parameters)
{
	constexpr float E0 = -20.0f;
	constexpr float E1 = 40.0f;

	TestEqual(TEXT("vaut 0 au bord bas"),
		WorldseedPerlin::Smoothstep(E0, E1, E0), 0.0f);
	TestEqual(TEXT("vaut 1 au bord haut"),
		WorldseedPerlin::Smoothstep(E0, E1, E1), 1.0f);
	TestEqual(TEXT("reste a 0 sous le bord bas"),
		WorldseedPerlin::Smoothstep(E0, E1, E0 - 1000.0f), 0.0f);
	TestEqual(TEXT("reste a 1 au-dessus du bord haut"),
		WorldseedPerlin::Smoothstep(E0, E1, E1 + 1000.0f), 1.0f);

	float Precedent = -1.0f;
	int32 Reculs = 0, HorsBornes = 0;
	for (int32 I = 0; I <= 600; ++I)
	{
		const float X = E0 - 30.0f + static_cast<float>(I) * 0.2f;
		const float S = WorldseedPerlin::Smoothstep(E0, E1, X);

		if (!EstFini(S) || S < 0.0f || S > 1.0f) { ++HorsBornes; }
		if (S < Precedent - 1.0e-6f) { ++Reculs; }
		Precedent = S;
	}

	TestEqual(TEXT("reste dans [0,1]"), HorsBornes, 0);
	TestEqual(TEXT("ne recule jamais"), Reculs, 0);

	// TEMOIN : il doit reellement monter, sinon une constante passerait.
	TestTrue(TEXT("temoin : il monte bien de 0 a 1"), Precedent > 0.999f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
