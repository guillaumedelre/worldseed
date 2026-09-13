# -*- coding: utf-8 -*-
"""Restreindre le semis a une PALETTE, c'est-a-dire a un jeu de packs.

POURQUOI CET OUTIL. Le monde peut piocher dans NEUF familles visuelles, qui ne
sont pas du meme dessin : le low-poly a facettes d'Orasot, le PBR stylise de
Stylized_PBR_Nature, le peint a la main de Stylized_Village, le plat et sature
de Kobo... Les melanger se voit -- un rocher facette a cote d'un feuillage peint
casse la lecture. Juger ce melange demande de pouvoir l'eteindre, pas d'en
discuter.

DEUX FICHIERS, ET C'EST LE POINT A COMPRENDRE :

  vegetation_recipes.complet.json   le CATALOGUE : tout ce qu'on possede, les
                                    neuf familles. C'est la source, on ne
                                    l'edite qu'en ajoutant des maillages.
  vegetation_recipes.json           les recettes ACTIVES, celles que lit
                                    vegetation.build_world(). C'est une VUE du
                                    catalogue, filtree par la palette courante.

Les deux sont versionnes : un clone frais retrouve le catalogue entier ET la
palette en vigueur.

CE QUE L'OUTIL NE TOUCHE PAS : ni les pas de grille, ni les echelles, ni les
distances de coupe. La densite et les tailles restent celles qui ont ete
reglees ; seule la PROVENANCE des maillages change. Le nombre d'instances est
donc rigoureusement identique d'une palette a l'autre -- mesure : 344 482 dans
les deux cas.

Usage :
    import palette
    print(palette.simuler("origine"))   # ce que ca donnerait, sans rien ecrire
    palette.appliquer("origine")        # puis vegetation.build_world()
    palette.catalogue()                 # reverse la palette active au catalogue
"""
import io
import json
import os

ACTIVES = os.path.join(os.path.dirname(__file__), "vegetation_recipes.json")
CATALOGUE = os.path.join(os.path.dirname(__file__), "vegetation_recipes.complet.json")

# Les familles, par leurs prefixes de racine. Un pack = un dessin.
FAMILLES = {
    "orasot": ["LPF", "SFL", "GRN", "RED", "DES", "BAM", "DRK"],
    "pbr_nature": ["PBF", "PBR"],
    "village": ["SVF"],
    "dreamscape": ["DSF", "DST", "DSR"],
    "stylized_forest": ["SFP", "SFT", "SFS"],
    "egypte": ["EGP", "EGS"],
    "kobo": ["KOB"],
    "snow_forest": ["SNW", "PIR"],
    "rochers": ["SRK", "SRF"],
}

# Les palettes proposees, du plus homogene au plus melange.
PALETTES = {
    # ATTENTION : "orasot" SEUL ne marche pas. La calotte glaciaire se retrouve
    # sans aucune vegetation, et la toundra, le desert froid et l'alpin perdent
    # leur tapis -- tous empruntaient a Stylized_PBR_Nature. Mesure, pas suppose.
    "orasot": ["orasot"],
    # La palette retenue par le proprietaire le 13 septembre 2026 : le look le
    # plus homogene atteignable, et celui avec lequel le monde a ete bati.
    "origine": ["orasot", "pbr_nature"],
    # Tout le catalogue.
    "tout": list(FAMILLES),
}


def _racines(palette) -> set:
    """Rend l'ensemble des prefixes autorises pour une palette."""
    if isinstance(palette, str):
        if palette in PALETTES:
            familles = PALETTES[palette]
        elif palette in FAMILLES:
            familles = [palette]
        else:
            raise ValueError("palette inconnue : {} (connues : {} / {})".format(
                palette, ", ".join(PALETTES), ", ".join(FAMILLES)))
    else:
        familles = list(palette)
    ok = set()
    for f in familles:
        if f not in FAMILLES:
            raise ValueError("famille inconnue : {}".format(f))
        ok.update(FAMILLES[f])
    return ok


def _lire(chemin) -> dict:
    with io.open(chemin, encoding="utf-8") as f:
        return json.load(f)


def _ecrire(d, chemin) -> None:
    with io.open(chemin, "w", encoding="utf-8", newline="\r\n") as f:
        f.write(json.dumps(d, indent=2, ensure_ascii=False))


def simuler(palette) -> dict:
    """Dit ce que donnerait une palette, SANS rien ecrire.

    C'est le controle a passer avant d'appliquer : il nomme les couches qui
    disparaitraient et les biomes qui resteraient nus.
    """
    ok = _racines(palette)
    d = _lire(CATALOGUE)
    garde = perdu = 0
    couches_vides, biomes_nus = [], []
    for bn, b in d["biomes"].items():
        restantes = 0
        for c in b["layers"]:
            n = sum(1 for m, _ in c["meshes"] if m.split(":")[0] in ok)
            garde += n
            perdu += len(c["meshes"]) - n
            if n:
                restantes += 1
            else:
                couches_vides.append("{}/{}".format(bn, c["name"]))
        if not restantes:
            biomes_nus.append(bn)
    return {"palette": palette, "racines": sorted(ok), "maillagesGardes": garde,
            "maillagesRetires": perdu, "couchesVidees": couches_vides,
            "biomesNus": biomes_nus, "utilisable": not biomes_nus}


def appliquer(palette) -> dict:
    """Ecrit les recettes actives a partir du CATALOGUE.

    Part toujours du catalogue, jamais des recettes actives : appliquer deux
    palettes de suite ne cumule donc pas les filtres.
    """
    r = simuler(palette)
    if not r["utilisable"]:
        raise RuntimeError("palette refusee, biomes sans aucune vegetation : {}".format(
            ", ".join(r["biomesNus"])))
    ok = _racines(palette)
    d = _lire(CATALOGUE)
    d["_palette"] = r["palette"] if isinstance(r["palette"], str) else list(r["palette"])
    for b in d["biomes"].values():
        couches = []
        for c in b["layers"]:
            c["meshes"] = [m for m in c["meshes"] if m[0].split(":")[0] in ok]
            if c["meshes"]:
                couches.append(c)
        b["layers"] = couches
    _ecrire(d, ACTIVES)
    return r


def catalogue() -> str:
    """Reverse les recettes ACTIVES dans le catalogue.

    A n'appeler qu'apres avoir ajoute des maillages a la main dans les recettes
    actives, pour que le catalogue ne les perde pas. Refuse de tourner si une
    palette est en vigueur, car le catalogue y perdrait tout le reste.
    """
    d = _lire(ACTIVES)
    if d.get("_palette") not in (None, "tout"):
        raise RuntimeError(
            "palette '{}' en vigueur : reverser ecraserait le catalogue. "
            "Faire appliquer('tout'), reporter la main, puis recommencer.".format(d["_palette"]))
    d.pop("_palette", None)
    _ecrire(d, CATALOGUE)
    return "catalogue mis a jour depuis les recettes actives"


def active() -> str:
    """Nom de la palette en vigueur, tel qu'inscrit dans les recettes actives."""
    return _lire(ACTIVES).get("_palette", "(non marquee)")
