// Worldseed - etat meteo instantane, depuis le climat et le temps qui passe.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedClimatePreset.h"

/** Ce qu'on ecrit dans UDS. Temperatures en Celsius : la conversion est au bord. */
struct WORLDSEED_API FWorldseedWeather
{
	// TOUT EST SUR L'ECHELLE D'UDS, ET CE N'ETAIT PAS LE CAS.
	//
	// Jusqu'au 28 septembre 2026, `Rain`, `Snow` et `Dust` etaient documentes
	// « 0..1 » quand `Fog` et `CloudCoverage`, dans la MEME structure, etaient
	// deja en unites d'UDS -- l'incoherence etait ecrite en toutes lettres, a
	// trois lignes d'intervalle. Or les prereglages LIVRES par le pack donnent
	// le bareme : Rain_Light 3, Rain 7, Rain_Thunderstorm 10 ; Snow_Light 3,
	// Snow 6, Snow_Blizzard 10 ; Sand_Dust_Storm 10. Notre averse la plus
	// violente valait donc 1,0, soit le TIERS de la bruine la plus legere que
	// le pack sache produire -- et c'est la raison pour laquelle on ne voyait
	// « jamais de pluie », bien avant toute question de frequence.
	float Rain = 0.0f;              // echelle UDS 0..10 (leger 3, fort 7, orage 10)
	float Snow = 0.0f;              // echelle UDS 0..10 (leger 3, normal 6, blizzard 10)
	float Fog = 1.0f;               // echelle UDS, 1 = normal
	float Dust = 0.0f;              // echelle UDS 0..10 (tempete 10)
	float CloudCoverage = 3.0f;     // echelle UDS, ~4 = ciel ordinaire

	/**
	 * L'ORAGE. Echelle UDS 0..10, et il accompagne la PLUIE, jamais la neige.
	 *
	 * Il n'etait pas pilote du tout : UDS ne l'arme que par ses TYPES de meteo
	 * tout faits, et nous ne selectionnons jamais de type -- nous posons des
	 * curseurs continus. La variable restait donc a zero en permanence, et
	 * aucun reglage de climat ne pouvait y changer quoi que ce soit.
	 */
	float Thunder = 0.0f;

	/**
	 * L'AURORE. Echelle UDS, ou 0,12 est le defaut du pack et 1 un beau rideau.
	 *
	 * ⚠ SON DEFAUT N'EST PAS ZERO. Laissee seule, UDS pose 0,12 PARTOUT, donc
	 * une aurore faible et permanente jusque sous les tropiques. La piloter
	 * corrige ce defaut-la autant qu'elle en ajoute une ou elle doit etre.
	 */
	float Aurora = 0.0f;

	float WindIntensity = 2.0f;
	float WindDirectionDeg = 180.0f;

	/** Plages jour/nuit par saison, en Celsius : (minimum, maximum). */
	FVector2D SeasonMinMaxC[FWorldseedClimatePreset::SeasonCount] = {
		FVector2D(2.0, 10.0), FVector2D(8.0, 18.0),
		FVector2D(16.0, 26.0), FVector2D(8.0, 18.0)
	};

	/** Nom du regime dominant, pour l'affichage. */
	FString DescribeRegime() const;
};

/** Ce qui gouverne la traduction, au-dela du prereglage climatique. */
struct WORLDSEED_API FWorldseedWeatherParams
{
	/** Duree d'un cycle meteo complet, en secondes. */
	float VariationPeriodS = 180.0f;

	/** Position dans l'annee : 0 au coeur de l'hiver, 1 un tour plus tard. */
	float SeasonPhase = 0.5f;

	/** Etendue de latitude de la carte, pour placer les ceintures de vent. */
	float LatSpanDeg = 180.0f;

	/** Graine du monde : le signal temporel en derive. */
	int32 Seed = 0;

	float TimeSeconds = 0.0f;
};

namespace WorldseedWeatherState
{
	/**
	 * Traduit un climat en meteo instantanee.
	 *
	 * LE POURCENTAGE DE CIEL COUVERT DU PREREGLAGE EST UNE FREQUENCE, pas une
	 * quantite : la part du temps ou le ciel est charge. Il fait donc un seuil
	 * naturel que le signal d'agitation doit franchir. Un climat couvert 93 % du
	 * temps le franchit presque toujours, un climat couvert 15 % presque jamais
	 * — et le desert garde ainsi son orage rare sans qu'on ait a l'inventer.
	 */
	WORLDSEED_API FWorldseedWeather Evaluate(const FWorldseedClimateSample& Sample,
		const FWorldseedClimatePresetRules& PresetRules,
		const FWorldseedWeatherParams& Params);

	/**
	 * Fondu vers une cible.
	 *
	 * Franchir une frontiere climatique ne doit pas commuter le ciel d'un coup :
	 * on tend vers la cible au lieu de s'y poser. Alpha est la part de chemin
	 * parcourue sur ce pas.
	 */
	WORLDSEED_API void BlendTowards(FWorldseedWeather& Current,
		const FWorldseedWeather& Target, float Alpha);

	/**
	 * LA CONSTANTE DE TEMPS DU FONDU, EN SECONDES REELLES.
	 *
	 * Elle vit ICI et non dans l'acteur parce que la SONDE doit rejouer le
	 * meme fondu que le jeu. Elle ne le faisait pas : la sonde lissait sur
	 * `pas / periode`, soit une periode entiere -- quinze fois plus fort --
	 * sous un commentaire qui annoncait « le meme fondu qu'en jeu ». Elle
	 * rendait donc 9 orages par an la ou le modele en designe 119, et ce
	 * chiffre etait une propriete de l'INSTRUMENT, pas du monde.
	 */
	constexpr float FonduDefautS = 12.0f;

	/**
	 * La part de chemin parcourue sur un pas, pour une constante de temps
	 * donnee. Exponentielle, donc INDEPENDANTE de la cadence d'appel : un
	 * fondu pose en « fraction par pas » changerait de vitesse avec le nombre
	 * d'images par seconde.
	 */
	WORLDSEED_API float AlphaDeFondu(float DeltaSeconds, float ConstanteS);
}
