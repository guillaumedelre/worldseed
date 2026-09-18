"""Releve complet d'une generation, et diff entre deux releves.

    python metrics.py <dossier> [--out releve.json]
    python metrics.py --diff reference.json apres.json

Sert de harnais de non-regression : avant chaque correction on fige un releve,
apres correction on regenere et on compare. Le diff montre ce qui a bouge, y
compris la ou on ne touchait pas.

Rien ici ne decide de ce qui est bon ou mauvais : ce module MESURE. Les seuils
attendus vivent dans les controles du rapport (pipeline._checks), pas ici.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

Image.MAX_IMAGE_PIXELS = None

_SOUS_ECH = 4          # sous-echantillonnage des cartes : le releve doit rester rapide
_EAU_IDS = (0,)        # ocean (les identifiants lac et riviere ne sont plus attribues)


def _charge(src: Path, nom: str, gris: bool = True) -> np.ndarray:
    im = Image.open(src / nom)
    if gris:
        im = im.convert("L")
    return np.asarray(im)[::_SOUS_ECH, ::_SOUS_ECH]


def releve(src: Path) -> dict:
    m = json.loads((src / "manifest.json").read_text(encoding="utf-8"))
    stats = m.get("stats", {})
    labels = m["biomes"]["labels"]
    noms_couches = m["layers"]

    biome = _charge(src, "biome_index.png")
    poids = np.stack([_charge(src, f) for f in m["layerFiles"]])
    dominante = poids.argmax(axis=0)
    eau = np.isin(biome, _EAU_IDS)
    terre = ~eau
    n_terre = int(terre.sum()) or 1

    biomes = {}
    for b in range(len(labels)):
        sel = biome == b
        if not sel.any():
            continue
        biomes[labels[str(b)]] = {
            "partMonde": round(100.0 * float(sel.mean()), 3),
            "partTerres": round(100.0 * float((sel & terre).sum()) / n_terre, 3),
        }

    couches = {}
    for k, nom in enumerate(noms_couches):
        couches[nom] = round(100.0 * float(((dominante == k) & terre).sum()) / n_terre, 3)

    taille_km = float(m["world"]["sizeKm"])

    return {
        "source": str(src),
        "graine": m.get("seed"),
        "monde": {
            "tailleKm": taille_km,
            "resolution": m["world"]["resolution"],
            "partTerresPct": round(100.0 * float(terre.mean()), 3),
            "partEauPct": round(100.0 * float(eau.mean()), 3),
            "altMinM": stats.get("elevMin"),
            "altMaxM": stats.get("elevMax"),
        },
        "biomes": biomes,
        "couchesDominantes": couches,
        "profilZonal": stats.get("zonal", []),
        "controles": stats.get("checks", []),
    }


def _aplatis(d: dict, prefixe: str = "") -> dict:
    plat = {}
    for k, v in d.items():
        cle = "{}.{}".format(prefixe, k) if prefixe else str(k)
        if isinstance(v, dict):
            plat.update(_aplatis(v, cle))
        elif isinstance(v, (int, float)) and not isinstance(v, bool):
            plat[cle] = float(v)
    return plat


def diff(avant: dict, apres: dict, seuil_pct: float = 0.5) -> None:
    a, b = _aplatis(avant), _aplatis(apres)
    cles = sorted(set(a) | set(b))
    print("%-46s %12s %12s %10s" % ("metrique", "avant", "apres", "delta"))
    bouge = 0
    for k in cles:
        va, vb = a.get(k), b.get(k)
        if va is None:
            print("%-46s %12s %12.3f %10s" % (k, "-", vb, "NOUVEAU")); bouge += 1
        elif vb is None:
            print("%-46s %12.3f %12s %10s" % (k, va, "-", "DISPARU")); bouge += 1
        else:
            d = vb - va
            ref = max(abs(va), 1e-9)
            if abs(d) > 1e-9 and 100.0 * abs(d) / ref >= seuil_pct:
                print("%-46s %12.3f %12.3f %+10.3f" % (k, va, vb, d)); bouge += 1
    print()
    print("%d metriques ont bouge (seuil %.1f %% de variation relative)" % (bouge, seuil_pct))


def main(argv: list[str]) -> int:
    if len(argv) >= 3 and argv[0] == "--diff":
        avant = json.loads(Path(argv[1]).read_text(encoding="utf-8"))
        apres = json.loads(Path(argv[2]).read_text(encoding="utf-8"))
        diff(avant, apres)
        return 0
    if not argv:
        print(__doc__)
        return 2
    src = Path(argv[0])
    r = releve(src)
    dest = None
    if "--out" in argv:
        dest = Path(argv[argv.index("--out") + 1])
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(json.dumps(r, indent=2, ensure_ascii=False), encoding="utf-8")
    print("monde     : %.0f km, terres %.1f %%, mer %.1f %%" % (
        r["monde"]["tailleKm"], r["monde"]["partTerresPct"], r["monde"]["partEauPct"]))
    if dest:
        print("releve ecrit : %s" % dest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
