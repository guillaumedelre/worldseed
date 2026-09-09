"""Regenere le rapport HTML depuis un dossier de sortie deja calcule.

    python rebuild_report.py [dossier] [destination.html]

Evite de resimuler le monde (plusieurs minutes) pour la seule mise en page du
rapport. Les champs sont relus depuis les PNG exportes et les plages de valeurs
depuis le manifeste.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

from worldgen import report
from worldgen.config import Rules


def rebuild(src: Path, dest: Path, rules_path: Path) -> Path:
    rules = Rules.load(rules_path)
    manifest = json.loads((src / "manifest.json").read_text(encoding="utf-8"))
    stats = manifest["stats"]
    ranges = manifest["climateRanges"]

    height = np.asarray(Image.open(src / "height_16bit.png")).astype(np.float32)
    lo = float(manifest["world"]["minElevationM"])
    hi = float(manifest["world"]["maxElevationM"])
    dem = height / 65535.0 * (hi - lo) + lo

    def gray(name: str, vmin: float, vmax: float) -> np.ndarray:
        raw = np.asarray(Image.open(src / name).convert("L")).astype(np.float32) / 255.0
        return raw * (vmax - vmin) + vmin

    temp = gray("climate_temp.png", ranges["tempMinC"], ranges["tempMaxC"])
    rain = gray("climate_rain.png", ranges["precipMinMm"], ranges["precipMaxMm"])
    biome = np.asarray(Image.open(src / "biome_index.png").convert("L")).astype(np.uint8)

    rivers = json.loads((src / "rivers.json").read_text(encoding="utf-8"))["rivers"]
    lakes = json.loads((src / "lakes.json").read_text(encoding="utf-8"))["lakes"]

    # La geometrie de sortie : les champs relus sont a la resolution du Landscape.
    geo = rules.out_geometry.rescaled(dem.shape[0])
    report.write(dest, rules, geo, dem, temp, rain, biome, rivers, lakes, stats)
    return dest


if __name__ == "__main__":
    here = Path(__file__).resolve().parent
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else here.parent.parent / "Saved" / "WorldGen" / "20260909"
    dest = Path(sys.argv[2]) if len(sys.argv) > 2 else src / "world_report.html"
    print(rebuild(src, dest, here / "rules" / "world_rules.json"))
