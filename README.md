# Worldseed

Générateur de monde procédural pour **Unreal Engine 5.8**. Un monde qui va d'un
pôle à l'autre — tectonique, lithologie, climat, érosion, biomes — et dont le
relief est un **champ de densité maillé en voxels** : grottes, gouffres,
dolines, arches, diaclases, mesas et canyons en font partie, ce ne sont pas des
décors posés dessus.

Toute la chaîne est en **C++, dans `Source/Worldseed/Procedural/`, et tourne
dans le jeu** à partir d'une graine choisie au menu. Rien ne se calcule hors du
moteur, rien ne s'importe : il n'y a ni Landscape, ni export d'images, ni étape
d'éditeur entre le menu et le monde. Le monde est déterministe — une graine
donne toujours le même monde — et il part au cache pour que le second lancement
ne le recalcule pas.

**Une seule taille : 64 × 32 km**, simulée sur une grille de 4096 × 2048, soit
une maille de 15,6 m. Le voxel travaille au mètre par-dessus, dans une bande de
100 m sous la surface, et la vue porte à 2400 m grâce à quatre paliers de
résolution cousus par Transvoxel.

L'eau se limite à l'**océan**, confié au plugin Water : rivières, cascades et
lacs ont été retirés le 18 septembre 2026, et `CLAUDE.md` porte les mesures qui
l'ont motivé.

Le climat n'est pas décoratif. Il est calé sur des mesures terrestres sourcées,
et il se vérifie : vingt-trois relevés de stations réelles passent dans notre
propre diagramme de Whittaker à chaque exécution des tests.

---

## Ce que l'on voit en jouant

`L_Menu` est le point d'entrée. On y choisit **une graine**, **un point de
naissance** — au clic sur un globe qui montre le monde que cette graine produit,
ou dans une liste de lieux remarquables : arches, canyons, gouffres — et **un
jeu de textures de sol**. Puis le monde se génère et l'on entre dans
`L_Worldseed_Proc`.

En jeu : une **minimap** avec ses points cardinaux et son cône de visée, une
**carte plein écran** (Tab) où l'on pose un repère et où Ctrl+clic téléporte, un
relevé local en bas d'écran, et Échap pour revenir au menu.

---

## ⚠️ Ce dépôt ne suffit pas à ouvrir le projet tel quel

**Des packs payants sont nécessaires et ne peuvent pas être versionnés ici.**
Les EULA de Fab et du Marketplace autorisent à intégrer un pack dans un *produit
compilé*, jamais à en redistribuer les fichiers sources. Ce dépôt étant public,
les y pousser serait une redistribution.

Ce qu'ils coûtent dépend de ce que l'on veut voir :

| pack | ce qu'on perd sans lui |
|---|---|
| **Ultra Dynamic Sky** | le ciel, le soleil physique, la météo et le cycle jour/nuit |
| **DreamscapeSeries**, **Stylized_Village**, **Stylized_Egypt** | les textures de sol — il reste les dix-neuf couleurs de biome, à plat |

**Le jeu tourne sans eux, et c'est délibéré.** `FWorldseedUdsBridge` est le seul
endroit du projet qui connaisse Ultra Dynamic Sky, et il le joint **par
réflexion** : si le pack manque, on renvoie faux et le jeu continue sans ciel
piloté. Aucune ligne de C++ ne nomme un pack. Le mode de sol par défaut,
« Couleurs de biome », ne demande aucune texture.

**Deux packs ne servent plus au monde actuel** et n'apparaissent donc plus
ci-dessus : `Orasot_Bundle`, dont le matériau à dix couches habillait le
Landscape de l'ancienne carte, et `Stylized_PBR_Nature`, **retiré du projet le
14 septembre 2026**. Ils restent nécessaires à `L_Worldseed`, l'ancienne carte,
que le dépôt conserve.

S'ajoute un composant gratuit : le plugin **VibeUE**, qui expose l'API Python
utilisée par les scripts de `Tools/UE/`. Le jeu n'en a pas besoin.

---

## Ce que ce dépôt contient, et ce qu'il ne contient pas

| | suivi | pourquoi |
|---|---|---|
| `Source/`, `Config/`, `*.uproject` | ✅ | le projet et toute la chaîne de génération |
| `ThirdParty/Transvoxel/` | ✅ | les tables de Lengyel, sous licence MIT, avec leur provenance |
| `Tools/WorldGen/rules/` | ✅ | **de la donnée, pas du code** — les règles et les relevés terrestres |
| `Tools/UE/` | ✅ | scripts d'éditeur visant l'ancienne carte `L_Worldseed` |
| `Content/Worldseed/` | ✅ | notre contenu : cartes, matériaux, préréglages climatiques, police d'icônes |
| packs payants sous `Content/` | ❌ | redistribution interdite — voir ci-dessus |
| `Content/__ExternalActors__/` | ❌ | acteurs World Partition, régénérés |
| `Saved/`, `Intermediate/`, `Binaries/`, `DerivedDataCache/` | ❌ | caches et sorties |
| `Plugins/` | ❌ | plugin tiers, dépôt séparé |

`M_WorldseedLandscape` est exclu pour la même raison de licence : c'est une
copie du matériau maître d'Orasot augmentée d'une dixième couche, donc le graphe
du pack. Son instance `MI_WorldseedLandscape`, elle, est versionnée.

---

## Reconstituer le projet depuis un clone

1. **Unreal Engine 5.8.**
2. **Git LFS** — `git lfs install` *avant* le clone, sinon les `.uasset`
   arrivent sous forme de pointeurs texte et le projet ne s'ouvre pas.
3. Le plugin **VibeUE**, si l'on veut les scripts d'éditeur :
   `git clone https://github.com/kevinpbuckley/VibeUE.git Plugins/VibeUE`
4. Les packs souhaités, depuis votre bibliothèque Fab, installés dans `Content/`
   sous leur nom d'origine.
5. Compiler, ouvrir, jouer. **Il n'y a aucune étape de génération préalable** :
   le monde se calcule au lancement, et la première partie le paie une fois.

---

## Lancer, mesurer, regarder

Tout passe par la ligne de commande, et **rien n'exige l'éditeur ouvert** — ce
qui compte, parce que le lien MCP tombe dès qu'on relance l'éditeur, donc à
chaque compilation.

```powershell
# Jouer.
UnrealEditor.exe Worldseed.uproject -game -windowed -resx=1600 -resy=900

# La tournée photo : se poser devant chaque forme et la photographier.
UnrealEditor.exe Worldseed.uproject /Game/Worldseed/Maps/L_Worldseed_Proc -game -WorldseedPhotos -WorldseedQuitter -windowed -resx=1600 -resy=900

# Le banc : trame, fils, chunks, triangles, mémoire, remplissage.
UnrealEditor.exe Worldseed.uproject /Game/Worldseed/Maps/L_Worldseed_Proc -game -WorldseedBanc -WorldseedQuitter -windowed -resx=1600 -resy=900

# Les tests -- 91 oracles, sans rendu.
UnrealEditor-Cmd.exe Worldseed.uproject -ExecCmds="Automation RunTests Worldseed;Quit" -unattended -nopause -nosplash -nullrhi
```

Le harnais se pilote par surcharges de ligne de commande — `-WorldseedGraine=`,
`-WorldseedRayon=`, `-WorldseedNiveaux=`, `-WorldseedDepartX/Y=`,
`-WorldseedCap=`, `-WorldseedVue=`, `-WorldseedCielClair`… — et c'est une règle
du dépôt : **quand un A/B demande un réglage qui n'a pas de surcharge, on ajoute
la surcharge ; on ne touche pas au fichier de règles.** L'éditer en place change
son empreinte, donc régénère le monde entre les deux moitiés — ce ne serait plus
le même monde.

⚠ **Un chemin de carte commençant par `/` ne survit pas à Git Bash** : MSYS le
prend pour un chemin POSIX et le préfixe de sa racine d'installation. Le jeu
démarre, ne charge rien, quitte, et le journal ne porte aucune ligne
`[Worldseed]` — ce qui ressemble trait pour trait à un module qui ne s'initialise
pas. **Lancer le jeu depuis PowerShell.**

### Les sondes

Vingt-cinq sondes répondent chacune à une question, en commandlet, **l'éditeur
arrêté** — le plugin écoute sur le port 8000 et le commandlet échouerait sinon :

```powershell
UnrealEditor-Cmd.exe Worldseed.uproject -run=pythonscript -script="<fichier.py>" -unattended -nopause -nosplash
```

```python
import unreal
P = unreal.WorldseedProbeLibrary
print(P.probe_terre(20260909, 32000.0, 2048))   # bulletin de conformité terrestre
print(P.probe_biomes(20260909, 32000.0, 2048))  # parts de biomes sur les terres
print(P.probe_zonal(20260909, 32000.0, 2048))   # fait-il froid LÀ OÙ il doit ?
```

Elles prennent toutes une **graine** et une **taille** en paramètres : c'est ce
qui permet d'itérer vite sans payer le monde de production. Le résultat se lit
dans `Saved/Logs/Worldseed.log` — la sortie standard ne le capture pas.

**Les sondes relisent `world_rules.json`, le jeu non.** Changer une règle puis
relancer une partie ne change rien tant qu'aucune sonde n'est passée entre les
deux.

---

## Vérifier que le monde est juste

`ProbeTerre` est l'instrument principal, et le seul qui confronte le monde à des
valeurs **extérieures au projet** : un monde procédural peut être parfaitement
cohérent avec lui-même et faux par rapport à la Terre. Il fait deux choses
distinctes — passer vingt-trois climats de villes réelles dans notre diagramme,
ce qui juge le **classificateur** et non le monde ; puis un bulletin sur des
critères **sourcés**, jamais un pourcentage de biome sorti de mémoire.

Ce bulletin est aussi un **test automatique** (`Worldseed.Terre.ClimatsReels`),
et il se lit station par station : un simple compte laisserait passer un
échange, ce qui s'est déjà produit — l'ajout de la forêt subtropicale humide a
fait passer deux relevés et basculer deux autres, pour un score inchangé.

Les quatre-vingt-onze oracles ne remplacent pas le regard. Une règle du dépôt :
**une forme qui n'a pas été vue n'est pas validée.** Les terrasses des parois de
canyon étaient mesurées justes — écart de pente dur contre tendre de +8,17° — et
rendaient à l'écran une surface qui ne ressemblait pas à de la roche.

---

## Où se règle quoi

Tout vit dans **`Tools/WorldGen/rules/world_rules.json`**. Aucun seuil n'est
écrit en dur dans le code, et chaque valeur non évidente porte un commentaire qui
dit d'où elle vient — `SOURCE`, `CALIBRE` ou `ARBITRAIRE` en premier mot —, ce
qu'on a mesuré en la changeant, et ce qui a été essayé puis rejeté.

**La règle d'échelle n'est pas « diviser par quatre ».** Elle l'a été, du temps
où le monde faisait 8 km ; ce qui la remplace est plus général et se paie cher
quand on l'oublie : **exprimer un réglage par la grandeur qu'on veut tenir**. Une
constante métrique figée sur une référence fausse un monde entier dès que la
carte change, et ce fichier a payé la leçon cinq fois — `shelfWidthKm`,
`mountainWidthKm`, `oceanModerationRangeKm`, `lapseRateCPerKm` et
`lithologieSocleElevationM` sont tous devenus des **fractions du monde** ou des
**quantiles**, qui ne connaissent pas l'échelle. Ce qui est *relatif* ne change
jamais : fréquences en cycles par monde, seuils normalisés par centile, angles,
degrés de latitude, températures, millimètres de pluie.

`world.sizeKm` vaut 8 et **n'est pas la taille du monde** : c'est la hauteur de
référence du calage métrique, dont `WorldseedVerticalScale` tire son rapport —
32000 / 8000 = 4,0, soit exactement son plafond. Les deux se sont longtemps
trouvées égales, ce qui masquait la distinction.

⚠ **L'empreinte des règles est un MD5 du fichier entier.** Modifier un simple
commentaire invalide tous les mondes en cache et force une régénération d'environ
200 s. Le monde obtenu est identique ; il n'y a rien à relancer, juste à la payer
une fois.

---

## Documentation

- **`CLAUDE.md`** — le registre. Les pièges rencontrés, la mesure qui les a
  révélés, et les pistes abandonnées avec la raison de ne pas les rouvrir. À lire
  avant de toucher au moteur : c'est le document le plus utile du dépôt, et de
  loin le plus long.
- **[`Docs/atlas-worldseed.html`](Docs/atlas-worldseed.html)** — le système
  climatique, ses formules, son calage terrestre, et le diagramme de Whittaker du
  projet avec les vingt-trois climats réels posés dessus. Fichier autonome : il
  s'ouvre par un double-clic, sans serveur.
- **[`.claude/rules/`](.claude/rules/)** — les conventions, une par fichier.
  `iconographie.md` dit d'où viennent les icônes des écrans : **Material Symbols**
  pour l'interface, **game-icons.net** pour le monde — parce que le catalogue
  Google n'a ni arche, ni grotte, ni canyon. Dans les deux cas, **aucun
  `.uasset`** : le fichier brut est versionné et lu au démarrage.

**Une réserve sur les chiffres.** Ceux de l'atlas sont des mesures prises sur une
graine et une résolution données, et il le dit en tête. Un relevé sur une seule
graine à la résolution de travail **ne se généralise pas** à la résolution de
production — le dépôt s'y est déjà laissé prendre.
