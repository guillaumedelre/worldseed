"""Importe un monde genere par Tools/WorldGen dans un niveau World Partition.

A executer DEPUIS l'editeur :

    import sys, importlib
    sys.path.insert(0, r"D:\\UE\\Worldseed\\Tools\\UE")
    import import_world; importlib.reload(import_world)
    print(import_world.build(r"D:\\UE\\Worldseed\\Saved\\WorldGen\\20260909"))

Le script est idempotent : il verifie l'existence avant de creer, et journalise
chaque operation avec un prefixe CREATED / MODIFIED / SKIPPED pour qu'un echec en
cours de route puisse etre repris ou annule a la main.
"""

from __future__ import annotations

import json
import os

import unreal

DEFAULT_MAP = "/Game/Worldseed/Maps/L_Worldseed"
DEFAULT_LAYERINFO_DIR = "/Game/Worldseed/LayerInfo"
LANDSCAPE_LABEL = "Worldseed_Landscape"

# Le bundle Orasot livre deja neuf des dix LayerInfo, avec exactement les noms de
# couche attendus par son materiau maitre (espaces compris). On les reutilise.
LAYERINFO_SEARCH_DIRS = [
    "/Game/Worldseed/LayerInfo",
    "/Game/Orasot_Bundle/Maps/5_Bioms_ShaderAssets",
]

_log: list[str] = []


def log(kind: str, message: str) -> None:
    line = "{}: {}".format(kind, message)
    _log.append(line)
    unreal.log(line)
    print(line)


# ---------------------------------------------------------------------- niveau


def ensure_level(map_path: str = DEFAULT_MAP, force_new: bool = False) -> bool:
    """Cree (ou ouvre) un niveau World Partition."""
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    exists = unreal.EditorAssetLibrary.does_asset_exist(map_path)

    if exists and not force_new:
        if not les.load_level(map_path):
            log("ERROR", "impossible d'ouvrir {}".format(map_path))
            return False
        log("SKIPPED", "niveau deja present, ouvert : {}".format(map_path))
        return True

    if not les.new_level(map_path, True):        # True = World Partition
        log("ERROR", "echec de creation du niveau {}".format(map_path))
        return False
    log("CREATED", "niveau World Partition {}".format(map_path))
    return True


def world_partition_active() -> bool:
    """Le niveau courant est-il bien partitionne ?

    create_landscape refuse un world_partition_grid_size non nul hors WP, et le
    message d'erreur est laconique : autant verifier avant.
    """
    try:
        bounds = unreal.WorldPartitionBlueprintLibrary.get_editor_world_bounds()
        return bounds is not None
    except Exception as exc:
        log("WARN", "World Partition non detecte : {}".format(exc))
        return False


# -------------------------------------------------------------------- landscape


def create_landscape(manifest: dict, label: str = LANDSCAPE_LABEL) -> str | None:
    """Cree le Landscape aux dimensions exactes du manifeste."""
    cfg = manifest["landscape"]
    loc = cfg["actorLocation"]
    scale = cfg["scale"]

    if unreal.LandscapeService.landscape_exists(label):
        log("SKIPPED", "Landscape {} deja present".format(label))
        return label

    grid = int(cfg.get("worldPartitionGridSize", 0))
    if grid and not world_partition_active():
        log("ERROR", "grille World Partition demandee mais le niveau n'est pas partitionne")
        return None

    result = unreal.LandscapeService.create_landscape(
        unreal.Vector(loc["x"], loc["y"], loc["z"]),
        unreal.Rotator(0.0, 0.0, 0.0),
        unreal.Vector(scale["x"], scale["y"], scale["z"]),
        int(cfg["sections_per_component"]),
        int(cfg["quads_per_section"]),
        int(cfg["component_count_x"]),
        int(cfg["component_count_y"]),
        label,
        grid,
    )
    if not result.success:
        log("ERROR", "create_landscape : {}".format(result.error_message))
        return None

    log("CREATED", "Landscape {} ({}x{} composants de {} quads, grille WP {})".format(
        result.actor_label, cfg["component_count_x"], cfg["component_count_y"],
        cfg["quads_per_section"], grid))
    return result.actor_label


def create_landscape_with_layers(manifest: dict, out_dir: str,
                                 label: str = LANDSCAPE_LABEL,
                                 layer_infos: dict | None = None) -> str | None:
    """Cree le Landscape ET importe relief + couches en UNE seule operation.

    Ajouter les couches une par une declenche a chaque fois une fusion complete
    des edit layers sur tous les composants ; ici il n'y en a qu'une.

    ATTENTION, ce n'est PAS ce qui evite le crash D3D12 (mesure) : une fusion
    unique d'un Landscape de 1024 composants avec dix couches depasse a elle
    seule le plafond de residency sets du moteur. Ce qui marche, c'est de
    limiter chaque Landscape a 256 composants (16x16 de 254 quads) et de tuiler
    le monde : voir Tools/WorldGen/tile_world.py. Une tuile 4065x4065 avec ses
    dix couches s'importe en ~35 s.
    """
    cfg = manifest["landscape"]
    loc, scale = cfg["actorLocation"], cfg["scale"]
    names = manifest["layers"]

    if unreal.LandscapeService.landscape_exists(label):
        log("SKIPPED", "Landscape {} deja present".format(label))
        return label

    grid = int(cfg.get("worldPartitionGridSize", 0))
    if grid and not world_partition_active():
        log("ERROR", "grille World Partition demandee mais le niveau n'est pas partitionne")
        return None

    infos = layer_infos if layer_infos is not None else ensure_layer_infos(names)
    missing = [n for n in names if not infos.get(n)]
    if missing:
        log("ERROR", "LayerInfo manquants, import annule : {}".format(missing))
        return None

    heightmap = os.path.join(out_dir, "height_16bit.png")
    weightmaps = [os.path.join(out_dir, f) for f in manifest["layerFiles"]]
    for path in [heightmap] + weightmaps:
        if not os.path.isfile(path):
            log("ERROR", "fichier absent : {}".format(path))
            return None

    result = unreal.LandscapeService.create_landscape_from_files(
        unreal.Vector(loc["x"], loc["y"], loc["z"]),
        unreal.Rotator(0.0, 0.0, 0.0),
        unreal.Vector(scale["x"], scale["y"], scale["z"]),
        int(cfg["sections_per_component"]),
        int(cfg["quads_per_section"]),
        int(cfg["component_count_x"]),
        int(cfg["component_count_y"]),
        label,
        grid,
        heightmap,
        list(names),
        [infos[n] for n in names],
        weightmaps,
    )
    if not result.success:
        log("ERROR", "create_landscape_from_files : {}".format(result.error_message))
        return None

    log("CREATED", "Landscape {} ({}x{} composants, {} couches, relief et poids importes "
                   "en une passe)".format(result.actor_label, cfg["component_count_x"],
                                          cfg["component_count_y"], len(names)))
    return result.actor_label


def import_heightmap(label: str, out_dir: str) -> bool:
    path = os.path.join(out_dir, "height_16bit.png")
    if not os.path.isfile(path):
        log("ERROR", "heightmap introuvable : {}".format(path))
        return False
    result = unreal.LandscapeService.import_heightmap(label, path)
    if not result.success:
        log("ERROR", "import_heightmap : {}".format(result.error_message))
        return False
    log("MODIFIED", "heightmap importee ({})".format(result.resolution))
    return True


# ----------------------------------------------------------------------- couches


def find_layer_info(layer_name: str, directories: list[str]) -> str | None:
    """Cherche un LandscapeLayerInfoObject dont le NOM DE COUCHE vaut layer_name.

    Le nom de l'asset ne suit aucune convention fiable (le bundle Orasot ecrit
    'Grass_Biom_4_LayerInfo', VibeUE ecrit 'LI_Snow') : seul le champ LayerName
    fait foi, car c'est lui que le materiau et import_weight_map comparent.
    """
    for directory in directories:
        if not unreal.EditorAssetLibrary.does_directory_exist(directory):
            continue
        for path in unreal.EditorAssetLibrary.list_assets(directory, True, False):
            obj = unreal.EditorAssetLibrary.load_asset(path)
            if not isinstance(obj, unreal.LandscapeLayerInfoObject):
                continue
            if str(obj.get_editor_property("layer_name")) == layer_name:
                return path.split(".")[0]
    return None


def ensure_layer_infos(layer_names: list[str],
                       directory: str = DEFAULT_LAYERINFO_DIR) -> dict:
    """Associe a chaque couche un LandscapeLayerInfoObject, en reutilisant l'existant.

    On cherche d'abord un asset deja present (dossier du projet, puis celui du
    bundle Orasot qui livre neuf des dix couches). A defaut on en cree un.

    LIMITE CONNUE : le service derive le nom d'asset du nom de couche
    ('LI_<nom>') et un nom de couche contenant des espaces produit alors un
    chemin de paquet invalide ("too many spaces"). Or LayerName est
    VisibleAnywhere, donc en lecture seule depuis Python : impossible de creer
    l'asset sous un nom assaini puis de le renommer. Les couches a espaces
    doivent donc exister au prealable — c'est le cas des neuf du bundle.
    """
    search = [directory] + [d for d in LAYERINFO_SEARCH_DIRS if d != directory]
    out = {}
    for name in layer_names:
        found = find_layer_info(name, search)
        if found:
            out[name] = found
            log("SKIPPED", "LayerInfo reutilise pour '{}' : {}".format(name, found))
            continue
        if " " in name:
            log("ERROR", "LayerInfo '{}' absent et increable : un nom de couche a "
                         "espaces donne un chemin de paquet invalide".format(name))
            continue
        result = unreal.LandscapeMaterialService.create_layer_info_object(
            name, directory, True)
        if not result.success:
            log("ERROR", "LayerInfo '{}' : {}".format(name, result.error_message))
            continue
        out[name] = result.asset_path
        log("CREATED", "LayerInfo {} -> {}".format(name, result.asset_path))
    return out


def import_weightmaps(label: str, out_dir: str, manifest: dict,
                      layer_infos: dict, only: str | None = None) -> int:
    """Importe une weightmap par couche. Retourne le nombre de couches reussies.

    `only` limite l'appel a une seule couche. Enchainer les dix d'affilee a fait
    tomber le RHI en D3D12 ("No command list slot was available. Too many
    residency sets are open concurrently") : chaque add_layer touche les textures
    de poids de TOUS les composants, et la limite atteinte est un nombre de
    listes de commandes concurrentes, pas une quantite de VRAM. Une couche par
    appel laisse le thread de rendu vider sa file entre deux.
    """
    names = manifest["layers"]
    files = manifest["layerFiles"]
    done = 0
    for name, filename in zip(names, files):
        if only is not None and name != only:
            continue
        asset = layer_infos.get(name)
        if not asset:
            continue
        if not unreal.LandscapeService.layer_exists(label, name):
            if not unreal.LandscapeService.add_layer(label, asset):
                log("ERROR", "add_layer a echoue pour '{}'".format(name))
                continue
            log("MODIFIED", "couche '{}' ajoutee au Landscape".format(name))
        path = os.path.join(out_dir, filename)
        if not os.path.isfile(path):
            log("ERROR", "weightmap manquante : {}".format(path))
            continue
        result = unreal.LandscapeService.import_weight_map(label, name, path)
        if not result.success:
            log("ERROR", "import_weight_map '{}' : {}".format(name, result.error_message))
            continue
        log("MODIFIED", "weightmap '{}' importee ({} sommets)".format(
            name, result.vertices_modified))
        done += 1
    return done


# ------------------------------------------------------------------ verification


def verify(label: str, manifest: dict) -> dict:
    """Relit l'asset dans l'editeur : un appel qui reussit n'est pas une preuve."""
    info = unreal.LandscapeService.get_landscape_info(label)
    analysis = unreal.LandscapeService.analyze_terrain(label, 0.0, 0.0, 0.0)
    layers = unreal.LandscapeService.list_layers(label)

    expected = manifest["world"]
    report = {
        "landscape": label,
        "couches": [str(l.layer_name) for l in layers],
        "altitudeAttendueM": [expected["minElevationM"], expected["maxElevationM"]],
    }
    for field in ("min_height", "max_height", "average_slope", "roughness"):
        if hasattr(analysis, field):
            report[field] = round(float(getattr(analysis, field)), 2)
    if info is not None:
        for field in ("component_count_x", "component_count_y", "quads_per_section",
                      "sections_per_component", "actor_label"):
            if hasattr(info, field):
                report[field] = getattr(info, field)
    return report


# ------------------------------------------------------------------------ point


def build(out_dir: str, map_path: str = DEFAULT_MAP, with_layers: bool = True,
          force_new_level: bool = False, save: bool = True) -> dict:
    """Chaine complete : niveau -> Landscape -> heightmap -> couches -> sauvegarde."""
    _log.clear()
    manifest_path = os.path.join(out_dir, "manifest.json")
    if not os.path.isfile(manifest_path):
        return {"success": False, "error": "manifeste introuvable : " + manifest_path}
    with open(manifest_path, "r", encoding="utf-8") as fh:
        manifest = json.load(fh)

    if not ensure_level(map_path, force_new_level):
        return {"success": False, "error": "niveau", "log": list(_log)}

    label = create_landscape(manifest)
    if label is None:
        return {"success": False, "error": "landscape", "log": list(_log)}

    if not import_heightmap(label, out_dir):
        return {"success": False, "error": "heightmap", "log": list(_log)}

    layers_done = 0
    if with_layers:
        infos = ensure_layer_infos(manifest["layers"])
        layers_done = import_weightmaps(label, out_dir, manifest, infos)

    if save:
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
        unreal.EditorAssetLibrary.save_directory("/Game/Worldseed", False, True)
        log("MODIFIED", "niveau et assets sauvegardes")

    return {
        "success": True,
        "map": map_path,
        "landscape": label,
        "couchesImportees": layers_done,
        "verification": verify(label, manifest),
        "log": list(_log),
    }
