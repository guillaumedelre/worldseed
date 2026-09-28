# Worldseed - greffer la neige d'Ultra Dynamic Sky sur le materiau du sol.
#
# LE DEFAUT QU'IL CORRIGE. Le globe peint la calotte glaciaire en blanc
# (registre : couleur 242/246/250) et le sol en jeu la peint a 100 % avec la
# texture de ROCHE (registre : matieres [0, 0, 1, 0], ordre herbe/aride/roche/
# mousse). Les deux ne peuvent pas s'accorder : l'habillage Orasot n'a AUCUNE
# texture de neige -- verifie, zero asset sous Content/Orasot_Bundle.
#
# Et la teinte ne rattrape rien : `TeinteNormalisee` DIVISE par la luminance,
# donc un blanc sort a (1, 1, 1), neutre. C'est delibere -- la teinte transporte
# la chrominance, jamais la clarte. Un biome ne peut donc etre blanchi que par
# une MATIERE, pas par sa couleur.
#
# POURQUOI DLWE PLUTOT QU'UNE CINQUIEME MATIERE. Arbitrage du proprietaire,
# 28 septembre 2026. `Mask Snow/Dust Coverage` et `Offset Coverage` sont des
# entrees SCALAIRES de DLWE_V3 -- releve, pas suppose -- donc pilotables par
# sommet. Une seule chaine donne alors la calotte PERMANENTE et la neige
# METEO, plus l'accumulation, les etincelles et les traces de pas, qu'une
# matiere ne donnera jamais.
#
#   UnrealEditor-Cmd.exe Worldseed.uproject -run=pythonscript
#     -script="Tools/UE/neige_dlwe.py" -unattended -nopause -nosplash
#
# L'EDITEUR DOIT ETRE ARRETE : le plugin ecoute sur le port 8000 et le
# commandlet echoue tout entier sur "HttpListener unable to bind".
#
# IDEMPOTENT PAR CONSTAT, jamais par destruction : s'il retrouve l'appel a
# DLWE il ne touche a rien. Le depot a deja paye une greffe posee DEUX FOIS
# sur la RVT -- deux echantillonneurs, deux Lerp chaines, teinte appliquee
# deux fois.
#
# POSER WORLDSEED_VERIFIER=1 pour ne rien ecrire et se contenter du releve.
# Ce n'est PAS un caprice : `-script=` du commandlet ne prend qu'un CHEMIN, et
# tout ce qu'on lui accole part dans le nom de fichier -- "Could not load
# Python file". L'environnement est la seule voie qui passe.
import os
import sys

import unreal

L = unreal.MaterialEditingLibrary

SOL = "/Game/Worldseed/Materials/M_WorldseedGround"
DLWE = "/Game/UltraDynamicSky/Materials/Weather/Dynamic_Landscape_Weather_Effects_V3"

# La rugosite du sol, portee par une constante TERMINALE du graphe. On la
# reconnait par sa VALEUR : 0,92 n'a de sens que pour une rugosite -- un sol
# metallique a 0,92 serait absurde. L'hypothese est dite ici parce qu'elle ne
# peut pas etre verifiee par l'API : les proprietes d'un materiau sont
# PROTEGEES en lecture depuis Python, comme les entrees d'une sortie RVT.
RUGOSITE_ATTENDUE = 0.92


def dire(t):
    unreal.log("[NEIGE] %s" % t)


def entrees_de(materiau, expression):
    """Les entrees d'une expression, appariees a leur nom.

    `get_inputs_for_material_expression` rend les EXPRESSIONS SOURCES et non
    des enveloppes de connexion, et son tableau est aligne sur
    `get_material_expression_input_names`. C'est la seule facon de lire la
    topologie sans la DEDUIRE des positions des noeuds -- deduction qui a
    deja fait conclure de travers dans ce depot.
    """
    try:
        srcs = list(L.get_inputs_for_material_expression(materiau, expression))
        noms = list(L.get_material_expression_input_names(expression))
    except Exception:
        return []
    return [(noms[k] if k < len(noms) else "", s) for k, s in enumerate(srcs)]


def nom_entree(expression, voulu):
    """Le nom EXACT d'une entree, cherche sans supposer sa casse.

    On ne code pas "WorldPositionOffset" en dur : on demande au noeud ses
    noms et on apparie. Une faute de casse echouerait EN SILENCE -- le depot
    a paye que `batch_connect_expressions` ne dit pas quelles connexions il
    a ratees.
    """
    cible = voulu.lower().replace(" ", "").replace("/", "")
    try:
        for n in L.get_material_expression_input_names(expression):
            if str(n).lower().replace(" ", "").replace("/", "") == cible:
                return str(n)
    except Exception:
        pass
    return None


def relier(source, sortie, cible, entree):
    """Relie et DIT ce qu'elle a fait. Une connexion ratee est silencieuse.

    UNE BROCHE D'ENTREE UNIQUE SE DESIGNE PAR LA CHAINE VIDE, jamais par son
    nom -- `ComponentMask`, `OneMinus` et `Clamp` annoncent pourtant "Input".
    Le depot a paye ce piege ; la premiere version de ce script l'a paye une
    seconde fois, en cherchant un nom AVANT d'appeler et en laissant UV3
    debranche sous un rapport qui disait "greffe posee".
    """
    if entree == "":
        nom = ""
    else:
        nom = nom_entree(cible, entree)
        if nom is None:
            dire("!! entree '%s' introuvable sur %s"
                 % (entree, cible.get_class().get_name()))
            return False
    ok = L.connect_material_expressions(source, sortie, cible, nom)
    if not ok:
        dire("!! connexion REFUSEE : %s -> %s.%s"
             % (source.get_class().get_name(),
                cible.get_class().get_name(), nom))
    return bool(ok)


def main():
    verifier = ("--verifier" in sys.argv
                or os.environ.get("WORLDSEED_VERIFIER", "") == "1")

    M = unreal.EditorAssetLibrary.load_asset(SOL)
    if M is None:
        dire("!! materiau introuvable : %s" % SOL)
        return

    F = unreal.EditorAssetLibrary.load_asset(DLWE)
    if F is None:
        dire("!! DLWE_V3 introuvable : %s" % DLWE)
        return

    ex = list(L.get_material_expressions(M))
    dire("%s : %d expressions avant greffe" % (SOL.rsplit("/", 1)[-1], len(ex)))

    # --- idempotence PAR CONSTAT -----------------------------------------
    for e in ex:
        if e.get_class().get_name() == "MaterialExpressionMaterialFunctionCall":
            f = e.get_editor_property("material_function")
            if f is not None and f.get_name() == F.get_name():
                dire("greffe DEJA POSEE (appel a %s) -- rien fait" % F.get_name())
                return

    # --- les ancrages, cherches par CRITERE et jamais par position -------
    consomme = set()
    for e in ex:
        for _, s in entrees_de(M, e):
            if s is not None:
                consomme.add(s)

    lerp_final = None    # la couleur finale : le Lerp pilote par TintStrength
    rampe = None         # MF_WorldseedNappeRampe -> WorldPositionOffset
    rugosite = None      # la constante terminale 0,92
    metallique = None    # la constante terminale 0
    rvt = None
    uv3 = None

    for e in ex:
        c = e.get_class().get_name()
        if c == "MaterialExpressionLinearInterpolate":
            for nom, s in entrees_de(M, e):
                if s is None:
                    continue
                if (s.get_class().get_name() == "MaterialExpressionScalarParameter"
                        and str(s.get_editor_property("parameter_name")) == "TintStrength"):
                    lerp_final = e
        elif c == "MaterialExpressionMaterialFunctionCall":
            f = e.get_editor_property("material_function")
            if f is not None and "NappeRampe" in f.get_name():
                rampe = e
        elif c == "MaterialExpressionRuntimeVirtualTextureOutput":
            rvt = e
        elif c == "MaterialExpressionTextureCoordinate":
            if int(e.get_editor_property("coordinate_index")) == 3:
                uv3 = e
        elif c == "MaterialExpressionConstant" and e not in consomme:
            v = float(e.get_editor_property("r"))
            if abs(v - RUGOSITE_ATTENDUE) < 1e-3:
                rugosite = e
            elif abs(v) < 1e-6:
                metallique = e

    dire("ancrages : couleur=%s rampe=%s rugosite=%s metal=%s rvt=%s"
         % (lerp_final is not None, rampe is not None,
            rugosite is not None, metallique is not None, rvt is not None))

    if lerp_final is None or rampe is None:
        dire("!! ancrage manquant -- le graphe n'est pas celui qu'on croit,")
        dire("!! greffe ANNULEE plutot que posee au hasard")
        return

    if rvt is None:
        dire("!! aucune sortie RVT -- ce materiau doit en porter une.")
        dire("!! greffe ANNULEE : le depot a deja detruit ce noeud une fois.")
        return

    if verifier:
        dire("=== releve seul, rien n'a ete ecrit ===")
        return

    # --- UV3.Y porte la PART DE NEIGE ------------------------------------
    # LE CANAL EST PARTAGE SANS CONFLIT, et c'est ce qui rend la greffe
    # propre : la nappe d'horizon pose UV3 = (enfoncement, 0,0) -- sa
    # composante Y est inutilisee -- et les chunks du terrain ne posent aucun
    # UV3 du tout. Meme convention que UV2.Y, qui porte deja la part d'estran
    # a cote de la teinte B.
    if uv3 is None:
        uv3 = L.create_material_expression(
            M, unreal.MaterialExpressionTextureCoordinate, -1400, 1400)
        uv3.set_editor_property("coordinate_index", 3)
        dire("UV3 pose")
    else:
        dire("UV3 deja present, reutilise")

    part = L.create_material_expression(
        M, unreal.MaterialExpressionComponentMask, -1150, 1400)
    part.set_editor_property("r", False)
    part.set_editor_property("g", True)
    part.set_editor_property("b", False)
    part.set_editor_property("a", False)
    relier(uv3, "", part, "")

    # --- les attributs de materiau ---------------------------------------
    # DLWE_V3 travaille en ATTRIBUTS : son entree `Material Attributes` est
    # l'une des trois qui n'ont AUCUNE valeur par defaut. Le materiau n'etant
    # pas en `use_material_attributes`, il faut les fabriquer -- et la
    # topologie relevee dit que c'est court : couleur, rugosite, metal, et le
    # deplacement de sommets.
    attrs = L.create_material_expression(
        M, unreal.MaterialExpressionMakeMaterialAttributes, 400, 300)

    relier(lerp_final, "", attrs, "BaseColor")

    # LE DEPLACEMENT DE SOMMETS DOIT SUIVRE, ET C'EST LE PIEGE DE CETTE
    # GREFFE. Passer en attributs deplace WorldPositionOffset dans le bloc ;
    # oublier de l'y rebrancher recasse exactement le defaut du 27 septembre
    # -- le decor d'horizon cesse de s'enfoncer et le joueur se retrouve
    # ENTERRE dedans.
    if not relier(rampe, "", attrs, "WorldPositionOffset"):
        dire("!! la rampe de la nappe n'a PAS ete rebranchee -- greffe a revoir")

    if rugosite is not None:
        relier(rugosite, "", attrs, "Roughness")
    else:
        dire("rugosite non retrouvee : le defaut des attributs s'applique")
    if metallique is not None:
        relier(metallique, "", attrs, "Metallic")

    # --- l'appel a DLWE ---------------------------------------------------
    appel = L.create_material_expression(
        M, unreal.MaterialExpressionMaterialFunctionCall, 800, 300)
    appel.set_editor_property("material_function", F)

    vrai = L.create_material_expression(
        M, unreal.MaterialExpressionStaticBool, 500, 700)
    vrai.set_editor_property("value", True)
    faux = L.create_material_expression(
        M, unreal.MaterialExpressionStaticBool, 500, 800)
    faux.set_editor_property("value", False)

    relier(attrs, "", appel, "Material Attributes")
    # LES DEUX SEULES ENTREES SANS VALEUR PAR DEFAUT, avec les attributs :
    # le releve les donne comme les seules a `defaut=False`, ce qui confirme
    # la note du 11 septembre 2026. Sans elles le materiau NE COMPILE PAS --
    # "Missing function input".
    relier(vrai, "", appel, "Apply Snow/Dust")
    relier(faux, "", appel, "Apply Wetness/Puddles")

    # LA CALOTTE EST BLANCHE PARCE QU'ELLE EST UNE CALOTTE, pas parce qu'il
    # neige aujourd'hui. `Offset Coverage` DECALE la couverture : c'est lui
    # qui rend la neige permanente la ou notre part vaut un, pendant que la
    # meteo continue de piloter le reste du monde.
    relier(part, "", appel, "Offset Coverage")

    # --- la bascule -------------------------------------------------------
    M.set_editor_property("use_material_attributes", True)
    ok = L.connect_material_property(
        appel, "Material Attributes", unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
    if not ok:
        dire("!! la sortie de DLWE n'a pas pu etre branchee sur les attributs")

    L.recompile_material(M)
    unreal.EditorAssetLibrary.save_loaded_asset(M, False)

    # --- ON RELIT CE QU'ON A ECRIT ---------------------------------------
    # Ce depot a paye qu'une ecriture de propriete de materiau peut ne rien
    # faire sans rien dire.
    apres = list(L.get_material_expressions(M))
    trouve_dlwe = False
    trouve_rvt = False
    trouve_rampe = False
    trouve_part = False
    for e in apres:
        c = e.get_class().get_name()
        if c == "MaterialExpressionMaterialFunctionCall":
            f = e.get_editor_property("material_function")
            if f is not None and f.get_name() == F.get_name():
                trouve_dlwe = True
                # LA PART DE NEIGE DOIT REMONTER JUSQU'A UV3, et c'est le
                # controle qui manquait : la premiere version a rapporte
                # "greffe posee" alors que le masque n'avait aucune entree,
                # donc `Offset Coverage` recevait zero et la calotte serait
                # restee grise. Un rapport vert ne dit rien des liaisons
                # qu'on ne lui a pas demande de verifier.
                for nom, s in entrees_de(M, e):
                    if s is None or "Offset" not in str(nom):
                        continue
                    amont = s
                    for _ in range(4):
                        if amont is None:
                            break
                        if (amont.get_class().get_name()
                                == "MaterialExpressionTextureCoordinate"
                                and int(amont.get_editor_property(
                                    "coordinate_index")) == 3):
                            trouve_part = True
                            break
                        suivants = [x for _, x in entrees_de(M, amont)
                                    if x is not None]
                        amont = suivants[0] if suivants else None
        elif c == "MaterialExpressionRuntimeVirtualTextureOutput":
            trouve_rvt = True
        elif c == "MaterialExpressionMakeMaterialAttributes":
            for nom, s in entrees_de(M, e):
                if s is not None and s.get_class().get_name() == \
                        "MaterialExpressionMaterialFunctionCall":
                    f = s.get_editor_property("material_function")
                    if f is not None and "NappeRampe" in f.get_name():
                        trouve_rampe = True

    dire("")
    dire("=== RELECTURE ===")
    dire("  expressions            %d -> %d" % (len(ex), len(apres)))
    dire("  use_material_attributes %s"
         % M.get_editor_property("use_material_attributes"))
    dire("  appel a DLWE_V3        %s" % trouve_dlwe)
    dire("  sortie RVT conservee   %s" % trouve_rvt)
    dire("  rampe de la nappe dans les attributs  %s" % trouve_rampe)
    dire("  Offset Coverage remonte a UV3         %s" % trouve_part)
    try:
        dire("  compile                %s"
             % M.get_editor_property("is_compiled_ok"))
    except Exception:
        dire("  compile                (non expose)")

    if trouve_dlwe and trouve_rvt and trouve_rampe and trouve_part:
        dire("=== greffe posee et relue ===")
    else:
        dire("!! GREFFE INCOMPLETE -- restaurer par : git checkout -- "
             "Content/Worldseed/Materials/M_WorldseedGround.uasset")


main()
