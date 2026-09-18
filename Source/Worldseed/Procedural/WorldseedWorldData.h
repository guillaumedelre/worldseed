// Worldseed - ce qu'un monde calcule transporte d'une etape a l'autre.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedHydrology.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedTexturePack.h"

/**
 * Le monde, tel qu'il voyage entre la generation, le cache disque, le menu et
 * le terrain.
 *
 * POURQUOI UNE STRUCTURE PLUTOT QUE DES PARAMETRES. Chaque grandeur ajoutee au
 * monde devait sinon l'etre a quatre signatures et a tous leurs appelants — et
 * l'hydrologie qui vient n'est meme pas une grille de plus : ce sont des
 * polylignes de rivieres et des contours de lacs, qui n'entrent dans aucun
 * TArray<float> supplementaire. Ici une grandeur s'ajoute a un seul endroit, et
 * les etapes qui ne la connaissent pas la transportent sans y toucher.
 */
struct WORLDSEED_API FWorldseedWorldData
{
	FWorldseedGeometry Geometry;
	int32 Seed = 0;

	/** Altitude en metres, 0 = niveau de la mer. Indexe J * NX + I. */
	TArray<float> ElevationM;

	/** Moyenne annuelle au sol, en degres. */
	TArray<float> TempC;

	/** Cumul annuel de precipitations, en millimetres. */
	TArray<float> PrecipMm;

	/**
	 * Ecart entre le mois le plus froid et le plus chaud, en degres.
	 *
	 * TRANSPORTE PLUTOT QUE RECALCULE. Sa formule depend de la CONTINENTALITE,
	 * que seul le modele climatique connait : la deviner a l'arrivee donnait
	 * des saisons uniformes alors qu'un coeur de continent doit geler l'hiver
	 * la ou une cote a la meme latitude reste douce.
	 */
	TArray<float> SeasonalAmpC;

	/**
	 * Continentalite : 0 au bord de mer, 1 loin des cotes.
	 *
	 * Comme l'amplitude, elle est TRANSPORTEE plutot que recalculee : elle
	 * demande une transformee de distance sur toute la grille, trop chere a
	 * l'arrivee, et c'est elle qui ouvre l'ecart jour/nuit des climats
	 * continentaux.
	 */
	TArray<float> Continentality;

	/**
	 * Rivieres, lacs et cascades.
	 *
	 * PRESENTE ICI MAIS JAMAIS SERIALISEE : WorldseedCache n'ecrit que les
	 * grilles nommees plus haut. L'hydrologie derive du relief et de la pluie,
	 * et se recalcule en quelques dizaines de millisecondes au chargement — bien
	 * moins cher que de faire entrer des polylignes dans un format de fichier
	 * qui ne connait que des nombres.
	 */
	FWorldseedHydrology Hydrology;

	/** Les 19 biomes. Comme l'hydrologie, recalcules plutot que serialises. */
	FWorldseedBiomeMap Biomes;

	/**
	 * Pack de textures choisi dans le menu.
	 *
	 * Ce n'est pas une donnee du monde mais un CHOIX D'AFFICHAGE : deux parties
	 * sur la meme graine produisent le meme relief et le meme climat, et seul
	 * l'habillage differe. Il voyage ici parce que c'est le menu qui le connait
	 * et le terrain qui l'applique.
	 */
	EWorldseedTexturePack TexturePack = EWorldseedTexturePack::BiomeColour;

	int32 CellCount() const { return Geometry.CellCount(); }

	bool HasClimate() const
	{
		const int32 N = CellCount();
		return N > 0 && TempC.Num() == N && PrecipMm.Num() == N;
	}

	bool IsValid() const
	{
		return Geometry.NX >= 2 && ElevationM.Num() == CellCount();
	}
};
