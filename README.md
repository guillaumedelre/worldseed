# Worldseed

Générateur de monde procédural pour **Unreal Engine 5.8**. Un monde de 8 km de
côté qui va d'un pôle à l'autre : tectonique, climat, érosion, hydrologie,
biomes et surfaces sont simulés hors du moteur, puis importés en relief, en eau,
en végétation et en météo dynamique.

Le climat n'est pas décoratif : il est calé sur des mesures terrestres et se
vérifie par des outils dédiés. Le monde est déterministe — une graine donne
toujours le même monde.

---

## Ce que ce dépôt contient, et ce qu'il ne contient pas

| | suivi | pourquoi |
|---|---|---|
| `Tools/WorldGen/` | ✅ | le générateur, en Python pur |
| `Tools/UE/` | ✅ | les scripts d'import et de construction dans l'éditeur |
| `Source/`, `Config/`, `*.uproject` | ✅ | le projet lui-même |
| `Content/Worldseed/` | ✅ | notre contenu : graphes PCG, presets climatiques, Blueprints |
| `Content/Orasot_Bundle/`, `UltraDynamicSky/`, `Stylized_PBR_Nature/` | ❌ | **packs payants** — voir ci-dessous |
| `Content/__ExternalActors__/` | ❌ | 363 Mo, entièrement régénérés par `rebuild_world` |
| `Saved/`, `Intermediate/`, `Binaries/`, `DerivedDataCache/` | ❌ | caches et sorties, régénérables |
| `Plugins/` | ❌ | plugin tiers, dépôt séparé |

**Sur les packs payants.** Les EULA de Fab et du Marketplace autorisent à
intégrer un pack dans un *produit compilé*, pas à en redistribuer les fichiers
sources. Ce dépôt étant public, ils en sont exclus. Il faut les posséder pour
ouvrir le projet tel quel. Pour la même raison, `M_WorldseedLandscape` est
exclu : c'est une copie du matériau maître d'Orasot augmentée d'une dixième
couche, donc le graphe du pack.

---

## Reconstituer le projet depuis un clone

### 1. Prérequis

- **Unreal Engine 5.8**
- **Git LFS** — `git lfs install` avant le clone, sinon les `.uasset` arrivent
  sous forme de pointeurs texte et le projet ne s'ouvre pas
- Trois packs, depuis votre bibliothèque Fab, à installer dans `Content/` :
  - `Orasot_Bundle` — terrain stylisé et végétation
  - `UltraDynamicSky` — ciel, météo et climat dynamiques
  - `Stylized_PBR_Nature` — végétation légère
- Le plugin **VibeUE**, qui expose l'API Python utilisée par `Tools/UE/` :
  ```bash
  git clone https://github.com/kevinpbuckley/VibeUE.git Plugins/VibeUE
  ```

### 2. L'environnement du générateur

```bash
cd Tools/WorldGen
python -m venv .venv
.venv/Scripts/python.exe -m pip install numpy scipy pillow
```

### 3. Le matériau de terrain

Dans l'éditeur : dupliquer `M_LandscapeMasterMaterial` du pack Orasot en
`/Game/Worldseed/Materials/M_WorldseedLandscape`, y ajouter une dixième couche
nommée `Snow` dans le `LandscapeLayerBlend`. L'instance
`MI_WorldseedLandscape`, elle, est versionnée : elle porte les 18 surcharges de
l'auteur du pack, sans lesquelles le terrain ne ressemble à rien.

### 4. Engendrer le monde

```bash
cd Tools/WorldGen
.venv/Scripts/python.exe -m worldgen            # environ 5 minutes
```

Puis, depuis la racine :

```bash
P=Tools/WorldGen/.venv/Scripts/python.exe
M=Saved/WorldGen/20260909

$P Tools/WorldGen/tile_world.py $M
$P Tools/WorldGen/export_biome_texture.py $M
$P Tools/WorldGen/spawn_point.py $M
$P Tools/WorldGen/export_uds_climate.py
```

### 5. Construire le niveau dans l'éditeur

Ouvrir `/Game/Worldseed/Maps/L_Worldseed`, puis exécuter :

```python
import sys, importlib
sys.path.insert(0, r"<racine>/Tools/UE")
import rebuild_world; importlib.reload(rebuild_world)
print(rebuild_world.rebuild(r"<racine>/Saved/WorldGen/20260909"))
```

Une minute plus tard : relief, dix couches peintes, océan, lacs, rivières,
semis PCG et point d'apparition. Compter environ 400 000 instances de
végétation et 88 images par seconde sur une RTX 4090.

---

## Vérifier que le monde est juste

```bash
P=Tools/WorldGen/.venv/Scripts/python.exe

$P Tools/WorldGen/terre.py               # bulletin de conformité terrestre
$P Tools/WorldGen/metrics.py <monde>     # relevé complet
$P Tools/WorldGen/metrics.py --diff avant.json apres.json
$P Tools/WorldGen/diag_uds_climat.py     # cohérence climat / biomes / UDS
$P Tools/WorldGen/diag_eau.py <monde>    # ajustement de l'eau au relief
```

`terre.py` est l'instrument principal. Il compare le monde à des références
**sourcées** — part des terres émergées, moyenne des précipitations terrestres,
part du climat BWh (Peel, Finlayson & McMahon 2007), parts de surface par zone
climatique déduites de la géométrie d'une sphère — et fait passer les 23 climats
réels livrés par Ultra Dynamic Sky dans notre propre diagramme de Whittaker.

---

## Où se règle quoi

Tout vit dans **`Tools/WorldGen/rules/world_rules.json`**. Aucun seuil n'est
écrit en dur dans le code, et chaque valeur non évidente porte un commentaire
qui dit d'où elle vient, ce qu'on a mesuré en la changeant, et ce qui a été
essayé puis rejeté.

**Le monde est une maquette au 1/4** d'un ancien monde de 32 km. Toute nouvelle
valeur suit cette règle : ce qui est *relatif* ne change pas (angles, degrés de
latitude, températures, millimètres de pluie, fréquences en cycles par monde),
ce qui est *métrique* se divise par 4, ce qui est une *aire* ou un *débit* se
divise par 16.

---

## Documentation

- **`CLAUDE.md`** — les pièges rencontrés et leur correctif, accumulés session
  après session. À lire avant de toucher au moteur : la plupart des impasses y
  sont déjà décrites, avec la mesure qui les a révélées.
- **`ETAT_DES_LIEUX.md`** — état du projet et points ouverts.
