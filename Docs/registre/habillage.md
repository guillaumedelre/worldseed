# Registre Worldseed — ce qu'on voit au sol

> **CE FICHIER NE SE LIT PAS EN ENTIER, IL SE CHERCHE.** Il porte le recit des
> seances : ce qui a ete mesure, ce qui a tranche, et surtout **les pistes
> abandonnees**, pour qu'on ne les retente pas. Les REGLES qu'on en a tirees
> vivent dans `CLAUDE.md`, chargé a chaque tour ; ici se trouve la PREUVE.
>
> ```
> grep -n -i '<le mot du sujet>' Docs/registre/*.md
> grep -A40 'le titre exact' Docs/registre/habillage.md
> ```
>
> **Le contenu est CUMULATIF.** Certaines notes decrivent un etat qui n'existe
> plus ; les plus dangereuses portent une marque de peremption. Avant d'agir sur
> une note, verifier que ce qu'elle nomme existe encore — une note qui survit a
> ce qu'elle decrit coute plus cher que pas de note.
>
> **Ce fichier** : vegetation, semis PCG, palettes, packs d'assets, materiaux, Runtime Virtual Texture, textures, pans de falaise, DLWE.

### Densite du foliage et LOD (11 septembre 2026)

- **`Stylized_PBR_Nature` etait sous-employe, et mal.** 23 citations sur 171
  (13 %), et surtout ses deux maillages les plus utiles n'etaient PAS utilises :
  `SM_Grass` (**40 triangles**) et `SM_Bush` (**64**) -- les plus legers du
  projet, quand le buisson equivalent d'Orasot en fait **1308**, soit vingt fois
  plus pour le meme role au sol. Les 14 `FoliageType` livres par le pack sont
  ignores, ce qui est normal pour PCG (il passe par des descripteurs ISM), mais
  les reglages de l'auteur sont alors a recopier a la main.
- **La densite ne se lit pas dans le graphe PCG mais dans les PAS DE GRILLE.**
  La couche la plus fine etait a 600 cm, soit **304 instances a l'hectare** : le
  sol se voyait partout entre les plantes. Une couche `tapis` a 150 cm et un
  sous-bois resserre a 250 cm portent une foret a **6 201/ha** (x16). Les biomes
  NUS gardent volontairement le pas large -- un desert dense n'est plus un
  desert (desert chaud 315/ha, roche nue 284/ha, inchanges).
- **Le groupe de LOD nomme `Foliage` NE GENERE AUCUN LOD.** Sa definition dans
  `BaseEngine.ini` est `NumLODs=1` : il suppose des LOD faits a la main. Le
  choisir pour du feuillage est exactement le mauvais reflexe. Les groupes qui
  reduisent sont `SmallProp`, `LargeProp`, `Deco` (4 crans, 50 % de triangles
  par cran) et `HighDetail` (6 crans). Mesure sur `SM_Common_Tree_01` en
  `LargeProp` : 1798 -> 899 -> 450 -> 225 triangles.
  `Tools/UE/foliage_lods.py` pose le groupe sur les maillages de plus de 400
  triangles qui n'en ont pas -- **42 maillages sur 141**, sans toucher a ceux qui
  ont deja des LOD faits a la main, ni aux petits, pour lesquels le levier est la
  distance de coupe et non le LOD.
- **`FPCGSoftISMComponentDescriptor` est une struct dont `dir()` ne montre
  RIEN.** Ses champs n'existent que par leur nom exact (voir
  `ISMComponentDescriptor.h`) : `cast_shadow`, `cast_contact_shadow`,
  `affect_distance_field_lighting`, `cast_far_shadow`,
  `world_position_offset_disable_distance`. Une faute de frappe passe inapercue
  jusqu'a l'execution.
- **NE PAS couper l'ombre portee du foliage "par precaution".** Fait une fois,
  au passage a 6 200 instances a l'hectare : ombre portee du tapis et ombres de
  contact coupees, sans mesure prealable. Verdict a l'image, immediat et sans
  appel -- **les plantes ne touchent plus le sol, elles flottent**. Et la coupe
  ne se justifiait pas : 105 FPS AVEC les ombres contre 114 sans, soit 0,7 ms de
  rendu, pour un budget de 16,67. **Une ombre se paie avant de se couper.**
  Ce qui reste coupe ne se voit pas : `cast_far_shadow` ne concerne que la
  cascade lointaine, au-dela de la distance ou l'instance a deja disparu par
  `instance_end_cull_distance`.
- **Verifier une densite en mesurant, pas a l'oeil.** Apres x16 :
  `PerformanceService.frame_timing()` donne 114-120 FPS, borne RENDER THREAD a
  8,8 ms pour un budget de 16,67, verdict PASS. C'est le nombre d'objets dessines
  qui limite, pas le GPU (6,6 ms) : les leviers utiles sont donc les distances de
  coupe et les LOD, pas la resolution d'ecran.

- **Le pool de streaming de textures d'UE vaut 1000 Mo par defaut**, une valeur
  de 2008. Des que la vegetation dense a ete posee, le viewport a affiche en
  rouge `POOL DE CHARGEMENT DYNAMIQUE DE TEXTURE SUR LE BUDGET 741,562 MiB` : la
  scene en demandait environ 1742. **Le moteur ne plante pas, il RETROGRADE les
  mip-maps** -- les textures deviennent floues et on cherche la cause ailleurs.
  La carte de cette machine a 24 Go de VRAM (`AdapterRAM` de WMI plafonne a 4 Go
  et ment : lire `HardwareInformation.qwMemorySize` dans le registre). Pool porte
  a 4000 Mo dans `Config/DefaultEngine.ini`. Prend effet sans redemarrer :
  `r.Streaming.PoolSize 4000` en console.

### Un pack de terrain se consomme par son INSTANCE, jamais par son maitre (11 septembre 2026)

**Le defaut le plus couteux de la session, et le plus banal.** Le materiau du
Landscape, `M_WorldseedLandscape`, avait ete obtenu en dupliquant le MAITRE du
pack Orasot (`M_LandscapeMasterMaterial`) pour y ajouter une 10e couche `Snow`.
Il portait donc les valeurs PAR DEFAUT du maitre. Or la demo du pack n'utilise
jamais le maitre : elle utilise `MI_LandscapeMasterMaterial`, une **instance**
qui surcharge **14 scalaires et 4 textures**. Ces valeurs SONT le rendu du pack ;
le maitre seul ne ressemble a rien.

Ecarts mesures entre notre defaut et l'instance de l'auteur :

| parametre | notre defaut | instance du pack | effet |
|---|---|---|---|
| `Uv Scale` | 0,200 | **0,112** | carrelage du sol, textures delavees |
| `Boost` | **0,0** | **579,9** | densite de l'herbe de Landscape |
| `Sand UV` | 0,200 | 0,495 | carrelage du sable |
| `Slope Vertex Offset` | **0,0** | 0,168 | seuil d'apparition de la roche |
| `Slope Vertex Hardness` | 1,0 | 0,5005 | durete de la transition vers la roche |
| `Noise Offset` | 0,0 | 0,176 | bruit de variation |

Avec `Slope Offset = 0` et `Hardness = 1`, la roche pale envahissait presque tout
le relief -- d'ou un monde blanchatre qu'on prenait pour de la neige alors que la
couche `Snow` valait 0 partout. **Le reflexe qui sauve : avant d'accuser les
poids peints, comparer les parametres de son materiau avec l'INSTANCE livree par
le pack** (`MaterialInstanceConstant.scalar_parameter_values`).

Corrige en creant `MI_WorldseedLandscape`, instance de notre maitre, dans
laquelle les 18 surcharges de l'auteur ont ete recopiees telles quelles.

**Deux autres reglages venaient de la demo, pas du materiau :**

- **Il n'y avait AUCUN `PostProcessVolume`.** La demo du pack en a un, INFINI
  (`unbound`), avec l'auto-exposition **verrouillee** : `auto_exposure_min_brightness`
  et `max_brightness` tous deux a **-1,0**, bias 0. Sans lui, l'auto-exposition
  histogramme court librement, s'adapte au ciel et delave tout le sol. C'est ce
  qui donnait a toutes les captures leur aspect surexpose et bleuatre. La fiche
  Fab du pack le dit d'ailleurs en premiere ligne.
- **SkyLight a 1,0 au lieu de 3,0.** Le soleil, lui, etait deja bon (5,0 lux
  contre 4,99 dans la demo) : inutile de le toucher.

**Ce qui reste ouvert : l'herbe de Landscape.** 16 composants d'herbe existent
sur les proxies et produisent **0 instance**. Les cinq entrees de
`LandscapeGrassOutput` sont des chaines `Add`/`Subtract` : quand le terme
soustrait depasse le premier, le resultat est negatif et rien ne pousse. C'est la
chirurgie de graphe signalee au §5.1 de ETAT_DES_LIEUX, toujours a arbitrer.

### La vegetation ne pousse pas DANS L'EDITEUR, et c'est normal (11 septembre 2026)

Signale : « il faut que l'herbe pousse ». Le viewport de l'editeur montrait un sol
nu, alors que le PIE, lui, etait couvert de vegetation.

**Ce n'etait pas une panne.** Le semis PCG est en `GenerateAtRuntime` : il ne
produit rien tant qu'aucune SOURCE DE GENERATION ne le reclame. En jeu, le pion
en tient lieu. Dans l'editeur, il faut poser
`PCGWorldActor.treat_editor_viewport_as_generation_source = True` -- coupe a
dessein pendant les reconstructions, pour que l'editeur ne seme pas autour de la
camera pendant le menage, et jamais rallume ensuite. Une fois rallume, la camera
de l'editeur devient la source et le monde se garnit. Mesure : 8,9 Go de memoire
et 87 FPS avec la vegetation dense generee autour du point de vue.

**LE PIEGE DE MESURE, ET IL M'A COUTE UNE HEURE.** Pour savoir si l'herbe de
Landscape poussait, j'ai compte les
`HierarchicalInstancedStaticMeshComponent` portes par le Landscape et ses
proxies : **0**. J'en ai conclu a un defaut du materiau et je suis parti
demonter la chaine `LandscapeGrassOutput`. **La demo du pack, qui a pourtant un
tapis d'herbe visible, rapporte exactement le meme 0** : l'herbe de Landscape
n'est PAS exposee comme composant de l'acteur. Le compteur ne mesurait rien.
**Sur un systeme dont on ne connait pas la representation interne, valider la
metrique sur un cas TEMOIN connu avant d'en tirer la moindre conclusion.**

**Ce qui reste vrai et utile de cette fouille**, pour qui voudra un jour le tapis
d'herbe emis par le materiau (en plus du semis PCG) :
- les cinq entrees de `LandscapeGrassOutput` valent
  `Lerp(poidsCouche, Floor(poidsCouche), Alpha)` avec Alpha a 0,8 ou 1,0.
  `Floor(p)` vaut 0 sauf si le poids vaut EXACTEMENT 1,0. Le pack peint ses
  couches a 100 % sur de grandes zones ; notre generateur melange partout
  (dominante a 0,62 en mediane, jamais 1,0). La porte est donc fermee par
  construction ;
- toutes soustraient aussi une couche `RemoveFoliage`, que le generateur ne
  peint pas. Elle n'est pas allouee sur le Landscape et son `PreviewWeight` vaut
  0, donc elle ne retranche rien : ce n'est PAS le coupable ;
- mettre les six `ConstAlpha` a 0 n'a eu aucun effet visible. Essai annule, les
  valeurs de l'auteur ont ete remises.

**DECISION DU PROPRIETAIRE DU PROJET, 11 septembre 2026 : on n'y touche pas.**
Cette porte fermee est un CHOIX de l'auteur du pack, pas un defaut. Le tapis
d'herbe du monde vient du semis PCG, qui remplit ce role et se regle depuis
`vegetation_recipes.json`. Ne pas rouvrir `LandscapeGrassOutput`, ne pas
retoucher les `ConstAlpha`, et ne pas rendre les poids peints plus purs pour
franchir le `Floor` : cela changerait le rendu du terrain entier pour un gain
que le PCG apporte deja.

### De l'herbe sur la roche : la carte des biomes n'est pas la carte des surfaces (11 septembre 2026)

Signale : de l'herbe pousse sur des textures de roche et de sable.

**La cause.** Le semis PCG lit `biome_index.png`, la carte des BIOMES. Le sol,
lui, affiche les dix COUCHES PEINTES. Or `surfaces.build` ECRASE la recette du
biome par la roche de pente (des 22 degres, `slopeRockStartDeg`) et par la neige :
dans un biome de foret, un versant raide est peint en pierre tout en restant
"foret" sur la carte des biomes. Les deux cartes divergent donc par construction,
et le PCG semait un tapis d'herbe sur de la roche.

Mesure sur la graine 20260909 : **39,6 % du tapis** tombait sur une surface peinte
en mineral. Par biome : **Alpin 99,9 %**, **Desert froid 100 %**, Toundra 64,9 %,
forets 24 a 32 %.

**LA SOLUTION EVIDENTE EST MAUVAISE.** Un filtre de PENTE a 22 degres retirerait
**65 % du tapis** sur ce monde montagneux -- il deshabillerait le monde entier --
tout en laissant 7 % d'herbe sur du sable PLAT, que la pente ne voit pas. Ce
n'est pas la pente qui dit ou l'herbe est credible, c'est la surface peinte.

**Ce qui a ete fait.** `export_biome_texture.masque_mineral` calcule la couche
DOMINANTE de chaque pixel ; la ou elle est `Snow`, `Stone`, `Gravel` ou
`DesertSand`, la carte ecrite dans `tiles/` porte l'identifiant du biome
**decale de 100**. La carte lue par le PCG dit donc deux choses a la fois : quel
biome, et si le sol y est mineral. Dans le graphe, chaque biome occupe deux
bandes de densite : le TAPIS ne prend que `id`, les arbres et le sous-bois
prennent `id` ET `id + 100` -- un versant raide reste boise meme quand la roche
affleure entre les troncs. Resultat : **-39,6 % de tapis, exactement les pixels
fautifs**, 111 FPS, verdict PASS.

`Biom 4 Gravel` n'est VOLONTAIREMENT pas compte comme mineral : c'est le sol
boueux du marais et des berges, ou la vegetation basse est chez elle.

**ATTENTION** : `MINERAL_ID_OFFSET` dans `Tools/UE/vegetation.py` et
`DECALAGE_MINERAL` dans `Tools/WorldGen/export_biome_texture.py` doivent rester
egaux. C'est le seul lien entre la carte et le graphe, et rien ne le verifie.

**Reste ouvert** : la COULEUR de l'herbe ne s'adapte pas au biome. Le meme
`SM_Grass` vert est seme en savane comme en foret tropicale. Le pack teinte son
feuillage par la couleur du terrain, via une Runtime Virtual Texture -- chemin
deja tente et abandonne (voir plus haut : herbe bleue puis noire, terrain aplati).

### La Runtime Virtual Texture marche : ce qui avait fait echouer la premiere fois (11 septembre 2026)

Le pack Orasot livre DEUX textures virtuelles, dans
`Stylized_Landscape_5_Bioms/Global/RVT/` :
`RVT_Landscape_Material` (BaseColor + Normal + Specular, YCoCg) et
`RVT_Landscape_Height` (WorldHeight). Le Landscape y ECRIT ; le feuillage les
RELIT et s'accorde au sol. C'est ce qui donne aux rendus du pack leur coherence.
Sa fiche Fab le demande d'ailleurs en toutes lettres.

**LE COUPABLE : `virtual_texture_render_pass_type`.** Laisse a son defaut, le
Landscape ne se dessine plus dans la passe principale mais DEPUIS la RVT : le
terrain proche perd sa geometrie fine et parait aplati. La session precedente
avait vu cet aplatissement, conclu que la RVT etait en cause et fait marche
arriere. **Il faut `ALWAYS`** : le terrain se dessine normalement ET alimente la
RVT. Avec ce reglage, aucun aplatissement -- verifie a l'image.

**Le montage complet est dans `Tools/UE/rvt_setup.py`**, releve sur la carte de
demo du pack. Deux points qui se ratent :
- **La transform d'un `RuntimeVirtualTextureVolume` est un COIN + une TAILLE**,
  pas un centre et une demi-portee. Verifie sur la demo : volume a
  (-101600, -101600, -25600), echelle 204800, pour un terrain allant de -101600
  a +101600.
- Les RVT doivent etre posees sur l'acteur `Landscape` ET sur **chaque proxy**
  World Partition, avec `virtual_texture_num_lods = 6`.

Ne PAS utiliser `unreal.RuntimeVirtualTextureService.create_rvt_volume` : il cree
des `Actor` generiques a l'origine et a l'echelle 1.

**Consequence sur le semis.** `vegetation.rvt_free_materials` dupliquait 15
instances de materiau avec `UseRVT = false`, pour eviter le bleu pur des rochers
de desert. Ce contournement est desormais CONDITIONNEL : `vegetation.rvt_disponible()`
regarde si le niveau porte un volume de RVT valide, et le contournement s'efface
de lui-meme. Plus aucun override n'est pose.

**PIEGE ANNEXE, paye deux fois : `treat_editor_viewport_as_generation_source` est
capricieux en 5.8.** La generation PCG autour de la camera de l'editeur s'arrete
apres une reconstruction du graphe et ne repart pas, meme en bougeant la camera --
`pcg.RuntimeGeneration.EnableDebugging 1` ne montre alors AUCUN `GENERATE`. En
PIE, ou la source est le pion, elle repart immediatement. **Le viewport de
l'editeur n'est pas une reference : juger la vegetation en PIE.** Le journal
signale d'ailleurs que `enable_world_partition_generation_sources` est deprecie
en 5.8 au profit de `UPCGGenSourceComponent`.

**PIEGE ANNEXE 2 : le `PlayerStart` revient a sa position d'avant apres un
rechargement de niveau.** Vu trois fois. `save_dirty_packages(True, True)` ne
suffit pas toujours ; passer par le paquet de l'acteur :
`unreal.EditorLoadingAndSavingUtils.save_packages([acteur.get_outermost()], False)`,
qui rend True et tient au rechargement. Symptome si on l'oublie : le pion apparait
hors du monde et tombe dans la mer.

### Une reconstruction du monde EFFACE la Runtime Virtual Texture (11 septembre 2026)

`rebuild_world.clear()` detruit le Landscape et ses proxies ; `landscape()` les
recree **NEUFS**, donc avec `runtime_virtual_textures` vide. Les deux
`RuntimeVirtualTextureVolume`, eux, sont des acteurs distincts et survivent
intacts. **Rien ne signale le probleme** : aucune alerte, aucun journal. Le
feuillage cesse simplement de prendre le ton du sol, et les maillages qui
passent par `M_Assets_MasterMat` peuvent redevenir bleu vif.

Mesure apres le re-import du monde recalibre : 2 volumes presents, **0 texture
sur les 5 acteurs de terrain**. `rvt_setup.poser()` est desormais appele par
`rebuild_world.rebuild()`, juste apres l'import du relief.

**Regle generale a en tirer** : tout reglage pose sur l'ACTEUR Landscape et non
sur un asset est perdu a la reconstruction. Verifier apres chaque `rebuild()`
que `runtime_virtual_textures` n'est pas vide, au meme titre que le materiau de
terrain -- qui, lui, est deja repose par `landscape()`.

### Le terrain utilisait le MAITRE et non l'INSTANCE (11 septembre 2026)

**Regression silencieuse, presente depuis le re-import.** `rebuild_world.py`
assignait `M_WorldseedLandscape` -- le MAITRE -- alors que le rendu du pack tient
dans son INSTANCE. Mesure : **10 des 14 scalaires different** entre les deux.

| parametre            | maitre (utilise) | instance (voulu) | effet                          |
|----------------------|------------------|------------------|--------------------------------|
| `Uv Scale`           | 0,200            | **0,112**        | carrelage, textures delavees   |
| `Boost`              | **0,0**          | **579,9**        | densite de l'herbe de Landscape|
| `Slope Vertex Offset`| **0,0**          | 0,168            | la roche envahit les versants  |
| `Sand UV`            | 0,200            | 0,495            | carrelage du sable             |

Le defaut avait deja ete trouve et corrige une fois, en creant l'instance -- mais
la CONSTANTE du script de reconstruction n'avait pas suivi, donc **chaque
reconstruction annulait la correction**. Corrige a la source :
`LANDSCAPE_MATERIAL` pointe desormais sur `MI_WorldseedLandscape`.

**Regle : corriger un asset ne suffit pas si un script en assigne un autre.**
Apres toute correction de materiau, verifier ce que le script de reconstruction
assigne reellement.

### DLWE_V3 : insere et compile, mais ne produit PAS encore de neige

Etat au 11 septembre 2026, a reprendre. Ce qui est fait et verifie :

- `M_WorldseedLandscape` est en `use_material_attributes = True`, donc
  l'insertion est bien UN seul noeud : le `MakeMaterialAttributes` final -- celui
  en (2048, -128), le seul dont la sortie n'alimente aucune autre expression --
  entre dans `Dynamic_Landscape_Weather_Effects_V3`, dont la sortie
  `Material Attributes` va a `MP_MATERIAL_ATTRIBUTES`.
  Il y a **six** `MakeMaterialAttributes` dans ce materiau, un par couche : ne
  pas prendre le premier venu, prendre celui qui ne nourrit rien.
- **Deux entrees de la fonction sont OBLIGATOIRES** et n'ont pas de defaut :
  `Apply Snow/Dust` et `Apply Wetness/Puddles`. Sans elles, le materiau ne
  compile pas -- « Missing function input ». Il faut y brancher un
  `MaterialExpressionStaticBool` a `true` (statique : cout nul a l'execution).
- Le materiau compile : 2301 expressions, 95 echantillons de texture.

**CE QUI NE MARCHE PAS ENCORE** : `Material Snow Coverage` force a 1,0 sur UDW
ne produit aucun changement visible, meme apres avoir appele
`Check for Material Refresh`, `Instant Weather Change Updates` et
`Update Current Global And Local Weather State`. Trois tentatives, arretees la.

Pistes non explorees, dans l'ordre :
1. Le readme du pack, auquel la description de la fonction renvoie explicitement
   (« See the readme for more information about setting this up »). Il est dans
   `UDS_Readme_Entries`, dont `list_variables` ne rend rien -- le contenu est
   probablement dans le graphe. **AUCUN materiau du pack n'utilise DLWE_V3**, il
   n'y a donc pas d'exemple a copier.
2. `Snow Color and Alpha` a pour defaut **(0, 0, 0, 1)**, soit du NOIR. A
   verifier : c'est peut-etre une convention signifiant « prendre la couleur
   d'UDW », ou un vrai noir qui rend la neige invisible.
3. La collection de parametres `UltraDynamicWeather_Parameters` porte
   `Dust or Snow`, `Snowy`, `DLWE_Snow Depth`. Lire ces valeurs en PIE dirait si
   UDW pousse reellement l'etat vers les materiaux. `KismetMaterialLibrary`
   n'existe pas en Python ; passer par un autre chemin.

Une sauvegarde du materiau d'avant insertion existe :
`M_WorldseedLandscape_SauvegardeAvantDLWE` (exclue du depot, comme l'original).

### La RVT teinte le feuillage : ce qu'elle couvre, et la greffe (11 septembre 2026)

**Le constat de depart.** Le terrain ECRIT sa couleur dans une Runtime Virtual
Texture ; un materiau de feuillage peut la RELIRE et prendre le ton du sol.
Sur les 142 maillages du semis, **38 seulement (27 %)** passent par un materiau
qui l'echantillonne : `M_Assets_MasterMat`, sa variante masquee,
`M_Master_Cliff_Mat` et le `M_Grass` de `Stylized_Landscape_5_Bioms`. Les 104
autres l'ignorent, dont tous les arbres et buissons. C'est pourquoi un A/B
"avec / sans RVT" ne montre presque rien : les trois quarts de ce qu'on voit ne
la lisent pas.

**La teinte, elle, est spectaculaire.** Meme maillage, meme materiau, part de
pixels verts dans la touffe : **0,0 % en desert chaud, 100 % en foret tropicale
humide**. En vue zenithale l'herbe d'Orasot DISPARAIT dans le sable, on ne voit
plus que son ombre, tandis que l'herbe non teintee ressort en vert etranger.

**DEUX MATERIAUX PEUVENT PORTER LE MEME NOM. Comparer des CHEMINS, jamais des
`get_name()`.** C'est l'erreur qui a coute le plus de temps ce jour-la :
  /Game/Orasot_Bundle/LowPolyForestVol2/Materials/M_Grass         MASQUE, feuillage deux faces
  /Game/Orasot_Bundle/Stylized_Landscape_5_Bioms/Global/M_Grass   OPAQUE, echantillonne la RVT
Ayant compare les noms, j'ai conclu que les deux herbes partageaient un parent
et que seules les surcharges d'instance differaient -- donc qu'il suffisait
d'imposer `MI_Grass` au maillage a 4 triangles. Resultat a l'image : un CARRE
PLEIN couleur sable. Ce maillage n'est que deux quads croises, sa silhouette
vient d'un masque alpha ; le materiau qui teinte est opaque parce que la
silhouette de l'herbe d'Orasot vient de sa GEOMETRIE (360 triangles).

**Piege de mesure du meme coup** : en vue ZENITHALE, un quad opaque couleur
sable pose a plat est indiscernable du sol. La mesure "18,1 % de vert -> 0,0 %"
semblait prouver une teinte parfaite ; elle mesurait une bache. **Juger une
silhouette CONTRE LE CIEL, de cote, jamais a la verticale.**

**Un echantillonnage de RVT peut etre cache dans une fonction de materiau.**
`M_Assets_MasterMat` ne contient AUCUN `RuntimeVirtualTextureSample` au premier
niveau de son graphe : il appelle `MF_RVT`. Un balayage qui ne regarde que
`export_material_graph` le classe a tort comme ignorant la RVT. Descendre dans
les `MaterialFunctionCall` via `export_function_graph`.

> **PERIME AU 28 SEPTEMBRE 2026, ET CETTE NOTE M'A FAIT ME TROMPER DEVANT LE
> PROPRIETAIRE.** Les deux assets existent toujours dans le projet, mais **ni
> `M_Assets_MasterMat` ni `MF_RVT` ne porte la moindre expression de RVT** --
> releve exhaustif : il n'y a que DOUZE expressions de RVT dans tout `/Game`,
> et aucune dans ces deux-la. Ce qui etait vrai du pack a l'epoque du Landscape
> ne l'est plus. **La methode ci-dessus reste juste** -- il faut bien descendre
> dans les fonctions de materiau -- **c'est le FAIT qui est mort.**

**`MF_RVT` fait mieux que notre greffe, et pourquoi on ne l'a pas prise.** Elle
melange couleur, speculaire, rugosite ET normale, avec un masque calcule sur la
hauteur du monde -- ce qui ANCRE l'objet dans le sol au lieu de le repeindre.
Mais elle travaille en ATTRIBUTS DE MATERIAU (`use_material_attributes`), et
aucune de nos cibles n'est cablee ainsi. `Tools/UE/rvt_graft.py` se contente
donc d'un `Lerp(couleur d'origine, couleur du sol, "Teinte RVT")` sur la seule
`BaseColor`, **en laissant toutes les autres sorties intactes -- en particulier
`OpacityMask`, sans quoi la silhouette disparait**. Dosage mesure en savane :
0,00 -> 45,7 % de vert, 0,85 -> 2,5 %, 1,00 -> 1,1 %. Retenu : 0,85.

**`delete_asset` sur un materiau rend `False` sans lever d'erreur**, meme quand
le registre ne signale AUCUN referenceur, et ni `close_all_editors_for_asset`,
ni `collect_garbage`, ni `delete_loaded_asset` n'y changent rien.
`duplicate_asset` rend alors l'asset deja present et la greffe se pose PAR-DESSUS
la precedente : deux echantillonneurs, deux Lerp chaines, teinte appliquee deux
fois. **Rendre un script d'installation idempotent par CONSTAT, pas par
destruction** : s'il trouve une greffe saine, il n'y touche pas.

**Retrouver l'OBJET d'une expression a partir de l'identifiant exporte.** Les
identifiants de `export_material_graph` sont des ADRESSES
(`MaterialExpressionLinearInterpolate_000001FB...`) ; `get_name()` rend un tout
autre nom (`MaterialExpressionLinearInterpolate_0`), et les entrees (`A`, `B`)
sont protegees en lecture. L'astuce qui marche : poser une valeur reconnaissable
sur une propriete LISIBLE via `batch_set_properties(id, "ConstAlpha", "0.123456")`,
puis chercher l'objet dont `const_alpha` vaut cette valeur.

**Le tapis d'herbe pese 67 % de toutes les instances du semis** (72 % en foret),
alors qu'il ne represente qu'une couche sur huit. C'est donc lui qui decide du
cout comme du rendu : toute question de densite ou de materiau commence par lui.
Il est desormais en TROIS couches -- `tapis` (petite touffe a 4 triangles,
greffee), `tapis_haut` (herbe d'Orasot, 360 triangles et 3 LOD, 10 % des
instances) et `tapis_accents` (fleurs, ble, lierre, a leur densite d'origine).
Bilan mesure sur les 12 biomes : **-2 % d'instances, -14 % de triangles**.

**`vegetation.py` testait `layer["name"] != "tapis"`** pour exclure le sol
mineral. Avec trois couches, le test doit porter sur le PREFIXE, sinon
`tapis_haut` seme de l'herbe sur les eboulis.

**`unreal.Rotator(a, b, c)` prend (ROLL, PITCH, YAW)**, pas (pitch, yaw, roll).
Se tromper fait viser le ciel et on croit a un probleme de rendu.

**`get_all_level_actors()` rend une liste VIDE pendant un PIE.** Compter les
instances du semis en PIE par ce chemin donne 0 et ne prouve rien.

**Les toolsets natifs d'Epic ne sont pas enregistres dans ce build** :
`vibeue.exec_tool("EditorToolset.EditorAppToolset", ...)` echoue sur "Toolset not
found", seuls les services `VibeUE.*` sont la. Pour le PIE, passer par
`unreal.LevelEditorSubsystem` : `editor_request_begin_play()` (avec un pion, donc
une source de generation PCG) et non `editor_play_simulate()`, qui n'en cree pas.

### Greffer la RVT sur les onze autres maitres (11 septembre 2026, suite)

**GREFFER LE MAITRE NE SUFFIT PAS.** Nos maillages ne pointent pas vers les
maitres du pack mais vers ses INSTANCES, qui portent textures et reglages.
`rvt_graft.rediriger()` copie donc chaque chaine d'instances rencontree dans les
recettes et reparente le haut de la chaine au maitre greffe : 11 maitres, mais
**55 instances** et 191 entrees de maillage sur 216 dans le graphe PCG.
`vegetation.overrides_greffe()` pose les overrides EMPLACEMENT PAR EMPLACEMENT,
parce qu'un maillage a plusieurs sections peut meler un materiau greffe (les
feuilles) et un materiau qui ne l'est pas (le tronc).

**LE FONDU EN HAUTEUR N'EST PAS UN RAFFINEMENT, C'EST LA CONDITION.** Teinter un
objet ENTIER a la couleur du sol ne vaut que pour un couvre-sol ; applique a un
arbre, cela le peint en terre. D'ou, comme dans `MF_RVT` :

    hauteur = WorldPosition.Z - RVT_Landscape_Height.WorldHeight
    alpha   = "Teinte RVT" * saturate(1 - hauteur / "Hauteur fondu RVT")

Le fondu se regle sur la TAILLE de ce que le materiau habille : 180 cm pour
l'herbe, 200 pour un tronc, 400 pour une cime. **Un fondu trop court ne teinte
que la base** -- mesure en savane : a 90 cm sur une herbe de 74 a 170 cm, le
sommet du brin restait vert, tres visible de pres et invisible de loin.

**`batch_connect_expressions` : une broche d'entree UNIQUE se designe par la
chaine VIDE, pas par son nom.** `OneMinus`, `Clamp` et `ComponentMask` annoncent
leur entree sous le nom "Input" dans `export_material_graph`, mais passer
"Input" echoue EN SILENCE -- le retour dit "9 connexions sur 12" sans preciser
lesquelles. Les broches nommees (`A`, `B`, `Alpha`) se passent bien par leur
nom. Corollaire : `WorldPosition` expose deja une sortie `Z`, inutile de lui
coller un `ComponentMask`.

**Un maillage a 4 triangles ne s'agrandit pas.** `SM_Env_Grass_small` (39 cm,
4 triangles) semblait le tapis ideal : dix fois plus leger que les 40 triangles
de `PBF:SM_Grass`. Mis a l'echelle pour retrouver la hauteur d'un brin (2,6 a
5,9, soit 101 a 230 cm), il ne lit plus comme une touffe mais comme un PIEU --
deux quads croises ne survivent pas a l'agrandissement, et le proprietaire l'a
signale immediatement : « on perd le look de l'herbe ». Ramene a sa taille
naturelle il redevient correct mais ne couvre plus rien. **Ecarte.** La bonne
reponse etait de greffer `M_Foliage_Master`, materiau de l'herbe d'origine :
on garde le maillage qui a le bon aspect ET on gagne la teinte, a cout
identique. Le tapis est donc revenu a sa forme d'une seule couche.

**Hauteur de l'herbe** : echelle ramenee de 0,7-1,6 a **0,55-1,15**, soit 58 a
122 cm pour un personnage de 180. A 74-170 cm elle arrivait a l'epaule.

**NE PAS CONCLURE D'UNE SILHOUETTE VERTE QU'UNE TEINTE NE MARCHE PAS.** Une
heure passee a soupconner la redirection parce qu'un eventail de lames vertes
restait vert au premier plan en savane, alors que tout le reste virait au
creme : c'etaient les PALMES d'un `SM_PalmTree_Small`, a trois metres du sol,
donc hors du fondu -- exactement le comportement voulu. Le controle qui aurait
du venir en premier : pousser `Teinte RVT` a 1,0 et le fondu a 100 000 cm. Ce
qui change alors est teinte ; ce qui ne change pas ne passe pas par ce materiau.

**`delete_asset` marche tant que le materiau n'a pas ete recompile ni ouvert.**
Les 11 copies ratees se sont supprimees sans difficulte, alors que la copie
recompilee de la veille resistait a tout. Supprimer AVANT de recompiler.

### Le vent d'UDS sur la vegetation, et le crash qu'il provoque (11 septembre 2026)

`Foliage_Wind_Movement` (`/Game/UltraDynamicSky/Materials/Weather/`) est le vent
d'Ultra Dynamic Sky : contrairement au `SimpleGrassWind` du moteur, amplitude
fixe et aveugle a la meteo, il est pilote par l'etat de vent d'UDW. Releve avant
intervention : **7 des 11 maitres greffes n'avaient AUCUN vent** -- les arbres ne
bougeaient pas -- et les deux qui en avaient passaient par `SimpleGrassWind`.

- Une seule sortie, `World Position Offset`. Trois classes de mouvement
  independantes, exposees en parametres : `Apply Small Movement` (herbe,
  feuilles), `Medium` (branches), `Large` (balancement de l'arbre entier).
- **UNE SEULE de ses 28 entrees n'a pas de valeur par defaut : `Small Movement
  Mask`.** Sans elle le materiau ne compile pas -- meme piege que `Apply
  Snow/Dust` sur DLWE_V3. On lui donne `saturate((Z - hauteur du sol) / "Hauteur
  du vent")`, reutilisant la chaine de hauteur deja posee par la greffe de RVT :
  immobile au pied, plein mouvement au sommet.
- **Les rochers et les props n'en recoivent pas** : un rocher qui ondule se voit
  immediatement. `rvt_graft.VENT` ne les liste pas.

**LE CRASH, DEUX FOIS DE SUITE, ET SA CAUSE EXACTE.** Greffer le vent sur neuf
materiaux a fait tomber l'editeur : `EXCEPTION_ACCESS_VIOLATION` dans
`UnrealEditor-D3D12RHI`, "Crash in runnable thread Background Worker". Le fil
d'Ariane du journal nomme la passe fautive :

    ParallelDraw -> RenderVelocities(Opaque) -> Scene

precede de `LogD3D12RHI: Waited for PSO creation for 100.000000ms` et d'une
douzaine de `LogShaderCompilers: Cancelled job ... with pending SubmitJob call`.

**Un materiau qui porte un WorldPositionOffset devient ECRIVAIN DE VELOCITE.**
Nos ~400 000 instances en ont toutes gagne un d'un coup, et la creation des
etats de pipeline correspondants a fait tomber le RHI. La documentation d'Epic
decrit exactement ce cas (`RendererSettings.h:972`) : *"That performance cost is
higher if many objects are using World Position Offset. A forest of trees for
example."* Remede, pose dans `Config/DefaultEngine.ini` :

    r.Velocity.EnableVertexDeformation=0      ; defaut 2 = Auto

Ce qu'on perd : le flou de mouvement et le TSR ne suivent plus le fremissement
du feuillage -- invisible sur un monde stylise. Apres ce reglage : aucun crash,
**117 FPS contre 115 avant le vent**, rendu 8,6 ms contre 8,7. Le vent ne coute
rien de mesurable.

**NE PAS RECOMPILER HUIT GROS MATERIAUX D'AFFILEE.** Le premier, traite seul,
etait passe sans incident ; la serie a tue l'editeur. Traiter par un ou deux, et
couper `treat_editor_viewport_as_generation_source` pendant l'operation. Corollaire
utile : `delete_asset` marche tant qu'un materiau n'a pas ete recompile ni
ouvert -- les 11 copies ratees se sont supprimees sans resistance, la copie
recompilee de la veille resistait a tout.

**RIEN N'EST PERDU QUAND L'EDITEUR TOMBE EN PLEINE BOUCLE** : chaque materiau
est sauve a la fin de son tour. Les dates des `.uasset` disent exactement ou la
boucle s'est arretee -- les lire AVANT de tout refaire.

### Un second PCGWorldActor desarme tue tout le semis, en silence (11 septembre 2026)

Symptome : **zero instance de vegetation**, ni dans l'editeur ni en PIE, alors
que le graphe, le volume et les recettes sont corrects. Le niveau portait DEUX
`PCGWorldActor` :

    ...C00003   sourcesWP = False   cache = NeverSerialize   <- desarme
    ...B80003   sourcesWP = True    cache = AlwaysSerialize   <- correct

PCG n'en interroge qu'un, et c'est le desarme qui repondait. Un `PCGWorldActor`
NEUF arrive ainsi (deja note plus haut) ; ce qui est nouveau, c'est qu'un
DOUBLON suffit a masquer celui qui est bien regle. Le supprimer a immediatement
ramene la foret complete. **Compter les `PCGWorldActor` doit etre le premier
reflexe devant un semis muet.**

**PIEGE DE LECTURE ASSOCIE** : sans instances PCG, ce qu'on voit au sol n'est PAS
le tapis mais l'HERBE DU LANDSCAPE, emise par le materiau de terrain. Elle ne
recoit aucune de nos greffes, donc elle ne bouge pas et ne se teinte pas -- et on
en conclut a tort que la greffe ne marche pas. Verifier par
`get_components_by_class(InstancedStaticMeshComponent)` : zero instance = ce
n'est pas le semis qu'on regarde.

**MESURER LE VENT, ET AVEC QUEL TEMOIN.** Comparer deux images successives ne
vaut que si le temoin est vraiment immobile : le personnage a une animation
d'attente (25,1 % de pixels modifies) et les nuages defilent. Temoins valables :
une geometrie non greffee dans le MEME cadre. Mesure de validation, deux images
consecutives en PIE :

    sous-bois (petit + moyen)        21,2 %
    houppier (petit+moyen+grand)     15,0 %
    herbe au sol (petit seul)        12,2 %
    tronc de bambou, sans vent        4,1 %
    nenuphars et eau, temoin          3,5 %

### Le sable scintillait : une valeur du pack calee sur une carte de demo (11 septembre 2026)

Signale : des points blancs tres marques sur le sable du desert, visibles sur
toutes les captures. Ils ressemblaient a des paillettes lumineuses avec halo.

**Ce que la recherche a etabli, dans l'ordre :**
- en `unlit` ils DISPARAISSENT, ne laissant que de fins grains blancs : ce ne
  sont donc pas des taches de la texture mais un phenomene d'ECLAIRAGE ;
- `ShowFlag.Bloom 0` les supprime entierement : c'est le bloom qui transforme
  chaque grain en pastille ;
- **mais le bloom n'etait que le revelateur.** Ni son seuil (`bloom_threshold`,
  essaye a 1 puis 8) ni son intensite (0,675 -> 0,15) ne traitent la cause ; ni
  `Roughness Intensity`, ni `Uv Scale`.

**LA CAUSE : `Sand UV` valait 0,495, la valeur de l'instance du pack -- calee
pour la petite carte de demo d'Orasot. Sur nos 8 km, la texture de sable est
etiree a un point tel que ses grains de quartz deviennent des TACHES de
plusieurs dizaines de centimetres, que le bloom fait ensuite exploser.** Porte a
**2,0**, le sable redevient une dune a grain fin, sans une seule etincelle a
moyenne et longue distance. Bloom rendu a ses valeurs par defaut : inutile d'y
toucher.

**C'est le meme piege que la constante metrique en dur de `tectonics.py`, dans
l'autre sens : une valeur d'auteur JUSTE devient fausse quand on change
l'echelle du monde.** Le reflexe "recopier l'instance du pack" reste bon pour
tout ce qui est couleur, force ou seuil ; il ne l'est PAS pour ce qui est une
FREQUENCE SPATIALE -- carrelage, taille de grain, echelle de bruit. Ces
valeurs-la se recalent sur la taille reelle du terrain.

**RESTE OUVERT** : les etincelles persistent dans les dix premiers metres, la ou
le materiau melange sans doute une texture de detail rapprochee. `Uv Scale`
(essaye a 0,35) n'y change rien et a ete remis a la valeur de l'auteur. A
reprendre en cherchant la chaine de detail proche dans le maitre du pack.

### Le sable scintillait : `T_Sand_Glitter` et un `Boost` de 580 (11 septembre 2026, suite)

**CORRECTION D'UNE NOTE FAUSSE PLUS HAUT.** Le tableau de la section "Un pack de
terrain se consomme par son INSTANCE" decrit `Boost` comme la "densite de
l'herbe de Landscape". **C'est faux.** Mesure : le materiau ne contient qu'UN
noeud nomme `Boost`, avec UNE seule utilisation -- un `Multiply` dans la chaine
EMISSIVE de la couche `DesertSand`. Il n'a aucun lien avec
`LandscapeGrassOutput`, qui est alimente par des `Add`/`Subtract` sans rapport.

**La chaine complete, remontee depuis `.EmissiveColor` du
`MakeMaterialAttributes` de DesertSand :**

    TextureSample T_Sand_Glitter -> Multiply(Boost) -> ... -> DotProduct
      avec CameraVectorWS et Noise Rot Speed / Noise Offset

Autrement dit : **le sable du pack porte une texture de PAILLETTES emissives,
dependante de l'angle de vue.** C'est voulu par Orasot, et discret sur leur
petite carte de demo. Avec `Boost = 579,9` -- la valeur de leur instance --
sur nos 8 km, cela donne un ciel etoile en plein desert. Ramene a **5,0**, il
reste un scintillement fin au ras du sol, credible pour du sable.

**Ce qui a ete elimine en chemin, et qu'il est inutile de retenter :**
- ce n'est PAS DLWE_V3 : `Apply Snow/Dust` mis a False n'y change rien ;
- ce n'est PAS le bloom : `ShowFlag.Bloom 0` supprime le symptome, mais ni le
  seuil (1 puis 8) ni l'intensite (0,675 -> 0,15) ne traitent la cause ; les
  valeurs par defaut ont ete rendues ;
- ce n'est PAS `Roughness Intensity`, ni `SandStrenght`, ni `Uv Scale`
  (essaye a 0,35), ni `LandscapeLayerCoords.MappingScale` (essaye a 16) --
  **aucun de ces deux derniers ne pilote le carrelage des couches**, contrairement
  a ce que leur nom laisse croire.

**LE CARRELAGE DES AUTRES COUCHES, mesure par autocorrelation sur des vues
zenithales prises a 6 m :**

    herbe (foret tropicale)   periode 100 cm
    sable avant correction    periode ~105 cm   (deduit : 26 x 2,0 / 0,495)
    sable apres (Sand UV 2,0) periode  26 cm
    neige (calotte)           aucune repetition nette

Les autres couches sont donc etirees exactement comme l'etait le sable. **Si
cela ne se voit que sur le sable, c'est que lui seul porte une texture a grains
quasi blancs que le soleil transforme en eclats.** Et il n'existe AUCUN
parametre expose pour retailler l'herbe, la roche ou la neige : seuls `Sand UV`
et `SizeOfGravel` existent. Les retailler demanderait une chirurgie de graphe
sur le maitre -- a n'engager que si un defaut visible le justifie.

### Retailler le sol : l'herbe oui, la roche non (11 septembre 2026)

**Le probleme, chiffre.** Sur la moitie basse d'une vue a hauteur d'oeil en
prairie -- ce que le joueur a devant les pieds -- **51 % de la surface est du
sol nu**, et **59,7 % dans les trois premiers metres**. La vegetation ne le
cache pas. Or ce sol, a la valeur du pack, n'a aucun grain : de larges trainees
vertes.

**OU SE REGLE LE CARRELAGE, et ce qui n'y sert a rien.** Ni `Uv Scale`
(essaye a 0,35), ni `LandscapeLayerCoords.MappingScale` (essaye a 16) ne
pilotent le carrelage des couches, malgre leur nom. Ce sont les **12 noeuds
`TextureCoordinate`**, tous a `UTiling = VTiling = 1`, chacun alimentant une
famille de textures. Les modifier est une simple edition de propriete,
reversible, sans recablage :

    @(-4720,-1666) @(-4608,-1008) @(-4624,-800) @(-4960,-528)  les 4 HERBES
    @(-6625,  976)                                             la ROCHE
    @(-6574,   99) @(-5552, 1872)                              les GRAVIERS
    @(-5504, 3056) @(-5822, 4226)                              le SABLE
    @(-2928,  672)                          carte de HAUTEUR du gravier
    @( 3948, 1760) @( 3964, 2064)           bruits de DISTRIBUTION des fleurs

Les quatre noeuds d'herbe ne remontent a aucune texture depuis le materiau :
leurs textures sont dans `MF_Grass`. On les reconnait en suivant leurs
connexions jusqu'a l'appel de cette fonction.

**NE PAS TOUCHER aux trois derniers** : la carte de hauteur du gravier sert au
MELANGE des couches, et les deux bruits de distribution decident d'OU poussent
les fleurs. Les retailler change le motif du monde, pas sa finesse.

**RESULTAT : herbe a 4, tout le reste a 1.**
- Herbe x4 : energie haute frequence **3,29 -> 4,41 (+34 %)**, aucun artefact.
  (La periode d'autocorrelation, elle, reste a 100 cm : elle mesure le grand
  motif de melange, pas le grain. Mauvaise metrique pour cette question.)
- **Roche x4 : MOIRE HEXAGONAL tres visible sur les pentes lointaines.**
  Verifie par A/B au meme cadrage : le motif apparait a 4, subsiste a 2,
  disparait a 1. La roche reste donc a la valeur de l'auteur. Serrer le
  carrelage monte la frequence spatiale, et les mip-maps d'un Landscape ne la
  rattrapent pas a distance.
- Graviers remis a 1 par prudence : petites surfaces, benefice nul, meme risque.

**REGLE** : apres tout resserrement de carrelage, CONTROLER LES PENTES
LOINTAINES, pas seulement le sol sous les pieds. Le gain est proche, le defaut
est loin.

### Couverture du sol, mesuree proprement (11 septembre 2026)

**METHODE, car la precedente etait faussee.** Mesurer "le sol nu" par la
douceur locale de l'image ne vaut RIEN des qu'on retouche la texture du
terrain : rendre le sol plus fin le rend moins lisse, et le compteur baisse
sans qu'un brin d'herbe ait pousse. La mesure juste est un A/B :

1. `Animate Time of Day` est deja a False, mais **`Cloud Speed` vaut 0,35** :
   les nuages defilent entre deux sessions et polluent tout. Le mettre a 0.
2. PIE A, semis actif -> capture. PIE B, `vegetation.set_runtime_enabled(...,
   False)` -> capture, meme `PlayerStart`.
3. Difference A-B au-dessus du bruit residuel (estime sur le CIEL, ou il n'y a
   aucune vegetation) = couverture du semis PCG.
4. Dans B, separer l'herbe du Landscape du sol par la couleur, avec un seuil
   CALIBRE sur des echantillons reels : un brin mesure **G-R = +28**, le sol nu
   **-3**. Couper a +12.

**RESULTAT en prairie, a hauteur d'oeil :**

                          sous l'horizon   3 premiers metres
    semis PCG                   16,3 %          15,6 %
    herbe du Landscape          39,6 %          30,5 %
    total couvert               55,9 %          46,1 %
    sol nu                      44,1 %          53,9 %

**L'HERBE DU LANDSCAPE MARCHE, et couvre 2,5 fois plus que le semis PCG.** La
note plus haut ("la porte est fermee par construction" via le `Floor()` de
`LandscapeGrassOutput`) est dementie par l'observation : semis eteint, le sol
reste couvert de brins denses jusqu'a l'horizon, sans une fleur ni un arbre.
Elle ne coute AUCUNE instance. Cela ne change rien a la decision du
proprietaire de ne pas y toucher -- mais il faut savoir qu'elle fait le gros du
travail, et ne pas l'oublier en comptant la densite de vegetation.

**DECISION : on ne densifie pas.** Le sol nu restant a desormais du grain (voir
le retaillage de l'herbe), la moitie de la surface est couverte, et le fil de
rendu est le goulot a 8,6 ms sur un budget de 16,67 : chaque instance s'y paie,
alors que l'herbe du Landscape est gratuite.

### Chaque `rebuild()` fabriquait un PCGWorldActor de trop (11 septembre 2026)

`rebuild_world.pcg_world_actor()` posait et armait un acteur, puis
`vegetation.build_world()` creait le volume de vegetation -- et PCG fabriquait
alors SON PROPRE `PCGWorldActor`, desarme. Le niveau finissait avec deux
acteurs ; PCG n'en interroge qu'un, et quand c'est le desarme qui repond **le
semis ne produit plus une seule instance, ni en editeur ni en PIE**, sans le
moindre message. Corrige : la fonction detruit desormais les surnumeraires en
gardant celui qui est deja arme, et `rebuild()` la rappelle APRES le semis.

### Silhouettes de végétation : ce que les packs contiennent vraiment (13 septembre 2026)

Quatre biomes étaient « habillés par emprunt » : toundra, taïga, savane, marais.
Inventaire fait AVANT de promettre quoi que ce soit — 273 maillages balayés :

**IL N'EXISTE NI ROSEAU, NI NÉNUPHAR, NI ACACIA, NI MOUSSE, NI LICHEN, NI
CACTUS** dans les trois packs. Ces silhouettes-là ne peuvent pas être créées, il
n'y a qu'à mieux choisir parmi les 141 maillages employés et la soixantaine qui
dormaient (4 brindilles, 5 branches, 3 buissons colorés, 3 troncs couchés,
2 souches, 2 champignons, 1 arbre sombre, une trentaine de rochers).

**UNE AFFIRMATION DE NOTRE PROPRE DOCUMENTATION ÉTAIT FAUSSE.** `ETAT_DES_LIEUX`
disait « taïga : deux conifères seulement ». Elle en a **neuf** — 3 LowPolyForest,
4 Stylized_PBR, 2 StylizedForest. La note était périmée. **Vérifier l'inventaire
avant de traiter un manque signalé par une note.**

**LA MÉTHODE QUI RENSEIGNE : regarder où un maillage est DÉJÀ employé.** Son nom
ne dit pas son allure, son usage si. `SM_Birch_Tree` sert en forêt tempérée, donc
c'est bien un feuillu — et le bouleau est l'arbre de la taïga. `SM_Hero_Tree_1`
sert en désert chaud ET en savane, donc c'est l'arbre sec, notre acacia de
substitution. Les arbres `RED`/`GRN` servent en forêt tropicale sèche, donc ils
ont l'allure qui convient à une savane.

**DÉCISION DU PROPRIÉTAIRE : la toundra n'a plus d'arbres.** Elle se définit par
la limite des arbres, c'est ce qui la sépare de la taïga. Les cinq petits arbres
morts sont retirés.

**ET UNE ITÉRATION À L'IMAGE, parce que le premier choix était mauvais.** J'avais
mis les `SM_Twig` comme arbrisseaux nains : à l'écran ce sont des **bâtons
allongés couchés à plat sur la neige**, qui se lisent comme du bois mort — sans
provenance crédible sur une toundra sans arbres. Remplacés par de vrais buissons
ramenés à 25-50 % (saule et bouleau nains), plus les buissons rouges et jaunes du
pack Green, qui donnent en prime les couleurs d'une toundra d'automne.
**Un nom de maillage ne dit pas comment il se pose : le juger à l'image.**

**LE PIÈGE PCG DU JOUR, ET IL ÉCRIT SUR LE DISQUE.** Relancer `build_world()` sur
un volume encore en `GenerateAtRuntime` a produit **289 `PCGPartitionActor`
PERSISTANTS** sous `Content/__ExternalActors__`, alors que le nouveau volume était
posé dans le bon ordre (déclencheur puis partitionnement).

- **La pose elle-même est innocente** : instrumentée pas à pas — spawn, échelle,
  déclencheur, partitionnement, `set_graph` — elle donne **0 acteur à chaque
  étape**. C'est le composant VIVANT que l'on détruit qui les laisse derrière lui.
- **Et un comptage synchrone ne voit rien** : PCG ne crée ces acteurs qu'au tick
  SUIVANT. C'est ce qui m'a fait chercher au mauvais endroit.
- **Le remède** : `set_runtime_enabled(label, False)` AVANT de reconstruire.
  Posé dans `build_world()`. Après : 0 acteur persistant, deux reconstructions de
  suite.
- Nettoyage d'un dégât déjà fait : repasser en `GenerateOnDemand`, puis
  `destroy_actors` sur les `PCGPartitionActor`, puis `save_dirty_packages`.

**PIÈGE DE MESURE ANNEXE** : pour juger un biome en PIE, ne pas se téléporter à un
pixel tiré de la carte des biomes sans vérifier son ALTITUDE — le premier essai a
fait tomber le pion dans l'océan. Et la grille de `BP_WorldseedClimat` est en
128×128 : à la frontière de deux biomes elle rend le voisin (biome 13 lu au lieu
de 12). Contrôler `BiomeCourant` avant de conclure sur une capture.

### Sable de plage : teinter par l'ALTITUDE, et le crash que ça coûte (13 septembre 2026)

Demande : « plutôt qu'une plage de galets, une teinte un peu plus jaune pour le
sable de plage ». Bonne idée, mais elle ne se règle pas — il faut une greffe.

**LE MATÉRIAU NE CONNAÎT PAS LES BIOMES.** Il ne voit que les dix poids peints,
et plage et désert sont dominés par la **même couche `DesertSand`** — 0,84 contre
0,72. Aucun poids ne les sépare. Et le matériau **n'expose aucun paramètre de
couleur** : `Sand UV`, `SandStrenght`, `HueShift`, `Desatureate`, dont les deux
derniers sont GLOBAUX et teinteraient tout le terrain.

**LE DISCRIMINANT DISPONIBLE EST L'ALTITUDE**, et il est physiquement juste :
une plage est au niveau de la mer, c'est même ce qui la définit. La greffe est
`Lerp(sable, sable × teinte, masque)` avec
`masque = "Palissement plage" × saturate(1 − Z / "Hauteur plage")`. Bonus : les
dunes côtières du désert pâlissent aussi, ce qui est correct.

**BONNE SURPRISE DE STRUCTURE** : le matériau échantillonne ses **dix couches au
premier niveau** (10 `LandscapeLayerSample`), et `T_Sand` y est directement
accessible — l'insertion se fait entre sa sortie `RGB` et le `Lerp` qui la
consomme. Pas besoin de descendre dans les fonctions du pack.

**UN SECOND PIÈGE DE `batch_connect_expressions`, frère du premier.** On savait
qu'une ENTRÉE unique se désigne par la chaîne VIDE et non par `"Input"`. On sait
maintenant qu'**une SORTIE unique aussi** : passer `"Output_0"` — le nom que rend
pourtant `list_expressions` — échoue **en silence**. Mesure : **4 connexions sur
12** à la première tentative, et l'appel ne dit pas lesquelles. Les sorties
NOMMÉES (`RGB`, `Z`) passent bien par leur nom. Diagnostic : relire le graphe et
comparer les liaisons attendues aux liaisons faites.

**PIÈGE DE VÉRIFICATION** : après coup, deux liaisons semblaient manquer parce
que l'export les nomme `'Input'` là où on avait passé la chaîne vide. Elles
étaient bien là. Comparer par l'export, pas par ce qu'on a envoyé.

**ET L'ÉDITEUR EST TOMBÉ, comme annoncé.** Chronologie précise :

    21:39:11  signal de sante : gameThreadStallSeconds = 0
    21:39:13  materiau sauvegarde, 164 861 octets, `is_compiled_ok = True`
    21:39:15  EXCEPTION_ACCESS_VIOLATION dans UnrealEditor-D3D12RHI,
              via RHI/Renderer, « Crash in runnable thread Foreground Worker #1 »

C'est la création des états de pipeline du matériau recompilé, la même signature
que les crashes du vent sur le feuillage. **Le travail était déjà sur le
disque** : la recompilation et la sauvegarde avaient rendu `True` avant.

**CE QUI A PERMIS DE TRANCHER SANS L'ÉDITEUR** : `Saved/VibeUE/Signals/editor-<pid>-health.json`
datait de 28 s avec un blocage à 0, mais `tasklist` ne trouvait plus
`UnrealEditor.exe` — et `CrashReportClientEditor.exe` tournait. **Un signal de
santé frais ne prouve donc pas que le processus vit encore** : il prouve qu'il
vivait il y a quelques secondes. Croiser avec la liste des processus.

Rejeu : `landscape_material.sable_de_plage()`, idempotent par constat.

### Doser une teinte : le multiplicateur qui SATURE au lieu de pâlir (13 septembre 2026)

La greffe de sable de plage posée, il restait à choisir la teinte. **Ma première
valeur allait dans le mauvais sens**, et seule la mesure l'a montré.

**LE PROTOCOLE, parce que l'œil ne suffisait pas.** Trois captures au même
endroit, heure figée à 13 h, ciel dégagé — et surtout **le personnage comme
témoin d'éclairage**, tout ramené à 100 de luminance sur lui :

    sans greffe        R 185,2  V 121,9  B  77,8   saturation 0,580   clarte 128,3
    (1,10/1,12/0,85)     203,1    140,3     76,7   saturation 0,622   clarte 140,0
    (1,12/1,35/1,75)     196,3    142,7    102,3   saturation 0,479   clarte 147,1

**Sans le témoin, la comparaison était fausse** : entre deux captures censées
être identiques, la couverture nuageuse avait bougé et le personnage lui-même
variait de +9 en luminance. Un sable « plus clair » ne prouvait donc rien.

**LA LEÇON DE FOND.** Un multiplicateur par canal ne peut désaturer qu'en
RELEVANT les canaux faibles. Baisser le bleu — le réflexe pour « jaunir » —
**augmente** la saturation : mon (1,10 / 1,12 / 0,85) a porté la saturation de
0,580 à 0,622, soit l'inverse de l'effet voulu. Pour pâlir un sable déjà très
saturé (R 185 / V 122 / B 78), il faut monter le bleu et le vert plus que le
rouge.

**ET LE RÉGLAGE S'EST FAIT SANS RECOMPILER**, sur l'INSTANCE : c'est tout
l'intérêt d'avoir exposé des paramètres plutôt que des constantes. Le matériau
qui fait tomber l'éditeur n'a été recompilé qu'une seule fois.

**PIÈGE ANNEXE, qui a coûté une capture pour rien** : `load_asset` rend `None`
pendant un PIE. `set_material_instance_scalar_parameter_value(None, ...)` ne
lève rien et `get_...(None, ...)` rend `0.0` — on croit donc avoir éteint la
greffe alors qu'on n'a rien fait, et `0.0` se confond avec « pas de surcharge ».
Toute écriture de paramètre se fait **PIE arrêté**, et se relit pour vérifier.

### MegaPlants n'est pas un pack d'arbres, et il rend un mât nu (13 septembre 2026)

Les 29 espèces de `Megaplant_Library` (1 847 assets, **8,6 Go**) sont une
bibliothèque pour le plugin **`ProceduralVegetationEditor`** — un générateur
d'arbres à graphe de nœuds livré avec 5.8, *Experimental*, **désactivé par
défaut**. Activé depuis (`Worldseed.uproject`), il expose ~230 classes `PV*`.

**Ce que contient réellement le pack**, et c'est contre-intuitif :

- les 134 plantes « finies » sont des **`SkeletalMesh` Nanite**, pas des
  `StaticMesh` — notre `PCGStaticMeshSpawner` ne peut donc rien en faire ;
- `Tree_Norway_Spruce_01_A` = **139 849 triangles et 1 082 OS**, mais ce n'est
  que le TRONC : posé en `SkeletalMeshActor`, il rend **un mât nu**, vérifié en
  fil de fer. Les bornes annoncent pourtant 6,6 m de large — elles décrivent le
  squelette, pas de la géométrie ;
- le feuillage est dans les **549 `StaticMesh` de `Instances/`** (`Branch_`,
  `Twig_`, `Decoration_`), instanciés sur les 1 082 os par le système. **Une
  seule branche, `SKM_Branch_Norway_Spruce_02`, fait 607 261 triangles** — quatre
  fois l'arbre entier. Total de la bibliothèque : **39,3 millions de triangles**.

**`ProceduralVegetationGraph` hérite de `PCGGraph`** et `ProceduralVegetationGraphInstance`
de `PCGGraphInstance` : l'assemblage est un graphe PCG joué dans l'éditeur du
plugin. Il n'existe **aucune classe d'acteur ni de composant** pour poser une de
ces plantes dans un niveau — la sortie attendue est un export (`PVExportSettings`).

**PCG SAIT semer du skinné** — `PCGSkinnedMeshSpawnerSettings`,
`PCGSkinnedMeshSelector`, `PCGSoftSkinnedMeshComponentDescriptor` existent. Mais
le sélecteur n'a qu'un `mesh_attribute` : **pas d'équivalent de
`PCGMeshSelectorWeightedByCategory`**, donc tout notre tri par bande de densité
de biome serait à reconstruire.

**Couverture par biome, chiffrée** : flore européenne tempérée. Bien couverts,
forêt tempérée (8,98 % des terres) et taïga (8,25) ; **rien du tout** pour désert
chaud (15,42), savane (11,29) et prairie (5,67) — la bibliothèque **n'a aucune
herbe**, alors que notre tapis pèse 67 % des instances. Elle comble en revanche
des manques documentés : **roseaux** (marais), **greasewood** (désert froid),
**rosier rugueux** (plage).

**DÉCISION : on ne remplace pas.** Cinq raisons indépendantes — échelle
(78 924 triangles de moyenne contre 1 798 pour nos pins), style photoréaliste
contre notre stylisé, 32 % du monde non couvert, pas d'herbe, et la chaîne PCG à
refaire. Le pack reste en place, inactif ; `Content/*` étant exclu par
`.gitignore`, il ne part pas sur GitHub.

### Importer un pack Fab : le point de montage se vérifie sur un témoin (13 septembre 2026)

`strings` ne lit pas la table de noms d'un `.uasset` : impossible d'y trouver le
chemin `/Game/...` attendu. Le contrôle qui tranche est un **témoin déjà
installé et fonctionnel** — `D:\Assets\Fab\UltraDynamicSky` et
`Content/UltraDynamicSky` ont la même arborescence, donc
**`Fab\<Pack>` → `Content\<Pack>`**, sans imbrication.

Importés ainsi : Kobo_Nature, Stylized_Environments, Stylized_Egypt,
Stylized_Forest, Stylized_Rocks(+_Free), DreamscapeMeadows (+SharedResources,
sans DreamscapeTower) et quatre sous-dossiers de Stylized_Village — **3,5 Go**,
2 044 assets, au lieu de 8,25 Go pour les packs entiers.

### Enrichir une couche PCG ne coûte RIEN, en ajouter une coûte tout (13 septembre 2026)

**Le nombre d'instances est fixé par le PAS DE GRILLE de la couche, pas par le
nombre de maillages qu'elle liste.** Ajouter des maillages à une couche existante
donne donc de la VARIÉTÉ À COÛT IDENTIQUE. Mesuré : **344 482 instances avant et
après** l'ajout de 179 maillages, 116,4 FPS, verdict PASS. C'est ce qui permet de
tenir la décision « on ne densifie pas » tout en renouvelant le monde.

**PIÈGE D'ÉCHELLE, payé à l'image : l'échelle est réglée PAR COUCHE.**
`SFP:SM_shrub_01` fait **3,05 m de haut et 3,4 m de large** ; posé dans une couche
de sous-bois dont les autres membres font 1,4 m (`SM_Bush`) à 0,3 m (`SM_Fern`),
à l'échelle 0,7-1,3, il a donné **26 exemplaires de 2,4 à 4,4 m dans un rayon de
25 m** — un mur. Retiré. **Avant d'ajouter un maillage à une couche, comparer son
`ApproxSize` à celui des maillages déjà présents** ; le tag `ApproxSize` du
registre le donne sans charger l'asset, avec `Triangles`, `LODs` et `Materials`.

**Et juger la couleur EN PLEIN JOUR.** Sous la pluie, ce mur d'arbustes
jaune-olive rendait **noir** et ressemblait à un défaut de matériau ; tous les
matériaux résolvaient pourtant. Forcer `Time of Day` à 13 h et `Cloud Coverage`
à 1 a montré la vraie couleur, et donc la vraie faute : un jaune saturé au milieu
du vert. Nos arbres, eux, restent très verts — c'est le rebond de la canopée
déjà documenté, assumé par le propriétaire.

**`_mesh` accepte désormais un sous-dossier** (`RACINE:SousDossier/Nom`) :
Stylized_Rocks range ses 26 rochers dans 26 dossiers, ce qui aurait demandé
26 racines. Sans `/`, le comportement est celui d'avant, au caractère près.

**Contrôler les références AVANT de reconstruire** : `does_asset_exist` sur les
257 références du fichier de recettes, PIE arrêté, coûte 35 ms et évite un semis
qui perd des maillages en silence. `foliage_lods.audit()` a de son côté trouvé
les **4 palmiers Kobo à LOD unique pour 7 768 triangles**, seuls maillages neufs
sans LOD.

### La palette : neuf familles visuelles, et on n'en garde que deux (13 septembre 2026)

Signale : « il y a enormement de style graphique different ». C'etait juste, et
la cause n'etait pas celle qu'on croyait.

**MEGAPLANTS N'EST PAS DANS LE MONDE, et n'y a jamais ete** : aucune racine
`Megaplant` dans les recettes. Le desaccord ne venait donc pas d'un melange
realiste / stylise mais de **neuf familles STYLISEES de dessins differents** --
le low-poly a facettes d'Orasot, le PBR stylise de Stylized_PBR_Nature, le peint
a la main de Stylized_Village, le plat et sature de Kobo. Trois d'entre elles
portaient 83 % du semis (PBF 36,2 %, SVF 20,7 %, Orasot 26,6 %).

**ET UNE VARIANTE REALISTE N'EST PAS REALISABLE.** Confirme au-dela de la note
precedente : `ProceduralVegetation` n'expose **rien** a Python -- ni graphe, ni
noeuds, ni export, et il n'est pas un `PCGGraph` malgre l'heritage de
`ProceduralVegetationGraph`. `PVExportMeshType` connait pourtant `STATIC_MESH`,
mais c'est un noeud a l'interieur d'un graphe hors de portee. Et il n'existe
aucune autre vegetation realiste sur le disque : les 20 dossiers de
`D:\Assets\Fab\` sont tous stylises sauf MegaPlants.

**`Tools/UE/palette.py`** filtre les recettes par pack. Deux fichiers, et c'est
le point a comprendre :

    vegetation_recipes.complet.json   le CATALOGUE, les neuf familles (413 maillages)
    vegetation_recipes.json           les recettes ACTIVES, vue filtree (237)

Les deux sont versionnes, donc un clone frais retrouve le catalogue entier ET la
palette en vigueur. `appliquer()` part TOUJOURS du catalogue : enchainer deux
palettes ne cumule pas les filtres. La cle `_palette` inscrite dans les recettes
actives dit laquelle est en vigueur.

**MESURE QUI COMPTE : le nombre d'instances ne bouge pas d'un iota d'une palette
a l'autre -- 344 482 dans les deux cas.** C'est le pas de grille qui le fixe, pas
la liste de maillages. Changer de palette ne change donc QUE le dessin : 86
maillages distincts en « origine » contre 139 en « tout », a cout egal.

**ORASOT SEUL EST IMPOSSIBLE, et c'est la surprise.** Filtre a ce seul pack, la
**calotte glaciaire se retrouve sans aucune vegetation**, et la toundra, le
desert froid et l'alpin perdent leur tapis : tous empruntaient a
Stylized_PBR_Nature. `simuler()` le dit avant d'ecrire quoi que ce soit, et
`appliquer()` refuse une palette qui laisserait un biome nu. La palette la plus
homogene atteignable est donc a DEUX familles, pas une.

**DECISION DU PROPRIETAIRE, 13 septembre 2026 : palette « origine »**, soit
Orasot + Stylized_PBR_Nature. Les huit packs Fab restent sur le disque et dans le
catalogue ; ils reviennent en une ligne (`palette.appliquer("tout")`). Ce qu'on
perd sciemment : la mousse, la toundra neigeuse et les variantes saisonnieres
importees le matin meme.

**Piege de methode, utile : une rupture de style ne se juge pas sous la pluie.**
Le premier constat avait ete fait par ciel couvert, ou tout s'aplatit en vert
sombre. Forcer `Time of Day` a 13 h 30, `Cloud Coverage` a 1 et `Cloud Speed` a
0 rend les deux captures comparables -- c'est la que le rocher facette et les
cerisiers roses sautent aux yeux.

### Doctrine de placement : chaque plante a son endroit (13 septembre 2026)

Signale : « ne pas essayer de mettre des fleurs des qu'il y a de l'herbe mais
plutot preter attention au relief, au biome et a ce qui existe deja ».

**LE DEFAUT N'ETAIT PAS DANS LE CHOIX DES BIOMES.** Contre-verifie par un
tableau type x biome : les fleurs n'etaient que dans trois biomes, les
champignons que dans les forets. Le defaut etait qu'**A L'INTERIEUR d'un biome,
tout etait seme uniformement sur une grille reguliere** -- ni clairieres, ni
bords, ni touffes.

**DEUX NOEUDS PCG DEBLOQUENT LE PROBLEME, et ils sont pilotables depuis Python :**

- **`PCGNormalToDensity`** ecrit `dot(normale du point, verticale)` dans la
  densite, soit exactement **`cos(pente)`**. Un `PCGDensityFilter` derriere
  devient donc un filtre de PENTE, sans passer par un selecteur d'attribut.
  La borne basse du filtre vient de la pente MAXIMALE -- le cosinus decroit.
- **`PCGSpatialNoise`** en Perlin 2D ecrit un bruit COHERENT EN ESPACE dans la
  densite : son `value_target` vaut `$Density` par defaut, verifie par
  `export_text()` qui rend `PCGBegin($Density)PCGEnd`. Un filtre derriere ne
  garde que les bosses, c'est-a-dire des TACHES. L'echelle de sa `transform`
  donne le diametre du motif.

**OU LES PLACER DANS LA CHAINE, et pourquoi.** Apres le tri par biome, qui a
besoin de la densite pour porter l'identifiant, et AVANT `TransformPoints`, qui
remplace la rotation du point par un lacet aleatoire et **detruit donc la
normale**. Mis apres, le filtre de pente ne verrait plus rien.

**Les points du `PCGSurfaceSampler` portent bien la normale du terrain** --
prouve par le fait que les couches reservees au raide (`eboulis`, `falaises`,
bornes 25-90 et 32-90) rendent 514 instances et non zero. Si la normale etait
perdue, la densite vaudrait 1,0 partout et ces bornes ne garderaient RIEN :
c'est le controle a refaire si un doute revient.

**LA MESURE QUI A TOUT RECALIBRE, et l'erreur qu'elle a rattrapee.** Les premiers
plafonds avaient ete poses a l'intuition terrestre -- « de l'herbe jusqu'a
25 degres ». Or la pente **MEDIANE des terres de Worldseed vaut 30,6 degres** :
ce plafond ne gardait que **36,7 % des terres** et l'herbe disparaissait des deux
tiers du monde, ce qui se voyait immediatement sur les versants nus. Un seuil de
pente se lit contre la DISTRIBUTION du monde, jamais contre l'intuition :

    sous 20 deg  25,5 %      sous 32 deg  53,3 %
    sous 25 deg  36,7 %      sous 35 deg  61,4 %
    sous 28 deg  43,7 %      sous 38 deg  70,9 %
    sous 30 deg  48,4 %      sous 45 deg  87,2 %

Apres recalibrage (tapis 38 deg, sous-bois 40, arbres 42) : **340 034 instances
contre 344 482 avant doctrine**, donc a densite quasi inchangee, mais placees.

**TROIS EXTRACTIONS, parce que la COUCHE est l'unite de placement.** On ne peut
pas donner sa regle propre a un type de plante tant qu'il partage sa couche :

- **la mousse et les fleurs etaient dans le TAPIS**, seme au pas de 150 cm, soit
  4 444 pieds a l'hectare. Elles se retrouvaient donc partout, une touffe tous
  les metres et demi ; la mousse pesait a elle seule **13,6 % de tout le semis** ;
- **les champignons etaient noyes dans le sous-bois**, melanges aux buissons ;
- **le bois mort aussi** : mesure a l'image, **cinq `SM_Env_fallen_tree` dans un
  rayon de 12 m**, dont un a 1,8 m du joueur -- 1 600 troncs couches a l'hectare.
  Passe au pas `arbres_epars`, il en reste **un**.

**PLAFOND DE TAILLE PAR ROLE, et la couche COMPAGNE.** L'echelle etant reglee
PAR COUCHE, un maillage dont la taille naturelle est double de celle de ses
voisins ressort au double : une fougere `SFL:SM_Fern` a **4,6 m** et un
`SFL:SM_Bush_2` a **5,6 m** ecrasaient le sous-bois d'une foret temperee. Les
retirer serait du gachis et deshabillerait des biomes entiers. Ils passent dans
`<couche>_haut` : un pas de grille plus large -- ce qui est aussi plus juste, un
buisson de trois metres est plus rare qu'un buisson d'un metre -- et une echelle
calculee pour que le plus gros tombe pile sous le plafond de son role (2 m pour
ce qui rampe, 3 m pour le sous-bois ; arbres, mineral et bois mort exemptes).
Resultat mesure dans 9 m du joueur : canopee 9,3 / 8,1 / 5,4 m, puis le plus
gros du sous-bois a **2,5 m**. Le profil d'une vraie foret.

**Le mineral est exempte de plafond A DESSEIN** : un eboulis melange le gravier
au bloc, et c'est cette dispersion qui le rend credible. Les falaises DOIVENT
faire 80 a 140 m. Un critere fonde sur la mediane de la couche les aurait
condamnees -- c'est pourquoi le plafond est absolu et par ROLE.

**Schema des recettes, deux cles optionnelles par couche :**

    "pente":  [0, 38]                        degres autorises
    "taches": {"taille": 3500, "seuil": 0.5} diametre en cm, part retenue

Sans elles, la couche traverse sans ajouter un seul noeud. Le graphe passe de
175 a 521 noeuds et de 44 a 77 couches ; 115,3 FPS, verdict PASS.

**PIEGE REFAIT, et il est deja note plus haut : ne pas enchainer
`editor_request_end_play()` et un chargement d'asset dans le meme script.**
`build_world()` a rendu « texture de biomes absente » et construit un graphe
vide. Deux appels separes.

### Le cout d'un semis est sa MISE A JOUR, pas son dessin (26 septembre 2026)

Signale : « depuis que nous avons ajoute les roches et le foliage les FPS sont
descendus drastiquement, utilisent-ils le multithreading ? ». La question etait
juste, la reponse est non -- et le multithreading n'etait pas le levier.

**MESURE QUI TRANCHE, et il a fallu trois protocoles pour l'obtenir.** A
l'ARRET, au meme point et sur une scene identique au chiffre pres (3054 chunks,
4 433 118 triangles des deux cotes), 477 257 instances DESSINEES coutent
**2,5 ms** -- 6,82 contre 4,31 -- et le fil de JEU ne bouge pas d'un centieme
(2,04 contre 2,05). EN MOUVEMENT, la meme vegetation coute **42 ms**. Facteur
quatorze entre dessiner et mettre a jour.

**LE BANC MESURE A L'ARRET ; LE JOUEUR, LUI, MARCHE.** C'est tout le piege de
protocole : les quatre premieres mesures donnaient 200 a 218 images par seconde
et ne reproduisaient rien. Le regime reel est le STREAMING ACTIF, ou des chunks
naissent et meurent en permanence -- 9565 en soixante secondes de vol.

**L'ISM PAR CHUNK EST LA REPONSE.** Un ISM GLOBAL par espece oblige le fil de
rendu a retraiter un composant qui porte jusqu'a un million d'instances, a
chaque chunk pose ou relache. A/B a trajet fixe, meme binaire, 60 s de vol :

    par chunk    7,75 ms   129 img/s   fil de rendu  6,73 ms
    global      57,89 ms    17 img/s   fil de rendu 60,73 ms

Le surcout de la vegetation passe de **53 a 3 ms**. Prix : 6 282 composants de
plus, et un semis un peu plus cher (4 934 contre 2 964 ms cumulees).
`-WorldseedIsmParChunk=0/1` rejoue l'A/B.

**UN NOM DE COMPOSANT DETERMINISTE FAIT ATTENDRE LE FIL DE JEU.** Les chunks
s'appelaient `Voxel_L<niveau>_<x>_<y>_<z>`, donc le nom REVENAIT des qu'un chunk
renaissait -- le cas NOMINAL du streaming. `NewObject` devait alors ecraser un
objet dont le fil de rendu n'avait pas fini de liberer les ressources, et le
moteur le dit lui-meme : « Gamethread hitch waiting for resource cleanup on a
UObject ... overwrite took 21.11ms ». Mesure : **263 attentes, 17 811 ms
CUMULEES sur soixante secondes**, dont une de 624 ms. Un compteur monotone
suffixe au nom : **zero**, verifie sur deux passes.

**DEUX PISTES ESSAYEES ET RETIREES, pour qu'on ne les retente pas :**
- *les distances de coupe* des recettes, lues depuis toujours et jamais
  appliquees. Posees sur 120 especes sur 120 : **aucun effet**. Elles agissent
  sur les 2,5 ms de DESSIN, jamais sur les 42 de mise a jour. Et le maximum par
  espece portait la borne a 600 m pour un rayon de semis de 350, donc elle ne
  mordait sur rien -- le garde-fou journalise l'a dit tout seul.
- *`SetUseConservativeBounds` + `SetRemoveSwap`*, qui traitent deux termes en
  O(n) bien reels de `CalcBoundsImpl` : fil de rendu **inchange a 51,82 ms**.
  Les deux visaient le fil de JEU, qui n'etait pas le goulot.

**LA LECON DE METHODE.** J'ai affirme, sur la foi de la mesure a l'arret, que
paralleliser le semis ne rendrait rien -- « 75 ms cumulees ». En marche il en
coute 3 267. **La conclusion etait juste, la mesure qui la soutenait ne l'etait
pas** : c'est le regime, pas le chiffre, qu'il fallait choisir d'abord.

### L'estran est un SUBSTRAT, et il porte sa propre recette (26 septembre 2026)

Signale : « sur les plages le foliage beaucoup moins dense, de l'herbe ne pousse
pas sur la plage, on y trouve quelques roches par contre ».

**LA NOTION EXISTAIT DEJA, ET C'EST CE QUI A RENDU LA CORRECTION COURTE.**
`EWorldseedCover::Beach` -- « une forme, pas un climat » -- couvre 3,9 % des
terres et vit dans `FWorldseedBiomeMap::Cover`, que le semis recevait DEJA. Il
ne lisait que `Index`, le biome, donc une plage heritait du tapis d'herbe de la
foret qui la borde.

**LA RECETTE D'ESTRAN REMPLACE CELLE DU BIOME, elle ne s'y ajoute pas.** Une
plage de desert et une plage de foret tropicale sont toutes deux du sable nu ;
le climat decide de ce qui pousse DERRIERE. L'ajouter comme couche a chacun des
dix-sept biomes aurait recopie la meme liste dix-sept fois.

**LE SUBSTRAT SE LIT AU POINT, PAS AU CENTRE DU CHUNK** -- contrairement au
biome, et c'est delibere : une bande littorale fait de l'ordre de 37 m quand un
chunk en fait 32. Trancher au centre donnerait un trait de cote en MARCHES
D'ESCALIER de trente-deux metres. Le biome, lui, varie a l'echelle du climat et
supporte tres bien la maille du chunk.

Mesure : **30 057 -> 25 585 instances**, la garde substrat rejetant **3,8 %** des
points -- ce qui recoupe les 3,9 % d'estran releves. Vu a l'image : sable nu au
bord de l'eau, galets isoles a 17, 36, 54 et 60 m, la dune gardant sa vegetation.

**RESTE OUVERT** : les PAROIS ne lisent pas le substrat, donc un pan de falaise
peut encore apparaitre sur l'estran ; et la recette d'estran est la meme partout,
donc une plage de toundra porte les memes galets qu'une plage tropicale.

### L'habillage du sol n'est plus un choix (26 septembre 2026)

Decision du proprietaire : des six habillages proposes par `L_Menu` -- couleurs
de biome, Dreamscape, Village, Egypte, Melange, Orasot -- **seul Orasot reste**.
C'est le seul qui pose des MAILLAGES, les pans de falaise, et celui
qu'accompagne tout le travail d'habillage.

`WorldseedTexturePack.h/.cpp` est supprime en entier, avec le selecteur, le
champ transporte par le monde, `AppliquerHabillageForce` et
`-WorldseedHabillage=`. **`EWorldseedTerrainColouring` survit a dessein** : c'est
un mode de VISUALISATION -- poids de couches, couleur de biome, pack de textures
-- qui sert au diagnostic.

**LA PALETTE DE VEGETATION SUIT LA MEME LOGIQUE, et le README le disait a
l'envers.** Il listait Dreamscape, Village et Egypte comme necessaires et
rangeait `Orasot_Bundle` parmi les packs qui « ne servent plus ». La palette en
vigueur est `orasot-pur` : les 217 maillages viennent tous de ce pack.

**NON-REGRESSION** : 91 tests au vert avant comme apres le retrait, et le rendu
inchange -- 11,9 % de pixels differents pour un ecart moyen de 6,3 sur 765,
**tres en deca des 82 %** que ce depot mesure entre deux lancements REPUTES
IDENTIQUES sous eclairage.

### Le dessus noir des pans : deux hypotheses tuees (27 septembre 2026)

Signale : « le dessus est noir mais devrait plutot etre vert selon moi car nous
sommes dans une foret tropicale humide ». Le diagnostic n'est pas termine, mais
deux pistes sont FERMEES et il ne faut pas les rouvrir :

- **« `M_WorldseedBiome` n'emet pas vers la RVT » : FAUX.** Le materiau porte
  bien un `RuntimeVirtualTextureOutput`, et il est CABLE -- `VertexColor` vers
  la couleur, `WorldPosition` vers la hauteur. Les deux RVT du pack sont posees
  au lancement (`RVT_Landscape_Material` a 12,2 cm par texel,
  `RVT_Landscape_Height` a 97,7 cm).
- **« le pan ne lit pas la RVT » : FAUX.** La chaine
  `MI_WorldseedParoi -> MI_Cliff_2 -> MI_Cliff_1 -> M_Master_Cliff_Mat` aboutit
  a un maitre de 110 expressions qui echantillonne `RVT_Landscape_Material`
  exactement une fois.

**LES ENTREES D'UN NOEUD DE SORTIE RVT SONT PROTEGEES EN LECTURE DEPUIS
PYTHON** (`Property 'Specular' ... is protected and cannot be read`), et
`MaterialService.export_material_graph` n'existe pas dans ce build de VibeUE.
Ce qui marche : lister les expressions avec leur POSITION dans le graphe
(`material_expression_editor_x/y`) -- sur vingt-quatre noeuds, la topologie se
lit d'un coup d'oeil.

**LE PROCHAIN CONTROLE EST CELUI QUE LE DEPOT S'IMPOSE DEPUIS SEPTEMBRE :**
`ShowFlag.Lighting 0` sur un pan cadre. Si le dessus reste noir, c'est la
couleur -- donc la RVT ; s'il devient gris uniforme, ce sont les normales ou
l'eclairage. Il n'avait jamais ete fait faute de pouvoir cadrer un pan.

**PIEGE D'OUTILLAGE : le compilateur MSVC a rendu DEUX `C1001` transitoires**
dans cette session, dont un sur `WorldseedProbeMonde.cpp`, un fichier que
personne n'avait touche. Relancer la compilation suffit. Le controle qui
tranche entre une ICE transitoire et un vrai defaut est l'HORODATAGE du binaire
apres la seconde passe.

### Un ProceduralMeshComponent ne peut PAS alimenter une RVT (27 septembre 2026)

Le dessus des pans de falaise rendait une masse NOIRE, signalee en jeu. La
cause n'etait ni le materiau des pans, ni le cablage de la sortie RVT du sol --
deux pistes ouvertes puis fermees par la mesure. Elle est structurelle.

**LE FAIT, RELEVE DANS LA SOURCE DU MOTEUR.** `FProceduralMeshSceneProxy`
n'implemente pas `DrawStaticElements` et ne contient pas une seule occurrence
du mot `RuntimeVirtualTexture`. Or la passe RVT se batit a partir des lots
STATIQUES. Poser `RuntimeVirtualTextures` sur un tel composant **compile,
s'applique proprement, et PERSONNE ne le lit** -- le champ vit sur
`UPrimitiveComponent`, donc rien ne signale l'erreur. La RVT restait vide, et
une RVT vide rend du noir.

Cela explique aussi, retrospectivement, pourquoi les dix-neuf substitutions
`SansRVT` du depot ont toujours ete necessaires.

**LES DEUX PISTES FERMEES AVANT, pour qu'on ne les rouvre pas :**
- *« `M_WorldseedGround` n'emet pas vers la RVT »* : FAUX, il porte bien un
  `RuntimeVirtualTextureOutput`, cable ;
- *« le pan ne lit pas la RVT »* : FAUX, la chaine `MI_WorldseedParoi ->
  MI_Cliff_2 -> MI_Cliff_1 -> M_Master_Cliff_Mat` echantillonne
  `RVT_Landscape_Material` exactement une fois.

**UN DEFAUT REEL TROUVE EN CHEMIN, ET C'EST LE MEME QUE CELUI DU MAILLEUR.**
`URuntimeVirtualTextureComponent` arrive en mobilite STATIQUE ; le deplacer
APRES `RegisterComponent` est refuse, et le moteur le dit -- « Mobility of ...
Rvt_0 has to be 'Movable' if you'd like to move ». La transform posee juste
apres ne prenait donc pas : le volume gardait l'origine et l'echelle un, soit
un cube d'UN CENTIMETRE, pour une RVT censee couvrir 64 x 32 km. Le journal
disait pourtant « 524288 texels sur 64 x 32 km » -- un chiffre calcule depuis
nos propres arguments, donc juste alors meme que rien n'avait bouge. **On relit
desormais la transform au lieu de la supposer**, et l'on crie si elle ne fait
pas la taille du monde. Meme famille que « un repli journalise ressemble a une
mesure ».

**LE TEMOIN QUI A TRANCHE, ET CE QU'IL A PROUVE EN PLUS.** Un plan statique de
600 m portant un MAGENTA FRANC, pose en l'air a dessein -- enterre, « le dessus
n'est pas magenta » ne se distinguerait pas de « le plan n'existe pas ». Le
dessus des pans est devenu MAGENTA : un ecrivain a lots statiques remplit donc
bien cette RVT. Mais il a prouve plus que la premisse -- **l'ecrivain n'a pas
besoin d'etre le terrain.**

**LE TEMOIN A ETE RETIRE LE MEME JOUR, decision du proprietaire**, selon le
critere du menage du 23 septembre : on garde le levier d'une question OUVERTE,
on retire celui d'une question CLOSE. Celle-ci l'est, et la nappe RVT est batie
sur sa reponse -- si la RVT redevenait noire, on deboguerait la nappe, qui est
desormais l'ecrivain de PRODUCTION, et non un temoin. `-WorldseedTemoinRvtStatique=`
et `M_WorldseedTemoinRvt` **N'EXISTENT DONC PLUS** ; le bloc se recupere par
`git show` sur ce commit. Ce qui vaut dans six mois est ci-dessus, pas les
quatre-vingt-douze lignes.

Deux pieges que ce temoin avait payes, gardes ici parce qu'ils resserviront a
tout maillage statique pose depuis le C++ : la mobilite STATIQUE interdit de
poser la transform APRES `RegisterComponent` -- il faut la poser AVANT, la
mobilite Movable n'etant pas une option quand ce sont justement les lots
statiques qu'on eprouve -- et un temoin **enterre ne temoigne pas**, « le dessus
n'est pas magenta » ne se distinguant alors pas de « le plan n'existe pas ».

#### La nappe RVT : un ecrivain dedie qui ne se dessine jamais

`WorldseedNappeRvt` pose UN plan de la taille du monde, en
`ERuntimeVirtualTextureMainPassType::Never` -- « Never render to the main pass.
Use this for primitives that only render to Runtime Virtual Texture », dit le
moteur. Il ne bouge jamais, donc ses pages sont calculees une fois et gardees.

**IL NE RECOPIE AUCUNE FORMULE, ET C'ETAIT LA CONDITION.** Les poids des quatre
matieres et la teinte sortent de `WorldseedBiomes::SlotWeights` et
`WorldseedApparence::TeinteNormalisee`, exactement comme le peintre du terrain
voxel ; le melange des quatre textures reste celui de `M_WorldseedGround`,
atteint par un commutateur STATIQUE -- donc gratuit -- qui dit seulement d'ou
viennent les poids : du sommet pour le sol, d'une texture pour la nappe. Deux
melangeurs divergeraient, et l'ecart se verrait exactement la ou le dessus d'un
pan touche le sol.

**LES TROIS AUTRES VOIES, ET POURQUOI ELLES ONT ETE ECARTEES :**

| voie | pourquoi non |
|---|---|
| garder `UseRVT = False` sur les pans | le dessus ne s'accorde jamais au biome |
| la couleur dans les donnees PAR INSTANCE de l'ISM | une seule teinte pour un pan de 77 m, chirurgie de graphe sur le maitre du PACK -- qui a deja fait tomber l'editeur deux fois -- et cela ne repare QUE les pans |
| porter le terrain sur un composant a lots statiques | **le terrain STREAME** : chaque chunk pose ou relache invaliderait des pages de RVT, a redessiner en permanence. Cout non mesure, et le portage complet en prime |

**MESURE, banc au meme point, deux passes de chaque cote :**

    nappe coupee   5,93 puis 5,96 ms   GPU 4,66 / 4,68
    nappe armee    6,18 puis 5,95 ms   GPU 4,88 / 4,72

La seconde paire -- 5,96 contre 5,95 -- dit que **la nappe ne coute rien de
mesurable** en regime etabli. La premiere passe armee porte une trame a
26,18 ms, non reproduite a la seconde et non expliquee. Cout reel : **64 Mo**
de textures transitoires (4096 x 2048, un texel par cellule) et 277 ms de
cuisson, une fois par monde.

**L'A/B A L'IMAGE, meme point, meme cap, meme binaire :**

    nappe coupee   R 80,9  V 92,6  B 114,0   -> B > V > R, bleu-noir
    nappe armee    R 136,0 V 161,3 B 95,6    -> V > R > B, vert
    le sol, dans la meme image                  V > R > B, le meme ordre

**LES DEUX TEMOINS ONT BOUGE, ET C'ETAIT ATTENDU ICI.** Ciel 179 -> 134,
terrain 198 -> 122. Le dessus du pan occupe la moitie de l'ecran : changer sa
couleur change l'auto-exposition de toute la scene. **Les valeurs absolues ne
sont donc pas comparables entre les deux moities**, et seul l'ORDRE DES CANAUX
-- invariant par exposition -- conclut.

**TROIS PIEGES PAYES COMPTANT :**

- **`AppearanceBiome` ne rend JAMAIS `Ocean`.** Elle ne traduit que les
  couvertures `Rock` et `Beach` ; pour toutes les autres, l'ocean compris, elle
  rend le biome CLIMATIQUE, qui est defini PARTOUT -- meme sous la mer, ou il
  decrit la bande climatique de l'eau. Le test terre/mer de la nappe valait
  donc `Biome != Ocean` et annoncait **« 8 388 608 terre / 0 mer »** sur un
  monde a 71 % d'ocean, ce qui rendait sa dilatation du rivage totalement
  inerte. La mer se lit sur la COUVERTURE. **C'est le compte qui a trouve le
  defaut, avant qu'aucune image ne puisse le montrer.**
- **Un heredoc bash casse sur les apostrophes** -- deja consigne, refait. Pour
  ecrire un long fichier C++, passer par l'editeur de fichiers.
- **`get_inputs_for_material_expression` rend les EXPRESSIONS SOURCES**, pas
  des enveloppes de connexion : `conn.get_editor_property("expression")` echoue
  en silence et l'on croit le graphe sans aretes. Le tableau est aligne sur
  `get_material_expression_input_names`. C'est ce qui permet de lire la
  topologie d'un materiau **sans la deduire des POSITIONS des noeuds** -- une
  deduction qui a deja fait conclure de travers dans ce depot.

**L'ALLER-RETOUR sRGB EST DELIBERE.** Les poids sont lineaires, mais un
echantillonneur `SAMPLERTYPE_COLOR` -- celui qu'une texture 2D porte par
defaut -- decode le sRGB. Changer le type d'echantillonneur risque de
desaccorder la texture PAR DEFAUT du parametre et de faire echouer la
compilation ; on encode donc a la cuisson (`ToFColor(true)`) et l'on laisse le
materiau decoder. L'aller-retour est exact a huit bits pres et gagne meme de la
precision dans les valeurs basses.

**CE QUI RESTE OUVERT, ET C'EST LE GAIN SUIVANT :**

1. **Les dix-neuf substitutions `SansRVT` peuvent etre retirees.** Rochers,
   ecorces et bambous les emploient parce que la RVT etait vide et rendait du
   bleu electrique ; elle ne l'est plus. Les retirer leur rendrait le ton du
   sol, ce que le pack a concu. **Non fait, non mesure.**
2. **La nappe n'ecrit PAS dans `RVT_Landscape_Height`**, a dessein : une nappe
   plate y poserait une altitude constante, ce qui deplacerait l'ancrage du
   feuillage partout sans qu'on l'ait mesure. Lui cuire une texture de hauteur
   est la suite naturelle.
3. **La resolution de la nappe est celle de la SIMULATION**, 15,6 m, quand la
   RVT fait 12,2 cm par texel. Le grain vient des quatre textures du pack,
   carrelees a 2 m ; c'est la TEINTE qui est grossiere. Assez pour un dessus de
   pan, a remesurer le jour ou un materiau en demandera plus.

### La chaine RVT ne sert qu'a quatre lecteurs (28 septembre 2026)

Releve exhaustif : il n'existe que **DOUZE expressions de RVT dans tout le
projet**, et quatre seulement sont des LECTEURS de notre RVT de couleur --
`M_Grass` (deux fois) et `M_Master_Cliff_Mat`. Sous les 120 maillages du
catalogue, 66 materiaux distincts, dont **TROIS** lisent la RVT.

**PERSONNE NE LIT LA RVT DE HAUTEUR**, et c'est verifie sur le TEMOIN qui
tranche : la carte de demonstration du pack, `M_5_Bioms_Showcase`, pose 81
materiaux, dont **29 lisent la couleur et ZERO la hauteur**, et ne contient
aucun acteur `VirtualHeightfieldMesh`. Le chantier « ecrire la RVT de hauteur »,
que j'avais annonce comme la suite naturelle, est donc **ABANDONNE** : il ne
changerait rien a l'image, pas meme dans le rendu que le pack est cense
produire. C'est la question du proprietaire -- « combien de materiaux de la demo
lisent la hauteur ? » -- qui l'a tranche, et elle valait mieux que ma reponse.

**UNE NOTE DE CE REGISTRE ETAIT PERIMEE ET M'A FAIT ME TROMPER.** Elle affirme
que « `M_Assets_MasterMat` ne contient aucun `RuntimeVirtualTextureSample` au
premier niveau : il appelle `MF_RVT` » -- et c'est sur elle que j'ai explique au
proprietaire, en trois phrases confiantes, ce que l'ecriture de la hauteur
changerait. Les deux assets EXISTENT bien dans le projet, mais **ni l'un ni
l'autre ne porte la moindre expression de RVT**. Ce qui etait vrai du pack a
l'epoque du Landscape ne l'est plus.

### Ce que la table SansRVT faisait vraiment (28 septembre 2026)

Dix-neuf instances, creees quand le niveau n'avait aucune RVT. La mesure, faite
AVANT de supprimer, a montre qu'elles ne faisaient presque rien :

    DOUZE ne changeaient RIEN, pas un switch. Des copies conformes -- dont les
      quatre MI_Rock_* de Biom_Dark, qui LISENT pourtant la RVT : la table n'a
      donc jamais corrige leur bleu.
    TROIS coupaient `UseTopVertexRVTMask` (les MI_Cliff_* de Biom_Green). Seul
      vrai correctif de RVT, et desormais nuisible.
    QUATRE coupaient `Use Roughness Map` / `Use Specular Map` -- AUCUN rapport
      avec la RVT, ce sont les « textures parasites » du commentaire.
      Arbitrage du proprietaire : le pack a raison.

**LIRE LES SURCHARGES PROPRES D'UNE INSTANCE NE DIT PAS CE QU'ELLE REND.** Mon
premier diff comparait les tableaux de surcharge de l'original et du
remplacant : il rendait « T_Bamboo_1 -> (non surcharge) » sur les dix-neuf, et
j'ai failli annoncer que le remplacement avait perdu toutes les textures du
pack. **Les dix-neuf ont pour parent l'instance d'origine** : ils HERITENT de
tout. La preuve se prend sur la valeur EFFECTIVE
(`get_material_instance_texture_parameter_value`), pas sur le tableau des
surcharges -- et l'en-tete de `ParoiMateriau` portait deja l'avertissement :
« un `get` seul rend `False` aussi bien pour une surcharge posee que pour un
parametre jamais surcharge ; c'est la comparaison AU PARENT qui le prouve ».

**UN DEFAUT LATENT PART AVEC LA TABLE** : son chargeur lisait un `.json` sous
`Content/`, qui n'est pas un `.uasset` et n'entre donc pas dans un build cuit
sans `DirectoriesToAlwaysStageAsUFS` -- absent. La table etait **vide en build
final**, et personne ne l'a jamais su. Le piege est deja consigne pour la police
d'icones ; il vaut pour tout fichier non-`.uasset` lu a l'execution.

### Trois packs rebrasses, et les temoins qui ont evite quatre fautes (28 septembre 2026)

Session de remaniement de l'habillage : l'Egypte rendue aux biomes secs,
LowPolyForest remplace par Stylized_Forest, et un sable propre a la plage. Ce
qui vaut d'etre garde n'est pas le resultat mais les mesures qui ont corrige
la route en chemin.

**LE RATIO HAUTEUR/LARGEUR NE DISTINGUE PAS UN CONIFERE D'UN FEUILLU.** Je
m'appretais a classer `SFT:SM_tree_04` et `_05` comme coniferes sur leur
elancement (2,41 et 2,75). Releve sur des TEMOINS CONNUS : coniferes 1,39 /
1,64 / 1,97 / 2,04, feuillus 1,44 / 1,59 / 1,44. **Les plages se recouvrent**,
le discriminant ne discrimine rien. Une mesure qui ne sait pas classer le cas
dont on connait deja la reponse ne peut pas trancher les autres -- la taiga et
l'alpin gardent donc `SFL:SM_Pine` / `SM_Pine_2`, et l'allure des sept arbres
de Stylized_Forest reste **a REGARDER**.

**UNE CRAINTE ANNONCEE DANS UN CORPS DE COMMIT, PUIS REFUTEE PAR LA MESURE.**
J'avais ecrit que remplacer `LPF:SM_Env_Grass_small` -- quatre triangles --
pouvait couter cher, le tapis pesant 67 % des instances du monde. Mesure :
les tapis passent de 231 a 175 triangles moyens, **-24 % a -43 %**. Les trois
herbes de Stylized_Forest font 40 a 48 triangles et DILUENT `SFL:SM_Grass`,
qui en fait 307 et portait seul le gros du poids. Ajouter des maillages
LEGERS a une couche l'allege, parce que le cout se lit en moyenne PONDEREE et
non par maillage.

**ET LA MEME MESURE A TROUVE UN MUR QUE JE VENAIS DE POSER.** `SFT:SM_stump_02`
fait 3,83 m, soit 4,6 m rendus a l'echelle de la couche, quand les souches
voisines en font 1,5. D'ou `Tools/UE/releve_couches.py`, qui compare deux
etats du fichier de recettes et rend, par couche, les triangles moyens
ponderes ET la hauteur rendue du plus grand. **Les deux pieges de ce depot --
le mur et le cout -- ne se voient que la.**

**`clear_all_material_instance_parameters` EFFACE TOUTE L'INSTANCE.** Employe
pour retirer UNE surcharge de temoin, il a emporte `TexArid`, `TexGrass`,
`TexRock` et `TexMoss` -- soit l'habillage entier du pack. Rattrape par
`git checkout`, l'asset etant sous `Content/Worldseed/`. Pour retirer une
surcharge, reposer la valeur voulue ; pour un A/B, restaurer par git.

**`T_Moss` N'EST PAS UN TEMOIN DE COULEUR FRANCHE.** Pose dans le canal du
sable de plage pour voir OU il mord, il n'a pas rendu du vert : les textures
de ce materiau sont peu colorees, la couleur venant de la TEINTE du biome
qui les multiplie. Le temoin restait utilisable -- la zone a bouge de 111,6
sur 765 quand la falaise bougeait de 20,8 et le ciel de 12,2 -- mais par son
ECART, pas par sa couleur. Un vrai temoin franc demande une texture unie et
saturee, pas une texture du pack.

**UNE CAPTURE NE VAUT QUE PAR SON SUJET, et l'arret `falaise` n'a pas de
plage.** Premiere serie de photos prise au pied d'une falaise cotiere : la
bande claire qu'on y prend pour un estran n'en est pas un, l'estran demandant
une pente DOUCE (`beachSlopeSteepDeg` = 30 degres). Le temoin n'a donc rien
montre, et j'ai failli conclure que la greffe ne marchait pas.

**COMMENT TROUVER UN POINT D'UN BIOME DONNE, sans sonde dediee.**
`probe_carte(graine, hauteur, resolution)` ecrit la carte du monde a plat,
UN PIXEL PAR CELLULE, dans `Saved/Worldseed/Cartes/`. On y cherche la couleur
du biome voulu -- celle du registre, 226/212/172 pour la plage -- en exigeant
des VOISINS de la meme couleur, sans quoi on tombe sur un pixel isole. Puis
`X = (col / largeur - 0,5) * 64000` et `Y = (ligne / hauteur - 0,5) * 32000`,
la ligne 0 etant au SUD. Mesure : 951 plages groupees sur la graine 20260909.
La graine du lancement DIRECT est `FallbackSeed` = 20260909, 32000 m, 2048.

**L'API DES EXPRESSIONS DE MATERIAU EST `MaterialEditingLibrary`, pas une
propriete.** `M.get_editor_property("expressions")` est PROTEGE et
`expression_collection` n'existe pas ; c'est
`unreal.MaterialEditingLibrary.get_material_expressions(M)`, avec
`get_inputs_for_material_expression`, `get_material_expression_input_names` et
`connect_material_expressions`. De quoi cartographier un graphe entier depuis
un commandlet, donc SANS MCP -- ce qui compte, le lien tombant a chaque
recompilation.

**PIEGE POWERSHELL REFAIT : les variables y sont INSENSIBLES A LA CASSE.**
`$a = Moy $A ...` ecrase le chemin `$A` avec le resultat des la premiere
iteration, et les suivantes echouent sur « impossible de trouver une surcharge
pour FromFile ». Deja consigne en septembre ; refait. Ne jamais faire differer
deux variables par la seule casse.

**UNE SECTION HORS DE `biomes` DANS LES RECETTES.** `estran` porte ses propres
couches, et une boucle qui ne parcourt que `biomes` lui retire ses galets --
donc tout ce qui pousse sur la plage. Verifier les cles de premier niveau
avant tout traitement de masse sur ce fichier.

### Le dessus gris-bleu des pans : quatre pistes FERMEES (28 septembre 2026)

Signale de nuit : « je vois un pan dans le loin qui semble briller ». Il ne
brille pas -- son albedo est gris-bleu la ou tout le terrain est chaud, et
l'auto-exposition nocturne fait le reste. Le diagnostic n'est PAS termine, mais
quatre pistes sont closes et il ne faut pas les rouvrir.

**LA MESURE, meme zone et meme critere a chaque fois** -- pixels desatures a
B > R dans la fenetre du versant, `ShowFlag.Lighting 0` pour ne juger que
l'albedo :

    reference (pans + nappe RVT)   1623
    sans les PANS                   117    -93 %   <- ce sont eux
    sans la NAPPE RVT              1597    -1,6 %  <- ce n'est pas elle
    UseRVT arme sur notre instance 1446    cadrage different, NON VALIDE

| piste | verdict |
|---|---|
| l'eclairage, le speculaire, le bloom | **NON** : le defaut survit a `ShowFlag.Lighting 0` |
| la nappe RVT | **NON** : couper son ecrivain ne deplace rien |
| le menage des dix-neuf instances SansRVT du matin | **NON** : `git log` montre que `MI_WorldseedParoi.uasset` n'a ete touche qu'a sa creation |
| armer `UseRVT` sur notre instance | **NON** : les pans restent gris-bleu a l'image |

**LE TEMOIN QUI A PAYE EST CELUI DU PROPRIETAIRE** : « regarder la carte de
reference `M_5_Bioms_Showcase` ». Elle repond sans une seule capture --
inventaire de ses 24 couples maillage/materiau de falaise et de rocher :

    UseRVT = True sur les VINGT-QUATRE, sans exception
    MI_Cliff_2   UseTopVertexRVTMask=True    UseTopLayerRvt=False
    MI_Cliff_1   UseTopVertexRVTMask=False   UseTopLayerRvt=True

**CHAQUE INSTANCE DU PACK ARME L'UN OU L'AUTRE DES DEUX MASQUES DE DESSUS ; LA
NOTRE A LES DEUX A FAUX.** C'est la seule difference structurelle qui reste, et
`UseTopVertexRVTMask` ayant ete essaye sans effet, le suspect restant est
**`UseTopLayerRvt`**, non teste.

**DEUX PIEGES PAYES, TOUS DEUX DEJA AU REGISTRE :**

- **`get_material_instance_static_switch_parameter_value` ne remonte pas
  l'heritage**, et `get_static_switch_parameter_source` rend un `SoftObjectPath`
  VIDE : on ne peut donc pas distinguer « surcharge a faux » de « jamais
  surcharge ». C'est la variante, pour les switches statiques, de
  l'avertissement deja porte par `ParoiMateriau` sur les textures. Le seul
  controle qui tranche est de POSER la valeur et de regarder.
- **Une capture ne vaut que par son SUJET.** L'A/B de `UseRVT` a compare deux
  vues dont la camera n'etait pas a la meme hauteur : le versant lointain, qui
  porte les plaques, n'etait plus dans le cadre. Les -11 % mesuraient le
  cadrage. Ce qui reste valable de cet essai est une observation DIRECTE, et
  non un compte : les pans visibles restent gris-bleu avec `UseRVT` arme.

L'essai a ete ANNULE (`git checkout` sur l'asset) : ce depot ne garde pas une
modification qui ne regle pas ce qu'on voit.

**RESTE OUVERT** : d'ou vient ce gris-bleu, puisque ni la RVT ni son ecrivain ne
le determinent ; et les **plantes blanches du premier plan**, defaut DISTINCT
qui n'a pas ete touche.

### Un materiau non marque « Instanced Static Meshes » rend un DAMIER (28 septembre 2026)

Signale en jeu : dans une oasis, deux palmiers cote a cote -- celui d'Orasot
impeccable, ecorce brune et palmes vert clair ; celui du pack Egypt avec un
tronc GRIS en damier et un ramage plat.

**LA CAUSE, ET ELLE TIENT EN QUATRE LIGNES :**

    M_tree             (tronc du palmier egyptien)   ISM = FALSE   <-- le coupable
    M_plants           (palmes du meme palmier)      ISM = True
    M_Assets_MasterMat (ecorce Orasot)               ISM = True
    M_Master_Leaf      (palme Orasot)                ISM = True

Un materiau non marque `used_with_instanced_static_meshes` **ne compile pas
pour ce cas**, et le moteur lui substitue le materiau par DEFAUT -- le damier
gris. Aucune erreur, aucun avertissement.

**CE QUI REND CE DEFAUT SI DIFFICILE A VOIR** : la carte de demonstration du
pack rend PARFAITEMENT, parce qu'elle pose des `StaticMeshActor`, ou le drapeau
n'est pas requis. Notre semis, lui, pose **UN ISM PAR CHUNK** depuis le
26 septembre. Le defaut est donc apparu sans qu'aucun asset du pack ne bouge,
et il est invisible partout ailleurs que chez nous.

**ET IL EXPLIQUE CHAQUE DETAIL DU SIGNALEMENT**, ce qu'aucune autre piste ne
faisait : le TRONC seul est touche et pas les palmes ; les palmiers Orasot
voisins vont bien ; la demo va bien.

#### Quatre pistes fermees avant, et une hypothese fausse que j'ai defendue

| piste | verdict |
|---|---|
| la demo assigne des materiaux que nous n'avons pas | **non** : elle pose ZERO override, elle rend les defauts du maillage |
| notre semis ecrase le materiau du tronc | **non** : il ne pose aucun override non plus |
| la RVT, comme pour les pans de falaise | **non** : `M_tree` ne porte AUCUNE expression de RVT |
| une texture manquante ou corrompue | **non** : 2048x2048 et 1024x1024, sRGB, mips, bien assignees |
| deux `SM_tree_01` homonymes, on charge le mauvais | **non** : le resolveur construit le chemin COMPLET, et l'autre n'est semé nulle part |

**J'AI DEFENDU LE POOL DE TEXTURES, ET C'ETAIT FAUX.** L'argument etait
seduisant -- le tronc porte 2048x2048 contre 1024 pour les palmes, donc il
serait le premier sacrifie quand le pool deborde, ce qui expliquait « in-game
oui, demo non ». Il n'expliquait NI le damier NI pourquoi le tronc seul.
**Une hypothese qui explique une partie des faits et pas les autres n'est pas
une hypothese faible : c'est une hypothese fausse**, et il faut la traiter
comme telle au lieu de lui chercher des circonstances.

#### Le balayage exhaustif, et pourquoi il fallait partir des RECETTES

Un premier balayage ne cherchait que « palm » et « tree » dans les NOMS de
maillage : il n'a rien trouve. Reparti des recettes -- donc de ce que le semis
pose reellement -- il rend **3 maitres sans le drapeau sur 19**, et le defaut
touche **quatre especes**, pas une :

    M_tree     -> EGP:SM_tree_01                            le palmier
    M_stones   -> EGS:SM_stone_01, _02, _03                 trois pierres

**Chercher par le NOM de l'objet rate ce qui ne porte pas le mot qu'on
cherche.** On part de la liste de ce qui est POSE, jamais d'une intuition sur
les noms.

#### La correction est un SCRIPT, parce que l'asset n'est pas versionne

`Content/Stylized_Egypt/` n'est pas dans le depot -- seul `Content/Worldseed/`
l'est. Une case cochee dans l'editeur serait perdue au prochain clone sans que
rien ne le signale, et ce depot a deja paye cette lecon trois fois (le
PlayerStart qui revient, les acteurs d'eclairage disparus, la grille climatique
qu'aucun script ne rejouait). D'ou `Tools/UE/materiaux_ism.py`, idempotent par
CONSTAT : il lit le drapeau et ne touche qu'aux materiaux qui en manquent.

Il **relit** ce qu'il ecrit -- ce depot a paye qu'une ecriture de propriete de
materiau peut ne rien faire sans rien dire -- et il **ne touche pas aux assets
du moteur** : quand `WorldGridMaterial` apparait dans le releve, le defaut
n'est pas le drapeau mais un maillage dont un emplacement n'a pas de materiau.

**RESTE OUVERT, et c'est ce cas-la** : `SFL:SM_Flower_2` porte
`WorldGridMaterial` a son emplacement [1]. Aucun drapeau ne corrigera cela --
il faut lui assigner un vrai materiau, ou retirer l'espece des recettes.

### Le globe et le sol ne peignaient pas la meme chose (28 septembre 2026)

Signale : « je vois la calotte glaciere au pole nord sur le globe mais pas
in-game, pourquoi ? ». Il y avait DEUX defauts distincts derriere cette
question, et le second attendait au pole sud.

#### 1. Au pole NORD ce n'est pas une calotte, c'est de la BANQUISE

`world_rules.json:27` -- `"northPole": "ocean"`. **Il n'y a aucune terre au
pole nord et il n'y en aura jamais**, par construction, comme l'Arctique. Ce
que le globe peint en blanc la-haut est donc la mer prise en glace :
`EWorldseedCover::SeaIce`, une COUVERTURE et non un biome.

**ELLE N'AVAIT ALORS QU'UN CONSOMMATEUR VISUEL DANS TOUT LE PROJET :**

    WorldseedGlobe.cpp:309    Color = CoverColour(SeaIce)    0,82 / 0,88 / 0,94

Ni la carte plein ecran, ni la minimap, ni le jeu ne la connaissaient --
`AppearanceBiome` ne traduit que `Rock` et `Beach`, tout le reste tombe dans
son `default`. **Il n'existe donc ni geometrie ni materiau de glace de mer** :
en jeu, la surface au pole nord est le plan d'eau du plugin Water, et elle est
bleue. La banquise est une couleur sur une carte, pas un objet du monde.
**DLWE n'y peut rien** : il habille un materiau de SOL, et la-haut il n'y a
pas de sol.

> **LES CARTES LA PEIGNENT DEPUIS LE 29 SEPTEMBRE 2026** -- voir « La banquise
> se voit enfin sur la carte » en fin de fichier. **Le JEU, lui, ne la connait
> toujours pas**, et c'est le chantier qui reste.

#### 2. La VRAIE calotte rendait GRISE, et la teinte ne pouvait pas la sauver

    "cle": "calotte"
    "couleur":  [242, 246, 250]        <- ce que peint le GLOBE : blanc
    "matieres": [0.0, 0.0, 1.0, 0.0]   <- ce que peint le SOL

L'ordre des matieres est *herbe, aride, roche, mousse* : la calotte glaciaire
etait donc peinte **a cent pour cent avec la texture de ROCHE**. Et l'habillage
Orasot n'a AUCUNE texture de neige -- releve, zero asset sous
`Content/Orasot_Bundle`.

**ET LA TEINTE NE RATTRAPE RIEN, ce qui est le point qu'on rate volontiers.**
`TeinteNormalisee` DIVISE par la luminance : un blanc sort a (1, 1, 1), neutre.
C'est delibere -- la teinte transporte la CHROMINANCE, jamais la clarte, sans
quoi elle ecraserait le grain des textures. **Un biome ne peut donc pas etre
blanchi par sa couleur ; il ne peut l'etre que par une MATIERE.**

**LA REGLE GENERALE** : le globe peint des IDENTIFIANTS, le sol melange QUATRE
TEXTURES. Les deux ne peuvent pas s'accorder partout ou l'habillage n'a pas la
matiere correspondante.

#### LA MESURE QUI A DECIDE LA ROUTE

Deux voies s'offraient -- une cinquieme matiere, ou DLWE d'Ultra Dynamic Sky.
Une seule question tranchait : la couverture de neige de DLWE se pilote-t-elle
par SOMMET ou seulement globalement ? Relevee dans le graphe plutot que
supposee :

    DLWE_Snow / DLWE_V3
        Mask Snow/Dust Coverage      FUNCTION_INPUT_SCALAR    optionnelle
        Offset Coverage              FUNCTION_INPUT_SCALAR    optionnelle
        Snow Color and Alpha         FUNCTION_INPUT_VECTOR4   optionnelle
        Mask Below Water Level Height  STATIC_BOOL            optionnelle

Ce sont des SCALAIRES d'entree, donc pilotables par sommet. **Une seule chaine
donne alors la calotte PERMANENTE et la neige METEO**, plus l'accumulation, les
etincelles et les traces de pas.

**LA METRIQUE A ETE VALIDEE SUR UN TEMOIN CONNU**, comme l'exige ce depot : le
registre du 11 septembre affirme que `Apply Snow/Dust` et
`Apply Wetness/Puddles` sont les seules entrees OBLIGATOIRES. Le releve les
ressort exactement comme les seules a `defaut=False`, avec `Material
Attributes`. La lecture est donc fiable.

#### J'AI DONNE UN TABLEAU COMPARATIF FAUX, ET SUR LE POINT QUI DECIDAIT

J'avais ecrit que la cinquieme matiere changerait `matieres` dans le registre,
**donc regenererait tous les mondes en cache**. C'est FAUX, et c'etait
l'argument qui pesait le plus contre elle : la part de neige n'a pas besoin
d'etre un cinquieme POIDS, elle peut etre une PART transportee par sommet,
exactement comme l'estran l'est par UV2.Y depuis le 26 septembre -- et
`PartEstran` n'a jamais touche au registre. Corrige devant le proprietaire, qui
a maintenu DLWE en connaissance de cause.

    REGLE : un tableau comparatif est une MESURE, pas une mise en forme. Une
    colonne fausse y pese autant qu'un chiffre faux dans un releve, et elle
    oriente une decision qu'on ne reprendra pas.

#### UV3.Y, ET POURQUOI IL N'A FALLU NI PORTE NI PARAMETRE

    nappe d'horizon : PlafondCm[i] = FVector2D(enfoncement, 0,0)   <- .Y libre
    chunks du terrain : UV3 = TArray<FVector2D>()                  <- vide

La composante Y du canal etait libre des DEUX cotes. Une seule convention --
**UV3 = (enfoncement de rampe, part de neige)** -- sert donc les deux maillages
sans porte ni parametre pour les distinguer. C'est exactement ce que UV2 fait
deja avec (teinte B, part d'estran).

**ET MON PREMIER RELEVE D'UV ETAIT INCOMPLET.** Il ne comptait que les
`TextureCoordinate` presents dans le materiau et annoncait « UV3 libre », alors
que `MF_WorldseedNappeRampe` lit UV3 **a l'interieur de la fonction**. C'est le
piege deja consigne pour la RVT -- « un echantillonnage peut etre cache dans
une fonction de materiau » -- et il aurait fait ecraser la rampe. Ce qui a
sauve la mesure est d'etre alle lire ce que les APPELANTS posent reellement,
plutot que ce que le materiau declare.

#### LE PIEGE DE LA GREFFE, ET IL ETAIT DANGEREUX

Passer un materiau en `use_material_attributes` **deplace
`WorldPositionOffset` dans le bloc d'attributs**. Ne pas y rebrancher
`MF_WorldseedNappeRampe` recasserait exactement le defaut du 27 septembre --
le decor d'horizon cesse de s'enfoncer et le joueur se retrouve ENTERRE dedans.
Le script le verifie et le journalise ; le jeu le confirme a chaque partie par
« rampe ARMEE et RELUE sur MI_WorldseedGround_Orasot ».

Le `RuntimeVirtualTextureOutput` devait survivre aussi -- je l'ai deja detruit
une fois par un elagage.

#### MA RELECTURE A VALIDE UNE GREFFE INCOMPLETE

La premiere passe a rapporte « greffe posee et relue » sur cinq controles
verts, **alors qu'une ligne disait** :

    !! entree '' introuvable sur MaterialExpressionComponentMask

UV3 n'etait pas relie au masque, donc `Offset Coverage` recevait zero et la
calotte serait restee grise. La cause est le piege documente -- **une broche
d'entree UNIQUE se designe par la chaine VIDE, jamais par son nom** -- et mon
helper cherchait un nom AVANT d'appeler.

    REGLE, et c'est la meme que celle de `build_graph` en septembre : un
    rapport vert ne dit rien des liaisons qu'on ne lui a pas demande de
    verifier. La relecture exige desormais que `Offset Coverage` REMONTE
    jusqu'a un TextureCoordinate d'index 3, et non qu'il soit simplement
    branche a quelque chose.

Le materiau etant sous `Content/Worldseed/`, `git checkout` l'a restaure et la
greffe corrigee a ete rejouee de zero -- ce qui a EPROUVE le script entier au
lieu de le rapiecer.

#### DEUX PIEGES D'OUTILLAGE

- **`-script=` du commandlet ne prend qu'un CHEMIN.** Tout ce qu'on lui accole
  part dans le nom de fichier : « Could not load Python file 'D... ». Le mode
  verification passe donc par une VARIABLE D'ENVIRONNEMENT
  (`WORLDSEED_VERIFIER=1`), seule voie qui traverse.
- **Les proprietes d'un materiau sont PROTEGEES en lecture depuis Python** --
  `base_color`, `normal`, `roughness`, les quinze. Impossible de reconstruire
  le graphe en les lisant. Ce qui marche est
  `get_inputs_for_material_expression` sur les EXPRESSIONS, sortie
  personnalisee comprise : la sortie RVT porte BaseColor, donc elle nomme la
  chaine de couleur. C'est la technique de `sable_de_plage.py`, reutilisee.

#### UNE CAPTURE NE VAUT QUE PAR SON SUJET -- REFAIT

Premiere vue de controle prise a (0, -15000) « vers le pole sud », au juge :
du SABLE et de l'HERBE, latitude -69,6. La calotte de cette graine est a -75 a
-83 degres. Trouvee en ECRIVANT une carte fraiche (`probe_carte`) et en y
cherchant la couleur du registre avec quatre voisins de la meme teinte --
2296 pixels groupes. **Et une carte deja presente dans `Saved/` ne sert a
rien** : celles qui y dormaient dataient d'avant la regeneration du 28.

#### CE QUI EST VERIFIE

- **la calotte est BLANCHE en jeu**, chunks ET horizon lointain, vue a
  (2250, -15750) -- neige avec relief et ondulations, donc bien DLWE et non un
  aplat ;
- **le desert est intact**, vue a (14516, 7984) : sable, palmier a tronc brun,
  herbe. Aucune neige parasite ;
- **128 oracles verts, 0 rouge** ;
- **le nouvel oracle discrimine** : `PartNeige` rendue a zero fait tomber
  « Expected 'UV3.Y vaut un sur la calotte glaciaire' to be 1.000000, but it
  was 0.000000 ». Temoin monte puis retire.

**LE MATERIAU NE GAGNE QUE SIX EXPRESSIONS (53 -> 59), et ce chiffre ne dit
PAS le cout** : DLWE est un APPEL DE FONCTION, ses 388 expressions vivent dans
la fonction. Le prix se paie en instructions de shader, **et il n'a pas ete
mesure**.

#### RESTE OUVERT

- **la banquise du pole nord** -- la question d'origine ; aucune geometrie ne
  la porte ;
- **le cout shader de DLWE**, jamais chiffre, sur des milliers de
  `ProceduralMeshComponent` ;
- **la RVT ne voit pas la neige** : la sortie RVT lit la couleur AVANT DLWE,
  donc le dessus des pans de falaise et le feuillage ne prendront pas le ton
  du manteau blanc ;
- **l'alpin et la toundra n'ont aucune part de neige**, a dessein : leur neige
  est SAISONNIERE et revient a la meteo, qui pilote deja DLWE. Leur donner une
  part permanente les figerait sous la neige en plein ete.

