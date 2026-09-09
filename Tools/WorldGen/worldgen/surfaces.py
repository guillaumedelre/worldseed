"""Etape 6a - des biomes aux couches de terrain.

Un materiau de Landscape porte une dizaine de couches ; il y a une vingtaine de
biomes. On ne mappe donc PAS un biome sur une couche. Chaque biome est une
RECETTE DE MELANGE des dix surfaces du materiau, et l'identite du biome vit
ailleurs (biome_index.png), ou elle pilote la vegetation.

Consequence pratique : ajouter un biome plus tard ne demande pas de toucher au
materiau, seulement d'ecrire une recette de plus.
"""

from __future__ import annotations

import numpy as np

from . import noise
from .config import Rules


def recipe_table(rules: Rules) -> np.ndarray:
    """Table [id_biome, couche] des poids de melange, prete pour l'indexation."""
    surf = rules["surfaces"]
    n_layers = len(surf["layers"])
    recipes = surf["recipes"]
    max_id = max(int(k) for k in recipes) + 1
    table = np.zeros((max(max_id, 256), n_layers), dtype=np.float32)
    for key, weights in recipes.items():
        row = np.asarray(weights, dtype=np.float32)
        total = float(row.sum())
        table[int(key)] = row / total if total > 0 else row
    return table


def build(
    rules: Rules,
    geo,
    biome_index: np.ndarray,
    slope_deg: np.ndarray,
    temp_max_c: np.ndarray,
    is_water: np.ndarray,
) -> np.ndarray:
    """Poids par couche, forme [n, n, n_couches], somme 1 sur chaque pixel."""
    surf = rules["surfaces"]
    layers = surf["layers"]
    n = geo.n
    idx_snow = layers.index("Snow")
    idx_stone = layers.index("Stone")

    weights = recipe_table(rules)[biome_index].astype(np.float32)

    # --- roche par la pente ---------------------------------------------------
    # Une falaise est de la roche quel que soit son climat. C'est ce terme qui
    # evite de voir de l'herbe collee sur une paroi verticale.
    rock = noise.smoothstep(
        float(surf["slopeRockStartDeg"]), float(surf["slopeRockFullDeg"]), slope_deg
    )
    rock = np.where(is_water, np.float32(0.0), rock)
    weights *= (np.float32(1.0) - rock)[..., None]
    weights[..., idx_stone] += rock

    # --- neige permanente -----------------------------------------------------
    # Basee sur le mois le PLUS CHAUD : la neige ne tient a l'annee que si l'ete
    # ne la fait pas fondre. Ultra Dynamic Sky ajoutera par-dessus la neige
    # saisonniere, qui elle va et vient.
    # On compare la temperature AUX SEUILS TELS QUELS. Passer -temp_max_c en
    # niant l'entree sans nier les seuils inversait la relation : avec
    # smoothstep(-1, -9, -T), une jungle a +27 degres donnait t = 3.25 -> borne a
    # 1, donc neige totale, tandis qu'un sol a -9 degres donnait t = -1.25 ->
    # borne a 0, donc aucune neige. La neige couvrait 82 % des terres emergees,
    # forets tropicales comprises. smoothstep gere des seuils decroissants.
    snow = noise.smoothstep(
        float(surf["snowStartC"]), float(surf["snowFullC"]), temp_max_c
    )
    snow = np.where(is_water, np.float32(0.0), snow)
    weights *= (np.float32(1.0) - snow)[..., None]
    weights[..., idx_snow] += snow

    # --- variation -----------------------------------------------------------
    # Sans cela chaque biome est un aplat parfait et le sol a l'air imprime.
    amount = float(surf.get("variationAmount", 0.0))
    if amount > 0.0:
        freq = float(surf.get("variationFrequency", 20.0))
        for k in range(weights.shape[2]):
            field = noise.fbm(n, freq, 3, rules.seed + 7001 + k * 131)
            weights[..., k] *= np.float32(1.0) + np.float32(amount) * field

    np.clip(weights, 0.0, None, out=weights)
    total = weights.sum(axis=2, keepdims=True)
    np.maximum(total, np.float32(1e-6), out=total)
    weights /= total
    return weights


def to_uint8(weights: np.ndarray) -> np.ndarray:
    """Quantifie en 0-255 pour l'import Unreal (une image 8 bits par couche)."""
    return np.clip(np.rint(weights * 255.0), 0, 255).astype(np.uint8)
