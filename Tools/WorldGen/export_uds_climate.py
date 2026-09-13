"""Traduit le climat de Worldseed en presets Ultra Dynamic Sky.

    Tools/WorldGen/.venv/Scripts/python.exe Tools/WorldGen/export_uds_climate.py

POURQUOI CE FICHIER EXISTE. UDS pilote sa meteo par un `UDS_Climate_Preset` :
quatre saisons x (temperature moyenne haute, moyenne basse, pourcentage de ciel
couvert, pluie mensuelle en mm, neige mensuelle en mm), plus un indicateur de
poussiere. C'est exactement ce que le generateur calcule deja par pixel. On ne
devine donc AUCUNE correspondance avec la classification de Koppen : on mesure
nos propres cartes et on remplit les memes cases.

CALAGE. Les formules ci-dessous sont calees sur les 23 presets LIVRES par UDS,
lus dans l'editeur. Ce que cette lecture a corrige, et qu'on aurait rate en
raisonnant de tete :

  - `Rainfall (mm)` est un cumul MENSUEL, pas saisonnier (Tropical_Rainforest
    145 a 184 mm). Le cumul annuel de Worldseed se divise donc par 12.
  - `Snowfall (mm)` est un EQUIVALENT-EAU, pas une hauteur de neige : Oceanic
    porte 36,2 mm de pluie ET 45,5 mm de neige le meme mois d'hiver.
  - L'ecart diurne reel ne fait que 4 a 9 degres (Hot_Desert 22/16 en hiver,
    34/29 en ete) : ce sont des MOYENNES haute et basse, pas des extremes.

DEUX HEMISPHERES, UNE SEULE SAISON. UDS n'a qu'une saison globale, or le monde
va d'un pole a l'autre : quand c'est l'ete au nord, c'est l'hiver au sud. Chaque
biome present dans les deux hemispheres recoit donc DEUX presets, le preset sud
etant le preset nord avec hiver/ete et printemps/automne echanges.

Sortie : `uds_climate.json` dans le dossier du monde.
"""

from __future__ import annotations

import io
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

sys.path.insert(0, str(Path(__file__).resolve().parent))
from worldgen import climate as climate_mod  # noqa: E402

RACINE = Path(__file__).resolve().parent.parent.parent
DEFAUT = RACINE / "Saved" / "WorldGen" / "20260909"

SAISONS = ("Winter", "Spring", "Summer", "Autumn")
# Les biomes d'eau n'ont pas de climat propre a piloter.
SANS_CLIMAT = {0, 1, 2}


def _charger_gris(chemin: Path) -> np.ndarray:
    a = np.array(Image.open(chemin)).astype(np.float32)
    return a / (65535.0 if a.max() > 255.0 else 255.0)


def continentalite(est_eau: np.ndarray, metres_par_px: float, portee_km: float) -> np.ndarray:
    """0 au bord de mer, tend vers 1 en profondeur des terres.

    Reprend mot pour mot `climate._continentality` : toute divergence ici
    donnerait des amplitudes saisonnieres differentes de celles qui ont servi a
    classer les biomes.
    """
    if not est_eau.any():
        return np.ones(est_eau.shape, dtype=np.float32)
    dist_km = ndimage.distance_transform_edt(~est_eau).astype(np.float32) * (metres_par_px / 1000.0)
    return (1.0 - np.exp(-dist_km / max(portee_km, 1e-3))).astype(np.float32)


def _facteur_saisonnier(lat: float, u: dict, tropique: float) -> dict:
    """Modulation saisonniere de la pluie, par bande de latitude.

    Deux motifs reels, et seulement deux :
      - sous les tropiques la ZCIT suit le soleil, donc ete humide / hiver sec.
        C'est ce qui donne a la savane sa saison seche -- son trait definitoire ;
      - entre 30 et 45 degres, le front polaire descend en hiver : hiver humide
        et ete sec, le regime mediterraneen.
    Ailleurs, pas de modulation : le generateur ne calcule qu'un cumul annuel et
    inventer une saisonnalite serait de l'ornement.

    ERREUR CORRIGEE : la premiere version faisait culminer l'effet ZCIT A
    L'EQUATEUR. C'est l'inverse du reel. L'equateur est humide toute l'annee
    parce que la ZCIT y passe DEUX FOIS par an ; la saison seche marquee est
    vers 10 a 20 degres, ou elle ne passe qu'une fois. Consequence du bug : la
    savane et la foret tropicale seche n'avaient pas de saison seche, et le
    controle de proximite les faisait tomber sur Tropical_Rainforest au lieu de
    Tropical_Savanna. Le poids suit donc un demi-sinus, nul a l'equateur comme
    au tropique et maximal a mi-chemin.
    """
    a = abs(lat)
    f = dict((s, 1.0) for s in SAISONS)
    if a <= tropique:
        poids = float(np.sin(np.pi * a / max(tropique, 1e-6)))
        f["Summer"] = 1.0 + (u["itczSummerFactor"] - 1.0) * poids
        f["Winter"] = 1.0 + (u["itczWinterFactor"] - 1.0) * poids
    elif u["mediterraneanLatMinDeg"] <= a <= u["mediterraneanLatMaxDeg"]:
        mid = 0.5 * (u["mediterraneanLatMinDeg"] + u["mediterraneanLatMaxDeg"])
        demi = 0.5 * (u["mediterraneanLatMaxDeg"] - u["mediterraneanLatMinDeg"])
        poids = max(0.0, 1.0 - abs(a - mid) / max(demi, 1e-6))
        f["Winter"] = 1.0 + (u["mediterraneanWinterFactor"] - 1.0) * poids
        f["Summer"] = 1.0 + (u["mediterraneanSummerFactor"] - 1.0) * poids
    # la moyenne annuelle doit rester la moyenne annuelle
    moy = sum(f.values()) / 4.0
    return dict((s, v / moy) for s, v in f.items())


def preset_du_biome(t_moy, amp, pluie_an, cont, lat, u, tropique):
    """Les 21 cases d'un UDS_Climate_Preset, depuis cinq mesures."""
    diurne = (u["diurnalBaseC"] + u["diurnalContinentalC"] * cont
              + u["diurnalAridC"] * (1.0 - min(pluie_an / u["aridPrecipRefMm"], 1.0)))
    moyennes = {"Winter": t_moy - amp * 0.5, "Summer": t_moy + amp * 0.5,
                "Spring": t_moy, "Autumn": t_moy}
    fact = _facteur_saisonnier(lat, u, tropique)
    mensuel = pluie_an / 12.0

    out = {}
    for s in SAISONS:
        t = moyennes[s]
        out["{} Average High Temp (C)".format(s)] = round(t + diurne * 0.5, 1)
        out["{} Average Low Temp (C)".format(s)] = round(t - diurne * 0.5, 1)
        mm = mensuel * fact[s]
        # part tombant en neige : tout sous le seuil, rien au-dessus de la bande
        part_neige = min(max((u["snowThresholdC"] - t) / max(u["snowBandC"], 1e-6), 0.0), 1.0)
        out["{} Rainfall (mm)".format(s)] = round(mm * (1.0 - part_neige), 1)
        out["{} Snowfall (mm)".format(s)] = round(mm * part_neige, 1)
        out["{} Cloudy Percentage".format(s)] = round(
            u["cloudyFloorPct"] + u["cloudySpanPct"]
            * (1.0 - float(np.exp(-mm / max(u["cloudyPrecipScaleMm"], 1e-6)))), 1)
    out["Dust/Sand Present"] = 1.0 if (pluie_an < u["dustPrecipMaxMm"] and t_moy > 5.0) else 0.0
    return out


def _echange_hemisphere(p):
    """Preset de l'hemisphere sud : la saison d'UDS est globale, pas le climat."""
    paires = {"Winter": "Summer", "Summer": "Winter",
              "Spring": "Autumn", "Autumn": "Spring"}
    out = {}
    for k, v in p.items():
        remplace = k
        for a, b in paires.items():
            if k.startswith(a + " "):
                remplace = b + k[len(a):]
                break
        out[remplace] = v
    return out


def grille_dominante(idx, n_grille, rayon_px):
    """Biome DOMINANT autour de chaque cellule, pas le biome du pixel.

    Un climat est une propriete regionale. Les taches de biome de ce monde ont
    une mediane sous 0,1 hectare : lire le pixel ferait changer la meteo a chaque
    pas. On prend donc le biome le plus represente dans un rayon, ce qui est a la
    fois plus juste physiquement et stable a l'execution.
    """
    n = idx.shape[0]
    pas = n / float(n_grille)
    eau = np.array(sorted(SANS_CLIMAT))
    sortie = []
    for gy in range(n_grille):
        ligne = []
        cy = int((gy + 0.5) * pas)
        y0, y1 = max(0, cy - rayon_px), min(n, cy + rayon_px + 1)
        for gx in range(n_grille):
            cx = int((gx + 0.5) * pas)
            x0, x1 = max(0, cx - rayon_px), min(n, cx + rayon_px + 1)
            fen = idx[y0:y1, x0:x1].ravel()
            terre = fen[~np.isin(fen, eau)]
            if terre.size == 0:
                ligne.append(0)
            else:
                v, c = np.unique(terre, return_counts=True)
                ligne.append(int(v[c.argmax()]))
        sortie.append(ligne)

    # EN MER, ON PREND LE CLIMAT DE LA COTE LA PLUS PROCHE.
    #
    # Une cellule sans aucune terre dans son rayon valait 0, et AUCUN prereglage
    # n'existe pour l'identifiant 0 : le `Cast` de BP_WorldseedClimat echouait
    # alors en silence et la meteo du dernier biome VISITE persistait. Le climat
    # dependait donc de l'historique du joueur et non de l'endroit ou il se
    # trouve -- un accident, pas une intention.
    #
    # POURQUOI LA COTE VOISINE, ET NON UN PREREGLAGE OCEANIQUE DEDIE. Mesure sur
    # ce monde : l'eau n'est JAMAIS a plus de 2002 m d'une terre, mediane 335 m,
    # 90e centile 911 m, et 7,5 % seulement au-dela d'un kilometre. La cote
    # voisine EST donc le climat local. A l'inverse, un prereglage unique pour
    # tout l'ocean serait absurde : la mer fait 27,0 C sous les tropiques,
    # 15,7 aux moyennes latitudes et -4,6 au-dela de 60 degres -- une seule
    # fiche ne peut pas decrire cela, et les autres biomes n'ont pas ce probleme
    # parce qu'ils sont contraints en latitude par construction.
    #
    # Le remplissage se fait par transformee de distance sur la GRILLE (128x128,
    # donc gratuit) et non sur la carte : `distance_transform_edt` rend, pour
    # chaque cellule, l'indice de la cellule NON VIDE la plus proche.
    g = np.array(sortie, dtype=np.int32)
    vide = g == 0
    if vide.any():
        _, (iy, ix) = ndimage.distance_transform_edt(vide, return_indices=True)
        g = g[iy, ix]
    return g.tolist()


def run(dossier: Path, regles: Path) -> dict:
    man = json.loads((dossier / "manifest.json").read_text(encoding="utf-8"))
    r = json.loads(io.open(regles, encoding="utf-8").read())
    u = r["uds"]
    temp_r = r["temperature"]
    tropique = float(r["world"]["tropicDeg"])

    idx = np.array(Image.open(dossier / "biome_index.png"))
    n = idx.shape[0]
    rng = man["climateRanges"]
    temp = rng["tempMinC"] + _charger_gris(dossier / "climate_temp.png") * (
        rng["tempMaxC"] - rng["tempMinC"])
    pluie = _charger_gris(dossier / "climate_rain.png") * rng["precipMaxMm"]

    taille_km = float(man["world"]["sizeKm"])
    m_par_px = taille_km * 1000.0 / (n - 1)
    demi_span = float(r["world"]["latitudeSpanDeg"]) * 0.5
    # ligne 0 = pole SUD, ligne n-1 = pole NORD (config.Geometry.latitude_deg)
    lat_ligne = (np.arange(n, dtype=np.float32) / (n - 1) * 2.0 - 1.0) * demi_span
    lat = np.repeat(lat_ligne[:, None], n, axis=1)

    # La continentalite se mesure a la distance a l'OCEAN, pas a l'eau en
    # general : un lac ou une riviere ne fait pas d'un interieur continental une
    # facade maritime. On repart donc de l'altitude, comme climate.generate.
    est_ocean = np.array(Image.open(dossier / "height_16bit.png")).astype(np.float32)
    mn, mx = float(r["world"]["minElevationM"]), float(r["world"]["maxElevationM"])
    est_ocean = (mn + est_ocean / 65535.0 * (mx - mn)) <= 0.0
    cont = continentalite(est_ocean, m_par_px, float(temp_r["oceanModerationRangeKm"]))
    # Amplitude saisonniere : on APPELLE la fonction du generateur au lieu de
    # recopier sa formule. C'est la seule facon d'etre sur que les temperatures
    # d'hiver et d'ete des presets UDS soient celles du monde.
    amp = climate_mod.seasonal_amplitude(np.abs(lat) / demi_span, cont, temp_r)

    labels = man["biomes"]["labels"]
    presets, table = {}, {}
    print("{:<28} {:>8} {:>7} {:>6} {:>7} {:>6} {:>6}".format(
        "biome / hemisphere", "pixels", "T moy", "amp", "pluie", "T hiv", "T ete"))
    for bid in sorted(int(k) for k in labels):
        if bid in SANS_CLIMAT:
            continue
        for hemi, sel_h in (("N", lat > 0.0), ("S", lat <= 0.0)):
            m = (idx == bid) & sel_h
            px = int(m.sum())
            if px < 200:                      # trop peu pour une mediane credible
                continue
            t_moy = float(np.median(temp[m]))
            p_an = float(np.median(pluie[m]))
            c_moy = float(np.median(cont[m]))
            a_moy = float(np.median(amp[m]))
            l_moy = float(np.median(lat[m]))
            p = preset_du_biome(t_moy, a_moy, p_an, c_moy, l_moy, u, tropique)
            if hemi == "S":
                p = _echange_hemisphere(p)
            # PAS DE ZERO DE REMPLISSAGE, et c'est un choix, pas un oubli.
            # L'acteur de pilotage reconstruit ce nom a l'execution, en
            # Blueprint, qui n'a aucun moyen simple de formater un entier sur
            # deux chiffres : il faudrait une branche et deux concatenations de
            # plus dans un chemin appele toutes les demi-secondes. Ici le nom
            # s'ecrit "CP_Worldseed_" + id + "_" + hemisphere, et rien d'autre.
            # Cout : le navigateur de contenu trie CP_Worldseed_10 avant
            # CP_Worldseed_3. C'est le seul inconvenient, et il est cosmetique.
            nom = "CP_Worldseed_{:d}_{}".format(bid, hemi)
            p["Data Source"] = (
                "Worldseed graine {} - {} ({}) : mediane de {} pixels, T {:.1f} C, "
                "amplitude saisonniere {:.1f} C, pluie {:.0f} mm/an, continentalite {:.2f}".format(
                    man["seed"], labels[str(bid)], hemi, px, t_moy, a_moy, p_an, c_moy))
            presets[nom] = p
            table.setdefault(str(bid), {})[hemi] = nom
            print("{:<28} {:>8} {:>7.1f} {:>6.1f} {:>7.0f} {:>6.1f} {:>6.1f}".format(
                (labels[str(bid)] + " " + hemi)[:28], px, t_moy, a_moy, p_an,
                p["Winter Average High Temp (C)"], p["Summer Average High Temp (C)"]))

    n_grille = int(u["gridResolution"])
    rayon_px = max(1, int(round(float(u["dominantRadiusM"]) / m_par_px)))
    grille = grille_dominante(idx, n_grille, rayon_px)

    demi_cm = taille_km * 1000.0 * 100.0 * 0.5
    sortie = {
        "seed": man["seed"],
        "_comment": ("Genere par export_uds_climate.py. `grid` est indexee [ligne][colonne] avec "
                     "ligne 0 = pole SUD (Y monde = -halfExtentCm) et colonne 0 = X monde minimal, "
                     "comme la carte des biomes du generateur."),
        # La latitude se recalcule cote jeu depuis Y. En 'linear' c'est un simple
        # produit ; en 'equalArea' il faut passer par un arc sinus :
        #     lat = degrees(asin(Y / halfExtentCm)) * spanDeg / 180
        # On transmet donc la CORRESPONDANCE, pas un taux constant qui serait
        # faux de plus de 40 degres pres des poles.
        "world": {"sizeKm": taille_km, "halfExtentCm": demi_cm,
                  "latitudeMapping": str(r["world"].get("latitudeMapping", "linear")),
                  "latitudeEqualAreaBlend": float(r["world"].get("latitudeEqualAreaBlend", 1.0)),
                  "latitudeSpanDeg": float(r["world"]["latitudeSpanDeg"])},
        "labels": labels,
        "presetsParBiome": table,
        "presets": presets,
        "grid": {"resolution": n_grille,
                 "cellSizeCm": 2.0 * demi_cm / n_grille,
                 "dominantRadiusM": float(u["dominantRadiusM"]),
                 "data": grille},
    }
    chemin = dossier / "uds_climate.json"
    chemin.write_text(json.dumps(sortie, ensure_ascii=False, indent=1), encoding="utf-8")

    # Meme grille en CSV : c'est le seul format qu'Unreal importe nativement en
    # DataTable, donc le seul moyen simple de rendre la grille lisible depuis un
    # Blueprint. Une ligne de la grille = une ligne de table, les identifiants
    # separes par des tirets (la virgule est le separateur du CSV).
    csv = ["---,Cellules"]
    for gy, ligne in enumerate(grille):
        csv.append("L{:04d},{}".format(gy, "-".join(str(v) for v in ligne)))
    (dossier / "uds_biome_grid.csv").write_text("\n".join(csv) + "\n", encoding="utf-8")
    print("\n{} presets, grille {}x{} a {:.0f} m/cellule (dominante sur {:.0f} m de rayon)".format(
        len(presets), n_grille, n_grille,
        2.0 * demi_cm / n_grille / 100.0, float(u["dominantRadiusM"])))
    print("Ecrit :", chemin)
    return sortie


if __name__ == "__main__":
    dossier = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAUT
    regles = (Path(sys.argv[2]) if len(sys.argv) > 2
              else RACINE / "Tools" / "WorldGen" / "rules" / "world_rules.json")
    run(dossier, regles)
