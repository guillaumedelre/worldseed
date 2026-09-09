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
    """T(phi) = T_eq - (T_eq - T_pole) * (|phi| / phi_max) ^ k.

    On n'utilise PAS sin^2(phi), qui est le reflexe habituel : cette loi refroidit
    beaucoup trop vite les latitudes moyennes. Comparaison avec le profil zonal
    reel de la Terre (T_eq = 27, T_pole = -25) :

        latitude    Terre     sin^2      (phi/90)^2
           30 deg    20 C      14 C         21 C
           45 deg    12 C       1 C         14 C
           60 deg     0 C     -12 C          4 C

    La puissance de la latitude normalisee colle au reel a 2-4 degres pres, la
    loi en sinus se trompe de 12 degres a 60 deg -- assez pour couvrir de calotte
    glaciaire tout un hemisphere.
    """
    lat = np.abs(geo.latitude_grid()).astype(np.float32)
    half = np.float32(max(geo.lat_span_deg * 0.5, 1e-6))
    k = np.float32(temp_rules.get("latitudeExponent", 2.0))
    t_eq = np.float32(temp_rules["equatorC"])
    t_pole = np.float32(temp_rules["poleC"])
    return (t_eq - (t_eq - t_pole) * np.power(lat / half, k)).astype(np.float32)


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

    # Amplitude saisonniere : faible a l'equateur, forte aux poles, amortie au
    # bord de mer (l'ocean est un volant thermique).
    lat_norm = np.abs(geo.latitude_grid()).astype(np.float32) / np.float32(geo.lat_span_deg * 0.5)
    amp = (
        np.float32(temp_rules["seasonalAmplitudeEquatorC"])
        + (np.float32(temp_rules["seasonalAmplitudePoleC"])
           - np.float32(temp_rules["seasonalAmplitudeEquatorC"])) * lat_norm
    )
    amp = amp - np.float32(temp_rules["oceanModerationC"]) * (np.float32(1.0) - cont)
    amp = np.maximum(amp, np.float32(1.0)).astype(np.float32)

    temp_min = (temp_mean - amp * np.float32(0.5)).astype(np.float32)
    temp_max = (temp_mean + amp * np.float32(0.5)).astype(np.float32)

    u, v = wind_field(geo, rules)
    precip_raw, uplift, conv = _advect_moisture(
        elevation_m, is_water, temp_mean, u, v, geo, prec, rules.seed
    )

    sigma = float(prec.get("smoothSigmaPx", 0.0))
    if sigma > 0.0:
        precip_raw = ndimage.gaussian_filter(precip_raw, sigma).astype(np.float32)

    # Mise a l'echelle en mm/an, ancree sur une valeur PHYSIQUE : la mediane des
    # precipitations terrestres (~715 mm/an sur Terre).
    #
    # Surtout pas une normalisation par centile haut : elle se sabote elle-meme.
    # Augmenter l'evaporation ferait monter le pic equatorial, donc rabaisserait
    # tout le reste de la carte, et le monde deviendrait plus aride alors qu'on
    # vient d'y mettre plus d'eau.
    land_vals = precip_raw[~is_water]
    ref = float(np.median(land_vals)) if land_vals.size else float(np.median(precip_raw))
    scale = float(prec["targetMedianLandMm"]) / max(ref, 1e-9)
    precip_mm = np.clip(
        precip_raw * np.float32(scale), 0.0, float(prec["maxPrecipMm"])
    ).astype(np.float32)

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
