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
	 * Chronometre le rendu du globe d'apercu.
	 *
	 * La taille de la texture ne change pas avec celle du monde : si le temps
	 * par image grimpe quand meme, c'est que le cout ne vient pas du nombre de
	 * pixels mais des ACCES au heightfield.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeGlobe(int32 Seed = 20260909, float HeightMeters = 8000.0f,
		int32 ResolutionY = 256, int32 Frames = 20);

	/**
	 * Maille des chunks de voxels et rend ce qu'ils ont coute.
	 *
	 * C'EST LA MESURE QUI DECIDE DE LA TAILLE DU VOXEL ET DU CHUNK, et elle
	 * vient avant tout le reste : streaming, collision et creusement se
	 * dimensionnent sur elle. Sans moteur de rendu, sans acteur, sans PIE --
	 * donc reproductible et comparable d'une session a l'autre.
	 *
	 * Les chunks sont pris en tuile autour d'un point de TERRE, sans quoi on
	 * mesurerait le cout du vide : au-dessus de l'ocean il n'y a pas de
	 * surface a mailler, et le releve serait flatteur autant qu'inutile.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeVoxel(int32 Seed = 20260909, float HeightMeters = 8000.0f,
		int32 ResolutionY = 256, int32 ChunkSideM = 32, int32 ChunksPerSide = 4);

	/**
	 * Mesure ce que le bruit 3D produit reellement : galeries et surplombs.
	 *
	 * DEUX GRANDEURS, ET ELLES SE SUFFISENT.
	 *
	 * Un SURPLOMB, c'est par definition une colonne que la surface traverse
	 * plus d'une fois : on compte donc les changements de signe du champ le
	 * long de chaque verticale. Une GALERIE, c'est de l'air sous la surface :
	 * on compte la part des points de la bande ou le champ est positif.
	 *
	 * Rien de tout cela ne se voit a l'oeil -- une grotte est sous terre et il
	 * y fait noir -- d'ou cette sonde. Elle relit les regles du disque a chaque
	 * appel, pour qu'un essai coute une seconde et non un redemarrage.
	 */
	/**
	 * Rend la part des terres que porte CHAQUE CASE du diagramme de Whittaker.
	 *
	 * POURQUOI CETTE SONDE. Le tableau des biomes donne la part de chaque NOM,
	 * or plusieurs noms couvrent plusieurs cases : "desert froid" s'etale sur
	 * trois bandes de temperature, "foret temperee" sur deux. Une case qui ne
	 * porte rien ne merite pas un nom ; une case qui porte cinq pour cent des
	 * terres sous le nom d'une autre est une erreur de vocabulaire. La part par
	 * NOM ne permet de trancher ni l'un ni l'autre.
	 *
	 * Elle relit les regles du disque, comme les autres : retoucher un seuil du
	 * diagramme et remesurer doit couter une seconde.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeWhittaker(int32 Seed = 20260909, float HeightMeters = 8000.0f,
		int32 ResolutionY = 1024);

	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeCaves(int32 Seed = 20260909, float HeightMeters = 8000.0f,
		int32 ResolutionY = 1024, float AreaM = 512.0f, float StepM = 4.0f,
		bool bSteepest = false);
};
