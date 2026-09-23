// Worldseed - revenir au menu depuis le jeu, sur une touche.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldseedRetourMenu.generated.h"

/**
 * QUI PREND ECHAP QUAND LA CARTE EST OUVERTE.
 *
 * Deux sous-systemes sondent la meme touche dans le meme tick, et **l'ordre de
 * tick entre sous-systemes n'est pas garanti**. Un simple « si la carte est
 * ouverte, je laisse passer » ne suffit donc pas : si la carte tique d'abord
 * et se ferme, le retour au menu voit ensuite « carte fermee », consomme Echap
 * a son tour, et l'on FERME LA CARTE ET QUITTE au menu sur une seule pression.
 *
 * L'arbitre porte donc la TRAME ou la carte a consomme la touche. Il est pur
 * -- quatre cas, quatre lignes -- et c'est ce qui le rend testable sans monde,
 * sans partie et sans clavier.
 */
namespace WorldseedEchap
{
	/**
	 * @param bCarteOuverte        etat de la carte AU MOMENT du sondage
	 * @param TrameCourante        `GFrameCounter`
	 * @param TrameCarteAConsomme  la trame ou la carte a pris Echap, ou zero
	 */
	WORLDSEED_API bool DoitRamenerAuMenu(bool bCarteOuverte, uint64 TrameCourante,
		uint64 TrameCarteAConsomme);
}

/**
 * UNE TOUCHE POUR REVENIR AU MENU, ET POURQUOI C'EST UN SOUS-SYSTEME.
 *
 * Le chemin normal d'Unreal serait une action d'entree liee dans un asset, ou
 * une fonction Exec sur une classe de PlayerController. Les deux exigent de
 * toucher a du CONTENU -- un `.uasset` d'Enhanced Input, ou le Blueprint
 * `BP_ThirdPersonGameMode` qui porte le pion et les entrees de ce projet. Or
 * `Content/*` est exclu du depot par `.gitignore` : un binding pose la ne
 * partirait pas sur GitHub, et le depot a deja perdu des reglages pour cette
 * raison exacte (le PlayerStart qui revenait a sa position, les acteurs
 * d'eclairage disparus entre deux sessions).
 *
 * Un sous-systeme de MONDE vit dans le code, donc il est versionne, et il
 * n'a besoin de rien d'autre que du PlayerController que le mode de jeu pose
 * deja. C'est le meme choix qui avait ete fait pour la tournee photo et pour
 * le banc.
 *
 * IL NE S'ARME QUE DANS LE NIVEAU DE JEU. Dans le menu, la meme touche doit
 * rester libre -- on y est deja.
 */
UCLASS()
class WORLDSEED_API UWorldseedRetourMenu : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bArme; }

private:
	/** Faux dans le menu, et faux une fois le retour demande. */
	bool bArme = false;
};
