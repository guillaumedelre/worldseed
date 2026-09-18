// Worldseed - acces aux acteurs Ultra Dynamic Sky, par reflexion.

#pragma once

#include "CoreMinimal.h"

/**
 * Le seul endroit du projet qui connaisse Ultra Dynamic Sky.
 *
 * POURQUOI PAR REFLEXION, ET NON PAR INCLUSION. UDS est un contenu payant,
 * ecrit en Blueprint et absent du depot : une dependance de compilation vers
 * lui empecherait le projet de se construire sans le pack. La reflexion degrade
 * proprement — si le pack n'est pas la, ou s'il renomme une variable, on
 * renvoie faux et le jeu continue sans ciel pilote.
 *
 * TOUTE LA FRAGILITE DU COUPLAGE EST DONC CONCENTREE ICI. Un changement de nom
 * cote UDS se repare dans ce fichier, et nulle part ailleurs.
 */
struct WORLDSEED_API FWorldseedUdsBridge
{
	/** Unite de temperature attendue par UDS. */
	enum class ETemperatureScale : uint8 { Fahrenheit, Celsius };

	/** Cherche les acteurs UDS du monde. Faux si le niveau n'en contient aucun. */
	bool Resolve(UWorld* World);

	bool IsValid() const { return SkyActor.IsValid() || WeatherActor.IsValid(); }

	/** Nom des classes trouvees, pour les journaux. */
	FString Describe() const;

	// --- lecture -------------------------------------------------------------

	/**
	 * Position dans l'annee, de 0 (coeur de l'hiver) a 1.
	 *
	 * Faux si UDS n'expose pas de saison ; l'appelant garde alors sa valeur.
	 */
	bool ReadSeasonPhase(float& OutPhase) const;

	// --- ecriture ------------------------------------------------------------

	bool WriteLatLon(float LatitudeDeg, float LongitudeDeg) const;

	/** Ecrit un nombre sur celui des deux acteurs qui possede la variable. */
	bool WriteNumber(FName PropertyName, double Value) const;

	/** Ecrit un couple (minimum, maximum), forme des plages de temperature. */
	bool WriteRange(FName PropertyName, const FVector2D& Value) const;

	ETemperatureScale TemperatureScale = ETemperatureScale::Fahrenheit;

	/** Prefixes de classe cherches dans le niveau. */
	FName SkyClassPrefix = TEXT("Ultra_Dynamic_Sky");
	FName WeatherClassPrefix = TEXT("Ultra_Dynamic_Weather");

	/** Noms des variables d'UDS, exposes au cas ou le pack les renommerait. */
	FName LatitudeProperty = TEXT("Latitude");
	FName LongitudeProperty = TEXT("Longitude");
	FName SeasonProperty = TEXT("Season");
	FName TemperatureScaleProperty = TEXT("Temperature Scale");

private:
	TWeakObjectPtr<AActor> SkyActor;
	TWeakObjectPtr<AActor> WeatherActor;

	/**
	 * Nombre de saisons couvertes par la variable Season d'UDS.
	 *
	 * Mesure plutot que supposee : voir Resolve.
	 */
	float SeasonPeriod = 12.0f;
};
