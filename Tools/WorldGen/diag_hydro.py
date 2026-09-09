"""Banc de diagnostic de l'hydrologie, sans resimuler le monde.

    python diag_hydro.py [dossier]

Recharge le relief et la pluie depuis les PNG exportes, les ramene a la
resolution de simulation, puis rejoue compute_flow / extract_rivers /
extract_lakes avec les regles courantes. Environ 30 s au lieu de 7 minutes, ce
qui permet d'iterer sur un diagnostic avant d'engager une regeneration complete.

Les conclusions tirees ici doivent etre reverifiees sur une vraie generation :
la pluie relue est quantifiee sur 8 bits et le relief a fait un aller-retour de
reechantillonnage.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

from worldgen import hydrology as hyd_mod
from worldgen.config import Rules

Image.MAX_IMAGE_PIXELS = None


def charge(src: Path, rules: Rules):
    """Relief (m) et pluie (mm/an) a la resolution de SIMULATION."""
    m = json.loads((src / "manifest.json").read_text(encoding="utf-8"))
    n_sim = int(m["world"]["simResolution"])
    lo, hi = float(m["world"]["minElevationM"]), float(m["world"]["maxElevationM"])

    h16 = np.asarray(Image.open(src / "height_16bit.png")).astype(np.float32)
    dem_out = h16 / 65535.0 * (hi - lo) + lo
    facteur = n_sim / dem_out.shape[0]
    dem = ndimage.zoom(dem_out, facteur, order=1).astype(np.float32)

    ranges = m["climateRanges"]
    rain8 = np.asarray(Image.open(src / "climate_rain.png").convert("L")).astype(np.float32) / 255.0
    rain_out = rain8 * (ranges["precipMinMm"] + (ranges["precipMaxMm"] - ranges["precipMinMm"]))
    rain = ndimage.zoom(rain_out, facteur, order=1).astype(np.float32)
    return m, dem[:n_sim, :n_sim], rain[:n_sim, :n_sim]


def terminus(receivers: np.ndarray, order: np.ndarray) -> np.ndarray:
    """Cellule d'arrivee de chaque cellule en suivant l'ecoulement jusqu'au bout."""
    term = np.arange(receivers.size, dtype=np.int32)
    rec = receivers.tolist()
    t = term.tolist()
    # order est trie par altitude DECROISSANTE ; en le parcourant a l'envers, le
    # recepteur d'une cellule est toujours deja resolu.
    for cell in reversed(order.tolist()):
        r = rec[cell]
        if r != cell:
            t[cell] = t[r]
    return np.asarray(t, dtype=np.int32)


def main(argv: list[str]) -> int:
    here = Path(__file__).resolve().parent
    src = Path(argv[0]) if argv else here.parent.parent / "Saved" / "WorldGen" / "20260909"
    rules = Rules.load(here / "rules" / "world_rules.json")
    geo = rules.sim_geometry
    m, dem, rain = charge(src, rules)
    n = dem.shape[0]
    print("relief %dx%d, %.1f..%.1f m | pluie %.0f..%.0f mm" % (
        n, n, dem.min(), dem.max(), rain.min(), rain.max()))

    hyd = rules["hydrology"]
    poids = hyd_mod.discharge_weights(rain, geo, float(hyd["runoffCoefficient"]))
    flow = hyd_mod.compute_flow(dem, poids, 0.0)
    terre = dem > 0.0
    mer = ~terre

    # --- 1. Les cuvettes comblees -------------------------------------------
    comble = (flow.lake_depth_m > 0.05) & terre
    lab, nb = ndimage.label(comble)
    mpp = geo.meters_per_pixel
    cell_ha = (mpp * mpp) / 10000.0
    tailles = ndimage.sum(comble, lab, index=np.arange(1, nb + 1)) if nb else np.array([])
    profs = ndimage.maximum(flow.lake_depth_m, lab, index=np.arange(1, nb + 1)) if nb else np.array([])
    aires = tailles * cell_ha
    print()
    print("--- cuvettes comblees ---")
    print("  nombre                    : %d" % nb)
    print("  surface totale            : %.0f ha (%.1f %% des terres)" % (
        aires.sum(), 100.0 * comble.sum() / max(terre.sum(), 1)))
    if nb:
        for seuil in (0.1, 0.5, 1.0, 2.0, 5.0, 10.0):
            n_gard = int((profs >= seuil).sum())
            a_gard = float(aires[profs >= seuil].sum())
            print("  profondeur >= %5.1f m      : %5d cuvettes, %7.0f ha (%.1f %% des terres)" % (
                seuil, n_gard, a_gard, 100.0 * a_gard / max(terre.sum() * cell_ha, 1e-9)))
        print("  profondeur : mediane %.2f m, 90e centile %.2f m, max %.1f m" % (
            float(np.median(profs)), float(np.percentile(profs, 90)), float(profs.max())))
        print("  aire       : mediane %.1f ha, max %.0f ha" % (
            float(np.median(aires)), float(aires.max())))

    # --- 2. Ou l'ecoulement aboutit-il ? ------------------------------------
    term = terminus(flow.receivers, flow.order)
    t_terre = term[terre.ravel()]
    arrive_mer = mer.ravel()[t_terre]
    tj, ti = np.unravel_index(t_terre, (n, n))
    sur_bord = (tj == 0) | (tj == n - 1) | (ti == 0) | (ti == n - 1)
    puits = (~arrive_mer) & (~sur_bord)
    print()
    print("--- destination de l'ecoulement, depuis chaque cellule de terre ---")
    print("  atteint la mer            : %.2f %%" % (100.0 * arrive_mer.mean()))
    print("  sort par le bord          : %.2f %%" % (100.0 * sur_bord.mean()))
    print("  meurt dans un puits       : %.2f %%" % (100.0 * puits.mean()))

    # --- 3. Le reseau de chenaux est-il continu ? ---------------------------
    seuil = float(hyd["riverDischargeThreshold"])
    chenal = (flow.accumulation >= seuil) & terre
    lab_c, nb_c = ndimage.label(chenal, structure=np.ones((3, 3)))
    tail_c = ndimage.sum(chenal, lab_c, index=np.arange(1, nb_c + 1)) if nb_c else np.array([])
    print()
    print("--- reseau de chenaux (accumulation >= %.4f m3/s) ---" % seuil)
    print("  cellules de chenal        : %d (%.2f %% des terres)" % (
        int(chenal.sum()), 100.0 * chenal.sum() / max(terre.sum(), 1)))
    print("  composantes connexes      : %d" % nb_c)
    if nb_c:
        print("  plus grande composante    : %d cellules (%.1f %% du reseau)" % (
            int(tail_c.max()), 100.0 * tail_c.max() / max(chenal.sum(), 1)))
        touche_mer = 0
        for l in np.argsort(-tail_c)[:20] + 1:
            cells = np.flatnonzero((lab_c == l).ravel())
            if mer.ravel()[term[cells]].any():
                touche_mer += 1
        print("  parmi les 20 plus grandes : %d atteignent la mer" % touche_mer)

    # --- 4. Ce que l'extraction en tire -------------------------------------
    rivieres = hyd_mod.extract_rivers(flow, dem, geo, hyd)
    lacs = hyd_mod.extract_lakes(flow, dem, geo, hyd)
    bouches = {}
    for r in rivieres:
        bouches[r.mouth] = bouches.get(r.mouth, 0) + 1
    print()
    print("--- extraction ---")
    print("  rivieres                  : %d, embouchures %s" % (len(rivieres), bouches))
    if rivieres:
        lg = [r.length_m for r in rivieres]
        print("  longueurs                 : max %.0f m (%.1f %% du monde), mediane %.0f m" % (
            max(lg), 100.0 * max(lg) / (float(m["world"]["sizeKm"]) * 1000.0), float(np.median(lg))))
    print("  lacs exportes             : %d (plafond maxLakeActors = %s)" % (
        len(lacs), hyd["maxLakeActors"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
