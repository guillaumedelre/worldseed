// Worldseed - poser au sol les particules de ce qui rampe.

#include "Procedural/WorldseedReptationComponent.h"

#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraFunctionLibrary.h"

#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	/**
	 * LE SYSTEME DE PARTICULES METEO DU PACK.
	 *
	 * Il s'appelle `Dust`, mais ses 51 parametres utilisateur portent aussi
	 * `Snow_Twirl`, `Splash Percentage` et `Rain Collision Channel` : c'est le
	 * systeme GENERIQUE d'Ultra Dynamic Sky, pas un systeme de poussiere.
	 *
	 * LE CHEMIN EST EN DUR, ET C'EST ASSUME. UDW porte bien une variable
	 * `Dust Niagara System` qui le nomme, mais notre pont ne sait lire que des
	 * nombres et des booleens -- lui ajouter un lecteur d'OBJET pour une seule
	 * reference coute plus que le chemin. En echange, l'absence est DITE : sans
	 * le pack, on journalise une fois et l'on ne rend rien, exactement comme le
	 * pont degrade ailleurs.
	 */
	const TCHAR* const CheminSysteme = TEXT("/Game/UltraDynamicSky/Particles/Dust");

	// LES NOMS SONT CEUX DU PACK, ET LEURS TYPES SONT MESURES.
	//
	// Releve par `NiagaraFunctionLibrary::GetAllUserParameters` le 29 septembre
	// 2026 : 29 noms demandes, 29 presents. Le releve a immediatement paye --
	// `Sprite Color Multiplier` est un **Vector3f**, pas une LinearColor, et
	// `World Spawn Offset` aussi. Poser une couleur sur un vecteur echoue EN
	// SILENCE, et ce depot a corrige trois defauts de cette famille le meme
	// jour (la rampe du decor, les surcharges d'UDW, l'horloge).
	const FName NomHauteurPonte = TEXT("Spawn Box Height");
	const FName NomDecalagePonte = TEXT("World Spawn Offset");     // Vector3f
	const FName NomPortee = TEXT("Max Spawn Distance");
	const FName NomTourbillon = TEXT("Twirl Velocity");
	const FName NomPonte = TEXT("Spawn Rate");
	const FName NomPonteCPU = TEXT("CPU Spawn Rate");
	const FName NomTeinte = TEXT("Sprite Color Multiplier");       // Vector3f
	const FName NomAlpha = TEXT("Sprite Alpha");
	const FName NomTaille = TEXT("Sprite Scale");
	const FName NomColler = TEXT("Stick Particles to Surface");    // bool
	const FName NomCollision = TEXT("Particle Collision Enabled"); // bool
	const FName NomPlafond = TEXT("Ceiling Check Height");
	const FName NomEtirement = TEXT("Motion Stretching");

	// LES TROIS QUI ARRIVENT A ZERO, et sans lesquels rien ne s'affiche.
	const FName NomTailleBase = TEXT("Sprite Scale");
	const FName NomAmbiant = TEXT("Ambient Intensity");
	const FName NomVentEchelle = TEXT("Wind Velocity Scale");
}

UWorldseedReptationComponent::UWorldseedReptationComponent()
{
	// Il ne tique pas : son proprietaire l'appelle au rythme du ciel, une fois
	// par demi-seconde. Ce qui rampe au sol change a l'echelle du kilometre.
	PrimaryComponentTick.bCanEverTick = false;
}

bool UWorldseedReptationComponent::Preparer()
{
	if (Particules)
	{
		return true;
	}

	// ON NE CHERCHE QU'UNE FOIS. Sans ce garde, un pack absent ferait une
	// tentative de chargement deux fois par seconde pour toute la partie.
	if (bSystemeCherche)
	{
		return false;
	}
	bSystemeCherche = true;

	UNiagaraSystem* const Systeme =
		LoadObject<UNiagaraSystem>(nullptr, CheminSysteme);
	if (!Systeme)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] reptation : systeme de particules introuvable (%s) -- ")
			TEXT("rien ne rampera. Le pack Ultra Dynamic Sky est-il installe ?"),
			CheminSysteme);
		return false;
	}

	// --- ⚠ ON S'ATTACHE AU PION, PAS AU TERRAIN -----------------------------
	//
	// C'EST LA CAUSE DE TOUT CE QUI PRECEDE, et elle a coute cinq tentatives.
	//
	// Ce composant vit sur `AWorldseedTerrain`, et c'est juste : c'est le
	// terrain qui sait ce qu'il y a SOUS les pieds. Mais son ACTEUR est a
	// l'ORIGINE DU MONDE, quand le joueur se promene a quatorze kilometres de
	// la. Un systeme de particules attache a cette racine a donc ses bornes
	// la-bas : il tourne -- etat Active, age qui avance, mille particules par
	// seconde -- et il n'est jamais DESSINE.
	//
	// LE SIGNE QUI L'A TRAHI : `SpawnSystemAttached` rend NULL sur cette racine,
	// parce qu'elle fait un controle d'elimination prealable que le chemin
	// manuel (`NewObject` + `RegisterComponent`) ne fait pas -- celui-la
	// acceptait en silence et produisait un systeme invisible.
	//
	// LA DECISION APPARTIENT AU TERRAIN, LE RENDU APPARTIENT AU JOUEUR.
	UWorld* const Monde = GetWorld();
	APawn* const Pion = Monde ? UGameplayStatics::GetPlayerPawn(Monde, 0) : nullptr;
	USceneComponent* const Support = Pion ? Pion->GetRootComponent() : nullptr;

	if (!Support)
	{
		// PAS ENCORE DE PION : on reessaiera. Surtout ne pas verrouiller la
		// recherche ici -- le pion arrive apres le terrain.
		bSystemeCherche = false;
		return false;
	}

	// ON PASSE PAR LA FONCTION DU MOTEUR, PAS PAR `NewObject`.
	//
	// C'EST LE DEFAUT QUI A COUTE LE PLUS CHER ICI. Un composant bati a la main
	// -- `NewObject` + `SetAsset` + `RegisterComponent` -- se charge, s'active,
	// rend un controleur d'instance en etat ACTIVE dont l'age avance, et ne
	// dessine RIEN. Mesure : meme image a un demi-point de clarte pres, que nos
	// reglages soient poses ou que le systeme tourne sur ses valeurs d'usine
	// (116,20 contre 116,89 dans le ciel, 145,24 contre 145,02 au sol).
	//
	// `SpawnSystemAttached` fait un travail d'initialisation que le chemin
	// manuel ne fait pas, et c'est la voie que le moteur emploie partout.
	Particules = UNiagaraFunctionLibrary::SpawnSystemAttached(
		Systeme,
		Support,
		NAME_None,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		EAttachLocation::KeepRelativeOffset,
		/*bAutoDestroy=*/false,
		/*bAutoActivate=*/false);

	if (!Particules)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] reptation : SpawnSystemAttached n'a rien rendu"));
		return false;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] reptation : systeme « %s » charge, particules pretes"),
		*Systeme->GetName());
	return true;
}

void UWorldseedReptationComponent::Eteindre()
{
	if (Particules && Particules->IsActive())
	{
		Particules->Deactivate();
		DerniereMatiere = EWorldseedMatiereRampante::Aucune;
	}
}

void UWorldseedReptationComponent::Appliquer(
	const FWorldseedReptation& Reptation, const FVector& Origine)
{
	// L'INTERRUPTEUR QUI PERMET DE SEPARER LES DEUX CAUSES.
	//
	// Une image noyee d'ocre a deux explications -- le voile ATMOSPHERIQUE qui
	// sature, ou la nappe de particules au sol -- et les regarder ensemble ne
	// les separe pas. Ce depot proscrit ce montage, et il a deja regle quatre
	// fois le mauvais bouton faute de l'avoir monte.
	//
	// `-WorldseedReptation=0` coupe les particules SANS toucher au voile.
	static const bool bCoupee = []()
	{
		int32 V = 1;
		FParse::Value(FCommandLine::Get(), TEXT("WorldseedReptation="), V);
		return V == 0;
	}();

	if (bCoupee)
	{
		Eteindre();
		return;
	}

	// ET LES DEUX REGLAGES QUI DECIDENT DU SURDESSIN, pilotables sans
	// recompiler : c'est par eux qu'un A/B de densite passe.
	static const float PonteSurcharge = []()
	{
		float V = -1.0f;
		FParse::Value(FCommandLine::Get(), TEXT("WorldseedReptationPonte="), V);
		return V;
	}();
	static const float HauteurSurcharge = []()
	{
		float V = -1.0f;
		FParse::Value(FCommandLine::Get(), TEXT("WorldseedReptationHauteur="), V);
		return V;
	}();
	static const float TailleSurcharge = []()
	{
		float V = -1.0f;
		FParse::Value(FCommandLine::Get(), TEXT("WorldseedReptationTaille="), V);
		return V;
	}();
	// CELLE-CI ACCEPTE LE NEGATIF, et c'est tout son objet : la boite se
	// descend sous la camera. Un sentinelle a -1 ne conviendrait donc pas.
	static const bool bDecalageDonne = FParse::Param(
		FCommandLine::Get(), TEXT("WorldseedReptationDecalageDonne"));
	static const float DecalageSurcharge = []()
	{
		float V = 0.0f;
		FParse::Value(FCommandLine::Get(), TEXT("WorldseedReptationDecalage="), V);
		return V;
	}();
	if (bDecalageDonne) { DecalageSolCm = DecalageSurcharge; }

	if (PonteSurcharge >= 0.0f) { PonteMax = PonteSurcharge; }
	if (HauteurSurcharge >= 0.0f) { HauteurPonteCm = HauteurSurcharge; }
	if (TailleSurcharge >= 0.0f) { TailleGrain = TailleSurcharge; }

	if (!Reptation.EstActive())
	{
		Eteindre();
		return;
	}

	if (!Preparer() || !Particules)
	{
		return;
	}

	// LE COMPOSANT SUIT LE JOUEUR. Le systeme se centre de toute facon sur la
	// camera -- il porte `Override Camera Transform` et `Custom Camera Position`
	// -- mais ses BORNES sont celles du composant : laisse a l'origine du monde,
	// il serait elimine du rendu des que le joueur s'en eloigne.

	// --- LA FORME DE LA NAPPE, POSEE UNE FOIS -------------------------------
	//
	// C'EST ICI QUE LE SYSTEME EST APLATI, et c'est tout ce qui le separe d'une
	// chute de poussiere : une boite de ponte a hauteur de genou, un decalage
	// juste au-dessus du sol, une portee rasante, et presque pas de tourbillon.
	// Ces valeurs ne dependent pas de la meteo : les reposer deux fois par
	// seconde ne servirait a rien.
	// LE TEST QUI SEPARE « MES REGLAGES CASSENT » DE « LE SYSTEME NE REND PAS ».
	//
	// Le controleur dit que le systeme TOURNE -- etat Active, age qui avance,
	// mille particules par seconde, visible, non elimine par la distance -- et
	// rien ne se dessine. Les deux explications restantes sont opposees : soit
	// un de mes reglages de forme etouffe le rendu (boite de ponte ramenee de
	// 2500 a 150, portee de 10000 a 2500), soit le systeme a besoin d'autre
	// chose. `-WorldseedReptationBrut=1` n'applique AUCUNE forme et laisse les
	// defauts du pack : si des grains apparaissent alors, le coupable est chez
	// moi.
	static const bool bBrut = []()
	{
		int32 V = 0;
		FParse::Value(FCommandLine::Get(), TEXT("WorldseedReptationBrut="), V);
		return V != 0;
	}();

	if (!bFormePosee && !bBrut)
	{
		bFormePosee = true;

		Particules->SetVariableFloat(NomHauteurPonte, HauteurPonteCm);
		Particules->SetVariableVec3(NomDecalagePonte,
			FVector(0.0, 0.0, DecalageSolCm));
		Particules->SetVariableFloat(NomPortee, PorteeCm);
		Particules->SetVariableFloat(NomTourbillon, Tourbillon);

		// COLLER A LA SURFACE, PARCE QUE LA REPTATION EST UN CONTACT. Un grain
		// qui saute retombe ; c'est ce qui distingue la course au sol d'une
		// nappe qui flotte.
		//
		// ⚠ NON VERIFIE SUR LE TERRAIN VOXEL, et c'est le seul point qui peut
		// faire echouer cette voie : la collision Niagara passe par les champs
		// de distance ou par des traces de scene, et un `ProceduralMeshComponent`
		// n'a peut-etre pas de mesh distance field. Si rien ne colle, le repli
		// est `World Spawn Offset` seul -- suffisant sur du plat, pas sur un
		// versant.
		Particules->SetVariableBool(NomColler, true);
		Particules->SetVariableBool(NomCollision, true);

		// Un plafond bas : on ne veut pas que les grains cherchent un toit a
		// trente metres au-dessus d'une dune.
		Particules->SetVariableFloat(NomPlafond, HauteurPonteCm * 2.0f);

		// --- ⚠ LE SYSTEME ARRIVE ENTIEREMENT DESARME -----------------------
		//
		// C'EST LE PIEGE DE L'INTERRUPTEUR MAITRE, TRANSPOSE AUX PARTICULES, et
		// il a coute deux captures et une mesure avant d'etre vu. Releve des
		// valeurs par defaut d'un composant neuf, le 29 septembre 2026 :
		//
		//     Sprite Scale          0,0   <- des grains de taille NULLE
		//     Ambient Intensity     0,0   <- non eclaires
		//     Wind Velocity Scale   0,0   <- ils ne suivent pas le vent
		//     Motion Stretching     0,0
		//     Twirl Velocity        0,0
		//     CPU Spawn Rate        0,0
		//
		// Le systeme se charge, s'active, pond ses deux mille particules par
		// seconde -- et rien ne s'affiche, parce qu'elles sont invisibles. C'est
		// UDW qui pose ces valeurs quand il l'instancie LUI-MEME ; notre
		// instance doit donc les poser aussi.
		//
		// LE SIGNE QUI L'A TRAHI : couper les particules ne changeait pas
		// l'image d'un dixieme de clarte -- 145,2 contre 145,1. On ne voyait
		// rien parce qu'il n'y avait rien a voir.
		Particules->SetVariableFloat(NomTailleBase, TailleGrain);
		Particules->SetVariableFloat(NomAmbiant, 1.0f);
		Particules->SetVariableFloat(NomVentEchelle, 1.0f);

		// L'etirement de mouvement donne la trainee des grains rapides.
		Particules->SetVariableFloat(NomEtirement, 0.35f);
	}

	// --- CE QUI SUIT LA METEO ------------------------------------------------
	//
	// EN MODE BRUT on ne touche a RIEN, pas meme la ponte : le but est de voir
	// le systeme tel que le pack le livre, sans une seule de nos ecritures.
	const float Ponte = FMath::Clamp(Reptation.Intensite, 0.0f, 1.0f) * PonteMax;
	if (!bBrut)
	{
		Particules->SetVariableFloat(NomPonte, Ponte);
		Particules->SetVariableFloat(NomPonteCPU, Ponte);
		Particules->SetVariableFloat(NomAlpha,
			FMath::Clamp(0.25f + 0.75f * Reptation.Intensite, 0.0f, 1.0f));
	}

	// LA TEINTE NE SE REPOSE QU'AU CHANGEMENT DE MATIERE. C'est un `Vector3f`
	// et non une couleur -- mesure, pas supposition -- donc l'alpha se pilote a
	// part.
	if (Reptation.Matiere != DerniereMatiere && !bBrut)
	{
		DerniereMatiere = Reptation.Matiere;
		Particules->SetVariableVec3(NomTeinte, FVector(
			Reptation.Teinte.R, Reptation.Teinte.G, Reptation.Teinte.B));

		// LA NEIGE A DES FLOCONS PLUS GROS QUE LES GRAINS DE SABLE, et c'est ce
		// qui les distingue d'un coup d'oeil quand la teinte seule ne suffit
		// pas -- de la neige au crepuscule et du sable a midi peuvent avoir la
		// meme clarte.
		//
		// ⚠ C'EST UN FACTEUR SUR `TailleGrain`, PAS UNE VALEUR ABSOLUE. Ces
		// deux lignes visent le MEME parametre `Sprite Scale` : poser 1,0 ici
		// ecrasait la taille de base posee plus haut, et ramenait les grains a
		// un quart de leur taille -- quasi invisibles, ce qui ressemblait trait
		// pour trait au defaut qu'on venait de corriger.
		Particules->SetVariableFloat(NomTailleBase, TailleGrain *
			(Reptation.Matiere == EWorldseedMatiereRampante::Neige ? 1.6f : 1.0f));

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] reptation : %s au sol, intensite %.2f (part %.2f), cap %.0f deg"),
			Reptation.Matiere == EWorldseedMatiereRampante::Neige
				? TEXT("NEIGE") : TEXT("SABLE"),
			Reptation.Intensite, Reptation.Part, Reptation.DirectionDeg);
	}

	if (!Particules->IsActive())
	{
		Particules->Activate();
	}

	// --- LE CONTROLE QUI SEPARE « INVISIBLE » DE « INEXISTANT » -------------
	//
	// IL MANQUAIT, ET C'EST POUR CELA QUE TROIS REGLAGES ONT ETE TENTES A
	// L'AVEUGLE. Juger sur l'image ne dit pas si le systeme TOURNE : des grains
	// de taille nulle, un systeme elimine par la distance et un systeme qui ne
	// pond rien rendent tous les trois la meme image -- et le meme chiffre.
	//
	// LE CONTROLEUR NE COMPTE PAS LES PARTICULES (`GetNumParticles` est
	// commente dans `NiagaraSystemInstance.h`), mais il dit ce qui compte
	// vraiment : l'etat d'execution REEL -- pas celui qu'on a demande --, si le
	// systeme s'est termine, si son age avance, et la distance de LOD qui
	// pourrait l'avoir elimine.
	//
	// UNE FOIS, PUIS TOUTES LES DIX SECONDES : un etat qui change apres coup est
	// precisement ce qu'on cherche, et un journal par demi-seconde serait
	// illisible.
	SecondesDepuisDiagnostic += 0.5f;
	if (!bDiagnostiquee || SecondesDepuisDiagnostic >= 10.0f)
	{
		bDiagnostiquee = true;
		SecondesDepuisDiagnostic = 0.0f;

		if (FNiagaraSystemInstanceControllerConstPtr Ctrl =
				Particules->GetSystemInstanceController())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] reptation : etat REEL %d (demande %d), termine %s, ")
				TEXT("age %.1f s, LOD %.0f cm | actif %s, visible %s, ponte %.0f, ")
				TEXT("taille %.1f"),
				static_cast<int32>(Ctrl->GetActualExecutionState()),
				static_cast<int32>(Ctrl->GetRequestedExecutionState()),
				Ctrl->IsComplete() ? TEXT("OUI") : TEXT("non"),
				Ctrl->GetAge(), Ctrl->GetLODDistance(),
				Particules->IsActive() ? TEXT("oui") : TEXT("NON"),
				Particules->IsVisible() ? TEXT("oui") : TEXT("NON"),
				Ponte, TailleGrain);
		}
		else
		{
			// PAS DE CONTROLEUR = PAS D'INSTANCE. Le composant existe, il est
			// « actif », et rien ne tourne derriere : c'est le cas que l'image
			// ne peut pas distinguer des autres.
			UE_LOG(LogTemp, Error,
				TEXT("[Worldseed] reptation : AUCUN controleur d'instance -- le ")
				TEXT("systeme n'a jamais demarre, quoi qu'en dise IsActive()"));
		}
	}
}


