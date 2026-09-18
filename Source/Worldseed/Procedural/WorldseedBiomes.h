// Worldseed - etape 5 : classification des biomes.
//
// Portage de Tools/WorldGen/worldgen/biomes.py.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedRules.h"

/**
 * Les 19 biomes, dans l'ordre des identifiants de world_rules.json.
 *
 * L'ETAGEMENT ALTITUDINAL N'EST PAS UNE REGLE SEPAREE : il tombe tout seul du
 * gradient adiabatique applique au climat. Un sommet tropical a 3 000 m est a
 * 7 degres, donc le diagramme de Whittaker y lit "foret temperee" — une foret
 * de montagne. A 4 500 m il lit "toundra". C'est exactement le Kilimandjaro,
 * sans une seule regle d'altitude.
 */
enum class EWorldseedBiome : uint8
{
	Ocean = 0,
	Lake = 1,
	River = 2,
	IceCap = 3,
	Tundra = 4,
	Taiga = 5,
	TemperateForest = 6,
	TemperateRainforest = 7,
	Grassland = 8,
	Steppe = 9,
	ColdDesert = 10,
	HotDesert = 11,
	Savanna = 12,
	TropicalDryForest = 13,
	TropicalRainforest = 14,
	Alpine = 15,
	BareRock = 16,
	Beach = 17,
	Marsh = 18,

	Count = 19
};

/** Un seuil du diagramme de Whittaker : jusqu'a tant de pluie, ce biome. */
struct WORLDSEED_API FWorldseedWhittakerCut
{
	float MaxPrecipMm = 0.0f;
	EWorldseedBiome Biome = EWorldseedBiome::Grassland;
};

/** Une bande de temperature du diagramme, avec ses seuils de pluie. */
struct WORLDSEED_API FWorldseedWhittakerBand
{
	float MaxTempC = 0.0f;
	TArray<FWorldseedWhittakerCut> Cuts;
};

/** Section "biomes" de world_rules.json. */
struct WORLDSEED_API FWorldseedBiomeRules
{
	TArray<FWorldseedWhittakerBand> Bands;

	float TreeLineTempC = 4.0f;
	float AlpineMinElevationM = 212.5f;
	float PermanentIceTempC = 0.0f;
	float BareRockSlopeDeg = 55.0f;

	float BeachElevationM = 3.75f;
	float BeachWidthM = 37.5f;
	float BeachSlopeFlatDeg = 8.0f;
	float BeachSlopeSteepDeg = 30.0f;
	float BeachWindwardBonus = 0.6f;

	float MarshMaxSlopeDeg = 2.0f;
	float MarshMinPrecipMm = 900.0f;

	static FWorldseedBiomeRules FromRules(const UWorldseedRules& Rules);
};

/**
 * Ce qui RECOUVRE le sol, independamment du climat qui y regne.
 *
 * POURQUOI C'EST UN AXE SEPARE. Ocean, lac et riviere ne sont pas des biomes :
 * ce sont des nappes POSEES SUR un climat. Les ranger dans le meme index que la
 * savane ou la taiga forcait a choisir entre les deux, et peindre une cellule
 * "riviere" effacait le biome qui etait dessous — on perdait qu'il s'agissait
 * d'une riviere DANS une savane plutot que DANS une taiga, alors que ce sont
 * deux paysages sans rapport. Impossible, dans ces conditions, de border le
 * cours d'une vegetation de savane : la berge n'etait plus de la savane.
 *
 * Deux axes separes, et l'information reste entiere.
 */
enum class EWorldseedCover : uint8
{
	/** Rien : le biome climatique affleure. */
	None = 0,
	Ocean = 1,
	Lake = 2,
	River = 3,

	Count = 4
};

/** Ce que la classification produit. */
struct WORLDSEED_API FWorldseedBiomeMap
{
	/**
	 * Le biome CLIMATIQUE par cellule, partout. Indexe J * NX + I.
	 *
	 * Defini MEME SOUS LA MER, ou il decrit la bande climatique de l'eau. Ce
	 * n'est pas une curiosite : l'ancienne chaine devait justement aller
	 * chercher "le climat de la cote la plus proche" pour piloter la meteo au
	 * large, faute de savoir lire le climat sur place. Ici la question ne se
	 * pose plus.
	 */
	TArray<uint8> Index;

	/** Ce qui recouvre chaque cellule, meme indexation. */
	TArray<uint8> Cover;

	/** Pente en degres, calculee en chemin et reutilisable. */
	TArray<float> SlopeDeg;

	/** Part de chaque biome sur les terres emergees, en pourcentage. */
	float LandSharePct[static_cast<int32>(EWorldseedBiome::Count)] = {};

	/** Part de chaque couverture sur les terres emergees, en pourcentage. */
	float CoverSharePct[static_cast<int32>(EWorldseedCover::Count)] = {};
};

/**
 * Les quatre matieres qu'un pack de textures doit fournir.
 *
 * POURQUOI QUATRE ET NON DIX-NEUF. Exiger une texture par biome interdirait
 * d'emblee tous les packs du commerce, qui en proposent une poignee. Quatre
 * matieres melangees par poids, TEINTEES par la couleur du biome, suffisent a
 * distinguer une savane d'une prairie sans demander deux textures d'herbe.
 *
 * La neige n'en fait pas partie : Ultra Dynamic Sky la depose lui-meme sur le
 * materiau, en fonction de la temperature et des chutes que le climat calcule.
 * Une texture de neige serait figee la ou la sienne fond et s'accumule.
 */
enum class EWorldseedGroundSlot : uint8
{
	Grass = 0,
	Arid = 1,     // sable et terre seche
	Rock = 2,
	Moss = 3,     // mousse, tourbe, sous-bois humide

	Count = 4
};

namespace WorldseedBiomes
{
	/**
	 * Poids des quatre matieres pour un biome, dans l'ordre de
	 * EWorldseedGroundSlot. La somme vaut un.
	 */
	WORLDSEED_API FLinearColor SlotWeights(EWorldseedBiome Biome);

	/** Couleur de reference d'un biome, depuis debugColors. */
	WORLDSEED_API FLinearColor Colour(EWorldseedBiome Biome);

	/** Couleur de reference d'une couverture, depuis debugColors. */
	WORLDSEED_API FLinearColor CoverColour(EWorldseedCover Cover);

	WORLDSEED_API const TCHAR* CoverName(EWorldseedCover Cover);

	/** Nom lisible, pour les journaux. */
	WORLDSEED_API const TCHAR* Name(EWorldseedBiome Biome);

	/** Pente locale en degres. */
	WORLDSEED_API void SlopeDegrees(const TArray<float>& ElevationM,
		const FWorldseedGeometry& Geometry, TArray<float>& OutSlopeDeg);

	/**
	 * Classe chaque cellule.
	 *
	 * TempMaxC est la moyenne du mois le plus chaud : c'est elle, et non la
	 * moyenne annuelle, qui decide de la calotte glaciaire — une terre ou meme
	 * l'ete ne degele pas.
	 */
	WORLDSEED_API void Classify(const FWorldseedGeometry& Geometry,
		const TArray<float>& ElevationM, const TArray<float>& TempMeanC,
		const TArray<float>& TempMaxC, const TArray<float>& PrecipMm,
		const TArray<bool>& LakeMask, const TArray<bool>& RiverMask,
		const FWorldseedBiomeRules& Rules, FWorldseedBiomeMap& Out);
}
