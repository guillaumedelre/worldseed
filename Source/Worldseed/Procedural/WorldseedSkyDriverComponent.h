// Worldseed - pilotage du ciel depuis le climat local.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Procedural/WorldseedAmbiance.h"
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

	/**
	 * L'ETAT METEO REELLEMENT AFFICHE, fondu compris.
	 *
	 * IL FAUT LE VECU, PAS LA CIBLE : ce qui rampe au sol doit suivre ce que le
	 * joueur VOIT dans le ciel, donc l'etat apres lissage. Rendre la cible
	 * ferait courir le sable avant que le vent ne se leve a l'ecran, et ce
	 * depot a exactement ce precedent -- une sonde qui mesurait la cible
	 * annoncait neuf orages la ou le modele en designait cent dix-neuf.
	 */
	const FWorldseedWeather& MeteoCourante() const { return Current; }

	/** Les regles de la section « uds », telles qu'elles ont ete lues. */
	const FWorldseedClimatePresetRules& ReglesMeteo() const { return PresetRules; }

	/**
	 * Fait suivre au SON D'AMBIANCE le sol sous les pieds du joueur.
	 *
	 * SEPAREE DE `Drive` PARCE QU'ELLE NE LIT PAS LE MEME CHAMP. Le climat
	 * s'echantillonne en continu ; le biome et la couverture se lisent AU POINT
	 * dans la carte des biomes, et c'est le terrain qui sait le faire -- il le
	 * fait deja pour la reptation, au meme minuteur et au meme endroit. Lui
	 * demander le resultat coute une lecture de plus et aucun tick.
	 *
	 * ELLE NE FAIT RIEN TANT QUE LA FAMILLE NE CHANGE PAS : un appel deux fois
	 * par seconde qui relancerait le fondu a chaque passage rendrait l'ambiance
	 * inaudible, et `Change Environment Sound` n'est pas gratuit -- il charge
	 * une source.
	 */
	void PiloterAmbiance(EWorldseedBiome Biome, EWorldseedCover Couverture);

	/**
	 * QUELLE TEMPETE RADIALE PEUT PASSER ICI, d'apres le climat.
	 *
	 * CE QU'ELLE REMPLACE. Le pack livre quatre tables de probabilite indexees
	 * par SAISON, et le releve du 29 septembre 2026 montre qu'elles ne portent
	 * qu'une entree chacune a probabilite 1,0 : orage au printemps, en ete et en
	 * automne, blizzard en hiver. Ce n'est donc pas un tirage au sort mais une
	 * regle purement saisonniere, qui ignore le lieu -- un blizzard en hiver a
	 * l'equateur, un orage dans le desert.
	 *
	 * ON GARDE LA STRUCTURE, ON CHANGE LE CONTENU : les quatre tables sont
	 * remplies avec le choix qui convient a CE biome dans CETTE saison, par
	 * `WorldseedOrage::Choisir`. La saison continue donc de compter la ou elle
	 * doit -- neige l'hiver en foret temperee -- et cesse de compter la ou elle
	 * n'a pas de sens.
	 *
	 * IDEMPOTENTE PAR CONSTAT : appelee a chaque evaluation du biome, elle
	 * n'ecrit que si le choix a CHANGE. Quatre ecritures de table par trame
	 * seraient du gaspillage pur.
	 */
	void PiloterLesOrages(EWorldseedBiome Biome);

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

	/**
	 * Arme `Simulate Real Sun`, sans quoi la latitude ecrite n'est pas evaluee.
	 *
	 * TROISIEME FOIS DANS CE DEPOT. `Animate Time of Day` et `Use Auroras`
	 * avaient deja ce defaut : une valeur ecrite fidelement, relue a la bonne
	 * valeur, et qu'aucun interrupteur n'evalue. Sans cette simulation, UDS
	 * trace un arc solaire SIMPLIFIE dont l'elevation de midi est la meme a
	 * l'equateur qu'au cercle polaire.
	 */
	void ArmerLaSimulationSolaire(const class UWorldseedRules& Rules);

	/**
	 * Pose `Global Water Level`, que `Use UDS Water Level` attend.
	 *
	 * La fonction est DEJA armee sur l'acteur meteo -- c'est son defaut -- mais
	 * le niveau vaut -100 000 000, donc elle tourne sur une mer inatteignable.
	 */
	void PoserNiveauDeLEau(const class UWorldseedRules& Rules);

	/**
	 * Releve l'elevation du soleil, pour prouver que la simulation est vivante.
	 *
	 * ELLE NE LIT AUCUNE VARIABLE DU PACK, a dessein : elle prend la rotation
	 * de la lumiere directionnelle, qui est ce que la scene recoit vraiment.
	 * Un nom de variable peut changer d'une version du pack a l'autre, et ce
	 * depot a deja recopie un nom faux depuis un message du moteur.
	 *
	 * LE CONTROLE QUI TRANCHE EST L'ECART ENTRE DEUX LATITUDES, pas une valeur
	 * absolue : sans simulation, l'elevation de midi vaut `90 - Sun Pitch`
	 * PARTOUT, donc deux latitudes eloignees rendent le meme chiffre. C'est le
	 * signe que ce depot connait par coeur -- deux mesures identiques pour deux
	 * reglages differents.
	 */
	bool ElevationDuSoleil(float& OutDegres) const;

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

	/**
	 * Temoin de poussiere force par `-WorldseedPoussiereForce=` ; negatif =
	 * inactif.
	 *
	 * IL SERT AUSSI A SAVOIR SI « Dust » DOSE OU COMMUTE, et c'est sa premiere
	 * question. Les DEUX prereglages de sable du pack posent `Dust = 10` -- le
	 * calme comme la tempete -- et ne different que par le vent : il est donc
	 * possible que ce curseur soit un interrupteur deguise, et que l'intensite
	 * visuelle vienne entierement de `Wind Intensity`. Un balayage a 0, 2, 5 et
	 * 10 au meme point et a la meme heure tranche en quatre captures.
	 */
	float TemoinPoussiere = -1.0f;

	/**
	 * Temoin de VENT force par `-WorldseedVentForce=` ; negatif = inactif.
	 *
	 * IL SERT A REGARDER CE QUI RAMPE AU SOL. Le temoin de poussiere force le
	 * voile ET le vent a la meme valeur : a 10 l'image est entierement noyee, et
	 * l'on ne peut rien juger de ce qui court au ras du sol. Celui-ci ne pose
	 * que le vent, la poussiere restant CALCULEE -- ce qui est l'etat reel d'un
	 * jour venteux, ou le sable court alors que le voile reste modere.
	 */
	float TemoinVent = -1.0f;

	/**
	 * Temoin de NEIGE force par `-WorldseedNeigeForce=` ; negatif = inactif.
	 *
	 * IL REPOND A UNE QUESTION DU PROPRIETAIRE : « quand on regarde le ciel on
	 * voit des grains en mouvement, la meme chose existe-t-elle pour la
	 * neige ? ». Elle se verifie au lieu de se supposer -- et c'est d'autant
	 * plus utile que les grains en question ne viennent PAS de notre composant
	 * de reptation, mais d'UDW lui-meme, qui instancie son propre systeme des
	 * que `Dust` depasse zero. Mesure : couper la reptation ne deplace pas
	 * l'image d'un demi-point de clarte, ciel compris.
	 *
	 * Il pose l'etat de `Snow_Blizzard`, releve dans les prereglages du pack.
	 */
	float TemoinNeige = -1.0f;

	/**
	 * Temoin de PLUIE force par `-WorldseedPluieForce=` ; negatif = inactif.
	 *
	 * IL SERT A REGARDER LES GOUTTES D'ECRAN, l'equivalent pluvieux du givre :
	 * `Screen Droplets`, qui arrivait eteint comme lui. Une fonctionnalite
	 * armee mais jamais VUE n'est pas validee -- c'est une regle du depot, et
	 * les quatre autres temoins existent pour la meme raison.
	 */
	float TemoinPluie = -1.0f;

	/**
	 * Temoin de BRUME force par `-WorldseedBrumeForce=` ; negatif = inactif.
	 *
	 * IL EXISTE PARCE QUE GUETTER UN EVENEMENT RARE NE SEPARE PAS LES DEUX
	 * CAUSES -- regle que ce depot a payee sur l'orage. Le bulletin du ciel
	 * donne la brume entre 0 et 11 % du temps selon le climat : attendre qu'elle
	 * vienne rendrait un zero compatible avec « le modele n'en demande pas » ET
	 * avec « il en demande et rien n'atteint l'ecran ».
	 *
	 * Il pose l'etat de `Foggy`, releve dans les prereglages du pack : le seul
	 * des treize a monter `Fog` au-dessus de 2, avec un vent a 1.
	 */
	float TemoinBrume = -1.0f;

	/** Le controle differe des effets d'ecran : armer n'est pas afficher. */
	bool bEcranVerifie = false;
	float TempsDepuisEcranS = 0.0f;

	/**
	 * Arme le son du pack et pose le volume maitre.
	 *
	 * ET POUR UNE FOIS LE PACK N'ARRIVE PAS ETEINT. Releve du 29 septembre
	 * 2026, instance ET defaut de classe : `Enable Weather Sound Effects` vaut
	 * VRAI, `Use Occlusion to Attenuate Sounds in Interiors` vaut VRAI, les six
	 * curseurs de volume valent 1, et les deux sources -- `UDS_Global_
	 * WeatherSounds` et `UDS_Directional_WeatherSounds` -- sont assignees. La
	 * pluie, le vent, le tonnerre et la poussiere s'entendent donc DEJA, pilotes
	 * par l'etat meteo que ce composant ecrit depuis le 28 septembre.
	 *
	 * CE QUI SUIT NE LES REARME PAS, IL LES MESURE ET LES MIXE. Reposer un
	 * drapeau deja vrai ne prouverait rien -- ce depot a cinq entrees sur cette
	 * classe d'erreur -- alors que relire ce que le pack a reellement retenu
	 * dit si la chaine tient.
	 */
	void ArmerLeSon(const class UWorldseedRules& Rules);

	/**
	 * QU'EST-CE QUI JOUE VRAIMENT ?
	 *
	 * UN DRAPEAU RELU NE PROUVE RIEN, et c'est la lecon la plus chere de ce
	 * depot. La seule mesure qui tranche pour du son est l'inventaire des
	 * COMPOSANTS AUDIO vivants : lesquels existent, lesquels jouent, et quelle
	 * source ils portent. On l'enumere sur le monde entier plutot que sur les
	 * deux acteurs d'UDS -- un inventaire tronque se lit exactement comme un
	 * inventaire complet, et le mixeur du pack peut vivre ailleurs.
	 *
	 * DIFFERE, comme le controle des effets d'ecran : les sources du pack sont
	 * instanciees au BeginPlay puis demarrees, et les lire au premier tick
	 * rendrait zero pour une bonne raison -- on conclurait de travers.
	 */
	void ReleverLeSon() const;

	/** Regles d'ambiance, lues une fois avec les autres. */
	FWorldseedAmbianceRegles AmbianceRegles;

	/**
	 * La famille d'ambiance en cours.
	 *
	 * `Count` VAUT « ON N'A ENCORE RIEN POSE », et ce n'est pas `Aucune` : la
	 * difference compte au tout premier passage, ou il faut bel et bien
	 * ARRETER l'ambiance si le joueur nait dans un desert. Sans cet etat
	 * distinct, le premier appel croirait n'avoir rien a faire.
	 */
	EWorldseedAmbiance AmbianceCourante = EWorldseedAmbiance::Count;

	// ------------------------------------------------- les pas dans la neige

	/**
	 * EQUIPER LE PION DE DEUX `DLWE_Interaction`, UN PAR PIED.
	 *
	 * POURQUOI ICI, ET C'EST UN ARBITRAGE DU PROPRIETAIRE (29 septembre 2026).
	 * Les douze assets sonores d'interaction de DLWE ne jouent que si un
	 * composant du pack est parente a un pied. Trois voies existaient : un
	 * script rejouable sur le Blueprint du pion, du code dans
	 * `AWorldseedCharacter`, ou ici. Les deux premieres ont un defaut chacune :
	 * le Blueprint du pion vit HORS du depot -- `.gitignore` n'ouvre que
	 * `Content/Worldseed/`, pour ne pas redistribuer des packs payants -- donc
	 * un composant pose a la main serait perdu au prochain clone ; et
	 * `AWorldseedCharacter` est du code a nous, mais y citer un chemin d'asset
	 * d'UDS briserait la promesse de ce projet, que le PONT soit le seul
	 * endroit qui connaisse le pack.
	 *
	 * Ce pilote, lui, connait UDS par construction : c'est sa raison d'etre.
	 *
	 * ET ELLE SE RAPPELLE A CHAQUE PASSAGE, A DESSEIN. Le pion n'existe pas
	 * forcement quand les autres `Armer*` tournent, et il est REMPLACE a chaque
	 * mort. Le garde-fou n'est donc pas un booleen mais le pion lui-meme :
	 * `PionEquipe` faible, compare au pion courant.
	 */
	void ArmerLesPasDlwe();

	/**
	 * Donner a UN composant des reglages a NOUS, portant notre physmat.
	 *
	 * ON DUPLIQUE, ON N'ECRIT PAS DANS CEUX DU PACK. `Interaction Settings`
	 * pointe par defaut sur `Standard_DLWE_Interaction_Settings`, un asset
	 * PARTAGE et non versionne : y ajouter notre materiau le salirait dans
	 * l'editeur et risquerait d'etre sauvegarde par accident. La copie est
	 * outree sur le composant, donc transitoire et privee.
	 */
	bool ReglerUnPasDlwe(class USceneComponent* Composant, UObject* Physmat) const;

	/**
	 * L'ENTONNOIR DES PAS : POURQUOI ILS SE TAISENT, QUAND ILS SE TAISENT.
	 *
	 * DEUX COMPOSANTS POSES NE SONT PAS DEUX SONS. Entre l'un et l'autre il y a
	 * quatre conditions, et un compte de composants n'en mesure aucune : le
	 * composant doit etre EVEILLE, son `Sound Enabled` arme, il doit VOIR de la
	 * neige sous le pied, et sa source audio doit exister. Les lire toutes rend
	 * un silence DIAGNOSTIQUABLE au lieu d'un silence.
	 *
	 * ET IL SE REPETE, parce qu'un son de pas est TRANSITOIRE : un instantane
	 * ne l'attrape que par chance, et le releve du son -- tire une fois a six
	 * secondes -- arrive avant que le banc n'ait commence a marcher. Quatre
	 * passages espaces couvrent la marche.
	 */
	void ReleverLesPasDlwe() const;

	/** Les deux composants poses, pour les relire. Faibles : le pion meurt. */
	TArray<TWeakObjectPtr<class USceneComponent>> PasDlwe;

	/** Temps cumule depuis l'armement, et nombre de releves deja faits. */
	float TempsDepuisPasS = 0.0f;
	int32 ReleveesDesPas = 0;

	/** Le pion deja equipe. FAIBLE : une mort du joueur doit re-armer. */
	TWeakObjectPtr<class APawn> PionEquipe;

	// --------------------------------------------- les tempetes radiales

	/**
	 * ARMER LES TEMPETES RADIALES, ET EN DECLENCHER UNE TOUT DE SUITE.
	 *
	 * ARME PAR `-WorldseedOrageRadial`, et rien autrement : c'est une PREMIERE
	 * LUEUR, pas un reglage du monde. Le mariage avec notre climat n'est pas
	 * tranche -- les quatre tables de probabilite du pack sont un tirage au sort
	 * saisonnier, c'est-a-dire le mecanisme que le proprietaire a ECARTE pour
	 * l'etat meteo global. On regarde d'abord, on arbitre ensuite.
	 *
	 * POURQUOI ON DECLENCHE A LA MAIN AU LIEU D'ATTENDRE. Releve du pack :
	 * `Radial Storm Wait Interval Range` vaut 1000 a 2000 SECONDES, et
	 * `First Wait Multiplier` 0,5 -- soit huit a dix-sept minutes avant la
	 * premiere. Un lancement de mesure ne verrait donc RIEN, et l'on
	 * conclurait a l'echec en ayant seulement mesure trop tot. `Spawn Radial
	 * Storm` ne prend aucun parametre, verifie : le pont sait l'appeler.
	 *
	 * ⚠ ET CETTE MESURE NE PEUT PAS ETRE APPARIEE. Le ciel d'inspection
	 * (`-WorldseedCielClair`), qui rend deux lancements comparables, COUPE les
	 * nuages volumetriques -- il detruirait le sujet. On regarde donc une seule
	 * image, et il faut le dire plutot que de laisser croire a un A/B.
	 */
	void ArmerLesOragesRadiaux();

	/**
	 * RECADRER LA GEOMETRIE DE LA TEMPETE EN FRACTIONS DU MONDE.
	 *
	 * LE PACK POSE DES METRIQUES FIGEES ET ELLES SONT HORS D'ECHELLE : rayon
	 * 13 km, naissance et mort a 25 km, sur un monde de 64 x 32 km. Un diametre
	 * de 26 km fait 81 % de la HAUTEUR du monde -- et le proprietaire l'a
	 * constate a l'oeil le 29 septembre 2026, a dix kilometres elle REMPLISSAIT
	 * LE CIEL. Une tempete censee rapporter le LIEU reproduisait donc le defaut
	 * qu'elle devait corriger.
	 *
	 * LES TROIS REGLAGES DEVIENNENT DES FRACTIONS DE LA HAUTEUR DU MONDE, la
	 * petite dimension etant celle qui contraint. Ils se surchargent par
	 * `-WorldseedOrageRayon=`, `-WorldseedOrageDistance=` et
	 * `-WorldseedOrageDispersion=` -- EN FRACTIONS, jamais en kilometres, sans
	 * quoi on reintroduirait ce qu'on vient de retirer.
	 *
	 * ET RIEN N'ENTRE DANS `world_rules.json` : son empreinte est un MD5 du
	 * fichier ENTIER, donc une virgule y invaliderait tous les mondes en cache
	 * et imposerait 210 a 260 s de regeneration -- pour un reglage cosmetique.
	 * Si ces fractions doivent devenir des regles, ce sera un changement
	 * delibere, avec son `BREAKING CHANGE`.
	 */
	void RecadrerLaGeometrieDesOrages() const;

	/**
	 * Compter les acteurs de tempete REELLEMENT presents.
	 *
	 * C'EST L'EFFET, PAS LE RETOUR D'APPEL. Trois fois aujourd'hui un appel qui
	 * rend vrai n'a rien prouve -- et une fois un `IsPlaying()` a menti. Un
	 * acteur `Radial_Storm_C` dans le monde, lui, est une chose qui existe.
	 */
	void ReleverLesOragesRadiaux() const;

	/**
	 * METTRE LE JOUEUR EN VOL, FACE A LA TEMPETE, A DISTANCE CHOISIE.
	 *
	 * ARME PAR `-WorldseedOrageFace=<km>`, et c'est un outil de REGARD, pas une
	 * regle du monde : une tempete se juge a l'oeil, et depuis le sol elle naît
	 * a vingt-cinq kilometres, derriere le relief et la brume.
	 *
	 * POURQUOI EN VOL ET NON POSE AU SOL. Choix du proprietaire, et il resout
	 * deux problemes d'un coup. 71 % de ce monde est ocean : viser une
	 * coordonnee a vingt kilometres, c'est jouer a pile ou face avec la mer, et
	 * l'on ne saurait pas si nager est un defaut ou le hasard. Et l'altitude
	 * degage l'horizon, qui est precisement le sujet.
	 *
	 * ON NE REUTILISE PAS `TeleporterJoueur` : elle tient en vol le temps que le
	 * sol durcisse, puis RELACHE -- elle fait atterrir. Ici on veut rester en
	 * l'air, donc `MOVE_Flying`.
	 *
	 * ET L'ON NE VISE QU'UNE FOIS. Re-orienter a chaque releve se battrait avec
	 * la souris du joueur ; le cap est journalise a chaque passage pour qu'il
	 * puisse se reorienter lui-meme.
	 */
	void EnvoyerLeJoueurVoirLOrage(const AActor* Orage);

	/** Vrai une fois le joueur envoye : il ne vaut que la premiere fois. */
	bool bJoueurEnvoyeVoirLOrage = false;

	/**
	 * Le biome pour lequel les quatre tables ont deja ete ecrites.
	 *
	 * `Count` VAUT « ON N'A ENCORE RIEN POSE », et ce n'est pas un biome :
	 * meme distinction que `AmbianceCourante`, et pour la meme raison -- au
	 * tout premier passage il faut bel et bien ecrire, meme si le choix se
	 * trouve etre celui que le pack livrait.
	 */
	EWorldseedBiome BiomeDesOrages = EWorldseedBiome::Count;

	/** Vrai une fois l'armement tente : il ne vaut que la premiere fois. */
	bool bOragesRadiauxArmes = false;

	/**
	 * MODE MESURE : on force une tempete et on la compte.
	 *
	 * SEPARE DE L'ALLUMAGE, parce que les tempetes tournent desormais en partie
	 * NORMALE. Sans cette separation, chaque partie forcerait une tempete a la
	 * naissance du joueur -- ce qui n'est pas de la meteo mais un banc -- et
	 * balaierait tous les acteurs du monde toutes les dix secondes pour une
	 * ligne que personne ne lit en jouant.
	 *
	 * Arme par `-WorldseedOrageRadial`, et aussi par `-WorldseedOrageFace=` :
	 * aller VOIR une tempete exige de savoir ou elle est.
	 */
	bool bOragesEnMesure = false;

	/** Temps depuis l'armement, et nombre de relevés faits. */
	float TempsDepuisOrageS = 0.0f;
	int32 ReleveesDesOrages = 0;

	/**
	 * Vrai quand la classe du pack est introuvable.
	 *
	 * UN SEUL AVERTISSEMENT, PAS UN PAR TRAME. Le jeu doit tourner sans UDS --
	 * c'est la promesse du README -- donc l'absence n'est pas une erreur, mais
	 * la journaliser soixante fois par seconde noierait tout le reste.
	 */
	bool bPasDlweIndisponible = false;
};

