# -*- coding: utf-8 -*-
"""Reglages du materiau de terrain qui ne peuvent pas etre versionnes.

POURQUOI CE FICHIER EXISTE
--------------------------
`M_WorldseedLandscape` est une COPIE du maitre d'Orasot augmentee d'une dixieme
couche : c'est un derive d'un pack payant, donc exclu du depot (voir .gitignore
et README). Tout reglage pose dans SON GRAPHE serait donc perdu au clone
suivant, sans que rien ne le signale -- le terrain redeviendrait simplement
flou. Ce script les rejoue.

Les reglages poses sur l'INSTANCE `MI_WorldseedLandscape`, eux, sont
versionnes : `Sand UV` et `Boost` n'ont pas besoin de ce script.

CE QUI EST REGLE ICI, ET POURQUOI
---------------------------------
Le carrelage des couches. Mesure en prairie, a hauteur d'oeil : **51 % de la
moitie basse du cadre est du sol nu**, 59,7 % dans les trois premiers metres.
La vegetation ne le cache pas, et ce sol n'avait aucun grain -- de larges
trainees vertes -- parce que la texture du pack, calee pour une petite carte de
demo, est etiree sur nos 8 km.

**Le carrelage ne se regle NI par `Uv Scale` NI par
`LandscapeLayerCoords.MappingScale`**, malgre leurs noms : essayes a 0,35 et a
16, aucun effet. Il se regle sur les 12 noeuds `TextureCoordinate`, tous livres
a `UTiling = VTiling = 1`, chacun alimentant une famille de textures. Les
modifier est une simple edition de propriete, reversible, sans recablage.

    (-4720,-1666) (-4608,-1008) (-4624,-800) (-4960,-528)   les 4 HERBES
    (-6625,  976)                                           la ROCHE
    (-6574,   99) (-5552, 1872)                             les GRAVIERS
    (-5504, 3056) (-5822, 4226)                             le SABLE
    (-2928,  672)                       carte de HAUTEUR du gravier
    ( 3948, 1760) ( 3964, 2064)         bruits de DISTRIBUTION des fleurs

Les quatre noeuds d'herbe ne remontent a aucune texture depuis le materiau :
leurs textures vivent dans `MF_Grass`. On les identifie en suivant leurs
connexions jusqu'a l'appel de cette fonction -- c'est ce que fait `_herbes()`,
plutot que de se fier a des coordonnees qui bougeraient si le graphe changeait.

**NE PAS TOUCHER aux trois derniers de la liste** : la carte de hauteur du
gravier sert au MELANGE des couches, et les deux bruits de distribution
decident d'OU poussent les fleurs. Les retailler changerait le motif du monde,
pas sa finesse.

**L'HERBE OUI, LA ROCHE NON.** Mesure : l'herbe a 4 gagne 34 % d'energie haute
frequence (3,29 -> 4,41) sans le moindre artefact. La roche a 4, en revanche,
fait apparaitre un **MOIRE HEXAGONAL tres visible sur les pentes lointaines** --
verifie par A/B au meme cadrage : present a 4, attenue mais present a 2, absent
a 1. Serrer le carrelage monte la frequence spatiale, et les mip-maps d'un
Landscape ne la rattrapent pas a distance. La roche et les graviers restent
donc a la valeur de l'auteur.

REGLE GENERALE : apres tout resserrement de carrelage, CONTROLER LES PENTES
LOINTAINES, pas seulement le sol sous les pieds. Le gain est proche, le defaut
est loin.

USAGE
-----
    import sys; sys.path.insert(0, r"<racine>/Tools/UE")
    import landscape_material; landscape_material.regler()
"""
import json

import unreal

MAITRE = "/Game/Worldseed/Materials/M_WorldseedLandscape"
CARRELAGE_HERBE = 4.0

_log = []


def log(genre, message):
    ligne = "{}: {}".format(genre, message)
    _log.append(ligne)
    unreal.log(ligne)
    print(ligne)


def _graphe():
    return json.loads(unreal.MaterialNodeService.export_material_graph(MAITRE))


def _herbes(d):
    """Les `TextureCoordinate` qui alimentent un appel de `MF_Grass`.

    On les retrouve par la TOPOLOGIE et non par leur position : un graphe
    reorganise deplacerait les noeuds, pas leurs connexions.
    """
    ex = {e["id"]: e for e in d["expressions"]}

    def atteint_mf_grass(i, prof=0, vus=None):
        vus = vus if vus is not None else set()
        if prof > 4 or i in vus:
            return False
        vus.add(i)
        for c in d["connections"]:
            if c["source_id"] != i:
                continue
            t = ex[c["target_id"]]
            if "MF_Grass" in (t.get("function_path") or ""):
                return True
            if atteint_mf_grass(c["target_id"], prof + 1, vus):
                return True
        return False

    return [e for e in d["expressions"]
            if e["class"] == "TextureCoordinate" and atteint_mf_grass(e["id"])]


def regler(carrelage=CARRELAGE_HERBE):
    """Repose le carrelage des quatre couches d'herbe. Idempotent."""
    _log.clear()
    if not unreal.EditorAssetLibrary.does_asset_exist(MAITRE):
        log("ERROR", "{} absent : le recreer d'abord (voir README)".format(MAITRE))
        return {"regles": 0, "log": list(_log)}
    d = _graphe()
    herbes = _herbes(d)
    if len(herbes) != 4:
        log("ERROR", "{} noeuds d'herbe trouves au lieu de 4 : le graphe a change, "
                     "verifier avant de forcer".format(len(herbes)))
        return {"regles": 0, "log": list(_log)}
    voulu = "{:.6f}".format(float(carrelage))
    if all((e.get("properties") or {}).get("UTiling") == voulu for e in herbes):
        log("SKIPPED", "l'herbe est deja a {}".format(carrelage))
        return {"regles": 0, "log": list(_log)}
    ids, props, vals = [], [], []
    for e in herbes:
        for p in ("UTiling", "VTiling"):
            ids.append(e["id"]); props.append(p); vals.append(voulu)
    n = unreal.MaterialNodeService.batch_set_properties(MAITRE, ids, props, vals)
    if n != len(ids):
        log("ERROR", "{} proprietes posees sur {}".format(n, len(ids)))
        return {"regles": 0, "log": list(_log)}
    mat = unreal.EditorAssetLibrary.load_asset(MAITRE)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(MAITRE)
    log("MODIFIED", "carrelage des 4 couches d'herbe pose a {}".format(carrelage))
    return {"regles": 4, "log": list(_log)}


def verifier():
    """Etat du carrelage, couche par couche."""
    d = _graphe()
    herbes = {e["id"] for e in _herbes(d)}
    out = []
    for e in d["expressions"]:
        if e["class"] != "TextureCoordinate":
            continue
        p = e.get("properties") or {}
        out.append({"pos": (e["pos_x"], e["pos_y"]),
                    "herbe": e["id"] in herbes,
                    "u": p.get("UTiling"), "v": p.get("VTiling")})
    return out


# --------------------------------------------------- sable de plage, par l'altitude

P_TEINTE = "Teinte sable plage"
P_HAUTEUR = "Hauteur plage"
P_FORCE = "Palissement plage"
# MESUREE, ET MA PREMIERE VALEUR ETAIT FAUSSE. J'avais pris (1,10 / 1,12 / 0,85),
# qui baisse le bleu : cela SATURE le sable en dore au lieu de le palir. Mesure en
# A/B a eclairage egal, personnage ramene a 100 de luminance comme temoin :
#   sans greffe   R 185,2  V 121,9  B  77,8   saturation 0,580   clarte 128,3
#   (1,10/1,12/0,85)  203,1    140,3     76,7   saturation 0,622   clarte 140,0  <- PIRE
#   (1,12/1,35/1,75)  196,3    142,7    102,3   saturation 0,479   clarte 147,1  <- retenu
# Pour PALIR un sable deja tres sature, il faut RELEVER le bleu et le vert plus
# que le rouge. Un multiplicateur ne peut desaturer qu'ainsi.
TEINTE_DEFAUT = "(R=1.120000,G=1.350000,B=1.750000,A=1.000000)"
HAUTEUR_DEFAUT = 1200.0          # cm : la zone atteinte par la mer
FORCE_DEFAUT = 1.0


def _sable(d):
    """L'echantillon de `T_Sand`, et le noeud qui consomme sa couleur."""
    src = next((e["id"] for e in d["expressions"]
                if e["class"] == "TextureSample"
                and "T_Sand." in str((e.get("properties") or {}).get("Texture", ""))), None)
    if src is None:
        return None, None, None
    for c in d["connections"]:
        if c["source_id"] == src and c["source_output_name"] == "RGB":
            return src, c["target_id"], c["target_input"]
    return src, None, None


def sable_de_plage(hauteur=HAUTEUR_DEFAUT, force=FORCE_DEFAUT, teinte=TEINTE_DEFAUT):
    """Palit et jaunit le sable EN DESSOUS d'une altitude donnee. Idempotent.

    POURQUOI PAR L'ALTITUDE, ET NON PAR LE BIOME. Le materiau ne connait pas les
    biomes : il ne voit que les dix poids peints, et la plage comme le desert
    sont domines par la MEME couche `DesertSand` -- 0,84 contre 0,72. Aucun
    poids ne les separe. En revanche le materiau connait l'altitude, et une
    plage est par definition au niveau de la mer : c'est meme ce qui la definit.
    Bonus physique : les dunes cotieres du desert palissent aussi, ce qui est
    juste -- un sable lave par la mer EST plus pale.

    LA GREFFE : `Lerp(sable, sable x teinte, masque)` avec
    `masque = "Palissement plage" * saturate(1 - Z / "Hauteur plage")`.

    DEUX PIEGES DE `batch_connect_expressions`, payes comptant tous les deux :
      - une entree UNIQUE se designe par la chaine VIDE, jamais par "Input" --
        deja note pour la greffe de RVT ;
      - et une SORTIE unique aussi. Passer "Output_0", le nom que rend pourtant
        `list_expressions`, echoue EN SILENCE : 4 connexions sur 12 a la
        premiere tentative, sans dire lesquelles. Les sorties NOMMEES (`RGB`,
        `Z`) se passent bien par leur nom.

    ⚠ RECOMPILER CE MATERIAU FAIT TOMBER L'EDITEUR. Mesure du 13 septembre :
    graphe pose et sauvegarde a 21:39:13, crash a 21:39:15 --
    EXCEPTION_ACCESS_VIOLATION dans D3D12RHI, via RHI/Renderer sur un
    « Foreground Worker », c'est-a-dire la creation des etats de pipeline. Le
    travail etait deja sur le disque. Viser le ciel et couper le temps reel
    AVANT, et s'attendre a relancer.
    """
    _log.clear()
    if not unreal.EditorAssetLibrary.does_asset_exist(MAITRE):
        log("ERROR", "{} absent : le recreer d'abord (voir README)".format(MAITRE))
        return {"greffe": False, "log": list(_log)}
    d = _graphe()
    noms = {(e.get("properties") or {}).get("ParameterName") for e in d["expressions"]}
    if P_TEINTE in noms and P_HAUTEUR in noms and P_FORCE in noms:
        log("SKIPPED", "le sable de plage est deja greffe")
        return {"greffe": False, "log": list(_log)}

    src, cible, broche = _sable(d)
    if src is None or cible is None:
        log("ERROR", "chaine du sable introuvable : le graphe du pack a change")
        return {"greffe": False, "log": list(_log)}

    S = unreal.MaterialNodeService
    classes = ["MaterialExpressionVectorParameter", "MaterialExpressionMultiply",
               "MaterialExpressionScalarParameter", "MaterialExpressionWorldPosition",
               "MaterialExpressionDivide", "MaterialExpressionOneMinus",
               "MaterialExpressionClamp", "MaterialExpressionScalarParameter",
               "MaterialExpressionMultiply", "MaterialExpressionLinearInterpolate"]
    xs = [-4700.0, -4300.0, -5300.0, -5300.0, -4950.0, -4750.0, -4550.0, -4950.0, -4350.0, -3700.0]
    ys = [4950.0, 4950.0, 5350.0, 5550.0, 5450.0, 5450.0, 5450.0, 5750.0, 5600.0, 4300.0]
    n = [e.id for e in S.batch_create_expressions(MAITRE, classes, xs, ys)]
    if len(n) != 10:
        log("ERROR", "{} noeuds crees sur 10".format(len(n)))
        return {"greffe": False, "log": list(_log)}
    (teint, sableT, haut, wpos, div, om, clamp, forc, masq, fondu) = n

    S.batch_set_properties(
        MAITRE,
        [teint, teint, haut, haut, forc, forc, clamp, clamp],
        ["ParameterName", "DefaultValue", "ParameterName", "DefaultValue",
         "ParameterName", "DefaultValue", "MinDefault", "MaxDefault"],
        [P_TEINTE, teinte, P_HAUTEUR, "{:.6f}".format(hauteur),
         P_FORCE, "{:.6f}".format(force), "0.0", "1.0"])

    # sorties NOMMEES d'abord, puis les sorties uniques (chaine vide)
    faites = S.batch_connect_expressions(
        MAITRE, [wpos, src, teint, src], ["Z", "RGB", "RGB", "RGB"],
        [div, sableT, sableT, fondu], ["A", "A", "B", "A"])
    faites += S.batch_connect_expressions(
        MAITRE, [haut, div, om, clamp, forc, sableT, masq, fondu],
        ["", "", "", "", "", "", "", ""],
        [div, om, clamp, masq, masq, fondu, fondu, cible],
        ["B", "", "", "A", "B", "B", "Alpha", broche])
    if faites != 12:
        log("ERROR", "{} liaisons sur 12".format(faites))
        return {"greffe": False, "log": list(_log)}

    unreal.MaterialEditingLibrary.recompile_material(
        unreal.EditorAssetLibrary.load_asset(MAITRE))
    unreal.EditorAssetLibrary.save_asset(MAITRE)
    log("MODIFIED", "sable de plage greffe : hauteur {:.0f} cm, force {:.2f}".format(
        hauteur, force))
    return {"greffe": True, "log": list(_log)}
