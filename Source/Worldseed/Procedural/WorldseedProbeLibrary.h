// Worldseed - points d'entree de verification, appelables sans lancer le jeu.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "WorldseedProbeLibrary.generated.h"

/**
 * Sondes de verification.
 *
 * POURQUOI CE FICHIER. La chaine de generation ne tourne qu'en jeu, derriere le
 * menu : verifier une etape demandait de lancer l'editeur, cliquer, attendre,
 * puis lire les journaux. Ces fonctions donnent le meme resultat depuis un
 * script, en quelques secondes — ce qui change la boucle de travail du tout au
 * tout quand on porte un algorithme et qu'on veut savoir s'il produit
 * les bons nombres.
 *
 * Elles ne servent qu'au diagnostic : rien du jeu n'en depend.
 */
UCLASS()
class WORLDSEED_API UWorldseedProbeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Genere un monde et journalise ce que l'hydrologie en tire.
	 *
	 * Rend un resume d'une ligne, pour l'appelant qui ne lit pas les journaux.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeHydrology(int32 Seed = 20260909, float HeightMeters = 8000.0f,
		int32 ResolutionY = 256);

	/**
	 * Chronometre le rendu du globe d'apercu.
	 *
	 * La taille de la texture ne change pas avec celle du monde : si le temps
	 * par image grimpe quand meme, c'est que le cout ne vient pas du nombre de
	 * pixels mais des ACCES au heightfield.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeGlobe(int32 Seed = 20260909, float HeightMeters = 8000.0f,
		int32 ResolutionY = 256, int32 Frames = 20);
};
