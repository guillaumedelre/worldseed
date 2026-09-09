"""Banc d'essai PCG : la carte des biomes comme source de peuplement.

A executer DEPUIS l'editeur :

    import sys, importlib
    sys.path.insert(0, r"D:\\UE\\Worldseed\\Tools\\UE")
    import pcg_bench; importlib.reload(pcg_bench)
    print(pcg_bench.step1_texture())     # import + verification pixel a pixel

CE QU'ON CHERCHE A SAVOIR. Le PCG doit peupler le monde a partir des 19 biomes,
pas des 10 couches de peinture : celles-ci confondent des biomes (la plage et le
desert chaud ont toutes deux DesertSand pour couche dominante et sont
indistinguables en jeu). L'identite du biome vit dans biome_index.png, un
tableau d'IDENTIFIANTS. La question du banc est donc : Unreal sait-il rendre ces
identifiants EXACTEMENT, sans en inventer sur les frontieres ?

DEUX FILTRAGES INDEPENDANTS, ET C'EST LA QUE CA SE JOUE :

  1. celui de la TEXTURE (TextureFilter) : TF_NEAREST. Sert au materiau et aux
     chemins GPU.
  2. celui de PCG (EPCGTextureFilter, sur le noeud Get Texture Data) : il vaut
     **Bilinear par defaut** (PCGCommon.h, et tous les noeuds du plugin). Il est
     TOTALEMENT independant du premier : poser TF_NEAREST sur la texture ne
     l'empeche pas. En Bilinear, un point entre Ocean (0) et Roche nue (16) rend
     une valeur intermediaire, donc un biome qui n'existe pas la, silencieusement.

Les trois autres reglages (NoMipmaps, TC_VectorDisplacementmap, sRGB off) sont
exactement ceux que PCG se force a lui-meme dans PCGTextureData.cpp : sans eux
il DUPLIQUE la texture en memoire pour se les donner. Sur les tuiles 4065 du
monde complet, cela ferait 66 Mo dupliques par tuile.

La verification ne se contente pas d'un import reussi. Etape 1 : la texture est
reexportee par export_to_disk et comparee au PNG source **texel par texel**
(262144, pas un sondage) via export_biome_texture.py --verify-roundtrip.
Etape 2 : le graphe echantillonne la texture et on compare les identifiants lus
au PNG, avec 400 points temoins dont 300 pris sur une frontiere de biome.

Resultats mesures le 9 septembre 2026, fenetre de 2 km, 10 biomes presents :
  - import                   : 0 ecart sur 262144 texels
  - PCG en Point             : 0 ecart sur 262144 texels ; 400/400 temoins exacts
  - PCG en Bilinear, grille de semis a 1500 cm (le cas reel, non aligne sur les
    texels de 393,7 cm) : 307 points sur 17956 faux, soit 1,71 %, du type
    plage lu comme alpin, roche_nue lu comme foret_tropicale_humide.
Attention en rejouant le contre-test : sur une grille alignee sur les centres de
texels, le bilineaire degenere en exact et ne montre AUCUN ecart. Il faut un pas
qui ne soit ni multiple ni diviseur du texel pour que le defaut apparaisse.
"""

from __future__ import annotations

import json
import os

import unreal

BENCH_DIR = r"D:\UE\Worldseed\Saved\WorldGen\20260909\bench"
DEST_PACKAGE = "/Game/Worldseed/PCG"
TEXTURE_NAME = "T_BiomeIndex_Bench"

_log: list[str] = []


def log(kind: str, message: str) -> None:
    line = "{}: {}".format(kind, message)
    _log.append(line)
    unreal.log(line)
    print(line)


def _spec() -> dict:
    with open(os.path.join(BENCH_DIR, "bench.json"), encoding="utf-8") as f:
        return json.load(f)


def _probes() -> list[dict]:
    with open(os.path.join(BENCH_DIR, "probes.json"), encoding="utf-8") as f:
        return json.load(f)["probes"]


# -------------------------------------------------------------------- texture


def import_biome_texture(force: bool = False) -> str | None:
    """Importe biome_bench.png avec les reglages qui preservent les identifiants."""
    asset_path = "{}/{}".format(DEST_PACKAGE, TEXTURE_NAME)
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        if not force:
            log("SKIPPED", "{} deja present (force=True pour reimporter)".format(asset_path))
            return asset_path
        unreal.EditorAssetLibrary.delete_asset(asset_path)
        log("DELETED", asset_path)

    png = os.path.join(BENCH_DIR, "biome_bench.png")
    if not os.path.isfile(png):
        log("ERROR", "absent : {} (lancer export_biome_texture.py)".format(png))
        return None

    task = unreal.AssetImportTask()
    task.filename = png
    task.destination_path = DEST_PACKAGE
    task.destination_name = TEXTURE_NAME
    task.replace_existing = True
    task.automated = True          # aucune boite de dialogue
    task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    tex = unreal.EditorAssetLibrary.load_asset(asset_path)
    if not isinstance(tex, unreal.Texture2D):
        log("ERROR", "import echoue : {}".format(asset_path))
        return None
    log("CREATED", "{} ({}x{})".format(
        asset_path, tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))

    # L'ordre compte : sRGB et compression AVANT le rebuild declenche par la
    # modification, sinon la premiere construction encode en BC et perd les IDs.
    tex.set_editor_property("srgb", False)
    tex.set_editor_property("mip_gen_settings",
                            unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("compression_settings",
                            unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    tex.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    tex.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    tex.set_editor_property("never_stream", True)
    unreal.EditorAssetLibrary.save_asset(asset_path)
    log("MODIFIED", "reglages poses : sRGB off, NoMipmaps, "
                    "TC_VectorDisplacementmap, TF_Nearest, Clamp")
    return asset_path


# Reglages exiges, et pourquoi. Les trois premiers sont ceux que PCG se force a
# lui-meme dans PCGTextureData.cpp : sans eux il duplique la texture en memoire.
REQUIRED = [
    ("srgb", False, "sRGB decoderait les identifiants en gamma"),
    ("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS,
     "un mip est une moyenne d'identifiants, donc un biome invente"),
    ("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP,
     "seul un format non compresse (B8G8R8A8) reste exact et lisible par PCG"),
    ("filter", unreal.TextureFilter.TF_NEAREST,
     "filtrage de la TEXTURE (materiau, GPU) ; celui de PCG est separe"),
    ("address_x", unreal.TextureAddress.TA_CLAMP, "pas de repetition en bord de tuile"),
    ("address_y", unreal.TextureAddress.TA_CLAMP, "pas de repetition en bord de tuile"),
    ("availability", unreal.TextureAvailability.GPU,
     "CPU n'enverrait qu'un placeholder noir au GPU et casserait le ground scatter"),
]


def verify_texture(asset_path: str | None = None) -> dict:
    """Controle les reglages, puis reexporte la texture pour un diff hors editeur.

    La copie CPU (blueprint_get_cpu_copy) est un cul-de-sac ici : elle exige
    availability = CPU, ce qui n'envoie au GPU qu'un placeholder noir. On passe
    donc par Texture.export_to_disk, qui rend les 262144 texels et permet une
    comparaison exhaustive au PNG source :

        python export_biome_texture.py --verify-roundtrip <png>
    """
    asset_path = asset_path or "{}/{}".format(DEST_PACKAGE, TEXTURE_NAME)
    tex = unreal.EditorAssetLibrary.load_asset(asset_path)
    if not isinstance(tex, unreal.Texture2D):
        log("ERROR", "texture introuvable : {}".format(asset_path))
        return {"ok": False}

    spec = _spec()
    size = spec["sizePx"]
    if tex.blueprint_get_size_x() != size or tex.blueprint_get_size_y() != size:
        log("ERROR", "taille {}x{} au lieu de {}".format(
            tex.blueprint_get_size_x(), tex.blueprint_get_size_y(), size))
        return {"ok": False}

    wrong = []
    for prop, expected, why in REQUIRED:
        got = tex.get_editor_property(prop)
        if got != expected:
            wrong.append((prop, expected, got, why))
    if wrong:
        for prop, expected, got, why in wrong:
            log("ERROR", "{} = {} au lieu de {} ({})".format(prop, got, expected, why))
    else:
        log("OK", "les {} reglages qui preservent les identifiants sont poses".format(
            len(REQUIRED)))

    # Les valeurs de source, converties en lineaire par le moteur : le maximum
    # doit correspondre au plus grand identifiant present dans la fenetre.
    ids_present = sorted({p["expectedId"] for p in _probes()})
    cmin, cmax = tex.compute_texture_source_channel_min_max()
    log("INFO", "source min/max lineaire {:.6f}/{:.6f} "
                "(identifiants attendus {}..{})".format(
                    cmin.r, cmax.r, ids_present[0], ids_present[-1]))

    out_png = os.path.join(BENCH_DIR, "roundtrip.png")
    options = unreal.ImageWriteOptions()
    options.set_editor_property("format", unreal.DesiredImageFormat.PNG)
    options.set_editor_property("overwrite_file", True)
    options.set_editor_property("async_", False)
    tex.export_to_disk(out_png, options)
    if not os.path.isfile(out_png):
        log("ERROR", "export_to_disk n'a rien ecrit")
        return {"ok": False}
    log("CREATED", "{} ({} octets) - a comparer avec "
                   "export_biome_texture.py --verify-roundtrip".format(
                       out_png, os.path.getsize(out_png)))

    return {"ok": not wrong, "settings_wrong": len(wrong), "roundtrip": out_png}


# --------------------------------------------------------------------- graphe

GRAPH_PATH = "/Game/Worldseed/PCG/PCG_BiomeBench"
BENCH_ACTOR = "Worldseed_PCGBench"

# Les trois biomes du banc. Le couple desert_chaud / plage est le cas d'espece :
# meme couche de peinture dominante (DesertSand), donc indistinguables par les
# couches, et pourtant deux identifiants nets dans biome_index.
# Les primitives du moteur font 1 m de cote : a l'echelle du banc, il faut les
# grossir pour qu'un temoin soit lisible d'un coup d'oeil.
MARKER_SCALE = 8.0

BENCH_BIOMES = [
    (11, "desert_chaud", "/Engine/BasicShapes/Cone.Cone"),
    (17, "plage", "/Engine/BasicShapes/Sphere.Sphere"),
    (7, "foret_temperee_humide", "/Engine/BasicShapes/Cylinder.Cylinder"),
]


def _pin(node, out: bool = False) -> str:
    ps = node.get_editor_property("output_pins" if out else "input_pins")
    return str(ps[0].get_editor_property("properties").get_editor_property("label"))


def build_bench_graph(texel_size_cm: float = 3000.0,
                      point_filter: bool = True) -> str:
    """Cree le graphe : carte des biomes -> points -> un maillage par biome.

    Chaine par biome : filtre de densite (bande d'un identifiant) -> projection
    sur le Landscape -> semeur de maillages. Le filtre se fait AVANT la
    projection, car la projection recombine les densites et effacerait
    l'identifiant qu'on vient de lire.
    """
    spec = _spec()
    surf = spec["pcgSurface"]
    # NE PAS supprimer le graphe pour le refaire : un PCGComponent du niveau le
    # reference encore, et delete_asset leve alors une ensure dans ObjectTools
    # qui OUVRE UN MODAL. L'editeur se fige, le thread de jeu part en stall et
    # tout appel MCP pend jusqu'au timeout du client. On reutilise l'asset et on
    # le vide de ses noeuds, ce qui est de toute facon non destructif.
    if unreal.EditorAssetLibrary.does_asset_exist(GRAPH_PATH):
        graph = unreal.EditorAssetLibrary.load_asset(GRAPH_PATH)
        # remove_nodes() prend son tableau en parametre SORTANT cote Python
        # (quirk du binding sur une reference non const) : inutilisable ici, on
        # retire noeud par noeud.
        olds = list(graph.get_editor_property("nodes"))
        for n in olds:
            graph.remove_node(n)
        log("MODIFIED", "graphe existant vide de ses {} noeuds".format(len(olds)))
    else:
        graph = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            GRAPH_PATH.rsplit("/", 1)[1], GRAPH_PATH.rsplit("/", 1)[0],
            unreal.PCGGraph, unreal.PCGGraphFactory())
        log("CREATED", GRAPH_PATH)

    sampler, st = graph.add_node_of_type(unreal.PCGTextureSamplerSettings)
    st.set_editor_property("texture", unreal.EditorAssetLibrary.load_asset(
        "{}/{}".format(DEST_PACKAGE, TEXTURE_NAME)))
    # LE reglage decisif. Bilinear (le defaut) melange les identifiants voisins
    # des que le point ne tombe pas pile sur un texel : mesure sur ce banc,
    # 1,71 % des points recoivent alors un biome qui n'est pas le leur.
    st.set_editor_property("filter", unreal.PCGTextureFilter.POINT if point_filter
                           else unreal.PCGTextureFilter.BILINEAR)
    st.set_editor_property("texel_size", float(texel_size_cm))
    st.set_editor_property("use_absolute_transform", True)
    st.set_editor_property("transform", unreal.Transform(
        unreal.Vector(surf["locationCm"]["x"], surf["locationCm"]["y"], 0.0),
        unreal.Rotator(0.0, 0.0, 0.0),
        unreal.Vector(surf["halfSpanCm"], surf["halfSpanCm"], 1.0)))
    st.set_editor_property("use_density_source_channel", True)
    st.set_editor_property("color_channel", unreal.PCGTextureColorChannel.RED)
    # L'ocean vaut 0 : sans ceci ses points disparaissent avant qu'on ait pu
    # constater qu'ils portaient bien l'identifiant 0.
    st.set_editor_property("keep_zero_density_points", True)
    st.set_editor_property("synchronous_load", True)

    convert, _ = graph.add_node_of_type(unreal.PCGConvertToPointDataSettings)
    graph.add_edge(sampler, "Out", convert, _pin(convert))

    # Les reglages d'echantillonnage vivent dans `sampling_properties`
    # (PCGLandscapeDataProps), pas a plat sur le settings ; les defauts
    # suffisent, on ne veut que l'altitude pour projeter.
    land, _ls = graph.add_node_of_type(unreal.PCGGetLandscapeSettings)

    for biome_id, name, mesh_path in BENCH_BIOMES:
        mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
        if not mesh:
            log("ERROR", "maillage introuvable : {}".format(mesh_path))
            continue
        filt, fs = graph.add_node_of_type(unreal.PCGDensityFilterSettings)
        # La densite porte l'identifiant : densite = id / 255. Une bande d'un
        # demi-cran de part et d'autre isole un identifiant et un seul.
        fs.set_editor_property("lower_bound", (biome_id - 0.5) / 255.0)
        fs.set_editor_property("upper_bound", (biome_id + 0.5) / 255.0)
        graph.add_edge(convert, "Out", filt, _pin(filt))

        proj, _ps = graph.add_node_of_type(unreal.PCGProjectionSettings)
        graph.add_edge(filt, "Out", proj, "In")
        graph.add_edge(land, "Out", proj, "Projection Target")

        # Les primitives du moteur font 1 m : invisibles sur un banc de 2 km.
        # On les grossit, c'est un temoin de lecture, pas du decor.
        grow, gs = graph.add_node_of_type(unreal.PCGTransformPointsSettings)
        gs.set_editor_property("absolute_scale", True)
        gs.set_editor_property("scale_min", unreal.Vector(MARKER_SCALE, MARKER_SCALE, MARKER_SCALE))
        gs.set_editor_property("scale_max", unreal.Vector(MARKER_SCALE, MARKER_SCALE, MARKER_SCALE))
        graph.add_edge(proj, "Out", grow, "In")
        proj = grow

        spawn, sp = graph.add_node_of_type(unreal.PCGStaticMeshSpawnerSettings)
        sel = sp.get_editor_property("mesh_selector_parameters")
        entry = unreal.PCGMeshSelectorWeightedEntry()
        entry.get_editor_property("descriptor").set_editor_property("static_mesh", mesh)
        entry.set_editor_property("weight", 1)
        sel.set_editor_property("mesh_entries", [entry])
        graph.add_edge(proj, "Out", spawn, "In")
        graph.add_edge(spawn, "Out", graph.get_output_node(), "Out")
        log("CREATED", "branche {} (id {}) -> {}".format(
            name, biome_id, mesh_path.rsplit("/", 1)[1]))

    unreal.EditorAssetLibrary.save_asset(GRAPH_PATH)
    log("MODIFIED", "graphe enregistre, {} noeuds, {} aretes".format(
        len(graph.get_editor_property("nodes")), len(graph.get_all_edges())))
    return GRAPH_PATH


def place_bench_actor() -> str:
    """Pose (ou repose) le PCGVolume qui porte le graphe, centre sur le banc."""
    spec = _spec()
    surf = spec["pcgSurface"]
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in sub.get_all_level_actors():
        if a.get_actor_label() == BENCH_ACTOR:
            sub.destroy_actor(a)
            log("DELETED", "acteur {} precedent".format(BENCH_ACTOR))

    v = sub.spawn_actor_from_class(
        unreal.PCGVolume,
        unreal.Vector(surf["locationCm"]["x"], surf["locationCm"]["y"], 20000.0),
        unreal.Rotator(0.0, 0.0, 0.0))
    v.set_actor_label(BENCH_ACTOR)
    # La brosse par defaut fait 200 uu de cote.
    s = float(surf["halfSpanCm"]) * 2.0 / 200.0
    v.set_actor_scale3d(unreal.Vector(s, s, s))
    comp = v.get_component_by_class(unreal.PCGComponent)
    comp.set_editor_property("is_component_partitioned", False)
    comp.set_editor_property("generation_trigger",
                             unreal.PCGComponentGenerationTrigger.GENERATE_ON_DEMAND)
    comp.set_graph(unreal.EditorAssetLibrary.load_asset(GRAPH_PATH))
    log("CREATED", "{} (PCGVolume, echelle {:.0f})".format(BENCH_ACTOR, s))
    return BENCH_ACTOR


def generate() -> None:
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    v = [a for a in sub.get_all_level_actors() if a.get_actor_label() == BENCH_ACTOR][0]
    comp = v.get_component_by_class(unreal.PCGComponent)
    comp.cleanup(True)
    comp.generate(True)
    log("MODIFIED", "generation demandee")


def sampled_ids() -> list:
    """Relit les points generes : [x_cm, y_cm, identifiant de biome].

    Le chemin de l'objet de sortie ne s'obtient que par export_text du wrapper :
    PCGDataPtrWrapper n'expose rien d'autre en Python.
    """
    import re
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    v = [a for a in sub.get_all_level_actors() if a.get_actor_label() == BENCH_ACTOR][0]
    tagged = v.get_component_by_class(unreal.PCGComponent) \
              .get_generated_graph_output().get_editor_property("tagged_data")
    rows = []
    for td in tagged:
        path = re.search(r"'([^']+)'", td.get_editor_property("data").export_text())
        if not path:
            continue
        data = unreal.load_object(None, path.group(1))
        if not isinstance(data, unreal.PCGBasePointData):
            continue
        rng = unreal.PCGPointInputRange()
        rng.point_data = data
        rng.range_start_index = 0
        rng.range_size = data.get_num_points()
        for t, d in zip(data.get_transform_values_from_range(rng),
                        data.get_density_values_from_range(rng)):
            rows.append([round(t.translation.x, 2), round(t.translation.y, 2),
                         int(round(d * 255.0))])
    return rows


def step2_graph(texel_size_cm: float = 3000.0) -> dict:
    """Etape 2 : semer un maillage different par biome sur le banc."""
    _log.clear()
    build_bench_graph(texel_size_cm=texel_size_cm)
    place_bench_actor()
    generate()
    return {"graph": GRAPH_PATH, "actor": BENCH_ACTOR, "log": list(_log)}


def step1_texture(force: bool = False) -> dict:
    """Etape 1 : la texture d'identifiants survit-elle a l'import ?"""
    _log.clear()
    path = import_biome_texture(force=force)
    if not path:
        return {"ok": False, "log": list(_log)}
    result = verify_texture(path)
    result["asset"] = path
    result["log"] = list(_log)
    return result


if __name__ == "__main__":
    print(json.dumps(step1_texture(), indent=2, ensure_ascii=False))
