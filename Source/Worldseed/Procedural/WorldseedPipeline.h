// Worldseed - chaine de generation du monde, pilotee par world_rules.json.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedClimate.h"
#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedFields.h"
#include "Procedural/WorldseedJob.h"
#include "Procedural/WorldseedRules.h"

/**
 * Point d'entree unique de la generation.
 *
 * La chaine, dans l'ordre : tectonique, climat, erosion, recalage du niveau
 * marin, climat une seconde fois, puis biomes. L'HYDROLOGIE N'EN FAIT PLUS
 * PARTIE — rivieres, cascades et lacs ont ete retires le 18 septembre 2026,
 * CLAUDE.md porte les mesures qui l'ont motive. Le relief n'est donc plus
 * retouche apres le cache : ce qui sort du cache est ce qui s'affiche.
 *
 * Toutes les valeurs viennent de world_rules.json, jamais de constantes C++ :
 * c'est la regle posee par la documentation du projet, et elle evite que les
 * deux moities du projet divergent a la premiere retouche.
 */
namespace WorldseedPipeline
{
	struct WORLDSEED_API FResult
	{
		/** Altitude en METRES, 0 = niveau de la mer. Indexe J * N + I. */
		TArray<float> ElevationM;

		/** Geometrie effective : NX = 2 NY, largeur = 2 hauteur. */
		FWorldseedGeometry Geometry;

		/** Decalage applique pour amener la mer a zero. */
		float SeaLevelShiftM = 0.0f;

		/** Part de terres emergees effectivement obtenue. */
		float LandRatio = 0.0f;

		/** Altitudes extremes, pour le rendu. */
		float MinElevationM = 0.0f;
		float MaxElevationM = 0.0f;

		/** Temperature, pluie, vents, continentalite. */
		FWorldseedClimateResult Climate;

		/** Vrai si l'etape climat a tourne. */
		bool bHasClimate = false;

		/** Les biomes, avec la pente qui a servi a les classer. */
		FWorldseedBiomeMap Biomes;

		/**
		 * Humidite du sol et ensoleillement, les deux champs CONTINUS.
		 *
		 * Ils sont produits a cote de la classification, jamais avant : rien en
		 * amont ne les relit. C'est deliberé — ce sont eux la source de verite
		 * pour la vegetation, et l'etiquette de biome n'est qu'un cache pose a
		 * cote pour lier des assets.
		 */
		FWorldseedGroundFields Ground;

		/** Vrai si le monde vient du cache disque plutot que d'un calcul. */
		bool bFromCache = false;

		bool IsValid() const { return Geometry.NX >= 2 && ElevationM.Num() == Geometry.CellCount(); }
	};

	/**
	 * Genere un monde. MapSizeMeters et Resolution surchargent les valeurs du
	 * fichier de regles : le joueur les choisit dans L_Menu, tout le reste
	 * — latitudes, plaques, part emergee, forcage polaire — reste pilote par
	 * world_rules.json.
	 */
	/**
	 * HeightMeters est la hauteur du monde, d'un pole a l'autre ; la largeur
	 * vaut le double. ResolutionY est le nombre de lignes ; les colonnes valent
	 * le double, pour que la carte ait la forme de la sphere.
	 */
	WORLDSEED_API bool Generate(int32 Seed, float HeightMeters, int32 ResolutionY,
		FResult& Out, FString& OutError, FWorldseedJob* Job = nullptr);

	/** Regles chargees, mises en cache. Nullptr si le fichier est introuvable. */
	WORLDSEED_API UWorldseedRules* GetRules(FString& OutError);

	/**
	 * Oublie les regles en cache, pour qu'elles soient relues du disque.
	 *
	 * POUR LE REGLAGE, PAS POUR LE JEU : il permet d'essayer une valeur de
	 * world_rules.json sans relancer l'editeur. En partie, les regles ne
	 * changent pas.
	 */
	WORLDSEED_API void ReloadRules();
}
