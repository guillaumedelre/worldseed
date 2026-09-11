"""Orchestration : des regles au dossier de sortie.

Ordre des etapes, et pourquoi :

  1. tectonique          relief brut, chaines orientees
  2. climat (passe 1)    sert a PONDERER l'erosion par la pluie
  3. erosion             vallees, talus, depots
  4. recalage marin      le ratio terres/mers redevient exact apres erosion
  5. climat (passe 2)    recalcule sur le relief FINAL : les ombres
                         pluviometriques doivent correspondre aux vallees
                         reellement creusees, pas au relief d'avant
  6. hydrologie          debits, rivieres, lacs
  7. biomes              Whittaker + surcharges
  8. surfaces            recettes de melange des couches
  9. export              PNG, JSON, manifeste, rapport
"""

from __future__ import annotations

import json
import time
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from . import biomes as biomes_mod
from . import climate as climate_mod
from . import erosion as erosion_mod
from . import export as export_mod
from . import hydrology as hydro_mod
from . import report as report_mod
from . import surfaces as surfaces_mod
from . import tectonics as tectonics_mod
from .config import Rules


@dataclass
class Outputs:
    directory: Path
    manifest: dict
    report: Path


def _log(verbose: bool, message: str, started: float) -> None:
    if verbose:
        print("[{:6.1f}s] {}".format(time.time() - started, message), flush=True)


def _zonal_profile(geo, dem, temp, precip, steps=13) -> list[dict]:
    """Profil par bandes de latitude.

    On moyenne sur une BANDE entiere, jamais sur une ligne unique : une seule
    ligne peut tomber sur une chaine de montagnes ou sur un detroit et donner un
    profil climatique faux, qui ferait echouer les controles a tort.
    """
    rows = []
    half = geo.lat_span_deg * 0.5
    land = dem > 0.0
    edges = np.linspace(-half, half, steps + 1)
    for k in range(steps):
        lo_lat, hi_lat = float(edges[k]), float(edges[k + 1])
        j0 = int(np.clip(geo.row_of_latitude(lo_lat), 0, geo.n - 1))
        j1 = int(np.clip(geo.row_of_latitude(hi_lat), 0, geo.n - 1))
        if j1 <= j0:
            j1 = min(geo.n - 1, j0 + 1)
        band_land = land[j0:j1]
        band_temp = temp[j0:j1]
        band_rain = precip[j0:j1]
        rows.append({
            "lat": (lo_lat + hi_lat) * 0.5,
            "tempC": float(band_temp[band_land].mean()) if band_land.any() else float(band_temp.mean()),
            "precipMm": float(np.median(band_rain[band_land])) if band_land.any() else 0.0,
            "landPct": float(band_land.mean() * 100.0),
        })
    return rows


def _checks(rules, geo, dem, climate, biome, rivers_json, zonal,
            weights=None, temp_max_c=None) -> list[list[str]]:
    """Controles automatiques : chaque ligne dit attendu / mesure."""
    out = []
    land = dem > 0.0

    target = float(rules.get("tectonics.landRatio")) * 100.0
    measured = float(land.mean() * 100.0)
    out.append(["Ratio terres / mers", "{:.0f} % +/- 2".format(target),
                "{:.1f} % {}".format(measured, "OK" if abs(measured - target) <= 2.5 else "ECART")])

    bad = sum(1 for r in rivers_json if r["mouth"] == "bord")
    endo = sum(1 for r in rivers_json if r["mouth"] == "endoreique")
    out.append(["Rivières sortant du monde", "0",
                "{} {}".format(bad, "OK" if bad == 0 else "ECART")])
    out.append(["Bassins endoréiques", "normal en zone aride", "{}".format(endo)])

    # La bande la plus humide doit etre l'equateur.
    # Controle durci : la ZCIT doit tomber a moins de 15 degres de l'equateur.
    wettest = max(zonal, key=lambda z: z["precipMm"])
    out.append(["Latitude la plus humide", "équateur, à ±15° près",
                "{:+.0f}° {}".format(wettest["lat"],
                                     "OK" if abs(wettest["lat"]) <= 15.0 else "ECART")])

    # Le pic equatorial doit dominer nettement la ceinture desertique. Sur Terre
    # le rapport vaut environ 8 ; en dessous de 2 la ZCIT n'existe pas vraiment.
    eq_band = min(zonal, key=lambda z: abs(z["lat"]))
    sub = [z["precipMm"] for z in zonal if 22.0 <= abs(z["lat"]) <= 38.0]
    if sub and max(sub) > 0.0:
        rapport = eq_band["precipMm"] / max(sum(sub) / len(sub), 1e-9)
        out.append(["Rapport pluie équateur / ±30°", "supérieur à 2",
                    "{:.1f} {}".format(rapport, "OK" if rapport > 2.0 else "ECART")])

    # La ceinture aride doit tomber entre 15 et 45 degres.
    sub = [z for z in zonal if 12.0 <= abs(z["lat"]) <= 50.0]
    driest = min(sub, key=lambda z: z["precipMm"]) if sub else None
    if driest:
        ok = 15.0 <= abs(driest["lat"]) <= 45.0
        out.append(["Ceinture désertique", "entre 15° et 45°",
                    "{:+.0f}° {}".format(driest["lat"], "OK" if ok else "ECART")])

    # Le gradient thermique doit etre monotone de l'equateur vers les poles.
    eq = min(zonal, key=lambda z: abs(z["lat"]))["tempC"]
    pole = min(z["tempC"] for z in zonal)
    out.append(["Gradient thermique", "équateur nettement plus chaud",
                "{:+.0f} vs {:+.0f} °C {}".format(eq, pole, "OK" if eq - pole > 20.0 else "ECART")])

    part_plage = biome.counts.get(rules["biomes"]["labels"][str(rules["biomes"]["ids"]["plage"])], 0.0)
    out.append(["Part de la plage sur les terres", "entre 1 % et 4 %",
                "{:.2f} % {}".format(part_plage,
                                     "OK" if 1.0 <= part_plage <= 4.0 else "ECART")])

    n_biomes = len(biome.counts)
    out.append(["Diversité de biomes", "au moins 8",
                "{} {}".format(n_biomes, "OK" if n_biomes >= 8 else "ECART")])

    walk = float((biome.slope_deg[land] < 25.0).mean() * 100.0)
    out.append(["Terrain praticable (moins de 25°)", "au moins 50 %",
                "{:.0f} % {}".format(walk, "OK" if walk >= 50.0 else "ECART")])

    # --- drainage ------------------------------------------------------------
    # Le controle "rivieres sortant du monde = 0" passait au vert pour la
    # mauvaise raison : rien ne sortait parce que rien n'allait nulle part. Ces
    # quatre lignes verifient que le reseau REJOINT la mer.
    hyd = rules["hydrology"]
    if rivers_json:
        vers_mer = sum(1 for r in rivers_json if r["mouth"] == "ocean")
        part = 100.0 * vers_mer / len(rivers_json)
        out.append(["Cours d'eau atteignant l'océan", "au moins 60 %",
                    "{:.0f} % ({}/{}) {}".format(part, vers_mer, len(rivers_json),
                                                 "OK" if part >= 60.0 else "ECART")])

        plus_long = max(r["lengthM"] for r in rivers_json)
        largeur_monde = float(rules.get("world.sizeKm")) * 1000.0
        part_long = 100.0 * plus_long / largeur_monde
        out.append(["Longueur du plus long cours d'eau", "au moins 30 % du monde",
                    "{:.0f} % ({:.1f} km) {}".format(part_long, plus_long / 1000.0,
                                                     "OK" if part_long >= 30.0 else "ECART")])

        seuil_aride = float(hyd["aridPrecipThresholdMm"])
        fautifs = sum(1 for r in rivers_json
                      if r["mouth"] == "endoreique"
                      and (r.get("mouthPrecipMm") or 0.0) >= seuil_aride)
        out.append(["Bassins endoréiques hors zone aride",
                    "0 (au-dessus de {:.0f} mm)".format(seuil_aride),
                    "{} {}".format(fautifs, "OK" if fautifs == 0 else "ECART")])

    part_lacs = 100.0 * float(biome.lake_mask[land].mean()) if land.any() else 0.0
    out.append(["Part des lacs sur les terres", "moins de 5 %",
                "{:.1f} % {}".format(part_lacs, "OK" if part_lacs < 5.0 else "ECART")])

    # --- couches de peinture -------------------------------------------------
    # Ces deux controles portent sur ce qu'Unreal PEINT reellement, pas sur la
    # carte des biomes. Ils manquaient, et un signe inverse dans le terme de
    # neige a pu couvrir 82 % des terres emergees, forets tropicales comprises,
    # sans qu'aucune ligne du rapport ne s'en emeuve.
    if weights is not None:
        noms = rules["surfaces"]["layers"]
        dominante = weights.argmax(axis=2)
        idx_snow = noms.index("Snow")
        chaud = land & (temp_max_c > 0.0)
        if chaud.any():
            fautif = float((dominante[chaud] == idx_snow).mean() * 100.0)
            out.append(["Neige sur terrain hors gel", "0 % des terres au-dessus de 0 °C",
                        "{:.1f} % {}".format(fautif, "OK" if fautif < 1.0 else "ECART")])
        jamais = [n for k, n in enumerate(noms)
                  if not (dominante[land] == k).any()]
        out.append(["Couches jamais dominantes", "aucune",
                    "{} {}".format(", ".join(jamais) if jamais else "aucune",
                                   "OK" if not jamais else "ECART")])

    return out


def run(
    rules: Rules,
    out_dir: Path,
    verbose: bool = True,
    write_outputs: bool = True,
) -> Outputs:
    started = time.time()
    geo = rules.sim_geometry
    geo_out = rules.out_geometry
    out_dir.mkdir(parents=True, exist_ok=True)

    # 1. tectonique -----------------------------------------------------------
    tec = tectonics_mod.generate(rules, geo)
    _log(verbose, "tectonique : {} plaques, relief {:.0f}..{:.0f} m".format(
        rules.get("tectonics.plateCount"), tec.elevation_m.min(), tec.elevation_m.max()), started)

    # 2. climat, premiere passe ------------------------------------------------
    clim = climate_mod.generate(rules, geo, tec.elevation_m)
    _log(verbose, "climat (passe 1) : pluie pour ponderer l'erosion", started)

    # 3. erosion ---------------------------------------------------------------
    fill_eps = float(rules.get("hydrology.fillEpsilonM"))
    # Un bassin ferme n'est tenable qu'en climat aride : ailleurs on lui perce un
    # exutoire plutot que de laisser un faux endoreisme.
    aride = clim.precip_mm < float(rules.get("hydrology.aridPrecipThresholdMm"))
    dem, ero_report = erosion_mod.run(
        tec.elevation_m, clim.precip_mm, geo, rules["erosion"],
        fill_epsilon_m=fill_eps, arid=aride,
    )
    _log(verbose, "erosion : {} iterations, {} recalculs d'ecoulement".format(
        ero_report.iterations, ero_report.flow_updates), started)

    # 4. recalage du niveau marin ---------------------------------------------
    # L'erosion deplace de la matiere : sans ce recalage, le ratio terres/mers
    # demande dans les regles ne serait plus respecte dans le monde livre.
    land_ratio = float(rules.get("tectonics.landRatio"))
    shift = float(np.quantile(dem, 1.0 - land_ratio))
    dem = (dem - np.float32(shift)).astype(np.float32)
    dem = np.clip(dem, float(rules.get("world.minElevationM")),
                  float(rules.get("world.maxElevationM"))).astype(np.float32)
    _log(verbose, "niveau marin recale de {:+.1f} m : terres {:.1f} %".format(
        -shift, (dem > 0).mean() * 100.0), started)

    # 5. climat, seconde passe -------------------------------------------------
    clim = climate_mod.generate(rules, geo, dem)
    _log(verbose, "climat (passe 2) sur le relief final", started)

    # 6. hydrologie ------------------------------------------------------------
    hyd = rules["hydrology"]
    weights = hydro_mod.discharge_weights(
        clim.precip_mm, geo, float(hyd.get("runoffCoefficient", 0.35))
    )
    flow = hydro_mod.compute_flow(dem, weights, 0.0, fill_eps, aride)
    rivers = hydro_mod.extract_rivers(flow, dem, geo, hyd)
    lakes = hydro_mod.extract_lakes(flow, dem, geo, hyd)
    _log(verbose, "hydrologie : {} rivieres, {} lacs, debit max {:.3f} m3/s".format(
        len(rivers), len(lakes), float(flow.accumulation.max())), started)

    # 7. biomes ----------------------------------------------------------------
    biome = biomes_mod.classify(
        rules, geo, dem, clim.temp_mean_c, clim.temp_max_c, clim.precip_mm,
        flow.lake_depth_m, flow.accumulation,
    )
    _log(verbose, "biomes : {} classes presentes".format(len(biome.counts)), started)

    # 8. surfaces --------------------------------------------------------------
    weights_layers = surfaces_mod.build(
        rules, geo, biome.index, biome.slope_deg, clim.temp_max_c, biome.is_water
    )
    _log(verbose, "surfaces : {} couches".format(weights_layers.shape[2]), started)

    # 9. export ----------------------------------------------------------------
    # Le relief de SORTIE est calcule ICI et non plus a l'ecriture : le trait de
    # cote des lacs doit etre trace sur le relief que voit Unreal, detail fractal
    # compris. Calcule avant le detail, il ne tombe plus sur la ligne de rivage
    # et l'eau se termine en mur vertical au-dessus du sol.
    # Le niveau d'eau local (0 en mer, le niveau propre a chaque lac dans sa
    # cuvette) dit au detail fractal ou s'effacer : sans lui, un lac perche a
    # 136 m recoit le bruit a pleine amplitude et ses berges se herissent.
    niveau_eau = export_mod.lake_level_field(
        lakes, biome.lake_mask, geo, geo_out,
        portee_m=float(rules.get("world.detailLakeFadeM", 60.0)))
    # Meme raison pour les rivieres : le detail depose des bosses dans le fond
    # de vallee que l'ecoulement vient de creuser, et comme la surface d'eau doit
    # decroitre vers l'aval, chaque bosse enterre tout le troncon qui suit.
    amorti = export_mod.river_corridor_damping(
        biome.river_mask, geo, geo_out,
        portee_m=float(rules.get("world.detailRiverFadeM", 30.0)))
    big_dem = export_mod.upsample_heightmap(dem, geo_out.n, rules,
                                            water_level=niveau_eau,
                                            detail_damp=amorti)
    del niveau_eau, amorti
    _log(verbose, "relief de sortie {0}x{0} : detail fractal ajoute, efface au bord de l'eau".format(
        geo_out.n), started)

    rivers_json = export_mod.rivers_to_json(rivers, dem, geo,
                                            precip_mm=clim.precip_mm,
                                            geo_out=geo_out, dem_out=big_dem, hyd=hyd)
    lakes_json = export_mod.lakes_to_json(
        lakes, geo, geo_out=geo_out, dem_out=big_dem,
        lake_mask=biome.lake_mask, hyd=hyd)
    zonal = _zonal_profile(geo, dem, clim.temp_mean_c, clim.precip_mm)

    land = dem > 0.0
    stats = {
        "sizeKm": float(rules.get("world.sizeKm")),
        "simResolution": int(geo.n),
        "outResolution": int(geo_out.n),
        "metresPerPixel": float(geo_out.meters_per_pixel),
        "landPct": float(land.mean() * 100.0),
        "elevMin": float(dem.min()),
        "elevMax": float(dem.max()),
        "tempMin": float(clim.temp_mean_c.min()),
        "tempMax": float(clim.temp_mean_c.max()),
        "precipMedian": float(np.median(clim.precip_mm[land])) if land.any() else 0.0,
        "walkablePct": float((biome.slope_deg[land] < 25.0).mean() * 100.0) if land.any() else 0.0,
        "biomeCount": len(biome.counts),
        "biomeShare": biome.counts,
        "zonal": zonal,
        "riverCount": len(rivers_json),
        "lakeCount": len(lakes_json),
        "erosion": {
            "iterations": ero_report.iterations,
            "flowUpdates": ero_report.flow_updates,
            "maxIncisionM": round(ero_report.max_incision_m, 2),
        },
        "climateRanges": {
            "tempMinC": float(clim.temp_mean_c.min()),
            "tempMaxC": float(clim.temp_mean_c.max()),
            "precipMinMm": 0.0,
            "precipMaxMm": float(rules.get("precipitation.maxPrecipMm")),
        },
    }
    stats["checks"] = _checks(rules, geo, dem, clim, biome, rivers_json, zonal,
                              weights_layers, clim.temp_max_c)
    stats["durationS"] = time.time() - started

    manifest = export_mod.build_manifest(rules, geo_out, stats)
    report_path = out_dir / "world_report.html"

    if write_outputs:
        _write_all(rules, geo, geo_out, out_dir, dem, clim, biome,
                   weights_layers, rivers_json, lakes_json, manifest, verbose, started,
                   big_dem=big_dem)
        report_mod.write(report_path, rules, geo, dem, clim.temp_mean_c,
                         clim.precip_mm, biome.index, rivers_json, lakes_json, stats)
        _log(verbose, "rapport ecrit : {}".format(report_path), started)

    stats["durationS"] = time.time() - started
    manifest["stats"] = stats
    if write_outputs:
        (out_dir / "manifest.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False), encoding="utf-8"
        )

    return Outputs(directory=out_dir, manifest=manifest, report=report_path)


def _write_all(rules, geo, geo_out, out_dir, dem, clim, biome, weights_layers,
               rivers_json, lakes_json, manifest, verbose, started,
               big_dem=None) -> None:
    out_n = geo_out.n

    if big_dem is None:
        big_dem = export_mod.upsample_heightmap(dem, out_n, rules)
    export_mod.write_heightmap_png(
        big_dem, out_dir / "height_16bit.png",
        float(rules.get("world.minElevationM")), float(rules.get("world.maxElevationM")),
    )
    _log(verbose, "heightmap {}x{} ecrite".format(out_n, out_n), started)
    del big_dem

    layer_keys = rules["surfaces"]["layers"]
    for k, key in enumerate(layer_keys):
        layer = export_mod.resample(weights_layers[..., k], out_n, order=1)
        export_mod.write_gray_png(layer, out_dir / "layer_{}.png".format(key))
        del layer
    _log(verbose, "{} weightmaps ecrites".format(len(layer_keys)), started)

    export_mod.write_index_png(
        export_mod.resample(biome.index, out_n, order=0).astype(np.uint8),
        out_dir / "biome_index.png",
    )
    export_mod.write_colour_png(
        export_mod.biome_rgb(export_mod.resample(biome.index, out_n, order=0).astype(np.uint8), rules),
        out_dir / "biome_debug_rgb.png",
    )

    if rules.get("export.writeClimateTextures", True):
        rng = manifest["climateRanges"]
        t01 = (clim.temp_mean_c - rng["tempMinC"]) / max(rng["tempMaxC"] - rng["tempMinC"], 1e-6)
        p01 = clim.precip_mm / max(rng["precipMaxMm"], 1e-6)
        export_mod.write_gray_png(export_mod.resample(np.clip(t01, 0, 1), out_n), out_dir / "climate_temp.png")
        export_mod.write_gray_png(export_mod.resample(np.clip(p01, 0, 1), out_n), out_dir / "climate_rain.png")

    (out_dir / "rivers.json").write_text(
        json.dumps({"rivers": rivers_json}, indent=1, ensure_ascii=False), encoding="utf-8")
    (out_dir / "lakes.json").write_text(
        json.dumps({"lakes": lakes_json}, indent=1, ensure_ascii=False), encoding="utf-8")
    rules.dump(out_dir / "world_rules_used.json")
