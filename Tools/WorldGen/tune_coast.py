"""Banc de reglage du littoral : quelle tectonique donne un vrai arriere-pays ?

    python tune_coast.py

La longueur maximale d'un cours d'eau est bornee par la distance du point le plus
continental a la mer : un fleuve ne peut pas etre plus long que le chemin qu'il a
pour descendre. Sur le monde de reference, ce maximum vaut 4,25 km, ce qui plafonne
les cours d'eau vers 8 km alors que le controle en demande 9,6.

Ce banc n'evalue QUE la tectonique et le recalage du niveau marin -- quelques
secondes par essai au lieu de huit minutes -- parce que l'erosion ne change pas
la forme generale des cotes. Il sert a choisir des parametres, pas a valider :
la validation se fait sur une generation complete.
"""

from __future__ import annotations

import copy
import json
import sys
from pathlib import Path

import numpy as np
from scipy import ndimage

from worldgen import tectonics as tec_mod
from worldgen.config import Rules

_SIM = 1025          # resolution d'evaluation : assez pour la forme des cotes


def evalue(rules: Rules, sim: int = _SIM) -> dict:
    """Mesure la forme du littoral pour un jeu de regles donne."""
    donnees = copy.deepcopy(rules.data)
    donnees["world"]["simResolution"] = sim
    essai = Rules(donnees)
    geo = essai.sim_geometry
    tec = tec_mod.generate(essai, geo)

    # Meme recalage que le pipeline : le niveau marin est fixe par quantile pour
    # que le ratio terres/mers demande soit exact.
    ratio = float(essai.get("tectonics.landRatio"))
    dem = tec.elevation_m - float(np.quantile(tec.elevation_m, 1.0 - ratio))
    terre = dem > 0.0
    mpp = geo.meters_per_pixel

    dist_km = ndimage.distance_transform_edt(terre) * mpp / 1000.0
    lab, nb = ndimage.label(terre, structure=np.ones((3, 3)))
    tailles = ndimage.sum(terre, lab, index=np.arange(1, nb + 1)) if nb else np.array([1.0])
    return {
        "distMaxKm": float(dist_km[terre].max()) if terre.any() else 0.0,
        "distMedKm": float(np.median(dist_km[terre])) if terre.any() else 0.0,
        "partTerres": float(terre.mean() * 100.0),
        "masses": int(nb),
        "partPlusGrande": float(100.0 * tailles.max() / max(tailles.sum(), 1)),
        "fleuveMaxKm": 1.75 * float(dist_km[terre].max()) if terre.any() else 0.0,
    }


def main() -> int:
    here = Path(__file__).resolve().parent
    base = Rules.load(here / "rules" / "world_rules.json")
    cible_km = 0.30 * float(base.get("world.sizeKm"))

    essais = [
        ("reference", {}),
        ("moins de plaques", {"plateCount": 10}),
        ("continents plus larges", {"baseFrequency": 3.5}),
        ("cotes lissees", {"coastNoiseAmount": 0.15, "coastNoiseFrequency": 3.0}),
        ("larges + lissees", {"baseFrequency": 3.5, "coastNoiseAmount": 0.15,
                              "coastNoiseFrequency": 3.0}),
        ("larges + lissees + 10 plaques", {"plateCount": 10, "baseFrequency": 3.5,
                                           "coastNoiseAmount": 0.15,
                                           "coastNoiseFrequency": 3.0}),
        ("tres larges", {"plateCount": 8, "baseFrequency": 2.5,
                         "coastNoiseAmount": 0.10, "coastNoiseFrequency": 2.5,
                         "continentSmoothKm": 3.0}),
        ("tres larges + peu de deformation", {"plateCount": 8, "baseFrequency": 2.5,
                                              "coastNoiseAmount": 0.08,
                                              "coastNoiseFrequency": 2.0,
                                              "continentSmoothKm": 3.5,
                                              "warpStrength": 0.20}),
    ]

    print("cible : un cours d'eau de %.1f km, soit une distance a la mer d'au moins %.1f km"
          % (cible_km, cible_km / 1.75))
    print()
    print("%-34s %9s %9s %8s %7s %9s" % (
        "essai", "dist max", "dist med", "terres", "masses", "fleuve~"))
    for nom, patch in essais:
        donnees = copy.deepcopy(base.data)
        donnees["tectonics"].update(patch)
        r = evalue(Rules(donnees))
        marque = "  <-- atteint" if r["fleuveMaxKm"] >= cible_km else ""
        print("%-34s %7.2f km %7.2f km %6.1f%% %7d %6.1f km%s" % (
            nom, r["distMaxKm"], r["distMedKm"], r["partTerres"], r["masses"],
            r["fleuveMaxKm"], marque))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
