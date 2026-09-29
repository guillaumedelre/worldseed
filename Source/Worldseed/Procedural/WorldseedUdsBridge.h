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

	/**
	 * L'acteur ciel lui-meme, pour ce que la reflexion ne sait pas atteindre.
	 *
	 * ELLE EXISTE POUR UN SEUL APPELANT : le releve de l'elevation du soleil,
	 * qui lit la rotation de la lumiere directionnelle plutot qu'une variable du
	 * pack. Un nom de variable change d'une version a l'autre -- ce depot a deja
	 * recopie un nom faux depuis un message du moteur -- alors que la rotation
	 * d'un composant est ce que la scene recoit vraiment.
	 */
	AActor* CielBrut() const { return SkyActor.Get(); }

	/** L'acteur meteo lui-meme, pour le releve des composants audio. */
	AActor* MeteoBrute() const { return WeatherActor.Get(); }

	// --- lecture -------------------------------------------------------------

	/**
	 * Position dans l'annee, de 0 (coeur de l'hiver) a 1.
	 *
	 * Faux si UDS n'expose pas de saison ; l'appelant garde alors sa valeur.
	 */
	bool ReadSeasonPhase(float& OutPhase) const;

	/**
	 * L'HORLOGE D'UDS TOURNE-T-ELLE ?
	 *
	 * CE N'EST PAS UN DETAIL D'AFFICHAGE : la phase de l'annee que le pilote
	 * emploie est LUE dans UDS, et UDS ne la fait avancer que si son
	 * `Animate Time of Day` est arme. Horloge arretee, la saison ne bouge
	 * jamais -- et tout le calage saisonnier des prereglages, quatre saisons
	 * par climat et inversion des hemispheres, devient decoratif.
	 *
	 * ON LIT SANS ECRIRE, a dessein. Armer l'horloge est un choix de jeu -- la
	 * duree d'une journee, celle d'une annee -- qui appartient a la carte et
	 * non au pilote du ciel.
	 */
	bool ReadClockRunning(bool& OutRunning) const;

	/** Lit une grandeur nommee sur l'un des deux acteurs, sans la modifier. */
	bool ReadNumber(FName PropertyName, double& OutValue) const;

	/**
	 * Lit un BOOLEEN nomme.
	 *
	 * ELLE EXISTE PARCE QUE `ReadNumber` NE SAIT PAS LE FAIRE, et que s'en
	 * servir quand meme produit un mensonge : elle rend faux pour une variable
	 * parfaitement presente, et le journal annonce « ILLISIBLE ». Ce depot a
	 * deja RETIRE une ligne pour cette raison exacte -- la relecture de
	 * `Simulate Real Sun`, le 29 septembre 2026 -- au lieu de la reparer. La
	 * reparation, la voici.
	 *
	 * UN DRAPEAU RELU NE PROUVE TOUJOURS RIEN SUR L'EFFET : il se relit a vrai
	 * des qu'on le pose. Ce qu'il prouve, c'est ce que le pack a par DEFAUT,
	 * avant qu'on y touche -- et c'est cela qu'on veut savoir.
	 */
	bool ReadBool(FName PropertyName, bool& OutValue) const;

	/** Pose un booleen nomme sur celui des deux acteurs qui le porte. */
	bool WriteBool(FName PropertyName, bool bValue) const;

	/** Appelle une fonction SANS ARGUMENT sur l'un des deux acteurs. */
	bool CallFunction(FName FunctionName) const;

	/**
	 * Change le SON D'AMBIANCE, avec un fondu.
	 *
	 * LA SEULE FONCTION A PARAMETRES DE CE PONT, et elle l'est par necessite :
	 * `Change Environment Sound` prend trois arguments -- l'asset, la duree du
	 * fondu, et s'il faut charger la source de facon asynchrone -- releves le
	 * 29 septembre 2026 par `list_functions`. La voie d'a cote -- ecrire la
	 * variable `Environment Sound` puis appeler `Start Up Environment Sound`,
	 * qui ne prend rien -- existe, mais la documentation du pack la reserve au
	 * BeginPlay : c'est `Change Environment Sound` qui sait enchainer deux
	 * ambiances sans coupure, et c'est exactement ce dont on a besoin quand le
	 * joueur sort de la foret.
	 *
	 * LA PILE SE CONSTRUIT PAR LES PROPRIETES DE LA FONCTION, jamais par une
	 * structure devinee : un decalage d'un octet corromprait la memoire, et
	 * c'est pour cela que `CallFunction` refusait jusqu'ici tout ce qui prend
	 * un argument. On APPARIE PAR TYPE -- un objet, un reel, un booleen -- et
	 * l'on refuse si la signature ne compte pas exactement ces trois-la : un
	 * pack qui en ajouterait un quatrieme nous trouverait muets plutot que
	 * dangereux.
	 *
	 * `NouveauSon` a nullptr ARRETE l'ambiance : c'est la voie que le pack
	 * documente pour cela, et c'est ce qu'on emploie dans un desert.
	 */
	bool ChangerAmbiance(UObject* NouveauSon, float FonduS, bool bChargementAsync) const;

	/**
	 * Nom du calendrier en vigueur, et sa longueur en jours.
	 *
	 * ELLE N'EST PAS SUR L'ACTEUR mais sur l'objet que porte sa variable
	 * `Calendar`, et elle y est CALCULEE au demarrage : dans l'asset au repos
	 * elle vaut zero, y compris pour le calendrier gregorien d'UDS qui
	 * fonctionne. Rend faux si aucun calendrier n'est assigne.
	 */
	bool ReadYearLength(double& OutDays, FString& OutName) const;


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
