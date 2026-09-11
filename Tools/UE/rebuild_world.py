"""Reconstruit TOUT le monde Worldseed dans le niveau, a partir d'une sortie du generateur.

A executer DEPUIS l'editeur :

    import sys, importlib
    sys.path.insert(0, r"D:\\UE\\Worldseed\\Tools\\UE")
    import rebuild_world; importlib.reload(rebuild_world)
    print(rebuild_world.rebuild(r"D:\\UE\\Worldseed\\Saved\\WorldGen\\20260909"))

POURQUOI CE FICHIER EXISTE. La sequence complete -- vider, importer le relief,
poser le materiau, refaire l'eau, la carte des biomes, la vegetation, le point
d'apparition -- tenait dans la tete de la session qui l'avait faite. Elle a du
etre retrouvee entierement au monde suivant. Elle est ici, dans l'ordre, avec
les raisons.

L'ORDRE COMPTE, et chaque etape a coute quelque chose :

1. `clear()` avant tout. Un Landscape ne se reimporte pas par-dessus lui-meme :
   `create_landscape_with_layers` saute si le label existe deja.
2. Couper `PCGWorldActor.treat_editor_viewport_as_generation_source` AVANT de
   charger le moindre volume PCG, sinon l'editeur se met a semer autour de la
   camera pendant le menage.
3. Repasser tout composant PCG en `GenerateOnDemand` AVANT d'y toucher :
   `UPCGComponent::OnRefresh` commence par `check(!IsManagedByRuntimeGenSystem())`
   et fait tomber l'editeur au tick SUIVANT.
4. Le materiau s'assigne APRES l'import des poids : `import_world.py` ne le fait
   pas, et un Landscape sans materiau est blanc.
5. L'ocean AVANT la WaterZone : c'est le premier corps d'eau qui fait naitre la
   zone, on ne peut pas la creer soi-meme.
6. Le `PCGWorldActor` se pose a la main, et desarme par defaut (voir
   `pcg_world_actor()`).
"""

from __future__ import annotations

import json
import os

import unreal

import import_world
import water_world
import pcg_bench
import vegetation

MAP = "/Game/Worldseed/Maps/L_Worldseed"
LANDSCAPE_MATERIAL = "/Game/Worldseed/Materials/M_WorldseedLandscape"
TUILE = "x0_y0"

_log: list[str] = []


def log(kind: str, message: str) -> None:
    line = "{}: {}".format(kind, message)
    _log.append(line)
    unreal.log("WORLDSEED_REBUILD " + line)
    print(line)


def _acteurs():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


# --------------------------------------------------------------------- menage


def clear() -> dict:
    """Vide le niveau de tout ce qui decrit l'ancien monde.

    Garde l'eclairage, le PlayerStart, le WorldDataLayers et la minimap.
    """
    sub = _acteurs()

    # 1. Couper la source de generation en editeur AVANT de charger les volumes.
    for a in sub.get_all_level_actors():
        if isinstance(a, unreal.PCGWorldActor):
            a.set_editor_property("treat_editor_viewport_as_generation_source", False)

    # 2. Charger ce qu'il faut detruire : les acteurs World Partition non charges
    #    sont invisibles pour get_all_level_actors, et un acteur non charge
    #    ressemble a un acteur inexistant.
    descs = unreal.WorldPartitionBlueprintLibrary.get_actor_descs()
    a_charger = [d for d in descs if any(k in str(d.native_class)
                                         for k in ("PCGVolume", "WaterBody", "Landscape"))
                 or str(d.label).startswith(("Worldseed_BiomeTex", "Worldseed_BiomeCore",
                                             "Worldseed_GenSource", "Worldseed_Vegetation"))]
    if a_charger:
        unreal.WorldPartitionBlueprintLibrary.load_actors([d.guid for d in a_charger])

    # 3. Desarmer les composants PCG avant d'y toucher.
    demande = unreal.PCGComponentGenerationTrigger.GENERATE_ON_DEMAND
    execution = unreal.PCGComponentGenerationTrigger.GENERATE_AT_RUNTIME
    for a in sub.get_all_level_actors():
        comp = a.get_component_by_class(unreal.PCGComponent)
        if comp is not None and comp.get_editor_property("generation_trigger") == execution:
            comp.set_editor_property("generation_trigger", demande)

    # 4. Detruire.
    jetables = []
    for a in sub.get_all_level_actors():
        nom = a.get_actor_label()
        if (isinstance(a, (unreal.PCGVolume, unreal.PCGWorldActor, unreal.WaterBody,
                           unreal.WaterZone, unreal.Landscape,
                           unreal.LandscapeStreamingProxy))
                or type(a).__name__ == "WaterBrushManager"
                or nom.startswith(("Worldseed_BiomeTex", "Worldseed_BiomeCore",
                                   "Worldseed_GenSource"))):
            comp = a.get_component_by_class(unreal.PCGComponent)
            if comp is not None:
                comp.cleanup(True)
            jetables.append(a)

    from collections import Counter
    compte = Counter(type(a).__name__ for a in jetables)
    sub.destroy_actors(jetables)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("DELETED", "{} acteurs : {}".format(len(jetables), dict(compte)))
    return dict(compte)


# ------------------------------------------------------------------- landscape


def landscape(out_dir: str) -> str | None:
    """Importe le relief et les dix couches en UNE passe, puis pose le materiau."""
    chemin = os.path.join(out_dir, "tiles", TUILE)
    with open(os.path.join(chemin, "manifest.json"), "r", encoding="utf-8") as fh:
        m = json.load(fh)
    label = import_world.create_landscape_with_layers(m, chemin, m["tile"]["label"])
    if label is None:
        return None

    # Le materiau n'est PAS pose par import_world : sans lui, terrain blanc.
    mat = unreal.EditorAssetLibrary.load_asset(LANDSCAPE_MATERIAL)
    if mat is None:
        log("ERROR", "materiau introuvable : {}".format(LANDSCAPE_MATERIAL))
        return label
    for a in _acteurs().get_all_level_actors():
        if isinstance(a, unreal.Landscape) and a.get_actor_label() == label:
            a.set_editor_property("landscape_material", mat)
            log("MODIFIED", "{} : materiau {}".format(label, LANDSCAPE_MATERIAL))
    return label


# ------------------------------------------------------------- PCGWorldActor


def pcg_world_actor(viewport_source: bool = False):
    """Pose le PCGWorldActor et l'ARME. Un acteur neuf ne genere rien.

    Par defaut il arrive avec `enable_world_partition_generation_sources = False`
    et un cache de paysage en `NeverSerialize` : en PIE, aucune instance et pas
    une ligne `LogPCG` dans le journal.

    `AlwaysSerialize` est OBLIGATOIRE pour que la projection sur le Landscape
    marche en generation a l'execution -- et il a un prix : le cache est ecrit
    dans le paquet d'acteur externe de ce PCGWorldActor. Mesure : 2,6 Go pour
    1024 composants, donc de l'ordre de 650 Mo pour 256.
    """
    sub = _acteurs()
    deja = [a for a in sub.get_all_level_actors() if isinstance(a, unreal.PCGWorldActor)]
    a = deja[0] if deja else sub.spawn_actor_from_class(unreal.PCGWorldActor,
                                                        unreal.Vector(0, 0, 0))
    if a is None:
        log("ERROR", "impossible de poser le PCGWorldActor")
        return None
    a.set_actor_label("PCGWorldActor")
    a.set_editor_property("enable_world_partition_generation_sources", True)
    a.set_editor_property("treat_editor_viewport_as_generation_source", viewport_source)
    cache = a.get_editor_property("landscape_cache_object")
    cache.set_editor_property(
        "serialization_mode", unreal.PCGLandscapeCacheSerializationMode.ALWAYS_SERIALIZE)
    log("MODIFIED", "PCGWorldActor arme (sources WP oui, viewport {}, cache serialise)".format(
        "oui" if viewport_source else "non"))
    return a


# ---------------------------------------------------------------- apparition


def player_start(out_dir: str):
    """Pose le point d'apparition d'apres `spawn_point.json`.

    LE CALCUL N'EST PAS ICI. Le Python embarque d'Unreal n'a ni numpy, ni PIL,
    ni scipy : l'analyse de la carte des biomes se fait du cote du generateur,
    par `Tools/WorldGen/spawn_point.py`, qui ecrit ce JSON. Ici on ne fait que
    le lire. Lancer :

        python spawn_point.py [dossier] [biome]

    Sans PlayerStart, le PIE fait apparaitre le pion a l'origine du monde --
    en pleine mer -- et il tombe.
    """
    chemin = os.path.join(out_dir, "spawn_point.json")
    if not os.path.isfile(chemin):
        log("ERROR", "spawn_point.json absent : lancer Tools/WorldGen/spawn_point.py")
        return None
    with open(chemin, "r", encoding="utf-8") as fh:
        p = json.load(fh)
    w = p["monde_cm"]

    sub = _acteurs()
    ps = [a for a in sub.get_all_level_actors() if isinstance(a, unreal.PlayerStart)]
    if not ps:
        a = sub.spawn_actor_from_class(
            unreal.PlayerStart, unreal.Vector(w["x"], w["y"], w["z"]))
    else:
        a = ps[0]
        a.set_actor_location(unreal.Vector(w["x"], w["y"], w["z"]), False, True)
    a.set_actor_label("Worldseed_PlayerStart")
    log("MODIFIED", "PlayerStart en {} : X={x:.0f} Y={y:.0f} Z={z:.0f}, alt {alt} m, "
                    "pente {pente} deg, eau a {eau} m".format(
                        p["biomeLabel"], x=w["x"], y=w["y"], z=w["z"],
                        alt=p["altitudeM"], pente=p["penteDeg"], eau=p["distanceEauM"]))
    return a


# ----------------------------------------------------------------- la chaine


def rebuild(out_dir: str, with_vegetation: bool = True) -> dict:
    """Vide puis refait tout le monde. Compter environ une minute."""
    _log.clear()
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if les.is_in_play_in_editor():
        les.editor_request_end_play()      # load_level echoue silencieusement sous PIE

    clear()
    label = landscape(out_dir)
    if label is None:
        return {"success": False, "etape": "landscape", "log": list(_log)}

    eau = water_world.build(out_dir)
    log("CREATED", "eau : {} lacs, {} rivieres".format(eau["lacs"], eau["rivieres"]))

    pcg_bench.import_world_biome_tiles(tiles=[TUILE], force=True)
    pcg_world_actor()
    if with_vegetation:
        vegetation.build_world(tiles=[TUILE])

    player_start(out_dir)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    unreal.EditorAssetLibrary.save_directory("/Game/Worldseed", False, True)
    log("MODIFIED", "niveau et assets sauvegardes")

    return {"success": True, "landscape": label, "eau": eau,
            "verification": water_world.verify(), "log": list(_log)}
