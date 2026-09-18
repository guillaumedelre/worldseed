// Worldseed - GameMode de l'ecran d'entree.

#include "Procedural/WorldseedMenuGameMode.h"
#include "Procedural/WorldseedMenuWidget.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

AWorldseedMenuGameMode::AWorldseedMenuGameMode()
{
	// Aucun pawn : l'ecran d'entree est purement UI.
	DefaultPawnClass = nullptr;
	PrimaryActorTick.bCanEverTick = false;
}

void AWorldseedMenuGameMode::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Worldseed] pas de PlayerController, menu non affiche"));
		return;
	}

	MenuWidget = CreateWidget<UWorldseedMenuWidget>(PC, UWorldseedMenuWidget::StaticClass());
	if (!MenuWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("[Worldseed] creation du menu impossible"));
		return;
	}

	MenuWidget->AddToViewport();

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(MenuWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(InputMode);
	PC->bShowMouseCursor = true;

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] menu affiche"));
}
