"""Exporte des dossiers de Content vers glTF 2.0, pour Godot.

A lancer dans une instance UNREAL SEPAREE, pas dans l'editeur de travail :

    "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" ^
        D:\\UE\\Worldseed\\Worldseed.uproject ^
        -run=pythonscript -script="D:\\UE\\Worldseed\\Tools\\UE\\export_godot.py" ^
        -unattended -nosplash

POURQUOI SEPAREE : le premier essai depuis l'editeur interactif a fait tomber le
processus sur `Assertion failed: IsValid()` (SharedPointer.h:1133), en instanciant
l'exportateur a la main (`task.exporter = unreal.GLTFStaticMeshExporter()`). On
laisse desormais Unreal choisir l'exportateur d'apres l'extension du fichier, et
on isole le traitement pour qu'un plantage ne coute pas la session de travail.

CHOIX DE FORMAT. glTF 2.0 binaire (.glb) est ce que Godot 4 importe le mieux :
un fichier par maillage, textures embarquees, materiaux PBR standards. Deux
reglages comptent :
  - `export_uniform_scale = 0.01` : Unreal travaille en CENTIMETRES, glTF et
    Godot en METRES. Sans cela tout arrive cent fois trop grand.
  - `bake_material_inputs = SIMPLE` : les materiaux Unreal ne se traduisent pas,
    on cuit donc leurs entrees en textures PBR que Godot sait lire.

Les textures sources sont exportees a cote en PNG, pour pouvoir reconstruire des
materiaux plus fins dans Godot que ce que la cuisson donne.
"""

from __future__ import annotations

import os
import time

import unreal

RACINES = ["/Game/Stylized_PBR_Nature", "/Game/Orasot_Bundle"]
DESTINATION = r"D:\UE\Worldseed\Export\Godot"
TAILLE_CUISSON = 1024


def _journal(msg: str) -> None:
    unreal.log("[GODOT] " + msg)
    print("[GODOT] " + msg)


def _chemin_disque(package: str, racine_pkg: str, extension: str) -> str:
    """/Game/Orasot_Bundle/A/B/SM_X -> <DESTINATION>/Orasot_Bundle/A/B/SM_X.<ext>"""
    relatif = package[len("/Game/"):]
    chemin = os.path.join(DESTINATION, relatif.replace("/", os.sep) + extension)
    os.makedirs(os.path.dirname(chemin), exist_ok=True)
    return chemin


def options_gltf() -> "unreal.GLTFExportOptions":
    o = unreal.GLTFExportOptions()
    o.set_editor_property("export_uniform_scale", 0.01)   # cm -> m
    o.set_editor_property("bake_material_inputs", unreal.GLTFMaterialBakeMode.SIMPLE)
    # La taille de cuisson porte un type dedie selon les versions : on ne la
    # pose que si on sait la construire, plutot que d'ecrire None dedans.
    try:
        taille = o.get_editor_property("default_material_bake_size")
        taille.set_editor_property("x", TAILLE_CUISSON)
        taille.set_editor_property("y", TAILLE_CUISSON)
        o.set_editor_property("default_material_bake_size", taille)
    except Exception as e:
        _journal("taille de cuisson laissee par defaut ({})".format(str(e)[:60]))
    o.set_editor_property("export_vertex_colors", True)
    o.set_editor_property("export_preview_mesh", False)
    return o


def exporter_maillages(racines=None) -> dict:
    racines = racines or RACINES
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    opts = options_gltf()
    faits, echecs = 0, []
    for racine in racines:
        f = unreal.ARFilter(
            class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "StaticMesh")],
            recursive_paths=True, package_paths=[racine])
        assets = ar.get_assets(f)
        _journal("{} : {} maillages".format(racine, len(assets)))
        for n, a in enumerate(assets, 1):
            pkg = str(a.package_name)
            mesh = unreal.EditorAssetLibrary.load_asset(pkg)
            if mesh is None:
                echecs.append((pkg, "chargement"))
                continue
            dest = _chemin_disque(pkg, racine, ".glb")
            task = unreal.AssetExportTask()
            task.set_editor_property("object", mesh)
            task.set_editor_property("filename", dest)
            task.set_editor_property("options", opts)
            task.set_editor_property("automated", True)
            task.set_editor_property("prompt", False)
            task.set_editor_property("replace_identical", True)
            # On NE fixe PAS `exporter` : Unreal le resout par l'extension. Le
            # fixer a la main fait tomber le processus (voir l'en-tete).
            try:
                ok = unreal.Exporter.run_asset_export_task(task)
            except Exception as e:
                ok = False
                echecs.append((pkg, str(e)[:60]))
            if ok and os.path.isfile(dest):
                faits += 1
            else:
                echecs.append((pkg, "export"))
            if n % 25 == 0:
                _journal("  {} / {}".format(n, len(assets)))
    _journal("maillages exportes : {} ; echecs : {}".format(faits, len(echecs)))
    for pkg, raison in echecs[:15]:
        _journal("  ECHEC {} ({})".format(pkg, raison))
    return {"faits": faits, "echecs": echecs}


def exporter_textures(racines=None) -> dict:
    """Textures sources en PNG, a cote des .glb."""
    racines = racines or RACINES
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    opt = unreal.ImageWriteOptions()
    opt.set_editor_property("format", unreal.DesiredImageFormat.PNG)
    opt.set_editor_property("overwrite_file", True)
    opt.set_editor_property("async_", False)
    faits, echecs = 0, 0
    for racine in racines:
        f = unreal.ARFilter(
            class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "Texture2D")],
            recursive_paths=True, package_paths=[racine])
        assets = ar.get_assets(f)
        _journal("{} : {} textures".format(racine, len(assets)))
        for n, a in enumerate(assets, 1):
            pkg = str(a.package_name)
            tex = unreal.EditorAssetLibrary.load_asset(pkg)
            if tex is None:
                echecs += 1
                continue
            dest = _chemin_disque(pkg, racine, ".png")
            try:
                tex.export_to_disk(dest, opt)
                faits += 1 if os.path.isfile(dest) else 0
            except Exception:
                echecs += 1
            if n % 50 == 0:
                _journal("  {} / {}".format(n, len(assets)))
    _journal("textures exportees : {} ; echecs : {}".format(faits, echecs))
    return {"faits": faits, "echecs": echecs}


def main() -> None:
    t0 = time.time()
    os.makedirs(DESTINATION, exist_ok=True)
    _journal("destination : " + DESTINATION)
    m = exporter_maillages()
    t = exporter_textures()
    _journal("TERMINE en {:.0f} s : {} maillages, {} textures".format(
        time.time() - t0, m["faits"], t["faits"]))


main()
