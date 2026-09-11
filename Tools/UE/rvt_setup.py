"""Pose la Runtime Virtual Texture du terrain, comme le pack Orasot l'attend.

A executer DEPUIS l'editeur :

    import sys, importlib
    sys.path.insert(0, r"D:\\UE\\Worldseed\\Tools\\UE")
    import rvt_setup; importlib.reload(rvt_setup)
    print(rvt_setup.poser())
    print(rvt_setup.verifier())

A QUOI SERT LA RVT ICI. Le Landscape ECRIT sa couleur et sa hauteur dans deux
textures virtuelles ; le feuillage les RELIT et s'y accorde. C'est ce qui donne
aux rendus du pack leur coherence : un buisson pousse sur du sable prend le ton
du sable, le meme buisson sur l'herbe prend celui de l'herbe, et le pied de
chaque plante se fond dans le sol au lieu de s'y planter net. La fiche Fab du
bundle le dit d'ailleurs en toutes lettres : "When using the pack please enable
Virtual Textures in your project settings."

CE QUI AVAIT FAIT ECHOUER LA PREMIERE TENTATIVE, et qui est le coeur de ce
fichier : le reglage `virtual_texture_render_pass_type`. Laisse a son defaut, le
Landscape ne se dessine plus dans la passe principale mais DEPUIS la RVT -- le
terrain proche perd sa geometrie fine et parait aplati. Il faut `ALWAYS` : le
terrain se dessine normalement ET alimente la RVT. La session precedente avait
conclu a un echec de la RVT elle-meme et fait marche arriere ; c'etait ce
reglage-la.

LE MONTAGE COMPLET, releve sur la carte de demo du pack
(`/Game/Orasot_Bundle/Maps/M_5_Bioms_Showcase`) :

  1. DEUX `RuntimeVirtualTextureVolume`, un par texture :
       RVT_Landscape_Material  (BaseColor + Normal + Specular, YCoCg)
       RVT_Landscape_Height    (WorldHeight)
     Convention de transform, verifiee sur la demo : la POSITION est le coin
     MINIMAL de la boite et l'ECHELLE est sa taille TOTALE -- ce n'est pas un
     centre avec une demi-portee.
  2. `Landscape.runtime_virtual_textures` = les deux, sur l'acteur ET sur chaque
     proxy World Partition.
  3. `virtual_texture_num_lods` = 6, `virtual_texture_lod_bias` = 0.
  4. `virtual_texture_render_pass_type` = ALWAYS.

Ne PAS utiliser `unreal.RuntimeVirtualTextureService.create_rvt_volume` : il
cree des `Actor` generiques a l'origine et a l'echelle 1, pas des volumes de RVT.
"""

from __future__ import annotations

import unreal

DOSSIER_RVT = "/Game/Orasot_Bundle/Stylized_Landscape_5_Bioms/Global/RVT"
TEXTURES = {
    "RVT_Landscape_Material": "Worldseed_RVT_Material",
    "RVT_Landscape_Height": "Worldseed_RVT_Height",
}

# Le volume deborde un peu du terrain : sur le bord exact, le filtrage de la RVT
# irait chercher des texels hors du domaine.
MARGE_XY = 0.01
MARGE_Z = 0.08
NUM_LODS = 6

_log: list[str] = []


def log(kind: str, message: str) -> None:
    line = "{}: {}".format(kind, message)
    _log.append(line)
    unreal.log("WORLDSEED_RVT " + line)
    print(line)


def _acteurs():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def bornes_du_landscape():
    """Boite du terrain, prise sur les PROXIES.

    L'acteur `Landscape` parent n'a pas de bornes propres en World Partition :
    `get_actor_bounds` y rend zero. Ce sont les `LandscapeStreamingProxy` qui
    portent la geometrie.
    """
    mn = [1e18, 1e18, 1e18]
    mx = [-1e18, -1e18, -1e18]
    trouve = False
    for a in _acteurs().get_all_level_actors():
        if not isinstance(a, unreal.LandscapeStreamingProxy):
            continue
        trouve = True
        c, e = a.get_actor_bounds(False)
        for i, (cc, ee) in enumerate(((c.x, e.x), (c.y, e.y), (c.z, e.z))):
            mn[i] = min(mn[i], cc - ee)
            mx[i] = max(mx[i], cc + ee)
    if not trouve:
        return None
    return mn, mx


def poser() -> dict:
    """Pose les deux volumes et cable le Landscape. Idempotent."""
    _log.clear()
    bornes = bornes_du_landscape()
    if bornes is None:
        log("ERROR", "aucun proxy de Landscape charge : charger les acteurs World Partition")
        return {"success": False, "log": list(_log)}
    mn, mx = bornes
    taille = [mx[i] - mn[i] for i in range(3)]
    coin = unreal.Vector(mn[0] - taille[0] * MARGE_XY,
                         mn[1] - taille[1] * MARGE_XY,
                         mn[2] - taille[2] * MARGE_Z)
    echelle = unreal.Vector(taille[0] * (1 + 2 * MARGE_XY),
                            taille[1] * (1 + 2 * MARGE_XY),
                            taille[2] * (1 + 2 * MARGE_Z))
    log("INFO", "terrain {:.0f} x {:.0f} x {:.0f} cm".format(*taille))

    sub = _acteurs()
    textures = []
    for asset, label in TEXTURES.items():
        rvt = unreal.EditorAssetLibrary.load_asset(DOSSIER_RVT + "/" + asset)
        if rvt is None:
            log("ERROR", "texture virtuelle introuvable : {}/{}".format(DOSSIER_RVT, asset))
            return {"success": False, "log": list(_log)}
        textures.append(rvt)
        deja = [a for a in sub.get_all_level_actors() if a.get_actor_label() == label]
        v = deja[0] if deja else sub.spawn_actor_from_class(
            unreal.RuntimeVirtualTextureVolume, coin)
        v.set_actor_label(label)
        # Position = coin MINIMAL, echelle = taille TOTALE.
        v.set_actor_location(coin, False, True)
        v.set_actor_scale3d(echelle)
        comp = v.get_component_by_class(unreal.RuntimeVirtualTextureComponent)
        comp.set_editor_property("virtual_texture", rvt)
        log("MODIFIED" if deja else "CREATED", "{} -> {}".format(label, asset))

    n = 0
    for a in sub.get_all_level_actors():
        if not isinstance(a, (unreal.Landscape, unreal.LandscapeStreamingProxy)):
            continue
        a.set_editor_property("runtime_virtual_textures", textures)
        a.set_editor_property("virtual_texture_num_lods", NUM_LODS)
        a.set_editor_property("virtual_texture_lod_bias", 0)
        # LE REGLAGE QUI AVAIT FAIT ECHOUER LA PREMIERE TENTATIVE.
        a.set_editor_property("virtual_texture_render_pass_type",
                              unreal.RuntimeVirtualTextureMainPassType.ALWAYS)
        n += 1
    log("MODIFIED", "{} acteurs de terrain ecrivent dans la RVT (passe principale ALWAYS)".format(n))
    return {"success": True, "log": list(_log)}


def verifier() -> dict:
    """Controle du montage, sans rien modifier."""
    sub = _acteurs()
    vols = {}
    for a in sub.get_all_level_actors():
        if isinstance(a, unreal.RuntimeVirtualTextureVolume):
            c = a.get_component_by_class(unreal.RuntimeVirtualTextureComponent)
            rvt = c.get_editor_property("virtual_texture") if c else None
            vols[a.get_actor_label()] = rvt.get_name() if rvt else None
    terrains, passes, lods = 0, set(), set()
    for a in sub.get_all_level_actors():
        if isinstance(a, (unreal.Landscape, unreal.LandscapeStreamingProxy)):
            terrains += 1
            passes.add(str(a.get_editor_property("virtual_texture_render_pass_type")).split(".")[-1])
            lods.add(int(a.get_editor_property("virtual_texture_num_lods")))
            noms = [r.get_name() for r in a.get_editor_property("runtime_virtual_textures")]
    return {
        "volumes": vols,
        "acteursDeTerrain": terrains,
        "texturesDuTerrain": noms if terrains else [],
        "passePrincipale": sorted(passes),
        "numLods": sorted(lods),
        "cvarVirtualTextures": unreal.SystemLibrary.get_console_variable_int_value(
            "r.VirtualTextures"),
    }
