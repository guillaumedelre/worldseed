# Worldseed - relever ce qu'Ultra Dynamic Sky/Weather expose pour le SON.
#
# POURQUOI CE SCRIPT, ET CE QU'IL A DEMENTI. On le lancait en croyant
# qu'« aucune brique sonore du pack n'est employee » -- c'etait ecrit dans
# l'inventaire publie -- et parce que ce depot a paye CINQ fois la doctrine du
# pack, qui desarme tout ce qui coute : « Use Auroras », les dix surcharges
# manuelles d'UDW, le givre, les gouttes d'ecran. Les cinq fois, le reste etait
# deja cable.
#
# LE RELEVE A DIT L'INVERSE, et c'est la premiere fois. Enable Weather Sound
# Effects arrive VRAI, l'occlusion en interieur arrive VRAIE, les deux sources
# meteo sont assignees et les six curseurs de volume valent 1 : la pluie, le
# vent, le tonnerre et la poussiere s'entendaient DEJA, pilotes par l'etat que
# le C++ ecrit depuis le 28 septembre. Un seul trou : Environment Sound a None,
# donc les vingt-sept chants d'oiseaux, les insectes de nuit et les deux vents
# d'arbres dormaient sur le disque.
#
# LA REGLE QUE CE SCRIPT SERT reste la meme, et elle est ecrite en toutes
# lettres dans la documentation du pack : devant un effet qui ne s'affiche pas,
# on cherche dans CET ORDRE -- l'interrupteur maitre, puis `OnRep_<nom>`, puis
# `Static Properties - <categorie>`. Elle vaut aussi a l'envers : on ne suppose
# pas qu'une brique est eteinte, on le VERIFIE.
#
#   UnrealEditor-Cmd.exe Worldseed.uproject -run=pythonscript
#     -script="Tools/UE/inventaire_son.py" -unattended -nopause -nosplash
#
# L'EDITEUR DOIT ETRE ARRETE : le plugin ecoute sur le port 8000, et un
# commandlet echoue alors sur « HttpListener unable to bind to 127.0.0.1:8000 ».
#
# IL N'ECRIT RIEN. C'est un releve. L'ecriture reste en C++, ou elle est RELUE.
#
# Le resultat se lit dans Saved/Logs/Worldseed.log -- la sortie standard de
# PowerShell ne le capture pas.
import re

import unreal

CARTE = "/Game/Worldseed/Maps/L_Worldseed_Proc"
DOSSIER_SON = "/Game/UltraDynamicSky/Sound"

# LES NOMS QU'ON INTERROGE UN PAR UN, ET C'EST LA GARDE ANTI-TRONCATURE.
#
# Un inventaire TRONQUE se lit exactement comme un inventaire complet : le
# balayage des variables d'aurore s'etait arrete a dix-huit entrees, il avait
# manque « Use Auroras » -- la seule qui comptait -- et le diagnostic d'une
# seance est parti avec. On demande donc NOMMEMENT, et une absence est BRUYANTE.
ATTENDUS = [
    # --- l'interrupteur maitre, celui que la doc nomme ------------------------
    "Enable Weather Sound Effects",
    "Weather Sound Effects",
    "Enable Sound Effects",
    # --- les volumes exposes : la doc recommande EXPLICITEMENT de les piloter
    #     a l'execution plutot que de basculer les effets eux-memes.
    "Wind Volume",
    "Rain Volume",
    "Thunder Volume",
    "Snow Volume",
    "Sound Volume",
    "Master Sound Volume",
    "Weather Sound Volume",
    "Environment Sound Volume",
    # --- le tonnerre et son retard -------------------------------------------
    "Close Thunder Delay Per KM",
    "Thunder Delay",
    "Thunder Sound Delay",
    # --- l'occlusion : elle attenue et etouffe les sons en interieur ---------
    "Enable Sound Occlusion",
    "Sound Occlusion",
    "Sound Occlusion Update Period",
    "Occluded Volume Multiplier",
    "Occluded Low Pass Frequency",
    "Occlusion Sampling Mode",
    "Enable Player Occlusion",
    "Player Occlusion",
    # --- le son d'ambiance : le seul des trois qui soit une DONNEE -----------
    "Environment Sound",
    "Environment Sounds",
    "Current Environment Sound",
    "Enable Environment Sound",
    # --- les sources, pour le journal ----------------------------------------
    "Global Weather Sound",
    "Directional Weather Sound",
    "Weather Sound Mixer",
    "Sound Update Period",
    # --- le niveau d'eau, qui occulte totalement (la doc le dit) -------------
    "Use UDS Water Level",
    "Global Water Level",
]

# LES FONCTIONS QU'ON CHERCHE. `Change Environment Sound` prend un argument :
# notre pont ne sait appeler que les fonctions SANS parametre, donc son
# existence ne suffit pas -- il faudra compter ses parametres avant d'y compter.
FONCTIONS = [
    "Set Enable Weather Sound Effects",
    "Change Environment Sound",
    "Get Current Temperature",
    "Static Properties - Sound",
    "Static Properties - Weather",
]

MOTIF = re.compile(r"sound|audio|volume|occlusion|thunder", re.IGNORECASE)
MOTIF_ETROIT = re.compile(r"sound|audio|occlusion", re.IGNORECASE)


def dire(t):
    unreal.log("[SON] %s" % t)


def lire(obj, nom):
    """Rend (trouve, valeur). Une lecture qui echoue doit etre VISIBLE."""
    try:
        return True, obj.get_editor_property(nom)
    except Exception:
        return False, None


def montrer(v):
    if v is None:
        return "None"
    if isinstance(v, bool):
        return "VRAI" if v else "faux"
    if isinstance(v, float):
        return "%.4g" % v
    try:
        return v.get_name()
    except Exception:
        pass
    return str(v)[:60]


# --- LES ACTEURS DE LA CARTE, ET LEUR DEFAUT DE CLASSE ----------------------
#
# ON IMPRIME LES DEUX COLONNES : une carte peut surcharger le defaut de classe,
# et supposer les deux egaux serait un pari.
sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not sub.load_level(CARTE):
    dire("!! impossible de charger %s -- rien releve" % CARTE)
    raise SystemExit

acteurs = {}
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    nom = a.get_class().get_name()
    if "Ultra_Dynamic_Weather" in nom:
        acteurs["meteo"] = a
    elif "Ultra_Dynamic_Sky" in nom:
        acteurs["ciel"] = a

if not acteurs:
    dire("!! aucun acteur Ultra_Dynamic_* dans la carte -- rien releve")
    raise SystemExit

dire("=" * 78)
for role, a in sorted(acteurs.items()):
    dire("%s : %s (%s)" % (role, a.get_actor_label(), a.get_class().get_name()))


def chemin_bp(acteur):
    """Le chemin du BLUEPRINT, jamais celui de la classe generee.

    `list_variables` prend le chemin du Blueprint ; celui de la classe generee,
    qui finit en `_C`, rend ZERO en silence.
    """
    c = acteur.get_class().get_path_name()
    if c.endswith("_C"):
        c = c[:-2]
    if c.endswith("."):
        c = c[:-1]
    return c


# --- L'ENUMERATION, ET ELLE SE COMPTE --------------------------------------
tous = {}
for role, a in sorted(acteurs.items()):
    total, retenus = 0, []
    tous[role] = []
    try:
        variables = unreal.BlueprintService.list_variables(chemin_bp(a))
        total = len(variables)
        for v in variables:
            # C'EST UN STRUCT, PAS UN DICT : le champ s'appelle `variable_name`,
            # pas `name`. Un `get_editor_property("name")` leve, et le repli
            # `str(v)` imprime toute la struct -- vingt lignes par variable.
            try:
                n = str(v.variable_name)
            except Exception:
                n = str(v)
            tous[role].append(n)
            if MOTIF.search(n):
                retenus.append(n)
    except Exception as e:
        dire("%s : list_variables indisponible (%s)" % (role, str(e)[:80]))

    dire("-" * 78)
    dire("%s : entrees rendues %d, dont %d citant sound/audio/volume/occlusion/thunder"
         % (role, total, len(retenus)))
    # ON N'IMPRIME LE DETAIL QUE POUR LE MOTIF ETROIT, mais AVEC SA VALEUR :
    # « Wind Volume existe » ne dit pas si le vent s'entend, et ce depot a pris
    # cinq fois une variable presente pour une fonctionnalite active. Les noms
    # qui ne citent que « volume » ou « thunder » sont imprimes aussi -- ce sont
    # les six curseurs de mixage, et la premiere version de ce script les
    # filtrait justement.
    for n in sorted(retenus):
        if MOTIF_ETROIT.search(n) or re.search(r"\bvolume\b|thunder", n, re.IGNORECASE):
            ok, val = lire(a, n)
            dire("    %-52s %s" % (n[:52], montrer(val) if ok else "(non lisible)"))

# --- LA LISTE EXPLICITE : c'est elle qui tranche ---------------------------
dire("=" * 78)
dire("%-38s %-20s %-20s" % ("nom", "instance", "defaut de classe"))
dire("-" * 78)

manquants = []
for nom in ATTENDUS:
    ligne, vu = None, False
    for role, a in sorted(acteurs.items()):
        ok, val = lire(a, nom)
        if not ok:
            continue
        vu = True
        cdo = None
        try:
            cdo = unreal.get_default_object(a.get_class())
        except Exception:
            pass
        okd, vald = lire(cdo, nom) if cdo else (False, None)
        ligne = "%-38s %-20s %-20s  [%s]" % (
            nom[:38], montrer(val)[:20],
            montrer(vald)[:20] if okd else "(illisible)", role)
        break
    if vu:
        dire(ligne)
    else:
        manquants.append(nom)
        dire("%-38s %s" % (nom[:38], "ABSENT des deux acteurs"))

dire("-" * 78)
dire("-> %d nom(s) absent(s) sur %d demandes" % (len(manquants), len(ATTENDUS)))

# --- LES FONCTIONS, ET LE TEMOIN QUI VALIDE LA SONDE -----------------------


def fonctions_de(acteur):
    try:
        return unreal.BlueprintService.list_functions(chemin_bp(acteur))
    except Exception:
        return None


def nom_fonction(f):
    try:
        return str(f.function_name)
    except Exception:
        return str(f)


dire("=" * 78)

# LE TEST SE VALIDE SUR UN CAS CONNU AVANT DE SERVIR.
#
# « OnRep_Animate Time of Day » EXISTE : notre C++ l'appelle et le journal dit
# « rappel OnRep appele ». Si la sonde ne le trouve pas, ce n'est pas qu'il
# n'existe pas -- c'est que la sonde est cassee. Ce depot a deja compte zero
# composant d'herbe de Landscape sur son propre monde ET sur la demo du pack,
# qui en montre un tapis : un compteur qui rend zero ne mesure rien tant qu'on
# ne l'a pas vu rendre autre chose.
catalogue = {}
for role, a in sorted(acteurs.items()):
    fns = fonctions_de(a)
    catalogue[role] = [nom_fonction(f) for f in fns] if fns else []
    dire("%s : %d fonction(s) listee(s)" % (role, len(catalogue[role])))

valide = any("OnRep_Animate Time of Day" in v for v in catalogue.values())
if valide:
    dire("temoin : « OnRep_Animate Time of Day » trouve -- la sonde de fonctions marche")
else:
    dire("!! TEMOIN EN ECHEC : « OnRep_Animate Time of Day » introuvable alors que "
         "le C++ l'appelle avec succes. La sonde ne mesure RIEN, et les absences "
         "ci-dessous ne prouvent rien.")

dire("-" * 78)
dire("les fonctions attendues :")
for nom in FONCTIONS:
    ou = [r for r, v in catalogue.items() if nom in v]
    dire("    %-40s %s" % (nom[:40], ", ".join(ou) if ou else "ABSENTE"))

dire("-" * 78)
dire("les rappels OnRep_ des noms attendus :")
trouves = 0
for nom in ATTENDUS:
    cible = "OnRep_%s" % nom
    ou = [r for r, v in catalogue.items() if cible in v]
    if ou:
        trouves += 1
        dire("    %-44s [%s]" % (cible[:44], ", ".join(ou)))
dire("    -> %d rappel(s) sur %d noms%s"
     % (trouves, len(ATTENDUS),
        "" if valide else " -- MAIS LE TEMOIN A ECHOUE, chiffre sans valeur"))

# LES « STATIC PROPERTIES », que la documentation nomme comme LA regle generale.
dire("-" * 78)
statiques = sorted({f for v in catalogue.values() for f in v
                    if f.startswith("Static Properties")})
dire("les fonctions « Static Properties - ... » du pack : %d" % len(statiques))
for f in statiques:
    dire("    %s" % f)

# ET TOUTE FONCTION DONT LE NOM CITE LE SON : c'est le balayage large, qui
# rattrape ce que la liste explicite n'a pas su nommer.
dire("-" * 78)
sonores = sorted({f for v in catalogue.values() for f in v if MOTIF_ETROIT.search(f)})
dire("toute fonction citant sound/audio/occlusion : %d" % len(sonores))
for f in sonores:
    dire("    %s" % f)

# --- LES ASSETS SONORES DU PACK --------------------------------------------
dire("=" * 78)
registre = unreal.AssetRegistryHelpers.get_asset_registry()
actifs = registre.get_assets_by_path(unreal.Name(DOSSIER_SON), recursive=True)
dire("assets sous %s : %d" % (DOSSIER_SON, len(actifs)))
for d in sorted(actifs, key=lambda x: str(x.package_name)):
    dire("    %-52s %s" % (str(d.asset_name)[:52], str(d.asset_class_path.asset_name)))

# --- LE SON D'AMBIANCE POSE, S'IL Y EN A UN --------------------------------
dire("-" * 78)
for role, a in sorted(acteurs.items()):
    ok, env = lire(a, "Environment Sound")
    if not ok:
        continue
    if env is None:
        dire("« Environment Sound » est a None sur %s -- AUCUNE ambiance ne joue" % role)
    else:
        dire("« Environment Sound » = %s sur %s" % (montrer(env), role))
        for champ in ("Sound", "Metasound", "Volume", "Volume Scale"):
            oke, ve = lire(env, champ)
            if oke:
                dire("    %-16s %s" % (champ, montrer(ve)))
    break

# --- DE QUOI UN SON D'AMBIANCE EST-IL FAIT ? ------------------------------
#
# C'EST CETTE PASSE QUI DECIDE DE LA FORME DU CHANTIER. Un
# `UDS_Environment_Sound` ne porte que trois champs -- une source metasound, une
# echelle de volume, des surcharges de parametre -- donc on peut en fabriquer
# par script. Mais UNE AMBIANCE NOUVELLE demanderait une SOURCE nouvelle, et la
# question est de savoir si les sous-sources du pack sont jouables seules.
#
# LE FORMAT DE SORTIE TRANCHE, et la documentation est explicite : « si vous
# voulez que votre source soit affectee par l'occlusion et le panoramique
# directionnel a quatre canaux, le format de sortie DOIT etre 5.1 ». Releve du
# 29 septembre 2026 : Forest_Example en 5.1, Forest_Birds en MONO,
# Forest_TreeWind en QUAD -- aucune des deux sous-sources n'est donc utilisable
# telle quelle, et composer une ambiance nouvelle demanderait d'ecrire un graphe
# MetaSound, qui n'est pas scriptable. Meme mur que Niagara.
dire("=" * 78)
par_nom = {}
for d in registre.get_assets_by_path(unreal.Name(DOSSIER_SON), recursive=True):
    par_nom.setdefault(str(d.asset_name), []).append(
        (str(d.package_name), str(d.asset_class_path.asset_name)))

for n in ("Forest_Example", "Forest_Birds", "Forest_TreeWind"):
    for chemin, classe in par_nom.get(n, [("ABSENT", "")]):
        if "MetaSoundSource" not in classe:
            continue
        src = unreal.load_asset(chemin)
        if src is None:
            dire("%-18s %s : INCHARGEABLE" % (n, chemin))
            continue
        ok, fmt = lire(src, "output_format")
        dire("%-18s %-24s format %s"
             % (n, classe, fmt if ok else "(illisible)"))

# LE DATA ASSET LUI-MEME : trois champs, donc scriptable.
for chemin, classe in par_nom.get("Forest_Example", []):
    if "Environment_Sound" not in classe:
        continue
    env = unreal.load_asset(chemin)
    if env is None:
        continue
    dire("-" * 78)
    dire("le data asset %s (%s) :" % (env.get_name(), classe))
    for c in ("Metasound Source", "Volume", "Parameter Overrides"):
        ok, v = lire(env, c)
        dire("    %-24s %s" % (c, montrer(v) if ok else "ABSENT"))
    dire("    classe a instancier : %s" % env.get_class().get_path_name())

dire("=" * 78)
dire("releve termine -- aucune ecriture")
