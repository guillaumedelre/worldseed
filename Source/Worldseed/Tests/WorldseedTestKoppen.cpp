// Worldseed - le classificateur de Koppen se reconnait-il lui-meme ?

#if WITH_DEV_AUTOMATION_TESTS

#include "Procedural/WorldseedClimatsReels.h"
#include "Procedural/WorldseedKoppen.h"

#include "Misc/AutomationTest.h"

/**
 * CE QUE CES ORACLES GARDENT, ET POURQUOI LE TAUX REMPLACE L'ECART.
 *
 * Tant que la meteo se DEDUISAIT d'une courbe, sa qualite se lisait en points
 * d'ecart a la couverture reelle. En classant, elle se lit autrement : une
 * cellule bien classee recoit la meteo d'une vraie station du meme climat, une
 * cellule mal classee celle d'un autre climat -- et cela coute 18,7 points en
 * moyenne, jusqu'a 66 entre une foret tropicale et un desert chaud. La qualite
 * tient donc a un TAUX DE CLASSEMENT, et c'est lui qu'on mesure.
 *
 * LES VINGT-TROIS RELEVES PORTENT LEUR PROPRE REPONSE : les prereglages d'Ultra
 * Dynamic Sky suivent la nomenclature de Koppen, « Mediterranean_Hot_Summer »
 * EST un Csa. La verite terrain est donc gratuite, et le test consiste a
 * demander au classificateur de retrouver ce que le nom annonce.
 */
namespace
{
	/** Ce que le classificateur rend pour un releve, depuis ses saisons. */
	EWorldseedKoppen Classer(const FWorldseedReleveReel& R, float Pointe)
	{
		FWorldseedKoppenEntree E;
		E.PrecipAnnuelMm = R.PluieMm;

		// LES SAISONS DU RELEVE, PAS UNE RECONSTRUCTION. Le test doit juger le
		// CLASSIFICATEUR, et le nourrir de saisons reconstruites melangerait
		// son erreur a celle de la reconstruction -- deux populations dans une
		// seule mesure, ce que ce depot s'interdit.
		float Min = TNumericLimits<float>::Max();
		float Max = TNumericLimits<float>::Lowest();
		for (int32 S = 0; S < 4; ++S)
		{
			E.PluieSaisonMm[S] = R.PluieSaisonMm[S] + R.NeigeSaisonMm[S];
		}
		// L'amplitude du releve donne les deux extremes autour de la moyenne.
		Min = R.TmoyC - 0.5f * R.AmplitudeC;
		Max = R.TmoyC + 0.5f * R.AmplitudeC;
		E.TFroidC = Min;
		E.TChaudC = Max;
		return WorldseedKoppen::Classer(E, Pointe);
	}

	/** Le groupe principal : A, B, C, D ou E. */
	TCHAR Groupe(EWorldseedKoppen K)
	{
		const TCHAR* const N = WorldseedKoppen::Nom(K);
		return N[0];
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedKoppenReconnaitLesReleves,
	"Worldseed.Koppen.ReconnaitLesReleves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedKoppenReconnaitLesReleves::RunTest(const FString&)
{
	TArray<FWorldseedReleveReel> Releves;
	FString Erreur;
	if (!TestTrue(TEXT("les releves se chargent"),
			WorldseedClimatsReels::Charger(Releves, Erreur)))
	{
		AddError(Erreur);
		return false;
	}
	if (!TestEqual(TEXT("vingt-trois releves"), Releves.Num(), 23))
	{
		return false;
	}

	// LA POINTE MENSUELLE EST BALAYEE, PAS SUPPOSEE. Koppen se definit sur les
	// mois et nous n'avons que des saisons, dont la moyenne adoucit les
	// extremes : le bon ecart se mesure, il ne se devine pas.
	float MeilleurePointe = 0.0f;
	int32 MeilleurJuste = -1;
	for (float Pointe = 0.0f; Pointe <= 6.01f; Pointe += 0.5f)
	{
		int32 Juste = 0;
		for (const FWorldseedReleveReel& R : Releves)
		{
			if (Classer(R, Pointe) == R.Koppen) { ++Juste; }
		}
		if (Juste > MeilleurJuste) { MeilleurJuste = Juste; MeilleurePointe = Pointe; }
	}

	int32 JusteGroupe = 0;
	FString Rates;
	for (const FWorldseedReleveReel& R : Releves)
	{
		const EWorldseedKoppen Obtenu = Classer(R, MeilleurePointe);
		if (Groupe(Obtenu) == Groupe(R.Koppen)) { ++JusteGroupe; }
		if (Obtenu != R.Koppen)
		{
			Rates += FString::Printf(TEXT("\n    %-33s attendu %-4s obtenu %s"),
				*R.Cle, WorldseedKoppen::Nom(R.Koppen), WorldseedKoppen::Nom(Obtenu));
		}
	}

	AddInfo(FString::Printf(
		TEXT("pointe mensuelle %.1f C -- classe exacte %d/23, groupe principal %d/23%s"),
		MeilleurePointe, MeilleurJuste, JusteGroupe, *Rates));

	// LE GROUPE PRINCIPAL D'ABORD : confondre un Cfa et un Cfb coute peu -- ce
	// sont deux temperes sans saison seche -- quand confondre un B et un A
	// donne un desert a la place d'une foret tropicale, 66 points d'ecart.
	TestTrue(TEXT("le groupe principal est juste au moins 18 fois sur 23"),
		JusteGroupe >= 18);
	TestTrue(TEXT("la classe exacte est juste au moins 14 fois sur 23"),
		MeilleurJuste >= 14);

	return true;
}


/**
 * LE CLASSIFICATEUR DISTINGUE, et on le verifie sur des cas FABRIQUES.
 *
 * Le test du dessus mesure un taux sur des donnees reelles ; celui-ci verifie
 * que les frontieres sont au bon endroit, sur des climats construits pour la
 * question. Une fonction qui rendrait toujours la meme classe passerait le
 * premier avec un taux mediocre sans qu'on sache pourquoi.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedKoppenDistingue,
	"Worldseed.Koppen.Distingue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedKoppenDistingue::RunTest(const FString&)
{
	auto Faire = [](float TFroid, float TChaud, float Annuel,
		float Hiver, float Printemps, float Ete, float Automne)
	{
		FWorldseedKoppenEntree E;
		E.TFroidC = TFroid;
		E.TChaudC = TChaud;
		E.PrecipAnnuelMm = Annuel;
		E.PluieSaisonMm[0] = Hiver;
		E.PluieSaisonMm[1] = Printemps;
		E.PluieSaisonMm[2] = Ete;
		E.PluieSaisonMm[3] = Automne;
		return WorldseedKoppen::Classer(E, 0.0f);
	};

	// Un pole : le mois le plus chaud decide, et lui seul.
	TestEqual(TEXT("glace permanente"),
		Faire(-40.0f, -5.0f, 150.0f, 12.0f, 12.0f, 13.0f, 13.0f), EWorldseedKoppen::EF);
	TestEqual(TEXT("toundra"),
		Faire(-25.0f, 6.0f, 250.0f, 20.0f, 20.0f, 21.0f, 21.0f), EWorldseedKoppen::ET);

	// L'ARIDE PASSE AVANT LA TEMPERATURE, et c'est l'ordre de Koppen : un
	// desert chaud est un desert avant d'etre un tropical.
	TestEqual(TEXT("desert chaud"),
		Faire(18.0f, 34.0f, 60.0f, 2.0f, 1.0f, 1.0f, 1.0f), EWorldseedKoppen::BWh);
	TestEqual(TEXT("desert froid"),
		Faire(-5.0f, 25.0f, 90.0f, 3.0f, 2.0f, 1.0f, 1.0f), EWorldseedKoppen::BWk);

	// Une foret tropicale : aucun mois sous dix-huit, aucun mois sec.
	TestEqual(TEXT("foret tropicale humide"),
		Faire(24.0f, 28.0f, 2400.0f, 180.0f, 200.0f, 220.0f, 200.0f),
		EWorldseedKoppen::Af);

	// Un mediterraneen : ete sec, hiver doux et arrose.
	TestEqual(TEXT("mediterraneen a ete chaud"),
		Faire(8.0f, 27.0f, 500.0f, 80.0f, 40.0f, 5.0f, 45.0f), EWorldseedKoppen::Csa);

	// Un continental : l'hiver franchit moins trois.
	TestEqual(TEXT("continental a ete chaud"),
		Faire(-8.0f, 24.0f, 900.0f, 60.0f, 80.0f, 90.0f, 70.0f), EWorldseedKoppen::Dfa);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
