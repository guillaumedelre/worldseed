// Worldseed - GameMode de l'ecran d'entree.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "WorldseedMenuGameMode.generated.h"

class UWorldseedMenuWidget;

/**
 * Affiche le menu au demarrage et bascule les entrees en mode UI.
 * Le widget est une classe C++ pure : aucun Widget Blueprint a referencer.
 */
UCLASS()
class WORLDSEED_API AWorldseedMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AWorldseedMenuGameMode();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(Transient)
	TObjectPtr<UWorldseedMenuWidget> MenuWidget;
};
