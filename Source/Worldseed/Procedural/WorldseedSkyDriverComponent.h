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
	 * Derniere latitude transmise au ciel.
	 *
	 * Un demi-degre vaut une cinquantaine de kilometres : en deca, la course du
	 * soleil ne bouge pas assez pour se voir, et reecrire a chaque pas du joueur
	 * ne ferait que du bruit.
	 */
	float LastSunLatitudeDeg = TNumericLimits<float>::Max();

	/** Faux des qu'on a constate qu'il n'y a rien a piloter. */
	bool bSearchedForSky = false;
};
