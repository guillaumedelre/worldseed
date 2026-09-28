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
		const float Alpha = 1.0f - FMath::Exp(
			-FMath::Max(DeltaSeconds, 0.0f) / FMath::Max(BlendSeconds, 0.1f));
		WorldseedWeatherState::BlendTowards(Current, Target, Alpha);
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
		if (Bridge.ReadNumber(TEXT("Number of Days in Year"), Jours) && Jours > 0.0)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] meteo : annee de %.0f jours"), Jours);
			if (Jours > 100.0)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[Worldseed] meteo : ANNEE DE %.0f JOURS -- a quarante-cinq ")
					TEXT("minutes reelles par journee, elle dure %.0f heures. La saison ")
					TEXT("ne changera pas en pratique. Assigner CAL_Worldseed."),
					Jours, Jours * 0.75);
			}
		}
		else
		{
			// ON LE DIT PLUTOT QUE DE SE TAIRE. Un silence ici se lirait comme
			// « le calendrier va bien », alors qu'il veut dire « je n'ai pas su
			// regarder » : la longueur de l'annee est CALCULEE dans l'asset
			// `UDS_Calendar` et non exposee sur l'acteur. Verifier a la main que
			// `CAL_Worldseed` y est assigne -- sans lui l'annee fait 365 jours,
			// soit deux cent soixante-quatorze heures reelles.
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] meteo : longueur de l'annee non lisible depuis ")
				TEXT("l'acteur (elle vit dans l'asset calendrier) -- verifier a la ")
				TEXT("main que CAL_Worldseed est assigne"));
		}

		if (Refusees.IsEmpty())
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] meteo : les 11 variables d'UDS/UDW acceptent l'ecriture")
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
