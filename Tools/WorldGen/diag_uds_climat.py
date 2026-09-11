"""Controle de coherence du branchement UDS : trois questions, trois mesures.

    Tools/WorldGen/.venv/Scripts/python.exe Tools/WorldGen/diag_uds_climat.py

1. LE CLIMAT COLLE-T-IL AUX BIOMES ? Les biomes sont classes par le diagramme de
   Whittaker a partir de la temperature et de la pluie. Un preset derive des
   MEMES cartes doit donc retomber dans la case Whittaker de son propre biome.
   C'est un controle falsifiable : si un preset de taiga tombe dans la case
   "desert chaud", la chaine est fausse quelque part.

2. NOS PRESETS RESSEMBLENT-ILS A CEUX D'UDS ? Pour chaque preset genere, on
   cherche le plus proche parmi les 23 presets Koppen livres par le pack, sur
   les huit temperatures saisonnieres et les quatre cumuls de pluie. Le nom
   trouve doit avoir un sens : une foret tropicale humide doit tomber sur
   Tropical_Rainforest, pas sur Subarctic.

3. LA METEO COLLE-T-ELLE AU SOL PEINT (pack Orasot) ? La ou nos presets font
   neiger, le terrain doit pouvoir etre blanc : on croise les mois de neige de
   chaque biome avec le poids MOYEN de la couche peinte `Snow` sur ce biome.
"""

from __future__ import annotations

import io
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

RACINE = Path(__file__).resolve().parent.parent.parent
DEFAUT = RACINE / "Saved" / "WorldGen" / "20260909"
SAISONS = ("Winter", "Spring", "Summer", "Autumn")


def _case_whittaker(t: float, p: float, bandes: list, ids: dict) -> str:
    """Rejoue la lecture du diagramme de biomes._whittaker pour un seul point."""
    for b in bandes:
        if t <= b["tMax"]:
            for p_max, nom in b["cuts"]:
                if p <= p_max:
                    return nom
            return b["cuts"][-1][1]
    return bandes[-1]["cuts"][-1][1]


def controle_1(gen: dict, regles: dict) -> int:
    bio = regles["biomes"]
    labels = gen["labels"]
    # surcharges de biomes.classify : elles ne viennent PAS du couple T/pluie
    hors_whittaker = {"calotte", "alpin", "roche_nue", "plage", "marais"}
    inv = dict((v, k) for k, v in bio["ids"].items())
    print("\n=== 1. Le climat retombe-t-il dans la case Whittaker du biome ? ===")
    print("{:<26} {:>7} {:>7}   {:<24} {}".format("biome", "T moy", "pluie", "case lue", "verdict"))
    faux = 0
    vus = set()
    for nom, p in sorted(gen["presets"].items()):
        bid = int(nom.split("_")[2])
        cle = inv.get(bid, "?")
        if cle in hors_whittaker or bid in vus:
            continue
        vus.add(bid)
        src = p["Data Source"]
        t = float(src.split("T ")[1].split(" C")[0])
        pl = float(src.split("pluie ")[1].split(" mm")[0])
        case = _case_whittaker(t, pl, bio["whittakerBands"], bio["ids"])
        ok = (case == cle)
        faux += 0 if ok else 1
        print("{:<26} {:>7.1f} {:>7.0f}   {:<24} {}".format(
            labels[str(bid)][:26], t, pl, case, "OK" if ok else "ECART -> " + cle))
    print("-> {} ecart(s) sur {} biomes classes par Whittaker".format(faux, len(vus)))
    return faux


def _vecteur(p: dict) -> np.ndarray:
    v = []
    for s in SAISONS:
        v += [p["{} Average High Temp (C)".format(s)], p["{} Average Low Temp (C)".format(s)]]
    for s in SAISONS:                       # pluie ramenee a l'echelle des degres
        v.append(p["{} Rainfall (mm)".format(s)] * 0.1)
    return np.array(v, dtype=np.float64)


def controle_2(gen: dict, livres: dict) -> None:
    print("\n=== 2. Le preset UDS le plus proche de chacun des notres ===")
    print("{:<30} {:<30} {:>8}".format("notre preset", "plus proche chez UDS", "ecart"))
    for nom, p in sorted(gen["presets"].items()):
        if nom.endswith("_S"):
            continue                        # meme climat, saisons echangees
        a = _vecteur(p)
        best, bd = None, 1e18
        for k, q in livres.items():
            d = float(np.sqrt(((a - _vecteur(q)) ** 2).mean()))
            if d < bd:
                best, bd = k, d
        etiquette = gen["labels"][str(int(nom.split("_")[2]))]
        print("{:<30} {:<30} {:>8.1f}".format(etiquette[:30], best, bd))


def controle_3(dossier: Path, gen: dict) -> None:
    print("\n=== 3. La neige des presets tombe-t-elle la ou le sol peut blanchir ? ===")
    idx = np.array(Image.open(dossier / "biome_index.png"))
    neige = np.array(Image.open(dossier / "layer_Snow.png")).astype(np.float32)
    neige = neige / (65535.0 if neige.max() > 255.0 else 255.0)
    pierre = np.array(Image.open(dossier / "layer_Stone.png")).astype(np.float32)
    pierre = pierre / (65535.0 if pierre.max() > 255.0 else 255.0)
    print("{:<26} {:>10} {:>12} {:>12}".format(
        "biome", "mois neig.", "couche Snow", "couche Stone"))
    lignes = []
    for nom, p in sorted(gen["presets"].items()):
        if nom.endswith("_S"):
            continue
        bid = int(nom.split("_")[2])
        # une saison = 3 mois ; on compte les saisons ou la neige domine la pluie
        mois = sum(3 for s in SAISONS
                   if p["{} Snowfall (mm)".format(s)] > p["{} Rainfall (mm)".format(s)])
        m = idx == bid
        if not m.any():
            continue
        lignes.append((mois, float(neige[m].mean()), float(pierre[m].mean()),
                       gen["labels"][str(bid)]))
    for mois, sn, st, lab in sorted(lignes, reverse=True):
        print("{:<26} {:>10} {:>12.3f} {:>12.3f}".format(lab[:26], mois, sn, st))
    a = np.array([l[0] for l in lignes], dtype=np.float64)
    b = np.array([l[1] for l in lignes], dtype=np.float64)
    if a.std() > 0 and b.std() > 0:
        print("-> correlation mois de neige / poids de la couche Snow : {:+.2f}".format(
            float(np.corrcoef(a, b)[0, 1])))


def run(dossier: Path, regles_p: Path) -> None:
    gen = json.loads((dossier / "uds_climate.json").read_text(encoding="utf-8"))
    livres = json.loads((dossier / "uds_presets_livres.json").read_text(encoding="utf-8"))
    regles = json.loads(io.open(regles_p, encoding="utf-8").read())
    controle_1(gen, regles)
    controle_2(gen, livres)
    controle_3(dossier, gen)


if __name__ == "__main__":
    d = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAUT
    run(d, RACINE / "Tools" / "WorldGen" / "rules" / "world_rules.json")
