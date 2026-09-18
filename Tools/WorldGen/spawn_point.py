"""Choisit le point d'apparition du joueur, sur des criteres MESURES.

    python spawn_point.py [dossier] [biome]

Ecrit `spawn_point.json` a cote des autres sorties. C'est `Tools/UE/rebuild_world.py`
qui le lit et pose le `PlayerStart` ; ce fichier-ci fait le calcul.

POURQUOI LE CALCUL EST ICI ET PAS DANS L'EDITEUR. Le Python embarque d'Unreal n'a
ni numpy, ni PIL, ni scipy : toute analyse d'image doit se faire du cote du
generateur, qui a son venv. Le cote editeur ne lit qu'un JSON.

POURQUOI CE N'EST PAS CHOISI A L'OEIL. Sans PlayerStart, le PIE fait apparaitre le
pion a l'origine du monde -- en pleine mer sur ce monde -- et il tombe. Et un
point choisi a vue peut tomber au fond d'un lac : ils sont profonds et rien ne les
signale depuis la berge (deja paye : premiere apparition 11 m sous la surface).

Criteres, tous exprimes en FRACTION du monde pour rester valables a toute echelle :
  - le biome demande ;
  - altitude entre 5 % et 25 % de l'altitude maximale ;
  - pente sous 8 degres, pour ne pas apparaitre sur une paroi ;
  - entre 75 et 300 pixels de toute eau : assez loin pour ne pas tomber dedans,
    assez pres pour que la cote soit en vue ;
  - parmi les candidats, le plus CENTRAL, pour rester loin du bord du monde.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

Image.MAX_IMAGE_PIXELS = None

BIOME_PAR_DEFAUT = "foret_temperee_humide"
HAUTEUR_AU_DESSUS_DU_SOL_CM = 200.0


def choisir(src: Path, biome: str = BIOME_PAR_DEFAUT) -> dict:
    manifest = json.loads((src / "manifest.json").read_text(encoding="utf-8"))
    ids = manifest["biomes"]["ids"]
    labels = manifest["biomes"]["labels"]
    if biome not in ids:
        raise SystemExit("biome inconnu : {}".format(biome))

    bio = np.asarray(Image.open(src / "biome_index.png"))
    h = np.asarray(Image.open(src / "height_16bit.png")).astype(np.float32)
    mn = float(manifest["world"]["minElevationM"])
    mx = float(manifest["world"]["maxElevationM"])
    alt = h / 65535.0 * (mx - mn) + mn
    mpp = float(manifest["world"]["metresPerPixel"])

    # Seul l'ocean reste : les identifiants lac et riviere ne sont plus
    # attribues depuis le retrait de l'hydrologie, le 18 septembre 2026.
    eau = bio == ids["ocean"]
    dist_m = ndimage.distance_transform_edt(~eau).astype(np.float32) * mpp
    gy, gx = np.gradient(alt, mpp)
    pente = np.degrees(np.arctan(np.hypot(gx, gy)))

    ok = ((bio == ids[biome]) & (alt > mx * 0.05) & (alt < mx * 0.25)
          & (pente < 8.0) & (dist_m > mpp * 75) & (dist_m < mpp * 300))
    critere = "strict"
    if not ok.any():
        ok = (bio == ids[biome]) & (alt > 0) & (pente < 12.0) & (dist_m > mpp * 40)
        critere = "elargi"
    if not ok.any():
        raise SystemExit("aucun point d'apparition possible en '{}'".format(biome))

    rr, cc = np.nonzero(ok)
    n = bio.shape[0]
    k = int(np.argmin((rr - n / 2.0) ** 2 + (cc - n / 2.0) ** 2))
    r, c = int(rr[k]), int(cc[k])

    ls = manifest["landscape"]
    cm = float(ls["scale"]["x"])
    point = {
        "biome": biome,
        "biomeLabel": labels[str(ids[biome])],
        "critere": critere,
        "candidats": int(ok.sum()),
        "pixel": {"ligne": r, "colonne": c},
        "altitudeM": round(float(alt[r, c]), 2),
        "penteDeg": round(float(pente[r, c]), 2),
        "distanceEauM": round(float(dist_m[r, c]), 1),
        # Convention verifiee : la COLONNE donne le X monde, la LIGNE donne le Y.
        "monde_cm": {
            "x": round(float(ls["actorLocation"]["x"]) + c * cm, 1),
            "y": round(float(ls["actorLocation"]["y"]) + r * cm, 1),
            "z": round(float(alt[r, c]) * 100.0 + HAUTEUR_AU_DESSUS_DU_SOL_CM, 1),
        },
        "hauteurAuDessusDuSolCm": HAUTEUR_AU_DESSUS_DU_SOL_CM,
    }
    (src / "spawn_point.json").write_text(
        json.dumps(point, indent=2, ensure_ascii=False), encoding="utf-8")
    return point


if __name__ == "__main__":
    here = Path(__file__).resolve().parent
    sys.path.insert(0, str(here))
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    src = Path(args[0]) if args else here.parent.parent / "Saved" / "WorldGen" / "20260909"
    biome = args[1] if len(args) > 1 else BIOME_PAR_DEFAUT
    p = choisir(src, biome)
    print("CREATED: {}".format(src / "spawn_point.json"))
    print("  {} ({} candidats, critere {})".format(p["biomeLabel"], p["candidats"], p["critere"]))
    print("  altitude {} m, pente {} deg, eau a {} m".format(
        p["altitudeM"], p["penteDeg"], p["distanceEauM"]))
    print("  monde X={x} Y={y} Z={z} cm".format(**p["monde_cm"]))
