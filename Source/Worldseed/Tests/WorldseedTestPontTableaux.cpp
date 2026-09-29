// Worldseed - le pont sait-il ajouter a un tableau d'objets sans rien casser ?

#if WITH_DEV_AUTOMATION_TESTS

#include "Procedural/WorldseedUdsBridge.h"

#include "Components/SceneComponent.h"
#include "Misc/AutomationTest.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundCue.h"

/**
 * CE QUE CES ORACLES PROTEGENT.
 *
 * `AjouterAuTableauObjets` ecrit un pointeur dans un tableau trouve PAR NOM,
 * dans une classe Blueprint d'un pack payant que le depot ne contient pas. Deux
 * familles de defaut la guettent, et aucune ne se signale a l'execution :
 *
 * 1. LE SILENCE. Une ecriture par reflexion posee sur une propriete absente ne
 *    rend rien, ne journalise rien, et ne fait rien -- douze fois paye dans ce
 *    depot sur des parametres de materiau. Un nom qui change d'une version
 *    d'UDS a l'autre nous laisserait croire la liste blanche peuplee.
 * 2. LA CORRUPTION. Un `TArray<double>` qui porterait le meme nom accepterait
 *    l'ecriture d'un pointeur et la memoire partirait en morceaux. C'est le
 *    meme danger que la pile mal formee de `CallFunction`, et il se traite de
 *    la meme facon : on COMPTE et l'on VERIFIE avant d'ecrire, et l'on refuse
 *    plutot que de deviner.
 *
 * POURQUOI DES FIXTURES DU MOTEUR ET NON UNE CLASSE DE TEST. La cible reelle --
 * `UDS_DLWE_Interaction_Settings` -- est du contenu de pack, absent du depot :
 * un oracle qui en dependrait ne tournerait pas sur un clone. Deux classes du
 * moteur ont exactement les formes qu'il faut eprouver, et elles se construisent
 * sans monde :
 *
 *     USoundClass::ChildClasses   TArray<USoundClass*>   <- la bonne forme
 *     UActorComponent::ComponentTags  TArray<FName>      <- un tableau, mais
 *                                                           pas d'objets
 *
 * Ce que ces oracles NE peuvent PAS garder, et il faut le dire : que le nom
 * `Physical Materials which enable DLWE Interactions on non-Landscapes` soit
 * toujours celui du pack. Aucun test hors contenu ne le saura ; seul un releve
 * sur l'asset le dit, et il est dans le registre du 29 septembre 2026.
 */
namespace
{
	const FName NomBon(TEXT("ChildClasses"));
	const FName NomPasObjets(TEXT("ComponentTags"));
	const FName NomAbsent(TEXT("CeNomNExistePas"));
}

// --- LA FORME NOMINALE ------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedPontTableauxAjouteEtRelit,
	"Worldseed.Pont.TableauxAjouteEtRelit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedPontTableauxAjouteEtRelit::RunTest(const FString&)
{
	USoundClass* const Cible = NewObject<USoundClass>();
	USoundClass* const Valeur = NewObject<USoundClass>();

	// LE TEMOIN EST L'ETAT D'AVANT : un tableau vide. Sans lui, un oracle qui
	// trouve une entree ne saurait pas s'il l'a ajoutee.
	TArray<UObject*> Avant;
	TestTrue(TEXT("le tableau se lit"),
		FWorldseedUdsBridge::LireTableauObjets(Cible, NomBon, Avant));
	TestEqual(TEXT("et il part vide"), Avant.Num(), 0);

	int32 Taille = -1;
	TestTrue(TEXT("l'ajout reussit"),
		FWorldseedUdsBridge::AjouterAuTableauObjets(Cible, NomBon, Valeur,
			&Taille));
	TestEqual(TEXT("la taille rendue est 1"), Taille, 1);

	// ET ON RELIT, parce qu'un appel qui rend vrai n'est pas une preuve.
	TArray<UObject*> Apres;
	TestTrue(TEXT("le tableau se relit"),
		FWorldseedUdsBridge::LireTableauObjets(Cible, NomBon, Apres));
	TestEqual(TEXT("il porte une entree"), Apres.Num(), 1);
	if (Apres.Num() == 1)
	{
		TestTrue(TEXT("et c'est LA notre, pas une autre"), Apres[0] == Valeur);
	}

	Cible->ChildClasses.Reset();
	return true;
}

// --- L'IDEMPOTENCE, QUI N'EST PAS UN LUXE ----------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedPontTableauxNEmpilePas,
	"Worldseed.Pont.TableauxNEmpilePas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedPontTableauxNEmpilePas::RunTest(const FString&)
{
	// LE PILOTE APPELLERA CECI A CHAQUE APPARITION DE PION. Sans idempotence, la
	// liste blanche du pack grossirait a chaque mort du joueur -- un defaut qui
	// ne se verrait jamais en jeu et qui finirait par peser.
	USoundClass* const Cible = NewObject<USoundClass>();
	USoundClass* const Valeur = NewObject<USoundClass>();

	int32 T1 = -1, T2 = -1, T3 = -1;
	FWorldseedUdsBridge::AjouterAuTableauObjets(Cible, NomBon, Valeur, &T1);
	FWorldseedUdsBridge::AjouterAuTableauObjets(Cible, NomBon, Valeur, &T2);
	FWorldseedUdsBridge::AjouterAuTableauObjets(Cible, NomBon, Valeur, &T3);

	TestEqual(TEXT("premier ajout : 1"), T1, 1);
	TestEqual(TEXT("deuxieme : toujours 1"), T2, 1);
	TestEqual(TEXT("troisieme : toujours 1"), T3, 1);

	// LE TEMOIN DE L'IDEMPOTENCE : une valeur DIFFERENTE doit, elle, s'ajouter.
	// Sans ce contre-controle, une fonction qui n'ecrit JAMAIS passerait les
	// trois assertions ci-dessus.
	USoundClass* const Autre = NewObject<USoundClass>();
	int32 T4 = -1;
	FWorldseedUdsBridge::AjouterAuTableauObjets(Cible, NomBon, Autre, &T4);
	TestEqual(TEXT("une autre valeur porte le compte a 2"), T4, 2);

	Cible->ChildClasses.Reset();
	return true;
}

// --- LES REFUS, ET C'EST LA MOITIE DE LA VALEUR ----------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedPontTableauxRefuseAuLieuDeDeviner,
	"Worldseed.Pont.TableauxRefuseAuLieuDeDeviner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedPontTableauxRefuseAuLieuDeDeviner::RunTest(const FString&)
{
	USoundClass* const Cible = NewObject<USoundClass>();
	USoundClass* const Valeur = NewObject<USoundClass>();
	USceneComponent* const PasDesObjets = NewObject<USceneComponent>();

	// LE MAUVAIS TYPE EST UN COMPOSANT, ET NON UN `UPhysicalMaterial` comme la
	// cible reelle. Le motif est prosaique : `UPhysicalMaterial::StaticClass()`
	// vit dans le module `PhysicsCore`, que ce module ne lie pas -- et ajouter
	// une dependance de module pour un test serait la payer partout. Ce qui est
	// EPROUVE ici ne change pas : un objet d'une autre classe est refuse.
	USceneComponent* const MauvaisType = PasDesObjets;

	// 1. UN NOM QUI N'EXISTE PAS. C'est le cas d'UDS renommant sa variable.
	TestFalse(TEXT("propriete absente : refuse"),
		FWorldseedUdsBridge::AjouterAuTableauObjets(Cible, NomAbsent, Valeur));
	TArray<UObject*> Ignore;
	TestFalse(TEXT("et elle ne se lit pas davantage"),
		FWorldseedUdsBridge::LireTableauObjets(Cible, NomAbsent, Ignore));

	// 2. UN TABLEAU QUI N'EST PAS D'OBJETS. C'est le cas dangereux : ecrire ici
	//    corromprait la memoire au lieu d'echouer.
	TestFalse(TEXT("TArray<FName> : refuse"),
		FWorldseedUdsBridge::AjouterAuTableauObjets(PasDesObjets, NomPasObjets,
			Valeur));
	TestEqual(TEXT("et rien n'y a ete ecrit"),
		PasDesObjets->ComponentTags.Num(), 0);

	// 3. LE BON TABLEAU, LE MAUVAIS TYPE. Le pack lirait un pointeur d'un type
	//    qu'il n'attend pas -- ce qui plante chez lui, pas chez nous.
	TestFalse(TEXT("objet d'une autre classe : refuse"),
		FWorldseedUdsBridge::AjouterAuTableauObjets(Cible, NomBon,
			MauvaisType));
	TestEqual(TEXT("et le tableau reste vide"), Cible->ChildClasses.Num(), 0);

	// 4. LES DEUX NULS.
	TestFalse(TEXT("cible nulle : refuse"),
		FWorldseedUdsBridge::AjouterAuTableauObjets(nullptr, NomBon, Valeur));
	TestFalse(TEXT("valeur nulle : refuse"),
		FWorldseedUdsBridge::AjouterAuTableauObjets(Cible, NomBon, nullptr));
	TestEqual(TEXT("toujours vide"), Cible->ChildClasses.Num(), 0);

	// LE TEMOIN DES QUATRE REFUS : la meme fonction, sur la bonne forme, DOIT
	// reussir. Sans cela, une fonction qui refuserait TOUT passerait cet oracle
	// en entier -- et c'est exactement le defaut qu'un test de refus laisse
	// filer s'il ne porte pas son positif.
	TestTrue(TEXT("temoin : la forme valide passe"),
		FWorldseedUdsBridge::AjouterAuTableauObjets(Cible, NomBon, Valeur));
	TestEqual(TEXT("temoin : et elle a bien ecrit"),
		Cible->ChildClasses.Num(), 1);

	Cible->ChildClasses.Reset();
	return true;
}

// --- LE POINTEUR SIMPLE, POUR `Interaction Settings` ----------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedPontObjetEcritRelitEtEffacce,
	"Worldseed.Pont.ObjetEcritRelitEtEffacce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedPontObjetEcritRelitEtEffacce::RunTest(const FString&)
{
	// `USoundCue::SoundClassObject` est un `USoundClass*` : exactement la forme
	// de `Interaction Settings`, et totalement inerte.
	USoundCue* const Cible = NewObject<USoundCue>();
	USoundClass* const Valeur = NewObject<USoundClass>();
	const FName Nom(TEXT("SoundClassObject"));

	// LE TEMOIN EST L'ETAT D'AVANT : nul. Sans lui, lire notre valeur ne
	// prouverait pas qu'on l'a ecrite.
	TestNull(TEXT("il part nul"),
		FWorldseedUdsBridge::LireObjet(Cible, Nom));

	TestTrue(TEXT("l'ecriture reussit"),
		FWorldseedUdsBridge::EcrireObjet(Cible, Nom, Valeur));
	TestTrue(TEXT("et se relit a la bonne valeur"),
		FWorldseedUdsBridge::LireObjet(Cible, Nom) == Valeur);

	// L'EFFACEMENT EST LEGITIME, et il doit passer : `EcrireObjet` accepte nul
	// a dessein. Sans ce cas, un refus trop zele empecherait de remettre les
	// reglages du pack si on le voulait.
	TestTrue(TEXT("nul est accepte"),
		FWorldseedUdsBridge::EcrireObjet(Cible, Nom, nullptr));
	TestNull(TEXT("et le champ redevient nul"),
		FWorldseedUdsBridge::LireObjet(Cible, Nom));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedPontObjetRefuseLeMauvaisType,
	"Worldseed.Pont.ObjetRefuseLeMauvaisType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedPontObjetRefuseLeMauvaisType::RunTest(const FString&)
{
	USoundCue* const Cible = NewObject<USoundCue>();
	USoundClass* const BonType = NewObject<USoundClass>();
	USceneComponent* const MauvaisType = NewObject<USceneComponent>();
	const FName Nom(TEXT("SoundClassObject"));

	TestFalse(TEXT("objet d'une autre classe : refuse"),
		FWorldseedUdsBridge::EcrireObjet(Cible, Nom, MauvaisType));
	TestNull(TEXT("et rien n'a ete ecrit"),
		FWorldseedUdsBridge::LireObjet(Cible, Nom));

	TestFalse(TEXT("propriete absente : refuse"),
		FWorldseedUdsBridge::EcrireObjet(Cible, NomAbsent, BonType));
	TestNull(TEXT("et sa lecture rend nul"),
		FWorldseedUdsBridge::LireObjet(Cible, NomAbsent));

	TestFalse(TEXT("cible nulle : refuse"),
		FWorldseedUdsBridge::EcrireObjet(nullptr, Nom, BonType));
	TestNull(TEXT("lecture sur cible nulle : nul"),
		FWorldseedUdsBridge::LireObjet(nullptr, Nom));

	// LE TEMOIN DES REFUS : la forme valide DOIT passer, sinon une fonction qui
	// refuse tout passerait cet oracle en entier.
	TestTrue(TEXT("temoin : la forme valide passe"),
		FWorldseedUdsBridge::EcrireObjet(Cible, Nom, BonType));
	TestTrue(TEXT("temoin : et elle a bien ecrit"),
		FWorldseedUdsBridge::LireObjet(Cible, Nom) == BonType);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
