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

**Paramètres de référence** : graine 20260909, 32 km de côté, simulation 2049²,
sortie 8129², soit 3,94 m/pixel.

---

## 2. Où sont les choses

| Chemin | Contenu |
|---|---|
| `Tools/WorldGen/worldgen/` | le générateur, un module par étape |
| `Tools/WorldGen/rules/world_rules.json` | **toutes** les règles ; aucun seuil n'est en dur dans le code |
| `Tools/WorldGen/metrics.py` | relevé et **diff** de non-régression |
| `Tools/WorldGen/diag_hydro.py` | banc de diagnostic hydrologique, sans resimuler |
| `Tools/WorldGen/diag_climat.py` | idem pour les précipitations |
| `Tools/WorldGen/tile_world.py` | découpe la sortie en 4 tuiles importables |
| `Tools/WorldGen/tune_coast.py` | banc de réglage de la forme du littoral |
| `Tools/UE/import_world.py` | import dans Unreal |
| `Saved/WorldGen/20260909/` | la sortie courante (PNG, JSON, rapport HTML) |
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

- **Exagération hydraulique assumée.** Un monde de 32 km ne peut pas porter de fleuve :
  son plus grand bassin fait quelques dizaines de km², soit 0,65 m³/s, ce qui donne
  physiquement un chenal de 1,6 m. Les coefficients `widthCoefA` = 2,0 et
  `depthCoefB` = 0,3 sont désormais les **vraies** valeurs physiques, et le grossissement
  passe par `geometryExaggeration` = 500, qui multiplie le **débit apparent** servant à la
  géométrie — largeur et profondeur restent donc dans leur proportion naturelle. Le débit
  exporté reste le vrai. Mettre ce paramètre à 1 rend le générateur strictement physique.
- **Un Landscape ne doit pas dépasser ~256 composants s'il porte dix couches.** Au-delà,
  l'import des poids fait tomber le thread RHI (plafond D3D12 non configurable). D'où le
  tuilage en 2×2. Détail complet dans `CLAUDE.md` §11.
- **Le critère de lac est volontairement sévère** (`minLakeDepthM` 2 m,
  `minLakeAreaHa` 150) : il ne laisse que 5 grands lacs. Le desserrer fait remonter la
  part des lacs au-dessus de la cible de 5 %.

### Le monde actuel en chiffres

Terres 38,1 %, eau 61,9 %. Relief −1033 à +1502 m. 45 rivières, toutes à l'océan, la
principale de 11,2 km portant 0,65 m³/s pour 36 m de large. 5 lacs, 2138 ha.

Biomes majeurs, en part des terres : forêt tempérée humide 12,7 %, roche nue 12,4 %,
désert chaud 11,9 %, forêt tropicale humide 9,3 %, forêt tempérée 9,1 %, toundra 7,8 %.

Couches de peinture dominantes : Stone 31,6 %, Grass 17,8 %, Snow 12,1 %,
DesertSand 11,6 %, Biom Grass 5 8,3 %.

**Les 19 biomes sont tous présents.** Attention : un biome n'est **pas** une couche de
peinture. Chaque biome est une recette de mélange des 10 couches
(`surfaces.recipes`), et l'identité du biome vit dans `biome_index.png`. C'est ce
fichier, et non les couches, qui doit piloter la végétation.

---

## 4. Le monde dans Unreal : état

Niveau `/Game/Worldseed/Maps/L_Worldseed`, World Partition. **À jour** : il contient le
monde d'après les quatre corrections.

- **4 Landscapes** de 4065² sommets, 16×16 = 256 composants chacun, 16 proxies au total.
  Les coutures entre tuiles sont exactes (écart mesuré 0,0000 cm).
- **Matériau** `/Game/Worldseed/Materials/M_WorldseedLandscape`, dupliqué du maître
  Orasot, auquel a été ajoutée la 10ᵉ couche `Snow`. Les 10 couches sont vérifiées
  exactes à leur pixel de poids maximal sur les quatre tuiles.
- **Eau** : 1 `WaterBodyOcean` à Z = 0, 5 `WaterBodyLake` (splines linéaires, chacun à son
  niveau), 45 `WaterBodyRiver` avec largeur et profondeur réelles par nœud, 1 `WaterZone`
  de 34 km. **`affects_landscape` est à `False` sur les 51 corps d'eau** : sans cela ils
  creuseraient le relief importé.
- **Éclairage** : soleil directionnel mobile lié à l'atmosphère, SkyLight en capture temps
  réel, SkyAtmosphere, brouillard. Le niveau avait été créé vide, sans aucune lumière.
- **`Worldseed_PlayerStart`** posé en forêt tempérée humide. Sans lui, le PIE fait
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

### 5.2 La végétation PCG — le gros morceau, entièrement à faire

Rien n'existe : **zéro graphe PCG** dans le projet. À ne pas confondre avec l'herbe de
Landscape traitée ci-dessus : ce sont deux systèmes complémentaires, pas deux options.

| | Herbe de Landscape | PCG |
|---|---|---|
| Émis par | le matériau du terrain | un graphe PCG |
| Piloté par | les 10 couches de peinture | les données qu'on veut, dont les 19 biomes |
| Pour quoi | brins, fleurs, cailloux | arbres, buissons, rochers |
| Coût | très faible, aucun acteur | plus élevé, instances gérées |

Le point technique à trancher en premier : **le PCG doit lire `biome_index.png`**, pas les
couches de peinture, qui n'en distinguent que dix et confondent des biomes (la plage et
le désert chaud ont toutes deux `DesertSand` pour couche dominante — constaté en jeu, ils
sont visuellement identiques). Cela veut dire importer la carte des biomes comme texture
et l'échantillonner dans le graphe.

**Assets disponibles** : 253 meshes de décor — 81 rochers, 67 arbres, 24 buissons,
22 champignons, 18 falaises, plus fougères et fleurs. De quoi peupler correctement forêt
tempérée, forêt tempérée humide, forêt tropicale (bambou), désert chaud, roche nue et
alpin. **Manques réels** si l'on veut des silhouettes distinctes : taïga (deux conifères
seulement), toundra (rien de spécifique), savane (pas d'acacia), marais (pas de
végétation palustre) — ensemble 15 % des terres, traitables par emprunt en attendant.

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
