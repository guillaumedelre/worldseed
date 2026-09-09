"""Exporte biome_index.png en textures importables par Unreal (pour le PCG).

    python export_biome_texture.py [dossier] [--bench-only|--tiles-only]

POURQUOI : tile_world.py ne decoupe que le relief et les dix couches de peinture.
La carte des biomes n'existe qu'en 8129x8129 non tuile, alors que c'est ELLE qui
doit piloter le PCG : les couches de peinture n'en distinguent que dix et
confondent des biomes (la plage et le desert chaud ont toutes deux DesertSand
pour couche dominante, et sont visuellement identiques en jeu).

CE QUI COMPTE, ET QUI SE RATE FACILEMENT : biome_index.png n'est pas une image,
c'est un tableau d'IDENTIFIANTS (0 a 18). Toute interpolation entre deux texels
fabrique un biome qui n'existe pas a cet endroit. Sur un trait de cote, un texel
entre Ocean (0) et Roche nue (16) interpole en bilineaire donne successivement
taiga, prairie, savane... Cote Unreal, l'import DOIT donc etre en Nearest, sans
mipmaps, sans sRGB et sans compression ; voir Tools/UE/pcg_bench.py qui pose ces
reglages et VERIFIE le retour pixel par pixel.

Sortie :
  tiles/<xN_yN>/biome_index.png   une carte par tuile Landscape (memes decoupes
                                  que tile_world.py, arete partagee comprise)
  bench/biome_bench.png           la fenetre de 2 km du banc d'essai PCG
  bench/biome_bench_rgb.png       la meme en couleurs, pour le controle a l'oeil
  bench/bench.json                rectangle monde, formule UV, composition
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

Image.MAX_IMAGE_PIXELS = None

# Centre du banc d'essai, en pixels de la sortie 8129.
# Choisi par balayage : c'est la fenetre de 2 km qui porte le plus de Plage au
# contact du Desert chaud (le couple que les couches de peinture confondent),
# avec en prime un littoral, une riviere et deux forets. Neuf biomes au-dessus
# de 1 %, ce qui teste vraiment l'echantillonnage d'identifiants.
BENCH_CENTRE_ROW = 6096
BENCH_CENTRE_COL = 7620
BENCH_SIZE_PX = 512          # 512 x 3,937 m = 2016 m


def _world_origin(manifest: dict) -> tuple[float, float, float]:
    """(x0, y0, cm_par_pixel) du pixel [0, 0] de la sortie pleine resolution.

    Convention de tile_world.py, verifiee : la COLONNE de l'image donne le X
    monde, la LIGNE donne le Y. La ligne 0 est donc au sud (Y = -1 600 000) et
    la ligne 4064 tombe sur l'equateur.
    """
    ls = manifest["landscape"]
    return (float(ls["actorLocation"]["x"]),
            float(ls["actorLocation"]["y"]),
            float(ls["scale"]["x"]))


def cut_tiles(src: Path, tiles_per_side: int = 2) -> list[Path]:
    """Decoupe la carte des biomes comme tile_world.py decoupe le relief."""
    manifest = json.loads((src / "manifest.json").read_text(encoding="utf-8"))
    full_n = int(manifest["world"]["resolution"])
    quads = full_n - 1
    if quads % tiles_per_side:
        raise SystemExit("{} quads ne se divise pas en {} tuiles".format(quads, tiles_per_side))
    tile_quads = quads // tiles_per_side
    tile_n = tile_quads + 1

    arr = np.asarray(Image.open(src / "biome_index.png"))
    if arr.shape != (full_n, full_n):
        raise SystemExit("biome_index.png fait {} au lieu de {}x{}".format(
            arr.shape, full_n, full_n))
    if arr.dtype != np.uint8:
        raise SystemExit("biome_index.png doit etre en 8 bits, pas {}".format(arr.dtype))

    root = src / "tiles"
    if not root.is_dir():
        raise SystemExit("{} absent : lancer tile_world.py d'abord".format(root))

    written = []
    for ty in range(tiles_per_side):
        for tx in range(tiles_per_side):
            d = root / "x{}_y{}".format(tx, ty)
            if not d.is_dir():
                raise SystemExit("tuile absente : {}".format(d))
            y0, x0 = ty * tile_quads, tx * tile_quads
            # +1 : la tuile inclut le sommet de bord, partage avec la voisine.
            sub = arr[y0:y0 + tile_n, x0:x0 + tile_n]
            out = d / "biome_index.png"
            Image.fromarray(sub, mode="L").save(out, optimize=True)
            written.append(out)
            print("CREATED: {}  ({}x{}, {} biomes)".format(
                out, tile_n, tile_n, len(np.unique(sub))))
    return written


def verify_tiles(src: Path, tiles_per_side: int = 2) -> None:
    """La couture doit etre identique des deux cotes, au bit pres."""
    root = src / "tiles"
    a = np.asarray(Image.open(root / "x0_y0" / "biome_index.png"))
    b = np.asarray(Image.open(root / "x1_y0" / "biome_index.png"))
    c = np.asarray(Image.open(root / "x0_y1" / "biome_index.png"))
    same_x = np.array_equal(a[:, -1], b[:, 0])
    same_y = np.array_equal(a[-1, :], c[0, :])
    print("COUTURE verticale identique :", same_x)
    print("COUTURE horizontale identique :", same_y)
    if not (same_x and same_y):
        raise SystemExit("les tuiles de biomes ne se raccordent pas")


def _make_probes(sub: np.ndarray, origin_x: float, origin_y: float,
                 cm_px: float, seed: int = 20260909) -> list[dict]:
    """Points de controle pour verifier l'echantillonnage cote Unreal.

    Deux populations, et c'est tout l'interet : les points de FRONTIERE (au
    moins un voisin d'identifiant different) sont ceux qu'une interpolation
    bilineaire fausse ; les points d'INTERIEUR servent de temoin, car ils
    restent justes meme avec un filtrage errone. Un banc ou seuls les points
    de frontiere echouent designe le filtre, pas l'import.
    """
    rng = np.random.default_rng(seed)
    n = sub.shape[0]
    inner = sub[1:-1, 1:-1]
    differs = ((inner != sub[:-2, 1:-1]) | (inner != sub[2:, 1:-1]) |
               (inner != sub[1:-1, :-2]) | (inner != sub[1:-1, 2:]))
    boundary = np.argwhere(differs) + 1
    interior = np.argwhere(~differs) + 1

    probes = []
    for kind, pool, count in (("frontiere", boundary, 300), ("interieur", interior, 100)):
        if not len(pool):
            continue
        pick = rng.choice(len(pool), size=min(count, len(pool)), replace=False)
        for row, col in pool[pick]:
            probes.append({
                "kind": kind,
                "px": {"col": int(col), "row": int(row)},
                # Centre du texel = sommet de Landscape correspondant.
                "worldCm": {"x": origin_x + int(col) * cm_px,
                            "y": origin_y + int(row) * cm_px},
                "expectedId": int(sub[row, col]),
            })
    return probes


def cut_bench(src: Path) -> Path:
    """Extrait la fenetre de 2 km du banc d'essai PCG."""
    manifest = json.loads((src / "manifest.json").read_text(encoding="utf-8"))
    labels = manifest["biomes"]["labels"]
    x0_cm, y0_cm, cm_px = _world_origin(manifest)

    arr = np.asarray(Image.open(src / "biome_index.png"))
    n = BENCH_SIZE_PX
    row0 = BENCH_CENTRE_ROW - n // 2
    col0 = BENCH_CENTRE_COL - n // 2
    if row0 < 0 or col0 < 0 or row0 + n > arr.shape[0] or col0 + n > arr.shape[1]:
        raise SystemExit("la fenetre du banc sort de la carte")
    sub = arr[row0:row0 + n, col0:col0 + n]

    out_dir = src / "bench"
    out_dir.mkdir(exist_ok=True)
    out = out_dir / "biome_bench.png"
    Image.fromarray(sub, mode="L").save(out, optimize=True)
    print("CREATED: {}  ({}x{}, IDs bruts 0-18)".format(out, n, n))

    # Meme fenetre en couleurs de debug : sert UNIQUEMENT au controle a l'oeil,
    # jamais au PCG (les couleurs ne sont pas des identifiants).
    rgb_src = src / "biome_debug_rgb.png"
    if rgb_src.is_file():
        rgb = np.asarray(Image.open(rgb_src))[row0:row0 + n, col0:col0 + n]
        out_rgb = out_dir / "biome_bench_rgb.png"
        Image.fromarray(rgb).save(out_rgb, optimize=True)
        print("CREATED: {}  (controle visuel, PAS pour le PCG)".format(out_rgb))

    counts = np.bincount(sub.ravel(), minlength=19)
    total = float(counts.sum())
    composition = {labels[str(i)]: round(counts[i] / total * 100, 2)
                   for i in np.argsort(counts)[::-1] if counts[i]}

    # Le texel i a pour CENTRE le sommet de Landscape i : les deux grilles se
    # superposent exactement. D'ou le demi-texel dans la formule UV ci-dessous.
    spec = {
        "source": "biome_index.png",
        "sizePx": n,
        "pixelRow0": row0,
        "pixelCol0": col0,
        "metresPerPixel": cm_px / 100.0,
        "world": {
            "originCm": {"x": x0_cm + col0 * cm_px, "y": y0_cm + row0 * cm_px},
            "spanCm": n * cm_px,
            "_comment": "originCm est le CENTRE du texel [0,0], "
                        "confondu avec le sommet de Landscape correspondant.",
        },
        # A donner tel quel au Transform du noeud PCG Texture Sampler. La surface
        # d'une PCGTextureData est la boite LOCALE [-1,1] transformee
        # (PCGTextureData.cpp), donc l'echelle vaut la DEMI-portee, et le centre
        # est decale d'un demi-texel par rapport a originCm + n/2 : le bord de la
        # surface passe a un demi-texel avant le centre du texel 0. Se tromper la
        # decale tout l'echantillonnage d'un demi-texel, soit 197 cm.
        "pcgSurface": {
            "locationCm": {"x": x0_cm + (col0 + (n - 1) / 2.0) * cm_px,
                           "y": y0_cm + (row0 + (n - 1) / 2.0) * cm_px},
            "halfSpanCm": n * cm_px / 2.0,
            "texelSizeCm": cm_px,
            "useAbsoluteTransform": True,
        },
        "uvFormula": "u = (worldX - originCm.x) / spanCm + 0.5 / sizePx ; "
                     "v = (worldY - originCm.y) / spanCm + 0.5 / sizePx",
        "decodeFormula": "biomeId = round(sample.r * 255)",
        "import": {
            "filter": "TF_NEAREST", "sRGB": False, "mips": "TMGS_NO_MIPMAPS",
            "compression": "TC_VECTOR_DISPLACEMENTMAP (B8G8R8A8 non compresse)",
            "_comment": "ces trois derniers reglages sont exactement ceux que PCG "
                        "force lui-meme (PCGTextureData.cpp) : sans eux il DUPLIQUE "
                        "la texture en memoire pour se les donner.",
        },
        "pcg": {
            "filter": "Point",
            "_comment": "PIEGE PRINCIPAL : EPCGTextureFilter vaut Bilinear par defaut "
                        "sur TOUS les noeuds PCG (PCGCommon.h). Ce filtre est celui de "
                        "PCG, distinct de TF_NEAREST pose sur la texture : le second "
                        "n'empeche PAS le premier. En Bilinear, un point entre Ocean (0) "
                        "et Roche nue (16) rend des identifiants intermediaires, donc "
                        "des biomes qui n'existent pas a cet endroit, sans aucune erreur.",
        },
        "biomeIds": manifest["biomes"]["ids"],
        "composition": composition,
    }
    probes = _make_probes(sub, spec["world"]["originCm"]["x"],
                          spec["world"]["originCm"]["y"], cm_px)
    (out_dir / "probes.json").write_text(
        json.dumps({"source": "biome_bench.png", "sizePx": n, "probes": probes},
                   indent=2, ensure_ascii=False), encoding="utf-8")
    n_bound = sum(1 for p in probes if p["kind"] == "frontiere")
    print("CREATED: {}  ({} points, dont {} sur une frontiere)".format(
        out_dir / "probes.json", len(probes), n_bound))
    (out_dir / "bench.json").write_text(
        json.dumps(spec, indent=2, ensure_ascii=False), encoding="utf-8")
    print("CREATED: {}".format(out_dir / "bench.json"))

    surf = spec["pcgSurface"]
    print("\nBanc d'essai : {:.0f} m de cote, surface PCG centree "
          "X={:.1f} Y={:.1f} cm, demi-portee {:.1f} cm".format(
              spec["world"]["spanCm"] / 100.0, surf["locationCm"]["x"],
              surf["locationCm"]["y"], surf["halfSpanCm"]))
    print("Composition :")
    for name, pct in composition.items():
        if pct >= 1.0:
            print("  {:<24} {:>5.1f} %".format(name, pct))
    return out


def biomecore_palette(n_biomes: int = 19) -> list[tuple[int, int, int]]:
    """Palette pour `BP_PCGBiomeTexture`, qui reconnait les biomes par COULEUR.

    BiomeCore ne lit pas des identifiants : il compare la couleur echantillonnee
    au `BiomeColor` de chaque definition, a `BiomeColorTolerance` pres (0,01 par
    defaut, en espace lineaire). Les couleurs de `biome_debug_rgb.png` ne sont
    pas faites pour ca : la paire la plus proche (foret_temperee_humide /
    foret_tropicale_humide) n'est ecartee que de 14/255, et l'ecart se resserre
    encore en lineaire dans les tons sombres.

    On prend donc un reseau a trois niveaux par canal : deux couleurs distinctes
    different d'au moins 127/255 sur un canal, soit 0,216 en lineaire une fois
    passe le decodage sRGB. C'est vingt fois la tolerance, et le reseau donne
    27 places pour 19 biomes.
    """
    levels = (0, 128, 255)
    lattice = [(r, g, b) for r in levels for g in levels for b in levels]
    # L'ordre fixe rend la palette reproductible d'une generation a l'autre.
    if n_biomes > len(lattice):
        raise SystemExit("{} biomes ne tiennent pas dans le reseau".format(n_biomes))
    return lattice[:n_biomes]


def cut_biomecore(src: Path, tiles_per_side: int = 2) -> Path:
    """Ecrit la carte des biomes en couleurs BiomeCore : tuiles + fenetre du banc."""
    manifest = json.loads((src / "manifest.json").read_text(encoding="utf-8"))
    labels = manifest["biomes"]["labels"]
    ids = manifest["biomes"]["ids"]
    arr = np.asarray(Image.open(src / "biome_index.png"))
    palette = biomecore_palette(len(ids))

    lut = np.zeros((256, 3), dtype=np.uint8)
    for i, colour in enumerate(palette):
        lut[i] = colour
    rgb = lut[arr]

    full_n = int(manifest["world"]["resolution"])
    tile_quads = (full_n - 1) // tiles_per_side
    tile_n = tile_quads + 1
    root = src / "tiles"
    for ty in range(tiles_per_side):
        for tx in range(tiles_per_side):
            d = root / "x{}_y{}".format(tx, ty)
            if not d.is_dir():
                raise SystemExit("tuile absente : {}".format(d))
            y0, x0 = ty * tile_quads, tx * tile_quads
            out = d / "biome_biomecore.png"
            Image.fromarray(rgb[y0:y0 + tile_n, x0:x0 + tile_n]).save(out, optimize=True)
            print("CREATED: {}  ({}x{})".format(out, tile_n, tile_n))

    bench = src / "bench"
    bench.mkdir(exist_ok=True)
    n = BENCH_SIZE_PX
    row0, col0 = BENCH_CENTRE_ROW - n // 2, BENCH_CENTRE_COL - n // 2
    out = bench / "biome_bench_biomecore.png"
    Image.fromarray(rgb[row0:row0 + n, col0:col0 + n]).save(out, optimize=True)
    print("CREATED: {}  ({}x{})".format(out, n, n))

    spec = {
        "_comment": "Couleurs a recopier dans le BiomeColor de chaque "
                    "BiomeDefinition de BiomeCore. Importer la texture en sRGB, "
                    "sans mipmaps, non compressee.",
        "tolerance": 0.01,
        "biomes": [
            {"id": i, "key": key, "label": labels[str(i)],
             "rgb255": list(palette[i]),
             "linear": [round(_srgb_to_linear(c / 255.0), 6) for c in palette[i]]}
            for key, i in sorted(ids.items(), key=lambda kv: kv[1])
        ],
    }
    (bench / "biomecore_palette.json").write_text(
        json.dumps(spec, indent=2, ensure_ascii=False), encoding="utf-8")
    print("CREATED: {}".format(bench / "biomecore_palette.json"))

    # Controle : la plus petite distance entre deux couleurs, en lineaire.
    lin = np.array([[_srgb_to_linear(c / 255.0) for c in col] for col in palette])
    dmin = min(float(np.linalg.norm(lin[i] - lin[j]))
               for i in range(len(lin)) for j in range(i + 1, len(lin)))
    print("\nEcart lineaire minimal entre deux biomes : {:.4f} "
          "(tolerance BiomeCore 0,0100 -> marge x{:.0f})".format(dmin, dmin / 0.01))
    return bench / "biomecore_palette.json"


def _srgb_to_linear(c: float) -> float:
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def verify_roundtrip(src: Path, exported: Path | None = None) -> bool:
    """Compare la texture reexportee d'Unreal au PNG source, texel par texel.

    C'est la preuve que l'import n'a rien altere : pas d'echantillonnage, pas de
    compression, pas de gamma. Exhaustif, pas par sondage — 262144 texels.
    """
    bench = src / "bench"
    exported = Path(exported) if exported else bench / "roundtrip.png"
    if not exported.is_file():
        raise SystemExit("{} absent : lancer pcg_bench.verify_texture() "
                         "depuis l'editeur".format(exported))

    ref = np.asarray(Image.open(bench / "biome_bench.png"))
    back = np.asarray(Image.open(exported))
    if back.ndim == 3:                       # Unreal reexporte en RGBA
        back = back[..., 0]
    if back.shape != ref.shape:
        print("ECHEC: taille {} au lieu de {}".format(back.shape, ref.shape))
        return False

    diff = back != ref
    n_bad = int(diff.sum())
    print("Identifiants source : {}".format(sorted(np.unique(ref).tolist())))
    print("Identifiants relus  : {}".format(sorted(np.unique(back).tolist())))
    if not n_bad:
        print("OK: {} texels identiques, aucun ecart".format(ref.size))
        return True

    # Un ecart concentre sur les frontieres designe un filtrage ; un ecart
    # partout designe la compression ou le gamma.
    inner = ref[1:-1, 1:-1]
    edge = np.zeros_like(diff)
    edge[1:-1, 1:-1] = ((inner != ref[:-2, 1:-1]) | (inner != ref[2:, 1:-1]) |
                        (inner != ref[1:-1, :-2]) | (inner != ref[1:-1, 2:]))
    on_edge = int((diff & edge).sum())
    print("ECHEC: {} texels sur {} different ({} sur une frontiere)".format(
        n_bad, ref.size, on_edge))
    print("  -> {}".format("filtrage (frontieres seules)" if on_edge == n_bad
                           else "compression ou gamma (ecart partout)"))
    ys, xs = np.nonzero(diff)
    for i in range(min(5, len(ys))):
        print("  texel ({}, {}) : {} -> {}".format(
            xs[i], ys[i], ref[ys[i], xs[i]], back[ys[i], xs[i]]))
    return False


if __name__ == "__main__":
    here = Path(__file__).resolve().parent
    sys.path.insert(0, str(here))
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {a for a in sys.argv[1:] if a.startswith("--")}

    root = Path(args[0]) if args else \
        here.parent.parent / "Saved" / "WorldGen" / "20260909"

    if "--verify-roundtrip" in flags:
        raise SystemExit(0 if verify_roundtrip(root) else 1)
    if "--biomecore" in flags:
        cut_biomecore(root)
        raise SystemExit(0)
    src = Path(args[0]) if args else \
        here.parent.parent / "Saved" / "WorldGen" / "20260909"

    if "--bench-only" not in flags:
        cut_tiles(src)
        verify_tiles(src)
    if "--tiles-only" not in flags:
        cut_bench(src)
