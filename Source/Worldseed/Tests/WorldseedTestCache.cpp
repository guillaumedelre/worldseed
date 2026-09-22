// Worldseed - le cache doit rendre EXACTEMENT ce qu'on lui a confie.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedCache.h"

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"

/**
 * POURQUOI CE TEST EXISTE, ET CE QU'IL AURAIT ATTRAPE.
 *
 * UN CACHE QUI SE TROMPE NE LEVE AUCUNE ERREUR. C'est ce qui le rend
 * dangereux : il rend un monde parfaitement bien forme, simplement faux. Ce
 * depot l'a paye deux fois, et les deux fois le defaut etait SILENCIEUX :
 *
 *   - le 19 septembre, une correction de l'attribution des roches ne changeait
 *     rien parce que les mondes revenaient du cache avec l'ancienne carte. Le
 *     signe qui a trahi : « un releve identique au chiffre pres apres une
 *     correction reelle » ;
 *   - le 22 septembre, l'ecriture du cache etait placee AVANT la passe des
 *     grottes. Le reseau partait au fichier alors qu'il n'existait pas encore,
 *     se relisait sans erreur, VIDE, et le code retombait proprement sur la
 *     reconstruction -- personne n'aurait su pourquoi l'attente persistait.
 *
 * Aucun des deux ne se voit a l'oeil ni au journal. Un aller-retour, si.
 *
 * ET IL NE TESTE PAS LA VERSION DE CHAINE. `WORLDSEED_PIPELINE_VERSION` doit
 * monter quand le CODE change le relief, ce qu'aucun test ne peut savoir : ce
 * jugement reste humain. Ce test verifie la mecanique du transport, pas la
 * discipline de l'incrementation.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestCacheAllerRetour,
	"Worldseed.Cache.AllerRetour",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestCacheAllerRetour::RunTest(const FString& Parameters)
{
	const FWorldseedWorldData Avant = WorldseedTest::Monde();

	// UNE CLE PROPRE A CE TEST, ET SUPPRIMEE A LA FIN. Sans cela le test
	// polluerait le cache du joueur, et la liste du menu montrerait un monde
	// fantome de seize lignes de haut.
	const FString Empreinte = TEXT("test-aller-retour");
	const FString Cle = WorldseedCache::MakeKey(Avant.Seed, Avant.Geometry.HeightM,
		Avant.Geometry.NY, Empreinte);
	const FString Chemin = WorldseedCache::PathForKey(Cle);

	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*Chemin, false, true, true);
	};

	if (!TestTrue(TEXT("l'ecriture du cache aboutit"),
		WorldseedCache::Save(Cle, Empreinte, Avant)))
	{
		return false;
	}

	FWorldseedWorldData Apres;
	if (!TestTrue(TEXT("la relecture du cache aboutit"),
		WorldseedCache::Load(Cle, Apres)))
	{
		return false;
	}

	// --- 1. LA GEOMETRIE ----------------------------------------------------
	TestEqual(TEXT("graine"), Apres.Seed, Avant.Seed);
	TestEqual(TEXT("NX"), Apres.Geometry.NX, Avant.Geometry.NX);
	TestEqual(TEXT("NY"), Apres.Geometry.NY, Avant.Geometry.NY);
	TestEqual(TEXT("hauteur du monde"), Apres.Geometry.HeightM, Avant.Geometry.HeightM);

	// --- 2. LES CHAMPS, VALEUR PAR VALEUR -----------------------------------
	//
	// PAS UN SONDAGE, UNE COMPARAISON EXHAUSTIVE. Un sondage a quelques points
	// laisserait passer un decalage d'indice ou une troncature en fin de
	// tableau, qui sont precisement les fautes qu'une serialisation commet.
	auto ComparerFloats = [this](const TCHAR* Nom, const TArray<float>& A,
		const TArray<float>& B)
	{
		if (!TestEqual(*FString::Printf(TEXT("%s : nombre de valeurs"), Nom),
			B.Num(), A.Num()))
		{
			return;
		}
		for (int32 I = 0; I < A.Num(); ++I)
		{
			if (A[I] != B[I])
			{
				// ON S'ARRETE AU PREMIER ECART, avec son INDICE. Signaler les
				// huit mille suivants noierait l'information utile : ce qui
				// compte est OU la divergence commence.
				AddError(FString::Printf(
					TEXT("%s : ecart au premier indice %d -- attendu %g, relu %g"),
					Nom, I, A[I], B[I]));
				return;
			}
		}
	};

	ComparerFloats(TEXT("altitude"), Avant.ElevationM, Apres.ElevationM);
	ComparerFloats(TEXT("temperature"), Avant.TempC, Apres.TempC);
	ComparerFloats(TEXT("precipitations"), Avant.PrecipMm, Apres.PrecipMm);
	ComparerFloats(TEXT("amplitude saisonniere"), Avant.SeasonalAmpC, Apres.SeasonalAmpC);
	ComparerFloats(TEXT("continentalite"), Avant.Continentality, Apres.Continentality);

	if (TestEqual(TEXT("lithologie : nombre de cellules"),
		Apres.LithologyId.Num(), Avant.LithologyId.Num()))
	{
		for (int32 I = 0; I < Avant.LithologyId.Num(); ++I)
		{
			if (Avant.LithologyId[I] != Apres.LithologyId[I])
			{
				AddError(FString::Printf(
					TEXT("lithologie : ecart a l'indice %d -- attendu %d, relu %d"),
					I, Avant.LithologyId[I], Apres.LithologyId[I]));
				break;
			}
		}
	}

	// --- 3. LE RESEAU DE CAVITES --------------------------------------------
	//
	// C'EST LE DEFAUT DU 22 SEPTEMBRE, et c'est pour lui que ce test existe.
	// Un reseau vide se relit sans erreur : seule une comparaison le voit.
	if (TestEqual(TEXT("cavites : nombre de chambres"),
		Apres.Caves.Chambers.Num(), Avant.Caves.Chambers.Num()))
	{
		for (int32 I = 0; I < Avant.Caves.Chambers.Num(); ++I)
		{
			TestEqual(*FString::Printf(TEXT("chambre %d : centre"), I),
				Apres.Caves.Chambers[I].CentreM, Avant.Caves.Chambers[I].CentreM);
			TestEqual(*FString::Printf(TEXT("chambre %d : rayon"), I),
				Apres.Caves.Chambers[I].RadiusM, Avant.Caves.Chambers[I].RadiusM);
		}
	}

	if (TestEqual(TEXT("cavites : nombre de troncons"),
		Apres.Caves.Segments.Num(), Avant.Caves.Segments.Num()))
	{
		for (int32 I = 0; I < Avant.Caves.Segments.Num(); ++I)
		{
			TestEqual(*FString::Printf(TEXT("troncon %d : depart"), I),
				Apres.Caves.Segments[I].AM, Avant.Caves.Segments[I].AM);
			TestEqual(*FString::Printf(TEXT("troncon %d : arrivee"), I),
				Apres.Caves.Segments[I].BM, Avant.Caves.Segments[I].BM);
		}
	}

	TestEqual(TEXT("cavites : nombre d'arches"),
		Apres.Caves.Arches.Num(), Avant.Caves.Arches.Num());
	TestEqual(TEXT("cavites : nombre de puits"),
		Apres.Caves.Puits.Num(), Avant.Caves.Puits.Num());

	// --- 4. L'INDEX SPATIAL EST DERIVE, DONC REBATI ET NON RELU -------------
	//
	// Les CASES ne sont pas serialisees -- elles pesent plus que ce qu'elles
	// indexent, une longue galerie figurant dans des dizaines d'entre elles --
	// mais les BORNES le sont, et il le faut : `ReconstruireIndex` ne les
	// calcule pas, elle les suppose posees. Sans elles, elle sort a sa
	// premiere garde et l'index reste vide.
	TestEqual(TEXT("cavites : taille de case de l'index"),
		Apres.Caves.CellM, Avant.Caves.CellM);
	TestEqual(TEXT("cavites : origine de l'index"),
		Apres.Caves.Min, Avant.Caves.Min);
	TestEqual(TEXT("cavites : etendue de l'index"),
		Apres.Caves.Size, Avant.Caves.Size);

	// ET ON VERIFIE LE COMPORTEMENT, PAS L'ETAT.
	//
	// Compter les cases dirait seulement qu'un tableau a ete alloue. Ce qui
	// compte est qu'une REQUETE rende la primitive attendue : c'est ce que le
	// terrain fait a chaque chunk, et c'est ce qui casse en silence quand
	// l'index manque. Le commentaire du cache le dit lui-meme -- « sans lui,
	// Query rendrait zero primitive par chunk et le terrain serait plein,
	// sans la moindre erreur ».
	if (Avant.Caves.Chambers.Num() > 0)
	{
		const FWorldseedCaveChamber& Visee = Avant.Caves.Chambers[0];
		FWorldseedCaveLocal Trouve;
		Apres.Caves.Query(
			FBox(Visee.CentreM - FVector(Visee.RadiusM),
				 Visee.CentreM + FVector(Visee.RadiusM)), Trouve);

		TestTrue(TEXT("une requete sur une chambre connue la retrouve"),
			Trouve.Chambers.Num() > 0);
	}

	TestTrue(TEXT("le reseau relu se declare valide"), Apres.Caves.IsValid());

	return true;
}

/**
 * LA CLE DOIT SEPARER CE QUI PRODUIT DES MONDES DIFFERENTS.
 *
 * Si deux jeux de reglages partageaient une cle, l'un servirait le monde de
 * l'autre -- exactement le defaut de 2026-09-19, mais permanent et sans
 * possibilite de le purger.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestCacheCle,
	"Worldseed.Cache.Cle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestCacheCle::RunTest(const FString& Parameters)
{
	const FString Base = WorldseedCache::MakeKey(1, 8000.0f, 1024, TEXT("aaa"));

	TestNotEqual(TEXT("une graine differente donne une cle differente"),
		WorldseedCache::MakeKey(2, 8000.0f, 1024, TEXT("aaa")), Base);
	TestNotEqual(TEXT("une taille differente donne une cle differente"),
		WorldseedCache::MakeKey(1, 16000.0f, 1024, TEXT("aaa")), Base);
	TestNotEqual(TEXT("une resolution differente donne une cle differente"),
		WorldseedCache::MakeKey(1, 8000.0f, 2048, TEXT("aaa")), Base);
	TestNotEqual(TEXT("des regles differentes donnent une cle differente"),
		WorldseedCache::MakeKey(1, 8000.0f, 1024, TEXT("bbb")), Base);

	TestEqual(TEXT("les memes parametres donnent la meme cle"),
		WorldseedCache::MakeKey(1, 8000.0f, 1024, TEXT("aaa")), Base);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
