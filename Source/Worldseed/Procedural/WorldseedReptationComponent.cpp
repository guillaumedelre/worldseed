// Worldseed - poser au sol les particules de ce qui rampe.

#include "Procedural/WorldseedReptationComponent.h"

#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

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

	AActor* const Proprietaire = GetOwner();
	if (!Proprietaire)
	{
		return false;
	}

	Particules = NewObject<UNiagaraComponent>(Proprietaire,
		TEXT("WorldseedReptationParticules"));
	if (!Particules)
	{
		return false;
	}

	Particules->SetAsset(Systeme);
	Particules->SetupAttachment(Proprietaire->GetRootComponent());
	// ON NE L'AUTO-ACTIVE PAS : il ne doit tourner que quand quelque chose
	// rampe reellement, sans quoi une foret paierait des particules qu'elle ne
	// montre pas.
	Particules->bAutoActivate = false;
	Particules->RegisterComponent();

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
	Particules->SetWorldLocation(Origine);

	// --- LA FORME DE LA NAPPE, POSEE UNE FOIS -------------------------------
	//
	// C'EST ICI QUE LE SYSTEME EST APLATI, et c'est tout ce qui le separe d'une
	// chute de poussiere : une boite de ponte a hauteur de genou, un decalage
	// juste au-dessus du sol, une portee rasante, et presque pas de tourbillon.
	// Ces valeurs ne dependent pas de la meteo : les reposer deux fois par
	// seconde ne servirait a rien.
	if (!bFormePosee)
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
	const float Ponte = FMath::Clamp(Reptation.Intensite, 0.0f, 1.0f) * PonteMax;
	Particules->SetVariableFloat(NomPonte, Ponte);
	Particules->SetVariableFloat(NomPonteCPU, Ponte);
	Particules->SetVariableFloat(NomAlpha,
		FMath::Clamp(0.25f + 0.75f * Reptation.Intensite, 0.0f, 1.0f));

	// LA TEINTE NE SE REPOSE QU'AU CHANGEMENT DE MATIERE. C'est un `Vector3f`
	// et non une couleur -- mesure, pas supposition -- donc l'alpha se pilote a
	// part.
	if (Reptation.Matiere != DerniereMatiere)
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
}
