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


# ------------------------------------------------------- la grille de biomes

BLUEPRINT_CLIMAT = "/Game/Worldseed/Climate/BP_WorldseedClimat"


def poser_grille() -> dict:
    """Ecrit la grille de biomes dans `BP_WorldseedClimat.GrilleBrute`.

    POURQUOI CETTE FONCTION EXISTE. La grille avait ete posee A LA MAIN lors
    d'une session precedente, et aucun script ne la rejouait : toute
    regeneration du monde laissait donc le Blueprint avec la grille d'un monde
    qui n'existait plus, sans que rien ne le signale.

    LE FORMAT EST DICTE PAR LE BLUEPRINT, qui decoupe en deux temps : les
    LIGNES par `;` une seule fois au BeginPlay, puis les CELLULES par `-` sur
    la seule ligne utile a chaque mise a jour. C'est ce decoupage en deux temps
    qui rend tenable une chaine de ~39 000 caracteres appelee deux fois par
    seconde.

    LA VALEUR NE SE RELIT PAS PAR `get_variable_info(...).default_value`, qui
    rend une chaine VIDE pour une valeur longue alors qu'elle est bien posee.
    Le controle se fait par `get_property`, qui dit vrai.
    """
    _log.clear()
    d = _donnees()
    g = d.get("grid") or {}
    lignes = g.get("data")
    if not lignes:
        log("ERROR", "pas de grille dans {}".format(SOURCE))
        return {"success": False, "log": list(_log)}

    brut = ";".join("-".join(str(int(v)) for v in ligne) for ligne in lignes)
    vides = sum(1 for ligne in lignes for v in ligne if int(v) == 0)
    if vides:
        log("ERROR", "{} cellules a 0 : aucun prereglage n'existe pour l'ocean, "
                     "relancer export_uds_climate.py".format(vides))
        return {"success": False, "log": list(_log)}

    ok = unreal.BlueprintService.set_variable_default_value(
        BLUEPRINT_CLIMAT, "GrilleBrute", brut)
    if not ok:
        log("ERROR", "ecriture de GrilleBrute refusee")
        return {"success": False, "log": list(_log)}
    unreal.BlueprintEditorLibrary.compile_blueprint(
        unreal.EditorAssetLibrary.load_asset(BLUEPRINT_CLIMAT))
    unreal.EditorAssetLibrary.save_asset(BLUEPRINT_CLIMAT)

    relu = unreal.BlueprintService.get_property(BLUEPRINT_CLIMAT, "GrilleBrute")
    conforme = str(relu) == brut
    log("MODIFIED" if conforme else "ERROR",
        "GrilleBrute : {} lignes, {} caracteres, relecture {}".format(
            len(lignes), len(brut), "conforme" if conforme else "DIFFERENTE"))
    return {"success": conforme, "lignes": len(lignes), "caracteres": len(brut),
            "resolution": g.get("resolution"), "celluleCm": g.get("cellSizeCm"),
            "log": list(_log)}
