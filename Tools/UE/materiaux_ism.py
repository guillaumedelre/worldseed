"""Worldseed - armer le drapeau « Used with Instanced Static Meshes ».

POURQUOI CE SCRIPT EXISTE
-------------------------
Signale en jeu : le tronc d'un palmier du pack Egypt rendait un DAMIER GRIS,
alors que les palmiers d'Orasot juste a cote rendaient bien, et que le meme
palmier rend bien dans la carte de demonstration du pack.

La cause n'est ni la RVT, ni une texture manquante, ni un materiau absent --
les trois ont ete mesurees et ecartees. C'est un DRAPEAU D'USAGE :

    M_tree    (tronc du palmier egyptien)   used_with_instanced_static_meshes = False
    M_plants  (palmes du meme palmier)                                        = True
    M_Assets_MasterMat (ecorce Orasot)                                        = True
    M_Master_Leaf      (palme Orasot)                                         = True

Un materiau non marque ne COMPILE PAS pour ce cas, et le moteur lui substitue
le materiau par defaut -- le damier gris. Cela explique chaque detail du
signalement : le tronc seul est touche et pas les palmes ; les palmiers Orasot
vont bien ; et la DEMO DU PACK va bien parce qu'elle pose des
`StaticMeshActor`, ou le drapeau n'est pas requis. Notre semis, lui, pose UN
ISM PAR CHUNK depuis le 26 septembre.

POURQUOI UN SCRIPT ET NON UNE RETOUCHE A LA MAIN
------------------------------------------------
`Content/Stylized_Egypt/` n'est PAS versionne -- seul `Content/Worldseed/`
l'est. Une case cochee dans l'editeur serait donc perdue au prochain clone,
sans que rien ne le signale. Ce depot a deja paye cette lecon plusieurs fois :
le PlayerStart qui revenait a sa position, les acteurs d'eclairage disparus
entre deux sessions, la grille climatique qu'aucun script ne rejouait.

IDEMPOTENT PAR CONSTAT, jamais par destruction : le script lit le drapeau et ne
touche qu'aux materiaux qui en manquent. Le relancer sur un projet sain ne
recompile rien.

USAGE
-----
    UnrealEditor-Cmd.exe Worldseed.uproject -run=pythonscript \
      -script="Tools/UE/materiaux_ism.py" -unattended -nopause -nosplash

Passer `--verifier` pour ne rien ecrire et se contenter du releve.
"""

import json
import os
import sys

import unreal


def references_semees():
    """Les especes que le semis pose reellement, lues dans les recettes.

    On part des RECETTES et non d'une liste de noms : un balayage precedent ne
    cherchait que « palm » et « tree » dans les noms de maillage, et il a rate
    les trois pierres qui souffrent du meme defaut.
    """
    chemin = os.path.join(unreal.Paths.project_dir(),
                          "Tools/UE/vegetation_recipes.json")
    with open(chemin, "r", encoding="utf-8") as f:
        data = json.load(f)

    racines = data.get("roots", {})
    refs = set()

    def ramasser(noeud):
        if isinstance(noeud, dict):
            for v in noeud.values():
                ramasser(v)
        elif isinstance(noeud, list):
            for v in noeud:
                if isinstance(v, str) and ":" in v:
                    refs.add(v)
                else:
                    ramasser(v)

    ramasser(data.get("biomes", {}))
    # `estran` vit HORS de `biomes` : une boucle qui ne parcourt que les biomes
    # lui retire ses galets. Piege deja consigne au registre.
    ramasser(data.get("estran", {}))
    return racines, refs


def maitre_de(materiau):
    """Le maitre au bout d'une chaine d'instances.

    Le drapeau d'usage vit sur le MAITRE : le lire sur une instance ne dirait
    rien, et l'y ecrire ne ferait rien.
    """
    cur = materiau
    while isinstance(cur, unreal.MaterialInstance):
        cur = cur.get_editor_property("parent")
    return cur


def main():
    verifier = "--verifier" in sys.argv
    racines, refs = references_semees()
    unreal.log("ISM  %d espece(s) semee(s) d'apres les recettes" % len(refs))

    # maitre -> (drapeau, especes qui en dependent)
    maitres = {}
    for ref in sorted(refs):
        prefixe, _, suffixe = ref.partition(":")
        base = racines.get(prefixe)
        if not base:
            continue
        nom = suffixe.rsplit("/", 1)[-1]
        sm = unreal.EditorAssetLibrary.load_asset(
            "%s/%s.%s" % (base, suffixe, nom))
        if not sm:
            unreal.log_warning("ISM  maillage absent : %s" % ref)
            continue
        for emplacement in sm.get_editor_property("static_materials"):
            mi = emplacement.get_editor_property("material_interface")
            if not mi:
                unreal.log_warning(
                    "ISM  %s : emplacement SANS materiau -- le moteur y mettra "
                    "son damier, et aucun drapeau ne corrigera cela" % ref)
                continue
            maitre = maitre_de(mi)
            if not maitre:
                continue
            cle = maitre.get_path_name()
            if cle not in maitres:
                maitres[cle] = [maitre, [], maitre.get_editor_property(
                    "used_with_instanced_static_meshes")]
            if ref not in maitres[cle][1]:
                maitres[cle][1].append(ref)

    manquants = [(k, v) for k, v in maitres.items() if not v[2]]
    unreal.log("ISM  %d maitre(s) distinct(s), %d sans le drapeau"
               % (len(maitres), len(manquants)))

    corriges = 0
    for cle, (maitre, especes, _) in sorted(manquants):
        if cle.startswith("/Engine/"):
            # ON NE TOUCHE PAS AUX ASSETS DU MOTEUR. Quand `WorldGridMaterial`
            # apparait ici, le defaut n'est pas le drapeau : c'est le maillage
            # qui a un emplacement sans materiau, et cela se corrige dans le
            # maillage ou dans les recettes.
            unreal.log_warning(
                "ISM  IGNORE (asset du moteur) %s -- pour %s"
                % (cle, ", ".join(sorted(especes))))
            continue

        unreal.log("ISM  %-62s  %d espece(s) : %s"
                   % (cle, len(especes), ", ".join(sorted(especes))))
        if verifier:
            continue

        maitre.set_editor_property("used_with_instanced_static_meshes", True)
        unreal.MaterialEditingLibrary.recompile_material(maitre)
        unreal.EditorAssetLibrary.save_loaded_asset(maitre, False)
        # ON RELIT. Ce depot a paye qu'une ecriture de propriete de materiau
        # peut ne rien faire sans rien dire.
        relu = maitre.get_editor_property("used_with_instanced_static_meshes")
        unreal.log("ISM    -> arme, relu = %s" % relu)
        if relu:
            corriges += 1

    if verifier:
        unreal.log("ISM  === releve seul, rien n'a ete ecrit ===")
    else:
        unreal.log("ISM  === %d materiau(x) arme(s) et sauvegarde(s) ===" % corriges)


main()
