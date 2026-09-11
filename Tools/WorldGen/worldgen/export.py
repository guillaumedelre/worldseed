"""Etape 6b - export vers des fichiers consommables par Unreal.

Convention : les tableaux sont indexes [j, i] avec j = 0 au pole SUD. Les PNG
sont ecrits DANS CET ORDRE, sans symetrie : ligne 0 du fichier = bord Y minimum
du Landscape. Le rapport HTML, lui, retourne les images pour afficher le nord en
haut, comme sur une carte.
"""

from __future__ import annotations

import base64
import io
import json
from dataclasses import asdict
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

from . import hydrology, noise
from .config import Rules, landscape_config_for, landscape_scale_for


# ------------------------------------------------------------- reechantillonnage


def resample(field: np.ndarray, out_n: int, order: int = 1) -> np.ndarray:
    """Passe un champ de la resolution de simulation a la resolution de sortie."""
    if field.shape[0] == out_n:
        return field
    zoom = out_n / field.shape[0]
    return ndimage.zoom(field, zoom, order=order, mode="nearest", grid_mode=False)


def lake_level_field(lakes, lake_mask: np.ndarray, geo, geo_out,
                     portee_m: float = 60.0) -> np.ndarray:
    """Niveau d'eau LOCAL, a la resolution de sortie.

    Zero partout -- le niveau de la mer -- et le niveau propre a chaque lac dans
    sa cuvette puis en fondu sur une bande de berge de `portee_m`. C'est ce champ
    qui dit au detail fractal ou s'effacer : sans lui il ne connait que le zero
    absolu, et un lac perche recoit le bruit a pleine amplitude.

    Le fondu vers zero est indispensable : sans lui, un point situe 200 m d'un
    petit lac perche mais a peine plus haut que lui verrait son detail supprime
    alors qu'il n'a rien d'une berge.
    """
    out_n = geo_out.n
    champ = np.zeros((out_n, out_n), dtype=np.float32)
    if not lakes or lake_mask is None or not lake_mask.any():
        return champ
    basin = resample(lake_mask.astype(np.float32), out_n, order=0) > 0.5
    if not basin.any():
        return champ

    etiq, nb = ndimage.label(basin)
    facteur = (out_n - 1) / (geo.n - 1)
    niveaux = np.zeros(nb + 1, dtype=np.float32)
    for lk in lakes:
        sj = int(np.clip(round(lk.seed_px[0] * facteur), 0, out_n - 1))
        si = int(np.clip(round(lk.seed_px[1] * facteur), 0, out_n - 1))
        e = int(etiq[sj, si])
        if e > 0:
            niveaux[e] = np.float32(lk.surface_m)

    # Distance a la cuvette la plus proche, ET son etiquette : c'est ce couple
    # qui porte le niveau jusque sur la berge.
    dist, idx = ndimage.distance_transform_edt(~basin, return_indices=True)
    proche = niveaux[etiq[idx[0], idx[1]]]
    del idx
    portee_px = max(float(portee_m) / geo_out.meters_per_pixel, 1e-6)
    fondu = np.float32(1.0) - noise.smoothstep(0.0, portee_px, dist.astype(np.float32))
    return (proche * fondu).astype(np.float32)


def river_corridor_damping(river_mask: np.ndarray, geo, geo_out,
                           portee_m: float = 30.0) -> np.ndarray | None:
    """Attenuation du detail fractal le long des cours d'eau.

    Rend 0 dans le couloir de la riviere et 1 au-dela de `portee_m`, en fondu.

    POURQUOI. Le detail fractal est ajoute APRES le calcul de l'hydrologie : il
    depose jusqu'a `world.detailAmplitudeM` de bosses dans le fond des vallees
    que l'ecoulement vient de creuser. Mesure sur la graine 20260909 : sur le
    relief final, un cours d'eau REMONTE de 18,8 m en cumule (mediane), soit un
    quart de sa descente totale -- pour une amplitude de detail de 21,25 m. Or
    une riviere ne remonte pas. La surface d'eau etant forcee a decroitre vers
    l'aval, chaque bosse enterre tout le troncon qui suit : 47 % des noeuds
    passaient sous terre. Effacer le detail dans le couloir rend au fond de
    vallee le profil que l'hydrologie lui avait donne.
    """
    if river_mask is None or not river_mask.any():
        return None
    corridor = resample(river_mask.astype(np.float32), geo_out.n, order=0) > 0.5
    if not corridor.any():
        return None
    dist = ndimage.distance_transform_edt(~corridor).astype(np.float32)
    portee_px = max(float(portee_m) / geo_out.meters_per_pixel, 1e-6)
    return noise.smoothstep(0.0, portee_px, dist).astype(np.float32)


def upsample_heightmap(dem: np.ndarray, out_n: int, rules: Rules,
                       water_level: np.ndarray | None = None,
                       detail_damp: np.ndarray | None = None) -> np.ndarray:
    """Sur-echantillonne le relief et lui rend du detail haute frequence.

    Une simple interpolation bicubique donnerait un terrain lisse et mou a la
    resolution du Landscape : le detail perdu doit etre remis, sinon la montee en
    resolution n'apporte rien de visible.
    """
    big = resample(dem.astype(np.float32), out_n, order=3).astype(np.float32)

    # Le trait de cote doit rester le MEME que celui vu par la classification des
    # biomes. Celle-ci travaille sur le `dem` de simulation, dont le masque
    # terre/mer monte en resolution au plus proche voisin ; le relief, lui, monte
    # en bicubique puis recoit du detail fractal. Les deux divergent donc sur le
    # littoral, et un pixel classe "plage" peut se retrouver sous le niveau zero.
    # Mesure sur la graine 20260909 avant correction : 89 420 pixels de biome
    # TERRESTRE sous l'altitude 0, jusqu'a -12,1 m, dont 64 % de plage - d'ou de
    # l'herbe et des arbres semes sous l'eau dans Unreal.
    terre = resample((dem > 0.0).astype(np.float32), out_n, order=0) > 0.5

    def caler(champ: np.ndarray) -> np.ndarray:
        """Force le signe de l'altitude a suivre le masque terre/mer."""
        return np.where(terre, np.maximum(champ, np.float32(0.01)),
                        np.minimum(champ, np.float32(-0.01))).astype(np.float32)

    amp = float(rules.get("world.detailAmplitudeM", 0.0))
    if amp <= 0.0:
        return caler(big)

    octaves = int(rules.get("world.detailOctaves", 4))
    base_freq = float(rules.get("world.detailFrequency", 90.0))
    detail = noise.fbm(out_n, base_freq, octaves, rules.seed + 24601)

    # Pas de detail sous l'eau ni sur les pentes deja raides : on ajoute du grain
    # sur les reliefs doux, pas des aiguilles sur les falaises.
    dy, dx = np.gradient(big)
    slope = np.sqrt(dx * dx + dy * dy)
    ref = float(np.percentile(slope, 92.0))
    mask = np.float32(1.0) - noise.smoothstep(0.0, max(ref, 1e-6), slope)
    # Le detail ne monte qu'au-dessus du trait de cote, en fondu : sous l'eau il
    # hérisserait les petits fonds, et pile sur la plage il ferait osciller le
    # littoral. Ces deux altitudes sont METRIQUES, donc liees a l'echelle du monde
    # -- seul seuil en dur qui restait dans le code. Sur la maquette au 1/4 (8 km)
    # elles valent -5 et +10 m la ou le monde de 32 km utilisait -20 et +40.
    fade_bas = float(rules.get("world.detailCoastFadeStartM", -20.0))
    fade_haut = float(rules.get("world.detailCoastFadeFullM", 40.0))
    # Le fondu se fait autour du NIVEAU D'EAU LOCAL, pas autour du zero absolu.
    # Sans cela un lac perche a 136 m recoit le detail a PLEINE amplitude : le
    # bruit creuse sa cuvette et herisse ses berges, le trait de cote calcule
    # avant l'export ne tombe plus sur la ligne d'eau, et l'eau se termine en mur
    # vertical. Mesure sur la graine 20260909 : l'isoligne du niveau d'un lac
    # etait brisee en 179 morceaux par ce bruit.
    niveau = np.float32(0.0) if water_level is None else water_level
    mask *= noise.smoothstep(fade_bas, fade_haut, big - niveau)
    if detail_damp is not None:
        mask = mask * detail_damp          # couloir des rivieres

    return caler((big + detail * np.float32(amp) * mask).astype(np.float32))


# --------------------------------------------------------------------- ecriture


def write_heightmap_png(dem_m: np.ndarray, path: Path, min_m: float, max_m: float) -> dict:
    """PNG 16 bits, plage lineaire min_m..max_m -> 0..65535."""
    span = max(max_m - min_m, 1e-6)
    norm = np.clip((dem_m - min_m) / span, 0.0, 1.0)
    data = np.rint(norm * 65535.0).astype(np.uint16)
    Image.fromarray(data, mode="I;16").save(path, optimize=True)
    return {
        "file": path.name,
        "resolution": int(dem_m.shape[0]),
        "minElevationM": float(min_m),
        "maxElevationM": float(max_m),
    }


def write_gray_png(data01: np.ndarray, path: Path) -> None:
    """PNG 8 bits en niveaux de gris, a partir d'un champ deja borne 0..1."""
    arr = np.clip(np.rint(data01 * 255.0), 0, 255).astype(np.uint8)
    Image.fromarray(arr, mode="L").save(path, optimize=True)


def write_index_png(index: np.ndarray, path: Path) -> None:
    Image.fromarray(index.astype(np.uint8), mode="L").save(path, optimize=True)


def write_colour_png(rgb: np.ndarray, path: Path) -> None:
    Image.fromarray(rgb.astype(np.uint8), mode="RGB").save(path, optimize=True)


def biome_rgb(index: np.ndarray, rules: Rules) -> np.ndarray:
    """Rend la carte des biomes en aplats de couleur, pour lecture humaine."""
    palette = np.zeros((256, 3), dtype=np.uint8)
    for key, colour in rules["biomes"]["debugColors"].items():
        palette[int(key)] = colour
    return palette[index]


# ------------------------------------------------------------------ coordonnees


def pixel_to_world_cm(j: float, i: float, geo) -> tuple[float, float]:
    """Pixel (j, i) -> (X, Y) monde en centimetres, origine au centre."""
    half = geo.half_size_m * 100.0
    x = (i / (geo.n - 1)) * (geo.size_m * 100.0) - half
    y = (j / (geo.n - 1)) * (geo.size_m * 100.0) - half
    return x, y


def _altitudes_riviere(points_px, dem, geo, dem_out, geo_out, fenetre: int):
    """Altitude du lit, echantillonnee sur le relief FINAL et lissee.

    POURQUOI PAS LE RELIEF DE SIMULATION. Unreal affiche le relief de SORTIE, qui
    a recu jusqu'a `world.detailAmplitudeM` de detail fractal APRES le calcul de
    l'hydrologie. Un lit cale sur la simulation se retrouve donc tantot enterre,
    tantot suspendu. Mesure sur la graine 20260909 avant correction, sur 1226
    noeuds : 46,4 % enterres de plus d'un metre (jusqu'a -21,7 m, riviere
    invisible) et 22,8 % flottants de plus d'un metre (jusqu'a +8,2 m, soit un
    mur d'eau comme au bord des lacs).

    POURQUOI LISSER. Echantillonne tel quel, le lit herite du bruit fractal et
    ondule verticalement de plusieurs metres d'un noeud a l'autre. Une moyenne
    glissante le long du cours lui rend un profil de riviere. La decroissance
    monotone vers l'aval est imposee ensuite par l'appelant.
    """
    if dem_out is None or geo_out is None:
        n = geo.n
        return np.array([float(dem[int(np.clip(round(j), 0, n - 1)),
                                   int(np.clip(round(i), 0, n - 1))])
                         for j, i in points_px], dtype=np.float64)
    facteur = (geo_out.n - 1) / (geo.n - 1)
    rr = np.array([j * facteur for j, i in points_px], dtype=np.float64)
    cc = np.array([i * facteur for j, i in points_px], dtype=np.float64)
    z = ndimage.map_coordinates(dem_out, np.vstack([rr, cc]), order=1, mode="nearest")
    if fenetre > 1 and z.size >= fenetre:
        noyau = np.ones(fenetre, dtype=np.float64) / fenetre
        # Bords repliques : sans cela la source et l'embouchure s'effondrent vers
        # zero et la riviere plonge sous terre a ses deux extremites.
        pad = fenetre // 2
        z = np.convolve(np.pad(z, pad, mode="edge"), noyau, mode="valid")[: len(rr)]
    return z


def rivers_to_json(rivers, dem: np.ndarray, geo, water_drop_m: float = 0.0,
                   precip_mm: np.ndarray | None = None,
                   geo_out=None, dem_out=None, hyd: dict | None = None) -> list[dict]:
    """Rivieres en coordonnees monde Unreal, pretes pour un WaterBodyRiver."""
    out = []
    n = geo.n
    fenetre = int((hyd or {}).get("riverSmoothPoints", 5))
    for r in rivers:
        pts = []
        z_lit = _altitudes_riviere(r.points_px, dem, geo, dem_out, geo_out, fenetre)
        for k, (j, i) in enumerate(r.points_px):
            x, y = pixel_to_world_cm(j, i, geo)
            z = float(z_lit[k]) * 100.0 - water_drop_m * 100.0
            pts.append({
                "x": round(x, 1), "y": round(y, 1), "z": round(z, 1),
                "widthCm": round(float(r.width_m[k]) * 100.0, 1),
                "depthCm": round(float(r.depth_m[k]) * 100.0, 1),
                "dischargeM3s": round(float(r.discharge[k]), 4),
            })
        # PROFIL DE LA SURFACE D'EAU : elle descend, mais elle ne s'enterre pas.
        #
        # La decroissance stricte -- la regle d'avant -- est trop raide : des
        # qu'un noeud tombe dans un creux, tout l'aval y reste accroche. Mesure
        # sur la graine 20260909 : 35,3 % des noeuds sous terre, jusqu'a 19 m,
        # donc un tiers du reseau invisible. La cause n'est pas un defaut de
        # routage : le terrain REMONTE reellement le long du cours a 13,5 % des
        # pas, parce que la riviere traverse de petites cuvettes comblees dont
        # elle ressort par un seuil.
        #
        # Deux bornes, donc : la surface ne remonte jamais de plus de
        # `riverMaxRisePerPointM` d'un noeud au suivant, et ne passe jamais plus
        # de `riverMaxSinkM` sous le sol. Mesure avec 0,5 m et 1,0 m : plus aucun
        # noeud suspendu, plus aucun enterre au-dela du metre.
        montee = float((hyd or {}).get("riverMaxRisePerPointM", 0.5)) * 100.0
        enfonce = float((hyd or {}).get("riverMaxSinkM", 1.0)) * 100.0
        for k in range(1, len(pts)):
            sol = float(z_lit[k]) * 100.0 - water_drop_m * 100.0
            z = min(sol, pts[k - 1]["z"] + montee)
            pts[k]["z"] = round(max(z, sol - enfonce), 1)
        # Pluie a l'embouchure : sert au controle des bassins endoreiques, qui ne
        # sont legitimes qu'en zone aride.
        jm, im = r.points_px[-1]
        jm = int(np.clip(round(jm), 0, n - 1))
        im = int(np.clip(round(im), 0, n - 1))
        out.append({
            "points": pts,
            "mouthPrecipMm": (round(float(precip_mm[jm, im]), 1)
                              if precip_mm is not None else None),
            "strahler": int(r.strahler),
            "lengthM": round(float(r.length_m), 1),
            "mouth": r.mouth,
            "maxWidthM": round(max(r.width_m), 1),
            "maxDischargeM3s": round(max(r.discharge), 4),
        })
    return out


def lakes_to_json(lakes, geo, geo_out=None, dem_out=None,
                  lake_mask=None, hyd=None) -> list[dict]:
    """Les lacs en JSON, avec leur TRAIT DE COTE.

    Quand `dem_out` est fourni, le contour est retrace sur le relief FINAL (celui
    qu'Unreal affiche, detail fractal compris) et accroche a la berge, par
    `hydrology.lake_shoreline`. Sans lui, on retombe sur le contour de
    simulation, qui ne connait pas le detail ajoute a l'export : le trait d'eau
    ne tombe alors plus sur la ligne de rivage et l'eau se termine en MUR
    vertical au-dessus du sol.
    """
    refait = dem_out is not None and lake_mask is not None and geo_out is not None
    if refait:
        out_n = geo_out.n
        # Le masque de cuvette monte a la resolution de sortie AU PLUS PROCHE
        # VOISIN : c'est un masque, pas une image, il ne s'interpole pas.
        basin = resample(lake_mask.astype(np.float32), out_n, order=0) > 0.5
        etiq, _ = ndimage.label(basin)
        facteur = (out_n - 1) / (geo.n - 1)

    out = []
    for lk in lakes:
        cx, cy = pixel_to_world_cm(*lk.centroid_px, geo)
        points_px, geo_px = lk.outline_px, geo
        if refait:
            sj = int(round(lk.seed_px[0] * facteur))
            si = int(round(lk.seed_px[1] * facteur))
            sj = int(np.clip(sj, 0, out_n - 1)); si = int(np.clip(si, 0, out_n - 1))
            e = int(etiq[sj, si])
            if e > 0:
                trace = hydrology.lake_shoreline(
                    dem_out, etiq == e, (sj, si), lk.surface_m, hyd or {})
                if len(trace) >= 4:
                    points_px, geo_px = trace, geo_out
        outline = []
        for (j, i) in points_px:
            x, y = pixel_to_world_cm(j, i, geo_px)
            outline.append({"x": round(x, 1), "y": round(y, 1)})
        out.append({
            "centre": {"x": round(cx, 1), "y": round(cy, 1),
                       "z": round(lk.surface_m * 100.0, 1)},
            "surfaceM": round(lk.surface_m, 2),
            "areaHa": round(lk.area_ha, 2),
            "cells": int(lk.cells),
            "outline": outline,
        })
    return out


# -------------------------------------------------------------------- manifeste


def build_manifest(rules: Rules, geo_out, stats: dict) -> dict:
    w = rules["world"]
    cfg = landscape_config_for(int(w["resolution"]))
    scale = landscape_scale_for(rules)
    mid_m = (float(w["minElevationM"]) + float(w["maxElevationM"])) * 0.5

    return {
        "worldseedVersion": 1,
        "seed": rules.seed,
        "world": {
            "sizeKm": float(w["sizeKm"]),
            "resolution": int(w["resolution"]),
            "simResolution": int(w["simResolution"]),
            "minElevationM": float(w["minElevationM"]),
            "maxElevationM": float(w["maxElevationM"]),
            "metresPerPixel": geo_out.meters_per_pixel,
        },
        "landscape": {
            **cfg,
            "scale": scale,
            # Le milieu de la plage d'altitude tombe sur la valeur 32768 du PNG,
            # c'est-a-dire sur Z = 0 DANS le repere de l'acteur. Ce milieu vaut
            # mid_m metres d'altitude reelle : pour que l'altitude 0 (le niveau
            # de la mer) tombe sur Z = 0 dans le monde, l'acteur doit donc etre
            # REMONTE de mid_m, pas descendu. Verifie dans l'editeur : avec le
            # signe inverse, la mer se retrouvait a Z = -400 m.
            "actorLocation": {"x": -geo_out.half_size_m * 100.0,
                              "y": -geo_out.half_size_m * 100.0,
                              "z": mid_m * 100.0},
            "worldPartitionGridSize": int(rules.get("export.worldPartitionGridSize", 8)),
        },
        "latitude": {
            "degreesPerMetre": (float(w["latitudeSpanDeg"]) * 0.5) / (geo_out.half_size_m),
            "tropicDeg": float(w["tropicDeg"]),
            "polarCircleDeg": float(w["polarCircleDeg"]),
            "landmarksY_cm": {k: round(v * 100.0, 1) for k, v in geo_out.landmarks().items()},
        },
        "layers": rules["surfaces"]["layerAssetNames"],
        "layerFiles": [f"layer_{name}.png" for name in rules["surfaces"]["layers"]],
        "biomes": {
            "labels": rules["biomes"]["labels"],
            "ids": rules["biomes"]["ids"],
        },
        "climateRanges": stats.get("climateRanges", {}),
        "stats": stats,
    }


# ---------------------------------------------------------------------- rapport


def _to_png_b64(arr: np.ndarray, mode: str = "L") -> str:
    buf = io.BytesIO()
    Image.fromarray(arr, mode=mode).save(buf, format="PNG", optimize=True)
    return base64.b64encode(buf.getvalue()).decode("ascii")


def _shrink(field: np.ndarray, max_px: int, order: int = 1) -> np.ndarray:
    if field.shape[0] <= max_px:
        return field
    return ndimage.zoom(field, max_px / field.shape[0], order=order, mode="nearest")


def hillshade(dem: np.ndarray, spacing_m: float, azimuth=315.0, altitude=42.0) -> np.ndarray:
    dy, dx = np.gradient(dem.astype(np.float32), np.float32(spacing_m))
    slope = np.arctan(np.sqrt(dx * dx + dy * dy))
    aspect = np.arctan2(-dx, dy)
    az = np.radians(360.0 - azimuth + 90.0)
    alt = np.radians(altitude)
    shade = np.sin(alt) * np.cos(slope) + np.cos(alt) * np.sin(slope) * np.cos(az - aspect)
    return np.clip(shade, 0.0, 1.0).astype(np.float32)


def _ramp(field: np.ndarray, lo: float, hi: float, stops: list[tuple[float, tuple[int, int, int]]]) -> np.ndarray:
    """Applique une rampe de couleur a un champ scalaire."""
    t = np.clip((field - lo) / max(hi - lo, 1e-6), 0.0, 1.0)
    positions = np.array([s[0] for s in stops], dtype=np.float32)
    colours = np.array([s[1] for s in stops], dtype=np.float32)
    out = np.zeros(field.shape + (3,), dtype=np.float32)
    for c in range(3):
        out[..., c] = np.interp(t, positions, colours[:, c])
    return out.astype(np.uint8)


_TEMP_STOPS = [(0.0, (40, 60, 140)), (0.35, (90, 170, 210)), (0.5, (235, 235, 200)),
               (0.7, (235, 165, 80)), (1.0, (170, 40, 40))]
_RAIN_STOPS = [(0.0, (200, 170, 110)), (0.25, (225, 220, 150)), (0.5, (130, 190, 130)),
               (0.75, (50, 140, 165)), (1.0, (25, 55, 130))]
