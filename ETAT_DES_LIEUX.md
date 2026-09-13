# Worldseed — état des lieux et suite

Document de reprise, **réécrit le 12 septembre 2026**, mis à jour le 13. Il remplace la version du
9 septembre, dont plusieurs affirmations étaient devenues *fausses* et non
seulement incomplètes — elles sont listées au §8 pour que personne ne reparte
dessus.

Trois documents, trois rôles, et il ne faut pas les confondre :

- **`CLAUDE.md`** est la mémoire des **pièges** et des impasses, accumulée session
  après session, avec la mesure qui a tranché. À lire avant de toucher au moteur.
- **Ce document-ci** dit **où en est le projet** et **ce qui reste**. Il ne répète
  pas les pièges, il y renvoie.
- **`README.md`** dit comment reconstituer le projet depuis un clone.

---

## 1. De quoi il s'agit

Un monde procédural pour un RPG sous Unreal Engine 5.8, en deux moitiés bien
séparées :

- **Le générateur**, en Python pur, dans `Tools/WorldGen/`. Il ne connaît pas
  Unreal et n'écrit que des PNG et des JSON. Chaîne : tectonique → climat →
  érosion → climat (2ᵉ passe) → hydrologie → biomes → surfaces → export.
- **L'éditeur**, dans `Tools/UE/`, exécuté *depuis* Unreal, qui consomme ces
  fichiers et construit le niveau.

Le monde est entièrement déterminé par `Tools/WorldGen/rules/world_rules.json` et
la graine. Même graine + mêmes règles = même monde, au bit près.

**Paramètres de référence** : graine 20260909, **8 km de côté**, simulation 2049²,
sortie 4065², soit **1,97 m/pixel**.

**Le monde est une maquette au 1/4** d'un ancien monde de 32 km. Ce qui est
*relatif* ne change pas (angles, degrés de latitude, températures, millimètres de
pluie, fréquences en cycles par monde) ; ce qui est *métrique* se divise par 4 ;
ce qui est une *aire* ou un *débit* se divise par 16. La règle vit dans
`world._comment_echelle`, et **toute nouvelle valeur doit la respecter**.

Deux exceptions assumées, décidées par le propriétaire : `lapseRateCPerKm` est
**multiplié** par 4 (6,5 → 26 °C/km) pour que l'altitude garde son rôle climatique
sur un relief quatre fois plus bas, et `geometryExaggeration` est multipliée par 16
(500 → 8000) pour que les rivières gardent leur largeur réelle — l'échelle humaine,
elle, ne rétrécit pas avec le monde.

Conséquence heureuse de la réduction : **4065² = 256 composants**, soit un
Landscape **unique**. Le tuilage 2×2 imposé par le plafond D3D12 a disparu.

---

## 2. Où sont les choses

### Le générateur — `Tools/WorldGen/`

| Fichier | Rôle |
|---|---|
| `worldgen/` | le générateur, un module par étape |
| `rules/world_rules.json` | **toutes** les règles ; aucun seuil n'est en dur dans le code |
| `terre.py` | **l'instrument principal** : bulletin de conformité terrestre, sur références sourcées |
| `metrics.py` | relevé complet et **diff** de non-régression |
| `diag_climat.py`, `diag_hydro.py` | diagnostics sans resimuler |
| `diag_eau.py` | **l'eau colle-t-elle au relief que voit Unreal ?** (murs d'eau, rivières enterrées) |
| `diag_uds_climat.py` | cohérence climat / biomes / préréglages Ultra Dynamic Sky |
| `tile_world.py` | découpe la sortie pour l'import (**une seule tuile** à 8 km) |
| `export_biome_texture.py` | carte des biomes pour le PCG, avec le **décalage minéral** |
| `export_uds_climate.py` | fabrique les 30 préréglages climatiques `CP_Worldseed_*` |
| `spawn_point.py` | choisit le point d'apparition sur des critères mesurés |
| `montage.py`, `rebuild_report.py`, `refresh_manifest.py`, `tune_coast.py` | outillage annexe |

### L'éditeur — `Tools/UE/`

| Fichier | Rôle |
|---|---|
| `rebuild_world.py` | **la chaîne complète** : vider, relief, matériau, RVT, eau, biomes, végétation, apparition |
| `import_world.py` | import du Landscape (ne pose **pas** le matériau) |
| `water_world.py` | océan, lacs et rivières depuis `lakes.json` / `rivers.json` |
| `vegetation.py` | recettes et graphe du semis PCG, redirections de matériaux |
| `rvt_setup.py` | pose les deux Runtime Virtual Textures sur le terrain et ses proxies |
| `rvt_graft.py` | greffe la teinte RVT **et** le vent d'UDS sur les 11 maîtres de végétation |
| `landscape_material.py` | rejoue le carrelage de l'herbe dans le maître de terrain (hors dépôt) |
| `foliage_lods.py` | pose les groupes de LOD sur les maillages qui n'en ont pas |
| `uds_climate.py` | inspection des préréglages climatiques d'Ultra Dynamic Sky |
| `pcg_bench.py`, `water_zones.py`, `export_godot*.py` | bancs d'essai et export annexe |

### Ce que git suit, et ce qu'il ne suit pas

**Attention : c'est le point que l'ancienne version disait faux.** Le dépôt suit
`Tools/`, `Config/`, `Source/`, la documentation **et `Content/Worldseed/`** —
notre contenu propre : graphes PCG, 30 préréglages climatiques, le calendrier,
`BP_WorldseedClimat`, `MI_WorldseedLandscape`, les `LayerInfo`, la carte.

Sont exclus, et pour des raisons différentes :

| Exclu | Pourquoi |
|---|---|
| `Content/Orasot_Bundle/`, `UltraDynamicSky/`, `Stylized_PBR_Nature/` | **packs payants** — les EULA interdisent d'en redistribuer les sources sur un dépôt public |
| `Content/Worldseed/Materials/M_WorldseedLandscape` | c'est une copie du maître d'Orasot, donc un dérivé du pack |
| `Content/__ExternalActors__/` | régénéré par `rebuild_world` |
| `Saved/`, `Intermediate/`, `Binaries/`, `DerivedDataCache/` | caches et sorties |
| `Plugins/` | VibeUE, dépôt séparé |

**Conséquence pratique, et elle a dicté un choix d'architecture cette semaine :**
un réglage posé sur un **acteur** du niveau vit dans `__ExternalActors__`, donc
hors dépôt, donc perdu au clone suivant. Tout réglage qui doit survivre va dans un
Blueprint de `Content/Worldseed/` ou dans un script de rejeu de `Tools/UE/`.

---

## 3. Le générateur : état

**Il est calé sur la Terre, et c'est vérifiable.** `terre.py` compare le monde à
des références **sourcées** — part des terres émergées, moyenne des précipitations
terrestres, part du climat BWh (Peel, Finlayson & McMahon 2007), parts de surface
par zone climatique déduites de la géométrie d'une sphère — et fait passer les
23 climats réels livrés par Ultra Dynamic Sky dans notre propre diagramme de
Whittaker.

### Le monde actuel en chiffres

Mesuré sur la sortie du **13 septembre**, celle qui est importée dans le niveau.

| | Worldseed | Terre |
|---|---|---|
| terres émergées | **29,2 %** | 29,2 % (ancre) |
| pluie moyenne sur les terres | **713 mm/an** | ~715 mm |
| terres au-dessus de 2000 mm | **6,6 %** | 7 à 8 % |
| terres sous 250 mm | 30,1 % | — |
| relief | −287 à +347 m | — |
| rivières / lacs | 16 / 3 | — |
| pente médiane des terres | 30,6° | — |

Parts des biomes, en pourcentage des terres :

| biome | part | Terre |
|---|---|---|
| désert chaud | **16,0 %** | 21 |
| savane | **11,7 %** | 13 |
| calotte glaciaire | **11,1 %** | 10 |
| forêt tropicale humide | **8,6 %** | 11 |
| taïga | **8,6 %** | 10 |
| toundra | **8,4 %** | 8 |
| forêt tempérée | **8,0 %** | 9 |
| forêt tropicale sèche | 7,7 % | — |
| prairie | 5,9 % | 8 |
| roche nue | 3,9 % | — |
| plage, steppe, alpin, forêt tempérée humide, désert froid, marais | 10,1 % cumulés | — |

Écart absolu moyen aux huit grands biomes terrestres : **16,7 %** — le
meilleur du projet, contre 24,3 % avant la correction de la sécheresse
polaire (13 septembre).

**Ce chiffre COMPARE, il ne juge pas.** Il ne descendra jamais très bas, pour
deux raisons structurelles : les terres étant fixées à 29,2 %, les
parts de biomes sont un jeu à **somme nulle** (agrandir l'Antarctique retire de la
savane) ; et nos 16 catégories ne se ramènent pas aux 8 de référence, qui n'en
couvrent que 68 %. L'utiliser pour départager deux réglages, **jamais** pour juger
un monde dans l'absolu. Le *bulletin* de `terre.py`, lui, se lit dans l'absolu.

### Les grandes corrections de calage

Toutes trouvées par la **mesure**, aucune par la lecture du code.

| Défaut | Cause | Effet |
|---|---|---|
| monde 70 % trop humide | 715 mm est la **moyenne** terrestre, pas la médiane, et l'ancrage portait sur la médiane | trop de forêts, pas assez de prairies |
| déserts à 16,6 °C | pas de prime d'aridité — ciel dégagé, donc plus d'insolation, et pas d'évaporation pour en consommer une part | désert chaud porté à 25,1 °C |
| profil zonal faux | il était en `(lat/90)^k` ; l'insolation suit **cos(latitude)** | écart au réel 1,95 → **0,84 °C** |
| amplitude saisonnière fausse deux fois | croissance linéaire au lieu du **sinus**, et océan amortissant par soustraction au lieu de proportionnellement | écart sur 8 stations 15,4 → **4,7 °C** |
| intérieurs continentaux inexistants | `oceanModerationRangeKm` cinq fois trop grand | ni taïga ni toundra possibles |
| biomes froids mangés | **`bareRockSlopeDeg` à 42° sur un monde de pente médiane 30,6°** | roche nue 12,6 → **3,5 %**, neuf points rendus à tous les biomes |

**Le motif à retenir** : quand aucune valeur d'un paramètre ne satisfait deux
mesures à la fois, c'est la **forme** de la fonction qui est fausse, pas la valeur.
Et un `max(x, 1)` posé pour rattraper un résultat absurde en est presque toujours
le symptôme.

**Ordre des termes, règle d'ingénierie.** La pluie pilote l'érosion, donc le
relief. Un terme de température placé **avant** le calcul des précipitations fait
bouger le terrain, les rivières et les lacs à chaque réglage ; placé **après**, il
ne déplace que les biomes. On choisit selon la causalité réelle : la prime
d'aridité vient après (c'est la sécheresse qui réchauffe), le refroidissement
continental vient avant (un air plus froid porte moins de vapeur).

---

## 4. Le monde dans Unreal : état

Niveau `/Game/Worldseed/Maps/L_Worldseed`, World Partition. **À jour** : relevé des
horodatages, génération 23 h 45 → tuiles 23 h 47 → import 23 h 48. Le niveau porte
bien le monde recalibré.

### Terrain

- **1 Landscape** `Worldseed_Landscape_x0_y0`, 4065² sommets, 16×16 = 256
  composants. Contrôle : le Landscape rend 6916,20 cm là où la heightmap annonce
  6916,80 — **0,6 cm d'écart**.
- **Matériau `MI_WorldseedLandscape`** — l'**instance**, pas le maître. Le rendu du
  pack Orasot tient dans ses 18 surcharges ; le maître seul ne ressemble à rien.
  Le défaut a été commis deux fois : une fois sur le matériau, une fois dans la
  constante de `rebuild_world.py`, qui annulait la correction à chaque
  reconstruction.
- **Deux Runtime Virtual Textures** posées sur le terrain, vérifiées présentes.
  `virtual_texture_render_pass_type = ALWAYS` est **obligatoire** : à son défaut le
  terrain proche paraît aplati, ce qui avait fait abandonner la RVT une première
  fois. Une reconstruction les efface — `rebuild_world` les repose désormais.
- **Carrelage** : l'herbe resserrée ×4 (+34 % d'énergie haute fréquence, aucun
  artefact). **La roche et les graviers restent à 1** — à 4 ils font apparaître un
  moiré hexagonal sur les pentes lointaines. Ce réglage vit dans le **maître**,
  donc hors dépôt : il se rejoue par `landscape_material.regler()`.
- **`DLWE_V3`** (neige et flaques dynamiques) est inséré dans le maître et compile.
  Prouvé fonctionnel : `Snow = 10` couvre la toundra à 185 FPS.

### Eau

1 `WaterBodyOcean` à Z = 0, 3 `WaterBodyLake`, 16 `WaterBodyRiver`, 1 `WaterZone` à
4096 texels (**2,08 m par texel**). **L'emprise de l'océan (`OceanExtents`) doit
valoir celle de la zone** : livrée à 512 m sur un monde de 8 km, elle ne rendait
d'eau qu'autour de l'origine, le *far mesh* de 40 km donnant le change au loin.
`water_world.emprise_ocean()` la cale, et `verify()` le contrôle. `affects_landscape` est à **False** sur tous les
corps d'eau, sans quoi ils creuseraient le relief importé. Rejouable par
`water_world.py`.

Le mur d'eau au bord des lacs a été **corrigé à la source** : le contour était trié
par angle autour du centre, ce qui fait zigzaguer le polygone à travers un lac non
étoilé. De 42-69 % du périmètre en surplomb à **0,3-3,5 %**, p95 sous 0,5 m. Les
rivières sont passées de 46,4 % enterrées et 22,8 % suspendues à **0 % suspendues**,
enterrement plafonné à 1,00 m. Contrôle : `diag_eau.py`.

### Végétation

- **Semis PCG** en génération à l'exécution, ~400 000 instances, un `PCGVolume` et
  **un seul `PCGWorldActor`** — un doublon désarmé suffit à tuer tout le semis en
  silence, et `rebuild_world` détruit désormais les surnuméraires.
- **La carte des biomes porte deux informations** : l'identifiant du biome, et
  **+100 si le sol y est peint en minéral**. Le tapis d'herbe ne prend que `id`,
  les arbres prennent `id` et `id + 100` — un versant raide reste boisé même quand
  la roche affleure. Cela a retiré exactement les 39,6 % de tapis qui poussaient
  sur de la roche. `MINERAL_ID_OFFSET` et `DECALAGE_MINERAL` doivent rester égaux,
  et **rien ne le vérifie**.
- **La RVT teinte le feuillage**, greffée sur 11 maîtres et redirigée sur ~55
  instances (191 entrées de maillage sur 216). La teinte est dosée par un **fondu
  en hauteur** : au ras du sol la plante prend le ton du terrain, au-delà elle garde
  le sien — c'est ce qui permet de teindre une touffe d'herbe sans peindre un arbre
  en terre.
- **Le vent d'Ultra Dynamic Sky** souffle sur 9 maîtres de feuillage, en trois
  classes (petit, moyen, grand). Coût mesuré : **nul** (117 FPS contre 115 sans).
  Il a en revanche imposé `r.Velocity.EnableVertexDeformation=0` : un matériau à
  WorldPositionOffset devient écrivain de vélocité, et 400 000 instances qui en
  gagnent un d'un coup font tomber le RHI.

### Couverture du sol, mesurée en A/B

En prairie, à hauteur d'œil : semis PCG **16,3 %**, herbe du Landscape **39,6 %**,
sol nu **44,1 %**. **L'herbe du Landscape couvre 2,5 fois plus que le semis PCG et
ne coûte aucune instance.** Décision prise : **on ne densifie pas**.

### Ciel, climat et météo

- **Ultra Dynamic Sky remplace les cinq acteurs d'éclairage manuels**, qui sont
  *neutralisés* et non supprimés (visibilité à False, réversible en une ligne).
- **`Simulate Real Sun = True`** — sans quoi `Latitude` ne sert à rien et le soleil
  culmine à 60° à toutes les latitudes.
- **L'exposition est rendue à UDS**, avec ses biais par moment de la journée
  (Day −0,6, Dawn/Dusk −1,2, Night −2,0). L'exposition verrouillée du pack Orasot
  est **incompatible** avec un soleil qui bouge : elle rend la nuit injouable.
- **`BP_WorldseedClimat`** lit la position du joueur deux fois par seconde, en
  déduit la latitude par arc sinus (`lat = degrés(asin(Y / demi-étendue))`, la carte
  étant équivalente-aire), lit le biome dans une grille 128×128 et applique le
  préréglage climatique correspondant, saisons inversées au sud. Il arme aussi, au
  démarrage, le tirage de météo aléatoire et le cycle jour/nuit, et force un
  nouveau tirage à chaque changement de biome.
  **Cet acteur doit être `is_spatially_loaded = False`** : sinon World Partition ne
  l'instancie jamais et il n'existe tout simplement pas en jeu.
- **Ultra Dynamic Sky fait lui-même la conversion physique** : il transforme les
  millimètres mensuels de pluie et de neige de chaque préréglage en probabilités de
  météo par saison, arbitre pluie contre neige par leur rapport, et en déduit
  jusqu'au brouillard et aux tempêtes de sable. Il ne faut **pas** écrire
  `Global Weather State` à la main.
- **Le temps passe à une vitesse jouable, et c'est un calendrier qui le règle.**
  Le grégorien d'UDS donnait une année de **274 heures réelles** et une saison de
  68 : les saisons ne changeaient jamais, et tout le calage saisonnier restait
  invisible. `CAL_Worldseed` (12 mois de 3 jours, 36 jours) ramène l'année à
  **27 h** et la saison à **6 h 45**, la journée restant à 45 min.
  **Ne PAS piloter `Season` directement** : avec `Simulate Real Sun`, c'est la
  DATE qui donne la déclinaison du soleil, et découpler les deux produirait un
  hiver sous un soleil d'été. Le calendrier déplace les deux ensemble.
  `BP_WorldseedClimat` l'assigne au démarrage — posé sur l'acteur UDS, il aurait
  vécu hors dépôt.
- Mesures de recette : biome 6, météo `Partly_Cloudy`, horloge qui avance,
  **107,6 images par seconde, verdict PASS** ; au pôle sud, météo retirée au sort
  et minuteur remis à zéro ; jour 8 sur 36 → « Mid Spring », jour 20 → « Mid
  Summer ».
- **Il neige vraiment**, prouvé en toundra boréale à +64° : le tirage sort `Snow`,
  l'état global prend les valeurs du préréglage et le sol se couvre — plaques
  brunes affleurantes **6,8 % → 0,0 %**. Fréquence mesurée après la correction de
  la sécheresse polaire : **3,1 % par tirage** en hiver, soit ~3 épisodes neigeux
  par hiver de toundra, et ~15 épisodes pluvieux par été (pluie 15,2 %).

---

## 5. La chaîne complète, dans l'ordre

```bash
cd Tools/WorldGen

# Le monde. ~4 min 45 en pleine resolution.
./.venv/Scripts/python.exe -m worldgen

# Banc de calibration : ~50 s, simulation 1025, erosion allegee.
# Comparer des previews ENTRE EUX, jamais un preview a une pleine resolution.
./.venv/Scripts/python.exe -m worldgen --preview

# Controler
./.venv/Scripts/python.exe terre.py                       # bulletin terrestre
./.venv/Scripts/python.exe metrics.py --diff avant.json apres.json
./.venv/Scripts/python.exe diag_eau.py <monde>
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

Enfin, dans l'éditeur, sur `L_Worldseed` :

```python
import sys, importlib
sys.path.insert(0, r"D:\UE\Worldseed\Tools\UE")
import rebuild_world; importlib.reload(rebuild_world)
print(rebuild_world.rebuild(r"D:\UE\Worldseed\Saved\WorldGen\20260909"))
```

Une minute plus tard : relief, dix couches peintes, océan, lacs, rivières, semis
PCG et point d'apparition.

**Deux choses ne sont PAS dans `rebuild()`**, parce qu'elles vivent hors du dépôt
et ne se refont qu'une fois : `landscape_material.regler()` (carrelage de l'herbe)
et la suite `rvt_graft.greffer()` / `greffer_vent()` / `rediriger()`. Le README les
détaille. ⚠ Traiter les matériaux **un ou deux à la fois** : recompiler neuf gros
matériaux d'affilée a fait tomber l'éditeur deux fois.

---

## 6. Ce qui reste à faire

**Au 13 septembre 2026, il ne reste presque rien.** Les sept chantiers ouverts
le 12 ont été traités ou tranchés en deux jours. Ce qui suit est court à dessein.

### 6.1 Ouvert

- **La carte du monde / minimap.** Mise de côté par le propriétaire le
  12 septembre ; tout ce qui avait été monté a été retiré. Le seul verrou
  technique était un **clic dans le menu `Build`** — la construction de la
  texture World Partition n'est pas exposée à Python (vérifié trois fois :
  pas d'API, pas de commande console, pas de binding sur le builder).
  Le reste — recopie de la texture en asset autonome, widget, câblage de la
  touche — est du travail ordinaire.
- **Le marais ne pèse que 0,01 % des terres**, soit 169 pixels dans tout le
  monde. Ses recettes de végétation sont correctes ; c'est sa SURFACE qui est
  négligeable. **C'est une question de générateur** — seuils d'humidité et de
  pente — et non de recette. Accepté en l'état pour l'instant.

### 6.2 Deux murs, à ne pas confondre avec des chantiers

- **La bande 80-90° plafonne à 27 mm** de précipitations contre ~150 sur Terre.
  Quatre variantes essayées — élargie, déplacée, renforcée — le même chiffre :
  à −40 °C la capacité de l'air vaut 0,09 contre 1,48 à l'équateur. Le franchir
  demanderait de truquer Clausius-Clapeyron, piste déjà rejetée.
- **Il n'existe ni roseau, ni nénuphar, ni acacia, ni mousse, ni lichen, ni
  cactus** dans les trois packs — 273 maillages balayés. Ces silhouettes ne
  peuvent pas être créées sans acheter un nouvel asset.

### 6.3 Clos les 12 et 13 septembre, avec ce qu'il ne faut pas rouvrir

| chantier | issue |
|---|---|
| **Sécheresse polaire** | corrigée par un transport méridien de l'humidité. 60-70° : 170 → 456 mm. Toundra 4,66 → 8,09 %, écart absolu moyen 24,3 → **16,7 %**, le meilleur du projet |
| **Rythme du temps** | l'année passait de 274 h à **27 h** via `CAL_Worldseed`. **Ne PAS piloter `Season` directement** : c'est la DATE qui donne la déclinaison du soleil |
| **Preuve de la neige** | il neige, vérifié en PIE. ~3 épisodes par hiver de toundra |
| **Silhouettes de végétation** | toundra sans arbres, taïga en forêt mixte bouleau-conifère, savane sans palmier, marais enrichi. Vérifié en jeu |
| **Climat en mer** | les cellules d'eau prennent le biome de la côte la plus proche — l'eau n'est jamais à plus de 2 km d'une terre |
| **Plage indiscernable du désert** | teinte de sable pilotée par l'ALTITUDE. Saturation −17 %, clarté +15 %, mesuré en A/B avec le personnage comme témoin d'éclairage. Réglable sur l'instance, **sans recompiler** |
| **Scintillement du sable de près** | accepté en l'état. Le résidu tient à une texture de détail dans le maître du pack — chirurgie hors dépôt, sur un matériau qui a fait tomber l'éditeur trois fois |
| **HLOD** | **rien à construire** : zéro `StaticMeshActor`, aucun acteur ne porte de couche HLOD, le Landscape n'est jamais déchargé, et la végétation PCG est générée à l'EXÉCUTION donc invisible à un build. À reprendre le jour où des maillages statiques seront posés à la main |
| **Atlas périmé** | refait sur le monde du 13 |
| **Carte par défaut** | `GameDefaultMap` et `EditorStartupMap` pointent `L_Worldseed` ; `Lvl_ThirdPerson` supprimée. `GlobalDefaultGameMode` reste `BP_ThirdPersonGameMode`, qui porte le pion et les entrées |

## 7. Décisions à ne pas défaire sans en parler

- **L'exagération hydraulique est assumée.** Un monde de 8 km ne peut pas porter de
  fleuve : son plus grand bassin donne physiquement un chenal de 40 cm. Les
  coefficients sont les **vraies** valeurs physiques ; le grossissement passe par
  `geometryExaggeration = 8000`, qui multiplie le *débit apparent* servant à la
  géométrie. Le débit exporté reste le vrai. Mettre ce paramètre à 1 rend le
  générateur strictement physique.
- **`LandscapeGrassOutput` ne se touche pas.** Sa « porte fermée » (un `Floor()` qui
  n'ouvre qu'à un poids exactement égal à 1,0) est un **choix de l'auteur du pack**.
  Décision du propriétaire, 11 septembre : ne pas rouvrir, ne pas retoucher les
  `ConstAlpha`, ne pas rendre les poids peints plus purs. Et de toute façon l'herbe
  du Landscape fonctionne et couvre l'essentiel du sol (§4).
- **Le critère de lac est volontairement sévère.** Le desserrer fait remonter la
  part des lacs au-dessus de la cible de 5 %.
- **Ne jamais rendre un contrôle vert en abaissant son seuil.** Règle du
  propriétaire.
- **Ne rien pousser de payant sur GitHub.** Règle du propriétaire, absolue. Le
  dépôt est public ; les packs et leurs dérivés directs en sont exclus.
- **Un réglage qui doit survivre ne se pose pas sur un acteur** (voir §2).
- **`PCGBiomeCore` est abandonné.** Monté entièrement puis mis de côté : aucune
  instance produite, appariement par **couleur** et non par identifiant,
  expérimental en v0.2, sans API C++ ni garantie entre versions du moteur. Le
  chemin natif marche et est prouvé exact.

---

## 8. Ce que l'ancienne version disait, et qui est faux

À ne pas ressortir d'un vieux clone.

| Elle disait | La vérité |
|---|---|
| « `Content/` est exclu du dépôt » | **Faux.** `Content/Worldseed/` est versionné ; seuls les packs payants, leurs dérivés et `__ExternalActors__` sont exclus. |
| « la RVT a été essayée et abandonnée : herbe bleue puis noire, terrain aplati » | **Faux depuis le 11 septembre.** La RVT marche ; le coupable était `virtual_texture_render_pass_type`, à mettre à `ALWAYS`. |
| « éclairage : soleil directionnel, SkyLight, SkyAtmosphere, brouillard » | **Périmé.** Ultra Dynamic Sky les remplace ; ils sont neutralisés, pas supprimés. |
| « la densité d'herbe ne répond pas — non résolu » | **Tranché.** Ce n'est pas une panne mais un choix du pack. Mesure à l'appui : l'herbe du Landscape couvre 39,6 % du sol, 2,5× le semis PCG. On n'y touche pas. |
| « 34 des 85 Blueprints d'UDS sont en erreur, la scène reste noire » | **Périmé.** Le pack réimporté donne 87 Blueprints sur 87 à jour. |
| « import une tuile par appel (~40 s chacune) » | **Périmé.** À 8 km il n'y a **qu'une** tuile, et `rebuild_world.rebuild()` fait tout. |
| « `M_WorldseedLandscape` est le matériau du terrain » | **Faux.** C'est `MI_WorldseedLandscape`, l'**instance**. Le maître seul ne ressemble à rien. |
| chiffres de biomes : forêt tempérée humide 12,7 %, roche nue 12,4 %… | **Périmés** — antérieurs à la correction de `bareRockSlopeDeg`. Voir §3. |
| « terres 38,5 %, 45 rivières, 5 lacs » | **Périmé.** 29,2 %, 16 rivières, 3 lacs. |
