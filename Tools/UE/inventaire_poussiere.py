# Worldseed - relever ce qu'Ultra Dynamic Sky/Weather expose pour la POUSSIERE,
# le SABLE et le VENT.
#
# POURQUOI CE SCRIPT. Notre C++ ecrit fidelement la variable « Dust » sur
# l'acteur meteo, sur la bonne echelle, et l'ecriture est ACCEPTEE -- le journal
# le dit. Et on ne voit jamais de poussiere en jeu. Ce depot connait cette forme
# d'echec par coeur : une ecriture qui prend, sur une fonctionnalite ETEINTE.
# L'aurore a coute une seance entiere pour cette raison exacte -- « Use Auroras »
# arrivait a False, et l'intensite de 0,12 qu'on reglait n'etait evaluee par
# personne.
#
# LA REGLE QUE CE SCRIPT SERT : devant un effet qui ne s'affiche pas, chercher
# l'INTERRUPTEUR MAITRE avant de regler l'intensite.
#
#   UnrealEditor-Cmd.exe Worldseed.uproject -run=pythonscript
#     -script="Tools/UE/inventaire_poussiere.py" -unattended -nopause -nosplash
#
# L'EDITEUR DOIT ETRE ARRETE : le plugin ecoute sur le port 8000, et un
# commandlet echoue alors sur « HttpListener unable to bind to 127.0.0.1:8000 »
# -- une erreur qui fait tomber tout le commandlet, pas seulement l'ecoute.
#
# IL N'ECRIT RIEN. C'est un releve. L'ecriture reste en C++, ou elle est RELUE :
# `WriteNumber`/`WriteBool` rendent un booleen, et ce depot a paye trois
# ecritures de parametre devenues des no-op silencieuses.
#
# Le resultat se lit dans Saved/Logs/Worldseed.log -- la sortie standard de
# PowerShell ne le capture pas.
import re

import unreal

CARTE = "/Game/Worldseed/Maps/L_Worldseed_Proc"
PRESETS = "/Game/UltraDynamicSky/Blueprints/Weather_Effects/Weather_Presets/"

# LES NOMS QU'ON INTERROGE UN PAR UN, ET C'EST LA GARDE ANTI-TRONCATURE.
#
# Un inventaire TRONQUE se lit exactement comme un inventaire complet : le
# balayage des variables d'aurore s'etait arrete a dix-huit entrees, il avait
# manque « Use Auroras » -- c'est-a-dire la seule qui comptait -- et le
# diagnostic d'une seance est parti avec. On demande donc NOMMEMENT ce qu'on
# veut savoir, et une absence est BRUYANTE au lieu de passer pour un silence.
ATTENDUS = [
    # --- les interrupteurs maitres soupconnes ---------------------------------
    "Enable Dust Particles",
    "Enable Post Process Wind Fog",
    "Post Process Wind Fog Enabled",
    "Post Process Wind Fog Active",
    "Enable Wind Debris",
    # --- les surcharges manuelles : UDW en a une PAR CURSEUR, et nous n'en
    #     posons qu'UNE (Thunder/Lightning). Le commentaire de notre propre code
    #     dit pourquoi elle est indispensable : « sans elle, UDS reprend la main
    #     depuis son propre systeme de types et notre ecriture serait ecrasee au
    #     tick suivant ». Le raisonnement vaut mot pour mot pour Dust et le vent.
    "Dust - Manual Override",
    "Wind Intensity - Manual Override",
    "Rain - Manual Override",
    "Snow - Manual Override",
    "Fog - Manual Override",
    "Cloud Coverage - Manual Override",
    "Thunder/Lightning - Manual Override",
    "Material Dust Coverage - Manual Override",
    # --- les drapeaux DERIVES : s'ils sont recalcules par la machine d'etats
    #     d'UDW plutot que depuis notre curseur, notre ecriture est fidele et
    #     invisible. C'est le piege de l'aurore en habit neuf.
    "Dusty",
    "Currently Dusty",
    # --- le dosage -----------------------------------------------------------
    "Dust",
    "Dust Amount",
    "Dust Bias",
    "Max Dust Coverage",
    "Dust Color",
    "Dust Spawn Rate Scale",
    "Dust Multiplier Above Clouds",
    "Dust Clear Speed when Calm",
    "Dust Clear Speed when Windy",
    "PPWF Intensity from Dust",
    "Material Dust Coverage",
    # --- le vent, et SON UNITE ------------------------------------------------
    "Wind Intensity",
    "Wind Direction",
    "Knots at Wind Intensity 10",
    "Wind Gust Multiplier",
    "Wind Gust Update Period",
    "Enable Wind Directional Source",
    "Wind Directional Source Speed Scale",
    # --- l'entree du volet B : le systeme de particules --------------------
    "Dust Niagara System",
]

# CE QU'ON LIT DANS CHAQUE PREREGLAGE, pour CONFIRMER un releve fait par parsing
# binaire des .uasset -- ce que la regle du projet interdit. Le parseur avait ete
# valide sur un temoin connu, mais une valeur plausible lue comme une mesure est
# le piege que ce depot paye le plus souvent : on la reprend donc par l'API.
CHAMPS_PRESET = [
    "Cloud Coverage", "Rain", "Snow", "Dust", "Fog",
    "Wind Intensity", "Thunder/Lightning",
    "Material Snow Coverage", "Material Dust Coverage", "Material Wetness",
]

MOTIF = re.compile(r"dust|sand|wind|ppwf|fog", re.IGNORECASE)


def dire(t):
    unreal.log("[POUSSIERE] %s" % t)


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
        return v.get_path_name()
    except Exception:
        pass
    return str(v)[:70]


# --- LES ACTEURS DE LA CARTE, ET LEUR DEFAUT DE CLASSE ----------------------
#
# ON IMPRIME LES DEUX COLONNES. Le cas de l'aurore avait ete resolu sur le
# defaut de classe, mais une carte peut surcharger : une difference entre les
# deux est elle-meme une information, et la supposer nulle serait un pari.
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

# --- L'ENUMERATION, ET ELLE SE COMPTE --------------------------------------
#
# `dir()` rend les noms PYTHON en minuscules a soulignements, pas les noms
# BLUEPRINT a espaces (« Dust - Manual Override ») : il ne peut donc pas servir
# de reference ici. `BlueprintService.list_variables` rend les vrais noms. Il
# rend zero pendant un PIE -- il n'y en a pas en commandlet.
tous = {}  # role -> liste complete des noms de variables, pour la recherche approchee
for role, a in sorted(acteurs.items()):
    total, retenus = 0, []
    tous[role] = []
    try:
        chemin = a.get_class().get_path_name().rstrip("_C").rstrip(".")
        variables = unreal.BlueprintService.list_variables(chemin)
        total = len(variables)
        for v in variables:
            # C'EST UN STRUCT, PAS UN DICT. `list_variables` rend des
            # `BlueprintVariableInfo` : un `v.get("name")` echoue en silence et
            # l'on retombe sur `str(v)`, qui imprime toute la struct -- vingt
            # lignes illisibles par variable.
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
    dire("%s : entrees rendues %d, dont %d citant dust/sand/wind/ppwf/fog"
         % (role, total, len(retenus)))
    # ON N'IMPRIME QUE LA POUSSIERE ET LE SABLE. Le motif attrape aussi tout le
    # brouillard et tout le vent du pack -- plus de deux cents entrees sur les
    # deux acteurs -- et noyer le releve le rend illisible, donc inutile. Le
    # COMPTE reste affiche : c'est lui qui dit si l'inventaire est complet.
    for n in sorted(retenus):
        if re.search(r"dust|sand", n, re.IGNORECASE):
            dire("    %s" % n)

# --- LA LISTE EXPLICITE : c'est elle qui tranche ---------------------------
dire("=" * 78)
dire("%-42s %-18s %-18s" % ("nom", "instance", "defaut de classe"))
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
        ligne = "%-42s %-18s %-18s  [%s]" % (
            nom[:42], montrer(val)[:18],
            montrer(vald)[:18] if okd else "(illisible)", role)
        break
    if vu:
        dire(ligne)
    else:
        manquants.append(nom)
        dire("%-42s %s" % (nom[:42], "ABSENT des deux acteurs"))

# ET LE RAPPEL DE REPLICATION. Poser un booleen par reflexion NE DECLENCHE AUCUN
# rappel : c'est `OnRep_<nom>` qui arme et desarme reellement la fonctionnalite.
# Ce depot l'a paye trois fois -- l'horloge, l'aurore, et le ciel d'inspection.
dire("-" * 78)


def a_la_fonction(acteur, nom):
    """Rend (testable, existe). `testable` a faux = la mesure ne vaut rien."""
    for essai in (
        lambda: unreal.BlueprintService.list_functions(
            acteur.get_class().get_path_name().rstrip("_C").rstrip(".")),
    ):
        try:
            fns = essai()
        except Exception:
            continue
        for f in fns:
            try:
                if str(f.function_name) == nom:
                    return True, True
            except Exception:
                if nom in str(f):
                    return True, True
        return True, False
    return False, False


# LE TEST SE VALIDE SUR UN CAS CONNU AVANT DE SERVIR.
#
# « OnRep_Animate Time of Day » et « OnRep_Use Auroras » EXISTENT : notre C++ les
# appelle et le journal confirme « rappel OnRep appele ». Si la sonde ne les
# trouve pas non plus, ce n'est pas qu'il n'y a pas de rappel -- c'est que la
# sonde est cassee. Ce depot a deja compte zero composant d'herbe de Landscape
# sur son propre monde ET sur la demo du pack, qui en montre un tapis : un
# compteur qui rend zero ne mesure rien tant qu'on ne l'a pas vu rendre autre
# chose.
temoins = ["OnRep_Animate Time of Day", "OnRep_Use Auroras"]
valide = False
for t in temoins:
    for role, a in sorted(acteurs.items()):
        testable, existe = a_la_fonction(a, t)
        if testable and existe:
            valide = True
            dire("temoin : « %s » trouve sur %s -- la sonde de fonctions marche"
                 % (t, role))
            break
    if valide:
        break

if not valide:
    dire("!! TEMOIN EN ECHEC : ni « %s » ni « %s » ne sont trouves, alors que le "
         "C++ les appelle avec succes. La sonde de fonctions ne mesure RIEN, et "
         "l'absence de rappel ci-dessous ne prouve donc pas leur absence."
         % (temoins[0], temoins[1]))

dire("les rappels OnRep_ des noms attendus :")
trouves = 0
for nom in ATTENDUS:
    for role, a in sorted(acteurs.items()):
        testable, existe = a_la_fonction(a, "OnRep_%s" % nom)
        if testable and existe:
            trouves += 1
            dire("    OnRep_%s   [%s]" % (nom, role))
            break
dire("    -> %d rappel(s) sur %d noms%s"
     % (trouves, len(ATTENDUS),
        "" if valide else " -- MAIS LE TEMOIN A ECHOUE, chiffre sans valeur"))

# --- L'UNITE DU VENT, EN CLAIR --------------------------------------------
#
# « Wind Intensity » est un index 0..10, et UDW porte le facteur qui le convertit
# en noeuds. Sans lui, « vent 10 » reste un chiffre sans grandeur physique.
for role, a in sorted(acteurs.items()):
    ok, noeuds = lire(a, "Knots at Wind Intensity 10")
    if ok and noeuds:
        dire("-" * 78)
        dire("vent : l'index 10 vaut %.1f noeuds, soit %.1f m/s (%.0f km/h)"
             % (noeuds, noeuds * 0.514444, noeuds * 1.852))
        break

# --- LES TREIZE PREREGLAGES, POUR CONFIRMER LE RELEVE BINAIRE -------------
dire("=" * 78)
dire("les prereglages de meteo livres par le pack :")
dire("%-20s %s" % ("preset", "  ".join("%-9s" % c[:9] for c in CHAMPS_PRESET)))
dire("-" * 78)

registre = unreal.AssetRegistryHelpers.get_asset_registry()
actifs = registre.get_assets_by_path(unreal.Name(PRESETS.rstrip("/")), recursive=False)
noms = sorted({str(d.asset_name) for d in actifs})
if not noms:
    dire("!! aucun prereglage trouve sous %s" % PRESETS)

for n in noms:
    # LES PREREGLAGES SONT DES `PrimaryDataAsset`, PAS DES BLUEPRINTS -- releve
    # ici, et c'est ce qui a fait echouer le premier jet : il cherchait une
    # `generated_class()` qui n'existe pas sur un DataAsset. On lit donc l'objet
    # DIRECTEMENT, sans defaut de classe.
    chemin = "%s%s" % (PRESETS, n)
    try:
        actif = unreal.load_asset(chemin)
    except Exception as e:
        dire("%-20s !! illisible : %s" % (n[:20], str(e)[:50]))
        continue
    if actif is None:
        dire("%-20s !! introuvable" % n[:20])
        continue
    cases = []
    for c in CHAMPS_PRESET:
        ok, v = lire(actif, c)
        cases.append("%-9s" % (montrer(v)[:9] if ok else "-"))
    dire("%-20s %s" % (n[:20], "  ".join(cases)))

dire("=" * 78)
if manquants:
    # ON CHERCHE LEUR VARIANTE AVANT DE CONCLURE A L'ABSENCE.
    #
    # Ces noms viennent d'un parsing binaire des .uasset, qui lit la table de
    # NOMS du paquet : elle contient les variables, mais aussi les fonctions, les
    # broches de noeuds et les variables locales. Un nom qui s'y trouve n'est
    # donc pas forcement une variable, et c'est exactement la reserve posee au
    # plan -- « une valeur plausible lue comme une mesure ». On compare sur le
    # nom REDUIT (sans espaces ni ponctuation, en minuscules), comme le pont le
    # fait deja pour « AnimateTimeOfDay ».
    def reduit(s):
        return re.sub(r"[^a-z0-9]", "", s.lower())

    dire("%d nom(s) ABSENT(s) des VARIABLES. Recherche de leur variante :"
         % len(manquants))
    for nom in manquants:
        cible = reduit(nom)
        proches = []
        for role, liste in sorted(tous.items()):
            for n in liste:
                r = reduit(n)
                if r == cible or (len(cible) > 6 and (cible in r or r in cible)):
                    proches.append("%s [%s]" % (n, role))
        if proches:
            dire("    %-38s -> %s" % (nom[:38], " | ".join(sorted(set(proches))[:3])))
        else:
            dire("    %-38s -> aucune variante : ce n'est pas une variable "
                 "(fonction, broche, ou nom local)" % nom[:38])
else:
    dire("les %d noms attendus existent tous" % len(ATTENDUS))
dire("releve termine -- RIEN n'a ete ecrit")
