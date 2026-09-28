// Worldseed - le cache doit rendre EXACTEMENT ce qu'on lui a confie.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedCache.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedRules.h"

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

	// --- 3 bis. LES TABLES ET LES CANYONS -----------------------------------
	//
	// MEME FAMILLE DE DEFAUT QUE LE RESEAU DE CAVITES, et c'est pourquoi le
	// controle est le meme : des sites absents se relisent VIDES, sans erreur,
	// et le seul symptome est une liste de lieux amputee au menu plus 2,8
	// secondes rendues a chaque retour. Rien de tout cela ne leve un
	// avertissement.
	//
	// ON COMPARE LES QUATRE CHAMPS, pas seulement le compte. Un compte juste
	// avec des altitudes decalees d'un cran decrirait des mesas ailleurs, et
	// le nombre ne le dirait pas.
	auto ComparerSites = [this](const TCHAR* Nom,
		const TArray<FWorldseedPlateauSite>& A,
		const TArray<FWorldseedPlateauSite>& B)
	{
		if (!TestEqual(*FString::Printf(TEXT("%s : nombre de sites"), Nom),
			B.Num(), A.Num()))
		{
			return;
		}
		for (int32 I = 0; I < A.Num(); ++I)
		{
			TestEqual(*FString::Printf(TEXT("%s %d : centre"), Nom, I),
				B[I].CentreM, A[I].CentreM);
			TestEqual(*FString::Printf(TEXT("%s %d : altitude"), Nom, I),
				B[I].AltitudeM, A[I].AltitudeM);
			TestEqual(*FString::Printf(TEXT("%s %d : escarpement"), Nom, I),
				B[I].EscarpementM, A[I].EscarpementM);
			TestEqual(*FString::Printf(TEXT("%s %d : direction de chute"), Nom, I),
				B[I].VersLeBas, A[I].VersLeBas);
		}
	};

	ComparerSites(TEXT("table"), Avant.Tables, Apres.Tables);
	ComparerSites(TEXT("canyon"), Avant.Canyons, Apres.Canyons);

	// ET LA FIXTURE DOIT PORTER DES SITES, sans quoi tout ce qui precede
	// compare deux tableaux vides et passe quoi qu'il arrive. Le depot a deja
	// paye une assertion muette de cette forme -- deux `nullptr` compares dans
	// le test de partage du monde.
	TestTrue(TEXT("le monde fictif porte des tables"), Avant.Tables.Num() > 0);
	TestTrue(TEXT("le monde fictif porte des canyons"), Avant.Canyons.Num() > 0);

	// --- 3 bis. LE DECOUPAGE EN REGIONS ------------------------------------
	//
	// ⚠ IL COMPTE PLUS QUE LES AUTRES, et l'oubli qui suit l'a prouve le jour
	// meme : `EcrireRegions` n'avait PAS ete insere dans `Save` -- une
	// substitution qui n'avait pas trouve sa ligne -- alors que `LireRegions`
	// etait bien branchee. Le flux lu au-dela de sa fin echouait sur ses
	// bornes, et c'est CE TEST qui l'a dit, en une ligne.
	//
	// LA RAISON DE FOND : les cavites et les sites sont deterministes et se
	// refont. Une serialisation qui ment leur coute du temps. Le decoupage,
	// lui, ne se refait PAS -- un bassin versant demande un etiquetage global
	// -- donc une serialisation qui ment coute les frontieres et les noms,
	// definitivement.
	{
		const FWorldseedRegions& A = Avant.Regions;
		const FWorldseedRegions& B = Apres.Regions;

		TestEqual(TEXT("regions : NX"), B.NX, A.NX);
		TestEqual(TEXT("regions : NY"), B.NY, A.NY);
		TestEqual(TEXT("regions : facteur"), B.Facteur, A.Facteur);
		TestEqual(TEXT("regions : largeur du monde"), B.LargeurM, A.LargeurM);
		TestEqual(TEXT("regions : hauteur du monde"), B.HauteurM, A.HauteurM);

		if (TestEqual(TEXT("regions : nombre de cellules"), B.Id.Num(), A.Id.Num()))
		{
			int32 Ecarts = 0;
			for (int32 I = 0; I < A.Id.Num(); ++I)
			{
				Ecarts += (A.Id[I] != B.Id[I]) ? 1 : 0;
			}
			TestEqual(TEXT("regions : la grille est identique"), Ecarts, 0);
		}

		if (TestEqual(TEXT("regions : nombre de regions"),
			B.Regions.Num(), A.Regions.Num()))
		{
			for (int32 I = 0; I < A.Regions.Num(); ++I)
			{
				const FWorldseedRegion& X = A.Regions[I];
				const FWorldseedRegion& Y = B.Regions[I];
				TestEqual(*FString::Printf(TEXT("region %d : id"), I), Y.Id, X.Id);
				TestEqual(*FString::Printf(TEXT("region %d : pays"), I), Y.Pays, X.Pays);
				TestEqual(*FString::Printf(TEXT("region %d : caractere"), I),
					static_cast<int32>(Y.Caractere), static_cast<int32>(X.Caractere));
				TestEqual(*FString::Printf(TEXT("region %d : biome dominant"), I),
					static_cast<int32>(Y.BiomeDominant),
					static_cast<int32>(X.BiomeDominant));
				TestEqual(*FString::Printf(TEXT("region %d : centre"), I),
					Y.CentreM, X.CentreM);
				TestEqual(*FString::Printf(TEXT("region %d : aire"), I),
					Y.AireKm2, X.AireKm2);
				TestEqual(*FString::Printf(TEXT("region %d : altitude"), I),
					Y.AltitudeMoyenneM, X.AltitudeMoyenneM);
				TestEqual(*FString::Printf(TEXT("region %d : temperature"), I),
					Y.TemperatureMoyenneC, X.TemperatureMoyenneC);
				TestEqual(*FString::Printf(TEXT("region %d : pluie"), I),
					Y.PluieMoyenneMm, X.PluieMoyenneMm);
				TestEqual(*FString::Printf(TEXT("region %d : part littorale"), I),
					Y.PartLittorale, X.PartLittorale);
			}
		}

		if (TestEqual(TEXT("regions : nombre de pays"), B.Pays.Num(), A.Pays.Num()))
		{
			for (int32 I = 0; I < A.Pays.Num(); ++I)
			{
				TestEqual(*FString::Printf(TEXT("pays %d : id"), I),
					B.Pays[I].Id, A.Pays[I].Id);
				TestEqual(*FString::Printf(TEXT("pays %d : centre"), I),
					B.Pays[I].CentreM, A.Pays[I].CentreM);
				TestEqual(*FString::Printf(TEXT("pays %d : aire"), I),
					B.Pays[I].AireKm2, A.Pays[I].AireKm2);
				TestEqual(*FString::Printf(TEXT("pays %d : nombre de regions"), I),
					B.Pays[I].Regions.Num(), A.Pays[I].Regions.Num());
				for (int32 K = 0; K < A.Pays[I].Regions.Num()
					&& K < B.Pays[I].Regions.Num(); ++K)
				{
					TestEqual(*FString::Printf(TEXT("pays %d : membre %d"), I, K),
						B.Pays[I].Regions[K], A.Pays[I].Regions[K]);
				}
			}
		}

		// LA FIXTURE DOIT PORTER DE LA MATIERE, sinon tout ce qui precede
		// compare des riens -- la faute la plus frequente de ce depot.
		TestTrue(TEXT("le monde fictif porte des regions"), A.Regions.Num() > 0);
		TestTrue(TEXT("le monde fictif porte des pays"), A.Pays.Num() > 0);
		TestTrue(TEXT("le monde fictif porte une grille de regions"),
			A.Id.Num() > 0);

		// ⚠ ET LES NOMS NE DOIVENT PAS TRAVERSER. Ils se rejouent depuis des
		// corpus qui n'entrent PAS dans l'empreinte du cache : les serialiser
		// figerait les noms d'un monde a ceux du corpus du jour de sa
		// generation, et retoucher une base de noms n'aurait plus d'effet sur
		// les mondes deja joues -- sans que rien ne le dise. Ce controle est
		// donc a l'ENVERS des autres, et c'est voulu.
		bool bUnNomATraverse = false;
		for (const FWorldseedRegion& Y : B.Regions)
		{
			bUnNomATraverse |= !Y.Nom.IsEmpty();
		}
		for (const FWorldseedPays& Y : B.Pays)
		{
			bUnNomATraverse |= !Y.Nom.IsEmpty();
		}
		TestFalse(TEXT("les noms ne sont PAS serialises, ils se rejouent"),
			bUnNomATraverse);
		TestTrue(TEXT("temoin : la fixture, elle, en portait bien"),
			!A.Regions[0].Nom.IsEmpty());
	}

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


/**
 * LE CACHE PORTE LA ROCHE, ET LES DEUX CHEMINS RENDENT LE MEME RELIEF.
 *
 * CE QU'IL AURAIT ATTRAPE, ET LE DEFAUT A VECU JUSQU'AU 28 SEPTEMBRE 2026.
 * `WorldseedIce::Apply` ecrit DANS le relief -- sa signature le dit,
 * `TArray<float>&` -- et l'ecriture du cache la suivait : le fichier gardait
 * un relief DEJA ENGLACE, sur lequel le rechargement reposait un second dome.
 * Mesure, graine 20260909 : bande -90..-80 a 685 m en generation contre 924 au
 * rechargement, -80..-70 a 595 contre 707, sommet du monde 1611 contre 1669.
 * Un joueur avait donc un pole trois cents metres plus haut au SECOND
 * lancement de son monde qu'au premier, sans un mot au journal.
 *
 * POURQUOI RIEN NE L'AVAIT VU, ET C'EST LE POINT.
 *   - Le defaut n'est pas CUMULATIF : deux relectures successives rendent le
 *     meme chiffre au metre. Il ne se lit qu'en comparant les deux CHEMINS, ce
 *     qu'aucune sonde ne faisait -- elles mesurent un monde, pas deux facons
 *     de l'obtenir.
 *   - Le commentaire de la passe decrivait l'invariant JUSTE -- « elle passe
 *     apres la mise en cache, qui garde le relief de roche » -- pendant que le
 *     code faisait l'inverse. Une note exacte sur du code faux ne protege rien.
 *
 * IL PORTE SON PROPRE TEMOIN, ET IL LE FAUT. Si la glace n'etait jamais posee
 * -- calotte absente a cette resolution, epaisseur reglee a zero -- la
 * comparaison finale passerait TRIVIALEMENT, et ce depot a deja paye quatre
 * fixtures muettes en une journee. On exige donc d'abord que le relief du
 * CACHE DIFFERE de celui qu'on vient de generer : cela prouve du meme coup que
 * la glace existe et que le fichier ne la porte pas.
 *
 * IL GENERE UN VRAI MONDE, et c'est assume : l'invariant porte sur la chaine
 * entiere et aucun monde fictif ne l'exerce. Cent vingt-huit lignes suffisent
 * -- on ne juge ici ni un calage ni une forme, seulement l'accord de deux
 * chemins -- et l'entree de cache est supprimee a la sortie, sans quoi la
 * liste du menu montrerait un monde fantome.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestCacheReliefDeRoche,
	"Worldseed.Cache.LeCachePorteLaRoche",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestCacheReliefDeRoche::RunTest(const FString& Parameters)
{
	constexpr int32 Graine = 20260928;
	constexpr float HauteurM = 32000.0f;
	constexpr int32 Lignes = 128;

	FString Erreur;
	const UWorldseedRules* Regles = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("les regles se chargent"), Regles))
	{
		AddError(Erreur);
		return false;
	}

	// ON SUPPRIME L'ENTREE AVANT DE COMMENCER, sans quoi le PREMIER appel
	// pourrait relire un cache laisse par une execution anterieure : les deux
	// appels seraient alors des lectures, et le test ne comparerait plus rien.
	// `bFromCache` le verifie ensuite, pour que l'echec soit franc si jamais.
	const FString Cle = WorldseedCache::MakeKey(Graine, HauteurM, Lignes,
		Regles->SourceHash);
	const FString Chemin = WorldseedCache::PathForKey(Cle);
	IFileManager::Get().Delete(*Chemin, false, true, true);

	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*Chemin, false, true, true);
	};

	WorldseedPipeline::FResult Genere;
	if (!TestTrue(TEXT("la generation aboutit"),
		WorldseedPipeline::Generate(Graine, HauteurM, Lignes, Genere, Erreur)))
	{
		AddError(Erreur);
		return false;
	}
	if (!TestFalse(TEXT("le premier appel GENERE, il ne relit pas"),
		Genere.bFromCache))
	{
		return false;
	}

	// LE TEMOIN : le fichier doit porter un AUTRE relief que le resultat.
	FWorldseedWorldData Roche;
	if (!TestTrue(TEXT("le cache vient d'etre ecrit et se relit"),
		WorldseedCache::Load(Cle, Roche)))
	{
		return false;
	}
	if (!TestEqual(TEXT("le cache porte autant de cellules que le monde"),
		Roche.ElevationM.Num(), Genere.ElevationM.Num()))
	{
		return false;
	}

	int32 Englacees = 0;
	float PlusGrosDome = 0.0f;
	for (int32 I = 0; I < Roche.ElevationM.Num(); ++I)
	{
		const float Dome = Genere.ElevationM[I] - Roche.ElevationM[I];
		if (Dome > 0.01f)
		{
			++Englacees;
			PlusGrosDome = FMath::Max(PlusGrosDome, Dome);
		}
	}
	AddInfo(FString::Printf(
		TEXT("glace : %d cellules sur %d, dome maximal %.1f m -- le cache, lui, ")
		TEXT("porte la roche"),
		Englacees, Roche.ElevationM.Num(), PlusGrosDome));

	// SANS CETTE LIGNE LE TEST SERAIT MUET : un monde sans calotte rendrait la
	// comparaison finale vraie sans rien prouver.
	if (!TestTrue(TEXT("de la glace a bien ete posee, donc le temoin vaut"),
		Englacees > 0 && PlusGrosDome > 1.0f))
	{
		return false;
	}

	WorldseedPipeline::FResult Repris;
	if (!TestTrue(TEXT("la reprise aboutit"),
		WorldseedPipeline::Generate(Graine, HauteurM, Lignes, Repris, Erreur)))
	{
		AddError(Erreur);
		return false;
	}
	if (!TestTrue(TEXT("le second appel RELIT le cache"), Repris.bFromCache))
	{
		return false;
	}

	// L'INVARIANT. Il se lit cellule par cellule et non sur le seul sommet :
	// un dome pose deux fois au pole ne deplace pas forcement le maximum du
	// monde, qui est une montagne ailleurs -- il ne l'avait deplace que de
	// 58 m quand la bande polaire, elle, montait de 239.
	if (!TestEqual(TEXT("la reprise rend autant de cellules"),
		Repris.ElevationM.Num(), Genere.ElevationM.Num()))
	{
		return false;
	}

	int32 Differentes = 0;
	float PireEcart = 0.0f;
	for (int32 I = 0; I < Genere.ElevationM.Num(); ++I)
	{
		const float Ecart = FMath::Abs(Repris.ElevationM[I] - Genere.ElevationM[I]);
		if (Ecart > 0.01f) { ++Differentes; }
		PireEcart = FMath::Max(PireEcart, Ecart);
	}

	AddInfo(FString::Printf(
		TEXT("generation contre reprise : %d cellules differentes, pire ecart %.3f m"),
		Differentes, PireEcart));

	TestEqual(TEXT("aucune cellule ne differe entre generation et reprise"),
		Differentes, 0);
	TestTrue(TEXT("le sommet du monde est le meme sur les deux chemins"),
		FMath::Abs(Repris.MaxElevationM - Genere.MaxElevationM) < 0.01f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
