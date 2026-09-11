# Worldseed — état des lieux et suite

Document de reprise, écrit le 9 septembre 2026 à la fin d'une longue session.
À lire en entier avant de toucher quoi que ce soit.

Le guide technique du plugin et les pièges déjà rencontrés vivent dans `CLAUDE.md`,
notamment sa **section 11**, qui est la mémoire des erreurs à ne pas refaire. Ce
document-ci ne la répète pas : il décrit **où en est le projet** et **ce qui reste**.

---

## 1. De quoi il s'agit

Un monde procédural pour un RPG sous Unreal Engine 5.8. Deux moitiés bien séparées :

- **Le générateur**, en Python, dans `Tools/WorldGen/`. Il ne connaît pas Unreal et
  n'écrit que des PNG et des JSON. Chaîne : tectonique → climat → érosion → climat
  (2ᵉ passe) → hydrologie → biomes → surfaces → export.
- **L'import**, dans `Tools/UE/import_world.py`, exécuté *depuis* l'éditeur, qui
  consomme ces fichiers.

Le monde est entièrement déterminé par `Tools/WorldGen/rules/world_rules.json` et la
graine. Même graine + mêmes règles = même monde, au bit près.

**Paramètres de référence** : graine 20260909, **8 km de côté**, simulation 2049²,
sortie 4065², soit **1,97 m/pixel**.

> **11 septembre 2026 — le monde est passé de 32 km à 8 km.** C'est une *maquette
> au 1/4* : même graine, même carte, en réduction. Le générateur étant presque
> entièrement exprimé en unités relatives (fréquences en cycles par monde,
> distances en pixels de simulation, seuils normalisés par centile), il a suffi
> de diviser par 4 ce qui est **métrique** et par 16 ce qui est une **aire** ou un
> **débit**. La règle complète vit dans `world._comment_echelle`, et le diff de
> `metrics.py` entre les deux mondes le confirme : forêt tempérée humide 12,71 %
> → 12,65 % des terres, roche nue 12,48 % → 11,91 %, Stone dominant 31,6 % →
> 29,9 %.
>
> Deux exceptions **assumées**, décidées par le propriétaire du projet :
> `temperature.lapseRateCPerKm` est **multiplié** par 4 (6,5 → 26 °C/km) pour que
> l'altitude garde son rôle climatique sur un relief quatre fois plus bas, et
> `hydrology.geometryExaggeration` est multipliée par 16 (500 → 8000) pour que
> les rivières gardent leur largeur réelle — l'échelle humaine, elle, ne
> rétrécit pas avec le monde. Mesure : 36,1 m de large avant, 37,0 m après.
>
> Conséquence heureuse : **4065² = 256 composants**, soit un Landscape unique.
> Le tuilage 2×2 imposé par le plafond D3D12 disparaît, et avec lui les
> 4 textures de biomes et les listes de tuiles du PCG.
>
> L'ancien monde de 32 km est conservé sous `Saved/WorldGen/20260909_32km`.

---

## 2. Où sont les choses

| Chemin | Contenu |
|---|---|
| `Tools/WorldGen/worldgen/` | le générateur, un module par étape |
| `Tools/WorldGen/rules/world_rules.json` | **toutes** les règles ; aucun seuil n'est en dur dans le code |
| `Tools/WorldGen/metrics.py` | relevé et **diff** de non-régression |
| `Tools/WorldGen/diag_hydro.py` | banc de diagnostic hydrologique, sans resimuler |
| `Tools/WorldGen/diag_climat.py` | idem pour les précipitations |
| `Tools/WorldGen/tile_world.py` | découpe la sortie en tuiles importables (**une seule** à 8 km ; le nombre est déduit de la résolution) |
| `Tools/WorldGen/tune_coast.py` | banc de réglage de la forme du littoral |
| `Tools/UE/rebuild_world.py` | **la chaîne complète côté Unreal** : vider, relief, matériau, eau, biomes, végétation, apparition |
| `Tools/UE/import_world.py` | import du Landscape (ne pose PAS le matériau) |
| `Tools/UE/water_world.py` | océan, lacs et rivières à partir de `lakes.json` / `rivers.json` |
| `Saved/WorldGen/20260909/` | la sortie courante (PNG, JSON, rapport HTML) |
| `Saved/WorldGen/20260909_32km/` | l'ancien monde de 32 km, conservé pour comparaison |
| `Saved/WorldGen/releves/` | relevés figés : `reference.json`, `final.json` |
| `Saved/VibeUE/Captures/` | captures d'écran de la session |

Le dépôt git couvre `Tools/`, `Config/`, `Source/` et `CLAUDE.md`. **`Content/` en est
exclu volontairement** (plusieurs Go de bundles achetés et d'assets régénérables), tout
comme `Plugins/`, qui a son propre dépôt.

---

## 3. Le générateur : état

Cinq commits, un par tâche :

```
c6a357a  Tache 4 : la bande littorale existe enfin
c330dbe  Tache 3 : la ZCIT est enfin alimentee
5f18044  Tache 2 : geometrie hydraulique physique, exageration nommee
6fd04c2  Tache 1 : le drainage rejoint enfin la mer
ea7d4e2  Etat de reference du generateur Worldseed
```

**Les quinze contrôles du rapport sont au vert.** Ils s'affichent à chaque génération
et sont écrits dans `pipeline._checks`. Ne jamais rendre un contrôle vert en abaissant
son seuil : c'est une règle posée par le propriétaire du projet.

### Ce qui a été corrigé, et pourquoi ça comptait

| | Avant | Après |
|---|---|---|
| Cours d'eau atteignant l'océan | 0 % (0/10) | **100 % (45/45)** |
| Plus long cours d'eau | 2,8 km (8,8 % du monde) | **11,2 km (35,1 %)** |
| Lacs sur les terres | 20,5 % | **5,5 %** |
| Bassins endoréiques hors zone aride | 1 | **0** |
| Latitude la plus humide | −42° | **équateur** |
| Rapport pluie équateur / ±30° | 0,51 | **6,6** |
| Plage sur les terres | 0,20 % | **1,4 %** |
| Neige dominante sur les terres | 82 % | **12 %** |

Quatre causes, toutes trouvées par la mesure et non par la lecture du code :

1. **Le remplissage epsilon n'existait pas.** Ce qui en tenait lieu relevait les
   cellules de déversement de zéro, les laissant à égalité avec leur voisine : 83 % des
   terres voyaient leur écoulement mourir dans un puits. Remplacé par une vraie variante
   epsilon du Priority-Flood.
2. **Le comblement numérique était pris pour un lac.** `extract_lakes` et
   `biomes.classify` appliquaient chacun de leur côté un seuil de 5 cm. Un critère
   unique, `hydrology.lake_mask`, évalue désormais la cuvette entière.
3. **Le terme de neige avait un signe inversé** : `smoothstep(-1, -9, -T)` donnait de la
   neige totale à +27 °C et aucune à −9 °C. La forêt tropicale était sous la neige.
4. **La ZCIT n'était alimentée par rien.** Le terme de convergence existait et
   fonctionnait, mais l'advection semi-lagrangienne transporte l'humidité sans la
   concentrer : l'équateur avait le taux de pluie le plus fort et l'humidité la plus
   faible du monde. Ajout de `precipitation.moistureConvergenceRate`.

### Décisions de conception à ne pas défaire sans en parler

- **Exagération hydraulique assumée.** Un monde de 8 km ne peut pas porter de fleuve :
  son plus grand bassin fait quelques km², soit 0,043 m³/s, ce qui donne physiquement un
  chenal de 40 cm. Les coefficients `widthCoefA` = 2,0 et `depthCoefB` = 0,3 sont les
  **vraies** valeurs physiques, et le grossissement passe par `geometryExaggeration`
  = 8000, qui multiplie le **débit apparent** servant à la géométrie — largeur et
  profondeur restent donc dans leur proportion naturelle. Le débit exporté reste le vrai.
  Mettre ce paramètre à 1 rend le générateur strictement physique.
- **Un Landscape ne doit pas dépasser ~256 composants s'il porte dix couches.** Au-delà,
  l'import des poids fait tomber le thread RHI (plafond D3D12 non configurable). À 8 km
  la sortie 4065² tombe **pile** sur 256 : un seul Landscape, aucun tuilage.
  `tile_world.py` déduit désormais le nombre de tuiles de la résolution.
  Détail complet dans `CLAUDE.md` §11.
- **Le critère de lac est volontairement sévère** (`minLakeDepthM` 0,5 m,
  `minLakeAreaHa` 9,375 — soit les seuils de 32 km divisés par 4 et par 16) : il ne
  laisse que quelques grands lacs. Le desserrer fait remonter la part des lacs au-dessus
  de la cible de 5 %.
- **La règle de mise à l'échelle est écrite dans `world._comment_echelle`.** Tout nouveau
  réglage doit la respecter. Et **après toute mise à l'échelle, lancer
  `metrics.py --diff` contre l'ancien monde** : c'est le seul contrôle qui voit une
  constante métrique oubliée en dur dans le code. C'est ainsi qu'a été trouvé le
  `+ 400.0` de `tectonics.py`, qui poussait le pôle sud à 430 m au lieu de 130 et faisait
  passer la calotte glaciaire de 7,1 à 11,9 % des terres.

### Le monde actuel en chiffres

Terres 38,5 %, eau 61,5 %. Relief −258 à +376 m. 45 rivières, toutes à l'océan, la
principale de 2,81 km portant 0,041 m³/s pour 36,1 m de large. 5 lacs, 134 ha.

Biomes majeurs, en part des terres : forêt tempérée humide 12,7 %, roche nue 12,4 %,
désert chaud 11,9 %, forêt tropicale humide 9,3 %, forêt tempérée 9,1 %, toundra 7,8 %.

**La preuve que c'est bien le même monde**, `metrics.py --diff` entre 32 km et 8 km :
**15 métriques bougent sur 200**, et toutes comme la construction l'exige —
altitude max 1502,0 → 375,5 m (**exactement ÷4**), plus long cours d'eau 11 230,9 →
2 807,7 m (**÷4**), aire des lacs 2 137,5 → 133,6 ha (**÷16**), débit max 0,650 →
0,0406 m³/s (**÷16**). La largeur maximale des rivières, 36,1 m, n'apparaît même pas
dans le diff : elle est **identique**.

Couches de peinture dominantes : Stone 31,6 %, Grass 17,8 %, Snow 12,1 %,
DesertSand 11,6 %, Biom Grass 5 8,3 %.

**Les 19 biomes sont tous présents.** Attention : un biome n'est **pas** une couche de
peinture. Chaque biome est une recette de mélange des 10 couches
(`surfaces.recipes`), et l'identité du biome vit dans `biome_index.png`. C'est ce
fichier, et non les couches, qui doit piloter la végétation.

---

## 4. Le monde dans Unreal : état

Niveau `/Game/Worldseed/Maps/L_Worldseed`, World Partition. **À jour** : il contient le
monde de 8 km.

**Tout se refait en un appel**, depuis l'éditeur :

```python
import sys, importlib
sys.path.insert(0, r"D:\UE\Worldseed\Tools\UE")
import rebuild_world; importlib.reload(rebuild_world)
print(rebuild_world.rebuild(r"D:\UE\Worldseed\Saved\WorldGen\20260909"))
```

- **1 seul Landscape** de 4065² sommets, 16×16 = 256 composants, 4 proxies. Plus aucune
  couture à vérifier. Contrôle mesuré : le Landscape rend 6916,20 cm là où la heightmap
  annonce 6916,80 — **0,6 cm d'écart**.
- **Matériau** `/Game/Worldseed/Materials/M_WorldseedLandscape`, dupliqué du maître
  Orasot, auquel a été ajoutée la 10ᵉ couche `Snow`. **`import_world.py` ne le pose
  pas** : un Landscape fraîchement importé est blanc. `rebuild_world.landscape()` s'en
  charge.
- **Eau** : 1 `WaterBodyOcean` à Z = 0, 4 `WaterBodyLake` (splines fermées et linéaires,
  chacun à son niveau, de 40,8 à 129,2 m), 48 `WaterBodyRiver` avec largeur et profondeur
  par nœud, 1 `WaterZone` de 8,5 km à 4096 texels. **`affects_landscape` est à `False`
  sur les 53 corps d'eau** : sans cela ils creuseraient le relief importé.
  Tout est rejouable par `Tools/UE/water_world.py`.
  - **Le rideau d'eau au bord des lacs s'est beaucoup amélioré tout seul** : la zone
    passe de 8,3 à **2,08 m par texel**, et la plage de hauteurs d'eau de 0–545 m à
    0–129 m. Soit environ 17 fois moins de marche à franchir par texel.
- **Végétation PCG** : 1 `PCGVolume` `Worldseed_Vegetation_x0_y0`, graphe de 104 nœuds
  et 32 couches, en **génération à l'exécution**. Vérifié en PIE : les acteurs de
  partition sont dépilés d'un pool transitoire, rien n'est écrit sur le disque.
- **`PCGWorldActor`** : posé à la main et **armé**. Un acteur neuf arrive avec
  `enable_world_partition_generation_sources = False` et un cache de paysage en
  `NeverSerialize` — et alors le PIE ne produit rien, sans une seule ligne `LogPCG`.
- **Éclairage** : soleil directionnel mobile lié à l'atmosphère, SkyLight en capture temps
  réel, SkyAtmosphere, brouillard. Le niveau avait été créé vide, sans aucune lumière.
- **`Worldseed_PlayerStart`** posé en forêt tempérée humide, sur un critère **mesuré**
  (biome, altitude, pente sous 8°, distance à l'eau) et non à l'œil. Sans lui, le PIE fait
  apparaître le pion à l'origine du monde, c'est-à-dire en pleine mer.
- **Herbe de Landscape** : cinq types dans `/Game/Worldseed/Landscape/`
  (`GT_Worldseed_Grass`, `_Flower_1`, `_Flower_2`, `_Flower_3`, `_Fern`), tous dupliqués
  chez nous pour ne pas modifier le bundle acheté.

---

## 5. Ce qui reste à faire

Dans l'ordre où je le ferais.

### 5.1 La densité d'herbe ne répond pas — non résolu

Monter `grass_density` de 581 à 1200 puis 4000 donne des captures **identiques au pixel
près**. Vider le cache (`grass.FlushCache`) supprime l'herbe, qui revient à l'identique
au redémarrage du PIE. Étendre la distance de coupe de 190 à 400 m ne change rien non
plus.

Conclusion : le facteur limitant est le **poids** que le matériau envoie à chaque
émetteur, pas la densité — le nombre d'instances vaut densité × poids, et les expressions
du pack (`Add_21`, `Subtract_6`, `Subtract_4`, `Subtract_20`, `Subtract_22`, qui
alimentent le `LandscapeGrassOutput`) produisent une valeur faible pour notre mélange de
couches. Obtenir un vrai tapis demande de **remettre ces entrées à l'échelle dans le
graphe du matériau**, ce qui touche au rendu du terrain. Le propriétaire du projet devait
donner son accord avant que j'y aille ; la question est restée ouverte.

### 5.2 La végétation PCG — faite, et vérifiée en exécution

**Résolu.** Un graphe `PCG_Vegetation_x0_y0` (104 nœuds, 32 couches) et un `PCGVolume`
en génération à l'exécution. Le chemin retenu est le **natif** :
`PCGSurfaceSampler` (bornée par la maille) → `PCGSampleTexture` sur `biome_index.png`
en filtrage `Point` → `PCGProjection` sur le Landscape → `PCGStaticMeshSpawner`.

Ce n'est **pas** `PCGBiomeCore` / `PCGBiomeSample`, le pack de biomes expérimental
d'Epic : il a été monté entièrement puis abandonné (aucune instance produite,
`GetAttributeFromPointIndex : index 0 hors limites` dans `LocalBiomeCore`, et il
apparie les biomes par **couleur** et non par identifiant). Il est expérimental, en
v0.2, sans aucune API C++ ni garantie entre versions du moteur. Voir `CLAUDE.md` §11.

Ce qui reste ouvert sur la végétation : les **silhouettes manquantes**. Taïga (deux
conifères seulement), toundra (rien de spécifique), savane (pas d'acacia), marais (pas
de végétation palustre) — ensemble 15 % des terres, traitées par emprunt en attendant.

### 5.3 Points ouverts, plus petits

- **La plage est indiscernable du désert** : même couche dominante. Se corrige soit par
  une recette de surface propre à la plage, soit en laissant le PCG faire la différence.
- **Le sol des zones végétalisées est un vert plat sans texture**, alors que le sable
  montre du détail. Probablement le parti pris stylisé du pack ; se règle dans `MF_Grass`.
- **La médiane équatoriale frôle le plafond `maxPrecipMm`.** Le réglage a été ramené de
  0,004 à 0,002 pour cette raison, mais il reste peu de marge.
- **Les HLOD ne sont pas construits.** À faire quand il y aura du contenu, pas avant.
- **Le rapport HTML** publié en artefact en début de session décrit le monde d'AVANT les
  corrections. Le rapport à jour est dans `Saved/WorldGen/20260909/world_report.html`.

---

## 6. Pièges de cette session, en plus de `CLAUDE.md` §11

- **L'herbe d'Orasot dépend d'une texture virtuelle.** `M_Grass` contient deux
  `RuntimeVirtualTextureSample` : sans `RuntimeVirtualTextureVolume` dans le niveau,
  l'échantillonnage retourne du vide et l'herbe s'affiche en **bleu pur**. J'ai essayé de
  créer la RVT et de l'assigner aux Landscapes : l'assignation prend, mais la texture
  reste vide (l'herbe passe de bleue à noire) et le terrain proche s'aplatit. **Revenu en
  arrière.** La solution retenue contourne : les types d'herbe de LowPolyForestVol2 et les
  fleurs de Biom_Green n'ont **aucun** nœud RVT. Vérifier avant d'utiliser un type d'herbe.
- **`RuntimeVirtualTextureService.create_rvt_volume` est défaillant** : il crée des
  `Actor` génériques à l'origine, à l'échelle 1, et non des `RuntimeVirtualTextureVolume`.
- **Les tableaux de structs ne se réécrivent pas tels quels depuis Python.**
  `o.get_editor_property("grass_varieties")` rend un `Array` dont la modification ne
  remonte pas ; il faut le repasser en `list(...)` puis réaffecter.
- **En PIE, sans `PlayerStart`, le pion apparaît en (0, 0, 0)** — au milieu de l'océan sur
  ce monde — et tombe. Et attention en choisissant un point : les lacs sont profonds et
  rien ne les signale depuis la berge ; mon premier point d'apparition était 11 m sous la
  surface d'un lac.
- **Le toolset `EditorToolset.EditorAppToolset` d'Epic n'est pas enregistré** dans ce
  build : pour le PIE, utiliser `LevelEditorSubsystem.editor_request_begin_play()` et
  `editor_request_end_play()`.
- **Quand le client MCP décroche**, l'éditeur reste joignable : son endpoint est un
  serveur HTTP sur `http://127.0.0.1:8000/mcp`. Un relais en curl (initialize → session →
  `tools/call`) permet de continuer sans redémarrer l'éditeur, donc sans perdre le travail
  non sauvegardé. C'est ce qui a servi pendant la moitié de la session.
- **Un `TaskStop` tue le shell mais pas le processus Python enfant** : une génération
  arrêtée peut continuer jusqu'au bout et écrire ses sorties, avec les anciens paramètres.
  Vérifier `tasklist` avant de conclure sur un fichier daté.

---

## 7. Comment faire les choses

```bash
cd D:/UE/Worldseed/Tools/WorldGen

# Générer le monde (~9 min ; affiche les quinze contrôles à la fin)
./.venv/Scripts/python.exe -m worldgen

# Figer un relevé, puis comparer à un autre
./.venv/Scripts/python.exe metrics.py ../../Saved/WorldGen/20260909 --out releve.json
./.venv/Scripts/python.exe metrics.py --diff reference.json releve.json

# Diagnostiquer sans resimuler (~1 min)
./.venv/Scripts/python.exe diag_hydro.py
./.venv/Scripts/python.exe diag_climat.py

# Découper pour l'import (coutures vérifiées automatiquement)
./.venv/Scripts/python.exe tile_world.py
```

Import dans Unreal, depuis l'éditeur, une tuile par appel (~40 s chacune) :

```python
import sys, importlib, json
sys.path.insert(0, r"D:\UE\Worldseed\Tools\UE")
import import_world; importlib.reload(import_world)
TILE = r"D:\UE\Worldseed\Saved\WorldGen\20260909\tiles\x0_y0"
m = json.load(open(TILE + r"\manifest.json", encoding="utf-8"))
import_world.create_landscape_with_layers(m, TILE, m["tile"]["label"])
```

**Avant de réimporter**, supprimer les anciens Landscapes *et* leurs proxies, puis
sauvegarder avec `EditorLoadingAndSavingUtils.save_dirty_packages(True, True)` —
`save_current_level()` n'écrit que le `.umap` et laisse les acteurs World Partition de
côté.

**Après réouverture du niveau**, aucun proxy n'est chargé et les lectures de hauteur
renvoient l'altitude minimale sans erreur. Charger d'abord via
`WorldPartitionBlueprintLibrary.get_actor_descs()` puis `load_actors(guids)`.
