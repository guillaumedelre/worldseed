// Worldseed - transport du monde genere entre les niveaux.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedWorldData.h"
#include "WorldseedGameInstance.generated.h"

/**
 * Le GameInstance survit aux changements de niveau : c'est lui qui porte le
 * monde choisi dans L_Menu jusqu'au terrain de L_Worldseed_Proc.
 *
 * Il transporte le HEIGHTFIELD deja calcule, pas les parametres. Depuis que la
 * chaine comporte une tectonique et un climat, regenerer cote terrain couterait
 * une seconde fois le prix fort — plusieurs secondes — alors que le menu vient
 * de le payer.
 */
UCLASS()
class WORLDSEED_API UWorldseedGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	/** Recupere le GameInstance Worldseed, ou nullptr. */
	UFUNCTION(BlueprintPure, Category = "Worldseed", meta = (WorldContext = "WorldContextObject"))
	static UWorldseedGameInstance* GetWorldseedGameInstance(const UObject* WorldContextObject);

	/**
	 * Range le monde calcule par le menu, climat compris.
	 *
	 * IL EST COPIE UNE FOIS ICI, ET PLUS JAMAIS ENSUITE. Le menu tient encore
	 * son resultat au moment de l'appel, donc on ne peut pas le lui prendre ;
	 * mais a partir de cet instant le monde devient partage et immuable, et
	 * tous ceux qui le consomment n'en prennent qu'une reference.
	 */
	void StoreWorld(const FWorldseedWorldData& InWorld);

	/**
	 * Rend une REFERENCE sur le monde en cache, ou un pointeur nul si le
	 * joueur n'est pas passe par le menu.
	 *
	 * IL RENDAIT UNE COPIE. Sur la grille du jeu, cela faisait cent
	 * quatre-vingt-treize megaoctets recopies a chaque consommateur -- et,
	 * plus grave, cela laissait la donnee SANS PROPRIETAIRE CLAIR : les
	 * travaux de maillage capturaient l'adresse d'un membre d'acteur que
	 * `EndPlay` pouvait liberer sous eux.
	 */
	FWorldseedMondePtr MondePartage() const { return Monde; }

	/** Vrai si un monde attend d'etre consomme. */
	UFUNCTION(BlueprintPure, Category = "Worldseed")
	bool HasPendingWorld() const { return Monde.IsValid(); }

private:
	/**
	 * Le monde entier : relief, climat, saisons, roches, biomes, cavites.
	 *
	 * Pas d'UPROPERTY : la structure ne contient que des nombres, aucun
	 * UObject a garder en vie pour le ramasse-miettes. Elle vit tant qu'un
	 * porteur la tient -- l'instance de jeu, les acteurs, et les travaux de
	 * maillage en vol.
	 */
	FWorldseedMondePtr Monde;
};
