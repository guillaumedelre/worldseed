// Worldseed - etape 5 : classification des biomes.
//
// Portage de Tools/WorldGen/worldgen/biomes.py.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedRules.h"

/**
 * Les 19 identifiants, dans l'ordre de world_rules.json.
 *
 * ILS NE SONT PAS TOUS DES BIOMES, ET ON NE LES RENUMEROTE PAS. Ces valeurs
 * sont des CLES : elles indexent surfaces.recipes, les recettes de vegetation
 * et les tables en dur de ce fichier. Les tasser pour combler les trous
 * decalerait tout, en silence. Etat reel de chacun :
 *
 *   0  Ocean, 1 Lake, 2 River  - plus jamais attribues comme biome : ce sont
 *      des nappes, elles vivent dans EWorldseedCover. Gardes comme cle de
 *      palette et de recette pour cet axe-la.
 *   16 BareRock, 17 Beach      - plus jamais attribues non plus depuis le
 *      18 septembre 2026 : ce sont des SUBSTRATS, une forme du relief et non
 *      un climat. Passes eux aussi dans EWorldseedCover. Ils restent la cle
 *      d'apparence de ce substrat, via AppearanceBiome.
 *   18 Marsh                   - inatteignable depuis le retrait de
 *      l'hydrologie : il nait de l'eau douce, qui n'existe plus.
 *
 * Restent donc 12 biomes climatiques et l'etage alpin, qui en est un vrai --
 * il est pose par la limite des arbres, pas par la seule altitude.
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

	/**
	 * Climat mediterraneen : ete sec, hiver doux et arrose.
	 *
	 * AJOUTE EN FIN DE LISTE, JAMAIS INTERCALE. C'est l'un des quatorze biomes
	 * de reference, environ 2 % des terres, et le seul grand absent de la
	 * liste -- le bulletin terrestre l'avouait en attendant "steppe ou prairie"
	 * pour ses trois releves reels, faute de mieux.
	 */
	Mediterranean = 19,

	Count = 20
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

	/**
	 * Temperature du MOIS LE PLUS CHAUD sous laquelle aucun arbre ne pousse.
	 *
	 * C'EST LE VRAI CRITERE DE LA LIMITE DES ARBRES, et c'est celui de Koppen
	 * (isotherme 10 degres du mois le plus chaud, frontiere ET/Df). Un arbre a
	 * besoin d'une SAISON DE CROISSANCE, pas d'une moyenne annuelle clemente :
	 * Iakoutsk est a -11,6 degres de moyenne et porte de la taiga, quand des
	 * cotes plus douces en moyenne n'ont pas un arbre.
	 *
	 * Sans lui, la separation toundra/taiga se faisait sur la moyenne annuelle,
	 * et AUCUN seuil ne pouvait marcher : le releve reel de Polar_Tundra est
	 * PLUS CHAUD (-8,4 C) que celui de Subarctic-Severe_Winter (-11,6 C) tout
	 * en etant de la toundra. Mesure du degat : toundra a 16,1 % des terres
	 * pour 8 attendus, taiga a 10,2 pour 10.
	 */
	float TreeLineWarmestMonthC = 10.0f;

	/**
	 * Cumul annuel a partir duquel une terre sans arbres en gagne, une fois la
	 * limite des arbres franchie, en millimetres.
	 *
	 * C'est le seuil qui separait deja la toundra de la taiga dans la bande de
	 * -5 a 5 degres ; il devient explicite parce qu'il ne depend plus de la
	 * bande ou l'on se trouve.
	 */
	float TaigaMinPrecipMm = 350.0f;

	/**
	 * Moyenne annuelle sous laquelle un desert est FROID, en degres.
	 *
	 * 18 degres est la frontiere k/h de Koppen, et elle est sourcee. Le
	 * diagramme seul ne pouvait pas trancher : le releve reel du Cold_Desert
	 * est a +17 degres de moyenne annuelle -- un BWk se definit par son HIVER,
	 * pas par son annee -- et tombait donc dans la bande chaude. Mesure du
	 * degat : desert froid a 0,31 % des terres, dont 0,00 % dans les deux
	 * bandes les plus froides, ou il etait pourtant declare.
	 */
	float ColdDesertMaxTempC = 18.0f;

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

	/**
	 * Part maximale de pluie estivale pour qu'un climat soit MEDITERRANEEN.
	 *
	 * Koppen definit le groupe Cs par un ete sec : le mois d'ete le plus sec
	 * recoit moins du tiers du mois d'hiver le plus arrose. Ramene a deux
	 * semestres, cela donne une part estivale nettement sous la moitie.
	 */
	float MediterraneanSummerFracMax = 0.38f;

	/** Bornes du climat mediterraneen, calees sur les trois releves reels. */
	float MediterraneanMinTempC = 6.0f;
	float MediterraneanMaxTempC = 20.0f;
	float MediterraneanMinPrecipMm = 300.0f;
	float MediterraneanMaxPrecipMm = 1000.0f;

	/**
	 * Part de pluie estivale par LIGNE de la grille, dans [0..1].
	 *
	 * Precalculee ici plutot que recalculee par cellule : elle ne depend que de
	 * la latitude. Et elle est calculee par WorldseedClimate, pas recopiee --
	 * si la circulation change un jour, la saisonnalite doit changer avec elle.
	 */
	TArray<float> SummerRainFracByRow;

	static FWorldseedBiomeRules FromRules(const UWorldseedRules& Rules,
		const FWorldseedGeometry& Geo);
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

	/**
	 * Roche a nu : la pente depasse l'angle de tenue, plus rien n'y reste.
	 *
	 * C'EST UN SUBSTRAT, PAS UN BIOME, et c'est tout l'objet de ce second axe.
	 * Un versant a 60 degres en foret tropicale reste climatiquement de la
	 * foret tropicale ; ce qui change, c'est ce qu'on a sous les pieds. Tant
	 * que "roche nue" occupait l'index des biomes, la carte perdait le climat
	 * de la paroi -- impossible d'y border la vegetation de la foret qui
	 * l'entoure, exactement le probleme que l'eau avait avant elle.
	 */
	Rock = 4,

	/** Estran : bande littorale. Meme raisonnement -- une forme, pas un climat. */
	Beach = 5,

	Count = 6
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

	/**
	 * Le biome dont il faut prendre l'APPARENCE pour une cellule.
	 *
	 * POURQUOI CETTE FONCTION EXISTE. En sortant la roche et la plage de l'axe
	 * des biomes, on gagne le climat qui regne sous la paroi -- mais le sol,
	 * lui, doit continuer a ressembler a de la roche et non a la foret qui
	 * l'entoure. C'est le seul point ou les deux axes se rejoignent : le
	 * SUBSTRAT decide de la matiere, le BIOME de tout le reste.
	 *
	 * Elle rend exactement l'identifiant d'avant la separation, donc le rendu
	 * ne bouge pas d'un pixel au passage a deux axes. C'etait la condition du
	 * changement : on ajoute de l'information, on ne retouche pas l'image.
	 */
	WORLDSEED_API EWorldseedBiome AppearanceBiome(uint8 BiomeIndex, uint8 Cover);

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
