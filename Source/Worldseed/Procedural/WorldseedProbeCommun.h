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
