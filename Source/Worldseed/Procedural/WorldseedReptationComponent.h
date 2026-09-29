// Worldseed - poser au sol les particules de ce qui rampe.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "Procedural/WorldseedReptation.h"

#include "WorldseedReptationComponent.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;

/**
 * LES PARTICULES DE LA REPTATION -- le sable, et la neige, qui courent au sol.
 *
 * IL NE DECIDE RIEN : `WorldseedReptation::Evaluer` dit ce qui rampe et a
 * quelle force, ce composant se contente de le RENDRE. La separation est
 * deliberee -- la decision est une fonction pure, testable sans moteur, et
 * c'est elle qui porte tous les pieges.
 *
 * ON N'ECRIT AUCUN SYSTEME NIAGARA, ET ON NE POURRAIT PAS. Le graphe d'un
 * Niagara n'est pas scriptable : `NiagaraEditorLibrary` n'existe meme pas cote
 * Python, contrairement a `MaterialEditingLibrary` -- c'est pourquoi ce depot a
 * des scripts de greffe de MATERIAU et aucun equivalent pour les particules. Il
 * faudrait donc le faire a la main, contre la regle du projet.
 *
 * HEUREUSEMENT LE PACK EN A DEJA UN, ET C'EST LE BON. Le systeme `Dust`
 * d'Ultra Dynamic Sky expose 51 parametres utilisateur -- releve par l'API le
 * 29 septembre 2026 -- dont `Spawn Box Height`, `World Spawn Offset`,
 * `Stick Particles to Surface` et `Twirl Velocity`. Et ses parametres
 * `Snow_Twirl`, `Splash Percentage` et `Rain Collision Channel` montrent qu'il
 * n'est pas un systeme de POUSSIERE mais le systeme de particules meteo
 * GENERIQUE du pack. « Du sable qui rampe » n'est donc pas un systeme a ecrire :
 * c'est celui-la, APLATI.
 */
UCLASS(ClassGroup = (Worldseed), meta = (BlueprintSpawnableComponent))
class WORLDSEED_API UWorldseedReptationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWorldseedReptationComponent();

	/**
	 * Rend l'etat decide par `WorldseedReptation::Evaluer`.
	 *
	 * `Origine` est la position de streaming -- donc celle du joueur. Le systeme
	 * se centre de toute facon sur la camera, mais son composant doit etre pres
	 * d'elle pour que ses bornes ne le fassent pas eliminer.
	 */
	void Appliquer(const FWorldseedReptation& Reptation, const FVector& Origine);

	/** Coupe les particules sans detruire le composant. */
	void Eteindre();

	/**
	 * HAUTEUR DE LA BOITE DE PONTE, en centimetres.
	 *
	 * C'EST LE REGLAGE QUI APLATIT. Le systeme du pack fait tomber ses grains
	 * d'un volume de ciel ; ramene a hauteur de genou, le meme systeme les fait
	 * courir au sol. Rien d'autre ne change.
	 */
	UPROPERTY(EditAnywhere, Category = "Worldseed|Reptation")
	float HauteurPonteCm = 150.0f;

	/** Decalage vertical de la ponte : juste au-dessus du sol. */
	UPROPERTY(EditAnywhere, Category = "Worldseed|Reptation")
	float DecalageSolCm = 20.0f;

	/**
	 * PORTEE DE LA NAPPE, en centimetres.
	 *
	 * Une nappe RASANTE, pas un brouillard : au-dela, ce serait le voile
	 * atmospherique qui s'en charge deja, et le surdessin translucide d'une
	 * nappe a hauteur d'oeil est ce qui coute le plus cher.
	 */
	UPROPERTY(EditAnywhere, Category = "Worldseed|Reptation")
	float PorteeCm = 2500.0f;

	/**
	 * TOURBILLON, tres faible a dessein.
	 *
	 * LE SABLE SERPENTE, IL NE TOURBILLONNE PAS. Le defaut du systeme sert des
	 * grains qui tombent du ciel et tournoient ; au sol, la course est presque
	 * rectiligne dans l'axe du vent.
	 */
	UPROPERTY(EditAnywhere, Category = "Worldseed|Reptation")
	float Tourbillon = 0.15f;

	/** Taux de ponte a pleine intensite. */
	UPROPERTY(EditAnywhere, Category = "Worldseed|Reptation")
	float PonteMax = 900.0f;

	/**
	 * TAILLE DES GRAINS, et elle arrive a ZERO dans le systeme du pack.
	 *
	 * `Sprite Scale` vaut 0,0 par defaut : c'est UDW qui la pose quand il
	 * instancie le systeme lui-meme. Une instance a nous naissait donc avec des
	 * grains de taille nulle -- invisibles quel qu'en soit le nombre -- et le
	 * systeme se chargeait, s'activait et pondait sans rien montrer. A calibrer
	 * a l'image ; `-WorldseedReptationTaille=` la balaye sans recompiler.
	 */
	UPROPERTY(EditAnywhere, Category = "Worldseed|Reptation")
	float TailleGrain = 4.0f;

private:
	/** Cree le composant de particules au premier besoin. Rend faux si absent. */
	bool Preparer();

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> Particules = nullptr;

	/** Ce qui a ete pose une fois pour toutes : la forme de la nappe. */
	bool bFormePosee = false;

	/** Pour ne journaliser l'echec de chargement qu'UNE fois. */
	bool bSystemeCherche = false;

	/** Derniere matiere rendue, pour ne reposer la teinte qu'au changement. */
	EWorldseedMatiereRampante DerniereMatiere = EWorldseedMatiereRampante::Aucune;
};
