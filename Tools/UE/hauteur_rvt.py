# Worldseed - faire ecrire a la nappe la RVT de HAUTEUR, et pas seulement la
# couleur.
#
# LE DEFAUT QU'IL CORRIGE, ET IL EST LE PREMIER MAILLON DU DESSUS DES PANS.
# `M_Master_Cliff_Mat` melange la roche du pan et la couleur du sol par
# `MF_HeightLerp_MaterialAttribute`, dont l'entree B vient de `MF_RVT`. Or
# `MF_RVT` echantillonne DEUX runtime virtual textures -- `RVT_Landscape_Material`
# ET `RVT_Landscape_Height` (type WORLD_HEIGHT) -- et son masque d'ancrage
# compare l'altitude du monde a celle lue dans la seconde. Notre nappe n'ecrivait
# que la couleur : ce masque ne mordait jamais, et le dessus du pan gardait sa
# roche quels que soient les switchs.
#
# CE QUI FAISAIT ECARTER LA PISTE, ET POURQUOI C'ETAIT FAUX. La note qui tenait
# dans `WorldseedNappeRvt` disait qu'une nappe PLATE ne pouvait ecrire qu'une
# altitude constante. Le constat etait juste -- le plan est a Z = 0, donc
# `WorldPosition.b` y vaut zero partout -- et la conclusion fausse : le moteur ne
# prend pas le Z de la GEOMETRIE, il prend ce que le materiau met sur l'entree
# `WorldHeight` du noeud de sortie RVT (`VirtualTextureMaterial.usf`, qui le
# repacke ensuite avec la transform du volume). Il suffisait donc de CUIRE la
# hauteur comme on cuit deja les poids et la teinte.
#
#   UnrealEditor-Cmd.exe Worldseed.uproject -run=pythonscript
#     -script="Tools/UE/hauteur_rvt.py" -unattended -nopause -nosplash
#
# L'EDITEUR DOIT ETRE ARRETE : le plugin ecoute sur le port 8000 et le
# commandlet echoue tout entier sur "HttpListener unable to bind".
#
# IDEMPOTENT PAR CONSTAT, jamais par destruction : s'il retrouve `TexHauteur` il
# ne touche a rien. Le depot a deja paye une greffe posee DEUX FOIS sur la RVT.
#
# POSER WORLDSEED_VERIFIER=1 pour ne rien ecrire et se contenter du releve --
# `-script=` du commandlet ne prend qu'un CHEMIN, et tout ce qu'on lui accole
# part dans le nom de fichier.
import os
import sys

import unreal

L = unreal.MaterialEditingLibrary

SOL = "/Game/Worldseed/Materials/M_WorldseedGround"

# LES UNITES SONT CELLES DU MONDE, EN CENTIMETRES, ET LE C++ LES POSE.
# `WorldseedNappeRvt::Poser` ecrit `MondeZCm = (bas, plage, 0, 0)` depuis
# `FWorldseedRvtRegles`, c'est-a-dire depuis les bornes du VOLUME lui-meme.
# Rien n'est recopie ici : une valeur en dur dans ce script divergerait le jour
# ou le volume change, et l'ancrage serait faux partout sans une erreur.
PARAM_BORNES = "MondeZCm"
PARAM_TEXTURE = "TexHauteur"

# LA NORMALE VA AVEC LA HAUTEUR, ET ELLE EST AUSSI PORTANTE QU'ELLE.
# `MF_RVT` melange des ATTRIBUTS de materiau, normale comprise : des que le
# masque d'ancrage mord, le dessus d'un pan INCLINE recoit la normale de la
# RVT. Une sortie dont l'entree `Normal` n'est pas branchee ecrit (0, 0, 1) --
# releve dans le moteur, `MaterialExpressions.cpp:3081` -- c'est-a-dire la
# normale d'un PLAN. Mesure a l'image : le dessus rend alors entierement NOIR
# sous un dither, et le noir disparait a `ShowFlag.DynamicShadows 0`. Il est
# eclaire comme un plan horizontal tout en projetant l'ombre d'une pente.
PARAM_NORMALE = "TexNormale"


def dire(t):
    unreal.log("[HAUTEUR] %s" % t)


def entrees_de(materiau, expression):
    """Les entrees d'une expression, appariees a leur nom.

    `get_inputs_for_material_expression` rend les EXPRESSIONS SOURCES et non
    des enveloppes de connexion, et son tableau est aligne sur
    `get_material_expression_input_names`. C'est la seule facon de lire la
    topologie sans la DEDUIRE des positions des noeuds.
    """
    try:
        srcs = list(L.get_inputs_for_material_expression(materiau, expression))
        noms = list(L.get_material_expression_input_names(expression))
    except Exception:
        return []
    return [(noms[k] if k < len(noms) else "", s) for k, s in enumerate(srcs)]


def nom_entree(expression, voulu):
    """Le nom EXACT d'une entree, cherche sans supposer sa casse."""
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
    nom -- `ComponentMask` annonce pourtant "Input". Le depot a paye ce piege
    deux fois, dont une en laissant UV3 debranche sous un rapport qui disait
    "greffe posee".
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


def parametre(e):
    try:
        return str(e.get_editor_property("parameter_name"))
    except Exception:
        return ""


def remonte_vers(materiau, depart, predicat, profondeur=8):
    """Vrai si `predicat` est satisfait quelque part en amont de `depart`.

    ON REMONTE, ON NE SE CONTENTE PAS DE VOIR LE VOISIN. Le depot a paye qu'un
    rapport vert ne dit rien des liaisons qu'on ne lui a pas demande de
    verifier : une entree branchee sur un noeud dont l'entree a lui est vide
    transporte zero, et cela se lit exactement comme une greffe posee.
    """
    vus = set()
    pile = [(depart, 0)]
    while pile:
        e, d = pile.pop()
        if e is None or d > profondeur or e in vus:
            continue
        vus.add(e)
        if predicat(e):
            return True
        for _, s in entrees_de(materiau, e):
            if s is not None:
                pile.append((s, d + 1))
    return False


def main():
    verifier = ("--verifier" in sys.argv
                or os.environ.get("WORLDSEED_VERIFIER", "") == "1")

    M = unreal.EditorAssetLibrary.load_asset(SOL)
    if M is None:
        dire("!! materiau introuvable : %s" % SOL)
        return

    ex = list(L.get_material_expressions(M))
    dire("%s : %d expressions avant greffe" % (SOL.rsplit("/", 1)[-1], len(ex)))

    # --- idempotence PAR CONSTAT -----------------------------------------
    # LE CONSTAT PORTE SUR LES DEUX MOITIES, ET IL LE FAUT. Une greffe posee a
    # MOITIE -- la hauteur sans la normale -- passerait pour posee si l'on ne
    # cherchait qu'un des deux parametres, et le script se tairait sur ce qui
    # manque. On restaure alors plutot que de poser par-dessus : ce depot a
    # deja paye une greffe empilee sur la precedente.
    deja = set()
    for e in ex:
        if e.get_class().get_name() == "MaterialExpressionTextureSampleParameter2D":
            n = parametre(e)
            if n in (PARAM_TEXTURE, PARAM_NORMALE):
                deja.add(n)

    if len(deja) == 2:
        dire("greffe DEJA POSEE (%s) -- rien fait" % ", ".join(sorted(deja)))
        return

    if deja:
        dire("!! greffe A MOITIE posee : %s present, %s manquant"
             % (", ".join(sorted(deja)),
                ", ".join(sorted({PARAM_TEXTURE, PARAM_NORMALE} - deja))))
        dire("!! restaurer d'abord, puis rejouer depuis zero :")
        dire("!!   git checkout -- Content/Worldseed/Materials/M_WorldseedGround.uasset")
        return

    # --- les ancrages, cherches par CRITERE et jamais par position -------
    rvt = None          # la sortie RVT, dont on remplace l'entree WorldHeight
    uv_nappe = None     # les UV du monde entier, ceux de TexPoids/TexTeinte
    poids = None        # le commutateur qui dit "on est la nappe"
    z_geometrie = None  # ce qui alimente aujourd'hui WorldHeight

    for e in ex:
        c = e.get_class().get_name()
        if c == "MaterialExpressionRuntimeVirtualTextureOutput":
            rvt = e
        elif (c == "MaterialExpressionTextureSampleParameter2D"
                and parametre(e) in ("TexPoids", "TexTeinte")):
            for nom, s in entrees_de(M, e):
                if s is not None and "UV" in str(nom).upper():
                    uv_nappe = s
        elif (c == "MaterialExpressionStaticSwitchParameter"
                and parametre(e) == "PoidsDepuisTexture"):
            poids = e

    if rvt is not None:
        for nom, s in entrees_de(M, rvt):
            if "WorldHeight" in str(nom).replace(" ", ""):
                z_geometrie = s

    dire("ancrages : rvt=%s uv_nappe=%s commutateur=%s z_geometrie=%s"
         % (rvt is not None, uv_nappe is not None,
            poids is not None, z_geometrie is not None))

    if rvt is None or uv_nappe is None or poids is None:
        dire("!! ancrage manquant -- le graphe n'est pas celui qu'on croit,")
        dire("!! greffe ANNULEE plutot que posee au hasard")
        return

    if z_geometrie is None:
        dire("!! `WorldHeight` n'est alimente par rien : la branche SOL n'aurait")
        dire("!! aucun repli, et le commutateur rendrait zero hors de la nappe.")
        dire("!! greffe ANNULEE")
        return

    if verifier:
        dire("=== releve seul, rien n'a ete ecrit ===")
        return

    # --- la hauteur cuite -------------------------------------------------
    # LES MEMES UV QUE LES DEUX AUTRES TEXTURES, et c'est portant : la nappe
    # couvre le monde entier, et ses trois textures sont cuites dans la meme
    # convention -- un texel par cellule, ligne 0 au sud. Leur donner deux
    # chaines d'UV ferait glisser la hauteur d'un demi-monde le jour ou l'une
    # des deux change.
    tex = L.create_material_expression(
        M, unreal.MaterialExpressionTextureSampleParameter2D, -1350, 2200)
    tex.set_editor_property("parameter_name", PARAM_TEXTURE)
    relier(uv_nappe, "", tex, "UVs")

    bornes = L.create_material_expression(
        M, unreal.MaterialExpressionVectorParameter, -2400, 2200)
    bornes.set_editor_property("parameter_name", PARAM_BORNES)

    # LE MATERIAU REND UN Z DU MONDE EN CENTIMETRES, PAS UNE VALEUR NORMALISEE.
    # `VirtualTextureMaterial.usf` prend l'entree `WorldHeight` telle quelle et
    # la repacke lui-meme (`PackWorldHeight`) avec la transform du volume :
    # laisser passer le [0..1] de la texture ecrirait une altitude d'UN
    # CENTIMETRE partout, ce qui se lirait comme une RVT de hauteur qui marche
    # et poserait l'ancrage du feuillage au ras de zero.
    echelle = L.create_material_expression(
        M, unreal.MaterialExpressionMultiply, -1050, 2200)
    relier(tex, "R", echelle, "A")
    relier(bornes, "G", echelle, "B")

    decode = L.create_material_expression(
        M, unreal.MaterialExpressionAdd, -900, 2200)
    relier(echelle, "", decode, "A")
    relier(bornes, "R", decode, "B")

    # --- le commutateur ---------------------------------------------------
    # LA NAPPE LIT SA TEXTURE, LE RESTE GARDE SA GEOMETRIE. C'est le meme
    # commutateur STATIQUE que les poids et la teinte -- donc gratuit a
    # l'execution -- et il dit la meme chose : d'ou vient la donnee.
    #
    # ET LE REPLI COMPTE : le sol de fond et les chunks partagent ce maitre.
    # Leur donner la hauteur CUITE les ferait tous ecrire la meme altitude que
    # la nappe, ce qui n'aurait aucun sens -- et surtout, ils n'ecrivent dans
    # aucune RVT, donc cette branche ne doit rien changer pour eux.
    bascule = L.create_material_expression(
        M, unreal.MaterialExpressionStaticSwitchParameter, -700, 2200)
    bascule.set_editor_property("parameter_name", "PoidsDepuisTexture")
    bascule.set_editor_property("default_value", False)
    relier(decode, "", bascule, "True")
    relier(z_geometrie, "", bascule, "False")

    relier(bascule, "", rvt, "WorldHeight")

    # --- la normale cuite -------------------------------------------------
    # LE DECODAGE EST FAIT A LA MAIN, ET C'EST DELIBERE. Un echantillonneur de
    # carte de normales (`SAMPLERTYPE_NORMAL`) doit s'accorder a la texture PAR
    # DEFAUT du parametre, sans quoi la compilation echoue -- et notre texture
    # est cuite a l'execution, donc absente au moment de la greffe. Un
    # echantillonneur de couleur sur une texture sans sRGB rend la valeur
    # lineaire telle quelle ; `x * 2 - 1` suffit alors.
    texn = L.create_material_expression(
        M, unreal.MaterialExpressionTextureSampleParameter2D, -1350, 2500)
    texn.set_editor_property("parameter_name", PARAM_NORMALE)
    relier(uv_nappe, "", texn, "UVs")

    deux = L.create_material_expression(
        M, unreal.MaterialExpressionMultiply, -1050, 2500)
    deux.set_editor_property("const_b", 2.0)
    relier(texn, "RGB", deux, "A")

    centre = L.create_material_expression(
        M, unreal.MaterialExpressionSubtract, -900, 2500)
    centre.set_editor_property("const_b", 1.0)
    relier(deux, "", centre, "A")

    # LE MEME COMMUTATEUR QUE LA HAUTEUR, ET POUR LA MEME RAISON. Le sol de
    # fond et les chunks partagent ce maitre et n'ecrivent dans aucune RVT :
    # cette branche ne doit rien changer pour eux. Le repli est la normale
    # PLATE, qui est exactement ce que le moteur ecrirait sans branchement --
    # donc le comportement d'avant, a l'identique.
    plate = L.create_material_expression(
        M, unreal.MaterialExpressionConstant3Vector, -900, 2680)
    plate.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 1.0))

    bascule_n = L.create_material_expression(
        M, unreal.MaterialExpressionStaticSwitchParameter, -700, 2500)
    bascule_n.set_editor_property("parameter_name", "PoidsDepuisTexture")
    bascule_n.set_editor_property("default_value", False)
    relier(centre, "", bascule_n, "True")
    relier(plate, "", bascule_n, "False")

    relier(bascule_n, "", rvt, "Normal")

    L.recompile_material(M)
    unreal.EditorAssetLibrary.save_loaded_asset(M, False)

    # --- ON RELIT CE QU'ON A ECRIT ---------------------------------------
    # Ce depot a paye qu'une ecriture de propriete de materiau peut ne rien
    # faire sans rien dire.
    apres = list(L.get_material_expressions(M))
    rvt2 = None
    for e in apres:
        if e.get_class().get_name() == "MaterialExpressionRuntimeVirtualTextureOutput":
            rvt2 = e

    monte_a_la_texture = False
    garde_la_geometrie = False
    normale_branchee = False
    if rvt2 is not None:
        for nom, s in entrees_de(M, rvt2):
            n = str(nom).replace(" ", "")
            if s is None:
                continue
            if n == "WorldHeight":
                monte_a_la_texture = remonte_vers(
                    M, s,
                    lambda e: (e.get_class().get_name()
                               == "MaterialExpressionTextureSampleParameter2D"
                               and parametre(e) == PARAM_TEXTURE))
                garde_la_geometrie = remonte_vers(
                    M, s,
                    lambda e: e.get_class().get_name()
                    == "MaterialExpressionWorldPosition")
            elif n == "Normal":
                normale_branchee = remonte_vers(
                    M, s,
                    lambda e: (e.get_class().get_name()
                               == "MaterialExpressionTextureSampleParameter2D"
                               and parametre(e) == PARAM_NORMALE))

    dire("")
    dire("=== RELECTURE ===")
    dire("  expressions              %d -> %d" % (len(ex), len(apres)))
    dire("  sortie RVT conservee     %s" % (rvt2 is not None))
    dire("  WorldHeight remonte a %-10s %s" % (PARAM_TEXTURE, monte_a_la_texture))
    dire("  ... et garde le repli WorldPosition  %s" % garde_la_geometrie)
    dire("  Normal remonte a %-15s %s" % (PARAM_NORMALE, normale_branchee))
    try:
        dire("  compile                  %s"
             % M.get_editor_property("is_compiled_ok"))
    except Exception:
        dire("  compile                  (non expose)")

    if (rvt2 is not None and monte_a_la_texture and garde_la_geometrie
            and normale_branchee):
        dire("=== greffe posee et relue ===")
    else:
        dire("!! GREFFE INCOMPLETE -- restaurer par : git checkout -- "
             "Content/Worldseed/Materials/M_WorldseedGround.uasset")


main()
