# Registre Worldseed — Unreal et ses plugins

> **CE FICHIER NE SE LIT PAS EN ENTIER, IL SE CHERCHE.** Il porte le recit des
> seances : ce qui a ete mesure, ce qui a tranche, et surtout **les pistes
> abandonnees**, pour qu'on ne les retente pas. Les REGLES qu'on en a tirees
> vivent dans `CLAUDE.md`, chargé a chaque tour ; ici se trouve la PREUVE.
>
> ```
> grep -n -i '<le mot du sujet>' Docs/registre/*.md
> grep -A40 'le titre exact' Docs/registre/unreal-pieges.md
> ```
>
> **Le contenu est CUMULATIF.** Certaines notes decrivent un etat qui n'existe
> plus ; les plus dangereuses portent une marque de peremption. Avant d'agir sur
> une note, verifier que ce qu'elle nomme existe encore — une note qui survit a
> ce qu'elle decrit coute plus cher que pas de note.
>
> **Ce fichier** : les pieges du MOTEUR et de ses plugins — Landscape, World Partition, PCG, Water, sauvegarde d'acteurs, API Python d'editeur. Une grande part decrit l'ere du Landscape, qui n'existe plus : lire la section A ZERO de `CLAUDE.md` avant d'agir dessus.

## 11. Pieges rencontres (projet Worldseed, UE 5.8)

- **Landscape : viser ~32 composants par cote, pas 64.** A nombre de sommets egal, des
  composants deux fois plus larges (sections 2x2) divisent par quatre le nombre de
  composants. Mesure : 8129x8129 en 1024 composants = **81 s** de creation ; 4033x4033 en
  4096 composants = **549 s** et 16 Go de RAM. Le cout suit le nombre de composants, pas
  la resolution.
- **Calage altimetrique.** La valeur 32768 de la heightmap tombe sur Z=0 *dans le repere de
  l'acteur*. Pour que l'altitude 0 (le niveau de la mer) tombe sur Z=0 dans le monde,
  l'acteur doit etre **remonte** de la mi-plage d'altitude, pas descendu.
- **`save_current_level()` ne sauve pas les acteurs World Partition** (retourne True en 0.1 s
  en n'ecrivant que le `.umap`). Utiliser
  `unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)`. Les acteurs WP sont
  ecrits sous `Content/__ExternalActors__/<chemin de la carte>`, PAS sous le dossier de la carte.
- **A la reouverture d'un niveau WP, aucun proxy n'est charge** : `get_height_in_region` renvoie
  alors l'altitude minimale partout, sans erreur. Charger d'abord :
  `WorldPartitionBlueprintLibrary.get_actor_descs()` -> filtrer -> `load_actors(guids)`
  (16 proxies d'un Landscape 8129 : 2,4 s).
- **LayerInfo a nom de couche avec espaces : increable depuis Python.**
  `create_layer_info_object` derive le nom d'ASSET du nom de COUCHE (`LI_<nom>`), et un espace
  donne un chemin de paquet invalide ("too many spaces") ; `LayerName` est VisibleAnywhere donc
  en lecture seule, impossible de creer sous un nom assaini puis de renommer. Solution :
  chercher un LayerInfo existant **par son LayerName** et le reutiliser.
- **`execute_python_code` remonte `PYTHON_EXECUTION_TIMEOUT` au bout de 30 s alors que le script
  continue et aboutit.** Ne pas relancer l'appel : lire la preuve dans `Saved/Logs/Worldseed.log`
  (d'ou l'interet de journaliser avec `unreal.log`). Idem pour l'abandon client a 300 s.
- **Un Landscape ne doit pas depasser ~256 composants s'il porte dix couches.** C'est LE
  point dur trouve ce jour-la. Au-dela, l'import des poids fait tomber le thread RHI :
  `D3D12Util.cpp:1062`, variantes "pSet->Open() failed" et "ExecuteCommandLists ... E_FAIL".
  Le gestionnaire de residence D3D12 plafonne a `MAX_NUM_CONCURRENT_CMD_LISTS = 1024`
  (constante de compilation de `d3dx12residency.h` : seul un moteur recompile depuis les
  sources peut la lever). Mesures : 1024 composants x 10 couches -> crash systematique, que
  les couches soient ajoutees une par une (mort a la 9e, celle qui ouvre une 3e texture de
  poids par composant) ou toutes importees d'un coup a la construction ; 256 composants x
  10 couches -> import en ~35 s, aucun incident. **Donc on tuile** : `tile_world.py` decoupe
  une sortie 8129 en 2x2 tuiles de 4065 (16x16 = 256 composants chacune) qui partagent leur
  arete, donc sans couture (verifie : ecart 0,0000 cm sur les quatre coutures).
  Ce qui NE suffit pas, teste : borner `r.D3D12.Submission.MaxExecuteBatchSize.*` (defaut UE
  INT_MAX) et `landscape.BatchedMerge.MaxResolutionPerRenderBatch` — ces deux bornes, posees
  dans `Config/DefaultEngine.ini [ConsoleVariables]`, font passer de 3 a 8 couches sur 1024
  composants, pas plus. On les garde, elles ne coutent rien.
- **`create_landscape_from_files` (VibeUE) importe relief + toutes les couches en une passe.**
  A utiliser pour tout import de masse : une seule fusion d'edit layers au lieu d'une par
  couche. Ne dispense pas de tuiler (voir ci-dessus).
- **Lire une region 1x1 pile sur le sommet de BORD d'un Landscape renvoie une valeur fausse.**
  `get_height_in_region(l, 4064, 2000, 1, 1)` a renvoye 107 cm a cote, la meme donnee lue dans
  une bande de 5 ou 9 sommets etant exacte. Pour controler une couture, lire une BANDE et
  prendre son dernier element, jamais un sommet isole.
- **Ne PAS poser `D3D12.ResidencyManagement=0`** pour contourner ce crash : l'editeur part en
  `DXGI_ERROR_DEVICE_HUNG` 4 s apres l'init du RHI.
- **En 5.8 les edit layers sont obligatoires** : un Landscape sans edit layer est converti
  automatiquement a l'enregistrement (`RegisterLandscapeActorWithProxyInternal`). Impossible
  d'eviter la passe de fusion GPU pour un import en masse.

### PCG et carte des biomes (session du 9 septembre 2026)

- **`EPCGTextureFilter` vaut `Bilinear` par defaut sur TOUS les noeuds PCG** (`PCGCommon.h`,
  `PCGTextureSampler.h`, `PCGPinPropertiesGPU.h`...). C'est le filtre de PCG, **distinct** de
  `TextureFilter` pose sur l'asset : mettre `TF_NEAREST` sur la texture ne l'empeche pas. Sur une
  carte d'IDENTIFIANTS comme `biome_index.png`, il interpole les identifiants et rend des biomes
  qui n'existent pas la, sans la moindre erreur. Mesure sur le banc de 2 km : grille de semis a
  1500 cm sur une carte a 393,7 cm/texel, **307 points sur 17956 faux (1,71 %)**, du type
  `plage` lu comme `alpin` ou `roche_nue` lu comme `foret_tropicale_humide`. En `Point` : 0 faux.
  Le piege ne se voit PAS si la grille tombe pile sur les centres de texels (le bilineaire y
  degenere en exact) : tester avec un pas qui n'est ni multiple ni diviseur du texel.
- **La surface d'une `PCGTextureData` est la boite LOCALE [-1,1] transformee**
  (`PCGTextureData.cpp`). Donc l'echelle du `Transform` vaut la **demi-portee**, et le centre est
  a `origine + (n-1)/2 * texel`, pas `origine + n/2 * texel` : se tromper decale tout
  l'echantillonnage d'un demi-texel (197 cm ici).
- **`keep_zero_density_points` doit etre a True** quand un identifiant vaut 0 : sinon les points
  de l'ocean (id 0, donc densite 0) sont filtres avant d'etre lus.
- **PCG force lui-meme sRGB off + `TMGS_NoMipmaps` + `TC_VectorDisplacementmap`** sur les textures
  qu'il echantillonne (`PCGTextureData.cpp`) : sans ces reglages il **duplique la texture en
  memoire** pour se les donner. Les poser a l'import evite la copie (66 Mo par tuile 4065).
- **Ne jamais `delete_asset` un graphe PCG encore reference par un `PCGComponent` du niveau** :
  ensure dans `ObjectTools.cpp:4045`, qui ouvre un **modal**. L'editeur se fige, un
  `CrashReportClientEditor` apparait, `gameThreadStallSeconds` s'envole et tout appel MCP pend
  jusqu'au timeout client. Reutiliser l'asset et le vider avec `PCGGraph.remove_nodes(...)`.
- **`Texture2D.blueprint_get_cpu_copy()` est un cul-de-sac pour verifier un import** : il exige
  `availability = CPU`, ce qui n'envoie au GPU qu'un placeholder noir. Utiliser
  `Texture.export_to_disk(path, ImageWriteOptions)` et comparer le PNG hors editeur (verification
  exhaustive, 262144 texels, et non par sondage).
- **Lire les points generes par PCG depuis Python** : `PCGDataPtrWrapper` n'expose rien ; passer
  par `export_text()` pour recuperer le chemin d'objet, puis `unreal.load_object`, puis
  `get_transform_values_from_range` / `get_density_values_from_range` avec un
  `PCGPointInputRange`.
- **`PCGBiomeCore` / `PCGBiomeSample` sont livres binaires avec l'engine 5.8** (Experimental,
  v0.2, `EnabledByDefault: false`) : les activer dans le `.uproject` suffit, aucun rebuild.
  Quasi tout est du contenu Blueprint/PCG (un seul `.h`, module vide) : aucune API C++, aucune
  garantie de compatibilite entre versions moteur.


### Semis de vegetation PCG a l'echelle du monde (9 septembre 2026)

- **La densite survit intacte a `PCGProjection` sur le Landscape.** Mesure : apres projection,
  les proportions d'identifiants reproduisent la composition de la fenetre au dixieme de point
  (desert 39,7 % contre 39,8 % attendus). On peut donc echantillonner et projeter UNE FOIS par
  pas de grille, puis trier par biome, au lieu de projeter une fois par couche : une trentaine
  de projections de terrain economisees.
- **Le bleu vif des rochers de desert, palmiers, bambous et falaises est un defaut de RVT.**
  Ces packs passent par `M_Assets_MasterMat`, dont le switch STATIQUE `UseRVT` echantillonne une
  Runtime Virtual Texture absente du niveau. Comme c'est un switch statique, aucune chirurgie de
  graphe : dupliquer l'instance de materiau, switch a false, et la poser dans
  `override_materials` du descripteur ISM du semeur. 15 materiaux concernes sur 139 maillages.
  Ne PAS chercher a creer la RVT : deja tente et abandonne (texture vide, terrain aplati).
- **`is_component_partitioned = True` est le reglage de la CUISSON EN EDITEUR, pas de
  l'execution.** Paye comptant : pose sur les 4 tuiles, il a cree **15 876 `PCGPartitionActor`
  persistants, 847 Mo sous `Content/__ExternalActors__`, pour zero instance produite**. Le
  planificateur d'execution (`PCGRuntimeGenScheduler.cpp`) fabrique ses propres acteurs de
  partition `RF_Transient`, depuis un pool, et ne passe pas par ce reglage. Nettoyage :
  `destroy_actors` par lots de 500 puis `save_dirty_packages` (comptez plusieurs minutes, le
  thread de jeu reste occupe ; ce n'est pas un blocage).
- **La generation a l'execution ne se laisse pas configurer en trois essais.** Mesures :
  composant partitionne -> 15 876 acteurs et 0 instance ; non partitionne + `GenerateAtRuntime`
  -> la tuile ENTIERE se genere (1,5 et 1,9 million d'instances, l'editeur monte a 15 Go) ;
  non partitionne + rayon borne a 500 m -> 0 instance. Le levier n'a pas ete trouve ; reprendre
  avec `pcg.RuntimeGeneration.EnableDebugging 1` et le `PCGWorldActor`
  (`treat_editor_viewport_as_generation_source`) plutot qu'en aveugle.
- **Ordre des operations pour couper un semis qui se regenere** : poser d'abord
  `generation_trigger = GenerateOnDemand`, `regenerate_in_editor = False` et `activated = False`,
  SEULEMENT ENSUITE `cleanup(True)`. Un `cleanup` seul est aussitot defait.
- **Cout du monde complet** : environ 12,9 millions d'instances pour les 16 biomes terrestres aux
  pas de 900 / 1400 / 3000 / 6000 cm, soit 292 fois le banc de 2 km (44 117 instances). Cuire
  n'est pas envisageable.


### Generation PCG a l'execution : la configuration qui marche (9 septembre 2026)

Reglee, verifiee en PIE, 1073 instances sur 1073 dans un biome autorise par leur recette.
Quatre points, tous payes comptant :

- **L'ORDRE : declencheur d'abord, partitionnement ensuite.** Le descripteur de grille est bati
  avec `SetIsRuntime(IsManagedByRuntimeGenSystem())` (`PCGComponent.cpp:202`), et
  `IsManagedByRuntimeGenSystem()` vaut exactement `GenerationTrigger == GenerateAtRuntime`
  (`PCGComponent.h:526`). Partitionner AVANT de poser le declencheur donne un descripteur non
  runtime, donc des `PCGPartitionActor` PERSISTANTS ecrits sous `Content/__ExternalActors__` :
  15 876 acteurs et 847 Mo pour zero instance. Dans le bon ordre, le planificateur pioche dans
  un pool d'acteurs `RF_Transient` et rien ne touche le disque.
- **Il FAUT borner le semis a la maille.** Un `PCGTextureSampler` a transform absolu couvre toute
  la tuile : sans bornage, CHAQUE maille de 256 m refait les 16 km. Mesure : des instances
  etalees sur 8000 m dans une cellule de 256, 560 000 instances pour trois cellules. Le remede
  est `PCGCullPointsOutsideActorBounds` juste apres `ConvertToPointData` ; apres correction,
  chaque cellule tient dans ses 252 m.
- **NE PAS remplacer `ConvertToPointData` par un `PCGSurfaceSampler`**, malgre son entree
  `Bounding Shape` qui semble faite pour ca : il remet la densite du point a `1.0f` puis la
  multiplie par celle de la forme bornante (`PCGSurfaceSampler.cpp:322`). La densite de la
  SURFACE - donc l'identifiant de biome - n'y survit pas. Mesure : 124 maillages au lieu de 77,
  chaque espece semee dans tous les biomes.
- **Le cache de paysage doit etre serialise.** `PCGWorldActor.landscape_cache_object.serialization_mode`
  vaut `NeverSerialize` par defaut ; la projection sur le Landscape echoue alors en generation a
  l'execution. Le passer a `AlwaysSerialize` (768 entrees ici).
- **NE JAMAIS reappliquer une propriete sur un composant deja en `GenerateAtRuntime`.**
  `UPCGComponent::OnRefresh` commence par `check(!IsManagedByRuntimeGenSystem())`
  (`PCGComponent.cpp:2968`), et `set_editor_property` declenche un PostEditChangeProperty MEME
  quand la valeur ne change pas. Le refresh mis en file fait tomber l'editeur au tick SUIVANT,
  donc pas dans l'appel fautif : editeur perdu en pleine sauvegarde. Pour modifier une tuile,
  repasser d'abord en `GenerateOnDemand` (voir `vegetation.set_runtime_enabled`).
- **Outils** : `pcg.RuntimeGeneration.EnableDebugging 1` fait tracer `[RUNTIMEGEN] UNPOOL /
  GENERATE / CLEANUP` par maille, avec la priorite de distance. C'est ce qui a permis de voir
  que le planificateur tournait bien et que le probleme etait ailleurs. La source de generation
  en editeur est `PCGWorldActor.treat_editor_viewport_as_generation_source` ; en PIE, le pion
  suffit via `enable_world_partition_generation_sources`.


### PCG : ne jamais transformer une grande texture en points (9 septembre 2026)

- **`ConvertToPointData` sur une surface de texture couvrant toute une tuile est une faute
  lourde en generation partitionnee.** Une tuile de 16 km au pas de 900 cm fait 1778^2, soit
  3,16 MILLIONS de points - et chaque maille de 256 m les fabrique tous avant d'en jeter 99 %
  via `CullPointsOutsideActorBounds`. Mesure au lancement d'un PIE : editeur a **32,2 Go**, thread
  de jeu bloque **74 s**.
- **Le bon sens de lecture est l'inverse** : semer d'abord DANS la maille avec un
  `PCGSurfaceSampler` (Surface = le Landscape, Bounding Shape = l'entree du graphe, donc les
  bornes de la maille), PUIS lire la carte des biomes au point avec **`PCGSampleTexture`**
  (`texture_mapping_method = Planar`, `density_merge_function = Set`), qui reporte la couleur lue
  dans la densite. L'identifiant de biome arrive intact et le cout suit la taille de la maille.
  Apres correction, meme PIE : pic a **22,6 Go**, stabilise a **13,6 Go**, blocage **4 s**.
  Resultat identique sur le banc : 44 244 instances et 77 maillages contre 44 074 et 77.
- **Un `PCGTextureSampler` par pas de grille, c'est une `PCGTextureData` par noeud**, chacune une
  copie FLOTTANTE de la texture (4065^2 x 4 canaux x 4 octets = 252 Mo). Quatre pas x cinq graphes
  = vingt copies. Un seul echantillonneur par graphe, decimation par `PCGSelectPoints` de ratio
  (fin/large)^2 pour les pas plus larges - il ne touche pas a la densite.
- **`pcg.Cache.Editor.MemoryBudgetMB` vaut 6144 par defaut** : PCG garde chaque resultat
  intermediaire et remplit ce budget. Ramene a 2048 dans `Config/DefaultEngine.ini`.
- **Lire l'alerte memoire de Windows correctement** : elle porte sur le COMMIT, pas sur la RAM
  libre. Mesure typique ici : 72,4 Go engages pour une limite machine de 90,5 (63,8 de RAM +
  26,7 de fichier d'echange), alors qu'il restait 44,9 Go de RAM libre. `memreport` en console
  donne le detail ; attention, ses colonnes `ResExcKB` sur-comptent (la somme depassait la
  memoire physique du processus).

### Vegetation semee sous l'eau : un desaccord de reechantillonnage (9 septembre 2026)

- **Symptome** : herbe et arbres sous la surface de l'ocean, jusqu'a -12 m.
- **Ce que ce n'est PAS** : un defaut d'alignement. L'ecart moyen entre l'altitude rendue par le
  generateur et le Z du monde Unreal est de **0,0 m** (mesure sur 98 points).
- **Cause reelle** : la simulation tourne en 2049 et la sortie en 8129. Le RELIEF monte en
  resolution en **bicubique** (`order=3`) puis recoit du detail fractal dont le masque autorise
  l'ajout des **-20 m** (`smoothstep(-20, 40, big)`), tandis que la carte des BIOMES monte au
  **plus proche voisin**. Les deux trait de cote divergent donc mecaniquement. Mesure sur la
  graine 20260909 : **89 420 pixels (0,35 % des terres)** portent un biome terrestre sous
  l'altitude zero, dont 64 % de plage.
- **Correction en amont** : `worldgen/export.py`, `upsample_heightmap` cale desormais le signe de
  l'altitude sur le masque terre/mer monte au plus proche voisin, avant ET apres l'ajout du
  detail. Toute nouvelle generation est saine.
- **Reparation d'une sortie deja produite**, sans relancer la simulation ni retoucher au relief
  importe : `export_biome_texture.py --repair` reclasse en ocean tout biome terrestre sous zero
  et sauvegarde l'original. Apres reparation, re-tuiler, reimporter les textures, regenerer.
  Resultat mesure : de 94 instances sous l'eau (jusqu'a -12 m) a **3, Z minimal -26 cm**, soit la
  ligne d'eau a la resolution du pixel pres (3,94 m).
- **Le selecteur d'attribut PCG n'est pas pilotable depuis Python** :
  `PCGAttributePropertyInputSelector` est une struct opaque (`to_dict()` vide), `import_text`
  ignore `$Position.Z` et `set_point_property` ne prend pas. Un filtre de hauteur pose ainsi
  retombe silencieusement sur `$Density` et ne filtre rien. Corriger la DONNEE en amont plutot
  que d'essayer de filtrer dans le graphe.


### Eau : rideau vertical au bord des lacs, plaques sur les falaises (10 septembre 2026)

- **Cause** : toute l'eau d'un niveau passe par la `WaterZone` qui la couvre, laquelle encode la
  hauteur de surface dans UNE texture. Worldseed n'a qu'une zone de 34 km pour de l'eau allant de
  0 m (ocean) a 545 m (Lac_03). A 1024 texels cela fait **33 m par texel** : au bord d'un lac
  perche la surface doit franchir toute la difference d'altitude en un ou deux texels (rideau
  vertical), et la ou le relief monte vite le texel voisin impose sa hauteur au-dessus de la roche
  (plaques d'eau sur les falaises). Les deux symptomes ont la meme origine.
- **Le moteur signale le second levier** : "Width of water quad tree tiles (1024) has exceeded the
  cap for this platform (256). Tile sizes have been biased by a factor of 0.25" -- la tuile de
  24 m demandee devient 96 m. D'ou `r.Water.WaterMesh.MaxWidthInTiles=2048` dans
  `Config/DefaultEngine.ini`.
- **CE QUI NE MARCHE PAS, et qui semble evident** : donner a chaque lac sa propre zone, petite et
  fine, via `WaterBodyComponent.set_water_zone_override()`. Deux echecs mesures : tant que
  l'override est pose **le lac cesse d'etre rendu** (verifie a l'image, zone de 4,9 km a 2048
  texels soit 2,4 m/texel), et **l'override ne se serialise pas** -- il revient a `None` apres
  rechargement du niveau, malgre un `save_dirty_packages` reussi. Les zones creees ont ete
  supprimees. Ne pas refaire ce chemin sans comprendre d'abord pourquoi le rendu s'arrete.
- **CE QUI MARCHE** : monter `render_target_resolution` de la zone unique de 1024 a 4096, soit
  **33,2 -> 8,3 m par texel**. Quatre fois mieux, sans nouvel acteur, et ca tient au rechargement.
  Ce n'est pas une correction complete : 8,3 m reste grossier pour une marche de 192 m.
- **Piege annexe, paye deux fois** : les acteurs d'eclairage
  (`Worldseed_SunLight` / `_SkyLight` / `_Atmosphere` / `_Fog`) ont disparu du niveau entre deux
  sessions malgre une sauvegarde reussie. Verifier leur presence par les DESCRIPTEURS World
  Partition (`get_actor_descs`), pas seulement par `get_all_level_actors` : un acteur non charge
  et un acteur inexistant se ressemblent, et une scene noire au demarrage vient souvent de la.
- **`load_level` echoue silencieusement si un PIE tourne** : il rend `False` et le journal dit
  "The Editor is currently in a play mode". Appeler `editor_request_end_play()` d'abord.
### Unreal : World Partition et sauvegarde

- **La suppression d'un acteur World Partition est ecrite sur le disque
  IMMEDIATEMENT**, un fichier `.uasset` en moins par acteur sous
  `Content/__ExternalActors__`. `save_dirty_packages` renvoie alors `True` en
  0,0 s sans rien avoir a faire, ce qui fait croire a tort que rien n'a ete
  sauve. Le controle qui tranche : recharger le niveau et recompter
  `get_actor_descs()`.
- **`PCGWorldActor` peut peser des GIGA-OCTETS a lui seul.** Avec
  `landscape_cache_object.serialization_mode = AlwaysSerialize` (obligatoire pour
  la generation PCG a l'execution), le cache de paysage est ecrit dans SON paquet
  d'acteur externe : **2,6 Go mesures pour 1024 composants**, soit 97 % du poids
  du niveau. Le supprimer libere tout, et il se recree par
  `spawn_actor_from_class(unreal.PCGWorldActor, ...)` -- contrairement a
  `WaterZone`, qui rend `None`.
- **Un `PCGWorldActor` NEUF arrive desarme** : `serialization_mode` a
  `NeverSerialize` et `enable_world_partition_generation_sources` a **False**.
  Resultat en PIE : aucune instance, et **pas une seule ligne `LogPCG` dans le
  journal** -- le planificateur n'a meme pas demarre. Poser les deux a la main.

### Unreal : l'eau

- **`WaterZone` ne se spawne pas depuis Python** (`spawn_actor_from_class` rend
  `None`). C'est le plugin Water qui la cree lui-meme des le premier corps d'eau
  d'un niveau qui n'en a pas, dimensionnee sur les bornes du monde. On l'ADOPTE :
  renommer, elargir, monter `render_target_resolution`. `zone_extent` est la
  largeur TOTALE, pas la demi-portee.
- **`RiverWidth` est la largeur TOTALE en centimetres**, alors que le commentaire
  de `WaterSplineMetadata.h` dit "from center in each direction". Le maillage
  tranche : `WaterBodyRiverComponent.cpp:111` fait `RiverWidth.Eval(Key) / 2`.
  Suivre le commentaire donne des rivieres deux fois trop larges.
- **`UWaterSplineMetadata` n'a pas de wrapper Python** : son `dir()` est vide et
  `get_editor_property("river_width")` echoue, mais
  `get_editor_property("RiverWidth")` -- le nom C++ EXACT -- marche. L'objet
  lui-meme est un `UPROPERTY(Instanced)` prive, atteignable seulement par
  `unreal.load_object(None, acteur.get_path_name() + ".WaterSplineMetadata")`.
  En pratique, preferer les setters exposes du composant :
  `set_river_width_at_spline_input_key` / `set_river_depth_at_spline_input_key`.
- **L'eau du monde se rejoue avec `Tools/UE/water_world.py`.** Le script de la
  premiere session n'avait pas ete conserve et tout a ete a retrouver.

### Unreal : divers

- **`import_world.py` n'assigne PAS le materiau de Landscape.** Un Landscape
  fraichement importe est blanc. Poser
  `landscape_material = /Game/Worldseed/Materials/M_WorldseedLandscape` sur
  l'acteur Landscape (0,5 s, les proxies suivent).
- **`LandscapeService.get_height_in_region` prend le LABEL de l'acteur (str)**,
  pas l'acteur lui-meme, sans quoi : "Cannot nativize 'Landscape' as 'String'".
- **`capture_image` capture le viewport ACTIF, qui n'est pas forcement la
  perspective.** Une disposition `FourPanes2x2` avec le panneau `top`/`unlit`
  actif rend une carte a plat, sans relief ni ciel, et on croit a un probleme de
  rendu. `unreal.ViewportService.get_viewport_info()` donne l'etat ;
  `set_viewport_layout("OnePane")` + `set_viewport_type("perspective")` +
  `set_view_mode("lit")` remettent les choses en place.

### Un acteur qui gere le monde entier ne doit pas etre spatialement charge (11 septembre 2026)

`BP_WorldseedClimat`, pose a l'origine pour piloter Ultra Dynamic Sky, etait
**absent du PIE**. Cause : `is_spatially_loaded` vaut **True** par defaut, donc
World Partition le diffuse par distance -- l'acteur etait a Y = 0, le pion a
2,9 km, et il n'a jamais ete instancie. Aucune erreur, aucun avertissement : il
n'existe simplement pas.

Les acteurs d'UDS, eux, se declarent **`is_spatially_loaded = False`** dans leur
propre construction, ce qui explique qu'ils soient toujours la. Tout acteur de
GESTION doit faire pareil. A poser sur le DEFAUT DE CLASSE du Blueprint
(`BlueprintService.set_property(bp, "bIsSpatiallyLoaded", "false")`), pas
seulement sur l'instance, sinon la prochaine instance repose le probleme.

**Piege de diagnostic** : chercher l'acteur par son libelle dans le monde de PIE
rend une liste vide, ce qui ressemble a « l'acteur n'a pas ete cree » alors
qu'il existe bel et bien dans le niveau. Verifier
`is_spatially_loaded` AVANT de soupconner la creation.

### build_graph : les executions cablees ne prouvent rien sur les DONNEES

Le meme acteur compilait sans erreur et ne faisait rien : ses references etaient
a `None` et la latitude ne bougeait pas. `build_graph` avait rapporte
**18 noeuds sur 18 et 17 liaisons sur 17**, tout en vert -- parce que les
17 liaisons demandees avaient bien ete faites. J'en avais simplement oublie
TROIS : la sortie de chaque `Cast` vers son `Set`, et l'arc sinus vers la
latitude. Un rapport vert ne dit rien des liaisons qu'on n'a pas demandees.

**Ne pas deviner les noms de broches.** Ceux-la portent des espaces :
`AsUltra Dynamic Sky`, `AsUltra Dynamic Weather`. Les lire avec
`get_node_pins`, dont les champs utiles sont `pin_name`, `pin_type`, `is_input`
et `is_connected` -- il n'y a PAS de champ `direction`. L'etoile de
`is_connected` sur chaque broche montre d'un coup d'oeil ce qui pend dans le
vide.

**Le controle qui tranche** : ne pas se fier a la compilation, faire bouger une
entree et mesurer la sortie. Ici, teleporter le pion a deux latitudes et lire
`UDS.Latitude` : -30,00 attendu / -30,00 lu, puis +71,81 / +71,81.

### Les HLOD n'ont rien à construire, et c'est mesuré (13 septembre 2026)

Chantier ouvert depuis longtemps sous la forme « les HLOD ne sont pas
construits ». Vérification faite avant de lancer quoi que ce soit :

- **zéro `StaticMeshActor` dans le niveau.** Les 42 acteurs chargés sont le
  terrain et ses 4 proxies, l'eau (1 océan, 3 lacs, 16 rivières, la zone), les
  2 volumes de RVT, le volume PCG, les lumières et les acteurs de gestion ;
- **aucun acteur ne porte de couche HLOD** (`hlod_layer` à `None` partout), bien
  que deux couches existent pour la carte — `L_Worldseed_HLODLayer_Instanced` et
  `_Merged` ;
- le **Landscape est `is_spatially_loaded = False`** : toujours chargé, donc
  jamais remplacé par un proxy lointain ;
- et la raison de fond : **la végétation est générée par PCG à l'EXÉCUTION**.
  Elle n'existe pas au moment d'un build, donc aucun HLOD ne peut la couvrir.
  C'est structurel, pas un oubli de configuration.

**Ce qui est exposé à Python**, pour le jour où ce sera utile :
`unreal.WorldPartitionHLODBlueprintLibrary.build_hlod_for_actors(actors)` et
`build_hlod_for_volume(...)`. En revanche le build COMPLET du menu
`Build > Build HLODs` ne l'est pas — même mur que la minimap du World
Partition, dont le builder est une classe sans binding Python.

**À reprendre le jour où des maillages statiques seront posés à la main.** Tant
que tout le contenu est terrain, eau ou PCG runtime, construire les HLOD produit
un fichier vide.

### Le monde se calcule maintenant DANS LE JEU, en C++ (14-15 septembre 2026)

La chaine de generation a ete portee de Python vers C++ : elle ne tourne plus
hors du moteur pour ecrire des PNG importes ensuite, elle **tourne a
l'execution**, depuis une graine choisie dans `L_Menu`, et le relief est un
maillage procedural par chunks au lieu d'un Landscape. `Source/Worldseed/Procedural/`
en porte tout le code ; la carte de jeu est `L_Worldseed_Proc`.

**LE GENERATEUR PYTHON A ETE SUPPRIME LE 19 SEPTEMBRE.** Cette note disait
qu'il etait legataire ; il n'existe plus du tout. Il ne reste de
`Tools/WorldGen` que de la DONNEE -- `rules/world_rules.json`, la source de
verite des reglages lue par le C++, et `rules/climats_reels.json`, les
vingt-trois releves de stations reelles que le bulletin terrestre confronte.
Voir la section « Le Python est parti » en fin de fichier.

Mesures de la chaine, graine 20260909 en 2048x1024 sur 64 x 32 km, apres la
boucle soulevement / erosion et la passe littorale : tectonique 780 ms,
lithologie 88, climat 8 832, **erosion 31 698**, littoral 222, climat 9 221,
biomes 26, cavites 9 684 -- **total 62 s**, cache 18,4 Mo, chaine v12.

