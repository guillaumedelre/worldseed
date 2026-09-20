// Worldseed - le socle commun des sondes : generer un monde et l'instrumenter.

#pragma once

#include "CoreMinimal.h"

#include "Procedural/WorldseedDensity.h"
#include "Procedural/WorldseedLithology.h"
#include "Procedural/WorldseedPipeline.h"

/**
 * Un monde genere, avec de quoi l'interroger.
 *
 * POURQUOI CE SOCLE EXISTE. Les huit sondes du projet repetaient toutes le
 * meme preambule : relire les regles, generer, verifier, batir le champ de
 * densite, y brancher la lithologie. Vingt lignes chacune, et le depot a deja
 * paye ce qu'un preambule recopie coute : probe_voxel avait OUBLIE
 * SetLithology, si bien que KarstifiableAt rendait 1 partout, que le terme de
 * diaclase sortait a la premiere garde, et que le releve annoncait "2,67 ms
 * par chunk, le bruit est gratuit". Le vrai chiffre etait 3,65.
 *
 * UN TEMOIN NON BRANCHE MESURE LE MONDE D'AVANT, et il ne le dit pas : le
 * chiffre reste plausible. Un socle unique rend l'oubli impossible.
 */
struct FWorldseedSonde
{
	WorldseedPipeline::FResult World;
	FWorldseedLithologyRules Litho;
	FWorldseedDensityRules DensiteRegles;
	FWorldseedDensity Densite;
	const UWorldseedRules* Regles = nullptr;
	FString Erreur;

	bool bValide = false;

	/**
	 * Genere le monde et branche tout ce qui doit l'etre.
	 *
	 * Rend faux et renseigne Erreur si quoi que ce soit manque -- ce qui vaut
	 * mieux qu'une sonde qui tourne sur un monde incomplet et rend un nombre.
	 */
	WORLDSEED_API bool Preparer(int32 Seed, float HeightMeters, int32 ResolutionY);
};

/**
 * QUANTIFICATION D'UNE POSITION, POUR SOUDER DES MAILLAGES INDEPENDANTS.
 *
 * Deux chunks sont mailles separement et ne partagent aucune numerotation :
 * on ne peut donc les coudre que par la POSITION. C'est d'ailleurs exactement
 * la situation du jeu -- deux composants distincts -- et une fissure y est une
 * discontinuite de position, jamais d'indice.
 *
 * Un seizieme de centimetre. Les deux cotes calculent la MEME interpolation
 * depuis les MEMES valeurs aux MEMES points, donc les doubles devraient
 * coincider au bit pres ; le grain n'est la que pour que la mesure ne depende
 * pas de cette esperance.
 *
 * ELLE VIT ICI PARCE QU'ELLE SERVAIT DEJA DEUX SONDES, et le depot a une regle
 * pour cela -- « ne jamais recopier une formule dans deux fichiers ». Le
 * compilateur l'a rappelee a sa facon : deux copies dans des namespaces
 * ANONYMES, qu'UBT a fait tomber dans la meme unite de traduction, ou deux
 * namespaces anonymes n'en font qu'un. La collision ne dependait pas du code
 * ecrit mais du REGROUPEMENT choisi par UBT -- troisieme fois dans ce depot.
 */
FORCEINLINE FIntVector WorldseedGrainSonde(const FVector& PosCm)
{
	return FIntVector(
		FMath::RoundToInt(PosCm.X * 16.0),
		FMath::RoundToInt(PosCm.Y * 16.0),
		FMath::RoundToInt(PosCm.Z * 16.0));
}
