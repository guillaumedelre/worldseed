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

	/** Range le monde calcule par le menu, climat compris. */
	void StoreWorld(const FWorldseedWorldData& InWorld);

	/** Rend le monde en cache. Faux si le joueur n'est pas passe par le menu. */
	bool TryGetWorld(FWorldseedWorldData& OutWorld) const;

	/** Vrai si un monde attend d'etre consomme. */
	UFUNCTION(BlueprintPure, Category = "Worldseed")
	bool HasPendingWorld() const { return bHasWorld; }

private:
	/**
	 * Le monde entier : relief, climat, saisons — et demain l'hydrologie.
	 *
	 * Pas d'UPROPERTY : la structure ne contient que des nombres, aucun
	 * UObject a garder en vie pour le ramasse-miettes. Elle vit et meurt avec
	 * le GameInstance, ce qui est exactement la duree voulue.
	 */
	FWorldseedWorldData World;

	UPROPERTY(Transient)
	bool bHasWorld = false;
};
