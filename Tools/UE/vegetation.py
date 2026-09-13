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
import math
import os

import unreal

RECIPES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "vegetation_recipes.json")
BENCH_DIR = r"D:\UE\Worldseed\Saved\WorldGen\20260909\bench"
TILES_DIR = r"D:\UE\Worldseed\Saved\WorldGen\20260909\tiles"
GRAPH_DIR = "/Game/Worldseed/PCG"
TILE_TEXTURE_DIR = "/Game/Worldseed/PCG/Biomes"

# Niveau de la mer, en centimetres monde : l'ocean est pose a Z = 0. Sert de
# plancher au semis, voir le filtre dans build_graph.
# Decalage d'identifiant marquant une surface peinte en mineral, pose par
# Tools/WorldGen/export_biome_texture.py. Les deux valeurs DOIVENT rester
# egales : c'est le seul lien entre la carte et le graphe.
MINERAL_ID_OFFSET = 100

SEA_LEVEL_CM = 0.0

_log: list[str] = []


def tuiles_presentes(racine: str) -> list:
    """Les tuiles reellement ecrites par tile_world.py, dans l'ordre.

    Ne JAMAIS coder en dur la liste des quatre tuiles : le monde de 8 km n'en a
    qu'une (4065 sommets = 256 composants, donc un seul Landscape), celui de
    32 km en avait quatre. Le nombre suit la resolution.
    """
    import os
    if not os.path.isdir(racine):
        return []
    noms = [n for n in sorted(os.listdir(racine))
            if n.startswith("x") and "_y" in n and os.path.isdir(os.path.join(racine, n))]
    return noms


def log(kind: str, message: str) -> None:
    line = "{}: {}".format(kind, message)
    _log.append(line)
    unreal.log(line)
    print(line)


def recipes() -> dict:
    with open(RECIPES, encoding="utf-8") as f:
        return json.load(f)


def _mesh(ref: str, roots: dict):
    """Resout 'RACINE:Nom' ou 'RACINE:SousDossier/Nom'.

    Le sous-dossier est indispensable pour les packs qui rangent un maillage
    par dossier -- Stylized_Rocks en a 26 --, sans quoi il faudrait une racine
    par rocher. Sans '/', le comportement est celui d'avant, au caractere pres.
    """
    prefix, name = ref.split(":")
    court = name.rsplit("/", 1)[-1]
    return unreal.EditorAssetLibrary.load_asset(
        "{}/{}.{}".format(roots[prefix], name, court))


MATERIAL_DIR = "/Game/Worldseed/PCG/Materials"
_RVT_SWITCHES = ("UseRVT", "UseTopLayerRvt")
_mat_cache: dict[str, object] = {}


def _switch(mat, name: str) -> bool:
    try:
        return bool(unreal.MaterialEditingLibrary
                    .get_material_instance_static_switch_parameter_value(mat, name))
    except Exception:
        return False


def rvt_disponible() -> bool:
    """Le niveau porte-t-il une Runtime Virtual Texture utilisable ?

    Un `RuntimeVirtualTextureVolume` avec une texture valide suffit : c'est lui
    qui fait exister la RVT dans le monde. Le cablage complet (le Landscape qui
    ECRIT dedans, et `virtual_texture_render_pass_type = ALWAYS`) est pose par
    `Tools/UE/rvt_setup.py`.
    """
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in sub.get_all_level_actors():
        if isinstance(a, unreal.RuntimeVirtualTextureVolume):
            c = a.get_component_by_class(unreal.RuntimeVirtualTextureComponent)
            if c is not None and c.get_editor_property("virtual_texture") is not None:
                return True
    return False


def rvt_free_materials(mesh) -> list:
    """Materiaux du maillage, avec le sample de RVT neutralise SI ELLE MANQUE.

    POURQUOI CE CONTOURNEMENT EXISTE. Les packs `Stylized_Landscape_5_Bioms`
    (palmiers, bambous, rochers de desert, falaises) passent par
    `M_Assets_MasterMat`, dont le switch statique `UseRVT` echantillonne une
    Runtime Virtual Texture. Sans `RuntimeVirtualTextureVolume` dans le niveau,
    l'echantillonnage rend du vide et le maillage s'affiche en BLEU PUR.

    POURQUOI IL EST CONDITIONNEL. Neutraliser la RVT, c'est renoncer a ce que le
    pack prevoit : le feuillage teinte par la couleur du terrain sous lui. Des
    que la RVT est correctement posee, ce contournement devient nuisible -- on le
    coupe alors de lui-meme, sans avoir a le desactiver a la main.

    Rend une liste vide si aucun emplacement n'a besoin d'etre corrige.
    """
    if rvt_disponible():
        return []
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


GREFFES = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "materiaux_greffes.json")
_greffes: dict | None = None


def materiaux_greffes() -> dict:
    """Table 'materiau du pack -> copie greffee', ecrite par `rvt_graft.rediriger()`.

    Absente, elle vaut table vide : le semis retombe alors sur les materiaux du
    pack, sans teinte par le sol. C'est un manque de rendu, pas une panne.
    """
    global _greffes
    if _greffes is None:
        try:
            with open(GREFFES, encoding="utf-8") as f:
                _greffes = json.load(f)
        except (FileNotFoundError, ValueError):
            _greffes = {}
            log("SKIPPED", "pas de table de materiaux greffes : {}".format(GREFFES))
    return _greffes


def overrides_greffe(mesh) -> list:
    """Redirige les materiaux d'un maillage vers leurs copies greffees.

    EMPLACEMENT PAR EMPLACEMENT, et c'est necessaire : un maillage a plusieurs
    sections peut meler un materiau greffe (les feuilles) et un materiau qui ne
    l'est pas (le tronc). Un override est une LISTE alignee sur les
    emplacements ; y mettre un seul materiau les repeindrait tous.

    Rend une liste vide si aucun emplacement n'est concerne -- ne jamais poser
    d'override inutile, il empeche le partage d'instances entre composants.
    """
    table = materiaux_greffes()
    if not table:
        return []
    out, besoin = [], False
    for slot in mesh.get_editor_property("static_materials"):
        mat = slot.get_editor_property("material_interface")
        if mat is None:
            out.append(None)
            continue
        neuf = table.get(mat.get_path_name().split(".")[0])
        if not neuf:
            out.append(mat)
            continue
        copie = _mat_cache.get(neuf)
        if copie is None:
            copie = unreal.EditorAssetLibrary.load_asset(neuf)
            if copie is None:
                log("ERROR", "materiau greffe introuvable : {}".format(neuf))
                out.append(mat)
                continue
            _mat_cache[neuf] = copie
        besoin = True
        out.append(copie)
    return out if besoin else []


def materiau_force(ref: str, mesh, rec: dict) -> list:
    """Materiau impose a un maillage par `materiaux_forces` de la recette.

    POURQUOI CELA EXISTE, et c'est la meme lecon que le materiau de terrain :
    UN PACK SE CONSOMME PAR SON INSTANCE, JAMAIS PAR SON MAITRE.

    `LPF:SM_Env_Grass_small` (4 triangles) et `GLO:SM_Grass` d'Orasot (360)
    ont le MEME materiau parent, `M_Grass`, dont le graphe lit la Runtime
    Virtual Texture du terrain pour teinter l'herbe a la couleur du sol. Et
    pourtant seule celle d'Orasot se teinte : la teinte est allumee par les
    SURCHARGES de l'instance -- `MI_Grass` pousse `Color Variation 1` a 10,3 et
    `Bottom Color Power` a 1,15 -- que `MI_Grass_Inst`, livre avec le petit
    maillage, ne porte pas.

    Mesure de la bascule sur un temoin pose en plein desert, vue zenithale :
    18,1 % de pixels verts avec `MI_Grass_Inst`, 0,0 % avec `MI_Grass`, le
    temoin de controle restant a 16 %. Autrement dit l'herbe disparait dans le
    sable, exactement comme le pack le prevoit.

    Rend une liste vide si la recette n'impose rien pour ce maillage.
    """
    chemin = (rec.get("materiaux_forces") or {}).get(ref)
    if not chemin:
        return []
    mat = _mat_cache.get(chemin)
    if mat is None:
        mat = unreal.EditorAssetLibrary.load_asset(chemin)
        if mat is None:
            log("ERROR", "materiau impose introuvable : {}".format(chemin))
            return []
        _mat_cache[chemin] = mat
    return [mat] * len(mesh.get_editor_property("static_materials"))


def _pin(node, out: bool = False) -> str:
    ps = node.get_editor_property("output_pins" if out else "input_pins")
    return str(ps[0].get_editor_property("properties").get_editor_property("label"))


def _etage_pente(graph, layer, amont):
    """Ne garde que les points dont la PENTE tombe dans l'intervalle demande.

    `PCGNormalToDensity` ecrit `dot(normale du point, verticale)` dans la
    densite, soit exactement `cos(pente)` : un filtre de densite devient donc un
    filtre de pente, sans passer par un selecteur d'attribut. Attention au sens,
    le cosinus DECROIT quand la pente monte -- la borne basse vient de la pente
    MAXIMALE.

    L'etage se place APRES le tri par biome, qui a besoin de la densite pour
    porter l'identifiant, et AVANT `TransformPoints`, qui remplace la rotation
    du point par un lacet aleatoire et detruit donc la normale.
    """
    p = layer.get("pente")
    if not p:
        return amont
    pmin, pmax = float(p[0]), float(p[1])
    nd, nds = graph.add_node_of_type(unreal.PCGNormalToDensitySettings)
    nds.set_editor_property("density_mode", unreal.PCGNormalToDensityMode.SET)
    nds.set_editor_property("normal", unreal.Vector(0.0, 0.0, 1.0))
    for n, pin in amont:
        graph.add_edge(n, pin, nd, _pin(nd))
    f, fs = graph.add_node_of_type(unreal.PCGDensityFilterSettings)
    fs.set_editor_property("lower_bound", math.cos(math.radians(pmax)))
    fs.set_editor_property("upper_bound", min(1.0, math.cos(math.radians(pmin)) + 1e-4))
    graph.add_edge(nd, "Out", f, _pin(f))
    return [(f, "Out")]


def _etage_taches(graph, layer, amont):
    """Seme par TACHES plutot qu'uniformement.

    Les fleurs, les champignons et les baies ne sont pas repartis au hasard :
    ils poussent en colonies. Une grille reguliere les eparpille un par un, ce
    qui se lit immediatement comme artificiel.

    `PCGSpatialNoise` en Perlin 2D ecrit un bruit COHERENT EN ESPACE dans la
    densite -- son `value_target` vaut `$Density` par defaut, verifie par
    `export_text()` -- et un filtre de densite ne garde alors que les creux ou
    les bosses, c'est-a-dire des taches. `taille` est le diametre approximatif
    d'une tache en centimetres ; `seuil` decide de la part de terrain retenue,
    0,5 en gardant a peu pres la moitie.
    """
    t = layer.get("taches")
    if not t:
        return amont
    taille = float(t.get("taille", 2500.0))
    seuil = float(t.get("seuil", 0.5))
    br, brs = graph.add_node_of_type(unreal.PCGSpatialNoiseSettings)
    brs.set_editor_property("mode", unreal.PCGSpatialNoiseMode.PERLIN2D)
    brs.set_editor_property("iterations", int(t.get("iterations", 2)))
    # L'echelle de la transformation donne la taille du motif : un Perlin a
    # frequence 1 sur une unite de monde, donc on etire d'autant.
    brs.set_editor_property("transform", unreal.Transform(
        unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0),
        unreal.Vector(taille, taille, taille)))
    for n, pin in amont:
        graph.add_edge(n, pin, br, _pin(br))
    f, fs = graph.add_node_of_type(unreal.PCGDensityFilterSettings)
    fs.set_editor_property("lower_bound", seuil)
    fs.set_editor_property("upper_bound", 1.0)
    graph.add_edge(br, "Out", f, _pin(f))
    return [(f, "Out")]


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

    # UN SEUL echantillonneur de texture pour tout le graphe, au pas le plus fin.
    # POURQUOI CA COMPTE : chaque noeud PCGTextureSampler materialise sa propre
    # `PCGTextureData`, qui est une copie FLOTTANTE de la texture - 4065 x 4065
    # x 4 canaux x 4 octets = 252 Mo la piece. La premiere version en creait un
    # par pas de grille, soit 4 par graphe et 20 en tout (4 tuiles + banc) :
    # 6,7 Go de RAM pour vingt copies de la meme carte, mesure au memreport.
    # Les pas plus larges s'obtiennent maintenant par decimation du meme nuage.
    finest = float(min(spacings.values()))
    sampler, st = graph.add_node_of_type(unreal.PCGTextureSamplerSettings)
    st.set_editor_property("texture", texture)
    st.set_editor_property("filter", unreal.PCGTextureFilter.POINT)
    st.set_editor_property("texel_size", finest)
    st.set_editor_property("use_absolute_transform", True)
    st.set_editor_property("transform", unreal.Transform(
        unreal.Vector(location_cm["x"], location_cm["y"], 0.0),
        unreal.Rotator(0.0, 0.0, 0.0),
        unreal.Vector(half_span_cm, half_span_cm, 1.0)))
    st.set_editor_property("use_density_source_channel", True)
    st.set_editor_property("color_channel", unreal.PCGTextureColorChannel.RED)
    st.set_editor_property("keep_zero_density_points", True)
    st.set_editor_property("synchronous_load", True)

    # ON NE TRANSFORME PAS LA TEXTURE EN POINTS. C'etait la faute lourde de la
    # version precedente : `ConvertToPointData` sur une surface qui couvre toute
    # la tuile fabrique 1600057/900 = 1778^2, soit 3,16 MILLIONS de points, et il
    # le refait dans CHAQUE maille de 256 m avant d'en jeter 99 %. Mesure au
    # lancement d'un PIE : l'editeur montait a 32 Go et le thread de jeu restait
    # bloque plus d'une minute.
    #
    # Le bon sens de lecture est l'inverse : on seme d'abord DANS la maille en
    # echantillonnant le Landscape (le `Bounding Shape` vient de l'entree du
    # graphe, donc les bornes de la maille), puis on lit la carte des biomes AU
    # POINT avec `PCGSampleTexture`, dont `density_merge_function = Set` reporte
    # la couleur lue dans la densite. L'identifiant de biome arrive donc intact,
    # et le cout suit la taille de la maille, plus celle de la tuile.
    surf, ss = graph.add_node_of_type(unreal.PCGSurfaceSamplerSettings)
    spacing_m = finest / 100.0
    ss.set_editor_property("points_per_squared_meter", 1.0 / (spacing_m ** 2))
    ss.set_editor_property("unbounded", False)
    ss.set_editor_property("apply_density_to_points", False)   # la densite viendra du biome
    ss.set_editor_property("point_extents", unreal.Vector(50.0, 50.0, 50.0))
    graph.add_edge(land, "Out", surf, "Surface")
    graph.add_edge(graph.get_input_node(), "In", surf, "Bounding Shape")

    lire, ls2 = graph.add_node_of_type(unreal.PCGSampleTextureSettings)
    ls2.set_editor_property("texture_mapping_method", unreal.PCGTextureMappingMethod.PLANAR)
    ls2.set_editor_property("density_merge_function", unreal.PCGDensityMergeOperation.SET)
    ls2.set_editor_property("clamp_output_density", False)
    graph.add_edge(surf, "Out", lire, "Point")
    graph.add_edge(sampler, "Out", lire, "BaseTexture")

    # RIEN NE POUSSE SOUS LA MER. Le generateur classe en biome TERRESTRE du
    # relief situe sous le niveau zero : mesure sur le banc, 98 points semes
    # sous l'eau, dont 48 % en Plage et 41 % en Desert froid, jusqu'a -11,5 m.
    # Ce n'est pas un defaut d'alignement (ecart moyen entre l'altitude du
    # generateur et le Z du monde : 0,0 m) mais un defaut de CLASSIFICATION, a
    # corriger en amont dans worldgen. En attendant, on coupe ici.
    # Reserve connue : ce filtre ne traite que l'ocean. Un terrain situe sous la
    # surface d'un LAC reste semable, faute de connaitre le niveau de chaque lac
    # dans le graphe.
    sec, secs = graph.add_node_of_type(unreal.PCGAttributeFilteringSettings)
    secs.get_editor_property("target_attribute").import_text("$Position.Z")
    secs.set_editor_property("operator", unreal.PCGAttributeFilterOperator.GREATER_OR_EQUAL)
    secs.set_editor_property("use_constant_threshold", True)
    seuil = secs.get_editor_property("attribute_types")
    seuil.set_editor_property("type", unreal.PCGMetadataTypes.DOUBLE)
    seuil.set_editor_property("double_value", float(SEA_LEVEL_CM))
    secs.set_editor_property("attribute_types", seuil)
    graph.add_edge(lire, "Out", sec, "In")
    # Le noeud de filtrage a DEUX sorties : on garde ce qui est au-dessus du
    # niveau de la mer, donc "InsideFilter".
    project, sortie_project = sec, "InsideFilter"

    for spacing_name, layers in by_spacing.items():
        texel = float(spacings[spacing_name])

        # Decimation : un pas deux fois plus large, c'est quatre fois moins de
        # points. `PCGSelectPoints` ne touche pas a la densite, donc
        # l'identifiant de biome traverse intact.
        if texel > finest:
            sel, sels = graph.add_node_of_type(unreal.PCGSelectPointsSettings)
            sels.set_editor_property("ratio", (finest / texel) ** 2)
            sels.set_editor_property("keep_zero_density_points", True)
            graph.add_edge(project, sortie_project, sel, _pin(sel))
            source, sortie = sel, "Out"
        else:
            source, sortie = project, sortie_project

        for bname, biome_id, layer in layers:
            # La densite vaut identifiant / 255 : une bande d'un demi-cran de
            # part et d'autre isole un identifiant et un seul.
            filt, fs = graph.add_node_of_type(unreal.PCGDensityFilterSettings)
            fs.set_editor_property("lower_bound", (biome_id - 0.5) / 255.0)
            fs.set_editor_property("upper_bound", (biome_id + 0.5) / 255.0)
            graph.add_edge(source, sortie, filt, _pin(filt))

            lo, hi = layer["scale"]
            # SURFACE MINERALE. La carte lue par le PCG porte l'identifiant
            # DECALE DE 100 la ou le sol est peint en roche, neige ou sable
            # (export_biome_texture.masque_mineral). Un biome occupe donc DEUX
            # bandes : `id` sur sol vivant, `id + 100` sur sol mineral.
            #
            # Le TAPIS ne prend que la premiere : de l'herbe sur un eboulis ou
            # sur du sable ne trompe personne. Mesure sur la graine 20260909
            # avant correction : 39,6 % du tapis tombait sur du mineral, et
            # jusqu'a 99,9 % en Alpin.
            #
            # Les arbres et le sous-bois prennent les DEUX : un versant raide
            # reste boise meme quand la roche affleure entre les troncs. On leur
            # ajoute donc un second filtre, branche sur le meme noeud de
            # transformation -- PCG reunit les deux entrees.
            # Depuis le 11 septembre 2026 le tapis est en TROIS couches --
            # `tapis` (la petite touffe teintee par la RVT), `tapis_haut`
            # (celle d'Orasot) et `tapis_accents` (fleurs, ble, lierre). Le
            # test porte donc sur le PREFIXE : les trois doivent rester hors du
            # sol mineral, pas seulement la premiere. Tester l'egalite avec
            # "tapis" laisserait `tapis_haut` semer de l'herbe sur les eboulis.
            filtres = [filt]
            if not str(layer.get("name", "")).startswith("tapis"):
                idm = biome_id + MINERAL_ID_OFFSET
                f2, f2s = graph.add_node_of_type(unreal.PCGDensityFilterSettings)
                f2s.set_editor_property("lower_bound", (idm - 0.5) / 255.0)
                f2s.set_editor_property("upper_bound", (idm + 0.5) / 255.0)
                graph.add_edge(source, sortie, f2, _pin(f2))
                filtres.append(f2)
            # PLACEMENT FIN. Deux etages optionnels, dans cet ordre : la
            # pente decide de ce qui tient au sol, les taches de ce qui pousse
            # en colonies. Une couche qui n'en declare aucun traverse sans
            # ajouter un seul noeud.
            amont = [(f, "Out") for f in filtres]
            amont = _etage_pente(graph, layer, amont)
            amont = _etage_taches(graph, layer, amont)

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
            for n, pin in amont:
                graph.add_edge(n, pin, xf, "In")

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

                # ECLAIRAGE.
                #
                # ATTENTION, `FPCGSoftISMComponentDescriptor` est une struct dont
                # `dir()` ne montre RIEN : ces champs n'existent que par leur nom
                # (voir ISMComponentDescriptor.h). Une faute de frappe passe donc
                # inapercue jusqu'a l'execution.
                #
                # CE QUI A ETE ESSAYE ET RETIRE. En passant le semis de 304 a
                # 6 200 instances a l'hectare, l'ombre portee du tapis et les
                # ombres de contact avaient ete coupees "par precaution". Verdict
                # a l'image, sans appel : les plantes ne touchaient plus le sol,
                # elles flottaient. Et la mesure ne justifiait pas la coupe --
                # 114 a 120 FPS avec, pour un budget de 16,67 ms. On ne coupe pas
                # une ombre sans l'avoir payee.
                #
                # Ce qui reste coupe ne se voit PAS :
                # - `cast_far_shadow` ne concerne que la cascade d'ombres
                #   lointaines, au-dela de la distance ou l'instance disparait
                #   deja par `instance_end_cull_distance` ;
                # - les champs de distance ne servent qu'a l'occlusion ambiante
                #   a grande echelle, ou un brin d'herbe ne pese rien, et ils
                #   coutent une representation par maillage.
                desc.set_editor_property("cast_far_shadow", False)
                if not layer.get("ombre", True):
                    desc.set_editor_property("affect_distance_field_lighting", False)
                # Ordre de priorite : un materiau IMPOSE par la recette prime
                # sur la redirection automatique vers les copies greffees, qui
                # prime elle-meme sur le contournement "RVT absente". Les deux
                # premiers sont des choix deliberes, le dernier un rattrapage.
                overrides = (materiau_force(ref, mesh, rec)
                             or overrides_greffe(mesh)
                             or rvt_free_materials(mesh))
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


def set_runtime_enabled(label: str, enabled: bool) -> None:
    """Allume ou eteint une tuile SANS jamais reappliquer une valeur inchangee.

    `UPCGComponent::OnRefresh` commence par `check(!IsManagedByRuntimeGenSystem())`
    (PCGComponent.cpp:2968). Or `set_editor_property` declenche un
    PostEditChangeProperty MEME quand la valeur ne change pas, et celui-ci met un
    refresh en file. Sur un composant deja en GenerateAtRuntime, ce refresh fait
    tomber l'editeur sur l'assertion, au tick suivant - donc pas dans l'appel
    fautif, ce qui rend le lien difficile a voir. Paye comptant : editeur perdu
    en pleine sauvegarde.

    Regle : on eteint TOUJOURS le mode execution avant de toucher au reste, et on
    ne le rallume qu'en dernier, une seule fois.
    """
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    hits = [a for a in sub.get_all_level_actors() if a.get_actor_label() == label]
    if not hits:
        log("ERROR", "acteur introuvable : {}".format(label))
        return
    comp = hits[0].get_component_by_class(unreal.PCGComponent)
    runtime_now = (comp.get_editor_property("generation_trigger")
                   == unreal.PCGComponentGenerationTrigger.GENERATE_AT_RUNTIME)
    if runtime_now == enabled:
        log("SKIPPED", "{} est deja {}".format(label, "actif" if enabled else "inerte"))
        return
    if enabled:
        comp.set_editor_property("generation_trigger",
                                 unreal.PCGComponentGenerationTrigger.GENERATE_AT_RUNTIME)
    else:
        comp.set_editor_property("generation_trigger",
                                 unreal.PCGComponentGenerationTrigger.GENERATE_ON_DEMAND)
        comp.cleanup(True)
    log("MODIFIED", "{} -> {}".format(label, "execution" if enabled else "inerte"))


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
    # L'ORDRE EST CRITIQUE, et il coute cher a l'envers. Le descripteur de grille
    # est construit avec `SetIsRuntime(IsManagedByRuntimeGenSystem())`
    # (PCGComponent.cpp:202), et `IsManagedByRuntimeGenSystem()` vaut exactement
    # `GenerationTrigger == GenerateAtRuntime` (PCGComponent.h:526). Si l'on
    # partitionne AVANT d'avoir pose le declencheur, le descripteur n'est pas
    # runtime et PCG ecrit un PCGPartitionActor PERSISTANT par maille sous
    # Content/__ExternalActors__ : mesure sur ce monde, 15 876 acteurs et 847 Mo
    # pour zero instance. Declencheur d'abord, partitionnement ensuite.
    comp.set_editor_property(
        "generation_trigger",
        unreal.PCGComponentGenerationTrigger.GENERATE_AT_RUNTIME if runtime
        else unreal.PCGComponentGenerationTrigger.GENERATE_ON_DEMAND)
    comp.set_editor_property("is_component_partitioned", partitioned)
    # L'entree du graphe doit rendre les BORNES de l'acteur : pour un composant
    # local, c'est celles de sa maille, et c'est ce que le Bounding Shape de
    # l'echantillonneur de surface consomme.
    comp.set_editor_property("input_type", unreal.PCGComponentInput.ACTOR)
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
    tiles = tiles or tuiles_presentes(TILES_DIR)
    made = {}
    for tile in tiles:
        texture = unreal.EditorAssetLibrary.load_asset(
            "{}/T_BiomeIndex_{}".format(TILE_TEXTURE_DIR, tile))
        if not texture:
            log("ERROR", "texture de biomes absente pour la tuile {}".format(tile))
            continue
        loc, half = tile_surface(tile)
        label = "Worldseed_Vegetation_" + tile
        # RENDRE LE COMPOSANT INERTE AVANT DE RECONSTRUIRE, et c'est mesure.
        # Relancer `build_world()` sur un volume encore en `GenerateAtRuntime`
        # a produit **289 PCGPartitionActor PERSISTANTS** -- ecrits sous
        # `Content/__ExternalActors__`, donc sur le disque -- alors que le
        # nouveau volume etait pose dans le bon ordre. La pose elle-meme est
        # innocente : instrumentee pas a pas, elle donne 0 acteur a chaque
        # etape. C'est le composant VIVANT que l'on detruit qui les laisse
        # derriere lui, et PCG ne les cree qu'au tick SUIVANT, si bien qu'un
        # comptage synchrone dans le meme script ne voit rien.
        # Apres cet appel : 0 acteur persistant, deux reconstructions de suite.
        set_runtime_enabled(label, False)
        path = build_graph("{}/PCG_Vegetation_{}".format(GRAPH_DIR, tile),
                           texture, loc, half, biomes, hierarchical=True)
        place_volume(label, path, loc, half, partitioned=True, runtime=True)
        made[tile] = label
        n_persistants = count_persistent_partition_actors()
        if n_persistants:
            log("ERROR", "{} acteurs de partition PERSISTANTS apparus sur {} : "
                         "l'ordre declencheur/partitionnement est faux".format(n_persistants, tile))
            return {"tiles": made, "aborted": tile, "log": list(_log)}
    return {"tiles": made, "log": list(_log)}


def count_persistent_partition_actors() -> int:
    """Garde-fou : un PCGPartitionActor a paquet EXTERNE finit sur le disque.

    Les acteurs de partition de la generation a l'execution sont RF_Transient et
    vivent dans le paquet transitoire, donc `is_package_external()` est faux.
    Ceux de la cuisson en editeur sont ecrits sous Content/__ExternalActors__.
    """
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    return len([a for a in sub.get_all_level_actors()
                if isinstance(a, unreal.PCGPartitionActor) and a.is_package_external()])
