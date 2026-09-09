"""Decoupe une sortie Worldseed en tuiles importables par Unreal.

    python tile_world.py [dossier] [tuiles_par_cote]

POURQUOI : un Landscape unique de 8129x8129 fait 1024 composants ; avec dix
couches de peinture (donc trois textures de poids par composant) l'import
depasse le plafond du gestionnaire de residence D3D12 du moteur
(MAX_NUM_CONCURRENT_CMD_LISTS = 1024, constante de compilation) et le thread RHI
meurt. En 2x2 tuiles, chaque Landscape ne fait plus que 256 composants, soit
768 textures de poids : on repasse sous le plafond sans rien perdre.

Le decoupage est exact : 8129 = 2 x 4064 + 1, donc deux tuiles de 4065 pixels
qui PARTAGENT leur colonne (ou ligne) de bord. Aucun reechantillonnage, aucune
interpolation : les altitudes de la couture sont les memes des deux cotes, et
l'encodage 16 bits est recopie tel quel.
"""

from __future__ import annotations

import json
import shutil
import sys
from pathlib import Path

import numpy as np
from PIL import Image

Image.MAX_IMAGE_PIXELS = None


def tile_config(tile_n: int) -> dict:
    """Configuration Landscape d'une tuile de tile_n sommets de cote."""
    from worldgen.config import landscape_config_for
    return landscape_config_for(tile_n)


def cut(src: Path, tiles_per_side: int = 2) -> list[Path]:
    manifest = json.loads((src / "manifest.json").read_text(encoding="utf-8"))
    full_n = int(manifest["world"]["resolution"])
    quads = full_n - 1
    if quads % tiles_per_side:
        raise SystemExit("{} quads ne se divise pas en {} tuiles".format(quads, tiles_per_side))
    tile_quads = quads // tiles_per_side
    tile_n = tile_quads + 1

    cfg = tile_config(tile_n)
    scale = manifest["landscape"]["scale"]
    origin = manifest["landscape"]["actorLocation"]
    # Largeur d'une tuile en unites monde : les tuiles se touchent exactement.
    span_cm = tile_quads * float(scale["x"])

    out_root = src / "tiles"
    if out_root.exists():
        shutil.rmtree(out_root)
    out_root.mkdir(parents=True)

    sources = [("height_16bit.png", "height_16bit.png")] + [
        (f, f) for f in manifest["layerFiles"]]
    images = {name: np.asarray(Image.open(src / name)) for name, _ in sources}
    for name, arr in images.items():
        if arr.shape[0] != full_n or arr.shape[1] != full_n:
            raise SystemExit("{} fait {} au lieu de {}x{}".format(name, arr.shape, full_n, full_n))

    written = []
    for ty in range(tiles_per_side):
        for tx in range(tiles_per_side):
            name = "x{}_y{}".format(tx, ty)
            d = out_root / name
            d.mkdir()
            y0, x0 = ty * tile_quads, tx * tile_quads
            for fname, arr in images.items():
                # +1 : la tuile inclut le sommet de bord, partage avec la voisine.
                sub = arr[y0:y0 + tile_n, x0:x0 + tile_n]
                Image.fromarray(sub).save(d / fname, optimize=True)

            tm = json.loads(json.dumps(manifest))     # copie profonde
            tm["world"]["resolution"] = tile_n
            tm["tile"] = {"x": tx, "y": ty, "tilesPerSide": tiles_per_side,
                          "vertexOffsetX": x0, "vertexOffsetY": y0,
                          "label": "Worldseed_Landscape_" + name}
            tm["landscape"] = {
                **cfg,
                "scale": scale,
                "actorLocation": {"x": origin["x"] + tx * span_cm,
                                  "y": origin["y"] + ty * span_cm,
                                  "z": origin["z"]},
                "worldPartitionGridSize": manifest["landscape"].get("worldPartitionGridSize", 8),
            }
            (d / "manifest.json").write_text(
                json.dumps(tm, indent=2, ensure_ascii=False), encoding="utf-8")
            written.append(d)
            print("CREATED: {}  ({} sommets, {}x{} composants, acteur X={:.0f} Y={:.0f})".format(
                d, tile_n, cfg["component_count_x"], cfg["component_count_y"],
                tm["landscape"]["actorLocation"]["x"], tm["landscape"]["actorLocation"]["y"]))
    return written


def verify(src: Path, tiles_per_side: int = 2) -> None:
    """La couture doit etre identique des deux cotes, au bit pres."""
    root = src / "tiles"
    a = np.asarray(Image.open(root / "x0_y0" / "height_16bit.png"))
    b = np.asarray(Image.open(root / "x1_y0" / "height_16bit.png"))
    same = np.array_equal(a[:, -1], b[:, 0])
    c = np.asarray(Image.open(root / "x0_y1" / "height_16bit.png"))
    same_y = np.array_equal(a[-1, :], c[0, :])
    print("COUTURE verticale identique :", same)
    print("COUTURE horizontale identique :", same_y)
    if not (same and same_y):
        raise SystemExit("les tuiles ne se raccordent pas")


if __name__ == "__main__":
    here = Path(__file__).resolve().parent
    sys.path.insert(0, str(here))
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else \
        here.parent.parent / "Saved" / "WorldGen" / "20260909"
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    cut(src, n)
    verify(src, n)
