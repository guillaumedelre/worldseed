"""Banc de diagnostic des precipitations : ou le maximum equatorial se perd-il ?

    python diag_climat.py [dossier]

Le profil zonal du monde de reference montre une bande equatoriale seche (679 mm)
alors que les moyennes latitudes recoivent deux a trois fois plus (2092 mm a
-42 degres). Le terme de convergence de la ZCIT existe pourtant dans le code.

Ce banc rejoue l'advection d'humidite en instrumentant chaque facteur du produit
`pluie = humidite x taux`, puis les compare bande de latitude par bande de
latitude. Il ne corrige rien : il dit ou le signal disparait.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

from worldgen import climate as clim_mod
from worldgen import noise
from worldgen.config import Rules

Image.MAX_IMAGE_PIXELS = None


def charge_relief(src: Path, n_sim: int) -> np.ndarray:
    m = json.loads((src / "manifest.json").read_text(encoding="utf-8"))
    lo, hi = float(m["world"]["minElevationM"]), float(m["world"]["maxElevationM"])
    h16 = np.asarray(Image.open(src / "height_16bit.png")).astype(np.float32)
    dem = h16 / 65535.0 * (hi - lo) + lo
    return ndimage.zoom(dem, n_sim / dem.shape[0], order=1).astype(np.float32)[:n_sim, :n_sim]


def advection_instrumentee(rules: Rules, geo, elevation_m: np.ndarray) -> dict:
    """Copie fidele de _advect_moisture, mais qui conserve les champs intermediaires."""
    prec = rules["precipitation"]
    n = geo.n
    is_water = elevation_m <= 0.0

    t_sea = clim_mod._sea_level_temperature(geo, rules["temperature"])
    lapse = np.float32(rules["temperature"]["lapseRateCPerKm"])
    temp_c = (t_sea - lapse * np.maximum(elevation_m, 0.0) / np.float32(1000.0)).astype(np.float32)
    u, v = clim_mod.wind_field(geo, rules)

    t_ref = np.float32(prec["saturationRefTempC"])
    t_scale = np.float32(max(float(prec["saturationScaleC"]), 1e-3))
    h_max = np.clip(np.exp((temp_c - t_ref) / t_scale), 0.02, 4.0).astype(np.float32)

    land_elev = np.maximum(elevation_m, np.float32(0.0))
    dh_dy, dh_dx = clim_mod._gradients(land_elev, geo.meters_per_pixel)
    uplift = np.maximum((u * dh_dx + v * dh_dy).astype(np.float32), 0.0)
    ref = float(np.percentile(uplift, 99.0))
    uplift_norm = np.clip(uplift / max(ref, 1e-6), 0.0, 2.0).astype(np.float32)

    cells = float(prec.get("cellsPerHemisphere", 3.0))
    abs_lat = np.abs(geo.latitude_grid()).astype(np.float32)
    half = np.float32(geo.lat_span_deg * 0.5)
    omega = np.cos(np.float32(cells * np.pi) * abs_lat / half).astype(np.float32)
    wobble = float(prec.get("cellWobble", 0.0))
    if wobble > 0.0:
        omega = omega + np.float32(wobble) * noise.fbm(
            n, float(prec.get("cellWobbleFrequency", 2.4)), 4, rules.seed + 6151)
        np.clip(omega, -1.0, 1.0, out=omega)
    conv = np.maximum(omega, 0.0)
    subs = np.maximum(-omega, 0.0)
    front = np.float32(prec.get("polarFrontStrength", 0.45))
    conv = conv * (front + (np.float32(1.0) - front) * (np.float32(1.0) - abs_lat / half))

    rate = np.float32(prec["baseRainRate"]) * (
        np.float32(1.0)
        + np.float32(prec["orographicFactor"]) * uplift_norm
        + np.float32(prec["convergenceFactor"]) * conv)
    rate = rate * np.maximum(np.float32(1.0) - np.float32(prec["subsidenceFactor"]) * subs,
                             np.float32(0.05))
    np.clip(rate, 0.0, 0.9, out=rate)

    evap = np.float32(prec["evaporationRate"])
    step = np.float32(prec["advectionStepPx"])
    sweeps = int(prec["advectionSweeps"])
    jj, ii = np.meshgrid(np.arange(n, dtype=np.float32), np.arange(n, dtype=np.float32),
                         indexing="ij")
    coords = np.stack((jj - v * step, ii - u * step))

    humidity = np.zeros((n, n), dtype=np.float32)
    precip = np.zeros((n, n), dtype=np.float32)
    humidite_moy = np.zeros((n, n), dtype=np.float32)
    damp = np.float32(0.35)
    for _ in range(sweeps):
        humidity = ndimage.map_coordinates(humidity, coords, order=1, mode="nearest").astype(np.float32)
        humidity = np.where(is_water, humidity + evap * (h_max - humidity), humidity)
        np.clip(humidity, 0.0, None, out=humidity)
        np.minimum(humidity, h_max, out=humidity)
        d = humidity * rate
        d = np.where(is_water, d * damp, d)
        humidity -= d
        precip += d
        humidite_moy += humidity
    humidite_moy /= np.float32(sweeps)

    return {"h_max": h_max, "humidite": humidite_moy, "taux": rate, "conv": conv,
            "subs": subs, "uplift": uplift_norm, "precip_brut": precip,
            "is_water": is_water, "temp": temp_c}


def profil(geo, champs: dict, bandes: int = 13) -> None:
    half = geo.lat_span_deg * 0.5
    edges = np.linspace(-half, half, bandes + 1)
    terre = ~champs["is_water"]
    print("%7s %7s %9s %9s %9s %9s %10s %10s" % (
        "lat", "terre%", "h_max", "humidite", "taux", "conv", "pluie brute", "pluie/hmax"))
    for k in range(bandes):
        lo, hi = float(edges[k]), float(edges[k + 1])
        j0 = int(np.clip(geo.row_of_latitude(lo), 0, geo.n - 1))
        j1 = max(int(np.clip(geo.row_of_latitude(hi), 0, geo.n - 1)), j0 + 1)
        sel = terre[j0:j1]
        if not sel.any():
            sel = np.ones_like(terre[j0:j1])
        def moy(nom):
            return float(np.median(champs[nom][j0:j1][sel]))
        pb, hm = moy("precip_brut"), moy("h_max")
        print("%+7.0f %6.1f%% %9.3f %9.4f %9.5f %9.3f %10.3f %10.2f" % (
            (lo + hi) * 0.5, 100.0 * terre[j0:j1].mean(), hm, moy("humidite"),
            moy("taux"), moy("conv"), pb, pb / max(hm, 1e-6)))


def main(argv: list[str]) -> int:
    here = Path(__file__).resolve().parent
    src = Path(argv[0]) if argv else here.parent.parent / "Saved" / "WorldGen" / "20260909"
    rules = Rules.load(here / "rules" / "world_rules.json")
    geo = rules.sim_geometry
    dem = charge_relief(src, geo.n)
    champs = advection_instrumentee(rules, geo, dem)
    print("Profils zonaux, medianes sur les TERRES de chaque bande")
    print()
    profil(geo, champs)
    print()
    print("Lecture : la pluie vaut humidite x taux. Si le taux est fort a l'equateur")
    print("mais l'humidite faible, le probleme est l'ALIMENTATION, pas la convergence.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
