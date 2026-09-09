"""Bruits deterministes, vectorises numpy.

Perlin par gradients sur reseau entier, hache a partir de la graine : aucune
table pre-calculee, donc aucun probleme de periodicite, et le meme couple
(graine, frequence) redonne toujours exactement le meme champ.
"""

from __future__ import annotations

import numpy as np

_U32 = np.uint32
_TAU_OVER_2P32 = np.float32(2.0 * np.pi / 4294967296.0)


def _hash_angle(ix: np.ndarray, iy: np.ndarray, seed: int) -> np.ndarray:
    """Angle de gradient pseudo-aleatoire, stable, pour chaque noeud du reseau."""
    with np.errstate(over="ignore"):
        h = (ix.astype(_U32) * _U32(374761393)) + (iy.astype(_U32) * _U32(668265263))
        h = h + _U32(seed & 0xFFFFFFFF)
        h ^= h >> _U32(13)
        h = h * _U32(1274126177)
        h ^= h >> _U32(16)
        h = h * _U32(2246822519)
        h ^= h >> _U32(13)
    return h.astype(np.float32) * _TAU_OVER_2P32


def _fade(t: np.ndarray) -> np.ndarray:
    """Quintique de Perlin : 6t^5 - 15t^4 + 10t^3 (derivees premiere et seconde nulles)."""
    return t * t * t * (t * (t * np.float32(6.0) - np.float32(15.0)) + np.float32(10.0))


def perlin(x: np.ndarray, y: np.ndarray, seed: int) -> np.ndarray:
    """Bruit de Perlin 2D en coordonnees deja mises a l'echelle de la frequence.

    Retourne des valeurs approximativement dans [-1, 1].
    """
    x = np.asarray(x, dtype=np.float32)
    y = np.asarray(y, dtype=np.float32)

    x0 = np.floor(x)
    y0 = np.floor(y)
    fx = x - x0
    fy = y - y0
    ix0 = x0.astype(np.int64)
    iy0 = y0.astype(np.int64)
    ix1 = ix0 + 1
    iy1 = iy0 + 1

    def corner(ix: np.ndarray, iy: np.ndarray, dx: np.ndarray, dy: np.ndarray) -> np.ndarray:
        ang = _hash_angle(ix, iy, seed)
        return np.cos(ang) * dx + np.sin(ang) * dy

    n00 = corner(ix0, iy0, fx, fy)
    n10 = corner(ix1, iy0, fx - np.float32(1.0), fy)
    n01 = corner(ix0, iy1, fx, fy - np.float32(1.0))
    n11 = corner(ix1, iy1, fx - np.float32(1.0), fy - np.float32(1.0))

    u = _fade(fx)
    v = _fade(fy)
    nx0 = n00 + u * (n10 - n00)
    nx1 = n01 + u * (n11 - n01)
    return (nx0 + v * (nx1 - nx0)) * np.float32(1.4142135)


def _unit_grid(n: int) -> tuple[np.ndarray, np.ndarray]:
    """Grille normalisee [0, 1] x [0, 1], indexee [j, i] (j = sud->nord)."""
    a = np.linspace(0.0, 1.0, n, dtype=np.float32)
    return np.meshgrid(a, a, indexing="ij")


def fbm(
    n: int,
    frequency: float,
    octaves: int,
    seed: int,
    lacunarity: float = 2.0,
    gain: float = 0.5,
    grid: tuple[np.ndarray, np.ndarray] | None = None,
) -> np.ndarray:
    """Somme fractale de bruits de Perlin, normalisee dans [-1, 1] environ."""
    gy, gx = _unit_grid(n) if grid is None else grid
    total = np.zeros_like(gx, dtype=np.float32)
    amp = np.float32(1.0)
    norm = np.float32(0.0)
    freq = np.float32(frequency)
    for octave in range(octaves):
        total += amp * perlin(gx * freq, gy * freq, seed + octave * 7919)
        norm += amp
        amp *= np.float32(gain)
        freq *= np.float32(lacunarity)
    return total / max(norm, np.float32(1e-6))


def ridged(
    n: int,
    frequency: float,
    octaves: int,
    seed: int,
    lacunarity: float = 2.0,
    gain: float = 0.5,
    grid: tuple[np.ndarray, np.ndarray] | None = None,
) -> np.ndarray:
    """Bruit a cretes : cree des aretes nettes, utile pour les chaines de montagnes.

    Retourne des valeurs dans [0, 1].
    """
    gy, gx = _unit_grid(n) if grid is None else grid
    total = np.zeros_like(gx, dtype=np.float32)
    amp = np.float32(1.0)
    norm = np.float32(0.0)
    freq = np.float32(frequency)
    for octave in range(octaves):
        v = np.float32(1.0) - np.abs(perlin(gx * freq, gy * freq, seed + octave * 6791))
        total += amp * (v * v)
        norm += amp
        amp *= np.float32(gain)
        freq *= np.float32(lacunarity)
    return total / max(norm, np.float32(1e-6))


def domain_warped_fbm(
    n: int,
    frequency: float,
    octaves: int,
    seed: int,
    warp_strength: float,
    warp_frequency: float,
    lacunarity: float = 2.0,
    gain: float = 0.5,
) -> np.ndarray:
    """fBm dont les coordonnees sont deplacees par un autre fBm.

    C'est ce qui casse l'aspect "patates regulieres" du bruit fractal brut et
    donne des cotes decoupees et des vallees sinueuses.
    """
    gy, gx = _unit_grid(n)
    wx = fbm(n, warp_frequency, 4, seed + 104729, lacunarity, gain, grid=(gy, gx))
    wy = fbm(n, warp_frequency, 4, seed + 104743, lacunarity, gain, grid=(gy, gx))
    s = np.float32(warp_strength)
    return fbm(n, frequency, octaves, seed, lacunarity, gain,
               grid=(gy + s * wy, gx + s * wx))


def normalize01(a: np.ndarray) -> np.ndarray:
    """Ramene un champ dans [0, 1] (constante -> 0.5)."""
    lo = float(a.min())
    hi = float(a.max())
    if hi - lo < 1e-12:
        return np.full_like(a, 0.5, dtype=np.float32)
    return ((a - lo) / (hi - lo)).astype(np.float32)


def smoothstep(edge0: float, edge1: float, x: np.ndarray) -> np.ndarray:
    """Interpolation lisse classique, bornee [0, 1]."""
    if abs(edge1 - edge0) < 1e-12:
        return (x >= edge1).astype(np.float32)
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0).astype(np.float32)
    return t * t * (np.float32(3.0) - np.float32(2.0) * t)
