"""Worldseed - donner un materiau physique au terrain voxel.

POURQUOI CE SCRIPT EXISTE
-------------------------
Douze des quatre-vingt-dix-neuf assets sonores d'Ultra Dynamic Sky ne sont
jamais joues : six compressions de neige, le deplacement dans la neige, quatre
flaques et le mouvement d'eau. Ils appartiennent a
`UDS_DLWE_Interaction_Sounds`, dont la documentation est explicite : hors
Landscape, il faut declarer les MATERIAUX PHYSIQUES du sol dans "Physical
Materials which Enable DLWE Interactions on Non-Landscapes".

Or le releve du 29 septembre 2026 est sans appel : ZERO occurrence de
`PhysicalMaterial` dans tout `Source/`, et AUCUN asset de materiau physique
dans `Content/`. Le terrain n'en a pas. Les traces de pas dans la neige butent
sur le meme verrou, et les sons de pas en general -- bien au-dela de ce pack --
aussi.

POURQUOI UN ASSET ET PAS DU C++
-------------------------------
Nos chunks portent `bUseComplexAsSimpleCollision`
(`ProceduralMeshComponent.cpp:518`), donc la resolution du physmat passe par
`FBodyInstance::GetComplexPhysicalMaterials`, qui construit UN PHYSMAT PAR SLOT
DE MATERIAU en appelant `Material->GetPhysicalMaterial()`
(`BodyInstance.cpp:3323-3341`). Le slot du materiau de terrain suffit donc, et
cela ne coute PAS UNE LIGNE sur le chemin chaud du streaming -- ou chaque chunk
est un composant neuf.

ET ON EQUIPE LE PARENT, PAS L'INSTANCE. Le materiau actif est
`MI_WorldseedGround_Orasot`, une instance ; elle retombe sur son parent tant que
`bOverridePhysMaterial` est faux (`MaterialInstance.cpp:2172`). Equiper le
parent couvre donc toutes ses instances, et la nappe d'horizon avec, qui porte
le meme materiau. Le script VERIFIE ce drapeau au lieu de le supposer.

L'ORDRE EST PORTANT, ET C'EST POURQUOI CE N'EST PAS UN REGLAGE DE RUNTIME
------------------------------------------------------------------------
Avec `bUseAsyncCooking`, la resolution est MISE EN CACHE a la creation du corps
(`BodyInstance.cpp:3285`). Un physmat pose apres qu'une section existe ne
prendrait pas -- meme forme que le piege deja paye ici : il n'existe aucune
facon de donner la collision a une section deja creee. Pose dans l'asset, il
est la AVANT que le premier chunk ne cuise.

COMMENT SE VERIFIE LE RESULTAT
------------------------------
PAS EN RELISANT LE CHAMP. Ce depot a paye le piege sur la collision :
`get_collision_enabled()` rend le reglage du composant, jamais la presence de
donnees cuites -- le sol etait troue quand l'inventaire le declarait plein. La
preuve est une TRACE, et son releve d'AVANT existe deja :

    -WorldseedPhysmat=50  ->  49 colonnes, 49 touchees, 0 ratee,
                              DefaultPhysicalMaterial x49  <- rien de pose

Apres ce script, la meme mesure doit rendre `PM_WorldseedTerre`.

USAGE
-----
    UnrealEditor-Cmd.exe Worldseed.uproject -run=pythonscript
      -script="D:/UE/Worldseed/Tools/UE/physmat_terrain.py"
      -unattended -nopause -nosplash

Passer `--verifier` pour ne rien ecrire et se contenter du releve.
"""

import sys

import unreal

PHYSMAT = "/Game/Worldseed/Physics/PM_WorldseedTerre"

# LES PARENTS, PAS LES INSTANCES -- ET LES DEUX, PAS UN.
# `ChooseTerrainMaterial` (`WorldseedTerrain.cpp:973`) rend l'un ou l'autre
# selon le mode d'apparence : le pack de textures passe par une instance de
# `M_WorldseedGround`, la coloration par biome par `M_WorldseedBiome`. N'en
# equiper qu'un donnerait un effet present dans un mode et SILENCIEUSEMENT
# absent dans l'autre.
PARENTS = [
    "/Game/Worldseed/Materials/M_WorldseedGround",
    "/Game/Worldseed/Materials/M_WorldseedBiome",
]

# L'instance effectivement posee au dernier lancement, d'apres le journal :
# "[Worldseed] sol : MI_WorldseedGround_Orasot (par convention de nom)".
INSTANCE = "/Game/Worldseed/Materials/MI_WorldseedGround_Orasot"


def dire(t):
    unreal.log("[PHYSMAT] %s" % t)


def obtenir_physmat(verifier):
    """Rendre le materiau physique, en le creant s'il manque. Idempotent."""
    if unreal.EditorAssetLibrary.does_asset_exist(PHYSMAT):
        dire("existe deja : %s" % PHYSMAT)
        return unreal.EditorAssetLibrary.load_asset(PHYSMAT)

    if verifier:
        dire("ABSENT : %s -- releve seul, rien n'a ete cree" % PHYSMAT)
        return None

    dossier, nom = PHYSMAT.rsplit("/", 1)
    actif = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        nom, dossier, unreal.PhysicalMaterial,
        unreal.PhysicalMaterialFactoryNew())
    if actif is None:
        unreal.log_error("[PHYSMAT] creation REFUSEE pour %s" % PHYSMAT)
        return None

    # LES VALEURS PAR DEFAUT SONT GARDEES A DESSEIN. DLWE ne lit que
    # l'IDENTITE du materiau physique : il verifie qu'il figure dans sa liste
    # blanche. Inventer un frottement ou une restitution ajouterait des boutons
    # que rien ne lit -- ce que le proprietaire a demande d'arreter de garder.
    unreal.EditorAssetLibrary.save_loaded_asset(actif, False)
    print("CREATED: %s" % PHYSMAT)
    dire("CREE : %s (valeurs par defaut, a dessein)" % PHYSMAT)
    return actif


def diagnostiquer_instance():
    """L'instance posee masque-t-elle le physmat de son parent ?"""
    if not unreal.EditorAssetLibrary.does_asset_exist(INSTANCE):
        unreal.log_warning(
            "[PHYSMAT] %s est ABSENTE. C'est l'instance que "
            "`ChooseTerrainMaterial` charge par convention de nom : sans elle "
            "le terrain retombe sur la coloration par biome, et ce serait "
            "`M_WorldseedBiome` qui porterait les pas." % INSTANCE)
        return

    mi = unreal.EditorAssetLibrary.load_asset(INSTANCE)
    parent = mi.get_editor_property("parent")
    surcharge = mi.get_editor_property("override_phys_material")
    dire("instance %s  parent = %s  override_phys_material = %s"
         % (mi.get_name(),
            parent.get_name() if parent else "<aucun>", surcharge))

    if surcharge:
        unreal.log_warning(
            "[PHYSMAT] L'INSTANCE SURCHARGE LE PHYSMAT : equiper le parent ne "
            "l'atteindra pas (`MaterialInstance.cpp:2157`). Il faut alors "
            "poser le physmat sur l'instance, ou retirer la surcharge.")


def equiper(chemin, physmat, verifier):
    """Poser le physmat dans le slot d'un materiau. Rend vrai s'il a ecrit."""
    if not unreal.EditorAssetLibrary.does_asset_exist(chemin):
        unreal.log_warning("[PHYSMAT] materiau ABSENT : %s" % chemin)
        return False

    mat = unreal.EditorAssetLibrary.load_asset(chemin)
    actuel = mat.get_editor_property("phys_material")
    if actuel is not None and actuel.get_path_name() == physmat.get_path_name():
        dire("%-24s deja equipe" % mat.get_name())
        return False

    if verifier:
        dire("%-24s A EQUIPER (actuel : %s)"
             % (mat.get_name(), actuel.get_name() if actuel else "aucun"))
        return False

    mat.set_editor_property("phys_material", physmat)

    # RELIRE, PARCE QU'UNE ECRITURE SUR UN MATERIAU NE SIGNALE RIEN QUAND ELLE
    # ECHOUE : posee sur un champ absent, l'API ne rend rien, ne journalise
    # rien, et ne fait rien. Douze fois paye dans ce depot.
    relu = mat.get_editor_property("phys_material")
    if relu is None or relu.get_path_name() != physmat.get_path_name():
        unreal.log_error("[PHYSMAT] %s : ecriture RELUE FAUSSE (%s)"
                         % (mat.get_name(), relu))
        return False

    unreal.EditorAssetLibrary.save_loaded_asset(mat, False)
    print("MODIFIED: %s" % chemin)
    dire("%-24s equipe, relu = %s" % (mat.get_name(), relu.get_name()))
    return True


def main():
    verifier = "--verifier" in sys.argv
    if verifier:
        dire("=== RELEVE SEUL, rien ne sera ecrit ===")

    physmat = obtenir_physmat(verifier)
    diagnostiquer_instance()
    if physmat is None:
        return

    poses = sum(1 for c in PARENTS if equiper(c, physmat, verifier))
    dire("=== %d materiau(x) equipe(s) ===" % poses)
    dire("LA PREUVE N'EST PAS ICI : relancer le jeu avec -WorldseedPhysmat=50 "
         "et lire le physmat que la trace rencontre.")


main()
