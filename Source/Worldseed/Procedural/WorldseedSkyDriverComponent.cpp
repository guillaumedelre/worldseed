// Worldseed - pilotage du ciel depuis le climat local.

#include "Procedural/WorldseedSkyDriverComponent.h"

#include "Components/AudioComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedVoxelTerrain.h"
#include "Procedural/WorldseedWeatherReadout.h"
#include "Sound/SoundBase.h"
#include "UObject/UObjectIterator.h"

namespace
{
	/** Un demi-degre de latitude : environ cinquante kilometres. */
	constexpr float SunLatitudeStepDeg = 0.5f;

	/** Noms des variables d'etat meteo d'UDS. */
	const FName NameRain = TEXT("Rain");
	const FName NameSnow = TEXT("Snow");
	const FName NameFog = TEXT("Fog");
	const FName NameDust = TEXT("Dust");
	const FName NameCloudCoverage = TEXT("Cloud Coverage");
	const FName NameWindIntensity = TEXT("Wind Intensity");
	const FName NameWindDirection = TEXT("Wind Direction");

	/**
	 * LES SURCHARGES MANUELLES DE LA POUSSIERE ET DU VENT, ET C'ETAIT LA CAUSE.
	 *
	 * SIGNALE : « je vois assez rarement de la pluie ou des orages tres fort,
	 * est-il bien branche sur le climat ? » -- et pour la poussiere, jamais.
	 *
	 * UDW PORTE UNE SURCHARGE PAR CURSEUR, ET NOUS N'EN POSIONS QU'UNE. Releve
	 * du 29 septembre 2026 sur l'acteur de la carte, instance ET defaut de
	 * classe : `Dust - Manual Override`, `Wind Intensity - Manual Override`,
	 * `Rain`, `Snow`, `Fog`, `Cloud Coverage`, `Material Dust Coverage` --
	 * TOUTES a faux. Seule celle du tonnerre etait armee par notre code, et le
	 * commentaire qui l'accompagne dit exactement pourquoi il le faut : sans
	 * elle, UDS reprend la main depuis son propre systeme de types et notre
	 * ecriture est ecrasee au tick suivant. Le raisonnement vaut mot pour mot
	 * pour la poussiere et pour le vent, et que la pluie soit visible ne prouve
	 * rien : ce sont des booleens INDEPENDANTS.
	 *
	 * CE N'ETAIT PAS UN INTERRUPTEUR ETEINT, et il fallait le verifier avant de
	 * regler quoi que ce soit -- l'aurore avait coute une seance pour cela.
	 * `Enable Dust Particles` vaut VRAI par defaut, `PPWF Intensity from Dust`
	 * vaut 1,4 : la chaine est armee, c'est notre valeur qui ne survivait pas.
	 *
	 * ET LEURS RAPPELS EXISTENT : `OnRep_Dust - Manual Override` et
	 * `OnRep_Wind Intensity - Manual Override` sont tous deux presents -- releve
	 * par une sonde VALIDEE sur un temoin connu (`OnRep_Animate Time of Day`,
	 * que notre code appelle avec succes). Poser un booleen par reflexion ne
	 * declenche aucun rappel, et ce depot l'a paye trois fois.
	 */
	const FName NameDustManuel = TEXT("Dust - Manual Override");
	const FName NameWindManuel = TEXT("Wind Intensity - Manual Override");

	/**
	 * LE DOSAGE DES PARTICULES, ET LE PACK EST TRES INEGAL.
	 *
	 * Releve du 29 septembre 2026 sur l'acteur meteo, instance et defaut de
	 * classe confondus :
	 *
	 *     Dust Particle Spawn Count    1 000     Dust Particle Alpha   0,60
	 *     Snow Particle Spawn Count   20 000     Snow Flakes Alpha     1,00
	 *     Rain Particle Spawn Count   20 000
	 *
	 * LA POUSSIERE EN A VINGT FOIS MOINS QUE LA NEIGE ET LA PLUIE, et elle est
	 * en plus deux fois moins opaque. C'est un choix d'auteur defendable -- une
	 * brume de poussiere est diffuse la ou une averse est faite de traits -- mais
	 * il rend la tempete de sable clairsemee a hauteur d'oeil, alors que c'est
	 * precisement l'evenement qu'on veut voir.
	 *
	 * ON PILOTE DONC LE DOSAGE, comme on pilote deja les curseurs. Ces valeurs
	 * ne dependent NI du climat NI du temps : elles se posent une fois, dans le
	 * bloc de controle, et non deux fois par seconde.
	 */
	/**
	 * LES DEUX EFFETS D'ECRAN, ET ILS ARRIVENT ETEINTS TOUS LES DEUX.
	 *
	 * Releve du 29 septembre 2026 : `Enable Screen Frost` et
	 * `Enable Screen Droplets` valent FAUX, instance comme defaut de classe.
	 * C'est le piege d'`Use Auroras`, pour la troisieme fois -- une
	 * fonctionnalite complete, cablee, dont l'interrupteur dort.
	 *
	 * ET TOUT LE RESTE EST DEJA FAIT. `Screen Frost from Snow` vaut 1, les
	 * materiaux (`Screen_Frost`, `Screen_Droplets`) et les textures
	 * (`Snow_Normal`, `Frost_Scatter`) sont assignes, et les durees de formation
	 * et d'effacement sont reglees -- 8 s et 12 s pour le givre. Il n'y a donc
	 * RIEN a piloter : le givre suivra la neige et les gouttes la pluie des que
	 * les deux booleens seront armes.
	 */
	const FName NameGivreActif = TEXT("Enable Screen Frost");
	const FName NameGouttesActives = TEXT("Enable Screen Droplets");

	const FName NameDustNombre = TEXT("Dust Particle Spawn Count");
	const FName NameDustAlpha = TEXT("Dust Particle Alpha");
	const FName NameDustTaille = TEXT("Dust Particle Scale");
	const FName NameNeigeNombre = TEXT("Snow Particle Spawn Count");
	const FName NameNeigeAlpha = TEXT("Snow Flakes Alpha");
	const FName NameNeigeTaille = TEXT("Snow Flakes Scale");

	/**
	 * L'ORAGE ET L'AURORE, QUI N'ETAIENT PILOTES NI L'UN NI L'AUTRE.
	 *
	 * UDS n'arme « Thunder/Lightning » que par ses TYPES de meteo tout faits --
	 * `Rain_Thunderstorm` et les douze autres -- et nous n'en selectionnons
	 * jamais : nous posons des curseurs continus, ce qui donne la precision
	 * regionale mais laisse cette variable a zero pour toute la partie. Aucun
	 * reglage de climat ne pouvait donc produire un seul eclair.
	 *
	 * L'AURORE A UN INTERRUPTEUR MAITRE, ET IL ARRIVE ETEINT.
	 *
	 * « Use Auroras » vaut FALSE par defaut -- releve sur le defaut de classe
	 * d'`Ultra_Dynamic_Sky_C`, avec `Using Either Aurora` et
	 * `Using Volumetric Aurora`, tous deux derives et faux eux aussi. On
	 * pouvait donc ecrire « Aurora Intensity » fidelement, la relire, la voir
	 * acceptee, et n'avoir JAMAIS un seul photon a l'ecran : c'est le piege
	 * que ce depot consigne sous « une couleur invisible a deux causes
	 * opposees -- le terme qui ne s'evalue jamais, et le terme dont rien
	 * n'atteint l'ecran ».
	 *
	 * CORRECTION D'UNE NOTE FAUSSE : ce commentaire affirmait que le defaut de
	 * 0,12 posait « une aurore faible et permanente a toutes les latitudes ».
	 * Il n'y en avait aucune, nulle part. 0,12 etait l'intensite d'une
	 * fonctionnalite eteinte.
	 */
	const FName NameThunder = TEXT("Thunder/Lightning");
	const FName NameThunderManuel = TEXT("Thunder/Lightning - Manual Override");
	const FName NameAurora = TEXT("Aurora Intensity");
	const FName NameAuroreActive = TEXT("Use Auroras");

	/** Plages de temperature, dans l'ordre des saisons du prereglage. */
	const FName SeasonRangeNames[FWorldseedClimatePreset::SeasonCount] = {
		TEXT("Winter Temperature Min and Max"),
		TEXT("Spring Temperature Min and Max"),
		TEXT("Summer Temperature Min and Max"),
		TEXT("Autumn Temperature Min and Max"),
	};

	float CelsiusToFahrenheit(float C) { return C * 1.8f + 32.0f; }

	// LE CIEL D'INSPECTION EST LU UNE FOIS, ET SOUS UN NOM PREFIXE.
	//
	// Prefixe `Worldseed` parce qu'UBT concatene les `.cpp` en une seule unite
	// de traduction, ou deux namespaces anonymes n'en font qu'un : ce depot a
	// casse TROIS fois sur des noms trop generiques partages entre deux fichiers
	// (`WorldseedMetersToCm`, `SUB`, `CompterNonFinis`), et la collision ne
	// dependait pas du code ecrit mais du REGROUPEMENT choisi par UBT.
	//
	// LU UNE SEULE FOIS parce que la ligne de commande ne change pas, et que
	// deux lecteurs -- l'armement et le controle d'avancement -- doivent voir la
	// meme reponse.
	bool WorldseedCielClairDemande()
	{
		static const bool bDemande = FParse::Param(
			FCommandLine::Get(), TEXT("WorldseedCielClair"));
		return bDemande;
	}
}

UWorldseedSkyDriverComponent::UWorldseedSkyDriverComponent()
{
	// Le composant ne tique pas : son proprietaire l'appelle au rythme qui lui
	// convient, et c'est lui qui sait quand un echantillon de climat est pret.
	PrimaryComponentTick.bCanEverTick = false;
}

void UWorldseedSkyDriverComponent::Drive(const FWorldseedClimateSample& Sample,
	float LongitudeDeg, float AltitudeM, float LatSpanDeg, int32 Seed,
	float DeltaSeconds)
{
	if (!bDriveSunPosition && !bDriveWeather)
	{
		return;
	}

	// --- COMPRIMER LE CYCLE METEO POUR ALLER LE REGARDER ---------------------
	//
	// UNE FREQUENCE JUSTE EST INOBSERVABLE EN DEMONSTRATION, et ce depot a le
	// precedent exact : le 12 septembre la neige tombait une fois sur
	// quarante-trois tirages -- mecanique parfaite, deux heures et demie
	// d'attente. Nos chiffres actuels ont le meme profil : soixante-six orages
	// par an sur une annee de vingt-sept heures font UN ORAGE TOUTES LES
	// VINGT-CINQ MINUTES, et quarante aurores dont la moitie tombe de jour.
	//
	// La surcharge ne change RIEN au modele : elle accelere l'horloge du signal,
	// donc la meme succession d'evenements defile plus vite. A vingt secondes au
	// lieu de cent quatre-vingts, un orage arrive toutes les trois minutes.
	//
	// ET ELLE PASSE PAR LA LIGNE DE COMMANDE, pas par le fichier de regles : en
	// toucher changerait l'empreinte, donc regenererait le monde -- ce ne serait
	// plus le meme endroit qu'on regarde.
	if (!bPeriodeLue)
	{
		bPeriodeLue = true;
		float Surcharge = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedMeteoPeriode="), Surcharge)
			&& Surcharge >= 1.0f)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] meteo : cycle COMPRIME de %.0f a %.0f s ")
				TEXT("-- pour REGARDER, pas pour jouer : les frequences par an ")
				TEXT("sont inchangees, elles defilent %0.1f fois plus vite"),
				WeatherPeriodS, Surcharge, WeatherPeriodS / Surcharge);
			WeatherPeriodS = Surcharge;
		}
	}

	// --- trouver le ciel, une seule fois ------------------------------------
	if (!Bridge.IsValid())
	{
		if (bSearchedForSky)
		{
			return;
		}

		bSearchedForSky = true;
		if (!Bridge.Resolve(GetWorld()))
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] aucun Ultra Dynamic Sky dans le niveau : ciel non pilote"));
			return;
		}
	}

	if (!bPresetRulesLoaded)
	{
		FString Error;
		if (const UWorldseedRules* Rules = WorldseedPipeline::GetRules(Error))
		{
			PresetRules = FWorldseedClimatePresetRules::FromRules(*Rules);
			bPresetRulesLoaded = true;
			ArmerHorloge(*Rules);
			ArmerLaSimulationSolaire(*Rules);
			PoserNiveauDeLEau(*Rules);
			AmbianceRegles = FWorldseedAmbianceRegles::FromRules(*Rules);
			ArmerLeSon(*Rules);
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] regles illisibles (%s) : valeurs de repli pour le ciel"),
				*Error);
			bPresetRulesLoaded = true;   // on n'insiste pas a chaque passage
		}
	}

	// --- les pas dans la neige, et il faut un pion ---------------------------
	//
	// HORS DU BLOC A TIR UNIQUE, ET CE N'EST PAS UN OUBLI : le pion n'existe pas
	// forcement quand les regles se chargent, et il est REMPLACE a chaque mort
	// du joueur. Le cout d'un passage sans rien a faire est une comparaison de
	// pointeurs.
	ArmerLesPasDlwe();

	// --- LES TEMPETES RADIALES, ARMEES PUIS COMPTEES -------------------------
	//
	// L'ARMEMENT EST A TIR UNIQUE, LE COMPTE NON : une tempete apparait en
	// fondu, traverse et disparait, donc un seul echantillon ne dit ni si elle
	// est nee ni si elle vit. Six passages a dix secondes couvrent une naissance
	// et le debut du trajet ; sa duree de vie livree est de 500 a 700 s, donc on
	// ne verra pas sa fin et ce n'est pas le sujet.
	ArmerLesOragesRadiaux();
	if (bOragesRadiauxArmes && ReleveesDesOrages < 6)
	{
		TempsDepuisOrageS += DeltaSeconds;
		if (TempsDepuisOrageS > 10.0f * static_cast<float>(ReleveesDesOrages + 1))
		{
			++ReleveesDesOrages;
			ReleverLesOragesRadiaux();
		}
	}

	// ET ON LES RELIT PENDANT QU'ON MARCHE. Quatre passages a cinq secondes
	// d'intervalle : le banc commence sa marche apres le premier, et un son de
	// pas ne dure qu'un instant.
	if (!PasDlwe.IsEmpty() && ReleveesDesPas < 4)
	{
		TempsDepuisPasS += DeltaSeconds;
		if (TempsDepuisPasS > 5.0f * static_cast<float>(ReleveesDesPas + 1))
		{
			++ReleveesDesPas;
			ReleverLesPasDlwe();
		}
	}

	// --- position du soleil --------------------------------------------------
	if (bDriveSunPosition
		&& FMath::Abs(Sample.LatitudeDeg - LastSunLatitudeDeg) >= SunLatitudeStepDeg)
	{
		if (Bridge.WriteLatLon(Sample.LatitudeDeg, LongitudeDeg))
		{
			LastSunLatitudeDeg = Sample.LatitudeDeg;

			// LE FUSEAU SUIT LA LONGITUDE, ET IL DOIT LA SUIVRE EN MARCHANT.
			// Un monde n'a pas de fuseaux administratifs : on pose le fuseau
			// SOLAIRE, `longitude / 15`, pour que midi soit partout le midi
			// solaire. Le poser une fois au demarrage ne suffirait pas -- notre
			// monde fait 64 km de large, soit 360 degres de longitude, et un
			// joueur qui le traverse passerait par tous les fuseaux.
			const double Fuseau = static_cast<double>(LongitudeDeg) / 15.0;
			const bool bFuseau = Bridge.WriteNumber(TEXT("Time Zone"), Fuseau);

			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] ciel : latitude %.1f  longitude %.1f  fuseau %+.1f h%s"),
				Sample.LatitudeDeg, LongitudeDeg, Fuseau,
				bFuseau ? TEXT("") : TEXT(" (REFUSE)"));
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] latitude non transmise : variable absente sur %s"),
				*Bridge.Describe());
			bDriveSunPosition = false;
		}
	}

	if (!bDriveWeather)
	{
		return;
	}

	// --- meteo ---------------------------------------------------------------
	FWorldseedWeatherParams Params;
	Params.VariationPeriodS = WeatherPeriodS;
	Params.LatSpanDeg = LatSpanDeg;
	Params.Seed = Seed;
	Params.TimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	Params.SeasonPhase = FallbackSeasonPhase;
	Bridge.ReadSeasonPhase(Params.SeasonPhase);

	// L'HEURE VIENT D'UDS, ET ELLE Y EST EN CENTIEMES D'HEURE.
	//
	// `Time of Day` va de 0 a 2400, pas de 0 a 24 : la meme convention que
	// `-WorldseedHeure=`, qui accepte les deux et le dit. La brume de radiation
	// en depend -- elle se forme la nuit et se leve en milieu de matinee -- et
	// une lecture qui echoue laisse le defaut a MIDI, donc l'etat sans cycle
	// diurne. Le silence ne poserait donc pas de brume ou il n'en faut pas.
	double Horloge = 0.0;
	if (Bridge.ReadNumber(TEXT("Time of Day"), Horloge))
	{
		Params.HeureDuJour = static_cast<float>(Horloge) * 0.01f;
	}

	const FWorldseedWeather Target =
		WorldseedWeatherState::Evaluate(Sample, PresetRules, Params);

	if (!bWeatherStarted)
	{
		// Au premier passage il n'y a rien vers quoi fondre : on se pose.
		Current = Target;
		bWeatherStarted = true;
	}
	else
	{
		WorldseedWeatherState::BlendTowards(Current, Target,
			WorldseedWeatherState::AlphaDeFondu(DeltaSeconds, BlendSeconds));
	}

	// --- LE TEMOIN FRANC : FORCER L'EVENEMENT AU LIEU DE L'ATTENDRE ----------
	//
	// Un orage absent a DEUX causes opposees -- le modele n'en demande aucun,
	// ou il en demande et rien n'atteint l'ecran -- et guetter ne les separe
	// pas : quarante captures sur neuf minutes n'ont rien montre, ce qui est
	// compatible avec les deux. Forcer la valeur repond a la seconde question
	// SEULE, en une capture, et c'est exactement ce que ce depot fait partout
	// ailleurs avec ses temoins de couleur franche.
	//
	// ON FORCE APRES LE FONDU, JAMAIS AVANT : pose sur la cible, le lissage
	// ecreterait le temoin lui-meme et l'on retomberait dans la question de
	// depart.
	if (!bTemoinLu)
	{
		bTemoinLu = true;
		float Valeur = -1.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedOrageForce="), Valeur)
			&& Valeur >= 0.0f)
		{
			TemoinOrage = FMath::Clamp(Valeur, 0.0f, 10.0f);
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] meteo : ORAGE FORCE a %.1f sur 10 -- ")
				TEXT("temoin, le climat ne decide plus de cette valeur"),
				TemoinOrage);
		}

		float Poussiere = -1.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedPoussiereForce="), Poussiere)
			&& Poussiere >= 0.0f)
		{
			TemoinPoussiere = FMath::Clamp(Poussiere, 0.0f, 10.0f);
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] meteo : POUSSIERE FORCEE a %.1f sur 10 -- ")
				TEXT("temoin, le climat ne decide plus de cette valeur"),
				TemoinPoussiere);
		}

		float Pluvieux = -1.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedPluieForce="), Pluvieux)
			&& Pluvieux >= 0.0f)
		{
			TemoinPluie = FMath::Clamp(Pluvieux, 0.0f, 10.0f);
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] meteo : PLUIE FORCEE a %.1f sur 10 -- temoin"),
				TemoinPluie);
		}

		float Brumeux = -1.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedBrumeForce="), Brumeux)
			&& Brumeux >= 0.0f)
		{
			TemoinBrume = FMath::Clamp(Brumeux, 0.0f, 10.0f);
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] meteo : BRUME FORCEE a %.1f sur 10 -- temoin"),
				TemoinBrume);
		}

		float Neigeux = -1.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedNeigeForce="), Neigeux)
			&& Neigeux >= 0.0f)
		{
			TemoinNeige = FMath::Clamp(Neigeux, 0.0f, 10.0f);
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] meteo : NEIGE FORCEE a %.1f sur 10 -- temoin"),
				TemoinNeige);
		}

		float Ventee = -1.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedVentForce="), Ventee)
			&& Ventee >= 0.0f)
		{
			TemoinVent = FMath::Clamp(Ventee, 0.0f, 10.0f);
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] meteo : VENT FORCE a %.1f sur 10 -- la poussiere, ")
				TEXT("elle, reste calculee par le modele"),
				TemoinVent);
		}
	}
	if (TemoinOrage >= 0.0f)
	{
		// UN TEMOIN D'ORAGE DOIT POSER UN ORAGE, PAS UN ECLAIR NU.
		//
		// La premiere version ne forcait que `Thunder` : des eclairs tombaient
		// sous un ciel bleu, et le proprietaire l'a signale aussitot. J'en ai
		// d'abord conclu que le modele calculait la couverture independamment
		// de la pluie -- C'ETAIT FAUX, et il suffisait de lire la ligne :
		// `CloudCoverage` pese deja `Occurrence * 0,8` contre `CloudyFraction
		// * 0,5`, donc la pluie DOMINE la nebulosite climatique. Le defaut
		// etait dans l'instrument, pas dans le monde.
		//
		// Les trois grandeurs bougent donc ensemble, comme `Evaluate` les
		// produirait a plein regime : sans quoi le temoin fabrique une scene
		// que le jeu ne peut pas produire, et l'on debogue une chimere.
		Current.Thunder = TemoinOrage;
		Current.Rain = TemoinOrage;
		Current.CloudCoverage = FMath::Max(Current.CloudCoverage, 9.0f);
	}

	if (TemoinPoussiere >= 0.0f)
	{
		// L'ETAT COHERENT EST CELUI DE `Sand_Dust_Storm`, ET IL EST MESURE.
		//
		// Releve dans les deux prereglages de sable du pack, le 29 septembre
		// 2026 : `Dust 10`, `Fog 1`, et `Cloud Coverage` NON SURCHARGEE.
		// `Sand_Dust_Calm` et `Sand_Dust_Storm` ne se distinguent que par
		// `Wind Intensity`, 1 contre 10 -- la tempete du pack est un etat de
		// VENT, la poussiere en etant l'effet, ce qui est aussi la physique de
		// la saltation. Le temoin fait donc monter le vent AVEC la poussiere.
		//
		// ⚠ ON NE COPIE PAS LE TEMOIN D'ORAGE. Celui-ci monte `CloudCoverage` a
		// 9, et ce serait une chimere A L'ENVERS ici : ni l'un ni l'autre des
		// deux prereglages de sable ne touche a la nebulosite, et tous deux
		// posent `Fog = 1`, la valeur neutre. Notre `Fog` calcule monte a 2,5 :
		// le laisser courir ferait deboguer la mauvaise nappe.
		//
		// ET LA PLUIE TOMBE A ZERO. Une tempete de sable ne mouille pas, et sans
		// cela on obtiendrait pluie ET sable -- la chimere symetrique des
		// eclairs sous un ciel bleu.
		Current.Dust = TemoinPoussiere;
		Current.WindIntensity = FMath::Max(Current.WindIntensity, TemoinPoussiere);
		Current.Fog = 1.0f;
		Current.Rain = 0.0f;
		Current.Snow = 0.0f;
	}

	if (TemoinPluie >= 0.0f)
	{
		// L'ETAT DE `Rain` ET `Rain_Thunderstorm`, MESURE dans les prereglages :
		// Rain 7 -> Cloud 7,5 Fog 3 Thunder 4 ; Rain_Thunderstorm 10 -> Cloud 8
		// Fog 6,5 Thunder 10. On interpole entre les deux, et la neige comme la
		// poussiere tombent a zero -- il ne neige pas sous une averse, et une
		// averse lessive l'air.
		const float Part = TemoinPluie / 10.0f;
		Current.Rain = TemoinPluie;
		Current.CloudCoverage = FMath::Max(Current.CloudCoverage, 6.0f + 2.0f * Part);
		Current.Fog = FMath::Max(Current.Fog, 1.0f + 5.5f * Part);
		Current.Thunder = 10.0f * Part * Part;
		Current.Snow = 0.0f;
		Current.Dust = 0.0f;
	}

	if (TemoinBrume >= 0.0f)
	{
		// L'ETAT DE `Foggy`, RELEVE PAR L'API DANS LES PREREGLAGES DU PACK :
		// Fog 10, Wind 1, et RIEN d'autre de surcharge. C'est le seul des treize
		// a monter `Fog` au-dessus de 2, et son vent a 1 est une SOURCE pour
		// nous : le pack lui-meme dit qu'un brouillard va avec de l'air calme,
		// ce qui est exactement le levier que le modele emploie.
		//
		// LA PLUIE ET LE SABLE TOMBENT A ZERO, comme dans les autres temoins :
		// une averse lessive l'air et une tempete de sable n'est pas une brume.
		// Sans cela on obtiendrait une chimere, et ce depot a deja paye « des
		// eclairs sous un ciel bleu ».
		Current.Fog = TemoinBrume;
		Current.WindIntensity = 1.0f;
		Current.Rain = 0.0f;
		Current.Dust = 0.0f;
	}

	if (TemoinNeige >= 0.0f)
	{
		// L'ETAT DE `Snow_Blizzard`, MESURE dans les prereglages du pack :
		// Snow 10, Wind 10, Cloud 10, Fog 10. On garde les proportions, et la
		// poussiere tombe a zero -- il ne neige pas dans une tempete de sable.
		const float Part = TemoinNeige / 10.0f;
		Current.Snow = TemoinNeige;
		Current.WindIntensity = FMath::Max(Current.WindIntensity, TemoinNeige);
		Current.CloudCoverage = FMath::Max(Current.CloudCoverage, 8.0f * Part);
		Current.Fog = FMath::Max(Current.Fog, 1.0f + 9.0f * Part);
		Current.Rain = 0.0f;
		Current.Dust = 0.0f;
	}

	if (TemoinVent >= 0.0f)
	{
		// LE TEMOIN DE VENT, ET IL REPOND A UNE QUESTION QUE L'AUTRE NE SAIT
		// PAS POSER.
		//
		// Le temoin de POUSSIERE force `Dust` et le vent a la MEME valeur, ce
		// qui n'arrive jamais dans le monde : a vent 9 le modele rend un voile
		// de 3,6, pas de 9. Pour REGARDER ce qui rampe au ras du sol -- lequel
		// ne se leve qu'au-dela du seuil de saltation -- il faut donc un vent
		// fort SANS que le voile sature l'image. Mesure qui l'a impose : sous un
		// temoin de poussiere a 10, couper les particules ne change pas l'image
		// d'un dixieme (137,2 contre 136,9 de clarte). On ne voyait rien parce
		// qu'il n'y avait rien a voir : tout etait noye.
		//
		// LA POUSSIERE RESTE CALCULEE, par la meme fonction que `Evaluate` --
		// on ne recopie pas la formule, on la rappelle avec le vent force.
		Current.WindIntensity = TemoinVent;
		Current.Dust = WorldseedWeatherState::PoussiereDepuisVent(
			TemoinVent, /*DustPart=*/1.0f, /*Occurrence=*/0.0f, PresetRules);
		Current.Rain = 0.0f;
		Current.Snow = 0.0f;
	}

	PushWeather();

	// --- LES EFFETS D'ECRAN MONTENT-ILS VRAIMENT ? ---------------------------
	//
	// ARMER N'EST PAS AFFICHER, et le depot a la meme lecon pour l'horloge et
	// pour l'aurore. Le givre s'est vu immediatement ; les gouttes, non. Or
	// elles n'ont PAS d'equivalent au `Screen Frost from Snow` du givre, et
	// elles portent une `Camera Exposure` a zero -- il faut donc lire ce qui
	// monte reellement plutot que de supposer.
	//
	// DIFFERE, parce qu'elles ont des durees de formation : les lire au premier
	// tick rendrait zero pour la bonne raison, et l'on conclurait de travers.
	if (!bEcranVerifie)
	{
		TempsDepuisEcranS += DeltaSeconds;
		if (TempsDepuisEcranS > 6.0f)
		{
			bEcranVerifie = true;
			double Givre = 0.0, Gouttes = 0.0, Coule = 0.0, Expo = 0.0;
			const bool bG = Bridge.ReadNumber(TEXT("Current Screen Frost Intensity"), Givre);
			const bool bD = Bridge.ReadNumber(TEXT("Current Screen Droplets Intensity"), Gouttes);
			Bridge.ReadNumber(TEXT("Current Screen Droplets Drips Intensity"), Coule);
			Bridge.ReadNumber(TEXT("Current Screen Droplets Camera Exposure"), Expo);

			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] meteo : ecran apres %.0f s -- givre %.3f%s, gouttes ")
				TEXT("%.3f%s (coulees %.3f, exposition camera %.3f) | pluie %.1f neige %.1f"),
				TempsDepuisEcranS,
				Givre, bG ? TEXT("") : TEXT(" ILLISIBLE"),
				Gouttes, bD ? TEXT("") : TEXT(" ILLISIBLE"),
				Coule, Expo, Current.Rain, Current.Snow);

			// ARMER N'EST PAS AFFICHER, quatrieme fois. Les deux effets neufs
			// se relisent par leur intensite COURANTE, qu'UDW calcule : c'est
			// un EFFET, pas un drapeau, et c'est la seule chose qui prouve que
			// la chaine va jusqu'au bout. La TEMPERATURE accompagne la chaleur
			// parce qu'elle en est l'entree -- sans elle, « chaleur 0,000 » ne
			// se separe pas en « il ne fait pas chaud ici » et « la chaleur ne
			// sait pas qu'il fait chaud ».
			double Arc = 0.0, Chaleur = 0.0, Temp = 0.0;
			const bool bArcLu = Bridge.ReadNumber(
				TEXT("Current Rainbow Strength"), Arc);
			const bool bChaudLu = Bridge.ReadNumber(
				TEXT("Current Heat Distortion Value"), Chaleur);
			const bool bTempLue = Bridge.ReadNumber(
				TEXT("Target Heat Distortion Value"), Temp);

			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] meteo : arc-en-ciel %.3f%s, chaleur %.3f%s ")
				TEXT("(cible %.3f%s) | pluie %.1f brume %.1f nuages %.1f vent %.1f"),
				Arc, bArcLu ? TEXT("") : TEXT(" ILLISIBLE"),
				Chaleur, bChaudLu ? TEXT("") : TEXT(" ILLISIBLE"),
				Temp, bTempLue ? TEXT("") : TEXT(" ILLISIBLE"),
				Current.Rain, Current.Fog, Current.CloudCoverage,
				Current.WindIntensity);

			// --- LA SIMULATION SOLAIRE EST-ELLE VIVANTE ? --------------------
			//
			// ARMER N'EST PAS EVALUER, et c'est le defaut que ce releve garde :
			// `Simulate Real Sun` arrivait a FAUX, si bien que la latitude
			// ecrite deux fois par seconde ne servait a rien. Le drapeau se
			// relit a vrai des qu'on le pose -- comme `Animate Time of Day` et
			// `Use Auroras` avant lui -- donc seul l'EFFET prouve quelque chose.
			//
			// LE CHIFFRE SE LIT CONTRE LA LATITUDE, jamais seul. Sans
			// simulation, l'elevation de midi vaut `90 - Sun Pitch`, c'est-a-
			// dire SOIXANTE degres a toutes les latitudes : deux lancements a
			// des latitudes eloignees rendraient le meme chiffre. Avec elle,
			// le registre de septembre donne 87,1 a 5 degres, 45,4 a 47 et 7,6
			// a 85. L'attendu est donc `90 - |latitude|` a la declinaison
			// saisonniere pres, au plus vingt-trois degres.
			// ON NE RELIT PAS LE DRAPEAU, ET C'EST DELIBERE. `ReadNumber` ne sait
			// pas lire un booleen : sa reponse dirait « illisible » pour une
			// variable parfaitement presente, donc un message qui ment. Et le
			// drapeau ne prouverait rien de toute facon -- il se relit a vrai
			// des qu'on le pose, comme `Animate Time of Day` et `Use Auroras`
			// avant lui. L'ELEVATION EST LA PREUVE, parce qu'elle est un EFFET.
			float Elevation = 0.0f;
			if (ElevationDuSoleil(Elevation))
			{
				const float Attendue = 90.0f - FMath::Abs(Sample.LatitudeDeg);
				UE_LOG(LogTemp, Warning,
					TEXT("[Worldseed] soleil : elevation %.1f deg a %.1f h, latitude ")
					TEXT("%.1f -- attendu %.1f au midi d'equinoxe, et SOIXANTE a ")
					TEXT("toute latitude si la simulation dort"),
					Elevation, Params.HeureDuJour, Sample.LatitudeDeg, Attendue);
			}
			else
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[Worldseed] soleil : aucune lumiere directionnelle sur %s -- ")
					TEXT("l'elevation ne peut pas etre relevee"), *Bridge.Describe());
			}

			// --- ET LE SON : CE QUI JOUE, NON CE QU'ON A POSE ---------------
			//
			// Meme famille que les trois releves ci-dessus, et meme raison :
			// un drapeau relu ne prouve rien. Pour du son, la seule mesure qui
			// tranche est l'inventaire des composants audio VIVANTS.
			ReleverLeSon();
		}
	}

	// --- L'HEURE AVANCE-T-ELLE VRAIMENT ? ------------------------------------
	//
	// ARMER N'EST PAS FAIRE AVANCER. La valeur peut etre posee et le temps
	// rester fige -- UDS met sa vitesse d'horloge en cache, et ce depot a deja
	// constate qu'elle ne s'accelere pas a chaud. Le seul controle qui tranche
	// est de relire l'heure quelques secondes plus tard.
	if (HeureALArmement >= 0.0 && !bAvanceVerifiee)
	{
		TempsDepuisArmementS += DeltaSeconds;
		if (TempsDepuisArmementS > 5.0f)
		{
			bAvanceVerifiee = true;
			double Maintenant = 0.0;
			if (!Bridge.ReadNumber(TEXT("Time of Day"), Maintenant))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[Worldseed] horloge : « Time of Day » illisible -- ")
					TEXT("impossible de dire si le temps passe"));
			}
			else
			{
				// LE MEME CONTROLE REPOND A DEUX QUESTIONS OPPOSEES, et son
				// verdict s'inverse avec le drapeau : sous `-WorldseedCielClair`
				// une horloge figee est le SUCCES, ailleurs c'est l'echec. Un
				// message unique aurait crie au defaut sur le comportement
				// voulu, et ce depot compte deja plusieurs releves ou du bruit
				// se lisait comme une information.
				const bool bFigee = FMath::IsNearlyEqual(Maintenant, HeureALArmement, 1e-4);
				const bool bVoulueFigee = WorldseedCielClairDemande();

				if (bFigee && bVoulueFigee)
				{
					UE_LOG(LogTemp, Log,
						TEXT("[Worldseed] horloge : FIGEE a %.4f apres %.1f s, ")
						TEXT("comme demande -- l'A/B est valide"),
						Maintenant, TempsDepuisArmementS);
				}
				else if (bFigee)
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[Worldseed] horloge : ARMEE MAIS FIGEE a %.4f apres %.1f s. ")
						TEXT("UDS met sa vitesse en cache : il faut probablement poser les ")
						TEXT("durees AVANT son BeginPlay, ou dans la carte."),
						Maintenant, TempsDepuisArmementS);
				}
				else if (bVoulueFigee)
				{
					// FIGER N'EST PAS ARRETER. `CielDInspection` pose le drapeau
					// par reflexion, ce qui ne declenche aucun rappel : une
					// boucle deja lancee continue. Si cette ligne parait, tout
					// A/B par lancements successifs est INVALIDE.
					UE_LOG(LogTemp, Error,
						TEXT("[Worldseed] horloge : le temps passe MALGRE ")
						TEXT("-WorldseedCielClair -- %.4f a %.4f en %.1f s. ")
						TEXT("Tout A/B par lancements successifs est invalide."),
						HeureALArmement, Maintenant, TempsDepuisArmementS);
				}
				else
				{
					UE_LOG(LogTemp, Log,
						TEXT("[Worldseed] horloge : le temps passe -- %.4f a %.4f en %.1f s"),
						HeureALArmement, Maintenant, TempsDepuisArmementS);
				}
			}
		}
	}

	if (bShowReadout)
	{
		// La duree couvre largement la periode de mise a jour : la ligne ne
		// clignote pas entre deux appels.
		WorldseedWeatherReadout::Draw(Current, Sample, LongitudeDeg, AltitudeM,
			Params.SeasonPhase, WeatherPeriodS, FMath::Max(DeltaSeconds, 0.05f) * 4.0f);
	}
}

void UWorldseedSkyDriverComponent::ArmerHorloge(const UWorldseedRules& Rules)
{
	static const FName NomAnimer = TEXT("Animate Time of Day");
	static const FName NomJour = TEXT("Day Length");
	static const FName NomNuit = TEXT("Night Length");

	if (Rules.Num(TEXT("uds"), TEXT("animerHorloge"), 1.0) < 0.5)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] horloge : non armee (uds.animerHorloge a zero) -- ")
			TEXT("Ultra Dynamic Sky garde ses propres reglages"));
		return;
	}

	// --- LE CIEL D'INSPECTION INTERDIT D'ARMER, ET C'EST LUI QUI DOIT GAGNER --
	//
	// `AWorldseedTerrain::CielDInspection` fige « Animate Time of Day » au
	// BeginPlay, sous `-WorldseedCielClair`, pour qu'un A/B par lancements
	// successifs ait la meme lumiere des deux cotes -- trente secondes d'ecart
	// au chargement font douze minutes de jeu. Puis cette fonction le reposait a
	// vrai quelques secondes plus tard, rappel `OnRep_` compris : le DERNIER
	// ecrivain gagnait, et la garde etait morte SILENCIEUSEMENT.
	//
	// PREUVE, sur SEPT journaux independants tous lances avec le drapeau : les
	// trois lignes se suivent -- « horloge figee sur 1 acteur(s) UDS », puis
	// « horloge : armee », puis « le temps passe -- 1300,0000 a 1303,3407 en
	// 5,5 s ». La troisieme mesure l'horloge EN MARCHE dans une session censee
	// la figer, et son chiffre est coherent sept fois (3,18 a 3,43 unites).
	//
	// ON LE JOURNALISE plutot que de sortir en silence : un silence se lirait
	// « tout va bien », et c'est exactement ce qui a laisse ce defaut vivre.
	if (WorldseedCielClairDemande())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] horloge : NON armee -- ciel d'inspection ")
			TEXT("(-WorldseedCielClair). L'heure de depart est posee quand meme."));
		PoserHeureDeDepart();
		return;
	}

	// LES DUREES D'ABORD, L'ARMEMENT ENSUITE, et l'ordre n'est pas indifferent :
	// UDS met sa vitesse d'horloge EN CACHE, et ce depot a deja constate qu'elle
	// ne s'accelere pas a chaud -- ni `Day Length` ni le multiplicateur ne
	// changent la vitesse en cours de partie. Poser les durees avant d'armer
	// donne au cache la chance de se construire sur les bonnes valeurs.
	const double Jour = Rules.Num(TEXT("uds"), TEXT("dureeJourneeMin"), 30.0);
	const double Nuit = Rules.Num(TEXT("uds"), TEXT("dureeNuitMin"), 15.0);
	const bool bJour = Bridge.WriteNumber(NomJour, Jour);
	const bool bNuit = Bridge.WriteNumber(NomNuit, Nuit);

	// `Time Speed` EST LE MULTIPLICATEUR DE L'HORLOGE, et c'est lui qui manquait :
	// armer « Animate Time of Day » sur un acteur dont la vitesse vaut zero pose
	// bien le drapeau et ne fait rien avancer -- mesure : heure figee a 1300,0000
	// apres cinq secondes et demie, alors que le journal annoncait « EN MARCHE ».
	// NE PAS CONFONDRE avec `Time of Day Change Speed`, de la categorie « Change
	// Monitoring » : celle-la MESURE la vitesse de changement, elle ne la fixe pas.
	static const FName NomVitesse = TEXT("Time Speed");
	double Vitesse = 0.0;
	const bool bLue = Bridge.ReadNumber(NomVitesse, Vitesse);
	if (bLue && Vitesse <= 0.0)
	{
		Bridge.WriteNumber(NomVitesse, 1.0);
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] horloge : « Time Speed » valait %.2f, pose a 1"), Vitesse);
	}

	const bool bArmee = Bridge.WriteBool(NomAnimer, true);

	// ET ON DECLENCHE LE RAPPEL A LA MAIN. `Animate Time of Day` est une
	// variable repliquee a RepNotify : c'est son `OnRep_` qui lance la boucle
	// d'animation, et le moteur ne l'appelle que sur une replication reelle --
	// jamais quand on pose la variable par reflexion. Mesure sans cet appel :
	// drapeau a vrai, lu a vrai, et l'heure figee a 1300,0000.
	const bool bReveillee = Bridge.CallFunction(TEXT("OnRep_Animate Time of Day"));

	if (!bArmee)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] horloge : « %s » introuvable sur %s -- le temps ne ")
			TEXT("passera pas"), *NomAnimer.ToString(), *Bridge.Describe());
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] horloge : armee -- journee %.0f min%s, nuit %.0f min%s, ")
		TEXT("rappel OnRep %s"),
		Jour, bJour ? TEXT("") : TEXT(" (REFUSEE)"),
		Nuit, bNuit ? TEXT("") : TEXT(" (REFUSEE)"),
		bReveillee ? TEXT("appele") : TEXT("INTROUVABLE"));

	PoserHeureDeDepart();
}

void UWorldseedSkyDriverComponent::ArmerLaSimulationSolaire(const UWorldseedRules& Rules)
{
	// --- LA LATITUDE N'ETAIT PAS EVALUEE, ET C'EST LA TROISIEME FOIS ---------
	//
	// Ce composant ecrit `Latitude` et `Longitude` dans Ultra Dynamic Sky des
	// que le joueur se deplace d'un demi-degre. Or `Simulate Real Sun` arrive a
	// FAUX, et sans lui UDS trace un arc solaire SIMPLIFIE dont l'elevation de
	// midi vaut `90 - Sun Pitch`, IDENTIQUE A TOUTES LES LATITUDES. Nous
	// ecrivions donc fidelement, deux fois par seconde, une valeur que rien ne
	// lisait -- exactement le defaut de `Animate Time of Day` (28 septembre
	// 2026) et celui de `Use Auroras` le meme jour. Le releve du 29 septembre
	// sur l'acteur du niveau : `Simulate Real Sun = False`, `Time Zone = 0`.
	//
	// CE QU'ON PERD SANS ELLE, et ce n'est pas un detail d'eclairage : la duree
	// du jour ne varie plus avec la saison, il n'y a pas de soleil de minuit aux
	// poles, et la course du soleil est la meme a l'equateur qu'au cercle
	// polaire. Tout le calage de latitude du monde devient decoratif.
	//
	// LE FUSEAU SUIT LA LONGITUDE, ET IL LE FAUT. La documentation du pack est
	// explicite : « make sure to set the Time Zone to the correct UTC offset for
	// the location, so that Time of Day will be interpreted correctly as local
	// to this location ». Un monde n'a pas de fuseaux administratifs ; on pose
	// donc le fuseau SOLAIRE, `longitude / 15`, pour que midi soit le midi
	// solaire partout. Sans lui, a 92 degres de longitude, midi tomberait plus
	// de six heures a cote -- et la brume, qui depend de l'heure, suivrait.
	if (Rules.Num(TEXT("uds"), TEXT("simulerLeSoleil"), 1.0) < 0.5)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] soleil : simulation NON armee (uds.simulerLeSoleil a ")
			TEXT("zero) -- la latitude ecrite ne sera pas evaluee"));
		return;
	}

	const bool bSoleil = Bridge.WriteBool(TEXT("Simulate Real Sun"), true);
	const bool bLune = Bridge.WriteBool(TEXT("Simulate Real Moon"), true);

	// ARMER N'EST PAS APPLIQUER, et la documentation du pack le dit en propres
	// termes : « Some properties, if you just set them directly at runtime, will
	// have no effect [...] they are static properties [...] you can call one of
	// the Static Properties functions to apply the change ». C'est la forme
	// GENERALE du piege que ce depot a paye trois fois sur des `OnRep_`. On
	// propose donc les deux voies, et l'on journalise laquelle a pris.
	const bool bRappel = Bridge.CallFunction(TEXT("OnRep_Simulate Real Sun"));
	const bool bStatique = Bridge.CallFunction(TEXT("Static Properties - Sun"));

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] soleil : simulation reelle %s (lune %s), rappel OnRep %s, ")
		TEXT("Static Properties - Sun %s"),
		bSoleil ? TEXT("ARMEE") : TEXT("REFUSEE -- « Simulate Real Sun » introuvable"),
		bLune ? TEXT("armee") : TEXT("refusee"),
		bRappel ? TEXT("appele") : TEXT("introuvable"),
		bStatique ? TEXT("appele") : TEXT("introuvable"));
}

bool UWorldseedSkyDriverComponent::ElevationDuSoleil(float& OutDegres) const
{
	const AActor* const Ciel = Bridge.CielBrut();
	if (Ciel == nullptr)
	{
		return false;
	}

	// LA LUMIERE LA PLUS INTENSE EST LE SOLEIL. UDS porte DEUX lumieres
	// directionnelles -- le soleil et la lune -- et la lune est reglee bien plus
	// bas. Prendre la premiere venue rendrait la lune une nuit sur deux, et le
	// releve sauterait sans qu'on sache pourquoi.
	const UDirectionalLightComponent* Meilleure = nullptr;
	float MeilleureIntensite = -1.0f;

	TArray<UDirectionalLightComponent*> Lumieres;
	Ciel->GetComponents<UDirectionalLightComponent>(Lumieres);
	for (const UDirectionalLightComponent* L : Lumieres)
	{
		if (L != nullptr && L->Intensity > MeilleureIntensite)
		{
			MeilleureIntensite = L->Intensity;
			Meilleure = L;
		}
	}

	if (Meilleure == nullptr)
	{
		return false;
	}

	// UNE LUMIERE DIRECTIONNELLE POINTE DANS LE SENS DE SON AXE X, donc son
	// vecteur va du soleil VERS la scene : l'elevation du soleil est l'oppose
	// de l'inclinaison de ce vecteur. Se tromper de signe rendrait un soleil
	// sous l'horizon a midi, et le chiffre resterait plausible.
	OutDegres = -Meilleure->GetComponentRotation().Pitch;
	return true;
}

void UWorldseedSkyDriverComponent::ArmerLeSon(const UWorldseedRules& Rules)
{
	// --- ON RELIT AVANT D'ECRIRE, ET CETTE FOIS TOUT ETAIT DEJA ARME --------
	//
	// C'est la premiere fonctionnalite de ce pack que ce depot trouve ALLUMEE,
	// apres cinq qui arrivaient eteintes -- les aurores, les dix surcharges
	// manuelles, le givre, les gouttes, l'arc-en-ciel et la chaleur. Le releve
	// du 29 septembre 2026 donne, instance et defaut de classe confondus :
	//
	//     Enable Weather Sound Effects                   VRAI
	//     Use Occlusion to Attenuate Sounds in Interiors VRAI
	//     Global Sound Asset       UDS_Global_WeatherSounds
	//     Directional Sound Asset  UDS_Directional_WeatherSounds
	//     Weather Sounds Master Volume                      1
	//     Wind Volume / Rain Volume / Wind Whistling Volume 1
	//     Close Thunder Volume / Distant Thunder Volume     1
	//     Environment Sound                              None   <- le seul trou
	//
	// DONC ON NE REARME RIEN : on relit, et l'on crie si la lecture dement. Un
	// drapeau qu'on repose se relit a vrai quoi qu'il arrive, et ce serait
	// exactement le bruit qui se lit comme une mesure.
	// ⚠ ET L'ON LIT UN BOOLEEN AVEC `ReadBool`, PAS AVEC `ReadNumber`. La
	// premiere version de ce bloc employait `ReadNumber`, qui ne sait pas lire
	// un booleen : le journal a annonce « effets meteo ILLISIBLES, occlusion
	// ILLISIBLE » sur deux variables parfaitement presentes et toutes deux a
	// VRAI. Ce depot avait deja RETIRE une ligne pour cette raison exacte --
	// la relecture de `Simulate Real Sun` -- au lieu de la reparer ; elle l'est
	// desormais, et le pont sait lire les deux.
	bool bEffets = false;
	const bool bEffetsLus = Bridge.ReadBool(TEXT("Enable Weather Sound Effects"), bEffets);

	// SI LE PACK CHANGEAIT D'AVIS, ON LE RATTRAPE -- et alors seulement on
	// ecrit, rappel compris. Poser un booleen par reflexion ne declenche aucun
	// `OnRep_`, et `OnRep_Enable Weather Sound Effects` existe : sonde validee
	// sur `OnRep_Animate Time of Day`, que ce fichier appelle avec succes.
	if (bEffetsLus && !bEffets)
	{
		Bridge.WriteBool(TEXT("Enable Weather Sound Effects"), true);
		Bridge.CallFunction(TEXT("OnRep_Enable Weather Sound Effects"));
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] son : « Enable Weather Sound Effects » etait ETEINT ")
			TEXT("-- arme. Ni la pluie ni le vent ne s'entendaient."));
	}

	// --- LE VOLUME MAITRE, ET LUI SEUL --------------------------------------
	//
	// UN SEUL CURSEUR, PAS SIX. Le pack en expose six -- maitre, vent, pluie,
	// tonnerre proche, tonnerre lointain, sifflement -- et sa documentation dit
	// que « le volume resultant s'echelonne avec ces reglages ET l'etat meteo
	// courant ». Les cinq par famille sont donc deja modules par la meteo que
	// nous ecrivons : y toucher doublerait la modulation, et les poser tous a
	// 1,0 dans le fichier de regles ajouterait cinq boutons INERTES -- ce que
	// le proprietaire a demande d'arreter de garder, le 23 septembre 2026.
	//
	// LE MAITRE, LUI, EST UN VRAI REGLAGE DE MIXAGE : c'est le seul qui permette
	// de faire de la place a autre chose sans deregler l'equilibre interne du
	// pack. Il porte aussi le SON D'AMBIANCE, qui sort sur le meme bus
	// (`UDS_Weather_AudioBus`) et passe par le meme mixeur.
	const double Volume = FMath::Clamp(
		Rules.Num(TEXT("uds"), TEXT("sonVolume"), 1.0), 0.0, 4.0);
	const bool bVolumePose = Bridge.WriteNumber(TEXT("Weather Sounds Master Volume"), Volume);
	if (bVolumePose)
	{
		// Son rappel existe, et il n'est pas decoratif : le volume maitre est
		// applique par le mixeur, qui doit etre prevenu.
		Bridge.CallFunction(TEXT("OnRep_Weather Sounds Master Volume"));
		// ET LA FONCTION QUI APPLIQUE LES VOLUMES, que le pack expose pour
		// cela -- trois des six curseurs n'ont AUCUN `OnRep_`, releve le
		// 29 septembre : `Close Thunder Volume`, `Distant Thunder Volume` et
		// `Wind Whistling Volume`. Sans cet appel, un reglage du maitre
		// laisserait le tonnerre a son ancien niveau.
		Bridge.CallFunction(TEXT("Apply Sound Effects Volume Levels"));
	}

	// --- L'OCCLUSION : ce qui etouffe les sons en grotte --------------------
	//
	// ELLE EST DEJA ARMEE, et c'est heureux : ce monde porte cent cinquante
	// chambres, soixante-six bouches et des centaines de gouffres. Sans elle,
	// la pluie s'entendrait aussi fort a trente metres sous terre qu'a l'air
	// libre. On se contente de la RELIRE -- et de dire si elle a disparu.
	bool bOcclusion = false;
	const bool bOcclusionLue = Bridge.ReadBool(
		TEXT("Use Occlusion to Attenuate Sounds in Interiors"), bOcclusion);

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] son : effets meteo %s, occlusion en interieur %s, ")
		TEXT("volume maitre %.2f%s"),
		bEffetsLus ? (bEffets ? TEXT("ARMES") : TEXT("ETEINTS")) : TEXT("ILLISIBLES"),
		bOcclusionLue ? (bOcclusion ? TEXT("ARMEE") : TEXT("ETEINTE"))
			: TEXT("ILLISIBLE"),
		Volume, bVolumePose ? TEXT("") : TEXT(" REFUSE"));
}

void UWorldseedSkyDriverComponent::ReleverLeSon() const
{
	// --- L'INVENTAIRE PORTE SUR LE MONDE, PAS SUR LES DEUX ACTEURS ----------
	//
	// Un inventaire TRONQUE se lit exactement comme un inventaire complet, et
	// ce depot a paye cette lecon sur l'aurore : un balayage arrete a dix-huit
	// entrees avait manque la seule qui comptait. Le mixeur du pack, les deux
	// sources meteo et les composants d'ambiance ne vivent pas forcement sur le
	// meme acteur -- et la documentation dit qu'UDW « met a jour les composants
	// audio periodiquement », sans dire ou il les pose. On les compte donc tous.
	const UWorld* const Monde = GetWorld();
	if (!Monde)
	{
		return;
	}

	int32 Total = 0;
	int32 Jouent = 0;
	TArray<FString> Detail;

	for (TObjectIterator<UAudioComponent> It; It; ++It)
	{
		const UAudioComponent* const C = *It;
		if (!C || C->GetWorld() != Monde || C->IsTemplate())
		{
			continue;
		}

		++Total;
		const bool bJoue = C->IsPlaying();
		if (bJoue)
		{
			++Jouent;
		}

		// ON NOMME LA SOURCE, pas le composant : « AudioComponent_3 » ne dit
		// rien, « UDS_Global_WeatherSounds » dit tout.
		const USoundBase* const Son = C->Sound;
		Detail.Add(FString::Printf(TEXT("%s%s x%.2f"),
			Son ? *Son->GetName() : TEXT("(sans source)"),
			bJoue ? TEXT("") : TEXT(" [MUET]"),
			C->VolumeMultiplier));
	}

	// LE COMPTE EN PREMIER, et il se lit meme quand le detail deborde : c'est
	// lui qui dit si l'inventaire vaut quelque chose. Zero composant sur un
	// monde qui vient de demarrer voudrait dire « trop tot », pas « rien ne
	// joue » -- d'ou le temps ecoule, affiche a cote.
	if (Total == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] son : AUCUN composant audio dans le monde apres %.0f s. ")
			TEXT("Ni la pluie, ni le vent, ni le tonnerre ne peuvent s'entendre."),
			TempsDepuisEcranS);
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[Worldseed] son apres %.0f s : %d composant(s) audio, dont %d qui ")
		TEXT("JOUENT -- %s"),
		TempsDepuisEcranS, Total, Jouent, *FString::Join(Detail, TEXT(", ")));

	// --- ET LE SON D'AMBIANCE, qui etait le seul trou du pack ----------------
	//
	// LES TROIS ENTIERS PROUVENT QUE LE PACK MODULE. Ils valent -1 tant
	// qu'aucune ambiance ne joue, et prennent un rang des qu'elle tourne :
	// c'est par eux que le graphe MetaSound choisit entre les oiseaux, les
	// insectes de nuit et le vent dans les arbres. Trois -1 sous une ambiance
	// posee voudraient dire qu'elle joue sans savoir quand ni ou.
	double Heure = -1.0, Meteo = -1.0, Vent = -1.0;
	const bool bHeureLue = Bridge.ReadNumber(TEXT("Environment Sound Time Integer"), Heure);
	Bridge.ReadNumber(TEXT("Environment Sound Weather Integer"), Meteo);
	Bridge.ReadNumber(TEXT("Environment Sound Wind Integer"), Vent);

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] son : ambiance « %s » -- le pack la module sur ")
		TEXT("moment %s, meteo %.0f, vent %.0f"),
		WorldseedAmbiance::Nom(AmbianceCourante),
		bHeureLue ? *FString::Printf(TEXT("%.0f"), Heure) : TEXT("ILLISIBLE"),
		Meteo, Vent);
}

void UWorldseedSkyDriverComponent::PiloterAmbiance(EWorldseedBiome Biome,
	EWorldseedCover Couverture)
{
	if (!Bridge.IsValid() || !bPresetRulesLoaded)
	{
		return;
	}

	const EWorldseedAmbiance Voulue = WorldseedAmbiance::Choisir(Biome, Couverture);
	if (Voulue == AmbianceCourante)
	{
		// RIEN A FAIRE, ET C'EST LE CAS NOMINAL. Relancer `Change Environment
		// Sound` deux fois par seconde rendrait l'ambiance inaudible -- chaque
		// appel repart d'un fondu -- et chargerait une source a chaque passage.
		return;
	}

	const FString& Chemin = AmbianceRegles.Chemin(Voulue);
	UObject* Asset = nullptr;
	if (!Chemin.IsEmpty())
	{
		// SYNCHRONE A DESSEIN, malgre le troisieme argument du pack. Une
		// ambiance chargee en differe arriverait APRES que le joueur a traverse
		// la lisiere, et sur une frontiere etroite elle arriverait apres en
		// etre ressorti. Les trois assets pesent ensemble moins qu'un maillage
		// d'arbre : le cout se paie une fois, a la premiere entree en foret.
		Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *Chemin);
		if (!Asset)
		{
			// UNE AMBIANCE INTROUVABLE NE DOIT PAS ETRE SILENCIEUSE. Ces trois
			// assets sont produits par `Tools/UE/ambiances_worldseed.py` ; un
			// depot fraichement clone sans le pack ne les a pas, et le jeu doit
			// continuer -- mais on le DIT, une fois par famille.
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] son : ambiance « %s » introuvable (%s) -- le ")
				TEXT("silence sera joue a la place. Rejouer ")
				TEXT("Tools/UE/ambiances_worldseed.py."),
				WorldseedAmbiance::Nom(Voulue), *Chemin);
		}
	}

	// ON APPELLE MEME AVEC `nullptr`, et c'est la voie que le pack documente
	// pour ARRETER une ambiance : « stop environment sounds by calling it with
	// no environment sound asset selected ». Sortir de la foret doit rendre le
	// silence, pas laisser les oiseaux dans le desert.
	const bool bPose = Bridge.ChangerAmbiance(Asset, AmbianceRegles.FonduS, false);

	if (!bPose)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] son : « Change Environment Sound » REFUSEE par %s ")
			TEXT("-- signature inattendue. L'ambiance ne changera jamais."),
			*Bridge.Describe());
		// ON RETIENT QUAND MEME LA FAMILLE : sans cela on reessaierait deux fois
		// par seconde, et le journal se remplirait d'un echec qu'on connait.
	}
	else
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] son : ambiance « %s » -> « %s » (fondu %.1f s)"),
			WorldseedAmbiance::Nom(AmbianceCourante),
			WorldseedAmbiance::Nom(Voulue), AmbianceRegles.FonduS);
	}

	AmbianceCourante = Voulue;
}

void UWorldseedSkyDriverComponent::PoserNiveauDeLEau(const UWorldseedRules& Rules)
{
	// --- UNE FONCTION ARMEE SUR UN NIVEAU A MOINS L'INFINI ------------------
	//
	// `Use UDS Water Level` vaut DEJA vrai sur l'acteur meteo -- c'est son
	// defaut -- mais `Global Water Level` vaut **-100 000 000**. La fonction
	// tourne donc sur un niveau d'eau inatteignable et ne fait rien. Releve du
	// 29 septembre 2026 sur l'acteur du niveau.
	//
	// CE QU'ELLE APPORTE, d'apres la documentation du pack : les particules de
	// pluie et de neige cessent de tomber SOUS la mer, les gouttes d'ecran
	// s'eteignent quand la camera passe sous l'eau et l'ecran se remouille en
	// ressortant, l'arc-en-ciel se masque sous la surface, et l'occlusion
	// sonore devient totale en plongee. Notre ocean est un plan a Z = 0, donc
	// la valeur n'est pas un reglage : c'est une CONSTANTE du monde.
	const float NiveauM = static_cast<float>(
		Rules.Num(TEXT("uds"), TEXT("niveauMerM"), 0.0));
	const double NiveauCm = static_cast<double>(NiveauM) * WorldseedMetersToCm;

	const bool bPose = Bridge.WriteNumber(TEXT("Global Water Level"), NiveauCm);
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] eau : niveau global %s a %.0f cm -- les particules ne ")
		TEXT("tomberont plus sous la mer"),
		bPose ? TEXT("pose") : TEXT("REFUSE, « Global Water Level » introuvable"),
		NiveauCm);
}

void UWorldseedSkyDriverComponent::PoserHeureDeDepart()
{
	// --- CHOISIR L'HEURE DE DEPART, POUR ALLER VOIR CE QUI N'ARRIVE QUE LA NUIT
	//
	// L'AURORE NE SE VOIT QUE DANS LE NOIR : UDS porte une « Daytime Aurora
	// Intensity » a zero et fait le fondu lui-meme. Or la journee dure trente
	// minutes contre quinze de nuit : arriver a midi, c'est une demi-heure
	// d'attente avant de pouvoir juger. La meme raison que la compression du
	// cycle meteo -- on ne regarde pas un phenomene rare en temps reel.
	//
	// L'ECHELLE D'UDS EST EN CENTIEMES D'HEURE ET NON EN HEURES : minuit vaut
	// 0, midi 1200, vingt-trois heures 2300. On accepte donc les deux ecritures
	// -- 23 comme 2300 -- parce que se tromper d'un facteur cent poserait midi
	// pile en croyant poser vingt-trois heures, et l'aurore serait invisible
	// sans qu'une ligne ne l'explique.
	float Heure = -1.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedHeure="), Heure) && Heure >= 0.0f)
	{
		const float EnUds = (Heure <= 24.0f) ? (Heure * 100.0f) : Heure;
		const bool bPosee = Bridge.WriteNumber(TEXT("Time of Day"), EnUds);
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] horloge : heure de depart posee a %.0f (soit %.1f h) -- %s"),
			EnUds, EnUds / 100.0f,
			bPosee ? TEXT("acceptee") : TEXT("REFUSEE, « Time of Day » introuvable"));
	}

	// ON MEMORISE L'HEURE POUR VERIFIER QU'ELLE AVANCE -- OU QU'ELLE N'AVANCE
	// PAS. Armer n'est pas faire avancer, et symetriquement FIGER N'EST PAS
	// ARRETER : `CielDInspection` pose le drapeau a faux par reflexion, ce qui
	// ne declenche aucun rappel, donc une boucle d'animation DEJA LANCEE peut
	// continuer de tourner. Le seul controle qui tranche est de relire l'heure
	// quelques secondes plus tard, et il vaut dans les DEUX sens : il se fait
	// dans `Drive`.
	Bridge.ReadNumber(TEXT("Time of Day"), HeureALArmement);
}

void UWorldseedSkyDriverComponent::PushWeather() const
{
	// ON RELIT CE QU'ON ECRIT, UNE FOIS. `WriteNumber` rend un booleen que ce
	// corps ignorait : un nom de variable absent d'Ultra Dynamic Weather fait
	// donc echouer l'ecriture EN SILENCE, et la meteo resterait sur ses valeurs
	// propres sans qu'une ligne ne bouge. Ce depot a exactement ce precedent
	// avec la rampe du decor -- trois ecritures de parametre devenues des no-op,
	// trouvees des mois plus tard -- et la parade y est la meme : relire.
	//
	// UNE SEULE FOIS, parce que les noms ne changent pas en cours de partie :
	// un controle par demi-seconde n'apprendrait rien de plus et remplirait le
	// journal.
	const bool bControle = !bNomsVerifies;
	bNomsVerifies = true;
	// LE COMPTE SE DERIVE, IL NE S'ECRIT PAS EN DUR.
	//
	// La ligne de bilan annoncait « les 13 variables d'UDS/UDW acceptent
	// l'ecriture » avec le treize dans la CHAINE : ajouter une ecriture laissait
	// donc le journal mentir, et ce depot a une entree entiere sur cette classe
	// de defaut -- un chiffre plausible qu'on lit comme une mesure.
	TArray<FString> Refusees;
	int32 Tentees = 0;
	auto Ecrire = [this, bControle, &Refusees, &Tentees](FName Nom, double Valeur)
	{
		++Tentees;
		const bool bPrise = Bridge.WriteNumber(Nom, Valeur);
		if (bControle && !bPrise)
		{
			Refusees.Add(Nom.ToString());
		}
	};

	// --- LES SURCHARGES S'ARMENT UNE FOIS, ET AVANT TOUTE VALEUR --------------
	//
	// Sans elles, UDS reprend la main sur chaque curseur depuis son propre
	// systeme de TYPES de meteo -- qui ne tire jamais rien chez nous, puisque
	// nous posons des valeurs continues -- et notre ecriture est ecrasee au tick
	// suivant. Le booleen se pose au premier passage seulement : le reposer deux
	// fois par seconde ne servirait a rien, et ce depot a deja paye qu'une
	// ecriture de propriete declenche un rappel meme quand la valeur ne change
	// pas -- c'est ce qui faisait tomber l'editeur sur les composants PCG.
	//
	// ⚠ CE BLOC ETAIT PLACE APRES LES ECRITURES, et son propre commentaire
	// disait deja « ET AVANT LA VALEUR ». Le tonnerre s'en tirait parce qu'il
	// ecrit plus bas, mais `Dust`, `Wind Intensity` et les quatre autres
	// partaient AVANT que leur surcharge soit armee : une trame perdue au
	// premier passage, et un temoin lance avec `-WorldseedQuitter` peut ne
	// photographier que celle-la. L'ordre est desormais le bon pour TOUS.
	if (bControle)
	{
		Bridge.WriteBool(NameThunderManuel, true);

		// L'A/B QUI SEPARE LES DEUX CHANGEMENTS, et il manquait.
		//
		// Le temoin de poussiere et ces surcharges ont ete poses ENSEMBLE, puis
		// mesures une seule fois : deux changements pour une mesure, ce qui ne
		// permet d'attribuer l'effet a aucun des deux. Ce depot proscrit
		// exactement ce montage, et il a deja regle quatre fois le mauvais
		// bouton faute de l'avoir monte.
		//
		// `-WorldseedPoussiereSurcharge=0` desarme les deux surcharges SANS
		// toucher au temoin : si l'image ne bouge pas, elles ne servaient pas a
		// rendre la poussiere visible -- elles resteraient justes pour autant,
		// puisque le releve les donne a faux et que le commentaire du tonnerre
		// dit ce que cela coute.
		int32 Surcharge = 1;
		FParse::Value(FCommandLine::Get(), TEXT("WorldseedPoussiereSurcharge="), Surcharge);
		if (Surcharge == 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] meteo : surcharges manuelles DESARMEES a la ")
				TEXT("demande (-WorldseedPoussiereSurcharge=0) -- temoin d'A/B"));
		}

		// LA POUSSIERE ET LE VENT, ET C'ETAIT LA CAUSE DU « je n'en vois jamais ».
		//
		// Releve du 29 septembre 2026 : les deux surcharges arrivent a FAUX,
		// instance comme defaut de classe, donc UDW recalculait ses propres
		// valeurs par-dessus les notres a chaque tick. Leurs rappels existent --
		// sonde validee sur `OnRep_Animate Time of Day`, que ce code appelle
		// deja avec succes -- et un booleen pose par reflexion n'en declenche
		// aucun.
		const bool bDustManuel = (Surcharge != 0)
			&& Bridge.WriteBool(NameDustManuel, true);
		const bool bDustReveille = (Surcharge != 0)
			&& Bridge.CallFunction(TEXT("OnRep_Dust - Manual Override"));
		const bool bVentManuel = (Surcharge != 0)
			&& Bridge.WriteBool(NameWindManuel, true);
		const bool bVentReveille = (Surcharge != 0)
			&& Bridge.CallFunction(TEXT("OnRep_Wind Intensity - Manual Override"));

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] meteo : surcharge manuelle -- poussiere %s (rappel %s), ")
			TEXT("vent %s (rappel %s)"),
			bDustManuel ? TEXT("ARMEE") : TEXT("REFUSEE (UDW ecrasera notre valeur)"),
			bDustReveille ? TEXT("appele") : TEXT("INTROUVABLE"),
			bVentManuel ? TEXT("ARMEE") : TEXT("REFUSEE (UDW ecrasera notre valeur)"),
			bVentReveille ? TEXT("appele") : TEXT("INTROUVABLE"));

		// --- LES DEUX EFFETS D'ECRAN, AVEC LEURS RAPPELS --------------------
		const bool bGivre = Bridge.WriteBool(NameGivreActif, true);
		const bool bGivreReveille = Bridge.CallFunction(
			TEXT("OnRep_Enable Screen Frost"));
		const bool bGouttes = Bridge.WriteBool(NameGouttesActives, true);
		const bool bGouttesReveillees = Bridge.CallFunction(
			TEXT("OnRep_Enable Screen Droplets"));

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] meteo : ecran -- givre %s (rappel %s), gouttes %s ")
			TEXT("(rappel %s)"),
			bGivre ? TEXT("ARME") : TEXT("REFUSE"),
			bGivreReveille ? TEXT("appele") : TEXT("absent"),
			bGouttes ? TEXT("ARMEES") : TEXT("REFUSEES"),
			bGouttesReveillees ? TEXT("appele") : TEXT("absent"));

		// --- L'ARC-EN-CIEL ET LA CHALEUR QUI TREMBLE ------------------------
		//
		// LES DEUX ARRIVENT ETEINTS, comme le givre et les gouttes avant eux --
		// releve du 29 septembre 2026 sur l'acteur du niveau : `Enable Rainbow`
		// et `Enable Heat Distortion` a FAUX. C'est la doctrine du pack, qui
		// livre desarme tout ce qui coute, et ce depot a maintenant paye quatre
		// fois pour l'apprendre.
		//
		// NI L'UN NI L'AUTRE NE DEMANDE DE PILOTAGE, et c'est ce qui les rend
		// bon marche a brancher : leur etat se deduit tout seul de ce que nous
		// ecrivons deja.
		//
		// L'ARC-EN-CIEL suit trois conditions, toutes tenues par l'etat meteo :
		// il faut de la pluie ou de la brume pour le rendre visible, un ciel
		// assez degage pour que le soleil atteigne la camera, et un soleil
		// assez BAS sur l'horizon. La troisieme est la plus interessante ici :
		// elle ne devient vraie que depuis que `Simulate Real Sun` est armee,
		// sans quoi le soleil suivait un arc identique a toutes les latitudes.
		//
		// LA CHALEUR QUI TREMBLE suit la TEMPERATURE d'UDW -- et nous la lui
		// donnons deja, saison par saison, depuis le prereglage climatique
		// (`<Saison> Temperature Min and Max`, quelques lignes plus haut). Elle
		// se leve donc d'elle-meme sur un desert chaud, et nulle part ailleurs.
		// LE VENT L'ETEINT DE LUI-MEME : l'effet est un mirage d'air immobile
		// au-dessus d'un sol surchauffe, et le pack le masque avec la poussiere
		// -- ce qui repond exactement a la demande, « dans les deserts quand il
		// n'y a pas de tempete ».
		// LE TEMOIN DE CHALEUR PASSE PAR `Manual Heat Distortion`, que le pack
		// expose pour cela. Il est indispensable ici plus qu'ailleurs : le
		// seuil du pack est 85 a 100 degres Fahrenheit -- 29,4 a 37,8 Celsius,
		// releve le 29 septembre 2026 dans `Heat Distortion Temperature Range`
		// -- quand notre desert le plus chaud fait 28,6 de MOYENNE ANNUELLE.
		// L'effet ne se leve donc qu'aux heures chaudes de l'ete, et guetter ce
		// moment ne separerait pas « il ne fait pas assez chaud » de « la
		// chaine est morte ».
		float Tremblement = -1.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedChaleurForce="), Tremblement)
			&& Tremblement >= 0.0f)
		{
			Bridge.WriteNumber(TEXT("Manual Heat Distortion"),
				FMath::Clamp(Tremblement, 0.0f, 1.0f));
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] meteo : CHALEUR FORCEE a %.2f -- temoin"),
				FMath::Clamp(Tremblement, 0.0f, 1.0f));
		}

		const bool bArc = Bridge.WriteBool(TEXT("Enable Rainbow"), true);
		const bool bArcReveille = Bridge.CallFunction(TEXT("OnRep_Enable Rainbow"));
		const bool bChaleur = Bridge.WriteBool(TEXT("Enable Heat Distortion"), true);
		const bool bChaleurReveillee = Bridge.CallFunction(
			TEXT("OnRep_Enable Heat Distortion"));

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] meteo : arc-en-ciel %s (rappel %s), chaleur qui ")
			TEXT("tremble %s (rappel %s)"),
			bArc ? TEXT("ARME") : TEXT("REFUSE"),
			bArcReveille ? TEXT("appele") : TEXT("absent"),
			bChaleur ? TEXT("ARMEE") : TEXT("REFUSEE"),
			bChaleurReveillee ? TEXT("appele") : TEXT("absent"));

		// --- LE DOSAGE DES PARTICULES ---------------------------------------
		//
		// UN FACTEUR, PAS UNE VALEUR ABSOLUE : les chiffres du pack sont son
		// calage, et le multiplier garde ses proportions internes -- c'est lui
		// qui sait ce qu'une particule coute a son systeme. Poser un nombre en
		// dur figerait notre reglage sur la version du pack du jour.
		//
		// ET LA POUSSIERE MONTE PLUS QUE LA NEIGE, parce qu'elle partait de bien
		// plus bas : mille contre vingt mille. Meme facteur, ce serait garder
		// l'ecart de vingt.
		//
		// `PushWeather` est const et ne porte pas les regles : on les relit une
		// fois, ici, comme le terrain le fait pour la reptation. Un defaut sert
		// si elles manquent -- une meteo sans regles doit rester jouable.
		double NombrePoussiere = 1000.0 * 6.0;
		double NombreNeige = 20000.0 * 1.5;
		double AlphaPoussiere = 0.85;
		{
			FString Erreur;
			if (const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur))
			{
				NombrePoussiere = R->Num(TEXT("uds"), TEXT("particulesPoussiere"),
					NombrePoussiere);
				NombreNeige = R->Num(TEXT("uds"), TEXT("particulesNeige"), NombreNeige);
				AlphaPoussiere = R->Num(TEXT("uds"), TEXT("particulesPoussiereAlpha"),
					AlphaPoussiere);
			}
		}

		// ET UNE SURCHARGE POUR BALAYER SANS RECOMPILER, parce que le bon
		// dosage se juge a l'image et qu'un A/B qui demande de rouvrir le
		// fichier de regles en changerait l'empreinte -- donc regenererait le
		// monde entre les deux moities.
		float Facteur = -1.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedParticules="), Facteur)
			&& Facteur >= 0.0f)
		{
			NombrePoussiere *= Facteur;
			NombreNeige *= Facteur;
		}

		NombrePoussiere = FMath::Clamp(NombrePoussiere, 0.0, 200000.0);
		NombreNeige = FMath::Clamp(NombreNeige, 0.0, 200000.0);
		AlphaPoussiere = FMath::Clamp(AlphaPoussiere, 0.0, 1.0);

		const bool bNbP = Bridge.WriteNumber(NameDustNombre, NombrePoussiere);
		const bool bNbN = Bridge.WriteNumber(NameNeigeNombre, NombreNeige);

		// L'OPACITE SUIT, parce que multiplier des grains transparents ne rend
		// pas une tempete : la poussiere du pack est a 0,60 quand la neige est
		// a 1,00, et c'est l'autre moitie de l'ecart.
		Bridge.WriteNumber(NameDustAlpha, AlphaPoussiere);

		// LA TAILLE RESTE A CELLE DU PACK, a dessein : grossir un grain de sable
		// le fait lire comme un flocon, et c'est le NOMBRE qui fait la densite.
		double TailleP = 0.0, TailleN = 0.0;
		Bridge.ReadNumber(NameDustTaille, TailleP);
		Bridge.ReadNumber(NameNeigeTaille, TailleN);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] meteo : particules -- poussiere %.0f%s (alpha %.2f, ")
			TEXT("taille %.2f), neige %.0f%s (taille %.2f)"),
			NombrePoussiere, bNbP ? TEXT("") : TEXT(" REFUSEE"), AlphaPoussiere,
			TailleP, NombreNeige, bNbN ? TEXT("") : TEXT(" REFUSEE"), TailleN);

		// ET L'INTERRUPTEUR MAITRE DE L'AURORE, AVEC SON RAPPEL.
		//
		// `Use Auroras` est une variable repliquee a RepNotify, comme
		// `Animate Time of Day` : la poser par reflexion arme le drapeau -- il
		// se relit meme a vrai -- sans reveiller quoi que ce soit, parce que
		// c'est `OnRep_Use Auroras` qui recalcule `Using Either Aurora` et
		// `Using Volumetric Aurora`, les deux drapeaux derives que le rendu
		// consulte. Ce depot a paye exactement cela sur l'horloge : tous les
		// controles disaient oui, et l'heure restait figee.
		const bool bAurorePosee = Bridge.WriteBool(NameAuroreActive, true);
		const bool bAuroreReveillee = Bridge.CallFunction(TEXT("OnRep_Use Auroras"));

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] ciel : aurores %s, rappel OnRep %s"),
			bAurorePosee ? TEXT("ARMEES") : TEXT("REFUSEES (rien ne s'affichera)"),
			bAuroreReveillee ? TEXT("appele") : TEXT("INTROUVABLE"));
	}

	Ecrire(NameRain, Current.Rain);
	Ecrire(NameSnow, Current.Snow);
	Ecrire(NameFog, Current.Fog);
	Ecrire(NameDust, Current.Dust);
	Ecrire(NameCloudCoverage, Current.CloudCoverage);
	Ecrire(NameWindIntensity, Current.WindIntensity);
	Ecrire(NameWindDirection, Current.WindDirectionDeg);
	Ecrire(NameThunder, Current.Thunder);
	Ecrire(NameAurora, Current.Aurora);

	const bool bFahrenheit =
		Bridge.TemperatureScale == FWorldseedUdsBridge::ETemperatureScale::Fahrenheit;

	for (int32 S = 0; S < FWorldseedClimatePreset::SeasonCount; ++S)
	{
		const FVector2D& C = Current.SeasonMinMaxC[S];
		const FVector2D Value = bFahrenheit
			? FVector2D(CelsiusToFahrenheit(static_cast<float>(C.X)),
				CelsiusToFahrenheit(static_cast<float>(C.Y)))
			: C;

		++Tentees;
		if (!Bridge.WriteRange(SeasonRangeNames[S], Value) && bControle)
		{
			Refusees.Add(SeasonRangeNames[S].ToString());
		}
	}

	if (bControle)
	{
		// L'HORLOGE EST LE SECOND POINT DE RUPTURE POSSIBLE, et il est muet lui
		// aussi : la phase de l'annee est LUE dans UDS, qui ne la fait avancer
		// que si `Animate Time of Day` est arme. Horloge arretee, la saison ne
		// bouge jamais et tout le calage saisonnier devient decoratif -- quatre
		// saisons par climat, inversion des hemispheres, tout cela sans effet.
		bool bHorloge = false;
		if (!Bridge.ReadClockRunning(bHorloge))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] meteo : « Animate Time of Day » introuvable sur %s ")
				TEXT("-- impossible de dire si la saison avancera"), *Bridge.Describe());
		}
		else if (!bHorloge && WorldseedCielClairDemande())
		{
			// ON NE CRIE PAS SUR LE COMPORTEMENT DEMANDE. Sous le ciel
			// d'inspection l'horloge est figee A DESSEIN, et l'avertissement
			// ci-dessous conseillerait d'armer ce qu'on vient volontairement de
			// desarmer -- du bruit qui se lit comme une information.
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] meteo : horloge figee (ciel d'inspection) -- la ")
				TEXT("saison ne bougera pas, c'est voulu"));
		}
		else if (!bHorloge)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] meteo : L'HORLOGE D'UDS EST ARRETEE. La saison ne ")
				TEXT("bougera pas, donc la meteo restera celle d'une seule saison ")
				TEXT("toute la partie. Armer « Animate Time of Day » sur l'acteur ")
				TEXT("Ultra Dynamic Sky de la carte."));
		}

		// ET LA LONGUEUR DE L'ANNEE, second reglage que `BP_WorldseedClimat`
		// posait avant le portage. Une annee de 365 jours dure ici deux cent
		// soixante-quatorze HEURES reelles -- la saison ne changerait jamais,
		// horloge armee ou non. `CAL_Worldseed` la ramene a trente-six jours,
		// soit vingt-sept heures, et une saison a six heures quarante-cinq.
		double Jours = 0.0;
		FString NomCalendrier;
		if (!Bridge.ReadYearLength(Jours, NomCalendrier))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] meteo : AUCUN CALENDRIER sur %s -- la saison ne ")
				TEXT("pourra pas avancer"), *Bridge.Describe());
		}
		else if (Jours > 100.0)
		{
			// A quarante-cinq minutes reelles par journee, trois cent
			// soixante-cinq jours font deux cent soixante-quatorze heures : la
			// saison ne changerait jamais, horloge armee ou non.
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] meteo : calendrier « %s », %.0f jours -- soit %.0f ")
				TEXT("heures reelles par annee. La saison ne changera pas en ")
				TEXT("pratique. Assigner CAL_Worldseed."),
				*NomCalendrier, Jours, Jours * 0.75);
		}
		else
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] meteo : calendrier « %s », %.0f jours, soit %.1f ")
				TEXT("heures par annee et %.1f par saison"),
				*NomCalendrier, Jours, Jours * 0.75, Jours * 0.75 / 4.0);
		}

		if (Refusees.IsEmpty())
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] meteo : les %d variables d'UDS/UDW acceptent l'ecriture")
				TEXT(" -- horloge %s"), Tentees,
				bHorloge ? TEXT("EN MARCHE") : TEXT("arretee"));
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] meteo : %d variable(s) REFUSEE(S) par %s -- %s. ")
				TEXT("Ces grandeurs ne seront jamais pilotees : le nom a change ")
				TEXT("de version, ou l'acteur ne les porte pas."),
				Refusees.Num(), *Bridge.Describe(), *FString::Join(Refusees, TEXT(", ")));
		}
	}
}


// ============================================================ LES PAS DLWE

namespace
{
	/**
	 * Le chemin de la classe du pack, en DUR et en MOU a la fois.
	 *
	 * En dur parce qu'il n'y a pas d'autre facon de nommer un Blueprint d'un
	 * pack absent du depot ; en mou parce que `LoadClass` rend nul plutot que
	 * d'empecher le projet de se construire. Le suffixe `_C` est celui de la
	 * CLASSE GENEREE : sans lui on charge le Blueprint et non la classe, et
	 * `NewObject` refuserait.
	 */
	const TCHAR* const CheminClasseDlwe =
		TEXT("/Game/UltraDynamicSky/Blueprints/Weather_Effects/")
		TEXT("DLWE_Interaction.DLWE_Interaction_C");

	/** Notre materiau physique du sol, pose le 29 septembre 2026. */
	const TCHAR* const CheminPhysmat =
		TEXT("/Game/Worldseed/Physics/PM_WorldseedTerre.PM_WorldseedTerre");

	/** La liste blanche, MESUREE sur l'asset et non lue dans la documentation. */
	const FName NomListeBlanche(
		TEXT("Physical Materials which enable DLWE Interactions on non-Landscapes"));

	const FName NomReglages(TEXT("Interaction Settings"));

	/**
	 * Le socket d'un pied, par ordre de preference.
	 *
	 * LES SOCKETS AUTHORES D'ABORD, LES OS ENSUITE. Relevé sur
	 * `SKM_Quinn_Simple`, 94 entrees : `foot_l_Socket` et `foot_r_Socket`
	 * existent, et ils sont poses a la SEMELLE ; `foot_l` et `foot_r` sont des
	 * os, donc a la CHEVILLE -- une quinzaine de centimetres trop haut pour un
	 * contact. On prend le meilleur qui existe, et l'on DIT lequel : un repli
	 * silencieux ressemblerait a une mesure.
	 */
	struct FPiedDlwe
	{
		const TCHAR* Cote;
		TArray<FName> Candidats;
	};

	TArray<FPiedDlwe> PiedsDlwe()
	{
		return {
			{ TEXT("gauche"), { TEXT("foot_l_Socket"), TEXT("foot_l") } },
			{ TEXT("droit"),  { TEXT("foot_r_Socket"), TEXT("foot_r") } },
		};
	}
}

bool UWorldseedSkyDriverComponent::ReglerUnPasDlwe(USceneComponent* Composant,
	UObject* Physmat) const
{
	if (!Composant || !Physmat)
	{
		return false;
	}

	UObject* const ReglagesDuPack =
		FWorldseedUdsBridge::LireObjet(Composant, NomReglages);
	if (!ReglagesDuPack)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] pas DLWE : « %s » illisible ou nul sur %s. ")
			TEXT("Sans reglages, la liste blanche n'a pas d'hote et les douze ")
			TEXT("sons resteront muets."),
			*NomReglages.ToString(), *Composant->GetName());
		return false;
	}

	// LA COPIE EST OUTREE SUR LE COMPOSANT : privee, transitoire, et l'asset
	// partage du pack n'est jamais touche.
	UObject* const Copie = DuplicateObject<UObject>(ReglagesDuPack, Composant);
	if (!Copie)
	{
		return false;
	}

	int32 Taille = 0;
	if (!FWorldseedUdsBridge::AjouterAuTableauObjets(Copie, NomListeBlanche,
			Physmat, &Taille))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] pas DLWE : « %s » REFUSEE sur %s. Le nom a ")
			TEXT("change de version d'UDS, ou ce n'est pas un tableau de ")
			TEXT("materiaux physiques."),
			*NomListeBlanche.ToString(), *Copie->GetClass()->GetName());
		return false;
	}

	if (!FWorldseedUdsBridge::EcrireObjet(Composant, NomReglages, Copie))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] pas DLWE : la copie des reglages n'a pas pu etre ")
			TEXT("REPOSEE sur %s -- le composant garde ceux du pack, sans ")
			TEXT("notre materiau."), *Composant->GetName());
		return false;
	}

	// ET L'ON RELIT, parce qu'une ecriture par reflexion qui rend vrai n'est
	// pas une preuve -- douze fois paye dans ce depot.
	const UObject* const Relu =
		FWorldseedUdsBridge::LireObjet(Composant, NomReglages);
	TArray<UObject*> ListeRelue;
	const bool bListeRelue = FWorldseedUdsBridge::LireTableauObjets(
		Relu, NomListeBlanche, ListeRelue);
	const bool bPhysmatPresent = ListeRelue.Contains(Physmat);

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] pas DLWE : %s -> reglages %s, liste %s (%d entree(s)), ")
		TEXT("notre physmat %s"),
		*Composant->GetName(),
		Relu == Copie ? TEXT("A NOUS") : TEXT("PAS les notres"),
		bListeRelue ? TEXT("relue") : TEXT("ILLISIBLE"),
		ListeRelue.Num(),
		bPhysmatPresent ? TEXT("PRESENT") : TEXT("ABSENT"));

	return Relu == Copie && bPhysmatPresent && Taille > 0;
}

void UWorldseedSkyDriverComponent::ArmerLesPasDlwe()
{
	if (bPasDlweIndisponible)
	{
		return;
	}

	UWorld* const Monde = GetWorld();
	APawn* const Pion = Monde ? UGameplayStatics::GetPlayerPawn(Monde, 0) : nullptr;
	if (!Pion || PionEquipe.Get() == Pion)
	{
		return;
	}

	// LE MAILLAGE AVANT TOUT LE RESTE : sans squelette, aucun socket, donc rien
	// a quoi parenter. On ne journalise pas ce cas a chaque trame -- il se
	// produirait pour un pion sans maillage, et le pion suivant reessaiera.
	const ACharacter* const Perso = Cast<ACharacter>(Pion);
	USkeletalMeshComponent* const Maille = Perso ? Perso->GetMesh() : nullptr;
	if (!Maille)
	{
		return;
	}

	UClass* const Classe = LoadClass<USceneComponent>(nullptr, CheminClasseDlwe);
	if (!Classe)
	{
		// PAS UNE ERREUR : le jeu doit tourner sans le pack. Mais dit une fois,
		// sinon l'absence se lirait comme un silence.
		bPasDlweIndisponible = true;
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] pas DLWE : %s introuvable -- pack absent, les ")
			TEXT("douze sons d'interaction resteront muets et c'est normal."),
			CheminClasseDlwe);
		return;
	}

	UObject* const Physmat = StaticLoadObject(UObject::StaticClass(), nullptr,
		CheminPhysmat);
	if (!Physmat)
	{
		bPasDlweIndisponible = true;
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] pas DLWE : notre materiau physique %s est ")
			TEXT("INTROUVABLE. Sans lui la liste blanche resterait vide et ")
			TEXT("DLWE ignorerait le terrain voxel."), CheminPhysmat);
		return;
	}

	// LE PION EST MARQUE AVANT LA BOUCLE, PAS APRES. Un echec partiel ne doit
	// pas faire recommencer a la trame suivante : on empilerait des composants
	// sur le meme pied.
	PionEquipe = Pion;
	PasDlwe.Reset();
	TempsDepuisPasS = 0.0f;
	ReleveesDesPas = 0;

	int32 Poses = 0;
	int32 Regles = 0;
	for (const FPiedDlwe& Pied : PiedsDlwe())
	{
		FName Socket = NAME_None;
		for (const FName& Candidat : Pied.Candidats)
		{
			if (Maille->DoesSocketExist(Candidat))
			{
				Socket = Candidat;
				break;
			}
		}
		if (Socket.IsNone())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] pas DLWE : aucun socket pour le pied %s ")
				TEXT("(essayes : %s). Ce pied ne fera pas de bruit."),
				Pied.Cote, *FString::JoinBy(Pied.Candidats, TEXT(", "),
					[](const FName& N) { return N.ToString(); }));
			continue;
		}

		USceneComponent* const C = NewObject<USceneComponent>(Pion, Classe);
		if (!C)
		{
			continue;
		}
		C->RegisterComponent();
		C->AttachToComponent(Maille,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
		++Poses;
		PasDlwe.Add(C);

		// ON DIT QUEL SOCKET A SERVI. Le repli sur l'os place le contact a la
		// cheville : su, c'est un compromis ; tu, c'est un defaut qu'on
		// chercherait ailleurs.
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] pas DLWE : pied %s parente a « %s »%s"),
			Pied.Cote, *Socket.ToString(),
			Socket == Pied.Candidats[0] ? TEXT("")
				: TEXT(" (REPLI sur l'os : contact a la cheville)"));

		if (ReglerUnPasDlwe(C, Physmat))
		{
			++Regles;
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] pas DLWE : %d/2 composant(s) poses, %d regle(s) avec ")
		TEXT("notre materiau physique, sur %s"),
		Poses, Regles, *Pion->GetName());
}

void UWorldseedSkyDriverComponent::ReleverLesPasDlwe() const
{
	// LES QUATRE CONDITIONS, DANS L'ORDRE OU ELLES TOMBENT. Un silence n'a pas
	// une cause mais quatre, et elles n'appellent pas le meme remede :
	//
	//   Asleep                    le composant s'est endormi (hors distance)
	//   Sound Enabled             l'interrupteur du composant
	//   Snow Depth                ce qu'il VOIT sous le pied -- zero = pas de
	//                             neige la, ou physmat refuse par le pack
	//   Interaction Sound         la source audio, que le pack instancie
	//
	// C'EST LA TROISIEME QUI SEPARE LES DEUX HYPOTHESES COUTEUSES : une
	// profondeur nulle avec un physmat accepte veut dire « il n'y a pas de
	// neige ici », et l'on force la meteo ; une profondeur nulle partout, meteo
	// forcee, veut dire que le pack ne reconnait pas notre sol -- et l'on
	// revient a la liste blanche.
	int32 Eveilles = 0, Armes = 0, VoientDeLaNeige = 0, Sonnent = 0;

	for (const TWeakObjectPtr<USceneComponent>& Faible : PasDlwe)
	{
		const USceneComponent* const C = Faible.Get();
		if (!C)
		{
			continue;
		}

		bool bDort = false, bSon = false;
		double Neige = 0.0, Poussiere = 0.0, Flaque = 0.0;
		const bool bDortLu =
			FWorldseedUdsBridge::LireBooleenDe(C, TEXT("Asleep"), bDort);
		const bool bSonLu =
			FWorldseedUdsBridge::LireBooleenDe(C, TEXT("Sound Enabled"), bSon);
		const bool bNeigeLue =
			FWorldseedUdsBridge::LireNombreDe(C, TEXT("Snow Depth"), Neige);
		FWorldseedUdsBridge::LireNombreDe(C, TEXT("Dust Depth"), Poussiere);
		FWorldseedUdsBridge::LireNombreDe(C, TEXT("Puddle Fluid Depth"), Flaque);

		// ⚠ `IsPlaying()` NE DISCRIMINE RIEN, MESURE LE 29 SEPTEMBRE 2026, et
		// cette ligne a d'abord menti. A/B apparie, horloge figee : la source
		// rend VRAI avec `-WorldseedNeigeForce=10` ET avec zero neige. C'est un
		// MetaSound persistant -- le pack l'instancie et le laisse tourner, la
		// selection se faisant a l'interieur du graphe. Un composant audio qui
		// « joue » ne prouve donc PAS qu'un son sort.
		//
		// On garde la colonne parce que l'ABSENCE de source, elle, serait
		// concluante -- mais on ne l'appelle plus « JOUE ».
		const UAudioComponent* const Source = Cast<UAudioComponent>(
			FWorldseedUdsBridge::LireObjet(C, TEXT("Interaction Sound")));

		// LA DISTANCE D'ACTIVITE VIENT DES REGLAGES QU'ON A DUPLIQUES : si la
		// copie l'a perdue, le composant dormirait pour cette raison, et ce
		// serait notre faute et non celle du pack.
		double Portee = -1.0;
		const UObject* const Reglages =
			FWorldseedUdsBridge::LireObjet(C, TEXT("Interaction Settings"));
		const bool bPorteeLue = FWorldseedUdsBridge::LireNombreDe(
			Reglages, TEXT("Active Distance"), Portee);

		if (bDortLu && !bDort) { ++Eveilles; }
		if (bSonLu && bSon) { ++Armes; }
		if (bNeigeLue && Neige > 0.0) { ++VoientDeLaNeige; }
		if (Source) { ++Sonnent; }

		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] pas DLWE #%d : %s | Asleep=%s%s | %s%s | ")
			TEXT("neige %.3f%s poussiere %.3f flaque %.3f | portee %.0f%s | ")
			TEXT("source %s"),
			ReleveesDesPas, *C->GetName(),
			bDort ? TEXT("VRAI") : TEXT("faux"),
			bDortLu ? TEXT("") : TEXT(" (ILLISIBLE)"),
			bSon ? TEXT("son arme") : TEXT("SON COUPE"),
			bSonLu ? TEXT("") : TEXT(" (ILLISIBLE)"),
			Neige, bNeigeLue ? TEXT("") : TEXT(" (ILLISIBLE)"),
			Poussiere, Flaque,
			Portee, bPorteeLue ? TEXT("") : TEXT(" (ILLISIBLE)"),
			Source ? *Source->GetName() : TEXT("AUCUNE"));
	}

	// LE COMPTE EN DERNIER, ET IL SE LIT SEUL -- MAIS IL NE DIT PAS
	// L'AUDIBILITE, ET C'EST ECRIT DANS LA LIGNE POUR QU'ON NE L'Y LISE PAS.
	// « source » compte des composants audio EXISTANTS, pas des sons entendus :
	// la seule preuve d'audibilite reste l'oreille, ou un enregistrement de
	// submix compare entre deux etats.
	UE_LOG(LogTemp, Warning,
		TEXT("[Worldseed] pas DLWE releve %d/4 : %d/%d non endormis, %d armes, ")
		TEXT("%d voient de la neige, %d sources (existence, PAS audibilite)"),
		ReleveesDesPas, Eveilles, PasDlwe.Num(), Armes, VoientDeLaNeige,
		Sonnent);
}

// ==================================================== LES TEMPETES RADIALES

namespace
{
	/**
	 * LES NOMS SONT RELEVES SUR L'ACTEUR, PAS LUS DANS LA DOCUMENTATION.
	 *
	 * Enumeration du 29 septembre 2026 : 584 variables et 408 fonctions sur
	 * `Ultra_Dynamic_Weather`. Ce chantier a deja paye deux fois le fait qu'un
	 * nom de documentation ne dit pas QUI porte la propriete -- la liste blanche
	 * de DLWE vivait sur un objet de reglages, pas sur l'acteur.
	 *
	 * ET L'INTERRUPTEUR PORTE UN `OnRep_`, donc poser le booleen ne suffit pas :
	 * onze fois paye dans ce depot. On appelle le rappel, puis le demarrage.
	 */
	const FName NomInterrupteurOrage(TEXT("Enable Radial Storm Spawning"));
	const FName NomRappelOrage(TEXT("OnRep_Enable Radial Storm Spawning"));
	const FName NomDemarrageOrage(TEXT("Start Up Radial Storm Spawning"));
	const FName NomSpawnOrage(TEXT("Spawn Radial Storm"));
	const FName NomChargementOrage(TEXT("Load Radial Storm Class"));
	const FName NomClasseDure(TEXT("Radial Storm Class Hard"));

	// --- LA GEOMETRIE DE LA TEMPETE, EN FRACTIONS DU MONDE -------------------
	//
	// LE PACK POSE DES METRIQUES FIGEES, ET ELLES SONT HORS D'ECHELLE ICI :
	// rayon 13 km, naissance et mort a 25 km. Sur un monde de 64 x 32 km, un
	// diametre de 26 km fait QUATRE-VINGT-UN POUR CENT de la hauteur du monde --
	// et le proprietaire l'a constate a l'oeil le 29 septembre 2026 : a dix
	// kilometres, elle REMPLISSAIT LE CIEL. Une tempete censee rapporter le LIEU
	// reproduisait donc le defaut qu'elle devait corriger, une meteo quasi
	// uniforme avec seulement un bord qui bouge.
	//
	// LA REGLE DU PROJET EST CELLE-CI : un reglage s'exprime en FRACTION du
	// monde ou en quantile, jamais en metrique figee -- parce que la taille du
	// monde est un choix, et qu'un chiffre en kilometres cesse d'etre juste des
	// qu'elle change.
	//
	// LA REFERENCE EST LA HAUTEUR, PAS LA LARGEUR. C'est la petite dimension,
	// donc celle qui CONTRAINT : un reglage exprime sur la largeur passerait
	// deux fois trop grand du nord au sud. Et c'est en hauteur que le monde se
	// lit, la latitude allant d'un pole a l'autre sur cette distance.
	//
	// LES TROIS VALEURS, ET POURQUOI CELLES-LA (monde de 32 km de haut) :
	//
	//   rayon     1/8  -> 4 km, soit un diametre de 8 km = un quart de la
	//                     hauteur du monde. A la distance de naissance il
	//                     sous-tend environ 28 degres : une FORMATION sur
	//                     l'horizon, ce qu'on cherchait, et non un plafond.
	//   distance  1/2  -> 16 km : la traversee vaut donc exactement UNE hauteur
	//                     de monde, ce qui est une quantite qu'on peut dire.
	//   dispersion 1/8 -> 4 km, pour que deux tempetes ne naissent pas sur le
	//                     meme cercle -- le pack livre zero, donc une distance
	//                     de naissance rigoureusement constante.
	constexpr double FractionRayonOrage = 0.125;
	constexpr double FractionDistanceOrage = 0.5;
	constexpr double FractionDispersionOrage = 0.125;

	const FName NomRayonOrage(TEXT("Radial Storm Outer Radius"));
	const FName NomDistanceDebut(TEXT("Radial Storm Start Distance"));
	const FName NomDistanceFin(TEXT("Radial Storm End Distance"));
	const FName NomDispersionOrage(TEXT("Radial Storm Spawn Random Offset"));
}

void UWorldseedSkyDriverComponent::ArmerLesOragesRadiaux()
{
	if (bOragesRadiauxArmes
		|| !FParse::Param(FCommandLine::Get(), TEXT("WorldseedOrageRadial")))
	{
		return;
	}
	bOragesRadiauxArmes = true;

	// L'ETAT D'AVANT, PARCE QU'UN TEMOIN COMMENCE PAR LA. Le pack livre faux ;
	// si on lisait vrai, c'est que la carte l'a deja arme et l'experience ne
	// dirait pas ce qu'on croit.
	bool bAvant = false;
	const bool bLu = Bridge.ReadBool(NomInterrupteurOrage, bAvant);

	const bool bEcrit = Bridge.WriteBool(NomInterrupteurOrage, true);
	const bool bRappel = Bridge.CallFunction(NomRappelOrage);
	const bool bDemarre = Bridge.CallFunction(NomDemarrageOrage);

	// ET ON RELIT, parce qu'un drapeau pose n'est pas un drapeau lu.
	bool bApres = false;
	const bool bRelu = Bridge.ReadBool(NomInterrupteurOrage, bApres);

	UE_LOG(LogTemp, Warning,
		TEXT("[Worldseed] orage radial : avant %s%s -> ecrit %s, OnRep_ %s, ")
		TEXT("demarrage %s, relu %s%s"),
		bAvant ? TEXT("VRAI") : TEXT("faux"),
		bLu ? TEXT("") : TEXT(" (ILLISIBLE)"),
		bEcrit ? TEXT("oui") : TEXT("REFUSE"),
		bRappel ? TEXT("appele") : TEXT("ABSENT"),
		bDemarre ? TEXT("appele") : TEXT("ABSENT"),
		bApres ? TEXT("VRAI") : TEXT("faux"),
		bRelu ? TEXT("") : TEXT(" (ILLISIBLE)"));

	// LA CLASSE SE CHARGE AVANT DE SPAWNER, ET C'EST UNE LECON PAYEE ICI MEME.
	// Le premier essai appelait `Spawn Radial Storm` dans la MEME trame que
	// l'armement : les quatre appels rendaient vrai, l'interrupteur se relisait
	// VRAI, et ZERO acteur naissait sur soixante secondes. Or l'enumeration
	// nomme le coupable : `Radial Storm Class` est un SOFTCLASS, double d'un
	// `Radial Storm Class Hard`, et le pack livre `Load Radial Storm Class`
	// pour resoudre l'un dans l'autre. Un spawn sur une classe nulle ne fait
	// rien -- et ne le dit pas.
	const bool bCharge = Bridge.CallFunction(NomChargementOrage);
	UE_LOG(LogTemp, Warning,
		TEXT("[Worldseed] orage radial : « %s » %s. Le spawn est DIFFERE au ")
		TEXT("premier releve : un softclass peut se charger de facon asynchrone, ")
		TEXT("et spawner dans la meme trame retomberait sur une classe nulle."),
		*NomChargementOrage.ToString(),
		bCharge ? TEXT("appelee") : TEXT("ABSENTE"));

	RecadrerLaGeometrieDesOrages();
}

void UWorldseedSkyDriverComponent::RecadrerLaGeometrieDesOrages() const
{
	// LA TAILLE DU MONDE SE LIT SUR LE TERRAIN CHARGE, PAS DANS LES REGLES.
	// `UWorldseedRules::Geometry` porte un defaut de 8 km ; la hauteur REELLE
	// est celle du monde qu'on a genere, et c'est le terrain qui la detient.
	// Prendre le defaut donnerait des fractions justes d'un monde qui n'existe
	// pas -- exactement la forme du piege « un chiffre derive recopie ».
	// PAS DE BOUCLE AVEC `break` : elle rendrait l'increment de l'iterateur
	// inatteignable, et ce module compile ses avertissements en erreurs. On
	// prend le premier et l'on s'arrete, sans boucler.
	const AWorldseedVoxelTerrain* Terrain = nullptr;
	if (UWorld* const Monde = GetWorld())
	{
		TActorIterator<AWorldseedVoxelTerrain> It(Monde);
		Terrain = It ? *It : nullptr;
	}
	if (!Terrain)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] orage radial : aucun terrain voxel -- la taille ")
			TEXT("du monde est inconnue, donc les distances du pack sont ")
			TEXT("LAISSEES TELLES QUELLES (13 et 25 km, hors d'echelle)."));
		return;
	}

	const double HauteurKm = Terrain->MondeGeometrie().HeightM / 1000.0;
	if (HauteurKm <= 0.0)
	{
		return;
	}

	// LES SURCHARGES SONT DES FRACTIONS, PAS DES KILOMETRES, et c'est le point
	// de toute cette fonction : on itere sans jamais reintroduire un chiffre
	// figé. Et elles evitent de toucher a `world_rules.json`, dont l'empreinte
	// est un MD5 du fichier ENTIER -- une virgule y invaliderait tous les
	// mondes en cache pour un reglage cosmetique.
	double FracRayon = FractionRayonOrage;
	double FracDistance = FractionDistanceOrage;
	double FracDispersion = FractionDispersionOrage;
	float Lu = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedOrageRayon="), Lu))
	{
		FracRayon = FMath::Max(0.001f, Lu);
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedOrageDistance="), Lu))
	{
		FracDistance = FMath::Max(0.001f, Lu);
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedOrageDispersion="), Lu))
	{
		FracDispersion = FMath::Max(0.0f, Lu);
	}

	const double RayonKm = FracRayon * HauteurKm;
	const double DistanceKm = FracDistance * HauteurKm;
	const double DispersionKm = FracDispersion * HauteurKm;

	// ⚠ L'UNITE DU PACK EST LE KILOMETRE, et ce n'est pas une supposition : la
	// tempete est nee a 25,0 km quand `Start Distance` valait 25,0, mesure au
	// releve du 29 septembre 2026.
	const bool bRayon = Bridge.WriteNumber(NomRayonOrage, RayonKm);
	const bool bDebut = Bridge.WriteNumber(NomDistanceDebut, DistanceKm);
	const bool bFin = Bridge.WriteNumber(NomDistanceFin, DistanceKm);
	const bool bDisp = Bridge.WriteNumber(NomDispersionOrage, DispersionKm);

	// ET L'ON RELIT LES QUATRE. Une ecriture par reflexion ne signale rien
	// quand elle echoue, et ce chantier a paye quatre fois aujourd'hui un appel
	// qui rendait vrai sans rien faire.
	double RayonRelu = 0.0, DebutRelu = 0.0, FinRelue = 0.0, DispRelue = 0.0;
	Bridge.ReadNumber(NomRayonOrage, RayonRelu);
	Bridge.ReadNumber(NomDistanceDebut, DebutRelu);
	Bridge.ReadNumber(NomDistanceFin, FinRelue);
	Bridge.ReadNumber(NomDispersionOrage, DispRelue);

	// L'ANGLE EST LA GRANDEUR QUI COMPTE, parce que c'est elle qu'on VOIT. Le
	// releve precedent disait « elle remplissait le ciel » : a dix kilometres,
	// un rayon de 13 km met l'observateur DEDANS. Ce chiffre rend la nouvelle
	// geometrie jugeable sans relancer.
	const double AngleDeg = 2.0 * FMath::RadiansToDegrees(
		FMath::Atan2(RayonRelu, FMath::Max(0.001, DebutRelu)));

	UE_LOG(LogTemp, Warning,
		TEXT("[Worldseed] orage radial : monde haut de %.0f km -- rayon %.3f ")
		TEXT("-> %.1f km (relu %.1f%s), distance %.3f -> %.1f km (relu %.1f/%.1f%s), ")
		TEXT("dispersion %.3f -> %.1f km (relu %.1f%s) | a la naissance la ")
		TEXT("tempete sous-tend %.0f degres"),
		HauteurKm,
		FracRayon, RayonKm, RayonRelu, bRayon ? TEXT("") : TEXT(" REFUSE"),
		FracDistance, DistanceKm, DebutRelu, FinRelue,
		(bDebut && bFin) ? TEXT("") : TEXT(" REFUSE"),
		FracDispersion, DispersionKm, DispRelue, bDisp ? TEXT("") : TEXT(" REFUSE"),
		AngleDeg);
}

void UWorldseedSkyDriverComponent::ReleverLesOragesRadiaux() const
{
	const UWorld* const Monde = GetWorld();
	if (!Monde)
	{
		return;
	}

	// LA CLASSE D'ABORD : c'est elle qui separe « le pack refuse de spawner »
	// de « le pack a spawne et l'acteur est mort ou ailleurs ». Sans cette
	// colonne, un zero ne se diagnostique pas.
	const UObject* const ClasseDure = FWorldseedUdsBridge::LireObjet(
		Bridge.MeteoBrute(), NomClasseDure);

	// ET LE SPAWN SE DECLENCHE ICI, AU PREMIER RELEVE -- donc apres que le
	// chargement de l'armement a eu une trame pour aboutir.
	if (ReleveesDesOrages == 1)
	{
		const bool bSpawn = ClasseDure
			? Bridge.CallFunction(NomSpawnOrage) : false;
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] orage radial : classe dure %s -- « %s » %s"),
			ClasseDure ? *ClasseDure->GetName() : TEXT("NULLE"),
			*NomSpawnOrage.ToString(),
			bSpawn ? TEXT("appelee") : TEXT("PAS appelee (classe nulle)"));
	}

	// ON COMPTE LES ACTEURS, ET L'ON NOMME CE QU'ON A COMPTE. Un compte nu se
	// lit comme une preuve ; un compte avec les noms se verifie. Le filtre
	// porte sur le nom de CLASSE parce que la classe du pack est un Blueprint
	// absent du depot -- on ne peut pas la citer a la compilation.
	// LA REFERENCE EST LE JOUEUR, PAS NOTRE ACTEUR -- ET LE PREMIER JET AVAIT
	// TORT. Il mesurait depuis `GetOwner()`, l'acteur de climat, qui se tient a
	// l'origine du monde : il annoncait 56 km quand le pack fait naitre ses
	// tempetes AUTOUR DU JOUEUR, a 25 de sa distance de depart. Le chiffre
	// n'etait pas faux, il ne mesurait pas la bonne chose -- et sur un monde de
	// 64 x 32 km il envoyait chercher hors de la carte.
	const APawn* const Pion = UGameplayStatics::GetPlayerPawn(
		const_cast<UWorld*>(Monde), 0);
	const FVector Oeil = Pion ? Pion->GetActorLocation() : FVector::ZeroVector;

	int32 Orages = 0;
	TArray<FString> Noms;
	for (TActorIterator<AActor> It(const_cast<UWorld*>(Monde)); It; ++It)
	{
		const AActor* const A = *It;
		if (!A || !A->GetClass())
		{
			continue;
		}
		if (A->GetClass()->GetName().Contains(TEXT("Radial_Storm")))
		{
			++Orages;

			// ON VA LA VOIR DES QU'ELLE EXISTE, et une seule fois : c'est un
			// outil de regard, arme par `-WorldseedOrageFace=<km>` et inerte
			// autrement.
			const_cast<UWorldseedSkyDriverComponent*>(this)
				->EnvoyerLeJoueurVoirLOrage(A);

			if (Noms.Num() < 6)
			{
				// ET ON DONNE LE CAP, parce qu'une distance ne dit pas ou
				// TOURNER LA TETE. Une tempete se juge a l'oeil, et une mesure
				// qui ne permet pas d'aller voir son sujet ne sert qu'a moitie.
				const FVector Vers = A->GetActorLocation() - Oeil;
				Noms.Add(FString::Printf(TEXT("%s a %.1f km, cap %.0f deg"),
					*A->GetName(), Vers.Size() / 100000.0,
					FMath::UnwindDegrees(
						FMath::RadiansToDegrees(FMath::Atan2(Vers.Y, Vers.X)))));
			}
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[Worldseed] orage radial releve %d : classe %s, %d acteur(s) de ")
		TEXT("tempete%s%s"),
		ReleveesDesOrages,
		ClasseDure ? TEXT("chargee") : TEXT("NULLE"), Orages,
		Orages > 0 ? TEXT(" -- ") : TEXT(""),
		Orages > 0 ? *FString::Join(Noms, TEXT(", ")) : TEXT(""));
}

void UWorldseedSkyDriverComponent::EnvoyerLeJoueurVoirLOrage(const AActor* Orage)
{
	float DistanceKm = 0.0f;
	if (bJoueurEnvoyeVoirLOrage || !Orage
		|| !FParse::Value(FCommandLine::Get(), TEXT("WorldseedOrageFace="),
			DistanceKm))
	{
		return;
	}
	bJoueurEnvoyeVoirLOrage = true;

	UWorld* const Monde = GetWorld();
	APawn* const Pion = Monde
		? UGameplayStatics::GetPlayerPawn(Monde, 0) : nullptr;
	ACharacter* const Perso = Cast<ACharacter>(Pion);
	UCharacterMovementComponent* const Mouvement =
		Perso ? Perso->GetCharacterMovement() : nullptr;
	if (!Mouvement)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] orage radial : aucun pion a deplacer -- ")
			TEXT("`-WorldseedOrageFace=` ne vaut qu'en jeu."));
		return;
	}

	// LE POINT DE VUE EST SUR LE SEGMENT ENTRE LA TEMPETE ET LE JOUEUR, du cote
	// du joueur : on se rapproche d'elle sans passer derriere, donc elle
	// continue de venir VERS nous, ce qui est tout l'interet.
	const FVector PosOrage = Orage->GetActorLocation();
	const FVector PosPion = Pion->GetActorLocation();
	const FVector Vers = PosPion - PosOrage;
	const double DistanceCm = FMath::Max(1.0, DistanceKm * 100000.0);
	const FVector Vue = PosOrage
		+ Vers.GetSafeNormal2D() * DistanceCm
		// L'ALTITUDE EST ABSOLUE, PAS RELATIVE AU SOL. A vingt kilometres du
		// joueur le chunk n'est pas maille -- le rayon de chargement fait
		// 250 m -- donc il n'y a RIEN sous les pieds a interroger. Deux mille
		// metres passent au-dessus du relief de ce monde.
		+ FVector(0.0, 0.0, 200000.0 - PosPion.Z);

	Pion->SetActorLocation(Vue, /*bSweep=*/false, nullptr,
		ETeleportType::TeleportPhysics);

	// EN VOL, ET C'EST CE QUI EVITE LA CHUTE. Sans ce mode le pion tomberait de
	// deux mille metres a travers un monde non maille, et l'on photographierait
	// une chute libre.
	Mouvement->SetMovementMode(MOVE_Flying);
	Mouvement->MaxFlySpeed = 6000.0f;
	Mouvement->BrakingDecelerationFlying = 4000.0f;

	// LE CAP SE POSE SUR LE CONTROLEUR, pas sur l'acteur : en vue a la
	// troisieme personne, c'est la rotation de CONTROLE qui oriente la camera.
	const FRotator Visee = (PosOrage - Vue).Rotation();
	if (APlayerController* const PC =
			UGameplayStatics::GetPlayerController(Monde, 0))
	{
		PC->SetControlRotation(Visee);
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[Worldseed] orage radial : joueur pose a (%.0f, %.0f) m, ")
		TEXT("altitude 2000 m, EN VOL, a %.1f km de la tempete -- cap %.0f deg, ")
		TEXT("site %.0f deg"),
		Vue.X / WorldseedMetersToCm, Vue.Y / WorldseedMetersToCm,
		DistanceKm, Visee.Yaw, Visee.Pitch);
}
