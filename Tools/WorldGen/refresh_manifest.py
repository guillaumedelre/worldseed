"""Reecrit le manifeste d'une sortie deja calculee, sans resimuler le monde.

    python refresh_manifest.py [dossier]

Le manifeste ne depend que des regles et des statistiques (deja stockees dans
l'ancien manifeste) : quand la configuration Landscape ou le calage altimetrique
changent dans le code, il suffit de le regenerer. Les PNG, eux, ne bougent pas.

L'ancien manifeste est conserve sous manifest.prev.json pour pouvoir revenir en
arriere.
"""

from __future__ import annotations

import json
import shutil
import sys
from pathlib import Path

from worldgen import export
from worldgen.config import Rules


def refresh(src: Path, rules_path: Path) -> dict:
    rules = Rules.load(rules_path)
    old = json.loads((src / "manifest.json").read_text(encoding="utf-8"))
    stats = old["stats"]

    # Les PNG exportes fixent la resolution reelle : elle prime sur les regles.
    resolution = int(old["world"]["resolution"])
    if int(rules["world"]["resolution"]) != resolution:
        raise SystemExit(
            "resolution des regles ({}) differente de celle de la sortie ({}) : "
            "regenere le monde ou ajuste world_rules.json".format(
                rules["world"]["resolution"], resolution))

    geo = rules.out_geometry
    new = export.build_manifest(rules, geo, stats)

    shutil.copyfile(src / "manifest.json", src / "manifest.prev.json")
    (src / "manifest.json").write_text(
        json.dumps(new, indent=2, ensure_ascii=False), encoding="utf-8")
    return {"avant": old["landscape"], "apres": new["landscape"]}


if __name__ == "__main__":
    here = Path(__file__).resolve().parent
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else \
        here.parent.parent / "Saved" / "WorldGen" / "20260909"
    diff = refresh(src, here / "rules" / "world_rules.json")
    print("MODIFIED:", src / "manifest.json")
    for phase in ("avant", "apres"):
        cfg = diff[phase]
        print("  {:<5} {}x{} composants de {} quads x {}x{} sections, acteur Z = {} cm".format(
            phase, cfg["component_count_x"], cfg["component_count_y"],
            cfg["quads_per_section"], cfg["sections_per_component"],
            cfg["sections_per_component"], cfg["actorLocation"]["z"]))
