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

# Roles devinés d'après le nom du paramètre de texture ou de l'asset. Ce n'est
# qu'une aide au recablage : Godot n'a aucun moyen de deviner seul qu'une texture
# nommee "T_Bark_N" est une normale.
INDICES_ROLE = (
    ("normal", ("normal", "_n", "norm")),
    ("roughness", ("rough", "_r", "_orm", "arm")),
    ("metallic", ("metal", "_m")),
    ("ambient_occlusion", ("_ao", "occlusion")),
    ("emissive", ("emiss", "_e")),
    ("opacity", ("opacity", "alpha", "mask")),
    ("base_color", ("basecolor", "albedo", "diffuse", "_d", "_bc", "color", "colour")),
)


def _role(nom: str) -> str:
    bas = nom.lower()
    for role, indices in INDICES_ROLE:
        if any(i in bas for i in indices):
            return role
    return "inconnu"


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


def _textures_du_materiau(mat) -> list:
    """Textures reellement utilisees par un materiau ou une instance."""
    out = []
    try:
        used = unreal.MaterialEditingLibrary.get_used_textures(mat) \
            if hasattr(unreal.MaterialEditingLibrary, "get_used_textures") else []
    except Exception:
        used = []
    if not used:
        # Repli : les parametres de texture de l'instance et de son parent.
        try:
            noms = unreal.MaterialEditingLibrary.get_texture_parameter_names(
                mat.get_editor_property("parent") if isinstance(mat, unreal.MaterialInstanceConstant) else mat)
            for nom in noms:
                t = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(mat, nom) \
                    if isinstance(mat, unreal.MaterialInstanceConstant) else None
                if t:
                    used.append(t)
        except Exception:
            pass
    for t in used:
        if t is None:
            continue
        pkg = t.get_path_name().split(".")[0]
        out.append({"texture": pkg,
                    "png": os.path.relpath(_chemin(pkg, ".png"), DESTINATION).replace("\\", "/"),
                    "role": _role(t.get_name())})
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
                slots.append({
                    "emplacement": idx,
                    "nom": str(sm.get_editor_property("material_slot_name")),
                    "materiau": mat.get_path_name().split(".")[0] if mat else None,
                    "textures": _textures_du_materiau(mat) if mat else [],
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
            "Le champ 'role' est DEVINE d'apres le nom : a verifier a l'oeil.",
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
