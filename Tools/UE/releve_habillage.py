# Worldseed - releve en LECTURE SEULE, pour preparer deux chantiers d'habillage.
#
# POURQUOI UN COMMANDLET ET NON MCP : le lien MCP tombe des qu'on relance
# l'editeur plusieurs fois de suite, donc a chaque compilation. Ce script se
# lance l'editeur ARRETE :
#
#   UnrealEditor-Cmd.exe Worldseed.uproject -run=pythonscript
#     -script="Tools/UE/releve_habillage.py" -unattended -nopause -nosplash
#
# Le resultat se lit dans Saved/Logs/Worldseed.log : la sortie standard de
# PowerShell ne le capture pas.
import unreal

def dire(t):
    unreal.log("[RELEVE] %s" % t)

dire("=" * 70)

# --- 1. Le materiau du sol : ou brancher le sable de plage ---------------
M = unreal.load_asset("/Game/Worldseed/Materials/M_WorldseedGround")
if M is None:
    dire("!! M_WorldseedGround introuvable")
else:
    exprs = []
    try:
        exprs = list(M.get_editor_property("expression_collection")
                      .get_editor_property("expressions"))
    except Exception as e:
        dire("expression_collection indisponible (%s), essai direct" % e)
        try:
            exprs = list(M.get_editor_property("expressions"))
        except Exception as e2:
            dire("!! impossible de lister les expressions : %s" % e2)

    dire("M_WorldseedGround : %d expressions" % len(exprs))
    for e in exprs:
        cls = e.get_class().get_name()
        nom = ""
        for prop in ("parameter_name", "texture", "desc"):
            try:
                v = e.get_editor_property(prop)
                if v:
                    nom += " %s=%s" % (prop, v.get_name() if hasattr(v, "get_name") else v)
            except Exception:
                pass
        try:
            x = e.get_editor_property("material_expression_editor_x")
            y = e.get_editor_property("material_expression_editor_y")
        except Exception:
            x = y = 0
        dire("   %-42s @(%6d,%6d)%s" % (cls, x, y, nom))

    # Les parametres deja exposes : c'est la que le slot aride se nomme.
    for genre, fn in (("scalaire", "get_scalar_parameter_names"),
                      ("vecteur", "get_vector_parameter_names"),
                      ("texture", "get_texture_parameter_names"),
                      ("switch statique", "get_static_switch_parameter_names")):
        try:
            noms = getattr(unreal.MaterialEditingLibrary, fn)(M)
            dire("parametres %s : %s" % (genre, ", ".join(str(n) for n in noms)))
        except Exception as e:
            dire("parametres %s : indisponible (%s)" % (genre, e))

# --- 2. Les textures que porte l'instance Egypte, deja cablee -----------
for inst in ("MI_WorldseedGround_Orasot", "MI_WorldseedGround_Egypt"):
    I = unreal.load_asset("/Game/Worldseed/Materials/%s" % inst)
    if I is None:
        dire("!! %s introuvable" % inst)
        continue
    dire("-" * 70)
    dire("%s :" % inst)
    try:
        for p in I.get_editor_property("texture_parameter_values"):
            info = p.get_editor_property("parameter_info")
            val = p.get_editor_property("parameter_value")
            dire("   %-28s = %s" % (info.get_editor_property("name"),
                                    val.get_name() if val else "None"))
    except Exception as e:
        dire("   !! lecture impossible : %s" % e)

# --- 3. Le cout des maillages qui remplacent LowPolyForest --------------
dire("-" * 70)
dire("COUT DES MAILLAGES -- c'est le tapis qui decide, il pese 67 % des instances")
lots = {
    "tapis (remplacants)": ["/Game/Stylized_Forest/Meshes/plants/SM_grass_01",
                            "/Game/Stylized_Forest/Meshes/plants/SM_grass_02",
                            "/Game/Stylized_Forest/Meshes/plants/SM_grass_03"],
    "tapis (temoins)":     ["/Game/Orasot_Bundle/LowPolyForestVol2/StaticMeshes/Environment/SM_Env_Grass_small",
                            "/Game/Orasot_Bundle/StylizedForestLandscape/Meshes/SM_Grass"],
    "sous-bois":           ["/Game/Stylized_Forest/Meshes/plants/SM_shrub_02",
                            "/Game/Stylized_Forest/Meshes/plants/SM_shrub_flower_01",
                            "/Game/Stylized_Forest/Meshes/plants/SM_flower_01",
                            "/Game/Stylized_Forest/Meshes/plants/SM_flower_02"],
    "pierres":             ["/Game/Stylized_Forest/Meshes/stones/SM_small_stone_01",
                            "/Game/Stylized_Forest/Meshes/stones/SM_small_stone_02",
                            "/Game/Stylized_Forest/Meshes/stones/SM_small_stone_03",
                            "/Game/Stylized_Forest/Meshes/stones/SM_small_stone_04",
                            "/Game/Stylized_Forest/Meshes/stones/SM_stone_01",
                            "/Game/Stylized_Forest/Meshes/stones/SM_stone_03"],
    "bois mort":           ["/Game/Stylized_Forest/Meshes/trees/SM_branch_01",
                            "/Game/Stylized_Forest/Meshes/trees/SM_branch_02",
                            "/Game/Stylized_Forest/Meshes/trees/SM_stump_02",
                            "/Game/Stylized_Forest/Meshes/trees/SM_log_01",
                            "/Game/Stylized_Forest/Meshes/trees/SM_log_02",
                            "/Game/Stylized_Forest/Meshes/trees/SM_stump_01"],
    "ARBRES -- conifere ou feuillu ?": [
                            "/Game/Stylized_Forest/Meshes/trees/SM_tree_0%d" % i for i in range(1, 8)],
    "Egypte":              ["/Game/Stylized_Egypt/Meshes/plants/SM_tree_01",
                            "/Game/Stylized_Egypt/Meshes/plants/SM_bush_01",
                            "/Game/Stylized_Egypt/Meshes/plants/SM_grass",
                            "/Game/Stylized_Egypt/Meshes/stones/SM_stone_01"],
}
for lot, chemins in lots.items():
    dire("")
    dire("  %s" % lot)
    for c in chemins:
        SM = unreal.load_asset(c)
        if SM is None:
            dire("     %-26s INTROUVABLE" % c.split("/")[-1])
            continue
        try:
            tris = SM.get_num_triangles(0)
            lods = SM.get_num_lods()
            b = SM.get_bounds().box_extent
            # LA TAILLE EST CE QUI DECIDE DE LA COUCHE : le depot a paye un mur
            # d'arbustes pour avoir ajoute un maillage de 3 m dans une couche
            # dont les voisins faisaient 1,4.
            dire("     %-26s %7d tri  %d LOD  %5.2f x %5.2f x %5.2f m"
                 % (SM.get_name(), tris, lods,
                    b.x * 2 / 100.0, b.y * 2 / 100.0, b.z * 2 / 100.0))
        except Exception as e:
            dire("     %-26s !! %s" % (SM.get_name(), e))

dire("=" * 70)
dire("RELEVE TERMINE")
