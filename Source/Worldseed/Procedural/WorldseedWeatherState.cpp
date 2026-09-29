// Worldseed - etat meteo instantane, depuis le climat et le temps qui passe.

#include "Procedural/WorldseedWeatherState.h"
#include "Procedural/WorldseedPerlin.h"
#include "Procedural/WorldseedWeatherSignal.h"
#include "Procedural/WorldseedWind.h"

namespace
{
	/**
	 * Bornes de l'echelle de nebulosite d'UDS.
	 *
	 * Elles decrivent L'ECHELLE D'UDS, pas le climat : leur place n'est donc pas
	 * dans world_rules.json, qui ne parle que du monde.
	 */
	constexpr float UdsClearCoverage = 0.8f;
	constexpr float UdsOvercastCoverage = 8.5f;

	/**
	 * L'echelle des curseurs de meteo d'UDS, relevee dans ses prereglages.
	 *
	 * Ce n'est pas une convention choisie mais une MESURE : les treize types
	 * livres par le pack portent Rain_Light 3, Rain 7, Rain_Thunderstorm 10,
	 * Snow_Light 3, Snow 6, Snow_Blizzard 10, Sand_Dust_Storm 10. Nos
	 * occurrences et nos intensites sont des FRACTIONS ; c'est ici qu'elles
	 * deviennent des unites du pack, et nulle part ailleurs.
	 */
	constexpr float UdsEchelle = 10.0f;


	// SEUILS D'AFFICHAGE DU REGIME DOMINANT, SUR L'ECHELLE D'UDS.
	//
	// Ils valaient 0,05 quand `Rain` et `Dust` etaient des fractions : sur une
	// echelle qui va a dix, ils auraient annonce « PLUIE » pour un demi-pour-
	// cent d'averse, c'est-a-dire rien. Un seuil oublie lors d'un changement
	// d'unite ne casse rien et ment a chaque ligne.
	constexpr float VisibleFall = 0.5f;
	constexpr float ThickFog = 1.6f;
	constexpr float OvercastSky = 5.0f;

	// LA POUSSIERE A DEUX SEUILS, PARCE QU'ELLE A DEUX REGIMES.
	//
	// Elle valait 0,5, ce qui convenait quand elle etait nulle les trois quarts
	// du temps. Depuis qu'un voile PERMANENT de 2 couvre les climats arides, ce
	// seuil ferait afficher « POUSSIERE » cent pour cent du temps dans un desert
	// et rendrait « degage » inatteignable -- un seuil oublie lors d'un
	// changement d'unite ne casse rien et ment a chaque ligne, et c'est
	// exactement ce que le commentaire du dessus reproche a la version d'avant.
	//
	// LES DEUX VALEURS SONT CALEES SUR LA MESURE du 29 septembre 2026, clarte du
	// lointain : Dust 2 -> +4,4 sur 124 (discret, c'est le voile), Dust 5 ->
	// +11,9 (le lointain s'estompe), Dust 10 -> +16,1 (l'horizon disparait).
	constexpr float VisibleVoile = 1.0f;
	constexpr float VisibleTempete = 5.0f;
}

FString FWorldseedWeather::DescribeRegime() const
{
	// L'ordre suit ce qui domine le regard : une averse se voit plus qu'un ciel
	// couvert, et la neige plus qu'une pluie.
	if (Dust > VisibleTempete) { return TEXT("TEMPETE DE SABLE"); }
	if (Snow > VisibleFall) { return TEXT("NEIGE"); }
	if (Rain > VisibleFall) { return TEXT("PLUIE"); }
	if (Fog > ThickFog) { return TEXT("BROUILLARD"); }
	if (CloudCoverage > OvercastSky) { return TEXT("couvert"); }
	// LE VOILE PASSE APRES LE CIEL COUVERT, a dessein : il est permanent dans un
	// climat aride, donc il ne doit masquer aucun evenement.
	if (Dust > VisibleVoile) { return TEXT("voile de sable"); }
	return TEXT("degage");
}

namespace WorldseedWeatherState
{
	float Saltation(float WindIntensity,
		const FWorldseedClimatePresetRules& PresetRules)
	{
		return FMath::Clamp(
			(WindIntensity - PresetRules.PoussiereVentSeuil)
				/ FMath::Max(PresetRules.VentMaxUds - PresetRules.PoussiereVentSeuil, 1e-3f),
			0.0f, 1.0f);
	}

	float PoussiereDepuisVent(float WindIntensity, float DustPart,
		float Occurrence, const FWorldseedClimatePresetRules& PresetRules)
	{
		// LA PLUIE RABAT LA POUSSIERE -- meme fait physique que le brouillard
		// qui se leve quand il ne pleut pas.
		const float Arrachement = Saltation(WindIntensity, PresetRules);
		return DustPart * (1.0f - Occurrence) * FMath::Max(
			PresetRules.PoussiereVoile,
			PresetRules.VentMaxUds
				* FMath::Pow(Arrachement, PresetRules.PoussiereVentMordant));
	}

	FWorldseedWeather Evaluate(const FWorldseedClimateSample& Sample,
		const FWorldseedClimatePresetRules& PresetRules,
		const FWorldseedWeatherParams& Params)
	{
		const FWorldseedClimatePreset Preset =
			WorldseedClimatePreset::Build(Sample, PresetRules);

		FWorldseedWeather Out;

		for (int32 S = 0; S < FWorldseedClimatePreset::SeasonCount; ++S)
		{
			Out.SeasonMinMaxC[S] = FVector2D(Preset.LowTempC[S], Preset.HighTempC[S]);
		}

		// --- ce que le climat prevoit ici, a cette saison -----------------------
		const float CloudyPct = WorldseedClimatePreset::SeasonLerp(
			Preset.CloudyPct, Params.SeasonPhase);
		const float RainMm = WorldseedClimatePreset::SeasonLerp(
			Preset.RainfallMm, Params.SeasonPhase);
		const float SnowMm = WorldseedClimatePreset::SeasonLerp(
			Preset.SnowfallMm, Params.SeasonPhase);
		const float TotalMm = RainMm + SnowMm;

		const float Storm = WorldseedWeatherSignal::Storminess(
			Params.TimeSeconds, Params.VariationPeriodS, Params.Seed);

		// --- occurrence ---------------------------------------------------------
		// LA FREQUENCE DE LA PLUIE VIENT DE SA QUANTITE, PAS DE LA NEBULOSITE.
		//
		// Ce seuil etait pose sur le pourcentage de ciel couvert, ce qui revient
		// a dire qu'il pleut des qu'il y a des nuages. Mesure au bulletin du
		// ciel, foret tropicale humide : il pleuvait **96 % du temps**. Une
		// foret tropicale est bien couverte quatre-vingts pour cent de l'annee,
		// mais l'averse convective y est BREVE -- on compte vingt a trente pour
		// cent du temps, pas quatre-vingt-seize.
		//
		// La quantite, elle, sait le dire : on sature le cumul mensuel sur une
		// echelle propre, calee pour qu'un mois tropical a 180 mm donne environ
		// un quart du temps. Et la nebulosite reste un PLAFOND, parce qu'il ne
		// peut pas pleuvoir plus souvent que le ciel n'est charge.
		const float CloudyFraction = FMath::Clamp(CloudyPct * 0.01f, 0.0f, 1.0f);
		const float FractionPluvieuse = FMath::Min(
			1.0f - FMath::Exp(-TotalMm / FMath::Max(PresetRules.PluieFrequenceEchelleMm, 1e-6f)),
			CloudyFraction);
		// ⚠ UN SEUIL N'EST PAS UNE PART, et ce depot l'a paye quatre fois --
		// diaclaseZoneSeuil, le littoral, la pente, et ici.
		//
		// `Storminess` somme TROIS octaves de bruit : sa loi est une cloche
		// autour d'un demi, pas une loi uniforme. Seuiller directement dessus ne
		// rend donc PAS la fraction demandee. Mesure au bulletin du ciel, foret
		// tropicale humide : un seuil pose a 0,15 donnait 96 % du temps sous la
		// pluie, le meme pose a 0,75 en donnait 0,4 -- deux fois faux, et dans
		// les deux sens. On rend donc le signal UNIFORME avant de le comparer,
		// apres quoi une part demandee est la part obtenue.
		//
		// ET LA TRANSFORMEE EST UNE MESURE, PAS UN CALCUL. Une premiere version
		// posait ici une cloche d'ecart-type 0,186, obtenu par l'algebre d'une
		// somme de trois lois uniformes -- sans jamais relever la loi reelle.
		// `ValueNoise` n'est pas uniforme, et la somme a un support borne : la
		// taiga voyait alors 0,5 % de precipitation pour les 6 % visees.
		const float StormUniforme = WorldseedWeatherSignal::Uniformiser(Storm);

		const float Threshold = 1.0f - FractionPluvieuse;
		const float Occurrence = FMath::Clamp(
			(StormUniforme - Threshold) / FMath::Max(1.0f - Threshold, 1e-3f), 0.0f, 1.0f);

		// --- intensite ----------------------------------------------------------
		// QUAND IL PLEUT, IL PLEUT -- et la quantite ne doit pas compter DEUX
		// FOIS. Depuis que la FREQUENCE porte le cumul, laisser l'intensite le
		// porter aussi penalise les climats moderes au CARRE : mesure au
		// bulletin du ciel, taiga et toundra rendaient zero pour cent de pluie
		// visible alors qu'elles recoivent quatre cents millimetres par an.
		//
		// Ce qui separe une averse tropicale d'une bruine oceanique est surtout
		// sa DUREE et sa FREQUENCE, pas son debit instantane : il tombe bien
		// plus fort sous l'equateur, mais deux a trois fois, pas dix. D'ou un
		// plancher, au-dessus duquel le cumul module encore.
		const float Plancher = FMath::Clamp(PresetRules.PluieIntensitePlancher, 0.0f, 1.0f);
		const float Intensity = Plancher + (1.0f - Plancher) * (1.0f - FMath::Exp(
			-TotalMm / FMath::Max(PresetRules.CloudyPrecipScaleMm, 1e-6f)));

		// Le partage pluie/neige est deja fait par le prereglage : on le
		// respecte plutot que de le recalculer avec d'autres seuils.
		const float SnowShare = (TotalMm > 1e-6f) ? (SnowMm / TotalMm) : 0.0f;
		const float Fall = Occurrence * Intensity;

		// L'ECHELLE D'UDS VA DE ZERO A DIX, et ces deux lignes l'ignoraient.
		// Le bareme est celui des prereglages LIVRES par le pack, releve dans
		// les assets : Rain_Light 3, Rain 7, Rain_Thunderstorm 10 ; Snow_Light
		// 3, Snow 6, Snow_Blizzard 10. En rendant `Fall` tel quel -- une
		// fraction -- l'averse la plus violente de ce monde valait 1,0, soit le
		// TIERS de la plus legere bruine que le pack sache dessiner.
		Out.Rain = Fall * (1.0f - SnowShare) * UdsEchelle;
		Out.Snow = Fall * SnowShare * UdsEchelle;

		// --- LE VENT D'ABORD, LA POUSSIERE ENSUITE ------------------------------
		//
		// L'ORDRE EST INVERSE PAR RAPPORT A CE QUI EXISTAIT, et c'est le pack
		// qui l'impose. Releve du 29 septembre 2026 sur ses treize prereglages :
		// `Sand_Dust_Calm` et `Sand_Dust_Storm` posent TOUS DEUX `Dust = 10` et
		// ne different QUE par `Wind Intensity` -- 1 contre 10. La tempete de
		// sable est donc un etat de VENT, la poussiere en etant l'effet, ce qui
		// est aussi la physique : le sable ne se souleve qu'au-dela d'une
		// vitesse de friction seuil.
		//
		// UN SIGNAL SEPARE ET PLUS LENT, pour deux raisons mesurees.
		//
		// 1. LE FONDU DU PILOTE VAUT DOUZE SECONDES. A la periode nominale,
		//    l'octave la plus rapide de `Storminess` bat toutes les trente
		//    secondes : une excursion au centile 95 dure environ trois secondes
		//    et le fondu n'en restituerait qu'un cinquieme. Ralentir NE DEPLACE
		//    AUCUN QUANTILE -- la table d'`Uniformiser` reste valide -- et troque
		//    du NOMBRE d'episodes contre de la DUREE, ce qui est le but.
		// 2. IL NE DOIT PAS SUIVRE LES AVERSES. La poussiere se leve quand il ne
		//    pleut PAS ; un signal commun les ferait coincider. Meme procede que
		//    l'aurore, qui decale deja sa graine.
		//
		// ⚠ ET IL PASSE PAR `Uniformiser`, CE QUE L'ANCIENNE FORME NE FAISAIT
		// PAS. Elle seuillait `Storm` BRUT a 0,62 -- le defaut « un seuil n'est
		// pas une part » que ce depot a paye quatre fois, et que la pluie evite
		// cinquante lignes plus haut dans ce meme fichier. Deux consequences :
		// la part obtenue n'etait pas celle qu'on croyait, et surtout le support
		// du signal etant BORNE a 0,9327, `Dust = 10` etait INATTEIGNABLE --
		// maximum reel 8,13. `Uniformiser` rend exactement 1,0 au sommet du
		// support, donc le haut de l'echelle redevient atteignable PAR
		// CONSTRUCTION, et non par un reglage qu'il faudrait pousser.
		const float Souffle = WorldseedWeatherSignal::Uniformiser(
			WorldseedWeatherSignal::Storminess(
				Params.TimeSeconds,
				Params.VariationPeriodS * FMath::Max(PresetRules.PoussierePeriodeFacteur, 0.1f),
				Params.Seed ^ 0x53414E44u));            // « SAND »

		// --- brouillard ---------------------------------------------------------
		// LE PYTHON N'EN MODELISE PAS : les prereglages d'UDS n'ont pas de case
		// pour lui. On le deduit donc du reste sans pretendre le mesurer — il
		// demande de l'humidite et de la fraicheur, et il se leve quand il ne
		// pleut PAS, une averse lessivant l'air.
		const float Coolness = 1.0f - WorldseedPerlin::Smoothstep(5.0f, 25.0f, Sample.TempMeanC);
		const float HazeNoise = WorldseedWeatherSignal::Haze(
			Params.TimeSeconds, Params.VariationPeriodS, Params.Seed);

		Out.Fog = FMath::Clamp(
			0.4f + 2.0f * CloudyFraction * Coolness * (1.0f - Occurrence) * HazeNoise,
			0.4f, 2.5f);

		// --- nuages -------------------------------------------------------------
		Out.CloudCoverage = FMath::Lerp(UdsClearCoverage, UdsOvercastCoverage,
			FMath::Clamp(CloudyFraction * 0.5f + Occurrence * 0.8f, 0.0f, 1.0f));

		// --- orage ---------------------------------------------------------------
		// L'ECLAIR EST CONVECTIF, DONC IL SUIT LA CHALEUR, PAS LA PLUIE SEULE.
		// Sur Terre la densite de coups au sol s'effondre avec la latitude : le
		// bassin du Congo compte plus de deux cents jours d'orage par an, la
		// Floride une centaine, Paris une vingtaine, l'Islande deux, et
		// l'Antarctique aucun. Une averse froide ne tonne pas -- il faut de
		// l'air chaud qui monte. On multiplie donc la pluie EN COURS par une
		// fenetre de temperature, et rien d'autre : le reste est deja porte par
		// la pluie, qui sait ou et quand elle tombe.
		//
		// LA NEIGE EST EXCLUE A DESSEIN. Le « thundersnow » existe mais il est
		// assez rare pour qu'un joueur qui le verrait souvent trouve le monde
		// faux -- et la part neige est deja retiree par `Out.Rain`.
		const float Convection = WorldseedPerlin::Smoothstep(
			PresetRules.OrageTempMinC, PresetRules.OrageTempMaxC, Sample.TempMeanC);
		Out.Thunder = Out.Rain * Convection;

		// --- aurore ---------------------------------------------------------------
		// ELLE SE PLACE SUR UN OVALE, PAS SUR UNE CALOTTE. L'ovale auroral est
		// un ANNEAU centre sur le pole magnetique, vers 67 degres : on en voit
		// plus a Tromso qu'au pole Nord meme, et presque jamais sous 45 degres
		// hors orage magnetique majeur. Une simple croissance vers le pole
		// serait donc fausse au pole meme -- d'ou une cloche, et non une rampe.
		//
		// ET ELLE NE SE VOIT PAS A TRAVERS LES NUAGES. C'est une emission de
		// haute atmosphere, a une centaine de kilometres : le moindre stratus
		// la cache entierement. On la module donc par la clarte du ciel.
		//
		// LE JOUR EST L'AFFAIRE D'UDS, PAS LA NOTRE : le pack porte une
		// `Daytime Aurora Intensity` distincte, a zero par defaut, et fait le
		// fondu lui-meme. Lui imposer notre propre facteur de nuit le ferait
		// deux fois.
		const float EcartAuPic = (FMath::Abs(Sample.LatitudeDeg)
			- PresetRules.AuroreLatitudePicDeg) / FMath::Max(PresetRules.AuroreLargeurDeg, 1e-3f);
		const float Ovale = FMath::Exp(-EcartAuPic * EcartAuPic);

		// L'ACTIVITE GEOMAGNETIQUE VARIE, et une aurore identique toutes les
		// nuits se lirait comme un decor. On reprend le signal lent du temps
		// qu'il fait, decale par la graine pour qu'il ne suive pas les averses.
		const float Activite = WorldseedWeatherSignal::Storminess(
			Params.TimeSeconds, Params.VariationPeriodS * 4.0f, Params.Seed ^ 0x4155524Fu);

		const float Clarte = 1.0f - FMath::Clamp(
			Out.CloudCoverage / FMath::Max(UdsOvercastCoverage, 1e-3f), 0.0f, 1.0f);

		Out.Aurora = PresetRules.AuroreIntensiteMax * Ovale * Clarte
			* FMath::Clamp(Activite * 1.4f, 0.0f, 1.0f);

		// --- vent ---------------------------------------------------------------
		//
		// La DIRECTION rejoue la circulation generale, donc les memes vents qui
		// ont transporte l'humidite pendant la generation du monde.
		Out.WindDirectionDeg = WorldseedWind::PrevailingYawDeg(
			Sample.LatitudeDeg, Params.LatSpanDeg);

		// L'EXPOSANT N'EST PAS DECORATIF : `Souffle` est UNIFORME, donc une rampe
		// lineaire rendrait une moyenne au MILIEU de la plage -- mesure, 6,3 sur
		// 10 en permanence, plus du double d'`Overcast`. Le vent reel est tres
		// dissymetrique, et l'exposant en est la forme la plus simple.
		Out.WindIntensity = FMath::Clamp(
			PresetRules.VentCalme
			+ PresetRules.VentMordantAgitation
				* FMath::Pow(Souffle, FMath::Max(PresetRules.VentForme, 0.1f))
			+ PresetRules.VentPluie * Occurrence,
			0.5f, PresetRules.VentMaxUds);

		// --- LA POUSSIERE SORT DU VENT ------------------------------------------
		//
		// LES DEUX MOITIES SONT DES FONCTIONS PUBLIQUES, et c'est la regle du
		// depot contre les formules recopiees : `Saltation` est partagee avec la
		// REPTATION au ras du sol -- qui decide si le sable ou la neige se met a
		// courir -- et `PoussiereDepuisVent` avec le temoin de VENT.
		//
		// ET ELLE SERT UNE TROISIEME FOIS, juste en dessous, pour le BLIZZARD :
		// la poudrerie qui mange la visibilite est le meme arrachement, applique
		// a de la neige au lieu du sable.
		const float Arrachement = Saltation(Out.WindIntensity, PresetRules);
		Out.Dust = PoussiereDepuisVent(Out.WindIntensity, Preset.DustPart,
			Occurrence, PresetRules);

		// --- LE BLIZZARD : LA NEIGE SOUFFLEE MANGE LA VISIBILITE ---------------
		//
		// ⚠ LE BROUILLARD FAISAIT L'INVERSE, et c'etait le defaut central de la
		// tempete de neige. Son terme `(1 - Occurrence)` le fait BAISSER quand
		// il precipite -- ce qui est juste pour une averse, qui lessive l'air, et
		// FAUX pour la neige : un blizzard se DEFINIT par sa visibilite reduite,
		// sous 400 m par convention meteorologique. Mesure du desaccord : le
		// prereglage `Snow_Blizzard` du pack pose `Fog = 10` quand notre modele
		// rendait 0,4 -- le plancher -- sous une neige a dix.
		//
		// CE N'EST PAS DE L'EAU EN SUSPENSION, C'EST DE LA NEIGE. Le blizzard ne
		// masque pas parce que l'air est sature, mais parce que le vent remplit
		// l'air de flocons deja tombes : c'est la POUDRERIE, exactement le meme
		// fait physique que la saltation du sable. D'ou la meme fonction
		// d'arrachement, et une forme symetrique de celle de la poussiere.
		//
		// LE `max` ET NON UNE SOMME : par temps calme le brouillard garde sa
		// valeur d'humidite, et c'est le blizzard qui prend le dessus quand il
		// se leve -- pas les deux qui s'additionnent en un voile impossible.
		const float NeigeSoufflee =
			FMath::Clamp(Out.Snow / UdsEchelle, 0.0f, 1.0f) * Arrachement;

		Out.Fog = FMath::Max(Out.Fog,
			PresetRules.BlizzardVisibiliteMax * NeigeSoufflee);

		return Out;
	}

	float AlphaDeFondu(float DeltaSeconds, float ConstanteS)
	{
		return 1.0f - FMath::Exp(
			-FMath::Max(DeltaSeconds, 0.0f) / FMath::Max(ConstanteS, 0.1f));
	}

	void BlendTowards(FWorldseedWeather& Current, const FWorldseedWeather& Target,
		float Alpha)
	{
		const float A = FMath::Clamp(Alpha, 0.0f, 1.0f);

		Current.Rain = FMath::Lerp(Current.Rain, Target.Rain, A);
		Current.Snow = FMath::Lerp(Current.Snow, Target.Snow, A);
		Current.Fog = FMath::Lerp(Current.Fog, Target.Fog, A);
		Current.Dust = FMath::Lerp(Current.Dust, Target.Dust, A);
		Current.CloudCoverage = FMath::Lerp(Current.CloudCoverage, Target.CloudCoverage, A);
		Current.WindIntensity = FMath::Lerp(Current.WindIntensity, Target.WindIntensity, A);

		// L'ORAGE ET L'AURORE SE FONDENT COMME LE RESTE. Les oublier ici les
		// laisserait a leur valeur initiale pour toute la partie : le fondu est
		// le SEUL chemin par lequel l'etat courant bouge, `Evaluate` ne faisant
		// que designer une cible.
		Current.Thunder = FMath::Lerp(Current.Thunder, Target.Thunder, A);
		Current.Aurora = FMath::Lerp(Current.Aurora, Target.Aurora, A);

		// LE VENT EST UN ANGLE : interpoler 350 vers 10 en ligne droite ferait
		// faire un tour complet a la girouette. On passe par l'ecart signe le
		// plus court.
		const float Delta = FMath::UnwindDegrees(
			Target.WindDirectionDeg - Current.WindDirectionDeg);
		Current.WindDirectionDeg = FMath::UnwindDegrees(
			Current.WindDirectionDeg + Delta * A);

		for (int32 S = 0; S < FWorldseedClimatePreset::SeasonCount; ++S)
		{
			Current.SeasonMinMaxC[S] = FMath::Lerp(
				Current.SeasonMinMaxC[S], Target.SeasonMinMaxC[S], A);
		}
	}
}
