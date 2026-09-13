"""Etapes 2 et 3 - temperature, circulation atmospherique, precipitations.

Temperature : loi en puissance de la latitude, corrigee du gradient adiabatique.
Un sommet equatorial a 5000 m tombe sous zero : neiges eternelles sur l'equateur,
comme le Kilimandjaro.

Precipitations : trois cellules de circulation par hemisphere, comme sur Terre
(alizes, westerlies, est polaires), puis advection semi-lagrangienne de
l'humidite. Rien n'est peint. Le desert apparait vers 30 deg parce que l'air y
arrive deja essore, l'ombre pluviometrique parce qu'une chaine coupe la route de
l'ocean, la foret equatoriale parce que la circulation y converge.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy import ndimage

from . import noise
from .config import Geometry, Rules


@dataclass
class ClimateResult:
    temp_mean_c: np.ndarray       # float32, moyenne annuelle au sol
    temp_min_c: np.ndarray        # float32, moyenne du mois le plus froid
    temp_max_c: np.ndarray        # float32, moyenne du mois le plus chaud
    seasonal_amp_c: np.ndarray    # float32, amplitude saisonniere
    precip_mm: np.ndarray         # float32, cumul annuel
    wind_u: np.ndarray            # float32, composante est (+ = vers l'est)
    wind_v: np.ndarray            # float32, composante nord
    continentality: np.ndarray    # float32, 0 au bord de mer -> 1 loin des cotes
    orographic_uplift: np.ndarray # float32, soulevement force par le relief
    convergence: np.ndarray       # float32, convergence du vent (ZCIT)


# --------------------------------------------------------------------- vents


def wind_field(geo: Geometry, rules: Rules) -> tuple[np.ndarray, np.ndarray]:
    """Champ de vent de surface : trois cellules par hemisphere.

    | bande      | vent de surface   | consequence                        |
    |------------|-------------------|------------------------------------|
    | 0 - 30 deg | alizes (E -> O)   | convergence equatoriale, ZCIT      |
    | 30 - 60    | westerlies (O->E) | facades ouest arrosees             |
    | 60 - 90    | est polaires      | air froid et sec, desert polaire   |
    """
    n = geo.n
    lat = geo.latitude_grid().astype(np.float32)
    abs_lat = np.abs(lat)
    half = np.float32(geo.lat_span_deg * 0.5)

    # Limites des cellules, mises a l'echelle si la plage de latitude n'est pas 180.
    c1 = half * np.float32(30.0 / 90.0)
    c2 = half * np.float32(60.0 / 90.0)
    tw = half * np.float32(7.5 / 90.0)          # largeur de transition

    a = noise.smoothstep(float(c1 - tw), float(c1 + tw), abs_lat)
    b = noise.smoothstep(float(c2 - tw), float(c2 + tw), abs_lat)
    w_trades = np.float32(1.0) - a
    w_west = a - b
    w_polar = b

    # Zonal : alizes vers l'ouest, westerlies vers l'est, est polaires vers l'ouest.
    u = -w_trades + w_west - w_polar
    # Meridien : Hadley et cellule polaire ramenent vers l'equateur, Ferrel vers le pole.
    meridional = np.float32(0.35) * (-w_trades + w_west - w_polar)
    v = np.sign(lat).astype(np.float32) * meridional

    # Un peu d'irregularite : sans cela les bandes sont des rubans parfaits.
    jitter = float(rules.get("precipitation.windJitter", 0.0))
    if jitter > 0.0:
        ju = noise.fbm(n, 3.0, 4, rules.seed + 31337)
        jv = noise.fbm(n, 3.0, 4, rules.seed + 31357)
        u = u + np.float32(jitter) * ju
        v = v + np.float32(jitter) * jv

    mag = np.maximum(np.sqrt(u * u + v * v), np.float32(1e-6))
    return (u / mag).astype(np.float32), (v / mag).astype(np.float32)


# --------------------------------------------------------------- temperature


def _sea_level_temperature(geo: Geometry, temp_rules: dict) -> np.ndarray:
    """Profil zonal de temperature au niveau de la mer.

    DEUX FORMES, reglees par `temperature.profile` :

      "cosine"  T(phi) = T_pole + (T_eq - T_pole) * cos(phi) ^ p
      "power"   T(phi) = T_eq - (T_eq - T_pole) * (|phi| / phi_max) ^ k

    LA FORME EN COSINUS EST LA BONNE, et pour deux raisons qui se rejoignent.
    Physiquement d'abord : l'energie solaire recue au sommet de l'atmosphere
    varie, en moyenne annuelle, a peu pres comme le cosinus de la latitude. Un
    profil lineaire en cos(phi) est donc la forme de premier ordre, pas une
    approximation choisie au hasard. Par la mesure ensuite, contre le profil
    zonal reel (0 deg 26 C, 30 deg 20, 45 deg 12, 60 deg 0, 75 deg -12) :

        forme                ecart moyen    ecart hors pole
        sin^2(phi)              7,02 C          7,19 C
        (phi/90)^2              1,95 C          1,69 C
        (phi/90)^1,8            1,11 C          0,79 C
        cos(phi)                0,84 C          0,49 C

    La loi en sinus carre, qui est le reflexe habituel, se trompe de 12 degres a
    60 deg -- assez pour couvrir de calotte glaciaire tout un hemisphere. Le seul
    ecart notable du cosinus est AU POLE (-25 contre -20 sur Terre), et il vient
    de `poleC`, pas de la forme : c'est un choix, l'Antarctique reel etant bien
    plus froid que -20.
    """
    lat = np.abs(geo.latitude_grid()).astype(np.float32)
    half = np.float32(max(geo.lat_span_deg * 0.5, 1e-6))
    t_eq = np.float32(temp_rules["equatorC"])
    t_pole = np.float32(temp_rules["poleC"])
    if str(temp_rules.get("profile", "power")) == "cosine":
        p = np.float32(temp_rules.get("cosineExponent", 1.0))
        # lat / half ramene la latitude sur [0, 1], donc l'angle sur [0, 90 deg]
        angle = (lat / half) * np.float32(np.pi * 0.5)
        c = np.clip(np.cos(angle), 0.0, 1.0).astype(np.float32)
        return (t_pole + (t_eq - t_pole) * np.power(c, p)).astype(np.float32)
    k = np.float32(temp_rules.get("latitudeExponent", 2.0))
    return (t_eq - (t_eq - t_pole) * np.power(lat / half, k)).astype(np.float32)


def seasonal_amplitude(lat_norm: np.ndarray, cont: np.ndarray, temp_rules: dict) -> np.ndarray:
    """Amplitude saisonniere : mois le plus chaud moins mois le plus froid.

    `lat_norm` va de 0 a l'equateur a 1 au pole ; `cont` est la continentalite.

    PUBLIQUE A DESSEIN : `export_uds_climate.py` a besoin exactement de cette
    valeur pour remplir les temperatures d'hiver et d'ete des presets Ultra
    Dynamic Sky. La formule ne doit exister qu'a UN endroit -- la dupliquer est
    precisement ce qui fabrique des divergences invisibles.

    DEUX FORMES, reglees par `temperature.seasonalAmplitudeShape`. La forme
    "sine" est la bonne, et l'ancienne "linear" se trompait DEUX FOIS :

      1. l'amplitude croissait LINEAIREMENT avec la latitude, alors que le
         contraste saisonnier suit le SINUS de la latitude -- c'est la
         projection de la declinaison solaire. En lineaire, corriger les poles
         casse les latitudes moyennes et inversement : aucun chiffre ne
         satisfait les deux ;
      2. l'ocean amortissait par SOUSTRACTION (amp - 6), alors qu'il amortit
         PROPORTIONNELLEMENT : il divise le contraste, il n'en retranche pas un
         nombre fixe de degres. La soustraction donnait des amplitudes negatives
         aux basses latitudes maritimes, qu'il fallait borner a 1 -- un
         rustinage qui signalait la mauvaise forme.

    Ecart absolu moyen mesure sur huit stations reelles (Singapour, Lisbonne,
    Teheran, Chicago, Bergen, Winnipeg, Iakoutsk, Vostok) :

        lineaire 22 / 6  (l'ancien reglage)   15,4 C   pire cas 40,9
        lineaire 45 / 6                        8,4 C   pire cas 25,1
        lineaire 60 / 20                       6,3 C   pire cas 16,4
        sinus 45, amorti 0,65                  4,7 C   pire cas 16,9

    L'ancien reglage donnait 16 C d'amplitude a Iakoutsk, qui en fait 57.
    """
    amp_eq = np.float32(temp_rules["seasonalAmplitudeEquatorC"])
    amp_pole = np.float32(temp_rules["seasonalAmplitudePoleC"])
    if str(temp_rules.get("seasonalAmplitudeShape", "linear")) == "sine":
        saison = np.sin(np.asarray(lat_norm, dtype=np.float32) * np.float32(np.pi * 0.5))
        frac = np.float32(temp_rules.get("oceanModerationFraction", 0.65))
        # L'amortissement ne porte que sur la part qui depend de la latitude :
        # a l'equateur il n'y a pas de saison a amortir.
        amp = amp_eq + (amp_pole - amp_eq) * saison * (
            np.float32(1.0) - frac * (np.float32(1.0) - cont))
    else:
        amp = amp_eq + (amp_pole - amp_eq) * np.asarray(lat_norm, dtype=np.float32)
        amp = amp - np.float32(temp_rules["oceanModerationC"]) * (np.float32(1.0) - cont)
    return np.maximum(amp, np.float32(1.0)).astype(np.float32)


def _continentality(is_water: np.ndarray, geo: Geometry, range_km: float) -> np.ndarray:
    """0 au bord de mer, tend vers 1 en profondeur des terres."""
    if not is_water.any():
        return np.ones_like(is_water, dtype=np.float32)
    dist_px = ndimage.distance_transform_edt(~is_water).astype(np.float32)
    dist_km = dist_px * np.float32(geo.meters_per_pixel / 1000.0)
    scale = max(float(range_km), 1e-3)
    return (np.float32(1.0) - np.exp(-dist_km / np.float32(scale))).astype(np.float32)


# ------------------------------------------------------------- precipitations


def _gradients(field: np.ndarray, spacing_m: float) -> tuple[np.ndarray, np.ndarray]:
    """Gradients (d/dy, d/dx) en unites par metre."""
    gy, gx = np.gradient(field.astype(np.float32), np.float32(spacing_m))
    return gy, gx


def _advect_moisture(
    elevation_m: np.ndarray,
    is_water: np.ndarray,
    temp_c: np.ndarray,
    u: np.ndarray,
    v: np.ndarray,
    geo: Geometry,
    prec: dict,
    rules_seed: int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Advection semi-lagrangienne de l'humidite. Retourne (pluie, soulevement, convergence)."""
    n = geo.n
    spacing = geo.meters_per_pixel

    # Capacite de l'air : Clausius-Clapeyron. L'air froid est sec par construction,
    # ce qui suffit a creer les deserts polaires sans aucune regle dediee.
    t_ref = np.float32(prec["saturationRefTempC"])
    t_scale = np.float32(max(float(prec["saturationScaleC"]), 1e-3))
    h_max = np.exp((temp_c - t_ref) / t_scale).astype(np.float32)
    h_max = np.clip(h_max, np.float32(0.02), np.float32(4.0))

    # Soulevement orographique : l'air pousse contre une pente montante precipite.
    land_elev = np.maximum(elevation_m, np.float32(0.0))
    dh_dy, dh_dx = _gradients(land_elev, spacing)
    uplift = (u * dh_dx + v * dh_dy).astype(np.float32)
    uplift_pos = np.maximum(uplift, np.float32(0.0))
    ref = float(np.percentile(uplift_pos, 99.0))
    uplift_norm = (uplift_pos / np.float32(max(ref, 1e-6))).astype(np.float32)
    np.clip(uplift_norm, 0.0, 2.0, out=uplift_norm)

    # Mouvement vertical des cellules de circulation, sous forme analytique.
    #
    # On NE derive PAS la divergence numeriquement du champ de vent : la gigue
    # haute frequence y produit des convergences locales plus fortes que le
    # signal des cellules, et la normalisation par centile ecrase alors la
    # ceinture desertique. La forme analytique donne exactement les bonnes
    # largeurs de bande.
    #
    #   omega(phi) = cos(3 * pi * |phi| / 90)     (3 cellules par hemisphere)
    #     |phi| =  0 -> +1  air ascendant  -> ZCIT, foret equatoriale
    #     |phi| = 30 -> -1  air descendant -> ceinture desertique
    #     |phi| = 60 -> +1  air ascendant  -> front polaire, rail des depressions
    #     |phi| = 90 -> -1  air descendant -> desert polaire
    cells = float(prec.get("cellsPerHemisphere", 3.0))
    abs_lat = np.abs(geo.latitude_grid()).astype(np.float32)
    half = np.float32(geo.lat_span_deg * 0.5)
    omega = np.cos(np.float32(cells * np.pi) * abs_lat / half).astype(np.float32)

    # Les cellules de circulation ne sont pas des rubans parfaits : la ZCIT
    # ondule, les anticyclones subtropicaux sont des cellules distinctes, la
    # mousson deplace le tout. Un bruit basse frequence deforme les bandes en
    # latitude, sans changer leur structure d'ensemble.
    wobble = float(prec.get("cellWobble", 0.0))
    if wobble > 0.0:
        omega = omega + np.float32(wobble) * noise.fbm(
            n, float(prec.get("cellWobbleFrequency", 2.4)), 4, rules_seed + 6151
        )
        np.clip(omega, -1.0, 1.0, out=omega)

    conv_norm = np.maximum(omega, np.float32(0.0))
    subs_norm = np.maximum(-omega, np.float32(0.0))

    # La ZCIT equatoriale est bien plus energetique que le front polaire : la
    # cellule de Hadley brasse une atmosphere chaude et epaisse, la cellule de
    # Ferrel une atmosphere froide et mince. Sans cette ponderation, cos() donne
    # +1 aux deux et la bande 55-60 deg devient aussi arrosee que l'equateur.
    front = np.float32(prec.get("polarFrontStrength", 0.45))
    conv_norm = conv_norm * (front + (np.float32(1.0) - front) * (np.float32(1.0) - abs_lat / half))

    # Les facteurs sont des MULTIPLES de la pluie de base, pas des coefficients
    # opaques : orographicFactor = 3 signifie "un versant pleinement au vent
    # recoit jusqu'a 3 fois la pluie de base en supplement".
    rate = np.float32(prec["baseRainRate"]) * (
        np.float32(1.0)
        + np.float32(prec["orographicFactor"]) * uplift_norm
        + np.float32(prec["convergenceFactor"]) * conv_norm
    )
    # Subsidence : la branche descendante de la cellule de Hadley supprime la
    # pluie vers 30 deg. C'est ce terme qui creuse la ceinture desertique --
    # sans lui, les tropiques restent mous.
    suppress = np.float32(1.0) - np.float32(prec["subsidenceFactor"]) * subs_norm
    rate = rate * np.maximum(suppress, np.float32(0.05))
    np.clip(rate, 0.0, 0.9, out=rate)

    evap = np.float32(prec["evaporationRate"])
    conv_moisture = np.float32(prec["moistureConvergenceRate"])
    step = np.float32(prec["advectionStepPx"])
    sweeps = int(prec["advectionSweeps"])

    # TRANSPORT MERIDIEN PAR LES TOURBILLONS -- le rail des depressions.
    #
    # L'advection ci-dessous suit le vent MOYEN, qui est zonal aux moyennes
    # latitudes : rien, dans ce modele, ne porte l'humidite vers les poles. Or
    # sur Terre ce transport n'est pas assure par le vent moyen mais par les
    # DEPRESSIONS BAROCLINIQUES, des tourbillons nes du fort gradient meridien
    # de temperature vers 50-60 degres, et qu'une simulation a cette resolution
    # ne resout pas. Sans terme dedie, ils n'existent tout simplement pas.
    #
    # CE QUE COUTAIT LEUR ABSENCE, mesure sur la graine 20260909 :
    #   60-70 deg  170 mm/an   (Terre ~500)
    #   70-80 deg   11 mm/an   (Terre ~250)
    #   80-90 deg    0 mm/an   (Terre ~150)
    # avec pour consequences une toundra a 4,7 % des terres contre 8 attendus,
    # une taiga a 6,4 contre 10, et surtout une neige quasi impossible : le
    # prereglage de toundra ne portait que 22,2 mm d'equivalent-eau en hiver,
    # soit 2,3 % de chances de neige par tirage de meteo.
    #
    # CE N'EST PAS L'ECRETAGE A SATURATION, et c'est mesure : la part
    # d'humidite supprimee par `min(humidity, h_max)` vaut 0,0 % au-dela de
    # 60 degres. Elle ne mord qu'a +-38 degres (15 a 23 %), la ou de l'air
    # chaud croise du froid. Piste ecartee.
    #
    # LA FORME : un melange DESCENDANT LE GRADIENT, ce que fait un tourbillon.
    # Un lissage gaussien en latitude est exactement l'operateur de diffusion
    # correspondant ; son ecart-type porte la taille caracteristique du
    # tourbillon. Le terme NE CREE PAS d'eau, il en deplace -- la capacite
    # reste gouvernee par Clausius-Clapeyron, et l'humidite transportee est
    # ecretee a `h_max` comme partout, donc elle PRECIPITE en chemin, comme une
    # depression qui se vide en remontant vers le nord.
    # L'echelle du tourbillon est donnee en DEGRES de latitude, jamais en
    # pixels : le banc de calibration tourne en simulation 1025 et le monde
    # final en 2049 : un sigma en pixels ne decrirait pas le meme phenomene
    # dans les deux cas. C'est la regle d'echelle du projet -- ce qui est
    # RELATIF ne change pas avec la resolution.
    eddy = float(prec.get("eddyMixingRate", 0.0))
    eddy_deg = float(prec.get("eddyMixingSigmaDeg", 0.0))
    eddy_sigma = eddy_deg * (float(n) / max(float(geo.lat_span_deg), 1e-6))
    k_eddy = None
    if eddy > 0.0 and eddy_sigma > 0.0:
        track = np.float32(prec.get("stormTrackLatDeg", 55.0))
        width = np.float32(max(float(prec.get("stormTrackWidthDeg", 20.0)), 1e-3))
        # Gaussienne centree sur le rail : maximale la ou naissent les
        # depressions, nulle a l'equateur comme au pole.
        k_eddy = (np.float32(eddy)
                  * np.exp(-(((abs_lat - track) / width) ** 2))).astype(np.float32)

    jj, ii = np.meshgrid(
        np.arange(n, dtype=np.float32), np.arange(n, dtype=np.float32), indexing="ij"
    )
    src_j = jj - v * step
    src_i = ii - u * step
    coords = np.stack((src_j, src_i))

    humidity = np.zeros((n, n), dtype=np.float32)
    precip = np.zeros((n, n), dtype=np.float32)
    water_rain_damp = np.float32(0.35)

    for _ in range(sweeps):
        humidity = ndimage.map_coordinates(
            humidity, coords, order=1, mode="nearest"
        ).astype(np.float32)
        # Evaporation au-dessus de l'eau, jamais au-dessus des terres :
        # c'est ce qui fait emerger la continentalite.
        humidity = np.where(is_water, humidity + evap * (h_max - humidity), humidity)

        # CONVERGENCE DE L'HUMIDITE.
        #
        # L'advection semi-lagrangienne TRANSPORTE l'humidite mais ne la
        # CONCENTRE pas : map_coordinates echantillonne un seul point amont, si
        # bien que des vents convergents n'accumulent aucune masse. Or c'est
        # precisement ce qui alimente la ZCIT : les alizes des deux hemispheres
        # convergent et empilent l'humidite d'un immense bassin oceanique.
        #
        # Sans ce terme, l'equateur avait le taux de pluie le plus fort (0,0159)
        # ET l'humidite la plus faible du monde (0,019 pour une capacite de
        # 1,395, soit 1,3 % de saturation) : l'air y etait lessive en quelques
        # kilometres apres la cote et l'interieur restait sec. La bande la plus
        # humide tombait a -42 degres au lieu de l'equateur.
        #
        # La forme est celle de l'evaporation -- un apport proportionnel au
        # deficit de saturation -- mais pilote par la convergence et actif AUSSI
        # au-dessus des terres, parce que la pluie de mousson tombe sur les
        # continents.
        humidity = humidity + conv_moisture * conv_norm * (h_max - humidity)

        # Melange meridien par les tourbillons (voir l'explication plus haut).
        # `mode="nearest"` et non un enroulement : le pole n'est pas voisin de
        # l'autre pole, et un enroulement y ferait passer l'humidite australe
        # dans l'Arctique.
        if k_eddy is not None:
            melangee = ndimage.gaussian_filter1d(
                humidity, eddy_sigma, axis=0, mode="nearest"
            ).astype(np.float32)
            humidity = humidity + k_eddy * (melangee - humidity)

        np.clip(humidity, 0.0, None, out=humidity)
        np.minimum(humidity, h_max, out=humidity)

        d_precip = humidity * rate
        d_precip = np.where(is_water, d_precip * water_rain_damp, d_precip)
        humidity -= d_precip
        precip += d_precip

    return precip, uplift_norm, conv_norm


# ------------------------------------------------------------------- pipeline


def generate(rules: Rules, geo: Geometry, elevation_m: np.ndarray) -> ClimateResult:
    temp_rules = rules["temperature"]
    prec = rules["precipitation"]

    is_water = elevation_m <= 0.0

    t_sea = _sea_level_temperature(geo, temp_rules)
    lapse = np.float32(temp_rules["lapseRateCPerKm"])
    altitude_km = np.maximum(elevation_m, np.float32(0.0)) / np.float32(1000.0)
    temp_mean = (t_sea - lapse * altitude_km).astype(np.float32)

    cont = _continentality(is_water, geo, float(temp_rules["oceanModerationRangeKm"]))

    # REFROIDISSEMENT CONTINENTAL DES HAUTES LATITUDES. Il manquait : la
    # continentalite ne jouait que sur l'AMPLITUDE saisonniere, jamais sur la
    # moyenne. Or aux hautes latitudes l'ocean ne fait pas que lisser l'annee,
    # il la rechauffe : il retient assez de chaleur pour que l'hiver cotier
    # reste pres de 0, quand un interieur continental descend a -40. L'ecart de
    # moyenne ANNUELLE est enorme et bien documente -- Iakoutsk, 62 N dans les
    # terres, -8,8 C ; Bergen, 60 N sur la cote, +7,6 C, soit 16 degres pour
    # deux degres de latitude.
    #
    # Sans ce terme, la taiga et la toundra n'avaient nulle part ou exister :
    # elles vivent dans les interieurs continentaux, pas sur les cotes. Le terme
    # ne mord qu'au-dessus de `continentalCoolingLat0Deg` -- sous les tropiques
    # un interieur continental est au contraire plus CHAUD que sa latitude, et
    # c'est la prime d'aridite plus bas qui s'en charge.
    #
    # Applique AVANT les precipitations, a la difference de la prime d'aridite :
    # un air plus froid porte moins de vapeur, donc ce refroidissement doit
    # etre vu par le calcul de la pluie, et de la par l'erosion et le relief.
    froid = float(temp_rules.get("continentalCoolingC", 0.0))
    if froid > 0.0:
        l0 = np.float32(temp_rules.get("continentalCoolingLat0Deg", 25.0))
        l1 = np.float32(temp_rules.get("continentalCoolingLat1Deg", 60.0))
        poids = noise.smoothstep(l0, l1, np.abs(geo.latitude_grid()).astype(np.float32))
        temp_mean = (temp_mean - np.float32(froid) * cont * poids).astype(np.float32)

    # AMPLITUDE SAISONNIERE : faible a l'equateur, forte dans les interieurs de
    # haute latitude, amortie au bord de mer (l'ocean est un volant thermique).
    #
    # DEUX FORMES, reglees par `temperature.seasonalAmplitudeShape`. La forme
    # "sine" est la bonne, et l'ancienne "linear" se trompait DEUX FOIS :
    #
    #   1. l'amplitude croissait LINEAIREMENT avec la latitude, alors que le
    #      contraste saisonnier suit le SINUS de la latitude -- c'est la
    #      projection de la declinaison solaire. En lineaire, corriger les poles
    #      casse les latitudes moyennes et inversement : aucun chiffre ne
    #      satisfait les deux ;
    #   2. l'ocean amortissait par SOUSTRACTION (amp - 6), alors qu'il amortit
    #      PROPORTIONNELLEMENT : il divise le contraste, il n'en retranche pas
    #      un nombre fixe de degres. La soustraction donnait des amplitudes
    #      negatives aux basses latitudes maritimes, qu'il fallait borner a 1 --
    #      un rustinage qui signalait la mauvaise forme.
    #
    # Ecart absolu moyen mesure sur huit stations reelles (Singapour, Lisbonne,
    # Teheran, Chicago, Bergen, Winnipeg, Iakoutsk, Vostok) :
    #
    #   lineaire 22 / 6  (l'ancien reglage)   15,4 C   pire cas 40,9
    #   lineaire 45 / 6                        8,4 C   pire cas 25,1
    #   lineaire 60 / 20                       6,3 C   pire cas 16,4
    #   sinus 45, amorti 0,65                  4,7 C   pire cas 16,9
    #
    # L'ancien reglage donnait 16 C d'amplitude a Iakoutsk, qui en fait 57.
    # Cela comptait bien au-dela des biomes : cette amplitude est exactement ce
    # qui alimente les temperatures d'hiver et d'ete des presets Ultra Dynamic
    # Sky, donc les hivers du jeu etaient beaucoup trop doux.
    lat_norm = np.abs(geo.latitude_grid()).astype(np.float32) / np.float32(geo.lat_span_deg * 0.5)
    amp = seasonal_amplitude(lat_norm, cont, temp_rules)

    temp_min = (temp_mean - amp * np.float32(0.5)).astype(np.float32)
    temp_max = (temp_mean + amp * np.float32(0.5)).astype(np.float32)

    u, v = wind_field(geo, rules)
    precip_raw, uplift, conv = _advect_moisture(
        elevation_m, is_water, temp_mean, u, v, geo, prec, rules.seed
    )

    sigma = float(prec.get("smoothSigmaPx", 0.0))
    if sigma > 0.0:
        precip_raw = ndimage.gaussian_filter(precip_raw, sigma).astype(np.float32)

    # Mise a l'echelle en mm/an, ancree sur une valeur PHYSIQUE : la MOYENNE des
    # precipitations sur les terres emergees, environ 715 mm/an sur Terre.
    #
    # ERREUR CORRIGEE LE 11 SEPTEMBRE 2026, ET ELLE COUTAIT CHER : cette valeur
    # de 715 mm etait prise pour la MEDIANE. Or c'est la MOYENNE, et la
    # distribution des pluies est tres dissymetrique (les deserts tassent la
    # moitie basse, les tropiques etirent la haute). Ancrer la mediane sur une
    # moyenne rendait donc le monde beaucoup trop humide : moyenne mesuree
    # 1217 mm contre 715 sur Terre, soit +70 %, et 23,7 % des terres au-dessus
    # de 2000 mm quand la Terre en a 7 a 8 %. D'ou trop de forets et pas assez
    # de prairies, de savanes et de deserts.
    #
    # Surtout pas une normalisation par centile haut : elle se sabote elle-meme.
    # Augmenter l'evaporation ferait monter le pic equatorial, donc rabaisserait
    # tout le reste de la carte, et le monde deviendrait plus aride alors qu'on
    # vient d'y mettre plus d'eau.
    cible = float(prec.get("targetMeanLandMm", prec.get("targetMedianLandMm", 715.0)))
    plafond = float(prec["maxPrecipMm"])
    sol = ~is_water
    # Deux passes : l'ecretage au plafond retire de l'eau, donc la premiere mise
    # a l'echelle manque la cible par le bas. La seconde la rattrape.
    precip_mm = precip_raw.astype(np.float32)
    for _ in range(2):
        vals = precip_mm[sol]
        ref = float(np.mean(vals)) if vals.size else float(np.mean(precip_mm))
        precip_mm = np.clip(precip_mm * np.float32(cible / max(ref, 1e-9)),
                            0.0, plafond).astype(np.float32)

    # Prime de chaleur aride. Un desert est plus chaud que sa latitude : ciel
    # degage, donc plus d'insolation atteint le sol, et pas d'evaporation pour
    # en consommer une part en chaleur latente. Sans ce terme, la temperature
    # ne connait que la latitude et l'altitude, et nos deserts chauds
    # plafonnaient a 16,6 degres de moyenne quand un desert reel est a 22-34.
    #
    # LE TERME EST APPLIQUE APRES LES PRECIPITATIONS, ET C'EST VOULU. L'ordre
    # est d'abord causal -- c'est la secheresse qui rechauffe, pas l'inverse --
    # mais il a aussi une consequence pratique qui vaut d'etre dite : la pluie
    # pilote l'erosion, donc le relief. Calculer la prime avant la pluie ferait
    # bouger le terrain, les rivieres et les lacs a chaque reglage de cette
    # seule valeur. Ici, regler `aridityHeatC` ne deplace que les biomes.
    chaleur = np.float32(temp_rules.get("aridityHeatC", 0.0))
    if chaleur > 0.0:
        ref_mm = np.float32(max(float(temp_rules.get("aridityRefMm", 600.0)), 1e-3))
        aridite = np.clip(np.float32(1.0) - precip_mm / ref_mm, 0.0, 1.0)
        # L'effet suit l'energie solaire recue, donc le cosinus de la latitude :
        # un desert polaire est sec mais ne recoit rien a amplifier.
        ensoleillement = np.clip(
            np.cos(np.radians(geo.latitude_grid().astype(np.float32))), 0.0, 1.0)
        prime = (chaleur * aridite * ensoleillement).astype(np.float32)
        temp_mean = (temp_mean + prime).astype(np.float32)
        temp_min = (temp_min + prime).astype(np.float32)
        temp_max = (temp_max + prime).astype(np.float32)

    return ClimateResult(
        temp_mean_c=temp_mean,
        temp_min_c=temp_min,
        temp_max_c=temp_max,
        seasonal_amp_c=amp,
        precip_mm=precip_mm,
        wind_u=u,
        wind_v=v,
        continentality=cont,
        orographic_uplift=uplift,
        convergence=conv,
    )
