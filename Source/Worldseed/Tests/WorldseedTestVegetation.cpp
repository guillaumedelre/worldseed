// Worldseed - le semis de vegetation : ce qu'il ne doit JAMAIS faire.
//
// POURQUOI CE FICHIER EXISTE. Le semis produit des centaines de milliers
// d'instances ; aucun de ses defauts ne leve d'erreur, et presque aucun ne se
// voit a l'oeil. Une plante posee DEUX fois se confond avec une plante posee
// une fois, de l'herbe sous la mer ne se remarque qu'en nageant, et un semis
// qui cesse d'etre deterministe fait SAUTER la vegetation a chaque remaillage
// -- un scintillement qu'on met sur le compte du streaming.
//
// Les quatre invariants testes ici sont donc ceux dont la violation est
// silencieuse. Chacun a deja ete un defaut reel, ou est garde par une seule
// ligne dont rien ne signalerait la disparition.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedPerlin.h"
#include "Procedural/WorldseedRecettes.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedVegetation.h"
#include "Procedural/WorldseedVoxelChunk.h"

#include "Misc/AutomationTest.h"

namespace
{
	/** Cote d'un chunk de test, en metres. Celui du jeu. */
	constexpr double CoteM = 32.0;
	constexpr double CoteCm = CoteM * 100.0;

	/**
	 * Un chunk PLAT a l'altitude demandee, maille en deux triangles.
	 *
	 * LE SEMIS NE LIT QUE LA GEOMETRIE, et c'est ce qui rend ce test possible
	 * sans monde ni acteur : `FGrilleSol::Batir` rasterise les triangles pour
	 * en tirer une altitude et une normale par case. Deux triangles couvrant
	 * le carre suffisent donc a fournir un sol complet.
	 *
	 * ON DEBORDE D'UN METRE, et ce n'est pas un detail : la grille du semis est
	 * celle du MONDE, pas celle du chunk, donc ses points tombent ou ils
	 * veulent. Un maillage qui s'arreterait pile au bord laisserait les cases
	 * de bordure sans sol, et le releve les compterait « case vide » -- on
	 * mesurerait alors le maillage de test, pas le semis.
	 */
	FWorldseedVoxelMesh ChunkPlat(const FVector& OrigineCm, float ZCm)
	{
		FWorldseedVoxelMesh M;

		const double X0 = OrigineCm.X - 100.0;
		const double Y0 = OrigineCm.Y - 100.0;
		const double X1 = OrigineCm.X + CoteCm + 100.0;
		const double Y1 = OrigineCm.Y + CoteCm + 100.0;

		M.Positions.Add(FVector(X0, Y0, ZCm));
		M.Positions.Add(FVector(X1, Y0, ZCm));
		M.Positions.Add(FVector(X1, Y1, ZCm));
		M.Positions.Add(FVector(X0, Y1, ZCm));

		for (int32 I = 0; I < 4; ++I)
		{
			M.Normals.Add(FVector(0.0, 0.0, 1.0));
		}

		M.Triangles.Append({ 0, 1, 2, 0, 2, 3 });
		return M;
	}

	/** Une recette d'une seule couche, semee au pas donne, sans garde. */
	FWorldseedCoucheRecette Couche(const FString& Nom, float PasCm)
	{
		FWorldseedCoucheRecette C;
		C.Nom = Nom;
		C.PasCm = PasCm;
		C.EchelleMin = 1.0f;
		C.EchelleMax = 1.0f;
		C.PenteMinDeg = 0.0f;
		C.PenteMaxDeg = 90.0f;

		FWorldseedEspece E;
		E.IndexCatalogue = 0;
		E.Poids = 1.0f;
		C.Especes.Add(E);
		C.PoidsTotal = 1.0f;
		return C;
	}

	/**
	 * Des recettes minimales : un biome, une couche, et l'estran a la demande.
	 *
	 * L'ESPECE D'ESTRAN PORTE L'INDEX 1 ET CELLE DU BIOME L'INDEX 0. C'est ce
	 * qui permet de dire, en lisant une plante posee, DE QUELLE recette elle
	 * vient -- sans quoi le test de l'estran ne pourrait rien distinguer.
	 */
	FWorldseedRecettes RecettesDeTest(int32 IdBiome, bool bAvecEstran)
	{
		FWorldseedRecettes R;
		R.Catalogue.Add(TEXT("/Game/Test/Plante.Plante"));
		R.Catalogue.Add(TEXT("/Game/Test/Galet.Galet"));

		FWorldseedBiomeRecette B;
		B.Couches.Add(Couche(TEXT("tapis"), 200.0f));
		R.ParBiome.Add(IdBiome, MoveTemp(B));
		R.NbCouches = 1;

		if (bAvecEstran)
		{
			FWorldseedCoucheRecette G = Couche(TEXT("galets"), 200.0f);
			G.Especes[0].IndexCatalogue = 1;
			R.Estran.Couches.Add(MoveTemp(G));
			++R.NbCouches;
		}
		return R;
	}

	/** Une carte de biomes uniforme, avec la couverture demandee. */
	FWorldseedBiomeMap Carte(const FWorldseedGeometry& G, uint8 IdBiome,
		EWorldseedCover Couverture)
	{
		FWorldseedBiomeMap B;
		B.Index.Init(IdBiome, G.CellCount());
		B.Cover.Init(static_cast<uint8>(Couverture), G.CellCount());
		B.SlopeDeg.Init(0.0f, G.CellCount());
		return B;
	}

	/**
	 * Pas de pan de falaise dans ces fixtures.
	 *
	 * NOMMEE PLUTOT QU'UN `{}` ANONYME : un tableau vide passe en temporaire
	 * ne dit pas s'il est vide PAR CHOIX ou par oubli, et ces six appels
	 * testent autre chose. L'emprise des pans a son propre test.
	 */
	const TArray<FWorldseedEmpriseParoi> SansParoi;

	FWorldseedVegetationRegles Regles()
	{
		FWorldseedVegetationRegles R;
		R.PasMultiplicateur = 1.0f;
		R.Densite = 1.0f;
		R.RayonSemisM = 100000.0f;   // hors de cause : on teste autre chose
		R.AltitudeMinM = 0.0f;
		return R;
	}
}

/**
 * UNE PLANTE N'APPARTIENT QU'A UN CHUNK, ET C'EST L'INVARIANT LE PLUS DUR.
 *
 * La grille de semis est celle du MONDE et non celle du chunk -- c'est ce qui
 * rend le semis stable quand le decoupage change, et c'est deja teste plus bas
 * par le determinisme. Mais elle deborde donc de chaque chunk, et deux voisins
 * voient les memes points. Une seule ligne les departage : le test qui rejette
 * un point tombant hors des bornes du chunk courant.
 *
 * SANS ELLE, CHAQUE PLANTE DE BORDURE EXISTERAIT EN DOUBLE, et rien ne le
 * dirait : deux instances au meme endroit se confondent a l'ecran, le compte
 * total parait simplement plus eleve, et l'on mettrait la difference sur le
 * compte de la densite. On seme donc quatre chunks jointifs et l'on verifie
 * qu'aucune position n'est servie deux fois.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedVegetationDoublonTest,
	"Worldseed.Vegetation.UneMailleUnSeulChunk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedVegetationDoublonTest::RunTest(const FString& Parameters)
{
	const FWorldseedGeometry G = WorldseedTest::Geometrie();
	const FWorldseedRecettes R = RecettesDeTest(3, false);
	const FWorldseedBiomeMap B = Carte(G, 3, EWorldseedCover::None);

	TSet<FString> Vues;
	int32 Doublons = 0;
	int32 Total = 0;

	// QUATRE CHUNKS JOINTIFS : les doublons ne peuvent naitre qu'a leurs
	// frontieres communes, donc il en faut au moins deux par axe.
	for (int32 CX = 0; CX < 2; ++CX)
	{
		for (int32 CY = 0; CY < 2; ++CY)
		{
			const FVector Origine(CX * CoteCm, CY * CoteCm, 0.0);
			const FWorldseedVoxelMesh Mesh = ChunkPlat(Origine, 500.0f);

			TArray<FWorldseedPlante> Plantes;
			FWorldseedVegetationReleve Releve;
			WorldseedVegetation::Semer(Mesh, Origine, CoteCm, R, B, G,
				Regles(), 1234, FVector2D::ZeroVector, SansParoi, Plantes, Releve);

			for (const FWorldseedPlante& P : Plantes)
			{
				++Total;
				// Au millimetre : deux chunks qui servent la meme maille
				// posent la plante EXACTEMENT au meme endroit, le tirage ne
				// dependant que des indices de maille et de la graine.
				const FVector T = P.Transform.GetLocation();
				const FString Cle = FString::Printf(TEXT("%.1f|%.1f"), T.X, T.Y);
				if (Vues.Contains(Cle))
				{
					++Doublons;
				}
				Vues.Add(Cle);
			}
		}
	}

	// LE TEMOIN EST DANS L'ASSERTION : sans plantes, « zero doublon » serait
	// vrai et vide de sens. Quatre chunks de 32 m au pas de 200 cm en portent
	// environ 256 par chunk.
	TestTrue(TEXT("le semis a bien pose des plantes"), Total > 100);
	TestEqual(TEXT("aucune plante n'est posee deux fois"), Doublons, 0);
	return true;
}

/**
 * RIEN NE POUSSE SOUS LA MER, ET LE SEMIS NE LE SAVAIT PAS.
 *
 * Defaut REEL, corrige et documente dans `FWorldseedVegetationRegles` : le
 * semis ne testait que la tranche du chunk et la pente, si bien qu'un fond de
 * baie a moins vingt metres recevait herbe, buissons et arbres -- qu'on voyait
 * ensuite a travers l'eau.
 *
 * LE TEST PORTE SUR LES DEUX SENS, et il le faut : une garde qui rejetterait
 * TOUT passerait un test qui ne verifie que le rejet. On seme donc le meme
 * chunk deux fois, une fois sous le niveau de la mer et une fois au-dessus.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedVegetationMerTest,
	"Worldseed.Vegetation.RienNePousseSousLaMer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedVegetationMerTest::RunTest(const FString& Parameters)
{
	const FWorldseedGeometry G = WorldseedTest::Geometrie();
	const FWorldseedRecettes R = RecettesDeTest(3, false);
	const FWorldseedBiomeMap B = Carte(G, 3, EWorldseedCover::None);

	auto SemerA = [&](float ZCm, FWorldseedVegetationReleve& Releve)
	{
		// LA TRANCHE DU CHUNK DOIT CONTENIR LE SOL, sans quoi le rejet
		// viendrait du test de tranche et non de celui de la mer -- on
		// mesurerait la mauvaise garde.
		const FVector Origine(0.0, 0.0, ZCm - CoteCm * 0.5);
		const FWorldseedVoxelMesh Mesh = ChunkPlat(Origine, ZCm);

		TArray<FWorldseedPlante> Plantes;
		WorldseedVegetation::Semer(Mesh, Origine, CoteCm, R, B, G,
			Regles(), 1234, FVector2D::ZeroVector, SansParoi, Plantes, Releve);
		return Plantes.Num();
	};

	FWorldseedVegetationReleve SousLEau;
	const int32 Noyees = SemerA(-1200.0f, SousLEau);

	FWorldseedVegetationReleve AuSec;
	const int32 Emergees = SemerA(1200.0f, AuSec);

	TestEqual(TEXT("aucune plante sous le niveau de la mer"), Noyees, 0);
	TestTrue(TEXT("la garde a bien vu des points a rejeter"),
		SousLEau.SousLaMer > 0);

	// LE TEMOIN : la meme recette, le meme chunk, au-dessus de l'eau. Sans
	// lui, une garde qui rejetterait tout passerait.
	TestTrue(TEXT("au-dessus de l'eau, le semis pose des plantes"),
		Emergees > 100);
	return true;
}

/**
 * L'ESTRAN PORTE SA RECETTE, ET ELLE REMPLACE CELLE DU BIOME.
 *
 * Livre le 26 septembre 2026 apres un signalement en jeu : une plage recevait
 * le tapis d'herbe de la foret qui la borde, parce que le semis ne lisait que
 * le biome. L'estran est un SUBSTRAT -- « une forme, pas un climat » -- et sa
 * recette se substitue a celle du climat sur ses cellules.
 *
 * LE TEST DISTINGUE PAR L'ESPECE, pas par le compte : les deux recettes ont le
 * meme pas, donc un simple total ne dirait pas laquelle a servi. L'espece 0
 * appartient au biome, l'espece 1 a l'estran.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedVegetationEstranTest,
	"Worldseed.Vegetation.EstranRemplaceLeBiome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedVegetationEstranTest::RunTest(const FString& Parameters)
{
	const FWorldseedGeometry G = WorldseedTest::Geometrie();
	const FWorldseedRecettes R = RecettesDeTest(3, /*bAvecEstran*/ true);
	const FVector Origine(0.0, 0.0, 0.0);
	const FWorldseedVoxelMesh Mesh = ChunkPlat(Origine, 500.0f);

	auto CompterParEspece = [&](EWorldseedCover Couverture, int32& OutBiome,
		int32& OutEstran)
	{
		const FWorldseedBiomeMap B = Carte(G, 3, Couverture);
		TArray<FWorldseedPlante> Plantes;
		FWorldseedVegetationReleve Releve;
		WorldseedVegetation::Semer(Mesh, Origine, CoteCm, R, B, G,
			Regles(), 1234, FVector2D::ZeroVector, SansParoi, Plantes, Releve);

		OutBiome = 0;
		OutEstran = 0;
		for (const FWorldseedPlante& P : Plantes)
		{
			(P.Espece == 0 ? OutBiome : OutEstran)++;
		}
	};

	int32 BiomeHorsEstran = 0, EstranHorsEstran = 0;
	CompterParEspece(EWorldseedCover::None, BiomeHorsEstran, EstranHorsEstran);

	int32 BiomeSurEstran = 0, EstranSurEstran = 0;
	CompterParEspece(EWorldseedCover::Beach, BiomeSurEstran, EstranSurEstran);

	// HORS ESTRAN : la recette du biome seule.
	TestTrue(TEXT("hors estran, le biome seme"), BiomeHorsEstran > 100);
	TestEqual(TEXT("hors estran, aucun galet"), EstranHorsEstran, 0);

	// SUR L'ESTRAN : la recette de l'estran seule. C'est le REMPLACEMENT qui
	// est teste ici, pas l'ajout -- une recette qui s'ajouterait laisserait
	// l'herbe sur le sable, c'est-a-dire exactement le defaut signale.
	TestEqual(TEXT("sur l'estran, le biome ne seme plus"), BiomeSurEstran, 0);
	TestTrue(TEXT("sur l'estran, les galets sont poses"), EstranSurEstran > 100);
	return true;
}

/**
 * LE MEME CHUNK, SEME DEUX FOIS, DONNE LE MEME SEMIS.
 *
 * CE QUI ARRIVE SANS CELA EST UN SCINTILLEMENT. Un chunk est remaille des que
 * son niveau d'anneau change -- donc en marchant -- et un semis qui ne serait
 * pas une fonction pure de (maille, graine) reposerait ses plantes AILLEURS a
 * chaque fois. Le commentaire de la passe le dit : « un semis cale sur les
 * triangles ferait SAUTER la vegetation a chaque remaillage ».
 *
 * LE TEST COMPARE LES TRANSFORMATIONS ENTIERES, pas seulement le compte :
 * l'echelle et le lacet sont tires du meme hachage, et un compte identique ne
 * dirait rien de leur stabilite.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedVegetationDeterminismeTest,
	"Worldseed.Vegetation.LeSemisEstDeterministe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedVegetationDeterminismeTest::RunTest(const FString& Parameters)
{
	const FWorldseedGeometry G = WorldseedTest::Geometrie();
	const FWorldseedRecettes R = RecettesDeTest(3, false);
	const FWorldseedBiomeMap B = Carte(G, 3, EWorldseedCover::None);
	const FVector Origine(3 * CoteCm, -2 * CoteCm, 0.0);

	auto Semer = [&](int32 Graine)
	{
		// UN MAILLAGE DIFFERENT A CHAQUE APPEL, ET C'EST LE POINT : on change
		// l'altitude du sol entre les deux passes pour que les triangles ne
		// soient pas les memes. Si le semis se calait sur eux, les positions
		// XY bougeraient.
		const float ZCm = (Graine == 1234) ? 500.0f : 520.0f;
		const FWorldseedVoxelMesh Mesh = ChunkPlat(Origine, ZCm);
		TArray<FWorldseedPlante> Plantes;
		FWorldseedVegetationReleve Releve;
		WorldseedVegetation::Semer(Mesh, Origine, CoteCm, R, B, G,
			Regles(), Graine, FVector2D::ZeroVector, SansParoi, Plantes, Releve);
		return Plantes;
	};

	const TArray<FWorldseedPlante> A = Semer(1234);
	const TArray<FWorldseedPlante> Bis = Semer(1234);

	TestTrue(TEXT("le semis a produit des plantes"), A.Num() > 100);
	if (!TestEqual(TEXT("meme nombre de plantes"), Bis.Num(), A.Num()))
	{
		return false;
	}

	int32 Ecarts = 0;
	for (int32 I = 0; I < A.Num(); ++I)
	{
		const FVector PA = A[I].Transform.GetLocation();
		const FVector PB = Bis[I].Transform.GetLocation();
		if (!PA.Equals(PB, 0.01)
			|| A[I].Espece != Bis[I].Espece
			|| !A[I].Transform.GetScale3D().Equals(Bis[I].Transform.GetScale3D(), 0.001))
		{
			++Ecarts;
		}
	}
	TestEqual(TEXT("chaque plante est identique d'une passe a l'autre"), Ecarts, 0);

	// LE TEMOIN : une AUTRE graine doit donner un AUTRE semis. Sans lui, un
	// semis qui poserait tout a la meme place -- ou rien du tout -- passerait
	// le test d'egalite sans rien prouver.
	const TArray<FWorldseedPlante> Autre = Semer(999);
	int32 Differences = 0;
	const int32 N = FMath::Min(A.Num(), Autre.Num());
	for (int32 I = 0; I < N; ++I)
	{
		if (!A[I].Transform.GetLocation().Equals(
			Autre[I].Transform.GetLocation(), 0.01))
		{
			++Differences;
		}
	}
	TestTrue(TEXT("une autre graine donne un autre semis"), Differences > N / 4);
	return true;
}

/**
 * RIEN NE POUSSE DANS UN ROCHER, ET C'EST UN DEFAUT SIGNALE EN JEU.
 *
 * Une roche est de la MATIERE, pas une image posee sur le sol : de l'herbe qui
 * la traverse se voit immediatement, et c'est ce qui a ouvert ce chantier.
 *
 * DEUX INVARIANTS, ET ILS NE SE REMPLACENT PAS. Aucune plante dans l'emprise
 * d'une roche -- ce qu'on est venu corriger -- et aucune roche dans celle
 * d'une autre : deux blocs qui se traversent se lisent aussi mal.
 *
 * LE TEMOIN EST L'ABSENCE DE GABARITS. Sans `RayonEspeceCm`, l'emprise ne peut
 * pas etre calculee et le semis retombe sur son comportement d'avant. C'est le
 * seul moyen de montrer que ce test DISCRIMINE : sans lui, « zero plante dans
 * la roche » serait aussi vrai d'un semis qui n'en pose aucune.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedVegetationRocheTest,
	"Worldseed.Vegetation.RienNePousseDansLaRoche",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedVegetationRocheTest::RunTest(const FString& Parameters)
{
	const FWorldseedGeometry G = WorldseedTest::Geometrie();
	const FWorldseedBiomeMap B = Carte(G, 3, EWorldseedCover::None);
	const FVector Origine(0.0, 0.0, 0.0);
	const FWorldseedVoxelMesh Mesh = ChunkPlat(Origine, 500.0f);

	// L'ESPECE 1 EST LA ROCHE, L'ESPECE 0 L'HERBE. Le rocher est seme au pas
	// de 800 cm pour un rayon de 250 : il occupe donc une part large mais non
	// totale du chunk, ce qui laisse de l'herbe a poser -- sans quoi le test
	// ne distinguerait pas « ecarte » de « rien seme ».
	auto Recettes = [&](bool bGabarits) -> FWorldseedRecettes
	{
		FWorldseedRecettes R;
		R.Catalogue.Add(TEXT("/Game/Test/Herbe.Herbe"));
		R.Catalogue.Add(TEXT("/Game/Test/Rocher.Rocher"));
		R.EspeceObstacle.Add(false);
		R.EspeceObstacle.Add(true);

		FWorldseedBiomeRecette Biome;

		// L'HERBE EST DECLAREE EN PREMIER A DESSEIN : le semeur doit passer
		// les couches obstacle AVANT, quel que soit leur rang dans la recette.
		// Declarer la roche en tete masquerait exactement le defaut teste.
		Biome.Couches.Add(Couche(TEXT("tapis"), 100.0f));

		FWorldseedCoucheRecette Roche = Couche(TEXT("rochers"), 800.0f);
		Roche.Especes[0].IndexCatalogue = 1;
		Roche.bObstacle = true;
		Biome.Couches.Add(MoveTemp(Roche));

		R.ParBiome.Add(3, MoveTemp(Biome));
		R.NbCouches = 2;

		if (bGabarits)
		{
			R.RayonEspeceCm.Add(0.0f);     // l'herbe n'occupe rien
			R.RayonEspeceCm.Add(250.0f);   // le rocher occupe 2,5 m de rayon
		}
		return R;
	};

	auto Compter = [&](const FWorldseedRecettes& R, int32& OutHerbe,
		int32& OutRoches, int32& OutDansLaRoche, int32& OutRochesQuiSeTouchent)
	{
		TArray<FWorldseedPlante> Plantes;
		FWorldseedVegetationReleve Releve;
		WorldseedVegetation::Semer(Mesh, Origine, CoteCm, R, B, G,
			Regles(), 4242, FVector2D::ZeroVector, SansParoi, Plantes, Releve);

		TArray<FVector2D> Roches;
		for (const FWorldseedPlante& P : Plantes)
		{
			if (P.Espece == 1)
			{
				const FVector T = P.Transform.GetLocation();
				Roches.Add(FVector2D(T.X, T.Y));
			}
		}

		OutHerbe = 0;
		OutRoches = Roches.Num();
		OutDansLaRoche = 0;
		OutRochesQuiSeTouchent = 0;

		// LE RAYON DU CONTROLE EST CELUI DU GABARIT, PAS CELUI DU SEMEUR : on
		// verifie une propriete geometrique du resultat, pas la ligne de code
		// qui l'a produite. Un test qui relirait le rayon depuis le semeur
		// passerait meme si le semeur le lisait de travers.
		constexpr double RayonCm = 250.0;

		for (const FWorldseedPlante& P : Plantes)
		{
			const FVector T = P.Transform.GetLocation();
			const FVector2D Pt(T.X, T.Y);

			if (P.Espece == 0)
			{
				++OutHerbe;
				for (const FVector2D& Roc : Roches)
				{
					if (FVector2D::Distance(Pt, Roc) < RayonCm)
					{
						++OutDansLaRoche;
						break;
					}
				}
			}
			else
			{
				for (const FVector2D& Roc : Roches)
				{
					if (!Roc.Equals(Pt, 0.01)
						&& FVector2D::Distance(Pt, Roc) < RayonCm)
					{
						++OutRochesQuiSeTouchent;
						break;
					}
				}
			}
		}
	};

	int32 Herbe = 0, Roches = 0, DansLaRoche = 0, QuiSeTouchent = 0;
	Compter(Recettes(true), Herbe, Roches, DansLaRoche, QuiSeTouchent);

	AddInfo(FString::Printf(
		TEXT("avec gabarits : %d rochers, %d herbes, %d dans la roche, %d rochers qui se touchent"),
		Roches, Herbe, DansLaRoche, QuiSeTouchent));

	// SANS MATIERE, LE TEST EST MUET. Ce depot a paye quatre fixtures muettes
	// en une journee ; on exige donc que les deux populations existent.
	TestTrue(TEXT("des rochers ont ete poses"), Roches >= 4);
	TestTrue(TEXT("de l'herbe a ete posee"), Herbe > 100);

	TestEqual(TEXT("aucune plante dans l'emprise d'un rocher"), DansLaRoche, 0);
	TestEqual(TEXT("aucun rocher dans l'emprise d'un autre"), QuiSeTouchent, 0);

	// --- LE TEMOIN : sans gabarits, l'emprise ne joue pas -----------------
	int32 HerbeT = 0, RochesT = 0, DansLaRocheT = 0, QuiSeTouchentT = 0;
	Compter(Recettes(false), HerbeT, RochesT, DansLaRocheT, QuiSeTouchentT);

	AddInfo(FString::Printf(
		TEXT("temoin sans gabarits : %d rochers, %d herbes, %d dans la roche"),
		RochesT, HerbeT, DansLaRocheT));

	TestTrue(TEXT("SANS gabarits, de l'herbe pousse bien dans la roche"),
		DansLaRocheT > 0);
	return true;
}

/**
 * LES TACHES : CE QU'ELLES RETIENNENT VRAIMENT, ET CE QUE CHAQUE REGLAGE FAIT.
 *
 * UN SEUIL N'EST PAS UNE PART, et ce depot l'a paye trois fois -- le plus cher
 * etant `diaclaseZoneSeuil`, ou un seuil cense garder seize pour cent n'en
 * gardait que 1,59 parce qu'un Perlin ne se repartit pas uniformement. Monter
 * les octaves deplace la meme chose : une somme fractale se masse plus pres de
 * zero, donc le meme seuil retient MOINS.
 *
 * Ce test ne juge donc pas une valeur : il IMPRIME LA TABLE, pour qu'un
 * reglage se lise au lieu de se deviner. Les assertions, elles, gardent les
 * trois proprietes que le code promet.
 *
 * ET IL MESURE PAR LE SEMEUR LUI-MEME, jamais par une copie de sa decision :
 * une regle recopiee dans un test valide la copie. On compare donc un semis
 * AVEC taches a un semis SANS, sur la meme graine et la meme etendue.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedVegetationTachesTest,
	"Worldseed.Vegetation.LesTachesRetiennent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedVegetationTachesTest::RunTest(const FString& Parameters)
{
	const FWorldseedGeometry G = WorldseedTest::Geometrie();
	const FWorldseedBiomeMap B = Carte(G, 3, EWorldseedCover::None);

	// DOUZE CHUNKS DE COTE, SOIT 384 m. Une tache fait 26 m de diametre : sur
	// un seul chunk de 32 m on mesurerait UNE tache, donc le hasard de sa
	// position, et non une couverture. Il en faut une dizaine par axe.
	constexpr int32 Cotes = 12;

	auto Semis = [&](int32 Octaves, float Seuil, float Douceur) -> int32
	{
		FWorldseedRecettes R;
		R.Catalogue.Add(TEXT("/Game/Test/Herbe.Herbe"));
		R.EspeceObstacle.Add(false);

		FWorldseedCoucheRecette C = Couche(TEXT("tapis"), 200.0f);
		if (Seuil > 0.0f)
		{
			C.TacheTailleCm = 2600.0f;
			C.TacheSeuil = Seuil;
			C.TacheOctaves = Octaves;
			C.TacheDouceur = Douceur;
		}

		FWorldseedBiomeRecette Biome;
		Biome.Couches.Add(MoveTemp(C));
		R.ParBiome.Add(3, MoveTemp(Biome));
		R.NbCouches = 1;

		int32 Total = 0;
		for (int32 CX = 0; CX < Cotes; ++CX)
		{
			for (int32 CY = 0; CY < Cotes; ++CY)
			{
				const FVector Origine(CX * CoteCm, CY * CoteCm, 0.0);
				const FWorldseedVoxelMesh Mesh = ChunkPlat(Origine, 500.0f);

				TArray<FWorldseedPlante> Plantes;
				FWorldseedVegetationReleve Releve;
				WorldseedVegetation::Semer(Mesh, Origine, CoteCm, R, B, G,
					Regles(), 7, FVector2D::ZeroVector, SansParoi, Plantes, Releve);
				Total += Plantes.Num();
			}
		}
		return Total;
	};

	// LA REFERENCE EST LE SEMIS SANS TACHES : c'est lui qui donne le cent pour
	// cent. Rapporter a une valeur theorique ferait mesurer les autres gardes
	// -- pente, bords de chunk -- en meme temps que les taches.
	const int32 Plein = Semis(1, 0.0f, 0.0f);
	TestTrue(TEXT("le semis de reference pose des plantes"), Plein > 5000);

	AddInfo(FString::Printf(TEXT("reference sans taches : %d plantes sur %d x %d chunks"),
		Plein, Cotes, Cotes));
	AddInfo(TEXT("  seuil  | 1 octave | 3 octaves | 1 oct. douceur 0,30"));

	const float Seuils[] = { 0.35f, 0.45f, 0.55f, 0.65f };
	float Cov1[4] = { 0.0f };
	float Cov3[4] = { 0.0f };
	float CovD[4] = { 0.0f };

	for (int32 I = 0; I < 4; ++I)
	{
		Cov1[I] = 100.0f * Semis(1, Seuils[I], 0.0f) / Plein;
		Cov3[I] = 100.0f * Semis(3, Seuils[I], 0.0f) / Plein;
		CovD[I] = 100.0f * Semis(1, Seuils[I], 0.30f) / Plein;

		AddInfo(FString::Printf(TEXT("   %.2f  |  %5.1f %% |   %5.1f %% |   %5.1f %%"),
			Seuils[I], Cov1[I], Cov3[I], CovD[I]));
	}

	// --- LA TABLE D'EQUIVALENCE, POUR MONTER LES OCTAVES SANS DEGARNIR ----
	//
	// Passer une couche a trois octaves a seuil CONSTANT change la quantite
	// autant que la forme -- deux choses a la fois, donc un reglage qu'on ne
	// sait plus juger. Ce balayage donne, pour trois octaves, le seuil qui
	// rend la couverture voulue : on change alors la FORME seule.
	AddInfo(TEXT("equivalence a 3 octaves -- seuil -> couverture"));
	for (int32 S = 30; S <= 70; S += 5)
	{
		const float Seuil = S / 100.0f;
		AddInfo(FString::Printf(TEXT("   3 oct. seuil %.2f -> %5.1f %%"),
			Seuil, 100.0f * Semis(3, Seuil, 0.0f) / Plein));
	}

	// --- TROIS PROPRIETES, ET CHACUNE A SON TEMOIN ------------------------

	// 1. LE SEUIL MORD DANS LE BON SENS. Sans cela, un bruit constant -- donc
	//    casse -- rendrait la meme couverture partout et passerait tout le
	//    reste du test.
	TestTrue(TEXT("un seuil plus haut retient moins"), Cov1[3] < Cov1[0] - 10.0f);
	TestTrue(TEXT("le seuil le plus bas retient une vraie part"), Cov1[0] > 20.0f);

	// 2. LES OCTAVES CHANGENT LA COUVERTURE, et c'est precisement l'avertissement
	//    porte par `TacheOctaves` : a seuil egal, trois octaves retiennent
	//    moins. Si les deux colonnes etaient identiques, le parametre ne serait
	//    pas lu -- le defaut le plus probable, et le plus silencieux.
	TestTrue(TEXT("trois octaves ne donnent pas la meme couverture qu'une"),
		FMath::Abs(Cov3[1] - Cov1[1]) > 2.0f);

	// 3. LA DOUCEUR CHANGE LE CONTRASTE, PAS LA QUANTITE. La transition est
	//    centree sur le seuil : ce qu'elle retire d'un cote, elle le rend de
	//    l'autre. C'est la promesse ecrite dans `TacheDouceur`, et elle se
	//    verifie -- sinon le proprietaire reglerait deux choses en croyant
	//    n'en regler qu'une.
	for (int32 I = 0; I < 4; ++I)
	{
		TestTrue(*FString::Printf(
			TEXT("la douceur ne deplace pas la couverture au seuil %.2f "
				 "(%.1f %% contre %.1f %%)"), Seuils[I], CovD[I], Cov1[I]),
			FMath::Abs(CovD[I] - Cov1[I]) < 8.0f);
	}

	return true;
}

/**
 * UNE OCTAVE DOIT VALOIR LE PERLIN D'AVANT, ET C'EST TOUTE LA COMPATIBILITE.
 *
 * Les cinquante-trois couches qui ne demandent pas d'octaves ne doivent pas
 * voir leur monde bouger : `FbmPoint` a une octave doit rendre exactement ce
 * que `Perlin` rendait. Une normalisation qui s'appliquerait des la premiere
 * octave -- l'erreur naturelle -- redistribuerait tout le decor du monde sans
 * qu'aucun test ne le dise.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedFbmPointTest,
	"Worldseed.Bruit.UneOctaveVautLePerlin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedFbmPointTest::RunTest(const FString& Parameters)
{
	constexpr float F = 1.0f / 2600.0f;
	double PireEcart = 0.0;
	double Amplitude = 0.0;

	for (int32 I = 0; I < 2000; ++I)
	{
		const float X = static_cast<float>(I) * 137.0f;
		const float Y = static_cast<float>(I) * 91.0f;

		const float A = WorldseedPerlin::Perlin(X * F, Y * F, 7);
		const float Un = WorldseedPerlin::FbmPoint(X, Y, F, 1, 7);
		PireEcart = FMath::Max(PireEcart, FMath::Abs<double>(A - Un));
		Amplitude = FMath::Max(Amplitude, FMath::Abs<double>(A));
	}

	AddInfo(FString::Printf(
		TEXT("une octave : pire ecart au Perlin %.9f, amplitude du signal %.3f"),
		PireEcart, Amplitude));

	// LE TEMOIN EST L'AMPLITUDE : sans lui, un bruit identiquement nul aurait
	// un ecart nul et passerait ce test sans rien prouver.
	TestTrue(TEXT("le bruit de reference varie"), Amplitude > 0.1);
	TestTrue(TEXT("une octave vaut exactement le Perlin"), PireEcart < 1e-6);

	// ET TROIS OCTAVES DOIVENT DIFFERER, sans quoi le parametre serait inerte.
	double PireEcart3 = 0.0;
	for (int32 I = 0; I < 2000; ++I)
	{
		const float X = static_cast<float>(I) * 137.0f;
		const float Y = static_cast<float>(I) * 91.0f;
		PireEcart3 = FMath::Max(PireEcart3, FMath::Abs<double>(
			WorldseedPerlin::Perlin(X * F, Y * F, 7)
			- WorldseedPerlin::FbmPoint(X, Y, F, 3, 7)));
	}
	TestTrue(TEXT("trois octaves donnent un autre bruit"), PireEcart3 > 0.05);
	return true;
}

/**
 * COMBIEN CHAQUE BIOME PORTE, A L'HECTARE. Une table, pas un verdict.
 *
 * SIGNALE EN JEU : « dans le desert la densite du foliage n'est pas realiste
 * (trop de vegetation, trop de roche partout) ». Regler un pas de grille a
 * l'intuition, c'est exactement ce que ce depot a paye trois fois -- un
 * plafond de pente a 25 degres qui ne gardait que 36 % des terres, un seuil de
 * diaclase cense garder 16 % qui en gardait 1,59, une part littorale a 0,45
 * qui n'a JAMAIS morde. On mesure donc d'abord.
 *
 * ELLE PASSE PAR LE SEMEUR REEL, sur les recettes REELLES lues du disque :
 * recalculer la densite depuis les pas de grille validerait une copie de la
 * regle, et manquerait tout ce que les gardes retirent -- les taches surtout,
 * qui coupent de moitie certaines couches.
 *
 * ⚠ C'EST UN TEST DE CALAGE, comme le bulletin terrestre : il depend du
 * fichier de recettes et DOIT rouvrir la question quand on le retouche.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedVegetationDensiteTest,
	"Worldseed.Vegetation.DensiteParBiome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedVegetationDensiteTest::RunTest(const FString& Parameters)
{
	FWorldseedRecettes R;
	FString Info;
	if (!R.Charger(Info))
	{
		// PAS UN ECHEC : `Content/` est exclu du depot, un clone frais n'a pas
		// le fichier. On le DIT plutot que de faire tomber la suite.
		AddInfo(FString::Printf(TEXT("recettes non lues, table impossible -- %s"), *Info));
		return true;
	}

	const FWorldseedGeometry G = WorldseedTest::Geometrie();

	// QUATRE CHUNKS PAR BIOME, SOIT 64 x 64 m. Assez pour que les taches --
	// 9 a 40 m de diametre -- soient representees plusieurs fois.
	constexpr int32 Cotes = 2;
	const double AireHa = FMath::Square(Cotes * CoteM) / 10000.0;

	AddInfo(FString::Printf(
		TEXT("densite mesuree sur %.2f ha de sol PLAT par biome (%s)"),
		AireHa, *FPaths::GetCleanFilename(Info)));
	AddInfo(TEXT("  biome | plantes/ha | dont roche/ha | couches"));

	TArray<int32> Ids;
	R.ParBiome.GetKeys(Ids);
	Ids.Sort();

	int32 PlusDense = 0;
	int32 PlusDenseId = INDEX_NONE;

	for (const int32 Id : Ids)
	{
		const FWorldseedBiomeRecette& Recette = R.ParBiome[Id];
		const FWorldseedBiomeMap B = Carte(G, static_cast<uint8>(Id),
			EWorldseedCover::None);

		int32 Total = 0;
		int32 Roches = 0;
		for (int32 CX = 0; CX < Cotes; ++CX)
		{
			for (int32 CY = 0; CY < Cotes; ++CY)
			{
				const FVector Origine(CX * CoteCm, CY * CoteCm, 0.0);
				const FWorldseedVoxelMesh Mesh = ChunkPlat(Origine, 500.0f);

				TArray<FWorldseedPlante> Plantes;
				FWorldseedVegetationReleve Releve;
				WorldseedVegetation::Semer(Mesh, Origine, CoteCm, R, B, G,
					Regles(), 1337, FVector2D::ZeroVector, SansParoi,
					Plantes, Releve);

				Total += Plantes.Num();
				for (const FWorldseedPlante& P : Plantes)
				{
					if (R.EspeceObstacle.IsValidIndex(P.Espece)
						&& R.EspeceObstacle[P.Espece])
					{
						++Roches;
					}
				}
			}
		}

		const int32 ParHa = FMath::RoundToInt(Total / AireHa);
		const int32 RocheParHa = FMath::RoundToInt(Roches / AireHa);
		AddInfo(FString::Printf(TEXT("   %-26s | %6d | %6d | %d"),
			WorldseedBiomes::Name(static_cast<EWorldseedBiome>(Id)),
			ParHa, RocheParHa, Recette.Couches.Num()));

		if (ParHa > PlusDense) { PlusDense = ParHa; PlusDenseId = Id; }
	}

	AddInfo(FString::Printf(TEXT("le plus dense : %s a %d plantes/ha"),
		PlusDenseId != INDEX_NONE
			? WorldseedBiomes::Name(static_cast<EWorldseedBiome>(PlusDenseId))
			: TEXT("--"),
		PlusDense));

	// LE TEMOIN, ET IL EST DANS L'ASSERTION : une table de zeros passerait
	// n'importe quelle lecture. On exige que le semis produise quelque chose.
	TestTrue(TEXT("au moins un biome porte de la vegetation"), PlusDense > 100);
	return true;
}

/**
 * RIEN NE POUSSE SOUS UN PAN DE FALAISE.
 *
 * SIGNALE EN JEU, EN MEME TEMPS QUE LA COLLISION : « la vegetation est posee
 * au meme endroit que le rocher ». Les pans viennent d'une AUTRE passe que le
 * semis ; celui-ci ne les connaitrait jamais si le terrain ne lui donnait pas
 * leur emprise, et c'est exactement ce qui mettait de l'herbe au travers.
 *
 * UNE BOITE ORIENTEE, ET LE TEST LE VERIFIE PAR OU CA COMPTE. Un pan fait
 * 77 x 55 m : le disque qui le contiendrait degarnirait tout autour, celui
 * qui tiendrait dedans laisserait de l'herbe a ses deux bouts. On pose donc le
 * pan EN BIAIS -- quarante-cinq degres -- pour qu'un test de boite alignee ne
 * puisse pas passer par hasard.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedVegetationPanTest,
	"Worldseed.Vegetation.RienNePousseSousUnPan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedVegetationPanTest::RunTest(const FString& Parameters)
{
	const FWorldseedGeometry G = WorldseedTest::Geometrie();
	const FWorldseedRecettes R = RecettesDeTest(3, false);
	const FWorldseedBiomeMap B = Carte(G, 3, EWorldseedCover::None);
	const FVector Origine(0.0, 0.0, 0.0);
	const FWorldseedVoxelMesh Mesh = ChunkPlat(Origine, 500.0f);

	// UN PAN EN BIAIS AU MILIEU DU CHUNK : 16 m par 6, tourne de 45 degres.
	const double Milieu = CoteCm * 0.5;
	FWorldseedEmpriseParoi Pan;
	Pan.CentreCm = FVector2D(Milieu, Milieu);
	Pan.DemiCm = FVector2D(800.0, 300.0);
	Pan.CosLacet = FMath::Cos(PI / 4.0);
	Pan.SinLacet = FMath::Sin(PI / 4.0);

	TArray<FWorldseedEmpriseParoi> Pans;
	Pans.Add(Pan);

	auto Semis = [&](const TArray<FWorldseedEmpriseParoi>& P) -> TArray<FWorldseedPlante>
	{
		TArray<FWorldseedPlante> Plantes;
		FWorldseedVegetationReleve Releve;
		WorldseedVegetation::Semer(Mesh, Origine, CoteCm, R, B, G,
			Regles(), 909, FVector2D::ZeroVector, P, Plantes, Releve);
		return Plantes;
	};

	// LE MEME COMPTE, DANS LE REPERE DU PAN : on verifie une propriete
	// geometrique du resultat, pas la ligne de code qui l'a produite.
	auto Dedans = [&](const TArray<FWorldseedPlante>& Plantes) -> int32
	{
		int32 N = 0;
		for (const FWorldseedPlante& Pl : Plantes)
		{
			const FVector T = Pl.Transform.GetLocation();
			const FVector2D D = FVector2D(T.X, T.Y) - Pan.CentreCm;
			const double LX = D.X * Pan.CosLacet + D.Y * Pan.SinLacet;
			const double LY = -D.X * Pan.SinLacet + D.Y * Pan.CosLacet;
			if (FMath::Abs(LX) <= Pan.DemiCm.X && FMath::Abs(LY) <= Pan.DemiCm.Y)
			{
				++N;
			}
		}
		return N;
	};

	const TArray<FWorldseedPlante> Avec = Semis(Pans);
	const TArray<FWorldseedPlante> Sans = Semis(TArray<FWorldseedEmpriseParoi>());

	AddInfo(FString::Printf(
		TEXT("avec le pan : %d plantes dont %d dessous  |  temoin sans pan : %d dont %d"),
		Avec.Num(), Dedans(Avec), Sans.Num(), Dedans(Sans)));

	// LE TEMOIN EST LE SEMIS SANS PAN : sans lui, « zero dessous » serait vrai
	// d'un chunk ou rien ne pousse, et ne prouverait rien du tout.
	TestTrue(TEXT("sans pan, des plantes tombent bien a cet endroit"),
		Dedans(Sans) > 10);
	TestEqual(TEXT("avec le pan, aucune plante dessous"), Dedans(Avec), 0);

	// ET LE RESTE DU CHUNK NE DOIT PAS BOUGER : une garde qui viderait tout
	// passerait le test precedent sans rien valoir.
	TestTrue(TEXT("le pan n'emporte que son emprise"),
		Avec.Num() >= Sans.Num() - Dedans(Sans) - 2);
	return true;
}

#endif
