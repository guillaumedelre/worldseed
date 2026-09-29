// Worldseed - pilotage du ciel depuis le climat local.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Procedural/WorldseedClimatePreset.h"
#include "Procedural/WorldseedUdsBridge.h"
#include "Procedural/WorldseedWeatherState.h"

#include "WorldseedSkyDriverComponent.generated.h"

/**
 * Fait suivre au ciel le climat sous les pieds du joueur.
 *
 * IL NE SAIT PAS OU LE CLIMAT EST LU. Son proprietaire lui pousse un
 * echantillon ; lui s'occupe du prereglage, du fondu et de l'ecriture dans
 * Ultra Dynamic Sky. Ce decoupage permet au terrain de rester un terrain, et a
 * ce composant d'etre pose sur n'importe quel acteur sachant echantillonner un
 * climat.
 */
UCLASS(ClassGroup = (Worldseed), meta = (BlueprintSpawnableComponent))
class WORLDSEED_API UWorldseedSkyDriverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWorldseedSkyDriverComponent();

	/**
	 * Place le soleil selon la latitude du joueur.
	 *
	 * Le monde a de VRAIES latitudes : un ciel qui ferait lever le soleil au
	 * meme endroit partout contredirait le climat qu'on vient de calculer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Ciel")
	bool bDriveSunPosition = true;

	/** Fait suivre a la meteo le climat du lieu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Ciel")
	bool bDriveWeather = true;

	/** Duree d'un cycle meteo complet, en secondes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Ciel",
		meta = (ClampMin = "10.0", EditCondition = "bDriveWeather"))
	float WeatherPeriodS = 180.0f;

	/** Constante de temps du fondu entre deux climats, en secondes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Ciel",
		meta = (ClampMin = "0.1", EditCondition = "bDriveWeather"))
	float BlendSeconds = 12.0f;

	/** Affiche le releve meteo a l'ecran. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Ciel")
	bool bShowReadout = true;

	/**
	 * Position dans l'annee quand UDS n'en expose pas : 0 hiver, 0,5 ete.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Ciel",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FallbackSeasonPhase = 0.5f;

	/**
	 * Pousse un echantillon de climat et met le ciel a jour.
	 *
	 * LongitudeDeg suit la convention geographique, de -180 a +180.
	 * DeltaSeconds est l'ecart depuis le dernier appel : il regle le fondu.
	 */
	void Drive(const FWorldseedClimateSample& Sample, float LongitudeDeg,
		float AltitudeM, float LatSpanDeg, int32 Seed, float DeltaSeconds);

	/** Vrai si un ciel a ete trouve dans le niveau. */
	bool HasSky() const { return Bridge.IsValid(); }

private:
	/** Ecrit l'etat courant dans UDS. */
	void PushWeather() const;

	FWorldseedUdsBridge Bridge;

	/** Regles de la section "uds", lues une fois. */
	FWorldseedClimatePresetRules PresetRules;
	bool bPresetRulesLoaded = false;

	/** Etat courant, qui tend vers la cible climatique. */
	FWorldseedWeather Current;
	bool bWeatherStarted = false;

	/**
	 * Le controle des noms a-t-il ete fait ?
	 *
	 * `mutable` parce que `PushWeather` est const : elle ne change rien a l'etat
	 * du pilote, seulement a celui du ciel. Le controle, lui, doit se souvenir
	 * qu'il a eu lieu -- sans quoi il crierait deux fois par seconde.
	 */
	mutable bool bNomsVerifies = false;

	/**
	 * Arme l'horloge d'UDS et pose la duree du jour et de la nuit.
	 *
	 * ELLE ETAIT ARRETEE, et c'etait le defaut dominant de la meteo : la phase
	 * de l'annee est LUE dans Ultra Dynamic Sky, qui ne la fait avancer que si
	 * son « Animate Time of Day » est arme. `BP_WorldseedClimat` s'en chargeait
	 * avant que la generation ne passe en C++ ; le reglage est parti avec lui.
	 *
	 * ELLE N'ARME PAS SOUS `-WorldseedCielClair` : ce drapeau existe pour qu'un
	 * A/B par lancements successifs ait la meme lumiere des deux cotes, et
	 * `AWorldseedTerrain::CielDInspection` fige l'horloge au BeginPlay pour
	 * cela. Cette fonction la reposait a vrai quelques secondes plus tard,
	 * `OnRep_` compris, donc la garde etait MORTE depuis le 28 septembre 2026.
	 */
	void ArmerHorloge(const class UWorldseedRules& Rules);

	/**
	 * Pose l'heure de depart demandee par `-WorldseedHeure=`.
	 *
	 * SEPAREE DE L'ARMEMENT A DESSEIN. `ArmerHorloge` faisait deux choses, et
	 * `-WorldseedCielClair` n'en concerne qu'une : on veut une horloge FIGEE a
	 * une heure CHOISIE, pour aller photographier ce qui n'arrive que la nuit.
	 * Une sortie precoce dans `ArmerHorloge` aurait casse le cadrage horaire de
	 * tout le harnais photo, qui emploie les deux drapeaux ensemble.
	 */
	void PoserHeureDeDepart();

	/** Heure relevee au moment de l'armement, ou -1 si l'horloge n'est pas armee. */
	double HeureALArmement = -1.0;

	float TempsDepuisArmementS = 0.0f;
	bool bAvanceVerifiee = false;

	/**
	 * Derniere latitude transmise au ciel.
	 *
	 * Un demi-degre vaut une cinquantaine de kilometres : en deca, la course du
	 * soleil ne bouge pas assez pour se voir, et reecrire a chaque pas du joueur
	 * ne ferait que du bruit.
	 */
	float LastSunLatitudeDeg = TNumericLimits<float>::Max();

	/** Faux des qu'on a constate qu'il n'y a rien a piloter. */
	bool bSearchedForSky = false;

	/** La surcharge de periode ne se lit qu une fois : la ligne de commande ne change pas. */
	bool bPeriodeLue = false;

	/** Temoin d'orage force par `-WorldseedOrageForce=` ; negatif = inactif. */
	bool bTemoinLu = false;
	float TemoinOrage = -1.0f;
};
