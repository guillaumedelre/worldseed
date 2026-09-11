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
