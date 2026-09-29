// Worldseed - les prereglages climatiques.
//
// L'ORIGINAL PYTHON A ETE SUPPRIME : ce fichier est la SEULE
// implementation. Il ne faut plus chercher de reference ailleurs, ni supposer
// qu'un autre fichier dit la meme chose autrement.
//
// Ce fichier Python a ete cale sur les 23 prereglages LIVRES par Ultra Dynamic
// Sky, lus dans l'editeur : ses formules sont des mesures, pas des suppositions.
// Toutes les constantes vivent dans la section "uds" de world_rules.json.

#pragma once

#include "CoreMinimal.h"

#include "Procedural/WorldseedClimatsReels.h"

class UWorldseedRules;

/** Index des saisons, dans l'ordre du Python. */
enum class EWorldseedSeason : uint8
{
	Winter = 0,
	Spring = 1,
	Summer = 2,
	Autumn = 3,
	Count = 4
};

/** Le climat lu en un point du monde. Cinq mesures suffisent au prereglage. */
struct WORLDSEED_API FWorldseedClimateSample
{
	/** Moyenne annuelle au sol, en degres Celsius. */
	float TempMeanC = 15.0f;

	/** Cumul annuel de precipitations, en millimetres. */
	float PrecipMm = 700.0f;

	/** Ecart entre le mois le plus froid et le plus chaud, en degres. */
	float SeasonalAmpC = 12.0f;

	/** 0 au bord de mer, 1 loin des cotes. Ouvre l'ecart jour/nuit. */
	float Continentality = 0.5f;

	/** Latitude signee, en degres. */
	float LatitudeDeg = 0.0f;

	/**
	 * Part de la pluie tombant au semestre CHAUD, dans [0..1].
	 *
	 * NEGATIVE = NON RENSEIGNEE, et la repartition retombe alors sur la
	 * latitude seule, comme avant. C'est ce qui rend l'ajout sur : un appelant
	 * qui n'a pas cette grandeur obtient exactement le comportement d'hier.
	 *
	 * POURQUOI LA TRANSMETTRE PLUTOT QUE LA REDERIVER. La chaine climatique la
	 * calcule deja par cellule -- `WorldseedClimate::SummerRainFraction` -- et
	 * `Build` la recalculait moins bien, d'apres la latitude seule. Mesure sur
	 * les vingt-trois releves reels : la repartition par latitude s'ecarte du
	 * reel de 25,7 mm par mois, par cette fraction de 21,8. Le plafond, avec la
	 * fraction EXACTE des releves, serait 15,0 -- ce qui reste est l'erreur de
	 * notre propre prediction, ramenee de 0,185 a 0,102 le meme jour.
	 */
	float SummerRainFrac = -1.0f;
};

/**
 * Les 21 cases d'un UDS_Climate_Preset.
 *
 * DEUX PRECISIONS QUE LA LECTURE DES PRESETS LIVRES A CORRIGEES, et qu'on
 * aurait ratees en raisonnant de tete :
 *   - la pluie est un cumul MENSUEL, pas saisonnier (Tropical_Rainforest porte
 *     145 a 184 mm) ;
 *   - la neige est un EQUIVALENT-EAU, pas une hauteur : un meme mois d'hiver
 *     peut porter 36 mm de pluie ET 45 mm de neige.
 */
struct WORLDSEED_API FWorldseedClimatePreset
{
	static constexpr int32 SeasonCount = 4;

	float HighTempC[SeasonCount] = { 10.0f, 18.0f, 26.0f, 18.0f };
	float LowTempC[SeasonCount] = { 2.0f, 8.0f, 16.0f, 8.0f };
	float CloudyPct[SeasonCount] = { 40.0f, 40.0f, 40.0f, 40.0f };
	float RainfallMm[SeasonCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float SnowfallMm[SeasonCount] = { 0.0f, 0.0f, 0.0f, 0.0f };

	/**
	 * A QUEL POINT LE SOL D'ICI PEUT DONNER DE LA POUSSIERE, de zero a un.
	 *
	 * C'ETAIT UN BOOLEEN, ET DEUX CHOSES CLOCHAIENT.
	 *
	 * LE FROID EN PRIVAIT : la regle exigeait `PrecipMm < 250 && TempMeanC > 5`,
	 * et ce `5` en dur excluait les deux plus grands deserts froids du monde. Le
	 * Gobi (-0,4 C de moyenne, 194 mm) et la Patagonie sont des sources de
	 * poussiere majeures -- les loess de Chine du Nord viennent du Gobi, pas du
	 * Sahara -- et le vent d'hiver y souffle plus fort qu'en ete. Ce qui souleve
	 * du sable est un sol NU ET SEC, pas un sol chaud.
	 *
	 * ET LE SEUIL FRANC FAISAIT UN MUR : a 250 mm en dur, une cellule a 249
	 * portait le voile entier et sa voisine a 251 rien du tout. Sur la maille de
	 * 15,6 m du monde, le joueur traversait ce mur en vingt metres -- douze
	 * secondes de marche -- et seul le fondu du ciel l'adoucissait.
	 *
	 * LA PART EST SURE PARCE QUE `Smoothstep` REND EXACTEMENT ZERO au-dela de
	 * `PoussiereNullePluieMm` : une foret tropicale a 2400 mm reste rigoureusement
	 * a zero, donc le defaut de l'aurore -- un voile faible et permanent PARTOUT,
	 * parce qu'un defaut valait 0,12 au lieu de 0 -- ne peut pas se reproduire.
	 */
	float DustPart = 0.0f;

	float Get(const float (&Values)[SeasonCount], EWorldseedSeason S) const
	{
		return Values[static_cast<int32>(S)];
	}
};

/** Section "uds" de world_rules.json, plus world.tropicDeg. */
struct WORLDSEED_API FWorldseedClimatePresetRules
{
	float DiurnalBaseC = 4.0f;
	float DiurnalContinentalC = 3.0f;
	float DiurnalAridC = 2.0f;
	float AridPrecipRefMm = 1000.0f;

	float CloudyFloorPct = 15.0f;
	float CloudySpanPct = 78.0f;
	float CloudyPrecipScaleMm = 60.0f;

	float SnowThresholdC = 2.0f;
	float SnowBandC = 4.0f;

	float ItczSummerFactor = 1.8f;
	float ItczWinterFactor = 0.2f;
	float MediterraneanWinterFactor = 1.5f;
	float MediterraneanSummerFactor = 0.5f;
	float MediterraneanLatMinDeg = 30.0f;
	float MediterraneanLatMaxDeg = 45.0f;

	/**
	 * LES DEUX BORNES DE L'ARIDITE QUI DONNE DE LA POUSSIERE, en mm de pluie
	 * par an : pleine au-dessous de la premiere, nulle au-dessus de la seconde.
	 *
	 * Elles remplacent `dustPrecipMaxMm`, un seuil franc a 250 -- la valeur de
	 * Koppen pour l'aridite, qui reste donc encadree par les deux.
	 */
	float PoussierePleinePluieMm = 120.0f;
	float PoussiereNullePluieMm = 350.0f;

	/**
	 * LE GEL, ET IL A FALLU LE REMETTRE -- MAIS PAS LE MEME.
	 *
	 * Retirer le `T > 5` en dur a bien rendu sa poussiere au Gobi, et il a
	 * aussi fait POUDROYER LA CALOTTE GLACIAIRE : releve du 29 septembre 2026,
	 * 99,9 % de voile et 56 tempetes par an a -18,8 C. Un desert polaire est
	 * couvert de GLACE, pas de sable.
	 *
	 * LE BON CRITERE N'EST DONC PAS LA CHALEUR MAIS LE GEL PERMANENT : sous une
	 * certaine moyenne annuelle, le sol reste en pergelisol et sous la neige
	 * toute l'annee, et il n'a rien a donner au vent. C'est la meme idee que le
	 * critere EF de Koppen, en continu plutot qu'en seuil.
	 *
	 * LES BORNES SE LISENT CONTRE LES SITES MESURES : calotte -10,4 et -18,8 C
	 * (doit rendre zero), toundra -13,9 (zero), desert froid BWk +13,0 (plein),
	 * et le Gobi reel -0,4 C de moyenne -- qui doit garder sa poussiere, puisque
	 * c'est LUI qui a motive le retrait du seuil d'origine.
	 */
	float PoussiereGelNulleC = -8.0f;
	float PoussiereGelPleineC = 0.0f;

	/**
	 * LE VOILE PERMANENT, sur l'echelle 0..10 d'UDS.
	 *
	 * Un desert n'est jamais parfaitement limpide : il y a presque toujours de
	 * la poussiere en suspension, et c'est la tempete qui est rare -- l'inverse
	 * de ce que le modele faisait, qui ne donnait RIEN les trois quarts du temps
	 * puis presque une tempete.
	 *
	 * DEUX VALEURS MESUREES L'ENCADRENT. Balayage du 29 septembre 2026, clarte
	 * du lointain au meme point : Dust 0 -> 124,4 ; Dust 2 -> 128,8 ; Dust 5 ->
	 * 136,3 ; Dust 10 -> 140,5, pour un bruit inter-lancement de 0,1. A 2 l'effet
	 * vaut donc QUARANTE-QUATRE FOIS le bruit tout en restant discret a l'oeil :
	 * c'est exactement ce qu'on demande a un voile de fond.
	 */
	float PoussiereVoile = 2.0f;

	/**
	 * LE SEUIL DE SALTATION ET SON EXPOSANT, sur l'echelle du vent.
	 *
	 * LE SABLE NE SE SOULEVE PAS PROGRESSIVEMENT : il faut depasser une vitesse
	 * de friction seuil, apres quoi le flux transporte croit comme le CUBE de
	 * cette vitesse (Bagnold, 1941). L'exposant n'est donc pas un curseur
	 * d'ambiance, il est source -- et c'est lui qui rend la tempete RARE sans
	 * qu'on ait a la rationner.
	 *
	 * LE SEUIL SE LIT CONTRE L'ECHELLE DU PACK, ou `Overcast` vaut 3 et ou seuls
	 * `Rain_Thunderstorm`, `Snow_Blizzard` et `Sand_Dust_Storm` atteignent 10.
	 */
	float PoussiereVentSeuil = 6.5f;
	float PoussiereVentMordant = 3.0f;

	/**
	 * COMBIEN DE FOIS LA POUSSIERE EST PLUS LENTE QUE LA PLUIE.
	 *
	 * UNE TEMPETE DE SABLE DURE DES HEURES, une averse passe. Et le fondu du
	 * pilote vaut douze secondes : a la periode nominale, l'octave la plus
	 * rapide du signal bat toutes les trente secondes, donc une excursion au
	 * centile 95 dure environ trois secondes et le fondu n'en restituerait qu'un
	 * cinquieme. Ralentir NE DEPLACE AUCUN QUANTILE -- la table d'`Uniformiser`
	 * reste valide -- et troque du NOMBRE d'episodes contre de la DUREE.
	 */
	float PoussierePeriodeFacteur = 3.0f;

	/**
	 * LE VENT, SUR L'ECHELLE 0..10 D'UDS, ET ELLE EST MESUREE.
	 *
	 * Relevee dans les treize prereglages livres par le pack le 29 septembre
	 * 2026 : 1 (Foggy, Snow_Light, Sand_Dust_Calm), 2 (Clear_Skies,
	 * Partly_Cloudy, Rain_Light), 2,5 (Cloudy), 3 (Overcast, Rain), 4 (Snow),
	 * 10 (Rain_Thunderstorm, Snow_Blizzard, Sand_Dust_Storm).
	 *
	 * NOTRE PLAFOND VALAIT 8, ET C'ETAIT UNE INVENTION : une tempete de sable de
	 * ce monde ne pouvait pas atteindre le vent que le pack reserve a ses trois
	 * etats violents. Pire, la formule seuillait le signal BRUT et faisait
	 * dependre le vent de la PLUIE -- donc dans un desert, ou `Occurrence` est
	 * nulle, il plafonnait a 1 + 3 x 0,9327 = 3,80, soit moins qu'un ciel gris.
	 *
	 * UDW AJOUTE SES PROPRES RAFALES par-dessus (`Current`/`Target Wind Gust
	 * Multiplier`) : notre valeur est la composante LENTE, et il ne faut pas
	 * modeliser la rafale deux fois.
	 *
	 * L'UNITE PHYSIQUE RESTE NON ETABLIE. Le releve binaire annoncait une
	 * variable `Knots at Wind Intensity 10` ; l'API ne la trouve pas. Ne pas
	 * citer de noeuds tant que ce n'est pas mesure.
	 */
	float VentCalme = 1.0f;
	float VentMordantAgitation = 9.0f;
	float VentPluie = 2.0f;
	float VentMaxUds = 10.0f;

	/**
	 * LA FORME DE LA DISTRIBUTION DU VENT, et sans elle il soufflait sans cesse.
	 *
	 * `Souffle` est UNIFORME par construction -- c'est tout l'objet
	 * d'`Uniformiser` -- donc une rampe lineaire rend une moyenne au MILIEU de
	 * la plage. Mesure du 29 septembre 2026 : 6,3 sur 10 en moyenne sur les 22
	 * sites, soit plus du double d'`Overcast` (3) EN PERMANENCE, quand le pack
	 * reserve 10 a ses trois etats violents et pose 2 pour un ciel clair.
	 *
	 * Le vent reel est tres dissymetrique -- beaucoup de vent faible, peu de
	 * vent fort, ce qu'une loi de Weibull decrit. L'exposant en est la forme la
	 * plus simple : a 3, la moyenne tombe a `VentCalme + Mordant/4`, soit 3,25
	 * -- l'echelle du pack entre `Overcast` et `Rain` -- et le maximum reste 10.
	 *
	 * IL S'AJOUTE AU CUBE DE LA SALTATION, et c'est voulu : le premier decrit la
	 * distribution du VENT, le second la physique du TRANSPORT. Ce sont deux
	 * faits distincts, pas un exposant qu'on empile pour rationner.
	 */
	float VentForme = 3.0f;

	/**
	 * LA VISIBILITE QUE MANGE UN BLIZZARD, sur l'echelle de brouillard d'UDS.
	 *
	 * SOURCE : `Snow_Blizzard` pose `Fog = 10`, releve dans les prereglages du
	 * pack. Notre modele rendait 0,4 -- son plancher -- sous une neige a dix,
	 * parce que son terme d'humidite fait BAISSER le brouillard quand il
	 * precipite. C'est juste pour une averse, qui lessive l'air ; c'est faux
	 * pour la neige, un blizzard se DEFINISSANT par sa visibilite reduite.
	 */
	float BlizzardVisibiliteMax = 10.0f;

	/**
	 * L'ECHELLE QUI TRANSFORME UN CUMUL MENSUEL EN FREQUENCE DE PLUIE, en mm.
	 *
	 * A ne pas confondre avec `CloudyPrecipScaleMm`, qui donne l'INTENSITE : la
	 * premiere dit COMBIEN DE TEMPS il pleut, la seconde A QUEL POINT. Les
	 * confondre revient a faire pleuvoir des qu'il y a des nuages -- c'etait le
	 * cas, et le bulletin du ciel a mesure 96 % du temps sous la pluie en foret
	 * tropicale humide, la ou on en compte vingt a trente.
	 */
	float PluieFrequenceEchelleMm = 625.0f;

	/**
	 * LE PLANCHER D'INTENSITE D'UNE AVERSE, dans [0, 1].
	 *
	 * Depuis que la FREQUENCE porte le cumul de pluie, l'intensite ne doit
	 * plus le porter aussi : sans plancher, un climat modere est penalise au
	 * CARRE et n'atteint jamais le seuil visible -- mesure au bulletin du ciel,
	 * taiga et toundra a zero pour cent de pluie pour quatre cents millimetres
	 * par an. Ce qui separe une averse tropicale d'une bruine oceanique est sa
	 * DUREE et sa FREQUENCE, pas son debit : deux a trois fois, pas dix.
	 */
	float PluieIntensitePlancher = 0.5f;

	/**
	 * LA FENETRE DE TEMPERATURE OU L'ORAGE DEVIENT POSSIBLE, en degres.
	 *
	 * L'eclair est CONVECTIF : il demande de l'air chaud qui monte, pas
	 * seulement de la pluie. Reference terrestre, en jours d'orage par an :
	 * bassin du Congo plus de deux cents, Floride une centaine, Paris une
	 * vingtaine, Islande deux, Antarctique aucun. Sous le minimum une averse
	 * ne tonne jamais ; au-dessus du maximum elle tonne autant que la pluie le
	 * permet.
	 */
	float OrageTempMinC = 8.0f;
	float OrageTempMaxC = 24.0f;

	/**
	 * L'OVALE AURORAL : sa latitude de plus forte occurrence et sa largeur.
	 *
	 * C'EST UN ANNEAU, PAS UNE CALOTTE, et c'est ce qui interdit une simple
	 * croissance vers le pole : l'ovale se centre sur le pole MAGNETIQUE vers
	 * 67 degres, si bien qu'on voit plus d'aurores a Tromso qu'au pole Nord
	 * lui-meme. Sous 45 degres elles demandent un orage magnetique majeur, ce
	 * que la largeur laisse arriver de temps a autre sans le garantir.
	 */
	float AuroreLatitudePicDeg = 67.0f;
	float AuroreLargeurDeg = 12.0f;

	/**
	 * L'intensite d'un beau rideau, sur l'echelle d'UDS.
	 *
	 * ⚠ LE DEFAUT DU PACK N'EST PAS ZERO mais 0,12 : sans pilotage, une aurore
	 * faible et permanente se pose PARTOUT, jusque sous les tropiques. La
	 * piloter corrige donc ce defaut autant qu'elle en ajoute une ou il faut.
	 */
	float AuroreIntensiteMax = 1.0f;

	/** world.tropicDeg : borne de la bande ou la ZCIT module les pluies. */
	float TropicLatDeg = 23.44f;

	/**
	 * Mordant du contraste saisonnier de la pluie.
	 *
	 * A un, les saisons suivent lineairement la part tombant au semestre chaud.
	 * Le reel est bien plus tranche : une savane a hiver sec recoit deux
	 * millimetres en hiver et cent cinquante-cinq en automne -- un rapport de
	 * CINQUANTE, quand une repartition lineaire plafonne vers dix.
	 */
	float SeasonContrastExponent = 1.7f;

	/**
	 * LA COUVERTURE NUAGEUSE VIENT-ELLE D'UNE STATION REELLE ?
	 *
	 * A vrai, la cellule est classee en climat de Koppen et recoit la
	 * couverture du releve correspondant, saison par saison. A faux, elle est
	 * calculee depuis la pluie par la courbe exponentielle.
	 *
	 * POURQUOI CE CHOIX EXISTE, et pourquoi il penche du cote des releves.
	 * Mesure sur les vingt-trois climats reels : la courbe s'ecarte de 15,3
	 * points, la meilleure courbe possible de 12,7, et aucune forme ne passe
	 * sous 6,74 -- la dispersion entre les quatre saisons d'un meme climat, que
	 * nulle formule ne capture. Classer puis COPIER descend a 4,3 points
	 * attendus, parce que la copie prend cette dispersion avec le reste.
	 *
	 * CE QU'IL FAUT SAVOIR AVANT DE S'EN REJOUIR : ce chiffre suppose que notre
	 * monde produise des climats que la Terre connait. Une cellule bien classee
	 * recoit la meteo d'une vraie station ; une cellule mal classee celle d'un
	 * AUTRE climat, ce qui coute 16,5 points en moyenne et jusqu'a 66 entre une
	 * foret tropicale et un desert. La qualite ne tient donc plus a un calage
	 * mais a un TAUX -- 17 sur 23 en classe exacte, 20 sur 23 sur le groupe.
	 */
	bool bCouvertureDepuisReleves = true;

	/**
	 * Ecart entre un extreme MENSUEL et l'extreme SAISONNIER, en degres.
	 *
	 * Koppen se definit sur les mois, nous n'avons que des saisons, et la
	 * moyenne de trois mois adoucit les pointes. Sans cette correction, des
	 * continentaux franchissent la frontiere du groupe tempere. Balayee sur les
	 * releves plutot que devinee.
	 */
	float KoppenPointeMensuelleC = 0.5f;

	/**
	 * Les vingt-trois releves, charges UNE FOIS avec les regles.
	 *
	 * Ils vivent ici plutot que dans une statique parce que `Build` tourne deux
	 * fois par seconde et ne doit pas relire un fichier ; et parce qu'une sonde
	 * qui veut essayer d'autres reglages doit pouvoir copier la structure sans
	 * emporter un etat global avec elle.
	 */
	TArray<FWorldseedReleveReel> Releves;

	static FWorldseedClimatePresetRules FromRules(const UWorldseedRules& Rules);
};

namespace WorldseedClimatePreset
{
	/**
	 * Les 21 cases, depuis cinq mesures du monde.
	 *
	 * DEUX HEMISPHERES, UNE SEULE SAISON. UDS n'a qu'une saison globale, or le
	 * monde va d'un pole a l'autre : quand c'est l'ete au nord, c'est l'hiver au
	 * sud. Un echantillon austral rend donc son prereglage avec hiver/ete et
	 * printemps/automne echanges, pour tomber juste dans les saisons d'UDS.
	 */
	WORLDSEED_API FWorldseedClimatePreset Build(const FWorldseedClimateSample& Sample,
		const FWorldseedClimatePresetRules& Rules);

	/**
	 * Valeur continue au sein de l'annee.
	 *
	 * Les saisons sont echantillonnees au quart de tour ; Phase va de 0 (coeur
	 * de l'hiver) a 1.
	 */
	WORLDSEED_API float SeasonLerp(const float Values[FWorldseedClimatePreset::SeasonCount],
		float Phase);
}
