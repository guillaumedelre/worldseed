"""Assemble les cartes d'une generation en une seule image, pour inspection rapide.

    python montage.py [dossier_de_sortie] [image_destination]

Utile pour regarder le monde d'un coup d'oeil sans ouvrir le rapport HTML.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from worldgen import export as ex
from worldgen.config import Rules

_TILE = 470
_PAD = 8
_TOP = 22

_LAT_MARKS = (
    (90.0, (143, 184, 255)), (66.56, (143, 184, 255)), (23.44, (255, 207, 107)),
    (0.0, (255, 138, 92)), (-23.44, (255, 207, 107)), (-66.56, (143, 184, 255)),
    (-90.0, (143, 184, 255)),
)

_LAND_STOPS = [
    (0.00, (20, 52, 104)), (0.30, (36, 88, 142)), (0.415, (74, 140, 186)),
    (0.435, (214, 202, 164)), (0.50, (108, 146, 88)), (0.66, (146, 140, 92)),
    (0.82, (132, 124, 116)), (1.00, (246, 248, 252)),
]


def _load(src: Path, name: str) -> Image.Image:
    im = Image.open(src / name).convert("RGB")
    return im.resize((_TILE, _TILE), Image.LANCZOS).transpose(Image.FLIP_TOP_BOTTOM)


def _ramped(src: Path, name: str, lo: float, hi: float, stops) -> Image.Image:
    """Applique la meme rampe de couleur que le rapport HTML.

    Le PNG brut est un niveau de gris lineaire ou l'equateur sature en blanc :
    il donne une fausse impression de bandes tranchees.
    """
    raw = np.asarray(Image.open(src / name).convert("L")).astype(np.float32) / 255.0
    field = raw * (hi - lo) + lo
    small = ex._shrink(field, _TILE, order=1)
    im = Image.fromarray(ex._ramp(small, lo, hi, stops))
    return im.resize((_TILE, _TILE), Image.LANCZOS).transpose(Image.FLIP_TOP_BOTTOM)


def _zoom(dem: np.ndarray, geo, km: float = 3.0) -> Image.Image:
    """Extrait a l'echelle 1:1, centre sur la zone la plus accidentee.

    Indispensable : une vignette de 470 px sur un monde de 32 km ne montre rien
    en dessous de 68 m, donc tout terrain y parait lisse.
    """
    n = dem.shape[0]
    size = max(64, int(km * 1000.0 / (geo.size_m / (n - 1))))
    size = min(size, n)
    # cherche la fenetre au relief le plus contraste
    step = max(1, n // 24)
    best, best_score = (n // 2 - size // 2, n // 2 - size // 2), -1.0
    for j in range(0, n - size, step):
        for i in range(0, n - size, step):
            win = dem[j:j + size:8, i:i + size:8]
            if (win > 0).mean() < 0.85:
                continue
            score = float(win.std())
            if score > best_score:
                best_score, best = score, (j, i)
    j, i = best
    crop = dem[j:j + size, i:i + size]
    shade = ex.hillshade(crop, geo.size_m / (n - 1))
    rgb = ex._ramp(crop, float(crop.min()), float(crop.max()), _LAND_STOPS[3:]).astype(np.float32)
    rgb *= (0.35 + 0.65 * shade)[..., None]
    im = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))
    return im.resize((_TILE, _TILE), Image.NEAREST).transpose(Image.FLIP_TOP_BOTTOM)


def build(src: Path, dest: Path, rules_path: Path) -> Path:
    rules = Rules.load(rules_path)
    geo = rules.out_geometry

    height = np.asarray(Image.open(src / "height_16bit.png")).astype(np.float32)
    lo = float(rules.get("world.minElevationM"))
    hi = float(rules.get("world.maxElevationM"))
    dem = height / 65535.0 * (hi - lo) + lo

    small = ex._shrink(dem, _TILE, order=1)
    shade = ex.hillshade(small, geo.size_m / (small.shape[0] - 1))
    relief = ex._ramp(small, lo, hi, _LAND_STOPS).astype(np.float32)
    relief *= (0.45 + 0.55 * shade)[..., None]
    relief_im = Image.fromarray(np.clip(relief, 0, 255).astype(np.uint8))
    relief_im = relief_im.resize((_TILE, _TILE), Image.LANCZOS).transpose(Image.FLIP_TOP_BOTTOM)

    grey = np.clip(shade * 205.0 + 22.0, 0, 255).astype(np.uint8)
    hydro = Image.fromarray(np.stack([grey] * 3, axis=-1))
    hydro = hydro.resize((_TILE, _TILE), Image.LANCZOS).transpose(Image.FLIP_TOP_BOTTOM)

    draw = ImageDraw.Draw(hydro)
    half = geo.half_size_m * 100.0

    def to_px(x: float, y: float) -> tuple[float, float]:
        return ((x + half) / (2.0 * half) * _TILE,
                (1.0 - (y + half) / (2.0 * half)) * _TILE)

    ranges = json.loads((src / "manifest.json").read_text(encoding="utf-8"))["climateRanges"]
    panels = [
        ("RELIEF", relief_im),
        ("BIOMES", _load(src, "biome_debug_rgb.png")),
        ("TEMPERATURE", _ramped(src, "climate_temp.png", ranges["tempMinC"],
                                ranges["tempMaxC"], ex._TEMP_STOPS)),
        ("PLUIE", _ramped(src, "climate_rain.png", 0.0, ranges["precipMaxMm"],
                          ex._RAIN_STOPS)),
        ("HYDROLOGIE", hydro),
        ("ZOOM 3 km (1:1)", _zoom(dem, geo)),
    ]

    width = len(panels) * (_TILE + _PAD) + _PAD
    out = Image.new("RGB", (width, _TILE + _TOP + _PAD * 2), (16, 19, 24))
    pen = ImageDraw.Draw(out)
    span = float(rules.get("world.latitudeSpanDeg"))
    for k, (name, image) in enumerate(panels):
        x = _PAD + k * (_TILE + _PAD)
        out.paste(image, (x, _TOP))
        pen.text((x + 4, 5), name, fill=(232, 237, 243))
        for lat, colour in _LAT_MARKS:
            y = _TOP + (0.5 - lat / span) * _TILE
            if _TOP + 1 < y < _TOP + _TILE - 1:
                pen.line([(x, y), (x + _TILE, y)], fill=colour, width=1)

    dest.parent.mkdir(parents=True, exist_ok=True)
    out.save(dest)
    return dest


if __name__ == "__main__":
    here = Path(__file__).resolve().parent
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else here.parent.parent / "Saved" / "WorldGen" / "20260909"
    dest = Path(sys.argv[2]) if len(sys.argv) > 2 else src / "montage.png"
    print(build(src, dest, here / "rules" / "world_rules.json"))
