"""Cree dans l'editeur les presets climatiques produits par le generateur.

A executer DEPUIS l'editeur :

    import sys, importlib
    sys.path.insert(0, r"D:\\UE\\Worldseed\\Tools\\UE")
    import uds_climate; importlib.reload(uds_climate)
    print(uds_climate.creer_presets())
    print(uds_climate.verifier())

CE QUE FAIT CE FICHIER. `Tools/WorldGen/export_uds_climate.py` a mesure le climat
de chaque biome sur les cartes du generateur et rempli les 21 cases d'un
`UDS_Climate_Preset`. Ici on materialise ces chiffres en assets Unreal, un par
couple (biome, hemisphere), pour que l'acteur de pilotage n'ait plus qu'a les
appliquer avec la fonction publique d'UDS `Apply Climate Preset Object`.

POURQUOI UN ASSET PAR HEMISPHERE. UDS n'a qu'une SAISON globale, alors que le
monde va d'un pole a l'autre : quand UDS dit "hiver", c'est l'ete dans
l'hemisphere sud. Le preset sud est donc le preset nord avec hiver/ete et
printemps/automne echanges -- l'echange est fait cote generateur.

PIEGE : un `UDS_Climate_Preset` n'est PAS un DataAsset. C'est un Blueprint dont
les instances sont sauvees comme assets (classe `UDS_Climate_Preset_C`). On le
duplique donc depuis un preset livre par le pack plutot que de le fabriquer.
"""

from __future__ import annotations

import json
import os

import unreal

MONDE = r"D:\UE\Worldseed\Saved\WorldGen\20260909"
SOURCE = os.path.join(MONDE, "uds_climate.json")
# Modele a dupliquer : n'importe quel preset livre fait l'affaire, on ecrase tout.
MODELE = "/Game/UltraDynamicSky/Blueprints/Weather_Effects/Climate_Presets/Oceanic"
DESTINATION = "/Game/Worldseed/Climate"

SAISONS = ("Winter", "Spring", "Summer", "Autumn")

_log: list = []


def log(kind: str, message: str) -> None:
    line = "{}: {}".format(kind, message)
    _log.append(line)
    unreal.log("WORLDSEED_CLIMAT " + line)
    print(line)


def _donnees() -> dict:
    with open(SOURCE, "r", encoding="utf-8") as fh:
        return json.load(fh)


def creer_presets() -> dict:
    """Cree ou met a jour un asset par preset. Idempotent."""
    _log.clear()
    d = _donnees()
    outils = unreal.AssetToolsHelpers.get_asset_tools()
    modele = unreal.EditorAssetLibrary.load_asset(MODELE)
    if modele is None:
        log("ERROR", "modele introuvable : " + MODELE)
        return {"success": False, "log": list(_log)}

    faits, ecrases = [], 0
    for nom, valeurs in sorted(d["presets"].items()):
        chemin = "{}/{}".format(DESTINATION, nom)
        if unreal.EditorAssetLibrary.does_asset_exist(chemin):
            asset = unreal.EditorAssetLibrary.load_asset(chemin)
            ecrases += 1
        else:
            asset = outils.duplicate_asset(nom, DESTINATION, modele)
            if asset is None:
                log("ERROR", "duplication impossible : " + chemin)
                continue
        for cle, v in valeurs.items():
            if cle == "Data Source":
                asset.set_editor_property(cle, unreal.Text(v))
            else:
                asset.set_editor_property(cle, float(v))
        unreal.EditorAssetLibrary.save_asset(chemin, False)
        faits.append(nom)
    log("CREATED" if ecrases == 0 else "MODIFIED",
        "{} presets ecrits dans {} ({} mis a jour)".format(len(faits), DESTINATION, ecrases))
    return {"success": True, "presets": len(faits), "log": list(_log)}


def verifier() -> dict:
    """Relit les assets et les compare au JSON, case par case."""
    d = _donnees()
    manquants, ecarts = [], []
    for nom, valeurs in sorted(d["presets"].items()):
        chemin = "{}/{}".format(DESTINATION, nom)
        a = unreal.EditorAssetLibrary.load_asset(chemin)
        if a is None:
            manquants.append(nom)
            continue
        for cle, v in valeurs.items():
            if cle == "Data Source":
                continue
            lu = float(a.get_editor_property(cle))
            if abs(lu - float(v)) > 0.05:
                ecarts.append("{} / {} : {} attendu {}".format(nom, cle, lu, v))
    # un temoin lisible
    temoin = {}
    for nom in sorted(d["presets"])[:1] + sorted(d["presets"])[-1:]:
        a = unreal.EditorAssetLibrary.load_asset("{}/{}".format(DESTINATION, nom))
        if a is not None:
            temoin[nom] = {
                "hiver": (a.get_editor_property("Winter Average High Temp (C)"),
                          a.get_editor_property("Winter Average Low Temp (C)")),
                "ete": (a.get_editor_property("Summer Average High Temp (C)"),
                        a.get_editor_property("Summer Average Low Temp (C)")),
                "source": str(a.get_editor_property("Data Source"))[:90],
            }
    return {"attendus": len(d["presets"]), "manquants": manquants,
            "ecarts": ecarts[:10], "nombreEcarts": len(ecarts), "temoin": temoin}
