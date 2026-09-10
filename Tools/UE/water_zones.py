"""Finesse de la surface d'eau : diagnostic et reglage de la WaterZone.

A executer DEPUIS l'editeur :

    import sys, importlib
    sys.path.insert(0, r"D:\\UE\\Worldseed\\Tools\\UE")
    import water_zones; importlib.reload(water_zones)
    print(water_zones.diagnostic())
    water_zones.regler(4096)

LE PROBLEME. Toute l'eau d'un niveau passe par la `WaterZone` qui la couvre :
celle-ci encode la hauteur de la surface dans une texture unique, et le maillage
d'eau lit cette texture. Worldseed n'a qu'UNE zone, de 34 km de cote, pour de
l'eau allant de 0 m (ocean) a 545 m (Worldseed_Lac_03). A 1024 texels, cela fait
**33 m par texel**, et deux defauts constates en jeu :

  - au bord d'un lac perche, la surface doit passer du niveau de l'ocean a celui
    du lac en un ou deux texels : RIDEAU D'EAU VERTICAL ;
  - la ou le relief monte vite, le texel voisin impose sa hauteur d'eau au-dessus
    de la roche : PLAQUES D'EAU sur le sommet des falaises.

Le moteur signale de son cote que la tessellation deborde : "Width of water quad
tree tiles (1024) has exceeded the cap for this platform (256). Tile sizes have
been biased by a factor of 0.25", ce qui porte la tuile de 24 m demandee a 96 m.
D'ou `r.Water.WaterMesh.MaxWidthInTiles` dans Config/DefaultEngine.ini.

CE QUI NE MARCHE PAS, ET QUI SEMBLAIT EVIDENT. Donner a chaque lac sa propre
zone, petite et fine, via `WaterBodyComponent.set_water_zone_override()`. Essaye
et abandonne :
  - tant que l'override est pose, le lac CESSE D'ETRE RENDU (verifie a l'image
    sur Worldseed_Lac_02, zone de 4,9 km a 2048 texels, soit 2,4 m/texel) ;
  - et l'override ne se serialise pas : apres rechargement du niveau il revient
    a `None`, meme apres `save_dirty_packages` reussi.
Les cinq zones creees ont ete supprimees. Ne pas refaire ce chemin sans avoir
d'abord compris pourquoi le rendu s'arrete.

CE QUI MARCHE. Monter la resolution de la zone unique : 1024 -> 4096 fait passer
de 33,2 a 8,3 m par texel, soit quatre fois mieux, sans nouvel acteur et sans
override. Ce n'est pas une correction complete - 8,3 m reste grossier pour une
marche de 192 m - mais c'est mesurable et ca tient au rechargement.
"""

from __future__ import annotations

import unreal


def _zones() -> list:
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    return [a for a in sub.get_all_level_actors() if isinstance(a, unreal.WaterZone)]


def charger_lacs() -> list:
    """Les lacs sont des acteurs World Partition : sans ce chargement explicite,
    `get_all_level_actors` ne les voit pas."""
    descs = unreal.WorldPartitionBlueprintLibrary.get_actor_descs() or []
    guids = [d.get_editor_property("guid") for d in descs
             if "WaterBodyLake" in str(d.get_editor_property("native_class"))]
    if guids:
        unreal.WorldPartitionBlueprintLibrary.load_actors(guids)
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    return [a for a in sub.get_all_level_actors() if isinstance(a, unreal.WaterBodyLake)]


def emprise(lac) -> tuple[float, float, float, float]:
    """(centre_x, centre_y, largeur, hauteur) de la spline, en cm monde."""
    sc = lac.get_water_spline()
    xs, ys = [], []
    for i in range(sc.get_number_of_spline_points()):
        p = sc.get_location_at_spline_point(i, unreal.SplineCoordinateSpace.WORLD)
        xs.append(p.x)
        ys.append(p.y)
    return ((min(xs) + max(xs)) / 2.0, (min(ys) + max(ys)) / 2.0,
            max(xs) - min(xs), max(ys) - min(ys))


def diagnostic() -> dict:
    """Finesse actuelle de chaque zone, et amplitude d'altitude a couvrir."""
    out = {"zones": [], "altitudes_eau_cm": {}}
    for z in _zones():
        r = z.get_editor_property("render_target_resolution")
        e = z.get_editor_property("zone_extent")
        out["zones"].append({
            "nom": z.get_actor_label(),
            "cote_km": round(e.x / 100000.0, 2),
            "texels": int(r.x),
            "m_par_texel": round(e.x / max(r.x, 1) / 100.0, 1),
        })
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in sub.get_all_level_actors():
        if isinstance(a, unreal.WaterBodyOcean):
            out["altitudes_eau_cm"]["ocean"] = round(a.get_actor_location().z)
    for l in charger_lacs():
        out["altitudes_eau_cm"][l.get_actor_label()] = round(l.get_actor_location().z)
    return out


def regler(resolution: int = 4096) -> dict:
    """Pose la resolution de la texture d'info d'eau sur toutes les zones.

    4096 est le maximum utile ici : au-dela le cout memoire de la texture croit
    en carre pour un gain que le plafond de tuiles du moteur annule de toute
    facon. Recharger le niveau ensuite pour que la texture soit refabriquee.
    """
    faits = []
    for z in _zones():
        avant = int(z.get_editor_property("render_target_resolution").x)
        z.set_editor_property("render_target_resolution",
                              unreal.IntPoint(int(resolution), int(resolution)))
        e = z.get_editor_property("zone_extent")
        faits.append({"nom": z.get_actor_label(), "avant": avant, "apres": int(resolution),
                      "m_par_texel": round(e.x / resolution / 100.0, 1)})
        unreal.log("MODIFIED: {} resolution {} -> {}".format(
            z.get_actor_label(), avant, resolution))
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    return {"zones": faits}
