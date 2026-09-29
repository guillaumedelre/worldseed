// Worldseed - ce qui RAMPE au ras du sol : le sable, et la neige.
#pragma once

#include "CoreMinimal.h"

#include "Procedural/WorldseedBiomes.h"

struct FWorldseedClimatePresetRules;
struct FWorldseedWeather;

/**
 * DE QUOI EST FAIT CE QUI COURT AU SOL.
 *
 * Deux matieres seulement, et c'est deliberement peu : ce qui se souleve, c'est
 * un grain LIBRE et SEC. Une prairie ne rampe pas, une roche non plus.
 */
enum class EWorldseedMatiereRampante : uint8
{
	Aucune,
	Sable,
	Neige,
};

/**
 * CE QUI RAMPE ICI, MAINTENANT.
 *
 * UNE SEULE MECANIQUE POUR DEUX MATIERES, et ce n'est pas une economie de code :
 * c'est le meme fait physique. La poudrerie -- le « ground blizzard » -- souleve
 * la neige DEJA AU SOL par le vent, sans qu'il neige ; la saltation souleve le
 * sable deja au sol, sans qu'il y ait tempete. Dans les deux cas c'est
 * `matiere disponible x arrachement par le vent`, et rien d'autre.
 *
 * LE PACK VA DANS LE MEME SENS. Le systeme Niagara qui sert ici s'appelle
 * `Dust`, mais le releve de ses 51 parametres utilisateur montre qu'il porte
 * aussi `Snow_Twirl`, `Splash Percentage` et `Rain Collision Channel` : ce n'est
 * pas un systeme de poussiere, c'est le systeme de particules meteo GENERIQUE
 * d'Ultra Dynamic Sky. Le rendre transportable ne demande donc pas de le
 * detourner, seulement de le regler.
 */
struct FWorldseedReptation
{
	EWorldseedMatiereRampante Matiere = EWorldseedMatiereRampante::Aucune;

	/** Combien il y a de matiere libre sous les pieds, de zero a un. */
	float Part = 0.0f;

	/**
	 * A quel point elle court, de zero a un.
	 *
	 * C'est `Part` multipliee par l'arrachement du vent : une plage de sable par
	 * temps calme ne rampe pas, et un vent de tempete sur de la roche non plus.
	 */
	float Intensite = 0.0f;

	/** La teinte des grains -- ocre pour le sable, blanche pour la neige. */
	FLinearColor Teinte = FLinearColor::White;

	/** Le cap du vent, en degres, pour orienter la course des grains. */
	float DirectionDeg = 0.0f;

	bool EstActive() const { return Intensite > 0.0f; }
};

/** Ce qui se regle, et qui vit dans `world_rules.json`. */
struct FWorldseedReptationRules
{
	/**
	 * LA PART D'ARIDE AU-DESSOUS DE LAQUELLE RIEN NE COURT.
	 *
	 * Le canal « aride » des poids de matiere dit a quel point le sol est nu et
	 * sableux : desert chaud et plage 1,00, desert froid 0,60, savane 0,55,
	 * steppe 0,50, forets 0,00. Une savane a du sable entre ses herbes, mais
	 * pas assez pour qu'il coure : le seuil est donc haut.
	 */
	float SablePartMin = 0.7f;

	/** Idem pour la neige, qui couvre plus franchement des qu'elle est la. */
	float NeigePartMin = 0.4f;

	/** Teintes des grains, en lineaire. */
	FLinearColor SableTeinte = FLinearColor(0.78f, 0.62f, 0.38f, 1.0f);
	FLinearColor NeigeTeinte = FLinearColor(0.92f, 0.94f, 0.98f, 1.0f);

	static WORLDSEED_API FWorldseedReptationRules FromRules(const class UWorldseedRules& Rules);
};

namespace WorldseedReptation
{
	/**
	 * CE QUI RAMPE SOUS LES PIEDS DU JOUEUR.
	 *
	 * ELLE NE TOUCHE NI AU MONDE NI AU MOTEUR : c'est une fonction pure, donc
	 * elle se teste sans instancier quoi que ce soit -- ce que ce depot n'a
	 * jamais pu faire pour le cablage du ciel, faute d'avoir separe la decision
	 * de son effet.
	 *
	 * ⚠ LA NEIGE PASSE AVANT LE SABLE, et l'ordre est le bon sens : quand il y
	 * a de la neige au sol, c'est elle qu'on voit courir, pas le sable qui est
	 * dessous.
	 */
	WORLDSEED_API FWorldseedReptation Evaluer(
		EWorldseedBiome BiomeApparent,
		EWorldseedCover Couverture,
		float ZCm, float NiveauMerCm,
		const FWorldseedWeather& Meteo,
		const FWorldseedClimatePresetRules& ReglesMeteo,
		const FWorldseedReptationRules& Regles);
}
