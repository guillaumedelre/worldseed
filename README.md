# Worldseed

Générateur de monde procédural pour **Unreal Engine 5.8**. Un monde qui va d'un
pôle à l'autre : tectonique, climat, érosion, biomes et surfaces. Depuis le
14 septembre 2026 la chaîne est **portée en C++ et tourne dans le jeu**, à
partir d'une graine choisie au menu ; `Tools/WorldGen`, le générateur Python
d'origine, reste comme référence de calibration mais n'alimente plus le jeu.

L'eau se limite à l'**océan** : rivières, cascades et lacs ont été retirés le
18 septembre 2026 — `CLAUDE.md` porte les mesures qui l'ont motivé.

Le climat n'est pas décoratif : il est calé sur des mesures terrestres et se
vérifie par des outils dédiés. Le monde est déterministe — une graine donne
toujours le même monde.

---

## ⚠️ Ce dépôt ne suffit pas à ouvrir le projet

**Trois packs payants sont nécessaires et ne sont pas — ne peuvent pas être —
versionnés ici.** Les EULA de Fab et du Marketplace autorisent à intégrer un
pack dans un *produit compilé*, jamais à en redistribuer les fichiers sources.
Ce dépôt étant public, les y pousser serait une redistribution.

| pack | rôle dans le projet | poids |
|---|---|---|
| **Orasot Bundle** | matériau de terrain à dix couches, rochers, arbres, falaises | 937 Mo |
| **Ultra Dynamic Sky** | ciel, soleil physique, météo et système de climat | 545 Mo |
| **Stylized PBR Nature** | végétation légère — `SM_Grass` 40 triangles, `SM_Bush` 64 | 393 Mo |

Il faut les **acheter ou les récupérer dans votre bibliothèque Fab**, puis les
installer dans `Content/` sous ces noms exacts :
`Content/Orasot_Bundle/`, `Content/UltraDynamicSky/`, `Content/Stylized_PBR_Nature/`.
Les chemins sont référencés en dur par les graphes PCG et les recettes de
végétation : un nom de dossier différent casse toutes les références.

Sans eux, le projet s'ouvre mais le terrain est blanc, la végétation absente et
le ciel noir. **Ce n'est pas un défaut du dépôt, c'est une contrainte de
licence.**

S'ajoute un quatrième composant, gratuit celui-là : le plugin **VibeUE**, qui
expose l'API Python utilisée par tout `Tools/UE/`.

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

### 3. Les matériaux dérivés

Deux jeux de matériaux dérivent des packs et ne peuvent donc pas être
versionnés. Ils se refont en quelques minutes.

**Le matériau de terrain.** Dans l'éditeur : dupliquer
`M_LandscapeMasterMaterial` du pack Orasot en
`/Game/Worldseed/Materials/M_WorldseedLandscape`, y ajouter une dixième couche
nommée `Snow` dans le `LandscapeLayerBlend`. L'instance
`MI_WorldseedLandscape`, elle, est versionnée : elle porte les 18 surcharges de
l'auteur du pack, sans lesquelles le terrain ne ressemble à rien.

Une fois le maître recréé, reposer son carrelage — ce réglage vit dans le
graphe, donc hors du dépôt :

```python
import landscape_material; landscape_material.regler()
```

Sans lui, le sol d'herbe n'a aucun grain : de larges traînées vertes. Et cela
se voit — mesure en prairie à hauteur d'œil, **51 % de la moitié basse du cadre
est du sol nu**, 59,7 % dans les trois premiers mètres. Le script ne retaille
que l'herbe : la roche resserrée fait apparaître un moiré hexagonal sur les
pentes lointaines (vérifié par A/B).

**Les matériaux greffés de la RVT.** Le terrain écrit sa couleur dans une
Runtime Virtual Texture, mais seuls **38 des 142 maillages semés (27 %)**
passaient par un matériau qui la relit : la même herbe verte était semée en
savane comme en forêt tropicale. `rvt_graft.py` copie les **11 matériaux
maîtres** concernés sous `/Game/Worldseed/PCG/Materials/`, y insère
l'échantillonnage, puis reparente les **~55 instances** que nos maillages
utilisent réellement :

```python
import sys; sys.path.insert(0, r"<racine>/Tools/UE")
import rvt_graft
rvt_graft.greffer()       # les 11 maitres : la teinte par le sol
rvt_graft.greffer_vent()  # le vent d'UDS sur les 9 maitres de feuillage
rvt_graft.rediriger()     # les instances + la table lue par le semis
```

⚠️ **Traiter par un ou deux matériaux à la fois.** Recompiler neuf gros
matériaux d'affilée a fait tomber l'éditeur deux fois — `EXCEPTION_ACCESS_VIOLATION`
dans `D3D12RHI`. Les fonctions sont idempotentes : elles se relancent sans
risque, et prennent une liste de suffixes en argument.

Les deux sont idempotents. **`rediriger()` n'est pas optionnel** : greffer un
maître ne change rien tant que les maillages pointent vers les instances du
pack. Il écrit `Tools/UE/materiaux_greffes.json`, que `vegetation.py` relit
pour poser les redirections emplacement par emplacement — 191 entrées de
maillage sur 216 dans le graphe PCG.

La greffe mélange la couleur du sol dans la couleur de base, **dosée par un
fondu en hauteur** : au ras du sol la plante prend le ton du terrain, au-delà
de `Hauteur fondu RVT` centimètres elle garde le sien. C'est ce qui permet de
teindre une touffe d'herbe sans peindre un arbre en terre. Les deux valeurs se
règlent par matériau dans `CIBLES`, et `rvt_graft.regler()` les reporte sans
refaire la greffe.

Mesure sur le feuillage d'une savane, part de pixels verts : **24,6 % → 11,5 %**
en portant la teinte de 0,75 à 0,85. Performance inchangée : 115 images par
seconde, verdict PASS.

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

### 6. La météo suit le biome, toute seule

Rien à lancer : `BP_WorldseedClimat` s'en charge au démarrage du jeu. Il lit la
position du joueur deux fois par seconde, en déduit la latitude et le biome, et
applique le préréglage climatique correspondant — nos 31 `CP_Worldseed_*`, tirés
des mesures du générateur, avec les saisons inversées dans l'hémisphère sud.

**Ultra Dynamic Sky fait le reste du calcul lui-même** : il convertit les
millimètres mensuels de pluie et de neige de chaque préréglage en probabilités de
météo par saison, arbitre pluie contre neige par leur rapport, et en déduit
jusqu'au brouillard et aux tempêtes de sable. Il ne faut donc surtout pas écrire
`Global Weather State` à la main : il suffit d'armer le tirage, ce que le
Blueprint fait aussi au démarrage (`Random Weather Variation` en intervalle
aléatoire de 200 à 300 s, et le cycle jour/nuit lancé — 45 minutes réelles pour
24 heures, sans quoi les saisons ne progressent jamais).

À chaque changement de biome, la météo est retirée au sort immédiatement. Mesure
en jeu : forêt tempérée → `Partly_Cloudy` ; téléportation au pôle sud → biome
calotte, `Clear_Skies`, minuteur de changement remis à zéro. Les cartes de
probabilités passent de 8-9 types de météo en forêt tempérée à 4 sous la calotte.
107,6 images par seconde, verdict PASS.

**Et le temps passe à une vitesse jouable.** Le calendrier grégorien d'Ultra
Dynamic Sky donnait une année de **274 heures réelles** — les saisons ne
changeaient donc jamais, et les quatre saisons de chaque préréglage ne servaient
à rien. `CAL_Worldseed` (12 mois de 3 jours) ramène l'année à **27 h** et la
saison à **6 h 45**, la journée restant à 45 minutes. Il est assigné au
démarrage par `BP_WorldseedClimat`.

⚠ **Ne pas piloter la saison directement** en passant `Season Mode` en manuel :
avec `Simulate Real Sun`, c'est la DATE qui donne la déclinaison du soleil, et
découpler les deux produit un hiver sous un soleil d'été. C'est le calendrier
qui doit être raccourci, parce qu'il déplace les deux ensemble.

Résultat mesuré en toundra boréale : ~3 épisodes neigeux par hiver et ~15
épisodes pluvieux par été.

---

## Vérifier que le monde est juste

```bash
P=Tools/WorldGen/.venv/Scripts/python.exe

$P Tools/WorldGen/terre.py               # bulletin de conformité terrestre
$P Tools/WorldGen/metrics.py <monde>     # relevé complet
$P Tools/WorldGen/metrics.py --diff avant.json apres.json
$P Tools/WorldGen/diag_uds_climat.py     # cohérence climat / biomes / UDS
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

- **[`Docs/atlas-worldseed.html`](Docs/atlas-worldseed.html)** — la référence
  complète. La chaîne étape par étape, le système climatique et ses formules,
  son calage sur la Terre, son imbrication dans Unreal et Ultra Dynamic Sky, et
  les pièges de mesure. Neuf schémas tracés sur des mesures réelles, dont le
  diagramme de Whittaker du projet avec les 23 climats réels posés dessus.
  Fichier autonome : il s'ouvre par un double-clic, sans serveur.
  <br>*Version en ligne, identique :*
  [claude.ai/code/artifact/c0cb40bf…](https://claude.ai/code/artifact/c0cb40bf-ea8f-4dba-889a-92f25245933d)
- **`CLAUDE.md`** — les pièges rencontrés et leur correctif, accumulés session
  après session. À lire avant de toucher au moteur : la plupart des impasses y
  sont déjà décrites, avec la mesure qui les a révélées.
- **`ETAT_DES_LIEUX.md`** — état du projet et points ouverts.

**Une réserve sur l'atlas.** Ses chiffres sont des mesures prises sur la graine
`20260909`, remises à jour le 13 septembre 2026. Régénérer le monde avec une autre graine ou d'autres règles les rend
caducs : ils décriraient un monde qui n'existe plus. Ils se refont en relisant
`manifest.json` et `uds_presets_livres.json`, comme lors de la rédaction.
