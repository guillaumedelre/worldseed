// Worldseed - revenir au menu depuis le jeu, sur une touche.

#include "Procedural/WorldseedRetourMenu.h"

#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	/** Le niveau du menu, tel que Config/DefaultEngine.ini le designe. */
	const TCHAR* const NiveauDuMenu = TEXT("/Game/Worldseed/Maps/L_Menu");

	/** Le niveau de jeu, celui ou la touche doit repondre. */
	const TCHAR* const NiveauDuJeu = TEXT("L_Worldseed_Proc");

	/**
	 * LA TOUCHE, ET POURQUOI CELLE-LA.
	 *
	 * Echap est la seule touche qu'un joueur essaie SANS QU'ON LUI DISE quand
	 * il veut sortir d'un monde. Elle est libre ici : ce projet n'a ni menu de
	 * pause ni inventaire, et rien d'autre ne l'ecoute.
	 */
	const FKey ToucheRetour = EKeys::Escape;
}

void UWorldseedRetourMenu::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// ON NE S'ARME QUE DANS LE NIVEAU DE JEU. Dans le menu la meme touche doit
	// rester libre -- on y est deja, et la rendre active y ferait recharger le
	// menu sur lui-meme, ce qui jetterait le monde en cours de generation.
	const FString Nom = InWorld.GetMapName();
	bArme = Nom.Contains(NiveauDuJeu);

	if (bArme)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] retour au menu : %s ramene a l'ecran de configuration"),
			*ToucheRetour.GetDisplayName().ToString());
	}
}

void UWorldseedRetourMenu::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bArme)
	{
		return;
	}

	UWorld* const W = GetWorld();
	APlayerController* const PC = W ? UGameplayStatics::GetPlayerController(W, 0) : nullptr;
	if (!PC)
	{
		// Le pion et son controleur arrivent apres le begin play du
		// sous-systeme -- le depot a deja paye cette lecon sur la tournee
		// photo, qui rendait zero vue ET aucune ligne de journal parce que
		// tout sortait sur le premier test de validite. On attend, sans se
		// desarmer.
		return;
	}

	if (!PC->WasInputKeyJustPressed(ToucheRetour))
	{
		return;
	}

	// ON SE DESARME AVANT D'OUVRIR. Le chargement de niveau n'est pas
	// instantane : sans cela, deux tics de plus suffisent a empiler deux
	// OpenLevel sur la meme pression.
	bArme = false;

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] retour au menu demande"));
	UGameplayStatics::OpenLevel(W, FName(NiveauDuMenu));
}

TStatId UWorldseedRetourMenu::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWorldseedRetourMenu, STATGROUP_Tickables);
}
