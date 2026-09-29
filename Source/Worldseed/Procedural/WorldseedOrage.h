// Worldseed - QUELLE tempete convient a ce climat, en cette saison.

#pragma once

#include "CoreMinimal.h"

enum class EWorldseedBiome : uint8;
enum class EWorldseedSeason : uint8;

/**
 * Les tempetes que ce monde sait faire passer.
 *
 * LE VIVIER EST CELUI DU PACK, RELEVE ET NON SUPPOSE : treize
 * `UDS_Weather_Settings` existent, dont huit decrivent un temps violent. Les
 * cinq autres -- `Clear_Skies`, `Partly_Cloudy`, `Cloudy`, `Overcast`,
 * `Foggy` -- ne sont pas des tempetes et n'ont rien a faire ici : une tempete
 * radiale qui traverse le monde en apportant un ciel degage serait une
 * contradiction.
 *
 * ET LES TROIS VARIANTES « LIGHT » SONT ECARTEES. `Rain_Light`, `Snow_Light` et
 * `Sand_Dust_Calm` decrivent un temps ORDINAIRE, que notre climat pose deja en
 * continu par les huit curseurs. Une tempete radiale est un EVENEMENT : elle
 * doit se distinguer du fond, sinon elle ne rapporte rien.
 */
enum class EWorldseedOrage : uint8
{
	/** `Rain_Thunderstorm` -- convectif : l'orage a cellule, avec ses eclairs. */
	Orage = 0,

	/** `Rain` -- pluie soutenue, sans appareil electrique. Le temps frontal. */
	Pluie = 1,

	/** `Snow_Blizzard` -- vent et neige : le temps polaire. */
	Blizzard = 2,

	/** `Snow` -- chute de neige franche, sans le vent du blizzard. */
	Neige = 3,

	/** `Sand_Dust_Storm` -- la tempete de sable, qui est un etat de VENT. */
	Sable = 4,

	/**
	 * Aucune tempete ne convient.
	 *
	 * CE N'EST PAS UN TROU, C'EST LA REPONSE JUSTE pour l'eau libre : une
	 * tempete radiale nait autour du JOUEUR, et le joueur ne nage pas. Si un
	 * jour il navigue, ce sera `Pluie` -- mais on ne pose pas aujourd'hui un
	 * comportement pour un cas qui n'existe pas.
	 */
	Aucune = 5,

	Count = 6
};

namespace WorldseedOrage
{
	/**
	 * QUELLE TEMPETE POUR CE BIOME, EN CETTE SAISON.
	 *
	 * POURQUOI CETTE FONCTION EXISTE, ET CE QU'ELLE REMPLACE. Le pack livre
	 * quatre tables de probabilite indexees par SAISON, et le releve du
	 * 29 septembre 2026 montre qu'elles ne portent qu'une entree chacune, a
	 * probabilite 1,0 : `Rain_Thunderstorm` au printemps, en ete et en automne,
	 * `Snow_Blizzard` en hiver. **Ce n'est donc pas un tirage au sort, c'est une
	 * regle purement SAISONNIERE** -- d'ou un blizzard en hiver a l'equateur, et
	 * un orage dans le desert.
	 *
	 * ON GARDE LA SAISON, ON AJOUTE LE CLIMAT. La structure a quatre tables du
	 * pack est bonne : c'est son contenu qui ignore le lieu. On remplit donc les
	 * quatre, avec le choix qui convient a CE biome dans CETTE saison.
	 *
	 * ⚠ LE BIOME EST CELUI DU JOUEUR, PAS CELUI DE LA NAISSANCE DE LA TEMPETE,
	 * et c'est une approximation qu'il faut dire. La tempete nait a seize
	 * kilometres, soit une demi-hauteur de monde : le climat y est parfois tout
	 * autre. Mais elle VIENT vers le joueur et l'atteindra -- c'est donc le
	 * temps qui va nous tomber dessus, et le climat du joueur est defendable
	 * comme reference. La corriger demanderait de connaitre le point de
	 * naissance AVANT qu'il soit tire, ce que le pack ne permet pas.
	 *
	 * ET C'EST UNE FONCTION PURE, comme la decision d'ambiance : la decision
	 * porte les pieges, pas son effet, donc elle se teste sans rien instancier.
	 */
	WORLDSEED_API EWorldseedOrage Choisir(EWorldseedBiome Biome,
		EWorldseedSeason Saison);

	/**
	 * Le nom de l'asset du pack, ou une chaine vide pour `Aucune`.
	 *
	 * SEPAREE DE LA DECISION A DESSEIN : le nom est un detail du PACK, qui peut
	 * changer de version en version, tandis que la decision est a NOUS. Les
	 * melanger ferait qu'un renommage chez l'editeur casserait la regle de
	 * climat.
	 */
	WORLDSEED_API const TCHAR* NomDAsset(EWorldseedOrage Orage);
}
