// Worldseed - les cavites : l'index spatial, l'espacement, et la borne du toit.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedCaves.h"
#include "Procedural/WorldseedLithology.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedRules.h"

#include "Misc/AutomationTest.h"

/**
 * TROIS PROPRIETES QUE LA PASSE DES CAVITES ACQUIERT PAR CONSTRUCTION, ET QUI
 * NE SE SIGNALENT PAS QUAND ELLES TOMBENT.
 *
 * C'est ce qui les rend couteuses. Une chambre dont le toit sort du sol ouvre
 * un puits a ciel ouvert que le controle de percement ne voit PAS -- il
 * n'echantillonne que les galeries -- et ce defaut a reellement existe, trouve
 * en preparant les chiffres d'un arbitrage et non par une sonde. Un index
 * spatial qui perd une primitive ne creuse tout simplement pas cette
 * cavite-la : le monde se maille sans erreur, avec un vide en moins.
 */
namespace
{
	/**
	 * Un monde fictif rendu KARSTIFIABLE, et la passe des cavites lancee dessus.
	 *
	 * LA ROCHE EST CHOISIE DANS LE CATALOGUE, PAS ECRITE EN DUR. Le catalogue a
	 * compte cinq roches puis huit, et un identifiant grave ici aurait designe
	 * une autre roche au premier ajout -- sans rien casser de visible, la passe
	 * cessant simplement de creuser.
	 */
	struct FSousSol
	{
		FWorldseedGeometry Geo;
		TArray<float> Relief;
		TArray<float> Pluie;
		FWorldseedLithology Litho;
		FWorldseedCaveNetwork Reseau;

		FWorldseedCaveRules Regles;
		bool bPret = false;
		FString Erreur;

		FSousSol()
		{
			const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
			if (!R) { return; }

			const FWorldseedLithologyRules LithoRegles =
				FWorldseedLithologyRules::FromRules(*R);
			Regles = FWorldseedCaveRules::FromRules(*R);

			// La roche la plus soluble du catalogue, quelle qu'elle soit.
			int32 Meilleure = INDEX_NONE;
			float MeilleurKarst = -1.0f;
			for (int32 I = 0; I < LithoRegles.Catalogue.Num(); ++I)
			{
				if (LithoRegles.Catalogue[I].Karstifiable > MeilleurKarst)
				{
					MeilleurKarst = LithoRegles.Catalogue[I].Karstifiable;
					Meilleure = I;
				}
			}
			if (Meilleure == INDEX_NONE || MeilleurKarst < Regles.MinKarstifiable)
			{
				Erreur = TEXT("aucune roche karstifiable dans le catalogue");
				return;
			}

			Geo = WorldseedTest::Geometrie(64);
			Relief = WorldseedTest::Relief(Geo);

			const int32 N = Geo.CellCount();
			Litho.Id.Init(static_cast<uint8>(Meilleure), N);

			// DE LA PLUIE PARTOUT, FRANCHEMENT AU-DESSUS DU SEUIL : ce test ne
			// porte pas sur le placement climatique, et une pluie juste a la
			// limite rendrait le nombre de chambres dependant d'un reglage.
			Pluie.Init(Regles.MinPrecipMm * 2.0f, N);

			WorldseedCaves::Build(Geo, Relief, Pluie, Litho, LithoRegles,
				Regles, /*HeightExaggeration=*/1.0f, /*Seed=*/4242, Reseau);

			bPret = true;
		}
	};
}

/**
 * LE TOIT D'UNE CHAMBRE NE DOIT PAS SORTIR DU SOL, ET C'EST UNE CONTRAINTE SUR
 * LES REGLES ELLES-MEMES.
 *
 * `DepthMinM` doit depasser `ChamberRadiusMaxM`, sinon une chambre semee a la
 * profondeur minimale avec le rayon maximal creve la surface et ouvre un puits
 * a ciel ouvert. CE DEFAUT A EXISTE -- douze metres de profondeur minimale pour
 * seize de rayon maximal -- et AUCUNE mesure ne le voyait : le controle de
 * percement n'echantillonne que les galeries. Il a ete trouve en preparant les
 * chiffres d'un arbitrage, par hasard.
 *
 * Ce test le rend impossible a reintroduire en silence. Il porte sur le FICHIER
 * DE REGLES, donc il echouera si quelqu'un y touche mal -- c'est precisement ce
 * qu'on veut ici, contrairement aux tests de code qui doivent l'ignorer.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestCavitesBornes,
	"Worldseed.Cavites.BornesDuToit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestCavitesBornes::RunTest(const FString& Parameters)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("world_rules.json se lit"), R))
	{
		AddError(Erreur);
		return false;
	}

	const FWorldseedCaveRules Regles = FWorldseedCaveRules::FromRules(*R);

	AddInfo(FString::Printf(
		TEXT("profondeur %.0f a %.0f m, rayon %.0f a %.0f m, espacement %.0f m"),
		Regles.DepthMinM, Regles.DepthMaxM,
		Regles.ChamberRadiusMinM, Regles.ChamberRadiusMaxM,
		Regles.ChamberSpacingM));

	TestTrue(TEXT("la profondeur minimale depasse le rayon maximal"),
		Regles.DepthMinM > Regles.ChamberRadiusMaxM);

	// LES BORNES SONT DANS LE BON ORDRE. Un minimum au-dessus de son maximum ne
	// leve aucune erreur : le tirage se contente de rendre des valeurs absurdes.
	TestTrue(TEXT("les profondeurs sont ordonnees"),
		Regles.DepthMinM <= Regles.DepthMaxM);
	TestTrue(TEXT("les rayons de chambre sont ordonnes"),
		Regles.ChamberRadiusMinM <= Regles.ChamberRadiusMaxM);
	TestTrue(TEXT("les rayons de galerie sont ordonnes"),
		Regles.TunnelRadiusMinM <= Regles.TunnelRadiusMaxM);

	// UNE GALERIE NE DOIT PAS ETRE PLUS LARGE QUE LA CHAMBRE QU'ELLE RELIE,
	// sans quoi elle deborde de la salle et perce le toit par le cote.
	TestTrue(TEXT("une galerie reste plus etroite que la plus petite chambre"),
		Regles.TunnelRadiusMaxM < Regles.ChamberRadiusMinM);

	return true;
}

/**
 * L'INDEX SPATIAL RETROUVE CE QU'IL INDEXE.
 *
 * IL EST DERIVE ET IL SE REBATIT, donc rien ne le verifie a la lecture : le
 * cache serialise le reseau mais PAS l'index -- il pese plus que ce qu'il
 * indexe, une entree par case TOUCHEE -- et `ReconstruireIndex` le refait au
 * chargement. C'est la MEME fonction que le temps `Indexer` de la generation,
 * sans quoi les deux auraient diverge a la premiere retouche.
 *
 * S'IL PERD UNE PRIMITIVE, RIEN NE LE DIT : le chunk qui aurait du contenir
 * cette cavite se maille sans erreur, plein. On ne voit qu'un vide en moins,
 * quelque part, sous terre.
 *
 * Le test interroge le reseau CHAMBRE PAR CHAMBRE, sur sa propre boite : une
 * primitive qui ne se retrouve pas elle-meme est un defaut, sans discussion
 * possible.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestCavitesIndex,
	"Worldseed.Cavites.IndexSpatial",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestCavitesIndex::RunTest(const FString& Parameters)
{
	// LA FIXTURE D'ABORD : un reseau fabrique a la main, dont on connait le
	// contenu exactement, et qui ne depend d'aucun reglage.
	{
		FWorldseedCaveNetwork Reseau = WorldseedTest::Reseau();
		if (!TestTrue(TEXT("le reseau fictif porte des chambres"),
			Reseau.Chambers.Num() > 0))
		{
			return false;
		}

		int32 Perdues = 0;
		for (int32 I = 0; I < Reseau.Chambers.Num(); ++I)
		{
			const FWorldseedCaveChamber& C = Reseau.Chambers[I];
			const FBox Boite(C.CentreM - FVector(1.0), C.CentreM + FVector(1.0));

			FWorldseedCaveLocal Local;
			Reseau.Query(Boite, Local);
			if (!Local.Chambers.Contains(I)) { ++Perdues; }
		}
		TestEqual(TEXT("chaque chambre fictive se retrouve elle-meme"), Perdues, 0);

		// ET L'INDEX SE REFAIT A L'IDENTIQUE. C'est ce que fait le chargement du
		// cache : si la reconstruction differait de la construction, les
		// cavites seraient a leur place au premier lancement et ailleurs au
		// second -- un defaut qui ne se reproduit pas, donc qu'on ne trouve pas.
		Reseau.ReconstruireIndex();

		int32 PerduesApres = 0;
		for (int32 I = 0; I < Reseau.Chambers.Num(); ++I)
		{
			const FWorldseedCaveChamber& C = Reseau.Chambers[I];
			const FBox Boite(C.CentreM - FVector(1.0), C.CentreM + FVector(1.0));

			FWorldseedCaveLocal Local;
			Reseau.Query(Boite, Local);
			if (!Local.Chambers.Contains(I)) { ++PerduesApres; }
		}
		TestEqual(TEXT("l'index reconstruit retrouve tout autant"), PerduesApres, 0);
	}

	// PUIS UN RESEAU REELLEMENT GENERE, ou les chambres sont nombreuses et
	// reparties par le semis plutot que posees en ligne.
	const FSousSol S;
	if (!TestTrue(TEXT("les regles se lisent"), S.bPret))
	{
		AddError(S.Erreur);
		return false;
	}

	AddInfo(FString::Printf(TEXT("%d chambres, %d troncons, %d puits, %d arches"),
		S.Reseau.Chambers.Num(), S.Reseau.Segments.Num(),
		S.Reseau.Puits.Num(), S.Reseau.Arches.Num()));

	// SANS CHAMBRE, TOUT CE QUI SUIT PASSE TRIVIALEMENT. Le depot a deja paye
	// deux fois un test muet faute de matiere -- des biomes vides ou deux
	// `nullptr` se comparaient egaux, et un comblement mesure sur un relief
	// sans cuvette.
	if (!TestTrue(TEXT("la passe a produit des chambres"),
		S.Reseau.Chambers.Num() > 0))
	{
		return false;
	}

	int32 Perdues = 0;
	for (int32 I = 0; I < S.Reseau.Chambers.Num(); ++I)
	{
		const FWorldseedCaveChamber& C = S.Reseau.Chambers[I];
		const FBox Boite(C.CentreM - FVector(1.0), C.CentreM + FVector(1.0));

		FWorldseedCaveLocal Local;
		S.Reseau.Query(Boite, Local);
		if (!Local.Chambers.Contains(I)) { ++Perdues; }
	}
	TestEqual(TEXT("chaque chambre generee se retrouve elle-meme"), Perdues, 0);

	return true;
}

/**
 * LE SEMIS TIENT SES DEUX PROMESSES : L'ESPACEMENT, ET RIEN SOUS LA MER.
 *
 * L'espacement minimal est obtenu par REFUS sur une grille de hachage -- sans
 * quoi le cout serait quadratique -- et un defaut de cette grille ne se verrait
 * que par des salles qui se recouvrent, ce qu'on ne regarde jamais.
 *
 * « Rien ne se creuse sous la mer » est une decision du proprietaire, posee AU
 * SEMIS et dans le COUT du routage, jamais corrigee apres coup. Elle mord
 * beaucoup plus qu'il n'y parait : une chambre profonde avec un grand rayon
 * exige une altitude consequente, ce qui cantonne les grottes aux collines.
 * C'est voulu, et c'est donc a verifier.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestCavitesSemis,
	"Worldseed.Cavites.Semis",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestCavitesSemis::RunTest(const FString& Parameters)
{
	const FSousSol S;
	if (!TestTrue(TEXT("les regles se lisent"), S.bPret))
	{
		AddError(S.Erreur);
		return false;
	}
	if (!TestTrue(TEXT("la passe a produit des chambres"),
		S.Reseau.Chambers.Num() > 0))
	{
		return false;
	}

	const int32 N = S.Reseau.Chambers.Num();

	// --- L'ESPACEMENT MINIMAL -----------------------------------------------
	double PlusProche = TNumericLimits<double>::Max();
	int32 TropProches = 0;
	for (int32 I = 0; I < N; ++I)
	{
		for (int32 J = I + 1; J < N; ++J)
		{
			const double D = FVector::Distance(S.Reseau.Chambers[I].CentreM,
				S.Reseau.Chambers[J].CentreM);
			PlusProche = FMath::Min(PlusProche, D);
			if (D < S.Regles.ChamberSpacingM * 0.999) { ++TropProches; }
		}
	}

	// --- LES BORNES DE CHAQUE CHAMBRE ---------------------------------------
	int32 HorsRayon = 0;
	int32 SousLaMer = 0;
	double PlusHautToit = -TNumericLimits<double>::Max();
	for (const FWorldseedCaveChamber& C : S.Reseau.Chambers)
	{
		if (C.RadiusM < S.Regles.ChamberRadiusMinM * 0.999f
			|| C.RadiusM > S.Regles.ChamberRadiusMaxM * 1.001f)
		{
			++HorsRayon;
		}
		// Le PLANCHER de la salle, pas son centre : c'est lui qui serait noye.
		if (C.CentreM.Z - C.RadiusM <= 0.0) { ++SousLaMer; }
		PlusHautToit = FMath::Max(PlusHautToit, C.CentreM.Z + C.RadiusM);
	}

	AddInfo(FString::Printf(
		TEXT("%d chambres, la paire la plus proche a %.1f m pour %.0f exiges"),
		N, PlusProche, S.Regles.ChamberSpacingM));

	TestEqual(TEXT("aucune paire de chambres trop proche"), TropProches, 0);
	TestEqual(TEXT("aucun rayon hors des bornes du catalogue"), HorsRayon, 0);
	TestEqual(TEXT("aucune chambre sous le niveau de la mer"), SousLaMer, 0);

	// --- LES TRONCONS, ET CE QU'ON NE PEUT PAS LEUR DEMANDER D'ICI ----------
	//
	// J'AI D'ABORD EXIGE QUE TOUT TRONCON TIENNE DANS LES BORNES DE GALERIE.
	// Le test a rendu 2526 violations sur 7578 -- et c'etait MA mesure qui
	// etait fausse, pas la passe. `Segments` melange QUATRE familles : les
	// galeries, les bouches de falaise, les capsules des puits -- dolines et
	// avens, qui s'evasent par construction -- et le percement des arches.
	// 504 puits d'environ cinq capsules font justement ces deux mille cinq
	// cents troncons.
	//
	// LE DEPOT CONNAISSAIT DEJA CE PIEGE SUR CE MEME TABLEAU : le controle de
	// percement « courait jusqu'a la fin du tableau des segments, donc il
	// avalait les entrees -- qui percent le sol A DESSEIN », et il a fallu une
	// borne `FinDesGaleries` pour le refermer. Cette borne est LOCALE a la
	// passe : le reseau ne la porte pas, donc rien ne permet de retrouver les
	// familles depuis l'exterieur.
	//
	// L'exposer serait la bonne reponse -- elle servirait aussi aux sondes --
	// mais le reseau est SERIALISE dans le cache depuis hier : ajouter un champ
	// oblige a bumper `WORLDSEED_PIPELINE_VERSION`, donc a faire regenerer leur
	// monde a tout le monde. Ce n'est pas une decision qu'un test prend.
	//
	// On n'affirme donc ici que ce qui vaut pour LES QUATRE FAMILLES : un
	// troncon degenere ne creuse rien, et il ne se signale nulle part.
	int32 Degeneres = 0;
	double RayonMin = TNumericLimits<double>::Max();
	double RayonMax = 0.0;
	for (const FWorldseedCaveSegment& G : S.Reseau.Segments)
	{
		if (G.RadiusAM <= 0.0f || G.RadiusBM <= 0.0f) { ++Degeneres; }
		if (FVector::DistSquared(G.AM, G.BM) <= 0.0) { ++Degeneres; }

		RayonMin = FMath::Min3(RayonMin, static_cast<double>(G.RadiusAM),
			static_cast<double>(G.RadiusBM));
		RayonMax = FMath::Max3(RayonMax, static_cast<double>(G.RadiusAM),
			static_cast<double>(G.RadiusBM));
	}

	AddInfo(FString::Printf(
		TEXT("%d troncons toutes familles confondues, rayons de %.2f a %.2f m ")
		TEXT("(galeries reglees a %.1f-%.1f)"),
		S.Reseau.Segments.Num(), RayonMin, RayonMax,
		S.Regles.TunnelRadiusMinM, S.Regles.TunnelRadiusMaxM));

	TestEqual(TEXT("aucun troncon degenere"), Degeneres, 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
