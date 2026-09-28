// Worldseed - l'apparence d'un sommet de terrain : poids, couleur, teinte.

#pragma once

#include "CoreMinimal.h"

#include "Procedural/WorldseedWorldData.h"


/** Ce que le terrain doit ecrire dans chaque sommet, selon le mode courant. */
struct FWorldseedAppearance
{
	bool bTexturePack = false;
	bool bColourByBiome = false;
	bool bHasClimate = false;
	bool bHasCover = false;

	/**
	 * Peindre en MER tout ce qui est sous le niveau zero.
	 *
	 * RESERVE AU SOL DE FOND, et il ne faut surtout pas l'armer ailleurs.
	 * Sous les pieds du joueur, on VOIT le fond a travers l'eau -- c'est le
	 * degrade turquoise du rivage -- et peindre ce fond en bleu opaque
	 * detruirait precisement ce que la nappe du plugin rend bien.
	 *
	 * Le sol de fond, lui, n'est jamais sous de l'eau RENDUE : la nappe
	 * glissante du plugin ne couvre qu'une fenetre autour du joueur, et
	 * au-dela il n'y a rien. Le plateau cotier s'y dessinait donc A SEC,
	 * peint en plage par son biome -- d'ou un trait DROIT en travers du
	 * paysage la ou la fenetre s'arrete. Peint en mer, ce qui est derriere
	 * la couture a la couleur de ce qui est devant, et la couture cesse de
	 * se voir.
	 */
	bool bMerOpaque = false;

	/**
	 * Peindre cette meme zone en MAGENTA, pour la voir.
	 *
	 * POURQUOI UN TEMOIN DE COULEUR PLUTOT QU'UN A/B. Deux lancements de ce
	 * jeu n'ont PAS le meme eclairage -- l'horloge d'UDS tourne, et trente
	 * secondes d'ecart au chargement font douze minutes de jeu. Mesure : sur
	 * un A/B au meme point et au meme cap, la crete rocheuse temoin, que le
	 * traitement ne peut pas toucher, a bouge de 101 sur 765 quand la zone
	 * testee bougeait de 15. Le bruit depassait le signal d'un facteur sept.
	 *
	 * Une couleur franche ne se compare a rien : elle est la ou elle n'est
	 * pas. C'est le seul controle qui dise, sans temoin et sans A/B, si le
	 * chemin s'execute ET si son resultat atteint l'ecran.
	 */
	bool bMerTemoin = false;
};

/**
 * Les seuils qui decident de ce qu-un sommet porte.
 *
 * ILS VIENNENT DE L'ACTEUR DE TERRAIN, mais ils ne lui appartiennent pas : ce
 * sont les seuils de SURFACE -- a partir de quelle pente la roche perce, a
 * partir de quelle temperature la neige tient, ce qu'on appelle aride et ce
 * qu'on appelle luxuriant. Les nommer ici permet au calcul de quitter un
 * acteur de deux mille lignes sans emporter le reste avec lui.
 */
struct WORLDSEED_API FWorldseedSurfaceRegles
{
	/** Pente a partir de laquelle la roche commence a percer, en degres. */
	float RockSlopeStartDeg = 26.0f;

	/** Pente a partir de laquelle il n'y a plus que de la roche. */
	float RockSlopeFullDeg = 42.0f;

	/** Altitude sous laquelle la plage remplace le sol du biome, en metres. */
	float BeachTopM = 12.0f;

	/** Temperature a laquelle la neige commence a tenir, en degres. */
	float SnowTempC = -2.0f;

	/** Temperature a laquelle elle couvre tout. */
	float SnowTempFullC = -8.0f;

	/** Pluie annuelle sous laquelle on est aride, en millimetres. */
	float AridMm = 180.0f;

	/** Pluie annuelle au-dela de laquelle on est luxuriant. */
	float LushMm = 900.0f;

	/** Force de la teinte de biome sur la texture, dans [0..1]. */
	float CoverTint = 0.55f;
};

/**
 * CE CALCUL A QUITTE `AWorldseedTerrain`, ET IL A FALLU SUPPRIMER UN MAILLEUR
 * POUR QUE CE SOIT POSSIBLE.
 *
 * Il avait DEUX consommateurs -- le mailleur en carte d'altitude et le sol de
 * fond -- et l'en-tete de l'acteur le disait : « partagee entre les chunks
 * detailles et le sol de fond : deux versions de cette regle finiraient par
 * diverger, et la difference se verrait exactement la ou les deux maillages se
 * rencontrent ». C'etait juste, et cela le CLOUAIT sur place : aucun des deux
 * consommateurs ne pouvait partir sans recopier la regle.
 *
 * Le mailleur legataire supprime, il n'en reste qu'un. Le calcul peut donc
 * vivre a part, ce qui le rend au passage atteignable depuis un test.
 *
 * ATTENTION, IL EXISTE UN SECOND CALCUL D'APPARENCE DANS CE DEPOT :
 * `AWorldseedVoxelTerrain::PaintVertices`. Il ne fait PAS la meme chose -- il
 * peint en trois dimensions et connait la roche, ce que celui-ci ignore -- donc
 * ce n'est pas une duplication a resorber. Mais les deux se rencontrent la ou
 * le terrain proche rejoint l'horizon, et une divergence s'y verrait.
 */
namespace WorldseedApparence
{
	/**
	 * Ce qu'un sommet doit porter : couleur, et teinte dans les deux UV.
	 *
	 * `Cell` indexe la grille 2D du monde, `HeightM` est l'altitude au sommet
	 * et `Normal` sa normale -- c'est d'elle que vient la pente, lue sur sa
	 * composante verticale, sans arc cosinus.
	 */
	WORLDSEED_API void Sommet(const FWorldseedWorldData& Monde,
		const FWorldseedSurfaceRegles& Regles,
		int32 Cell, float HeightM, const FVector& Normal,
		const FWorldseedAppearance& Mode,
		FLinearColor& OutColour, FVector2D& OutTintRG, FVector2D& OutTintB,
		float& OutNeige);

	/**
	 * UNE TEINTE DE BIOME RAMENEE A LUMINANCE UN, POUR QUE LE MATERIAU SE
	 * CONTENTE D'UN MULTIPLIE.
	 *
	 * Sans cette normalisation, multiplier la texture par la couleur de
	 * reference d'un biome -- qui vaut autour de 0,4 -- assombrirait le sol :
	 * une savane aurait une herbe plus sombre qu'une prairie, alors qu'elle
	 * doit seulement etre plus JAUNE. Le calcul delicat se lit et se verifie
	 * ici, en C++, pas dans un graphe de shader.
	 *
	 * ELLE EST PUBLIQUE PARCE QU'ELLE A DEUX APPELANTS, et que le depot
	 * interdit de recopier une formule dans deux fichiers : la nappe
	 * d'horizon la lit par `Sommet`, le terrain VOXEL par `WorldseedPeinture`.
	 * Deux copies divergeraient, et la difference se verrait exactement la ou
	 * les deux maillages se rencontrent -- le defaut que ce depot a deja paye
	 * en gardant deux calculs d'apparence.
	 *
	 * La borne haute de 2,5 n'est pas cosmetique : un biome tres sombre et
	 * tres sature donnerait sinon un facteur enorme sur un seul canal, donc un
	 * sol fluo.
	 */
	WORLDSEED_API FLinearColor TeinteNormalisee(const FLinearColor& Teinte);

	/**
	 * PART D'ESTRAN D'UN SOMMET, DANS [0..1] -- POUR QUE LA PLAGE N'AIT PAS LE
	 * SABLE DU DESERT.
	 *
	 * IL N'Y A QUE QUATRE MATIERES, ET C'EST STRUCTUREL : elles voyagent dans
	 * les quatre canaux RGBA de la couleur de sommet. Or la plage et le desert
	 * chaud portent EXACTEMENT les memes poids -- [0, 1, 0, 0], soit cent pour
	 * cent de la matiere aride -- et ne se distinguent aujourd'hui que par leur
	 * teinte. Echanger la texture de sable les changerait donc tous les deux,
	 * canyons compris.
	 *
	 * D'ou ce scalaire, transporte a part dans UV2.Y -- le seul canal libre du
	 * sommet, UV0 portant le placage, UV1 la teinte RG et UV2.X la teinte B.
	 * Le materiau y fond deux sables : celui du desert a zero, celui de la
	 * plage a un. La matiere reste unique, seule sa TEXTURE change.
	 *
	 * ELLE EST PUBLIQUE POUR LA MEME RAISON QUE `TeinteNormalisee` : deux
	 * appelants -- la nappe d'horizon et la peinture du terrain voxel -- et le
	 * depot interdit de recopier une formule dans deux fichiers. Une divergence
	 * se verrait exactement la ou les deux maillages se rencontrent.
	 *
	 * ELLE PREND LE BIOME D'APPARENCE, jamais l'index brut : l'estran est un
	 * SUBSTRAT depuis le 26 septembre 2026, et l'axe des biomes porte le climat
	 * meme sur une plage.
	 */
	WORLDSEED_API float PartEstran(EWorldseedBiome BiomeApparent);

	/**
	 * PART DE NEIGE PERMANENTE D'UN SOMMET, DANS [0..1].
	 *
	 * LE GLOBE ET LE SOL NE PEIGNAIENT PAS LA MEME CHOSE, et c'est le defaut
	 * qu'elle corrige. Le registre donne a la calotte glaciaire une COULEUR
	 * blanche (242/246/250) que le globe emploie, et des MATIERES [0, 0, 1, 0]
	 * -- ordre herbe, aride, roche, mousse -- donc cent pour cent de ROCHE. En
	 * jeu la calotte rendait donc grise, et aucune teinte ne pouvait la sauver :
	 * `TeinteNormalisee` DIVISE par la luminance, si bien qu'un blanc sort
	 * neutre. C'est delibere -- la teinte transporte la chrominance, jamais la
	 * clarte -- donc un biome ne peut etre blanchi que par une MATIERE.
	 *
	 * ELLE NE PASSE PAS PAR UN CINQUIEME POIDS, et c'est ce qui evite de faire
	 * regenerer son monde a tout le monde : les quatre poids voyagent dans le
	 * RGBA du sommet, qui est plein, et en ajouter un cinquieme au registre
	 * changerait l'empreinte de `world_rules.json`. Elle voyage donc par UV3.Y,
	 * exactement comme `PartEstran` voyage par UV2.Y depuis le 26 septembre.
	 *
	 * OU ELLE ABOUTIT : l'entree `Offset Coverage` de DLWE_V3, la fonction de
	 * neige d'Ultra Dynamic Sky. Elle est SCALAIRE -- releve du 28 septembre
	 * 2026, et c'est ce fait qui a decide la route -- donc pilotable par
	 * sommet. Une seule chaine donne ainsi la calotte PERMANENTE et la neige
	 * METEO, plus l'accumulation et les traces de pas.
	 *
	 * ELLE PREND LE BIOME D'APPARENCE, comme sa soeur, et pour la meme raison.
	 */
	WORLDSEED_API float PartNeige(EWorldseedBiome BiomeApparent);
}
