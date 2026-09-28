# Worldseed - assigner notre calendrier a l'acteur Ultra Dynamic Sky de la carte.
#
# POURQUOI CE SCRIPT. La carte portait le calendrier GREGORIEN d'UDS -- trois
# cent soixante-cinq jours -- soit, a quarante-cinq minutes reelles par journee,
# une annee de deux cent soixante-quatorze HEURES. La saison ne changeait donc
# jamais, horloge armee ou non, et tout le calage saisonnier des prereglages
# restait decoratif. `CAL_Worldseed` ramene l'annee a trente-six jours, soit
# vingt-sept heures et une saison toutes les six heures quarante-cinq.
#
#   UnrealEditor-Cmd.exe Worldseed.uproject -run=pythonscript
#     -script="Tools/UE/calendrier_worldseed.py" -unattended -nopause -nosplash
#
# IDEMPOTENT PAR CONSTAT : s'il trouve deja notre calendrier, il n'ecrit rien.
import unreal

CARTE = "/Game/Worldseed/Maps/L_Worldseed_Proc"
NOTRE = "/Game/Worldseed/Climate/CAL_Worldseed"

def dire(t):
    unreal.log("[CALENDRIER] %s" % t)

cal = unreal.load_asset(NOTRE)
if cal is None:
    dire("!! %s introuvable -- rien fait" % NOTRE)
    raise SystemExit

# LE CONTROLE QUI VALIDE NOTRE CALENDRIER EST UN TEMOIN, pas une lecture seule :
# ses champs derives (`Number of Days in Year`, `Month Lengths`) sont VIDES dans
# l'asset, et le calendrier gregorien livre par UDS -- qui fonctionne -- est
# dans exactement le meme etat. Ils sont donc calcules au demarrage, et un vide
# ici ne prouve rien. Ce qui compte est la table `Months`.
try:
    mois = cal.get_editor_property("Months")
    total = sum(int(v) for v in mois.values()) if mois else 0
    dire("%s : %d mois, %d jours par an" % (cal.get_name(), len(mois) if mois else 0, total))
    if total <= 0 or total > 100:
        dire("!! table des mois suspecte (%d jours) -- rien fait" % total)
        raise SystemExit
except Exception as e:
    dire("!! table des mois illisible : %s" % str(e)[:120])
    raise SystemExit

sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not sub.load_level(CARTE):
    dire("!! impossible de charger %s" % CARTE)
    raise SystemExit

paquets = []
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if "Ultra_Dynamic_Sky" not in a.get_class().get_name():
        continue

    avant = a.get_editor_property("Calendar")
    dire("acteur %s -- calendrier actuel : %s"
         % (a.get_actor_label(), avant.get_name() if avant else "None"))

    change = False
    if avant != cal:
        a.set_editor_property("Calendar", cal)
        change = True

    # ET L'HORLOGE DANS LA CARTE. Le C++ l'arme deja au demarrage, mais il doit
    # pour cela reveiller le rappel de replication a la main : posee ici, elle
    # est vraie des le BeginPlay d'UDS, qui construit alors son cache de vitesse
    # sur les bonnes valeurs. Ceinture et bretelles, et c'est la source.
    if not a.get_editor_property("Animate Time of Day"):
        a.set_editor_property("Animate Time of Day", True)
        change = True
        dire("  « Animate Time of Day » arme dans la carte")

    apres = a.get_editor_property("Calendar")
    dire("  relu : %s" % (apres.get_name() if apres else "None"))
    if change:
        paquets.append(a.get_outermost())

if not paquets:
    dire("rien a changer -- deja en place")
else:
    # LES ACTEURS WORLD PARTITION VIVENT DANS LEUR PROPRE PAQUET, sous
    # __ExternalActors__ : `save_current_level` ne les ecrit pas, et
    # `save_dirty_packages` ne suffit pas toujours. On sauve le paquet de
    # l'acteur, ce qui rend vrai et tient au rechargement.
    ok = unreal.EditorLoadingAndSavingUtils.save_packages(paquets, False)
    dire("sauvegarde de %d paquet(s) : %s" % (len(paquets), ok))
