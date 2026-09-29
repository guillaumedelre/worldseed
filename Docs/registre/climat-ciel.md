# Registre Worldseed — le climat et le ciel

> **CE FICHIER NE SE LIT PAS EN ENTIER, IL SE CHERCHE.** Il porte le recit des
> seances : ce qui a ete mesure, ce qui a tranche, et surtout **les pistes
> abandonnees**, pour qu'on ne les retente pas. Les REGLES qu'on en a tirees
> vivent dans `CLAUDE.md`, chargé a chaque tour ; ici se trouve la PREUVE.
>
> ```
> grep -n -i '<le mot du sujet>' Docs/registre/*.md
> grep -A40 'le titre exact' Docs/registre/climat-ciel.md
> ```
>
> **Le contenu est CUMULATIF.** Certaines notes decrivent un etat qui n'existe
> plus ; les plus dangereuses portent une marque de peremption. Avant d'agir sur
> une note, verifier que ce qu'elle nomme existe encore — une note qui survit a
> ce qu'elle decrit coute plus cher que pas de note.
>
> **Ce fichier** : climat, biomes, Koppen, calage terrestre, Ultra Dynamic Sky et Weather, meteo, aurores, orages, poussiere, vent, brume, neige, son.

### Ultra Dynamic Sky : le pack COMPILE maintenant (11 septembre 2026)

**UNE NOTE DU 9 SEPTEMBRE DISAIT L'INVERSE, ET ELLE A ETE SUPPRIMEE** -- elle
affirmait que 34 des 85 Blueprints d'UDS etaient en erreur et que la scene
restait noire. Le pack reimporte donne **87 Blueprints sur 87 en `BS_UP_TO_DATE`**, et l'acteur
instancie bien ses **56 composants** (Sun, Moon, SkyAtmosphere, HeightFog,
VolumetricCloud, deux SkyLight). Ne pas rouvrir ce constat d'echec.

- **UDS embarque son propre systeme de CLIMAT, pas seulement un ciel.** Un
  `UDS_Climate_Preset` porte 4 saisons x (temperature moyenne haute, moyenne
  basse, pourcentage de ciel couvert, pluie mensuelle, neige mensuelle) +
  poussiere. Les 23 presets livres sont des RELEVES DE VILLES REELLES, chacun
  citant sa station et sa source weatherspark : c'est une reference terrestre
  utilisable telle quelle (`Tools/WorldGen/terre.py` s'en sert).
- **Trois pieges de lecture de ces presets, tous payes en les lisant plutot
  qu'en raisonnant de tete** : `Rainfall (mm)` est un cumul **MENSUEL**, pas
  saisonnier (Tropical_Rainforest 145 a 184) ; `Snowfall (mm)` est un
  **EQUIVALENT-EAU**, pas une hauteur de neige (Oceanic porte 36,2 mm de pluie
  ET 45,5 mm de neige le meme mois) ; l'ecart diurne reel ne fait que **4 a
  9 degres**, ce sont des MOYENNES haute et basse, pas des extremes.
- **`Weather_Override_Volume` n'est PAS pilotable depuis Python.** Le volume se
  connecte bien a UDW (reference `UDW` valide, `Local Weather State` cree), mais
  son `Total Sphere Bounds` reste fige a la valeur par defaut (400 m) : c'est le
  *construction script* qui la recalcule depuis la spline, et il ne peut pas
  etre relance depuis Python (`rerun_construction_scripts` non expose ; ni un
  `set_actor_location`, ni un `set_editor_property` ne le declenchent). Comme
  `Filter Local Weather Actors by Proximity` s'appuie sur ce rayon,
  `Player is In Volume` reste False meme au centre exact du volume. Ces volumes
  doivent etre traces a la main dans l'editeur, ou pas du tout.
- **`BlueprintService.list_variables` renvoie 0 pendant un PIE.** Toute
  l'introspection Blueprint doit se faire PIE arrete. En PIE, passer par
  `acteur.call_method("Nom De La Fonction", (args,))`, qui marche.
- **Le viewport de l'editeur ne juge PAS l'exposition du jeu** : il a la sienne
  (`exposure_ev100` dans `ViewportService.get_viewport_info()`). La meme scene
  etait sombre et bleutee dans l'editeur et correctement exposee en PIE. Juger
  l'exposition en PIE, comme la vegetation.
- **UDS et une exposition verrouillee s'excluent.** `Worldseed_PostProcess` ne
  portait QUE trois surcharges, toutes d'exposition (min = max = -1, bias 0).
  C'etait le bon choix avec un soleil fixe ; des qu'un cycle jour/nuit tourne,
  c'est lui qui rend la nuit totalement noire. UDS a son propre pilotage
  (`Apply Exposure Settings`, histogramme, plage -10 a +20, et des biais par
  moment de la journee). Surcharges desactivees, exposition rendue a UDS.
- **UDS remplace les cinq acteurs d'eclairage manuels** (`Worldseed_SunLight`,
  `_SkyLight`, `_Atmosphere`, `_Fog`). Ils sont NEUTRALISES, pas supprimes :
  visibilite de leurs composants a False, ce qui se defait en une ligne. Ne pas
  les detruire -- la suppression d'un acteur World Partition est ecrite sur le
  disque immediatement.
- **Changer de carte avec `load_level` pendant que le proprietaire travaille
  declenche une boite de dialogue de sauvegarde chez lui.** Verifier
  `LevelEditorSubsystem.get_current_level()` AVANT d'editer quoi que ce soit :
  la carte ouverte n'est pas forcement `L_Worldseed` (elle etait sur
  `/Game/ThirdPerson/Lvl_ThirdPerson`).

### 715 mm n'est pas une mediane, c'est une moyenne (11 septembre 2026)

**La plus grosse erreur de calibration trouvee a ce jour, et elle se cachait
dans un commentaire juste.** `climate.generate` mettait les precipitations a
l'echelle en ancrant leur **mediane** sur 715 mm/an, avec ce commentaire :
"ancree sur une valeur PHYSIQUE : la mediane des precipitations terrestres". Or
**715 mm est la MOYENNE terrestre, pas la mediane**, et la distribution des
pluies est tres dissymetrique : les deserts tassent la moitie basse, les
tropiques etirent la haute.

Mesure sur la graine 20260909, avant correction :

| | monde | Terre |
|---|---|---|
| pluie moyenne sur les terres | **1217 mm** | ~715 mm |
| pluie mediane | 690 mm | -- |
| terres au-dessus de 2000 mm | **23,7 %** | 7 a 8 % |

Le monde etait donc **70 % trop humide en moyenne**, ce qui se voyait dans les
biomes : trop de forets (foret temperee humide a 15,3 % des terres), pas assez
de savane (4,0 %), de prairie ni de desert. Corrige en ancrant la MOYENNE
(`precipitation.targetMeanLandMm`), en deux passes parce que l'ecretage a
`maxPrecipMm` retire de l'eau et fait manquer la cible a la premiere.

**Regle a retenir : quand une constante physique sert d'ancrage, verifier de
QUELLE statistique il s'agit.** Moyenne, mediane et mode d'une distribution
dissymetrique n'ont rien a voir, et le code ne peut pas s'en apercevoir.

**Trois metriques ajoutees au manifeste pour que ca ne se reproduise pas** :
`precipMeanLand`, `precipWetPct` (part des terres au-dessus de 2000 mm) et
`precipAridPct` (sous 250 mm). L'ecart entre `precipMeanLand` et `precipMedian`
est la dissymetrie, et elle reste desormais sous les yeux.

### Un desert est plus chaud que sa latitude (11 septembre 2026)

Le generateur ne connaissait que la latitude et l'altitude : nos deserts chauds
plafonnaient a **16,6 C** de moyenne quand les relevés reels d'UDS sont a 22-34.
Il manquait la prime d'aridite -- ciel degage, donc plus d'insolation au sol, et
pas d'evaporation pour en consommer une part en chaleur latente
(`temperature.aridityHeatC`, 8 C).

**Le terme est applique APRES le calcul des precipitations, et c'est le point
d'ingenierie a retenir.** L'ordre est d'abord causal (c'est la secheresse qui
rechauffe), mais il a surtout une consequence pratique : la pluie pilote
l'erosion, donc le relief. Calculer la prime AVANT la pluie ferait bouger le
terrain, les rivieres et les lacs a chaque reglage de cette seule valeur. Dans
le bon ordre, regler `aridityHeatC` ne deplace QUE les biomes. Mesure : 8 C
porte le desert chaud a 25,1 C et ne fait changer de biome qu'a **4,2 % des
terres**, forets inchangees.

### La ZCIT ne culmine pas a l'equateur (11 septembre 2026)

Erreur commise puis corrigee dans `export_uds_climate.py` : la modulation
saisonniere de la pluie faisait culminer l'effet de ZCIT **a l'equateur**. C'est
l'inverse du reel. L'equateur est humide toute l'annee parce que la ZCIT y passe
**deux fois** par an ; la saison seche marquee est vers **10 a 20 degres**, ou
elle ne passe qu'une fois -- c'est ce qui definit la savane. Le poids suit
desormais un demi-sinus, nul a l'equateur comme au tropique, maximal a
mi-chemin.

### Quand un reglage resiste, c'est souvent la FORME qui est fausse (11 septembre 2026)

Trois defauts de climat trouves le meme jour, et aucun des trois ne se corrigeait
en changeant un nombre. C'est le motif a retenir : **si aucune valeur d'un
parametre ne satisfait a la fois deux mesures, c'est la fonction qui est
mauvaise, pas la valeur.**

- **Le profil zonal de temperature.** Il etait en `(lat/90)^k`. L'energie solaire
  recue varie en moyenne annuelle a peu pres comme **cos(latitude)** : c'est la
  forme de premier ordre. Ecart absolu moyen au profil zonal reel : `sin^2` 7,02 C
  (le reflexe habituel, et le pire), `(lat/90)^2` 1,95, `(lat/90)^1,8` 1,11,
  **`cos(lat)` 0,84**.
- **L'amplitude saisonniere**, fausse DEUX fois a la fois : elle croissait
  lineairement avec la latitude alors que le contraste saisonnier suit le
  **sinus** de la latitude, et l'ocean amortissait par **soustraction** alors
  qu'il amortit **proportionnellement**. Symptome qui aurait du alerter : la
  soustraction donnait des amplitudes NEGATIVES aux basses latitudes maritimes,
  qu'il fallait borner a 1. **Un `max(x, 1)` pose pour rattraper un resultat
  absurde est presque toujours le signe d'une mauvaise forme.** Ecart sur huit
  stations reelles : lineaire 22/6 **15,4 C** (16 C d'amplitude a Iakoutsk, qui
  en fait 57) contre **4,7 C** en sinus amorti proportionnellement.
- **Le forcage du continent polaire.** Il montait en smoothstep sur TOUT son
  rayon, donc a mi-chemin il ne remontait le fond oceanique qu'a mi-hauteur : le
  continent n'emergeait qu'au pole meme. Un continent est un **plateau avec un
  littoral**, pas un degrade : la rampe ne doit occuper que le tiers exterieur.
  Calotte glaciaire 5,6 -> 9,6 % des terres.

**Deux constantes qui n'avaient jamais ete calees**, et qui ne se voyaient pas
parce que le code etait juste :
- `oceanModerationRangeKm` = 0,75 km, **cinq fois trop grand**. Sur Terre le
  passage maritime -> continental se fait sur 300 a 800 km, soit 0,06 a 0,16 km
  rapporte a un monde 5000 fois plus petit. La continentalite plafonnait donc a
  0,16 de mediane au lieu d'environ 0,6, et **les interieurs continentaux
  n'existaient pas climatiquement** : ni taiga ni toundra possibles.
- La continentalite ne jouait que sur l'AMPLITUDE, **jamais sur la moyenne**.
  Aux hautes latitudes l'ocean ne lisse pas seulement l'annee, il la rechauffe
  (Iakoutsk -8,8 C contre Bergen +7,6 C, 16 degres pour deux degres de latitude).

**ORDRE DES TERMES : une regle d'ingenierie qui vaut d'etre rappelee.** La pluie
pilote l'erosion, donc le relief. Un terme de temperature place AVANT le calcul
des precipitations fait donc bouger le terrain, les rivieres et les lacs a chaque
reglage ; place APRES, il ne deplace que les biomes. On choisit selon la
causalite reelle : la prime d'aridite vient APRES (c'est la secheresse qui
rechauffe), le refroidissement continental vient AVANT (un air plus froid porte
moins de vapeur, la pluie doit le voir).

**Ne jamais recopier une formule dans deux fichiers.** `export_uds_climate.py`
recalculait l'amplitude saisonniere de son cote ; a la premiere correction de
forme, les presets Ultra Dynamic Sky auraient garde l'ancienne. Extraite en
`climate.seasonal_amplitude`, publique a dessein.

**Le score de `terre.py` COMPARE, il ne juge pas.** L'ecart absolu moyen aux huit
grands biomes terrestres plafonne vers 28-32 % quel que soit le reglage, pour
deux raisons structurelles : les terres etant fixees a 29,2 %, les parts de
biomes sont un jeu a SOMME NULLE (agrandir l'Antarctique retire de la savane) ;
et nos huit biomes de reference ne couvrent que 62 % de nos terres, le reste
allant a "roche nue", "foret tropicale seche", steppe, plages et lacs, alors que
la Terre compte sa roche desertique DANS le BWh. Utiliser ce chiffre pour
departager deux reglages, jamais pour juger un monde dans l'absolu. Le
**bulletin** de `terre.py`, lui, ne repose que sur des criteres sources et se
lit dans l'absolu.

**Mesurer avant d'accuser, meme soi-meme.** J'ai attribue l'absence de taiga a
des continents trop petits. Mesure : indice de fragmentation (rayon du disque
equivalent divise par la distance maximale a l'ocean) **2,58 chez nous contre
2,60 sur Terre**, et nos trois plus grandes masses portent 89,5 % des terres
contre 68 % sur Terre. Nos continents sont deja plus concentres que ceux de la
Terre ; les agrandir aurait ete une regression. La cause etait l'amplitude
saisonniere.

**Critere reel refuse a dessein** : classer la toundra par le critere de Koppen
ET (mois le plus chaud sous 10 C), qui est pourtant LE critere de la limite des
arbres, detruit la taiga ici (0,1 a 6 % des terres contre 10 attendus). Notre
amplitude a 60 degres plafonne a 30 C contre 38 sur Terre meme apres correction,
donc "le mois le plus chaud" reste trop froid pour que ce critere morde. Ne pas
le retenter sans avoir d'abord rapproche l'amplitude du reel.

### UDS : le soleil ignore la latitude tant que `Simulate Real Sun` est a False (11 septembre 2026)

`Ultra_Dynamic_Sky.Latitude` ne sert A RIEN par defaut. UDS trace alors un arc
solaire simplifie, dont l'elevation de midi vaut `90 - Sun Pitch` (30 par
defaut, donc 60 degres), IDENTIQUE a toutes les latitudes. Mesure : latitude 0
et latitude 47 donnaient toutes deux 60 degres a midi. Il faut poser
**`Simulate Real Sun = True`**, apres quoi le soleil devient exact :

    latitude  5 deg -> 87,1 deg a midi   (attendu 87,2)
    latitude 47 deg -> 45,4 deg          (attendu 45,2)
    latitude 85 deg ->  7,6 deg          (attendu  7,2)

- **PIEGE DE MESURE : UDS met le soleil a jour sur son TICK d'editeur, pas au
  moment ou l'on ecrit la propriete.** Ecrire `Time of Day` puis lire la rotation
  du composant `Sun` DANS LE MEME SCRIPT rend l'etat PRECEDENT. Il faut ecrire
  dans un appel et lire dans le suivant, et le viewport doit etre en temps reel
  (`ViewportService.set_realtime(True)`). Premiere serie de mesures entierement
  faussee par ce decalage.
- **NE PAS enchainer `editor_request_end_play()` et un chargement d'asset dans
  le meme script** : la fermeture du PIE est asynchrone, et `load_asset` rend
  None pendant la transition. Symptome trompeur : 31 assets d'un coup declares
  introuvables, alors que `does_asset_exist` les voit tous a l'appel suivant.
- **La latitude n'est plus proportionnelle a Y** depuis le passage a la carte
  equivalente-aire : `lat = degres(asin(Y / demiEtendue))`. Au point
  d'apparition (Y = 292 323 cm), la bonne valeur est **+46,95 deg** la ou un
  produit lineaire aurait donne **+65,77** -- 19 degres d'erreur. Le manifeste
  n'ecrit donc plus `degreesPerMetre` dans ce mode : mieux vaut une cle
  manquante qu'un chiffre faux.

### Piloter UDS depuis le biome : ce qui marche, et les impasses (11 septembre 2026)

`BP_WorldseedClimat` lit la position du joueur toutes les demi-secondes, en
deduit la latitude par arc sinus, l'ecrit dans UDS, lit le biome dominant dans
une grille 128x128 et applique le preset climatique correspondant. Verifie en
conditions reelles : au point d'apparition, biome lu 6 contre 6 attendu ; au
pole sud, biome 3 (calotte) et latitude -77,16 contre -77,16.

**LA PREUVE QUE L'INVERSION D'HEMISPHERE MARCHE** : au pole SUD, UDS annonce un
« ete » a -41,8 C et un « hiver » a -8,7 C. L'ete y est plus froid que l'hiver,
et c'est exactement ce qu'on veut -- le preset austral porte ses saisons
echangees, donc quand UDS dit « ete » (ete boreal), c'est l'hiver austral.

**IMPASSE : la DataTable.** `GetDataTableRow` n'est PAS constructible depuis
Python -- c'est un `K2Node_GetDataTableRow` sans cle de spawner, et
`discover_nodes` ne remonte que `GetDataTableRowStruct`, qui ne sert a rien ici.
La table `DT_WorldseedGrille` a donc ete abandonnee au profit d'UNE CHAINE de
39 120 caracteres stockee en variable, decoupee en deux temps :
`ParseIntoArray(";")` une seule fois au BeginPlay, puis `ParseIntoArray("-")`
sur la seule ligne utile a chaque mise a jour.

**PAS DE ZERO DE REMPLISSAGE DANS LES NOMS D'ASSETS qu'un Blueprint doit
reconstruire.** Blueprint n'a aucun moyen simple de formater un entier sur deux
chiffres : il faudrait une branche et deux concatenations de plus dans un chemin
appele toutes les demi-secondes. Les presets sont donc `CP_Worldseed_6_N` et non
`CP_Worldseed_06_N`. Cout : le navigateur trie 10 avant 3, et c'est tout.

**`MakeSoftObjectPath` ne se branche PAS sur `LoadAsset_Blocking`** : le premier
rend un `FSoftObjectPath`, le second attend un `TSoftObjectPtr`. Il faut
`Conv_SoftObjPathToSoftObjRef` entre les deux. Et le chemin doit etre complet,
avec le nom d'objet apres le point : `/Game/.../CP_Worldseed_6_N.CP_Worldseed_6_N`.

**LES TEMPERATURES D'UDW SONT EN FAHRENHEIT** par defaut (`Temperature Scale`).
Lire 64,6 et croire a un bug alors que c'est 18,1 C fait perdre du temps.

**Trois methodes d'introspection qui MENTENT, et il faut les connaitre :**
- `get_variable_info(...).default_value` rend une chaine VIDE pour une valeur
  longue, alors que la valeur est bien posee. Verifier avec
  `get_property(bp, nom)`, qui dit vrai -- teste jusqu'a 39 120 caracteres.
- `does_asset_exist` rend **False** pendant un PIE pour un asset qui existe.
  Comme `list_variables`, qui rend 0. Toute verification d'assets se fait PIE
  arrete.
- `get_node_pins(...).default_value` rend vide pour les broches de CLASSE et
  d'OBJET, meme renseignees. L'audit des broches non connectees remonte donc des
  faux positifs sur `ActorClass` et sur les `self`.

**Une variable creee par `add_member_variable` n'est PAS editable sur
l'instance** : `set_editor_property` sur l'acteur pose echoue avec « cannot be
edited on instances ». Passer par le defaut de CLASSE
(`set_variable_default_value`).

### Exposition : le reglage du pack et un cycle jour/nuit sont incompatibles (11 septembre 2026)

Arbitrage tranche a l'image, six captures au meme endroit et au meme cadrage.

|                              | midi                          | 22 h 30                 |
|------------------------------|-------------------------------|-------------------------|
| exposition VERROUILLEE (pack)| riche, contrastee             | **quasi noire, injouable** |
| UDS, biais a 0               | claire, un peu delavee        | trop claire, pas une nuit |
| **UDS + biais regles**       | **plus riche, lisible**       | **nuit credible et jouable** |

L'exposition verrouillee du pack Orasot n'est pas « meilleure » : elle est
**incompatible avec un soleil qui bouge**. Une exposition fixe ne peut pas
couvrir les quatorze diaphragmes qui separent midi de minuit. Le pack la
verrouillait parce que son soleil ne bougeait jamais.

**LA SOLUTION N'ETAIT PAS DE CHOISIR ENTRE LES DEUX.** UDS expose un
`Exposure Bias` PAR MOMENT DE LA JOURNEE -- Day, Dawn/Dusk, Night, Cloudy,
Foggy -- et ils etaient **tous a 0**, c'est-a-dire jamais regles. C'est la que
se recupere la richesse du pack sans perdre le cycle. Retenu :

    Exposure Bias Day        -0,6
    Exposure Bias Dawn/Dusk  -1,2
    Exposure Bias Night      -2,0

**Ne PAS laisser Dawn/Dusk a 0 quand les deux autres sont negatifs** : la valeur
bondirait de -0,6 a 0 puis a -2,0 en quelques minutes de jeu, ce qui se verrait
comme un flash au crepuscule. La valeur intermediaire est calculee, pas mesuree ;
verifiee sur une image a 19 h, pas sur la transition en mouvement.

**Rappel de methode** : cette exposition ne se juge qu'en PIE. Le viewport de
l'editeur a la sienne (`exposure_ev100` dans `ViewportService.get_viewport_info()`)
et rend la meme scene sombre ou delavee sans rapport avec le jeu.

### Biomes froids : le coupable n'etait pas le climat (11 septembre 2026)

**CE QUE JE CHERCHAIS** : toundra a 2,91 % des terres contre 8 sur Terre, taiga
5,58 contre 10. **CE QUE J'AI TROUVE** : le froid ne manque pas. **30,6 % des
terres sont deja sous 5 degres**, quand la Terre porte 28 % de biomes froids.
Les temperatures et la repartition des terres en latitude sont bonnes (29,3 %
des terres au-dela de 50 degres, comparable a la Terre).

**LE VRAI COUPABLE : LA ROCHE NUE, DEUXIEME BIOME DU MONDE A 12,6 % DES
TERRES.** Le seuil `bareRockSlopeDeg` valait 42 degres sur un monde dont la
pente MEDIANE sur les terres est de **30,6 degres**, et dont 22,8 % des terres
depassent 40. Porte a 55 -- un versant boise a 45 degres reste une foret --, la
roche nue tombe a 3,52 % et rend neuf points de terres a TOUS les biomes.

Mesure a pleine resolution, avec les seuils de desert froid abaisses :

    biome                avant   apres   Terre
    toundra               2,91    4,47     8,0
    taiga                 5,58    6,14    10,0
    foret tropicale hum.  9,18   10,91    11,0
    desert chaud         14,64   15,82    21,0
    savane                8,60    9,15    13,0
    ecart absolu moyen   31,6 %  24,3 %

**UNE REGLE DE BIOMES NE TOUCHE PAS AU RELIEF, UNE REGLE DE PLUIE SI.** Verifie
a l'octet pres : apres regeneration complete, `height_16bit.png` a la MEME
empreinte SHA-256. Rivieres, lacs et point d'apparition sont inchanges, seules
la carte des biomes et les couches peintes bougent. A l'inverse,
`saturationScaleC` deplace 0,56 % des pixels de relief jusqu'a 11 m, parce que
la pluie pilote l'erosion. **Verifier de quel cote de l'erosion tombe un
reglage avant d'annoncer son cout.**

**LA SECHERESSE POLAIRE RESTE OUVERTE, et c'est un manque du MODELE.**

    latitude    nous    Terre (ordre)
    60-70 deg   170 mm    ~500 mm
    70-80 deg    11 mm    ~250 mm
    80-90 deg     0 mm    ~150 mm

Essaye et NON retenu : `saturationScaleC` 16 -> 24 porte 60-70 a 297 mm et
l'ecart moyen a 20,5 %, mais s'eloigne de Clausius-Clapeyron (echelle theorique
10/ln2 = 14,4 degres). C'est un pansement sur un mecanisme absent -- aucun
transport d'humidite par les tempetes vers les poles -- et il change le relief.
Sans effet mesurable : `polarFrontStrength` (0,45 -> 0,70) et `subsidenceFactor`
(0,75 -> 0,60), moins de 0,2 point chacun.

**BANC DE CALIBRATION** : `python -m worldgen --preview` tourne en **50 s**
(simulation 1025, erosion allegee) et suffit a departager des reglages de
biomes. Une passe pleine resolution prend 4 min 45. Comparer les previews entre
eux, jamais un preview a une pleine resolution.

**RAPPEL SUR LE SCORE** : nos 16 categories contre 8 de reference. Apres
correction les huit couvrent 68 % de nos terres ; le reste est en foret
tropicale seche, desert froid, steppe, plage, alpin. Un ecart moyen de 24 % ne
veut donc pas dire "24 % faux" -- il sert a COMPARER deux reglages.

### DLWE : la neige marchait depuis le debut, il ne neigeait jamais (12 septembre 2026)

**LES TROIS PISTES NOTEES PLUS HAUT ETAIENT TOUTES FAUSSES.** Le materiau n'a
jamais eu de defaut. Preuve : en PIE, poser `Snow = 10` sur le
`Global Weather State` d'UDW couvre la toundra d'un manteau blanc convaincant,
a **185 FPS**. Ni le readme du pack, ni `Snow Color and Alpha`, ni un cablage
manquant n'etaient en cause.

**LA CHAINE REELLE**, et ce n'est pas celle qu'on soupconnait :

    Global Weather State.Snow  ->  collection UltraDynamicWeather_Parameters
                               ->  parametre `Snowy`  ->  DLWE_Snow  ->  sol

`Material Snow Coverage`, que la session precedente forcait a 1,0 sans effet,
**n'est PAS le pilote** : il valait encore 0,0 pendant que le sol etait blanc.
`Currently Snowing` vaut False aussi -- il parle des flocons qui tombent, pas
de la neige au sol.

**POURQUOI IL NE NEIGE JAMAIS** : `Ultra_Dynamic_Weather.Random Weather
Variation` vaut **DISABLED**, et rien d'autre ne pilote la meteo. L'etat global
reste donc a `Snow = 0, Rain = 0, Cloud Coverage = 3,8` en permanence, quel que
soit le biome ou la saison. Le froid seul ne declenche rien : en pleine toundra,
`Currently Snowing` = False.

Essaye et INSUFFISANT : passer `Random Weather Variation` a `HOURLY` puis
appeler `Change to Random Weather Variation` douze fois de suite ne change
AUCUNE valeur de l'etat global (neige 0, pluie 0, nuages 3,8 aux douze
tirages). Activer le reglage ne suffit donc pas.

**COMMENT VERIFIER CE GENRE DE CHOSE**, puisque `get_all_level_actors()` rend
une liste vide en PIE : passer par le monde de jeu --
`UnrealEditorSubsystem.get_game_world()` puis
`GameplayStatics.get_all_actors_of_class(monde, unreal.Actor)` et filtrer sur
le LIBELLE. `load_blueprint_class` + `get_all_actors_of_class` sur la classe
exacte a rendu 0 acteur, alors que l'acteur existe.

**PIEGE PAYE COMPTANT : modifier une MaterialParameterCollection fait tomber
l'editeur.** Changer une valeur par defaut force la mise a jour de TOUS les
materiaux qui la referencent -- dont notre materiau de terrain avec DLWE --,
et le RHI D3D12 de cette machine n'y survit pas
(`EXCEPTION_ACCESS_VIOLATION`). Pire, le gestionnaire de crash a SAUVEGARDE la
collection modifiee : le pack est reste avec `Snowy = 1` jusqu'a la relance.
Recette qui passe : viser le ciel avec la camera, couper le temps reel
(`ViewportService.set_realtime(False)`), poser les valeurs dans un appel, et
SAUVEGARDER DANS UN APPEL SEPARE.

**RESTE A FAIRE** : piloter la meteo. Le plus coherent avec le reste du projet
est de le faire depuis `BP_WorldseedClimat`, qui applique deja un preset par
biome : poser `Global Weather State.Snow` et `.Rain` d'apres les millimetres
mensuels de pluie et de neige du preset et la saison en cours. Tout est deja
dans `uds_climate.json`.

### La météo se pilote par le BIOME, et UDS fait déjà tout le calcul (12 septembre 2026)

**LE PIÈGE PRINCIPAL : j'allais recoder ce que le pack fait déjà, et mieux.**
Le plan de départ était de lire les millimètres de pluie et de neige du préréglage,
de les convertir à la main en intensité 0-10 et de les écrire dans
`Global Weather State`. C'était inutile. `Apply Climate Preset Object` appelle
`Make Climate Probability Map` (229 nœuds, sur `Random_Weather_Variation`), qui
convertit déjà, **par saison** :

- `Cloudy Percentage` → répartition ensoleillé / nuageux ;
- `Rainfall + Snowfall` → `Precipitating Percentage`, via `Map Range Clamped`
  puis `Power` puis `Lerp` ;
- `Rainfall / (Rainfall + Snowfall)` → arbitrage pluie contre neige ;
- `Dust/Sand Present` → probabilité de tempête de sable ;
- `Rainfall + Snowfall` → probabilité de brouillard ;
- puis normalisation de toutes les probabilités à 100.

**Règle à en tirer : avant d'écrire une conversion physique vers un pack, chercher
si le pack ne la fait pas déjà.** Ici la recherche tenait en un appel —
`get_nodes_in_graph` sur le graphe `Apply Climate Preset Object`.

**LA VRAIE CAUSE DE L'INERTIE : `Random Weather Variation = DISABLED`.**
`BP_WorldseedClimat` appliquait fidèlement un préréglage par biome, UDS remplissait
fidèlement ses quatre cartes de probabilités (8 à 10 entrées par saison)… et
personne n'y piochait jamais. Un seul énumérateur rendait tout le calage climatique
décoratif. **Quand une chaîne complète ne produit rien, chercher d'abord
l'interrupteur, pas l'erreur de calcul.**

**`Animate Time of Day` était à False.** L'horloge d'UDS était figée à 11 h 00.
Conséquences en cascade : les modes `DAILY` et `HOURLY` ne se déclenchent jamais,
et surtout la **saison ne progresse pas** — nos préréglages portent quatre saisons,
une seule servait. Jour de 30 min + nuit de 15 min, soit 45 minutes réelles pour
24 h, et `Time of Day` avance de 1 unité par 1,125 s réelle.

**DEUX FONCTIONS D'UDW SONT PROTÉGÉES**, et cela ne se voit qu'à la compilation :
`Check For Season Instant Refresh` et `Initialize Random Weather Variation` —
« La fonction est protégée et ne peut être accessible en dehors de sa hiérarchie ».
`get_function_info` n'expose AUCUN indicateur d'accès (seulement `is_pure`) : le
seul moyen de savoir est de créer le nœud et de compiler. Les équivalents publics :

| voulu | protégé | public à utiliser |
|---|---|---|
| retirer une météo au sort | `Check For Season Instant Refresh` | `Clear and Restart` (sur `Random Weather Manager`) |
| relancer le système météo | `Initialize Random Weather Variation` | `Full Reconstruction at Runtime` (sur UDW) |

Différence de comportement à connaître : `Check For Season Instant Refresh` ne
rebascule que si la météo en cours est **devenue impossible** dans la nouvelle
carte ; `Clear and Restart` retire **toujours**. Comme `BP_WorldseedClimat`
n'applique le préréglage que sur un CHANGEMENT de biome, le tirage forcé reste
rare — mais longer une frontière de biome fera changer la météo à chaque passage.

**`Random Weather Manager` n'apparaît PAS dans `list_variables` d'UDW** (584
variables listées, celle-là absente) alors qu'elle est bien lisible depuis un autre
Blueprint par un `member_get`. Ne pas conclure de l'absence dans la liste à
l'inaccessibilité : essayer le nœud.

**LES ÉNUMÉRATIONS BLUEPRINT SE POSENT PAR `NewEnumerator<N>`, ET N N'EST PAS LA
VALEUR.** `set_node_pin_value` refuse `RANDOM_INTERVAL` comme `Random Interval`
comme `1` ; il n'accepte que `NewEnumerator0..3`. Or **N est l'ordre de CRÉATION,
pas l'index d'affichage ni la valeur d'octet**. Mesuré sur
`UDS_RandomWeatherTiming` :

    NewEnumerator0 -> RANDOM_INTERVAL (1)     NewEnumerator2 -> HOURLY (3)
    NewEnumerator1 -> DAILY (2)               NewEnumerator3 -> DISABLED (0)

L'auteur avait écrit les trois modes, puis ajouté `DISABLED` et l'avait remonté en
tête. **Poser `NewEnumerator1` en croyant écrire la valeur 1 donne DAILY.** Le seul
contrôle qui tranche est de relire la propriété sur l'acteur EN PIE — c'est ce qui
a rattrapé l'erreur ici.

**PIÈGE DE MESURE, et il m'a coûté six appels : `Apply Climate Preset` reste à
`None` même quand le préréglage est appliqué.** J'ai cru à un échec du
`LoadAsset_Blocking` ou du `Cast`, et je suis parti vérifier le chemin, la classe
de l'asset, l'existence du préréglage austral, l'identité de l'acteur… tout était
juste. La preuve que l'application fonctionne n'est pas cette variable, c'est la
**taille des cartes de probabilités** : 4 types par saison sous la calotte polaire
contre 8, 8, 8 et 9 en forêt tempérée. **Choisir comme témoin une grandeur que le
traitement fait VARIER, pas un drapeau dont on suppose qu'il est posé.**

**Ce qui a été monté dans `BP_WorldseedClimat`** (tout est dans `Content/Worldseed/`,
donc versionné — un réglage posé sur l'acteur UDW aurait vécu dans
`__ExternalActors__`, qui ne l'est pas, et aurait été perdu au clone suivant) :

- au `BeginPlay`, entre `Set UDW` et `Set Grille` : `Random Weather Variation`
  = `RANDOM_INTERVAL`, puis `Full Reconstruction at Runtime` sur UDW pour que le
  système relise ce réglage, puis `Animate Time of Day` = True sur UDS ;
- dans `MajClimat`, après `Apply Climate Preset Object` : un `IsValid` sur
  `Random Weather Manager` — indispensable, sinon le journal se remplit
  d'« Accessed None » si le tirage aléatoire est un jour redésactivé — puis
  `Clear and Restart`.

**Mesures de recette**, PIE, point d'apparition : biome 6, météo `Partly_Cloudy`,
minuteur de changement qui court vers 200-300 s, horloge qui avance
(1100,0 → 1101,4), **107,6 images par seconde, verdict PASS**. Téléportation au
pôle sud : biome 3, météo retirée au sort en `Clear_Skies` et **minuteur remis à
0,0 s** — c'est la signature de `Clear and Restart`.

**FERMÉ LE 13 SEPTEMBRE** -- voir « En mer, le climat est celui de la côte la
plus proche » : une transformée de distance sur la grille donne à chaque cellule
d'eau le biome côtier le plus proche, 0 cellule à zéro sur 16 384. Le constat
d'alors, gardé parce qu'il dit le SYMPTÔME : au-dessus de l'océan le biome valait 0, pour lequel
aucun préréglage n'existe. Le `Cast` échoue, la chaîne s'arrête, et la météo du
dernier biome terrestre persiste. C'est un comportement acceptable ; le corriger
demanderait une branche « biome maritime » et un préréglage océanique.

### « La végétation stylisée est très fluo » : ce n'est pas la végétation (12 septembre 2026)

Signalé sur une capture en PIE, sous la pluie : le feuillage de
`Stylized_PBR_Nature` paraît vert fluo. Question posée : est-ce la pluie, ou
a-t-on déréglé quelque chose ?

**CE QUE CE N'EST PAS.** Quatre contrôles, tous propres — ne pas les refaire :

| contrôle | résultat |
|---|---|
| nos 20 copies d'instances vs celles du pack, paramètre par paramètre | **zéro écart** |
| dosages `Teinte RVT` / `Hauteur fondu RVT` sur les 11 maîtres greffés | valeurs documentées, aucune dérive |
| `Worldseed_PostProcess` | **aucune surcharge active** |
| les 5 acteurs d'éclairage manuels | tous `visible = False`, correctement neutralisés |

**LA MÉTHODE QUI A TRANCHÉ, et elle est réutilisable.** Avant de mesurer des
couleurs sur une capture, **valider la capture sur un témoin hors rendu** :
l'interface de l'éditeur. Quatre échantillons de panneaux gris donnent
**V−R = 0,0** — donc aucune dérive de profil colorimétrique, l'image est fidèle.
Puis prendre pour témoin un objet dont on connaît la couleur : **le personnage
est gris, et il rend à V−R = +27**. Un robot gris ne doit pas être vert.

**CE N'EST PAS UN FILTRE PLEIN ÉCRAN.** Luminance du personnage 60,9, du
feuillage 62,2 — quasi identiques. Une teinte appliquée à l'écran les décalerait
donc pareillement ; or le feuillage est à V−R = +48 et le personnage à +27.
**C'est la lumière AMBIANTE qui est verte**, pas l'image.

**LA CAUSE.** Le `Captured Scene Sky Light` d'UDS est en `SLS_CAPTURED_SCENE`,
capture temps réel, `sky_distance_threshold` = **500 m**. Toute géométrie plus
PROCHE que ce seuil est traitée comme de la *scène* et nourrit l'ambiance ; plus
loin, elle est traitée comme du *ciel*. En forêt dense, c'est donc du feuillage
vert dans toutes les directions qui fait la lumière ambiante, et elle repeint
tout — y compris un personnage gris.

**POURQUOI SEULEMENT MAINTENANT.** Par ciel dégagé le soleil domine et noie
l'ambiance. Sous la pluie il disparaît, et **l'ambiance devient la lumière
principale** : le vert, toujours présent, prend toute la place. Il a fallu armer
la météo (12 septembre) pour qu'un ciel couvert existe enfin dans ce monde. Nos
6 200 instances à l'hectare et la teinte RVT qui accorde le feuillage au sol ne
font qu'amplifier le phénomène.

**DÉCISION DU PROPRIÉTAIRE, 12 septembre 2026 : on ne change rien.** Le rebond
vert d'une canopée existe réellement, et une forêt sous la pluie est verte et
sourde. Ce rendu est physiquement défendable.

**Conséquence à ne PAS « corriger » par mégarde** : `Exposure Bias Cloudy` et
`Exposure Bias Foggy` valent toujours **0**, alors que `Day` vaut −0,6. C'est
un écart connu et **assumé**, pas un oubli. Les pistes écartées le même jour,
pour mémoire : baisser `sky_distance_threshold` (on perdrait aussi le rebond
chaud légitime d'un désert), et basculer sur le `Cubemap Sky Light` (on perdrait
tout rebond de couleur, partout).

### Il neige vraiment — mais une fois sur quarante-trois (12 septembre 2026)

Vérification demandée : la chaîne biome → préréglage → météo → neige au sol
produit-elle réellement de la neige, sans qu'on force `Snow` à la main ?

**OUI, ET C'EST PROUVÉ DE BOUT EN BOUT.** En PIE, joueur posé en toundra boréale
(monde −92913 / 344882, **latitude +59,57°**, biome 4), saison forcée à l'hiver :
le préréglage de toundra s'applique (cartes de probabilités à 7 types), le
tirage sort `Snow`, l'état global prend **`Snow` 6,0 / `Cloud` 8,5 / `Fog` 5,0**
— c'est-à-dire les valeurs du préréglage — et le sol se couvre.

**MAIS LA PROBABILITÉ EST DÉRISOIRE.** Carte d'hiver de la toundra, mesurée :

    Clear_Skies 31,8 %   Overcast 25,4 %   Partly_Cloudy 24,6 %   Cloudy 15,9 %
    Snow_Light   2,2 %   Snow      0,1 %   Foggy          0,1 %

**2,3 % de neige par tirage.** À un tirage toutes les 200 à 300 s, il faut en
moyenne **43 tirages, soit deux heures et demie à trois heures et demie de jeu**,
pour voir un seul épisode neigeux — en toundra, en plein hiver. Autant dire
jamais.

**LA CAUSE EST LA SÉCHERESSE POLAIRE, et c'est un argument nouveau pour la
corriger.** Le préréglage de toundra ne porte que **22,2 mm** de neige en hiver,
parce que notre bande 60-70° reçoit 170 mm/an contre ~500 sur Terre. On savait
que ce manque coûtait des parts de biomes ; on sait maintenant qu'il rend aussi
**la neige météorologique quasi impossible**, donc tout le travail DLWE
inopérant en pratique.

**LE PIÈGE DE MESURE, ET IL EST BEAU : la toundra est DÉJÀ blanche.** Le
générateur y peint la couche `Snow`, donc une capture montrant un sol blanc ne
prouve RIEN sur la neige météo. Le témoin qui discrimine est la part de
**plaques brunes** qui affleurent : **6,8 % avant, 0,0 % après**, plus les
flocons qui tombent et le ciel qui se bouche. Toujours choisir un témoin que le
traitement fait VARIER.

**Ordre des saisons, mesuré et non devine** : `Season` 0 = **printemps**, 1 été,
2 automne, **3 hiver**. Le témoin lisible est `Season Debug` (FString, rend
« Early Spring », « Mid Winter »...) ; `Get Season` rend le couple (double, enum).
Pour forcer une saison, passer `Season Mode` de `USE_UDS_DATE` à
`MANUAL_SETTING`.

**APPELER `Clear and Restart` DEPUIS PYTHON NE SUFFIT PAS.** Il pose bien la
cible du tirage, mais laisse `Current Lerp Alpha` à 1,0 : le système croit la
transition finie et n'applique jamais l'état. Mesure : cible `Snow` pendant 64 s
alors que la transition dure 26 s, et `Global Weather State.Snow` toujours à 0.
Il faut enchaîner `Instant Weather Change Updates` puis
`Update Current Global And Local Weather State`. **La preuve que c'est bien le
système qui applique, et non notre écriture** : on demande `Snow = 8,0` et on
relit **6,0**, la valeur du préréglage.

Tout le test est resté confiné au PIE : l'acteur du niveau a gardé son
`Season Mode`, sa saison et sa carte d'hiver, et aucune carte n'a été salie.

### Une année durait 274 heures : le calendrier de Worldseed (13 septembre 2026)

Mesuré après avoir armé l'horloge : journée de 30 min + nuit de 15 = **45 minutes
réelles pour 24 h**, multiplié par les 365 jours du calendrier grégorien d'UDS.
Soit **273,8 heures pour une année, 68,4 heures pour une saison**. Autrement dit,
les saisons ne changeaient jamais — et tout le calage saisonnier (quatre saisons
par préréglage, inversion des hémisphères, physique de l'amplitude) était
invisible. Même famille de défaut que la neige à 2,3 % : la mécanique est juste,
le rythme la rend inobservable.

**LA SOLUTION EST UN CALENDRIER, PAS UN PILOTAGE DIRECT DE LA SAISON.** On
pouvait passer `Season Mode` à `MANUAL_SETTING` et faire avancer `Season` depuis
`BP_WorldseedClimat`. **Mauvaise idée** : avec `Simulate Real Sun = True`, c'est
la DATE qui donne la déclinaison du soleil. Découpler la saison de la date
produirait un « hiver » sous un soleil d'été. Raccourcir l'année déplace les
deux ensemble.

`CAL_Worldseed` (`Content/Worldseed/Climate/`, donc versionné) : **12 mois de
3 jours, 36 jours par an**, soit une année de **27 h** et une saison de
**6 h 45**. Il est assigné à UDS par `BP_WorldseedClimat` au BeginPlay — et non
posé sur l'acteur, qui vit dans `__ExternalActors__` et serait perdu au clone.

**QUATRE PIÈGES, tous payés comptant :**

- **`Number of Days in Year` et `Month Lengths` sont CALCULÉS.** Un duplicata
  frais les rend à `0` et `[]` : la source de vérité est la table `Months`. Poser
  les deux — la table ET les tableaux dérivés (`Day Count At Start of Each Month`
  compris) — puis `Calendar Data Saved = True`.
- **`Winter Solstice Offset` est en JOURS.** −11 sur une année de 365 devient
  ininterprétable sur 36. Mis à l'échelle : −11 × 36/365 = −1,08, donc **−1**.
  Une valeur laissée à −11 décalerait les saisons de presque un tiers d'année.
- **L'HORLOGE NE S'ACCÉLÈRE PAS À CHAUD.** Ni `Day Length` ni
  `Time of Day Movement Multiplier` (essayé à 400) ne changent la vitesse en
  cours de partie : elle est mise en cache dans `Time of Day Change Speed`.
  Ne pas perdre de temps à vouloir accélérer le temps pour observer une saison.
- **ÉCRIRE `Month` / `Day` À CHAUD NE RECALCULE PAS LA SAISON.** Elle est
  calculée au démarrage et sur l'événement `Date Changed`, qu'une écriture
  directe ne déclenche pas. Mesure : date déplacée du jour 8 au jour 20, saison
  figée à 3,884. **Pour éprouver une saison, poser la date dans le NIVEAU et
  relancer le PIE.**

**LA PREUVE, par cette méthode** : jour 8 sur 36 → saison 3,884, « Mid Spring » ;
jour 20 sur 36 → saison 1,106, « **Mid Summer** ». La saison suit bien notre
calendrier. Contrôle annexe qui confirme qu'il est en vigueur : la date de départ
26/3 est ramenée à 3/3, puisque les mois ne font plus que 3 jours.

### La sécheresse polaire : ce qui manquait était un TRANSPORT (13 septembre 2026)

Les hautes latitudes ne recevaient presque rien — 170 mm/an à 60-70° contre
~500 sur Terre, 11 à 70-80 contre ~250, 0 à 80-90 contre ~150 — avec deux
conséquences mesurées : toundra à 4,7 % des terres contre 8 attendus, taïga à
6,4 contre 10, et surtout **une neige quasi impossible** (2,3 % de probabilité
par tirage en toundra, en plein hiver).

**PREMIÈRE HYPOTHÈSE, FAUSSE, ET C'EST LA MESURE QUI L'A DITE.** Je soupçonnais
la boucle d'advection de *jeter* de l'eau : elle plafonne l'humidité à la
saturation **avant** de faire pleuvoir, au lieu de laisser le surplus condenser.
Compté bande par bande : **0,0 % d'humidité écrêtée au-delà de 60 degrés**. Le
terme ne mord qu'à ±38° (15 à 23 %). Piste abandonnée, code non touché.
**Mesurer avant de corriger, même quand l'hypothèse est séduisante.**

**LA VRAIE CAUSE.** La capacité de l'air s'effondre avec le froid — 1,48 à
l'équateur, 0,21 à 68°, 0,09 à 82° — et surtout **rien ne porte l'humidité vers
les pôles** : l'advection suit le vent MOYEN, zonal aux moyennes latitudes. Sur
Terre ce transport est l'œuvre des dépressions barocliniques, des tourbillons
qu'une simulation à cette résolution ne résout pas.

**LE TERME AJOUTÉ** (`climate._advect_moisture`) : un mélange méridien
descendant le gradient — un lissage gaussien en latitude est exactement
l'opérateur de diffusion correspondant — pondéré par une gaussienne centrée sur
le rail des dépressions. Il **déplace** l'eau, il n'en crée pas.

    eddyMixingRate      0,5     intensité, calibrée au banc
    eddyMixingSigmaDeg  10      taille d'une dépression synoptique
    stormTrackLatDeg    55      position réelle des rails terrestres
    stormTrackWidthDeg  20      largeur réelle

**L'ÉCHELLE EST EN DEGRÉS, JAMAIS EN PIXELS** : le banc tourne en simulation
1025 et le monde final en 2049.

**UNE MÉTHODE QUI A ÉCHOUÉ, à ne pas refaire.** J'ai voulu déduire la position
du rail de notre propre modèle, en cherchant où le gradient méridien de
température culmine. Mesure : **il culmine à 82°**, parce que notre profil est
en `cos(latitude)` dont la dérivée croît jusqu'au pôle. Artefact de la forme,
pas physique. Cette valeur doit venir du réel.

**UN RÉGLAGE MEILLEUR AU SCORE, ÉCARTÉ.** Rail à 60° et largeur 28° donnent
18,1 % contre 18,4 % aux valeurs ancrées. Déplacer un paramètre hors de son sens
physique pour 0,3 point sur un score qui COMPARE et ne juge pas, c'est ce qui
avait fait rejeter `saturationScaleC`.

**RÉSULTAT, pleine résolution :**

| | avant | après | Terre |
|---|---|---|---|
| 60-70° | 170 mm | **456** | ~500 |
| 70-80° | 11 mm | **136** | ~250 |
| 80-90° | 0 mm | **26** | ~150 |
| toundra | 4,66 % | **8,09 %** | 8 |
| taïga | 6,37 % | **8,25 %** | 10 |
| savane | 9,48 % | **11,29 %** | 13 |
| forêt tropicale humide | 11,32 % | 8,28 % | 11 |
| **écart absolu moyen** | **24,3 %** | **16,7 %** | — |

Et le gain qui justifiait le chantier : la neige hivernale du préréglage de
toundra passe de **22,2 à 39,2 mm** d'équivalent-eau.

**UN PLAFOND PHYSIQUE SUBSISTE.** La bande 80-90° reste à ~27 mm quelle que soit
la variante — élargie, déplacée, renforcée : quatre essais, même chiffre. À
−40 °C la capacité de l'air vaut 0,09. Ne pas retenter.

**CE QUE ÇA COÛTE, jeu à somme nulle** (la pluie moyenne est ancrée à 715 mm) :
forêt tropicale humide 11,32 → 8,28 %, désert chaud 16,20 → 15,42. Le relief
bouge aussi : 21 rivières → 16, mais plus longues (médiane 354 → 510 m).

**DEUX ÉCARTS AU RAPPORT, dont un préexistant :**
- *longueur du plus long cours d'eau* 20,6 % contre 30 — **préexistait**, le
  relevé d'avant donne 17,38 % ; il a progressé sans passer la barre ;
- *neige sur terrain hors gel* 1,0 %, à la limite exacte. Vérifié : là où la
  neige domine son poids vaut 1,00 et la température moyenne −20,6 °C. Ce 1 %
  est du sol en recette ALPINE dont le mois le plus chaud repasse juste au-dessus
  de zéro. Marginal, laissé ouvert, **seuil NON abaissé**.

### En mer, le climat est celui de la côte la plus proche (13 septembre 2026)

Au-dessus de l'eau, la grille de `BP_WorldseedClimat` valait **0**, identifiant
pour lequel aucun préréglage n'existe. Le `Cast` échouait donc en silence et la
météo du **dernier biome VISITÉ** persistait : le climat dépendait de
l'historique du joueur et non de l'endroit où il se trouve. Un accident, pas une
intention.

**POURQUOI PAS UN PRÉRÉGLAGE OCÉANIQUE, malgré l'évidence apparente.** Deux
mesures l'ont écarté :

- **l'eau n'est JAMAIS à plus de 2002 m d'une terre** — médiane 335 m, 90ᵉ
  centile 911 m, et 7,5 % seulement au-delà d'un kilomètre. Sur un monde de
  8 km, la côte voisine **est** le climat local ;
- **un préréglage unique pour tout l'océan serait absurde** : la mer fait
  **27,0 °C** sous les tropiques, **15,7** aux moyennes latitudes et **−4,6**
  au-delà de 60°. Une seule fiche ne peut pas décrire cela. Les autres biomes
  n'ont pas ce problème parce qu'ils sont contraints en latitude par
  construction — la toundra n'existe qu'où il fait froid.

**LE CORRECTIF** tient en quelques lignes dans `export_uds_climate.grille_dominante` :
une transformée de distance sur la GRILLE (128×128, donc gratuite) donne pour
chaque cellule vide l'indice de la cellule non vide la plus proche.
`ndimage.distance_transform_edt(vide, return_indices=True)`. Résultat : **0
cellule à 0 sur 16 384**.

**ET LA GRILLE N'ÉTAIT REJOUÉE PAR AUCUN SCRIPT.** Elle avait été posée à la
main lors d'une session précédente : toute régénération du monde laissait donc
`BP_WorldseedClimat` avec la grille d'un monde qui n'existait plus, **sans que
rien ne le signale**. D'où `uds_climate.poser_grille()`, qui l'écrit depuis
`uds_climate.json` et relit pour vérifier. Rappel utile :
`get_variable_info(...).default_value` rend une chaîne VIDE pour une valeur
longue ; le contrôle passe par `get_property`.

**PREUVE, en PIE.** Le témoin n'est pas la taille des cartes de probabilités —
elles font 7 entrées des deux côtés — mais leur CONTENU :

    en toundra : Snow_Light 2,7  Snow 0,4  et pas une goutte de pluie
    au large   : Rain_Light 9,9  Rain 8,8  et pas un flocon

La grille passe de 39 120 à 43 544 caractères, les cellules d'océan portant
désormais un identifiant à deux chiffres au lieu de `0`.

### La pluie a enfin une SAISON, et le mediterraneen avec elle (18 septembre 2026)

`PrecipMm` n'etait qu'un CUMUL ANNUEL, et tout un pan des climats terrestres se
definit par la saison de cette pluie, pas par sa quantite. Le bulletin de
`terre.py` l'avouait : il attendait « steppe ou prairie » pour les trois releves
`Mediterranean` livres avec Ultra Dynamic Sky, faute d'avoir une case a leur
donner.

**AUCUNE PHYSIQUE NOUVELLE N'A ETE NECESSAIRE, et c'est ce qui rend le terme
sur.** La circulation est deja une fonction de la latitude,
`omega(phi) = cos(cellules * pi * |phi| / demi-portee)` -- ZCIT a l'equateur,
subsidence vers 30 degres, front polaire vers 60. Les ceintures MIGRENT avec le
soleil : il suffit d'evaluer la MEME fonction a deux latitudes decalees de
`precipitation.beltShiftDeg`, une fois vers le pole (ete) et une fois vers
l'equateur (hiver), et de comparer les deux taux. A 38 degres la subsidence
passe SUR le point en ete et s'en ecarte en hiver : l'ete sec sort tout seul.
`WorldseedClimate::SummerRainFraction` ne depend que de la latitude et des
regles, donc elle se recalcule a la demande et **ne pese pas sur le cache**.

Le terme ne touche ni au cumul annuel ni au relief : il repartit une pluie deja
calculee. `beltShiftDeg = 0` rend le comportement d'avant, a l'identique.

**LE NOUVEAU BIOME.** `mediterraneen`, identifiant **19, ajoute en FIN de liste**
-- les identifiants sont des cles, on ne les intercale jamais. Il ne prend que
les cases que le diagramme donne a une vegetation temperee ou herbacee : une
foret tropicale a mousson a elle aussi une saison seche, et elle n'est pas
mediterraneenne pour autant.

Calage : bornes de temperature et de pluie = l'enveloppe des trois releves reels
(7,2 a 17,0 degres, 420 a 809 mm), legerement elargie ; seuil de part estivale
**0,25**, qui est le rapport 1/3 de Koppen (groupe Cs) ramene a deux semestres.
Resultat **2,44 % des terres**, la Terre en portant environ 2. Un seuil a 0,20
donnait 2,24 % et 0,2 point de score en plus : refuse, on ne deplace pas une
valeur sourcee pour cela.

**DEUX ECHELLES, ET LES CONFONDRE SERAIT UNE FAUTE.** Notre part estivale est
plus CONTRASTEE que la realite -- 0,15 a 0,21 entre 38 et 50 degres chez nous,
contre 0,24 a 0,30 mesures sur les villes mediterraneennes reelles -- parce que
le modele est purement zonal : ni moderation maritime de la saisonnalite, ni
asymetrie est/ouest des bassins oceaniques (sur Terre le mediterraneen est un
climat de FACADE OUEST, pas une ceinture). `terre.py` emploie donc son propre
seuil, 0,31, qui separe proprement les trois releves mediterraneens de leurs
voisins immediats (Oceanic 0,427, Humid_Subtropical 0,413). Les deux nombres ne
sont pas le meme et ne doivent jamais etre unifies sans refaire la mesure.

**Bulletin terrestre : de 15 a 19 climats reels sur 23** dans la case attendue,
au fil de la session (etiquettes, seuil de foret pluviale, desert froid,
mediterraneen).

**CE QUI RESTAIT OUVERT CE JOUR-LA, et c'est chiffre** *(le premier point a ete
TRAITE le jour meme -- voir « La foret subtropicale humide » juste apres ; les
deux autres restent ouverts)* **:**

- *Foret subtropicale humide.* **FAIT.** `Humid_Subtropical` (14,0 C, 673 mm) et sa
  variante a hiver sec (14,4 C, 1364 mm) tombent en foret temperee. La table des
  attendus les envoie vers `foret_temperee_humide`, ce qui est un pis-aller :
  une foret subtropicale humide n'est pas une foret PLUVIALE temperee. La case
  de 12 a 20 degres au-dessus de 600 mm porte **1,90 % des terres** et merite
  son propre identifiant -- le 20, en fin de liste.
- *Trop de terres froides.* 30,9 % des terres ont un ete sous 10 degres quand la
  Terre en a 18, et la foret temperee mixte plafonne a 3,78 % pour 13 attendus.
  C'est ce qui bloque la limite des arbres (voir la section precedente). Ce n'est
  PAS l'amplitude saisonniere : continentalite mediane 0,58 pour une cible de
  0,6, amplitude a 50-70 degres mediane 27,9 et p90 38,6 contre ~38 sur Terre en
  continental.
- *Cold_Semi-Arid* (6,1 C, 610 mm) tombe en foret temperee. 610 mm a 6 degres
  EST une foret dans un diagramme de Whittaker ; le releve inclut l'equivalent
  en eau de la neige, ce qui le gonfle. Probablement un artefact du releve, a
  ne pas corriger par un seuil.

### La foret subtropicale humide, et une limite du diagramme (18 septembre 2026)

Entre 12 et 20 degres de moyenne annuelle, une foret n'est plus temperee : c'est
la Floride, le sud de la Chine, le sud du Japon. Cette bande declarait pourtant
"foret temperee" de 600 a 1500 mm puis "foret temperee humide" au-dela -- **deux
etiquettes fausses pour la meme chose**, et le seuil de 1500 qui les separait a
disparu avec elles.

Mesure : la nouvelle case porte **2,42 % des terres**, et les deux releves
`Humid_Subtropical` tombent enfin juste.

**LE SCORE NE BOUGE PAS POUR AUTANT : 19 sur 23 avant, 19 apres.** Deux releves
passent, deux autres basculent -- `Subtropical_Highland` (16,5 C, 1130 mm) et sa
variante a hiver sec, que la table attend en foret temperee et qui tombent
desormais en subtropicale.

**ET C'EST UNE LIMITE DE FOND, pas un reglage a affiner.** Le fait qui tranche :
`Subtropical_Highland` est a **16,5 degres** de moyenne annuelle quand
`Humid_Subtropical` est a **14,0** -- le climat d'ALTITUDE est le PLUS CHAUD des
deux. Aucun seuil sur le couple temperature-pluie ne peut donc separer une foret
subtropicale de plaine d'une foret de montagne subtropicale : ce qui les
distingue est l'amplitude diurne et la photoperiode, que le diagramme ne connait
pas. Ne pas chercher a corriger cela par un seuil.

**Effet de bord assume** : la foret pluviale temperee tombe a **0,03 %** des
terres, contre ~0,3 % sur Terre. Sa part de 12 a 20 degres etait en realite de la
foret subtropicale ; ce qui reste est la vraie foret pluviale temperee, et nous
en avons dix fois trop peu -- meme cause que la foret temperee mixte a 1,97 %
pour 13 attendus, c'est-a-dire le manque de terres temperees documente plus haut.
L'ecart absolu moyen passe de 36,0 a 37,8 % : **le vocabulaire gagne, le score
perd**, et c'est le vocabulaire qui avait ete demande.

### Le pilotage meteo etait ecrit, complet, et n'avait AUCUN APPELANT (28 septembre 2026)

Signale a l'usage : « je vois assez rarement de la pluie dans le jeu ou des
orages tres fort avec UDS, est-il bien branche sur le climat, les biomes ? ».
Reponse : non. Et ce n'etait pas un defaut de calage, c'etait un fil manquant.

**CE QUI EXISTAIT.** `WorldseedClimatePreset` traduit temperature, pluie,
amplitude saisonniere, continentalite et latitude en un prereglage UDS, cale
sur les vingt-trois prereglages LIVRES par le pack. `WorldseedWeatherState` en
tire une meteo instantanee avec une idee juste -- le pourcentage de ciel
couvert du prereglage sert de SEUIL DE FREQUENCE, si bien qu'un desert garde
son orage rare sans qu'on l'invente. `UWorldseedSkyDriverComponent` pousse le
tout dans UDS, avec un fondu pour qu'une frontiere climatique ne commute pas
le ciel d'un coup. Tout cela etait juste, et rien ne tournait.

**LA PREUVE, EN QUATRE POINTS CONCORDANTS :**

    appelants de FeedSky                        AUCUN
    AWorldseedTerrain      PrimaryActorTick.bCanEverTick = false
    UWorldseedSkyDriver    PrimaryComponentTick.bCanEverTick = false
    journal de partie      ni « ciel : latitude », ni « aucun Ultra Dynamic Sky »

**LE DERNIER POINT EST LE TEMOIN QUI TRANCHE**, et c'est lui qu'il faut
retenir : `Drive` emet FORCEMENT l'une de ces deux lignes, quelle que soit la
branche prise -- elle a trouve le ciel, ou elle ne l'a pas trouve. Aucune des
deux dans 395 Ko de journal : la fonction ne s'executait jamais. **Chercher la
ligne qu'un chemin emet dans TOUS les cas coute moins cher que lire le code**,
et c'est une mesure, pas une deduction.

**CE QUE LE BRANCHEMENT A REVELE : le systeme etait excellent.** Mesure des
que l'appelant existe, sur quatre cents echantillons d'un cycle complet --

    part de pluie   desert chaud    0,0 %     foret tropicale humide  100,0 %
    neige maximale  a -12 degres    0,34      a +14 degres              0,00

**UN MINUTEUR, PAS LE TICK DE L'ACTEUR.** Le climat change a l'echelle du
kilometre, pas de l'image : a six kilometres-heure, une demi-seconde
represente quatre-vingt-trois centimetres, tres en deca de la maille de quinze
metres de la carte des biomes. C'est aussi la cadence qu'employait
`BP_WorldseedClimat` avant le portage. `SkyPeriodS = 0` rend l'etat d'avant --
ciel non pilote -- donc l'A/B se fait sans recompiler.

**ET MON PREMIER ORACLE ETAIT FAUX, PAS LE CODE.** `NeigeAuFroid` exigeait
qu'il ne neige jamais a quatorze degres ; le modele en rendait 0,25 et le test
criait. Mais j'avais pose TRENTE degres d'amplitude saisonniere : a quatorze de
moyenne, l'hiver tombe alors a moins un, et il neige -- le modele avait raison.
Corrige en ramenant l'amplitude a six, ou l'hiver doux reste a onze degres.
**Un attendu naif se CORRIGE, il ne s'elargit pas** : allonger la tolerance
aurait fait passer le test sans rien apprendre, et masque la question.

**CE QU'UN TEST NE PEUT PAS GARDER ICI**, et il faut le dire : un minuteur ne
se verifie pas sans instancier l'acteur. Les deux oracles gardent donc ce que
le cablage SERT A PRODUIRE -- une meteo qui distingue les climats -- et non le
cablage lui-meme. Si le fil sautait de nouveau, ils resteraient verts. Le seul
controle qui le verrait est la ligne de journal ci-dessus.

**RESTE OUVERT** : l'intensite maximale des orages n'est pas mesuree. Le
prereglage traduit la pluie annuelle en frequence de ciel couvert ; ce qu'il
atteint en pointe se regle dans la section `uds` de `world_rules.json`, et
personne ne l'a encore chiffre.

### Regler la meteo : trois pistes mesurees, TROIS REFUTEES (28 septembre 2026)

Demande du proprietaire : « constater pour chaque climat les habitudes de
precipitation moyennes sur Terre et relier ce constat a UDW ». Le constat
existait deja -- `climats_reels.json` porte, saison par saison, le ciel
couvert, la pluie et la neige de vingt-trois stations -- mais personne ne
l'avait confronte a notre conversion. D'ou `ProbeMeteo`, qui appelle
`WorldseedClimatePreset::Build`, la MEME fonction que le pilote du ciel.

**LE BULLETIN A SERVI A CE POUR QUOI IL EXISTE : il a dit ou NE PAS chercher.**

| piste | ecart de couverture | ecart de pluie |
|---|---|---|
| etat actuel | 16,3 points | 25,7 mm/mois |
| recalibrer les trois cles au mieux | **12,7** | -- |
| + plancher fonction de la pluie ANNUELLE | **12,2** | -- |
| interpoler les vingt-trois releves | 13,9 | **37,8 -- pire** |
| repartir par la fraction estivale REELLE | -- | **15,0** |
| ... par la NOTRE, fausse de 0,185 | -- | **34,0 -- pire** |
| temoin : repartition uniforme | -- | 32,3 |

**LA COUVERTURE NE SE PREDIT PAS DEPUIS LA PLUIE, et c'est physique.** Aucune
forme testee ne passe sous douze points, et la meilleure parametrisation
DEGRADE les deserts -- deux pour cent de ciel couvert rendus trente. La raison :
deux climats a quarante millimetres par mois portent 35 ou 78 pour cent de
couverture selon que la pluie est convective ou frontale. Une averse tropicale
tombe en deux heures sous un ciel par ailleurs degage ; une pluie frontale
oceanique tombe sous un gris permanent. **La quantite ne dit pas le type.** Ne
pas retoucher `cloudyFloorPct`, `cloudySpanPct` ni `cloudyPrecipScaleMm` : le
gain plafonne a trois points et il se paie sur les cas qui marchent.

**ET LA PISTE QUI SEMBLAIT LA BONNE AURAIT REGRESSE.** `SeasonalRainFactors`
repartit la pluie d'apres la LATITUDE SEULE, alors que la chaine calcule deja
une fraction estivale par cellule (`WorldseedClimate::SummerRainFraction`).
Mesure hors moteur : s'en servir ramenerait l'ecart de 25,7 a 15,0 mm par mois,
soit 41 pour cent. J'ai propose de le coder, et le proprietaire a accepte.

**LE CONTROLE QUI A SAUVE LE CHANTIER TIENT EN UNE LIGNE** : ce plafond
employait la fraction estivale REELLE des releves. Notre modele la predit-il
seulement ? Mesure ajoutee au bulletin -- moyenne 0,515 contre 0,494 pour le
reel, donc JUSTE EN MOYENNE, mais **ecart individuel 0,185** sur une grandeur
bornee a [0, 1]. Avec une fraction fausse de 0,185, la repartition rend
**34,0 mm : pire que le modele actuel, et pire que l'uniforme.**

    REGLE : un plafond mesure avec la donnee PARFAITE n'est atteignable que si
    l'on sait produire cette donnee. Verifier qu'on la predit AVANT de batir
    dessus -- ici, le plafond etait a 15 et le resultat reel a 34.

C'est le meme piege que le score de `terre.py` qui COMPARE sans juger, ou que
l'estimation JavaScript qui recopiait une formule : une mesure juste sur une
question qui n'est pas celle qu'on croit poser.

**CE QUI RESTE VRAI, ET C'EST LE RESULTAT UTILE.** Le modele en vigueur est
MEILLEUR que toutes les alternatives testees : il capture un cinquieme du
signal saisonnier la ou l'uniforme n'en capture rien, et aucune des quatre
pistes ne fait mieux. Le vrai defaut est d'un cran en amont --
`SummerRainFraction` se trompe de 0,185 par climat -- et c'est un chantier de
CLIMAT, pas de meteo. Le reel va jusqu'a CINQUANTE fois entre saison seche et
humide (savane a hiver sec : 2 mm en hiver, 155 en automne) quand nos facteurs
ITCZ plafonnent a neuf.

### La meteo est branchee, mais L'HORLOGE EST ARRETEE (28 septembre 2026)

Question du proprietaire : « que manque-t-il pour avoir une meteo credible dans
chacun des climats, et que faudrait-il pour la brancher a UDW ? ». La seconde
moitie avait sa reponse -- c'est branche depuis le matin -- mais il fallait le
PROUVER, et le faire a trouve un defaut plus gros.

**PREUVE DU BRANCHEMENT, variable par variable.** `PushWeather` ecrit onze
grandeurs dans UDS/UDW et **IGNORAIT le booleen que `WriteNumber` rend** : un
nom absent aurait fait echouer l'ecriture en silence, exactement comme les
trois parametres de la rampe du decor devenus des no-op. Le controle est
desormais fait UNE FOIS au premier passage, et il dit : *les 11 variables
d'UDS/UDW acceptent l'ecriture*. Le pont tient.

**MAIS LA PHASE DE L'ANNEE EST LUE DANS UDS, PAS CALCULEE.** Et UDS ne la fait
avancer que si son `Animate Time of Day` est arme. Mesure ajoutee au meme
controle : **l'horloge est ARRETEE dans `L_Worldseed_Proc`**, saison figee a
3,78 sur 12. Consequences : pas de cycle jour/nuit, et la meteo d'UNE SEULE
saison pour toute la partie -- quel que soit le modele en amont. C'est le
defaut dominant, et il rend decoratif tout le calage saisonnier : quatre
saisons par prereglage, inversion des hemispheres, tout cela sans effet.

**LA CAUSE EST UN RESTE DU PORTAGE.** `BP_WorldseedClimat` armait l'horloge au
BeginPlay depuis le 12 septembre, et assignait `CAL_Worldseed` -- le calendrier
de trente-six jours qui ramene l'annee de deux cent soixante-quatorze heures a
vingt-sept. Ce Blueprint n'est plus employe depuis que la generation est passee
en C++, et ces deux reglages sont partis avec lui. Meme famille que `FeedSky`
sans appelant, trouve le matin meme : le portage a emporte le calcul et laisse
les fils.

**CE QU'ON N'A PAS SU LIRE, ET ON LE DIT** : la longueur de l'annee est
CALCULEE dans l'asset `UDS_Calendar`, pas exposee sur l'acteur -- donc le
controle ne peut pas verifier que `CAL_Worldseed` est assigne. Il journalise
son impuissance plutot que de se taire : un silence se lirait « le calendrier
va bien ».

**L'ORDRE D'IMPORTANCE, MESURE ET NON SUPPOSE :**

| ce qui manque | effet | ou cela se corrige |
|---|---|---|
| horloge arretee | une seule saison, pas de jour/nuit | la CARTE |
| calendrier non verifie | annee de 274 h si absent | la CARTE |
| contraste saisonnier de la pluie | un cinquieme du signal reel | le CLIMAT |
| type de precipitation inconnu | 12 points de couverture irreductibles | le MODELE |
| intensite des orages | jamais mesuree | la section `uds` |

Les deux premieres lignes coutent deux cases a cocher et dominent tout le
reste ; les trois suivantes sont des chantiers. **Regler un modele dont
l'horloge est arretee n'aurait rien donne**, et c'est pourquoi ce controle vient
avant.

**SUITE, LE MEME JOUR : L'HORLOGE EST ARMEE, ET IL A FALLU TROIS ESSAIS.**
Arbitrage du proprietaire : armer depuis le C++ plutot que dans la carte, pour
qu'un `Content/` perdu n'emporte pas le reglage. Les durees vont dans
`world_rules.json` (`uds.animerHorloge`, `dureeJourneeMin`, `dureeNuitMin`).

| essai | ce qu'on a pose | mesure |
|---|---|---|
| poser `Animate Time of Day` | drapeau a vrai, RELU a vrai | heure figee a 1300,0000 |
| + poser `Time Speed` | elle n'etait pas nulle | figee |
| + appeler `OnRep_Animate Time of Day` | -- | **1300,0000 -> 1303,3146 en 5,5 s** |

**POSER UNE VARIABLE PAR REFLEXION NE DECLENCHE AUCUN RAPPEL.** `Animate Time
of Day` est une variable Blueprint repliquee a RepNotify : c'est son `OnRep_`
qui lance la boucle d'animation, et le moteur ne l'appelle que sur une
replication REELLE. Ecrire la valeur par `FBoolProperty::SetPropertyValue`
pose le drapeau -- il se RELIT meme a vrai -- et ne reveille rien. C'est un
echec parfaitement muet : tous les controles disaient oui.

    REGLE : apres avoir pose une variable Blueprint par reflexion, chercher un
    `OnRep_<nom>` et l'appeler. `FindFunction` puis `ProcessEvent`, et
    seulement si `NumParms == 0` -- une pile de parametres mal formee
    corromprait la memoire.

**ET C'EST LA MESURE D'AVANCEMENT QUI A TOUT VU.** Sans elle, le journal
annoncait « horloge EN MARCHE » et les onze variables acceptees : on aurait
conclu a une victoire. Armer n'est pas faire avancer, et la seule preuve est de
relire l'heure quelques secondes plus tard. Verification finale : 0,60 unite
par seconde mesuree contre 0,667 attendue pour une journee de trente minutes,
l'ecart etant le chargement compte dans l'intervalle.

**NE PAS CONFONDRE `Time Speed` ET `Time of Day Change Speed`** : le premier,
categorie « Animate Time Of Day », est le multiplicateur ; le second, categorie
« Change Monitoring », MESURE la vitesse de changement et ne la fixe pas.

**RESTE A VERIFIER A LA MAIN** : que `CAL_Worldseed` est bien assigne a la
variable `Calendar` de l'acteur UDS. La longueur de l'annee est CALCULEE dans
l'asset calendrier et non exposee sur l'acteur, donc le controle ne peut pas la
lire -- il journalise son impuissance. Sans ce calendrier, l'annee des trois
cent soixante-cinq jours d'UDS dure deux cent soixante-quatorze heures reelles
et la saison ne changera jamais, horloge armee ou non.

**ET LE CALENDRIER Y ETAIT AUSSI, ce que j'avais dit ne pas pouvoir verifier.**
J'ai ecrit qu'il fallait l'ouvrir a la main dans l'editeur ; c'etait faux --
un commandlet Python charge la carte, lit l'acteur et l'ecrit. La limite
n'existait qu'en JEU, ou la longueur de l'annee n'est pas sur l'acteur.

    releve : Calendar = Gregorian_Calendar   -> 365 jours, soit 274 h par annee
    apres  : Calendar = CAL_Worldseed        -> 36 jours, 27 h, saison de 6,8 h

**LE TEMOIN QUI A VALIDE NOTRE CALENDRIER.** Dans l'asset au repos,
`CAL_Worldseed` a `Number of Days in Year` a ZERO, `Month Lengths` vide et
`Calendar Data Saved` a False -- de quoi le croire incomplet, et une note de ce
registre dit d'ailleurs qu'il faut poser ces champs a la main. Or **le
calendrier gregorien livre par UDS, qui fonctionne, est dans EXACTEMENT le meme
etat**. Ces champs sont donc calcules au demarrage, et un vide dans l'asset ne
prouve rien. La seule chose qui compte est la table `Months` -- douze mois de
trois jours. Verifie en jeu : « calendrier CAL_Worldseed, 36 jours ».

**LA SAISON AVANCE, ET C'EST LA PREUVE DE BOUT EN BOUT** : le releve du ciel
passe de `3.88` a `3.89` dans une meme partie, la ou il affichait `3.78` sur
toutes les lignes avant. Le temps, lui, va de 1300,34 a 1303,66 en 5,5 s.

`Tools/UE/calendrier_worldseed.py` rejoue l'operation, idempotent par constat.

### Le chantier climat : ce que la mesure a rendu, et ce qu'elle a refuse (28 septembre 2026)

Suite du bulletin meteo, et fin du chantier. Deux corrections posees, chacune
calee sur les vingt-trois releves de stations reelles -- la seule reference
EXTERIEURE au projet.

**1. L'OSCILLATION SAISONNIERE ETAIT DEUX FOIS TROP FORTE.** `SummerRainFraction`
s'ecartait du reel de **0,185** en moyenne, quand REPONDRE 0,5 PARTOUT -- ne
rien predire du tout -- n'en fait que **0,157**. Elle ajoutait donc du bruit
plutot que de l'information, et aucune mesure interne ne pouvait le dire.

**MAIS L'ERREUR ETAIT STRUCTUREE, ET C'EST CE QUI L'A RENDUE CORRIGIBLE.**
Rangee par latitude, elle dessine un motif : trop estivale sous les tropiques
(+0,05 a +0,40) et aux hautes latitudes (+0,09 a +0,31), trop hivernale aux
moyennes (-0,08 a -0,34). Bonne FORME, mauvaise amplitude.
`precipitation.saisonAmortissement` = 0,54 la ramene a **0,102**.

    REGLE : avant de jeter un modele parce que son erreur est grande, la RANGER
    par la variable dont il depend. Une erreur structuree est un reglage ; une
    erreur dispersee est un modele a refaire.

**LE SEUIL MEDITERRANEEN DEVAIT SUIVRE, ET C'EST LE PIEGE DE CETTE CORRECTION.**
`biomes.mediterraneanSummerFracMax` s'applique a cette fraction : l'amortir sans
le deplacer aurait supprime le biome. Il passe de 0,25 a **0,365**, qui est la
MEME frontiere dans la nouvelle echelle -- `0,5 + k(S - 0,5)`. Verifie :
mediterraneen a **2,71 % des terres avant comme apres**, bulletin terrestre a
20 sur 23 dans les deux cas, memes echecs. **Deux reglages lies doivent bouger
ensemble, et le fichier de regles le dit aux DEUX endroits.**

**2. LA PLUIE SE REPARTIT DESORMAIS PAR CETTE FRACTION**, et non plus par la
latitude que `SeasonalRainFactors` rederivait moins bien. Bilan mesure :

| | couverture | pluie |
|---|---|---|
| au depart | 16,3 points | 25,7 mm/mois |
| fraction amortie seule | 16,3 | 25,7 (elle n'etait pas encore lue) |
| + repartition par la fraction | **15,3** | **24,2** |
| temoin : repartition uniforme | -- | 32,3 |

**ET J'AVAIS ANNONCE 21,8 mm, LA MESURE EN REND 24,2.** L'ecart vient de ma
simulation : pour estimer ce que vaudrait une fraction imparfaite, j'avais
ajoute un bruit de +/- 0,102 de signe aleatoire -- alors que la vraie erreur est
STRUCTUREE par latitude, donc bien moins favorable qu'un bruit. **Simuler une
erreur par du bruit surestime ce qu'on gagnera** : le bruit se compense, une
erreur systematique non.

**LE MORDANT SE CHERCHE DANS LA SONDE, PAS EN EDITANT LES REGLES.** Le balayage
de `uds.saisonExposant` vit dans `ProbeMeteo` : rouvrir `world_rules.json` entre
deux moities d'un A/B change son empreinte, donc regenere le monde -- et ce
depot a deja VIDE ce fichier avec une boucle de ce genre. Releve :
1,0 -> 26,6 | 1,6 -> 24,8 | **1,9 -> 24,2** | 2,5 -> 24,8 | 3,4 -> 28,1.

**CE QUI RESTE HORS DE PORTEE, ET C'EST MESURE.** La couverture nuageuse
plafonne vers douze points quelle que soit la forme : deux climats a quarante
millimetres par mois portent 35 ou 78 pour cent de ciel couvert selon que la
pluie est convective ou frontale, et la quantite ne dit pas le type. Quatre
pistes ont ete essayees et refutees -- recalibrer les trois cles, ajouter un
plancher fonction de la pluie annuelle, interpoler les releves, ajouter un terme
de continentalite (coefficient optimal NUL). Ne pas les reprendre sans une
variable nouvelle qui distingue le TYPE de precipitation.

**LE TYPE DE PRECIPITATION : DEMANDE, MESURE, ET REFUSE (28 septembre 2026).**
Une averse convective tombe en deux heures sous un ciel par ailleurs degage,
une pluie frontale sous un gris permanent : c'est ce qui explique que deux
climats a quarante millimetres par mois portent 35 ou 78 pour cent de
couverture. Restait a savoir si nous pouvions PREDIRE ce type.

**LA LATITUDE N'EXPLIQUE RIEN, et c'etait mon hypothese.** Correlation du
residu avec |latitude| : **+0,013**. Les tropiques ne sont pas
systematiquement plus convectifs que les moyennes latitudes, ou du moins la
couverture ne s'en ressent pas dans ces releves.

**LA TEMPERATURE, ELLE, PORTE UN VRAI SIGNAL -- ET IL NE RAPPORTE RIEN.**
Correlation -0,230 apres calage de la courbe : plus il fait chaud, moins il y
a de nuages pour la meme pluie, ce qui EST la signature convective. Mais
traduite en modele, elle ne donne rien :

    courbe calee, sans temperature            12,66 points
    A  echelle croissante avec la temperature 12,66   coefficient NUL
    B  retrait proportionnel a la chaleur     12,60   soit 0,06 point

**LE PLANCHER EST STRUCTUREL, ET SA DECOMPOSITION LE DIT.** Sur 12,66 points
d'ecart, **6,74 sont de la dispersion INTRA-climat** -- entre les quatre
saisons d'un meme climat -- et le reste un biais CONSTANT par climat. Aucune
formule saisonniere ne passe sous 6,74, et le biais par climat est l'identite
du climat lui-meme, que cinq variables continues ne distinguent pas.

    REGLE : avant de chercher une variable explicative, DECOMPOSER le residu
    en part intra-groupe et part inter-groupe. Si le gros est inter-groupe,
    aucune variable continue ne le capturera -- il faut un IDENTIFIANT.

**CE QUI RESTE GAGNABLE, ET C'EST MESURE SUR LA VRAIE CHAINE.** Le balayage de
`uds.cloudyPrecipScaleMm` vit desormais dans `ProbeMeteo`, avec sa
CONTREPARTIE affichee : l'ecart sur les climats arides, ou notre formule tombe
juste aujourd'hui et ou un ciel gris se remarque le plus.

    echelle   ecart moyen   climats arides
        60       15,3           13,5   <- en vigueur
        80       13,7           14,8
       100       14,0           15,8
       120       14,9           16,7

Une moyenne ne suffit pas a trancher : c'est pourquoi la sonde rend les deux
colonnes.

**ET L'IDENTIFIANT NE MARCHE PAS NON PLUS -- LE BIOME EST TROP GROSSIER.**
La regle ci-dessus disait « si le gros du residu est inter-groupe, il faut un
IDENTIFIANT ». Nous en avons un, le biome, et la correspondance releve ->
biome existe deja dans la table `Attendus`. Essaye : un decalage de couverture
par biome, cale sur les releves, en validation croisee PAR CLIMAT -- le
decalage d'un biome calcule sans le climat qu'on estime, sinon on mesurerait
sa capacite a se retenir lui-meme.

| echelle | sans identifiant | avec decalage par biome |
|---|---|---|
| 60 | 14,83 | **16,27** |
| 80 | **13,54** | 15,92 |
| 100 | 13,54 | 15,57 |

**IL FAIT PIRE, ET LA RAISON EST STRUCTURELLE** : plusieurs climats de KOPPEN
tombent dans le meme biome de WHITTAKER. `TemperateForest` regroupe Oceanic,
deux continentaux et deux subtropicaux d'altitude -- cinq climats dont le
decalage moyen ne predit celui d'aucun. Le biais est par climat de Koppen, pas
par biome, et notre monde ne calcule que le second.

    REGLE, seconde moitie : un identifiant ne vaut que s'il est a la RESOLUTION
    du biais. Trop grossier, il ajoute la variance de son groupe a l'erreur au
    lieu de la retirer.

**BILAN DE TOUTES LES PISTES**, pour qu'aucune ne soit retentee :

    formule en vigueur, sur la vraie chaine        15,3 points
    echelle de pluie portee a 80                   13,7   (arides 13,5 -> 14,8)
    meilleure formule possible, pluie seule        12,7
    + plancher fonction de la pluie annuelle       12,2
    + terme de temperature                         12,6   (0,06 de gain)
    interpolation des vingt-trois releves          13,9
    decalage par biome                             15,9   PIRE
    plancher structurel, dispersion intra-climat    6,74

Rien ne descend sous douze points et demi, et le plancher absolu est a 6,74.
**Le realisme percu ne se joue plus la** : il vient du CONTRASTE entre climats,
qui lui est acquis -- zero pour cent de pluie au desert chaud, cent en foret
tropicale humide.

### Classer en Koppen : la meteo d'une vraie station, et non d'une courbe (28 septembre 2026)

Demande du proprietaire apres le constat que la couverture nuageuse plafonne :
« allons plus loin en classant chaque cellule en climat de Koppen ». C'est fait,
et c'est le seul changement de la journee qui divise l'ecart par deux.

**POURQUOI LE BIOME NE SUFFISAIT PAS.** Le biome dit ce qui POUSSE, Koppen dit
le TEMPS QU'IL FAIT, et les deux ne sont pas a la meme resolution :
`TemperateForest` recouvre l'oceanique, deux continentaux et deux subtropicaux
d'altitude. Un decalage par biome rendait 15,9 points la ou ne rien faire en
rendait 13,5 -- le groupe ajoutait sa variance a l'erreur.

**CE QUE LE CLASSEMENT PERMET.** Les vingt-trois prereglages d'Ultra Dynamic Sky
suivent la nomenclature de Koppen : « Mediterranean_Hot_Summer » EST un Csa. La
verite terrain est donc gratuite, et une cellule classee peut recevoir la
couverture d'une VRAIE station du meme climat, saison par saison.

    couverture nuageuse : 15,3 -> 7,5 points d'ecart  (-51 %)
    plancher d'une formule saisonniere : 6,74  -- desormais approche

**ON NE COPIE QUE LE CIEL, PAS LE CLIMAT.** Les temperatures et le cumul de
pluie viennent du MONDE ; seule la part du temps ou le ciel est charge vient du
releve. C'est le seul endroit ou une donnee exterieure apporte ce qu'aucun
calcul n'atteint -- la quantite de pluie ne dit pas son TYPE.

**DEUX FAUTES DE FORMULE, TROUVEES PAR L'ORACLE ET NON PAR LA LECTURE.** Le
seuil d'aridite de Koppen s'ecrit `20 x T + 280 / 140 / 0` selon la saison des
pluies, et la regle est « desert sous la MOITIE du seuil, steppe sous le
seuil ». Mon premier jet avait les primes au dixieme -- 28, 14, 0, la meme
formule en CENTIMETRES -- et la comparaison inversee : seuil pour le desert,
double du seuil pour la steppe. Londres et ses sept cents millimetres passaient
alors pour un climat aride. Corrige : 15 sur 23 -> 17 sur 23 en classe exacte,
18 -> 20 sur le groupe principal.

**LA QUALITE NE TIENT PLUS A UN CALAGE MAIS A UN TAUX**, et c'est un changement
de nature : une cellule bien classee recoit la meteo d'une vraie station, une
cellule mal classee celle d'un AUTRE climat -- 16,5 points en moyenne, jusqu'a
66 entre une foret tropicale et un desert chaud. L'oracle mesure donc le taux,
et non plus un ecart.

**ET L'ECART MESURE EST 7,5 QUAND J'EN ANNONCAIS 4,3.** La difference est
l'erreur de RECONSTRUCTION : mon estimation partait des saisons reelles des
releves, alors que la chaine reconstruit les siennes depuis une moyenne, une
amplitude et une fraction estivale. Deux erreurs s'ajoutent -- celle du
classement et celle de la reconstruction -- et ne pas les distinguer aurait
fait promettre un chiffre hors d'atteinte.

**POINTE MENSUELLE** : Koppen se definit sur les mois, nous n'avons que des
saisons, et la moyenne de trois mois adoucit les pointes -- au point de faire
passer des continentaux dans le groupe tempere. Le correctif est un ecart
unique, BALAYE sur les releves (0 a 6 degres) plutot que devine : un demi-degre.

### La distribution des classes de Koppen : ce que le monde produit vraiment (28 septembre 2026)

`ProbeKoppen` compte les classes sur les terres emergees et les confronte aux
parts terrestres. Elle repond a la question qui conditionnait tout le chantier :
notre monde produit-il des climats que la Terre connait ?

**LA REPONSE EST OUI, ET C'EST LA LIGNE QUI COMPTE : zero pour cent de cellules
sans releve.** Toute cellule emergee tombe dans une classe qui a une station
reelle, donc recoit une vraie meteo -- il n'y a pas de melange entre la voie
des releves et la voie de la courbe, qui se verrait comme une couture
climatique. Ecart absolu moyen a la Terre : **2,71 points sur 22 classes**.

**MAIS QUATRE CLASSES NE SONT JAMAIS ATTEINTES**, et leur releve ne sert
jamais : `As`, `Cwa`, `Cwb`, `Dfd`. Les deux du milieu sont la MOUSSON D'ASIE
-- un hiver sec en climat tempere -- que notre modele ne sait pas produire :
`itczWinterFactor` ne module qu'entre les tropiques, et rien ne dessine un
regime de mousson aux latitudes moyennes. Quatre pour cent et demi des terres
terrestres n'ont donc pas d'equivalent ici.

**ET LA SONDE RE-MESURE UN DEFAUT CONNU PAR UN CHEMIN INDEPENDANT** :

    ET + EF (polaires)   28,6 %   contre 16 attendus   -- DEUX FOIS TROP
    Aw (savane)           2,6 %   contre 11            -- QUATRE FOIS TROP PEU
    Cfa (subtropical)    13,8 %   contre 6

Le registre note depuis le 18 septembre que « 30,9 % des terres ont un ete sous
10 degres quand la Terre en a 18 ». Or `ET` et `EF` se definissent EXACTEMENT
par ce critere -- le mois le plus chaud sous dix degres. Deux mesures sans
rapport l'une avec l'autre, le bulletin des biomes et le classement de Koppen,
donnent donc le meme verdict : **ce monde est trop froid, et c'est le defaut de
climat le plus solidement etabli du depot.**

    REGLE : une sonde neuve qui retrouve un defaut deja connu par un autre
    chemin vaut mieux qu'une sonde qui n'en trouve aucun -- c'est ce qui
    distingue une mesure d'un compteur.

**CE QUI RESTE GAGNABLE ET N'A PAS ETE FAIT** : la chaine calcule `TempMinC` et
`TempMaxC` -- les moyennes du mois le plus froid et du plus chaud, exactement ce
que Koppen demande -- alors que le prereglage les RECONSTRUIT depuis l'amplitude
saisonniere. C'est cette reconstruction qui explique l'ecart entre les 4,3
points attendus et les 7,5 mesures. Les transmettre par
`FWorldseedClimateSample` supprimerait cette erreur ; la sonde les emploie deja,
le jeu non.

### Pourquoi le monde est trop froid : ce n'est pas le climat, c'est le SUD (28 septembre 2026)

**PREMIERE CHOSE, ET C'EST UN CHANTIER ANNULE.** Le proprietaire a demande de
transmettre `TempMinC` et `TempMaxC` par `FWorldseedClimateSample`, sur mon
diagnostic que la reconstruction des extremes expliquait l'ecart entre 4,3 et
7,5 points. **C'ETAIT FAUX, et la verification l'a dit avant la compilation** :

    Out.TempMinC[Index] = T - Amp * 0.5f;
    Out.TempMaxC[Index] = T + Amp * 0.5f;

C'est EXACTEMENT ce que `DepuisChamps` recalcule, et la prime d'aridite
s'ajoute aux trois de facon identique. Les transmettre n'aurait strictement
rien rapporte. Le code ecrit a ete annule par `git checkout`. L'ecart vient
donc de la reconstruction de la PLUIE saisonniere, non des temperatures.

    REGLE : avant de transporter une donnee « plus exacte », VERIFIER qu'elle
    differe de ce qu'on recalcule. Ici les deux etaient identiques a l'octet
    pres, et j'ai propose le chantier sans l'avoir ouvert.

**LE MONDE N'EST PAS TROP FROID -- SON HEMISPHERE SUD L'EST.** `ProbeZonal`,
qui existait deja, tranche en une lecture :

    hemisphere NORD : -0,2 degre d'ecart a la Terre   (8 bandes)
    hemisphere SUD  : -7,2 degres                     (9 bandes)

| latitude | NORD | | | SUD | | | ecart |
|---|---|---|---|---|---|---|---|
| 70-80 | -10,5 C | 8 % emerge | 212 m | -24,1 C | **100 %** | **965 m** | **13,6 C** |
| 60-70 | -6,5 | 16 % | 120 m | -15,0 | 70 % | 690 m | 8,5 |
| 50-60 | +2,1 | 10 % | 71 m | -9,8 | 41 % | 715 m | 11,9 |

**NOTRE PROFIL ZONAL EST SYMETRIQUE, ET LE RESULTAT NE L'EST PAS** : l'ecart ne
vient donc pas du climat mais de ce qu'il trouve sous lui. Le pole sud est
force en continent -- ce qui est voulu, et terrestre : l'Antarctique existe --
mais il culmine a 965 a 1088 m, quand les terres du nord aux memes latitudes
tiennent entre 71 et 212. `tectonics.poleContinentBonusM` vaut 175 m AVANT
mise a l'echelle, donc **700 m** sur un monde de 32 km de hauteur. C'est le
meme defaut que la constante metrique de `tectonics.py` en septembre, a une
autre echelle.

**DEUX RESERVES D'HONNETETE, et elles limitent ce diagnostic :**

- **la reference zonale de la sonde est SYMETRIQUE, et la Terre ne l'est pas.**
  Son propre commentaire le dit : l'hemisphere nord terrestre est plus chaud
  parce qu'il porte plus de terres. Comparer notre sud continental a une
  moyenne symetrique SURESTIME donc l'ecart -- l'Antarctique cotier reel est
  vers -20 a -30, pas -14 ;
- **le nord manque de terres** autant que le sud en a : 8 % emerge a 70-80
  contre environ 35 sur Terre. Notre nord est « juste » en temperature parce
  qu'il est surtout oceanique, ce qui n'est pas la meme chose qu'etre juste.

**CE QUI EST SOLIDE** : `ET + EF` pese 28,6 % des terres contre 16 attendus, et
la moitie de cet exces tient au continent polaire sud, haut et integralement
emerge. La piste est donc `poleContinentBonusM` et la mise a l'echelle
verticale -- de la TECTONIQUE, pas du climat. Ne pas y toucher sans mesurer
d'abord la part emergee par bande APRES changement : la regle existe pour que
le pole EMERGE, et l'amputer le ferait disparaitre.

### Le monde n'est pas trop froid, et aucun de mes deux leviers n'etait le bon (28 septembre 2026)

Chantier ouvert sur ma conclusion de la veille : « la piste est
`poleContinentBonusM` et la mise a l'echelle verticale -- de la TECTONIQUE, pas
du climat ». **Les mesures l'ont refutee, et elles ont refute la premisse avec.**

**REFERENCE, graine 20260909, 512 lignes, relevee AVANT de toucher a quoi que ce
soit** -- la regle du depot : un temoin se refait dans l'etat courant.

    ET 13,52 + EF 15,09 = 28,61 %   (Terre 16)
    calotte glaciaire       16,02 % (Terre 10)
    ecart absolu moyen a la Terre : 2,71 points sur 22 classes

#### 1. Le rayon du forcage polaire : REFUTE

`poleForcingRadiusDegSouth` 30 -> 22.

    ET+EF        28,61 -> 26,91   (-1,7)
    calotte      16,02 -> 15,12
    ecart moyen   2,71 ->  2,69   (rien)

**ET IL COUTE SON SOURCAGE** : le commentaire de la regle fonde le 30 sur le
fait que l'Antarctique atteint 63 degres sud a la pointe de sa peninsule, ce qui
est vrai.

**POURQUOI SI PEU, ET C'EST LE FAIT INSTRUCTIF.** La part emergee est recalibree
a 29,2 % : retirer des terres forcees au sud abaisse le niveau marin, et le
relief NATUREL austral emerge a la place. Mesure, bande -70..-60 :
**690 m et 69,7 % emerge -> 857 m et 54,2 %.** Elle MONTE en perdant son
forcage, parce qu'il y a une chaine sous le plateau.

*Par extrapolation, non mesure* : 228 m d'altitude en moins sur la bande
-80..-70 n'ont rapporte que 0,9 point de calotte. `poleContinentBonusM` joue
dans la meme fourchette, donc la piste que j'annoncais comme principale vaut au
mieux un point ou deux. Cela reste une extrapolation.

#### 2. Le plateau austral est une CONSTANTE, et c'est le fait dur

Quatre graines independantes (20260909, 1337, 424242, 7), rayon d'origine :

| bande | 20260909 | 1337 | 424242 | 7 |
|---|---|---|---|---|
| -80--70, part emergee | **100,0** | **100,0** | **100,0** | **100,0** % |
| -80--70, part des terres | **8,03** | **8,03** | **8,03** | **8,03** % |
| -90--80, part emergee | **100,0** | **100,0** | **100,0** | **100,0** % |
| -90--80, part des terres | **2,68** | **2,68** | **2,68** | **2,68** % |
| 70-80 N, part emergee | 8,1 | 7,6 | 7,5 | **62,6** % |

**Identique au centieme sur quatre graines** -- le signe que ce depot connait
par coeur, et pour une fois il ne denonce pas une mesure fausse, il PROUVE :
**10,71 % des terres sont un plateau impose, que la graine ne touche pas.**
L'Antarctique pese 9,3 % des terres. **Le budget terrestre de calotte est donc
consomme ENTIER par le seul forcage, avant la moindre terre naturelle.** Le
nord, lui, depend bien de la graine -- de 7,5 a 62,6 % emerge a 70-80.

ET+EF par graine : **28,6 / 24,4 / 28,1 / 38,4 %** pour 16 attendus. EF descend
jusqu'a 9,5 sur la meilleure graine ; **ET n'est JAMAIS sous 13,5 pour 8
attendus.** L'exces systematique est donc ET, pas EF.

#### 3. Le refroidissement continental : le seul levier qui morde

`climate.continentalCoolingC` 14 -> 8 degres.

    EF          15,09 -> 8,19   (Terre 8,0)   <- quasi exact
    calotte     16,02 -> 8,91   (Terre 10)
    Dfc (taiga)  6,95 -> 8,03   (Terre 9)
    ecart moyen  2,71 -> 2,48
    60-70 N      -6,5 -> -5,2   (Terre -6,0)
    50-60 N       2,1 ->  3,0   (Terre  2,0)
    ET          13,52 -> 17,49  (Terre 8)     <- la calotte devient TOUNDRA

**ET+EF ne descend qu'a 25,68 : l'exces CHANGE DE CASE, il ne disparait pas.**

**UNE NUANCE DE SOURCE, ET ELLE COMPTE.** Le commentaire de la regle fonde les
14 degres sur Iakoutsk (-8,8) contre Bergen (+7,6), soit 16,4. C'est la paire la
plus EXTREME de la Terre -- Iakoutsk est le pole du froid boreal. Des paires
ordinaires donnent bien moins : Moscou (+5,8) contre Bergen = **1,8** ; Winnipeg
(+3,0) contre Vancouver (+10,1) = **7,1**. **Meme famille que « 715 mm n'est pas
une mediane » : une valeur sourcee prise dans la QUEUE de la distribution et lue
comme typique.** Et le commentaire avoue lui-meme une calibration peu
discriminante -- 0 degre donnait 13,8 % de biomes froids, 14 en donne 16,5, soit
2,7 points sur toute la plage.

#### LE MONDE N'EST PAS TROP FROID -- MESURE

- notre pole sud tient **-34 a -35 degres** quand le pole Sud reel a une moyenne
  annuelle de **-49** (Vostok -55). **Notre Antarctique est quatorze degres trop
  CHAUD** ;
- nos terres a 50-60 N sont a **+2,1** quand la Siberie et le Canada a 55 N sont
  entre -3 et -8.

**CE QUI EST REELLEMENT EN EXCES EST ALPIN.** Colonne « alpin » du profil zonal :
**38,1 % a -50..-40 et 27,3 % a -60..-50**, quand la toundra alpine terrestre
pese environ 3 % des terres. Nos latitudes moyennes australes sont une chaine de
montagnes -- 628 a 715 m de moyenne -- la ou leur symetrique nord est a 71-111 m.

**LA PISTE SUIVANTE EST DONC LE RELIEF AUSTRAL DE LATITUDE MOYENNE, ni le
forcage polaire ni le climat.** Et elle depend de la graine : la meme colonne
tombe a 4,7 % sur la graine 1337.

**CE QUE LE PROPRIETAIRE DOIT ARBITRER** : `continentalCoolingC` a 8 ameliore EF
(8,19 pour 8,0), la calotte (8,91 pour 10), la taiga et l'ecart moyen, et
degrade ET. C'est un recalibrage qui regenere tous les mondes en cache. Rien n'a
ete pose : le depot interdit de trancher en silence un arbitrage qui engage le
rendu.

### « Je ne vois jamais de pluie » : une ERREUR D'ECHELLE, pas de frequence (28 septembre 2026)

Rappel du proprietaire, et il recadrait tout : « la base de tout ca c'etait un
reglage meteo avec UDS et UDW [...] le but est de voir des pluies, du ciel
couvert, degage, des orages violents, des aurores boreales et/ou australes, de
la neige ». Le chantier alpin a ete abandonne pour celui-la.

#### LE DEFAUT PRINCIPAL : notre averse la plus violente valait un tiers de bruine

`FWorldseedWeather` documentait `Rain`, `Snow` et `Dust` en **0..1** quand
`Fog` et `CloudCoverage`, DANS LA MEME STRUCTURE et a trois lignes d'ecart,
etaient deja en unites d'UDS. L'incoherence etait ecrite en toutes lettres et
personne ne l'avait lue.

**L'ECHELLE SE MESURE DANS LES PREREGLAGES LIVRES PAR LE PACK**, et c'est la
que la question se tranche sans discussion -- treize assets sous
`Weather_Effects/Weather_Presets/` :

    Rain_Light    rain  3    Snow_Light     snow  3    Sand_Dust_Storm dust 10
    Rain          rain  7    Snow           snow  6    Foggy            fog 10
    Rain_Thunderstorm 10     Snow_Blizzard  snow 10    Clear_Skies      tout 0

**L'echelle est 0..10.** Notre pluie maximale valait donc **1,0, soit le tiers
de la plus legere bruine que le pack sache dessiner** -- et c'est cela, bien
avant toute question de frequence, qui rendait la meteo invisible.

    REGLE : l'echelle d'un pack se releve dans SES assets, jamais dans son
    vocabulaire. « 0..1 » et « 0..10 » se ressemblent dans un commentaire et
    donnent dix fois rien a l'ecran.

Corollaire paye dans la foulee : `VisibleFall` et `VisibleDust`, les seuils du
libelle de regime, valaient 0,05 -- justes en fraction, absurdes sur dix. **Un
seuil oublie lors d'un changement d'unite ne casse rien et ment a chaque ligne.**

#### L'ORAGE ET L'AURORE N'ETAIENT PAS PILOTES DU TOUT

Zero occurrence de « Thunder », « Aurora » ou « Random Weather » dans toute la
source. La cause est architecturale et vaut d'etre comprise : **UDS n'arme
`Thunder/Lightning` que par ses TYPES de meteo tout faits** -- `Rain_Thunderstorm`
et les douze autres -- et nous n'en selectionnons JAMAIS, puisque nous posons
des curseurs continus. Ce choix donne la precision regionale ; il laisse en
echange cette variable a zero pour toute la partie, et **aucun reglage de climat
ne pouvait produire un seul eclair**.

`Aurora Intensity` n'etait pas touchee non plus, **et son defaut n'est pas
zero mais 0,12** : le monde portait une aurore faible et permanente A TOUTES LES
LATITUDES, equateur compris. La piloter corrige ce defaut autant qu'elle en
ajoute une ou il faut.

**ARBITRAGE DU PROPRIETAIRE** : piloter les deux variables nous-memes plutot que
de rendre la main au tirage de types d'UDW -- ce second chemin avait ete mesure
le 12 septembre a 2,3 % de neige par tirage en toundra l'hiver, et il aurait
coute la precision Koppen gagnee le matin meme.

#### LA SONDE QUI REPOND A « EST-CE QUE JE VAIS LE VOIR »

`ProbeCiel` fait defiler UNE ANNEE de jeu -- 27 h, calendrier de trente-six
jours -- sur onze sites repartis en latitude, et rend la part du temps sous la
pluie, la neige, le ciel degage et couvert, plus le NOMBRE d'episodes d'orage et
d'aurore. Elle n'a pas besoin du jeu : `Evaluate` est une fonction PURE du temps
et de la saison.

**ELLE PORTE DEUX COLONNES TEMOINS, et les deux ont servi le jour meme :**
- la CIBLE sans fondu, a cote du vecu : si la part s'effondre entre les deux,
  c'est le lissage qu'il faut regler, pas le climat. Elle a REFUTE mon
  hypothese -- la cible est elle-meme a 0,6 % en taiga ;
- le CUMUL ANNUEL de chaque site, sans lequel on ne peut pas distinguer « le
  modele perd la pluie » de « ce site est sec ». C'est lui qui a rendu le
  diagnostic lisible : 593 mm/an en taiga pour 0,5 % de neige.

**TROIS DEFAUTS DE LA SONDE ELLE-MEME, trouves en la lisant :**
1. *un echantillon arbitraire n'est pas une mesure.* Elle prenait le PREMIER
   point emerge de chaque ligne : a l'equateur elle est tombee sur un BSh
   semi-aride la ou la bande porte de la foret tropicale, et annoncait zero
   averse pour tout le monde. Elle prend desormais la cellule MEDIANE en pluie ;
2. *le pas doit resoudre l'octave la plus rapide du signal*, pas le cycle :
   `Storminess` somme trois octaves dont une bat toutes les trente secondes ;
3. *le fondu fait partie de ce qu'on mesure* -- on rejoue donc `BlendTowards`
   comme le jeu, et l'on compare a la cible brute.

#### CE QUI MARCHE, MESURE

| site | mm/an | pluie | averse | orages/an | aurores/an |
|---|---|---|---|---|---|
| foret tropicale humide, 0 deg | 1881 | **21,7 %** | 0,9 % | **42** | 0 |
| desert chaud, 30 deg | 176 | 0,0 % | -- | 0 | 0 (99,8 % degage) |
| calotte, 75 deg | 143 | 0,0 % | -- | 0 | **40** |
| toundra, -60 deg | 314 | 0,0 % | -- | 0 | **37** |

La foret tropicale tombe exactement dans la fourchette terrestre -- vingt a
trente pour cent du temps sous la pluie -- et ses quarante-deux orages par an
sont du meme ordre que la centaine de la Floride. L'ovale auroral est net :
**quarante episodes par an au-dela de 60 degres, ZERO sous 45**.

#### DEUX CORRECTIONS DE MODELE, ET LA SECONDE EST UNE LECON DEJA ECRITE

**1. La frequence de la pluie venait de la NEBULOSITE.** Le seuil d'occurrence
etait pose sur le pourcentage de ciel couvert, ce qui revient a faire pleuvoir
des qu'il y a des nuages : premiere mesure de la sonde, **96 % du temps sous la
pluie en foret tropicale**. Une foret tropicale est bien couverte quatre-vingts
pour cent de l'annee, mais l'averse convective y est BREVE. La frequence vient
desormais de la QUANTITE, la nebulosite restant un plafond.

**2. UN SEUIL N'EST PAS UNE PART -- QUATRIEME FOIS, et je l'ai refaite le jour
meme ou je l'ecrivais.** `Storminess` somme trois octaves : sa loi est une
cloche, pas une loi uniforme. Seuiller dessus ne rend pas la part demandee --
0,15 donnait 96 % du temps, 0,75 en donnait 0,4. On uniformise donc le signal
avant de le comparer.

    ET J'AI CALCULE L'ECART-TYPE AU LIEU DE LE MESURER. La valeur posee, 0,186,
    vient de l'algebre d'une somme de trois uniformes -- pas d'un releve. Le
    resultat le dit : la taiga devrait voir 6 % de precipitation et en voit 0,5,
    soit un facteur DOUZE. C'est exactement la faute que le paragraphe du dessus
    denonce, commise deux cents lignes plus bas.

#### CE QUI RESTE OUVERT, ET C'EST DIT SANS L'ADOUCIR

**Les climats temperes et froids ne voient toujours presque rien** : taiga
593 mm/an pour 0,5 % de neige, toundra 314 mm/an pour 0,0 %, mediterraneen
476 mm/an pour 0,8 % de pluie. Le cumul est la, le modele le perd.

La cause est identifiee et la voie est nommee : **mesurer la loi de
`Storminess`** -- histogramme sur quelques milliers de tirages -- au lieu de la
supposer, puis inverser sa vraie fonction de repartition. Tant que ce n'est pas
fait, `StormEcartType` est un chiffre calcule, et ce depot sait ce que valent
les chiffres calcules qu'on n'a pas mesures.

Ce qui est livre reste un PROGRES NET et sans regression : avant, la pluie etait
invisible PARTOUT, il n'y avait aucun orage, aucune aurore, et une aurore
parasite a l'equateur.

#### La loi de `Storminess`, enfin MESUREE -- et elle rend deux a trois fois plus (28 septembre 2026)

Suite immediate du chantier ci-dessus, et reparation de la faute qu'il avouait :
j'avais CALCULE l'ecart-type du signal d'agitation au lieu de le RELEVER.

**LE RELEVE**, quatre graines, une annee de jeu chacune, 51 840 tirages :

    moyenne 0,5011   ecart-type 0,1616   support BORNE a [0,0435 ; 0,9327]
    quantiles (pas de 5 %) : 0,0435 0,2315 0,2855 0,3244 0,3558 0,3832 0,4098
      0,4342 0,4575 0,4801 0,5027 0,5250 0,5474 0,5707 0,5946 0,6194 0,6469
      0,6785 0,7152 0,7642 0,9327

**DEUX CHOSES ETAIENT FAUSSES DANS MON CALCUL, et la seconde comptait le plus :**

- `ValueNoise` n'est PAS uniforme. Il interpole DEUX tirages uniformes par un
  smoothstep, ce qui resserre la loi autour de sa moyenne : l'ecart-type reel
  est 0,1616 quand l'algebre d'une somme de trois uniformes en donne 0,186 ;
- **la somme a un SUPPORT BORNE, donc ses queues tombent bien plus vite que
  celles d'une cloche** -- et c'est exactement la que le seuil de pluie
  travaille. Au quantile 0,95 le signal vaut 0,7642 quand la logistique posee
  le placait a 0,918. L'erreur de quinze pour cent sur l'ecart-type etait
  benigne ; celle sur la FORME des queues valait un facteur trois.

**LA TRANSFORMEE EST DESORMAIS UNE TABLE DE QUANTILES MESUREE**, interpolee --
c'est-a-dire la fonction de repartition empirique. Verification :

    pire ecart a l'uniforme   0,1858  ->  0,0001

**ET L'ORACLE GARDE LA CORRESPONDANCE, pas le chiffre.**
`Worldseed.Meteo.LeSignalEstUniforme` releve la loi du signal REEL et exige que
`Uniformiser` la rende uniforme a quatre centiemes pres. Changer les poids des
octaves, ou la forme de `ValueNoise`, fait donc tomber un test au lieu de
deregler le ciel en silence -- et le releve part au journal meme quand il passe,
pour que recalibrer la table ne demande pas de refaire l'instrument.

**CE QUE CA DONNE, bulletin du ciel, memes sites :**

| site | avant | apres |
|---|---|---|
| foret tropicale, 0 deg | 21,7 % de pluie, 42 orages/an | **25,9 %, 66 orages** |
| subtropicale humide, 15 deg | 3,0 %, 0 orage | **6,9 %, 1 orage** |
| mediterraneen, 45 deg | 0,8 % | **2,4 %** |
| taiga, 60 deg | 0,5 % de neige | **1,9 % de neige, 0,5 % de pluie** |

Tout a double ou triple, et la foret tropicale tombe au centre de la fourchette
terrestre -- vingt a trente pour cent du temps sous la pluie.

**UN ATTENDU DE TEST EST TOMBE, ET IL AVAIT TORT.** `Worldseed.Meteo.
DistingueLesClimats` exigeait qu'une foret tropicale pleuve plus d'UN TIERS du
temps : cet attendu avait ete cale sur le modele ou la pluie se declenchait des
qu'il y avait des nuages -- celui qui donnait 96 %. Le releve terrestre place une
foret tropicale entre vingt et trente pour cent, on en mesure 32, donc le seuil
descend a un cinquieme. **Le garder aurait exige de rendre le modele moins juste
pour qu'un test passe** ; ce n'est pas un elargissement de complaisance, et la
raison est ecrite dans le test.

**CE QUI RESTE, DIT SANS L'ADOUCIR.** Les climats froids restent bas -- taiga
1,9 % de precipitation pour 593 mm par an, la ou la frequence NOMINALE du modele
en vise 7,5. Il reste donc un facteur trois, et il ne vient plus du signal :
l'echelle de frequence est concave, si bien que la moyenne des quatre saisons
rend MOINS que la frequence de la moyenne -- inegalite de Jensen -- et le seuil
de visibilite mange le reste. C'est un calage, plus un defaut de forme.

### L'aurore avait un INTERRUPTEUR MAITRE, et il arrivait eteint (28 septembre 2026)

Signale en jeu : « je ne vois pas d'aurore ». Le site etait bon -- toundra
australe, latitude -60,1, vingt-trois heures -- et **tous les controles disaient
oui** : les treize variables d'UDS/UDW acceptees, l'heure posee et acceptee,
l'horloge en marche, la latitude juste. Releve sur le defaut de classe
d'`Ultra_Dynamic_Sky_C` :

    Use Auroras                = False     <- l'interrupteur maitre
    Using Either Aurora        = False     (derive)
    Using Volumetric Aurora    = False     (derive)
    Aurora Intensity           = 0.12
    Daytime Aurora Intensity   = 0.0

**NOUS ECRIVIONS FIDELEMENT UNE VALEUR QUE RIEN N'EVALUAIT.** C'est le piege
que ce fichier consigne depuis le carre d'ocean -- « une couleur invisible a
deux causes opposees : le terme qui ne s'evalue jamais, et le terme dont rien
n'atteint l'ecran ». Ici c'etait le second, et la parade habituelle -- relire ce
qu'on ecrit -- ne pouvait RIEN voir : l'ecriture prenait.

    REGLE : devant un effet qui ne s'affiche pas, chercher l'INTERRUPTEUR
    MAITRE de la fonctionnalite avant de regler son intensite. Un pack tiers
    livre ses fonctions couteuses eteintes, et une intensite non nulle sur une
    fonction eteinte se lit exactement comme un reglage qui marche.

**ET POSER LE BOOLEEN NE SUFFIT PAS -- TROISIEME FOIS.** `Use Auroras` est
repliquee a RepNotify, comme `Animate Time of Day` : la poser par reflexion arme
le drapeau -- il se relit meme a vrai -- sans reveiller quoi que ce soit, parce
que c'est `OnRep_Use Auroras` qui recalcule les deux drapeaux DERIVES que le
rendu consulte. La regle etait deja ecrite pour l'horloge ; elle vaut pour toute
variable Blueprint posee par reflexion, et l'inventaire des fonctions
(`BlueprintService.list_functions`) dit en une ligne si un `OnRep_<nom>` existe.

**CORRECTION D'UNE NOTE FAUSSE QUE J'AI ECRITE ET DITE AU PROPRIETAIRE.** Le
commentaire de ce bloc annoncait que le defaut de 0,12 posait « une aurore faible
et permanente a toutes les latitudes, equateur compris ». Il n'y en avait
**aucune, nulle part** : 0,12 etait l'intensite d'une fonctionnalite eteinte.
Une valeur par defaut non nulle ne prouve pas qu'elle serve.

**PIEGE D'INVENTAIRE, ET IL A COUTE LE DIAGNOSTIC D'UNE SEANCE.** Mon premier
balayage des variables d'aurore a ete **tronque a dix-huit entrees** et il a
manque `Use Auroras` -- c'est-a-dire la seule qui comptait. Un inventaire
partiel se lit exactement comme un inventaire complet. Compter les entrees
rendues et le dire, ou ne pas conclure de l'absence.

**VU A L'IMAGE**, ce qui est la seule validation qui vaille : site
(-19938, -13875), latitude -59,9, toundra a -13 C, 23 h, ciel degage -- rubans
verts et violets d'un bord a l'autre du ciel. `-WorldseedHeure=` pose l'heure de
depart, en centiemes d'heure comme UDS (23 et 2300 acceptes tous deux), sans
quoi il faut attendre une demi-heure de journee avant de pouvoir juger.

### La sonde du ciel mesurait son propre lissage (28 septembre 2026)

Suite de l'aurore. La neige et la pluie etant vues, restait l'orage -- et la
seance a rendu un defaut d'INSTRUMENT bien plus gros que le defaut cherche.

**LA SONDE FONDAIT QUINZE FOIS PLUS FORT QUE LE JEU.** `ProbeCiel` lissait sur
`pas / periode`, soit une constante de temps d'une PERIODE ENTIERE (180 s),
quand `UWorldseedSkyDriverComponent` fond sur **douze secondes reelles**. Et le
commentaire annoncait deja « LE MEME FONDU QU'EN JEU » : c'est cette phrase qui
l'a rendu invisible pendant toute la seance precedente. **Une note exacte posee
sur du code faux ne protege rien** -- le depot a exactement la meme entree pour
la glace ecrite dans le cache.

Meme graine, meme monde, seul le fondu de la sonde corrige :

    site                        avant   apres   cible sans fondu
    foret tropicale seche, 0        9     109      119
    savane, -15                    13      88      105
    savane, +15                     1      42       56
    aurores, taiga 60              62     136       --
    neige en taiga                6,5 %   8,2 %     --

**ET CELA ANNULE UN DIAGNOSTIC QUE J'AVAIS ECRIT ICI LA VEILLE.** J'y affirmais
que les climats froids sous-pleuvaient d'un facteur trois -- « taiga 1,9 % de
precipitation pour 7,5 nominaux » -- et j'attribuais ce manque a l'inegalite de
Jensen sur la courbe de frequence, plus le seuil de visibilite. La taiga voit
8,2 % de neige plus 3,1 % de pluie, soit **11,3 %** : elle est AU-DESSUS du
nominal. Le facteur trois etait mon fondu, et le mecanisme invoque n'avait rien
a expliquer. **Une explication physique plausible posee sur une mesure fausse
se lit exactement comme un diagnostic.**

`AlphaDeFondu` et `FonduDefautS` vivent desormais dans `WorldseedWeatherState`,
appeles par la sonde ET par le pilote : la formule etait recopiee dans les deux
fichiers, et elle avait diverge.

#### GUETTER UN EVENEMENT RARE NE SEPARE PAS LES DEUX CAUSES

Quarante captures sur neuf minutes, zero orage. Ce zero est compatible avec les
deux explications opposees que ce fichier consigne depuis le carre d'ocean : le
modele n'en demande aucun, ou il en demande et rien n'atteint l'ecran. **C'est
le proprietaire qui a pose la question qui tranche** -- « est-il possible de
verifier plus rapidement que d'attendre ? ». D'ou `-WorldseedOrageForce=<0..10>`,
qui pose la valeur APRES le fondu -- posee sur la cible, le lissage ecreterait
le temoin lui-meme et l'on retomberait dans la question de depart.

Verdict en une capture : **les eclairs tombent**, la chaine de foudre est saine,
et aucun interrupteur maitre ne dort (925 et 584 variables balayees ; `Spawn
Lightning Flashes`, `Enable Obscured Lightning`, `Lightning Flash Light Source`
et `Lightning Flashes Cast Shadows` sont VRAIS par defaut).

**UN ECLAIR SE DETECTE A LA LUMINANCE MOYENNE DE L'IMAGE ENTIERE**, pas au ciel :
c'est une source de lumiere, elle eclaire la scene. Le pic se lit sans
ambiguite contre ses voisines -- 126 -> 154 a ciel clair, 100 -> 141 sous
l'orage.

#### UN TEMOIN DOIT POSER UN ETAT COHERENT, SINON ON DEBOGUE UNE CHIMERE

La premiere version ne forcait que `Thunder` : des eclairs sous un ciel bleu,
signale aussitot. **J'en ai conclu que le modele calculait la couverture
independamment de la pluie, et je l'ai dit au proprietaire avant d'avoir lu la
ligne.** Elle dit :

    CloudCoverage = Lerp(Clear, Overcast, clamp(CloudyFraction * 0,5 + Occurrence * 0,8))

Le couplage existe, et la pluie y **domine** la nebulosite climatique. Le defaut
etait dans l'instrument. Le temoin force desormais les trois grandeurs ensemble,
comme `Evaluate` les produirait a plein regime.

#### UN DETECTEUR DE COULEUR SE VALIDE SUR UN CAS NEGATIF CONNU

Mon guet comptait les pixels « orange » du bandeau dans une bande de l'ecran.
Sur la savane ocre il a rendu **1794 pixels et crie victoire** alors que le
bandeau affichait `orage 0.00` en argent : il mesurait le SOL. Le zero obtenu
plus tot sur fond vert etait une chance, pas une mesure. Critere resserre a
`R>230, G 130-180, B<60` et VALIDE a zero sur ce cas negatif, sur fond ocre
comme sur fond vert. **Localiser les pixels comptes avant de lire le compte** --
le depot a deja cette note, avec la dune et la jambe du personnage.

#### DEUX PIEGES DE MONTAGE, TOUS DEUX DEJA AU REGISTRE SOUS UNE AUTRE FORME

- **Comprimer le cycle pousse le defaut hors de portee.** Un guet lance a
  `-WorldseedMeteoPeriode=20` ne pouvait RIEN montrer : a vingt secondes de
  cycle, les douze secondes de fondu du jeu couvrent **soixante pour cent** de
  la periode et ecrasent tout pic.
- **La sonde et le jeu doivent tourner a la MEME resolution.** Lancee a 512
  lignes quand le jeu tourne a 2048, elle a rendu les coordonnees d'un monde de
  travail : le site vise portait 1881 mm/an chez elle et 1344 en jeu, et le
  biome n'etait pas le meme. A 2048 le meme site ne compte plus 66 orages mais
  109 -- les deux mondes ne sont pas comparables.

**ET LE HANDLE D'UNE FENETRE DE JEU SE RELIT A CHAQUE PASSE.** Celui qu'on
obtient dans les premieres secondes est le SPLASH, detruit ensuite : le figer
rend « fenetre absente » pour toujours, et cela a coute deux series completes.

**PIEGE POWERSHELL** : `git log -1 --pretty=%B` rend un TABLEAU de lignes ; un
`-replace` suivi d'un `Out-File -NoNewline` les colle en une seule ligne et
detruit le message de commit. Joindre explicitement, ou reecrire le message
entier depuis un here-string.

#### CE QUI RESTE OUVERT, ET IL FAUT LE DIRE

**LES SIX EVENEMENTS DE LA LISTE DU PROPRIETAIRE SONT VUS** -- pluie, ciel
degage, ciel couvert, neige, aurore, orage. Mais **l'orage a ete vu par le
TEMOIN**, donc force : ce qui est prouve est que la chaine atteint l'ecran et
que les trois grandeurs sont coherentes, pas la frequence.

**ET UNE MESURE NE SE RECOUPE PAS :**

    la sonde annonce          109 orages/an en foret tropicale seche
    le guet en jeu a rendu    0 sur 9 minutes, cycle comprime a 60 s

A 109 par an on attendait un episode toutes les cinq minutes. Deux explications
possibles, AUCUNE VERIFIEE : la compression a 60 s amortit plus que je ne l'ai
estime -- les douze secondes de fondu y pesent 20 % de la periode contre 6,7 en
regime nominal -- ou bien le point ou le pion s'est pose n'est pas celui que la
sonde a mesure, le HUD disant *Savane, 885 mm/an* la ou elle annoncait *foret
tropicale seche, 942*. **Tant que ce n'est pas tranche, « 109 par an » reste un
chiffre de sonde et non un fait de jeu.**

**DECISION DU PROPRIETAIRE, 28 septembre 2026** : ne pas attendre un orage
naturel pour clore -- « je suis sur que je le verrai en jeu ». Le controle qui
trancherait, le jour ou l'on y revient, est un guet a cadence NOMINALE (180 s)
sur le site exact de la sonde, avec le biome relu au HUD avant de compter.

### Le sable vole : ce qui l'empechait, et ce qui ne l'empechait PAS (29 septembre 2026)

Demande : « est-il possible d'utiliser UDS/UDW pour des tempetes de sable, ou
sans parler de tempete de voir du sable voler ? ». Oui, et c'etait deja branche.

**LE DEFAUT LE PLUS UTILE DE LA SEANCE N'ETAIT PAS LA POUSSIERE.**
`-WorldseedCielClair` NE FIGEAIT PLUS L'HORLOGE depuis le 28 septembre.
`CielDInspection` la fige au BeginPlay ; `ArmerHorloge`, appelee sept secondes
plus tard par le minuteur du ciel, la reposait a vrai sans jamais consulter le
drapeau. Le dernier ecrivain gagnait. **Preuve sur SEPT journaux independants** :
« horloge figee sur 1 acteur(s) UDS », puis « horloge : armee », puis « le temps
passe -- 1300,0000 a 1303,3407 en 5,5 s », coherent sept fois.

**ET FIGER N'EST PAS ARRETER** : `CielDInspection` posait le booleen par
reflexion, ce qui ne declenche AUCUN rappel -- or `OnRep_Animate Time of Day`
demarre ET arrete la boucle. Ce depot avait paye la moitie « demarrer » trois
fois, et la moitie « arreter » etait restee.

**CE QUE LE CORRECTIF RAPPORTE DEPASSE CE CHANTIER.** Le registre mesurait
**60 a 82 % de pixels changes entre deux lancements REPUTES IDENTIQUES**, et en
tirait qu'un A/B d'image par lancements successifs ne vaut rien. Horloge
reellement figee, deux lancements identiques rendent **0,1 sur 124 de clarte**.
Les A/B d'image sont redevenus utilisables ; le bruit venait de la, pas de Lumen
ni de TSR.

#### Ce qui n'etait PAS la cause, et je l'avais ecrit dans un commit

UDW porte **dix** surcharges `<curseur> - Manual Override`, toutes a FAUX, et
nous n'en posions qu'une. J'en ai conclu que `Dust` etait ecrase au tick suivant
-- le commentaire du tonnerre le dit pour lui-meme -- et j'ai pose le temoin ET
les surcharges ENSEMBLE, puis mesure une fois. **Deux changements, une mesure.**

L'A/B monte apres coup (`-WorldseedPoussiereSurcharge=0`) tranche :

    surcharges ARMEES     clarte 139,9
    surcharges DESARMEES  clarte 140,9     (sans poussiere : 124,4)

Desarmer ne ramene pas vers 124 : **elles ne changent rien a la visibilite**,
parce qu'on ecrit `Dust` deux fois par seconde -- ecrase au tick d'UDW, il est
reecrit au notre. Elles sont gardees en CEINTURE, pas en correctif. Le corps du
commit a ete corrige.

#### Ce qui est etabli, et par quelle mesure

**`Dust` DOSE, et tres non lineairement.** Balayage au meme point (desert chaud
BWh, 22 C, 163 mm/an), horloge figee, clarte du lointain :

    Dust 0      124,4    |  Dust 0 bis  124,3   <- TEMOIN de bruit
    Dust 2      128,8    |  Dust 5      136,3   |  Dust 10   140,5

Le pas 0->2 vaut **quarante-quatre fois** le bruit. A l'image : 0 = ciel bleu
franc et palmiers nets jusqu'au fond ; 5 = voile leger, lointain estompe, c'est
le « sable qui vole sans tempete » ; 10 = horizon entierement mange.

> **CETTE LIGNE DISAIT « UN PLANCHER PERMANENT A 1,5 SERAIT INVISIBLE » ET ELLE
> ETAIT FAUSSE**, contredite par la mesure du paragraphe juste au-dessus. Je
> l'avais tiree de ma lecture A L'OEIL du cas 2 -- que la mesure venait pourtant
> de dementir dans la meme seance. Deux affirmations incompatibles a six lignes
> d'ecart. **Un voile permanent DOIT etre discret a l'oeil et net a la mesure**,
> et c'est exactement ce que rend `Dust = 2` : +4,4 de clarte, quarante-quatre
> fois le bruit. La valeur retenue est donc 2,0, et elle est vue en jeu --
> clarte naturelle 128,0 contre 128,8 pour le temoin force a 2.
>
> **LA LECON N'EST PAS LE CHIFFRE, C'EST L'ENCHAINEMENT** : j'ai mesure, la
> mesure m'a dementi, je l'ai ecrit -- puis j'ai quand meme conclu avec mon
> impression de depart. Une mesure qui dement une intuition doit remplacer
> l'intuition PARTOUT, y compris dans la conclusion qu'on avait deja redigee.

**LA SATURATION N'EST PAS UNE BONNE METRIQUE ICI** : elle baisse de 0 a 5 puis
REMONTE a 10, parce que le voile n'est pas gris mais OCRE, donc lui-meme sature.
C'est la CLARTE qui est monotone -- un voile eclaircit le lointain par
diffusion. Et j'avais juge le cas 2 « imperceptible » a l'oeil : il vaut -25 %
de saturation. **L'oeil a tort sur les faibles doses ; la mesure tranche.**

#### Les deux prereglages de sable ne different QUE par le vent

Releve par l'API sur les treize prereglages du pack :

    Sand_Dust_Calm    Dust 10   Wind  1   Fog 1   Cloud NON surchargee
    Sand_Dust_Storm   Dust 10   Wind 10   Fog 1   Cloud NON surchargee

La tempete du pack est un etat de **VENT**, la poussiere en etant l'effet -- ce
qui est aussi la physique de la saltation. Un temoin de poussiere ne doit donc
PAS copier celui de l'orage, qui monte la nebulosite a 9 : ce serait une chimere
a l'envers.

#### La reserve sur le parsing binaire etait justifiee

Les valeurs avaient d'abord ete tirees d'un **parsing binaire des `.uasset`**,
ce que la regle du projet interdit. L'API confirme toutes les VALEURS -- mais
quatre NOMS de ce releve ne sont pas des variables : `Dusty`, `Dust Bias`,
`Dust Spawn Rate Scale`, `Wind Gust Update Period`. **La table de noms d'un
paquet contient aussi les fonctions et les broches.** `Knots at Wind Intensity 10`
n'existe pas non plus : **l'unite physique du vent reste NON ETABLIE**, et il ne
faut pas la citer.

#### Trois gardes a reprendre dans toute sonde d'inventaire

- **le COMPTE s'imprime** (925 variables sur le ciel, 584 sur la meteo) : un
  inventaire tronque se lit exactement comme un inventaire complet ;
- **les noms se demandent UN PAR UN**, et une absence doit etre bruyante ;
- **une sonde de fonctions se valide sur un cas connu.** `OnRep_Animate Time of
  Day` existe, puisque le C++ l'appelle avec succes : sans ce temoin, « aucun
  rappel trouve » ne se distingue pas de « la sonde est cassee ».

#### Divers, paye comptant

- **Les « Error: Condition failed » au demarrage d'une passe d'automation sont
  du BRUIT DU MOTEUR**, pas nos oracles : ils suivent des lignes
  `UE::UnifiedErrorTest` et tombent pendant le chargement, avant que nos tests
  ne demarrent. Le decompte qui vaut est `LogAutomationController ... Test
  Completed` -- **130 sur 130 en Success** ce jour-la.
- **Le moteur est dans `C:\Program Files\Epic Games\UE_5.8`**, pas sous `D:\UE`,
  et il n'est declare NI dans `HKLM\SOFTWARE\EpicGames` NI dans
  `HKCU\...\Builds`. `BuildAndLaunchGame.ps1` le trouve par sa liste de chemins
  usuels ; une compilation directe passe par
  `Engine\Build\BatchFiles\Build.bat WorldseedEditor Win64 Development -Project=...`.
- **Une vue libre coute 38 s** avec `-WorldseedQuitter` a 1280x720, et les photos
  vont dans `Saved/Photos/<nom>.png`. C'est abordable, contrairement a la
  tournee des formes.

#### Reste ouvert

- **La vraie cause du « je n'en vois jamais » n'est PAS etablie.** Elle est
  probablement dans le MODELE : `bDustPresent` exige moins de 250 mm/an ET plus
  de 5 C -- ce qui exclut le Gobi -- et le seuil de 0,62 est pose sur le signal
  d'agitation BRUT, non uniformise : le defaut « un seuil n'est pas une part »,
  cinquieme fois. La pluie, vingt lignes plus haut dans le meme fichier, passe
  par `Uniformiser` pour cette raison exacte.
- **`Snow - Manual Override` est a faux lui aussi** -- utile le jour ou l'on
  fera les tempetes de NEIGE, demandees le 29 septembre. `Snow_Blizzard` existe
  deja dans le pack : Snow 10, **Wind 10**, Cloud 10, Fog 10, Material Snow
  Coverage 1. La meme chaine servira, et le vent y est encore l'axe.
- **`Max Dust Coverage` vaut 0,5** : la couverture de poussiere est plafonnee a
  la moitie, et personne n'a mesure ce que cela coute.

### La poussiere sort du vent : ce que la sonde a trouve APRES coup (29 septembre 2026)

Suite du meme chantier. La forme est ecrite -- le vent sort du signal
d'agitation, la poussiere sort du vent -- et **c'est la sonde qui a trouve les
deux defauts que je n'avais pas vus**, dont un absurde.

**LA CALOTTE GLACIAIRE POUDROYAIT.** Retirer le `T > 5` en dur a bien rendu sa
poussiere au Gobi -- c'etait le but, et l'arbitrage du proprietaire -- et il a
aussi donne **99,9 % de voile et 56 tempetes par an a -18,8 C**. Un desert
polaire est couvert de GLACE. Le bon critere n'est donc pas la CHALEUR mais le
**GEL PERMANENT** : sous une certaine moyenne annuelle, le sol reste en
pergelisol et sous la neige toute l'annee, et il n'a rien a donner au vent.
C'est le critere EF de Koppen, pose en continu. **Les deux termes se
MULTIPLIENT** : il faut un sol sec ET degele.

**LE VENT SOUFFLAIT SANS CESSE, ET LE SIGNE ETAIT LE MEME QUE D'HABITUDE.**
6,3 sur 10 en moyenne, **IDENTIQUE sur les vingt-deux sites** -- plus du double
d'`Overcast` (3) en permanence. Cause : `Souffle` est UNIFORME par construction,
c'est tout l'objet d'`Uniformiser`, donc une rampe lineaire rend une moyenne au
MILIEU de la plage. Le vent reel est tres dissymetrique (loi de Weibull) :
`ventForme = 3` ramene la moyenne a 3,25 sans toucher au maximum.

    site                  voile %      sable %      tempetes/an   vent moy
    calotte glaciaire     99,9 -> 0,0   0,0          56 -> 0       6,3 -> 3,5
    desert chaud BWh      99,9          9,6 -> 1,4   50 -> 12      6,3 -> 3,5
    desert froid BWk      99,9          8,3 -> 1,1   44 -> 12      6,3 -> 3,5
    taiga, savane, forets   0,0         0,0          0             3,6

**LE FONDU NE DECAPITE RIEN, et c'etait le risque principal.** La colonne CIBLE
de la sonde -- meme seuil, sans fondu -- rend **12 la ou le vecu rend 12**. Ce
depot avait exactement ce piege sur les orages (9 annonces pour 119 designes),
et `poussierePeriodeFacteur = 3` le couvre : ralentir le signal ne deplace aucun
quantile, mais troque du NOMBRE d'episodes contre de la DUREE, ce qui laisse au
fondu de douze secondes le temps de suivre.

#### Un defaut d'INSTRUMENT qui aurait fait regler le mauvais bouton

`ProbeCiel` prenait la cellule **MEDIANE en pluie** de chaque ligne de latitude,
et son commentaire dit pourquoi : « un echantillon arbitraire n'est pas une
mesure ». C'est juste pour la pluie. Mais **la cellule mediane de la ligne 30
degres n'est pas le Sahara** : elle ne porte presque jamais d'aridite, donc la
colonne poussiere aurait rendu ZERO quel que soit le modele -- et l'on aurait
conclu que la chaine ne marchait pas. Un second site au **decile inferieur** de
pluie repare cela ; pas le minimum, qui serait l'extremum arbitraire que le
commentaire denonce.

#### La promesse « le monde ne bouge pas » se PROUVE

Changer `world_rules.json` invalide tout cache par son empreinte MD5, donc
impose une regeneration -- ici 4 min en 4096x2048. Le controle qui tranche est
de comparer le **CORPS** du fichier (tout sauf l'en-tete de 68 octets, qui porte
justement l'empreinte) : trois caches de **CLES differentes**, dont un anterieur
au chantier, portent le meme MD5 `CE255F6745CA1D2E` sur 82 461 649 octets.
Devant un cache dont la cle a bouge sans que le code du monde change, comparer
les corps et non les noms de fichier.

#### Reste ouvert

- **LE VENT EST LE MEME PARTOUT DANS LE MONDE au meme instant** : le signal
  d'agitation ne depend que du TEMPS, jamais du lieu. La colonne « vent max » le
  rend visible en affichant 10,0 sur les vingt-deux sites. Le rendre local
  demanderait de le lier a la latitude -- les rails de depressions existent deja
  dans le modele de climat -- mais ce n'etait pas demande.
- **`Max Dust Coverage` vaut 0,5 dans UDW**, et personne n'a mesure ce que ce
  plafond coute a l'image.
- **Le sable au ras du sol (volet B) n'est pas commence.** Le releve donne son
  point d'entree : `Dust Niagara System` sur l'acteur meteo, et ses 53
  parametres utilisateur -- dont `Spawn Box Height`, `World Spawn Offset` et
  `Stick Particles to Surface`. « Du sable qui rampe » n'est pas un systeme a
  ecrire, c'est celui du pack APLATI.

### Les nappes rasantes : POURQUOI ON S'ARRETE, et par ou reprendre (29 septembre 2026)

**CHANTIER SUSPENDU PAR DECISION DU PROPRIETAIRE**, apres un diagnostic complet.
La DECISION est faite, testee et gardee ; c'est l'EFFET qui bute.

**CE QUI MARCHE ET RESTE EN PLACE.** `WorldseedReptation::Evaluer` est une
fonction PURE qui dit, en un point, ce qui rampe -- sable ou neige -- et a
quelle force. Une seule mecanique pour deux matieres, parce que c'est le meme
fait physique : la poudrerie souleve la neige DEJA AU SOL par le vent, comme la
saltation souleve le sable. Quatre oracles la gardent, dont celui qui verifie
qu'a part egale et vent egal les deux rampent **exactement pareil**. Le
declencheur ne s'invente pas : c'est le canal « aride » des poids de matiere,
celui que le semis de vegetation emploie deja.

**LE MUR : LE SYSTEME NIAGARA DU PACK NE SE REINSTANCIE PAS.** Il tourne --
etat Active, age qui avance, mille particules par seconde, visible, non elimine
par la distance -- et il ne dessine RIEN.

    hypothese                      verdict
    mes reglages de forme          NON : en mode brut (valeurs d'usine), identique
    le materiau                    NON : GPU_Dust porte bien Dust_ParticleMat
    `Sprite Scale` a zero          NON : corrige, sans effet
    attache au terrain (a 14 km)   NON : attache au PION, identique
    `NewObject` vs SpawnSystem     NON : identique une fois le support corrige

**CE QUI RESTE, ET QUI N'EST PAS VERIFIE** : le systeme porte des interfaces de
DONNEES -- `Impact Normals` (NiagaraDataInterfaceArrayFloat3), `Start Positions`,
`End Positions`, `Particle Paths CPU`, `Recycled Paths`. Ce sont des TABLEAUX,
remplis par le Blueprint d'UDW. Sans trajectoires a suivre, des emetteurs GPU
peuvent tourner sans rien produire. **C'est l'hypothese a tester en premier si
l'on reprend**, et elle est coherente avec le fait que le meme systeme, pilote
PAR UDW, s'affiche parfaitement.

**PAR OU REPRENDRE, dans l'ordre :** verifier cette hypothese des tableaux ;
sinon, un systeme Niagara a NOUS, fait a la main -- le graphe n'est pas
scriptable, `NiagaraEditorLibrary` n'existe meme pas cote Python, donc c'est une
exception a la regle du projet et une decision du proprietaire.

#### Trois defauts de MESURE payes sur ce chantier, et ils se ressemblent

- **J'AI MESURE LA ZONE OU J'ATTENDAIS L'EFFET.** Trois reglages rendaient le
  meme chiffre au SOL, j'en ai conclu « invisible » -- les grains etaient dans
  le CIEL, et c'est le proprietaire qui l'a vu en levant les yeux. Une mesure
  ciblee confirme surtout son propre cadrage : elle doit s'accompagner d'une
  mesure de l'image ENTIERE.
- **J'AI CONCLU SUR UNE IMAGE NOYEE, SANS TEMOIN.** « La nappe est trop dense »
  -- le temoin monte ensuite a rendu 137,2 contre 136,9 : c'etait le voile
  atmospherique a 10 qui saturait. Et j'avais place l'oeil a 180 METRES du sol
  en croyant poser des centimetres (`-WorldseedVueH=` est en metres).
- **LE TEMOIN DE POUSSIERE N'ETAIT PAS REPRESENTATIF** : il force `Dust` ET le
  vent a la meme valeur, alors qu'a vent 9 le modele rend un voile de 3,6. Un
  temoin doit poser un etat que le monde peut produire, sinon on regarde une
  scene qui n'existe pas. D'ou `-WorldseedVentForce=`.

### Le pack livre ses effets DESARMES, et c'est desormais un reflexe (29 septembre 2026)

Quatrieme et cinquieme occurrence du meme piege, apres `Use Auroras` et les dix
surcharges manuelles d'UDW :

    Enable Screen Frost      faux      Enable Screen Droplets   faux

Les deux effets sont COMPLETS -- materiaux `Screen_Frost` et `Screen_Droplets`
assignes, textures `Snow_Normal` et `Frost_Scatter` en place, durees de
formation et d'effacement reglees (8 s et 12 s), et surtout
`Screen Frost from Snow` = 1, donc le givre suit DEJA la neige. Il n'y avait
rien a piloter : deux booleens et leurs rappels, et le givre apparait aux
quatre bords de l'ecran sous un blizzard.

**LE REFLEXE A PRENDRE : devant une fonctionnalite d'un pack tiers qui ne se
voit pas, chercher son interrupteur AVANT de regler son intensite.** Ce depot l'a
paye cinq fois en un mois, et les cinq fois le reste etait deja cable.

**ET LE DOSAGE DES PARTICULES EST TRES INEGAL DANS CE PACK** : poussiere 1000
grains a 0,60 d'opacite, neige et pluie 20 000 a 1,00. Vingt fois moins pour le
sable. Mesure du renforcement, contraste du lointain sous Dust = 8 : 1000 ->
36,12 au ciel et 25,41 au sol ; 6000 -> 32,82 et 23,57 ; 12000 -> 29,00 et
20,35. **C'est le CONTRASTE qui bouge, pas la clarte** -- une densite de grains
ne deplace pas la luminance moyenne, elle mange les DETAILS. Mesurer la clarte
sur cette question ne voit rien, et c'est ce qui m'avait egare.

### Le pack a une DOCUMENTATION, et elle nomme le piege paye trois fois (29 septembre 2026)

`Ultra Dynamic Sky 9.7 Documentation.html` -- 245 Ko, 1308 lignes de texte une
fois les balises retirees, une entree par fonction du pack. **Elle n'avait
jamais ete lue.** Le proprietaire l'a signalee au moment ou ce depot en etait a
sa TROISIEME variable ecrite fidelement et jamais evaluee.

**LA REGLE GENERALE Y EST ECRITE EN TOUTES LETTRES**, section « Changing a
Property at Runtime Has No Effect » :

> *Some properties, if you just set them directly at runtime, will have no
> effect. In most cases this would be because they are what the system calls
> **static properties**. They are applied when the system starts up [...] For
> these, you can call one of the **Static Properties** functions to apply the
> change.*

Nous avions reconstitue ce fait a la main, trois fois, sous la forme
particuliere des `OnRep_` : `Animate Time of Day`, `Use Auroras`, puis
`Simulate Real Sun`. Mesure du 29 septembre : `OnRep_Simulate Real Sun`
**n'existe pas**, `Static Properties - Sun` **oui**, et c'est elle qui applique.
Il existe aussi `Hard Reset Cache`, qui force tout d'un coup, et
`Max Property Cache Period` pour la latence d'application.

**LA REGLE POUR LA SUITE** : devant un effet du pack qui ne s'affiche pas, on
cherche dans CET ORDRE -- l'interrupteur maitre de la fonctionnalite, puis
`OnRep_<nom>`, puis `Static Properties - <categorie>`. Et l'on ne conclut que
sur un EFFET mesure, jamais sur le drapeau relu : un drapeau se relit a vrai des
qu'on le pose, et c'est exactement ce qui a laisse vivre les trois defauts.

**COMMENT LA LIRE SANS L'OUVRIR** : `sed -e 's/<[^>]*>/ /g'` puis un decodage
des entites rend un texte grepable de 197 Ko ; `grep -o` sur les balises de
titre donne le plan complet en une commande. Les deux tiennent dans un contexte.

**ET UN INVENTAIRE DE VARIABLES SE FAIT PAR `list_variables`, PAS PAR `dir()`.**
`unreal.BlueprintService.list_variables` prend le chemin du BLUEPRINT
(`/Game/UltraDynamicSky/Blueprints/Ultra_Dynamic_Weather`), **jamais celui de la
classe generee** qui finit en `_C` -- celui-la rend zero, en silence. Et le champ
du struct rendu s'appelle **`variable_name`**, pas `name` : `get_editor_property`
sur `"name"` leve, le repli `str(v)` imprime alors le struct entier et l'on croit
a une API capricieuse. Releve ainsi : 925 variables sur UDS, 584 sur UDW.

### La latitude ecrite dans le vide, et un second defaut de la meme famille (29 septembre 2026)

**TROISIEME OCCURRENCE, ET LA PLUS COUTEUSE** : le pilote ecrivait `Latitude` et
`Longitude` deux fois par seconde depuis des mois, dans un ciel dont
`Simulate Real Sun` valait **FAUX**. UDS trace alors un arc solaire SIMPLIFIE
dont l'elevation de midi vaut `90 - Sun Pitch`, **soixante degres a toutes les
latitudes**. Tout le calage de latitude du monde etait decoratif : pas de
variation saisonniere de la duree du jour, pas de soleil de minuit, meme course
du soleil a l'equateur et au cercle polaire.

**LA MESURE QUI TRANCHE EST L'ECART ENTRE DEUX LATITUDES, jamais une valeur
absolue** -- un arc simplifie rend un chiffre parfaitement plausible :

    equateur, latitude  -0,1 -> elevation 88,9 deg a midi  (attendu 89,9)
    polaire,  latitude  74,8 -> elevation 15,3 deg         (attendu 15,2)
    avant : les deux auraient rendu 60,0

C'est le signe que ce depot connait par coeur -- deux mesures identiques pour
deux reglages differents -- et c'est la sixieme fois qu'il sert.

**ELLE SE LIT SUR LA LUMIERE DIRECTIONNELLE, PAS SUR UNE VARIABLE DU PACK.** Un
nom de variable change d'une version a l'autre, et ce depot a deja recopie un nom
FAUX depuis un message du moteur (`r.Water.WaterMesh.MaxWidthInTiles`, qui
n'existe pas). La rotation d'un composant est ce que la scene recoit vraiment.
**On prend la plus INTENSE des lumieres** : UDS en porte deux, le soleil et la
lune, et prendre la premiere venue rendrait la lune une nuit sur deux.

**LE FUSEAU SUIT LA LONGITUDE, ET IL LA SUIT EN MARCHANT.** La documentation est
explicite. Un monde n'a pas de fuseaux administratifs : on pose le fuseau
SOLAIRE, `longitude / 15`. Le poser une fois au demarrage ne suffirait pas --
ce monde couvre 360 degres de longitude.

**SECOND DEFAUT, MEME FAMILLE : UNE FONCTION ARMEE SUR UN NIVEAU A MOINS
L'INFINI.** `Use UDS Water Level` vaut DEJA vrai sur l'acteur meteo -- c'est son
defaut -- mais `Global Water Level` valait **-100 000 000**. Elle tournait donc
sur une mer inatteignable. Notre ocean est un plan a Z = 0 : ce n'est pas un
reglage mais une CONSTANTE du monde.

**UNE LIGNE DE JOURNAL QUI MENTAIT A ETE RETIREE EN CHEMIN.** Elle relisait
`Simulate Real Sun` par `ReadNumber`, qui ne sait pas lire un booleen, et
annoncait « illisible » pour une variable parfaitement presente -- du bruit qui
ressemble a une information, exactement ce que ce fichier reproche ailleurs.

### La brume : cinq champs continus, et deux suppositions dementies (29 septembre 2026)

Le brouillard n'avait qu'un terme -- la fraicheur -- plafonne a 2,5 sur une
echelle qui va a 10. Il ne pouvait donc jamais etre un brouillard.

**LES PREREGLAGES DU PACK NE DONNENT QUE LES DEUX BORNES** : `Foggy` pose
`Fog = 10`, les douze autres 1 ou 2. La brume est le SEUL terme de cette chaine
qui n'ait aucun releve derriere lui, et ses poids sont ARBITRAIRES -- a juger a
l'image et au bulletin, pas contre une source. Le pack donne quand meme une
SOURCE utile : `Foggy` pose `Wind Intensity = 1`, quand `Overcast` vaut 3 et les
trois etats violents 10. Le pack dit donc lui-meme qu'un brouillard va avec de
l'air calme, et c'est le levier le plus sur des cinq.

**CINQUIEME FORME DE « UN SEUIL N'EST PAS UNE PART ».** Le premier jet employait
le bruit `Haze` BRUT, dont la loi se masse autour d'un demi : le produit de cinq
facteurs tous bornes par un n'atteignait alors JAMAIS le haut de l'echelle.
Mesure -- la brume EPAISSE tombait a 0,0 ou 0,1 pour cent sur les VINGT-DEUX
sites, y compris les plus humides. Le signal passe desormais par `Uniformiser` :
maximum releve 8,3 en taiga, 6,8 sur la calotte.

**DEUX DE MES PROPRES SUPPOSITIONS ONT ETE DEMENTIES PAR LEUR TEMOIN**, et c'est
la partie qui vaut :

1. *« Sans un terme d'humidite cotiere, la brume manquerait le desert cotier »* --
   le cas meme dont je me servais pour refuser une liste de biomes. Le temoin :
   a zero, **aucun test ne tombe**. Le desert de ce monde est couvert **23 % de
   l'annee**, et rend 2,05 de brume sans ce terme contre 2,48 avec. Je l'avais
   suppose sans nuages.
2. *« Notre modele lie la pluie aux nuages, donc l'arc-en-ciel est
   inatteignable »* -- colonne ajoutee au bulletin : **87 episodes par an en
   mediterraneen, 102 en foret tropicale, 97 en savane, 31 en taiga**. Le moment
   existe parce que l'averse est BREVE et que la couverture ne sature pas a
   chaque fois.

**LE VENT EST EXTRAIT EN FONCTION PUBLIQUE** (`VentDepuisSouffle`) : la brume en
a besoin et se calcule AVANT lui -- les nuages dont elle depend sont plus haut,
la poussiere qui depend du vent plus bas, et aucun ordre ne met les trois
d'affilee. Recopier la formule aurait fait diverger deux copies, et la brume
aurait suivi un vent qui n'existe pas.

**LIMITES MESUREES, NON CORRIGEES.** La sonde n'a AUCUN site de cote temperee
oceanique -- le mediterraneen a 45 degres est le plus proche, et il est sec en
ete par definition -- donc elle ne peut pas montrer le cas ou la brume devrait
culminer. Et la foret tropicale ne fait JAMAIS de brume matinale (maximum 2,6)
parce que le terme de fraicheur s'annule au-dessus de 25 degres, alors que le
vrai critere est l'ECART AU POINT DE ROSEE : a humidite relative proche de cent,
un faible refroidissement nocturne suffit.

### L'arc-en-ciel et la chaleur qui tremble : ou les chercher (29 septembre 2026)

Les deux arrivaient eteints, comme le givre et les gouttes avant eux --
QUATRIEME fois que ce depot paye la doctrine du pack, qui desarme tout ce qui
coute. Ni l'un ni l'autre ne demande de pilotage : leur etat se deduit de ce que
nous ecrivons deja.

**LA CHALEUR NE SE CHERCHE PAS DE PRES**, et le chiffre le dit :

    versant proche, oeil a 20 m      0,2 % de pixels changes
    vue d'horizon,  oeil a 140 m    31,0 %

Ce n'est pas une contradiction : `Heat Distortion Start Distance` vaut **70 m**
et le masque d'horizon un exposant de 2,5. C'est un mirage LOINTAIN, et le
chercher a vingt metres revenait a le pousser hors de sa portee -- meme faute
que la fenetre d'eau ramenee a 0,5 km.

**ET ELLE NE SE LEVERA PRESQUE JAMAIS SEULE**, ce qui est un arbitrage et non un
defaut : `Heat Distortion Temperature Range` vaut **85 a 100 degres FAHRENHEIT**
-- 29,4 a 37,8 Celsius -- quand notre desert le plus chaud fait **28,6 de
moyenne annuelle**. D'ou `-WorldseedChaleurForce=`, qui pose
`Manual Heat Distortion` : sans temoin, « chaleur 0,000 » ne se separe pas en
« il ne fait pas assez chaud » et « la chaine est morte ».

**PIEGE DE CAPTURE REFAIT** : la premiere vue a ete prise avec l'oeil a deux
metres, donc DANS le sable. Le depot a la regle depuis septembre -- « toujours
sonder avant de poser » -- et l'image obtenue, une masse blanchatre illisible,
ressemble trait pour trait a un defaut de rendu.

**CE QUI RESTE A BRANCHER** est inventorie a part, lecture de documentation par
lecture de documentation, avec ce qui est MESURE distingue de ce qui ne l'est
pas. Les trois lignes qui pesent le plus : les `Radial Storms`, qui repondent a
la limite que `world_rules.json` documente lui-meme -- le signal meteo ne depend
que du TEMPS, jamais du lieu ; le brouillard volumetrique au sol
(`Render Ground Fog`), qui donnerait la nappe rasante SANS Niagara, sous reserve
que notre `ProceduralMeshComponent` genere des champs de distance ; et le SON,
dont aucune des trois briques du pack n'est employee alors que l'etat meteo
qu'elles consomment est deja ecrit et mesure.

> **LA DERNIERE LIGNE EST FAUSSE, ET LE RELEVE DU MEME JOUR L'A DEMENTIE** --
> voir « Le son : pour une fois, le pack n'arrivait PAS eteint » en fin de
> fichier. **DEUX des trois briques etaient deja employees** : les effets meteo
> et l'occlusion arrivent ARMES, et vingt-six assets sonores etaient joues par
> l'etat meteo qu'on ecrivait deja. Une seule etait absente, le son d'ambiance.
> **Je l'avais deduit de cinq precedents et non mesure** -- cinq fois ou le pack
> livrait une fonctionnalite eteinte. Une habitude n'est pas une mesure.

### Le son : pour une fois, le pack n'arrivait PAS eteint (29 septembre 2026)

Demande du proprietaire : « commencons par utiliser tous les sons du pack ».
L'inventaire publie la veille affirmait qu'« aucune des trois briques sonores
du pack n'est employee ». **C'ETAIT FAUX, et le releve l'a dit avant qu'une
ligne de C++ ne soit ecrite.**

**LE RELEVE, instance ET defaut de classe, sur les 925 variables du ciel et
les 584 de la meteo :**

    Enable Weather Sound Effects                   VRAI
    Use Occlusion to Attenuate Sounds in Interiors VRAI
    Global Sound Asset       UDS_Global_WeatherSounds       assigne
    Directional Sound Asset  UDS_Directional_WeatherSounds  assigne
    Weather Sounds Master Volume / Wind / Rain            1
    Close Thunder / Distant Thunder / Wind Whistling      1
    Environment Sound                              None   <- le seul trou

**C'EST LA PREMIERE FONCTIONNALITE DE CE PACK QUE CE DEPOT TROUVE ALLUMEE**,
apres cinq qui etaient eteintes -- les aurores, les dix surcharges manuelles
d'UDW, le givre, les gouttes d'ecran, l'arc-en-ciel et la chaleur. La pluie, le
vent, le tonnerre et la poussiere s'entendaient donc DEJA, pilotes par l'etat
meteo que le pilote du ciel ecrit depuis le 28 septembre : **vingt-six des
quatre-vingt-dix-neuf assets sonores du pack etaient joues sans qu'une ligne
ait jamais ete ecrite pour eux.**

    REGLE, ET C'EST L'AUTRE SENS DE CELLE QU'ON CONNAISSAIT : on ne suppose pas
    qu'une brique d'un pack tiers est eteinte, on le VERIFIE. Cinq precedents
    dans l'autre sens ne font pas une regle d'inference -- ils font une
    habitude, et une habitude n'est pas une mesure.

#### Ce qui manquait vraiment, et ce que le pack ne peut pas savoir

`Environment Sound` valait None : les vingt-sept chants d'oiseaux, les insectes
de nuit et les deux vents d'arbres dormaient sur le disque. **Et c'est le seul
trou que le pack ne puisse pas combler seul** : son metasound module deja les
oiseaux, les insectes et le vent dans les arbres par l'HEURE, la METEO et le
VENT -- il ne connait pas le BIOME. C'est donc exactement, et seulement, ce que
nous avions a lui apporter.

**TROIS DOSAGES ET NON UNE AMBIANCE PAR BIOME, et c'est une contrainte MESUREE
et non un choix.** Le pack livre une seule source, `Forest_Example`. Ses deux
sous-sources existent bien comme assets separes, mais elles sortent en **MONO**
(`Forest_Birds`) et en **QUAD** (`Forest_TreeWind`), quand la documentation
exige du **5.1** pour qu'une ambiance recoive l'occlusion et le panoramique
directionnel. Aucune des deux n'est jouable seule, et composer une ambiance
NOUVELLE demanderait d'ecrire un graphe MetaSound -- **pas scriptable, meme mur
que Niagara le 29 septembre**. Ce qui reste scriptable est le DATA ASSET, qui ne
porte que trois champs : une source, une echelle de volume, des surcharges de
parametre.

#### LA MESURE QUI TRANCHE EST UN INVENTAIRE DE COMPOSANTS, PAS UN DRAPEAU

Un drapeau relu ne prouve rien -- cinquieme fois. Pour du son, la seule mesure
qui tranche est l'inventaire des composants audio VIVANTS, et on l'enumere sur
le MONDE et non sur les deux acteurs d'UDS : un inventaire tronque se lit
exactement comme un inventaire complet.

Deux lancements apparies, meme graine, meme heure, horloge figee, releve a six
secondes :

| | foret tropicale (19438, -3656) | desert chaud (19375, -8844) |
|---|---|---|
| composants audio | 7 | 6 |
| qui JOUENT | 4 | 3 |
| `Forest_Example` | **present** | **absent** |
| `Environment Sound Time Integer` du pack | 5 | **-1** |

**La difference est BINAIRE, pas un pourcentage.** Et le -1 du desert est une
preuve de plus : c'est le pack lui-meme qui dit que rien ne tourne.

#### Trois defauts de journal corriges en chemin, tous de la meme famille

1. **`ReadNumber` NE SAIT PAS LIRE UN BOOLEEN**, et s'en servir quand meme rend
   faux pour une variable presente : le premier releve annoncait « effets meteo
   ILLISIBLES, occlusion ILLISIBLE » sur deux variables a VRAI. Ce depot avait
   deja RETIRE une ligne pour cette raison exacte -- la relecture de
   `Simulate Real Sun` -- au lieu de la reparer. `ReadBool` existe desormais.
2. **`GetNumber` ne lisait pas les ENTIERS.** UDW expose ses trois etats
   d'ambiance en INT, et le journal les declarait illisibles.
3. **`Count` n'est pas `Aucune`.** Tant que les deux rendaient « aucune », le
   premier passage dans un desert ecrivait « ambiance « aucune » -> « aucune » »
   -- ce qui se lit comme un appel inutile alors que c'est la POSE INITIALE.

#### Et un avertissement qui mentait, dans le script meme qui le denonce

Le controle de format du script de fabrication testait les chaines `"SURROUND"`
et `"5_1"` ; l'enumerateur s'appelle **`FIVE_DOT_ONE`**. Il a donc crie « la
source n'est pas en 5.1 » sur une source qui l'etait. **Un avertissement qui
ment coute plus cher que pas d'avertissement**, et celui-la se trouvait a
quarante lignes d'un commentaire qui l'explique.

#### Ce qui reste, et ce qu'il coute

**DOUZE ASSETS SUR QUATRE-VINGT-DIX-NEUF NE SONT TOUJOURS PAS JOUES** : les six
compressions de neige, le deplacement dans la neige, les quatre flaques et le
mouvement d'eau. Ils appartiennent a `UDS_DLWE_Interaction_Sounds`, qui se
declenche quand le joueur POSE LE PIED -- il faut un composant
`DLWE_Interaction` parente a chaque pied du squelette, et il n'en existe aucun.

**ET LA CONDITION PREALABLE N'EST PAS LE COMPOSANT, C'EST LE SOL.** La
documentation est explicite : hors Landscape, il faut declarer les MATERIAUX
PHYSIQUES du sol dans « Physical Materials which Enable DLWE Interactions on
Non-Landscapes ». Or notre terrain est un `ProceduralMeshComponent`, et le
releve du 29 septembre 2026 est sans appel -- **zero occurrence de
`PhysicalMaterial` dans tout `Source/Worldseed/Procedural/`**. Il faut donc
d'abord donner un materiau physique au terrain voxel, ce qui est un chantier a
part entiere et non un detail de cablage.

> ⚠ **CORRIGE LE MEME JOUR, PLUS TARD : LE SOL EST FAIT, ET LA PROPRIETE
> N'EST PAS OU CE PARAGRAPHE LA LAISSE CHERCHER.**
>
> **1. Le verrou du sol est leve.** `PM_WorldseedTerre` existe et le terrain le
> porte -- pose dans le SLOT `PhysMaterial` des deux materiaux parents, ce qui
> ne coute aucune ligne sur le chemin chaud du streaming : nos chunks portent
> `bUseComplexAsSimpleCollision`, donc le physmat vient du materiau
> (`BodyInstance.cpp:3323`). Mesure par TRACE, meme point, meme monde :
> `DefaultPhysicalMaterial` x49 avant, `PM_WorldseedTerre` x49 apres. Le releve
> « zero occurrence » ci-dessus etait d'ailleurs trop etroit -- c'etait zero
> dans TOUT `Source/`, et zero asset dans `Content/`.
>
> **2. La propriete n'est sur AUCUN des deux acteurs.** Enumeration des 925
> variables du ciel et des 584 de la meteo, motif large
> (`dlwe|physical|interaction|footprint|footstep`) : elle n'y est pas. Les deux
> seuls `TArray<PhysicalMaterial>` que les acteurs portent sont des listes qui
> **DESACTIVENT** (`Physical Materials which disable Snow/Dust Sounds and
> Particles`, et son equivalent pour les flaques).
>
> **Elle vit sur `UDS_DLWE_Interaction_Settings`**, que chaque composant
> `DLWE_Interaction` tient PAR INSTANCE (variable `Interaction Settings`). Donc
> rien du pack n'a besoin d'etre modifie : on duplique les reglages par
> composant. Et sur l'instance livree `Standard_DLWE_Interaction_Settings`, le
> tableau vaut `[]` tandis que `Enable Snow Sound Effects` et `Enable Puddle
> Sound Effects` valent deja VRAI -- pas d'interrupteur maitre a trouver ici.
>
> **LA LECON, ET ELLE N'ETAIT PAS ECRITE :** ce paragraphe citait un nom de
> propriete tire de la DOCUMENTATION et le presentait comme un emplacement. Un
> nom lu n'est pas un objet trouve -- on enumere l'objet avant d'ecrire dessus,
> sinon l'ecriture par reflexion echoue en SILENCE. Septieme supposition que ce
> chantier paie a ses propres notes.

### Les douze derniers assets jouent : 99 sur 99 (29 septembre 2026)

**LE PROPRIETAIRE A MARCHE DANS LA NEIGE ET A RAPPORTE : les empreintes, le son
des pas, le givre d'ecran, la tempete.** C'est la preuve, et c'est une oreille
et un oeil -- aucune ligne de journal ne pouvait la donner.

**LA CHAINE, DE BOUT EN BOUT :**

    PM_WorldseedTerre  ->  slot PhysMaterial des deux materiaux parents
                       ->  liste blanche de reglages DUPLIQUES, par composant
                       ->  2 DLWE_Interaction aux sockets foot_*_Socket
                       ->  empreintes vues, son entendu

Trois points valent d'etre retenus. Le physmat vient du SLOT DU MATERIAU, donc
zero ligne sur le chemin chaud du streaming, parce que nos chunks portent
`bUseComplexAsSimpleCollision`. La liste blanche vit sur
`UDS_DLWE_Interaction_Settings`, que chaque composant tient PAR INSTANCE -- on
duplique, donc rien du pack n'est touche. Et l'attachement se fait depuis le
PILOTE DE CIEL, arbitrage du proprietaire : c'est le seul endroit qui connaisse
UDS par construction, et il est versionne, alors que le Blueprint du pion est
hors du depot.

#### ⚠ LE PIEGE PAYE, ET IL ETAIT DEJA ECRIT DANS LE GUIDE

**J'AI SOUPCONNE LE PACK PENDANT DEUX LANCEMENTS A CAUSE DE MON PROPRE BANC.**
L'entonnoir rendait `Asleep = VRAI` sur les deux composants, dans les deux etats
d'un A/B -- et j'ai designe la logique de veille d'UDS comme suspecte. C'etait
faux :

| | au banc, neige 10 | au banc, temoin | en JEU, neige 10 |
|---|---|---|---|
| `Snow Depth` | 14,000 | 0,000 | 14,000 |
| `Active Distance` | 4000 | 4000 | 4000 |
| `Asleep` | **VRAI** | **VRAI** | **faux** |

**`-WorldseedBanc` DEPLACE LE PION, IL NE LE FAIT PAS MARCHER**, et c'est ce
deplacement qui endormait les composants. La regle existait : *un temoin doit
poser un etat COHERENT, que le monde puisse produire, sinon on debogue une
chimere.* Je l'avais lue et j'ai quand meme choisi le banc, parce qu'il marche
tout seul et que c'etait commode. **Le confort d'un temoin n'est pas un critere
de validite.**

#### ET UN INSTRUMENT QUI MENTAIT, ECRIT LE JOUR MEME

La premiere version de l'entonnoir imprimait « 2 SONNENT », sur la foi de
`UAudioComponent::IsPlaying()`. **Le temoin l'a dementi : vrai avec ZERO
neige.** C'est un MetaSound PERSISTANT, que le pack instancie et laisse tourner
en gatant a l'interieur du graphe -- un composant audio qui « joue » ne prouve
donc rien. La colonne est gardee, parce qu'une ABSENCE de source serait
concluante, mais elle s'appelle desormais « sources (existence, PAS
audibilite) ». Deuxieme fois que ce chantier paie du bruit lu comme une
information sur un releve sonore.

**CE QUE L'ENTONNOIR VAUT DESORMAIS.** Il ne prouve pas l'audibilite, mais il a
ete CALIBRE une fois contre une oreille : un entonnoir vert -- deux composants
non endormis, armes, voyant de la neige -- correspondait a un son audible. Il
tient donc lieu de preuve indirecte pour la suite, et c'est le seul controle
rejouable que ce chantier laisse.

#### Ce qui n'a PAS ete touche, et pourquoi

**LES CINQ CURSEURS DE VOLUME PAR FAMILLE.** La documentation dit que le volume
resultant s'echelonne avec ces reglages **ET** avec l'etat meteo courant : ils
sont donc deja modules par ce que nous ecrivons, et y toucher doublerait la
modulation. Les poser tous a 1,0 dans le fichier de regles ajouterait cinq
boutons INERTES -- ce que le proprietaire a demande d'arreter de garder le
23 septembre. Seul le MAITRE est expose, parce que c'est le seul qui permette
de faire de la place a autre chose sans deregler l'equilibre interne du pack.

**TROIS DES SIX N'ONT AUCUN `OnRep_`** -- tonnerre proche, tonnerre lointain,
sifflement -- d'ou l'appel a `Apply Sound Effects Volume Levels`, que le pack
expose pour cela.

#### Le pont sait desormais appeler une fonction A PARAMETRES

`Change Environment Sound` en prend trois. `CallFunction` refusait tout ce qui
en prend un, et son commentaire disait pourquoi : une pile mal formee
corromprait la memoire. `ChangerAmbiance` construit donc la pile par les
PROPRIETES de la fonction, apparie par **TYPE** et non par nom -- les noms
Blueprint portent des espaces que la reflexion assainit d'une version a l'autre
-- **COMPTE** les parametres et refuse si la signature n'en porte pas exactement
trois, un de chaque. Un pack qui en ajouterait un quatrieme nous trouve muets
plutot que dangereux.

#### Mesure de non-regression

150 oracles verts (145 avant), dont cinq neufs avec **trois temoins montes,
verifies, retires**. Le monde n'a pas bouge d'un octet malgre le changement
d'empreinte des regles : le CORPS du cache rend le meme MD5
`ce255f6745ca1d2ee5cdc80a29e10991` sur trois entrees de cles differentes, dont
une anterieure au chantier.

### Tempetes radiales : premiere lueur, et elle a ete VUE (29 septembre 2026)

**LE PROPRIETAIRE L'A VUE**, en vol a 2000 m, a dix kilometres d'elle. La
reserve que l'inventaire portait depuis le debut -- « les effets visibles de
l'exterieur exigent le mode Volumetric Clouds » -- est donc levee, et c'etait
la seule question qu'aucune mesure ne pouvait trancher.

**Ce que ca rapporte** : le LIEU. Le fichier de regles documente lui-meme la
limite que ces tempetes comblent -- *« le signal d'agitation ne depend que du
TEMPS, jamais du lieu -- le vent est donc le meme partout dans le monde au meme
instant »*.

#### L'API, RELEVEE ET NON LUE

Enumeration sur `Ultra_Dynamic_Weather` : 584 variables, 408 fonctions.

    Enable Radial Storm Spawning      bool  + OnRep_      livre a FAUX
    Spawn / Start Up / Load Class     ZERO parametre      le pont sait deja
    Radial Storm Probabilities x4     TMap, 1 entree chacune -- PEUPLEES
    Start Distance 25, Outer Radius 13, End Distance 25    EN KILOMETRES
    Lifetime 500-700 s, Wait Interval 1000-2000 s

Les cinq fonctions ne prenant aucun parametre, **aucune primitive n'etait a
ecrire** -- et les tables etant peuplees, aucune ecriture de `TMap` non plus.
Le chantier s'est donc revele bien plus petit que prevu.

#### LE PIEGE PAYE : LE SOFTCLASS

Le premier essai appelait `Spawn Radial Storm` dans la MEME trame que
l'armement. Les quatre appels rendaient vrai, l'interrupteur se relisait VRAI,
et **ZERO acteur** naissait sur soixante secondes. `Radial Storm Class` est un
SOFTCLASS, double d'un `Radial Storm Class Hard`, et le pack livre
`Load Radial Storm Class` pour resoudre l'un dans l'autre. **Un spawn sur une
classe nulle ne fait rien, et ne le dit pas.** Chargement a l'armement, spawn au
releve suivant. Regle en section B3.

#### DEUX PIEGES EVITES PAR LA MESURE

**L'attente livree est de 1000 a 2000 SECONDES**, premier delai a moitie : huit
a dix-sept minutes avant la premiere tempete. Un lancement de mesure n'aurait
rien vu, et l'on aurait conclu a l'echec en ayant seulement mesure trop tot.
D'ou l'appel direct au spawn.

**`-WorldseedCielClair` COUPE LES NUAGES VOLUMETRIQUES.** Le harnais d'A/B du
depot -- celui qui rend deux lancements comparables -- detruit donc le sujet.
Cette mesure ne peut pas etre appariee, et il faut le DIRE. Regle en section B2 :
verifier ce que le temoin eteint avant de s'y fier.

#### UN INSTRUMENT CORRIGE, ET UNE UNITE DECOUVERTE

La premiere colonne de distance mesurait depuis `GetOwner()`, l'acteur de climat,
qui se tient a l'origine du monde : elle annoncait **56 km**, hors de la carte,
pour une tempete qui nait AUTOUR DU JOUEUR. Le chiffre n'etait pas faux, il ne
mesurait pas la bonne chose. Corrige sur le pion, il rend **25,0 km** -- soit
exactement `Radial Storm Start Distance`, ce qui etablit que **l'unite est le
kilometre**, ce que rien ne disait. Elle se rapproche de 0,8 km par dix
secondes, donc environ cinq minutes avant l'arrivee.

Le cap est journalise avec la distance : une mesure qui ne permet pas d'aller
VOIR son sujet ne sert qu'a moitie.

#### LA TOURNEE PHOTO NE CONVIENT PAS A CE SUJET

Ses huit vues visent des cibles a 15 a 800 m -- arches, canyons, tables -- et
**aucune ne regarde l'horizon** ; elle teleporte en outre la camera a travers le
monde, ce qui prive la distance a la tempete de tout sens. D'ou
`-WorldseedOrageFace=<km>`, qui met le joueur EN VOL a distance choisie, cap sur
elle.

**POURQUOI EN VOL, ET C'EST UN CHOIX DU PROPRIETAIRE.** Il resout deux problemes
d'un coup : 71 % de ce monde est ocean, donc viser une coordonnee a vingt
kilometres serait jouer a pile ou face avec la mer -- et l'altitude degage
l'horizon, qui est le sujet. Une requete « est-ce de la terre ferme ? » avait
commence a etre ecrite pour le cas pose au sol ; elle a ete RETIREE, le vol la
rendant inutile. On ne garde pas ce que rien ne lit.

#### CE QUI RESTE, ET C'EST UNE QUESTION D'ECHELLE

Le pack pose des metriques **FIGEES** : naissance a 25 km, rayon 13 km, mort a
25 km. Sur 64 x 32 km, une tempete de 13 km de large couvre **40 % de la hauteur
du monde**, et un trajet de 50 km en sort. La regle d'echelle de ce projet veut
qu'un reglage s'exprime en FRACTION du monde, jamais en metrique figee -- faute
de quoi la tempete risque de reproduire le defaut qu'elle devait corriger, une
meteo quasi uniforme avec seulement un bord qui se deplace.

Et les quatre tables de probabilite sont un **tirage au sort saisonnier**, soit
exactement le mecanisme ECARTE pour l'etat meteo global : *« nous posons des
curseurs continus cales sur le climat de Koppen de la cellule »*. Arbitrage
ouvert.

#### ET L'ARC-EN-CIEL EST VALIDE A L'OEIL, LE MEME JOUR

Le proprietaire l'a confirme dans la meme seance. **Cette ligne n'avait jusque-la
qu'une preuve indirecte** : 87 a 102 episodes par an (un compte du modele, pas
une observation) et `Current Rainbow Strength` relu a 0,500. Or *« armer n'est
pas afficher »* est un piege paye QUATRE fois dans ce depot -- le givre s'est vu
tout de suite, les gouttes non. Une confirmation a l'oeil change donc la nature
de la preuve, et pas seulement son nombre.

**CE QUE CELA DIT DE LA METHODE.** Trois lignes ont ete validees a l'oeil ou a
l'oreille aujourd'hui -- les sons de pas, les empreintes, la tempete radiale,
plus l'arc-en-ciel -- et AUCUNE ne l'aurait ete par un oracle. Le depot n'a pas
de harnais pour l'audibilite ni pour « est-ce que ca se voit » ; le releve de
l'entonnoir et les comptes d'acteurs sont des preuves INDIRECTES, utiles parce
que calibrees une fois contre un sens humain. C'est une limite a assumer, pas a
masquer.

#### LE RECADRAGE EN FRACTIONS, VALIDE A L'OEIL

Le proprietaire a tranche : *« elle remplissait le ciel »*, puis apres
recadrage *« ca le fait bien »*.

    reglage      fraction de la HAUTEUR    ->  km      relu    avant (pack)
    rayon              0,125                   4,0     4,0         13
    distance           0,500                  16,0    16,0         25
    dispersion         0,125                   4,0     4,0          0

    28 degres sous-tendus a la naissance, 37 a douze kilometres

**LA REFERENCE EST LA HAUTEUR, PAS LA LARGEUR** : c'est la petite dimension,
donc celle qui contraint, et celle sur laquelle la latitude va d'un pole a
l'autre. Un reglage exprime sur la largeur passerait deux fois trop grand du
nord au sud. Le diametre vaut donc un quart de la hauteur du monde, et la
traversee exactement UNE hauteur -- des quantites qu'on peut dire.

**LA TAILLE DU MONDE SE LIT SUR LE TERRAIN CHARGE**, et la mesure le justifie :
il rend 32 km quand `UWorldseedRules::Geometry` porte un defaut de 8. Prendre le
defaut aurait donne des fractions justes d'un monde qui n'existe pas.

**ET RIEN N'EST ENTRE DANS `world_rules.json`, A DESSEIN.** Son empreinte est un
MD5 du fichier entier : une virgule y aurait invalide tous les mondes en cache
et impose 210 a 260 s de regeneration, pour un reglage cosmetique. Les fractions
vivent en C++ avec trois surcharges qui prennent des FRACTIONS et jamais des
kilometres -- sans quoi on reintroduirait ce qu'on vient de retirer.

**CE QUI RESTE OUVERT.** La tempete n'est armee que par
`-WorldseedOrageRadial`, donc eteinte en partie normale ; et les quatre tables
de probabilite restent un tirage au sort saisonnier, qui ignore le climat de
Koppen de la cellule.

**OBSERVE, NON RESOLU : la vitesse suit la distance.** Elle se rapprochait de
0,8 km/10 s avant recadrage, 0,5 apres -- le pack derive donc vraisemblablement
la vitesse de la distance et de la duree de vie. La traversee de 32 km en 500 a
700 s donne 165 a 230 km/h, rapide pour un orage reel. Mais ce monde est un
MODELE COMPRIME : 32 km d'un pole a l'autre contre 20 000 reels, soit un facteur
625, quand la journee de 30 minutes comprime le temps d'un facteur 48 seulement.
Comparer au reel exige donc de decider QUEL FACTEUR D'ECHELLE S'APPLIQUE AU
TEMPS, et cet arbitrage depasse les tempetes.

### La carte du ciel reelle, validee a l'oeil (29 septembre 2026)

**LE PROPRIETAIRE A VALIDE LE CIEL DE NUIT.** Le pack repetait la meme texture
d'etoiles **2,5 fois** sur la voute -- `Stars Tiling` -- et `Simulate Real Stars`
lui substitue une carte a 360 degres orientee par la LATITUDE et la DATE.

**LE PREREQUIS N'ETAIT ACQUIS QUE DEPUIS LE MEME JOUR**, et c'est ce qui rendait
la ligne mure : un ciel reel ne veut rien dire sans latitude, or `Simulate Real
Sun` arrivait a FAUX et la latitude qu'on ecrivait n'etait pas evaluee. D'ou
l'ordre d'armement -- **les etoiles APRES le soleil**.

Rien a fabriquer, releve et non suppose : `Real Stars Texture` pointe
`Real_Stars`, et `Real Stars Sprites Starmap` un catalogue `Starmap500k`.

**`Static Properties - <categorie>` A SUFFI UNE TROISIEME FOIS**, ce qui en fait
un patron et non une coincidence. Aucun `OnRep_Simulate Real Stars` n'existe --
verifie sur les 568 fonctions du ciel -- mais bien `Static Properties - Stars`.
Le releve :

    avant faux -> ecrit oui, OnRep_ ABSENT (attendu),
                  Static Properties - Stars appelee, relu VRAI
                  repetition 2.50, sprites NODE_AddNiagaraComponent-3

Le composant Niagara des sprites passe de NUL a un objet : un effet, pas un
drapeau -- mais faible, et journalise sans en faire une preuve. **La justesse du
ciel ne se lisait pas** : elle s'est regardee.

**LA PORTE EST DANS LES REGLES SANS Y ETRE ECRITE**, et c'est un patron a
reprendre : `Rules.Num` prend un defaut, donc `uds.simulerLesEtoiles` absent vaut
ARME et apparait au releve des cles manquantes. Aucune empreinte de
`world_rules.json` touchee, donc aucun monde en cache invalide -- et le reglage
existe le jour ou l'on acceptera une regeneration pour l'y ecrire.

#### ⚠ ET LE LANCEMENT DE NUIT A REVELE UN MENSONGE DANS UN RELEVE PREEXISTANT

`-WorldseedHeure=1` rendait « soleil : elevation 54,7 deg a 1,0 h ». Impossible
pour un soleil -- et c'est ce qui a fait tirer le fil.

`ElevationDuSoleil` prend la lumiere directionnelle **la plus INTENSE**, sur le
motif que « la lune est reglee bien plus bas ». **C'est vrai de jour et faux la
nuit** : le soleil est alors eteint, la lune gagne, et le releve annoncait son
elevation en l'appelant « soleil ». Le calcul etait juste ; c'est le NOM qui
mentait, et 54,7 degres est une elevation de lune parfaitement plausible.

    de nuit  Moon a 54.7 deg a 1.0 h  (hors de midi : l'attendu ne s'applique pas)
    a midi   Sun  a 62.7 deg a 12.0 h -- attendu 60.4 au midi d'equinoxe

**On ne choisit PAS par nom de composant** -- un pack renomme -- on garde le
critere d'intensite, qui decrit ce que la scene RECOIT vraiment, et l'on REND le
nom. La comparaison d'equinoxe ne s'imprime plus qu'autour de midi.

**LA REGLE EN SECTION B1** : un releve cale sur un etat du monde ment dans
l'autre, et son domaine de validite doit etre DIT. Le cas temoin etait juste ;
c'est son domaine qui n'etait pas ecrit. Troisieme releve de ce depot corrige le
meme jour pour avoir dit plus que ce qu'il mesurait.
