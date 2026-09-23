// Worldseed - la police d'icones : ses codepoints existent-ils vraiment ?

#if WITH_DEV_AUTOMATION_TESTS

#include "Procedural/WorldseedIcones.h"

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace WorldseedTestIcones
{
	/**
	 * Le catalogue livre avec la police : une ligne par icone, `nom codepoint`.
	 *
	 * ON LE RELIT PLUTOT QUE DE LUI FAIRE CONFIANCE. Un codepoint se recopie a
	 * la main depuis un fichier de 4284 lignes ; une faute d'un chiffre donne un
	 * AUTRE glyphe, parfaitement dessine, et rien ne la signale -- on met alors
	 * la faute sur le rendu, sur la taille, sur le DPI. C'est exactement la
	 * famille de defaut que ce depot paie le plus cher.
	 */
	bool LireCatalogue(TMap<FString, uint32>& Out)
	{
		TArray<FString> Lignes;
		if (!FFileHelper::LoadFileToStringArray(Lignes,
			*WorldseedIcones::CheminCodepoints()))
		{
			return false;
		}

		for (const FString& Ligne : Lignes)
		{
			FString Nom, Hex;
			if (!Ligne.Split(TEXT(" "), &Nom, &Hex))
			{
				continue;
			}

			Out.Add(Nom.TrimStartAndEnd(),
				static_cast<uint32>(FParse::HexNumber64(*Hex.TrimStartAndEnd())));
		}

		return Out.Num() > 0;
	}
}


/**
 * CHAQUE CODEPOINT QUE NOUS EMPLOYONS EXISTE DANS LA POLICE LIVREE.
 *
 * C'est un test de DONNEE, donc il depend d'un fichier -- comme le bulletin des
 * climats reels, et pour la meme raison assumee : la question qu'il pose est
 * precisement « la donnee livree dit-elle ce que le code croit ». Si Google
 * renomme une icone ou deplace un codepoint a la version suivante, c'est ici
 * qu'on doit l'apprendre, et non sur une capture d'ecran.
 *
 * LE TEMOIN EST LE NOM INVENTE. Sans lui, un lecteur de catalogue casse qui
 * rendrait « tout existe » passerait toutes les affirmations -- c'est la
 * fixture muette que ce depot a payee quatre fois en une journee.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestIconesCodepoints,
	"Worldseed.Icones.LesCodepointsExistentDansLaPolice",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestIconesCodepoints::RunTest(const FString& Parameters)
{
	TMap<FString, uint32> Catalogue;
	if (!WorldseedTestIcones::LireCatalogue(Catalogue))
	{
		AddError(FString::Printf(
			TEXT("catalogue illisible : %s -- voir Content/Worldseed/Fonts/PROVENANCE.md"),
			*WorldseedIcones::CheminCodepoints()));
		return false;
	}

	// Le catalogue livre en portait 4284 le 23 septembre 2026. On ne fige pas ce
	// nombre -- Google en ajoute -- mais un catalogue qui aurait fondu a trois
	// entrees signalerait un lecteur casse, pas une police allegee.
	TestTrue(TEXT("le catalogue porte des milliers d'icones"), Catalogue.Num() > 1000);

	for (const WorldseedIcones::FEntree& E : WorldseedIcones::Catalogue())
	{
		const uint32* const Attendu = Catalogue.Find(E.Nom);
		if (!Attendu)
		{
			AddError(FString::Printf(
				TEXT("l'icone '%s' n'existe pas dans la police livree"), E.Nom));
			continue;
		}

		TestEqual(FString::Printf(TEXT("codepoint de '%s'"), E.Nom),
			static_cast<int64>(E.Codepoint), static_cast<int64>(*Attendu));
	}

	// TEMOIN : un nom qui n'existe pas doit etre RAPPORTE comme tel. Sans cette
	// ligne, un lecteur qui rendrait n'importe quoi passerait la boucle.
	TestFalse(TEXT("TEMOIN : un nom invente est bien absent"),
		Catalogue.Contains(TEXT("ceci_nest_pas_une_icone")));

	// SECOND TEMOIN, sur la VALEUR et non sur la presence : deux icones du
	// catalogue doivent porter des codepoints differents. Un lecteur qui
	// rendrait zero partout passerait tout ce qui precede.
	const uint32* const A = Catalogue.Find(TEXT("place"));
	const uint32* const B = Catalogue.Find(TEXT("map"));
	if (A && B)
	{
		TestNotEqual(TEXT("TEMOIN : deux icones ont deux codepoints"),
			static_cast<int64>(*A), static_cast<int64>(*B));
	}

	return true;
}


/**
 * LE FICHIER DE POLICE EST LA, ET C'EST UNE VRAIE POLICE.
 *
 * Deux choses seulement, et la seconde est celle qui compte : un `.ttf` absent
 * donne un ecran de carres vides, mais un `.ttf` remplace par une page HTML
 * d'erreur -- ce que rend un telechargement qui a echoue en silence -- donne
 * exactement la meme chose. La signature departage les deux.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestIconesFichier,
	"Worldseed.Icones.LaPoliceEstUneVraiePolice",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestIconesFichier::RunTest(const FString& Parameters)
{
	const FString Chemin = WorldseedIcones::CheminPolice();

	const int64 Taille = IFileManager::Get().FileSize(*Chemin);
	if (Taille <= 0)
	{
		AddError(FString::Printf(TEXT("police introuvable : %s"), *Chemin));
		return false;
	}

	TestTrue(TEXT("la police pese plus de cent kilo-octets"), Taille > 100 * 1024);

	TArray<uint8> Entete;
	if (!FFileHelper::LoadFileToArray(Entete, *Chemin) || Entete.Num() < 4)
	{
		AddError(TEXT("police illisible"));
		return false;
	}

	// SIGNATURE TrueType : 0x00010000. Les autres formes valides d'un fichier de
	// police sont 'OTTO' (CFF) et 'true' ; on n'accepte que celle qu'on livre.
	const bool bTrueType = Entete[0] == 0x00 && Entete[1] == 0x01
		&& Entete[2] == 0x00 && Entete[3] == 0x00;
	TestTrue(TEXT("la signature est celle d'une police TrueType"), bTrueType);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
