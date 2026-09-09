"""Etape 4a - erosion.

Trois processus, appliques en alternance :

1. Incision par puissance de courant  dh = -K * A^m * S^n
   L'aire drainee A est PONDEREE PAR LA PLUIE : les bassins humides se creusent
   en vallees profondes, les bassins arides restent en plateaux. C'est le lien
   direct entre le climat de l'etape 3 et la forme du terrain.
2. Erosion thermique : au-dela de l'angle de talus, la matiere glisse.
3. Diffusion de versant (creep) : adoucit les collines.

Les exposants sont normalises (aire et pente ramenees a un centile de
reference) de sorte qu'un meme jeu de reglages donne le meme resultat visuel a
1025, 2049 ou 4097 pixels. Sans cela, changer de resolution changerait le monde.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy import ndimage

from .hydrology import compute_flow

_LAPLACE = np.array([[0.5, 1.0, 0.5], [1.0, -6.0, 1.0], [0.5, 1.0, 0.5]], dtype=np.float32)

_NEIGHBOURS = (
    (-1, -1, 1.41421356), (-1, 0, 1.0), (-1, 1, 1.41421356),
    (0, -1, 1.0),                        (0, 1, 1.0),
    (1, -1, 1.41421356),  (1, 0, 1.0),  (1, 1, 1.41421356),
)


@dataclass
class ErosionReport:
    iterations: int
    flow_updates: int
    total_incision_m: float
    total_deposition_m: float
    max_incision_m: float


def _shift(a: np.ndarray, dj: int, di: int, fill: float) -> np.ndarray:
    out = np.full_like(a, fill)
    n_rows, n_cols = a.shape
    src_j = slice(max(0, -dj), n_rows - max(0, dj))
    dst_j = slice(max(0, dj), n_rows - max(0, -dj))
    src_i = slice(max(0, -di), n_cols - max(0, di))
    dst_i = slice(max(0, di), n_cols - max(0, -di))
    out[dst_j, dst_i] = a[src_j, src_i]
    return out


def slope_to_receiver(dem: np.ndarray, spacing_m: float) -> np.ndarray:
    """Pente (sans dimension) vers le voisin le plus bas."""
    best = np.zeros_like(dem, dtype=np.float32)
    for dj, di, dist in _NEIGHBOURS:
        neighbour = _shift(dem, dj, di, float(dem.max()))
        drop = (dem - neighbour) / np.float32(dist * spacing_m)
        np.maximum(best, drop, out=best)
    return best


def thermal_erosion(
    dem: np.ndarray, talus_angle_deg: float, spacing_m: float, iterations: int
) -> np.ndarray:
    """Au-dela de l'angle de talus, la matiere glisse vers les voisins plus bas."""
    if iterations <= 0:
        return dem
    max_drop = np.float32(np.tan(np.radians(talus_angle_deg)) * spacing_m)
    out = dem
    for _ in range(iterations):
        delta = np.zeros_like(out, dtype=np.float32)
        for dj, di, dist in _NEIGHBOURS:
            neighbour = _shift(out, dj, di, np.float32(np.inf))
            excess = out - neighbour - max_drop * np.float32(dist)
            np.maximum(excess, 0.0, out=excess)
            move = excess * np.float32(0.125 * 0.5)
            delta -= move
            delta += _shift(move, -dj, -di, 0.0)
        out = out + delta
    return out.astype(np.float32)


def hillslope_diffusion(dem: np.ndarray, kappa: float, iterations: int) -> np.ndarray:
    """Creep : lissage isotrope leger, qui arrondit les interfluves."""
    if iterations <= 0 or kappa <= 0.0:
        return dem
    out = dem
    k = np.float32(kappa / 6.0)
    for _ in range(iterations):
        lap = ndimage.convolve(out, _LAPLACE, mode="nearest")
        out = out + k * lap
    return out.astype(np.float32)


def run(
    elevation_m: np.ndarray,
    precip_mm: np.ndarray,
    geo,
    ero: dict,
    sea_level: float = 0.0,
    progress=None,
    fill_epsilon_m: float = 1e-4,
    arid: np.ndarray | None = None,
) -> tuple[np.ndarray, ErosionReport]:
    """Boucle d'erosion complete. Retourne (relief erode, rapport)."""
    dem = elevation_m.astype(np.float32, copy=True)
    spacing = geo.meters_per_pixel
    cell_area = spacing * spacing

    # Poids de pluie : 1.0 pour une cellule de pluviometrie mediane.
    median_p = float(np.median(precip_mm[elevation_m > sea_level])) if (elevation_m > sea_level).any() else 1.0
    rain_weight = (precip_mm / np.float32(max(median_p, 1e-6))).astype(np.float32)
    rain_weight = np.maximum(rain_weight, np.float32(0.02)) * np.float32(cell_area)

    iterations = int(ero["iterations"])
    every = max(1, int(ero["flowUpdateEvery"]))
    m = float(ero["areaExponent"])
    n_exp = float(ero["slopeExponent"])
    rate = float(ero["incisionRateM"])
    max_step = float(ero["maxIncisionPerStepM"])
    deposition = float(ero["depositionRate"])
    talus = float(ero["talusAngleDeg"])
    thermal_iters = int(ero["thermalIterationsPerStep"])
    kappa = float(ero["hillslopeDiffusion"])

    total_incision = 0.0
    total_deposition = 0.0
    max_incision = 0.0
    flow_updates = 0
    area_norm = None

    for it in range(iterations):
        if it % every == 0:
            flow = compute_flow(dem, rain_weight, sea_level, fill_epsilon_m, arid)
            flow_updates += 1
            acc_ref = float(np.percentile(flow.accumulation, 99.9))
            area_norm = (flow.accumulation / np.float32(max(acc_ref, 1e-9))).astype(np.float32)
            np.clip(area_norm, 0.0, 4.0, out=area_norm)
            if progress is not None:
                progress(it, iterations)

        land = dem > sea_level
        slope = slope_to_receiver(dem, spacing)
        slope_ref = float(np.percentile(slope[land], 90.0)) if land.any() else 1.0
        slope_norm = slope / np.float32(max(slope_ref, 1e-9))

        incision = np.float32(rate) * (area_norm ** np.float32(m)) * (slope_norm ** np.float32(n_exp))
        np.clip(incision, 0.0, max_step, out=incision)
        incision = np.where(land, incision, np.float32(0.0))

        eroded = float(incision.sum())
        dem = dem - incision

        # Depot : la matiere arrachee se redepose dans les fonds de vallee, la ou
        # la pente est faible et le drainage important (cones alluviaux, plaines).
        if deposition > 0.0 and eroded > 0.0:
            weight = np.clip(np.float32(1.0) - slope_norm, 0.0, 1.0) * area_norm
            weight = np.where(land, weight, np.float32(0.0))
            total_w = float(weight.sum())
            if total_w > 1e-9:
                deposit = weight * np.float32(eroded * deposition / total_w)
                dem = dem + deposit
                total_deposition += float(deposit.sum())

        dem = thermal_erosion(dem, talus, spacing, thermal_iters)
        total_incision += eroded
        max_incision = max(max_incision, float(incision.max()))

    dem = hillslope_diffusion(dem, kappa, int(ero.get("hillslopeIterations", 4)))

    return dem.astype(np.float32), ErosionReport(
        iterations=iterations,
        flow_updates=flow_updates,
        total_incision_m=total_incision,
        total_deposition_m=total_deposition,
        max_incision_m=max_incision,
    )
