# -*- coding: utf-8 -*-
"""Greffe la Runtime Virtual Texture sur les materiaux du pack qui l'ignorent.

POURQUOI CE SCRIPT EXISTE
-------------------------
Le terrain ECRIT sa couleur dans une Runtime Virtual Texture ; un materiau de
feuillage peut la RELIRE et teinter la plante a la couleur du sol sous elle.
C'est ce qui donne aux rendus du pack Orasot leur coherence, et c'est ce qui
manque a notre monde : la meme herbe verte etait semee en savane comme en
foret tropicale.

MESURE DE DEPART (11 septembre 2026) : sur les 142 maillages du semis, **38
seulement (27 %)** passent par un materiau qui echantillonne la RVT. Quatre
materiaux le font -- `M_Assets_MasterMat`, sa variante masquee,
`M_Master_Cliff_Mat` et le `M_Grass` de `Stylized_Landscape_5_Bioms`. Les deux
premiers ne le font qu'a l'INTERIEUR de la fonction `MF_RVT` : un balayage qui
ne regarde que le premier niveau du graphe les rate.

LE PIEGE QUI A COUTE LE PLUS DE TEMPS : **il existe DEUX materiaux nommes
`M_Grass` dans le pack**, et ils n'ont rien en commun.
    LowPolyForestVol2/Materials/M_Grass            MASQUE, feuillage deux faces
    Stylized_Landscape_5_Bioms/Global/M_Grass      OPAQUE, echantillonne la RVT
Comparer des `get_name()` au lieu des CHEMINS fait conclure que les deux herbes
partagent un parent et que seule l'instance differe. C'est faux, et la
consequence est visible : imposer le materiau OPAQUE au maillage a 4 triangles
-- qui n'est que deux quads croises et depend d'un masque alpha -- le
transforme en carre plein couleur sable. La silhouette de l'herbe d'Orasot,
elle, vient de sa GEOMETRIE (360 triangles), d'ou un materiau opaque qui lui
convient. **Comparer des chemins, jamais des noms.**

CE QUE LA GREFFE FAIT
---------------------
Elle ne touche PAS aux assets du pack. Pour chaque cible :
  1. le maitre est duplique sous `/Game/Worldseed/PCG/Materials/` ;
  2. on y ajoute un `RuntimeVirtualTextureSample` sur `RVT_Landscape_Material`,
     en `BaseColor_Normal_Specular_Mask_YCoCg` -- le type DOIT correspondre a
     celui de la RVT, sans quoi le noeud retombe sur `BaseColor` et lit des
     octets empaquetes en YCoCg comme s'ils etaient du RGB ;
  3. la couleur de base d'origine et celle du sol entrent dans un
     `LinearInterpolate` dose par un scalaire `Teinte RVT` ;
  4. le Lerp devient la nouvelle `BaseColor`. **Toutes les autres sorties sont
     laissees telles quelles** -- en particulier `OpacityMask`, sans quoi la
     silhouette du feuillage disparait ;
  5. chaque instance listee est dupliquee et reparentee au maitre greffe.

POURQUOI PAS `MF_RVT`, la fonction du pack. Elle fait mieux -- elle melange
couleur, speculaire, rugosite ET normale, avec un masque calcule sur la hauteur
du monde, ce qui ANCRE l'objet dans le sol au lieu de le repeindre. Mais elle
travaille en ATTRIBUTS DE MATERIAU (`use_material_attributes`), et aucune de
nos cibles n'est cablee ainsi : les convertir, c'est recabler entierement des
graphes payants. La greffe ci-dessous est volontairement plus modeste et sans
risque. Passer a `MF_RVT` reste la bonne evolution si le rendu le demande.

OU VIVENT LES COPIES, ET POURQUOI. `/Game/Worldseed/PCG/Materials/` est exclu
de git : ces materiaux DERIVENT d'un pack payant, les redistribuer serait une
violation d'EULA. Ils se refont en une commande -- c'est tout l'objet de ce
fichier.

RESULTAT MESURE sur `LPF:SM_Env_Grass_small` (4 triangles), part de vert dans
la touffe, vue de cote contre le ciel :
                        desert            foret tropicale
    avant la greffe     51,4 % de vert    100 %
    apres la greffe      0,0 %            100 %
    herbe d'Orasot       0,0 %            100 %
soit un ecart de moins de 10 unites RVB avec l'herbe a 360 triangles, dans les
deux biomes, pour un quatre-vingt-dixieme du cout en triangles.

USAGE
-----
    import sys; sys.path.insert(0, r"<racine>/Tools/UE")
    import rvt_graft; rvt_graft.greffer()

Puis reporter les chemins rendus dans `materiaux_forces` de
`vegetation_recipes.json` et reconstruire le semis.
"""
import json

import unreal

DEST = "/Game/Worldseed/PCG/Materials"
RVT = "/Game/Orasot_Bundle/Stylized_Landscape_5_Bioms/Global/RVT/RVT_Landscape_Material"
# Le type DOIT etre celui de la RVT elle-meme. Pose a autre chose, le noeud
# retombe silencieusement sur `BaseColor` et lit du YCoCg comme du RGB.
TYPE_RVT = "BaseColor_Normal_Specular_Mask_YCoCg"

# Les cibles, dans l'ordre de leur poids en INSTANCES -- pas en maillages.
# `LowPolyForestVol2/M_Grass` ne porte qu'un maillage, mais ce maillage est le
# tapis : environ 60 % de toutes les instances du monde.
CIBLES = [
    {
        "maitre": "/Game/Orasot_Bundle/LowPolyForestVol2/Materials/M_Grass",
        "suffixe": "GrassRas",
        "teinte": 0.85,
        "instances": ["/Game/Orasot_Bundle/LowPolyForestVol2/Material_Instances/MI_Grass_Inst"],
    },
]

_log = []


def log(genre, message):
    ligne = "{}: {}".format(genre, message)
    _log.append(ligne)
    unreal.log(ligne)
    print(ligne)


def _expressions(chemin):
    return json.loads(unreal.MaterialNodeService.export_material_graph(chemin))


def _source_de(donnees, propriete):
    """Expression branchee sur une sortie du materiau, ou None."""
    for o in donnees.get("output_connections", []):
        if o.get("property") == propriete:
            return o
    return None


def greffer_un(cible):
    """Greffe une cible et rend {chemin du maitre greffe, instances creees}."""
    src = cible["maitre"]
    dst = "{}/M_Worldseed{}".format(DEST, cible["suffixe"])
    dst_mi = "{}/MI_Worldseed{}".format(DEST, cible["suffixe"])

    # IDEMPOTENCE PAR CONSTAT, ET NON PAR DESTRUCTION -- et c'est une lecon
    # payee comptant. `delete_asset` sur un materiau deja greffe rend False
    # sans lever d'erreur (l'editeur de materiaux le tient encore ; ni
    # `close_all_editors_for_asset`, ni `collect_garbage`, ni
    # `delete_loaded_asset` n'y changent rien, et le registre ne signale
    # AUCUN referenceur). `duplicate_asset` rend alors l'asset deja present et
    # la greffe se pose PAR-DESSUS la precedente : deux echantillonneurs, deux
    # Lerp chaines, teinte appliquee deux fois.
    # Donc : si la copie existe deja et porte exactement une greffe saine, on
    # n'y touche pas ; si elle est malformee, on s'arrete en le disant.
    if unreal.EditorAssetLibrary.does_asset_exist(dst):
        etat = verifier(dst)
        saine = (etat["echantillons"] == 1
                 and etat["sorties"].get("BaseColor") == "LinearInterpolate")
        if not saine:
            log("ERROR", "{} existe deja mais sa greffe est malformee ({} "
                         "echantillons, BaseColor <- {}). Le supprimer a la main "
                         "dans le navigateur de contenu, puis relancer."
                .format(dst, etat["echantillons"], etat["sorties"].get("BaseColor")))
            return None
        log("SKIPPED", "{} porte deja sa greffe, laisse tel quel".format(dst))
        return _instances(cible, unreal.EditorAssetLibrary.load_asset(dst), dst, dst_mi)

    unreal.EditorAssetLibrary.duplicate_asset(src, dst)
    log("CREATED", "{} (copie de {})".format(dst, src.rsplit("/", 1)[-1]))
    avant = _expressions(dst)
    base = _source_de(avant, "BaseColor")
    if base is None:
        log("ERROR", "{} n'a rien sur BaseColor : greffe impossible".format(src))
        return None

    faits = unreal.MaterialNodeService.batch_create_expressions(
        dst, ["RuntimeVirtualTextureSample", "LinearInterpolate", "ScalarParameter"],
        [-900, -500, -900], [-400, -250, -150])
    if len(faits) != 3:
        log("ERROR", "creation des noeuds incomplete sur {}".format(dst))
        return None

    apres = _expressions(dst)
    connus = {e["id"] for e in avant["expressions"]}
    neufs = {}
    for e in apres["expressions"]:
        if e["id"] not in connus:
            neufs[e["class"]] = e
    rvt, lerp, scal = (neufs.get("RuntimeVirtualTextureSample"),
                       neufs.get("LinearInterpolate"), neufs.get("ScalarParameter"))
    if not (rvt and lerp and scal):
        log("ERROR", "noeuds neufs introuvables sur {}".format(dst))
        return None

    n = unreal.MaterialNodeService.batch_set_properties(
        dst,
        [rvt["id"], rvt["id"], scal["id"], scal["id"]],
        ["VirtualTexture", "MaterialType", "ParameterName", "DefaultValue"],
        ["/Script/Engine.RuntimeVirtualTexture'{}.{}'".format(RVT, RVT.rsplit("/", 1)[-1]),
         TYPE_RVT, "Teinte RVT", "{:.6f}".format(float(cible["teinte"]))])
    if n != 4:
        log("ERROR", "{} proprietes posees sur 4 ({})".format(n, dst))
        return None

    c = unreal.MaterialNodeService.batch_connect_expressions(
        dst,
        [base["expression_id"], rvt["id"], scal["id"]],
        [base.get("output_name") or "", "BaseColor", ""],
        [lerp["id"], lerp["id"], lerp["id"]],
        ["A", "B", "Alpha"])
    if c != 3:
        log("ERROR", "{} connexions sur 3 ({})".format(c, dst))
        return None

    mat = unreal.EditorAssetLibrary.load_asset(dst)
    objets = list(unreal.MaterialEditingLibrary.get_material_expressions(mat))
    lerp_obj = None
    for o in objets:
        if isinstance(o, unreal.MaterialExpressionLinearInterpolate):
            lerp_obj = o
    if lerp_obj is None or not unreal.MaterialEditingLibrary.connect_material_property(
            lerp_obj, "", unreal.MaterialProperty.MP_BASE_COLOR):
        log("ERROR", "BaseColor non rebranchee sur {}".format(dst))
        return None
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(dst)
    log("MODIFIED", "{} : BaseColor <- Lerp(origine, sol, Teinte RVT={})".format(
        dst.rsplit("/", 1)[-1], cible["teinte"]))

    return _instances(cible, mat, dst, dst_mi)


def _instances(cible, mat, dst, dst_mi):
    """Duplique les instances de l'auteur et les reparente au maitre greffe."""
    instances = {}
    for chemin_mi in cible["instances"]:
        if unreal.EditorAssetLibrary.does_asset_exist(dst_mi):
            mi = unreal.EditorAssetLibrary.load_asset(dst_mi)
            log("SKIPPED", "{} existe deja".format(dst_mi))
        else:
            mi = unreal.EditorAssetLibrary.duplicate_asset(chemin_mi, dst_mi)
            if mi is None:
                log("ERROR", "duplication impossible : {}".format(chemin_mi))
                continue
            log("CREATED", "{} (copie de {})".format(dst_mi, chemin_mi.rsplit("/", 1)[-1]))
        # Reparenter garde les surcharges de l'auteur -- ici la texture `Mask`,
        # le mode MASQUE et l'ombrage feuillage deux faces. Sans elles, plus de
        # silhouette du tout : le maillage n'est que deux quads croises et
        # rendrait un carre plein. C'est tout l'interet de partir de SON
        # instance plutot que d'en creer une vierge.
        mi.set_editor_property("parent", mat)
        unreal.MaterialEditingLibrary.update_material_instance(mi)
        unreal.EditorAssetLibrary.save_asset(dst_mi)
        instances[chemin_mi.rsplit("/", 1)[-1]] = dst_mi
    return {"maitre": dst, "instances": instances}


def greffer(cibles=None):
    """Greffe toutes les cibles. Idempotent : relancer ecrase les copies."""
    _log.clear()
    faits = {}
    for cible in (cibles or CIBLES):
        r = greffer_un(cible)
        if r:
            faits[cible["maitre"]] = r
    return {"greffes": faits, "log": list(_log)}


def verifier(chemin):
    """Le materiau echantillonne-t-il vraiment la RVT, et sur quelle sortie ?"""
    d = _expressions(chemin)
    ex = {e["id"]: e for e in d["expressions"]}
    n = sum(1 for e in d["expressions"] if e["class"] == "RuntimeVirtualTextureSample")
    sorties = {o["property"]: ex.get(o["expression_id"], {}).get("class", "?")
               for o in d.get("output_connections", [])}
    return {"echantillons": n, "sorties": sorties}
