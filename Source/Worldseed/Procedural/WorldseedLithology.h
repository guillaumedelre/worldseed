// Worldseed - la lithologie : de quelle ROCHE est fait le sous-sol.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedRules.h"

/** Une roche du catalogue, lue dans world_rules.json. */
struct WORLDSEED_API FWorldseedLithologyEntry
{
	FString Key;
	FString Label;
	FLinearColor Colour = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);

	/**
	 * Aptitude a se dissoudre, dans [0..1].
	 *
	 * C'EST LE PARAMETRE QUI DECIDERA DES GROTTES. Un karst est un reseau creuse
	 * par dissolution : il demande du calcaire et de l'eau. Le granite ne se
	 * dissout pas — ses cavites, quand il y en a, sont des diaclases, c'est-a-dire
	 * des fractures, et c'est un tout autre generateur.
	 */
	float Karstifiable = 0.0f;

	/**
	 * Durete relative, dans [0..1]. Non consommee pour l'instant.
	 *
	 * ELLE N'EST PAS BRANCHEE SUR L'EROSION, ET C'EST DELIBERE : la brancher
	 * changerait le relief de tous les mondes existants, donc les rivieres, les
	 * biomes et le point d'apparition. C'est un arbitrage a part entiere, pas un
	 * effet de bord de l'ajout d'un catalogue.
	 */
	float Hardness = 0.5f;
};

/** Reglages de la lithologie, section "substrat" de world_rules.json. */
struct WORLDSEED_API FWorldseedLithologyRules
{
	/** Le catalogue, INDEXE PAR IDENTIFIANT, comme le registre des biomes. */
	TArray<FWorldseedLithologyEntry> Catalogue;

	/** Identifiants resolus depuis les cles du catalogue, -1 si absent. */
	int32 IdOceanique = -1;
	int32 IdSocle = -1;
	TArray<int32> IdsSedimentaires;
	TArray<float> PartsSedimentaires;

	/**
	 * Convergence au-dela de laquelle une croute continentale expose son socle.
	 *
	 * Une chaine en formation souleve et decape : ce qui affleure au coeur d'un
	 * orogene est du cristallin, pas la couverture sedimentaire qui a ete
	 * emportee.
	 */
	float SocleConvergence = 0.35f;

	/** Meme effet par l'altitude, pour les reliefs anciens deja decapes. */
	float SocleElevationM = 150.0f;

	/** Frequence du motif sedimentaire, en cycles par tour de monde. */
	float MotifFrequency = 6.0f;
	int32 MotifOctaves = 3;

	static FWorldseedLithologyRules FromRules(const UWorldseedRules& Rules);
};

/**
 * De quelle roche est fait le sous-sol, par cellule.
 *
 * POURQUOI CE CHAMP EXISTE, ET POURQUOI IL N'EST PAS UN BIOME. Ce qui gouverne
 * un reseau de grottes, c'est la ROCHE, pas le climat de surface : un massif
 * calcaire est karstique sous une foret tropicale comme sous un maquis. Faire
 * dependre les cavites du biome reviendrait a laisser une ETIQUETTE decider
 * d'une geometrie — exactement ce que l'architecture de ce projet interdit,
 * puisque l'etiquette ne sert qu'a lier des assets.
 *
 * ELLE EST 2D POUR L'INSTANT, une roche dominante par colonne. Un vrai
 * sous-sol est feuillete, et une galerie qui descend traverse des couches ;
 * c'est une extension naturelle, mais elle demande de decider comment les
 * couches se deposent, ce qui est un chantier a part.
 *
 * ELLE SE CALCULE DEPUIS LA TECTONIQUE, AVANT L'EROSION : les plaques ne
 * bougent pas quand la surface se creuse, et l'erosion ne transforme pas du
 * granite en calcaire.
 */
struct WORLDSEED_API FWorldseedLithology
{
	/** Identifiant de roche par cellule. Indexe J * NX + I. */
	TArray<uint8> Id;

	bool IsValid(int32 CellCount) const { return Id.Num() == CellCount; }
};

namespace WorldseedLithology
{
	WORLDSEED_API void Compute(const FWorldseedGeometry& Geometry,
		const TArray<float>& ElevationM, const TArray<uint8>& IsContinental,
		const TArray<float>& Convergence, const FWorldseedLithologyRules& Rules,
		int32 Seed, FWorldseedLithology& Out);

	/** Nom lisible d'une roche, pour les journaux. */
	WORLDSEED_API const TCHAR* Name(const FWorldseedLithologyRules& Rules, uint8 Id);

	/**
	 * Erodabilite par cellule : le coefficient K de la puissance de courant.
	 *
	 * RAPPORTEE A LA MOYENNE, ET C'EST DELIBERE. Une erodabilite absolue
	 * changerait la quantite TOTALE d'erosion du monde, donc l'amplitude du
	 * relief, donc tout le calage terrestre -- pour une question qui ne porte
	 * que sur sa REPARTITION. En centrant sur la moyenne, on redistribue
	 * l'erosion de la roche tendre vers la roche dure sans changer son volume
	 * au premier ordre, et le bulletin terrestre ne bouge que de ce qu'on
	 * voulait vraiment changer.
	 *
	 * Poids a zero : tableau vide, donc comportement d'avant a l'identique.
	 */
	WORLDSEED_API void Erodibility(const FWorldseedLithology& Lithology,
		const FWorldseedLithologyRules& Rules, float Weight, TArray<float>& Out);
}
