"""Point d'entree ligne de commande.

    python -m worldgen --rules rules/world_rules.json --out ../../Saved/WorldGen
    python -m worldgen --preview            # basse resolution, iteration rapide
    python -m worldgen --seed 42 --sim 4097 # une autre graine, en pleine finesse
"""

from __future__ import annotations

import argparse
import sys
import webbrowser
from pathlib import Path

from .config import Rules, RulesError, landscape_config_for, landscape_scale_for
from .pipeline import run

_HERE = Path(__file__).resolve().parent.parent
_DEFAULT_RULES = _HERE / "rules" / "world_rules.json"
_DEFAULT_OUT = _HERE.parent.parent / "Saved" / "WorldGen"


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="worldgen",
        description="Generateur de monde climatique Worldseed (hors Unreal).",
    )
    p.add_argument("--rules", type=Path, default=_DEFAULT_RULES,
                   help="fichier de regles JSON")
    p.add_argument("--out", type=Path, default=_DEFAULT_OUT,
                   help="dossier de sortie (un sous-dossier par graine y est cree)")
    p.add_argument("--seed", type=int, default=None,
                   help="remplace la graine du fichier de regles")
    p.add_argument("--sim", type=int, default=None,
                   help="resolution de simulation (defaut : celle des regles)")
    p.add_argument("--resolution", type=int, default=None,
                   help="resolution de sortie du Landscape")
    p.add_argument("--preview", action="store_true",
                   help="raccourci : simulation 1025, sortie 2017, erosion allegee")
    p.add_argument("--no-write", action="store_true",
                   help="calcule et affiche les controles sans rien ecrire")
    p.add_argument("--open", action="store_true",
                   help="ouvre le rapport HTML a la fin")
    p.add_argument("--quiet", action="store_true", help="pas de journal d'etapes")
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)

    try:
        rules = Rules.load(args.rules)
    except FileNotFoundError:
        print("Fichier de regles introuvable : {}".format(args.rules), file=sys.stderr)
        return 2
    except RulesError as exc:
        print("Regles invalides : {}".format(exc), file=sys.stderr)
        return 2

    if args.preview:
        rules.set("world.simResolution", 1025)
        rules.set("world.resolution", 2017)
        rules.set("erosion.iterations", 30)
        rules.set("precipitation.advectionSweeps", 220)
    if args.seed is not None:
        rules.data["seed"] = int(args.seed)
    if args.sim is not None:
        rules.set("world.simResolution", int(args.sim))
    if args.resolution is not None:
        rules.set("world.resolution", int(args.resolution))

    try:
        rules._validate()
    except RulesError as exc:
        print("Regles invalides apres surcharge : {}".format(exc), file=sys.stderr)
        return 2

    cfg = landscape_config_for(int(rules.get("world.resolution")))
    scale = landscape_scale_for(rules)
    if not args.quiet:
        print("Worldseed | graine {} | monde {:.0f} km | simulation {} -> sortie {}".format(
            rules.seed, rules.get("world.sizeKm"),
            rules.get("world.simResolution"), rules.get("world.resolution")))
        print("Landscape : {}x{} composants de {} quads, echelle {:.2f} / {:.2f} / {:.2f}".format(
            cfg["component_count_x"], cfg["component_count_y"],
            cfg["quads_per_section"], scale["x"], scale["y"], scale["z"]))

    out_dir = Path(args.out) / str(rules.seed)
    result = run(rules, out_dir, verbose=not args.quiet, write_outputs=not args.no_write)

    print()
    print("CONTROLES")
    for label, expected, measured in result.manifest["stats"]["checks"]:
        flag = "!!" if "ECART" in measured else "  "
        print("  {} {:32s} attendu {:26s} -> {}".format(flag, label, expected, measured))

    if not args.no_write:
        print()
        print("Sortie : {}".format(out_dir))
        print("Rapport : {}".format(result.report))
        if args.open:
            webbrowser.open(result.report.resolve().as_uri())

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
