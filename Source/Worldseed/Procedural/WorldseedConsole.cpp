// Worldseed - commandes console : aller quelque part, et savoir ou l'on est.

#include "Procedural/WorldseedVoxelTerrain.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

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
