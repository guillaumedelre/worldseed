// Worldseed - classer une cellule dans un climat de Koppen.

#pragma once

#include "CoreMinimal.h"

/**
 * POURQUOI CLASSER EN KOPPEN ALORS QU'ON A DEJA DES BIOMES.
 *
 * Le biome dit ce qui POUSSE, le climat de Koppen dit le TEMPS QU'IL FAIT --
 * et les deux ne sont pas a la meme resolution. `TemperateForest` recouvre
 * l'oceanique, deux continentaux et deux subtropicaux d'altitude : cinq
 * climats dont la meteo n'a rien de commun. C'est mesure, et c'est ce qui a
 * fait echouer une correction de couverture nuageuse par biome -- elle rendait
 * 15,9 points d'ecart la ou ne rien faire en rendait 13,5, le groupe ajoutant
 * sa propre variance a l'erreur.
 *
 * CE QUE LA CLASSIFICATION PERMET, ET QU'AUCUNE FORMULE N'ATTEINT. Nos
 * vingt-trois releves de stations reelles PORTENT une classe de Koppen chacun.
 * Classer une cellule, c'est donc pouvoir lui donner la meteo d'une vraie
 * station du meme climat -- saison par saison, avec son contraste propre --
 * au lieu de la deduire d'une courbe. Mesure de ce que cela vaut : 6,5 points
 * d'ecart attendu contre 12,7 pour la meilleure formule possible, et sous le
 * plancher de 6,74 qu'aucune formule saisonniere ne pouvait franchir.
 *
 * CE QUE CELA COUTE, ET IL FAUT LE SAVOIR : la qualite ne tient plus a un
 * calage mais a un TAUX DE CLASSEMENT. Une cellule mal classee recoit la meteo
 * d'un autre climat, et l'erreur vaut alors 18,7 points en moyenne -- jusqu'a
 * 66 entre une foret tropicale et un desert chaud. C'est pourquoi l'oracle
 * verifie que les vingt-trois releves se reclassent dans leur propre case.
 */
enum class EWorldseedKoppen : uint8
{
	Aucun = 0,

	// A -- tropicaux : le mois le plus froid au-dessus de 18 degres.
	Af,   // foret tropicale humide, aucun mois sec
	Am,   // mousson
	Aw,   // savane a hiver sec
	As,   // savane a ete sec

	// B -- arides : la pluie ne couvre pas ce que l'evaporation demande.
	BWh, BWk,   // deserts chaud et froid
	BSh, BSk,   // steppes chaude et froide

	// C -- temperes : mois le plus froid entre -3 et 18, le plus chaud au-dessus de 10.
	Csa, Csb, Csc,   // ete sec -- mediterraneens
	Cwa, Cwb,        // hiver sec
	Cfa, Cfb, Cfc,   // sans saison seche

	// D -- continentaux : mois le plus froid sous -3.
	Dfa, Dfb, Dfc, Dfd,

	// E -- polaires : le mois le plus chaud sous 10 degres.
	ET,   // toundra
	EF,   // glace permanente

	Count
};

/** Ce qu'il faut savoir d'un climat pour le classer. */
struct WORLDSEED_API FWorldseedKoppenEntree
{
	/** Moyenne du mois le plus froid et du plus chaud, en Celsius. */
	float TFroidC = 0.0f;
	float TChaudC = 0.0f;

	/** Cumul annuel, en millimetres. */
	float PrecipAnnuelMm = 0.0f;

	/** Pluie MENSUELLE de chaque saison : hiver, printemps, ete, automne. */
	float PluieSaisonMm[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

namespace WorldseedKoppen
{
	/**
	 * La classe d'un climat.
	 *
	 * ELLE TRAVAILLE SUR DES SAISONS, PAS SUR DES MOIS, et c'est la limite de
	 * cette implementation : Koppen se definit sur les extremes MENSUELS, dont
	 * une moyenne saisonniere adoucit les pointes. `TFroidC` sous-estime donc
	 * le vrai mois le plus froid, ce qui pousse des continentaux vers le groupe
	 * tempere -- defaut mesure sur `Hot_Summer_Continental`. Le facteur
	 * `PointeMensuelleC` corrige ce biais d'une valeur unique, calee sur les
	 * releves : mieux vaut une correction assumee qu'un biais silencieux.
	 */
	WORLDSEED_API EWorldseedKoppen Classer(const FWorldseedKoppenEntree& E,
		float PointeMensuelleC);

	/** Le code a deux ou trois lettres, pour les journaux et les bulletins. */
	WORLDSEED_API const TCHAR* Nom(EWorldseedKoppen K);

	/**
	 * Reconstruit les saisons d'une cellule depuis ce que la chaine calcule.
	 *
	 * NOTRE MONDE NE PORTE PAS DE MENSUELLES : il connait une temperature
	 * moyenne, une amplitude saisonniere, un cumul de pluie et la part qui
	 * tombe au semestre chaud. Cette fonction en tire les quatre saisons, avec
	 * exactement les memes conventions que le prereglage climatique -- l'ete a
	 * la moyenne plus la demi-amplitude, les equinoxes a la moyenne -- pour que
	 * les deux ne divergent pas.
	 */
	WORLDSEED_API FWorldseedKoppenEntree DepuisChamps(float TempMeanC,
		float PrecipAnnuelMm, float SeasonalAmpC, float SummerRainFrac,
		float ContrasteExposant);
}
