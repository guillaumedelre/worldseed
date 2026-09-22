// Worldseed - le monde partage : une seule copie, et elle survit a son porteur.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedWorldData.h"

#include "Misc/AutomationTest.h"

/**
 * PARTAGER, C'EST NE PAS COPIER -- ET CELA SE TESTE PAR L'IDENTITE.
 *
 * POURQUOI L'IDENTITE ET NON L'EGALITE. Comparer les VALEURS de deux tableaux
 * ne dit rien : une copie fidele les rend egales. Ce qu'on veut prouver est
 * qu'il n'y a qu'UN exemplaire en memoire, et la seule chose qui le prouve est
 * que les deux porteurs voient la MEME ADRESSE. Un test d'egalite passerait
 * exactement dans le cas qu'on cherche a interdire.
 *
 * L'ENJEU EST CHIFFRE : sur la grille 4096x2048 du jeu, les sept grands
 * tableaux du monde pesent cent quatre-vingt-treize megaoctets. Avant ce
 * chantier, le terrain et l'acteur voxel en tenaient chacun un jeu complet --
 * `AdoptWorld` recopiait tout.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestMondePartage,
	"Worldseed.Monde.PartageSansCopie",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestMondePartage::RunTest(const FString& Parameters)
{
	FWorldseedMondeRef Monde = MakeShared<const FWorldseedWorldData, ESPMode::ThreadSafe>(
		WorldseedTest::Monde());

	// Deux porteurs, comme le terrain et l'acteur voxel.
	FWorldseedMondePtr Porteur1 = Monde;
	FWorldseedMondePtr Porteur2 = Monde;

	if (!TestTrue(TEXT("les deux porteurs tiennent un monde"),
		Porteur1.IsValid() && Porteur2.IsValid()))
	{
		return false;
	}

	// LE TEST LUI-MEME : la meme adresse, donc le meme octet en memoire.
	TestEqual(TEXT("altitude : la meme memoire"),
		(void*)Porteur2->ElevationM.GetData(), (void*)Porteur1->ElevationM.GetData());
	TestEqual(TEXT("temperature : la meme memoire"),
		(void*)Porteur2->TempC.GetData(), (void*)Porteur1->TempC.GetData());
	TestEqual(TEXT("precipitations : la meme memoire"),
		(void*)Porteur2->PrecipMm.GetData(), (void*)Porteur1->PrecipMm.GetData());
	TestEqual(TEXT("amplitude saisonniere : la meme memoire"),
		(void*)Porteur2->SeasonalAmpC.GetData(), (void*)Porteur1->SeasonalAmpC.GetData());
	TestEqual(TEXT("continentalite : la meme memoire"),
		(void*)Porteur2->Continentality.GetData(), (void*)Porteur1->Continentality.GetData());
	TestEqual(TEXT("lithologie : la meme memoire"),
		(void*)Porteur2->LithologyId.GetData(), (void*)Porteur1->LithologyId.GetData());
	TestEqual(TEXT("biomes : la meme memoire"),
		(void*)Porteur2->Biomes.Index.GetData(), (void*)Porteur1->Biomes.Index.GetData());

	// ET LE TEMOIN QUI DONNE SON SENS AU TEST : une COPIE, elle, a une autre
	// adresse. Sans lui, on ne saurait pas que la comparaison discrimine --
	// c'est la meme precaution que le temoin bilineaire du test de grille.
	const FWorldseedWorldData Copie = *Monde;
	TestNotEqual(TEXT("une copie, elle, occupe une AUTRE memoire"),
		(void*)Copie.ElevationM.GetData(), (void*)Monde->ElevationM.GetData());

	return true;
}

/**
 * UNE REFERENCE SURVIT A SON PORTEUR, et c'est le pointeur pendant qu'on corrige.
 *
 * LE DEFAUT REEL. Le champ de densite etait un membre PAR VALEUR de l'acteur
 * voxel, et chaque travail de maillage en capturait l'ADRESSE. `EndPlay`
 * annule les travaux sans les attendre -- c'est le bon choix, attendre
 * bloquerait la fermeture du jeu -- donc un fil pouvait lire cette memoire
 * pendant que le ramasse-miettes s'appretait a la liberer.
 *
 * Ce test reproduit la sequence : un porteur cree le monde, un « travail » en
 * prend une reference, le porteur disparait, et le travail lit encore.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestMondeSurvie,
	"Worldseed.Monde.SurvitAuProprietaire",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestMondeSurvie::RunTest(const FString& Parameters)
{
	FWorldseedMondePtr Travail;
	const float* Adresse = nullptr;
	int32 Attendu = 0;
	float PremiereValeur = 0.0f;

	{
		// Le porteur : l'acteur, dans le jeu.
		FWorldseedMondePtr Porteur = MakeShared<const FWorldseedWorldData, ESPMode::ThreadSafe>(
			WorldseedTest::Monde());

		Travail = Porteur;          // le travail prend sa reference
		Adresse = Porteur->ElevationM.GetData();
		Attendu = Porteur->ElevationM.Num();
		PremiereValeur = Porteur->ElevationM[0];

		TestEqual(TEXT("deux references pendant que le porteur vit"),
			Porteur.GetSharedReferenceCount(), 2);

		// L'acteur meurt -- EndPlay, puis le ramasse-miettes.
		Porteur.Reset();
	}

	// ET LE TRAVAIL LIT ENCORE. Sans la reference partagee, cette lecture
	// tomberait dans de la memoire liberee -- silencieusement la plupart du
	// temps, ce qui est le pire des cas.
	if (!TestTrue(TEXT("le travail tient toujours le monde"), Travail.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("une seule reference reste"),
		Travail.GetSharedReferenceCount(), 1);
	TestEqual(TEXT("la memoire n'a pas bouge"),
		(void*)Travail->ElevationM.GetData(), (void*)Adresse);
	TestEqual(TEXT("le tableau est intact"), Travail->ElevationM.Num(), Attendu);
	TestEqual(TEXT("la valeur est intacte"), Travail->ElevationM[0], PremiereValeur);

	return true;
}

/**
 * LE MONDE EST IMMUABLE PAR LE TYPE, PAS PAR CONVENTION.
 *
 * Entre fils, un porteur qui modifierait ce que les autres lisent serait une
 * course -- et une course ne se voit pas, elle se paie plus tard. Le `const`
 * de l'alias l'interdit, et ce controle le CONSTATE au lieu de le supposer :
 * si quelqu'un retire le `const` un jour, cette ligne casse la compilation.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestMondeImmuable,
	"Worldseed.Monde.Immuable",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestMondeImmuable::RunTest(const FString& Parameters)
{
	static_assert(
		std::is_const_v<std::remove_reference_t<decltype(*std::declval<FWorldseedMondeRef>())>>,
		"Le monde partage doit etre const : plusieurs fils le lisent en meme temps.");

	static_assert(
		std::is_const_v<std::remove_reference_t<decltype(*std::declval<FWorldseedMondePtr>())>>,
		"Le monde partage doit etre const : plusieurs fils le lisent en meme temps.");

	// Le test existe pour porter les assertions ci-dessus, qui sont verifiees a
	// la COMPILATION. On le fait aussi passer a l'execution pour qu'il figure
	// dans le releve au meme titre que les autres.
	AddInfo(TEXT("immuabilite verifiee a la compilation (static_assert)"));
	return true;
}

/**
 * LE POIDS DU MONDE SUIT LA GRILLE, ET RIEN D'AUTRE.
 *
 * `OctetsApprox` existe pour qu'une duplication se VOIE : la memoire du
 * processus derive de trois cents megaoctets d'un lancement a l'autre -- 7,60
 * a 7,99 Go releves sur cinq passes identiques -- donc les deux cent vingt
 * qu'une copie du monde ajouterait s'y noient. Un chiffre qui sert de temoin
 * doit lui-meme etre juste, et c'est ce que ce test constate.
 *
 * TROIS PROPRIETES, ET LA DEUXIEME EST LA SEULE QUI COMPTE VRAIMENT. La valeur
 * exacte sur la fixture attrape une erreur de type -- compter un `uint8` comme
 * un `float` quadruplerait sa part. Le facteur QUATRE quand la grille double
 * sur ses deux axes est la propriete de fond : un poids qui ne suivrait pas la
 * grille ne mesurerait pas le monde. Et l'insensibilite aux petits champs dit
 * ce que « approximatif » veut dire, pour qu'on ne le lise pas comme le poids
 * exact de la structure.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestMondePoids,
	"Worldseed.Monde.Poids",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestMondePoids::RunTest(const FString& Parameters)
{
	const FWorldseedWorldData Petit = WorldseedTest::Monde(16);
	const int32 N = Petit.CellCount();

	// Cinq tableaux de flottants, trois d'octets, plus la pente en flottants.
	const int64 Attendu = static_cast<int64>(N) * (5 * 4 + 3 * 1 + 4);
	TestEqual(TEXT("le compte tombe juste sur la fixture"),
		Petit.OctetsApprox(), Attendu);

	// LA PROPRIETE DE FOND : doubler chaque axe quadruple le poids.
	const FWorldseedWorldData Grand = WorldseedTest::Monde(32);
	TestEqual(TEXT("la grille doublee quadruple les cellules"),
		Grand.CellCount(), N * 4);
	TestEqual(TEXT("et quadruple le poids"),
		Grand.OctetsApprox(), Petit.OctetsApprox() * 4);

	// « APPROXIMATIF » : les petits champs n'y entrent pas.
	FWorldseedWorldData Retouche = Petit;
	Retouche.Seed = 999;
	Retouche.bHasSpawn = true;
	Retouche.SpawnXYM = FVector2D(1234.0, -5678.0);
	TestEqual(TEXT("les petits champs ne pesent pas"),
		Retouche.OctetsApprox(), Petit.OctetsApprox());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
