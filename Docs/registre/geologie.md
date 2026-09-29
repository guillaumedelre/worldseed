# Registre Worldseed — la forme du monde

> **CE FICHIER NE SE LIT PAS EN ENTIER, IL SE CHERCHE.** Il porte le recit des
> seances : ce qui a ete mesure, ce qui a tranche, et surtout **les pistes
> abandonnees**, pour qu'on ne les retente pas. Les REGLES qu'on en a tirees
> vivent dans `CLAUDE.md`, chargé a chaque tour ; ici se trouve la PREUVE.
>
> ```
> grep -n -i '<le mot du sujet>' Docs/registre/*.md
> grep -A40 'le titre exact' Docs/registre/geologie.md
> ```
>
> **Le contenu est CUMULATIF.** Certaines notes decrivent un etat qui n'existe
> plus ; les plus dangereuses portent une marque de peremption. Avant d'agir sur
> une note, verifier que ce qu'elle nomme existe encore — une note qui survit a
> ce qu'elle decrit coute plus cher que pas de note.
>
> **Ce fichier** : tectonique, relief, erosion, sapement, lithologie, strates, cavites, mesas et canyons, arches, cotes, eau, regions et pays.

### Mise a l'echelle du generateur

- **Une constante metrique en dur suffit a fausser un monde entier.**
  `tectonics.py` portait `target = continentBaseM + 400.0` : cette prime
  d'altitude du pole continental n'a pas suivi la division par 4, le pole sud est
  monte a 430 m au lieu de 130, et la calotte glaciaire est passee de 7,1 a
  11,9 % des terres. **C'est le diff de `metrics.py` qui l'a trouvee**, pas la
  lecture du code : toutes les autres metriques collaient a moins d'un point,
  celle-la sautait de 4,7. Sortie dans `tectonics.poleContinentBonusM`. Second
  cas du meme genre : le fondu cotier du detail dans `export.py`
  (`smoothstep(-20, 40)`), sorti dans `world.detailCoastFadeStartM/FullM`.
  **Apres toute mise a l'echelle, comparer un releve AVANT et APRES : c'est le
  seul controle qui voit ce genre d'erreur.** (A l'epoque `metrics.py --diff` ;
  aujourd'hui les sondes C++, dont le releve se lit au journal.)
- **Reduire la resolution de sortie impose de retirer une octave de detail.**
  A 8129 pixels la 6e octave de `detailFrequency` = 160 tombait a 1,6 pixel ; a
  4065 elle tomberait a 0,8, soit sous Nyquist, donc de l'aliasing pur.

### L'eau et le relief final : quatre defauts, une seule cause (11 septembre 2026)

**La cause commune.** Les lacs et les rivieres sont calcules a la resolution de
SIMULATION. Unreal, lui, affiche le relief de SORTIE, qui recoit jusqu'a
`world.detailAmplitudeM` de detail fractal APRES l'hydrologie. Les deux reliefs
divergent donc exactement de l'amplitude du detail, et toute geometrie d'eau
calee sur le premier flotte ou s'enterre dans le second. **Aucun des seize
controles du rapport ne regarde cet ajustement** : un lac dont l'eau s'arrete a
20 m de la berge les passe tous. D'ou `Tools/WorldGen/diag_eau.py`.

- **Le contour des lacs etait trie PAR ANGLE autour du centre de gravite.** Cela
  ne marche que pour une forme en etoile : des qu'un lac a un bras ou une baie,
  un meme rayon coupe le bord plusieurs fois, le tri entremele les points proches
  et lointains, et le polygone zigzague a travers le lac. Signature chiffree : un
  perimetre de 5359 m pour un lac de 66 ha, la ou un disque en ferait 2900.
  Mesure : **42 a 69 % du perimetre surplombait un sol situe plus d'un metre sous
  la surface**, avec des murs verticaux jusqu'a 26 m. Remplace par un suivi de
  contour (marching squares) puis un accrochage de chaque point a la berge
  (`hydrology.lake_shoreline`). Apres : 0,3 a 3,5 %, p95 sous 0,5 m.
- **Ne jamais tronquer une polyligne par `linspace`.** C'est ce que faisait la
  decimation a 64 points : elle jette des sommets au hasard et coupe des baies en
  deux. Douglas-Peucker a tolerance croissante garde les points qui portent la
  forme (`hydrology._reduire`).
- **Le fondu du detail ne connaissait que le niveau ZERO.** Un lac perche a 136 m
  recevait donc le bruit a pleine amplitude : son isoligne de niveau etait brisee
  en **179 morceaux**. Corrige par `export.lake_level_field`, qui donne a chaque
  cellule le niveau d'eau LOCAL ; le fondu se fait sur `big - niveau`.
- **Le detail bosselait le fond des vallees.** Sur le relief final, un cours d'eau
  REMONTAIT de 18,8 m en cumule (mediane), un quart de sa descente totale, pour
  une amplitude de detail de 21,25 m -- le rapport est la preuve. Comme la surface
  d'eau est forcee a decroitre, chaque bosse enterrait tout le troncon aval :
  46,4 % des noeuds sous terre, jusqu'a 21,7 m, et 22,8 % suspendus au-dessus du
  vide. Corrige par `export.river_corridor_damping` (le detail s'efface dans le
  couloir de la riviere) plus un profil borne des deux cotes : la surface ne
  remonte pas de plus de `riverMaxRisePerPointM` et ne passe pas plus de
  `riverMaxSinkM` sous le sol. Apres : **0 % suspendu, enterrement plafonne a
  1,00 m**.

**Deux pieges de methode, payes comptant :**

- **Une moyenne glissante ne franchit pas une marche.** Premier remede essaye sur
  le lit des rivieres : sur une cascade descendant 224 m en 451 m, elle a etale la
  falaise sur 200 m et suspendu l'eau a **+70 m** dans le vide. Abandonnee
  (`riverSmoothPoints` = 1). La bonne reponse etait en amont : ne pas deposer le
  bruit dans la vallee.
- **Mesurer le mur au bon endroit.** Echantillonner le terrain SUR le contour d'un
  masque donne une valeur fausse : le trait passe entre deux cellules et l'arrondi
  tombe une fois sur deux dans l'eau. Et pour une riviere, comparer l'eau au
  terrain de l'axe n'est pas la meme chose que la comparer au fond du lit.
  Echantillonner en BILINEAIRE, et le long des ARETES du polygone -- c'est entre
  deux sommets qu'un polygone grossier coupe a travers le lac.

### L'océan ne faisait que 512 m de côté sur un monde de 8 km (12 septembre 2026)

Signalé : « je ne vois plus l'eau, est-ce que je ne suis pas au bon endroit ? ».
La réponse était non — l'océan était bien là, mais **rendu seulement dans un carré
de 512 m autour de l'origine**.

**LA CAUSE.** Un `WaterBodyOcean` neuf arrive avec `OceanExtents` = 51 200 cm.
C'est cette propriété, et elle seule, qui décide de l'emprise du maillage d'eau.
`water_world.ocean()` posait l'acteur sans jamais y toucher, avec une docstring
qui affirmait « il remplit la WaterZone tout seul ». C'était faux. La `WaterZone`
faisait bien 8,5 km, l'océan 0,512.

**POURQUOI ÇA NE SE VOIT PAS.** Le `WaterMeshComponent` porte un
`far_distance_mesh_extent` de **40 km** avec le matériau `Water_FarMesh` : de
loin, l'horizon est couvert d'eau et tout paraît normal. Ce n'est qu'en arrivant
au rivage qu'on trouve du sable là où il devrait y avoir deux mètres d'eau.

**ET LE PLUGIN MENT DE BONNE FOI.**
`WaterBodyOceanComponent.get_water_surface_info_at_location` répondait
« surface à Z = 0, profondeur 241 cm » à l'endroit exact où l'image montrait du
sable sec. **La surface LOGIQUE était juste, c'est le MAILLAGE qui manquait.**
Interroger le plugin ne suffit donc pas : il faut regarder l'image. Le contrôle
qui tranche est une vue zénithale au-dessus d'un point dont on a vérifié par
`line_trace_single` que le sol est sous zéro.

**CE N'EST PAS LA SPLINE, contrairement à ce qu'on croit spontanément.** La
spline d'un océan dessine **l'ÎLE** — le trou dans l'eau — et non son contour :
`WaterBodyOceanComponent.cpp:223` construit `IslandBounds` à partir de ses
sommets, puis `AddAABBQuadToDynamicMesh` remplit les huit quads autour d'elle.
Notre spline est restée au carré de 200 m par défaut, et c'est sans conséquence.
J'ai perdu quelques appels à la soupçonner.

**LE CORRECTIF EST CELUI DU MOTEUR.**
`UWaterBodyOceanComponent::FillWaterZoneWithOcean()`
(`WaterBodyOceanComponent.cpp:110`) fait exactement
`OceanExtents = WaterZone->GetZoneExtent()`. La méthode n'est ni `UFUNCTION` ni
exposée à Python : on pose la propriété directement. D'où
`water_world.emprise_ocean()`, appelée après `zone()` — l'ordre compte, la zone
doit d'abord avoir été élargie à la taille du monde.

`water_world.verify()` rend désormais `oceanKm` et `oceanCouvreLaZone`.

**Piège de méthode à retenir** : la caméra de l'éditeur était à Z = −122 cm, donc
*sous* le niveau de la mer, et le sol dessous à −241. Avant de chercher un défaut,
lire la position de la caméra : `ViewportService.get_viewport_info()` donne
`location`, `rotation` et `is_realtime`. Ce jour-là `is_realtime` était à False —
séquelle de la recette anti-crash des collections de paramètres, jamais rallumée.

### L'hydrologie a ete RETIREE du generateur (18 septembre 2026)

**Decision du proprietaire.** Rivieres, cascades et lacs ne sont plus produits.
L'ocean reste, confie au plugin Water. Ne pas rouvrir ce chantier sans relire
ce qui suit : c'est l'etat MESURE au moment du retrait, pas un souvenir.

| constat | mesure |
|---|---|
| lits de riviere enterres | **0 sur 1156 points** -- ce point-la etait bon |
| tranchee creusee | **198 m de large en moyenne** (594 au pire) **pour un lit de 7 m** |
| rivage des lacs | terrain sous la surface sur **32 a 62 %** des points, 14,6 m au pire |
| couverture d'eau douce | **12,2 % du monde** (lac 6,0, riviere 6,2) pour une cible ~2 % |
| relief retouche apres le cache | **78 711 cellules abaissees de 1,69 m**, 53,7 m au plus |

**Les deux impasses a ne pas refaire a l'identique :**

- **Le creusement du lit mutilait le relief.** `WorldseedRiverCarve::Apply`
  ecrivait dans `ElevationM` APRES le cache, donc a chaque chargement. Une
  tranchee vingt-huit fois plus large que le lit qu'elle porte n'est pas un
  reglage a affiner, c'est une methode qui ne tient pas.
- **Le trace des rives de lac n'a jamais ete porte a parite avec la version
  Python** (0,3-3,5 % de surplomb la-bas, 32-62 % ici). Les quatre cles
  `shoreline*` de `world_rules.json` n'etaient d'ailleurs lues que par le
  Python : le portage n'avait jamais repris l'accrochage a la berge.

**CE QUI EST GARDE, ET IL NE FAUT PAS LE SUPPRIMER :**

- **`WorldseedFlow`** (et son miroir `worldgen/flow.py`). Le comblement des
  depressions et l'accumulation de flux ne sont PAS de l'hydrologie : c'est du
  ROUTAGE, et l'erosion s'en sert pour son incision par puissance de courant.
  Sans lui, plus de vallees. `FWorldseedFlow::LakeDepthM` porte un nom trompeur :
  c'est la hauteur comblee, pas une nappe.
- ~~**`WorldseedPolyline` et `WorldseedLabel`**, briques geometriques generiques
  gardees sans appelant, en vue d'une reprise eventuelle.~~ **PERIME : les deux
  ont ete SUPPRIMES le 23 septembre 2026** (798 lignes, cinq jours sans
  appelant), et cette entree est restee sous un titre qui dit « il ne faut pas
  les supprimer ». Elle a ete rapportee telle quelle a une exploration du
  26 septembre, qui a conclu que la brique d'etiquetage existait encore.
  `WorldseedLabel::Components` -- composantes connexes a quatre voisins,
  longitude enroulee, pile explicite -- se recupere par
  `git show ba1fc2a^:Source/Worldseed/Procedural/WorldseedLabel.h`.
  **Une note qui survit a ce qu'elle decrit coute plus cher que pas de note.**
- **Les identifiants positionnels.** `EWorldseedCover` (0 None, 1 Ocean, 2 Lake,
  3 River) et les ids de biome (0 ocean, 1 lac, 2 riviere, 18 marais) ne sont PAS
  renumerotes : ils servent de cles a `surfaces.recipes` et aux tables en dur de
  `WorldseedBiomes.cpp`. Ils ne sont simplement plus attribues.

**LE PIEGE QUI TUE L'OCEAN, et il est desormais notre cas nominal.** La
`WaterZone` agrege les bornes en Z de TOUS les corps d'eau en un seul
intervalle, dans lequel la texture d'information normalise chaque hauteur. Un
ocean d'epaisseur nulle donne un intervalle `[0, 0]` **des qu'il est le seul
corps d'eau** -- ce qui est maintenant toujours vrai -- et l'eau cesse de se
dessiner, sans le moindre avertissement. `SeabedM` (le point le plus bas du
monde) lui donne cette epaisseur via `CurveSettings.ChannelDepth` : cette chaine
est PORTANTE, pas defensive. Le controle qui tranche est la ligne de journal
`eau : ... hauteurs d'eau [%.0f .. %.0f] m` -- elle doit valoir [-296 .. 296] et
jamais [0 .. 0].

**Consequences assumees :** le biome **marais** devient inatteignable (il
naissait de l'eau douce dilatee, il pesait 0,01 % des terres) ; la couche
**`Biom4Gravel`** n'est plus jamais dominante (elle ne l'etait que dans les
recettes Lac et Marais, a 0,5 ; ailleurs c'est un accent a 0,04-0,15). Le
rapport du generateur Python signale ce second point : c'est la consequence,
pas un defaut.

**CE QUE LES REGLES SUPPRIMEES AVAIENT APPRIS**, recopie ici parce que la
section `hydrology` de `world_rules.json` est partie avec ses commentaires :

- *Le lit doit etre echantillonne sur le relief de SORTIE*, pas sur celui de la
  simulation : le detail fractal est ajoute APRES l'hydrologie, et un lit cale
  sur la simulation se retrouve tantot enterre, tantot suspendu. Mesure avant
  correction, 1226 noeuds : 46,4 % enterres de plus d'un metre (jusqu'a -21,7 m)
  et 22,8 % flottants (jusqu'a +8,2 m, soit un mur d'eau).
- *La vraie cause etait en amont* : le detail deposait jusqu'a 21 m de bosses
  dans le fond de vallee, au point qu'un cours d'eau REMONTAIT de 18,8 m en
  cumule (mediane), un quart de sa descente.
- *Une moyenne glissante le long du cours a ete ESSAYEE PUIS ABANDONNEE* : sur
  une cascade descendant 224 m en 451 m, elle etalait la marche et suspendait
  l'eau a **+70 m** dans le vide.
- *Le contour d'un lac ne se trie pas par angle autour du centre* : cela ne vaut
  que pour une forme en etoile, et le polygone zigzague des qu'il y a une baie.
  Suivi de contour puis accrochage a la berge ; une marge trop large fait fuir
  le polygone par les cols (20 m de marge : le defaut remontait de 5 a 27 %).

**`fillEpsilonM` et `aridPrecipThresholdMm` ont demenage sous `erosion`** : ce
sont des reglages de ROUTAGE, et l'erosion est leur seul consommateur restant.

**PIEGE DE VERIFICATION, paye comptant.** Pour juger le monde en PIE, ne pas
teleporter le pion loin de sa position : les chunks de terrain se construisent
AUTOUR DU JOUEUR, donc il tombe a travers le vide et se retrouve sous la mer --
mesure, **-1396 m** apres deux captures trompeuses ou tout etait bleu-vert. Le
voile sous-marin donne alors a croire a un defaut de rendu de l'eau, alors que
la surface est exactement a Z = 0. Remede : passer le pion en vol
(`CharacterMovementComponent.set_movement_mode(MOVE_FLYING)` + `gravity_scale = 0`)
avant de se deplacer, et juger de haut.

### La lithologie : de quelle roche est fait le sous-sol (18 septembre 2026)

Arbitrage B7 du proprietaire. **Ce qui gouverne un reseau de grottes, c'est la
ROCHE, pas le climat de surface** : un massif calcaire est karstique sous une
foret tropicale comme sous un maquis. Faire dependre les cavites du biome
reviendrait a laisser une ETIQUETTE decider d'une geometrie, ce que
l'architecture interdit -- l'etiquette ne sert qu'a lier des assets.

Cinq roches au catalogue, chacune avec son aptitude a la dissolution et sa
durete. Trois regles d'attribution, dans cet ordre : la croute **oceanique** est
basaltique (vrai partout sur Terre, aucun reglage) ; un **orogene** expose son
socle, soulevement et decapage ayant emporte la couverture ; le reste est un
**bassin**, dont la roche suit un bruit coherent -- un calcaire poivre au hasard
dans du gres ne donnerait jamais le massif d'un seul tenant dont un reseau
karstique a besoin.

**Calculee depuis la TECTONIQUE et AVANT l'erosion** : les plaques ne bougent pas
quand la surface se creuse, et l'erosion ne transforme pas du granite en
calcaire. **2D pour l'instant**, une roche dominante par colonne.

*(CES CHIFFRES SONT CEUX DU MONDE DE 8 KM ET A CINQ ROCHES. Le catalogue en
compte HUIT depuis le 19 septembre -- craie, dolomie et tuf volcanique
ajoutes -- et le seuil du socle est passe d'une constante metrique a un
quantile. Les valeurs a jour sont plus bas, section « Trois roches de plus ».
On garde celles-ci parce que c'est le releve qui a valide l'ATTRIBUTION a
l'epoque, et que le raisonnement qui suit porte sur elle.)*

| roche | des terres | des mers | altitude moyenne | karst |
|---|---|---|---|---|
| Basalte | 18,17 % | **80,86 %** | 42 m | 0,00 |
| Granite | 30,88 % | 0,07 % | **180 m** | 0,00 |
| Calcaire | 22,98 % | 9,85 % | 37 m | 1,00 |
| Gres | 17,87 % | 5,46 % | 32 m | 0,15 |
| Schiste | 10,10 % | 3,75 % | 35 m | 0,10 |

**LE CONTROLE N'EST PAS LA PART, C'EST LA PLACE.** Des parts seules ne prouvent
rien : un tirage au hasard donnerait les memes. Ce qui tranche est le croisement
avec l'altitude et la mer -- basalte sous l'eau a 81 %, granite le plus haut a
180 m de moyenne, roches de bassin dans les bas pays a 32-37 m. **26,67 % des
terres sont karstifiables** ; sur Terre le karst couvre 15 a 20 % des terres
libres de glace. Cout : **54 ms**. *(Chiffre du monde de 8 km a CINQ roches.
Avec huit roches et le seuil de socle par quantile, il vaut 23,26 % -- voir
« Trois roches de plus ».)*

**DEUX PIEGES PAYES COMPTANT :**

- *Les quantiles portaient sur le mauvais domaine.* J'echantillonnais le bruit
  sur toute la terre continentale, alors que la regle du socle en emporte une
  partie juste apres : les proportions demandees n'etaient pas tenues --
  40,8 / 38,1 / 21,1 % pour 45 / 35 / 20. Corrige en n'echantillonnant que le
  domaine qui restera au bassin : 45,1 / 35,1 / 19,8.
- *Corriger le code de la lithologie N'INVALIDAIT PAS le cache.* Le monde
  revenait avec l'ancienne carte des roches et la sonde rendait le meme chiffre
  qu'avant correction, sans le moindre signe. C'est exactement ce que
  `WORLDSEED_PIPELINE_VERSION` existe pour couvrir -- l'empreinte de
  `world_rules.json` couvre les REGLAGES, ce compteur couvre le CODE. Porte a 7
  ce jour-la ; il vaut **12** au 19 septembre 2026, et chaque cran a une raison
  ecrite dans son commit.
  **Toute correction future de l'attribution des roches doit le bumper.**

**A SURVEILLER** : 18,17 % des terres reposent sur de la croute oceanique, donc
du basalte, a 42 m d'altitude moyenne. Sur Terre les terres emergees de croute
oceanique sont marginales -- l'Islande, Hawai. Ce n'est pas un defaut de la
lithologie, qui lit fidelement la tectonique : c'est le modele tectonique qui
emerge beaucoup de croute oceanique. A reprendre de ce cote-la si le basalte
parait trop present.

**`durete` N'EST PAS BRANCHEE SUR L'EROSION, et c'est delibere.** La brancher
changerait le relief de tous les mondes, donc les rivieres, les biomes et le
point d'apparition. C'est un arbitrage a part entiere, pas un effet de bord de
l'ajout d'un catalogue.

### La passe macro des grottes : la connexite acquise par construction (19 septembre 2026)

Arbitrages B5 a B8 du proprietaire. Le bruit a cretes deja en place produit des
conduits credibles -- 12,35 % des colonnes avec 2 m de libre, verifies en fil de
fer -- mais **rien n'assure qu'ils communiquent, ni qu'un seul debouche a l'air
libre**. Cette passe repond a ce manque, et a lui seul.

**LA CHAINE.** Chambres semees a distance minimale (refus par grille de hachage,
sinon le cout serait quadratique), la ou la ROCHE est karstifiable et la PLUIE
suffisante -- jamais d'apres le biome, qui est une etiquette. Puis un **arbre
couvrant minimal** : il touche tous les sommets par definition, donc la connexite
est acquise et **il n'y a RIEN a verifier apres coup**. Puis quelques aretes
courtes en plus, sans quoi un arbre n'a aucun cycle et le joueur revient toujours
sur ses pas.

**Le controle interne qui rassure** : un arbre couvrant sur 124 sommets a
exactement 123 aretes. Releve : `124 chambres, 141 galeries (18 de boucle)`, soit
123 + 18. Cout : **4 ms**, une fois par monde.

**CE QU'ELLE COUTE A L'EVALUATION, ET COMMENT ON L'EVITE.** Tester toutes les
primitives a chaque voxel serait du O(voxels x primitives). Un index spatial en
XY -- la bande creusable etant mince -- est bati une fois, et **chaque chunk
extrait UNE FOIS la liste des primitives qui le touchent**, elargie du rayon de
raccordement. La liste voyage avec le TRAVAIL, pas avec l'acteur : le fil de
maillage ne doit rien tenir qui puisse mourir avant lui.

**LE PIEGE QUI AURAIT TOUT ANNULE, ET IL EST SUBTIL.** La sortie rapide du champ
de densite ne connait que la portee du BRUIT -- dix-sept metres. Une chambre a
quatre-vingt-dix metres de profondeur serait donc sortie AVANT d'etre evaluee, et
n'aurait tout simplement jamais existe. Le reseau est desormais lu **avant** la
sortie rapide, et le socle force au plein rend `max(-1, air)` et non `-1`.

**L'UNION EST LISSE, ET C'EST UN REGLAGE CONTINU.** `UnionLisse` degenere
EXACTEMENT vers le maximum dur quand son rayon tend vers zero : "un compromis
entre maximum dur et lisse" n'est donc pas un choix binaire mais une valeur de
`cavites.raccordM`.

**MESURE, A/B sur la meme graine et la meme grille**, sondes verticales tous les
5 m sur 400 m de cote :

| | colonnes a repli | vide median |
|---|---|---|
| sans reseau | 805 (13,17 %) | 40,7 m |
| avec reseau | 974 (**15,94 %**) | 45,2 m |

Un cinquieme de colonnes traversables en plus -- mais l'essentiel n'est pas la
quantite, c'est que celles-la sont RELIEES.

**PIEGE DE PROTOCOLE PAYE DEUX FOIS SUR CE MEME A/B.** Mon premier temoin a rendu
des chiffres RIGOUREUSEMENT identiques a la mesure avec reseau -- au centieme.
Cause : **le PIE ne relit pas `world_rules.json`**, seules les sondes le font
(`WorldseedPipeline::ReloadRules`). Changer une regle puis relancer le PIE ne
change donc RIEN, et le temoin mesure la meme chose que le cas teste. Il faut
appeler une sonde entre les deux. Le controle qui tranche est dans le journal :
la ligne `grottes : ...` doit DISPARAITRE quand on desactive le reseau.

**RESTE OUVERT, et ce n'est pas anodin** : une chambre creusee sous une terre
basse se retrouve SOUS LE NIVEAU DE LA MER -- celle qu'on a photographiee est a
-56 m -- et le plugin Water y applique alors son rendu sous-marin, teinte
turquoise comprise. Physiquement ce n'est pas absurde (une grotte sous le niveau
marin est noyee), mais rien ne le decide : c'est un effet de bord du fait que
l'eau est un plan infini a l'altitude zero.

**PAS ENCORE FAIT, et annonce comme tel** : le routage en A* des galeries. Elles
sont pour l'instant des capsules DROITES entre chambres. Le cout du routage --
proximite de la surface, pente praticable, durete de la roche -- est ce qui
donnera des galeries credibles plutot que des tubes tendus.

### Le routage en A*, et le chiffre agrege qui cachait tout (19 septembre 2026)

Fin de l'arbitrage B5. Une capsule DROITE entre deux chambres ignore tout : elle
peut ressortir a l'air libre en franchissant une colline, traverser du granite
comme du calcaire, et monter d'une pente impraticable. L'A* encode ces regles
dans son COUT et n'a plus qu'a obeir -- interdit des que le PLAFOND de la galerie
atteint la surface, tres cher au-dessous, surcout de la roche dure, penalite de
denivele, remise sur une cellule deja empruntee pour mutualiser les troncs
communs. Il tourne sur une grille GROSSIERE bornee a un couloir autour de la
droite.

**RESULTAT : 0,00 % des points de galerie au-dessus du sol**, contre **24,93 %**
avec des capsules droites. 10 805 points echantillonnes. Cout 1,16 s par monde.

**MAIS LA LECON EST AILLEURS, ET ELLE M'A COUTE QUATRE CORRECTIONS INUTILES.**

Le premier releve donnait 22,78 % de points au-dessus du sol APRES routage,
contre 24,93 % sans. Autant dire que le routage ne servait a rien. J'ai alors
corrige, dans l'ordre : l'interdit qui portait sur l'axe de la galerie et non sur
son plafond ; la simplification de Douglas-Peucker qui remplacait un coude
contournant la colline par une corde qui la traverse ; la lecture de la surface
au plus proche voisin la ou le champ de densite lit en bilineaire -- trente et un
metres de cellule, donc des dizaines de metres d'ecart sur un versant ; puis la
largeur du couloir. **Aucune des quatre n'a bouge le chiffre de plus d'un point.**

Les trois premieres etaient de VRAIS defauts et sont gardees. Mais aucune
n'etait LA cause, et c'est en separant enfin les deux populations que tout est
apparu :

| | points | au-dessus du sol |
|---|---|---|
| galeries ROUTEES | 4454 | **0,00 %** |
| galeries REPLIEES sur la droite | 6174 | **39,20 %** |

**Le routage etait parfait depuis le debut.** Les douze liaisons sur
soixante-dix-neuf ou l'A* echoue -- les plus longues, donc les plus fournies en
points -- portaient 58 % de l'echantillon et perçaient librement. Le chiffre
global melangeait un resultat impeccable et un repli defaillant, et aucune
correction du routage ne pouvait le deplacer.

**REGLE A EN TIRER, et elle depasse largement ce cas : quand une correction ne
bouge pas la mesure, se demander d'abord si la mesure MELANGE deux populations.**
Un agregat sur des choses de natures differentes ne se corrige pas, il se
decompose. J'aurais du le faire au premier echec, pas au quatrieme.

**LE REPLI EST DESORMAIS DRAPE.** On ne peut pas abandonner une liaison -- la
connexite est tout l'objet de la passe -- ni se contenter d'une droite. Le repli
abaisse donc chaque echantillon de la droite autant qu'il faut pour que le
plafond reste enfoui. Ce n'est pas un itineraire intelligent, il ne contourne
rien, mais il ne perce plus et il relie : 0,00 % lui aussi.

### Le reseau n'avait AUCUNE entree (19 septembre 2026)

Signale par le proprietaire : « l'entree des galeries doit se faire sur une pente
raide d'une paroi a sa jonction avec le sol ». Il avait raison sur le fond, et
au-dela de ce qu'il visait : **le reseau etait hermetique**. Chambres a vingt
metres sous terre au minimum, routage qui INTERDIT au plafond d'atteindre la
surface, bruit qui s'estompe a vingt-cinq metres du sol. La connexite garantie
etait purement INTERNE -- tout communiquait avec tout, et rien avec le dehors.
Je ne l'avais pas pense, et aucune des mesures precedentes ne pouvait le dire :
elles comptaient des vides, pas des acces.

**POURQUOI L'ESCARPEMENT EST LA BONNE REPONSE, et ce n'est pas une question de
gout** : en penetrant horizontalement dans un versant raide, on gagne de la
profondeur en quelques metres. La meme galerie sur un terrain plat resterait a
fleur de sol sur des dizaines de metres et eventrerait le paysage. La bouche se
place donc sur la cellule la plus RAIDE du voisinage d'une chambre, et s'enfonce
vers l'AMONT. Le troncon de bouche est le seul du reseau qui perce
volontairement : c'est l'ouverture.

Releve : sept entrees, sur des pentes de **36 a 47 degres**, entre 35 et 102 m
d'altitude, et leurs positions sont journalisees -- une grotte qu'on ne sait pas
trouver n'existe pas pour le joueur, et c'est aussi ce que le gameplay voudra
interroger.

**RIEN NE SE CREUSE SOUS LA MER**, decision du proprietaire pour simplifier. La
contrainte est posee AU SEMIS pour les chambres et dans le COUT pour le routage,
jamais corrigee apres coup. **Elle mord beaucoup plus qu'il n'y parait** : une
chambre a quatre-vingt-dix metres de profondeur avec vingt-deux metres de rayon
exige plus de cent dix-sept metres d'altitude, et ce monde est bas. Mesure :
**70 chambres avant, 21 apres**. La bande a donc ete resserree a 12-45 m et le
rayon maximal a 16 m, ce qui redonne **28 chambres et 7 entrees**. C'est le vrai
arbitrage cache derriere « pas de grottes sous la mer » : moins profond, ou moins
de grottes.

**LES BOUCHES EN DOUBLE, corrigees le 19 septembre.** Deux chambres voisines
elisaient la MEME cellule la plus raide : elles cherchent chacune l'escarpement
de leur voisinage, et ces voisinages se recouvrent, si bien que la meme paroi
gagnait deux fois. Constate sur deux entrees a la meme position AU METRE PRES.

**Le point qui fait la difference : on ecarte les candidates PENDANT la
recherche, pas apres.** Refuser a la fin aurait simplement fait perdre l'entree ;
ecarter en cours de route laisse la recherche trouver le SECOND escarpement du
voisinage, qui fait tres bien l'affaire. Mesure : toujours **sept** entrees, donc
aucune perdue, et la paire la plus proche passe a **125 m** pour un minimum exige
de 120 -- aucun doublon sous cinq metres.

### Huit liaisons increusables, et pourquoi il y a PLUSIEURS reseaux (19 septembre 2026)

Neuf liaisons sur trente et une se repliaient faute de chemin. **Le diagnostic a
d'abord consiste a separer deux causes que je confondais**, et elles appellent
des remedes opposes : la file de l'A* qui se VIDE dit qu'aucun chemin n'existe
dans le couloir ; le plafond de noeuds atteint dit que la recherche a manque de
souffle. Releve : **7 sans issue, 2 trop longues**.

- *Trop longues* : plafond porte de 40 000 a 150 000 noeuds. **2 -> 0.**
- *Sans issue* : l'interdit employait le rayon MAXIMAL du catalogue, quatre
  metres, pour toutes les galeries -- alors qu'une galerie donnee en fait 1,5 a
  4. On barrait des passages qu'un tunnel etroit franchit. Corrige : le contexte
  porte le rayon de la galerie en cours. **Mais le compte est passe de 7 a 8** :
  avec plus de souffle, la recherche a PROUVE qu'un de ces chemins n'existait
  pas. Ce n'etait donc pas un manque de moyens.

**DEUX BRICOLAGES ESSAYES ET MESURES COMME PIRES.** Draper la galerie sous la
surface la fait passer SOUS LA MER, ce que la regle venait d'interdire. La borner
au-dessus de la mer la fait PERCER LE SOL : **70,43 % de ses points au-dessus du
terrain**, et 4062 troncons au lieu de 297. Le second est tellement mauvais qu'il
tranche la question.

**LA BONNE REPONSE EST GEOLOGIQUE : il n'y a pas UN reseau, il y en a
PLUSIEURS.** Deux massifs separes par une baie ont deux systemes karstiques
distincts -- c'est vrai sur Terre. Les liaisons sans issue sont donc ABANDONNEES,
le graphe se separe, et chaque morceau garde sa connexite interne par
construction. Il suffit alors de donner **une entree a chacun** : le nombre
demande devient un PLANCHER de densite, pas un plafond d'accessibilite, et
l'attribution tourne entre les composantes -- sans quoi la plus haute raflerait
toutes les entrees et les autres resteraient murees.

**ETAT FINAL SUR LE MONDE DE 8 KM** : 28 chambres, 31 liaisons dont 8
abandonnees, **5 reseaux**, **7 entrees**, 297 troncons, 466 ms. *(Sur 64 x
32 km : 822 chambres, 944 liaisons, 8 reseaux, 66 bouches, 574 gouffres,
154 dolines, 9 arches, 9,7 s.)* Percement **0,29 %**, soit exactement les
sept bouches, qui percent a dessein. Le repli drape n'est plus jamais employe.

### Arbitrage de la profondeur des grottes (19 septembre 2026)

**Retenu : 20 a 45 m de profondeur, salles de 6 a 16 m de rayon.** 25 chambres,
4 reseaux, 6 entrees, 6 liaisons perdues sur 28.

**DEUX BORNES ENCADRENT CE CHOIX, et l'une manquait.**

- *Le plancher* : une chambre sous le niveau de la mer serait noyee par le
  plugin Water. L'altitude minimale exigee vaut donc
  `profondeurMax + rayonMax + niveauMerMarge`, soit **66 m** ici. C'est ce qui
  cantonne les grottes aux collines, et c'est voulu.
- *Le plafond* : **`profondeurMin` doit depasser `rayonMax`**, sinon le haut de
  la chambre sort du sol et ouvre un puits a ciel ouvert. Ce defaut a EXISTE --
  la configuration en vigueur juste avant cet arbitrage avait douze metres de
  profondeur minimale pour seize de rayon maximal -- et **aucune mesure ne le
  voyait**, le controle de percement n'echantillonnant que les galeries. Trouve
  en preparant les chiffres de l'arbitrage, pas par une sonde.

**LES QUATRE EQUILIBRES, a bornes respectees :**

| profondeur | rayon | chambres | reseaux | entrees | liaisons perdues |
|---|---|---|---|---|---|
| 20-45 m | 6-16 | **25** | **4** | 6 | 6 sur 28 |
| 28-90 m | 6-22 | 21 | 5 | 5 | **4 sur 23** |
| 24-65 m | 6-20 | 22 | **4** | 6 | 6 sur 24 |
| 16-30 m | 5-12 | **29** | 7 | 7 | 8 sur 32 |

**RESULTAT CONTRE-INTUITIF A RETENIR : la bande la PLUS PROFONDE perd le MOINS
de liaisons.** On attend l'inverse -- plus on creuse loin, plus c'est difficile.
Mais une chambre profonde ne peut exister que sur les hauteurs, et la il y a de
la roche partout pour creuser entre elles. Les chambres peu profondes
s'eparpillent jusqu'en plaine, ou la mer interrompt tout : la bande la plus
superficielle morcelle le sous-sol en **sept** systemes independants contre
quatre.

### Gouffres et diaclases : la roche decide de la forme de la cavite (19 septembre 2026)

Deuxieme moitie du chantier des cavites, demandee par le proprietaire :
« je prefere finir avec les infractuosites, (gouffres etc...) lithographie ».
Deux formes ajoutees, et elles ne se choisissent pas -- **la lithologie les
repartit** : la ou la roche se dissout, le karst de la section `cavites` ; la ou
elle ne se dissout pas, des **diaclases**, c'est-a-dire des fractures.

**LE GOUFFRE : LE CRITERE EST A L'APLOMB, PAS ALENTOUR.** Un karst a deux formes
d'entree, et c'est le terrain qui tranche : la bouche s'ouvre a l'horizontale
dans un versant recoupe par une vallee, l'aven s'ouvre a la verticale la ou
l'eau s'infiltre a travers un plateau. Premiere version fautive : je cherchais
d'abord une falaise dans le voisinage de la chambre et ne posais un gouffre que
si je n'en trouvais AUCUNE. Un voisinage de 180 m contient presque toujours un
escarpement -- **mesure : zero gouffre sur six entrees**. Le bon critere est la
pente de la cellule qui surmonte la chambre. Apres correction : **1 gouffre sur
6 entrees, 26 m de haut, plateau a 19 degres**, verifie a l'image et par une
grille de sondages (trou de 6 m de large, 40 m de chute, plateau a 88 m).
Cette rarete n'est pas un defaut de reglage : la pente MEDIANE des terres vaut
30,6 degres, donc le plateau est la forme rare ici.

**LE CONTROLE DE PERCEMENT DOIT S'ARRETER AUX GALERIES.** Le dernier troncon
courait jusqu'a la fin du tableau des segments, donc il avalait les entrees --
qui percent le sol A DESSEIN. Mesure : 0,38 % de points au-dessus du sol la ou
le routage est a 0,00. Meme melange de deux populations qui avait deja fait
regler quatre fois le mauvais bouton. Une borne `FinDesGaleries` prise avant la
pose des entrees le referme.

**LES DIACLASES : UN BRUIT DE WORLEY, ECRIT POUR L'OCCASION** (`Worley3D`, le
projet n'en avait aucun). La difference entre les distances aux deux germes les
plus proches s'annule exactement sur la frontiere de Voronoi : la seuiller donne
un reseau de parois fines, **connexes par construction** puisque les faces d'un
diagramme de Voronoi se touchent toutes. Meme vertu que l'arbre couvrant des
chambres, sans aucun calcul global.
- **L'aplatissement vertical fait toute la difference entre une fracture et une
  bulle** : en comprimant Z avant d'evaluer, les cellules deviennent des prismes
  et leurs parois des plans quasi verticaux.
- **Le fondu de profondeur est l'INVERSE de celui des galeries.** Un karst se
  creuse en profondeur ; une diaclase se referme, parce que la charge sus-jacente
  serre les joints et que c'est la decompression, pres de la surface, qui les
  ouvre. Une fissure de granite est donc une forme de SURFACE -- et elle debouche
  a l'air libre, ce qui lui tient lieu d'entree sans qu'on ait rien a poser.
- **DEUX METRES D'OUVERTURE EST UN PLANCHER IMPOSE PAR LE VOXEL**, pas un gout :
  a 1 m de voxel, le marching cubes ne peut pas representer une fente plus
  etroite que deux voxels. On ne retient que les fissures elargies, ou un homme
  passe -- les seules qui interessent le jeu.

**TROIS MESURES FAUSSES DE SUITE, ET LA MEME LECON A CHAQUE FOIS.**

1. **Un temoin non branche mesure le monde d'avant.** `probe_voxel` ne posait pas
   `SetLithology`, donc `KarstifiableAt` rendait 1 partout, le terme sortait a la
   premiere garde, et le releve annoncait **2,67 ms/chunk contre 2,62 avant
   l'ajout** -- ce qui donnait a croire que le Worley etait gratuit. Branche, le
   vrai chiffre etait **3,65**.
2. **Une sonde qui part du relief MACRO compte de l'air ordinaire.** Elle
   descendait depuis `SurfaceHeightM` et comptait tout vide en dessous ; or le
   champ deplace la surface de plusieurs metres (8 d'amplitude, 25 de
   deplacement horizontal). Elle annoncait **15,85 % de colonnes ouvertes** la ou
   la maille, en jeu, n'en montrait AUCUNE. Le signe qui aurait du alerter : ses
   chiffres n'avaient pas bouge d'un iota quand le masque de zone avait change.
   La version juste cherche le premier changement de signe en descendant -- le
   sol REEL -- et ne compte que dessous.
3. **Mesurer une forme rare au mauvais endroit.** La sonde visait la cellule
   insoluble la plus HAUTE ; comme le masque n'ouvre que 5 % de cette roche, ce
   point avait 95 % de chances d'etre dans une zone fermee, et il l'etait.

**C'EST LE TEMOIN AVEC/SANS QUI A TOUT DEBLOQUE**, et il devrait etre le reflexe :
la meme sonde, deux fois, avec la forme et avec son ouverture a zero. Au mauvais
endroit il donnait **2902 colonnes dans les deux cas, au chiffre pres** -- preuve
immediate que la diaclase ne produisait rien. Au bon endroit :

    AVEC  : 20,37 % des colonnes ouvertes sur 2 m, vide continu 42,5 m
    SANS  :  4,04 %                                vide continu 25,5 m

**UN SEUIL N'EST PAS UNE PART.** Le reglage s'appelait `diaclaseZonePct` et
valait 0,16 ; le code en tirait un seuil en supposant le bruit UNIFORME sur
[-1..1]. Un Perlin ne l'est pas, il se masse autour de zero : **le seuil 0,68
cense garder 16 % n'en gardait que 1,59**. Meme famille d'erreur que les 715 mm
pris pour une mediane. Renomme `diaclaseZoneSeuil`, avec la courbe relevee dans
le commentaire de la regle -- part de la roche insoluble emergee : 0,15 -> 32,53 % ;
0,35 -> 14,44 ; 0,55 -> 5,00 ; 0,68 -> 1,59. Retenu 0,55 ce jour-la, **0,45 aujourd'hui**.

**LE COUT SE PAIE DANS LA GARDE, PAS DANS LE WORLEY.** Le Worley ne tourne que
sur une fraction du volume ; c'est le test qui decide de l'appeler qui s'evalue
partout. En fBm 3D a deux octaves il demandait seize evaluations de gradient ;
un Perlin 2D a une octave en demande quatre et suffit a des taches larges.
**3,65 -> 2,85 ms/chunk, geometrie inchangee au chiffre pres** (8147 colonnes
dans les deux cas) : c'est la signature d'une vraie optimisation.

**PIEGE DE VERIFICATION EN PIE, nouveau et couteux.** Les chunks se batissent
dans un rayon de **250 m autour du pion**, en 3D. Teleporte a 400 m d'altitude
au-dessus d'un sol a 48 m, le pion etait hors de ce rayon : **zero chunk, zero
collision, et les sondages rendaient « rien » partout**, ce qui ressemble
exactement a un terrain qui ne se genere plus. Verifier l'ecart vertical au sol
avant de soupconner le streaming.

### La doline d'effondrement, et le sol de fond corrige au passage (19 septembre 2026)

Premiere des trois formes arbitrees. **LA SEULE FORME KARSTIQUE QUI SE VOIE DE
LOIN** : la bouche s'ouvre dans un versant, l'aven perce un plateau, il faut les
avoir trouves pour les voir. Une doline EST un accident du paysage.

**ELLE NE SE POSE PAS, ELLE SE DEDUIT.** Le plafond d'une salle porte ce qui le
surmonte ; sous une certaine epaisseur il cede, la surface s'affaisse en
entonnoir jusqu'au vide, et les parois s'eboulent jusqu'a leur angle de repos --
d'ou une ouverture plus LARGE que la salle. Le critere est `profondeur - rayon`,
et rien d'autre. Profil en entonnoir, exactement l'inverse du profil en cloche
de l'aven : les deux formes se distinguent d'un coup d'oeil pour cette seule
raison.

**COMPTEE A PART DES ENTREES, ET C'EST LA LECON DU GOUFFRE APPLIQUEE.** L'aven
avait d'abord ete fabrique DANS la boucle des entrees, donc plafonne par leur
budget : six pour tout le monde quoi qu'il arrive, et la forme n'existait pour
ainsi dire pas. Une doline ne se forme pas parce qu'il manquait un acces, elle
se forme parce que le plafond est mince.

**MESURE**, graine 20260909 : 4 dolines sur 25 chambres, ouvertures de 39 a
58 m, creux de 10 a 14 m, plafonds de 5 a 11 m. Coupe au sondage sur la plus
grande : 74,9 m au bord, 55,8 au fond, 67,2 en remontant, et **au centre le
sondage ne rencontre plus rien** -- le trou s'ouvre sur la salle.

**A SAVOIR** : sur une pente raide la doline se lit comme une ENTAILLE plutot
que comme une cuvette, et ses parois s'ouvrent en plusieurs trous distincts. Ce
n'est pas un defaut -- un effondrement sur un versant ressemble a cela -- mais
les quatre dolines de ce monde sont sur des pentes, donc aucune ne montre la
cuvette franche.

### Trois formes d'ouverture, et elles ne se valent pas (19 septembre 2026)

Deuxieme des trois formes arbitrees. L'aven etait fabrique DANS la boucle des
entrees, donc plafonne par leur budget -- six pour tout le monde quoi qu'il
arrive -- et il fallait en plus qu'aucune falaise ne l'ait devance. Mesure
avant : **UN seul aven dans le monde**.

**LA DISTINCTION QUI STRUCTURE DESORMAIS LA PASSE.** Deux formes se FORMENT
toutes seules, par geologie, et ne doivent rien devoir a un budget :

- **la doline** la ou le plafond est MINCE : il cede, la surface s'affaisse ;
- **l'aven** la ou le plafond est EPAIS mais le terrain PLAT au-dessus : l'eau
  s'y infiltre et dissout un puits.

Les deux criteres sont complementaires et se partagent les chambres sans se
recouvrir -- une chambre dont le plafond a cede n'a pas eu le temps de se
dissoudre. La troisieme forme, **la bouche de falaise, est la GARANTIE** : elle
existe pour qu'aucun reseau ne reste mure, et son compte tient compte de ce que
les deux autres ont deja ouvert.

**MESURE, graine 20260909** (25 chambres, 4 reseaux) :

    avant   6 bouches de falaise   1 aven    0 doline
    apres   1 bouche de falaise    9 avens   4 dolines

**UNE SEULE FONCTION CREUSE LES DEUX PUITS** (`CreuserPuits`) : doline et aven
sont le meme objet -- une chaine de capsules le long d'un axe vertical ondulant
-- et ne different que par le SENS de la variation de rayon. La doline s'evase
vers le HAUT (entonnoir d'effondrement), l'aven vers le BAS (cloche de
dissolution). Les ecrire deux fois aurait fait diverger deux moities qui
doivent rester identiques.

**LA GARANTIE SE VERIFIE, ELLE NE SE SUPPOSE PAS.** La bouche de falaise peut
ECHOUER -- aucun escarpement assez raide dans le voisinage -- et l'echec est
silencieux. Un compte des reseaux sans ouverture est journalise a chaque
generation : `les 4 reseaux ont au moins une ouverture`. Sans lui, on croirait
la garantie tenue parce que le code a tourne.

**LA BOUCHE DE FALAISE GARDE SON PROPRE PLANCHER**, arbitre par le
proprietaire le 19 septembre 2026. Premiere version : le plancher de densite
etait COMMUN aux trois formes, donc les avens et les dolines le consommaient --
6 bouches avant, UNE apres. Or ce n'est pas une ouverture parmi d'autres, c'est
la SEULE des trois ou l'on entre EN MARCHANT ; dans un aven comme dans une
doline, on tombe. Les compter dans le meme budget revenait a traiter comme
interchangeables deux experiences de jeu qui ne le sont pas.

**ETAT FINAL** : 6 bouches, 9 avens, 4 dolines pour 25 chambres et 4 reseaux,
368 troncons, 275 ms, percement des galeries 0,00 %, 2,85 ms/chunk.

### La durete est branchee sur l'erosion, et ca ne change rien (19 septembre 2026)

Arbitrage ouvert par le proprietaire : adapter le relief a la lithologie pour
avoir les bons reliefs aux bons endroits. Le terme est ecrit, il est au bon
endroit, il est mesure -- **et son effet est de quatre a sept centimetres**.

**OU IL ENTRE, ET POURQUOI LA.** Dans la loi de puissance de courant
`E = K . A^m . S^n`, c'est K -- l'erodabilite -- qui porte la resistance du
substrat. Le mettre dans l'exposant ou en post-traitement reviendrait a bricoler
un resultat au lieu de decrire une cause. L'erodabilite est **rapportee a la
moyenne du monde**, ponderee par la surface reellement couverte : une
erodabilite absolue changerait la quantite TOTALE d'erosion, donc l'amplitude du
relief, donc tout le calage terrestre, pour une question qui ne porte que sur sa
REPARTITION.

**MESURE, temoin avec/sans, perte moyenne par roche sur les terres :**

    roche      durete   poids 1    temoin    ecart
    Granite     0,95    +5,37 m    +5,44 m   -0,07   (erode moins : le SIGNE est bon)
    Basalte     0,85    -0,27 m    -0,23 m   -0,04
    Gres        0,55    -0,56 m    -0,58 m   +0,02
    Calcaire    0,45    -0,62 m    -0,66 m   +0,04
    Schiste     0,35    -0,38 m    -0,43 m   +0,05

Les signes sont tous justes. Les amplitudes sont nulles.

**DEUX HYPOTHESES ELIMINEES PAR LA MESURE, dans cet ordre :**

- *le plafond d'incision saturerait et effacerait K* -- le journal disait
  « incision max 1.5 m », soit exactement `maxIncisionPerStepM`. Compte fait :
  **3 cellules sur 7 864 320**, soit 0,00 %. Le plafond n'y est pour rien. (Il
  est desormais mis a l'echelle de la roche quand meme, `MaxStep * K` : un
  garde-fou numerique ne doit pas manger la physique qu'il protege.)
- *l'erosion serait trop faible* -- taux d'incision **quadruple**, de 0,25 a
  1,0 : la perte du granite passe de 5,37 a **6,27 m**. Dix-sept pour cent pour
  un facteur quatre.

**LA VRAIE CAUSE, ET ELLE EST STRUCTURELLE : L'EROSION N'EST PAS CE QUI FAIT LE
RELIEF DE CE MONDE.** Elle en retire cinq a six metres, quoi qu'on fasse, sur
une amplitude de plusieurs centaines. Le relief vient de la TECTONIQUE et de son
bruit a cretes ; l'erosion organise le drainage et arrondit les aretes -- c'est
d'ailleurs ce que dit son propre commentaire, « c'est elle qui rend le relief
ORGANIQUE ». Elle est une passe de finition, pas une passe de sculpture.

Et sans soulevement continu, K ne fait que **reechelonner le temps** : la
relaxation d'un relief sans apport converge vers la meme forme, un peu plus tot
ou un peu plus tard. Les escarpements et les corniches du monde reel naissent
d'un soulevement qui alimente l'erosion pendant qu'elle decape -- les couches
dures tiennent, les tendres sont emportees. Sans cette boucle, il n'y a rien a
tenir.

**CE QU'IL FAUDRAIT POUR QUE CA MORDE** : faire de l'erosion le processus
DOMINANT du relief, c'est-a-dire une boucle soulevement/erosion couplee. Ce
n'est pas un terme a ajouter, c'est la generation du relief a refaire.

**LE TERME EST GARDE, `duretePoids` a 1,0.** Il est physiquement juste, il ne
coute rien, et il sera deja en place le jour ou le relief changera de methode. A
zero, le comportement est celui d'avant, a l'identique.

**ET LA LECON DE METRIQUE, payee deux fois de suite** : ni l'altitude moyenne
par roche ni la perte brute par roche ne mesurent l'erosion differentielle.
Toutes deux sont dominees par la POSITION -- le granite est haut parce que la
regle d'attribution le pose sur les orogenes, et il perd le plus parce qu'il est
raide et arrose. Seul l'ECART AU TEMOIN isole le terme qu'on teste.

### La boucle soulevement / erosion : la roche s'exprime enfin (19 septembre 2026)

Chantier ouvert par le proprietaire apres le constat que la durete branchee sur
une erosion sans apport ne deplacait que quatre a sept centimetres. Decision :
**hybride d'abord** -- le relief tectonique reste la condition initiale, la
boucle le retravaille -- et **qualite d'abord**, le cout sera optimise ensuite.

**LE MODELE.** `dz/dt = U - K.A^m.S^n + D.grad^2 z`. Les trois termes existaient
sauf le premier. Le soulevement vient de la **convergence des plaques**, deja
calculee par la tectonique et que personne ne lisait a ce stade, plus une
composante continentale uniforme. Il ne s'applique qu'au continental : soulever
le plancher oceanique changerait la part des terres a chaque passe.

**POURQUOI IL FALLAIT AUSSI MONTER L'INCISION, et ce n'etait pas evident.** A
l'equilibre `U = K.A^m.S^n`. L'incision d'alors ne retirait que **0,04 m par
passe** : un soulevement capable de faire du relief l'aurait submergee. Les deux
montent donc ensemble, et c'est leur RAPPORT qui fixe l'amplitude -- pas leurs
valeurs. `incisionRateM` 0,25 -> 6,0 et `maxIncisionPerStepM` 1,5 -> 6,0, pour
300 passes au lieu de 60.

**MESURE, pente moyenne par roche -- LA SEULE VRAIE SIGNATURE :**

    roche      durete   temoin   couple   ecart
    Granite     0,95    16,9 deg 25,0 deg  +8,1
    Basalte     0,85    17,9     13,6      -4,3
    Gres        0,55     9,9      9,1      -0,8
    Calcaire    0,45     9,8      9,5      -0,3
    Schiste     0,35     8,7      8,6      -0,1

**L'ecart granite / calcaire passe de 7,1 a 15,5 degres.** La roche dure tient
desormais une pente que la tendre ne tient pas -- c'est exactement
`S = (U / K.A^m)^(1/n)`, et c'est ce qu'on etait venu chercher. Le basalte
BAISSE, et c'est coherent : il est oceanique a 80,9 %, et le peu qui emerge est
marginal et peu souleve.

**NI L'ALTITUDE NI LA PERTE NE MESURENT CELA**, et je m'y suis laisse prendre
deux fois de suite. Les deux sont dominees par la POSITION que la regle
d'attribution donne a chaque roche : le granite est haut parce qu'il est pose
sur les orogenes, et il perd le plus parce qu'il y est raide et arrose. Seule la
PENTE dit quelque chose de la resistance, et seul l'ecart au temoin l'isole.

**CE QUE CA COUTE, mesure a la resolution 256 :**

    relief        -296..306 m  ->  -331..405 m    (1,3 fois plus ample)
    part des terres   29,2 %   ->    29,2 %       (inchangee, elle est recalee)
    pluie moyenne      714 mm  ->     714 mm      (inchangee, elle est ancree)
    erosion            367 ms  ->    1896 ms      (5,2 fois, pour 5 fois les passes)
    ecart aux 8 biomes  44,1 % ->     49,6 %      **degradation a reprendre**

La degradation du bulletin etait attendue -- c'est la dette de recalibrage que
le proprietaire a acceptee en ouvrant le chantier. Le mix se refroidit :
calotte glaciaire entre dans les trois premiers biomes, parce que le relief est
plus haut donc plus froid. A reprendre par le couple
`soulevement / incisionRateM`, qui pilote l'amplitude, avant de toucher au
climat.

**RESTE A FAIRE, dans l'ordre :**
1. recalibrer l'amplitude pour ramener le bulletin au moins a 44 % ;
2. verifier a l'image que les escarpements sont la -- **rien n'a encore ete
   regarde en jeu** ;
3. mesurer a pleine resolution, ou le cout des 300 passes sera bien plus lourd
   qu'a 256 ;
4. seulement ensuite, decider du passage au modele PUR.

### 64 x 32 km : le relief credible etait a portee de regle (19 septembre 2026)

Signale par le proprietaire : « environ 300 m pour le plus haut sommet, ca ne me
parait pas tres realiste ». Il avait raison, et la reponse n'etait pas celle
qu'on croit.

**LES 300 m NE SONT PAS UN RESULTAT, C'EST UNE BORNE.** La tectonique finit par
`E = FMath::Clamp(E, MinElev, MaxElev)` avec `world.minElevationM` et
`maxElevationM`. Le sommet est un reglage, pas une consequence.

**MAIS IL N'EST PAS LIBRE, ET LE PROJET L'AVAIT DEJA MESURE.** Le commentaire au
dessus du code le dit : sur une carte de 1 km de hauteur, garder l'amplitude
metrique donne **92 % des sommets au-dela de l'angle de roche, un terrain
integralement gris**. L'altitude et la largeur sont liees par un budget de
pente : mettre 2000 m sur 16 km ne ferait pas des montagnes, ca ferait des
eboulis.

**LA DECOUVERTE QUI DEBLOQUE TOUT : `world.sizeKm` N'EST PAS LA TAILLE DU MONDE,
C'EST LA HAUTEUR DE REFERENCE DU CALAGE METRIQUE.**

    VerticalScale = clamp(Geo.HeightM / (world.sizeKm * 1000), 0,05, 4,0)

Les deux se trouvaient egales -- 8 km partout -- ce qui masquait completement la
distinction. Une carte de 32 km de hauteur avec une reference restee a 8 donne
donc un facteur **4**, et tout ce qui est metrique suit : profondeur oceanique,
base continentale, hauteur de montagne, bornes d'altitude. **Sans toucher a une
seule valeur.**

**MESURE, monde de 64 x 32 km, grille 2048x1024 :**

    altitudes        -1244 .. 1205 m   (contre -331 .. 405 a 16 x 8 km)
    part des terres        29,2 %      inchangee, elle est recalee
    pluie moyenne           714 mm     ancree, cible 715
    generation             58,9 s      tectonique 0,8 / climat 2 x 9,3 / erosion 39,4
    maille de simulation    31,3 m     contre 7,8 m a 16 km

**Des sommets a 1205 m.** Le relief credible n'a rien coute : il etait dans la
regle d'echelle, il suffisait de cesser de confondre la reference et la taille.

**CE QUI PLAFONNE MAINTENANT LA GRILLE, ET LE COMMENTAIRE D'ORIGINE EST
PERIME.** Il disait « au-dela, le maillage devient trop lourd sans decoupage en
chunks » : le terrain est desormais maille en voxels diffuses autour du joueur,
la grille de simulation n'est plus ce qui se dessine. Ce qui plafonne est le
COUT de la chaine -- doubler la grille quadruplerait les 59 s. La contrepartie
est que **la maille s'elargit sur une grande carte** ; le detail fin ne vient
plus d'elle mais de la couche voxel, qui travaille au metre.

**RECALIBRAGE A PREVOIR, et il n'est pas fait :**
- `climate.oceanModerationRangeKm` vaut 0,75 km, calibre pour un monde de 8 km
  (« 300 a 800 km sur Terre, soit 0,06 a 0,16 km rapporte a un monde 5000 fois
  plus petit »). A 4 fois la taille il devrait valoir **3,0** -- sans quoi les
  interieurs continentaux redeviennent maritimes ;
- les cavites sont metriques (espacement 180 m, profondeurs) : seize fois la
  surface donnera seize fois les chambres, donc un cout a mesurer ;
- le sol de fond faisait 512 sommets, soit **125 m par maille** a 64 km ;
  *(porte a 1024 le 19 septembre, soit 63 m -- ce n'est toujours pas assez, la
  vraie reponse serait des anneaux de resolution decroissante)* ;
- le bulletin terrestre est entierement a reprendre.

**VerticalScale PLAFONNE A 4,0.** Au-dela de 32 km de hauteur, le facteur cesse
de suivre et le relief redeviendrait plat en proportion. C'est la prochaine
borne a lever si la carte grandit encore.

### Le fond marin ne suit plus l'agrandissement (19 septembre 2026)

Propose par le proprietaire : « on n'a pas besoin in-game de plus de 200 m de
profondeur en mer, on pourrait peut-etre recuperer le delta pour avoir des
sommets plus hauts ». **L'intuition etait bonne, le mecanisme non**, et la
mesure a tranche les deux.

**IL N'Y A PAS DE BUDGET COMMUN A REPARTIR.** `minElevationM` et
`maxElevationM` sont deux ecretages independants ; baisser l'un ne donne rien a
l'autre. Et le plafond ne mordait meme pas : 1600 m autorises, **1205 mesures**.
Ce qui limitait les sommets etait `tectonics.mountainHeightM`, pas la borne.

**LE PLAFOND NE BORNE D'AILLEURS PLUS RIEN.** L'ecretage est applique dans la
tectonique, AVANT la boucle couplee ; le soulevement le franchit ensuite --
1698 m mesures pour 1600 autorises. Il ne borne que l'entree de la tectonique.

**CE QUE COUTENT DES SOMMETS PLUS HAUTS**, `mountainHeightM` 325 -> 500 :

    sommet          1205 -> 1698 m
    pente basalte   26,3 -> 33,5 deg
    pente granite   19,0 -> 21,8 deg
    pente calcaire   9,5 ->  9,4 deg   (inchangee)
    pente gres       8,6 ->  8,7 deg   (inchangee)

**Le cout tombe entierement sur la roche qui porte les montagnes.** Les plaines
ne bougent pas d'un dixieme de degre. C'est ce qui rend l'operation sure.

**LE VRAI GAIN DE L'IDEE ETAIT AILLEURS : LA PRECISION DE L'EAU.** Le plugin
Water encode la hauteur de toute l'eau du niveau dans UNE texture, dont la plage
va du point le plus bas au plus haut -- c'est deja documente plus haut, avec les
rideaux verticaux au bord des lacs pour symptome. Un fond a -1250 m au lieu de
-300 divise donc par quatre la precision de chaque texel.

**LA REGLE D'ECHELLE NE S'APPLIQUE PLUS AU FOND QUE VERS LE BAS :**

    SeabedScale = min(VerticalScale, 1.0)

Elle existe pour que le relief rapporte a la largeur reste borne, sinon tout
devient falaise. Cet argument porte sur les pentes TERRESTRES : on ne marche pas
sur le fond de l'ocean et l'eau le cache. Mais il faut garder la reduction :
sur une PETITE carte, un fond non reduit ferait de chaque cote une falaise
plongeant a trois cents metres -- le meme defaut, transpose sous l'eau.

Appliquee a `oceanDepthM`, `riftDepthM`, `detailAmplitudeOceanM` et au plancher
`minElevationM`. Pas a `continentBaseM`, `mountainHeightM` ni au plafond.

**MESURE, 64 x 32 km :**

    fond marin      -1250 -> -351 m    (3,6 fois moins profond)
    sommet           1698 -> 1700 m    (intact)
    part des terres  29,2 %  inchangee
    generation       58,2 -> 52,3 s

Relief total : **2051 m sur 64 km.** Un monde de montagnes, avec une plage en Z
d'eau trois fois et demie plus serree qu'avant.

### Les arches existent : il fallait tailler la lame avant de percer (19 septembre 2026)

Le chantier avait ete abandonne sur un constat chiffre -- zero site sur 402
points emerges avec une crete sous 80 m. Rouvert a la demande du proprietaire
apres la boucle soulevement / erosion, il aboutit : **9 vraies arches sur 10
posees**, traversantes et sous un pont de roche continu.

**LE RELIEF AVAIT CHANGE LE CONSTAT.** Meme mesure sur 64 x 32 km, 392 sites
valides : crete mediane 106 m, p10 44 m, et **37,76 % des sites sous 80 m**
contre 0,00 % avant. Le mecanisme est direct -- `S = (U / K.A^m)^(1/n)` : une
pente plus raide resserre la crete a une profondeur donnee sous le sommet.

**MAIS LES LAMES ETAIENT SUR LA MAUVAISE ROCHE, ET C'EST LE FAIT QUI A TOUT
DECIDE.** Sur 6327 sondages bien espaces, 229 cretes assez minces : **135 en
granite, 94 en basalte, ZERO en calcaire, gres ou schiste**. La cause est
mecanique -- granite a 33,5 degres de pente, gres a 8,7 -- donc la roche des
arches reelles est precisement celle qui, chez nous, n'a plus de relief.
**DECISION DU PROPRIETAIRE : fabriquer les lames en gres**, par le mecanisme
reel d'Arches National Park -- des joints verticaux PARALLELES qui decoupent
des murs minces.

**`WorldseedFins` : des fentes paralleles, PAS un bruit cellulaire.** Le Worley
des diaclases donne un nid d'abeille de parois, tres bien pour un chaos de
blocs et inutile ici. Une lame demande une fonction periodique le long d'UN axe.
Couverture mesuree : **1,01 % des terres** (gres 10,89 % des terres, dont
9,28 % en zone), lames de 54 m entre des fentes de 16 m profondes de 50. Le
meme champ donne les CANYONS et les MESAS a d'autres reglages.

**LE BUG QUI TUAIT TOUT, ET IL EST MATHEMATIQUE.** Ecrire
`U = X.cos(theta(X,Y)) + Y.sin(theta(X,Y))` pour des bandes qui tournent
doucement est FAUX : theta multiplie la coordonnee ABSOLUE du monde, qui monte
a 32 000 m, donc `|grad U| = 1 + X.d(theta)/ds` vaut environ **CINQ** loin de
l'origine. L'espacement reel des fentes tombait a quatorze metres pour
soixante-dix demandes, et il ne restait aucune lame. Remede : **theta constant
par morceaux** -- case de 16 km, bien plus large qu'une tache de lames, donc la
discontinuite tombe la ou la zone est deja nulle. `|grad U|` vaut alors
exactement 1. **A retenir pour tout champ oriente.**

**QUATRE MESURES FAUSSES AVANT LA BONNE, ET TOUTES DU MEME GENRE.**

1. *Un seul espacement pour sonder et pour poser.* Tant qu'aucune arche n'etait
   posee, CHAQUE cellule du monde etait examinee, y compris en plein versant ou
   la "crete" a vingt metres sous le point est la montagne entiere : 433 057
   sites et 99,44 % de cretes trop larges, quand la sonde -- qui espace ses
   points et ne garde que des sommets -- en trouvait 37,76 % d'assez minces.
   **On sonde souvent, on pose rarement.**
2. *La verification n'evaluait pas les cavites.* `Density.At(P)` sans le reseau
   rend la roche PLEINE : la sonde mesurait le monde d'AVANT le percement et
   annoncait "0 traversante" quelles que soient les arches. Meme piege que
   `probe_voxel` sans `SetLithology`, et il ne se signale pas.
3. *La mesure du pont partait DANS le trou.* Compter la roche vers le haut
   depuis le sommet calcule de l'ouverture donne zero : ce point est de l'air
   par construction, et l'union lisse l'inflate encore du rayon de raccord. Il
   faut d'abord SORTIR du trou, puis compter. **Le signe qui aurait du
   alerter : 0 m au meme offset de -22 m sur les DIX arches.** Un chiffre
   identique partout n'est jamais un hasard de terrain.
4. *Un booleen ne dit pas ou ca casse.* Deux corrections n'ont pas bouge le
   compte "avec pont" ; c'est en remplacant le oui/non par une EPAISSEUR
   MESUREE, avec l'offset du point le plus mince, que l'anomalie est apparue.

**LE PONT DEMANDE N'EST PAS LE PONT OBTENU**, et il faut le savoir partout ou
une epaisseur compte : l'union lisse gonfle chaque forme creusee d'environ
`raccordM` (2,5 m). Un pont demande a 4 m ne laissait que 1,5 m de roche.
Plancher porte a 7. Et la marge de securite sur le sommet macro (10 m) s'ajoute
au pont : mesure finale **7 a 26 m** pour des ouvertures de 18 a 33 m de haut.
C'est epais pour une arche ; le chemin d'amelioration est d'evaluer le
deplacement vertical du voxel dans la passe des cavites plutot que de le
couvrir par une marge forfaitaire.

**Le reseau conserve desormais les arches** (`FWorldseedCaveArch` : centre, axe
de percement, epaisseur, rayon, pont). C'est l'arbitrage B8, et c'est ce qui
permet a `Worldseed.Lieux` d'y envoyer le joueur. Le reseau etant REBATI a
chaque chargement et jamais serialise, aucun impact sur le cache.

**CE « PONT PERCE » ÉTAIT UN DÉFAUT DE MESURE, PAS DE GÉOMÉTRIE.** La sonde
annonçait 0 m d'épaisseur au MÊME décalage de -22 m sur les DIX arches -- une
coïncidence trop parfaite pour être géologique, et c'est ce qui a mis sur la
piste. Elle commençait à compter la roche DEPUIS L'INTÉRIEUR du trou qu'on
venait de creuser. Corrigée en sortant d'abord de l'air, puis en comptant.
**RÈGLE : une mesure identique au chiffre près sur dix cas indépendants mesure
le mesureur, pas le mesuré.** Et les arches ont depuis été regardées en jeu, par
la tournée photo.

### Le plateau disseque : mesas et canyons (19 septembre 2026)

Demande du proprietaire : « fais les canyons et les mesas », puis, en cours de
route, « ils doivent etre places dans des zones geographiques appropriees en
terme de climat, de relief et de lithologie ». Les deux formes sortent d'UNE
seule passe, `WorldseedPlateau`, posee a cote de la passe littorale.

**CE QUI FAIT LIRE UNE MESA TIENT EN UN SEUL FAIT.** Sur une photo de Monument
Valley, TOUS les sommets sont a la MEME altitude, parce qu'ils sont les
morceaux d'UNE ancienne plaine. Aucun bruit fractal ne peut produire cela : un
fBm donne des sommets a des hauteurs toutes differentes, donc des collines et
jamais des tables. D'ou une surface de REFERENCE -- maximum glissant du relief,
puis lissage. Le maximum retient les sommets ; le lissage les relie en une
surface continue ; et la ou ce lissage passe SOUS un sommet isole, le `min()`
final rabote ce sommet A PLAT. **Prendre le maximum seul ne raboterait rien** :
il vaut H sur chaque sommet par construction.

Mesas et canyons sont les deux faces d'un meme objet -- ce qui reste, et ce qui
a ete enleve -- ce qui est precisement pourquoi une seule passe les produit.

**UNE AFFIRMATION DU DEPOT ETAIT FAUSSE, et elle a ete corrigee.** Le
commentaire de la section `lames` de `world_rules.json` annoncait que « le meme
champ donne les canyons et les mesas a d'autres reglages ». C'est vrai pour les
canyons EN FENTE, faux pour les mesas : des fentes paralleles laissent des
CRETES, dont les sommets suivent le relief existant, donc a des altitudes
toutes differentes. Il y manquait la surface de reference, qui est tout le
mecanisme.

**LA PASSE N'ABAISSE JAMAIS**, comme la passe littorale dont elle copie la
structure, et le plancher du fond de canyon est PORTANT et non defensif : sans
lui, un escarpement de 220 m creuse depuis une table qui n'en fait que 80
enverrait le fond SOUS le niveau de la mer, donc convertirait de la terre en
mer. La part emergee est calibree a 29,2 pour cent et traitee comme un
invariant du projet. Verifie : **29,20 pour cent avant comme apres**.

**ELLE VIT DANS LE RELIEF 2D, PAS DANS LE VOXEL**, et ce n'est pas un choix de
commodite : le voxel ne creuse que `bandeM` sous la surface, cent metres, ou une
table de deux cents ne tiendrait pas. Le voxel ajoute ensuite son grain d'un
metre sur la paroi, ce qui est son role.

**LE PLACEMENT SE LIT SUR LES CHAMPS CONTINUS, JAMAIS SUR L'ETIQUETTE DE
BIOME** -- meme regle que le karst. Trois criteres physiques, et chacun a sa
raison : roche sedimentaire tendre, parce qu'une table-montagne est un
empilement debite en bancs horizontaux que le granite ne fait pas ; pluie
faible, parce qu'un escarpement vertical est une forme ARIDE -- sous la pluie
le sol se forme, la vegetation s'installe et la paroi s'adoucit en versant ; et
relief local faible, parce qu'une mesa est le RESTE d'une plaine et que sans
plaine il n'y a rien a dissequer.

**LE CANYON SUIT LE DRAINAGE, PONDERE PAR LA PLUIE.** Un flux a poids uniforme
mesure une AIRE ; pondere par la pluie il mesure un DEBIT, et c'est la
difference entre un oued et un fleuve. Cela fait exister le fleuve ALLOGENE --
celui qui ramasse son eau dans des montagnes humides et traverse ensuite un
desert. Le Colorado est exactement cela, et c'est pourquoi le plus grand canyon
du monde se trouve dans une region qui ne recoit presque rien. Le champ de
lames ajoute la seconde echelle, les fentes etroites.

**L'ENTONNOIR EST CE QUI A RENDU LE REGLAGE POSSIBLE**, et c'est la lecon de
methode a retenir. Une couverture trop faible peut venir de six gardes
differentes, et sans le compte de chacune on regle au hasard la mauvaise -- ce
que le depot a deja paye quatre fois de suite sur le routage des galeries.
Releve, graine 20260909, 64 x 32 km :

    terres                         612 369   100,00 %
    altitude >= 60 m               437 220    71,40 %
    pluie <= 520 mm                280 012    45,73 %
    relief local <= 320 m           84 033    13,72 %
    durete 0,25 a 0,70              29 028     4,74 %
    masque de region                   857     0,14 %   <- le coupable

Les quatre criteres physiques laissaient un gisement sain de **4,74 % des
terres**. C'est le masque de region qui etranglait tout.

**UNE FREQUENCE TROP BASSE REND LA COUVERTURE ALEATOIRE, et ce n'est pas une
question de dosage.** A 0,00012 cycle/m la longueur d'onde fait 8,3 km, soit
une trentaine de taches sur tout le monde ; or le terrain eligible est lui-meme
groupe en quelques regions. **L'intersection de deux ensembles peu nombreux est
une LOTERIE sur la graine, pas une proportion** : le masque ne gardait que 3 %
de l'eligible la ou son seuil en promettait 18. Porte a 0,00028 -- environ 120
taches -- l'intersection redevient statistique. **A retenir pour tout masque
pose EN PLUS d'autres criteres deja selectifs.**

**ETAT MESURE**, apres calibrage, temoin SPATIAL dans le meme monde (les
cellules qui passent toutes les gardes sauf le masque) :

|                                   | en zone | temoin |
|---|---|---|
| part des terres                   | 1,47 % | 3,31 % |
| pente mediane                     | 10,3 deg | 4,0 |
| cellules sous 5 degres            | 31,5 % | 55,7 |
| **cellules au-dela de 45 degres** | **18,2 %** | **0,0** |
| ecart d'altitude des sommets      | 17 m pour 220 d'escarpement, soit 7,8 % | -- |

Chute maximale 192 m, moyenne 61 m, cout 1,4 s par monde.

**LE TEMOIN EST SPATIAL, PAS TEMPOREL, ET C'EST DELIBERE.** Comparer deux
generations demanderait de changer une regle entre les deux -- or **le PIE NE
RELIT PAS `world_rules.json`**, seules les sondes le font, si bien qu'un temoin
ainsi obtenu mesure exactement le cas teste. Le depot a deja paye ce piege sur
l'A/B du reseau de grottes.

**ET L'ECART DE SOMMETS NE SE LIT PAS CONTRE LE TEMOIN** -- piege que cette
sonde a d'abord tendu elle-meme. Le temoin est une PLAINE : ses rares sommets y
partagent trivialement leur altitude, donc son ecart est petit PAR ABSENCE DE
RELIEF, pas par partage d'une surface. Compare a lui, la zone parait toujours
pire. La seule lecture juste est RELATIVE A L'ESCARPEMENT.

**DEUX DEFAUTS TROUVES A L'IMAGE, ET SEULEMENT A L'IMAGE.**

1. **Un masque de region dit ou une forme a le DROIT d'exister, pas qu'elle
   existe.** La premiere version de `WorldseedPlateau::Sites` ne testait que ce
   masque : les sites designes avaient leur sommet a **700 et 1064 metres**,
   c'est-a-dire des MONTAGNES que la passe n'avait jamais touchees. Aucune
   mesure agregee ne le voyait. Corrige par deux gardes purement geometriques,
   qui ne demandent ni la roche ni la pluie : le sommet doit etre PLAT (moins de
   8 degres) et le denivele local doit valoir a peu pres UN escarpement (0,5 a
   1,6 fois). Un pic a le meme denivele mais une pente forte ; une montagne a un
   denivele bien plus grand. Apres : 5 sites sur 139 candidats, sommets de 89 a
   420 m, parois de 118 a 191 m.

2. **ON NE PHOTOGRAPHIE PAS UNE FORME D'UN KILOMETRE AVEC UN RAYON DE 250 m.**
   Le voxel n'existe que dans le rayon de chargement ; au-dela on photographie
   le SOL DE FOND, a 63 m par maille. Les premieres vues, prises a 900 m, ne
   montraient donc pas le relief mais son decor d'horizon : des formes lisses et
   arrondies ou l'on croit voir un relief mou alors qu'on ne voit pas le relief
   du tout. **Le depot avait deja perdu une heure sur ce piege**, a 420-580 m, et
   il est consigne plus haut -- je l'ai refait. Les vues sont desormais a 200 m
   et cadrent la PAROI et son rebord contre le ciel, pas la silhouette entiere.

**CE QUI SE REBATIT NE SE TRANSPORTE PAS.** `WorldseedPlateau::Sites` est une
fonction PURE du relief fini, des regles et de la graine : le menu ne la
transporte pas et le terrain la rejoue au chargement, exactement comme le reseau
de grottes. Transporter une donnee deterministe la doublerait. Aucun effet sur
le cache.

**UNE SEULE IMPLEMENTATION, DEUX CONSOMMATEURS.** `Surfaces` et `ZoneAt` sont
publiques a dessein : la passe les emploie pour creuser, la sonde pour mesurer.
Une sonde qui reimplemente son critere valide une COPIE du mecanisme, pas le
mecanisme -- c'est la regle du depot, et le portage de `terre.py` l'avait deja
rappelee.

**RESTE OUVERT, ET CHIFFRE :**

- **La silhouette entiere n'a pas ete vue.** A 250 m de rayon de chargement, une
  table de plus d'un kilometre ne tient pas dans une vue. Ce qui est verifie a
  l'image est la PAROI ; ce qui ne l'est pas est le partage d'altitude entre
  tables voisines, qui n'est etabli que par la mesure (17 m pour 220). Pour le
  voir il faudrait soit elargir le rayon pour la tournee, soit reduire l'echelle
  des tables (`porteeM`), soit des anneaux de resolution pour le sol de fond --
  chantier deja ouvert par ailleurs.
- **La distribution des pentes n'est pas franchement bimodale** : 31,5 % de plat,
  39,8 % entre 5 et 25 degres, 18,2 % au-dela de 45. La bande intermediaire
  reste la plus large, ce qui veut dire que les parois sont encore adoucies par
  la maille de simulation de 31 m. Le voxel les raidit localement ; la mesure
  2D, elle, ne peut pas descendre sous sa maille.

### La lithologie passe en 3D : une COLONNE, pas une grille (19 septembre 2026)

Question du proprietaire : « est-il possible de passer la lithologie en 3D ?
cela permettrait de resoudre les choses ? ». Reponse : oui, et c'etait la brique
manquante commune a DEUX chantiers -- les gradins du canyon et le chapiteau de
la mesa sont le meme mecanisme, l'erosion differentielle VERTICALE.

**UNE GRILLE 3D EST IMPOSSIBLE, ET LE PROJET AVAIT DEJA FAIT LE CALCUL.** A un
metre de voxel ce monde ferait soixante-seize milliards de voxels : un seul
identifiant de roche par voxel pese 76 Go. C'est exactement l'argument qui avait
fait du relief une FONCTION plutot qu'une grille, et il vaut tel quel ici.

**LA REPONSE EST UNE COLONNE STRATIGRAPHIQUE.** La carte 2D existante dit quelle
roche fait le SOCLE ; une SERIE de bancs se lit par-dessus a n'importe quel
(x, y, z), sans rien stocker. `WorldseedStrata::BancAt` remplace un acces
tableau par une descente dans une pile de six elements. Cout memoire : zero.
`WorldseedLithology.h` l'avait d'ailleurs annonce -- « un vrai sous-sol est
feuillete [...] c'est une extension naturelle ».

**LES BANCS SONT HORIZONTAUX, ET CE N'EST PAS UNE SIMPLIFICATION.** Les rayures
du Grand Canyon sont horizontales PARCE QUE les bancs le sont, et toutes les
mesas d'une region partagent leur sommet parce qu'elles s'arretent TOUTES sur le
meme banc dur. La platitude EST la cause de la forme. Le gauchissement -- 140 m
sur 16 km de longueur d'onde, moins d'un degre -- existe seulement pour eviter
que le monde entier porte ses corniches au meme niveau sur 64 km.

**LA SERIE NE RECOUVRE QUE LE SEDIMENTAIRE**, par une fenetre de durete sur le
socle. Sur granite ou basalte, tout rend la roche 2D : le comportement d'avant,
a l'identique. Une donnee absente doit rester sans effet.

**CE QUE LA STRATIGRAPHIE REMPLACE, ET C'EST UN GAIN DE FOND.** Le sommet d'une
mesa n'est plus RABOTE geometriquement sur un maximum glissant : il se cale sur
le TOIT DU PREMIER BANC DUR. C'est le mecanisme reel, decrit par le
proprietaire -- « le banc dur protege les tendres du dessous, l'erosion sape la
base, la roche dure s'effondre par blocs ». Le maximum glissant reste, mais pour
ce qu'il sait faire : servir de repli la ou aucun banc dur n'affleure.

**MESURE, et c'est elle qui valide le remplacement** (graine 20260909, 64 x 32 km) :

| | rabotage geometrique | chapiteau geologique |
|---|---|---|
| cellules remodelees | 0,20 % des terres | **0,77 %** |
| plat sous 5 degres | 40,2 % | 32,3 % |
| cellules au-dela de 45 degres | 6,8 % | **12,6 %** |
| ecart d'altitude des sommets | 3 m, soit 1,4 % | **3 m, soit 1,5 %** |
| part emergee | 29,20 % | **29,20 %** |

**L'ECART DES SOMMETS NE S'EST PAS DEGRADE**, et c'est le point : la propriete
qui definit une mesa vient desormais de la GEOLOGIE et non d'une astuce, sans
rien perdre.

**LE PROFIL DE CANYON A ETE REFAIT AU PASSAGE, et le defaut valait la lecon.**
La premiere version seuillait le flux accumule par un `smoothstep` : la
profondeur decroissait donc continument en s'eloignant du lit, ce qui donne une
RAMPE et non une gorge. Le releve le disait sans ambiguite -- abaissement MOYEN
58 m pour un maximum de 166, et 39,8 % des cellules entre 5 et 25 degres contre
16,4 au-dela de 45. **Le remede etait deja ecrit dans la passe littorale**, dont
le commentaire porte : « LA FACE DOIT TENIR DANS UNE MAILLE DE SIMULATION, sinon
la falaise n'est qu'une rampe ». Le profil se dessine desormais depuis la
DISTANCE AU CHENAL -- transformee exacte du projet -- : plancher plat sur
`largeurFondM`, puis chute sur une maille. La cle `drainageLargeur` a disparu
avec le smoothstep, plutot que d'etre laissee a mentir dans le fichier.

**ET LE SEUIL DE DRAINAGE SE LIT CONTRE LA DISTRIBUTION, une fois de plus.**
A 3,20 -- le rang 75 -- un QUART du terrain devenait chenal, et avec 76 m de
gorge de chaque cote il ne restait plus de plateau : la part de sommets plats
tombait de 31,5 a 11,6 %. Or le Colorado est UN fleuve sur un vaste plateau.
Porte au rang 93, le plateau survit entre les gorges -- condition pour qu'il
s'y forme des mesas.

**RESTE A FAIRE, dans l'ordre et sans l'adoucir :**

1. **L'EROSION NE LIT PAS ENCORE LES BANCS.** `Erodibility` fabrique un K par
   cellule depuis la roche 2D, une fois, AVANT la boucle. Pour que les gradins
   EMERGENT au lieu d'etre poses, il faut reechantillonner K a l'altitude de la
   surface COURANTE a chaque passe : en descendant, la surface traverse alors
   les bancs et le taux change. C'est la que l'escalier du Grand Canyon naitra
   vraiment. Le precedent est favorable -- la durete branchee sur une erosion
   sans apport ne deplacait que 4 a 7 cm, mais une fois la boucle
   soulevement/erosion en place elle a donne 25 degres de pente au granite
   contre 9,5 au calcaire.
2. **Les huit autres sites d'appel de la durete restent 2D** : grottes, lames,
   diaclases, littoral, sondes. Rien n'est casse -- ils lisent le socle -- mais
   une galerie qui descend devrait changer de roche.
3. **Rien n'a ete REGARDE depuis la stratigraphie.** Les parois rayees sont
   branchees et compilent ; elles n'ont pas ete photographiees.

### L'erosion lit les bancs : ce que ca donne, et ce que ca ne donne pas (19 septembre 2026)

Demande du proprietaire : « fais l'erosion qui lit les bancs ». C'est fait,
c'est mesure, et le resultat est A MOITIE celui qu'on cherchait -- il faut le
dire dans cet ordre.

**CE QUI A CHANGE DANS LE CODE.** `K` n'est plus fige avant la boucle : il est
REECHANTILLONNE a l'altitude de la surface courante, a la cadence du drainage.
Le datum -- toit de la serie -- est precalcule une fois, puisque l'erosion ne
deplace pas une surface geologique ; chaque reechantillonnage ne coute alors
qu'une descente dans la pile. **Cout mesure : nul.** 51,96 s de generation
contre 51,72 avant.

**LE CONTRASTE A DU ETRE AMPLIFIE, ET LA MESURE L'A EXIGE.** Premiere version :
`K de 1,08 a 1,48`, soit un rapport de **1,37**. Une corniche qui resiste 1,37
fois mieux que son talus ne se voit pas. La cause est dans la formule, qui est
ADDITIVE et ancree sur la durete moyenne du MONDE -- 0,73, dominee par le
granite et le basalte qui couvrent le plus de surface. Toute la serie
sedimentaire etant SOUS cette moyenne, ses K se tassaient. D'ou
`strates.contrasteErosion`, qui ecarte les bancs AUTOUR DE LA MOYENNE DE LA
SERIE : leur moyenne ne bouge pas, donc le volume d'erosion sur la couverture
non plus, et le calage du granite et du basalte n'est pas touche. Resultat :
`K de 0,61 a 1,81`, rapport **2,98**.

**CE QUI MARCHE, ET C'EST MESURE :**

    PENTE par banc -- dur 16,65 deg (3093 cellules) contre tendre 12,42 (10949)
                      ecart +4,22 deg, rapport 1,34

Un banc dur tient une pente un tiers plus raide que son talus. C'est exactement
`S = (U / K.A^m)^(1/n)`, la meme loi qui donnait deja 25 degres au granite
contre 9,5 au calcaire.

**CE QUI NE MARCHE PAS, ET IL NE FAUT PAS LE MAQUILLER :**

    ALTITUDE -- durete du banc occupe 0,393 contre 0,411 en moyenne de serie,
                rapport 0,957

La surface ne s'attarde PAS sur les bancs durs. Il n'y a donc pas d'escalier a
marches franches.

**LA RAISON EST PHYSIQUE, PAS UN DEFAUT DE REGLAGE.** A l'equilibre soulevement
/ erosion, un banc dur ajuste sa PENTE, il ne retient pas une ALTITUDE. Or
l'escalier du Grand Canyon est une forme TRANSITOIRE : ses marches viennent de
falaises qui reculent HORIZONTALEMENT par sapement -- le banc tendre se
desagrege, la corniche dure perd son appui et s'effondre par blocs. Un modele
d'incision a l'equilibre ne produit pas ce mecanisme, quel que soit le
contraste qu'on lui donne.

**LA PISTE, ET LE PROJET A DEJA LA BRIQUE.** `WorldseedCoast` implemente
exactement un recul par sapement -- « une falaise marine ne nait pas d'un
equilibre de pente mais d'un SAPEMENT ». Le transposer aux limites de bancs,
avec la desagregation du tendre pour moteur au lieu de la houle, est le chemin
vers de vrais gradins. C'est un chantier a part entiere.

**DEUX FAUTES DE MESURE PAYEES DANS LA MEME HEURE, et elles se ressemblent :**

1. **Une fenetre qui sature.** Le premier test comptait les cellules a moins de
   douze metres du toit d'un banc dur. Il marchait sur des bancs epais et s'est
   effondre des qu'on les a amincis : a 18-35 m d'epaisseur, les fenetres se
   recouvrent et « pres d'un toit dur » devient vrai presque partout -- 47,4 %
   attendus au hasard, donc plus aucun pouvoir discriminant. **Une mesure dont
   la reference derive avec le reglage qu'on teste ne mesure rien.** Meme
   famille que « un seuil n'est pas une part ».
2. **Tester la mauvaise grandeur.** Chercher un escalier dans l'ALTITUDE quand
   la physique implementee produit un changement de PENTE. Le depot avait
   pourtant deja ecrit, deux fois, que « ni l'altitude moyenne par roche ni la
   perte brute ne mesurent l'erosion differentielle : seule la PENTE dit
   quelque chose de la resistance ». Relu trop tard.

**L'EPAISSEUR DES BANCS A ETE RAMENEE DE 45-100 m A 18-35 m**, et la serie
compte desormais DIX bancs pour 253 m. Motif : l'erosion retire 46 a 107 m
selon la roche, donc avec des bancs de 45-100 la surface n'en traversait
qu'UN -- on ne fait pas un escalier avec une marche. Ce changement n'a pas
suffi a faire apparaitre l'escalier, pour la raison physique ci-dessus, mais il
reste le bon calage : c'est celui qui met l'epaisseur des bancs a l'echelle du
budget d'erosion.

### Le sapement des corniches, transpose de la falaise marine (19 septembre 2026)

Demande du proprietaire : « fais le sapement des bancs comme pour les falaises
marines ». C'etait le bon chemin, et la brique etait deja ecrite.

**POURQUOI IL FALLAIT CETTE PASSE, et c'est la mesure qui l'a etabli.** Brancher
l'erosion sur les bancs donne bien un contraste de PENTE -- 16,65 degres sur le
dur contre 12,42 sur le tendre -- mais AUCUN escalier : la surface ne s'attarde
pas sur les bancs durs, rapport 0,957. Ce n'est pas un defaut de reglage. A
l'equilibre soulevement / erosion, un banc dur ajuste sa PENTE ; il ne retient
pas une ALTITUDE. Les marches du Grand Canyon sont une forme TRANSITOIRE, faite
de falaises qui reculent HORIZONTALEMENT -- et un modele d'incision ne sait pas
la produire, quel que soit le contraste qu'on lui donne.

**LE MECANISME EST CELUI DE LA FALAISE MARINE, TERME A TERME**, d'ou une passe
qui copie `WorldseedCoast` au lieu d'inventer autre chose :

    falaise marine                      corniche de banc
    --------------------------------    --------------------------------
    la houle creuse une encoche         le banc TENDRE se desagrege
    la masse au-dessus s'effondre       la corniche perd son appui
    le front recule, reste vertical     la corniche recule, reste verticale
    la roche tendre recule plus vite    plus le talus est tendre, plus la
                                        corniche recule
    plateforme d'abrasion au pied       banquette au toit du banc dur suivant
    distance a la MER                   distance a la zone DEJA DESCENDUE

**UNE ERREUR DE GEOLOGIE PAYEE A LA MESURE.** Premiere version : la banquette
visait la BASE du banc dur, c'est-a-dire le sommet du talus tendre qui le porte.
Or ce talus se desagrege entierement -- c'est tout le mecanisme -- et la
nouvelle marche est portee par la CORNICHE D'EN DESSOUS. Viser le talus revenait
a poser la banquette sur ce qui, precisement, ne tient pas. Le controle
d'altitude est tombe de 0,957 a **0,842** : la surface se retrouvait en moyenne
sur des bancs PLUS TENDRES qu'avant la passe.

**MESURE, apres correction** (graine 20260909, 64 x 32 km) :

| | erosion stratifiee seule | + sapement |
|---|---|---|
| ecart de pente dur / tendre | +4,22 deg | **+8,17 deg** |
| rapport | 1,34 | **1,75** |
| chute maximale | -- | **52 m** |
| cellules sapees | -- | 5 371, soit 0,88 % des terres |
| cout | -- | **146 ms** |
| part emergee | 29,20 % | **29,20 %** |

**52 M EST LA PREUVE QUE LA MARCHE A LA BONNE HAUTEUR** : c'est exactement
`toit du gres -> toit de la dolomie`, soit 22 + 30. Le chiffre ne vient pas du
terrain, il vient de la GEOMETRIE DE LA SERIE -- donc la marche est celle qu'on
a dessinee.

**LE PIEGE DU CACHE, PAYE UNE FOIS DE PLUS.** Apres la correction de la
banquette, le releve est revenu IDENTIQUE AU CHIFFRE PRES et la ligne
`sapement` avait disparu du journal : le monde venait du CACHE. La correction
touchait le code, pas les regles, et je n'avais pas rebumpe
`WORLDSEED_PIPELINE_VERSION`. C'est exactement ce que le compteur existe pour
couvrir, et le depot le consigne deja pour la lithologie -- « le monde revenait
avec l'ancienne carte des roches sans le moindre signe ». **LE SIGNE QUI
TRAHIT : un releve identique au chiffre pres apres une correction reelle, et
une ligne de journal qui manque.**

**CE QUI RESTE OUVERT, sans l'adoucir :**

1. **Le controle d'altitude reste sous 1** (0,928 contre 0,957 sans sapement).
   Il est agrege sur les 14 042 cellules de la serie alors que le sapement n'en
   touche que 5 371 : il est donc domine par le terrain NON sape. Le mesurer
   sur les seules cellules sapees demanderait de les marquer, ce qui n'est pas
   fait.
2. **Rien n'a ete REGARDE.** L'escalier est mesure, pas photographie. Et le
   depot a une regle pour cela : une forme qui n'a pas ete vue n'est pas
   validee.
3. **Le recul est uniforme par banc.** Une vraie corniche recule plus vite la
   ou elle est mieux drainee ; ici seule la tendrete du talus module `reculM`.

### Une metrique JUSTE peut ne pas mesurer le defaut signale (21 septembre 2026)

Signale : « la forme des continents est anguleuse et geometrique, elle n'est pas
organique du tout ». Le diagnostic a pris trois seances, et la lecon ne porte
pas sur les continents.

**LA SONDE ETAIT BONNE. ELLE NE REGARDAIT SIMPLEMENT PAS AU BON ENDROIT.**
`ProbeCotes` mesure la dimension fractale du trait par comptage de boites, elle
est validee sur deux temoins, et elle DISTINGUE. Mais une dimension fractale
quantifie la rugosite **FINE** du contour, alors que « anguleux et geometrique »
decrit la **SILHOUETTE** : des segments de plusieurs kilometres se rejoignant a
angles nets. Les deux sont independants. Nos continents mesuraient **1,013**,
c'est-a-dire mieux que la cote sud-africaine -- la plus lisse de la Terre --
tout en etant, a l'image, des polygones a aretes rectilignes.

**C'EST CE QUI A FAIT ECHOUER SEPT ESSAIS DE SUITE.** Gain du bruit de cote,
amplitude, octaves, diffusion de versant, bruit de relief, erosion elle-meme,
socle continental : tous reglaient la bonne grandeur a la MAUVAISE ECHELLE, et
aucune mesure ne pouvait le dire puisque la mesure ne voyait pas l'echelle
fautive. On regle longtemps le mauvais bouton quand la metrique est muette sur
le vrai defaut.

**LA REGLE A EN TIRER, et elle est plus generale que ce cas :** quand un defaut
est signale A L'OEIL et qu'aucun reglage ne deplace le chiffre, se demander si
le chiffre MESURE LE DEFAUT -- avant de se demander quel terme le corrige.
C'est la cousine de « quand une correction ne bouge pas la mesure, se demander
si la mesure melange deux populations » : la aussi, on decompose au lieu de
regler plus fort.

**LE CORRECTIF PROPREMENT DIT.** Un diagramme de Voronoi a des aretes
RECTILIGNES par definition -- l'ensemble des points equidistants de deux germes
est un plan -- et aucun reglage du Voronoi ne les courbe. Seul le deplacement du
point d'ECHANTILLONNAGE avant classement le fait. `plateWarpStrength` valait
0,14 pour 0,60 necessaire.

**ET C'EST LA FREQUENCE QUI DECIDE SI L'ON COURBE OU SI L'ON DECHIRE**, bien
plus que la force. Sur un monde de 64 km : a 3, longueur d'onde 21 km, la
frontiere ondule a l'echelle du CONTINENT et les peninsules deviennent courbes
et effilees ; a 14, longueur d'onde 4,6 km, l'echelle d'une ILE, et le continent
se fragmente en chapelet. **Les deux ne se distinguent pas au chiffre** --
1,010 contre 1,007 a l'arrivee -- alors que ce sont deux mondes differents.

D'ou **`ProbeCarte`**, qui ecrit la carte a plat dans `Saved/Worldseed/Cartes/`,
un pixel par cellule de simulation. Le globe du menu ne pouvait pas tenir ce
role : il est SPHERIQUE, on n'en voit qu'une face, il tourne, et deux reglages
ne s'y comparent pas image contre image.

**UN A/B MAL MONTE, ET JE L'AI ECRIT DANS UN CORPS DE COMMIT.** `fdf62aa`
affirme que « l'arrivee plafonne vers 1,01, la chaine reprend tout le gain ».
C'est faux : j'avais compare un chiffre mesure ce jour-la a un chiffre mesure la
veille dans un autre etat du CODE. Le temoin refait dans le meme etat donne
0,981, et le warp gagne bien trois centiemes. **Les regles n'avaient pourtant
pas bouge entre les deux -- verifie par `git log` sur le fichier.** Un temoin
doit etre refait dans l'etat courant, pas repris d'un releve anterieur, meme
quand on croit que rien n'a change.

**LE CONTROLE QUI RESTE OBLIGATOIRE SUR TOUT REGLAGE DE FORME** : les BIOMES, pas
seulement le trait. L'essai du socle avait donne la meilleure dimension de toutes
en effondrant la toundra de 13,7 a 6,6 %. Ici la toundra ne bouge pas (8,49 ->
8,52) ; ce qui bouge est un jeu a somme nulle -- des cotes plus decoupees donnent
des terres plus MARITIMES, donc prairie et foret temperee gagnent ce que la
savane et le desert chaud perdent, et l'ecart absolu moyen passe de 35,1 a
37,0 %. Accepte : le deficit de terres temperees se traite par le climat.

**PIEGE D'OUTILLAGE, paye deux fois dans la meme heure.** `[int](3/2)` vaut
**2** en PowerShell : l'arrondi par defaut est au pair (banquier), pas la
troncature. Une planche de comparaison a quatre vignettes en a perdu une, dessinee
hors du cadre. Utiliser `[Math]::Floor`. Et une commande PowerShell passee en
ligne a travers bash se fait manger ses `$tableau[$i]` avant que PowerShell ne
les voie : ecrire le script dans un fichier.

**VU EN JEU LE 21 SEPTEMBRE, ET C'EST CE QUI VALIDE LE CHANTIER.** Le globe du
menu porte desormais une cote irreguliere -- baies, mer interieure, peninsule
au nord, avancee au sud -- et plus aucune arete droite. Graine 1337, grille
4096 x 2048, 15,6 m par cellule, terres 29,2 %, altitudes -337 a 1520 m ; le
sous-sol a suivi avec 155 chambres, 11 reseaux, 39 bouches, 26 gouffres,
33 dolines et 24 arches, dont neuf ARCHES MARINES percees dans des caps de
basalte.

**ET CE MONDE CORRIGE UN CHIFFRE DU CORPS DE `d52057a`.** J'y annonce une
calotte glaciaire portee a 16,2 % des terres pour 10 attendus. Celui-ci la
donne a **10,3 %**, et la savane a 14,1 pour 13. Ma mesure portait sur la
graine 20260909 a la resolution 1024 ; celle-ci est la graine 1337 a 4096,
quatre fois plus fine. **Le cout sur les biomes depend donc de la GRAINE et de
la RESOLUTION bien plus que je ne l'ai ecrit** -- ce n'est pas une degradation
generale, et il ne faut pas lire ce tableau comme tel. Ce qui reste vrai et
independant de la graine est la foret temperee mixte a 3,4 % pour 13 attendus :
c'est le deficit de terres temperees deja ouvert au registre.

**LA LECON DE MESURE, pour la enieme fois** : un releve sur UNE graine a la
resolution de travail ne se generalise pas a la resolution de production. Le
dire quand on cite le chiffre, ou mesurer sur plusieurs graines.

### Le carre d'ocean : ce que le plugin Water ancre, et ce qu'il ne suit pas (21 septembre 2026)

Signale en jeu : « je vois nettement un carre d'ocean autour de moi », puis
« elle ne me suit pas, j'arrive au bord et elle se regenere devant moi ». Un
seul defaut, deux visages -- et TROIS affirmations de ce registre etaient
fausses.

**`r.Water.WaterMesh.MaxWidthInTiles` N'EXISTE PAS.** La vraie variable est
**`r.Water.WaterMesh.MaxDimensionInTiles`** (WaterMeshComponent.cpp:38,
defaut 256). Le mauvais nom vient du MOTEUR lui-meme : son message
d'avertissement (ligne 605) cite la variable inexistante, et ce registre l'a
recopie. La ligne reclamee dans `DefaultEngine.ini` etait donc doublement
morte -- absente ET mal nommee. **Un message du moteur n'est pas une source
pour un nom de variable : le chercher dans la declaration.**

**LA NAPPE LOINTAINE NE COUVRE PAS L'HORIZON, ET ELLE NE LE PEUT PAS.**
`FarMeshBounds = WaterZoneBounds2D` (WaterQuadTreeBuilder.cpp:181) : la jupe
de huit quads s'accroche aux bornes de la ZONE -- 64 x 32 km chez nous -- et
part vers l'EXTERIEUR. Depuis l'interieur du monde elle est inatteignable,
quelle que soit sa portee. Le commit qui la posait a 40 km est juste sur le
fond (le reglage valait zero) mais **ne pouvait rien corriger de visible**,
et il l'annoncait a tort comme verifie.

**LA TEXTURE D'INFO VALAIT 512, PAS 4096.** Le defaut du plugin est
512 x 512 (WaterZoneActor.cpp:96) et notre code ne la reglait pas. Le 4096
du registre appartenait a la zone posee dans l'editeur, morte avec elle --
meme famille que le PlayerStart qui revient et les acteurs d'eclairage
disparus. **Tout reglage d'une `WaterZone` se pose PAR CODE.** Le moteur ne
plafonne rien (`r.Water.WaterInfo.RenderTargetResolutionMax` vaut 0).

**LE PLAFOND DE LA FENETRE SE CALCULE.** `RoundUpToPowerOfTwo(demi-etendue /
24 m)`, borne a 256 tuiles : 4 km -> 128, 8 km -> 256, **12,288 km -> 256
exactement**, 16 km -> 512 donc tuiles DIVISEES par deux. A 12,288 km la
boite du quadtree coincide pile avec la fenetre. Retenu, avec la texture a
4096 : **7,8 -> 3,0 m par texel**, soit un rivage 2,6 fois plus fin malgre
une fenetre triplee.

**LE SAUT DE LA FENETRE EST UN REGLAGE DU MOTEUR** : `UpdateMargin`
(WaterViewExtension.cpp:24) vaut 150 m, donc le recentrage n'a lieu qu'apres
**1 850 m** de marche sur une fenetre de 4 km. Agrandir la fenetre le repousse
au-dela de ce qu'on peut atteindre : inutile d'y toucher.

**AGRANDIR NE SUPPRIME PAS LA COUTURE, CELA LA DEPLACE.** Ce qu'on voit
derriere le bord n'est pas du vide : c'est le sol de fond, qui peint son
plateau cotier en PLAGE. Mesure au temoin : eau R78 G125 B153, plateau a sec
R196 G188 B174, et au zoom le trait droit TRAVERSE les dunes. Le remede qui
ferme la question est de peindre en mer tout le sol de fond sous zero --
couleur seulement, jamais la geometrie, et **jamais sur le voxel** : sous les
pieds on voit le fond a travers l'eau, et c'est ce qu'on veut.

**UN A/B PEUT ETRE RIGOUREUX ET PARFAITEMENT VIDE.** Deux captures au meme
point, au meme metre, a la meme altitude -- donc valides -- et tournees vers
les collines, sans une goutte d'eau, alors qu'il s'agissait de juger la mer.
`-WorldseedDepartX/Y` posent le joueur, elles ne disent rien de ce qu'il
REGARDE. D'ou `-WorldseedCap=`. **Une capture ne vaut que par son SUJET.**

**ET L'ECLAIRAGE INTERDIT L'A/B PAR LANCEMENTS SUCCESSIFS DANS CE PROJET.**
`Animate Time of Day` tourne, et trente secondes d'ecart au chargement font
douze minutes de jeu. Mesure : la crete rocheuse TEMOIN, au-dessus du niveau
de la mer donc hors d'atteinte du traitement, a bouge de **101 sur 765**
quand la zone testee bougeait de 15 -- le bruit a sept fois le signal, et
51 % de l'image « changee » sur toute sa surface. **Le remede n'est pas une
meilleure moyenne, c'est un TEMOIN DE COULEUR** : peindre la zone traitee en
magenta. Une couleur franche ne se compare a rien, elle est la ou elle n'est
pas. Avec, en plus, un COMPTE journalise -- « 5 939 134 sommets sur
8 388 608 sous zero » -- parce qu'une couleur invisible a deux causes
opposees, le terme qui ne s'evalue jamais et le terme dont rien n'atteint
l'ecran, qui n'appellent pas le meme remede.

**PIEGE DE MONTAGE PAYE SUR CE MEME ESSAI** : pour grossir le defaut j'ai
ramene la fenetre a 0,5 km. Le bord tombait alors a 250 m, donc DANS le rayon
voxel de 1 200 m -- la seule zone ou le terme testé ne s'applique pas. **On
peut pousser un defaut hors de portee du traitement qu'on veut juger.**

**LE GETTER DU PLUGIN N'EST PAS EXPORTE** : `SetRenderTargetResolution` porte
`UE_API`, `GetRenderTargetResolution` non (WaterZoneActor.h, lignes 66 et 67).
On peut ECRIRE depuis notre module, pas RELIRE -- et l'erreur n'arrive qu'a
l'EDITION DE LIENS, bien apres une compilation reussie. Relire par reflexion,
comme la fenetre glissante : c'est de toute facon la valeur que l'acteur
porte vraiment.

**RESTE OUVERT** : le pion ne reste pas ou on le pose -- 2,7 km de glissade
mesures depuis un depart a 22,1 degres de pente, sans ligne de filet au
journal, mais NON reproduit au lancement suivant. Hypothese non verifiee :
`PenteDepartMaxDeg` se lit sur la grille 2D a 15,6 m de maille quand le voxel
travaille au metre. Et aucune vue n'a ete prise depuis un VRAI sommet : 69,
229 et 315 m, pas 1 500.

### La pile sedimentaire est SOUS le relief qui montre la roche (22 septembre 2026)

Tache #24. La stratigraphie est calculee, elle coute dans l'erosion et dans le
sapement, la peinture des sommets lit bien `WorldseedStrata::BancAt` et le code
promet « exactement ce qui donne au Grand Canyon ses rayures ». **Les parois
sont unies.**

#### L'entonnoir, et il a fallu l'ecrire pour arreter de deviner

Une couverture nulle peut venir de cinq gardes differentes, et sans le compte de
chacune on regle au hasard la mauvaise -- ce que ce depot a deja paye sur le
routage des galeries puis sur les mesas. Releve au banc, autour d'un canyon :

    3 023 628 sommets
    sous la surface   79,5 %
    serie active      78,0 %
    TEINTES           28,6 %
    profondeur moyenne 13,0 m, de -35,3 a 181,0
    bancs touches  0:2145872  1:46203  2:25958  3:37426  4:24914
                   5:29451    6:28676  7:15720  8:2657   9:2019

**LE BANC 0 RAFLE 92 % DES SOMMETS QUI LISENT LA PILE**, et c'est toute la
reponse. `BancAt` rend le banc SOMMITAL pour tout ce qui se trouve au-dessus du
toit de la couverture -- ce qui est juste, un relief plus haut que le datum est
fait de la roche du sommet de la pile. Or la pile fait 253 m sous un datum a
520, donc elle couvre **267 a 520 m** dans un monde qui va de -351 a 1700. Les
canyons de ce monde sont a **556 et 580 m**, les massifs au-dessus de 1000 : ils
sont TOUS au-dessus du toit, donc tous d'un seul banc, donc unis.

La regle le disait d'ailleurs d'elle-meme : « ARBITRAIRE. A regler contre
l'altitude des terres -- ce monde va de -351 a 1700 m. » Elle n'a jamais ete
reglee contre.

#### Ce qui a ete ELIMINE, chiffre par chiffre

    la teinte de roche n'atteindrait pas la paroi
        -> FAUX : la couper change 99,5 % des pixels DE LA PAROI

    les bancs ne seraient pas lus
        -> FAUX : deplacer le datum de 520 a 620 change 99,0 % des pixels

    `BancAt` serait casse
        -> FAUX : oracle `Worldseed.Strates.LaPileChangeDeBanc` -- la pile
           traverse ses quatre etats en descendant, ne recule jamais, et deux
           altitudes d'une meme paroi rendent des bancs differents

    on verrait deja des bandes dans la vue large
        -> FAUX, et c'est le temoin qui l'a dit : SANS la teinte de roche la
           variation de teinte le long d'une colonne est PLUS FORTE qu'avec
           (ecart-type 12,1 contre 10,0). Ce que je prenais pour des bandes
           etait l'ombrage du versant.

#### Deux pieges de mesure payes sur cette seule tache

- **MESURER UNE FORME RARE AU MAUVAIS ENDROIT.** Le premier releve de
  l'entonnoir donnait « bancs touches AUCUN » -- et il etait pris au banc, qui
  nait sur une plaine cotiere a 4,9 m d'altitude. La pile s'arrete a 267 m :
  elle n'y arrive jamais. Le depot a exactement cette note depuis le
  19 septembre, pour les diaclases. Refaite.
- **UNE COLONNE DE PIXELS QUI VARIE N'EST PAS UNE BANDE DE ROCHE.** J'ai
  mesure R-B le long d'une paroi, vu osciller de 6 a 40, et conclu aux bancs.
  Le temoin -- la meme colonne, teinte coupee -- oscillait DAVANTAGE. Une
  variation n'a de sens que contre l'etat ou la cause supposee est absente.

#### CE QUI N'EST PAS FAIT, ET POURQUOI

**Aucun correctif.** Ou doit se trouver la pile, et doit-elle se REPETER en
profondeur plutot que s'arreter, est un arbitrage qui engage le rendu de tout
le monde -- et `ToitDuChapiteau` lit la meme pile pour poser les sommets de
mesa, donc y toucher deplace de la GEOMETRIE, pas seulement de la couleur. Cela
revient au proprietaire.

Outillage laisse en place : l'entonnoir au journal, et `-WorldseedDatum=` pour
deplacer le toit sans rouvrir le fichier de regles.

#### La pile est ENROULEE, et les diaclases sont hors de cause

Demande du proprietaire : « rends la pile cyclique pour voir si c'est mieux »,
puis « je pense que les rayures peuvent egalement etre a cause des diaclases,
peux-tu tester ? ».

**L'ENROULEMENT NE TOUCHE QUE LA COULEUR, et c'est ce qui le rend sur.**
`BancAt` n'a que deux appelants, tous deux dans la peinture des sommets. Les
corniches de mesa passent par `ToitDuChapiteau` et l'erosion par
`FWorldseedStrataK`, qui parcourent la pile pour leur compte. Aucune geometrie
ne bouge, donc aucune regeneration ni bump de version -- ce qui n'etait pas
evident avant de compter les appelants.

**LE RESULTAT, mesure sur la meme paroi :**

    pile bornee -> cyclique          99,4 % des pixels changes, 61,6/765
    diaclases coupees                 0,0 %                      1,0/765
    temoin, deux etats identiques     0,0 %                      2,2/765
    temoin, les deux mailleurs        0,9 %                      4,7/765

**LES DIACLASES NE CREUSENT RIEN SUR CETTE PAROI.** Zero pour cent, contre un
temoin a zero egalement -- donc la methode distingue, elle ne rend pas zero par
construction. Elles ne sont pour rien dans les terrasses de #21, qui restent un
defaut de NORMALES.

**ET LES BANDES SONT PEUT-ETRE TROP FRANCHES.** Le schiste (108/106/116) contre
la craie (236/234/226) donne un contraste de zebre, et les bandes epousent les
courbes de niveau -- ce qui peut se lire comme une carte topographique plutot
que comme de la roche. Cela se regle sans code, par les couleurs du catalogue.
Arbitrage laisse au proprietaire.

**L'INCOHERENCE ASSUMEE, ET IL FAUT LA SAVOIR** : au-dessus du toit, la COULEUR
raye desormais alors que le CHAPITEAU d'une mesa se cale toujours sur le premier
banc dur de la pile NON enroulee. La teinte d'une corniche peut donc ne pas etre
celle du banc qui la porte. Aligner la geometrie demanderait une regeneration.

**UN PIEGE DE MESURE PAYE ICI, ET C'EST LE PLUS COURANT DE TOUS.** J'ai d'abord
compare une image prise AVANT l'enroulement a une image prise APRES avec les
diaclases coupees, et conclu que les diaclases changeaient 99,5 % des pixels.
Les deux moities differaient par DEUX changements. Le controle qui a sauve la
mesure est celui que ce depot s'impose : valider la methode sur un cas dont on
connait la reponse -- deux etats reputes identiques doivent rendre zero. Ils ont
rendu 0,0 %, et la comparaison refaite proprement a rendu les diaclases
innocentes.

#### Le noir des parois n'est PAS une couleur, et sept etats le disent

Question du proprietaire apres l'enroulement de la pile : « je ne vois rien de
choquant a la base en terme de geometrie, ce qui fait bizarre c'est le rendu
des couleurs [...] si tu remplaces le noir par une couleur plus approchante de
l'autre, ces imperfections s'estompent-elles ? »

**LE SCHISTE ETAIT BIEN LE COUPABLE DESIGNE, ET IL LE MERITAIT A MOITIE.** Sur
les CINQ roches de la serie -- gres, schiste, dolomie, craie, calcaire, le
basalte n'y est pas -- il est a 108/106/116, le seul a la fois SOMBRE et
BLEUTE quand les quatre autres sont chauds et clairs (158 a 234 de clarte), et
il pese 97 m sur les 253 de la pile.

**ET POURTANT LE CHANGER N'ESTOMPE RIEN.** Vue large du canyon, meme monde,
horloge figee, ecart-type de la luminance sur les pixels de roche et de neige
-- c'est l'amplitude du zebre :

    A  actuel, schiste 108/106/116          53,5    11,8 % de pixels sombres
    B  schiste 150/142/130                  52,4     9,6 %      45 % changes
    C  schiste 175/166/152                  52,7    11,2 %      53 % changes
    D  LES CINQ ROCHES A LA MEME TEINTE     54,2    14,4 %      92 % changes
    E  lumieres coupees                     56,2    15,7 %      90 % changes
    F  lumieres coupees + teinte unique     51,3    10,9 %      66 % changes
    G  TEINTE DE ROCHE COUPEE               57,2    13,5 %      90 % changes
    H  diaclases coupees                    aucune difference visible

**L'ECART-TYPE NE QUITTE JAMAIS LA BANDE 51-57**, sur sept etats dont DEUX
suppriment toute couleur de roche et un supprime l'eclairage. Et les deux
suppressions les plus radicales -- D et G -- l'AUGMENTENT : la teinte de roche
et l'eclairage ATTENUAIENT legerement le zebre.

**LE 90 % DE PIXELS CHANGES EST CE QUI REND CES ZEROS CREDIBLES.** Une moitie
d'A/B qui n'a pas pris rend elle aussi « aucun effet », et ce depot s'y est
deja laisse prendre. Ici chaque levier DEPLACE massivement l'image ; ce qu'il
ne deplace pas, c'est le contraste.

**CE QUE LE CHIFFRE DESIGNE A LA PLACE : LA DISTANCE.** La meme paroi, au pied
puis de loin :

    au pied de la paroi    2,2 % de pixels sombres, ecart-type 24,6
    de loin               11,8 %                              53,5

Cinq fois plus de noir a distance, deux fois l'amplitude, sur la MEME geometrie
et la MEME palette. Un defaut qui croit avec la distance n'est pas dans la
matiere, il est dans l'ECHANTILLONNAGE -- les anneaux maillent a 2 puis 4 m de
voxel. C'est exactement la note du 21 septembre sur les diaclases, dont le
remede n'a jamais ete ecrit : un detail plus fin que la maille ne doit pas etre
echantillonne, il doit etre ATTENUE, comme le fait un mip-map. La passe H dit
que ce ne sont pas les diaclases ; le mecanisme, lui, vaut pour toute forme
fine.

**CONSEQUENCE POUR L'HABILLAGE, ET ELLE VA CONTRE L'INTUITION DU
PROPRIETAIRE** : « ce sont des choses qui s'estomperont avec de vraies
textures ». Pour le COTELE peut-etre ; pour le NOIR, non -- une texture ne
change que l'albedo, et l'albedo est precisement ce que sept etats viennent
d'innocenter. Il valait mieux le savoir avant de texturer.

**LE SCHISTE MERITE QUAND MEME D'ETRE RETOUCHE**, pour l'autre raison : il est
le seul bleute d'une serie de cinq roches chaudes, ce qui se voit. B ou C sont
defendables et ne coutent qu'une ligne du fichier de regles. A arbitrer a part,
sans en attendre le moindre gain sur le zebre.

**LES SURCHARGES AJOUTEES, et elles ne touchent pas au fichier de regles** --
son empreinte regenererait le monde entre les deux moities :

    -WorldseedRoche=<cle>:<R>,<G>,<B>[;<cle>:...]   remplace UNE teinte
    -WorldseedContrasteRoches=<0..1>                 rapproche TOUTES les
        teintes de la serie de leur moyenne PONDEREE PAR L'EPAISSEUR des bancs.
        A zero la serie est d'une seule couleur, donc aucune bande possible :
        c'est le temoin D, et c'est lui qui a ferme la question.

**PIEGE D'OUTILLAGE, paye deux fois dans l'heure** : un `-ExecCmds="ShowFlag.
Lighting 0"` passe a un script PowerShell qui fait `$Extra.Split(" ")` arrive
comme DEUX arguments, et le drapeau est silencieusement perdu. La premiere
passe « sans lumieres » etait donc une copie de la passe temoin -- et elle
ressemblait assez a l'originale pour qu'on la croie. Ce qui l'a trahie est le
compte : 90,4 % de pixels changes la seconde fois contre presque rien la
premiere. **Un harnais qui compose une ligne de commande doit passer un
TABLEAU d'arguments, jamais une chaine a decouper.**

**ET LE HUITIEME ETAT A TRANCHE : CE SONT LES ANNEAUX.** Terrain maille a 1 m
de voxel PARTOUT (`-WorldseedNiveaux=0`), meme camera, meme monde, meme
palette -- donc un A/B propre, contrairement a la comparaison pres/loin :

    I  anneaux coupes, 1 m partout           48,8     9,1 % de sombres

A l'oeil c'est net, les parois passent du NOIR PERFORE AU GRIS et la structure
en gradins reste -- elle, c'est de la vraie geometrie. Au chiffre c'est plus
modeste : **-23 % de pixels sombres et -9 % d'amplitude**. Les anneaux sont
donc bien une cause, la seule que huit etats aient fait bouger, et ils
n'expliquent pas tout le noir.

**LA COMPARAISON PRES/LOIN N'EST PAS UN A/B, et je l'avais d'abord presentee
comme tel.** 2,2 % de pixels sombres au pied contre 11,8 en vue large, sur la
meme geometrie et la meme palette -- mais pas sur le meme CADRAGE : le gros
plan porte bien plus de ciel et de neige, donc l'ecart melange le defaut et la
composition. Il reste indicatif ; seul A contre I compare a camera egale.

**RESERVE SUR LA VUE I, dite parce qu'elle a failli l'invalider** : a 1 m de
voxel sur 1200 m de vue, le monde met une dizaine de minutes a se batir, et ce
registre note qu'on y photographie parfois une FILE D'ATTENTE plutot qu'un
monde. Controle fait avant de conclure : le terrain est complet jusqu'a
l'horizon, sans trou ni reprise par le sol de fond. La vue est valide.

#### La peinture n'ecrit JAMAIS de noir, et c'est la carte des causes qui l'a dit

Question du proprietaire, et c'est elle qui a debloque le diagnostic : « as-tu
la possibilite de savoir a partir de quel moment tu affiches du noir ou du
brun ? car si tu savais quand est-ce que tu mets du noir on pourrait remonter
la piste ».

**ELLE EN CONTENAIT UNE PLUS FONDAMENTALE, QUE HUIT ETATS N'AVAIENT JAMAIS
POSEE : la peinture ECRIT-elle seulement du noir ?** Tant qu'on ne le sait
pas, on cherche la cause d'une couleur dont on n'a jamais verifie qu'elle
vient de nous -- et c'est exactement ce que la journee avait fait, en
eliminant une a une des hypotheses qui portaient toutes sur l'albedo.

**LE RELEVE, sur 9,26 millions de sommets :**

    luminance ECRITE   66 a 234 sur 255
    SOMBRES (< 70)     0,05 %
      biome    70,9 %  dont 0,00 % sombres
      banc     28,5 %  dont 0,00 % sombres
      roche 2D  0,6 %  dont 7,73 %   <- le basalte, a 66 : tout le 0,05 %

Les captures comptent **11,8 % de pixels noirs**. La peinture en ecrit 0,05.
Facteur 236.

**LA CARTE DES CAUSES (`-WorldseedCarteCauses=1`) rend la reponse visuelle.**
Chaque sommet est peint par la BRANCHE qui a decide sa couleur, en aplats
francs et TOUS CLAIRS -- magenta pour le repli, vert pour le biome, bleu pour
la roche 2D, une teinte vive par banc. Aucun aplat n'etant sombre, **tout
pixel noir sur cette image serait par construction quelque chose que nous ne
peignons pas**. C'est le temoin de couleur du depot applique a un diagnostic :
« une couleur franche ne se compare a rien, elle est la ou elle n'est pas ».

Resultat : **AUCUN pixel noir sur le terrain**, et chaque paroi qui etait noire
est un arc-en-ciel de bancs. Les dix bancs sont tous a l'ecran, de 486 000 a
946 000 sommets chacun. **Les rayures SONT les strates, et elles sont peintes
juste.**

**LA CARTE SERT ENSUITE DE MASQUE SUR LE RENDU NORMAL**, les deux passes ayant
la meme camera et le meme monde donc des images alignees au pixel :

    branche        part    ecrit noir    lum. moy.  minimum   noir a l'ecran
    biome          62,9 %     0,00 %       151,2        3         8,7 %
    banc           16,0 %     0,00 %       134,4        3        19,1 %
    roche 2D        3,2 %     7,73 %        85,7        6        37,8 %
    hors terrain   17,2 %        --        153,9        3         4,6 %

**LA BRANCHE BIOME N'ECRIT JAMAIS SOUS 70 ET 8,7 % DE SES PIXELS ARRIVENT SOUS
70, JUSQU'A 3.** C'est la preuve, et elle ne souffre pas de discussion :
l'assombrissement se produit APRES la couleur de sommet, et il touche toutes
les branches -- y compris celles qui n'ecrivent rien de sombre.

**ET CE N'EST PAS QUE L'OMBRE.** Lumieres coupees, le meme croisement donne
13,6 % pour le biome et 23,6 % pour les bancs -- PLUS, pas moins. C'est la
chaine exposition + tonemapping, dans une scene dont la neige est a 236
d'albedo : un banc a 108 n'a pas de place dans la meme image.

**LES BANCS ENCAISSENT DEUX FOIS PLUS QUE LE BIOME** -- 19,1 contre 8,7 % --
parce qu'ils habillent les CONTREMARCHES des gradins, des faces raides qui
prennent mal la lumiere et que le maillage dentelle.

**D'OU LA PISTE : LA MARCHE, ET NON PLUS LA MATIERE.** Ce que la mesure etablit
est que le noir est l'OMBRE de surfaces raides, et que les bancs y tombent deux
fois plus que le biome parce qu'ils habillent ces surfaces-la.

**CE QUI N'EST PAS MESURE, ET J'AI FAILLI L'ECRIRE COMME SI.** Que les gradins
soient TAILLES PAR LA STRATIGRAPHIE -- erosion stratifiee et sapement des
corniches -- est une INFERENCE tiree de la carte des causes, pas une mesure.
Les bandes de couleur sont horizontales PAR CONSTRUCTION (`BancAt` ne depend
que de Z), et les gradins sont horizontaux aussi : leur coincidence a l'image
peut n'etre que celle de deux choses independamment horizontales. **Deux
grandeurs qui se ressemblent a l'ecran ne sont pas pour autant liees**, et ce
depot a deja paye la version voisine de cette faute -- prendre l'ombrage d'un
versant pour des bandes de roche.

Le controle qui trancherait ne demande PAS de regeneration : relever un
transect vertical du relief 2D sur une paroi de canyon et comparer l'espacement
des marches aux epaisseurs de bancs (18 a 35 m), et leurs altitudes aux limites
de bancs (datum 520 moins les epaisseurs cumulees). Si les marches tombent sur
les limites, la stratigraphie les a faites ; sinon la piste est ailleurs.

**TROIS INSTRUMENTS POSES, ET ILS RESSERVIRONT :**

    -WorldseedCarteCauses=1   la carte, a regarder avec ShowFlag.Lighting 0
    -WorldseedArrets=canyon   ne photographier que ce qu'on juge
    (toujours)                l'histogramme des couleurs ecrites, au journal

**ET LA TOURNEE DIT DESORMAIS CE QU'ELLE A PHOTOGRAPHIE.** Le releve du
terrain n'existait que dans le BANC -- et c'est exactement ce qui avait fait
mesurer l'entonnoir de la teinte de roche au mauvais endroit, au point
d'apparition du banc, une plaine cotiere a 4,9 m ou la serie (267 a 520 m) ne
monte jamais. Le releve annoncait « bancs touches AUCUN » pour un monde qui en
touche dix.

**UN ARRET NE COUTE PAS UNE CAPTURE, IL COUTE UN REMPLISSAGE.** Le diffuseur
relache tout ce qui sort du rayon, donc onze arrets sont onze remplissages
complets -- une dizaine de minutes a resolution uniforme. Quatre arrets
rendent l'A/B REPETABLE, et c'est ce qui compte : ce depot a deja conclu sur
des moities d'A/B qu'il n'avait pas les moyens de refaire.

**PIEGE POWERSHELL, PAYE DEUX FOIS DE SUITE ET IL EST VICIEUX : LES VARIABLES
Y SONT INSENSIBLES A LA CASSE.** Un parametre `[string]$Rendu` et une variable
locale `$rendu` sont LA MEME VARIABLE, et le typage du parametre gagne :
affecter un bitmap a `$rendu` le CONVERTIT silencieusement en la chaine
« System.Collections.Hashtable ». La boucle a ensuite indexe un tableau nul
deux millions de fois et rendu **192 Mo d'erreurs identiques** a la place
d'une mesure. Deux regles : ne jamais reutiliser le nom d'un parametre pour
autre chose, et **faire tomber un script de mesure TOUT DE SUITE et FORT** --
un `Test-Path` avec `throw` en tete de script aurait coute une ligne et rendu
l'erreur en une seconde.

#### Les gradins SONT cales sur les bancs, et la pile ne couvre que 13,7 % du terrain

Suite directe du diagnostic du noir. J'avais ECRIT au registre que l'erosion
stratifiee taillait les marches, sur la foi d'une carte des causes -- puis
corrige en disant que c'etait une inference. `ProbeMarches` la mesure.

**CE QU'ELLE MESURE : LA PHASE.** Pour chaque cellule de paroi de canyon, sa
position DANS son banc -- 0 au toit, 1 a la base -- puis le nombre de points,
la pente moyenne et la part de plats par dixieme de banc. Une stratigraphie qui
taille des marches doit concentrer la surface et l'aplatir au TOIT des bancs.

    casier   points (x attendu)   pente moy.   part de plats
    0.0-0.1      4494  (x3.66)      31,4 deg      52,0 %
    0.1-0.2       836  (x0.68)      51,0            2,8 %
    0.3-0.4      1202  (x0.98)      42,8           22,0 %
    les six autres  ~820 (x0.67)    ~50,8          ~2,3 %

**LA SURFACE SEJOURNE AU TOIT DES BANCS**, trois fois et demie plus que le
hasard ne le voudrait, elle y est deux fois moins pentue, et **la moitie de ces
points sont PLATS contre deux pour cent ailleurs**. C'est la banquette, et
c'est exactement le mecanisme que `WorldseedSapement` decrit.

**DEUX TEMOINS, ET IL EN FALLAIT DEUX -- LE PREMIER JET N'EN AVAIT QU'UN, ET IL
NE POUVAIT RIEN REFUTER.** J'avais compare des AMPLITUDES entre la vraie phase
et une phase DECALEE. Or l'amplitude d'une courbe est INVARIANTE PAR
TRANSLATION : decaler les limites de bancs ne detruit pas la structure, il la
DEPLACE, donc le temoin rendait le meme chiffre quoi qu'il arrive. Il annoncait
20,6 % contre 18,7 et concluait « aucun calage » -- l'inverse de la verite.

    TEMOIN HASARD, phase tiree au hachage de la cellule :
        concentration x1,03 et amplitude de pente 3,2 % contre 41,9 %
    TEMOIN DECALAGE de 12 m, le pic doit BOUGER :
        casier 6 contre casier 0

Le hasard DETRUIT la structure et rend le bruit d'echantillonnage ; le decalage
la DEPLACE. Les deux ensemble disent qu'elle est reelle. **Un temoin qui ne
peut pas rendre un resultat negatif n'est pas un temoin.**

**SECONDE FAUTE DU PREMIER JET, ET ELLE EST PIRE** : je filtrais les cellules
sous huit degres de pente « pour ne garder que les parois ». J'ecartais donc
exactement les BANQUETTES -- la partie plate d'une marche -- c'est-a-dire la
moitie de ce dont je testais l'existence. **On ne cherche pas un escalier en
jetant ses marches.** Le tri est desormais SPATIAL, par le relief local.

**ET LE CHIFFRE QUI CHANGE LA LECTURE DU ZEBRE :**

    couverture de la pile, sur les fenetres de canyon
        13,7 % des cellules DEDANS
        64,4 % AU-DESSUS du datum
        21,9 % sous la pile

La geometrie n'a de bancs que dans la pile BORNEE, 267 a 520 m -- l'erosion
stratifiee et le sapement lisent celle-la, l'enroulement du 22 septembre
n'ayant touche que `BancAt`, donc la couleur. **Sur pres des deux tiers du
terrain de canyon, on peint donc des rayures sur un relief qui n'en porte
aucune : de la strate sans marche.** C'est, mesuree, la consequence de
l'incoherence signalee en livrant la pile cyclique -- et c'est tres
probablement ce que le proprietaire designait par « la pile cyclique fait
beaucoup de mal aux canyons ».

**CE QUI RESTE VRAI DU DIAGNOSTIC PRECEDENT** : le noir n'est pas une couleur
(66 a 234 ecrits, 0,00 % sur la branche des bancs), et il nait de l'ombre de
surfaces raides. On sait maintenant d'ou viennent ces surfaces la ou la pile
mord, et qu'ailleurs elles ne viennent pas des bancs.

**PIEGE D'OUTILLAGE, PAYE DEUX FOIS DANS L'HEURE** : `sed` pour inserer du C++
multi-ligne mange les barres obliques inverses d'un chemin Windows -- `"$t\$X\$V.png"`
est devenu `"$t$X$V.png"` en silence, et le script a lu un fichier inexistant.
Pour une insertion de code ou un chemin echappe, employer l'editeur de fichiers
plutot qu'une substitution.

### La zebrure est biome-contre-roche, et le noir est de l'ombre (23 septembre 2026)

Signale : « est-ce que tu as reussi a attenuer les rayures ? ». **Non** -- et la
question a fait remonter toute la piste, par la carte des causes.

**LA CHAINE, CHIFFREE, ET C'EST ELLE QUI TRANCHE :**

| ou l'on regarde | plancher de luminance | pixels sous 40 |
|---|---|---|
| ce que la peinture ECRIT | 66 | 0,05 % (le basalte seul) |
| rendu SANS eclairage | **65,2** | **0,00 %** |
| carte des causes | 68,1 | **0,00 %** |
| rendu ECLAIRE | **0,0** | **18,5 a 22,1 %** |

Le plancher rendu colle au plancher ecrit et la carte des causes n'a pas un pixel
noir : **la couleur atteint l'ecran intacte, il n'y a ni trou ni surface non
peinte**. Le noir est donc fabrique **a 100 % par la passe d'eclairage**.

**ET LA CARTE DES CAUSES DIT AUSSI CE QU'EST LA ZEBRURE.** Les bandes alternent
**branche biome contre branche roche**, et elles epousent l'escalier : les
MARCHES gardent la couleur du biome, les CONTREMARCHES prennent celle de la
roche, parce que la profondeur sous la surface macro saute a chaque ressaut.
**L'alternation n'est PAS banc contre banc.** C'est ce qui explique enfin
pourquoi les huit etats de palette du 22 septembre -- schiste reteinte, serie
uniforme, teinte de roche coupee -- n'avaient jamais deplace le contraste : ils
agissent tous sur la branche roche SEULE.

**Les deux zebrures ont donc UNE cause, l'escalier.** L'une le lit par la
couleur, l'autre par l'ombre.

**LE LEVIER EST `voxel.couleurRocheFonduM`** -- porte a 28 m puis **ANNULE le
meme soir, decision du proprietaire** : la moitie couleur ne se voyait pas en
jeu a cote du noir, et il a prefere ne pas garder une modification qui ne regle
pas ce qu'on voit. La regle vaut donc toujours **12,0**. Le releve ci-dessous
reste vrai et se rejoue par `-WorldseedCouleurRoche=`, garde a dessein puisque
la question est ROUVERTE. Saut maximal de
luminance entre bandes voisines : 82,4 -> 53,8. La valeur est DERIVEE et non
choisie -- roche pleine a `overhangAmplitudeM + detailAmplitudeM + fondu`, donc
28 la place aux quarante metres du cas de reference cite par la regle. 45 et 60
font mieux (39,8 et 31,5) et CASSENT cette garantie.

**ESSAYE ET MESURE COMME MOINS BON : le retour a la pile bornee.** Il change bien
33,8 % de la paroi, mais la carte des causes bornee montre la MEME alternance en
deux teintes au lieu de l'arc-en-ciel, il ne gagne que 88 -> 56, et il DEGRADE le
noir (18,5-19,2 -> 21,0 %). Innocentes de meme : la porte perpendiculaire et les
diaclases (18,46 / 18,15 / 17,83 %).

**PIEGE NEUF ET SILENCIEUX : `-DPCVars=` NE PEUT PAS POSER UN CVAR `ECVF_Cheat`.**
`ShowFlag.Lighting` en est un. Le moteur refuse et ne le dit QUE dans le journal :

    Error: The ini file 'DeviceProfiles' tries to set the console variable
    'ShowFlag.Lighting' marked with ECVF_Cheat, this is only allowed in
    consolevariables.ini

J'en ai conclu « le noir survit aux lumieres eteintes » alors que je mesurais
deux fois la meme image -- 18,46 contre 18,75 %. **Sixieme fois que ce depot
rencontre deux mesures presque identiques pour deux reglages differents.** La
voie qui marche est `-ExecCmds="ShowFlag.Lighting 0"`, et le journal doit montrer
`ShowFlag.Lighting = "0"`.

**COROLLAIRE POWERSHELL** : `-ExecCmds="..."` contient un ESPACE, donc un
`-ArgumentList` en TABLEAU perd ses guillemets internes. Passer **une seule
chaine brute** a `Start-Process`. `-DPCVars=nom=valeur` n'a pas ce probleme
(aucun espace), mais il ne peut pas tout poser -- voir ci-dessus.

**LA COMPARAISON IMAGE CONTRE IMAGE ENTRE DEUX LANCEMENTS EST INUTILISABLE
ECLAIREE** : **82,1 %** de pixels changes entre deux passes reputees identiques,
ciel fige compris -- Lumen et TSR ne convergent pas pareil. **Sans eclairage,
0,1 %.** Donc tout A/B de COULEUR se fait lumieres eteintes ; eclairee, seules
les statistiques agregees (part de sombres, plancher) veulent dire quelque chose.

**PIEGE DE MESURE ANNEXE** : un `%` de pixels changes ne dit pas le SENS. Les
6-7 % mesures a l'interieur d'une arche a fondu 60 sont la frange beige de la
BOUCHE, pas la paroi -- il a fallu regarder l'image pour le voir. Et une arche ne
vaut pas une chambre : son pont fait 7 a 26 m, donc elle ne peut pas eprouver une
garantie posee a quarante metres. **Il n'existe aucun arret photo de chambre.**

**`-WorldseedArrets=` accepte une LISTE** (`canyon,arche`), ce qui rend un A/B
sur deux familles abordable.

**L'EMPREINTE DES REGLES EST UN MD5 DU FICHIER ENTIER** (`WorldseedRules.cpp:144`)
: un reglage de PEINTURE invalide donc tous les mondes en cache et force une
regeneration -- 201 s en 4096x2048. Le monde obtenu est identique (terres 29,2 %
avant comme apres) ; il n'y a rien a relancer, seulement a la payer une fois.

**PISTE NON EXPLOREE, ET ELLE EST DEJA ECRITE DANS LE CODE** :
`WorldseedVoxelTerrain.cpp:479` soutient que les terrasses sont l'ALIASING du
dernier octave de detail -- « quatre octaves depuis douze metres descendent a un
metre et demi, soit la taille du voxel », donc sous Nyquist -- et qu'il ne se voit
que sur les parois parce que le detail est module par la pente. `-WorldseedDetailOctaves=`
permet de l'eprouver sans recompiler. C'est la piste a prendre pour le NOIR.

### Les regions et les pays : quatre seuils, et trois etaient faux (26 septembre 2026)

Le monde se decoupe desormais en REGIONS geographiques groupees en PAYS,
nommees, mises en cache. Mesure sur le monde de reference (graine 1337,
64 x 32 km, 593,3 km2 de terres) : **47 regions, 8 pays**, aires de 6,3 a
32,7 km2, mediane 12,5 ; partition parfaite -- 0 terre sans region, 0 mer
avec, 0 identifiant hors bornes -- et 55 noms distincts sans un doublon.

**LE DECOUPAGE EST GEOGRAPHIQUE, ET C'EST TOUT SON OBJET.** Le Voronoi des
plaques dessine deja les continents ; il ne dit rien de ce qui fait qu'un pays
tient ensemble. Ce qui le dit est le BASSIN VERSANT -- une vallee dont toutes
les eaux se rejoignent est une unite humaine avant d'etre hydrologique : les
routes y suivent les rivieres, les villes s'y posent aux confluences, et les
cretes font les frontieres.

**ET C'EST LA SEULE GRANDEUR DE CETTE CHAINE QUI NE SOIT PAS POSITIONNELLE.**
Tout le reste -- roche, biome, vegetation, cavites, mesas -- se recalcule
cellule par cellule sans rien savoir des voisines, et c'est cet invariant qui
permet de tout rejouer. Un bassin versant ne le peut pas : savoir ou s'ecoule
une cellule demande de suivre la pente jusqu'a la mer. **Sa mise en cache
n'est donc pas une commodite mais une NECESSITE** -- l'oublier ne couterait
pas du temps, comme pour les cavites ou les sites, il couterait les
frontieres.

#### Trois seuils poses a l'intuition, trois fois faux

1. **`littoralPart` a 0,45 -- « une cellule sur deux borde l'eau ».** Il n'a
   JAMAIS mordu : zero region littorale sur quarante-sept. La part mesuree va
   de 0,005 a 0,161, mediane 0,051. La raison est d'echelle -- une region de
   12 km2 sur une maille de 62 m compte des milliers de cellules pour quelques
   centaines de rivage. Recale a **0,09** : cinq regions littorales.
   **Ce depot a EXACTEMENT la meme note pour la pente** (« un plafond a 25
   degres ne gardait que 36 % des terres sur un monde dont la pente mediane
   vaut 30,6 ») et pour le seuil des diaclases (« un seuil n'est pas une
   part »). Troisieme fois. La sonde rend desormais la distribution, pour que
   le prochain calage ne se fasse pas a l'aveugle.
2. **Les ilots faisaient region.** L'agglomeration absorbe une miette dans son
   voisin de plus longue frontiere -- mais une ile n'a AUCUN voisin terrestre,
   donc aucune frontiere par ou l'absorber. Mesure avant correction, sur un
   banc de 16 x 8 km : **16 regions dont la MEDIANE d'aire valait 0,0 km2** --
   une cellule -- et **14 pays pour 16 regions**, chaque ilot formant le sien.
   La partition etait pourtant parfaite. Un oracle de partition ne voit pas
   cela ; un QUANTILE le montre d'un coup. Corrige par un rattachement par
   PROXIMITE, ce qui est aussi la geographie reelle des archipels : 3 regions
   de 10,8 a 15,3 km2, un pays.
3. **La coupe en hautes et basses terres casse la connexite.** Elle se fait a
   un SEUIL D'ALTITUDE, et rien n'assure que les cellules au-dessus se
   touchent : deux sommets separes par un col sous la mediane donnent une
   « region » en deux morceaux disjoints, portant UN nom pour deux endroits
   sans rapport. Aucune erreur, aucune mesure de travers -- seulement une
   frontiere qui saute par-dessus une vallee. Trouve en RELISANT la conception
   avant de tester, pas par un test. D'ou une passe de composantes connexes
   suivie d'une seconde agglomeration.

#### Une langue par PAYS, jamais par region

Le premier jet donnait sa base a chaque region d'apres son propre caractere.
Le resultat aurait ete une bouillie : deux vallees voisines d'un meme royaume,
l'une un peu plus haute que l'autre, l'une nommee en nain et l'autre en
francais. **Une frontiere politique separe des langues ; un col n'en separe
pas.** C'est donc la region la plus ETENDUE du pays qui donne le ton, et
toutes les autres la suivent -- verifie a l'oeil : le pays celtique
*Abermurodia* porte Aweryles, Erynoldynon, Llantobron, Trerynolloch.

#### UNE SUBSTITUTION QUI NE MATCHE PAS NE DIT RIEN

`EcrireRegions` n'a jamais ete insere dans `Save` : le `perl -i -pe` n'avait
pas trouve sa ligne -- fins de ligne CRLF -- et j'ai lu sa verification trop
vite. `LireRegions`, elle, etait branchee : la lecture partait au-dela de la
fin du flux et echouait sur ses bornes.

**C'EST L'ORACLE D'ALLER-RETOUR QUI L'A DIT, EN UNE LIGNE**, et c'est
exactement ce pour quoi il avait ete ecrit quatre jours plus tot. Le controle
apres une substitution se fait sur le RESULTAT -- `grep` du texte insere --
jamais sur l'absence d'erreur : `perl -i -pe` qui ne trouve rien sort zero.

**ET L'ORACLE A ETE RENFORCE POUR COUVRIR CE QU'IL NE VOYAIT PAS** : la
grille, chaque champ de chaque region, chaque membre de chaque pays -- avec un
controle A L'ENVERS sur les noms, qui NE doivent PAS traverser. Ils se
rejouent depuis des corpus qui n'entrent pas dans l'empreinte du cache ; les
serialiser figerait les noms d'un monde a ceux du corpus du jour de sa
generation, et retoucher une base n'aurait plus d'effet sur les mondes deja
joues, sans que rien ne le dise.

