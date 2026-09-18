// Worldseed - le sol de fond, et le seul que le systeme d'eau regarde.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WaterTerrainComponent.h"

#include "WorldseedGroundProxy.generated.h"

class UProceduralMeshComponent;

/**
 * Le composant qui presente le sol au plugin Water — et LUI SEUL.
 *
 * GetTerrainPrimitives() rend par defaut TOUTES les primitives de l'acteur
 * porteur. Or celui-ci en porte deux, et elles ne disent pas la meme chose :
 * celle qu'on AFFICHE passe volontairement sous le relief detaille pour ne pas
 * le traverser, celle que l'eau doit LIRE suit le relief exactement.
 *
 * Laisser le defaut donnait la premiere a l'eau. Sur une carte de seize
 * kilometres, ce sol d'affichage descend jusqu'a soixante-douze metres sous le
 * terrain reel : l'eau croyait pouvoir monter d'autant, et noyait des flancs
 * de colline entiers. Un correctif d'affichage avait casse le rendu de l'eau
 * sans que rien ne relie les deux.
 */
UCLASS()
class WORLDSEED_API UWorldseedWaterTerrainComponent : public UWaterTerrainComponent
{
	GENERATED_BODY()

public:
	virtual TArray<UPrimitiveComponent*> GetTerrainPrimitives() const override;

	/** Le sol de fond, qui couvre toute la carte. */
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Ground;

	/**
	 * Le sol exact, proche du joueur : les chunks du terrain detaille.
	 *
	 * ILS APPARTIENNENT A UN AUTRE ACTEUR, ET C'EST TOUT L'INTERET.
	 *
	 * Le sous-systeme d'eau redemande la texture d'information quand un
	 * composant salit son etat de rendu — mais il filtre par acteur
	 * PROPRIETAIRE, et le seul acteur enregistre est celui-ci. Les chunks
	 * peuvent donc etre rendus dans la texture sans jamais declencher la
	 * tempete de reconstructions qui faisait clignoter l'eau.
	 *
	 * On y gagne un sol EXACT sur les deux kilometres et demi qui entourent le
	 * joueur — la ou l'eau se regarde — sans rien changer a l'affichage.
	 */
	UPROPERTY()
	TWeakObjectPtr<AActor> DetailedTerrain;
};

/**
 * Le sol basse resolution qui couvre tout le monde, sur son PROPRE acteur.
 *
 * POURQUOI UN ACTEUR A LUI SEUL, ET NON UN COMPOSANT DU TERRAIN.
 *
 * Le sous-systeme d'eau ecoute MarkRenderStateDirtyEvent, evenement GLOBAL du
 * moteur, et ne le filtre que par acteur proprietaire :
 *
 *     if (WaterTerrainActors.Find(ComponentOwner) != nullptr)
 *         OnWaterTerrainActorChanged(ComponentOwner);   // -> MarkForRebuild
 *
 * Pose sur le terrain, le composant d'eau aurait fait redemander la texture
 * d'information du monde entier a chaque chunk qui apparait ou change de
 * resolution — plusieurs fois par tick en marche. L'eau passait son temps a se
 * refaire, et se voyait clignoter.
 *
 * DEUX MAILLAGES, DEUX BESOINS CONTRADICTOIRES. L'affichage veut un sol qui
 * passe SOUS les chunks, sinon les deux surfaces s'interpenetrent. L'eau veut
 * un sol EXACT, sinon elle deborde. Un seul maillage ne peut pas servir les
 * deux : c'est le constat qui a coute le plus cher de ce chantier.
 */
UCLASS()
class WORLDSEED_API AWorldseedGroundProxy : public AActor
{
	GENERATED_BODY()

public:
	AWorldseedGroundProxy();

	/** Declare le terrain detaille, dont les chunks serviront de sol a l'eau. */
	void SetDetailedTerrain(AActor* Terrain);

	/** Le sol AFFICHE : basse resolution, enfonce sous le relief detaille. */
	UProceduralMeshComponent* GetMesh() const { return Mesh; }

private:
	UPROPERTY(VisibleAnywhere, Category = "Worldseed")
	TObjectPtr<UProceduralMeshComponent> Mesh;

	/**
	 * Declare le maillage exact au systeme d'eau comme etant LE sol.
	 *
	 * Sans lui, la zone d'eau ne cherche que des Landscape — et ce monde n'en
	 * a pas, puisqu'un Landscape ne peut pas naitre au runtime. Elle retombe
	 * alors sur une profondeur forfaitaire, et la texture d'information ne sait
	 * plus ou l'eau rencontre la terre.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Worldseed")
	TObjectPtr<UWorldseedWaterTerrainComponent> WaterTerrain;
};
