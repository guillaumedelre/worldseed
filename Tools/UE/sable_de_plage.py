# Worldseed - donner a la plage son propre sable, sans toucher au desert.
#
# LE PROBLEME QU'IL RESOUT. Le sol ne melange que QUATRE matieres -- herbe,
# aride, roche, mousse -- et c'est structurel : leurs poids voyagent dans les
# quatre canaux RGBA de la couleur de sommet. Or la plage et le desert chaud
# portent EXACTEMENT les memes poids, [0, 1, 0, 0], et ne se distinguent que
# par leur teinte. Echanger la texture du slot aride les changerait tous les
# deux, canyons compris.
#
# LA GREFFE. Le C++ ecrit desormais la part d'estran dans UV2.Y -- le dernier
# canal libre du sommet -- et ce script insere un fondu entre deux sables a
# l'endroit ou TexArid est consomme. La MATIERE reste unique ; seule sa
# TEXTURE change au bord de l'eau.
#
#   UnrealEditor-Cmd.exe Worldseed.uproject -run=pythonscript
#     -script="Tools/UE/sable_de_plage.py" -unattended -nopause -nosplash
#
# IDEMPOTENT PAR CONSTAT, jamais par destruction : s'il retrouve sa greffe, il
# n'y touche pas. Le depot a deja paye une greffe posee DEUX FOIS sur la RVT --
# deux echantillonneurs, deux Lerp chaines, teinte appliquee deux fois.
import unreal

L = unreal.MaterialEditingLibrary
CHEMIN = "/Game/Worldseed/Materials/M_WorldseedGround"
PARAM = "TexAridPlage"
# Le sable du pack Egypte, deja cable dans MI_WorldseedGround_Egypt avant que
# le selecteur de packs ne soit supprime : l'instance est restee sur le disque.
SABLE = "/Game/Stylized_Egypt/Textures/landscape_textures/sand/T_sand_BaseColor"

def dire(t):
    unreal.log("[SABLE] %s" % t)

M = unreal.load_asset(CHEMIN)
if M is None:
    dire("!! materiau introuvable : %s" % CHEMIN)
    raise SystemExit

ex = list(L.get_material_expressions(M))

def par_parametre(nom):
    for e in ex:
        try:
            if e.get_editor_property("parameter_name") == nom:
                return e
        except Exception:
            pass
    return None

if par_parametre(PARAM) is not None:
    dire("greffe DEJA POSEE (%s existe) -- rien fait" % PARAM)
    raise SystemExit

arid = par_parametre("TexArid")
if arid is None:
    dire("!! TexArid introuvable -- le materiau n'est pas celui qu'on croit")
    raise SystemExit

# QUI CONSOMME TexArid : on ne le suppose pas, on le cherche. C'est la seule
# facon de rebrancher sans casser, et le graphe peut bouger.
cible, entree = None, None
for e in ex:
    try:
        entrees = list(L.get_inputs_for_material_expression(M, e))
        noms = list(L.get_material_expression_input_names(e))
    except Exception:
        continue
    for k, src in enumerate(entrees):
        if src == arid:
            cible, entree = e, (noms[k] if k < len(noms) else "A")
if cible is None:
    dire("!! personne ne consomme TexArid -- greffe annulee")
    raise SystemExit
dire("TexArid alimente %s par son entree '%s'"
     % (cible.get_class().get_name(), entree))

# L'UV2 EXISTE DEJA DANS LE GRAPHE : il sert a la teinte B, dont il n'emploie
# que la composante R. On le reutilise plutot que d'en poser un second -- deux
# TextureCoordinate sur le meme index divergeraient a la premiere retouche.
uv2 = None
for e in ex:
    if e.get_class().get_name() == "MaterialExpressionTextureCoordinate":
        try:
            if e.get_editor_property("coordinate_index") == 2:
                uv2 = e
        except Exception:
            pass
if uv2 is None:
    dire("UV2 absent du graphe -- on en pose un")
    uv2 = L.create_material_expression(
        M, unreal.MaterialExpressionTextureCoordinate, -1400, 900)
    uv2.set_editor_property("coordinate_index", 2)
else:
    dire("UV2 retrouve dans le graphe, reutilise")

try:
    x, y = L.get_material_expression_node_position(arid)
except Exception:
    x, y = -800, 400

sable = unreal.load_asset(SABLE)
if sable is None:
    dire("!! texture de sable introuvable : %s" % SABLE)
    raise SystemExit

# LA TEXTURE PAR DEFAUT EST CELLE DE LA PLAGE, ET C'EST VOULU : aucune
# instance n'a besoin d'etre retouchee pour que la greffe rende quelque chose.
plage = L.create_material_expression(
    M, unreal.MaterialExpressionTextureSampleParameter2D, x, y + 320)
plage.set_editor_property("parameter_name", PARAM)
plage.set_editor_property("texture", sable)
try:
    plage.set_editor_property("group", "Sol")
except Exception:
    pass

masque = L.create_material_expression(
    M, unreal.MaterialExpressionComponentMask, x - 300, y + 560)
masque.set_editor_property("r", False)
masque.set_editor_property("g", True)
masque.set_editor_property("b", False)
masque.set_editor_property("a", False)

fondu = L.create_material_expression(
    M, unreal.MaterialExpressionLinearInterpolate, x + 320, y + 160)

# LE PLACAGE DU SABLE DE PLAGE SUIT CELUI DES AUTRES MATIERES. Sans cette
# liaison il carrelerait a l'UV0 brut, donc a une toute autre echelle -- et ce
# depot a deja paye qu'une frequence spatiale juste sur une carte de demo
# devienne fausse sur un monde de 64 km.
uvs = None
try:
    entrees = list(L.get_inputs_for_material_expression(M, arid))
    noms = list(L.get_material_expression_input_names(arid))
    for k, src in enumerate(entrees):
        if src is not None and noms[k] == "UVs":
            uvs = src
except Exception:
    pass

liens = [
    (uvs, "", plage, "UVs") if uvs is not None else None,
    (uv2, "", masque, ""),
    (arid, "RGB", fondu, "A"),
    (plage, "RGB", fondu, "B"),
    (masque, "", fondu, "Alpha"),
    (fondu, "", cible, entree),
]
for lien in liens:
    if lien is None:
        dire("!! l'UV de placage de TexArid n'a pas ete retrouve")
        continue
    a, sortie, b, e = lien
    ok = L.connect_material_expressions(a, sortie, b, e)
    dire("   %-28s -> %-28s [%s] : %s"
         % (a.get_class().get_name().replace("MaterialExpression", ""),
            b.get_class().get_name().replace("MaterialExpression", ""), e,
            "ok" if ok else "ECHEC"))

L.recompile_material(M)
unreal.EditorAssetLibrary.save_asset(CHEMIN)

# ON RELIT PLUTOT QUE DE SUPPOSER. Une liaison refusee ne leve rien : le
# retour de connect_material_expressions est la seule alerte, et le depot a
# deja vu « 9 connexions sur 12 » sans savoir lesquelles manquaient.
ex2 = list(L.get_material_expressions(M))
ok = False
for e in ex2:
    try:
        entrees = list(L.get_inputs_for_material_expression(M, e))
        noms = list(L.get_material_expression_input_names(e))
    except Exception:
        continue
    for k, src in enumerate(entrees):
        if src is not None \
                and src.get_class().get_name() == "MaterialExpressionLinearInterpolate" \
                and e == cible and noms[k] == entree:
            ok = True
dire("CONTROLE : %s alimente par le fondu -- %s"
     % (entree, "OUI" if ok else "NON, LA GREFFE N'A PAS PRIS"))
dire("parametres texture : %s"
     % ", ".join(str(n) for n in L.get_texture_parameter_names(M)))
dire("FIN")
