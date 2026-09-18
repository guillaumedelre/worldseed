// Worldseed - les champs CONTINUS du sol : humidite et ensoleillement.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedRules.h"

/**
 * Ce qui decrit le sol sans le classer.
 *
 * POURQUOI CE MODULE EXISTE, ET POURQUOI IL EST SEPARE DES BIOMES. Un biome est
 * une ETIQUETTE : un decoupage discret, fait pour le confort d'auteur, qui sert
 * a lier des assets — un preset climatique, une banque de sons, une table
 * d'apparition. Rien dans la nature ne calcule un biome. Ce qui se calcule, ce
 * sont des champs continus, et ce sont EUX la source de verite.
 *
 * Deux manquaient, et ce sont les deux que la litterature met en tete du
 * rapport credibilite sur cout apres le relief et la pluie :
 *
 *   - l'HUMIDITE DU SOL, qui explique ce que ni la pluie ni la pente ne disent
 *     seules : un fond de vallon est humide sous un climat sec, une crete est
 *     seche sous la pluie. C'est elle qui reverdit les talwegs, et elle a
 *     d'autant plus d'importance ici que l'hydrologie a ete retiree — sans elle
 *     rien ne distingue plus un fond de vallee d'un versant ;
 *
 *   - l'ENSOLEILLEMENT, c'est-a-dire l'adret et l'ubac. Deux versants d'une
 *     meme vallee, meme altitude, meme pluie, meme biome, et pourtant deux
 *     vegetations : c'est l'un des motifs les plus lisibles d'un vrai paysage
 *     de montagne, et le plus bon marche a obtenir.
 *
 * ILS NE REALIMENTENT RIEN. Ni le climat ni le relief ne les relisent : ils
 * sont produits en bout de chaine, a cote de la classification, pour etre
 * consommes par la vegetation. Les faire remonter reintroduirait les seuils
 * nets que toute cette architecture cherche a eviter.
 */
struct WORLDSEED_API FWorldseedGroundFields
{
	/**
	 * Humidite du sol, dans [0..1]. Indexee J * NX + I.
	 *
	 * Melange de la pluie recue et de l'indice d'humidite topographique.
	 */
	TArray<float> SoilMoisture01;

	/**
	 * Ensoleillement relatif au terrain PLAT de la meme latitude, dans [0..1].
	 *
	 * 0,5 vaut terrain plat. Au-dessus, l'adret ; en dessous, l'ubac, jusqu'a
	 * zero pour un versant que le soleil moyen n'atteint plus. Le choix de
	 * rapporter au plat local, et non a une valeur absolue, est deliberé : on
	 * veut la difference entre deux versants VOISINS, pas le fait qu'il y a
	 * plus de soleil au Sahara qu'au Groenland — ca, la temperature le dit
	 * deja.
	 */
	TArray<float> SunExposure01;

	bool IsValid(int32 CellCount) const
	{
		return SoilMoisture01.Num() == CellCount && SunExposure01.Num() == CellCount;
	}

	void Reset()
	{
		SoilMoisture01.Reset();
		SunExposure01.Reset();
	}
};

/** Section "ground" de world_rules.json. */
struct WORLDSEED_API FWorldseedGroundRules
{
	/**
	 * Cumul annuel, en millimetres, au-dessus duquel la pluie seule sature le
	 * sol. Sert a ramener les precipitations dans [0..1].
	 */
	float SoilMoistureRefMm = 1200.0f;

	/**
	 * Poids de la topographie dans l'humidite, dans [0..1].
	 *
	 * A zero, l'humidite du sol n'est que la pluie — c'est-a-dire l'etat
	 * d'avant ce module. A un, elle n'est que le relief, ce qui rendrait un
	 * talweg saharien aussi vert qu'un talweg breton. La valeur retenue dit
	 * combien le terrain corrige le climat.
	 */
	float SoilWetnessWeight = 0.40f;

	static FWorldseedGroundRules FromRules(const UWorldseedRules& Rules);
};

namespace WorldseedFields
{
	/**
	 * Calcule les deux champs depuis le relief final et la pluie.
	 *
	 * PrecipMm peut etre vide : l'humidite se reduit alors a sa part
	 * topographique, ce qui reste utilisable pour un apercu sans climat.
	 *
	 * Le calcul se fait sur le relief de SORTIE, jamais sur celui de la
	 * simulation : c'est la lecon la plus chere de ce depot. Le detail fractal
	 * est ajoute apres l'erosion, et tout ce qui est cale sur le relief d'avant
	 * se retrouve tantot enterre, tantot suspendu.
	 */
	WORLDSEED_API void Compute(const FWorldseedGeometry& Geometry,
		const TArray<float>& ElevationM, const TArray<float>& PrecipMm,
		const FWorldseedGroundRules& Rules, FWorldseedGroundFields& Out);
}
