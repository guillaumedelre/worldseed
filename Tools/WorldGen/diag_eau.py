"""Banc de diagnostic : l'eau colle-t-elle au relief que voit Unreal ?

    python diag_eau.py [dossier]

CE QU'IL MESURE, ET POURQUOI CA N'EST PAS VISIBLE AUTREMENT. Les lacs et les
rivieres sont calcules a la resolution de SIMULATION, alors qu'Unreal affiche le
relief de SORTIE, qui a recu du detail fractal APRES. Rien dans les quinze
controles du rapport ne regarde cet ajustement : un lac dont le polygone
s'arrete avant la berge passe tous les controles, et se voit pourtant en jeu
comme un MUR D'EAU vertical de plusieurs metres.

Deux mesures, toutes deux sur le relief FINAL (`height_16bit.png`) :

  LACS     -- le long du polygone d'eau, de combien la surface surplombe-t-elle
              le sol ? C'est exactement la hauteur du mur que voit le joueur
              depuis la berge. On echantillonne les ARETES, pas seulement les
              sommets : c'est entre deux sommets qu'un polygone trop grossier
              coupe a travers le lac.

  RIVIERES -- chaque noeud est-il pose sur le sol, enterre dessous (riviere
              invisible) ou suspendu au-dessus (mur, la encore) ?

Un deversoir laisse toujours un petit residu : la ou un lac se jette dans une
riviere, le sol descend et une chute est physiquement normale. Ce qui doit
alerter, c'est un pourcentage a deux chiffres ou un mur de plus de 10 m ailleurs.

Mesures de reference (graine 20260909, monde de 8 km), avant et apres la
correction du 11 septembre 2026 :

  lacs     : 42 a 69 % du perimetre surplombant le sol de plus d'1 m,
             mur maximal 26 m   ->   1,5 a 5,8 %, mur maximal 4 a 7 m
  rivieres : 46,4 % enterrees, 22,8 % suspendues   ->   voir la sortie
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

Image.MAX_IMAGE_PIXELS = None

SEUIL_MUR_M = 1.0
PAS_ECHANTILLON_CELL = 1.0


class Monde:
    """Le relief final et la conversion monde <-> pixel."""

    def __init__(self, racine: Path):
        self.racine = racine
        self.manifest = json.loads((racine / "manifest.json").read_text(encoding="utf-8"))
        h = np.asarray(Image.open(racine / "height_16bit.png")).astype(np.float32)
        w = self.manifest["world"]
        self.alt = h / 65535.0 * (w["maxElevationM"] - w["minElevationM"]) + w["minElevationM"]
        self.n = self.alt.shape[0]
        self.taille_cm = float(w["sizeKm"]) * 100000.0
        self.demi_cm = self.taille_cm * 0.5
        self.mpp = float(w["metresPerPixel"])

    def pixel(self, x_cm: float, y_cm: float) -> tuple[float, float]:
        """Convention verifiee : la COLONNE porte le X, la LIGNE porte le Y."""
        return ((y_cm + self.demi_cm) / self.taille_cm * (self.n - 1),
                (x_cm + self.demi_cm) / self.taille_cm * (self.n - 1))

    def altitude(self, lignes, colonnes) -> np.ndarray:
        return ndimage.map_coordinates(
            self.alt, np.vstack([np.atleast_1d(lignes), np.atleast_1d(colonnes)]),
            order=1, mode="nearest")


def _densifie(poly: np.ndarray, pas: float = PAS_ECHANTILLON_CELL) -> np.ndarray:
    """Points le long des ARETES, pas seulement aux sommets."""
    morceaux = []
    for a, b in zip(poly, np.vstack([poly[1:], poly[:1]])):
        longueur = float(np.hypot(*(b - a)))
        nb = max(2, int(longueur / pas))
        morceaux.append(a + (b - a) * np.linspace(0.0, 1.0, nb)[:, None])
    return np.vstack(morceaux)


def lacs(monde: Monde) -> list[dict]:
    chemin = monde.racine / "lakes.json"
    if not chemin.is_file():
        print("lakes.json absent")
        return []
    donnees = json.loads(chemin.read_text(encoding="utf-8"))["lakes"]
    print("LACS -- hauteur du mur d'eau le long du polygone")
    print("  {:<4} {:>8} {:>8} {:>7} {:>9} {:>8} {:>8} {:>8}".format(
        "lac", "surf m", "aire ha", "somm", "> 1 m", "> 3 m", "pire", "p95"))
    releve = []
    for i, lac in enumerate(donnees):
        surface = float(lac["surfaceM"])
        poly = np.array([monde.pixel(p["x"], p["y"]) for p in lac["outline"]])
        if len(poly) < 3:
            print("  {:<4} contour de {} points, ignore".format(i, len(poly)))
            continue
        pts = _densifie(poly)
        mur = np.maximum(0.0, surface - monde.altitude(pts[:, 0], pts[:, 1]))
        ligne = {
            "lac": i, "surfaceM": surface, "sommets": len(poly),
            "pctPlusDe1m": float((mur > SEUIL_MUR_M).mean() * 100.0),
            "pctPlusDe3m": float((mur > 3.0).mean() * 100.0),
            "murMaxM": float(mur.max()), "murP95M": float(np.percentile(mur, 95)),
        }
        releve.append(ligne)
        print("  {:<4} {:>8.2f} {:>8.1f} {:>7d} {:>8.1f}% {:>7.1f}% {:>7.2f}m {:>7.2f}m".format(
            i, surface, float(lac["areaHa"]), len(poly),
            ligne["pctPlusDe1m"], ligne["pctPlusDe3m"], ligne["murMaxM"], ligne["murP95M"]))
    if releve:
        print("  {:<4} {:>8} {:>8} {:>7} {:>8.1f}% {:>7.1f}% {:>7.2f}m {:>7.2f}m  <- pire cas".format(
            "", "", "", "",
            max(l["pctPlusDe1m"] for l in releve), max(l["pctPlusDe3m"] for l in releve),
            max(l["murMaxM"] for l in releve), max(l["murP95M"] for l in releve)))
    return releve


def rivieres(monde: Monde) -> dict:
    chemin = monde.racine / "rivers.json"
    if not chemin.is_file():
        print("rivers.json absent")
        return {}
    donnees = json.loads(chemin.read_text(encoding="utf-8"))["rivers"]
    ecarts, montees, descentes = [], [], []
    for riviere in donnees:
        lignes = np.array([monde.pixel(p["x"], p["y"])[0] for p in riviere["points"]])
        colonnes = np.array([monde.pixel(p["x"], p["y"])[1] for p in riviere["points"]])
        sol = monde.altitude(lignes, colonnes)
        eau = np.array([p["z"] / 100.0 for p in riviere["points"]])
        ecarts.append(eau - sol)
        # Le denivele que le TERRAIN fait remonter le long du cours. Une riviere
        # ne remonte pas : cette valeur mesure directement le bruit depose dans
        # le fond de vallee apres le calcul de l'hydrologie. C'est elle qui a
        # confondu le coupable -- mediane 18,8 m pour une amplitude de detail de
        # 21,25 m -- alors que l'ecart eau/sol, lui, ne disait pas d'ou ca venait.
        d = np.diff(sol)
        montees.append(float(d[d > 0].sum()))
        descentes.append(float(sol[0] - sol[-1]))
    e = np.concatenate(ecarts)
    montees = np.array(montees)
    descentes = np.array(descentes)
    res = {
        "noeuds": int(e.size),
        "medianM": float(np.median(e)),
        "suspenduPct": float((e > SEUIL_MUR_M).mean() * 100.0),
        "enterrePct": float((e < -SEUIL_MUR_M).mean() * 100.0),
        "maxSuspenduM": float(e.max()), "maxEnterreM": float(-e.min()),
    }
    print()
    print("RIVIERES -- position du lit par rapport au sol, sur {} noeuds".format(res["noeuds"]))
    print("  ecart median          {:+.2f} m   (0 = pose sur le sol)".format(res["medianM"]))
    print("  suspendu de plus d'1 m {:6.1f} %   au pire {:+.2f} m".format(
        res["suspenduPct"], res["maxSuspenduM"]))
    print("  enterre  de plus d'1 m {:6.1f} %   au pire {:.2f} m".format(
        res["enterrePct"], res["maxEnterreM"]))
    res["remonteeMedianeM"] = float(np.median(montees))
    res["remonteePctDescente"] = float(
        100.0 * np.median(montees) / max(np.median(descentes), 1e-9))
    print("  remontee du TERRAIN le long du cours : {:.1f} m en cumule (mediane),"
          " soit {:.0f} % de sa descente".format(
              res["remonteeMedianeM"], res["remonteePctDescente"]))
    print("    (une riviere ne remonte pas : au-dela de ~10 %, c'est du bruit")
    print("     depose dans le fond de vallee -- voir world.detailRiverFadeM)")
    return res


def main(racine: Path) -> int:
    monde = Monde(racine)
    w = monde.manifest["world"]
    print("{} -- monde {} km, sortie {}x{}, {:.3f} m/pixel".format(
        racine.name, w["sizeKm"], monde.n, monde.n, monde.mpp))
    print()
    l = lacs(monde)
    r = rivieres(monde)
    print()
    alerte = []
    if l and max(x["pctPlusDe1m"] for x in l) > 10.0:
        alerte.append("le polygone d'un lac s'ecarte trop de la berge")
    # L'enfoncement est PLAFONNE a dessein (hydrology.riverMaxSinkM) : une
    # riviere est encaissee. Ce qui doit alerter, c'est un depassement de ce
    # plafond, pas le fait qu'il soit atteint.
    plafond = 1.0
    regles = racine / "world_rules_used.json"
    if regles.is_file():
        plafond = float(json.loads(regles.read_text(encoding="utf-8"))
                        .get("hydrology", {}).get("riverMaxSinkM", 1.0))
    if r and r["suspenduPct"] > 1.0:
        alerte.append("de l'eau de riviere flotte au-dessus du sol")
    if r and r["maxEnterreM"] > plafond + 0.2:
        alerte.append("des rivieres passent sous le sol au-dela du plafond de "
                      "{:.1f} m".format(plafond))
    if r and r.get("remonteePctDescente", 0.0) > 10.0:
        alerte.append("le detail fractal bosselle le fond des vallees")
    if alerte:
        for a in alerte:
            print("ECART : {}".format(a))
        return 1
    print("OK : l'eau colle au relief de sortie.")
    return 0


if __name__ == "__main__":
    here = Path(__file__).resolve().parent
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    src = Path(args[0]) if args else here.parent.parent / "Saved" / "WorldGen" / "20260909"
    raise SystemExit(main(src))
