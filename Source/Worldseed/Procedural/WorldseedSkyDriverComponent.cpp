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

