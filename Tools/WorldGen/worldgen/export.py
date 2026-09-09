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

from . import noise
from .config import Rules, landscape_config_for, landscape_scale_for


# ------------------------------------------------------------- reechantillonnage


def resample(field: np.ndarray, out_n: int, order: int = 1) -> np.ndarray:
    """Passe un champ de la resolution de simulation a la resolution de sortie."""
    if field.shape[0] == out_n:
        return field
    zoom = out_n / field.shape[0]
    return ndimage.zoom(field, zoom, order=order, mode="nearest", grid_mode=False)


def upsample_heightmap(dem: np.ndarray, out_n: int, rules: Rules) -> np.ndarray:
    """Sur-echantillonne le relief et lui rend du detail haute frequence.

    Une simple interpolation bicubique donnerait un terrain lisse et mou a la
    resolution du Landscape : le detail perdu doit etre remis, sinon la montee en
    resolution n'apporte rien de visible.
    """
    big = resample(dem.astype(np.float32), out_n, order=3).astype(np.float32)
    amp = float(rules.get("world.detailAmplitudeM", 0.0))
    if amp <= 0.0:
        return big

    octaves = int(rules.get("world.detailOctaves", 4))
    base_freq = float(rules.get("world.detailFrequency", 90.0))
    detail = noise.fbm(out_n, base_freq, octaves, rules.seed + 24601)

    # Pas de detail sous l'eau ni sur les pentes deja raides : on ajoute du grain
    # sur les reliefs doux, pas des aiguilles sur les falaises.
    dy, dx = np.gradient(big)
    slope = np.sqrt(dx * dx + dy * dy)
    ref = float(np.percentile(slope, 92.0))
    mask = np.float32(1.0) - noise.smoothstep(0.0, max(ref, 1e-6), slope)
    mask *= noise.smoothstep(-20.0, 40.0, big)
    return (big + detail * np.float32(amp) * mask).astype(np.float32)


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


def rivers_to_json(rivers, dem: np.ndarray, geo, water_drop_m: float = 0.0) -> list[dict]:
    """Rivieres en coordonnees monde Unreal, pretes pour un WaterBodyRiver."""
    out = []
    n = geo.n
    for r in rivers:
        pts = []
        for k, (j, i) in enumerate(r.points_px):
            x, y = pixel_to_world_cm(j, i, geo)
            jj = int(np.clip(round(j), 0, n - 1))
            ii = int(np.clip(round(i), 0, n - 1))
            z = float(dem[jj, ii]) * 100.0 - water_drop_m * 100.0
            pts.append({
                "x": round(x, 1), "y": round(y, 1), "z": round(z, 1),
                "widthCm": round(float(r.width_m[k]) * 100.0, 1),
                "depthCm": round(float(r.depth_m[k]) * 100.0, 1),
                "dischargeM3s": round(float(r.discharge[k]), 4),
            })
        # La surface d'une riviere doit descendre de facon monotone vers l'aval,
        # sinon le WaterBody produit des marches d'escalier remontantes.
        for k in range(1, len(pts)):
            pts[k]["z"] = min(pts[k]["z"], pts[k - 1]["z"])
        out.append({
            "points": pts,
            "strahler": int(r.strahler),
            "lengthM": round(float(r.length_m), 1),
            "mouth": r.mouth,
            "maxWidthM": round(max(r.width_m), 1),
            "maxDischargeM3s": round(max(r.discharge), 4),
        })
    return out


def lakes_to_json(lakes, geo) -> list[dict]:
    out = []
    for lk in lakes:
        cx, cy = pixel_to_world_cm(*lk.centroid_px, geo)
        outline = []
        for (j, i) in lk.outline_px:
            x, y = pixel_to_world_cm(j, i, geo)
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
