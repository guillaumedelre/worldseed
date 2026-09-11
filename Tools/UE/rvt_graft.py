# -*- coding: utf-8 -*-
"""Greffe la Runtime Virtual Texture sur les materiaux du pack qui l'ignorent.

POURQUOI CE SCRIPT EXISTE
-------------------------
Le terrain ECRIT sa couleur dans une Runtime Virtual Texture ; un materiau de
feuillage peut la RELIRE et prendre le ton du sol sous lui. C'est ce qui donne
aux rendus du pack Orasot leur coherence, et c'est ce qui manquait a notre
monde : la meme herbe verte etait semee en savane comme en foret tropicale.

MESURE DE DEPART (11 septembre 2026) : sur les 142 maillages du semis, **38
seulement (27 %)** passent par un materiau qui echantillonne la RVT --
`M_Assets_MasterMat`, sa variante masquee, `M_Master_Cliff_Mat` et le `M_Grass`
de `Stylized_Landscape_5_Bioms`. Les deux premiers ne le font qu'a l'INTERIEUR
de la fonction `MF_RVT` : un balayage qui ne regarde que le premier niveau du
graphe les rate.

LE FONDU EN HAUTEUR, ET POURQUOI IL EST INDISPENSABLE
-----------------------------------------------------
Teinter un objet ENTIER a la couleur du sol n'a de sens que pour un brin
d'herbe. Applique tel quel a un arbre de dix metres, cela le peint en terre.
La fonction du pack, `MF_RVT`, calcule donc son melange sur la HAUTEUR DU
MONDE : le pied de l'objet se fond dans le sol, la cime garde sa couleur.

La greffe reprend ce principe :

    hauteur = WorldPosition.Z - RVT_Landscape_Height.WorldHeight
    alpha   = "Teinte RVT" * saturate(1 - hauteur / "Hauteur fondu RVT")
    BaseColor = Lerp(couleur d'origine, couleur du sol, alpha)

Au ras du sol l'alpha vaut la teinte demandee ; a `Hauteur fondu RVT`
centimetres au-dessus, il vaut zero. Un couvre-sol prend donc tout le ton du
terrain, un tronc seulement son pied, une cime rien du tout.

CE QUE LA GREFFE NE FAIT PAS. Elle ne touche qu'a `BaseColor` et laisse TOUTES
les autres sorties intactes -- en particulier `OpacityMask`, sans quoi la
silhouette du feuillage disparait. `MF_RVT` va plus loin (speculaire, rugosite
et normale aussi), mais elle travaille en ATTRIBUTS DE MATERIAU
(`use_material_attributes`) et aucune de nos cibles n'est cablee ainsi :
les convertir reviendrait a recabler entierement des graphes payants.

PIEGES PAYES COMPTANT
---------------------
**DEUX MATERIAUX PEUVENT PORTER LE MEME NOM. Comparer des CHEMINS, jamais des
`get_name()`.**
    LowPolyForestVol2/Materials/M_Grass            MASQUE, feuillage deux faces
    Stylized_Landscape_5_Bioms/Global/M_Grass      OPAQUE, echantillonne la RVT
Avoir compare les noms a fait conclure que les deux herbes partageaient un
parent et qu'il suffisait d'imposer l'instance de l'une a l'autre. Resultat a
l'image : un CARRE PLEIN couleur sable. Le petit maillage n'est que deux quads
croises et depend d'un masque alpha ; la silhouette de l'herbe d'Orasot, elle,
vient de sa GEOMETRIE (360 triangles), d'ou un materiau opaque qui lui convient.

**`delete_asset` sur un materiau rend `False` sans lever d'erreur**, meme quand
le registre ne signale AUCUN referenceur, et ni `close_all_editors_for_asset`,
ni `collect_garbage`, ni `delete_loaded_asset` n'y changent rien.
`duplicate_asset` rend alors l'asset deja present et la greffe se pose
PAR-DESSUS la precedente. D'ou l'idempotence PAR CONSTAT : si la copie porte
deja une greffe saine, on n'y touche pas.

OU VIVENT LES COPIES. `/Game/Worldseed/PCG/Materials/` est exclu de git : ces
materiaux DERIVENT d'un pack payant, les redistribuer violerait l'EULA. Ils se
refont en une commande -- c'est tout l'objet de ce fichier.

USAGE
-----
    import sys; sys.path.insert(0, r"<racine>/Tools/UE")
    import rvt_graft; rvt_graft.greffer()

Les chemins rendus vont dans `materiaux_forces` de `vegetation_recipes.json`,
apres quoi `vegetation.build_world()` recable le semis.
"""
import json

import unreal

DEST = "/Game/Worldseed/PCG/Materials"
RVT_COULEUR = "/Game/Orasot_Bundle/Stylized_Landscape_5_Bioms/Global/RVT/RVT_Landscape_Material"
RVT_HAUTEUR = "/Game/Orasot_Bundle/Stylized_Landscape_5_Bioms/Global/RVT/RVT_Landscape_Height"
# Le type DOIT etre celui de la RVT visee. Pose a autre chose, le noeud retombe
# silencieusement sur `BaseColor` et lit du YCoCg empaquete comme du RGB.
TYPE_COULEUR = "BaseColor_Normal_Specular_Mask_YCoCg"
TYPE_HAUTEUR = "WorldHeight"

P_TEINTE = "Teinte RVT"
P_FONDU = "Hauteur fondu RVT"

# Les cibles, dans l'ordre de leur poids en INSTANCES du semis -- pas en
# nombre de maillages. `fondu` est la hauteur en centimetres au-dela de
# laquelle l'objet ne prend plus rien du sol : elle se regle sur la TAILLE de
# ce que le materiau habille.
CIBLES = [
    # --- couvre-sol : teinte franche, fondu court -------------------------
    # `M_Foliage_Master` porte NOTRE TAPIS (PBF:SM_Grass, PBF:SM_Clover). C'est
    # la cible la plus importante du lot : le tapis pese 67 % des instances.
    # Le fondu doit couvrir TOUTE la hauteur de ce qu'il habille, sinon seule
    # la base se teinte et le haut du brin reste vert : mesure en savane, un
    # fondu de 90 cm sur une herbe de 74 a 170 cm laissait le sommet etranger
    # au sol -- tres visible de pres, invisible de loin.
    {"maitre": "/Game/Stylized_PBR_Nature/Foliage/Materials/M_Foliage_Master",
     "suffixe": "FoliagePBF", "teinte": 0.85, "fondu": 180.0},
    # --- feuillage bas et fleurs : fondu moyen ---------------------------
    {"maitre": "/Game/Orasot_Bundle/StylizedForestLandscape/Materials/M_Foliage_Flower_Master",
     "suffixe": "FleurSFL", "teinte": 0.55, "fondu": 120.0},
    {"maitre": "/Game/Orasot_Bundle/Stylized_Landscape_5_Bioms/Global/Materials/M_Master_Flower",
     "suffixe": "FleurGlobal", "teinte": 0.55, "fondu": 120.0},
    # --- buissons et feuilles d'arbres : fondu long, teinte discrete -----
    {"maitre": "/Game/Orasot_Bundle/LowPolyForestVol2/Materials/M_MasterMat",
     "suffixe": "MasterLPF", "teinte": 0.45, "fondu": 400.0},
    {"maitre": "/Game/Orasot_Bundle/LowPolyForestVol2/Materials/M_MasterMat_TwoSided",
     "suffixe": "MasterLPF2F", "teinte": 0.45, "fondu": 400.0},
    {"maitre": "/Game/Orasot_Bundle/Stylized_Landscape_5_Bioms/Global/Materials/M_Master_Leaf",
     "suffixe": "FeuilleGlobal", "teinte": 0.40, "fondu": 400.0},
    {"maitre": "/Game/Orasot_Bundle/StylizedForestLandscape/Materials/M_Leaf_Master",
     "suffixe": "FeuilleSFL", "teinte": 0.40, "fondu": 400.0},
    # --- troncs, rochers et props : le pied se salit, le reste non -------
    {"maitre": "/Game/Stylized_PBR_Nature/Foliage/Materials/M_Tree_Trunk_Master",
     "suffixe": "TroncPBF", "teinte": 0.65, "fondu": 200.0},
    {"maitre": "/Game/Orasot_Bundle/StylizedForestLandscape/Materials/M_Bark_Master",
     "suffixe": "EcorceSFL", "teinte": 0.65, "fondu": 200.0},
    {"maitre": "/Game/Stylized_PBR_Nature/Rocks/Materials/M_Rock_Master",
     "suffixe": "RochePBR", "teinte": 0.70, "fondu": 250.0},
    {"maitre": "/Game/Orasot_Bundle/StylizedForestLandscape/Materials/M_Master_Material_Prop",
     "suffixe": "PropSFL", "teinte": 0.60, "fondu": 200.0},
]

# `LowPolyForestVol2/Materials/M_Grass` n'est PAS dans la liste. Son seul
# maillage, `SM_Env_Grass_small` (4 triangles), a ete essaye comme tapis puis
# ecarte : agrandi pour retrouver la hauteur d'un brin d'herbe, il ne lit plus
# comme une touffe mais comme un PIEU -- deux quads croises ne survivent pas a
# l'agrandissement. Une copie greffee de ce materiau traine peut-etre encore
# sous /Game/Worldseed/PCG/Materials/M_WorldseedGrassRas : elle est orpheline.

_log = []


def log(genre, message):
    ligne = "{}: {}".format(genre, message)
    _log.append(ligne)
    unreal.log(ligne)
    print(ligne)


def _graphe(chemin):
    return json.loads(unreal.MaterialNodeService.export_material_graph(chemin))


def verifier(chemin):
    """Le materiau porte-t-il une greffe, et de quelle forme ?"""
    d = _graphe(chemin)
    ex = {e["id"]: e for e in d["expressions"]}
    noms = {(e.get("parameter_name") or "") for e in d["expressions"]}
    return {
        "echantillons": sum(1 for e in d["expressions"]
                            if e["class"] == "RuntimeVirtualTextureSample"),
        "teinte": P_TEINTE in noms,
        "fondu": P_FONDU in noms,
        "sorties": {o["property"]: ex.get(o["expression_id"], {}).get("class", "?")
                    for o in d.get("output_connections", [])},
    }


def _saine(etat):
    """Une greffe complete : deux echantillons (couleur + hauteur), les deux
    parametres, et la couleur de base pilotee par le Lerp."""
    return (etat["echantillons"] == 2 and etat["teinte"] and etat["fondu"]
            and etat["sorties"].get("BaseColor") == "LinearInterpolate")


def greffer_un(cible):
    src = cible["maitre"]
    dst = "{}/M_Worldseed{}".format(DEST, cible["suffixe"])

    if not unreal.EditorAssetLibrary.does_asset_exist(src):
        log("ERROR", "maitre introuvable : {}".format(src))
        return None

    if unreal.EditorAssetLibrary.does_asset_exist(dst):
        etat = verifier(dst)
        if not _saine(etat):
            log("ERROR", "{} existe mais sa greffe est incomplete ({} echantillons, "
                         "teinte={}, fondu={}, BaseColor <- {}). Le supprimer a la main "
                         "dans le navigateur de contenu, puis relancer."
                .format(dst, etat["echantillons"], etat["teinte"], etat["fondu"],
                        etat["sorties"].get("BaseColor")))
            return None
        log("SKIPPED", "{} porte deja sa greffe".format(dst.rsplit("/", 1)[-1]))
        return {"maitre": dst}

    unreal.EditorAssetLibrary.duplicate_asset(src, dst)
    log("CREATED", "{} (copie de {})".format(dst.rsplit("/", 1)[-1], src.rsplit("/", 1)[-1]))

    avant = _graphe(dst)
    base = next((o for o in avant.get("output_connections", [])
                 if o.get("property") == "BaseColor"), None)
    if base is None:
        log("ERROR", "{} n'a rien branche sur BaseColor : greffe impossible".format(src))
        return None

    classes = ["RuntimeVirtualTextureSample",   # 0 couleur du sol
               "LinearInterpolate",             # 1 melange
               "ScalarParameter",               # 2 Teinte RVT
               "RuntimeVirtualTextureSample",   # 3 hauteur du sol
               "WorldPosition",                 # 4 (sa sortie Z suffit)
               "Subtract",                      # 5 Z - hauteur du sol
               "Divide",                        # 6 / hauteur de fondu
               "ScalarParameter",               # 7 Hauteur fondu RVT
               "OneMinus",                      # 8
               "Clamp",                         # 9 saturate
               "Multiply"]                      # 10 x teinte
    xs = [-1500, -700, -1500, -1500, -1900, -1600, -1450, -1600, -1300, -1150, -1000]
    ys = [-700, -450, -1450, -1200, -1000, -1100, -1150, -1000, -1150, -1150, -1250]
    faits = unreal.MaterialNodeService.batch_create_expressions(dst, classes, xs, ys)
    if len(faits) != len(classes):
        log("ERROR", "creation incomplete sur {} ({} sur {})".format(dst, len(faits), len(classes)))
        return None

    apres = _graphe(dst)
    connus = {e["id"] for e in avant["expressions"]}
    neufs = [e for e in apres["expressions"] if e["id"] not in connus]
    par_classe = {}
    for e in neufs:
        par_classe.setdefault(e["class"], []).append(e)
    try:
        rvt_c, rvt_h = par_classe["RuntimeVirtualTextureSample"]
        lerp = par_classe["LinearInterpolate"][0]
        teinte, fondu = par_classe["ScalarParameter"]
        wpos = par_classe["WorldPosition"][0]
        soustr = par_classe["Subtract"][0]
        divis = par_classe["Divide"][0]
        unmoins = par_classe["OneMinus"][0]
        borne = par_classe["Clamp"][0]
        mult = par_classe["Multiply"][0]
    except (KeyError, ValueError) as err:
        log("ERROR", "noeuds neufs inattendus sur {} : {}".format(dst, err))
        return None

    ids, props, vals = [], [], []
    def poser(nid, nom, val):
        ids.append(nid); props.append(nom); vals.append(val)
    poser(rvt_c["id"], "VirtualTexture",
          "/Script/Engine.RuntimeVirtualTexture'{0}.{1}'".format(
              RVT_COULEUR, RVT_COULEUR.rsplit("/", 1)[-1]))
    poser(rvt_c["id"], "MaterialType", TYPE_COULEUR)
    poser(rvt_h["id"], "VirtualTexture",
          "/Script/Engine.RuntimeVirtualTexture'{0}.{1}'".format(
              RVT_HAUTEUR, RVT_HAUTEUR.rsplit("/", 1)[-1]))
    poser(rvt_h["id"], "MaterialType", TYPE_HAUTEUR)
    poser(teinte["id"], "ParameterName", P_TEINTE)
    poser(teinte["id"], "DefaultValue", "{:.6f}".format(float(cible["teinte"])))
    poser(fondu["id"], "ParameterName", P_FONDU)
    poser(fondu["id"], "DefaultValue", "{:.6f}".format(float(cible["fondu"])))
    n = unreal.MaterialNodeService.batch_set_properties(dst, ids, props, vals)
    if n != len(ids):
        log("ERROR", "{} proprietes posees sur {} ({})".format(n, len(ids), dst))
        return None

    src_ids, src_out, tgt_ids, tgt_in = [], [], [], []
    def relier(a, sortie, b, entree):
        src_ids.append(a); src_out.append(sortie); tgt_ids.append(b); tgt_in.append(entree)
    # PIEGE : pour une broche d'entree UNIQUE (`OneMinus`, `Clamp`,
    # `ComponentMask`), `batch_connect_expressions` veut une chaine VIDE, alors
    # que `export_material_graph` annonce cette broche sous le nom "Input".
    # Passer "Input" echoue en silence -- le compteur de retour rend 9 sur 12 et
    # rien ne dit lesquelles. Les broches NOMMEES (`A`, `B`, `Alpha`) se passent
    # bien par leur nom.
    relier(base["expression_id"], base.get("output_name") or "", lerp["id"], "A")
    relier(rvt_c["id"], "BaseColor", lerp["id"], "B")
    relier(mult["id"], "", lerp["id"], "Alpha")
    relier(wpos["id"], "Z", soustr["id"], "A")
    relier(rvt_h["id"], "WorldHeight", soustr["id"], "B")
    relier(soustr["id"], "", divis["id"], "A")
    relier(fondu["id"], "", divis["id"], "B")
    relier(divis["id"], "", unmoins["id"], "")
    relier(unmoins["id"], "", borne["id"], "")
    relier(borne["id"], "", mult["id"], "A")
    relier(teinte["id"], "", mult["id"], "B")
    c = unreal.MaterialNodeService.batch_connect_expressions(dst, src_ids, src_out, tgt_ids, tgt_in)
    if c != len(src_ids):
        log("ERROR", "{} connexions sur {} ({})".format(c, len(src_ids), dst))
        return None

    mat = unreal.EditorAssetLibrary.load_asset(dst)
    objets = list(unreal.MaterialEditingLibrary.get_material_expressions(mat))
    lerp_obj = next((o for o in reversed(objets)
                     if isinstance(o, unreal.MaterialExpressionLinearInterpolate)), None)
    if lerp_obj is None or not unreal.MaterialEditingLibrary.connect_material_property(
            lerp_obj, "", unreal.MaterialProperty.MP_BASE_COLOR):
        log("ERROR", "BaseColor non rebranchee sur {}".format(dst))
        return None
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(dst)

    etat = verifier(dst)
    if not _saine(etat):
        log("ERROR", "greffe posee mais non conforme sur {} : {}".format(dst, etat))
        return None
    log("MODIFIED", "{} : teinte {} sur {} cm de fondu".format(
        dst.rsplit("/", 1)[-1], cible["teinte"], cible["fondu"]))
    return {"maitre": dst}


def greffer(cibles=None):
    """Greffe toutes les cibles. Idempotent : une greffe saine est laissee."""
    _log.clear()
    faits = {}
    for cible in (cibles or CIBLES):
        r = greffer_un(cible)
        if r:
            faits[cible["maitre"]] = r["maitre"]
    return {"greffes": faits, "log": list(_log)}


def regler(cibles=None):
    """Reporte `teinte` et `fondu` de CIBLES sur les maitres deja greffes.

    Regler ces deux valeurs ne demande PAS de refaire la greffe : ce sont des
    defauts de parametres scalaires. Cette fonction existe parce que `greffer()`
    laisse intacte une greffe saine -- sans elle, changer un dosage obligerait a
    supprimer le materiau, ce qui echoue souvent (voir plus haut).
    """
    _log.clear()
    faits = 0
    for cible in (cibles or CIBLES):
        dst = "{}/M_Worldseed{}".format(DEST, cible["suffixe"])
        if not unreal.EditorAssetLibrary.does_asset_exist(dst):
            continue
        d = _graphe(dst)
        ids, props, vals = [], [], []
        for e in d["expressions"]:
            nom = e.get("parameter_name") or ""
            if nom == P_TEINTE:
                ids.append(e["id"]); props.append("DefaultValue")
                vals.append("{:.6f}".format(float(cible["teinte"])))
            elif nom == P_FONDU:
                ids.append(e["id"]); props.append("DefaultValue")
                vals.append("{:.6f}".format(float(cible["fondu"])))
        if not ids:
            log("ERROR", "{} ne porte pas les parametres attendus".format(dst))
            continue
        n = unreal.MaterialNodeService.batch_set_properties(dst, ids, props, vals)
        mat = unreal.EditorAssetLibrary.load_asset(dst)
        unreal.MaterialEditingLibrary.recompile_material(mat)
        unreal.EditorAssetLibrary.save_asset(dst)
        faits += 1
        log("MODIFIED", "{} : teinte {} / fondu {} cm ({} valeurs)".format(
            dst.rsplit("/", 1)[-1], cible["teinte"], cible["fondu"], n))
    return {"regles": faits, "log": list(_log)}


# ------------------------------------------------------- redirection du semis

TABLE = r"D:\UE\Worldseed\Tools\UE\materiaux_greffes.json"


def _sans_objet(chemin_objet):
    return chemin_objet.split(".")[0]


def _copier_chaine(mat, greffes, vus):
    """Copie une chaine d'instances en la reparentant au maitre greffe.

    GREFFER LE MAITRE NE SUFFIT PAS. Nos maillages ne pointent pas vers les
    maitres du pack mais vers ses INSTANCES, qui portent les textures et les
    reglages de l'auteur. Il faut donc copier chaque instance rencontree et
    reparenter le HAUT de la chaine au maitre greffe -- reparenter garde les
    surcharges, alors qu'une instance vierge perdrait la silhouette.

    Rend le chemin de la copie, ou None si ce materiau n'a rien a voir avec une
    cible greffee.
    """
    chemin = _sans_objet(mat.get_path_name())
    if chemin in vus:
        return vus[chemin]
    if not isinstance(mat, unreal.MaterialInstance):
        # C'est un maitre : greffe ou non, on ne le copie pas ici.
        return greffes.get(chemin)

    parent = mat.get_editor_property("parent")
    if parent is None:
        return None
    copie_parent = _copier_chaine(parent, greffes, vus)
    if copie_parent is None:
        return None          # la racine n'est pas une cible : rien a faire

    dst = "{}/MI_Wsd_{}".format(DEST, mat.get_name().replace("MI_", "").replace("M_", ""))
    if unreal.EditorAssetLibrary.does_asset_exist(dst):
        copie = unreal.EditorAssetLibrary.load_asset(dst)
    else:
        copie = unreal.EditorAssetLibrary.duplicate_asset(chemin, dst)
        if copie is None:
            log("ERROR", "copie impossible : {}".format(chemin))
            return None
        log("CREATED", dst.rsplit("/", 1)[-1])
    copie.set_editor_property("parent", unreal.EditorAssetLibrary.load_asset(copie_parent))
    unreal.MaterialEditingLibrary.update_material_instance(copie)
    unreal.EditorAssetLibrary.save_asset(dst)
    vus[chemin] = dst
    return dst


def rediriger(recettes=r"D:\UE\Worldseed\Tools\UE\vegetation_recipes.json"):
    """Construit la table 'materiau du pack -> copie greffee' pour le semis.

    Ecrit `materiaux_greffes.json`, que `vegetation.py` relit pour poser les
    `override_materials` EMPLACEMENT PAR EMPLACEMENT -- un maillage a plusieurs
    sections peut mêler un materiau greffe et un materiau qui ne l'est pas.
    """
    _log.clear()
    greffes = {c["maitre"]: "{}/M_Worldseed{}".format(DEST, c["suffixe"])
               for c in CIBLES
               if unreal.EditorAssetLibrary.does_asset_exist(
                   "{}/M_Worldseed{}".format(DEST, c["suffixe"]))}
    with open(recettes, encoding="utf-8") as f:
        rec = json.load(f)
    roots = rec["roots"]
    refs = set()
    for b in rec["biomes"].values():
        for c in b["layers"]:
            for r, _p in c["meshes"]:
                refs.add(r)

    vus, table = {}, {}
    for r in sorted(refs):
        rac, nom = r.split(":")
        sm = unreal.EditorAssetLibrary.load_asset("{}/{}".format(roots[rac], nom))
        if sm is None:
            continue
        for i in range(sm.get_num_sections(0)):
            mat = sm.get_material(i)
            if mat is None:
                continue
            copie = _copier_chaine(mat, greffes, vus)
            if copie:
                table[_sans_objet(mat.get_path_name())] = copie
    with open(TABLE, "w", encoding="utf-8", newline="\n") as f:
        json.dump(table, f, ensure_ascii=False, indent=2, sort_keys=True)
        f.write("\n")
    log("MODIFIED", "{} : {} materiaux rediriges".format(TABLE, len(table)))
    return {"table": table, "log": list(_log)}
