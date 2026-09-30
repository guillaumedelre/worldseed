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

	// --- tableaux d'objets, SUR UNE CIBLE QUELCONQUE -------------------------
	//
	// POURQUOI CES DEUX-LA SONT STATIQUES, quand tout le reste du pont vise les
	// deux acteurs. La liste blanche de DLWE n'est PAS portee par l'acteur du
	// ciel ni par celui de la meteo : elle vit sur
	// `UDS_DLWE_Interaction_Settings`, un objet de reglages que CHAQUE composant
	// `DLWE_Interaction` tient par instance. Mesure du 29 septembre 2026 : les
	// 925 variables du ciel et les 584 de la meteo ont ete enumerees avec un
	// motif large, et `Physical Materials which enable DLWE Interactions on
	// non-Landscapes` n'y figure pas. Le registre la donnait pourtant a declarer
	// sans dire ou -- une lecture de documentation prise pour un emplacement.
	//
	// La cible est donc un parametre, mais la reflexion reste ICI : c'est la
	// promesse de ce fichier, que toute la fragilite du couplage y soit
	// concentree.

	/**
	 * AJOUTER UN OBJET A UN TABLEAU D'OBJETS, SANS RIEN EFFACER.
	 *
	 * NON DESTRUCTIF PAR CONSTRUCTION, et pas seulement parce que le tableau se
	 * trouve vide aujourd'hui : on AJOUTE, et l'on ne fait rien si la valeur y
	 * est deja. Un pack qui peuplerait sa propre liste dans une version future
	 * nous trouverait donc inoffensifs.
	 *
	 * ET ELLE REFUSE PLUTOT QUE DE DEVINER. Le type interne est verifie contre
	 * la valeur proposee : pose sur un `TArray<double>` qui porterait le meme
	 * nom, une ecriture non verifiee corromprait la memoire. C'est la meme
	 * prudence que `ChangerAmbiance`, qui compte ses parametres avant
	 * d'empiler -- un pack qui change de forme nous trouve muets plutot que
	 * dangereux.
	 *
	 * `OutTaille`, s'il est fourni, recoit la taille du tableau APRES l'appel :
	 * c'est ce qui permet a l'appelant de journaliser un COMPTE, donc de
	 * distinguer « ajoute » de « deja la » de « refuse ».
	 */
	static bool AjouterAuTableauObjets(UObject* Cible, FName Propriete,
		UObject* Valeur, int32* OutTaille = nullptr);

	/**
	 * Relire un tableau d'objets. Faux si la propriete manque ou n'en est pas un.
	 *
	 * ELLE EXISTE PARCE QU'UNE ECRITURE SE RELIT. Ce depot a paye douze fois le
	 * fait qu'une ecriture par reflexion ne signale rien quand elle echoue.
	 */
	static bool LireTableauObjets(const UObject* Cible, FName Propriete,
		TArray<UObject*>& OutValeurs);

	/**
	 * Un pointeur d'objet SIMPLE, pas un tableau. Nullptr si la propriete
	 * manque, n'est pas un objet, ou vaut nul -- les trois cas sont
	 * indiscernables ici, et c'est l'appelant qui doit le dire dans son journal.
	 *
	 * ELLE EXISTE POUR `Interaction Settings`, que chaque composant
	 * `DLWE_Interaction` tient et qui porte la liste blanche. On le lit pour le
	 * DUPLIQUER : ecrire dans celui du pack toucherait un asset partage, non
	 * versionne, et le salirait dans l'editeur.
	 */
	static UObject* LireObjet(const UObject* Cible, FName Propriete);

	/** Poser un pointeur d'objet simple. Faux si la propriete manque, n'est pas
	 *  un objet, ou si la valeur n'est pas du type attendu -- on refuse plutot
	 *  que de deviner, comme pour les tableaux. */
	static bool EcrireObjet(UObject* Cible, FName Propriete, UObject* Valeur);

	/**
	 * Un nombre, sur une cible quelconque. Double, float ou int.
	 *
	 * ELLES EXISTENT POUR L'ENTONNOIR DES PAS. Savoir que deux composants sont
	 * poses ne dit pas pourquoi ils sont muets : il faut lire, SUR eux, la
	 * profondeur de neige qu'ils voient, s'ils dorment, et si leur source audio
	 * existe. Un compte de composants n'est pas une mesure d'effet.
	 */
	static bool LireNombreDe(const UObject* Cible, FName Propriete,
		double& OutValeur);

	/**
	 * Un couple (minimum, maximum), sur une cible quelconque.
	 *
	 * Le gestionnaire de temperature d'UDW est un COMPOSANT du Blueprint meteo,
	 * pas l'acteur : sa plage courante ne se lit donc pas par `ReadRange`, qui
	 * ne connait que les deux acteurs.
	 */
	static bool LirePlageDe(const UObject* Cible, FName Propriete,
		FVector2D& OutValeur);

	/** Un booleen, sur une cible quelconque. */
	static bool LireBooleenDe(const UObject* Cible, FName Propriete,
		bool& OutValeur);

	/**
	 * POSER UNE TABLE `objet -> flottant` A UNE SEULE ENTREE.
	 *
	 * ELLE EXISTE POUR LES QUATRE TABLES DE PROBABILITE DES TEMPETES RADIALES,
	 * `TMap<UDS_Weather_Settings, float>`, une par saison.
	 *
	 * POURQUOI ELLE REMPLACE, ALORS QUE LA REGLE DU DEPOT EST NON DESTRUCTIVE.
	 * La regle interdit d'effacer pour faire passer une ecriture, ou de
	 * remplacer un objet entier pour changer un champ. Ici le CONTENU de la
	 * table EST le reglage : « une entree a probabilite 1,0 » veut dire « cette
	 * tempete-la ». Ajouter une entree signifierait « l'une ou l'autre », donc
	 * introduirait le tirage au sort que ce chantier retire. Remplacer est donc
	 * l'operation juste, et non un contournement.
	 *
	 * ET ELLE REFUSE PLUTOT QUE DE DEVINER, comme ses voisines : la propriete
	 * doit etre une table, sa CLE un pointeur d'objet de la classe attendue, et
	 * sa VALEUR un flottant. Une table dont la valeur serait un objet
	 * accepterait un flottant a l'ecriture et corromprait la memoire.
	 *
	 * UNE CLE NULLE VIDE LA TABLE, et c'est un contrat EXPLICITE : l'appelant en
	 * a besoin pour dire « aucun choix ne convient ici », et laisser l'ancien
	 * contenu ferait passer un blizzard sur l'ocean.
	 */
	static bool PoserTableUneEntree(UObject* Cible, FName Propriete,
		UObject* Cle, float Valeur);

	/** Relire une table `objet -> flottant`. Faux si la propriete n'en est pas
	 *  une -- et l'appelant doit relire, une ecriture par reflexion ne signalant
	 *  rien quand elle echoue. */
	static bool LireTableObjetFlottant(const UObject* Cible, FName Propriete,
		TMap<UObject*, float>& OutTable);

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
	 * La meme chose sur une cible QUELCONQUE -- un composant, un objet de
	 * reglages.
	 *
	 * ELLE EXISTE POUR LE GESTIONNAIRE DE TEMPERATURE, qui est un composant du
	 * Blueprint meteo et non l'acteur : `CallFunction` ne le voit pas. Mesure du
	 * 30 septembre 2026 -- nos quatre plages saisonnieres etaient POSEES sur
	 * l'acteur et le gestionnaire tournait sur celles du pack, faute d'un
	 * recalcul. Rend faux si la fonction est absente ou prend un argument :
	 * muet plutot que dangereux.
	 */
	static bool AppelerSansArgumentSur(UObject* Cible, FName NomFonction);

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

	/**
	 * Relit ce couple. Sans elle, les quatre plages saisonnieres s'ecrivaient
	 * sans qu'on puisse dire si elles avaient remplace celles du pack.
	 */
	bool ReadRange(FName PropertyName, FVector2D& OutValue) const;

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
