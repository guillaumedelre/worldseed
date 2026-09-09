"""Etape 5 - classification des biomes (Whittaker + etagement altitudinal).

L'etagement n'est PAS une regle separee : il tombe tout seul du gradient
adiabatique applique a l'etape 2. Un sommet tropical a 3 000 m est a 7 degres,
donc le diagramme de Whittaker y lit "foret temperee" - une foret de montagne.
A 4 500 m il lit "toundra". C'est exactement le Kilimandjaro, sans une seule
regle d'altitude.

Les surcharges ci-dessous ne traitent que ce que le couple temperature/pluie ne
peut pas savoir : l'eau, la roche a nu sur les fortes pentes, l'estran, le marais.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy import ndimage

from . import hydrology
from .config import Rules


@dataclass
class BiomeResult:
    index: np.ndarray            # uint8 [n, n], identifiant de biome
    slope_deg: np.ndarray        # float32
    is_water: np.ndarray         # bool (ocean + lac + riviere)
    river_mask: np.ndarray       # bool
    lake_mask: np.ndarray        # bool
    counts: dict[str, float]     # part de chaque biome sur les terres, en %


def slope_degrees(dem: np.ndarray, spacing_m: float) -> np.ndarray:
    dy, dx = np.gradient(dem.astype(np.float32), np.float32(spacing_m))
    return np.degrees(np.arctan(np.sqrt(dx * dx + dy * dy))).astype(np.float32)


def _whittaker(
    temp_c: np.ndarray, precip_mm: np.ndarray, bands: list, ids: dict
) -> np.ndarray:
    """Lecture du diagramme : bande de temperature, puis seuil de precipitation.

    Les bandes sont parcourues de la plus froide a la plus chaude et la premiere
    dont `tMax` depasse la temperature l'emporte ; a l'interieur, le premier
    seuil de pluie depasse l'emporte. La validation des regles garantit que les
    listes sont croissantes et couvrent tout le domaine.
    """
    out = np.full(temp_c.shape, 255, dtype=np.uint8)
    unassigned = np.ones(temp_c.shape, dtype=bool)

    for band in bands:
        in_band = unassigned & (temp_c <= np.float32(band["tMax"]))
        if not in_band.any():
            continue
        remaining = in_band.copy()
        for p_max, name in band["cuts"]:
            sel = remaining & (precip_mm <= np.float32(p_max))
            if sel.any():
                out[sel] = np.uint8(ids[name])
                remaining &= ~sel
            if not remaining.any():
                break
        unassigned &= ~in_band

    if unassigned.any():
        # Filet de securite : la derniere bande, seuil le plus haut.
        out[unassigned] = np.uint8(ids[bands[-1]["cuts"][-1][1]])
    return out


def classify(
    rules: Rules,
    geo,
    dem: np.ndarray,
    temp_mean_c: np.ndarray,
    temp_max_c: np.ndarray,
    precip_mm: np.ndarray,
    lake_depth_m: np.ndarray,
    accumulation: np.ndarray,
) -> BiomeResult:
    bio = rules["biomes"]
    ids = bio["ids"]
    spacing = geo.meters_per_pixel

    slope = slope_degrees(dem, spacing)
    is_land = dem > 0.0
    is_ocean = ~is_land

    # Meme critere que l'export des lacs : une cuvette comblee n'est un lac
    # que si elle est assez profonde et assez etendue (hydrology.lake_mask).
    lake_mask = hydrology.lake_mask(lake_depth_m, is_land, geo, rules["hydrology"])
    channel = (accumulation >= float(rules["hydrology"]["riverDischargeThreshold"])) & is_land
    riparian = int(rules["hydrology"]["riparianCells"])
    if riparian > 0:
        river_mask = ndimage.binary_dilation(channel, iterations=riparian) & is_land
    else:
        river_mask = channel

    index = _whittaker(temp_mean_c, precip_mm, bio["whittakerBands"], ids)

    # --- surcharges, de la moins prioritaire a la plus prioritaire -------------

    # Etage alpin : au-dessus de la limite des arbres ET en altitude reelle.
    # Sans le critere d'altitude, toute la toundra polaire de bord de mer
    # basculerait en "alpin", ce qui n'a aucun sens.
    alpine = (
        is_land
        & (temp_mean_c < np.float32(bio["treeLineTempC"]))
        & (dem > np.float32(bio["alpineMinElevationM"]))
    )
    index[alpine] = np.uint8(ids["alpin"])

    # Roche a nu : au-dela de l'angle de tenue, aucune terre ne reste, quel que
    # soit le climat.
    index[is_land & (slope > np.float32(bio["bareRockSlopeDeg"]))] = np.uint8(ids["roche_nue"])

    # Calotte : meme le mois le plus chaud reste sous le seuil de gel permanent.
    index[is_land & (temp_max_c < np.float32(bio["permanentIceTempC"]))] = np.uint8(ids["calotte"])

    # Estran : bande basse au contact de l'ocean.
    coast = ndimage.binary_dilation(is_ocean, iterations=2) & is_land
    beach = coast & (dem < np.float32(bio["beachElevationM"])) & (slope < np.float32(12.0))
    index[beach] = np.uint8(ids["plage"])

    # Marais : plat, humide, au contact de l'eau douce.
    near_fresh = ndimage.binary_dilation(lake_mask | channel, iterations=3) & is_land
    marsh = (
        near_fresh
        & (slope < np.float32(bio["marshMaxSlopeDeg"]))
        & (precip_mm > np.float32(bio["marshMinPrecipMm"]))
    )
    index[marsh] = np.uint8(ids["marais"])

    # L'eau ecrase tout le reste.
    index[river_mask] = np.uint8(ids["riviere"])
    index[lake_mask] = np.uint8(ids["lac"])
    index[is_ocean] = np.uint8(ids["ocean"])

    is_water = is_ocean | lake_mask | river_mask

    land_total = int(is_land.sum())
    counts: dict[str, float] = {}
    if land_total:
        labels = bio["labels"]
        for value, share in zip(*np.unique(index[is_land], return_counts=True)):
            counts[labels[str(int(value))]] = round(float(share) / land_total * 100.0, 2)

    return BiomeResult(
        index=index,
        slope_deg=slope,
        is_water=is_water,
        river_mask=river_mask,
        lake_mask=lake_mask,
        counts=dict(sorted(counts.items(), key=lambda kv: -kv[1])),
    )
