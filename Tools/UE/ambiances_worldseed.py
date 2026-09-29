# Worldseed - fabriquer les trois dosages de SON D'AMBIANCE.
#
# POURQUOI TROIS ASSETS QUI NE DIFFERENT QUE PAR UN VOLUME, et ce n'est pas un
# choix de paresse. Ultra Dynamic Sky ne livre qu'UNE source d'ambiance,
# `Forest_Example` -- oiseaux le jour, insectes la nuit, vent dans les arbres
# selon le vent, le tout deja module par l'heure et la meteo PAR LE PACK. Ses
# deux sous-sources existent bien comme assets separes, mais le releve du
# 29 septembre 2026 les donne en MONO (`Forest_Birds`) et en QUAD
# (`Forest_TreeWind`), quand la documentation du pack exige une sortie 5.1 pour
# qu'une ambiance recoive l'occlusion et le panoramique directionnel : aucune
# des deux n'est jouable seule.
#
# COMPOSER UNE AMBIANCE NOUVELLE DEMANDERAIT D'ECRIRE UN GRAPHE METASOUND, qui
# n'est pas scriptable -- meme mur que Niagara, deja paye le 29 septembre sur
# les nappes rasantes. Ce qui reste scriptable est le DATA ASSET, qui ne porte
# que trois champs : une source, une echelle de volume, des surcharges de
# parametre. D'ou trois dosages, et non une ambiance par biome.
#
#   UnrealEditor-Cmd.exe Worldseed.uproject -run=pythonscript
#     -script="D:/UE/Worldseed/Tools/UE/ambiances_worldseed.py"
#     -unattended -nopause -nosplash
#
# L'EDITEUR DOIT ETRE ARRETE : le plugin ecoute sur le port 8000.
#
# IDEMPOTENT PAR CONSTAT, JAMAIS PAR DESTRUCTION. Un asset deja present n'est
# PAS reecrit : son volume a peut-etre ete regle a l'oreille, et ce depot a une
# entree entiere sur les greffes reposees par-dessus les precedentes. Pour
# changer un dosage, on edite l'asset -- ou on le supprime et l'on rejoue.
import unreal

CLASSE = ("/Game/UltraDynamicSky/Blueprints/System/"
          "UDS_Environment_Sound.UDS_Environment_Sound_C")
SOURCE = "/Game/UltraDynamicSky/Sound/Environment/Forest_Example/Forest_Example"
DOSSIER = "/Game/Worldseed/Sound"

# LES TROIS DOSAGES. Ils se jugent a l'oreille et n'ont aucune source derriere
# eux : ce sont des ARBITRAIRES assumes, comme les poids de la brume.
#
# LA LISIERE EST BASSE A DESSEIN : sur une savane ou une steppe, ce qu'on doit
# entendre est un oiseau LOINTAIN et rare, pas un sous-bois. A plein volume la
# meme bande sonore ferait pousser une foret autour du joueur.
DOSAGES = [
    ("EA_Worldseed_ForetDense", 1.00,
     "les forets humides -- la source du pack y est chez elle"),
    ("EA_Worldseed_ForetClaire", 0.55,
     "taiga, foret tropicale seche, marais, mediterraneen"),
    ("EA_Worldseed_Lisiere", 0.28,
     "savane, prairie, steppe -- des oiseaux, de loin, et peu"),
]


def dire(t):
    unreal.log("[AMBIANCE] %s" % t)


# --- LES DEUX DEPENDANCES, VERIFIEES AVANT DE CREER QUOI QUE CE SOIT --------
#
# Sans le pack il n'y a ni classe ni source : on ne cree alors RIEN plutot que
# trois assets vides qui se chargeraient sans erreur et ne joueraient rien --
# exactement le genre d'echec muet que ce depot passe son temps a deterrer.
classe = unreal.load_object(None, CLASSE)
if classe is None:
    dire("!! classe %s introuvable -- Ultra Dynamic Sky absent, rien fait" % CLASSE)
    raise SystemExit

source = unreal.load_asset(SOURCE)
if source is None:
    dire("!! source %s introuvable -- rien fait" % SOURCE)
    raise SystemExit

dire("classe : %s" % classe.get_name())
dire("source : %s (%s)" % (source.get_name(), source.get_class().get_name()))

# LE FORMAT DE SORTIE EST LA CONDITION, et la documentation du pack est
# explicite : une ambiance doit sortir en 5.1 pour recevoir l'occlusion et le
# panoramique directionnel. On le RELIT plutot que de le supposer -- c'est ce
# controle qui a elimine `Forest_Birds` (mono) et `Forest_TreeWind` (quad).
try:
    fmt = source.get_editor_property("output_format")
    dire("format de sortie : %s" % fmt)
    # ⚠ LE NOM DE L'ENUMERATEUR EST `FIVE_DOT_ONE`, pas « 5.1 » ni « SURROUND ».
    # La premiere version testait ces deux chaines-la et criait « pas en 5.1 »
    # sur une source qui l'etait : un avertissement qui ment coute plus cher que
    # pas d'avertissement, et ce depot a une entree entiere sur cette classe de
    # defaut -- du bruit qu'on lit comme une information.
    if fmt != unreal.MetaSoundOutputAudioFormat.FIVE_DOT_ONE:
        dire("!! ATTENTION : la source n'est PAS en 5.1 -- l'ambiance jouera sans "
             "occlusion ni direction. Ce n'est pas bloquant, mais il faut le savoir.")
    else:
        dire("        5.1 : l'ambiance recevra l'occlusion et la direction")
except Exception as e:
    dire("format de sortie illisible (%s)" % str(e)[:60])

outils = unreal.AssetToolsHelpers.get_asset_tools()
fabrique = unreal.DataAssetFactory()
fabrique.set_editor_property("data_asset_class", classe)

crees, gardes = [], []
for nom, volume, role in DOSAGES:
    chemin = "%s/%s" % (DOSSIER, nom)

    if unreal.EditorAssetLibrary.does_asset_exist(chemin):
        # IDEMPOTENT PAR CONSTAT : on lit, on dit, on ne touche pas.
        deja = unreal.load_asset(chemin)
        try:
            v = deja.get_editor_property("Volume")
            s = deja.get_editor_property("Metasound Source")
        except Exception:
            v, s = None, None
        dire("GARDE   %-26s volume %s, source %s"
             % (nom, v, s.get_name() if s else "None"))
        if s is None:
            # UNE SOURCE NULLE EST UN ASSET CASSE, pas un reglage : il se
            # chargerait sans erreur et ne jouerait rien. On le repare.
            deja.set_editor_property("Metasound Source", source)
            unreal.EditorAssetLibrary.save_asset(chemin)
            dire("        source NULLE -- reparee")
        gardes.append(nom)
        continue

    actif = outils.create_asset(nom, DOSSIER, None, fabrique)
    if actif is None:
        dire("!! ECHEC de creation pour %s" % nom)
        continue

    actif.set_editor_property("Metasound Source", source)
    actif.set_editor_property("Volume", float(volume))

    # ON RELIT CE QU'ON VIENT D'ECRIRE. `set_editor_property` sur un nom absent
    # ne rend rien et ne fait rien : ce depot a paye trois ecritures de
    # parametre devenues des no-op silencieuses, trouvees des mois plus tard.
    relu_v = actif.get_editor_property("Volume")
    relu_s = actif.get_editor_property("Metasound Source")
    if relu_s is not source or abs(float(relu_v) - float(volume)) > 1e-4:
        dire("!! %s : relecture DIFFERENTE (volume %s, source %s) -- non sauve"
             % (nom, relu_v, relu_s.get_name() if relu_s else "None"))
        continue

    unreal.EditorAssetLibrary.save_asset(chemin)
    crees.append(nom)
    dire("CREE    %-26s volume %.2f  (%s)" % (nom, volume, role))

dire("-" * 70)
dire("%d cree(s), %d garde(s) tel(s) quel(s)" % (len(crees), len(gardes)))
if crees:
    dire("Ces assets vivent sous Content/Worldseed/, donc ils sont VERSIONNES. "
         "Ils referencent en revanche un pack qui ne l'est pas -- comme "
         "M_WorldseedGround et CAL_Worldseed avant eux.")
