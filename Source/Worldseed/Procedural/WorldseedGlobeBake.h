// Worldseed - cuisson du monde en textures equirectangulaires pour le globe.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedRules.h"

class UTexture2D;

/** Les deux textures que le materiau du globe consomme. */
struct WORLDSEED_API FWorldseedGlobeTextures
{
	/** Couleur du sol, sans aucun eclairage. */
	TObjectPtr<UTexture2D> Albedo;

	/** Normale du terrain en repere tangent (est, nord, haut), encodee 0..1. */
	TObjectPtr<UTexture2D> Normal;

	bool IsValid() const { return Albedo != nullptr && Normal != nullptr; }
};

/** Reglages de la cuisson. */
struct WORLDSEED_API FWorldseedGlobeBakeSettings
{
	/** Altitude, en metres, au-dela de laquelle la teinte est neigeuse. */
	float SnowStartM = 250.0f;

	/** Profondeur, en metres, du bleu le plus sombre. */
	float DeepOceanM = -300.0f;

	/** Accentuation du relief. 1 = pentes reelles. */
	float ReliefStrength = 1.0f;

	/** Largeur de la texture. La hauteur vaut la moitie. */
	int32 Width = 2048;
};

/**
 * Transforme le monde en deux textures, UNE SEULE FOIS.
 *
 * POURQUOI CUIRE PLUTOT QUE RELIRE. L'ancien globe relisait le heightfield a
 * chaque image : cinq echantillonnages bilineaires par pixel, cinq millions de
 * lectures dispersees par image, puis un televersement complet de la texture.
 * Tout ce travail refaisait a l'identique, trente fois par seconde, un calcul
 * dont le RESULTAT NE CHANGE JAMAIS — seul le point de vue tourne.
 *
 * Cuit une fois, le monde devient deux textures que la carte graphique
 * echantillonne pour rien, et faire tourner le globe ne coute plus qu'un
 * parametre scalaire.
 *
 * CE QU'ON NE CUIT SURTOUT PAS : L'ECLAIRAGE. Une ombre peinte dans la texture
 * tournerait AVEC la planete, qui emporterait ainsi son propre terminateur —
 * l'inverse de ce qu'on voit d'un astre eclaire par une source fixe. On cuit
 * donc la couleur et la NORMALE, et le materiau refait le Lambert par pixel.
 */
namespace WorldseedGlobeBake
{
	/**
	 * Cuit le monde.
	 *
	 * BiomeIndex et Cover sont facultatifs : fournis, ils donnent au globe les
	 * teintes des dix-neuf biomes plutot qu'un simple degrade d'altitude.
	 * Outer porte les textures creees.
	 */
	WORLDSEED_API FWorldseedGlobeTextures Build(UObject* Outer,
		const TArray<float>& Heights, const TArray<uint8>& BiomeIndex,
		const TArray<uint8>& Cover, const FWorldseedGeometry& Geometry,
		const FWorldseedGlobeBakeSettings& Settings);
}
