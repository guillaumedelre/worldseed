"""Chargement et validation du fichier de regles + geometrie du monde."""

from __future__ import annotations

import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np

# Tailles de Landscape valides : composants * quads_par_section * sections + 1.
# On ne verifie que la forme N*k+1 avec k dans les tailles de section usuelles.
_SECTION_QUADS = (7, 15, 31, 63, 127, 255)


class RulesError(ValueError):
    """Le fichier de regles est incoherent."""


@dataclass(frozen=True)
class Geometry:
    """Geometrie du monde, a une resolution donnee.

    L'origine (0, 0) est au CENTRE du monde, sur l'equateur.
    """

    size_m: float
    n: int
    lat_span_deg: float
    tropic_deg: float
    polar_circle_deg: float
    # "linear" : la latitude varie proportionnellement a Y.
    # "equalArea" : Y varie comme le SINUS de la latitude, ce qui donne a chaque
    #   zone climatique la part de surface qu'elle a sur une sphere.
    # Voir latitude_deg() pour ce que ce choix change, et de combien.
    latitude_mapping: str = "linear"
    # Melange entre les deux, 0 = lineaire, 1 = equivalent-aire. Sert a garder
    # des calottes praticables tout en se rapprochant des proportions reelles.
    latitude_equal_area_blend: float = 1.0

    @property
    def meters_per_pixel(self) -> float:
        return self.size_m / (self.n - 1)

    @property
    def half_size_m(self) -> float:
        return self.size_m * 0.5

    def axis_m(self) -> np.ndarray:
        """Coordonnee monde, en metres, de chaque ligne/colonne (-half .. +half)."""
        return np.linspace(-self.half_size_m, self.half_size_m, self.n, dtype=np.float64)

    def _melange(self) -> float:
        if self.latitude_mapping != "equalArea":
            return 0.0
        return min(max(float(self.latitude_equal_area_blend), 0.0), 1.0)

    def latitude_deg(self) -> np.ndarray:
        """Latitude (deg) de chaque LIGNE. j=0 -> pole sud, j=n-1 -> pole nord.

        DEUX CORRESPONDANCES POSSIBLES, et le choix pese plus lourd que n'importe
        quel reglage de climat, parce qu'il fixe la PART DE SURFACE de chaque
        zone climatique :

          lineaire    tropiques 26,0 %   temperees 47,9 %   polaires 26,0 %
          equal-area  tropiques 39,8 %   temperees 52,0 %   polaires  8,3 %
          sphere      tropiques 39,8 %   temperees 52,0 %   polaires  8,3 %

        La correspondance lineaire donne donc TROIS FOIS trop de surface polaire
        et un tiers de tropiques en moins que la Terre. Sur une sphere, la
        surface entre -L et +L vaut sin(L) : c'est la projection cylindrique
        equivalente de Lambert.

        Contrepartie de gameplay, a assumer : en equivalent-aire, la bande qui
        va du cercle polaire au pole passe d'environ 1040 a 330 metres sur un
        monde de 8 km. D'ou le melange reglable.
        """
        y = self.axis_m() / self.half_size_m               # -1 .. +1
        m = self._melange()
        if m > 0.0:
            y = (1.0 - m) * y + m * (np.arcsin(np.clip(y, -1.0, 1.0)) / (np.pi * 0.5))
        return y * (self.lat_span_deg * 0.5)

    def latitude_grid(self) -> np.ndarray:
        """Carte 2D de la latitude, meme forme que le terrain."""
        return np.repeat(self.latitude_deg()[:, None], self.n, axis=1)

    def y_of_latitude(self, lat_deg: float) -> float:
        """Coordonnee Y monde (m) d'une latitude donnee. Inverse de latitude_deg."""
        u = lat_deg / (self.lat_span_deg * 0.5)            # -1 .. +1
        m = self._melange()
        if m <= 0.0:
            return u * self.half_size_m
        # Pas de forme fermee quand on melange : on inverse numeriquement sur une
        # fonction strictement croissante, donc sans ambiguite.
        grille = np.linspace(-1.0, 1.0, 4001)
        image = (1.0 - m) * grille + m * (np.arcsin(grille) / (np.pi * 0.5))
        return float(np.interp(u, image, grille)) * self.half_size_m

    def row_of_latitude(self, lat_deg: float) -> int:
        y = self.y_of_latitude(lat_deg)
        return int(round((y + self.half_size_m) / self.size_m * (self.n - 1)))

    def landmarks(self) -> dict[str, float]:
        """Latitudes remarquables -> Y en metres."""
        t, p = self.tropic_deg, self.polar_circle_deg
        h = self.lat_span_deg * 0.5
        return {
            "pole_nord": self.y_of_latitude(+h),
            "cercle_polaire_arctique": self.y_of_latitude(+p),
            "tropique_cancer": self.y_of_latitude(+t),
            "equateur": 0.0,
            "tropique_capricorne": self.y_of_latitude(-t),
            "cercle_polaire_antarctique": self.y_of_latitude(-p),
            "pole_sud": self.y_of_latitude(-h),
        }

    def rescaled(self, n: int) -> "Geometry":
        return Geometry(self.size_m, n, self.lat_span_deg, self.tropic_deg,
                        self.polar_circle_deg, self.latitude_mapping,
                        self.latitude_equal_area_blend)


class Rules:
    """Acces typé au fichier de regles, avec validation."""

    def __init__(self, data: dict[str, Any], source: Path | None = None):
        self.data = data
        self.source = source
        self._validate()

    # ---------------------------------------------------------------- chargement

    @classmethod
    def load(cls, path: str | Path) -> "Rules":
        p = Path(path)
        with p.open("r", encoding="utf-8") as fh:
            return cls(json.load(fh), source=p)

    def dump(self, path: str | Path | None = None) -> None:
        """Reecrit le fichier de regles en gardant les listes courtes sur une ligne.

        Le dump standard de json eclate chaque nombre sur sa propre ligne, ce qui
        rend les matrices de recettes et les palettes illisibles. Ce fichier est
        fait pour etre relu et edite a la main.
        """
        target = Path(path) if path is not None else self.source
        if target is None:
            raise RulesError("aucun chemin de sortie et aucune source connue")
        target.write_text(_dumps_compact(self.data) + "\n", encoding="utf-8")

    def set(self, dotted: str, value: Any) -> None:
        """Ecrit une valeur par chemin pointe : rules.set('erosion.iterations', 60)."""
        parts = dotted.split(".")
        node = self.data
        for part in parts[:-1]:
            if part not in node or not isinstance(node[part], dict):
                raise RulesError(f"chemin inconnu dans les regles : '{dotted}'")
            node = node[part]
        node[parts[-1]] = value

    # ---------------------------------------------------------------- raccourcis

    def __getitem__(self, key: str) -> Any:
        return self.data[key]

    def get(self, path: str, default: Any = None) -> Any:
        """Acces pointe : rules.get('erosion.talusAngleDeg')."""
        node: Any = self.data
        for part in path.split("."):
            if not isinstance(node, dict) or part not in node:
                return default
            node = node[part]
        return node

    @property
    def seed(self) -> int:
        return int(self.data["seed"])

    @property
    def sim_geometry(self) -> Geometry:
        w = self.data["world"]
        return Geometry(
            size_m=float(w["sizeKm"]) * 1000.0,
            n=int(w["simResolution"]),
            lat_span_deg=float(w["latitudeSpanDeg"]),
            tropic_deg=float(w["tropicDeg"]),
            polar_circle_deg=float(w["polarCircleDeg"]),
            latitude_mapping=str(w.get("latitudeMapping", "linear")),
            latitude_equal_area_blend=float(w.get("latitudeEqualAreaBlend", 1.0)),
        )

    @property
    def out_geometry(self) -> Geometry:
        w = self.data["world"]
        return Geometry(
            size_m=float(w["sizeKm"]) * 1000.0,
            n=int(w["resolution"]),
            lat_span_deg=float(w["latitudeSpanDeg"]),
            tropic_deg=float(w["tropicDeg"]),
            polar_circle_deg=float(w["polarCircleDeg"]),
            latitude_mapping=str(w.get("latitudeMapping", "linear")),
            latitude_equal_area_blend=float(w.get("latitudeEqualAreaBlend", 1.0)),
        )

    def rng(self, stream: str = "") -> np.random.Generator:
        """Generateur deterministe, derive de la graine et d'un nom de flux.

        Utiliser un flux par etape : ainsi modifier le nombre de tirages d'une
        etape ne decale pas les etapes suivantes.
        """
        mix = self.seed & 0xFFFFFFFF
        for ch in stream:
            mix = (mix * 1099511628211 + ord(ch)) & 0xFFFFFFFFFFFFFFFF
        return np.random.default_rng(mix)

    # ---------------------------------------------------------------- validation

    def _validate(self) -> None:
        d = self.data
        for key in ("seed", "world", "tectonics", "temperature", "precipitation",
                    "erosion", "biomes", "surfaces"):
            if key not in d:
                raise RulesError(f"section manquante dans les regles : '{key}'")

        w = d["world"]
        for n_key in ("resolution", "simResolution"):
            n = int(w[n_key])
            if n < 65:
                raise RulesError(f"world.{n_key} = {n} : trop petit (minimum 65)")
            if not _is_valid_landscape_size(n) and n_key == "resolution":
                raise RulesError(
                    f"world.resolution = {n} n'est pas une taille de Landscape valide. "
                    f"Attendu composants * quads_section * sections + 1, par ex. "
                    f"{_suggest_sizes(n)}"
                )

        if int(w["simResolution"]) > int(w["resolution"]):
            raise RulesError(
                "world.simResolution ne doit pas depasser world.resolution "
                "(la simulation est sur-echantillonnee vers la sortie, jamais l'inverse)"
            )

        if float(w["minElevationM"]) >= float(w["maxElevationM"]):
            raise RulesError("world.minElevationM doit etre < world.maxElevationM")

        land = float(d["tectonics"]["landRatio"])
        if not 0.02 <= land <= 0.98:
            raise RulesError(f"tectonics.landRatio = {land} : attendu entre 0.02 et 0.98")

        for pole in ("northPole", "southPole"):
            v = d["tectonics"][pole]
            if v not in ("ocean", "continent", "free"):
                raise RulesError(f"tectonics.{pole} = '{v}' : attendu ocean|continent|free")

        # surfaces : coherence noms de couches / recettes
        layers = d["surfaces"]["layers"]
        names = d["surfaces"]["layerAssetNames"]
        if len(layers) != len(names):
            raise RulesError("surfaces.layers et surfaces.layerAssetNames ont des tailles differentes")
        n_layers = len(layers)
        ids = d["biomes"]["ids"]
        for biome_id in ids.values():
            key = str(biome_id)
            if key not in d["surfaces"]["recipes"]:
                raise RulesError(f"surfaces.recipes : recette manquante pour le biome {key}")
            recipe = d["surfaces"]["recipes"][key]
            if len(recipe) != n_layers:
                raise RulesError(
                    f"surfaces.recipes['{key}'] a {len(recipe)} poids "
                    f"pour {n_layers} couches"
                )
            total = sum(recipe)
            if total <= 0.0:
                raise RulesError(f"surfaces.recipes['{key}'] : somme des poids nulle")

        # bandes de Whittaker : temperatures strictement croissantes, seuils croissants
        bands = d["biomes"]["whittakerBands"]
        if not bands:
            raise RulesError("biomes.whittakerBands est vide")
        last_t = -math.inf
        for band in bands:
            t = float(band["tMax"])
            if t <= last_t:
                raise RulesError(
                    f"biomes.whittakerBands : tMax doit etre strictement croissant "
                    f"({last_t} puis {t})"
                )
            last_t = t
            last_p = -math.inf
            for p_max, name in band["cuts"]:
                if float(p_max) <= last_p:
                    raise RulesError(
                        f"biomes.whittakerBands (tMax={t}) : seuils de pluie non croissants"
                    )
                last_p = float(p_max)
                if name not in ids:
                    raise RulesError(
                        f"biomes.whittakerBands : biome '{name}' absent de biomes.ids"
                    )
        if last_t < 60.0:
            raise RulesError(
                "biomes.whittakerBands : la derniere bande doit couvrir les temperatures "
                "extremes (tMax >= 60)"
            )


def _dumps_compact(obj: Any, indent: int = 0) -> str:
    """JSON indente, mais listes de scalaires gardees sur une seule ligne."""
    pad = "  " * indent
    if isinstance(obj, dict):
        if not obj:
            return "{}"
        body = ",\n".join(
            f"{pad}  {json.dumps(k, ensure_ascii=False)}: {_dumps_compact(v, indent + 1)}"
            for k, v in obj.items()
        )
        return "{\n" + body + "\n" + pad + "}"
    if isinstance(obj, list):
        if all(isinstance(x, (int, float, str, bool)) or x is None for x in obj):
            return "[" + ", ".join(json.dumps(x, ensure_ascii=False) for x in obj) + "]"
        body = ",\n".join(f"{pad}  {_dumps_compact(v, indent + 1)}" for v in obj)
        return "[\n" + body + "\n" + pad + "]"
    return json.dumps(obj, ensure_ascii=False)


def _is_valid_landscape_size(n: int) -> bool:
    """Une taille de Landscape vaut composants * quads_section * sections + 1."""
    quads = n - 1
    for section in _SECTION_QUADS:
        for sections_per_comp in (1, 2):
            per_comp = section * sections_per_comp
            if quads % per_comp == 0 and quads // per_comp >= 1:
                return True
    return False


def _suggest_sizes(target: int) -> str:
    """Les trois tailles valides les plus proches de la cible."""
    candidates: set[int] = set()
    for section in _SECTION_QUADS:
        for sections_per_comp in (1, 2):
            per_comp = section * sections_per_comp
            for comps in range(1, 257):
                candidates.add(comps * per_comp + 1)
    ranked = sorted(candidates, key=lambda c: abs(c - target))[:3]
    return ", ".join(str(c) for c in sorted(ranked))


def landscape_config_for(resolution: int) -> dict[str, int]:
    """Configuration Landscape (section/composants) pour une resolution donnee.

    Prefere les sections de 127 quads, 1 section par composant : c'est ce
    qu'Epic recommande pour les grands mondes.
    """
    quads = resolution - 1
    best: dict[str, int] | None = None
    for section in (127, 63, 255, 31, 15, 7):
        for sections_per_comp in (1, 2):
            per_comp = section * sections_per_comp
            if quads % per_comp:
                continue
            comps = quads // per_comp
            cand = {
                "quads_per_section": section,
                "sections_per_component": sections_per_comp,
                "component_count_x": comps,
                "component_count_y": comps,
                "resolution": resolution,
            }
            # On veut le MOINS de composants possible, donc les composants les
            # plus larges (sections 2x2 de 127 quads = 254 quads par composant).
            # Mesures dans l'editeur (5.8, World Partition) :
            #   - 4033 sommets en 4096 composants : creation en 549 s, 20 Go de RAM ;
            #   - 8129 sommets en 1024 composants : creation en 81 s.
            # Et surtout, le nombre de composants pilote le nombre de textures de
            # poids traitees en une passe GPU : au-dela d'environ 1024 composants
            # avec dix couches, l'import fait tomber le thread RHI (plafond D3D12
            # MAX_NUM_CONCURRENT_CMD_LISTS). La boucle essaie 127 avant 63, donc a
            # nombre de composants egal la plus grande section l'emporte.
            if best is None or comps < best["component_count_x"]:
                best = cand
    if best is None:
        raise RulesError(f"aucune configuration Landscape pour la resolution {resolution}")
    return best


def landscape_scale_for(rules: Rules) -> dict[str, float]:
    """Echelle XYZ du Landscape, en unites Unreal (cm)."""
    geo = rules.out_geometry
    w = rules["world"]
    xy = geo.size_m * 100.0 / (geo.n - 1)
    span_m = float(w["maxElevationM"]) - float(w["minElevationM"])
    # Le Landscape couvre 512 unites de hauteur * echelle Z (en cm).
    z = span_m * 100.0 / 512.0
    return {"x": xy, "y": xy, "z": z}
