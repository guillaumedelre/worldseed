"""Cree l'eau du monde Worldseed a partir des sorties du generateur.

A executer DEPUIS l'editeur :

    import sys, importlib
    sys.path.insert(0, r"D:\\UE\\Worldseed\\Tools\\UE")
    import water_world; importlib.reload(water_world)
    print(water_world.build(r"D:\\UE\\Worldseed\\Saved\\WorldGen\\20260909"))

POURQUOI CE FICHIER EXISTE. L'eau du monde de 32 km avait ete posee par un script
ad hoc, jamais conserve : au moment de refaire le monde a 8 km il a fallu tout
retrouver. Ce fichier-ci est la reference, et il est idempotent (il saute ce qui
existe deja) pour pouvoir etre relance apres un echec en cours de route.

CE QUI SE RATE FACILEMENT, ET QUI A COUTE CHER A TROUVER :

1. **`affects_landscape` DOIT etre a False sur chaque corps d'eau.** Sinon le
   plugin Water creuse le relief IMPORTE pour y loger le lit de la riviere, et
   le terrain du generateur est detruit.

2. **`RiverWidth` est la largeur TOTALE en centimetres, pas la demi-largeur**,
   contrairement a ce qu'affirme le commentaire de `WaterSplineMetadata.h`
   ("the width of the river (from center) in each direction"). La preuve est
   dans le maillage : `WaterBodyRiverComponent.cpp:111` fait
   `const float HalfWidth = WaterSplineMetadata->RiverWidth.Eval(Key) / 2;`.
   Suivre le commentaire donnerait des rivieres deux fois trop larges.

3. **La metadonnee de spline ne repond qu'a ses noms C++ exacts.**
   `UWaterSplineMetadata` n'a pas de wrapper Python genere : son `dir()` est
   vide et `get_editor_property("river_width")` echoue, alors que
   `get_editor_property("RiverWidth")` marche. On passe malgre tout par les
   setters du composant (`set_river_width_at_spline_input_key`), qui sont
   propres et exposes ; le nom exact n'est note ici que pour le diagnostic.

4. **La metadonnee est un `UPROPERTY(Instanced)` prive** : elle n'est pas
   atteignable par `get_editor_property` sur l'acteur. On y accede en chargeant
   le sous-objet : `unreal.load_object(None, acteur.get_path_name() +
   ".WaterSplineMetadata")`.

5. **Une seule `WaterZone` pour tout le niveau.** Elle encode la hauteur de la
   surface dans UNE texture : sa finesse limite la marche que l'eau peut
   franchir entre deux texels. Voir `water_zones.py` pour le detail, et ne pas
   retenter les zones par lac (essaye, abandonne : le lac cesse d'etre rendu et
   l'override ne se serialise pas).
"""

from __future__ import annotations

import json
import os

import unreal

DEFAULT_MAP = "/Game/Worldseed/Maps/L_Worldseed"

OCEAN_LABEL = "Worldseed_Ocean"
ZONE_LABEL = "Worldseed_WaterZone"
LAKE_PREFIX = "Worldseed_Lac_"
RIVER_PREFIX = "Worldseed_Riviere_"

# Finesse de la texture d'information de la WaterZone. 4096 est la valeur reglee
# et verifiee ; voir water_zones.py.
ZONE_RESOLUTION = 4096

# Marge de la zone d'eau autour du monde, en fraction de la largeur du monde.
# L'ocean doit deborder du Landscape, sinon on voit le bord du monde.
ZONE_MARGE = 0.0625          # 6,25 % -> 8 km de monde donnent une zone de 8,5 km

_log: list[str] = []


def log(kind: str, message: str) -> None:
    line = "{}: {}".format(kind, message)
    _log.append(line)
    unreal.log("WORLDSEED_EAU " + line)
    print(line)


def _acteurs() -> "unreal.EditorActorSubsystem":
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _par_label(label: str):
    for a in _acteurs().get_all_level_actors():
        if a.get_actor_label() == label:
            return a
    return None


def _charge_json(out_dir: str, nom: str) -> dict:
    chemin = os.path.join(out_dir, nom)
    with open(chemin, "r", encoding="utf-8") as fh:
        return json.load(fh)


def _sans_relief(acteur) -> None:
    """Le corps d'eau ne doit JAMAIS retoucher le relief importe."""
    comp = acteur.get_editor_property("water_body_component")
    comp.set_editor_property("affects_landscape", False)


# ------------------------------------------------------------------ zone d'eau


def zone(manifest: dict, resolution: int = ZONE_RESOLUTION):
    """Regle la zone unique qui couvre tout le monde, un peu plus large que lui.

    ON NE LA CREE PAS : `spawn_actor_from_class(unreal.WaterZone, ...)` renvoie
    None. C'est le plugin Water qui pose la zone LUI-MEME des qu'un premier corps
    d'eau apparait dans un niveau qui n'en a pas, dimensionnee sur les bornes du
    monde (mesure ici : 8 km et 1024 texels pour un monde de 8 km). Le travail
    consiste donc a l'adopter : la renommer, l'elargir un peu pour que l'ocean
    deborde du Landscape, et monter sa finesse.

    `zone_extent` est la largeur TOTALE, pas la demi-portee : le moteur a inscrit
    800 000 cm pour un monde de 8 km.
    """
    zones = [a for a in _acteurs().get_all_level_actors() if isinstance(a, unreal.WaterZone)]
    if not zones:
        log("ERROR", "aucune WaterZone : poser d'abord un corps d'eau (ocean)")
        return None
    a = zones[0]

    cote_cm = float(manifest["world"]["sizeKm"]) * 1000.0 * 100.0 * (1.0 + ZONE_MARGE)
    a.set_actor_label(ZONE_LABEL)
    a.set_editor_property("zone_extent", unreal.Vector2D(cote_cm, cote_cm))
    a.set_editor_property("render_target_resolution",
                          unreal.IntPoint(resolution, resolution))
    log("MODIFIED", "{} : {:.2f} km de cote, {} texels, soit {:.2f} m par texel".format(
        ZONE_LABEL, cote_cm / 100000.0, resolution, cote_cm / 100.0 / resolution))
    return a


# ----------------------------------------------------------------------- ocean


def ocean(manifest: dict):
    """L'ocean, a l'altitude zero. Il remplit la WaterZone tout seul."""
    existant = _par_label(OCEAN_LABEL)
    if existant is not None:
        log("SKIPPED", "{} deja present".format(OCEAN_LABEL))
        return existant

    a = _acteurs().spawn_actor_from_class(unreal.WaterBodyOcean,
                                          unreal.Vector(0.0, 0.0, 0.0))
    a.set_actor_label(OCEAN_LABEL)
    _sans_relief(a)
    log("CREATED", "{} a Z = 0".format(OCEAN_LABEL))
    return a


# ------------------------------------------------------------------------ lacs


def lakes(out_dir: str) -> list:
    """Un WaterBodyLake par lac, sa spline suivant le contour de la cuvette."""
    donnees = _charge_json(out_dir, "lakes.json")["lakes"]
    faits = []
    for i, lac in enumerate(donnees):
        label = "{}{:02d}".format(LAKE_PREFIX, i)
        if _par_label(label) is not None:
            log("SKIPPED", "{} deja present".format(label))
            continue
        contour = lac.get("outline") or []
        if len(contour) < 3:
            log("ERROR", "{} : contour de {} points, ignore".format(label, len(contour)))
            continue

        z_cm = float(lac["surfaceM"]) * 100.0
        centre = unreal.Vector(float(lac["centre"]["x"]), float(lac["centre"]["y"]), z_cm)
        a = _acteurs().spawn_actor_from_class(unreal.WaterBodyLake, centre)
        a.set_actor_label(label)

        sp = a.get_editor_property("spline_comp")
        pts = [unreal.Vector(float(p["x"]), float(p["y"]), z_cm) for p in contour]
        sp.set_spline_points(pts, unreal.SplineCoordinateSpace.WORLD, False)
        # Un lac est une boucle fermee, et ses berges sont des segments droits :
        # une spline lissee deborderait sur la terre entre deux points du contour.
        sp.set_closed_loop(True, False)
        for k in range(sp.get_number_of_spline_points()):
            sp.set_spline_point_type(k, unreal.SplinePointType.LINEAR, False)
        sp.update_spline()

        _sans_relief(a)
        faits.append(label)
        log("CREATED", "{} : surface {:.1f} m, {:.1f} ha, contour de {} points".format(
            label, lac["surfaceM"], lac["areaHa"], len(contour)))
    return faits


# -------------------------------------------------------------------- rivieres


def rivers(out_dir: str, max_actors: int | None = None) -> list:
    """Un WaterBodyRiver par cours d'eau, largeur et profondeur par NOEUD.

    La largeur ecrite est `widthCm` tel quel : le moteur divise lui-meme par 2
    pour obtenir la demi-largeur (WaterBodyRiverComponent.cpp:111).
    """
    donnees = _charge_json(out_dir, "rivers.json")["rivers"]
    if max_actors is not None:
        donnees = donnees[:max_actors]
    faits = []
    for i, riv in enumerate(donnees):
        label = "{}{:02d}".format(RIVER_PREFIX, i)
        if _par_label(label) is not None:
            log("SKIPPED", "{} deja present".format(label))
            continue
        pts = riv.get("points") or []
        if len(pts) < 2:
            log("ERROR", "{} : {} points, ignore".format(label, len(pts)))
            continue

        depart = unreal.Vector(float(pts[0]["x"]), float(pts[0]["y"]), float(pts[0]["z"]))
        a = _acteurs().spawn_actor_from_class(unreal.WaterBodyRiver, depart)
        a.set_actor_label(label)

        sp = a.get_editor_property("spline_comp")
        sp.set_spline_points(
            [unreal.Vector(float(p["x"]), float(p["y"]), float(p["z"])) for p in pts],
            unreal.SplineCoordinateSpace.WORLD, True)

        comp = a.get_editor_property("water_body_component")
        for k, p in enumerate(pts):
            comp.set_river_width_at_spline_input_key(float(k), float(p["widthCm"]))
            comp.set_river_depth_at_spline_input_key(float(k), float(p["depthCm"]))
        sp.k2_synchronize_and_broadcast_data_change()

        _sans_relief(a)
        faits.append(label)
        log("CREATED", "{} : {} noeuds, {:.0f} m de long, largeur max {:.1f} m".format(
            label, len(pts), riv["lengthM"], riv["maxWidthM"]))
    return faits


# ----------------------------------------------------------------------- chaine


def build(out_dir: str, resolution: int = ZONE_RESOLUTION) -> dict:
    """Chaine complete : zone -> ocean -> lacs -> rivieres."""
    _log.clear()
    manifest = _charge_json(out_dir, "manifest.json")
    # L'ocean D'ABORD : c'est lui qui fait apparaitre la WaterZone.
    ocean(manifest)
    zone(manifest, resolution)
    lacs = lakes(out_dir)
    rivs = rivers(out_dir)
    return {"lacs": len(lacs), "rivieres": len(rivs), "log": list(_log)}


def verify() -> dict:
    """Releve ce qui est reellement dans le niveau, et le controle qui compte."""
    acteurs = _acteurs().get_all_level_actors()
    corps = [a for a in acteurs if isinstance(a, unreal.WaterBody)]
    fautifs = [a.get_actor_label() for a in corps
               if a.get_editor_property("water_body_component").get_editor_property(
                   "affects_landscape")]
    zones = [a for a in acteurs if isinstance(a, unreal.WaterZone)]
    res = {
        "ocean": len([a for a in corps if isinstance(a, unreal.WaterBodyOcean)]),
        "lacs": len([a for a in corps if isinstance(a, unreal.WaterBodyLake)]),
        "rivieres": len([a for a in corps if isinstance(a, unreal.WaterBodyRiver)]),
        "zones": len(zones),
        "affectent_le_relief": fautifs,
    }
    if zones:
        z = zones[0]
        ext = z.get_editor_property("zone_extent")
        r = z.get_editor_property("render_target_resolution")
        res["zoneKm"] = round(float(ext.x) / 100000.0, 3)
        res["zoneTexels"] = int(r.x)
        res["metresParTexel"] = round(float(ext.x) / 100.0 / max(int(r.x), 1), 3)
    return res
