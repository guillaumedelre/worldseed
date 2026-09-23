// Worldseed - l'eau : une epaisseur d'ocean JAMAIS nulle, et une fenetre que
// le moteur n'a pas a diviser. Les deux pieges les plus silencieux du depot.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedWaterBodies.h"

#include "Misc/AutomationTest.h"

/**
 * L'OCEAN A TOUJOURS UNE EPAISSEUR, ET C'EST CE QUI LE REND VISIBLE.
 *
 * LE PIEGE LE PLUS SILENCIEUX DE CE DEPOT. La `WaterZone` agrege les bornes en
 * Z de TOUS les corps d'eau en UN intervalle, dans lequel sa texture
 * d'information normalise chaque hauteur. Un ocean d'epaisseur nulle donne
 * `[0 .. 0]` -- et l'eau cesse de se dessiner, sans erreur, sans
 * avertissement, sans rien. On cherche alors un defaut de materiau, de
 * rendu, de plugin ; la cause est une division par un intervalle vide.
 *
 * ET DEPUIS LE RETRAIT DE L'HYDROLOGIE, L'OCEAN EST LE SEUL CORPS D'EAU DU
 * MONDE : cette chaine n'est donc plus defensive, elle est PORTANTE. Le
 * controle en jeu est la ligne `eau : ... hauteurs d'eau [%.0f .. %.0f] m`,
 * qui doit valoir [-371 .. 371] et jamais [0 .. 0] ; ce test le garantit en
 * amont, pour TOUT fond marin.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestEauProfondeur,
	"Worldseed.Eau.LOceanATOUJOURSUneEpaisseur",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestEauProfondeur::RunTest(const FString& Parameters)
{
	// LES CAS DURS D'ABORD, et ils sont tous realistes : un monde sans fond
	// marin sous le niveau de la mer, un fond a zero, et meme un fond POSITIF
	// -- que la passe littorale ou un recalibrage pourraient produire.
	const float FondsDurs[] = { 0.0f, -0.0f, 1.0f, 50.0f, 1e-6f };

	for (const float Fond : FondsDurs)
	{
		const float D = WorldseedWaterBodies::ProfondeurDOceanCm(Fond, 1.0f);
		AddInfo(FString::Printf(TEXT("fond marin %.6f m -> epaisseur %.0f cm"),
			Fond, D));
		TestTrue(FString::Printf(
			TEXT("fond a %.6f m : l'ocean garde une epaisseur STRICTEMENT positive"),
			Fond), D > 0.0f);
	}

	// --- et sur toute la plage des mondes reels -----------------------------
	float PlusMince = BIG_NUMBER;
	float PlusEpais = 0.0f;
	for (float Fond = 200.0f; Fond >= -3000.0f; Fond -= 7.0f)
	{
		for (const float Exageration : { 0.25f, 1.0f, 4.0f })
		{
			const float D = WorldseedWaterBodies::ProfondeurDOceanCm(Fond, Exageration);
			PlusMince = FMath::Min(PlusMince, D);
			PlusEpais = FMath::Max(PlusEpais, D);

			if (FMath::IsNaN(D) || !FMath::IsFinite(D))
			{
				AddError(FString::Printf(
					TEXT("epaisseur non finie a fond %.0f m, exageration %.2f"),
					Fond, Exageration));
				return false;
			}
		}
	}

	AddInfo(FString::Printf(
		TEXT("sur 458 fonds x 3 exagerations : epaisseur de %.0f a %.0f cm"),
		PlusMince, PlusEpais));

	TestTrue(TEXT("jamais nulle, quel que soit le fond ou l'exageration"),
		PlusMince > 0.0f);

	// LE TEMOIN QUI DONNE SON SENS AU BORNAGE : si l'epaisseur etait CONSTANTE,
	// « jamais nulle » serait vrai trivialement et le fond marin ne servirait a
	// rien. Il faut qu'elle SUIVE le monde entre ses deux bornes.
	TestTrue(TEXT("TEMOIN : l'epaisseur suit reellement le fond marin"),
		PlusEpais > PlusMince * 2.0f);

	// --- et elle est MONOTONE : plus le fond est bas, plus l'ocean est epais -
	//
	// Un signe inverse donnerait un ocean mince au-dessus des fosses et epais
	// au-dessus des hauts-fonds : l'intervalle de la zone resterait non nul,
	// donc le controle `[0 .. 0]` passerait, et rien d'autre ne le dirait.
	int32 Reculs = 0;
	float Precedente = 0.0f;
	for (float Fond = 0.0f; Fond >= -2000.0f; Fond -= 10.0f)
	{
		const float D = WorldseedWaterBodies::ProfondeurDOceanCm(Fond, 1.0f);
		if (D < Precedente - 1.0e-3f) { ++Reculs; }
		Precedente = D;
	}
	TestEqual(TEXT("un fond plus bas ne rend jamais un ocean plus mince"),
		Reculs, 0);

	return true;
}

/**
 * LA FENETRE NE DEPASSE JAMAIS CE QUE LE PLAFOND DE TUILES AUTORISE.
 *
 * LE MOTEUR NE REFUSE PAS, IL DIVISE, et c'est ce qui rend la borne traitre.
 * `FWaterZoneActor` fait `RoundUpToPowerOfTwo(demi-etendue / 24 m)` puis
 * plafonne a `r.Water.WaterMesh.MaxDimensionInTiles` ; au-dela, la taille de
 * tuile est DIVISEE et le rivage devient deux fois plus grossier sans qu'une
 * seule ligne ne le dise. Un essai fait la sans le savoir conclurait a
 * l'envers -- exactement ce qui est arrive avec la tuile de 24 m devenue 96.
 *
 * ATTENTION AU NOM, ET LE DEPOT L'A PAYE : le message d'avertissement du
 * moteur cite `MaxWidthInTiles`, qui N'EXISTE PAS. Ce nom avait ete recopie
 * dans `DefaultEngine.ini`, ou il est reste une ligne morte pendant des
 * semaines -- doublement morte, puisque la vraie variable n'y etait pas non
 * plus. Un message du moteur n'est pas une source pour un nom de variable.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestEauFenetre,
	"Worldseed.Eau.LaFenetreTientDansLePlafondDeTuiles",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestEauFenetre::RunTest(const FString& Parameters)
{
	// LES DEUX VALEURS QUI COMPTENT, ET ELLES SONT SOURCEES : 256 est le defaut
	// du moteur, 512 ce que le projet pose. Les chiffres attendus sont ceux que
	// le commentaire du code annonce.
	const float A256 = WorldseedWaterBodies::FenetreMaximaleKm(256);
	const float A512 = WorldseedWaterBodies::FenetreMaximaleKm(512);

	AddInfo(FString::Printf(
		TEXT("plafond 256 -> %.3f km ; plafond 512 -> %.3f km"), A256, A512));

	TestTrue(TEXT("256 tuiles rendent 12,288 km"),
		FMath::IsNearlyEqual(A256, 12.288f, 1.0e-3f));
	TestTrue(TEXT("512 tuiles rendent 24,576 km"),
		FMath::IsNearlyEqual(A512, 24.576f, 1.0e-3f));

	// --- elle croit avec le plafond, et jamais l'inverse --------------------
	int32 Reculs = 0;
	float Precedente = 0.0f;
	for (int32 P = 1; P <= 2048; P *= 2)
	{
		const float Km = WorldseedWaterBodies::FenetreMaximaleKm(P);
		if (Km < Precedente) { ++Reculs; }
		Precedente = Km;
	}
	TestEqual(TEXT("un plafond plus haut n'autorise jamais une fenetre plus petite"),
		Reculs, 0);

	// --- un plafond absurde ne rend pas une fenetre absurde -----------------
	TestTrue(TEXT("un plafond nul ou negatif rend une fenetre positive"),
		WorldseedWaterBodies::FenetreMaximaleKm(0) > 0.0f
			&& WorldseedWaterBodies::FenetreMaximaleKm(-5) > 0.0f);

	// --- LE CONTROLE QUI COMPTE : la fenetre du jeu tient dans le plafond ----
	//
	// C'est la seule assertion qui protege reellement le rivage. Le projet pose
	// le plafond a 512 et demande 24,576 km ; si l'un des deux bougeait sans
	// l'autre, la tuile serait divisee EN SILENCE.
	constexpr float FenetreDuJeuKm = 24.576f;
	constexpr int32 PlafondDuJeu = 512;

	AddInfo(FString::Printf(
		TEXT("le jeu demande %.3f km pour un plafond de %d tuiles, soit %.3f km autorises"),
		FenetreDuJeuKm, PlafondDuJeu,
		WorldseedWaterBodies::FenetreMaximaleKm(PlafondDuJeu)));

	TestTrue(TEXT("la fenetre du jeu tient dans son plafond de tuiles"),
		FenetreDuJeuKm <= WorldseedWaterBodies::FenetreMaximaleKm(PlafondDuJeu) + 1.0e-3f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
