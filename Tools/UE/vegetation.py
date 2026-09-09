"""Semis de vegetation Worldseed, pilote par la carte des biomes.

A executer DEPUIS l'editeur :

    import sys, importlib
    sys.path.insert(0, r"D:\\UE\\Worldseed\\Tools\\UE")
    import vegetation; importlib.reload(vegetation)
    print(vegetation.build_bench())        # les 2 km du banc, genere et verifie
    print(vegetation.build_world())        # les 4 tuiles, composants partitionnes

CE QUI DECIDE DE L'ASPECT DU MONDE est `vegetation_recipes.json`, pas ce
fichier : une recette par biome, chaque recette faite de couches (arbres,
sous-bois, rochers) avec leur pas de grille, leurs maillages ponderes, leur
plage d'echelle et leurs distances de coupe. Ce module ne fait que traduire ces
recettes en graphe PCG.

FORME DU GRAPHE, et pourquoi elle est ainsi :

    par pas de grille distinct :
        TextureSampler(Point) -> ConvertToPointData -> Projection(Landscape)
          `-> pour chaque couche qui utilise ce pas :
                 DensityFilter(bande d'un identifiant)
                 -> TransformPoints(rotation et echelle aleatoires)
                 -> StaticMeshSpawner(maillages ponderes)

La projection sur le Landscape est faite UNE FOIS par pas de grille, avant le
tri par biome, et non une fois par couche. C'est possible parce que la densite
- qui porte l'identifiant de biome - survit intacte a la projection : mesure
faite sur le banc, les proportions apres projection reproduisent la composition
de la fenetre au dixieme de point (desert 39,7 % contre 39,8 % attendus). Cela
economise une projection de terrain par couche, soit une trentaine.

Le filtre du sampler est `Point`. En `Bilinear` - la valeur PAR DEFAUT de tous
les noeuds PCG - les identifiants de biome s'interpolent et la vegetation d'un
biome deborde sur ses voisins : 1,71 % des points mesures sur le banc, du type
palmier de plage plante en zone alpine. Voir CLAUDE.md section 11.
"""

from __future__ import annotations

import json
import os

import unreal

RECIPES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "vegetation_recipes.json")
BENCH_DIR = r"D:\UE\Worldseed\Saved\WorldGen\20260909\bench"
TILES_DIR = r"D:\UE\Worldseed\Saved\WorldGen\20260909\tiles"
GRAPH_DIR = "/Game/Worldseed/PCG"
TILE_TEXTURE_DIR = "/Game/Worldseed/PCG/Biomes"

_log: list[str] = []


def log(kind: str, message: str) -> None:
    line = "{}: {}".format(kind, message)
    _log.append(line)
    unreal.log(line)
    print(line)


def recipes() -> dict:
    with open(RECIPES, encoding="utf-8") as f:
        return json.load(f)


def _mesh(ref: str, roots: dict):
    prefix, name = ref.split(":")
    return unreal.EditorAssetLibrary.load_asset(
        "{}/{}.{}".format(roots[prefix], name, name))


MATERIAL_DIR = "/Game/Worldseed/PCG/Materials"
_RVT_SWITCHES = ("UseRVT", "UseTopLayerRvt")
_mat_cache: dict[str, object] = {}


def _switch(mat, name: str) -> bool:
    try:
        return bool(unreal.MaterialEditingLibrary
                    .get_material_instance_static_switch_parameter_value(mat, name))
    except Exception:
        return False


def rvt_free_materials(mesh) -> list:
    """Materiaux du maillage, avec le sample de RVT neutralise si besoin.

    POURQUOI. Les packs `Stylized_Landscape_5_Bioms` (palmiers, bambous,
    rochers de desert, falaises) passent par `M_Assets_MasterMat`, dont le
    switch statique `UseRVT` echantillonne une Runtime Virtual Texture. Sans
    `RuntimeVirtualTextureVolume` dans le niveau, l'echantillonnage rend du vide
    et le maillage s'affiche en BLEU PUR - constate en jeu sur les rochers du
    desert. Creer la RVT a deja ete tente et abandonne (la texture restait vide
    et le terrain proche s'aplatissait).

    Comme la contribution est gardee par un switch STATIQUE, il n'y a aucune
    chirurgie de graphe a faire : on duplique l'instance de materiau chez nous,
    switch a false, et on la pose en override sur l'instance semee. Le bundle
    achete n'est jamais modifie.

    Rend une liste vide si aucun emplacement n'a besoin d'etre corrige.
    """
    slots = list(mesh.get_editor_property("static_materials"))
    out, needed = [], False
    for slot in slots:
        mat = slot.get_editor_property("material_interface")
        if mat is None:
            out.append(None)
            continue
        if not any(_switch(mat, s) for s in _RVT_SWITCHES):
            out.append(mat)
            continue
        needed = True
        name = mat.get_name() + "_NoRVT"
        path = "{}/{}".format(MATERIAL_DIR, name)
        fixed = _mat_cache.get(path)
        if fixed is None:
            if unreal.EditorAssetLibrary.does_asset_exist(path):
                fixed = unreal.EditorAssetLibrary.load_asset(path)
            else:
                fixed = unreal.EditorAssetLibrary.duplicate_asset(mat.get_path_name(), path)
                for s in _RVT_SWITCHES:
                    unreal.MaterialEditingLibrary \
                        .set_material_instance_static_switch_parameter_value(fixed, s, False)
                unreal.EditorAssetLibrary.save_asset(path)
                log("CREATED", "{} (RVT neutralisee)".format(path))
            _mat_cache[path] = fixed
        out.append(fixed)
    return out if needed else []


def _pin(node, out: bool = False) -> str:
    ps = node.get_editor_property("output_pins" if out else "input_pins")
    return str(ps[0].get_editor_property("properties").get_editor_property("label"))


def _fresh_graph(asset_path: str) -> "unreal.PCGGraph":
    """Reutilise l'asset et le vide, sans jamais le supprimer.

    Supprimer un graphe encore reference par un PCGComponent du niveau leve une
    ensure dans ObjectTools qui ouvre un modal : l'editeur se fige et tout appel
    MCP pend jusqu'au timeout. Voir CLAUDE.md section 11.
    """
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        graph = unreal.EditorAssetLibrary.load_asset(asset_path)
        olds = list(graph.get_editor_property("nodes"))
        for n in olds:
            graph.remove_node(n)
        log("MODIFIED", "{} vide de ses {} noeuds".format(asset_path, len(olds)))
        return graph
    folder, name = asset_path.rsplit("/", 1)
    graph = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, unreal.PCGGraph, unreal.PCGGraphFactory())
    log("CREATED", asset_path)
    return graph


def build_graph(asset_path: str, texture, location_cm: dict, half_span_cm: float,
                biomes: list[str] | None = None, hierarchical: bool = False) -> str:
    """Traduit les recettes en graphe PCG pour une texture de biomes donnee."""
    rec = recipes()
    roots, spacings = rec["roots"], rec["spacings"]
    wanted = biomes or list(rec["biomes"].keys())

    # Regroupement par pas de grille : une chaine d'echantillonnage par pas,
    # partagee par toutes les couches qui l'utilisent.
    by_spacing: dict[str, list] = {}
    for bname in wanted:
        b = rec["biomes"][bname]
        for layer in b["layers"]:
            by_spacing.setdefault(layer["spacing"], []).append((bname, b["id"], layer))

    graph = _fresh_graph(asset_path)
    if hierarchical:
        # Mailles de 256 m : une maille porte environ 800 points de sous-bois par
        # biome, ce qui reste instantane a fabriquer quand le joueur avance.
        graph.set_editor_property("use_hierarchical_generation", True)
        graph.set_editor_property("hi_gen_grid_size", unreal.PCGHiGenGrid.GRID256)
    land, _ls = graph.add_node_of_type(unreal.PCGGetLandscapeSettings)
    out_node = graph.get_output_node()
    n_layers = 0

    for spacing_name, layers in by_spacing.items():
        texel = float(spacings[spacing_name])

        sampler, st = graph.add_node_of_type(unreal.PCGTextureSamplerSettings)
        st.set_editor_property("texture", texture)
        st.set_editor_property("filter", unreal.PCGTextureFilter.POINT)
        st.set_editor_property("texel_size", texel)
        st.set_editor_property("use_absolute_transform", True)
        st.set_editor_property("transform", unreal.Transform(
            unreal.Vector(location_cm["x"], location_cm["y"], 0.0),
            unreal.Rotator(0.0, 0.0, 0.0),
            unreal.Vector(half_span_cm, half_span_cm, 1.0)))
        st.set_editor_property("use_density_source_channel", True)
        st.set_editor_property("color_channel", unreal.PCGTextureColorChannel.RED)
        st.set_editor_property("keep_zero_density_points", True)
        st.set_editor_property("synchronous_load", True)

        convert, _c = graph.add_node_of_type(unreal.PCGConvertToPointDataSettings)
        graph.add_edge(sampler, "Out", convert, _pin(convert))

        project, ps = graph.add_node_of_type(unreal.PCGProjectionSettings)
        ps.set_editor_property("keep_zero_density_points", True)
        graph.add_edge(convert, "Out", project, "In")
        graph.add_edge(land, "Out", project, "Projection Target")

        for bname, biome_id, layer in layers:
            # La densite vaut identifiant / 255 : une bande d'un demi-cran de
            # part et d'autre isole un identifiant et un seul.
            filt, fs = graph.add_node_of_type(unreal.PCGDensityFilterSettings)
            fs.set_editor_property("lower_bound", (biome_id - 0.5) / 255.0)
            fs.set_editor_property("upper_bound", (biome_id + 0.5) / 255.0)
            graph.add_edge(project, "Out", filt, _pin(filt))

            lo, hi = layer["scale"]
            xf, xs = graph.add_node_of_type(unreal.PCGTransformPointsSettings)
            xs.set_editor_property("absolute_scale", True)
            xs.set_editor_property("uniform_scale", True)
            xs.set_editor_property("scale_min", unreal.Vector(lo, lo, lo))
            xs.set_editor_property("scale_max", unreal.Vector(hi, hi, hi))
            # Rotation libre autour de la verticale seulement : un arbre couche
            # sur le flanc se remarque plus qu'une foret trop reguliere.
            xs.set_editor_property("absolute_rotation", True)
            xs.set_editor_property("rotation_min", unreal.Rotator(0.0, 0.0, 0.0))
            xs.set_editor_property("rotation_max", unreal.Rotator(0.0, 0.0, 360.0))
            graph.add_edge(filt, "Out", xf, "In")

            spawn, sp = graph.add_node_of_type(unreal.PCGStaticMeshSpawnerSettings)
            selector = sp.get_editor_property("mesh_selector_parameters")
            start_cull, end_cull = layer.get("cull", [0, 0])
            entries = []
            for ref, weight in layer["meshes"]:
                mesh = _mesh(ref, roots)
                if not mesh:
                    log("ERROR", "maillage introuvable : {}".format(ref))
                    continue
                entry = unreal.PCGMeshSelectorWeightedEntry()
                desc = entry.get_editor_property("descriptor")
                desc.set_editor_property("static_mesh", mesh)
                desc.set_editor_property("instance_start_cull_distance", int(start_cull))
                desc.set_editor_property("instance_end_cull_distance", int(end_cull))
                # Le decalage de position du monde coute cher au loin et ne se
                # voit pas : on l'eteint la ou l'instance commence a disparaitre.
                desc.set_editor_property("world_position_offset_disable_distance",
                                         int(start_cull))
                overrides = rvt_free_materials(mesh)
                if overrides:
                    desc.set_editor_property("override_materials", overrides)
                entry.set_editor_property("descriptor", desc)
                entry.set_editor_property("weight", int(weight))
                entries.append(entry)
            selector.set_editor_property("mesh_entries", entries)
            graph.add_edge(xf, "Out", spawn, "In")
            graph.add_edge(spawn, "Out", out_node, "Out")
            n_layers += 1

    unreal.EditorAssetLibrary.save_asset(asset_path)
    log("MODIFIED", "{} : {} pas de grille, {} couches, {} noeuds, {} aretes".format(
        asset_path.rsplit("/", 1)[1], len(by_spacing), n_layers,
        len(graph.get_editor_property("nodes")), len(graph.get_all_edges())))
    return asset_path


def place_volume(label: str, graph_path: str, location_cm: dict, half_span_cm: float,
                 partitioned: bool = False, runtime: bool = False) -> str:
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in sub.get_all_level_actors():
        if a.get_actor_label() == label:
            c = a.get_component_by_class(unreal.PCGComponent)
            if c:
                c.cleanup(True)
            sub.destroy_actor(a)
            log("DELETED", "acteur {} precedent".format(label))

    v = sub.spawn_actor_from_class(
        unreal.PCGVolume,
        unreal.Vector(location_cm["x"], location_cm["y"], 20000.0),
        unreal.Rotator(0.0, 0.0, 0.0))
    v.set_actor_label(label)
    s = float(half_span_cm) * 2.0 / 200.0        # la brosse fait 200 uu de cote
    v.set_actor_scale3d(unreal.Vector(s, s, s))
    comp = v.get_component_by_class(unreal.PCGComponent)
    comp.set_editor_property("is_component_partitioned", partitioned)
    # A l'echelle du monde on ne CUIT rien : le semis complet pese 12,9 millions
    # d'instances, soit 292 fois le banc. En GenerateAtRuntime, PCG ne fabrique
    # que les mailles autour du joueur et les defait derriere lui : aucun acteur
    # sur le disque, rien dans le versionnage, rien a charger au demarrage.
    comp.set_editor_property(
        "generation_trigger",
        unreal.PCGComponentGenerationTrigger.GENERATE_AT_RUNTIME if runtime
        else unreal.PCGComponentGenerationTrigger.GENERATE_ON_DEMAND)
    comp.set_graph(unreal.EditorAssetLibrary.load_asset(graph_path))
    log("CREATED", "{} ({}, {}, echelle {:.0f})".format(
        label, "partitionne" if partitioned else "monobloc",
        "a l'execution" if runtime else "a la demande", s))
    return label


def generate(label: str) -> None:
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    v = [a for a in sub.get_all_level_actors() if a.get_actor_label() == label][0]
    comp = v.get_component_by_class(unreal.PCGComponent)
    comp.cleanup(True)
    comp.generate(True)
    log("MODIFIED", "generation demandee sur {}".format(label))


def census(label: str) -> dict:
    """Compte les instances posees, par maillage puis par total."""
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    hits = [a for a in sub.get_all_level_actors() if a.get_actor_label() == label]
    if not hits:
        return {"total": 0, "meshes": {}}
    per_mesh: dict[str, int] = {}
    for m in hits[0].get_components_by_class(unreal.InstancedStaticMeshComponent):
        sm = m.get_editor_property("static_mesh")
        key = sm.get_name() if sm else "?"
        per_mesh[key] = per_mesh.get(key, 0) + m.get_instance_count()
    return {"total": sum(per_mesh.values()), "meshes": per_mesh}


# ------------------------------------------------------------------------ banc


def build_bench(biomes: list[str] | None = None) -> dict:
    """Semis complet sur les 2 km du banc, a partir de la texture du banc."""
    _log.clear()
    with open(os.path.join(BENCH_DIR, "bench.json"), encoding="utf-8") as f:
        surf = json.load(f)["pcgSurface"]
    texture = unreal.EditorAssetLibrary.load_asset(GRAPH_DIR + "/T_BiomeIndex_Bench")
    path = build_graph(GRAPH_DIR + "/PCG_Vegetation_Bench", texture,
                       surf["locationCm"], float(surf["halfSpanCm"]), biomes)
    place_volume("Worldseed_Vegetation_Bench", path,
                 surf["locationCm"], float(surf["halfSpanCm"]))
    generate("Worldseed_Vegetation_Bench")
    return {"graph": path, "actor": "Worldseed_Vegetation_Bench", "log": list(_log)}


# ----------------------------------------------------------------------- monde


def tile_surface(tile: str) -> tuple[dict, float]:
    """Centre et demi-portee de la surface PCG d'une tuile, comme au banc.

    Le texel de la carte de biomes se superpose au sommet de Landscape, donc le
    centre de la surface est a `origine + (n-1)/2 * texel`, pas `n/2`.
    """
    with open(os.path.join(TILES_DIR, tile, "manifest.json"), encoding="utf-8") as f:
        m = json.load(f)
    ls = m["landscape"]
    n = int(m["world"]["resolution"])
    cm = float(ls["scale"]["x"])
    loc = {"x": float(ls["actorLocation"]["x"]) + (n - 1) / 2.0 * cm,
           "y": float(ls["actorLocation"]["y"]) + (n - 1) / 2.0 * cm}
    return loc, n * cm / 2.0


def build_world(tiles: list[str] | None = None,
                biomes: list[str] | None = None) -> dict:
    """Un graphe et un volume par tuile de Landscape, generes A L'EXECUTION.

    Le semis complet du monde pese environ 12,9 millions d'instances, soit 292
    fois le banc : il n'est pas question de le cuire. En `GenerateAtRuntime`
    avec un graphe hierarchique en mailles de 256 m, PCG ne fabrique que ce qui
    entoure la source de generation et le defait derriere elle.

    PIEGE, PAYE COMPTANT : ne PAS mettre `is_component_partitioned = True`.
    Ce reglage-la est celui de la cuisson EN EDITEUR, pas de l'execution : il
    cree un `PCGPartitionActor` PERSISTANT par maille, ecrit sous
    `Content/__ExternalActors__`. Mesure sur ce monde : 15 876 acteurs et
    847 Mo sur le disque, pour zero instance produite. La generation a
    l'execution gere ses propres mailles, transitoires, sans ce reglage.
    """
    _log.clear()
    tiles = tiles or ["x0_y0", "x1_y0", "x0_y1", "x1_y1"]
    made = {}
    for tile in tiles:
        texture = unreal.EditorAssetLibrary.load_asset(
            "{}/T_BiomeIndex_{}".format(TILE_TEXTURE_DIR, tile))
        if not texture:
            log("ERROR", "texture de biomes absente pour la tuile {}".format(tile))
            continue
        loc, half = tile_surface(tile)
        path = build_graph("{}/PCG_Vegetation_{}".format(GRAPH_DIR, tile),
                           texture, loc, half, biomes, hierarchical=True)
        label = "Worldseed_Vegetation_" + tile
        place_volume(label, path, loc, half, partitioned=False, runtime=True)
        made[tile] = label
    return {"tiles": made, "log": list(_log)}
