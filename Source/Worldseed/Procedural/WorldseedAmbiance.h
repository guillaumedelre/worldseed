// Worldseed - quel SON D'AMBIANCE convient a l'endroit ou l'on se trouve.

#pragma once

#include "CoreMinimal.h"

enum class EWorldseedBiome : uint8;
enum class EWorldseedCover : uint8;

/**
 * Les familles d'ambiance que ce monde sait jouer.
 *
 * POURQUOI SI PEU, ET C'EST UNE MESURE ET NON UN CHOIX. Ultra Dynamic Sky ne
 * livre qu'UNE source d'ambiance, `Forest_Example` -- oiseaux le jour,
 * insectes la nuit, vent dans les arbres selon le vent, le tout deja module par
 * l'heure et la meteo par le pack lui-meme. Ses deux sous-sources,
 * `Forest_Birds` et `Forest_TreeWind`, existent bien comme assets separes, mais
 * le releve du 29 septembre 2026 les donne en MONO et en QUAD : la
 * documentation du pack exige une sortie 5.1 pour qu'une ambiance recoive
 * l'occlusion et le panoramique directionnel, donc aucune des deux n'est
 * jouable seule. Composer une ambiance NOUVELLE demanderait d'ecrire un graphe
 * MetaSound, qui n'est pas scriptable -- meme mur que Niagara.
 *
 * CE QUI RESTE SCRIPTABLE EST DONC : jouer cette source, ou ne pas la jouer,
 * et a quel VOLUME -- un `UDS_Environment_Sound` ne porte que trois champs, une
 * source, une echelle de volume et des surcharges de parametre. C'est peu, et
 * c'est deja une vraie difference : on sort de la foret et les oiseaux
 * s'arretent.
 *
 * ET C'EST EXACTEMENT CE QUE LE PACK NE PEUT PAS SAVOIR. Il connait l'heure, la
 * meteo et le vent ; il ne connait pas le BIOME. C'est donc la seule chose que
 * nous ayons a lui apporter, et elle suffit a rendre le monde audible.
 */
enum class EWorldseedAmbiance : uint8
{
	/**
	 * Aucune ambiance.
	 *
	 * CE N'EST PAS UN TROU, C'EST LA REPONSE JUSTE : la seule source du pack
	 * est une foret. La jouer sur une calotte glaciaire, un desert de sable ou
	 * la haute mer ferait chanter des merles sur la banquise. Le pack documente
	 * cet etat -- « stop environment sounds by calling it with no environment
	 * sound asset selected » -- donc il est prevu, pas subi. Il reste du vent,
	 * de la pluie et du tonnerre : le silence n'est jamais total.
	 */
	Aucune = 0,

	/** Lisiere : savane, prairie, steppe. Des oiseaux, de loin, et peu. */
	Lisiere = 1,

	/** Foret claire : taiga, foret tropicale seche, marais, mediterraneen. */
	ForetClaire = 2,

	/** Foret dense : les forets humides, ou la source du pack est chez elle. */
	ForetDense = 3,

	Count = 4
};

/** Reglages lus dans `world_rules.json`, section `uds`. */
struct WORLDSEED_API FWorldseedAmbianceRegles
{
	/**
	 * Duree du fondu entre deux ambiances, en secondes.
	 *
	 * ELLE EST LONGUE A DESSEIN. Une frontiere de biome n'est pas une porte :
	 * le joueur la franchit en marchant, et un chant d'oiseau qui s'arreterait
	 * net trahirait la grille. Six secondes a six kilometres-heure font dix
	 * metres, ce qui est du meme ordre que la maille de la carte des biomes --
	 * donc la coupure se noie dans le pas.
	 */
	float FonduS = 6.0f;

	/** Chemins des trois assets d'ambiance, par famille. */
	FString CheminLisiere;
	FString CheminForetClaire;
	FString CheminForetDense;

	static FWorldseedAmbianceRegles FromRules(const class UWorldseedRules& Rules);

	/** Le chemin de la famille demandee, vide pour `Aucune`. */
	const FString& Chemin(EWorldseedAmbiance Famille) const;
};

namespace WorldseedAmbiance
{
	/**
	 * Quelle ambiance convient a ce point ?
	 *
	 * ON LIT LA COUVERTURE AVANT LE BIOME, et l'ordre compte : le biome
	 * CLIMATIQUE est defini partout, y compris sous la mer, ou il decrit la
	 * bande climatique de l'eau. Une foret tropicale bordant l'ocean declare
	 * donc « foret tropicale » a deux cents metres du rivage, et sans cette
	 * garde on entendrait les oiseaux en nageant. Ce depot a paye exactement
	 * cela sur la nappe RVT, qui annoncait « 8 388 608 terre / 0 mer » sur un
	 * monde a 71 % d'ocean.
	 */
	WORLDSEED_API EWorldseedAmbiance Choisir(EWorldseedBiome Biome, EWorldseedCover Couverture);

	/** Libelle court, pour les journaux et le bulletin. */
	WORLDSEED_API const TCHAR* Nom(EWorldseedAmbiance Famille);
}
