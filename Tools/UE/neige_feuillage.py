# -*- coding: utf-8 -*-
"""Greffe la neige et l'humidite du pack sur les materiaux de feuillage.

POURQUOI CE SCRIPT EXISTE
-------------------------
Depuis le 29 septembre 2026 la neige tient au SOL et les empreintes s'y
creusent -- et le feuillage reste vert par-dessus. C'est le defaut le plus
voyant du monde, et il l'est devenu a cause de ce qui a reussi : un sol blanc
sous des arbres verts se remarque plus qu'un monde uniformement vert.

Ultra Dynamic Sky livre pour cela deux fonctions de materiau, relevees et non
supposees : `Foliage_Weather_Effects` pour les feuilles et l'herbe,
`Surface_Weather_Effects` pour les objets solides.

LE PATRON EST CELUI DE NOTRE PROPRE SOL, ET IL EST PROUVE
---------------------------------------------------------
`M_WorldseedGround` porte `Dynamic_Landscape_Weather_Effects_V3`, qui prend et
rend des ATTRIBUTS DE MATERIAU comme les deux fonctions ci-dessus. Le releve de
son graphe montre exactement ceci :

    valeurs propriete par propriete  ->  MakeMaterialAttributes
                                     ->  la fonction de meteo
                                     ->  racine d'attributs (MP_MaterialAttributes)
    avec use_material_attributes = True

Ce n'est pas une deduction : c'est par ce cablage que la neige au sol fonctionne,
ce que le proprietaire a vu.

⚠ ET `use_material_attributes` NE DIT PAS QU'UN GRAPHE EST CABLE EN ATTRIBUTS.
Trois materiaux du pack portent ce drapeau a VRAI tout en branchant leurs
sorties propriete par propriete. J'ai d'abord cru y voir un raccourci -- « il
n'y a qu'un noeud a inserer sur un fil existant » -- et c'etait faux : le
travail est le meme partout, et ce qui le dimensionne est le NOMBRE DE
PROPRIETES a emballer, quatre a six, pas le drapeau.

ON GREFFE DANS LE MAITRE DU PACK, SUR PLACE -- ET C'EST UNE MESURE QUI L'A DECIDE
--------------------------------------------------------------------------------
Mon premier jet greffait sur des COPIES, dans `/Game/Worldseed/PCG/Materials/`,
et les aurait imposees aux maillages par `materiaux_forces`. C'etait faux, et
`vegetation.py` porte la lecon en clair : « UN PACK SE CONSOMME PAR SON INSTANCE,
JAMAIS PAR SON MAITRE ». `materiau_force` remplace TOUS les slots par le meme
materiau, donc imposer une copie du maitre ferait perdre les surcharges de
l'instance -- ce que le depot a paye a l'image, un carre plein couleur sable.

MESURE DU 30 SEPTEMBRE 2026, sur les 91 references de maillage du catalogue :

    slots atteignant nos trois maitres : 40
        par une INSTANCE : 40
        en DIRECT        :  0

QUARANTE SUR QUARANTE passent par une instance. Greffer dans le maitre est donc
la seule forme juste : chaque instance herite de la neige en GARDANT ses
parametres, et il n'y a AUCUNE table de correspondance a maintenir.

REJOUABLE, ET C'EST LA RAISON DE CE CHOIX AUTANT QUE LA MESURE. Les materiaux
changeront et d'autres viendront : ajouter une cible est une ligne dans `CIBLES`,
et un rejeu. Le patron est celui de `materiaux_ism.py` -- modifier du contenu NON
VERSIONNE par un script, parce qu'une retouche a la main serait perdue au
prochain clone sans que rien ne le signale.

CE QUE CETTE GREFFE NE FAIT PAS
-------------------------------
Elle ne touche a AUCUNE valeur : elle emballe ce que le graphe produisait deja,
le fait traverser la fonction, et rebranche. `OpacityMask` passe donc par
l'emballage comme le reste -- sans quoi la silhouette du feuillage
disparaitrait, piege que `rvt_graft.py` a paye en septembre.

PIEGES REPRIS DU PRECEDENT, PAYES COMPTANT
------------------------------------------
**UNE BROCHE UNIQUE SE DESIGNE PAR LA CHAINE VIDE**, alors que
`export_material_graph` l'annonce sous le nom « Input ». Passer le nom echoue en
SILENCE : le compteur de retour rend moins que demande et ne dit pas lesquelles.

**`delete_asset` sur un materiau rend faux sans lever**, donc `duplicate_asset`
rend l'asset deja present et la greffe se poserait PAR-DESSUS la precedente.
D'ou l'idempotence PAR CONSTAT : si la copie porte deja la fonction, on n'y
touche pas.

**ON COMPARE DES CHEMINS, JAMAIS DES `get_name()`** : deux materiaux de packs
differents portent le meme nom, et l'avoir oublie une fois a rendu un carre
plein couleur sable a l'image.

LA LIMITE DE L'INSTRUMENT, ET IL FAUT LA DIRE
---------------------------------------------
`export_material_graph` NE MONTRE PAS la racine d'attributs. La derniere
connexion -- fonction -> `MP_MaterialAttributes` -- ne peut donc PAS etre
verifiee par relecture du graphe. Le controle est la recompilation, puis
l'IMAGE. On ne conclura pas sur un appel qui rend vrai.

USAGE
-----
    UnrealEditor-Cmd.exe Worldseed.uproject -run=pythonscript
      -script="D:/UE/Worldseed/Tools/UE/neige_feuillage.py"
      -unattended -nopause -nosplash

Passer `--verifier` pour ne rien ecrire et se contenter du releve.
Passer `--cible=<suffixe>` pour n'en traiter qu'une -- c'est ainsi qu'on a
valide la premiere avant de generaliser.
"""

import json
import sys

import unreal

DEST = "/Game/Worldseed/PCG/Materials"

FN_FEUILLAGE = ("/Game/UltraDynamicSky/Materials/Weather/"
                "Foliage_Weather_Effects")
FN_SURFACE = ("/Game/UltraDynamicSky/Materials/Weather/"
              "Surface_Weather_Effects")

# LES CIBLES : celles que le releve du 30 septembre 2026 trouve INSTALLEES.
# Trois autres de `rvt_graft.py` -- M_Foliage_Master, M_Tree_Trunk_Master,
# M_Rock_Master -- viennent de `Stylized_PBR_Nature`, absent de cette machine.
#
# `fonction` suit la NATURE de ce qu'on habille, pas sa commodite : les feuilles
# et l'herbe prennent `Foliage_Weather_Effects`, qui sait masquer la neige sur
# le dessous d'une feuille et par la courbure ; l'ecorce et les props prennent
# `Surface_Weather_Effects`, qui gere en plus les gouttes et le ruissellement.
CIBLES = [
    {"maitre": "/Game/Orasot_Bundle/StylizedForestLandscape/Materials/M_Bark_Master",
     "suffixe": "EcorceSFL", "fonction": FN_SURFACE},
    {"maitre": "/Game/Orasot_Bundle/Stylized_Landscape_5_Bioms/Global/Materials/M_Master_Leaf",
     "suffixe": "FeuilleGlobal", "fonction": FN_FEUILLAGE},
    {"maitre": "/Game/Orasot_Bundle/StylizedForestLandscape/Materials/M_Leaf_Master",
     "suffixe": "FeuilleSFL", "fonction": FN_FEUILLAGE},
]

# LES BROCHES DE `MakeMaterialAttributes` PORTENT LE NOM DE LEUR PROPRIETE,
# relevé dans l'en-tete du moteur. On ne traite que celles qu'un graphe de
# feuillage alimente vraiment : y en ajouter d'inutiles ferait des connexions
# refusees, donc un compteur qui ne tombe pas juste et une erreur a diagnostiquer
# pour rien.
BROCHES = {
    "BaseColor": "BaseColor",
    "Metallic": "Metallic",
    "Specular": "Specular",
    "Roughness": "Roughness",
    "EmissiveColor": "EmissiveColor",
    "Opacity": "Opacity",
    "OpacityMask": "OpacityMask",
    "Normal": "Normal",
    "AmbientOcclusion": "AmbientOcclusion",
    "SubsurfaceColor": "SubsurfaceColor",
}

# `WorldPositionOffset` NE PASSE PAS PAR L'EMBALLAGE, et c'est deliberé. Il
# porte le vent du feuillage (`Foliage_Wind_Movement`), il n'a rien a voir avec
# la neige, et le faire traverser la fonction reviendrait a lui laisser une
# chance de le modifier. Il reste branche en direct sur sa propriete.
HORS_EMBALLAGE = {"WorldPositionOffset", "PixelDepthOffset", "Displacement"}

_log = []


def log(genre, message):
    ligne = "{}: {}".format(genre, message)
    _log.append(ligne)
    unreal.log("[NEIGE] " + ligne)


def _graphe(chemin):
    return json.loads(unreal.MaterialNodeService.export_material_graph(chemin))


def _reference_fonction(chemin):
    """La forme que `batch_set_properties` attend pour une MaterialFunction."""
    return "/Script/Engine.MaterialFunction'{0}.{1}'".format(
        chemin, chemin.rsplit("/", 1)[-1])


def etat(chemin, fonction):
    """La copie porte-t-elle deja la greffe ? Rend un dictionnaire lisible."""
    d = _graphe(chemin)
    appels = [e for e in d.get("expressions", [])
              if e.get("class") == "MaterialFunctionCall"
              and fonction in json.dumps(e.get("properties", {}))]
    emballages = [e for e in d.get("expressions", [])
                  if e.get("class") == "MakeMaterialAttributes"]

    # L'ARETE QUI COMPTE : un emballage alimente-t-il la fonction de meteo ?
    # C'est le seul maillon verifiable par relecture -- le dernier, vers la
    # racine d'attributs, est invisible a l'export.
    ids_appels = {a.get("id") for a in appels}
    ids_makes = {m.get("id") for m in emballages}
    alimentee = any(c.get("source_id") in ids_makes
                    and c.get("target_id") in ids_appels
                    for c in d.get("connections", []))

    mat = unreal.EditorAssetLibrary.load_asset(chemin)
    return {
        "fonction": len(appels),
        "emballage": len(emballages),
        "alimentee": alimentee,
        "attributs": bool(mat.get_editor_property("use_material_attributes")),
        "sorties": [o.get("property") for o in d.get("output_connections", [])],
    }


def _saine(e):
    """Une greffe complete, et l'invariant N'EST PAS un compte de noeuds.

    MA PREMIERE VERSION EXIGEAIT « un seul emballage », et c'etait faux : les
    materiaux du pack en portent DEJA un -- c'est ce qui explique leur
    `use_material_attributes = True`. Le controle juste est donc que la fonction
    de meteo soit ALIMENTEE par un emballage, arete que l'export montre, et que
    le drapeau soit arme. Le dernier maillon -- fonction vers la racine
    d'attributs -- reste invisible a l'export, et c'est dit dans l'en-tete.
    """
    return e["fonction"] == 1 and e["alimentee"] and e["attributs"]


def greffer_une(cible, verifier):
    fonction = cible["fonction"]
    # LA CIBLE EST LE MAITRE LUI-MEME : les 40 slots concernes passent tous par
    # une INSTANCE, donc greffer dans le maitre est la seule facon de ne pas
    # perdre leurs surcharges.
    dst = cible["maitre"]
    court = dst.rsplit("/", 1)[-1]

    if not unreal.EditorAssetLibrary.does_asset_exist(dst):
        log("ABSENT", "{} -- pack non installe, cible ignoree".format(dst))
        return None

    # IDEMPOTENCE PAR CONSTAT, et elle est INDISPENSABLE ici : on ecrit dans du
    # contenu de pack, donc un second passage qui ajouterait une seconde greffe
    # abimerait l'original sans filet. On relit avant d'ecrire.
    e = etat(dst, fonction)
    if _saine(e):
        log("DEJA", "{} porte la greffe (sorties : {})".format(
            court, ", ".join(e["sorties"]) or "aucune"))
        return dst

    if verifier:
        d = _graphe(dst)
        sorties = [o.get("property") for o in d.get("output_connections", [])]
        log("A FAIRE", "{} | {} expression(s), sorties : {} | etat {}".format(
            court, len(d.get("expressions", [])), ", ".join(sorties), e))
        return None

    # --- UNE SAUVEGARDE AVANT DE TOUCHER AU PACK ----------------------------
    #
    # On ecrit dans un materiau d'un pack PAYANT, non versionne. Une case a
    # cocher se decoche ; une greffe de graphe ne se defait pas, et sans filet
    # il faudrait REINSTALLER le pack. Le depot a deja ce patron --
    # `M_WorldseedLandscape_SauvegardeAvantDLWE`, gardee hors du depot comme
    # l'original.
    #
    # ELLE NE SE REFAIT JAMAIS : si une sauvegarde existe, c'est qu'un passage
    # precedent a deja modifie le maitre, et la REMPLACER figerait l'etat
    # GREFFE comme s'il etait l'original.
    secours = "{0}/{1}_AvantNeige".format(DEST, court)
    if not unreal.EditorAssetLibrary.does_asset_exist(secours):
        if unreal.EditorAssetLibrary.duplicate_asset(dst, secours) is None:
            log("ERROR", "sauvegarde impossible pour {} -- on NE GREFFE PAS "
                         "sans filet".format(court))
            return None
        print("CREATED: {}".format(secours))
        log("SECOURS", "{} sauvegarde avant greffe".format(court))
    else:
        log("SECOURS", "{} a deja une sauvegarde, elle est GARDEE telle "
                       "quelle".format(court))

    avant = _graphe(dst)
    sorties = {o.get("property"): o for o in avant.get("output_connections", [])}
    if not sorties:
        log("ERROR", "{} n'a AUCUNE sortie branchee : greffe impossible".format(dst))
        return None

    # CE QU'ON EMBALLE : les sorties reellement branchees, moins celles qui
    # n'ont rien a voir avec la neige. Emballer ce qui n'existe pas ferait des
    # connexions refusees et un compteur faux.
    a_emballer = [p for p in sorties
                  if p in BROCHES and p not in HORS_EMBALLAGE]
    if "BaseColor" not in a_emballer:
        log("ERROR", "{} n'a rien sur BaseColor : la neige n'aurait rien a "
                     "colorer".format(dst))
        return None

    # ON INSERE, ON N'AJOUTE PAS -- ET C'EST UNE CORRECTION DE MA PREMIERE
    # VERSION. Les materiaux du pack portent DEJA un `MakeMaterialAttributes`
    # qui alimente la racine d'attributs : c'est ce qui explique leur
    # `use_material_attributes = True`, et son « 0 sortie » a l'export vient de
    # ce que l'export ne montre PAS les liens vers la racine.
    #
    # En creer un second marchait -- la racine se reconnecte au dernier venu --
    # mais laissait le premier en orphelin, et surtout perdait les choix du
    # pack : son emballage peut alimenter des broches que la liste des sorties
    # par propriete ne mentionne pas. On reprend donc le SIEN.
    existants = [e for e in avant.get("expressions", [])
                 if e.get("class") == "MakeMaterialAttributes"]
    a_creer = ["MaterialFunctionCall"] if existants \
        else ["MakeMaterialAttributes", "MaterialFunctionCall"]

    faits = unreal.MaterialNodeService.batch_create_expressions(
        dst, a_creer, [-300] * len(a_creer), [1600] * len(a_creer))
    if len(faits) != len(a_creer):
        log("ERROR", "creation incomplete sur {} ({} sur {})".format(
            dst, len(faits), len(a_creer)))
        return None

    apres = _graphe(dst)
    connus = {e["id"] for e in avant["expressions"]}
    neufs = [e for e in apres["expressions"] if e["id"] not in connus]
    par_classe = {}
    for e in neufs:
        par_classe.setdefault(e["class"], []).append(e)
    try:
        appel = par_classe["MaterialFunctionCall"][0]
        emballage = existants[0] if existants \
            else par_classe["MakeMaterialAttributes"][0]
    except (KeyError, IndexError) as err:
        log("ERROR", "noeuds neufs inattendus sur {} : {}".format(dst, err))
        return None

    if existants:
        log("REPRIS", "{} : l'emballage du pack est reutilise, rien n'est "
                      "duplique".format(dst.rsplit("/", 1)[-1]))

    n = unreal.MaterialNodeService.batch_set_properties(
        dst, [appel["id"]], ["MaterialFunction"],
        [_reference_fonction(fonction)])
    if n != 1:
        log("ERROR", "fonction non posee sur {}".format(dst))
        return None

    src_ids, src_out, tgt_ids, tgt_in = [], [], [], []

    # ON NE RECABLE PAS UN EMBALLAGE QU'ON REUTILISE : ses entrees sont deja
    # branchees par le pack, et les reposer serait au mieux inutile, au pire un
    # ecrasement de ce qu'il avait choisi.
    if not existants:
        for prop in a_emballer:
            o = sorties[prop]
            src_ids.append(o["expression_id"])
            # UNE BROCHE DE SORTIE UNIQUE SE DESIGNE PAR LA CHAINE VIDE.
            # L'export nomme parfois la sortie ("Z", "BaseColor") et parfois
            # non ; on reprend ce qu'il dit, vide compris.
            src_out.append(o.get("output_name") or "")
            tgt_ids.append(emballage["id"])
            tgt_in.append(BROCHES[prop])

    # L'EMBALLAGE ENTRE DANS LA FONCTION. La broche s'appelle « Material
    # Attributes » -- avec l'espace -- et c'est le nom que l'entree de fonction
    # porte, releve sur les deux fonctions.
    src_ids.append(emballage["id"])
    src_out.append("")
    tgt_ids.append(appel["id"])
    tgt_in.append("Material Attributes")

    c = unreal.MaterialNodeService.batch_connect_expressions(
        dst, src_ids, src_out, tgt_ids, tgt_in)
    if c != len(src_ids):
        log("ERROR", "{} connexions sur {} attendues, dans {} -- broches : {}"
            .format(c, len(src_ids), dst, ", ".join(tgt_in)))
        return None

    # LE DRAPEAU AVANT LA RACINE : tant qu'il est faux, la racine d'attributs
    # n'existe pas comme cible de connexion.
    mat = unreal.EditorAssetLibrary.load_asset(dst)
    mat.set_editor_property("use_material_attributes", True)

    objets = list(unreal.MaterialEditingLibrary.get_material_expressions(mat))
    appel_obj = None
    for o in reversed(objets):
        if isinstance(o, unreal.MaterialExpressionMaterialFunctionCall):
            appel_obj = o
            break
    if appel_obj is None:
        log("ERROR", "appel de fonction introuvable en objet sur {}".format(dst))
        return None

    # LA SORTIE : `Foliage_Weather_Effects` n'en a QU'UNE, donc chaine vide ;
    # `Surface_Weather_Effects` en a trois, donc il faut la NOMMER. Se tromper
    # ici echouerait en silence -- troisieme forme du meme piege.
    nom_sortie = "" if fonction == FN_FEUILLAGE else "Material Attributes"
    if not unreal.MaterialEditingLibrary.connect_material_property(
            appel_obj, nom_sortie,
            unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES):
        log("ERROR", "racine d'attributs non branchee sur {}".format(dst))
        return None

    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(dst)
    print("MODIFIED: {}".format(dst))

    e = etat(dst, fonction)
    if not _saine(e):
        log("ERROR", "{} : greffe posee mais RELUE INCOMPLETE {}".format(dst, e))
        return None

    log("GREFFE", "{} : {} emballe ({}), fonction {}, attributs {}".format(
        dst.rsplit("/", 1)[-1], len(a_emballer), ", ".join(a_emballer),
        fonction.rsplit("/", 1)[-1], e["attributs"]))
    return dst


def greffer():
    verifier = "--verifier" in sys.argv
    filtre = None
    for a in sys.argv:
        if a.startswith("--cible="):
            filtre = a.split("=", 1)[1]

    if verifier:
        log("MODE", "RELEVE SEUL, rien ne sera ecrit")
    if filtre:
        log("MODE", "une seule cible : {}".format(filtre))

    faits = []
    for cible in CIBLES:
        if filtre and cible["suffixe"] != filtre:
            continue
        r = greffer_une(cible, verifier)
        if r:
            faits.append(r)

    log("BILAN", "{} materiau(x) greffe(s)".format(len(faits)))
    log("SUITE", "LA PREUVE N'EST PAS ICI : l'export de graphe ne montre pas la "
                 "racine d'attributs. Il faut REGARDER, sous la neige.")
    return faits


greffer()
