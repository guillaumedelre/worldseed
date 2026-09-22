// Worldseed - le sol de fond, et le seul que le systeme d'eau regarde.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WaterTerrainComponent.h"

#include "WorldseedGroundProxy.generated.h"

class UProceduralMeshComponent;

namespace WorldseedNappe
{
	/**
	 * De combien CE sommet de la nappe vue a le droit de descendre.
	 *
	 * LA NAPPE VUE S'ENFONCE POUR NE BOUCHER AUCUNE CAVITE : le voxel ne creuse
	 * que dans une bande sous la surface, et un decor pose sous cette bande ne
	 * peut rien obstruer. Mais elle porte le relief du monde ENTIER, et un
	 * enfoncement uniforme fait passer sous le niveau de la mer tout ce qui
	 * culmine plus bas que lui -- l'ocean, plan a l'altitude zero, le recouvre
	 * alors. Mesure du 22 septembre : **16,3 % des terres** ainsi noyees, une
	 * vallee verte se lisant comme une baie depuis un sommet.
	 *
	 * LE PLAFOND EST EXACTEMENT LA BONNE BORNE, et ce n'est pas un reglage
	 * heureux. Aucune chambre n'existe sous `profondeurMax + rayonMax +
	 * margeMer` d'altitude -- soixante-six metres sur ce monde -- donc tout
	 * plancher de cavite se trouve AU-DESSUS de la marge de mer, partout. Une
	 * nappe qui s'arrete a cette marge reste dessous sans creuser plus loin :
	 * les deux contraintes se rejoignent au lieu de s'opposer.
	 *
	 * ET IL EPINGLE LE RIVAGE, ce qui supprime le seul risque serieux de la
	 * rampe. L'enfoncement suit la camera, donc le relief lointain « respire »
	 * quand on marche ; sur un trait de cote cela se verrait. Or ce plafond
	 * vaut ZERO au niveau de la mer : le rivage ne bouge pas, par construction.
	 * Mesure sur 200 m parcourus : silhouette lointaine deplacee de 10,8 px
	 * avec la rampe contre 13,0 sans, donc moins que la parallaxe de la marche.
	 *
	 * @param AltitudeM     altitude du sommet, en metres, zero au niveau de la mer
	 * @param MargeMerM     la marge sous laquelle on ne descend jamais
	 * @param EnfoncementM  l'enfoncement plein, celui qui passe sous la bande
	 * @return de zero a `EnfoncementM`, JAMAIS negatif -- on n'eleve pas un sommet
	 */
	inline double PlafondDEnfoncement(double AltitudeM, double MargeMerM,
		double EnfoncementM)
	{
		return FMath::Clamp(AltitudeM - MargeMerM, 0.0,
			FMath::Max(EnfoncementM, 0.0));
	}
}

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

	/**
	 * Le sol que l'EAU lit. Jamais visible.
	 *
	 * Il sort du rendu principal une fois pour toutes, a la construction, et
	 * plus personne n'y touche. C'est ce qui rend son cout de bascule NUL par
	 * construction : on ne bascule pas ce qu'on ne bascule jamais.
	 */
	UProceduralMeshComponent* GetMesh() const { return Mesh; }

	/** Le sol qu'on VOIT vit ailleurs : voir AWorldseedHorizonProxy. */

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

/**
 * Le sol qu'on VOIT : l'horizon au-dela du terrain detaille.
 *
 * POURQUOI DEUX MAILLAGES, ET LA MESURE QUI L'A IMPOSE. Un seul servait les
 * deux, et les deux besoins se contredisent :
 *
 *   - l'EAU veut la nappe dans la passe de PROFONDEUR, toujours ;
 *   - l'IMAGE veut pouvoir la retirer quand on passe sous un plafond.
 *
 * Le seul levier qui distingue les deux est `bRenderInMainPass`
 * (PrimitiveSceneProxy.h:804), et le poser MARQUE l'etat de rendu sale : le
 * proxy de scene est recree, soit 8 388 608 sommets. Mesure en jeu, quatre
 * bascules, quatre trames -- 508 a 528 ms chacune, pour un budget de 16,67. Le
 * remede evident, `SetMeshSectionVisible`, tombe a 0,00 ms mais retire la
 * geometrie de TOUTES les passes : l'ocean se coupe, et cela aussi a ete
 * essaye puis rendu.
 *
 * Separes, chacun fait une seule chose. Celui de l'eau ne bascule plus ; celui
 * de l'image bascule pour rien. Et l'image gagne une liberte que l'autre ne
 * pouvait pas lui donner : ne nourrissant plus l'eau, elle peut etre ENFONCEE
 * sous toute la bande creusable, donc cesser de boucher arches et grottes.
 *
 * ET IL LUI FAUT SON PROPRE ACTEUR, ce qui n'etait pas evident et a ete paye.
 * Pose comme composant du sol de fond, il empoisonnait l'eau par un chemin que
 * `GetTerrainPrimitives` ne couvre PAS :
 *
 *     UWaterTerrainComponent::GetTerrainBounds()
 *       -> Owner->GetComponentsBoundingBox(true)
 *
 * Le moteur prend la boite de TOUS les composants de l'acteur porteur, sans
 * demander lesquels sont du terrain. Une nappe enfoncee de 125 m abaissait
 * donc le plancher de la WaterZone d'autant -- releve au journal, « sol a
 * partir de » passe de -349 a -474 m -- et c'est dans cette plage que la
 * texture d'information NORMALISE chaque hauteur. On perdait la precision de
 * l'eau pour un maillage qui ne la concerne en rien.
 *
 * IL NE PEUT PAS DAVANTAGE ALLER SUR LE TERRAIN : `GetTerrainPrimitives`
 * enumere TOUS les composants primitifs du terrain detaille, donc il s'y
 * retrouverait dans la texture d'information -- 125 m trop bas, ce qui est
 * exactement le defaut qui noyait des flancs de colline entiers.
 *
 * Un acteur a lui seul est donc le seul endroit ou il ne ment a personne.
 */
UCLASS()
class WORLDSEED_API AWorldseedHorizonProxy : public AActor
{
	GENERATED_BODY()

public:
	AWorldseedHorizonProxy();

	UProceduralMeshComponent* GetMesh() const { return Mesh; }

private:
	UPROPERTY(VisibleAnywhere, Category = "Worldseed")
	TObjectPtr<UProceduralMeshComponent> Mesh;
};
