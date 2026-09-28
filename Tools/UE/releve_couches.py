# Worldseed - le COUT et la TAILLE de chaque couche de semis, avant et apres.
#
# POURQUOI CET OUTIL. Deux pieges de ce depot se mesurent ici et nulle part
# ailleurs. Le premier est le MUR : un maillage dont la taille naturelle est le
# double de celle de ses voisins ressort au double, parce que l'echelle est
# reglee PAR COUCHE et non par maillage -- un SM_shrub de 3 m pose parmi des
# buissons de 1,4 a donne vingt-six exemplaires de 2,4 a 4,4 m dans un rayon de
# 25 m. Le second est le COUT : le tapis pese 67 % de toutes les instances du
# monde, donc un triangle de plus s'y paie 67 fois.
#
#   UnrealEditor-Cmd.exe Worldseed.uproject -run=pythonscript
#     -script="Tools/UE/releve_couches.py" -unattended -nopause -nosplash
#
# Un second fichier de recettes peut etre passe par WORLDSEED_RECETTES_AVANT
# pour obtenir la comparaison ; sans lui, le releve est celui de l'etat courant.
import json
import os
import unreal

def dire(t):
    unreal.log("[COUCHES] %s" % t)

CACHE = {}

def fiche(ref, roots):
    """Triangles et encombrement d'une reference 'RACINE:Nom', ou None."""
    if ref in CACHE:
        return CACHE[ref]
    i = ref.find(":")
    racine, nom = ref[:i], ref[i + 1:]
    if racine not in roots:
        CACHE[ref] = None
        return None
    SM = unreal.load_asset("%s/%s" % (roots[racine], nom))
    if SM is None:
        CACHE[ref] = None
        return None
    b = SM.get_bounds().box_extent
    CACHE[ref] = {
        "tri": SM.get_num_triangles(0),
        "lod": SM.get_num_lods(),
        # LA HAUTEUR EST CE QUI FAIT LE MUR, pas le volume : c'est elle qu'on
        # voit depasser du sous-bois.
        "h": b.z * 2 / 100.0,
        "l": max(b.x, b.y) * 2 / 100.0,
    }
    return CACHE[ref]

def couches(doc):
    """Toutes les couches du fichier, `estran` compris -- il vit hors de `biomes`."""
    out = []
    for nom, b in (doc.get("biomes") or {}).items():
        for L in (b.get("layers") or []):
            out.append((nom, L))
    for L in (doc.get("estran") or []):
        out.append(("estran", L))
    return out

def mesurer(doc):
    """Par couche : triangles moyens PONDERES et hauteur du plus grand."""
    roots = doc.get("roots") or {}
    res = {}
    for biome, L in couches(doc):
        cle = "%s/%s" % (biome, L.get("name"))
        tot_p = 0.0
        tot_t = 0.0
        hmax = 0.0
        gros = ""
        manquants = []
        for ref, poids in (L.get("meshes") or []):
            f = fiche(ref, roots)
            if f is None:
                manquants.append(ref)
                continue
            tot_p += poids
            tot_t += poids * f["tri"]
            if f["h"] > hmax:
                hmax, gros = f["h"], ref
        ech = L.get("scale") or [1.0, 1.0]
        res[cle] = {
            "tri": (tot_t / tot_p) if tot_p else 0.0,
            "hmax": hmax * ech[1],   # la HAUTEUR RENDUE, echelle comprise
            "gros": gros,
            "n": len(L.get("meshes") or []),
            "manquants": manquants,
        }
    return res

ici = os.path.join(unreal.Paths.project_dir(), "Tools/UE/vegetation_recipes.json")
with open(ici, "r", encoding="utf-8") as f:
    apres = mesurer(json.load(f))

avant = None
chemin_avant = os.environ.get("WORLDSEED_RECETTES_AVANT")
if chemin_avant and os.path.exists(chemin_avant):
    with open(chemin_avant, "r", encoding="utf-8") as f:
        avant = mesurer(json.load(f))

dire("=" * 78)
dire("%-42s %9s %9s %8s" % ("couche", "tri avant", "tri apres", "ecart"))
dire("-" * 78)
pires = []
for cle in sorted(apres):
    a = apres[cle]
    if a["manquants"]:
        dire("!! %s : references introuvables %s" % (cle, ", ".join(a["manquants"])))
    av = avant.get(cle) if avant else None
    if av is None:
        dire("%-42s %9s %9.0f %8s  (couche neuve)" % (cle, "-", a["tri"], "-"))
        continue
    d = a["tri"] - av["tri"]
    if abs(d) > 0.5:
        pct = (d / av["tri"] * 100.0) if av["tri"] else 0.0
        dire("%-42s %9.0f %9.0f %+7.0f%%" % (cle, av["tri"], a["tri"], pct))
        pires.append((abs(d), cle, av["tri"], a["tri"]))

if avant:
    dire("-" * 78)
    for cle in sorted(avant):
        if cle not in apres:
            dire("SUPPRIMEE : %-32s elle coutait %.0f tri" % (cle, avant[cle]["tri"]))

dire("")
dire("HAUTEUR RENDUE DU PLUS GRAND DE CHAQUE COUCHE -- c'est le mur qu'on cherche")
dire("-" * 78)
for cle in sorted(apres):
    a = apres[cle]
    av = avant.get(cle) if avant else None
    marque = ""
    if av and a["hmax"] > av["hmax"] * 1.5 + 0.5:
        marque = "   <-- A REGARDER, %.1f m contre %.1f avant" % (a["hmax"], av["hmax"])
    if a["hmax"] >= 4.0 and "arbre" not in cle and "conifere" not in cle \
            and "feuillus" not in cle and "palmier" not in cle and "bois" not in cle:
        marque = marque or "   <-- %.1f m HORS D'UN ROLE D'ARBRE" % a["hmax"]
    if marque:
        dire("%-42s %6.1f m  %s%s" % (cle, a["hmax"], a["gros"], marque))
dire("=" * 78)
dire("RELEVE TERMINE")
