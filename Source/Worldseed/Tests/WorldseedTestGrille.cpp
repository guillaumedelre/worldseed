// Worldseed - l'echantillonnage du relief : exact aux noeuds, et LISSE entre eux.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedGrid.h"

#include "Misc/AutomationTest.h"

namespace
{
	double Lire(const TArray<float>& F, int32 NX, int32 NY, double U, double V,
		bool bCubique)
	{
		return bCubique
			? WorldseedGrid::SampleUVCubic(F, NX, NY,
				static_cast<float>(U), static_cast<float>(V))
			: WorldseedGrid::SampleUV(F, NX, NY,
				static_cast<float>(U), static_cast<float>(V));
	}

	/**
	 * Le SAUT de derivee au passage d'un noeud, mesure d'un seul cote a la fois.
	 *
	 * POURQUOI PAS UNE DIFFERENCE CENTREE DE PART ET D'AUTRE. Premiere version :
	 * j'evaluais la derivee a un DEMI-NOEUD de chaque cote. A cette distance,
	 * l'ecart obtenu melange deux choses de natures differentes -- la COURBURE,
	 * qui est legitime et presente dans les deux interpolations, et la
	 * DISCONTINUITE, qui est le defaut et n'est presente que dans la
	 * bilineaire. Resultat : cubique 508,7 contre bilineaire 918,7, un rapport
	 * de 1,8 qui ne prouvait rien.
	 *
	 * Les deux se separent en restant du MEME cote du noeud : la courbure
	 * s'efface en O(epsilon) quand on serre, le saut ne bouge pas. C'est la
	 * meme lecon que le routage des galeries -- quand une correction ne deplace
	 * pas la mesure, la mesure melange deux populations.
	 */
	double SautAuNoeud(const TArray<float>& F, int32 NX, int32 NY, double UNoeud,
		double V, bool bCubique)
	{
		const double E = 0.02 / static_cast<double>(NX);

		const double Gauche =
			(Lire(F, NX, NY, UNoeud - E, V, bCubique)
				- Lire(F, NX, NY, UNoeud - 2.0 * E, V, bCubique)) / E;
		const double Droite =
			(Lire(F, NX, NY, UNoeud + 2.0 * E, V, bCubique)
				- Lire(F, NX, NY, UNoeud + E, V, bCubique)) / E;

		return FMath::Abs(Droite - Gauche);
	}
}

/**
 * L'ECHANTILLONNAGE DOIT PASSER PAR LES POINTS DE LA GRILLE.
 *
 * Catmull-Rom a cette propriete -- elle la distingue d'une B-spline, qui lisse
 * en DEPLACANT les points. Si elle se perdait, tout le relief macro glisserait
 * de quelques metres sans qu'aucune mesure agregee ne le voie : les parts de
 * biomes, l'altitude moyenne et la part emergee resteraient toutes justes.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestGrilleNoeuds,
	"Worldseed.Grille.PasseParLesNoeuds",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestGrilleNoeuds::RunTest(const FString& Parameters)
{
	const FWorldseedGeometry G = WorldseedTest::Geometrie(16);
	const TArray<float> F = WorldseedTest::Relief(G);

	// On balaye des noeuds INTERIEURS et des noeuds de BORD : les bords sont
	// la ou un noyau a quatre points doit serrer ses indices, donc la ou une
	// erreur d'un cran se cache.
	const int32 Colonnes[] = { 0, 1, 5, G.NX / 2, G.NX - 2, G.NX - 1 };
	const int32 Lignes[] = { 0, 1, 4, G.NY / 2, G.NY - 2, G.NY - 1 };

	for (const int32 I : Colonnes)
	{
		for (const int32 J : Lignes)
		{
			const float U = static_cast<float>(I) / static_cast<float>(G.NX);
			const float V = static_cast<float>(J) / static_cast<float>(G.NY - 1);
			const float Attendu = F[J * G.NX + I];
			const float Lu = WorldseedGrid::SampleUVCubic(F, G.NX, G.NY, U, V);

			// La tolerance est RELATIVE a l'amplitude du relief de test
			// (environ mille metres), pas absolue : un ecart d'un millimetre
			// sur un kilometre n'a aucun sens physique.
			if (FMath::Abs(Lu - Attendu) > 0.5f)
			{
				AddError(FString::Printf(
					TEXT("noeud (%d, %d) : attendu %.3f, lu %.3f"),
					I, J, Attendu, Lu));
			}
		}
	}

	return true;
}

/**
 * ET IL DOIT ETRE C1 : SA DERIVEE NE SAUTE PAS AU BORD D'UNE CELLULE.
 *
 * POURQUOI CETTE PROPRIETE VAUT UN TEST. Une interpolation bilineaire est C0 :
 * sa derivee saute a chaque bord de maille, et le marching cubes rend ces sauts
 * comme des ARETES -- a 64 km la maille fait 31 m, donc le monde entier se
 * lisait comme un pavage de grands triangles. Le portage du generateur Python
 * vers le C++ avait perdu la bicubique SANS QUE PERSONNE NE S'EN APERCOIVE,
 * parce qu'aucune mesure chiffree ne voit ce defaut : il a fallu regarder une
 * photo. Un test, lui, le voit.
 *
 * LE TEMOIN EST LA BILINEAIRE, ET C'EST TOUT L'INTERET DU MONTAGE. Une borne
 * absolue sur le saut de derivee serait arbitraire -- combien est « trop » ?
 * Mesuree contre la bilineaire sur le MEME relief et au MEME endroit, la
 * question disparait : la cubique doit sauter BEAUCOUP MOINS. Ce depot a paye
 * d'avoir valide une metrique nulle part (le comptage de l'herbe de Landscape,
 * qui rendait zero sur la demo du pack aussi) ; ici la metrique est validee sur
 * un cas dont on connait la reponse.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestGrilleC1,
	"Worldseed.Grille.DeriveeContinue",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestGrilleC1::RunTest(const FString& Parameters)
{
	const FWorldseedGeometry G = WorldseedTest::Geometrie(16);
	const TArray<float> F = WorldseedTest::Relief(G);

	double SautCubique = 0.0;
	double SautBilineaire = 0.0;
	int32 Bords = 0;

	// V est fixe au milieu d'une ligne : on n'interroge qu'un axe a la fois,
	// sinon un saut en V viendrait polluer la mesure en U.
	const double V = 0.5;
	for (int32 I = 2; I < G.NX - 2; ++I)
	{
		const double UNoeud = static_cast<double>(I) / static_cast<double>(G.NX);
		SautCubique += SautAuNoeud(F, G.NX, G.NY, UNoeud, V, true);
		SautBilineaire += SautAuNoeud(F, G.NX, G.NY, UNoeud, V, false);
		++Bords;
	}

	if (!TestTrue(TEXT("des bords de cellule ont ete examines"), Bords > 0))
	{
		return false;
	}

	SautCubique /= Bords;
	SautBilineaire /= Bords;

	AddInfo(FString::Printf(
		TEXT("saut moyen de derivee AU NOEUD : cubique %.1f, bilineaire %.1f "
			 "(sur %d noeuds)"),
		SautCubique, SautBilineaire, Bords));

	// LE TEMOIN DOIT SAUTER, sans quoi le test ne prouve rien : si la
	// bilineaire elle-meme etait lisse sur ce relief, la comparaison serait
	// vide de sens et l'on validerait n'importe quoi.
	if (!TestTrue(TEXT("le temoin bilineaire saute bien, donc la mesure discrimine"),
		SautBilineaire > 1.0))
	{
		return false;
	}

	// DIX FOIS, ET LE SEUIL N'EST PAS ARBITRAIRE. Mesuree AU NOEUD, la cubique
	// est C1 donc son saut tend vers zero avec le pas : il ne reste que le
	// bruit de troncature des flottants. La bilineaire, elle, garde un saut
	// FINI quel que soit le pas -- c'est la definition d'une derivee
	// discontinue. L'ecart n'est donc pas une question de dosage : il est
	// d'une autre nature, et un facteur dix le constate sans pretendre le
	// quantifier.
	TestTrue(
		*FString::Printf(
			TEXT("la cubique saute au moins dix fois moins que la bilineaire "
				 "(%.1f contre %.1f)"),
			SautCubique, SautBilineaire),
		SautCubique * 10.0 < SautBilineaire);

	return true;
}

/**
 * LA LONGITUDE S'ENROULE, LA LATITUDE SE BORNE.
 *
 * Le monde est une sphere deroulee : ses bords est et ouest sont le MEME
 * meridien. Un echantillonnage qui bornerait U au lieu de l'enrouler poserait
 * une couture verticale sur toute la hauteur de la carte -- et comme elle
 * tomberait exactement au bord du domaine, on ne la verrait qu'en traversant
 * l'antimeridien.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestGrilleEnroulement,
	"Worldseed.Grille.Enroulement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestGrilleEnroulement::RunTest(const FString& Parameters)
{
	const FWorldseedGeometry G = WorldseedTest::Geometrie(16);
	const TArray<float> F = WorldseedTest::Relief(G);

	for (const float U : { 0.0f, 0.13f, 0.5f, 0.87f })
	{
		const float Ici = WorldseedGrid::SampleUVCubic(F, G.NX, G.NY, U, 0.4f);
		const float UnTourPlusLoin =
			WorldseedGrid::SampleUVCubic(F, G.NX, G.NY, U + 1.0f, 0.4f);
		const float UnTourAvant =
			WorldseedGrid::SampleUVCubic(F, G.NX, G.NY, U - 1.0f, 0.4f);

		TestEqual(*FString::Printf(TEXT("U = %.2f : un tour plus loin"), U),
			UnTourPlusLoin, Ici, 0.01f);
		TestEqual(*FString::Printf(TEXT("U = %.2f : un tour avant"), U),
			UnTourAvant, Ici, 0.01f);
	}

	// La latitude, elle, ne s'enroule PAS : passer le pole ne ramene pas de
	// l'autre cote de la carte, cela reste au pole.
	const float AuPole = WorldseedGrid::SampleUVCubic(F, G.NX, G.NY, 0.3f, 1.0f);
	TestEqual(TEXT("au-dela du pole nord, la valeur est bornee"),
		WorldseedGrid::SampleUVCubic(F, G.NX, G.NY, 0.3f, 1.4f), AuPole, 0.01f);

	const float AuPoleSud = WorldseedGrid::SampleUVCubic(F, G.NX, G.NY, 0.3f, 0.0f);
	TestEqual(TEXT("en deca du pole sud, la valeur est bornee"),
		WorldseedGrid::SampleUVCubic(F, G.NX, G.NY, 0.3f, -0.4f), AuPoleSud, 0.01f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
