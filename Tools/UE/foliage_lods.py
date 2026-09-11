"""Donne des LOD aux maillages de vegetation qui n'en ont pas.

A executer DEPUIS l'editeur :

    import sys, importlib
    sys.path.insert(0, r"D:\\UE\\Worldseed\\Tools\\UE")
    import foliage_lods; importlib.reload(foliage_lods)
    print(foliage_lods.audit())        # ne touche a rien
    print(foliage_lods.appliquer())    # pose les groupes de LOD

POURQUOI. Le semis est passe de 304 a environ 6 200 instances a l'hectare. A
cette densite, un arbre de 1 798 triangles rendu a pleine resolution jusqu'a
l'horizon coute tres cher. Or **aucun maillage de Stylized_PBR_Nature n'a de
LOD** (LOD = 1 partout), et 43 des 141 maillages cites par les recettes sont
dans ce cas au-dessus de 400 triangles.

LE PIEGE, ET IL EST GROS : le groupe de LOD nomme **`Foliage` ne genere AUCUN
LOD**. Sa definition dans `BaseEngine.ini` est `NumLODs=1` -- il est prevu pour
du feuillage dont l'artiste a AUTORISE les LOD a la main, ou qui passe en
billboard. Choisir "Foliage" pour du feuillage est donc exactement le mauvais
reflexe. Les groupes qui reduisent vraiment sont :

    SmallProp   4 LOD, 50 % de triangles par cran, PixelError 10
    LargeProp   idem, LightMapResolution plus grande
    Deco        idem
    HighDetail  6 LOD, PixelError 6

Mesure sur SM_Common_Tree_01 apres passage en LargeProp :
    LOD0 1798  ->  LOD1 899  ->  LOD2 450  ->  LOD3 225 triangles.

DEUX REGLES QUE CE SCRIPT S'IMPOSE :

1. **On ne touche pas a un maillage qui a deja des LOD.** Ceux du bundle Orasot
   en ont souvent trois ou quatre, faits a la main : une reduction automatique
   ferait moins bien.
2. **On ne touche pas aux maillages en dessous de `SEUIL_TRIANGLES`.** Reduire
   un brin d'herbe de 40 triangles ne rapporte rien et abime sa silhouette. Pour
   eux, le levier est la distance de coupe, reglee par couche dans
   `vegetation_recipes.json`.
"""

from __future__ import annotations

import json
import os

import unreal

RECETTES = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "vegetation_recipes.json")

# En dessous, un LOD ne rapporte rien et abime la silhouette.
SEUIL_TRIANGLES = 400
# Au-dessus de cette hauteur, le maillage est traite comme un gros element.
SEUIL_GRAND_M = 4.0

_log: list[str] = []


def log(kind: str, message: str) -> None:
    line = "{}: {}".format(kind, message)
    _log.append(line)
    unreal.log("WORLDSEED_LOD " + line)
    print(line)


def _maillages() -> dict:
    """Tous les maillages cites par les recettes, par reference."""
    with open(RECETTES, "r", encoding="utf-8") as fh:
        rec = json.load(fh)
    roots = rec["roots"]
    refs = set()
    for b in rec["biomes"].values():
        for couche in b["layers"]:
            for ref, _poids in couche["meshes"]:
                refs.add(ref)
    out = {}
    for ref in sorted(refs):
        racine, nom = ref.split(":")
        out[ref] = unreal.EditorAssetLibrary.load_asset(roots[racine] + "/" + nom)
    return out


def _profil(mesh) -> tuple[int, int, float]:
    b = mesh.get_bounds().box_extent
    return (mesh.get_num_triangles(0), mesh.get_num_lods(),
            max(b.x, b.y, b.z) * 2.0 / 100.0)


def audit() -> dict:
    """Etat des lieux, sans rien modifier."""
    _log.clear()
    a_traiter, deja, trop_petits, absents = [], [], [], []
    for ref, mesh in _maillages().items():
        if mesh is None:
            absents.append(ref)
            continue
        tris, lods, taille = _profil(mesh)
        if tris < SEUIL_TRIANGLES:
            trop_petits.append(ref)
        elif lods > 1:
            deja.append(ref)
        else:
            a_traiter.append((tris, taille, ref))
    a_traiter.sort(reverse=True)
    log("INFO", "{} a traiter, {} ont deja des LOD, {} trop petits, {} absents".format(
        len(a_traiter), len(deja), len(trop_petits), len(absents)))
    if absents:
        log("ERROR", "introuvables : {}".format(absents))
    return {"aTraiter": [r for _t, _h, r in a_traiter],
            "dejaPourvus": deja, "tropPetits": trop_petits,
            "absents": absents,
            "trianglesLOD0": sum(t for t, _h, _r in a_traiter),
            "log": list(_log)}


def appliquer(dry_run: bool = False) -> dict:
    """Pose un groupe de LOD sur chaque maillage lourd qui n'en a pas."""
    _log.clear()
    faits, avant_total, apres_total = [], 0, 0
    for ref, mesh in _maillages().items():
        if mesh is None:
            continue
        tris, lods, taille = _profil(mesh)
        if tris < SEUIL_TRIANGLES or lods > 1:
            continue
        groupe = "LargeProp" if taille >= SEUIL_GRAND_M else "SmallProp"
        if dry_run:
            log("SKIPPED", "{} -> {} ({} tris, {:.1f} m)".format(ref, groupe, tris, taille))
            continue
        sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        sub.set_lod_group(mesh, groupe, True)
        n = mesh.get_num_lods()
        cout_avant = tris * 4          # ce que couterait 4 crans sans reduction
        cout_apres = sum(mesh.get_num_triangles(i) for i in range(n))
        avant_total += cout_avant
        apres_total += cout_apres
        faits.append(ref)
        log("MODIFIED", "{:<32} {} -> {} LOD  ({} -> {} tris au dernier cran)".format(
            ref, groupe, n, tris, mesh.get_num_triangles(n - 1)))
    if faits and not dry_run:
        unreal.EditorAssetLibrary.save_directory("/Game/Stylized_PBR_Nature", False, True)
        unreal.EditorAssetLibrary.save_directory("/Game/Orasot_Bundle", False, True)
        log("MODIFIED", "{} maillages sauvegardes".format(len(faits)))
    return {"traites": len(faits), "maillages": faits, "log": list(_log)}


def verifier() -> dict:
    """Controle d'apres coup : il ne doit plus rester de maillage lourd sans LOD."""
    reste = []
    for ref, mesh in _maillages().items():
        if mesh is None:
            continue
        tris, lods, _t = _profil(mesh)
        if tris >= SEUIL_TRIANGLES and lods <= 1:
            reste.append((tris, ref))
    return {"sansLOD": sorted(reste, reverse=True), "nombre": len(reste)}
