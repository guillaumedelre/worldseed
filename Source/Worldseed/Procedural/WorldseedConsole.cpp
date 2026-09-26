// Worldseed - commandes console : aller quelque part, et savoir ou l'on est.

#include "Procedural/WorldseedVoxelTerrain.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	/**
	 * POURQUOI UNE COMMANDE CONSOLE ET PAS UNE FONCTION exec.
	 *
	 * Les fonctions marquees exec ne sont trouvees que sur les objets de la
	 * chaine de routage d'ULocalPlayer::Exec -- entree, pion, HUD, mode de jeu,
	 * gestionnaire de triche, etat de jeu, camera (Player.cpp:125-153). Aucun
	 * n'est a nous : le pion et le mode de jeu de ce projet sont des Blueprints
	 * du modele Epic, et y ajouter une fonction demanderait de toucher a du
	 * contenu... qui n'est pas versionne. FAutoConsoleCommand s'enregistre
	 * aupres du gestionnaire global : elle ne depend d'aucun acteur, elle
	 * survit a un remplacement du pion, et elle existe aussi en build final.
	 */
	AWorldseedVoxelTerrain* TrouverTerrain(UWorld* Monde)
	{
		if (!Monde)
		{
			return nullptr;
		}
		for (TActorIterator<AWorldseedVoxelTerrain> It(Monde); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	void Aller(const TArray<FString>& Args, UWorld* Monde, FOutputDevice& Ar)
	{
		AWorldseedVoxelTerrain* const Terrain = TrouverTerrain(Monde);
		if (!Terrain)
		{
			Ar.Logf(TEXT("Worldseed : aucun terrain voxel dans ce monde. ")
				TEXT("Cette commande ne vaut qu'en jeu."));
			return;
		}

		if (Args.Num() < 2)
		{
			Ar.Logf(TEXT("Usage : Worldseed.Aller <x en metres> <y en metres>"));
			Ar.Logf(TEXT("Worldseed.Lieux donne les endroits de CE monde."));
			return;
		}

		const double X = FCString::Atod(*Args[0]);
		const double Y = FCString::Atod(*Args[1]);
		Terrain->TeleporterJoueur(X, Y);
		Ar.Logf(TEXT("Worldseed : en route vers (%.0f, %.0f) m. Le joueur est ")
			TEXT("tenu en vol le temps que le sol se batisse."), X, Y);
	}

	// UNE COMMANDE QUI NE REPOND RIEN NE SE DIAGNOSTIQUE PAS. Sans terrain, ces
	// deux-la se taisaient : impossible de distinguer "la commande n'existe
	// pas" de "elle n'a rien trouve a dire".
	void Ou(const TArray<FString>&, UWorld* Monde, FOutputDevice& Ar)
	{
		AWorldseedVoxelTerrain* const Terrain = TrouverTerrain(Monde);
		Ar.Logf(TEXT("Worldseed : %s"), Terrain
			? *Terrain->OuSuisJe()
			: TEXT("aucun terrain voxel dans ce monde -- cette commande ne vaut qu'en jeu."));
	}

	void Lieux(const TArray<FString>&, UWorld* Monde, FOutputDevice& Ar)
	{
		AWorldseedVoxelTerrain* const Terrain = TrouverTerrain(Monde);
		Ar.Logf(TEXT("%s"), Terrain
			? *Terrain->LieuxRemarquables()
			: TEXT("Worldseed : aucun terrain voxel dans ce monde -- cette commande ne vaut qu'en jeu."));
	}

	/**
	 * CE QU'ON REGARDE, ET SOUS QUEL MATERIAU.
	 *
	 * Sans elle, un defaut d'aspect signale en jeu se diagnostique en DEVINANT
	 * quel maillage on a sous les yeux -- et l'on corrige alors les especes
	 * qu'on croit presentes pendant que la fautive reste dehors.
	 */
	void Especes(const TArray<FString>& Args, UWorld* Monde, FOutputDevice& Ar)
	{
		AWorldseedVoxelTerrain* const Terrain = TrouverTerrain(Monde);
		if (!Terrain)
		{
			Ar.Logf(TEXT("Worldseed : aucun terrain voxel dans ce monde."));
			return;
		}

		// LE CENTRE EST LE POINT VISE, PAS LE JOUEUR. On regarde un rocher a
		// quelques metres ; centrer sur le pion noierait la reponse sous tout
		// ce qui pousse a ses pieds. A defaut de visee, on retombe sur le pion.
		double RayonM = 15.0;
		if (Args.Num() >= 1)
		{
			RayonM = FMath::Max(1.0, FCString::Atod(*Args[0]));
		}

		FVector Centre = Terrain->GetActorLocation();
		if (const APlayerCameraManager* const Cam =
			UGameplayStatics::GetPlayerCameraManager(Monde, 0))
		{
			const FVector Oeil = Cam->GetCameraLocation();
			const FVector Avant = Cam->GetCameraRotation().Vector();

			// Un sondage droit devant : c'est CE qu'on regarde, et le rayon
			// part de la surface touchee plutot que de l'oeil.
			FHitResult Touche;
			FCollisionQueryParams P(SCENE_QUERY_STAT(WorldseedEspeces), false);
			if (const APawn* const Pion = UGameplayStatics::GetPlayerPawn(Monde, 0))
			{
				P.AddIgnoredActor(Pion);
			}
			const bool bTouche = Monde->LineTraceSingleByChannel(
				Touche, Oeil, Oeil + Avant * 20000.0, ECC_Visibility, P);
			Centre = bTouche ? Touche.ImpactPoint : Oeil;
		}

		Ar.Logf(TEXT("%s"), *Terrain->EspecesAutour(Centre,
			RayonM * WorldseedMetersToCm));
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GEspeces(
		TEXT("Worldseed.Especes"),
		TEXT("Ce qui est pose autour du point vise, avec son materiau effectif. "
			 "Argument optionnel : rayon en metres (15 par defaut)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Especes));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GAller(
		TEXT("Worldseed.Aller"),
		TEXT("Teleporte le joueur a une position du monde, en metres."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Aller));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GOu(
		TEXT("Worldseed.Ou"),
		TEXT("Position, altitude, pente et roche sous le joueur."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Ou));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GLieux(
		TEXT("Worldseed.Lieux"),
		TEXT("Les endroits du monde charge qui meritent d'etre vus."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Lieux));
}
