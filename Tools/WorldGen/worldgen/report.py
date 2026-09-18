"""Planche de releve HTML : c'est ici que le monde se juge, avant tout import.

Le document est ecrit en fragment (titre, styles, contenu) sans balises
englobantes : il s'ouvre tel quel dans un navigateur et se publie tel quel comme
Artifact.
"""

from __future__ import annotations

from pathlib import Path
from string import Template

import numpy as np

from .config import Rules
from .export import (
    _RAIN_STOPS,
    _TEMP_STOPS,
    _ramp,
    _shrink,
    _to_png_b64,
    biome_rgb,
    hillshade,
)


_LAND_STOPS = [
    (0.00, (18, 46, 92)), (0.30, (33, 82, 133)), (0.415, (70, 133, 178)),
    (0.435, (216, 203, 166)), (0.50, (105, 143, 86)), (0.66, (144, 138, 90)),
    (0.82, (130, 122, 114)), (1.00, (247, 249, 252)),
]

# Paralleles remarquables : couleur, style, et le fait qu'ils portent un sens
# geographique precis (ce ne sont pas des reperes decoratifs).
_MARKS = (
    ("Pôle nord", "half", "solid", "polar"),
    ("Cercle polaire arctique", "polar", "dashed", "polar"),
    ("Tropique du Cancer", "tropic", "dashed", "tropic"),
    ("Équateur", "zero", "equator", "equator"),
    ("Tropique du Capricorne", "-tropic", "dashed", "tropic"),
    ("Cercle polaire antarctique", "-polar", "dashed", "polar"),
    ("Pôle sud", "-half", "solid", "polar"),
)


def _graticule(geo) -> str:
    half = geo.lat_span_deg * 0.5
    values = {"half": half, "-half": -half, "polar": geo.polar_circle_deg,
              "-polar": -geo.polar_circle_deg, "tropic": geo.tropic_deg,
              "-tropic": -geo.tropic_deg, "zero": 0.0}
    out = []
    for label, key, kind, tone in _MARKS:
        lat = values[key]
        y = (0.5 - lat / geo.lat_span_deg) * 1000.0
        if y < 4.0 or y > 996.0:
            continue
        dash = ' stroke-dasharray="8 6"' if kind == "dashed" else ""
        width = 2.6 if kind == "equator" else 1.5
        out.append(
            '<line x1="0" y1="{y:.1f}" x2="1000" y2="{y:.1f}" class="g-{t}" '
            'stroke-width="{w}"{d}/>'
            '<text x="12" y="{ty:.1f}" class="g-{t} lbl">{lab}</text>'.format(
                y=y, ty=y - 8.0, t=tone, w=width, d=dash, lab=label)
        )
    return "".join(out)


def _plate(number: str, title: str, note: str, b64: str, geo, overlay: str = "") -> str:
    return (
        '<figure class="plate">'
        '<div class="frame">'
        '<img src="data:image/png;base64,{b}" alt="{t}"/>'
        '<svg viewBox="0 0 1000 1000" preserveAspectRatio="none">{g}{o}</svg>'
        "</div>"
        '<figcaption><span class="pl-n">{n}</span>'
        '<span class="pl-t">{t}</span><span class="pl-d">{d}</span></figcaption>'
        "</figure>"
    ).format(n=number, t=title, d=note, b=b64, g=_graticule(geo), o=overlay)


# ------------------------------------------------------------------ graphiques


def _profile_chart(zonal: list[dict], geo) -> str:
    """Profil zonal : temperature et pluie, deux graphiques a echelle propre.

    Jamais un double axe : deux mesures d'unites differentes se lisent cote a
    cote, sur des echelles distinctes, avec l'axe des latitudes en commun --
    oriente nord en haut, comme les planches.
    """
    rows = sorted(zonal, key=lambda z: -z["lat"])
    n = len(rows)
    row_h = 26.0
    top, bottom = 30.0, 22.0
    height = top + n * row_h + bottom
    label_w = 62.0
    plot_w = 236.0

    temps = [r["tempC"] for r in rows]
    rains = [r["precipMm"] for r in rows]
    t_span = max(abs(min(temps)), abs(max(temps)), 1.0)
    r_max = max(max(rains), 1.0)

    def bar_geometry(y: float) -> tuple[float, float]:
        return y + 5.0, row_h - 10.0

    parts = ['<svg class="profile" viewBox="0 0 {w} {h}" role="img" '
             'aria-label="Profil zonal de température et de précipitations">'.format(
                 w=label_w + plot_w * 2 + 58, h=height)]

    # -- colonne des latitudes
    for k, r in enumerate(rows):
        y = top + k * row_h
        parts.append(
            '<text x="{x}" y="{y:.1f}" class="ax lat">{v:+.0f}&#176;</text>'.format(
                x=label_w - 10, y=y + row_h * 0.5 + 4, v=r["lat"]))

    # -- temperature : divergente autour de 0 degC
    t_x0 = label_w
    t_zero = t_x0 + plot_w * 0.5
    parts.append('<text x="{x:.1f}" y="16" class="ax hd">Température moyenne (&#176;C)</text>'
                 .format(x=t_x0))
    parts.append('<line x1="{x:.1f}" y1="{a:.1f}" x2="{x:.1f}" y2="{b:.1f}" class="axis"/>'
                 .format(x=t_zero, a=top - 4, b=top + n * row_h + 2))
    for k, r in enumerate(rows):
        y, h = bar_geometry(top + k * row_h)
        w = abs(r["tempC"]) / t_span * (plot_w * 0.5 - 26.0)
        warm = r["tempC"] >= 0.0
        x = t_zero if warm else t_zero - w
        parts.append('<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" '
                     'rx="3" class="{c}"/>'.format(
                         x=x, y=y, w=max(w, 1.5), h=h, c="warm" if warm else "cool"))
        tx = x + w + 6 if warm else x - 6
        parts.append('<text x="{tx:.1f}" y="{ty:.1f}" class="val" '
                     'text-anchor="{a}">{v:+.0f}</text>'.format(
                         tx=tx, ty=y + h * 0.72, a="start" if warm else "end", v=r["tempC"]))

    # -- precipitations : sequentielle, une seule teinte
    p_x0 = label_w + plot_w + 40
    parts.append('<text x="{x:.1f}" y="16" class="ax hd">Précipitations (mm/an)</text>'
                 .format(x=p_x0))
    parts.append('<line x1="{x:.1f}" y1="{a:.1f}" x2="{x:.1f}" y2="{b:.1f}" class="axis"/>'
                 .format(x=p_x0, a=top - 4, b=top + n * row_h + 2))
    for k, r in enumerate(rows):
        y, h = bar_geometry(top + k * row_h)
        w = r["precipMm"] / r_max * (plot_w - 46.0)
        parts.append('<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" '
                     'rx="3" class="rain"/>'.format(x=p_x0, y=y, w=max(w, 1.5), h=h))
        parts.append('<text x="{tx:.1f}" y="{ty:.1f}" class="val">{v:.0f}</text>'.format(
            tx=p_x0 + max(w, 1.5) + 6, ty=y + h * 0.72, v=r["precipMm"]))

    parts.append("</svg>")
    return "".join(parts)


def _checks_block(checks: list[list[str]]) -> str:
    out = []
    for label, expected, measured in checks:
        state = "warn" if "ECART" in measured else ("pass" if "OK" in measured else "info")
        shown = measured.replace(" OK", "").replace(" ECART", "")
        mark = {"pass": "conforme", "warn": "écart", "info": "relevé"}[state]
        out.append(
            '<li class="check {s}"><span class="ck-l">{l}</span>'
            '<span class="ck-e">{e}</span>'
            '<span class="ck-m">{m}</span>'
            '<span class="chip {s}">{k}</span></li>'.format(
                s=state, l=label, e=expected, m=shown, k=mark))
    return '<ul class="checks">' + "".join(out) + "</ul>"


def _biome_block(share: dict[str, float], rules: Rules) -> str:
    palette = rules["biomes"]["debugColors"]
    labels = rules["biomes"]["labels"]
    by_name = {labels[k]: palette[k] for k in labels}
    top = max(share.values()) if share else 1.0
    out = []
    for name, pct in share.items():
        rgb = by_name.get(name, [140, 140, 140])
        out.append(
            '<li><span class="sw" style="background:rgb({r},{g},{b})"></span>'
            '<span class="bn">{n}</span>'
            '<span class="bt"><span class="bf" style="width:{w:.1f}%"></span></span>'
            '<span class="bp">{p:.1f}&#8201;%</span></li>'.format(
                r=rgb[0], g=rgb[1], b=rgb[2], n=name, w=pct / top * 100.0, p=pct))
    return '<ul class="biomes">' + "".join(out) + "</ul>"


# --------------------------------------------------------------------- ecriture


def write(
    path: Path,
    rules: Rules,
    geo,
    dem: np.ndarray,
    temp_c: np.ndarray,
    precip_mm: np.ndarray,
    biome_index: np.ndarray,
    stats: dict,
) -> None:
    max_px = int(rules.get("export.reportMaxPx", 1200))

    def flip(a: np.ndarray) -> np.ndarray:
        return a[::-1]

    small_dem = _shrink(dem, max_px, order=1)
    shade = hillshade(small_dem, geo.size_m / max(small_dem.shape[0] - 1, 1))

    lo = float(rules.get("world.minElevationM"))
    hi = float(rules.get("world.maxElevationM"))
    relief = _ramp(small_dem, lo, hi, _LAND_STOPS).astype(np.float32)
    relief *= (0.45 + 0.55 * shade)[..., None]
    relief_b64 = _to_png_b64(flip(np.clip(relief, 0, 255).astype(np.uint8)), "RGB")

    temp_b64 = _to_png_b64(flip(_ramp(_shrink(temp_c, max_px), -30.0, 30.0, _TEMP_STOPS)), "RGB")
    rain_b64 = _to_png_b64(flip(_ramp(_shrink(precip_mm, max_px), 0.0, 2500.0, _RAIN_STOPS)), "RGB")
    biome_b64 = _to_png_b64(
        flip(biome_rgb(_shrink(biome_index, max_px, order=0).astype(np.uint8), rules)), "RGB")

    plates = "".join([
        _plate("I", "Relief", "Ombrage et étagement des altitudes", relief_b64, geo),
        _plate("II", "Température", "Moyenne annuelle au sol, gradient adiabatique inclus",
               temp_b64, geo),
        _plate("III", "Précipitations", "Cumul annuel issu de l'advection d'humidité",
               rain_b64, geo),
        _plate("IV", "Biomes", "Whittaker croisé avec l'étagement altitudinal", biome_b64, geo),
    ])

    landmarks = geo.landmarks()
    coord_rows = "".join(
        "<tr><td>{}</td><td>{:+.2f}&#176;</td><td>{:+.0f}</td></tr>".format(
            name.replace("_", " ").capitalize(),
            lat, landmarks[key])
        for name, key, lat in (
            ("Pôle nord", "pole_nord", geo.lat_span_deg * 0.5),
            ("Cercle polaire arctique", "cercle_polaire_arctique", geo.polar_circle_deg),
            ("Tropique du Cancer", "tropique_cancer", geo.tropic_deg),
            ("Équateur", "equateur", 0.0),
            ("Tropique du Capricorne", "tropique_capricorne", -geo.tropic_deg),
            ("Cercle polaire antarctique", "cercle_polaire_antarctique", -geo.polar_circle_deg),
            ("Pôle sud", "pole_sud", -geo.lat_span_deg * 0.5),
        )
    )

    figures = [
        ("{:.1f}".format(stats["landPct"]), "%", "Terres émergées"),
        ("{:.0f} / {:.0f}".format(stats["elevMin"], stats["elevMax"]), "m", "Amplitude du relief"),
        ("{:+.0f} / {:+.0f}".format(stats["tempMin"], stats["tempMax"]), "°C", "Extrêmes thermiques"),
        ("{:.0f}".format(stats["precipMedian"]), "mm", "Pluie médiane, terres"),
        ("{:.0f}".format(stats["walkablePct"]), "%", "Pente sous 25°"),
        (str(stats["biomeCount"]), "", "Biomes distincts"),
    ]
    figure_html = "".join(
        '<div class="fig"><b>{v}<i>{u}</i></b><span>{l}</span></div>'.format(v=v, u=u, l=l)
        for v, u, l in figures
    )

    html = _TEMPLATE.substitute(
        seed=rules.seed,
        size="{:.0f}".format(stats["sizeKm"]),
        sim=stats["simResolution"],
        out=stats["outResolution"],
        mpp="{:.2f}".format(stats["metresPerPixel"]),
        dur="{:.0f}".format(stats["durationS"]),
        figures=figure_html,
        plates=plates,
        checks=_checks_block(stats["checks"]),
        profile=_profile_chart(stats["zonal"], geo),
        biomes=_biome_block(stats["biomeShare"], rules),
        coords=coord_rows,
    )
    path.write_text(html, encoding="utf-8")


_TEMPLATE = Template("""<title>Relevé du monde Worldseed</title>
<link rel="preconnect" href="https://fonts.googleapis.com"/>
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin/>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Bricolage+Grotesque:opsz,wght@12..96,500;12..96,700&family=Instrument+Sans:wght@400;500;600&family=IBM+Plex+Mono:wght@400;500&display=swap"/>
<style>
:root {
  --paper:#f4f1ea; --sheet:#fffdf8; --ink:#1a2028; --ink-2:#4a5663; --ink-3:#6f7c8a;
  --rule:#ddd6c8; --rule-soft:#e9e3d7;
  --equator:#cf5b2c; --tropic:#b3852c; --polar:#4d7cb0; --water:#2f7cb0;
  --warm:#c9622f; --cool:#3f7fae; --rain:#2f6f9e;
  --pass:#3f7a55; --pass-bg:#e6efe7; --warn:#a85f22; --warn-bg:#f6ecdf;
  --info-bg:#eae6db;
  --shadow:0 1px 2px rgba(26,32,40,.06), 0 8px 24px -14px rgba(26,32,40,.22);
}
:root:not([data-theme="light"]) { color-scheme: light dark; }
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --paper:#0d1218; --sheet:#141b23; --ink:#e6ecf2; --ink-2:#a9b6c4; --ink-3:#7d8b9a;
    --rule:#243039; --rule-soft:#1c252d;
    --equator:#e8794a; --tropic:#d3a04a; --polar:#7fa8d8; --water:#54a3d8;
    --warm:#e07845; --cool:#5b9dc9; --rain:#4b9ad0;
    --pass:#69ad83; --pass-bg:#16281d; --warn:#d69148; --warn-bg:#2c2113;
    --info-bg:#1c242c;
    --shadow:0 1px 2px rgba(0,0,0,.4), 0 10px 30px -16px rgba(0,0,0,.7);
  }
}
:root[data-theme="dark"] {
  --paper:#0d1218; --sheet:#141b23; --ink:#e6ecf2; --ink-2:#a9b6c4; --ink-3:#7d8b9a;
  --rule:#243039; --rule-soft:#1c252d;
  --equator:#e8794a; --tropic:#d3a04a; --polar:#7fa8d8; --water:#54a3d8;
  --warm:#e07845; --cool:#5b9dc9; --rain:#4b9ad0;
  --pass:#69ad83; --pass-bg:#16281d; --warn:#d69148; --warn-bg:#2c2113;
  --info-bg:#1c242c;
  --shadow:0 1px 2px rgba(0,0,0,.4), 0 10px 30px -16px rgba(0,0,0,.7);
}

* { box-sizing:border-box; }
body {
  margin:0; background:var(--paper); color:var(--ink);
  font-family:"Instrument Sans", ui-sans-serif, system-ui, -apple-system, sans-serif;
  font-size:15px; line-height:1.6;
  -webkit-font-smoothing:antialiased;
}
.sheet { max-width:1220px; margin:0 auto; padding:0 28px 88px; }

/* ---------------------------------------------------------------- masthead */
.masthead { padding:52px 0 26px; border-bottom:2px solid var(--ink); }
.eyebrow {
  font-family:"IBM Plex Mono", ui-monospace, monospace; font-size:11.5px;
  letter-spacing:.16em; text-transform:uppercase; color:var(--ink-3);
  display:flex; gap:18px; flex-wrap:wrap; margin-bottom:14px;
}
h1 {
  font-family:"Bricolage Grotesque", ui-sans-serif, system-ui, sans-serif;
  font-weight:700; font-size:clamp(34px, 5.2vw, 56px); line-height:1.02;
  letter-spacing:-.03em; margin:0 0 12px; text-wrap:balance;
}
h1 em { font-style:normal; color:var(--equator); }
.standfirst { margin:0; max-width:62ch; color:var(--ink-2); font-size:16.5px; }

/* ------------------------------------------------------------------ chiffres */
.figures {
  display:grid; grid-template-columns:repeat(auto-fit, minmax(148px, 1fr));
  gap:0; margin:30px 0 44px; border-top:1px solid var(--rule);
  border-bottom:1px solid var(--rule);
}
.fig { padding:16px 18px 15px; border-right:1px solid var(--rule-soft); }
.fig:last-child { border-right:none; }
.fig b {
  display:block; font-family:"Bricolage Grotesque", sans-serif; font-weight:500;
  font-size:24px; letter-spacing:-.02em; font-variant-numeric:tabular-nums;
}
.fig b i { font-style:normal; font-size:14px; color:var(--ink-3); margin-left:3px; }
.fig span {
  display:block; font-size:11.5px; letter-spacing:.05em; text-transform:uppercase;
  color:var(--ink-3); margin-top:3px;
}

/* -------------------------------------------------------------------- titres */
h2 {
  font-family:"Bricolage Grotesque", sans-serif; font-weight:700;
  font-size:23px; letter-spacing:-.02em; margin:56px 0 6px;
}
h2 + .lede { margin:0 0 22px; color:var(--ink-2); max-width:64ch; }
.sec-n {
  font-family:"IBM Plex Mono", monospace; font-size:11.5px; letter-spacing:.16em;
  text-transform:uppercase; color:var(--ink-3); display:block; margin-bottom:2px;
}

/* -------------------------------------------------------------------- planches */
.plates { display:grid; grid-template-columns:repeat(auto-fit, minmax(340px,1fr)); gap:26px; }
.plate { margin:0; }
.frame {
  position:relative; aspect-ratio:1; background:var(--sheet);
  border:1px solid var(--rule); box-shadow:var(--shadow); overflow:hidden;
}
.frame img { width:100%; height:100%; display:block; }
.frame svg { position:absolute; inset:0; width:100%; height:100%; pointer-events:none; }
.g-equator { stroke:var(--equator); fill:var(--equator); }
.g-tropic  { stroke:var(--tropic);  fill:var(--tropic); }
.g-polar   { stroke:var(--polar);   fill:var(--polar); }
.frame .lbl {
  stroke:none; font-family:"IBM Plex Mono", monospace; font-size:15px;
  letter-spacing:.04em; paint-order:stroke; stroke:rgba(0,0,0,.35); stroke-width:3px;
}
.river { fill:none; stroke:var(--water); stroke-linecap:round; stroke-linejoin:round; }
.lake  { fill:var(--water); stroke:none; opacity:.92; }
figcaption { display:flex; align-items:baseline; gap:10px; padding-top:11px; flex-wrap:wrap; }
.pl-n {
  font-family:"IBM Plex Mono", monospace; font-size:12px; letter-spacing:.1em;
  color:var(--equator); border:1px solid currentColor; padding:1px 6px;
}
.pl-t { font-weight:600; font-size:15.5px; }
.pl-d { color:var(--ink-3); font-size:13.5px; }

/* -------------------------------------------------------------------- controles */
.checks { list-style:none; margin:0; padding:0; border-top:1px solid var(--rule); }
.check {
  display:grid; grid-template-columns:1fr auto auto auto; gap:16px; align-items:center;
  padding:13px 0; border-bottom:1px solid var(--rule-soft);
}
.ck-l { font-weight:500; }
.ck-e { color:var(--ink-3); font-size:13.5px; text-align:right; }
.ck-m {
  font-family:"IBM Plex Mono", monospace; font-size:13.5px;
  font-variant-numeric:tabular-nums; text-align:right; min-width:96px;
}
.chip {
  font-size:11px; letter-spacing:.07em; text-transform:uppercase; font-weight:600;
  padding:3px 9px; border-radius:2px; white-space:nowrap;
}
.chip.pass { color:var(--pass); background:var(--pass-bg); }
.chip.warn { color:var(--warn); background:var(--warn-bg); }
.chip.info { color:var(--ink-3); background:var(--info-bg); }

/* --------------------------------------------------------------------- profil */
.split { display:grid; grid-template-columns:minmax(0,1.15fr) minmax(0,1fr); gap:40px; align-items:start; }
@media (max-width:900px) { .split { grid-template-columns:1fr; gap:28px; } }
.profile { width:100%; height:auto; display:block; }
.profile .ax {
  font-family:"IBM Plex Mono", monospace; fill:var(--ink-3); font-size:11px;
  letter-spacing:.06em;
}
.profile .hd { text-transform:uppercase; }
.profile .lat { text-anchor:end; font-size:12px; fill:var(--ink-2); }
.profile .val {
  font-family:"IBM Plex Mono", monospace; font-size:11.5px; fill:var(--ink-3);
  font-variant-numeric:tabular-nums;
}
.profile .axis { stroke:var(--rule); stroke-width:1; }
.profile .warm { fill:var(--warm); }
.profile .cool { fill:var(--cool); }
.profile .rain { fill:var(--rain); }

/* -------------------------------------------------------------------- biomes */
.biomes { list-style:none; margin:0; padding:0; display:grid; gap:7px; }
.biomes li { display:grid; grid-template-columns:12px 1fr 90px 56px; gap:11px; align-items:center; }
.sw { width:12px; height:12px; border-radius:2px; box-shadow:inset 0 0 0 1px rgba(0,0,0,.18); }
.bn { font-size:14px; }
.bt { height:7px; background:var(--rule-soft); border-radius:4px; overflow:hidden; }
.bf { display:block; height:100%; background:var(--ink-2); border-radius:4px; }
.bp {
  font-family:"IBM Plex Mono", monospace; font-size:12.5px; text-align:right;
  color:var(--ink-2); font-variant-numeric:tabular-nums;
}

/* -------------------------------------------------------------------- tableaux */
.scroller { overflow-x:auto; }
table { width:100%; border-collapse:collapse; }
th, td { padding:9px 14px 9px 0; text-align:left; border-bottom:1px solid var(--rule-soft); }
th {
  font-size:11px; letter-spacing:.07em; text-transform:uppercase; color:var(--ink-3);
  font-weight:600; border-bottom:1px solid var(--rule); white-space:nowrap;
}
td { font-size:14px; }
td:nth-child(n+2) {
  font-family:"IBM Plex Mono", monospace; font-variant-numeric:tabular-nums; font-size:13px;
}
.empty { color:var(--ink-3); }

footer {
  margin-top:64px; padding-top:22px; border-top:1px solid var(--rule);
  color:var(--ink-3); font-size:13px; max-width:70ch;
}
code {
  font-family:"IBM Plex Mono", monospace; font-size:.92em;
  background:var(--info-bg); padding:1px 5px; border-radius:2px;
}
@media (prefers-reduced-motion:reduce) { * { animation:none !important; transition:none !important; } }
</style>

<div class="sheet">
  <header class="masthead">
    <p class="eyebrow"><span>Relevé de génération</span><span>Graine $seed</span>
       <span>Simulation $sim&#178; &rarr; sortie $out&#178;</span>
       <span>$mpp m/pixel</span><span>$dur s de calcul</span></p>
    <h1>Un monde de $size km, de l'<em>équateur</em> aux pôles</h1>
    <p class="standfirst">Rien de cette carte n'a été peint. Le relief vient de plaques
      tectoniques en dérive, la pluie d'une circulation atmosphérique à trois
      cellules par hémisphère, les biomes du croisement température ×
      précipitations, et le grain du terrain de l'érosion fluviale sur le
      relief brut.</p>
  </header>

  <section class="figures">$figures</section>

  <h2><span class="sec-n">Planches I &ndash; V</span>Le monde, étape par étape</h2>
  <p class="lede">Les planches suivent l'ordre de la chaîne de génération : le relief
    conditionne le climat, le climat conditionne les biomes et les débits. Les
    parallèles remarquables sont reportés sur chaque planche.</p>
  <div class="plates">$plates</div>

  <h2><span class="sec-n">Verification</span>Ce que le monde doit respecter</h2>
  <p class="lede">Contrôles automatiques exécutés à chaque génération. Un écart ne
    bloque rien : il signale une règle à revoir dans <code>world_rules.json</code>.</p>
  $checks

  <h2><span class="sec-n">Profil zonal</span>Température et pluie par bande de latitude</h2>
  <p class="lede">Moyenne sur les terres émergées de chaque bande. On y lit la signature
    terrestre : maximum pluvieux à l'équateur, ceinture désertique vers 30&#176;,
    remontée au front polaire vers 60&#176;, désert froid aux pôles.</p>
  <div class="split">
    <div>$profile</div>
    <div>
      <h3 style="font-family:'Bricolage Grotesque',sans-serif;font-size:16px;margin:0 0 10px;">Parallèles remarquables</h3>
      <div class="scroller"><table>
        <thead><tr><th>Repère</th><th>Latitude</th><th>Y monde (cm)</th></tr></thead>
        <tbody>$coords</tbody>
      </table></div>
    </div>
  </div>

  <h2><span class="sec-n">Composition</span>Répartition des biomes</h2>
  <p class="lede">Part de chaque biome sur les terres émergées.</p>
  $biomes

  <footer>
    Le monde est entièrement déterminé par <code>world_rules.json</code> et la
    graine : même graine, même monde. Les cartes sont orientées nord en haut ; les
    PNG exportés vers Unreal conservent l'ordre natif du Landscape, ligne 0 au Y
    minimum.
  </footer>
</div>
""")
