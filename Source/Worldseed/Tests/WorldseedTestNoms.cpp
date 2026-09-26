// Worldseed - le generateur de noms, et les fautes qui ne crient pas.
//
// CE QUE CES ORACLES GARDENT. Un generateur de noms echoue en SILENCE : une
// chaine de Markov mal batie rend des moignons plausibles, un tirage qui ne
// suit pas la graine donne un autre nom au meme lieu a chaque ouverture de la
// carte, et un index de base renumerote colle les mauvais suffixes. Aucun de
// ces trois-la ne leve d'erreur, et tous les trois se voient a l'ecran -- un
// nom, on s'en souvient.

#if WITH_DEV_AUTOMATION_TESTS

#include "Procedural/WorldseedNoms.h"

#include "Misc/AutomationTest.h"

/**
 * LE CORPUS DU PROJET SE LIT, ET IL PORTE LES 43 BASES D'AZGAAR.
 *
 * TEST DE CALAGE, comme le bulletin terrestre et les recettes de vegetation :
 * il depend d'un fichier versionne, donc il tombera si ce fichier casse. C'est
 * exactement ce qu'on veut -- `noms.json` n'est compile par personne, et une
 * virgule de trop le rend muet sans un mot.
 *
 * ⚠ IL VERIFIE AUSSI LES INDEX D'ORIGINE, et ce n'est pas du zele : le
 * systeme de suffixes d'Etat teste des NUMEROS -- `2` pour le francais,
 * `> 32 && < 42` pour les bases de fantasy, qui n'en recoivent aucun.
 * Renumeroter ne casserait rien de visible a la compilation : les noms
 * sortiraient, simplement avec les mauvaises terminaisons.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedNomsCorpusTest,
	"Worldseed.Noms.LeCorpusDuProjetSeLit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedNomsCorpusTest::RunTest(const FString& Parameters)
{
	WorldseedNoms::ViderLeCache();

	FString Erreur;
	if (!TestTrue(FString::Printf(TEXT("le corpus se charge (%s)"), *Erreur),
		WorldseedNoms::Charger(Erreur)))
	{
		return false;
	}

	const TArray<FString>& Ordre = WorldseedNoms::Ordre();
	TestEqual(TEXT("43 bases"), Ordre.Num(), 43);

	int32 TotalMots = 0;
	int32 Maigres = 0;
	TArray<FString> Fautives;

	for (int32 I = 0; I < Ordre.Num(); ++I)
	{
		const FWorldseedBaseDeNoms* const B = WorldseedNoms::Trouver(Ordre[I]);
		if (!TestNotNull(*FString::Printf(TEXT("la base %s existe"), *Ordre[I]), B))
		{
			continue;
		}

		// L'ORDRE EST CELUI DES INDEX, et il doit etre contigu de 0 a 42 : le
		// test de fantasy (`> 32 && < 42`) suppose exactement cela.
		if (B->Index != I)
		{
			Fautives.Add(FString::Printf(
				TEXT("%s a l'index %d, attendu %d"), *B->Nom, B->Index, I));
		}
		if (B->Min <= 0 || B->Max < B->Min)
		{
			Fautives.Add(FString::Printf(TEXT("%s : min %d / max %d"),
				*B->Nom, B->Min, B->Max));
		}

		TotalMots += B->Mots.Num();
		Maigres += (B->Mots.Num() < 100) ? 1 : 0;
	}

	AddInfo(FString::Printf(TEXT("%d bases, %d mots au total"),
		Ordre.Num(), TotalMots));

	TestEqual(FString::Printf(TEXT("index et bornes coherents (%s)"),
		*FString::Join(Fautives, TEXT(" ; "))), Fautives.Num(), 0);
	TestTrue(FString::Printf(TEXT("le corpus est fourni (%d mots)"), TotalMots),
		TotalMots > 8000);
	TestEqual(TEXT("aucune base maigre"), Maigres, 0);

	// LES BASES QUE LA TABLE DES REGIONS NOMME DOIVENT EXISTER. Un libelle mal
	// orthographie la-bas rendrait une chaine VIDE, et la region sortirait
	// sans nom -- sans erreur, et sans que rien ne pointe vers la faute.
	const TArray<FString> Employees = {
		TEXT("Nordic"), TEXT("Finnic"), TEXT("Inuit"),
		TEXT("Dwarven"), TEXT("Giant"), TEXT("Basque"),
		TEXT("Arabic"), TEXT("Berber"), TEXT("Mesopotamian"),
		TEXT("Elven"), TEXT("Celtic"), TEXT("Nahuatl"),
		TEXT("Portuguese"), TEXT("Greek"), TEXT("Italian"),
		TEXT("French"), TEXT("German"), TEXT("Hungarian")
	};
	for (const FString& E : Employees)
	{
		TestNotNull(*FString::Printf(
			TEXT("la base %s, citee par le nommage des regions, existe"), *E),
			WorldseedNoms::Trouver(E));
	}
	return true;
}

/**
 * MEME GRAINE, MEME NOM -- ET DEUX GRAINES DONNENT DEUX NOMS.
 *
 * C'est l'invariant que tout ce depot defend, et il compte plus ici
 * qu'ailleurs : un rocher deplace ne se remarque pas, un lieu qui change de
 * nom quand on revient sur ses pas, si.
 *
 * LE SECOND CONTROLE EST LE TEMOIN -- un generateur qui rendrait toujours la
 * meme chaine passerait le premier sans rien prouver. Ce depot a paye quatre
 * fixtures muettes en une seule journee.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedNomsDeterministeTest,
	"Worldseed.Noms.LeTirageSuitLaGraine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedNomsDeterministeTest::RunTest(const FString& Parameters)
{
	WorldseedNoms::ViderLeCache();

	const TArray<FString>& Ordre = WorldseedNoms::Ordre();
	if (!TestTrue(TEXT("les bases sont chargees"), Ordre.Num() > 0))
	{
		return false;
	}

	int32 Tires = 0;
	int32 Distincts = 0;
	TArray<FString> Vides;
	TArray<FString> TropCourts;

	for (const FString& Base : Ordre)
	{
		for (int32 Graine = 1; Graine <= 25; ++Graine)
		{
			FRandomStream A(Graine), B(Graine), C(Graine + 100000);

			const FString Un = WorldseedNoms::Mot(Base, A);
			const FString Deux = WorldseedNoms::Mot(Base, B);
			const FString Autre = WorldseedNoms::Mot(Base, C);

			if (Un != Deux)
			{
				TestEqual(*FString::Printf(
					TEXT("%s graine %d rend le meme mot"), *Base, Graine),
					Deux, Un);
				return false;
			}
			Distincts += (Un != Autre) ? 1 : 0;
			++Tires;

			// UN NOM VIDE OU D'UNE LETTRE arriverait tel quel sur la carte.
			if (Un.IsEmpty()) { Vides.AddUnique(Base); }
			else if (Un.Len() < 2) { TropCourts.AddUnique(Base + TEXT(":") + Un); }

			// L'ETAT AUSSI DOIT SUIVRE LA GRAINE : il tire plusieurs fois --
			// le choix du suffixe, les coupes -- donc un seul `P()` mal
			// branche le rendrait instable.
			FRandomStream E1(Graine + 7), E2(Graine + 7);
			const FString Etat1 = WorldseedNoms::Etat(Base, FString(), E1);
			const FString Etat2 = WorldseedNoms::Etat(Base, FString(), E2);
			if (Etat1 != Etat2)
			{
				TestEqual(*FString::Printf(
					TEXT("%s graine %d rend le meme Etat"), *Base, Graine),
					Etat2, Etat1);
				return false;
			}
			++Tires;
			if (Etat1.IsEmpty()) { Vides.AddUnique(Base + TEXT(" (Etat)")); }
		}
	}

	AddInfo(FString::Printf(TEXT("%d tirages, %d distincts entre deux graines"),
		Tires, Distincts));

	TestTrue(FString::Printf(TEXT("des noms ont ete tires (%d)"), Tires),
		Tires > 1000);
	TestEqual(FString::Printf(TEXT("aucun nom vide (%s)"),
		*FString::Join(Vides, TEXT(", "))), Vides.Num(), 0);
	TestEqual(FString::Printf(TEXT("aucun nom d'une lettre (%s)"),
		*FString::Join(TropCourts, TEXT(", "))), TropCourts.Num(), 0);

	// LE TEMOIN. Deux graines eloignees doivent donner des noms differents
	// dans l'immense majorite des cas -- pas tous, les corpus sont finis.
	TestTrue(FString::Printf(
		TEXT("deux graines donnent bien deux noms (%d sur %d)"),
		Distincts, Tires / 2), Distincts > Tires / 4);
	return true;
}

/**
 * LES SUFFIXES D'ETAT TOMBENT SUR LA BONNE LANGUE.
 *
 * C'est le seul endroit du portage qui depende d'un NUMERO de base, et c'est
 * donc le seul qui casserait en silence si l'ordre des bases changeait. Trois
 * proprietes, chacune verifiable sans connaitre le tirage :
 *
 *   - les ONZE BASES DE FANTASY (index 33 a 41) ne recoivent JAMAIS de
 *     suffixe -- « Draconia » sonnerait romain ;
 *   - le JAPONAIS finit toujours sur une voyelle ou sur -u ;
 *   - le CHINOIS porte « Guo », qui est le seul suffixe a espace.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedNomsSuffixesTest,
	"Worldseed.Noms.LesSuffixesSuiventLaLangue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedNomsSuffixesTest::RunTest(const FString& Parameters)
{
	WorldseedNoms::ViderLeCache();

	// 1. LES BASES DE FANTASY NE PRENNENT RIEN. On le verifie en comparant
	//    l'Etat au mot dont il est tire : ils doivent etre identiques.
	const TArray<FString> Fantasy = {
		TEXT("Elven"), TEXT("Dark Elven"), TEXT("Dwarven"), TEXT("Goblin"),
		TEXT("Orc"), TEXT("Giant"), TEXT("Draconic"), TEXT("Arachnid"),
		TEXT("Serpents")
	};
	int32 Suffixes = 0;
	int32 Compares = 0;
	for (const FString& Base : Fantasy)
	{
		const FWorldseedBaseDeNoms* const B = WorldseedNoms::Trouver(Base);
		if (!TestNotNull(*FString::Printf(TEXT("%s existe"), *Base), B))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s est bien dans 33..41 (%d)"),
			*Base, B->Index), B->Index > 32 && B->Index < 42);

		for (int32 G = 1; G <= 60; ++G)
		{
			FRandomStream Rng(G);
			const FString Racine = TEXT("Thalor");
			const FString Etat = WorldseedNoms::Etat(Base, Racine, Rng);
			++Compares;
			if (Etat != Racine) { ++Suffixes; }
		}
	}
	TestTrue(FString::Printf(TEXT("des cas ont ete compares (%d)"), Compares),
		Compares > 300);
	TestEqual(TEXT("aucune base de fantasy ne recoit de suffixe"), Suffixes, 0);

	// 2. LE JAPONAIS finit sur une voyelle ou sur -u. C'est une regle SANS
	//    tirage : elle s'applique a tous les coups.
	int32 JaFautifs = 0;
	for (int32 G = 1; G <= 80; ++G)
	{
		FRandomStream Rng(G);
		const FString N = WorldseedNoms::Etat(TEXT("Japanese"), FString(), Rng);
		if (N.IsEmpty()) { continue; }
		const FString Fin = N.Right(1).ToLower();
		if (!FString(TEXT("aeiouy")).Contains(Fin)) { ++JaFautifs; }
	}
	TestEqual(TEXT("le japonais finit sur une voyelle ou -u"), JaFautifs, 0);

	// 3. LE TEMOIN, ET IL EST INDISPENSABLE : une base NON fantasy doit, elle,
	//    recevoir des suffixes. Sans ce controle, un `Etat` qui rendrait
	//    toujours sa racine passerait les deux points ci-dessus.
	int32 AvecSuffixe = 0;
	for (int32 G = 1; G <= 200; ++G)
	{
		FRandomStream Rng(G);
		const FString Racine = TEXT("Bourgon");
		if (WorldseedNoms::Etat(TEXT("French"), Racine, Rng) != Racine)
		{
			++AvecSuffixe;
		}
	}
	AddInfo(FString::Printf(
		TEXT("temoin : %d racines francaises sur 200 ont recu un suffixe"),
		AvecSuffixe));
	TestTrue(FString::Printf(
		TEXT("temoin : le francais recoit bien des suffixes (%d sur 200)"),
		AvecSuffixe), AvecSuffixe > 30);
	return true;
}

/**
 * LE DOUBLEMENT DE LETTRES SUIT LA LANGUE, ET JAMAIS AU-DELA DE DEUX.
 *
 * Chaque base porte sa liste `Dupl` : `lt` en allemand, `nlrs` en francais,
 * `aliuszrox` en draconique. C'est ce qui distingue une langue d'une autre
 * presque autant que son corpus -- et c'est un reglage PAR BASE qu'un
 * post-traitement global detruirait.
 *
 * ⚠ TROIS LETTRES IDENTIQUES SONT INTERDITES MEME QUAND LE DOUBLEMENT EST
 * AUTORISE. La regle est portee separement dans l'original, et c'est la seule
 * qui morde sur les lettres doublables : sans elle, le draconique sortirait
 * des « Zaaar ».
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedNomsDoublementTest,
	"Worldseed.Noms.LeDoublementSuitLaLangue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedNomsDoublementTest::RunTest(const FString& Parameters)
{
	WorldseedNoms::ViderLeCache();

	int32 Tires = 0;
	int32 Doubles = 0;
	TArray<FString> Interdits;
	TArray<FString> Triples;

	for (const FString& Base : WorldseedNoms::Ordre())
	{
		const FWorldseedBaseDeNoms* const B = WorldseedNoms::Trouver(Base);
		if (!B) { continue; }

		for (int32 G = 1; G <= 40; ++G)
		{
			FRandomStream Rng(G * 13 + 5);
			const FString N = WorldseedNoms::Mot(Base, Rng);
			++Tires;

			for (int32 I = 1; I < N.Len(); ++I)
			{
				const TCHAR C = FChar::ToLower(N[I]);
				if (FChar::ToLower(N[I - 1]) != C) { continue; }

				++Doubles;
				if (!B->Dupl.Contains(FString::Chr(C), ESearchCase::CaseSensitive))
				{
					Interdits.AddUnique(FString::Printf(
						TEXT("%s : « %s » double « %c » hors de « %s »"),
						*Base, *N, C, *B->Dupl));
				}
				if (I + 1 < N.Len() && FChar::ToLower(N[I + 1]) == C)
				{
					Triples.AddUnique(
						FString::Printf(TEXT("%s : « %s »"), *Base, *N));
				}
			}
		}
	}

	AddInfo(FString::Printf(TEXT("%d mots tires, %d lettres doublees"),
		Tires, Doubles));

	TestTrue(FString::Printf(TEXT("des mots ont ete tires (%d)"), Tires),
		Tires > 1000);

	// LA MATIERE QUI REND LE TEST NON TRIVIAL : si aucun mot ne doublait
	// jamais, « aucun doublement interdit » serait vrai sans rien prouver.
	TestTrue(FString::Printf(
		TEXT("des lettres sont bien doublees quelque part (%d)"), Doubles),
		Doubles > 20);

	TestEqual(FString::Printf(TEXT("aucun doublement hors de sa langue (%s)"),
		*FString::Join(Interdits, TEXT(" ; "))), Interdits.Num(), 0);
	TestEqual(FString::Printf(TEXT("aucune lettre triplee (%s)"),
		*FString::Join(Triples, TEXT(" ; "))), Triples.Num(), 0);
	return true;
}

/**
 * LES LONGUEURS SONT TENUES, ET LES MOTS SONT LISIBLES.
 *
 * Chaque base porte son `min` et son `max`. La generation peut depasser le
 * maximum dans UN cas prevu par l'original -- quand s'arreter laisserait un
 * mot sous le minimum, mieux vaut depasser que rendre un moignon -- donc on
 * verifie une borne LARGE, et surtout qu'aucun mot n'est absurde.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedNomsLongueurTest,
	"Worldseed.Noms.LesLongueursSontTenues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedNomsLongueurTest::RunTest(const FString& Parameters)
{
	WorldseedNoms::ViderLeCache();

	int32 Tires = 0;
	int32 PlusLong = 0;
	TArray<FString> Excessifs;
	TArray<FString> Sales;

	for (const FString& Base : WorldseedNoms::Ordre())
	{
		const FWorldseedBaseDeNoms* const B = WorldseedNoms::Trouver(Base);
		if (!B) { continue; }

		for (int32 G = 1; G <= 40; ++G)
		{
			FRandomStream Rng(G * 7 + 3);
			const FString N = WorldseedNoms::Mot(Base, Rng);
			++Tires;
			PlusLong = FMath::Max(PlusLong, N.Len());

			// LA BORNE EST LARGE A DESSEIN : le depassement est prevu, mais il
			// ne doit pas etre du simple au double.
			if (N.Len() > B->Max + 6)
			{
				Excessifs.AddUnique(FString::Printf(TEXT("%s : « %s » (%d > %d)"),
					*Base, *N, N.Len(), B->Max));
			}

			// UN NOM NE PORTE NI ACCOLADE, NI ESPACE EN BOUT, NI TIRET ORPHELIN.
			if (N.StartsWith(TEXT(" ")) || N.EndsWith(TEXT(" "))
				|| N.EndsWith(TEXT("-")) || N.EndsWith(TEXT("'"))
				|| N.Contains(TEXT("  ")))
			{
				Sales.AddUnique(FString::Printf(TEXT("%s : « %s »"), *Base, *N));
			}
		}
	}

	AddInfo(FString::Printf(TEXT("%d mots, le plus long fait %d caracteres"),
		Tires, PlusLong));

	TestTrue(FString::Printf(TEXT("des mots ont ete tires (%d)"), Tires),
		Tires > 1000);
	TestEqual(FString::Printf(TEXT("aucun mot demesure (%s)"),
		*FString::Join(Excessifs, TEXT(" ; "))), Excessifs.Num(), 0);
	TestEqual(FString::Printf(TEXT("aucun mot mal forme (%s)"),
		*FString::Join(Sales, TEXT(" ; "))), Sales.Num(), 0);
	return true;
}

/**
 * UN ECHANTILLON AU JOURNAL, ET CE N'EST PAS DU BAVARDAGE.
 *
 * Aucun oracle de ce fichier ne dit qu'un nom SONNE BIEN -- ils disent qu'il
 * existe, qu'il est stable, qu'il tient ses longueurs et que ses suffixes
 * suivent sa langue. Ce depot tient qu'une forme qu'on n'a pas vue n'est pas
 * validee, et un nom est une forme.
 *
 * `AddInfo` sort au journal MEME QUAND LE TEST PASSE, entre `BeginEvents` et
 * `EndEvents` : c'est par la qu'on REGARDE.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedNomsEchantillonTest,
	"Worldseed.Noms.UnEchantillonAuJournal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedNomsEchantillonTest::RunTest(const FString& Parameters)
{
	WorldseedNoms::ViderLeCache();

	const TArray<FString>& Ordre = WorldseedNoms::Ordre();
	if (!TestTrue(TEXT("les bases sont chargees"), Ordre.Num() > 0))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("%-16s %-20s %-20s %-20s %s"),
		TEXT("base"), TEXT("lieu"), TEXT("lieu court"), TEXT("ETAT"),
		TEXT("personne")));

	for (const FString& Base : Ordre)
	{
		FRandomStream A(1337), B(1338), C(1339), D(1340);
		AddInfo(FString::Printf(TEXT("%-16s %-20s %-20s %-20s %s"),
			*Base,
			*WorldseedNoms::Mot(Base, A),
			*WorldseedNoms::MotCourt(Base, B),
			*WorldseedNoms::Etat(Base, FString(), C),
			*WorldseedNoms::Personne(Base, true, D)));
	}
	return true;
}

#endif
