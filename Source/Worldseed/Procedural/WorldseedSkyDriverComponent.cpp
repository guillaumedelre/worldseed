// Worldseed - pilotage du ciel depuis le climat local.

#include "Procedural/WorldseedSkyDriverComponent.h"

#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedWeatherReadout.h"

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
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] regles illisibles (%s) : valeurs de repli pour le ciel"),
				*Error);
			bPresetRulesLoaded = true;   // on n'insiste pas a chaque passage
		}
	}

	// --- position du soleil --------------------------------------------------
	if (bDriveSunPosition
		&& FMath::Abs(Sample.LatitudeDeg - LastSunLatitudeDeg) >= SunLatitudeStepDeg)
	{
		if (Bridge.WriteLatLon(Sample.LatitudeDeg, LongitudeDeg))
		{
			LastSunLatitudeDeg = Sample.LatitudeDeg;
			UE_LOG(LogTemp, Log, TEXT("[Worldseed] ciel : latitude %.1f  longitude %.1f"),
				Sample.LatitudeDeg, LongitudeDeg);
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

	PushWeather();

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
			else if (FMath::IsNearlyEqual(Maintenant, HeureALArmement, 1e-4))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[Worldseed] horloge : ARMEE MAIS FIGEE a %.4f apres %.1f s. ")
					TEXT("UDS met sa vitesse en cache : il faut probablement poser les ")
					TEXT("durees AVANT son BeginPlay, ou dans la carte."),
					Maintenant, TempsDepuisArmementS);
			}
			else
			{
				UE_LOG(LogTemp, Log,
					TEXT("[Worldseed] horloge : le temps passe -- %.4f a %.4f en %.1f s"),
					HeureALArmement, Maintenant, TempsDepuisArmementS);
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

	// ON MEMORISE L'HEURE POUR VERIFIER QU'ELLE AVANCE. Armer n'est pas faire
	// avancer : la valeur peut etre posee et le temps rester fige, et c'est
	// precisement le genre d'echec muet que ce depot a paye plusieurs fois.
	// Le controle se fait quelques secondes plus tard, dans `Drive`.
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
	TArray<FString> Refusees;
	auto Ecrire = [this, bControle, &Refusees](FName Nom, double Valeur)
	{
		const bool bPrise = Bridge.WriteNumber(Nom, Valeur);
		if (bControle && !bPrise)
		{
			Refusees.Add(Nom.ToString());
		}
	};

	Ecrire(NameRain, Current.Rain);
	Ecrire(NameSnow, Current.Snow);
	Ecrire(NameFog, Current.Fog);
	Ecrire(NameDust, Current.Dust);
	Ecrire(NameCloudCoverage, Current.CloudCoverage);
	Ecrire(NameWindIntensity, Current.WindIntensity);
	Ecrire(NameWindDirection, Current.WindDirectionDeg);

	// LA SURCHARGE MANUELLE S'ARME UNE FOIS, ET AVANT LA VALEUR.
	//
	// Sans elle, UDS reprend la main sur « Thunder/Lightning » depuis son
	// propre systeme de types -- qui ne tire jamais rien chez nous -- et notre
	// ecriture serait ecrasee au tick suivant. Le booleen se pose au premier
	// passage seulement : le reposer deux fois par seconde ne servirait a rien
	// et ce depot a deja paye qu'une ecriture de propriete declenche un rappel
	// meme quand la valeur ne change pas.
	if (bControle)
	{
		Bridge.WriteBool(NameThunderManuel, true);

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
				TEXT("[Worldseed] meteo : les 13 variables d'UDS/UDW acceptent l'ecriture")
				TEXT(" -- horloge %s"), bHorloge ? TEXT("EN MARCHE") : TEXT("arretee"));
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
