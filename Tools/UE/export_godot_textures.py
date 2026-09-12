"""Textures en PNG + manifeste de correspondance, pour completer l'export Godot.

A executer DEPUIS L'EDITEUR INTERACTIF :

    import sys, importlib
    sys.path.insert(0, r"D:\\UE\\Worldseed\\Tools\\UE")
    import export_godot_textures as t; importlib.reload(t)
    print(t.tout())

POURQUOI CE MODULE EXISTE. L'export glTF (`export_godot.py`) sort bien la
GEOMETRIE des 285 maillages, a la bonne echelle, mais **sans textures** :

  - lance en ligne de commande (`-run=pythonscript`), la cuisson des materiaux
    ne produit rien, faute de moteur de rendu : les .glb sortent avec
    `images: 0`, `textures: 0` et des materiaux vides ;
  - lance depuis l'editeur interactif, l'exportateur glTF fait TOMBER le
    processus sur `Assertion failed: IsValid()` (SharedPointer.h:1133). Essaye
    deux fois, avec et sans instanciation manuelle de l'exportateur.

On contourne : la geometrie vient des .glb deja produits, et les textures
sortent ici par `Texture.export_to_disk`, qui lui fonctionne (c'est le meme
chemin que la verification aller-retour des cartes de biomes). Le manifeste dit
quel maillage utilise quel materiau, et quel materiau utilise quelle texture,
avec le role de chacune : de quoi recabler les materiaux dans Godot.
"""

from __future__ import annotations

import json
import os

import unreal

RACINES = ["/Game/Stylized_PBR_Nature", "/Game/Orasot_Bundle"]
DESTINATION = r"D:\UE\Worldseed\Export\Godot"

# Le role d'une texture se LIT sur le nom de son PARAMETRE de materiau, il ne se
# devine pas sur le nom de l'asset. Unreal nomme lui-meme ces parametres
# "BaseColor", "Normal", "Roughness" : c'est la verite, pas un indice.
#
# L'ORDRE COMPTE, et "color" est le plus glouton : "EmissiveColor" doit sortir en
# emissif, pas en couleur de base. Les entrees generiques passent donc en dernier.
# ⚠ "normal" PASSE AVANT "orm", et ce n'est pas un detail de style : le mot
# "n-ORM-al" contient "orm", et contient aussi "rma". Range dans l'autre sens, un
# parametre nomme "Normal" sortait en carte ORM. C'est la faute meme qu'on corrige
# ici -- une sous-chaine non ancree -- et seul le test l'a rendue visible.
ROLE_PAR_PARAMETRE = (
    ("normal", ("normal", "bump")),
    ("orm", ("orm", "arm", "rma", "packed")),
    ("ambient_occlusion", ("occlusion",)),
    ("emissive", ("emissive", "emission")),
    ("opacity", ("opacity", "alpha", "mask", "cutout")),
    ("height", ("height", "displacement", "parallax")),
    ("roughness", ("roughness", "rough", "gloss")),
    ("metallic", ("metallic", "metal")),
    ("specular", ("specular",)),
    ("base_color", ("basecolor", "albedo", "diffuse", "color", "colour")),
)

# Repli quand le nom du parametre ne dit rien ("Texture", "T1", "Param"). On
# retombe sur le nom de l'asset, mais avec un suffixe ANCRE EN FIN DE NOM.
#
# C'EST LE CORRECTIF PRINCIPAL DE CETTE VERSION. L'ancienne table cherchait les
# codes a une lettre N'IMPORTE OU dans le nom : ("roughness", (..., "_r", ...))
# faisait sortir T_Big_Rock en rugosite -- son "_R" etant celui de "Rock" -- et
# ("metallic", (..., "_m")) faisait sortir T_Moss en metallique. Mesure sur le lot
# cote Godot : 424 des 590 references retenues changeaient de role une fois
# reclassees avec un suffixe ancre.
SUFFIXE_ROLE = (
    ("orm", ("orm", "rma", "mra", "arm")),
    ("normal", ("n", "normal", "nrm")),
    ("base_color", ("d", "bc", "albedo", "diffuse", "basecolor", "color")),
    ("ambient_occlusion", ("ao", "occlusion")),
    ("opacity", ("op", "opacity", "mask", "alpha")),
    ("emissive", ("e", "emissive", "emission")),
    ("roughness", ("r", "rough", "roughness")),
    ("metallic", ("m", "metal", "metallic")),
    ("height", ("h", "height", "displacement")),
    ("specular", ("s", "spec", "specular")),
)


def _role_du_parametre(nom_parametre: str) -> str:
    bas = (nom_parametre or "").lower().replace("_", "").replace(" ", "")
    for role, indices in ROLE_PAR_PARAMETRE:
        if any(i in bas for i in indices):
            return role
    return ""


def _role_du_suffixe(nom_asset: str) -> str:
    """Le DERNIER segment separe par '_', et lui seul."""
    morceaux = (nom_asset or "").split("_")
    if len(morceaux) < 2:
        return "inconnu"
    fin = morceaux[-1].lower()
    for role, suffixes in SUFFIXE_ROLE:
        if fin in suffixes:
            return role
    return "inconnu"


def _role(nom_parametre: str, nom_asset: str) -> str:
    return _role_du_parametre(nom_parametre) or _role_du_suffixe(nom_asset)


def _chemin(package: str, extension: str) -> str:
    relatif = package[len("/Game/"):]
    chemin = os.path.join(DESTINATION, relatif.replace("/", os.sep) + extension)
    os.makedirs(os.path.dirname(chemin), exist_ok=True)
    return chemin


def exporter_textures(racines=None) -> dict:
    """Chaque Texture2D en PNG, dans l'arborescence miroir de Content."""
    racines = racines or RACINES
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    opt = unreal.ImageWriteOptions()
    opt.set_editor_property("format", unreal.DesiredImageFormat.PNG)
    opt.set_editor_property("overwrite_file", True)
    opt.set_editor_property("async_", False)

    faits, echecs = [], []
    for racine in racines:
        f = unreal.ARFilter(
            class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "Texture2D")],
            recursive_paths=True, package_paths=[racine])
        assets = ar.get_assets(f)
        unreal.log("[GODOT] {} : {} textures".format(racine, len(assets)))
        for n, a in enumerate(assets, 1):
            pkg = str(a.package_name)
            tex = unreal.EditorAssetLibrary.load_asset(pkg)
            if tex is None:
                echecs.append((pkg, "chargement"))
                continue
            dest = _chemin(pkg, ".png")
            # PAS `Texture.export_to_disk` : il ecrit les donnees de RENDU, donc
            # le mip resident le plus bas (mesure : 270 textures sur 274 sorties
            # en 32x32), et il refuse carrement les formats compresses BC
            # ("Unsupported texture format"). `AssetExportTask` passe par
            # TextureExporterPNG, qui lit l'art SOURCE : 512x512 pleine
            # resolution sur le meme asset.
            try:
                task = unreal.AssetExportTask()
                task.set_editor_property("object", tex)
                task.set_editor_property("filename", dest)
                task.set_editor_property("automated", True)
                task.set_editor_property("prompt", False)
                task.set_editor_property("replace_identical", True)
                unreal.Exporter.run_asset_export_task(task)
            except Exception as e:
                echecs.append((pkg, str(e)[:50]))
                continue
            if os.path.isfile(dest) and os.path.getsize(dest) > 0:
                faits.append(pkg)
            else:
                echecs.append((pkg, "fichier vide"))
            if n % 50 == 0:
                unreal.log("[GODOT]   {} / {}".format(n, len(assets)))
    unreal.log("[GODOT] textures : {} exportees, {} echecs".format(len(faits), len(echecs)))
    return {"faits": len(faits), "echecs": echecs}


def _nom_parametre(pv) -> str:
    """Le nom porte par un *ParameterValue, quelle que soit la version d'Unreal."""
    try:
        info = pv.get_editor_property("parameter_info")
        return str(info.get_editor_property("name"))
    except Exception:
        try:
            return str(pv.get_editor_property("parameter_name"))
        except Exception:
            return ""


def _params_du_materiau(mat) -> dict:
    """Ce que l'INSTANCE declare : textures, couleurs, scalaires.

    ⚠ ON NE PASSE PLUS PAR `get_used_textures()`. Cette fonction rend TOUT ce que
    le graphe touche, valeurs par defaut du materiau PARENT comprises. Sur un
    materiau maitre partage -- `M_MasterMat_TwoSided`, qui habille les 112 modeles
    de LowPolyForestVol2 -- elle rendait donc la meme liste pour tous : la terre
    `T_Dirt_basecolor` et la mire `T_Uvmap`. Cote Godot, bois morts et buissons
    sortaient noirs, peints avec de la terre. Mesure : RVB moyen 18,18,18.

    Ce qu'on lit maintenant, ce sont les SURCHARGES de l'instance : ce que ce
    materiau-la dit de lui-meme, et rien de ce qu'il herite sans le vouloir.

    ⚠ ET ON LIT AUSSI LES COULEURS, ce que l'ancienne version ne faisait pas du
    tout. C'etait le vrai trou : `T_Oak_Leaf.png` est un masque BLANC pur (RVB
    255,255,255), sa couleur vivant dans un parametre vectoriel de `MI_Leaf`.
    Sans ce parametre, le feuillage sort blanc dans Godot -- constate en rendu.
    """
    out = {"textures": [], "couleurs": {}, "couleurs_heritees": {}, "scalaires": {}}
    if mat is None:
        return out

    est_instance = isinstance(mat, unreal.MaterialInstanceConstant)
    if est_instance:
        try:
            for pv in mat.get_editor_property("texture_parameter_values") or []:
                t = pv.get_editor_property("parameter_value")
                if t is None:
                    continue
                nom = _nom_parametre(pv)
                pkg = t.get_path_name().split(".")[0]
                out["textures"].append({
                    "texture": pkg,
                    "png": os.path.relpath(_chemin(pkg, ".png"), DESTINATION).replace("\\", "/"),
                    "parametre": nom,
                    "role": _role(nom, t.get_name()),
                    "surcharge": True,
                })
        except Exception as e:
            unreal.log_warning("[GODOT] textures de {} : {}".format(mat.get_name(), e))

        for prop, cle in (("vector_parameter_values", "couleurs"),
                          ("scalar_parameter_values", "scalaires")):
            try:
                for pv in mat.get_editor_property(prop) or []:
                    nom = _nom_parametre(pv)
                    v = pv.get_editor_property("parameter_value")
                    if not nom:
                        continue
                    out[cle][nom] = [v.r, v.g, v.b, v.a] if cle == "couleurs" else float(v)
            except Exception as e:
                unreal.log_warning("[GODOT] {} de {} : {}".format(prop, mat.get_name(), e))

    # ⚠ LES COULEURS HERITEES COMPTENT AUTANT QUE LES SURCHARGES. Une instance qui
    # ne surcharge PAS sa couleur de base prend celle du parent, et Unreal la rend
    # ainsi. Ne lire que les surcharges laissait sept feuillages sans teinte --
    # bouleaux et fleurs -- dont le masque blanc serait sorti blanc dans Godot.
    # On resout donc tout parametre vectoriel declare par le parent, en marquant
    # a part ceux qui ne sont pas surcharges.
    if est_instance:
        try:
            parent = mat.get_editor_property("parent")
            mel = unreal.MaterialEditingLibrary
            for nom in mel.get_vector_parameter_names(parent) or []:
                n = str(nom)
                if n in out["couleurs"]:
                    continue
                v = mel.get_material_instance_vector_parameter_value(mat, nom)
                if v is not None:
                    out["couleurs_heritees"][n] = [v.r, v.g, v.b, v.a]
        except Exception as e:
            unreal.log_warning("[GODOT] couleurs heritees de {} : {}".format(mat.get_name(), e))

    # Repli : une instance sans aucune surcharge, ou un Material simple. On resout
    # alors chaque parametre declare par le parent -- ce qu'Unreal rend vraiment.
    if not out["textures"]:
        try:
            parent = mat.get_editor_property("parent") if est_instance else mat
            mel = unreal.MaterialEditingLibrary
            for nom in mel.get_texture_parameter_names(parent) or []:
                t = mel.get_material_instance_texture_parameter_value(mat, nom) if est_instance else None
                if t is None:
                    continue
                pkg = t.get_path_name().split(".")[0]
                out["textures"].append({
                    "texture": pkg,
                    "png": os.path.relpath(_chemin(pkg, ".png"), DESTINATION).replace("\\", "/"),
                    "parametre": str(nom),
                    "role": _role(str(nom), t.get_name()),
                    "surcharge": False,
                })
        except Exception:
            pass
    return out


def manifeste(racines=None) -> dict:
    """Maillage -> emplacements de materiau -> textures, avec leur role."""
    racines = racines or RACINES
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    entrees = []
    for racine in racines:
        f = unreal.ARFilter(
            class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "StaticMesh")],
            recursive_paths=True, package_paths=[racine])
        for a in ar.get_assets(f):
            pkg = str(a.package_name)
            mesh = unreal.EditorAssetLibrary.load_asset(pkg)
            if mesh is None:
                continue
            slots = []
            for idx, sm in enumerate(mesh.get_editor_property("static_materials")):
                mat = sm.get_editor_property("material_interface")
                p = _params_du_materiau(mat) if mat else {
                    "textures": [], "couleurs": {}, "couleurs_heritees": {}, "scalaires": {}}
                slots.append({
                    "emplacement": idx,
                    "nom": str(sm.get_editor_property("material_slot_name")),
                    "materiau": mat.get_path_name().split(".")[0] if mat else None,
                    "parent": (mat.get_editor_property("parent").get_path_name().split(".")[0]
                               if isinstance(mat, unreal.MaterialInstanceConstant)
                               and mat.get_editor_property("parent") else None),
                    "textures": p["textures"],
                    "couleurs": p["couleurs"],
                    "couleurs_heritees": p["couleurs_heritees"],
                    "scalaires": p["scalaires"],
                })
            entrees.append({
                "maillage": pkg,
                "glb": os.path.relpath(_chemin(pkg, ".glb"), DESTINATION).replace("\\", "/"),
                "emplacements": slots,
            })
    doc = {
        "_lire_moi": [
            "Export Unreal -> Godot. La GEOMETRIE est dans les .glb (echelle 0.01 :",
            "Unreal travaille en centimetres, glTF et Godot en metres).",
            "Les .glb ne portent PAS de textures : l'exportateur glTF d'Unreal ne",
            "sait pas cuire les materiaux sans moteur de rendu, et il fait tomber",
            "l'editeur quand on l'appelle depuis Python. Les textures sont donc",
            "fournies en PNG a cote, et ce manifeste dit laquelle va ou.",
            "",
            "Le champ 'role' est LU sur le nom du PARAMETRE de materiau quand il",
            "dit quelque chose ('BaseColor', 'Normal'...), et seulement a defaut",
            "devine sur un suffixe ANCRE en fin de nom d'asset.",
            "",
            "'textures' ne liste que les SURCHARGES de l'instance ('surcharge':",
            "true), pas ce que son materiau parent traine. 'couleurs' et",
            "'scalaires' portent ses parametres vectoriels et scalaires : c'est la",
            "que vit la teinte des feuillages dont l'albedo n'est qu'un masque",
            "blanc. Sans eux, ces feuillages sortent BLANCS dans Godot.",
        ],
        "racines": racines,
        "maillages": entrees,
    }
    chemin = os.path.join(DESTINATION, "manifeste.json")
    with open(chemin, "w", encoding="utf-8") as fh:
        json.dump(doc, fh, indent=2, ensure_ascii=False)
    unreal.log("[GODOT] manifeste : {} maillages -> {}".format(len(entrees), chemin))
    return {"maillages": len(entrees), "fichier": chemin}


def tout() -> dict:
    t = exporter_textures()
    m = manifeste()
    return {"textures": t["faits"], "echecs_textures": len(t["echecs"]),
            "maillages_au_manifeste": m["maillages"]}
