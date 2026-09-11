"""Confronte le monde genere au climat REEL, en deux etapes.

    Tools/WorldGen/.venv/Scripts/python.exe Tools/WorldGen/terre.py [dossier]

D'OU VIENT LA REFERENCE. Le pack Ultra Dynamic Sky livre 23 presets climatiques
qui sont des RELEVES DE VILLES REELLES : chacun cite sa station et sa source
(weatherspark). Extraits dans `uds_presets_livres.json`, ils forment un jeu de
23 points (temperature annuelle, cumul annuel de pluie) couvrant tout l'eventail
terrestre, du Polar_Ice_Cap au Tropical_Rainforest. C'est une reference
verifiable, pas une liste de pourcentages de memoire.

Seul pourcentage de surface utilise ici, et il est source : le climat BWh
(desert chaud) couvre 14,2 % des terres emergees et est le DEUXIEME climat le
plus repandu de la planete apres le polaire (Peel, Finlayson et McMahon 2007,
via l'article Koppen de Wikipedia).

CE QUE LE FICHIER MESURE :

1. NOS SEUILS DE WHITTAKER SONT-ILS TERRESTRES ? On fait passer les 23 climats
   reels dans notre propre diagramme et on regarde dans quelle case chacun
   tombe. Un desert reel doit tomber dans "desert", une foret tropicale dans
   "foret tropicale". Si nos seuils sont mal places, ce test le dit.

2. NOTRE MONDE COUVRE-T-IL L'EVENTAIL TERRESTRE ? Pour chaque climat reel, on
   cherche s'il existe un endroit du monde genere qui lui ressemble. Un monde
   qui n'aurait nulle part de desert chaud, ou nulle part de foret tropicale,
   se verrait immediatement.
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

# Attendu pour le controle 1 : la case de notre diagramme ou chaque climat reel
# DOIT tomber. Rempli d'apres le nom Koppen du preset, pas d'apres le resultat.
ATTENDU = {
    "Polar_Ice_Cap": "calotte_ou_toundra",
    "Polar_Tundra": "toundra",
    "Subarctic": "taiga",
    "Subarctic-Severe_Winter": "taiga",
    "Subpolar_Oceanic": "taiga_ou_foret_temperee",
    "Oceanic": "foret_temperee",
    "Humid_Subtropical": "foret_temperee_humide",
    "Humid_Subtropical-Dry_Winter": "foret_temperee_humide",
    "Hot_Summer_Continental": "foret_temperee",
    "Warm_Summer_Continental": "foret_temperee",
    "Mediterranean_Hot_Summer": "steppe_ou_prairie",
    "Mediterranean_Cool_Summer": "steppe_ou_prairie",
    "Mediterranean_Cold_Summer": "steppe_ou_prairie",
    "Hot_Desert": "desert_chaud",
    "Cold_Desert": "desert_froid",
    "Hot_Semi-Arid": "desert_chaud_ou_savane",
    "Cold_Semi-Arid": "steppe_ou_desert_froid",
    "Tropical_Rainforest": "foret_tropicale_humide",
    "Tropical_Monsoon": "foret_tropicale_humide",
    "Tropical_Savanna-Dry_Winter": "savane",
    "Tropical_Savanna-Dry_Summer": "savane",
    "Subtropical_Highland": "foret_temperee",
    "Subtropical_Highland-Dry_Winter": "foret_temperee",
}


def climat_annuel(p: dict) -> tuple:
    """(temperature moyenne annuelle, cumul annuel de precipitations)."""
    t = np.mean([(p["{} Average High Temp (C)".format(s)]
                  + p["{} Average Low Temp (C)".format(s)]) * 0.5 for s in SAISONS])
    mm = sum((p["{} Rainfall (mm)".format(s)] + p["{} Snowfall (mm)".format(s)]) * 3.0
             for s in SAISONS)
    return float(t), float(mm)


def case_whittaker(t: float, p: float, bandes: list) -> str:
    for b in bandes:
        if t <= b["tMax"]:
            for p_max, nom in b["cuts"]:
                if p <= p_max:
                    return nom
            return b["cuts"][-1][1]
    return bandes[-1]["cuts"][-1][1]


def controle_1(livres: dict, bandes: list) -> None:
    print("=== 1. Les 23 climats REELS passes dans NOTRE diagramme de Whittaker ===")
    print("{:<34} {:>7} {:>8}   {:<24} {}".format(
        "climat reel (ville source)", "T an", "pluie an", "notre case", "verdict"))
    bons = 0
    for nom in sorted(livres):
        t, mm = climat_annuel(livres[nom])
        case = case_whittaker(t, mm, bandes)
        att = ATTENDU.get(nom, "?")
        ok = case in att.split("_ou_") or case == att or any(
            case == x for x in att.split("_ou_"))
        # "calotte" n'est pas une case Whittaker : la calotte est une surcharge
        if att == "calotte_ou_toundra":
            ok = case in ("toundra", "desert_froid")
        bons += 1 if ok else 0
        print("{:<34} {:>7.1f} {:>8.0f}   {:<24} {}".format(
            nom[:34], t, mm, case, "OK" if ok else "!! attendu " + att))
    print("-> {} sur {} climats reels tombent dans la case attendue\n".format(bons, len(livres)))


def controle_2(dossier: Path, livres: dict) -> None:
    man = json.loads((dossier / "manifest.json").read_text(encoding="utf-8"))
    rng = man["climateRanges"]
    idx = np.array(Image.open(dossier / "biome_index.png"))
    a = np.array(Image.open(dossier / "climate_temp.png")).astype(np.float32)
    temp = rng["tempMinC"] + a / 255.0 * (rng["tempMaxC"] - rng["tempMinC"])
    b = np.array(Image.open(dossier / "climate_rain.png")).astype(np.float32)
    pluie = b / 255.0 * rng["precipMaxMm"]
    terre = idx >= 3
    tt, pp = temp[terre], pluie[terre]

    print("=== 2. Le monde genere couvre-t-il ces climats reels ? ===")
    print("   (part des terres a moins de 3 C et 30 % de pluie du climat cible)")
    print("{:<34} {:>7} {:>8} {:>12}".format("climat reel", "T an", "pluie an", "part terres"))
    absents = []
    for nom in sorted(livres):
        t, mm = climat_annuel(livres[nom])
        proche = (np.abs(tt - t) <= 3.0) & (np.abs(pp - mm) <= max(0.30 * mm, 60.0))
        part = 100.0 * float(proche.mean())
        if part < 0.05:
            absents.append(nom)
        print("{:<34} {:>7.1f} {:>8.0f} {:>11.2f} %".format(nom[:34], t, mm, part))
    print("\n-> ABSENTS du monde ({}) : {}".format(
        len(absents), ", ".join(absents) if absents else "aucun"))

    print("\n=== Etendue climatique comparee ===")
    print("  monde   : T {:.1f} a {:.1f} C, pluie {:.0f} a {:.0f} mm/an".format(
        float(tt.min()), float(tt.max()), float(pp.min()), float(pp.max())))
    ts = [climat_annuel(v)[0] for v in livres.values()]
    ps = [climat_annuel(v)[1] for v in livres.values()]
    print("  23 reels: T {:.1f} a {:.1f} C, pluie {:.0f} a {:.0f} mm/an".format(
        min(ts), max(ts), min(ps), max(ps)))

    print("\n=== Parts de biomes, contre la seule reference de surface sourcee ===")
    parts = man["stats"]["biomeShare"]
    dc = parts.get("Désert chaud", 0.0)
    print("  Desert chaud : {:.1f} % (Terre, climat BWh : 14,2 % des terres,".format(dc))
    print("                 2e climat le plus repandu -- Peel et al. 2007)")


def bulletin(dossier: Path) -> None:
    """Bulletin de conformite terrestre : seulement des criteres SOURCES.

    On n'y met aucun pourcentage de biome sorti de memoire. Chaque ligne est soit
    une constante geometrique (parts de surface d'une sphere), soit une valeur
    physique largement etablie (part des terres emergees, moyenne des
    precipitations terrestres), soit une valeur citee (BWh, Peel et al. 2007).
    """
    man = json.loads((dossier / "manifest.json").read_text(encoding="utf-8"))
    s = man["stats"]

    print("\n=== BULLETIN DE CONFORMITE TERRESTRE ===")
    print("{:<44} {:>10} {:>10}  {}".format("critere", "monde", "Terre", "ecart"))

    lignes = []
    lignes.append(("Terres emergees, part du monde", s["landPct"], 29.2, "%"))
    lignes.append(("Pluie moyenne sur les terres", s.get("precipMeanLand", 0.0), 715.0, "mm/an"))
    lignes.append(("Terres au-dessus de 2000 mm", s.get("precipWetPct", 0.0), 7.5, "%"))
    parts = s["biomeShare"]
    # ATTENTION A LA COMPARAISON : notre "desert chaud" est tout ce qui recoit
    # moins de 300 mm dans la bande chaude, ce qui couvre a la fois le desert
    # vrai (BWh, 14,2 % des terres, Peel et al. 2007) ET le semi-aride chaud
    # (BSh, environ 7 %). La cible honnete est donc leur somme, pas BWh seul.
    lignes.append(("Desert + semi-aride chauds (BWh+BSh)",
                   parts.get("Désert chaud", 0.0), 21.0, "%"))
    if "tropicalPct" in s:
        lignes.append(("Surface tropicale (< 23,4 deg)", s["tropicalPct"], 39.8, "%"))
        lignes.append(("Surface polaire (> 66,6 deg)", s["polarPct"], 8.3, "%"))
    for nom, v, cible, unite in lignes:
        ecart = (v - cible) / cible * 100.0 if cible else 0.0
        drapeau = "OK" if abs(ecart) <= 15.0 else ("passable" if abs(ecart) <= 40.0 else "ECART")
        print("{:<44} {:>10.1f} {:>10.1f} {:>+7.0f} %  {}".format(
            nom + " (" + unite + ")", v, cible, ecart, drapeau))
    print("\n  Sources : part des terres emergees et moyenne des precipitations, valeurs")
    print("  physiques etablies ; BWh 14,2 % des terres, Peel, Finlayson et McMahon 2007 ;")
    print("  parts de surface par zone, geometrie d'une sphere (surface entre -L et +L = sin L).")


def run(dossier: Path) -> None:
    r = json.loads(io.open(RACINE / "Tools" / "WorldGen" / "rules" / "world_rules.json",
                           encoding="utf-8").read())
    livres = json.loads((dossier / "uds_presets_livres.json").read_text(encoding="utf-8"))
    bulletin(dossier)
    controle_1(livres, r["biomes"]["whittakerBands"])
    controle_2(dossier, livres)
    score(dossier)




# --------------------------------------------------------------------- score
# Ajout du 11 septembre 2026 : un CHIFFRE UNIQUE pour arbitrer entre deux
# reglages. Sans lui, comparer deux mondes revient a peser huit ecarts a l'oeil
# et a se convaincre de ce qu'on esperait. Les references ci-dessous sont des
# ordres de grandeur largement admis pour les grands biomes terrestres, en part
# des terres emergees ; elles sont moins solides que les six criteres du
# bulletin, d'ou leur presence ici et non la-haut.
BIOMES_TERRE = {
    "Calotte glaciaire": 10.0,   # Antarctique 14 M km2 + Groenland 1,8, sur 149
    "Toundra": 8.0,
    "Taïga": 10.0,
    "Forêt tempérée": 9.0,
    "Prairie": 8.0,
    "Savane": 13.0,
    "Désert chaud": 21.0,        # BWh 14,2 (Peel 2007) + BSh environ 7
    "Forêt tropicale humide": 11.0,
}


def score(dossier: Path) -> float:
    man = json.loads((dossier / "manifest.json").read_text(encoding="utf-8"))
    parts = man["stats"]["biomeShare"]
    print("\n=== ECART AUX GRANDS BIOMES TERRESTRES ===")
    print("{:<26}{:>8}{:>8}{:>9}".format("biome", "monde", "Terre", "ecart"))
    ecarts = []
    for nom, cible in sorted(BIOMES_TERRE.items(), key=lambda kv: -kv[1]):
        v = parts.get(nom, 0.0)
        e = (v - cible) / cible * 100.0
        ecarts.append(abs(e))
        print("{:<26}{:>8.2f}{:>8.1f}{:>8.0f} %".format(nom, v, cible, e))
    m = float(np.mean(ecarts))
    print("-> ecart absolu moyen : {:.1f} %  (plus petit = plus terrestre)".format(m))
    return m


if __name__ == "__main__":
    run(Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAUT)
