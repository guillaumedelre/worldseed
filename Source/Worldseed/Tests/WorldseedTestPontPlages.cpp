// Worldseed - le pont sait-il relire une plage, et appeler sans se faire mal ?

#if WITH_DEV_AUTOMATION_TESTS

#include "Procedural/WorldseedUdsBridge.h"

#include "Components/SceneComponent.h"
#include "Particles/ParticleSystem.h"
#include "Particles/Modules/Location/ParticleModulePivotOffset.h"
#include "Misc/AutomationTest.h"

/**
 * CE QUE CES ORACLES PROTEGENT, et pourquoi ils existent depuis aujourd'hui.
 *
 * Deux briques du pont sont nees le 30 septembre 2026 d'une mesure : nos quatre
 * plages de temperature saisonnieres etaient POSEES sur l'acteur meteo -- relues
 * identiques colonne par colonne -- et `UDW_Temperature_Manager` tournait sur
 * celles du pack. A 77,5 degres de latitude il annoncait +11,6 C quand notre
 * climat disait -12,1 C. Vingt-quatre degres, invisibles parce que rien ne
 * relisait la plage VIVANTE et que rien ne demandait le recalcul.
 *
 *     LirePlageDe             la relecture qui manquait
 *     AppelerSansArgumentSur  le recalcul, sur un COMPOSANT et non un acteur
 *
 * LES DEUX FAMILLES DE DEFAUT QU'ILS GARDENT, et aucune ne se signale :
 *
 * 1. LE SILENCE. Une lecture par reflexion sur une propriete absente ne rend
 *    rien et ne journalise rien. Une plage restee a (0,0) se lit comme une
 *    mesure -- c'est exactement ce que la premiere version du releve de
 *    temperature a fait, en convertissant un zero en -17,8 C et en le mettant
 *    en titre.
 * 2. LA CORRUPTION. `ProcessEvent` avec une pile mal formee casse la memoire.
 *    La garde est `NumParms != 0`, et elle doit REFUSER plutot que deviner :
 *    `Activate` prend un booleen, `Deactivate` non, et la difference se joue
 *    sur un octet.
 *
 * POURQUOI DES FIXTURES DU MOTEUR. La cible reelle est le contenu d'un pack
 * payant, absent du depot : un oracle qui en dependrait ne tournerait pas sur un
 * clone. Trois formes du moteur suffisent, et se construisent sans monde :
 *
 *     UParticleModulePivotOffset::PivotOffset  FVector2D     <- la bonne forme
 *     UActorComponent::ComponentTags        TArray<FName>    <- pas une plage
 *     UActorComponent::Deactivate()         zero parametre   <- appelable
 *     UActorComponent::Activate(bool)       un parametre     <- a REFUSER
 *
 * ET L'ON MESURE UN EFFET, PAS UN RETOUR. `Deactivate` appelee par reflexion
 * doit rendre `IsActive()` faux : un appel qui rendrait vrai sans rien faire
 * passerait un controle de valeur de retour. C'est la regle du depot -- un
 * drapeau relu ne prouve rien.
 *
 * CE QU'ILS NE PEUVENT PAS GARDER, et il faut le dire : que `Update Temperature
 * Range` soit toujours le nom du recalcul chez UDS, et qu'il vive toujours sur
 * un composant dont le nom de classe contient « Temperature ». Aucun test hors
 * contenu ne le saura ; seul le releve sur l'asset le dit, et il est dans
 * `Docs/registre/climat-ciel.md` au 30 septembre 2026.
 */
namespace
{
	// PREFIXES A DESSEIN : le build unifie fusionne les namespaces anonymes, et
	// ce depot a deja casse trois fois sur des noms partages entre deux fichiers
	// de tests. `WorldseedTestPontTableaux.cpp` porte deja `NomBon` et
	// `NomAbsent`.
	const FName PlageNomBon(TEXT("PivotOffset"));
	const FName PlageNomPasUnePlage(TEXT("ComponentTags"));
	const FName PlageNomAbsent(TEXT("CeNomDePlageNExistePas"));

	const FName AppelSansParam(TEXT("Deactivate"));
	const FName AppelAvecParam(TEXT("Activate"));
	const FName AppelAbsent(TEXT("CetteFonctionNExistePas"));

	/**
	 * LA FIXTURE EXIGE SON PROPRIETAIRE, et l'oublier n'echoue pas franchement.
	 *
	 * `UParticleModulePivotOffset` declare `ClassWithin = UParticleSystem` : un
	 * `NewObject` dans le paquet transitoire leve un `ensure` du moteur --
	 * « created in invalid Outer » -- qui fait tomber l'oracle sur une pile
	 * d'appel, sans rapport avec ce qu'il mesure. Le premier des deux tests
	 * passait quand meme, ce qui est le pire des deux mondes : la meme faute
	 * rendait un vert et un rouge.
	 */
	UParticleModulePivotOffset* FabriquerLeModule()
	{
		UParticleSystem* const Proprietaire = NewObject<UParticleSystem>();
		return NewObject<UParticleModulePivotOffset>(Proprietaire);
	}
}

// --- LA RELECTURE D'UNE PLAGE -----------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedPontPlageRelitCeQuiEstPose,
	"Worldseed.Pont.PlageRelitCeQuiEstPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedPontPlageRelitCeQuiEstPose::RunTest(const FString&)
{
	UParticleModulePivotOffset* const Cible = FabriquerLeModule();
	if (!TestNotNull(TEXT("la fixture module de particules existe"), Cible))
	{
		return false;
	}

	// UNE VALEUR QUI NE PEUT PAS ETRE UN DEFAUT. Les deux bornes sont
	// differentes l'une de l'autre et de zero : une lecture qui rendrait la
	// structure vide, ou qui echangerait les deux composantes, se verrait.
	const FVector2D Posee(-16.25, 42.5);
	Cible->PivotOffset = Posee;

	FVector2D Relue(7.0, 7.0);
	if (!TestTrue(TEXT("LirePlageDe accepte une vraie FVector2D"),
			FWorldseedUdsBridge::LirePlageDe(Cible, PlageNomBon, Relue)))
	{
		return false;
	}
	TestEqual(TEXT("le minimum relu est celui pose"), Relue.X, Posee.X);
	TestEqual(TEXT("le maximum relu est celui pose"), Relue.Y, Posee.Y);

	return true;
}

// --- ET CE QU'ELLE DOIT REFUSER ---------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedPontPlageRefuseCeQuiNEnEstPas,
	"Worldseed.Pont.PlageRefuseCeQuiNEnEstPas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedPontPlageRefuseCeQuiNEnEstPas::RunTest(const FString&)
{
	UParticleModulePivotOffset* const Module = FabriquerLeModule();
	USceneComponent* const Composant = NewObject<USceneComponent>();
	if (!TestNotNull(TEXT("le module existe"), Module)
		|| !TestNotNull(TEXT("le composant existe"), Composant))
	{
		return false;
	}

	// LE TEMOIN EST DANS LA VALEUR D'ENTREE : on remplit la sortie d'une valeur
	// reconnaissable AVANT chaque refus, et l'on verifie qu'elle n'a pas bouge.
	// Une lecture qui ecrirait (0,0) en echouant rendrait un zero qu'on lirait
	// comme une mesure -- le defaut meme que ces oracles existent pour empecher.
	const FVector2D Sentinelle(-999.0, 999.0);

	FVector2D Sortie = Sentinelle;
	TestFalse(TEXT("un TArray n'est pas une plage"),
		FWorldseedUdsBridge::LirePlageDe(Composant, PlageNomPasUnePlage, Sortie));
	TestEqual(TEXT("la sortie n'a pas ete touchee (TArray)"), Sortie, Sentinelle);

	Sortie = Sentinelle;
	TestFalse(TEXT("un nom absent est refuse"),
		FWorldseedUdsBridge::LirePlageDe(Module, PlageNomAbsent, Sortie));
	TestEqual(TEXT("la sortie n'a pas ete touchee (nom absent)"), Sortie, Sentinelle);

	Sortie = Sentinelle;
	TestFalse(TEXT("une cible nulle est refusee"),
		FWorldseedUdsBridge::LirePlageDe(nullptr, PlageNomBon, Sortie));
	TestEqual(TEXT("la sortie n'a pas ete touchee (cible nulle)"), Sortie, Sentinelle);

	Sortie = Sentinelle;
	TestFalse(TEXT("un nom vide est refuse"),
		FWorldseedUdsBridge::LirePlageDe(Module, NAME_None, Sortie));
	TestEqual(TEXT("la sortie n'a pas ete touchee (nom vide)"), Sortie, Sentinelle);

	return true;
}

// --- L'APPEL SANS ARGUMENT, ET SA GARDE -------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedPontAppelleEtProduitUnEffet,
	"Worldseed.Pont.AppelleEtProduitUnEffet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedPontAppelleEtProduitUnEffet::RunTest(const FString&)
{
	USceneComponent* const Composant = NewObject<USceneComponent>();
	if (!TestNotNull(TEXT("le composant existe"), Composant))
	{
		return false;
	}

	// ON POSE L'ETAT PAR LE C++, ET ON LE CHANGE PAR REFLEXION. Sans cette
	// premiere ligne le composant serait deja inactif et `Deactivate` ne
	// prouverait rien : un oracle qui ne peut pas rendre un resultat negatif
	// n'est pas un oracle.
	Composant->SetActive(true);
	if (!TestTrue(TEXT("l'etat de depart est bien actif"), Composant->IsActive()))
	{
		return false;
	}

	TestTrue(TEXT("une fonction sans parametre est appelee"),
		FWorldseedUdsBridge::AppelerSansArgumentSur(Composant, AppelSansParam));

	// L'EFFET, ET NON LE RETOUR : c'est la seule mesure qui tranche.
	TestFalse(TEXT("l'appel a VRAIMENT desactive le composant"),
		Composant->IsActive());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedPontRefuseUnAppelDangereux,
	"Worldseed.Pont.RefuseUnAppelDangereux",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedPontRefuseUnAppelDangereux::RunTest(const FString&)
{
	USceneComponent* const Composant = NewObject<USceneComponent>();
	if (!TestNotNull(TEXT("le composant existe"), Composant))
	{
		return false;
	}

	// `Activate(bool bReset)` PREND UN PARAMETRE, et c'est tout le sujet : sans
	// la garde, `ProcessEvent` lirait un booleen dans une pile nulle. L'oracle
	// verifie DEUX choses -- le refus, et l'absence d'effet : un appel qui
	// serait passe quand meme aurait active le composant.
	TestFalse(TEXT("une fonction a parametre est REFUSEE"),
		FWorldseedUdsBridge::AppelerSansArgumentSur(Composant, AppelAvecParam));
	TestFalse(TEXT("et elle n'a rien active"), Composant->IsActive());

	TestFalse(TEXT("une fonction absente est refusee"),
		FWorldseedUdsBridge::AppelerSansArgumentSur(Composant, AppelAbsent));
	TestFalse(TEXT("une cible nulle est refusee"),
		FWorldseedUdsBridge::AppelerSansArgumentSur(nullptr, AppelSansParam));
	TestFalse(TEXT("un nom vide est refuse"),
		FWorldseedUdsBridge::AppelerSansArgumentSur(Composant, NAME_None));

	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
