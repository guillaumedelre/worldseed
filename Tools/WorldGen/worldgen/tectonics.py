"""Etape 1 - tectonique et relief brut.

Des plaques de Voronoi derivent les unes contre les autres. Les frontieres
convergentes soulevent des chaines, les divergentes ouvrent des rifts. Par
dessus vient un fBm a warp de domaine pour le detail.

Pourquoi des plaques plutot qu'un simple bruit fractal : les chaines de
montagnes doivent etre LINEAIRES et ORIENTEES. C'est ce qui produit de vraies
ombres pluviometriques a l'etape climat. Un fBm seul donne des bosses isotropes,
donc un climat sans structure.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy import ndimage
from scipy.spatial import cKDTree

from . import noise
from .config import Geometry, Rules


@dataclass
class TectonicResult:
    elevation_m: np.ndarray      # float32 [n, n], 0 = niveau de la mer
    plate_id: np.ndarray         # int16 [n, n]
    is_continental: np.ndarray   # bool [n, n], type de plaque (avant niveau marin)
    convergence: np.ndarray      # float32 [n, n], > 0 collision, < 0 rift
    sea_level_shift_m: float


def generate(rules: Rules, geo: Geometry) -> TectonicResult:
    n = geo.n
    tec = rules["tectonics"]
    rng = rules.rng("tectonics")

    n_plates = int(tec["plateCount"])
    land_ratio = float(tec["landRatio"])

    # --- plaques : germes, type, vecteur de derive ------------------------------
    seeds = rng.random((n_plates, 2), dtype=np.float64)          # (y, x) en [0, 1]
    drift = rng.normal(0.0, 1.0, (n_plates, 2))
    drift /= np.maximum(np.linalg.norm(drift, axis=1, keepdims=True), 1e-9)
    drift *= rng.uniform(0.35, 1.0, (n_plates, 1))

    # Assez de plaques continentales pour tenir le ratio vise ; le niveau marin
    # final est de toute facon fixe par quantile, ceci ne regle que la forme.
    n_continental = int(round(n_plates * min(0.95, land_ratio * 1.35)))
    order = rng.permutation(n_plates)
    plate_is_continental = np.zeros(n_plates, dtype=bool)
    plate_is_continental[order[:n_continental]] = True

    # --- diagramme de Voronoi : plaque la plus proche et la suivante ------------
    # Les coordonnees d'interrogation sont DEPLACEES par un bruit basse frequence.
    # Sans ce warp, les frontieres de plaques sont des segments de droite : on
    # obtient des continents en polygone et des chaines de montagnes rectilignes,
    # qui se voient immediatement sur une carte.
    axis = np.linspace(0.0, 1.0, n, dtype=np.float64)
    gy, gx = np.meshgrid(axis, axis, indexing="ij")

    warp = float(tec.get("plateWarpStrength", 0.0))
    if warp > 0.0:
        freq = float(tec.get("plateWarpFrequency", 2.2))
        wy = noise.fbm(n, freq, 5, rules.seed + 4441)
        wx = noise.fbm(n, freq, 5, rules.seed + 4457)
        qy = gy + warp * wy
        qx = gx + warp * wx
    else:
        qy, qx = gy, gx
    points = np.column_stack((qy.ravel(), qx.ravel()))

    tree = cKDTree(seeds)
    dist, idx = tree.query(points, k=2, workers=-1)
    d1 = dist[:, 0].astype(np.float32)
    d2 = dist[:, 1].astype(np.float32)
    p1 = idx[:, 0]
    p2 = idx[:, 1]

    plate_id = p1.reshape(n, n).astype(np.int16)
    is_continental = plate_is_continental[p1].reshape(n, n)

    # --- convergence aux frontieres --------------------------------------------
    # n_ab : direction unitaire de la plaque a vers la plaque b.
    delta = seeds[p2] - seeds[p1]
    delta_norm = np.maximum(np.linalg.norm(delta, axis=1, keepdims=True), 1e-9)
    normal = delta / delta_norm
    relative = drift[p1] - drift[p2]
    convergence = np.einsum("ij,ij->i", relative, normal).astype(np.float32)

    # Proximite de la frontiere : (d2 - d1) vaut 0 exactement sur la frontiere.
    width = max(float(tec["mountainWidthKm"]) * 1000.0 / geo.size_m, 1e-4)
    proximity = np.exp(-(d2 - d1) / np.float32(width))

    convergence = convergence.reshape(n, n)
    proximity = proximity.reshape(n, n)

    # --- assemblage du relief ---------------------------------------------------
    # Le masque continental est adouci PUIS bruite avant d'etre reseuille. C'est
    # l'etape decisive pour la forme des cotes : un simple where() donne une
    # marche de 900 m pile sur l'arete de Voronoi, que le bruit de detail (260 m)
    # ne peut pas casser -- d'ou des continents en polygone. En rendant le MASQUE
    # fractal, le trait de cote devient decoupe a toutes les echelles.
    smooth_px = float(tec.get("continentSmoothKm", 0.0)) * 1000.0 / geo.meters_per_pixel
    mask = is_continental.astype(np.float32)
    if smooth_px >= 0.5:
        mask = ndimage.gaussian_filter(mask, smooth_px, mode="nearest")

    coast_amount = float(tec.get("coastNoiseAmount", 0.0))
    if coast_amount > 0.0:
        coast_freq = float(tec.get("coastNoiseFrequency", 5.5))
        mask = mask + np.float32(coast_amount) * noise.fbm(
            n, coast_freq, 6, rules.seed + 8821
        )

    blend = noise.smoothstep(0.35, 0.65, mask)
    base = (
        np.float32(tec["oceanDepthM"])
        + (np.float32(tec["continentBaseM"]) - np.float32(tec["oceanDepthM"])) * blend
    ).astype(np.float32)
    # Le type de plaque servant plus loin (amplitude du detail) suit le meme seuil.
    is_continental = blend > 0.5

    # Les chaines suivent la frontiere mais ne sont pas des murs reguliers :
    # un bruit a cretes module leur hauteur le long de la suture.
    ridge_tex = noise.ridged(n, 14.0, 5, rules.seed + 5501)
    uplift = (
        np.float32(tec["mountainHeightM"])
        * np.clip(convergence, 0.0, 1.0)
        * proximity
        * (np.float32(0.45) + np.float32(0.55) * ridge_tex)
    )
    rift = (
        np.float32(tec["riftDepthM"])
        * np.clip(-convergence, 0.0, 1.0)
        * proximity
    )

    detail = noise.domain_warped_fbm(
        n,
        frequency=float(tec["baseFrequency"]),
        octaves=int(tec["octaves"]),
        seed=rules.seed + 911,
        warp_strength=float(tec["warpStrength"]),
        warp_frequency=float(tec["warpFrequency"]),
        lacunarity=float(tec["lacunarity"]),
        gain=float(tec["gain"]),
    )
    # Une part de bruit a CRETES melangee au fBm. Le fBm seul donne des collines
    # rondes et molles ; les cretes apportent les aretes et les lignes de partage
    # des eaux, qui sont ce que l'oeil lit comme "montagne".
    ridge_mix = float(tec.get("ridgedMix", 0.0))
    if ridge_mix > 0.0:
        crests = noise.ridged(
            n, float(tec["baseFrequency"]) * 1.7, int(tec["octaves"]),
            rules.seed + 3313, float(tec["lacunarity"]), float(tec["gain"]),
        )
        detail = detail * np.float32(1.0 - ridge_mix) + (
            crests * np.float32(2.0) - np.float32(1.0)
        ) * np.float32(ridge_mix)

    # Le detail porte plus loin sur les continents que sur les fonds oceaniques.
    detail_amp = np.where(
        is_continental,
        np.float32(tec["detailAmplitudeContinentM"]),
        np.float32(tec["detailAmplitudeOceanM"]),
    )

    elevation = base + uplift - rift + detail * detail_amp

    # --- contrainte polaire -----------------------------------------------------
    elevation = _force_poles(elevation, geo, tec)

    # --- bordure oceanique -------------------------------------------------------
    elevation = _force_ocean_border(elevation, geo, tec)

    # --- niveau de la mer par quantile : le ratio terres/mers est exact ---------
    shift = float(np.quantile(elevation, 1.0 - land_ratio))
    elevation = elevation - np.float32(shift)

    # --- plateau continental ----------------------------------------------------
    elevation = _shelf(elevation, geo, tec)

    w = rules["world"]
    elevation = np.clip(
        elevation, float(w["minElevationM"]), float(w["maxElevationM"])
    ).astype(np.float32)

    return TectonicResult(
        elevation_m=elevation,
        plate_id=plate_id,
        is_continental=is_continental,
        convergence=convergence,
        sea_level_shift_m=shift,
    )


def _force_poles(elevation: np.ndarray, geo: Geometry, tec: dict) -> np.ndarray:
    """Impose un pole oceanique et/ou continental, comme sur Terre.

    Terre : pole nord = ocean sous banquise, pole sud = continent sous calotte.
    """
    radius = float(tec["poleForcingRadiusDeg"])
    strength = float(tec["poleForcingStrength"])
    if radius <= 0.0 or strength <= 0.0:
        return elevation

    lat = geo.latitude_grid().astype(np.float32)
    half = geo.lat_span_deg * 0.5
    out = elevation

    for pole, sign in (("northPole", +1.0), ("southPole", -1.0)):
        mode = tec[pole]
        if mode == "free":
            continue
        # Poids 0 loin du pole, 1 au pole.
        signed_lat = lat * np.float32(sign)
        w = noise.smoothstep(half - radius, half, signed_lat) * np.float32(strength)
        if mode == "ocean":
            target = np.float32(tec["oceanDepthM"])
        else:
            # Un pole continental est un PLATEAU, pas une simple terre emergee :
            # l'Antarctique culmine a 2 000 m sous sa calotte. D'ou une prime
            # d'altitude au-dessus du socle continental ordinaire. Cette valeur
            # est METRIQUE, donc liee a l'echelle du monde : elle etait ecrite en
            # dur (400 m) et n'a pas suivi le passage de 32 a 8 km, ce qui a
            # pousse le pole sud a 430 m au lieu de 130 et fait passer la calotte
            # de 7,1 a 11,9 % des terres. Voir world._comment_echelle.
            target = (np.float32(tec["continentBaseM"])
                      + np.float32(tec.get("poleContinentBonusM", 400.0)))
        out = out * (np.float32(1.0) - w) + target * w

    return out.astype(np.float32)


def _force_ocean_border(elevation: np.ndarray, geo: Geometry, tec: dict) -> np.ndarray:
    """Noie une marge tout autour du monde.

    Sans cela, les terres touchent le bord de la carte : les fleuves sortent du
    domaine au lieu de rejoindre la mer, et le joueur bute sur une falaise de
    fin de monde. Une ceinture d'ocean rend le monde fini de facon naturelle.
    """
    border_km = float(tec.get("oceanBorderKm", 0.0))
    if border_km <= 0.0:
        return elevation

    n = geo.n
    border_px = border_km * 1000.0 / geo.meters_per_pixel
    if border_px < 1.0:
        return elevation

    idx = np.arange(n, dtype=np.float32)
    edge = np.minimum(idx, (n - 1) - idx)
    dist = np.minimum(edge[:, None], edge[None, :]).astype(np.float32)

    # 0 au bord (pleine mer) -> 1 au dela de la marge (relief intact).
    w = noise.smoothstep(0.0, float(border_px), dist)
    depth = np.float32(tec["oceanDepthM"])
    return (elevation * w + depth * (np.float32(1.0) - w)).astype(np.float32)


def _shelf(elevation: np.ndarray, geo: Geometry, tec: dict) -> np.ndarray:
    """Fait remonter le fond marin en approchant de la cote.

    Sans plateau continental, on passe de +120 m a -800 m en une cellule : une
    falaise sous-marine de 80 degres tout autour de chaque continent. Cela
    pollue les statistiques de pente, casse la lecture des plages et donne des
    cotes en mur. Ici la bathymetrie existante est simplement multipliee par une
    rampe qui vaut 0 au trait de cote et 1 au large.
    """
    width_km = float(tec.get("shelfWidthKm", 0.0))
    if width_km <= 0.0:
        return elevation

    ocean = elevation < 0.0
    if not ocean.any():
        return elevation

    dist_km = ndimage.distance_transform_edt(ocean).astype(np.float32)
    dist_km *= np.float32(geo.meters_per_pixel / 1000.0)
    ramp = noise.smoothstep(0.0, width_km, dist_km)

    out = elevation.copy()
    out[ocean] = (elevation * ramp)[ocean]
    return out.astype(np.float32)
