# Registre Worldseed — comment on mesure

> **CE FICHIER NE SE LIT PAS EN ENTIER, IL SE CHERCHE.** Il porte le recit des
> seances : ce qui a ete mesure, ce qui a tranche, et surtout **les pistes
> abandonnees**, pour qu'on ne les retente pas. Les REGLES qu'on en a tirees
> vivent dans `CLAUDE.md`, chargé a chaque tour ; ici se trouve la PREUVE.
>
> ```
> grep -n -i '<le mot du sujet>' Docs/registre/*.md
> grep -A40 'le titre exact' Docs/registre/outillage.md
> ```
>
> **Le contenu est CUMULATIF.** Certaines notes decrivent un etat qui n'existe
> plus ; les plus dangereuses portent une marque de peremption. Avant d'agir sur
> une note, verifier que ce qu'elle nomme existe encore — une note qui survit a
> ce qu'elle decrit coute plus cher que pas de note.
>
> **Ce fichier** : cache du monde, oracles d'automation, bancs, performance, memoire, refactors, menage, et les protocoles de mesure qui ont echoue.

### Toute mesure de performance pilotee par MCP exige de couper le bridage (19 septembre 2026)

**C'est le piege qui m'a le plus coute sur ce projet a ce jour**, parce qu'il ne
se signale pas : il rend des chiffres plausibles, stables, reproductibles -- et
faux. Une journee entiere de conclusions de performance a ete batie dessus avant
que deux lectures identiques au centieme pour deux reglages differents ne
trahissent l'affaire.

**CE QUI SE PASSE.** L'editeur non focalise bride son rendu, et c'est le cas
PERMANENT quand on le pilote par MCP depuis un terminal. `PerformanceService`
rapporte alors un temps de trame plafonne par le bridage, et l'attribue au GPU
avec un verdict "GPU-bound" et un conseil de profilage GPU parfaitement
convaincant.

**LE SIGNE QUI TRAHIT, et il est fiable** : `frame_ms` vaut EXACTEMENT `gpu_ms`.
Sur une scene reellement limitee par le GPU, la trame depasse toujours un peu le
temps GPU. Quand les deux sont egaux au centieme, la trame est plafonnee par
autre chose.

**LE REMEDE, deja documente en section 7 et que je n'avais pas applique** :

    unreal.PerformanceService.set_background_throttling(False)

**L'ECART MESURE SUR LA MEME SCENE** : 39 a 48 ms de "GPU" et 21 a 33 FPS avec le
bridage, **3,4 ms de GPU et 210 a 224 FPS sans**. Un facteur dix.

**A RETENIR AUSSI** : trois lectures IDENTIQUES au centieme ne prouvent pas la
stabilite, elles peuvent prouver que la trame n'avance pas. Le controle qui
tranche est de changer quelque chose de visible entre deux lectures et de
verifier que le chiffre bouge.

### Assainissement : ce qui a ete decoupe, et ce qui reste (19 septembre 2026)

Question du proprietaire : « on est d'accord que tout ces calculs sont fait en
C++ et le code est bien decoupe de maniere atomique au niveau des classes et
des responsabilites ? » La reponse mesuree etait **oui pour le C++, non pour
trois fichiers**, et il a demande l'assainissement.

**LE C++ : SANS RESERVE.** Toute la chaine que le jeu execute est dans
`Source/Worldseed/Procedural/`.

*(Cette note disait, une heure avant sa suppression, que les 5 770 lignes de
Python de `Tools/WorldGen` etaient LEGATAIRES. Elles ont ete supprimees le meme
jour -- voir « Le Python est parti » en fin de fichier. Il reste `Tools/UE`,
douze scripts d'automatisation d'EDITEUR, gardes a dessein jusqu'au portage de
la vegetation : ils visent un Landscape qui n'existe plus, mais `vegetation.py`
porte le semis PCG et c'est la seule implementation qui en existe.)*

**CE QUI ETAIT PROPRE, et c'est la majorite.** Chaque passe de la chaine est un
module autonome : un espace de noms, une structure de regles chargee du JSON,
et UN point d'entree -- `Tectonics::Generate`, `Climate::Generate`,
`Erosion::Run`, `Lithology::Compute`, `Coast::Build`, `Biomes::Classify`,
`Fins::SlotAt`. Elles prennent des entrees, rendent des sorties, ne gardent
aucun etat cache, et tournent depuis n'importe quel fil. C'est ce qui a permis
d'inserer la passe littorale en une ligne au bon endroit.

**TROIS CHANTIERS FAITS, chacun verifie par un releve identique au chiffre
pres :**

1. **`WorldseedCaves::Build`, 1 254 lignes dans UNE fonction** portant onze
   responsabilites. Decoupee en DIX TEMPS NOMMES -- `Semer`, `Relier`,
   `Creuser`, `Composantes`, `Effondrer`, `Dissoudre`, `Ouvrir`, `Percer`,
   `Indexer`, `Verifier` -- portes par un CHANTIER (`FChantierGrottes`).
   Plus longue fonction du fichier : **1 254 -> 311 lignes**.

   *Pourquoi une structure et non dix fonctions libres* : la passe porte une
   trentaine de grandeurs qui traversent les temps. Les faire circuler en
   parametres donnerait des signatures de vingt arguments, moins lisibles que
   le probleme. Les nommer une fois DIT ce qui est commun.

   *Trois promotions imposees par le compilateur, et elles sont instructives* :
   `TropPres` et `Trouver` deviennent des methodes (les quatre formes
   d'ouverture s'en servent), `Ctx` traverse jusqu'a la verification finale.
   Chacune designait une grandeur reellement partagee que la fonction unique
   masquait.

   *Collision de noms* : `Router()` existait deja comme fonction libre -- c'est
   l'A* lui-meme. Le TEMPS qui l'emploie s'appelle donc `Creuser`.

2. **La tournee photo sortie de `AWorldseedVoxelTerrain`**, en
   `UTickableWorldSubsystem`. Elle y vivait par COMMODITE -- le minuteur du
   terrain offrait un fil du temps -- pas par decision : un acteur qui diffuse
   des chunks n'a pas a savoir cadrer une photo. Terrain **426 + 1 490 ->
   395 + 1 148** lignes.

   Le terrain expose en echange ce qu'il possede legitimement, en lecture
   seule : geometrie, altitudes, reseau de grottes, champ de densite, etat du
   filet du joueur.

   **PIEGE DU SOUS-SYSTEME, paye comptant** : `OnWorldBeginPlay` arrive AVANT
   celui des acteurs. Le terrain n'existe pas encore, et surtout son monde
   n'est pas charge -- une minute a la premiere generation. Construire la
   tournee la donnait zero vue ET AUCUNE LIGNE DE JOURNAL, parce que tout
   sortait sur le premier test de validite. On ARME au begin play, on CONSTRUIT
   au premier tick ou le monde est la. Et l'on compte des SECONDES, pas des
   tics : le tick d'un sous-systeme suit la trame, qui varie.

3. **`WorldseedProbeLibrary`, 1 661 lignes de fourre-tout**, eclate par domaine
   en quatre fichiers (monde 2D 455, voxel 150, roche 363, cavites 674), avec
   un socle commun `FWorldseedSonde::Preparer`.

   *Le socle n'est pas qu'une economie de lignes* : les huit sondes repetaient
   le meme preambule de vingt lignes, et le depot a deja paye ce que cela
   coute -- `probe_voxel` avait OUBLIE `SetLithology`, annoncait « 2,67 ms par
   chunk, le bruit est gratuit », et mesurait le monde d'avant. Le vrai chiffre
   etait 3,65.

**PIEGE D'OUTILLAGE DU REFACTOR, paye deux fois** : decouper par BORNES DE
LIGNES ne tient pas -- les numeros bougent des qu'on touche au fichier, et deux
tentatives ont produit des coupes en plein milieu d'une declaration. Le
decoupage par NOM DE FONCTION, en suivant les accolades, est robuste : c'est
celui qui a marche.

**ET LA VERIFICATION D'UN REFACTOR SE PREND AVANT DE TOUCHER AU FICHIER.** La
mesure de reference a ete relevee en premier ; sans elle, « ca compile » n'est
pas une preuve. Releve identique avant et apres les trois chantiers :

    822 chambres, 944 liaisons (123 de boucle), 14145 troncons,
    14 abandonnees, 8 reseaux, 66 bouches, 574 GOUFFRES, 154 DOLINES, 9 ARCHES
    9 traversantes, 8 avec un pont, 8 VRAIES ARCHES

**CE QUI RESTE, ET POURQUOI JE M'ARRETE LA :**

- **`AWorldseedTerrain`, 1 413 lignes et six roles** : maillage legataire, sol
  de fond, ciel, requetes climat/latitude, ponte de l'acteur voxel, visibilite
  du proxy. C'est un god-actor ASSUME et deja documente -- quand le voxel a
  remplace le mailleur, deplacer ces services aurait demande sept cents lignes
  d'un coup, sans filet ; un drapeau et trente lignes ont suffi.

  **Le gros du poids est le mailleur LEGATAIRE** : `BuildChunk` (357),
  `UpdateChunks` (71), `ReleaseChunk`, `StrideForDistance`, soit environ
  450 lignes derriere `bUseVoxelMesher`. Ce N'EST PAS du code mort : c'est un
  REPLI si l'acteur voxel ne se pose pas (`WorldseedTerrain.cpp:364`). Le
  supprimer est une decision de conception -- garder ou non ce filet -- et non
  un refactor : elle revient au proprietaire.

  `ComputeVertexAppearance` (140) est PARTAGE entre le mailleur legataire et le
  sol de fond : il reste quoi qu'il arrive.

- **`BuildGroundProxy` (253) et `UpdateGroundProxyVisibility` (84) appartiennent
  a `AWorldseedGroundProxy`**, qui existe deja comme acteur. Mais le premier
  appelle `ComputeVertexAppearance` : le deplacer proprement demande de decider
  ou vit ce calcul d'apparence. A faire avec la decision precedente.

- **`ProbeLithology` (363) et `ProbeArches` (381) posent chacune PLUSIEURS
  questions** -- j'y ai empile trois mesures dans la journee. Elles gagneraient
  a etre eclatees par question posee, ce qui est le vrai critere pour une
  sonde : lithologie / relief / diaclases d'un cote, cretes / couverture des
  lames / verification des arches de l'autre.

### Le Python est parti : tout est en C++ (19 septembre 2026)

Demande du proprietaire : « je souhaite vraiment tout passer en C++ de maniere
propre ». Les 5 770 lignes de `Tools/WorldGen` etaient LEGATAIRES -- aucun code
du jeu ne les appelait -- mais elles restaient une seconde implementation, avec
tout ce que cela porte de risque de divergence.

**INVENTAIRE AVANT DE SUPPRIMER, et il tranche seul.** Vingt-six fichiers, dont
vingt-quatre servent l'ancien pipeline PNG -- generer hors du moteur, exporter
des images, les tuiler, les importer dans l'editeur -- qui n'existe plus depuis
que le monde se calcule DANS LE JEU. Ils lisent tous un dossier de sortie qui
n'est plus produit : ils etaient deja morts en pratique.

**DEUX AVAIENT ENCORE DE LA VALEUR**, et elle a ete portee :

- `terre.py` -- le BULLETIN DE CONFORMITE TERRESTRE. C'est le seul controle qui
  confronte le monde a des valeurs EXTERIEURES au projet : un monde procedural
  peut etre parfaitement coherent avec lui-meme et faux par rapport a la Terre.
- `metrics.py` -- un harnais de non-regression. Il lisait le manifeste et les
  PNG de l'ancienne sortie ; son role est desormais tenu par les sondes C++,
  qui mesurent le monde genere en memoire.

**`ProbeTerre` REMPLACE `terre.py`, ET CORRIGE UN DEFAUT DE FOND.** La version
Python REIMPLEMENTAIT le diagramme de Whittaker pour confronter les vingt-trois
climats reels. Elle validait donc une COPIE du classificateur, pas le
classificateur -- exactement ce que la regle du depot interdit, « ne jamais
recopier une formule dans deux fichiers ». D'ou l'extraction de
`WorldseedBiomes::FromClimate(T, P, TempMax, FractionEte, Rules)` hors de la
boucle de `Classify` : la sonde appelle desormais la MEME fonction que la
generation.

**RESULTAT : 20 sur 23 contre 19 avec le Python**, et les trois echecs sont
tous explicables :

- *Mediterranean_Cool_Summer* (13,2 C, 809 mm) tombe en foret subtropicale
  humide. **C'EST UN DEFAUT REVELE PAR LE PORTAGE**, pas un artefact : la foret
  subtropicale humide, ajoutee apres le mediterraneen, capture la case AVANT
  que la surcharge mediterraneenne ne s'applique -- et celle-ci ne regarde que
  `TemperateForest`, `Grassland` et `Steppe`. A reprendre.
- *Subarctic-Severe_Winter* tombe en toundra. Le portage applique la LIMITE DES
  ARBRES, que la version Python n'appliquait pas du tout : elle passait donc ce
  releve en ne le testant pas. Notre approximation du mois le plus chaud par la
  moyenne de la saison d'ete est plus froide que le vrai maximum mensuel, d'ou
  la bascule. Le releve reel de ce climat est d'ailleurs un cas dur connu : il
  est PLUS FROID en moyenne (-11,6 C) que la toundra polaire (-8,4), et porte
  pourtant de la taiga.
- *Cold_Semi-Arid* -- echec deja documente et assume : 610 mm a 6 degres EST
  une foret dans un diagramme de Whittaker, et le releve gonfle sa pluie en
  comptant l'equivalent-eau de la neige.

**DEUX SEUILS QUI NE DOIVENT PAS ETRE UNIFIES, et le portage les preserve.** La
part estivale de notre monde est plus CONTRASTEE que la realite -- 0,15 a 0,21
entre 38 et 50 degres chez nous, contre 0,24 a 0,30 sur les villes
mediterraneennes reelles -- parce que le modele de circulation est purement
zonal. Le seuil du MOTEUR vaut donc 0,25, celui des RELEVES 0,31. Les confondre
ferait basculer Oceanic (0,427) ou Humid_Subtropical (0,413).

**ET UN PIEGE DE PORTAGE PAYE COMPTANT : UN LIBELLE N'EST PAS UNE CLE.** Le
premier jet comparait la case obtenue au nom attendu, en chaines -- « Foret
temperee mixte » contre « Forêt tempérée mixte ». QUATRE releves sur vingt-trois
passaient : exactement ceux dont le nom n'a pas d'accent. La comparaison se fait
sur l'ENUMERATION, comme pour les identifiants de biome et de roche.

**CE QUI RESTE DANS `Tools/WorldGen`, ET C'EST DE LA DONNEE, PAS DU CODE :**

    rules/world_rules.json    la source de verite des reglages, lue par le C++
                              a WorldseedRules.cpp:99
    rules/climats_reels.json  les 23 releves de stations reelles extraits
                              d'Ultra Dynamic Sky, que le bulletin confronte

Le second vivait dans `Saved/WorldGen/20260909/`, c'est-a-dire dans un dossier
de SORTIE : une donnee de reference n'a rien a faire la, elle aurait disparu au
premier menage.

**LES EN-TETES QUI DISAIENT « portage de worldgen/climate.py » ONT ETE
CORRIGES.** Neuf fichiers designaient un original qui n'existe plus. Ils disent
desormais qu'ils sont la SEULE implementation -- il ne faut plus chercher de
reference ailleurs, ni supposer qu'un autre fichier dit la meme chose
autrement.

Non-regression verifiee apres suppression : 822 chambres, 8 reseaux, 9 arches
dont 8 vraies, au chiffre pres.

### Un A/B ne se fait JAMAIS en editant le fichier de regles en place (20 septembre 2026)

**J'ai vide `world_rules.json`**, et le proprietaire l'a vu avant moi : « le
personnage tombe depuis un moment ». Le fichier faisait 1322 lignes, il en
faisait zero.

**LE MECANISME, ET IL EST BANAL.** Pour comparer deux reglages j'ai ecrit une
boucle qui MODIFIE le fichier, lance une mesure, puis le RESTAURE a la fin.
Une restauration posee a la fin d'une commande longue n'est pas une
restauration : il suffit d'un depassement de delai ou d'une interruption pour
que la commande meure entre la modification et la remise en etat. `Set-Content`
tronque avant d'ecrire -- tue au mauvais instant, il laisse un fichier VIDE.

**LE SYMPTOME NE RESSEMBLE PAS A LA CAUSE, ET C'EST CE QUI COUTE.** Sans
regles lisibles, la generation de secours echoue ; le monde n'existe pas ; le
pion n'a aucun terrain sous lui et **tombe indefiniment**. Cela se lit trait
pour trait comme un defaut du terrain ou du filet de rattrapage -- j'ai
commence a relire `HoldOrReleasePlayer` avant de penser au fichier. **Devant un
pion qui tombe sans fin, lire d'abord le journal : la ligne
`[Worldseed] JSON invalide` ou `generation de secours impossible` tranche en
une seconde.**

**LA REGLE, ET LE DEPOT LA PORTAIT DEJA.** Le commentaire de
`-WorldseedTransvoxel=` le dit en toutes lettres : « un A/B qui demande de
rouvrir le fichier de regles change son empreinte, donc regenere le monde
entre les deux moities : ce ne serait plus le meme monde, et le depot a une
regle contre les A/B mal montes ». D'ou les surcharges en ligne de commande --
`-WorldseedNiveaux=`, `-WorldseedRayon=`, `-WorldseedTransvoxel=`,
`-WorldseedVoxel=`. **Quand un A/B demande un reglage qui n'a pas de
surcharge, on AJOUTE la surcharge ; on ne touche pas au fichier.** C'est
quelques lignes, c'est reutilisable, et cela ne peut pas laisser le depot
casse.

Si une edition en place est vraiment inevitable : copier le fichier a cote
AVANT, restaurer depuis la copie, et ne jamais faire dependre la restauration
d'un processus qui peut etre tue. Le filet de secours reste `git checkout --`,
a condition que la valeur en cours soit commitee.

### « Repris du cache » ne veut pas dire « lu » (22 septembre 2026)

Signale : « lorsque je fais echap pour revenir au menu, malgre que la carte
soit dans le cache, elle met beaucoup de temps a s'afficher sur le globe,
pourtant aucun calcul de re-generation n'est effectue ». Le releve disait
bien « monde repris du cache (12982 ms) », et le mot CACHE laissait croire a
une lecture.

**CETTE BRANCHE REJOUE QUATRE PASSES DERIVEES**, et il n'y avait aucun chrono
pour le dire :

    lecture du fichier      456 ms      3 %
    biomes                  199 ms      1 %
    champs du sol         3 066 ms     21 %
    GROTTES              11 181 ms     75 %

**UN TOTAL SANS DETAIL NE SE CORRIGE PAS, IL SE DECOMPOSE.** Meme famille que
« quand une correction ne bouge pas la mesure, se demander si la mesure
melange deux populations » : ici un seul chiffre couvrait quatre traitements
dont un pesait les trois quarts.

**LE RESEAU DE CAVITES EST DESORMAIS SERIALISE** -- reprise **14,9 s ->
3,6 s**, grottes **0 ms**. L'argument du registre (« rebati a chaque
chargement, donc aucun impact sur le cache ») valait quand on ne payait ce
prix qu'une fois au lancement ; il ne vaut plus des qu'on fait des
allers-retours menu / partie, et le prix etait paye DEUX fois puisque le jeu
le rebatit aussi. Cout disque : **0,1 Mo** -- 74,0 -> 74,1 Mo, trois fois
moins que les « quelques centaines de Ko » que j'avais annoncees.

**L'INDEX SPATIAL NE SE SERIALISE PAS** : il est derive et pese PLUS que ce
qu'il indexe (une entree par case TOUCHEE, donc un long tunnel figure dans
des dizaines de cases). `ReconstruireIndex` le refait en quelques
millisecondes -- et c'est la MEME fonction que le temps `Indexer` de la
generation, sans quoi elles auraient diverge a la premiere retouche.

**ECRIRE CHAMP PAR CHAMP, JAMAIS LA STRUCTURE EN BLOC.** Un
`Serialize(&S, sizeof(S))` sur un tableau de structures grave le bourrage du
compilateur et l'ordre des membres : ajouter un champ produirait un cache qui
se relit SANS ERREUR en rendant des chambres au mauvais endroit.

**LE DEFAUT D'ORDRE, ET IL S'EST VU AU JOURNAL AVANT LE CHRONOMETRE.** La
premiere version ne changeait RIEN -- 10 682 ms de grottes au second
lancement, comme avant. L'ecriture du cache etait placee AVANT la passe des
biomes, donc avant les grottes : le reseau partait au fichier alors qu'il
n'existait pas encore. Ce qui l'a trahi est l'ORDRE DES LIGNES -- « monde
genere » precedait « grottes : 155 chambres ».

    REGLE : une ecriture de cache se place APRES tout ce qu'elle pretend
    contenir, jamais la ou la DERNIERE grandeur ajoutee se trouvait prete.
    La place etait sans consequence tant que le cache ne portait que le
    relief et le climat, tous deux prets a cet endroit.

**UN CACHE D'UNE VERSION INTERMEDIAIRE PEUT ETRE VALIDE ET INUTILE.** Les
fichiers ecrits par la premiere version portaient un reseau VIDE : ils se
relisaient sans erreur, et le code retombait proprement sur la
reconstruction. Personne n'aurait su pourquoi l'attente persistait. D'ou le
second bump de version -- **22 -> 24** -- plutot que de laisser cohabiter des
caches avec et sans cavites.

**LE CONTROLE QUI TRANCHE N'EST PAS LE TEMPS, C'EST LE COMPTE.** Le script de
verification compte les lignes « chambres, ... liaisons » par lancement :
**0 au second tour** prouve que le reseau vient du fichier. Le temps total,
lui, aurait pu passer pour une variation.

**RESTE OUVERT** : les champs du sol, **2 923 ms**, sont dans le meme cas --
deterministes, recalcules a l'identique. Les serialiser ramenerait la reprise
autour de la seconde.

**ET UN DEFAUT DU HARNAIS, TROUVE EN CHEMIN** : avec `-WorldseedMenuAuto`, le
menu appelait `StartGeneration` DEUX fois -- une dans le bloc du parcours
automatique, une inconditionnelle plus bas -- donc deux generations completes
en parallele, deux reconstructions de cavites de onze secondes pour un
resultat identique (155 chambres des deux cotes, a 34 ms d'intervalle). Rien
ne le signalait, les deux lignes etant separees par mille autres. Cela montre
au passage que **l'annulation n'interrompt PAS une passe de grottes en
cours**.

### Le drainage triait ce qui sortait deja trie (22 septembre 2026)

Suite du retour au menu. Une fois les cavites sorties du chemin, les champs
du sol devenaient 82 % de l'attente -- et dedans, le drainage 94,5 %.

**LA DECOMPOSITION, ET ELLE A SAUVE LE CHANTIER :**

    flood 1 415 ms   TRI 1 038 ms   accumulation 73 ms   D8 17 ms

**UNE SECONDE POUR TRIER CE QUI SORTAIT DEJA TRIE.** Priority-Flood depile
toujours le minimum, et la valeur empilee est FIGEE au moment de
l'empilement -- la garde `< Max` interdit de repasser sur une cellule, et
toute valeur poussee vaut au moins celle qu'on vient de depiler. Les
depilements sortent donc par altitude non decroissante, soit l'ordre
topologique a l'envers. Noter l'ordre au passage : **1 000 ms -> 22 ms**.

**CE QUI RENDAIT CE TRI SI CHER N'EST PAS SON O(n log n)**, c'est son
comparateur : `Filled[A] > Filled[B]` lit AILLEURS en memoire a chaque
appel, donc un defaut de cache par comparaison, une vingtaine par element,
sur 8,4 millions. **Un tri indirect sur des indices est toujours suspect a
cette taille.**

**L'EQUIVALENCE SE PROUVE AVANT DE REGARDER LE TEMPS, et c'est LA regle de
cette session.** Un ordre topologique faux NE PLANTE PAS : il verse l'eau
dans le mauvais sens et rend des debits errones -- donc humidite du sol,
canyons et lacs faux -- sans un message. Or supprimer un tri produit
mecaniquement un meilleur temps, Y COMPRIS quand l'ordre obtenu est mauvais.
D'ou une EMPREINTE de l'accumulation (somme et pic) journalisee a chaque
appel, et un A/B sur le MEME binaire (`-WorldseedFluxTri=1`) :

    AVANT  somme 1.91641e+11  pic 7.38526e+07
    APRES  somme 1.91641e+11  pic 7.38526e+07

**CE QUE J'AVAIS REPERE A LA LECTURE ET QUI NE VALAIT RIEN.**
`NormaliserParCentiles` copie le champ, puis appelle `Quantile` deux fois,
et `Quantile` recopie a chaque appel : trois copies de 33 Mo. Defaut reel,
visible, et **98 ms sur 2 802** -- 3,5 %. Le corriger aurait donne soixante
millisecondes en laissant les 94 % de cote. **Un defaut qu'on VOIT en lisant
le code n'est pas forcement celui qui coute** : c'est la quatrieme fois que
ce depot paye cette lecon, apres les quatre corrections du routage des
galeries qui n'ont jamais bouge le chiffre.

**BILAN DU RETOUR AU MENU, de bout en bout :**

    au depart                      14 927 ms
    reseau de cavites serialise     3 433 ms   (-77 %)
    ordre topologique              2 439 ms   (-84 % au total)

**RESTE, ET CE SONT MAINTENANT DE VRAIS MURS** : le Priority-Flood
(1 437 ms) est sequentiel par nature -- un tas binaire sur 8,4 M cellules --
et la lecture disque (418 ms) est incompressible. Les serialiser
couterait 20 a 30 Mo de cache pour deux champs pleine resolution, contre
0,1 Mo pour les cavites : **trois cents fois plus de disque par seconde
gagnee**. Arbitrage non tranche.

**NON MESURE, ET ANNONCE COMME TEL** : `WorldseedFlow::Compute` est aussi ce
que l'EROSION appelle a chaque passe, trois cents fois par generation. Le
gain devrait s'y retrouver ; il faudrait une regeneration complete pour le
chiffrer.

### Le projet etait INVISIBLE dans Insights, et le message jaune ne coute rien (22 septembre 2026)

Trois questions posees le meme jour -- le message jaune du moteur, le
multithreading, et « est-ce que des tests existent » -- et la reponse aux trois
a commence par la meme chose : instrumenter, puis mesurer.

**ZERO MARQUEUR DE PROFILAGE DANS QUARANTE MILLE LIGNES.**
`grep -rn "TRACE_CPUPROFILER_EVENT_SCOPE" Source/Worldseed/` ne rendait RIEN.
Quand on ouvrait une trace Unreal Insights, tout Worldseed etait invisible : on
ne voyait que les marqueurs du moteur, et la seule mesure disponible etait un
agregat. C'est exactement ce qui a rendu possible le piege du bridage de
l'editeur -- « une journee entiere de conclusions de performance batie
dessus ». Corrige : `WorldseedTrace.h` et sa macro `WORLDSEED_TRACE(Nom)`, qui
impose le prefixe `Worldseed_` pour qu'une passe se filtre d'un mot dans une
trace. Dix-huit marqueurs poses. **Cout nul** : la macro d'Epic se reduit a du
vide en build final, et ne coute en developpement que si une trace tourne.

**ET LE BANC NE SAVAIT PAS DIRE « LIMITE PAR QUOI ».** Il ne rapportait que la
trame entiere. Or c'est la PREMIERE question de toute mesure de performance, et
sans elle on optimise au hasard. Ajoute : les quatre fils, lus sur les globales
du moteur (`GGameThreadTime`, `GRenderThreadTime`, `GRHIThreadTime`,
`RHIGetGPUFrameCycles`) et **non** via `FStatUnitData`, qui n'est rempli que par
son propre affichage (`UnrealClient.cpp:361`) et rendrait zero quand `stat unit`
est eteint.

**LE GOULOT EST LE FIL DE RENDU, ET CE N'ETAIT PAS SU.**

    trame 4,69 ms (213 img/s)
    jeu 2,07  |  rendu 4,68  |  RHI 2,60  |  GPU 3,59

Avec 2390 chunks, soit autant de `ProceduralMeshComponent` a soumettre. Ni le
GPU ni le fil de jeu ne limitent. **Tout gain GPU est donc largement perdu**, et
le levier utile est le nombre de PRIMITIVES, pas les triangles ni les pixels.

#### Le message jaune : diagnostic exact, et son prix

    [VSM] Non-Nanite Marking Job Queue overflow. Performance may be affected.

- texte : `VirtualShadowMapCacheManager.cpp:1081` ;
- mecanique : une instance dont le rectangle depasse HUIT pages
  (`MAX_SINGLE_THREAD_MARKING_AREA`) devient un « gros travail » et prend une
  place dans une file de 128 (`MARKING_JOB_QUEUE_SIZE = NUM_THREADS_PER_GROUP * 2`,
  avec `NumThreadsPerGroup = 64`). Cette file est une constante de **COMPILATION
  du shader** : **AUCUNE variable de console ne la leve**, ne pas la chercher ;
- au debordement, le shader retombe sur un marquage MONO-THREAD. Le resultat
  reste JUSTE : c'est un avertissement de PERFORMANCE, jamais un artefact -- le
  meme `switch` du moteur annonce explicitement « will produce visual artifacts »
  pour `PagePool` et `VisibleInstances`, et **ne le dit pas** pour celui-ci ;
- chez nous : des milliers de `ProceduralMeshComponent` de 32 m, qui ne peuvent
  pas etre Nanite, tous en `SetCastShadow(true)`. Le sol de fond est HORS DE
  CAUSE, il est deja en `SetCastShadow(false)`.

**MESURE, A/B sur la MEME binaire** (`-WorldseedOmbres=0`), meme monde --
2390 chunks, 3 142 750 triangles et 1 675 321 sommets des deux cotes, donc la
comparaison porte bien sur la meme geometrie :

| | ombres ON | ombres OFF | ecart |
|---|---|---|---|
| trame | 4,69 ms | 4,40 ms | **+0,29 ms** |
| images/s | 213 | 227 | -14 |
| fil de rendu | 4,68 | 4,39 | +0,29 |
| GPU | 3,59 | 3,34 | +0,25 |
| pire trame | 9,37 | 7,66 | +1,71 |

**TOUTE l'ombre portee du terrain coute 0,29 ms sur un budget de 16,67, soit
1,7 %. Et le debordement n'en est qu'une PART** -- le reste est le rendu de
profondeur des ombres, qu'on veut garder. **0,29 ms est donc le PLAFOND du cout
du debordement, pas sa valeur.** Couper les ombres du relief pour faire taire
l'avertissement serait un tres mauvais marche.

**ATTENTION SI L'ON VEUT MASQUER LE MESSAGE** :
`r.Shadow.Virtual.AllowScreenOverflowMessages=0` masque les QUATRE messages de
debordement du VSM, dont deux -- `PagePool` et `VisibleInstances` -- signalent
de VRAIS artefacts. On perdrait un avertissement utile pour en cacher un
inoffensif.

#### Une mesure fausse dont la conclusion etait juste

Le chrono du televersement demarrait APRES `PaintVertices` : le « 0,21 ms par
chunk » qui a justifie de porter `UploadsPerPass` de 6 a 16 **n'incluait pas la
peinture des sommets**, laquelle echantillonne le relief en BICUBIQUE -- seize
lectures dispersees -- pour chaque sommet.

**J'EN AI CONCLU QUE C'ETAIT CHER. LA MESURE M'A REFUTE** : 0,08 ms par chunk
contre 0,17 pour le televersement, soit **0,075 us par sommet**. La bicubique ne
coute rien parce que des sommets voisins retombent dans les memes lignes de
cache. Il ne faut donc PAS deplacer cette passe sur le fil de travail -- ce que
j'allais recommander. La mesure etait bien faussee, d'un tiers ; la conclusion
qu'elle soutenait tenait quand meme.

**LE COUT PAR SOMMET EST LA GRANDEUR QUI TRANCHE**, pas le cout par chunk : un
chunk plat et une paroi ne portent pas le meme nombre de sommets, donc un temps
par chunk ne se compare a rien. S'il est eleve, c'est le traitement qu'il faut
deplacer ; s'il est faible, c'est qu'il y a beaucoup de geometrie et le remede
est ailleurs.

#### Le threading etait DEJA bon, et le conseil courant ne s'applique pas ici

Trace dans la source d'UE 5.8 plutot que suppose :

    LaunchEngineLoop.cpp:2621   GLargeThreadPool = new FQueuedLowLevelThreadPool();
    LaunchEngineLoop.cpp:2630   GThreadPool = new FQueuedThreadPoolWrapper(GLargeThreadPool, N);
    QueuedThreadPool.h:18       EQueuedWorkPriority::Normal = 3
    QueuedThreadPoolWrapper.h:490  TaskPriorityMapper[3] -> ETaskPriority::BackgroundNormal

**`Async(EAsyncExecution::ThreadPool)` tourne DEJA en priorite d'arriere-plan,
sur le meme ordonnanceur bas niveau que `UE::Tasks` et `ParallelFor`.** Ce n'est
pas un pool de threads OS separe, il ne vole rien au rendu. Migrer vers
`UE::Tasks::Launch` ne donnerait donc AUCUN gain de performance -- seulement de
l'ergonomie (`TryCancel`, priorite ecrite dans le code, `Pipe`, `Wait`). Le
conseil qu'on lit partout ne s'applique pas a ce cas.

**ET « NE PLUS VOIR LA GENERATION SE FAIRE » N'EST PAS UN PROBLEME DE THREADS.**
Le maillage est hors du fil de jeu, le fil de jeu est a 2,07 ms pour un budget
de 16,67 : ajouter des fils ne changerait rien. Ce qui se voit est de la
LATENCE. Les leviers sont la prediction par la vitesse du joueur, le tri des
candidats par le champ de vision et non par la seule distance, et les anneaux --
deja en place, et mesures ici a 1277 / 637 / 476 chunks pour 1200 m de vue.

#### AUCUN test automatique n'existe, et les commits `test(...)` n'en sont pas

`grep -rl "IMPLEMENT_SIMPLE_AUTOMATION_TEST" Source/` ne rend RIEN. Les vingt
commits `test(...)` de l'historique sont des SONDES : elles mesurent, un humain
lit le journal, et **aucune ne peut echouer toute seule**. La distinction n'est
pas de vocabulaire -- le registre ci-dessus est litteralement la liste des
defauts qui sont passes inapercus faute d'un oracle automatique.

Rien a installer pour y remedier : `#if WITH_DEV_AUTOMATION_TESTS` dans le
module de jeu, et `-ExecCmds="Automation RunTests Worldseed;Quit"` en
commandlet. Les six qui auraient attrape de VRAIS defauts de ce depot, dont
trois ont deja leur oracle ecrit : l'enroulement Transvoxel contre le gradient
(`ProbeVoisins`), l'aller-retour du cache, les 23 climats reels contre
`WorldseedBiomes::FromClimate`, l'empreinte du drainage (somme et pic, deja
journalisee), les invariants de bout en bout (terres 29,2 %, pluie 715 mm,
connexite des reseaux), et la continuite C1 de `SampleUVCubic`.

#### PIEGE DE PROTOCOLE REFAIT, alors qu'il est ecrit plus haut

J'ai lance le premier A/B **sans `-WorldseedCielClair`**, alors que ce fichier
porte deja la note : l'horloge d'UDS tourne, trente secondes d'ecart au
chargement font douze minutes de jeu, et « trois A/B de la journee sont partis
a la poubelle pour cette seule raison ». Mesure jetee, montage refait. **Tout
A/B par lancements successifs dans ce projet se fait avec ce drapeau**, et le
journal doit montrer « horloge figee sur N acteur(s) UDS » -- sans quoi le
releve ne vaut rien.

**Corollaire pose dans le code** : le releve du banc DIT desormais de quelle
moitie il est (« ombre portee des chunks ACTIVE / COUPEE »). Un releve qui ne
porte pas sa configuration ne se compare a rien six mois plus tard -- ce depot a
deja compare deux releves pris dans deux etats differents du code en croyant
qu'ils etaient comparables.

### La latence est un ROBINET, et le terrain n'est pas le cout (22 septembre 2026)

Suite de la session d'instrumentation, et elle REFUTE le chantier qu'elle avait
elle-meme recommande. A lire avant de rouvrir la question du nombre de chunks.

#### Le terrain voxel ne coute presque rien, et c'est mesure

Balayage croise `NiveauMax` x `rugositeMin`, monde 4096x2048, vue 1200 m,
banc en jeu avec `-WorldseedCielClair` :

| configuration | chunks | triangles | trame | GPU |
|---|---|---|---|---|
| 3 anneaux, rugosite 0 | **2346** | 3 049 232 | 4,44 ms | 3,46 |
| 3 anneaux, rugosite 0,20 | 2314 | 2 985 964 | 4,47 | 3,46 |
| 3 anneaux, rugosite 0,35 | 2033 | 2 495 611 | 4,44 | 3,46 |
| 4 anneaux, rugosite 0,35 | **30** | 62 360 | **4,09** | **3,38** |

**TRENTE CHUNKS AU LIEU DE DEUX MILLE TROIS CENT QUARANTE-SIX -- soixante-dix-huit
fois moins -- POUR HUIT POUR CENT DE TRAME.** Et le GPU ne bouge que de 3,46 a
3,38 entre trois millions et soixante-deux mille triangles : ce qu'il fait n'est
pas de la geometrie, c'est du travail plein ecran. Il existe un PLANCHER d'environ
quatre millisecondes qui ne vient pas du terrain.

**CONSEQUENCE : on n'arme PAS `rugositeMin`.** Elle rend 13 % de chunks en moins
pour 0 % de trame, et l'equilibrage 2:1 qu'elle exige coute 3 ms de passe. Le
levier existe, il est correct, il ne sert a rien ici.

#### « Limite par le fil de RENDU » N'EST PAS ETABLI

Le banc rapporte les quatre fils depuis les globales du moteur, et son verdict
designe le fil de rendu. **Ce verdict est douteux, et le signe est celui que ce
depot connait par coeur** : `rendu` vaut `trame` au centieme dans TOUTES les
mesures -- 4,43/4,44, 4,46/4,47, 4,08/4,09, 4,29/4,30 -- sur des charges qui
vont de 30 a 2390 chunks. Deux nombres qui restent colles sur des charges sans
rapport ne mesurent pas une charge.

**`GRenderThreadTime` inclut les ATTENTES du fil de rendu.** Un fil qui attend le
GPU ou le fil de jeu voit son temps egaler la trame sans en etre la cause. Meme
famille que le `frame_ms == gpu_ms` du bridage d'editeur. Le banc garde le
verdict parce qu'il reste une indication, mais il ne faut pas en tirer de
conclusion sans un profil qui separe le travail de l'attente.

#### Le remplissage est borne par DEUX robinets qui se passent le relais

C'EST LA REPONSE A « peut-on multithreader pour ne plus voir la generation ».
Non : le maillage est DEJA hors du fil de jeu, et il tourne tres en dessous de
ses moyens. Ce qui borne est une paire de constantes.

    debit = min( UploadsPerPass / UpdatePeriod ,  MaxJobsInFlight / UpdatePeriod )

Un travail de maillage dure 8 a 9 ms quand la passe revient toutes les 100 : au
plus `MaxJobsInFlight` d'entre eux peuvent etre lances ET recoltes par passe.

**LE MODELE A ETE VALIDE PAR DEUX PREDICTIONS ANNONCEES AVANT LECTURE**, ce qui
vaut mieux qu'un ajustement apres coup :

    poses 16, travaux  24  ->  160/s  ->  20 s de remplissage
    poses 32, travaux  24  ->  240/s  ->  15 s   (les TRAVAUX bornent)
    poses 64, travaux  24  ->  240/s  ->  15 s   <- predit, verifie
    poses 32, travaux  64  ->  320/s  ->  12 s   (les POSES bornent de nouveau)
    poses 32, travaux 128  ->  320/s  ->  12 s   <- predit, verifie

**Retenu : 32 poses et 64 travaux. Remplissage 20 -> 12 s, soit -40 %, ET LA
TRAME S'AMELIORE** -- 4,84 a 4,30 ms, 207 a 233 images par seconde, pire trame
8,15 a 6,12 -- parce que le transitoire dure moins longtemps. Le maillage
ralentit un peu par travail (8,4 a 9,3 ms, contention sur le pool) et le debit
total monte quand meme.

**OUVRIR UN SEUL DES DEUX NE DONNE RIEN.** C'est le piege de ce reglage, et il
explique pourquoi le passage de 6 a 16 du 19 septembre n'avait pas rendu tout ce
qu'on en attendait : les travaux bornaient deja.

#### L'equilibrage 2:1 : une preuve du depot etait fausse

`NoeudAccidente` portait cette preuve : « si un noeud A se subdivise, son emprise
elargie est accidentee ; cette emprise contient ses voisins, donc chaque voisin
se subdivise aussi ». Elle est JUSTE A UN NIVEAU DONNE et FAUSSE d'un niveau a
l'autre, parce que le seuil est une PENTE -- donc divise par deux a chaque cran :

    C (niveau L-1) descend si  etendue >= R x 3 x Cote(L) / 2
    A (niveau L)   descend si  etendue >= R x 3 x Cote(L)

Entre les deux seuils, C descend et A reste : deux crans d'ecart. **Une grandeur
sans dimension ne peut pas etre monotone contre un seuil qui change d'echelle** :
aucune retouche locale ne ferme cela en gardant l'invariance d'echelle.

D'ou une passe `Equilibrer` sur l'ENSEMBLE EMIS, et sa consequence : `NiveauEn`
a disparu. Tant que la partition etait une fonction du POINT, une descente
ponctuelle pouvait la reproduire ; l'equilibrage regarde les VOISINS, donc le
niveau d'une feuille depend de ses voisines. Le masque de transition LIT
desormais `FeuillesCourantes` -- une source de verite au lieu de deux calculs
qu'on espere d'accord.

**TROIS DEFAUTS DE MA PROPRE PASSE, tous trouves par la mesure suivante :**

- *je sondais depuis le mauvais cote.* Chaque feuille GROSSIERE sondait ses six
  faces en leur centre. Or la face d'un chunk de niveau 3 touche jusqu'a
  SOIXANTE-QUATRE chunks de niveau 0 : un point par face n'en voit qu'un. Mesure
  du defaut : « 87 sur 512 feuilles » en regime etabli, apres equilibrage.
  **Sonder depuis le cote FIN est complet par construction** -- le point juste
  au-dela d'une face tombe forcement DANS la feuille qui couvre cette position,
  puisqu'un voisin plus grossier est plus GRAND que le point sonde. On marque
  alors le VOISIN, pas soi-meme. Apres : zero violation sur 2113 feuilles ;
- *mon controle se verrouillait.* « Une seule alerte par partie » : il a crie a
  la TRAME 3, pendant le premier remplissage, puis s'est tu pour toujours --
  impossible de savoir si le defaut persistait, ce qui est la seule question qui
  compte ;
- *puis il a compte sur une FENETRE TOURNANTE.* Le balayage des masques
  n'examine que 512 feuilles par passe, avec un curseur qui tourne : le compte
  sautait de 87 a 0 et revenait. **Un controle dont la valeur depend de la
  fenetre qu'on regarde mesure la fenetre, pas la propriete.** Il vit desormais
  dans la passe d'equilibrage, qui parcourt TOUT, et il y est gratuit.

#### Trois pieges de methode, payes le meme jour

- **EXPLIQUER N'EST PAS VERIFIER.** Devant deux releves identiques a 0,00 et
  0,10, j'ai produit une explication plausible -- « le seuil ne coupe rien a
  0,10 » -- qui n'a pas survecu a la TROISIEME passe identique. La vraie cause
  etait que les anneaux vont a 300/600/1200 pour une vue de 1200 : le dernier
  touche deja le rayon, donc la rugosite n'avait AUCUNE COURSE. Le chiffre du
  registre (2165 -> 255) tournait a QUATRE niveaux, pas trois : un releve ne se
  compare qu'a configuration egale.
- **LE PIEGE DES DEUX CHIFFRES IDENTIQUES, rencontre deux fois de plus** -- une
  fois a tort (trois rugosites identiques : le reglage etait bien lu, il etait
  sans effet) et une fois a raison (`rendu` colle a `trame`). Le signe merite
  toujours une verification, jamais une conclusion.
- **UN A/B PAR LANCEMENTS SUCCESSIFS SE FAIT AVEC `-WorldseedCielClair`.** J'ai
  lance le premier sans, alors que ce fichier porte deja la note. Mesure jetee,
  montage refait. Le journal doit montrer « horloge figee sur N acteur(s) UDS ».

#### Ce qui reste ouvert

- **Le plancher de 4 ms n'est pas explique.** A 30 chunks et 62 000 triangles,
  la trame vaut encore 4,09 ms dont 3,38 de GPU. C'est du travail plein ecran --
  atmosphere, post-traitement, eclairage -- ou le sol de fond. C'est LA qu'il
  faut chercher si l'on veut vraiment gagner, et non dans le terrain.
- **La pire passe de diffusion monte a 40 ms** avec 64 travaux, pendant le
  remplissage. Hors de la fenetre de mesure stabilisee, donc sans effet sur la
  pire TRAME relevee (6,12 ms), mais a surveiller.
- **Les jointures n'ont pas ete regardees** depuis l'equilibrage corrige. Une
  forme qui n'a pas ete vue n'est pas validee.

### Le plancher de 4 ms : c'est Lumen, pas la geometrie (22 septembre 2026)

Suite de la session. Le banc disait « GPU 3,4 ms » sans dire ou, et la mesure
de la veille avait etabli que ce cout ne vient PAS du terrain -- trente chunks
et 62 000 triangles rendent encore 3,38 ms contre 3,47 pour deux mille quatre
cents chunks et trois millions de triangles.

**L'OUTIL : `ProfileGPU` ECRIT DANS LE JOURNAL.** RHI, `GPUProfiler.cpp:2199`,
derriere `WITH_PROFILEGPU` -- actif en Development, absent du build final. Il
est donc utilisable SANS editeur, ce qui est la condition pour mesurer ici. Le
banc le declenche par `-WorldseedProfilGPU`, APRES la stabilisation et apres la
fenetre de mesure : lance pendant le remplissage, il capturerait une trame ou
le streaming travaille encore.

**DEUX PIEGES DE BRANCHEMENT**, payes tous les deux : la capture se declenche a
la trame SUIVANTE et son vidage est asynchrone -- quitter aussitot rend un
journal sans ventilation ; et `IsTickable` rendait `bArme || bEnCours`, tous
deux faux apres `Conclure`, donc l'attente ne s'ecoulait jamais.

#### La ventilation

GPU total 3,51 ms, monde 4096x2048, vue 1200 m, ciel d'inspection :

    Lumen (DiffuseIndirectAndAO + LumenSceneLighting)   1,02 ms   29 %
    ombres (ShadowDepths + VSM non-Nanite)              0,63      18 %
    eau (SingleLayerWater + son prepass)                0,57      16 %
    post-traitement (dont TSR 0,31)                     0,54      15 %
    eclairage differe                                   0,39      11 %
    RayTracingScene + build                             0,26       7 %
    BasePass -- le dessin de la scene                   0,17       5 %

**LE DESSIN DE LA GEOMETRIE COUTE CINQ POUR CENT.**

#### UN A/B QUI MUTE UN ETAT PERSISTANT N'EST PAS UN A/B

**LA FAUTE LA PLUS COUTEUSE DE CETTE SEANCE, et elle a invalide six mesures.**
`Scalability::SetQualityLevels` et les cvars `sg.*` sont SAUVEGARDES a la
fermeture dans `Saved/Config/<Plateforme>/GameUserSettings.ini`. Une passe de
balayage qui pose `sg.EffectsQuality 0` le laisse donc en place pour TOUTES
les suivantes.

Constate en lisant le fichier apres coup :

    sg.ResolutionQuality=0    sg.ViewDistanceQuality=0   sg.AntiAliasingQuality=0
    sg.ShadowQuality=0        sg.PostProcessQuality=0    sg.TextureQuality=0
    sg.EffectsQuality=0       sg.FoliageQuality=0        sg.ShadingQuality=0
    sg.GlobalIlluminationQuality=1   sg.ReflectionQuality=1

Les neuf zeros venaient d'une passe « tout au minimum » lancee bien avant. Les
mesures qui ont suivi, presentees comme « seul le groupe GI baisse », tournaient
en realite avec HUIT AUTRES GROUPES au minimum. L'echelle annoncait alors
3,34 ms au cran 1 et 2,22 au cran 0 ; les vrais chiffres sont 3,83 et 3,23.

**C'est exactement le piege deja ecrit pour `world_rules.json`**, sous une autre
forme : la ou une boucle de mesure avait VIDE le fichier de regles en etant
interrompue, ici elle l'a rempli sans le dire. La regle se generalise donc :
**un balayage doit remettre a zero l'etat persistant entre chaque passe**, et
pas seulement s'abstenir d'editer des fichiers. `Tools` : supprimer
`GameUserSettings.ini` avant chaque lancement.

**LE SIGNE QUI AURAIT DU ALERTER, et il etait sous les yeux** : le sous-systeme
declare INERTE rendait quand meme 3,36 ms la ou la reference en donnait 4,34.
Un reglage eteint qui deplace la mesure ne peut signifier qu'une chose --
quelque chose d'autre a change et n'a pas ete remis.

#### L'echelle, mesuree PROPREMENT

Cinq passes, `GameUserSettings.ini` supprime avant chacune, tout le reste egal :

    niveau                         trame    images/s   GPU    ecart
    reference (rien applique)      4,38 ms     228     3,47     --
    3 Epique, pose explicitement   4,29        233     3,45   -0,09   <- temoin
    2 Haut                         4,29        233     3,35   -0,09
    1 Moyen                        3,83        261     2,91   -0,55
    0 Bas (plus de Lumen)          3,23        310     2,64   -1,15

**LUMEN COUTE 1,15 ms SUR 4,38, soit vingt-six pour cent de la trame** -- et non
la moitie, comme la mesure contaminee le laissait croire.

Le TEMOIN est la ligne du cran 3 : appliquer explicitement le niveau que le
moteur avait deja ne deplace rien (0,09 ms, la variation d'une execution a
l'autre). Sans elle, on ne saurait pas distinguer l'effet du reglage de l'effet
du sous-systeme lui-meme.

**ET LA TRAME SUIT LE GPU, contrairement a ce que j'avais conclu.** 3,47 ->
2,64 de GPU pour 4,38 -> 3,23 de trame : les deux bougent ensemble. Mon « la
trame n'est pas limitee par le GPU » reposait sur la passe `eau-off`, qui etait
justement contaminee. Ce qui reste etabli, en revanche, c'est que le NOMBRE DE
CHUNKS n'y change rien : trente chunks au lieu de deux mille trois cent
quarante-six ne rendent que huit pour cent.

#### DEUX INTERRUPTEURS POUR LA MEME CHOSE, ET ILS NE COUPENT PAS PAREIL

`r.Lumen.DiffuseIndirect.Allow 0` rend **0,15 ms**.
`sg.GlobalIlluminationQuality 0` rend **1,15 ms**. Un facteur huit.

Le cvar n'arrete que le rassemblement final ; la SCENE Lumen -- cache de
surface, cartes, capture -- et le lancer de rayons materiel continuent de
tourner. Le groupe de qualite, lui, coupe l'ensemble. **Avoir coupe « Lumen » ne
dit donc rien tant qu'on n'a pas dit PAR QUEL interrupteur** : j'ai d'abord
conclu que Lumen etait marginal sur la foi du premier.

#### Ce que coute le cran 0, mesure a l'image

Deux captures au meme point, ciel fige, seul le reglage change :

    sable, tiers bas    avec Lumen L=169,4    sans L=206,1    +36,7
    sable, mi-hauteur   avec Lumen L=160,2    sans L=198,3    +38,1
    ciel                avec Lumen L=125,6    sans L=110,9    -14,7

**LE SABLE ET LE CIEL BOUGENT EN SENS OPPOSES, et c'est ce qui prouve que ce
n'est pas une derive d'exposition** -- celle-ci les deplacerait ensemble. Le sol
perd reellement son occlusion ambiante et se delave de vingt-deux pour cent ;
l'auto-exposition assombrit le ciel pour compenser. L'eau perd aussi ses
reflexions.

#### DECISION DU PROPRIETAIRE : ON NE CHANGE RIEN

4,38 ms pour un budget de 16,67, soit vingt-six pour cent : il n'y a pas de
probleme a resoudre. Et la scene qui justifie Lumen -- une foret, une grotte,
des materiaux -- **n'existe pas encore** : ce monde n'est ni texture ni
vegetalise. Trancher sur une dune nue reviendrait a juger une fonction sur le
cas ou elle sert le moins.

`UWorldseedQualiteRendu` reste, ETEINT (`NiveauParDefaut = INDEX_NONE`), pour
deux raisons : il porte la mesure ci-dessus, qui serait autrement a refaire, et
il est la couture ou un menu d'affichage viendra se brancher -- il appellera
`Appliquer`, et rien d'autre n'aura a changer. `-WorldseedQualite=<0..4>`
l'arme pour un A/B sans recompiler.

**POURQUOI PAS UN CVAR DANS UN .INI** : les groupes `sg.*` sont precisement ce
que le systeme de qualite REECRIT -- au demarrage, a la detection materielle, et
a chaque application des reglages joueur. Un `sg.` pose dans
`[ConsoleVariables]` serait ecrase sans prevenir, et l'on chercherait longtemps
pourquoi le reglage « ne prend pas ». On passe par `Scalability::SetQualityLevels`,
qui est ce que le systeme lit.

**ET L'ON NE DEPLACE QUE DEUX GROUPES.** `SetFromSingleQualityLevel` les
ecraserait tous les huit, y compris les six que la mesure declare sans
interet -- c'est exactement ce qu'un reglage global fait sans le dire.

#### A SUIVRE

Le vrai consommateur du budget n'est pas encore la : la vegetation, qui n'a pas
ete portee au monde voxel. Le registre parle de douze millions neuf cent mille
instances pour le monde entier. C'est a ce moment-la que cette ventilation
servira -- et c'est pourquoi elle est ecrite ici plutot que refaite alors.

### Le monde etait recopie TROIS fois, et la memoire du processus ne savait pas le dire (22 septembre 2026)

Le monde -- relief, climat, roches, biomes -- se transmettait PAR VALEUR a
chaque etape : `StoreWorld` en gardait un exemplaire dans l'instance de jeu,
`TryGetWorld(Out)` en rendait une COPIE, puis `AdoptWorld` la recopiait encore
vers l'acteur voxel, argument par argument -- neuf parametres, dont cinq grands
tableaux. Trois exemplaires vivants d'une donnee que PERSONNE ne modifie apres
sa generation.

**LE POIDS EST DETERMINISTE, ET IL SE CALCULE** : sur la grille 4096x2048, les
neuf grands tableaux font **208 Mio** (cinq tableaux de flottants a 32 Mio, la
pente 32, l'index et la couverture 8 chacun ; la roche ajoute 8 de plus quand
elle est presente). Le banc le journalise desormais avec le NOMBRE DE PORTEURS,
et c'est ce second chiffre qui dit s'il y a duplication.

**MAIS LA MEMOIRE DU PROCESSUS NE PEUT PAS L'ETABLIR, et c'est la lecon de
methode.** Releve sur cinq lancements rigoureusement identiques : **7,60 / 7,69
/ 7,75 / 7,77 / 7,99 Go**. Quatre cents megaoctets de derive naturelle -- donc
les deux cent huit qu'une copie ajoute s'y NOIENT. J'ai compare des moyennes
avant de m'en apercevoir : 7,90 Go avant, 7,76 apres, avec des etendues qui se
recouvrent. **Un instrument dont le bruit vaut le signal ne mesure rien**, et la
bonne reponse n'est pas d'empiler les passes : c'est de COMPTER LES OCTETS, qui
ne dependent que de la grille, et de compter les PORTEURS, qui ne dependent que
du code.

**UN POINTEUR VERS UN MEMBRE D'ACTEUR EST AUSSI DANGEREUX QUE L'ACTEUR.** Le
champ de densite etait un membre par valeur, et chaque travail de maillage en
capturait l'ADRESSE, sous un commentaire qui affirmait que c'etait sur « parce
qu'on ne capture pas l'acteur ». On capturait un pointeur DEDANS, ce qui revient
au meme des qu'il meurt : `EndPlay` annule les travaux SANS LES ATTENDRE -- le
bon choix, attendre bloquerait la fermeture -- donc un travail deja entre dans
`Build` lisait une memoire que le ramasse-miettes allait reprendre. Fenetre
courte, plantage rare au changement de niveau : celui qu'on ne reproduit jamais.
Le fichier portait pourtant deja la bonne regle, pour les primitives de grottes :
« le fil de maillage ne doit rien tenir qui puisse mourir avant lui ».

**UNE MESURE PRISE UNE HEURE PLUS TOT N'EST PAS UN TEMOIN.** Apres le chantier,
la trame passait de 4,34 a 4,63 ms -- une degradation de sept pour cent sur le
FIL DE RENDU, que ce refactor ne touche pas. J'ai monte le seul temoin qui
tranche : `git stash` du chantier, reconstruction, et banc dans l'ETAT MACHINE
DU MOMENT.

    temoin (meme etat machine)   4,67  4,73 ms
    monde partage                4,63  4,67  4,71 ms

Identique. Les 4,34 appartenaient a une machine plus froide, et la derive a
continue toute la session -- derniere passe a 5,07. **Le depot avait deja ecrit
cette regle en septembre -- « un temoin doit etre refait dans l'etat courant,
pas repris d'un releve anterieur, meme quand on croit que rien n'a change » --
et j'ai failli conclure a une regression.** Stasher coute cinq minutes ; une
fausse regression coute une journee.

**ET UNE ASSERTION SUR UN TABLEAU VIDE NE VERIFIE RIEN.** Le test de partage
compare les ADRESSES des tableaux de deux porteurs -- la seule chose qui prouve
qu'il n'y a qu'un exemplaire, l'egalite des VALEURS passant justement dans le
cas qu'on veut interdire. Mais la fixture ne garnissait pas les biomes : les
deux adresses valaient `nullptr`, et la ligne passait qu'il y ait partage ou
non. Meme famille que le comptage de composants d'herbe qui rendait zero sur le
cas TEMOIN comme sur le notre. **Une fixture incomplete ne rend pas un test
indulgent, elle le rend MUET sur ce qu'il pretend verifier.**

**CE QUI RESTE COPIE, ET C'EST ASSUME** : `FWorldseedLithology::Id` (8 Mio), que
la generation REMPLIT -- la partager demanderait de rendre la structure non
proprietaire. On deplace ce qui pese, et l'on dit ce qu'on laisse.

### Quatre oracles automatiques, et quatre facons d'ecrire un test qui ne teste rien (22 septembre 2026)

Les quatre controles que le registre designait comme « ceux qui auraient
attrape de VRAIS defauts » sont desormais des tests d'automation : le mailleur
Transvoxel, le drainage, le bulletin des vingt-trois climats reels, et les
cavites. Vingt tests au vert. **Mais les quatre ont d'abord ete ecrits FAUX, et
c'est cette partie qui vaut d'etre gardee** -- chaque fois la meme famille de
faute, et chaque fois une faute que ce fichier documentait deja.

#### 1. Un enroulement ne se deduit JAMAIS -- troisieme fois

Le premier jet du test de face avant posait
`dot(cross(B-A, C-A), gradient) > 0` : le gradient croit vers l'air, donc vers
le dehors, donc un triangle a l'endroit devrait s'y accorder. La chaine de
raisonnement est complete, et elle est FAUSSE -- Unreal travaille en repere
INDIRECT, si bien que l'enroulement visible donne un produit vectoriel dirige
vers l'INTERIEUR du solide.

    maillage tel qu'il est rendu    0,05 %      le meme, retourne   99,95 %

La mesure est BINAIRE, elle l'a toujours ete dans ce depot, et elle concorde
avec ce que `ProbeVoisins` avait releve sur le mailleur du MOTEUR -- 0,2 et
0,3 % -- lequel s'affiche correctement. **Le fichier portait deja la regle.**
Elle est maintenant ecrite dans le test, avec les deux chiffres, pour qu'un
quatrieme passage coute moins cher.

#### 2. Une fixture sans cuvette ne mesure pas un comblement

Le test du comblement tournait sur le relief de la fixture -- une somme
d'harmoniques lisses, donc **sans aucun bassin ferme**. Releve : « plus forte
hauteur comblee **0,00 m** », `FilledM` valant `Relief` partout. Les deux
assertions passaient trivialement : j'avais ecrit un test de remplissage de
cuvette sur un relief sans cuvette.

Corrige par un relief FABRIQUE pour la question -- un versant qui descend vers
la ligne polaire, et dedans un trou que rien ne relie au bas du versant sans
franchir une levre. Apres : **cuvette de 42 m, 39 m effectivement combles**, et
le test EXIGE desormais cette remontee, sans quoi il se tait de nouveau.

**REGLE GENERALISEE : tout test doit porter une assertion qui echoue quand la
matiere manque.** C'est la troisieme forme de ce defaut apres les biomes vides
(deux `nullptr` qui se comparent egaux) et le comptage de composants d'herbe
qui rendait zero sur le cas TEMOIN comme sur le notre.

#### 3. `Segments` melange QUATRE familles, et le depot s'y etait deja fait prendre

J'exigeais que tout troncon de cavite tienne dans les bornes de galerie :
**2526 violations sur 7578**. C'etait la mesure qui etait fausse. Le tableau
melange galeries, bouches de falaise, capsules des puits -- qui s'evasent par
construction -- et percement des arches ; 504 puits d'environ cinq capsules
font justement ces deux mille cinq cents troncons. Le releve le confirme :
rayons de **1,50 a 28,73 m** quand les galeries sont reglees a 1,5-4,0.

C'est trait pour trait « le controle de percement courait jusqu'a la fin du
tableau des segments, donc il avalait les entrees -- qui percent le sol A
DESSEIN », referme a l'epoque par une borne `FinDesGaleries`. **Cette borne est
LOCALE a la passe** : le reseau ne la porte pas, donc les familles sont
indiscernables de l'exterieur. L'exposer serait la bonne reponse -- les sondes
en profiteraient -- mais le reseau est SERIALISE dans le cache depuis la veille,
et ajouter un champ oblige a bumper `WORLDSEED_PIPELINE_VERSION`, donc a faire
regenerer leur monde a tout le monde.

**L'assertion a donc ete RETIREE plutot qu'affaiblie**, la limite ecrite dans
le fichier, et il ne reste que ce qui vaut pour les quatre familles.

#### 4. Extraire avant de tester, jamais recopier

La lecture des vingt-trois releves, la table des attendus et le seuil de part
estivale propre aux releves vivaient dans un namespace **ANONYME** de
`WorldseedProbeTerre.cpp` : inatteignables depuis un test. Les recopier aurait
valide une COPIE de la lecture plutot que la lecture elle-meme -- exactement le
defaut que le portage de `terre.py` avait corrige un cran plus bas.

Extraits dans **`WorldseedClimatsReels`**, que la sonde et le test appellent
tous deux. Non-regression relevee AVANT de toucher au fichier et comparee
apres : **les vingt-trois lignes sont identiques au caractere pres**, 20 sur 23.
La sonde perd 79 lignes et ne change pas d'un chiffre.

#### 5. Un test de CALAGE a le droit de dependre de `world_rules.json`

La regle de la fixture veut qu'un test echoue quand le CODE casse, pas quand
une valeur physique bouge. `Worldseed.Terre.ClimatsReels` et
`Worldseed.Cavites.BornesDuToit` font exception **a dessein** : le bulletin EST
une mesure de calage, et la borne du toit porte sur le fichier de reglages
lui-meme. Un recalibrage DOIT rouvrir ces questions.

**MAIS LE VERDICT SE LIT STATION PAR STATION, JAMAIS SUR UN COMPTE.** Un
« au moins vingt sur vingt-trois » laisserait passer un ECHANGE, et le depot a
vu exactement cela : l'ajout de la foret subtropicale humide a fait PASSER deux
releves et BASCULER deux autres, pour un score inchange de 19 sur 23. Le compte
n'avait rien vu. Le test compare donc deux ENSEMBLES de cles et signale les
deux sens -- une regression nomme la station et sa nouvelle case, un progres
demande de retirer la station de la liste des echecs connus.

#### Ce que ces tests protegent, et qui n'etait garde par rien

| test | le defaut qu'il aurait attrape |
|---|---|
| `Transvoxel.FaceAvant` | le terrain rendu en faces arriere, donc invisible -- signale comme « des trous », trouve a l'oeil |
| `Transvoxel.Couture` | la fissure entre deux resolutions, que seul un releve lu a la main verifiait |
| `Transvoxel.Cloture` | un maillage troue ou non manifold, et les aretes plus longues qu'une cellule |
| `Drainage.OrdreTopologique` | le tri supprime la veille -- un ordre faux verse l'eau dans le mauvais sens SANS un message, et supprimer un tri ameliore le temps meme quand le resultat est faux |
| `Drainage.Conservation` | un receveur qui pointe sur lui-meme a tort, une cellule oubliee |
| `Drainage.Enroulement` | une ligne de partage des eaux ARTIFICIELLE sur l'antimeridien, invisible sur une carte vue de face |
| `Terre.ClimatsReels` | tout recalibrage de biome qui deplace une station reelle sans qu'on le remarque |
| `Cavites.BornesDuToit` | la chambre dont le toit creve le sol -- defaut reel, qu'aucune sonde ne voyait |
| `Cavites.IndexSpatial` | une primitive perdue a la reconstruction : le chunk se maille plein, sans erreur |
| `Cavites.Semis` | des salles qui se recouvrent, ou creusees sous la mer |

**CE QUI RESTE SANS ORACLE** : l'erosion et le sapement (leurs invariants sont
des ecarts de PENTE, donc statistiques et lies au calage), la passe littorale,
les mesas et canyons, et tout le rendu -- qui ne se juge qu'a l'image.

### On a REGARDE le monde, et la dette de regard etait justifiee (22 septembre 2026)

Quatre formes etaient mesurees mais jamais vues -- la stratigraphie, l'escalier
de sapement, la silhouette d'une mesa, et la vue depuis un vrai sommet. La
tournee photo les a enfin cadrees. **Elle a rendu une forme excellente et
quatre defauts, dont un que la mesure seule ne pouvait pas trouver.**

#### Ce qui est BON

**L'arche marine est impeccable** : roche massive et lisse, ouverture franche,
eau qui la traverse, terre verte des deux cotes. C'est la forme la mieux
reussie du monde, et elle est en BASALTE -- donc sans serie stratigraphique.
Retenir cet indice, il sert au diagnostic ci-dessous.

#### LE DEFAUT PRINCIPAL : les parois sont COTELEES

Les parois de canyon portent une striation horizontale tres reguliere, avec des
cavites sombres, visible de pres comme de loin. De loin, avec les anneaux, elle
se lit comme un TREILLIS PERFORE -- des centaines de petits trous alignes sur
les axes. **Ce n'est pas de la geologie, cela lit comme une surface corrompue.**

**A/B PROPRE, et il a fallu s'y reprendre a deux fois :**

    1200 m, 3 anneaux    striation + treillis perfore
    1200 m, anneaux OFF  striation + cavites, aspect moins perfore
    170 m (gros plan)    IDENTIQUE des deux cotes

**Le defaut est present dans les DEUX.** Les anneaux changent son ASPECT, pas
son existence : ce n'est donc pas de l'aliasing d'anneau, contrairement a ce
que la note du 21 septembre laissait attendre.

**ET MON PREMIER TEMOIN ETAIT CONFONDU.** Je l'avais lance a
`-WorldseedRayon=300` : au-dela de 300 m, ce n'est plus du voxel mais le SOL DE
FOND, lisse par construction. La vue large ne comparait donc rien -- seul le
gros plan, a 170 m, tombait au niveau 0 des deux cotes et restait valide.
**C'est exactement « pousser un defaut hors de portee du traitement qu'on veut
juger », deja consigne pour la fenetre d'eau ramenee a 0,5 km.** Un temoin de
rayon doit couvrir la distance qu'on photographie.

**TROIS PISTES, AUCUNE MESUREE**, a discriminer avant de corriger :
- les **dix bancs** de la serie stratigraphique -- 253 m, soit 25 m par banc --
  rendus en marches dures plutot qu'en nuances ;
- le **sapement des corniches**, dont les banquettes sont precisement des
  marches ;
- le **champ de densite**, qui mesure une distance VERTICALE a la surface et
  est donc mal conditionne sur une paroi. Le depot a deja rencontre ce point
  exact pour les formes de paroi, et le corrige en divisant par la norme du
  gradient -- le terrain lui-meme n'a jamais recu ce traitement.

L'arche en basalte, SANS serie stratigraphique et parfaitement lisse, oriente
fortement vers la premiere ou la deuxieme.

#### TROIS AUTRES DEFAUTS VUS

- **Des fragments detaches** flottent sur la falaise cotiere : quatre blobs
  sombres nettement separes de la surface, plus des eclats disperses sur la
  neige dans les vues larges.
- **La tournee ne cadre PAS les mesas.** Cible annoncee « sommet 222 m, paroi
  200 m », image obtenue : une pente de dune lisse, aucune paroi. La remontee
  de camera sort de la roche en MONTANT tant que le champ est plein ; sur une
  table, elle aboutit donc au sommet du plateau. **Le registre attribuait ce
  manque a la distance de vue ; c'etait le CADRAGE.**
- **Une tache verte carree** apparait au meme endroit dans toutes les vues
  larges, sur la neige. Non diagnostiquee.

#### UNE CORRECTION A UN COMMIT DE LA VEILLE

Le `BREAKING CHANGE` de `feat(voxel)!: tenir le point de naissance vise`
annonce que « les tournees photo et les bancs qui visent un point precis
peuvent desormais poser la camera sur une paroi ». **C'est FAUX pour la
tournee** : elle passe par `TeleporterJoueur`, et ce chemin est un `else if`
qui n'atteint JAMAIS le bloc modifie. L'avertissement ne vaut que pour les
lancements en `-WorldseedDepartX/Y`. Les onze vues sont sorties normalement,
ce qui le confirme a l'execution.

#### CE QUE CETTE SEANCE APPREND SUR LA METHODE

**Une forme mesuree n'est pas une forme validee, et l'ecart peut etre enorme.**
La stratigraphie etait mesuree juste -- ecart de pente dur/tendre +8,17 degres,
rapport 1,75 -- et elle produit a l'ecran une surface qui ne ressemble pas a de
la roche. Aucune des sondes ne pouvait le dire : elles mesurent des PENTES et
des PARTS, pas l'allure. La regle du depot -- « une forme qui n'a pas ete vue
n'est pas validee » -- vient de se payer sur quatre formes d'un coup.

### Serialiser ne suffit pas : il faut TRANSVASER (22 septembre 2026)

Les tables et les canyons entrent dans le cache, comme le reseau de cavites la
veille et pour la meme raison : `WorldseedPlateau::Sites` coute **2,8 s** sur la
grille du jeu, et ce prix etait paye au chargement ET a chaque retour au menu
pour quelques centaines d'octets de resultat. Trois choses apprises en chemin,
et la premiere est la plus utile.

**LE DEFAUT : L'ECRITURE ETAIT JUSTE, LA LECTURE AUSSI, ET RIEN N'ARRIVAIT.**
Le fichier portait bel et bien les sites -- `Save` les ecrivait, `Load` les
relisait dans son `FWorldseedWorldData` -- mais la branche de cache du pipeline
ne les TRANSVASAIT pas de ce monde relu vers son `FResult`. Elle transvase
pourtant tout le reste, ligne a ligne, et j'avais ajoute les lignes de
l'altitude a la lithologie sans voir que deux manquaient au bout. Symptome :
aucun. L'acteur voxel retombait sur son recalcul et les 2,8 secondes restaient
payees, sans un avertissement.

**C'EST LE COMPTE QUI L'A VU, ET LE CHRONOMETRE AURAIT DECLARE LA VICTOIRE.**
Le releve du retour au cache portait une colonne `sites %.0f ms`. Une fois la
passe sortie de cette branche, ce temps vaut zero **que le transvasement
marche ou non** : la colonne ne pouvait plus rien dire. Remplacee par
`sites relus %d tables / %d canyons`, elle a affiche `0 tables / 0 canyons` au
premier essai et tout etait dit. C'est exactement la lecon deja ecrite pour les
cavites -- « le controle qui tranche n'est pas le temps, c'est le compte » --
et elle vaut une seconde fois : **quand une optimisation consiste a ne plus
faire quelque chose, son temps tend vers zero par construction et cesse d'etre
un temoin.**

**DEUXIEME DEFAUT, TROUVE EN CHERCHANT OU SERIALISER : LES DEUX CHEMINS NE
CALCULAIENT PAS LA MEME CHOSE.** `Sites` etait appele depuis `Build`, a
l'etape 4c -- donc AVANT le littoral, AVANT le sapement des corniches et
surtout avant le DOME DE GLACE, qui ajoute jusqu'a 479 m au centre d'une
calotte. Les sites d'un monde fraichement genere decrivaient un relief
intermediaire, quand ceux d'un monde repris du cache -- rejoues en fin de
chaine -- decrivaient le bon. **Personne ne pouvait le voir parce que les deux
chemins n'etaient jamais compares** : on est toujours dans l'un ou dans
l'autre. La passe est desormais appelee UNE fois, en fin de chaine, sur le
relief final. Corollaire general : **deux chemins qui produisent la meme
grandeur doivent partager leur point d'appel, pas seulement leur fonction.**

**LE TEMOIN DU TEST, ET IL FAUT LE MONTER.** `Worldseed.Cache.AllerRetour`
compare desormais les quatre champs de chaque site. Pour prouver qu'il
DISCRIMINE, on a simule la faute du 22 septembre -- `EcrireSites` ecrivant un
compte de zero, c'est-a-dire une section presente mais vide, qui se relit sans
erreur : le test tombe sur « Expected 'table : nombre de sites' to be 5, but it
was 0 ». Puis on remet. Une assertion posee apres coup n'a pas de temoin
d'avant le defaut qu'elle doit voir ; celui-la coute une compilation.
Le monde fictif garnit ses sites **avec des valeurs distinctes champ par
champ** : altitude et escarpement egaux laisseraient passer une inversion des
deux.

**MESURE, deux lancements sur le meme binaire, meme graine, meme monde :**

    appels a Sites au second lancement    1  ->  0
    cache lu -> premiere trame jouee      6,10 s  ->  3,39 s
    sites relus                           0 tables / 0 canyons -> 1 / 16
    geometrie                             2390 chunks, 3 140 011 triangles, inchangee

Les 2,71 s gagnees sont exactement la passe : dans le lancement temoin elle
tenait 2,74 s entre l'adoption du monde par le voxel et sa disponibilite.

**COUT DISQUE : 688 octets avant compression** -- 17 sites de deux `FVector2D`
(donc deux fois seize octets, `FVector2D` etant en double) et deux flottants,
plus deux comptes. A cote de 78,6 Mo de cache. Version de chaine portee a
**25** : un cache anterieur relirait les sites vides, exactement le cas qu'on
vient de corriger.

**PIEGE D'OUTILLAGE, ET IL EST SILENCIEUX : Git Bash reecrit les chemins de
carte.** Lancer le jeu depuis bash avec `/Game/Worldseed/Maps/L_Worldseed_Proc`
donne dans le journal :

    LoadMap: C:/Program Files/Git/Game/Worldseed/Maps/L_Worldseed_Proc
    Error: Failed to load package

MSYS prend tout argument commencant par `/` pour un chemin POSIX et le prefixe
de sa racine d'installation. Le jeu demarre, ne charge rien, et QUITTE -- le
journal ne porte alors **aucune ligne `[Worldseed]`**, ce qui ressemble trait
pour trait a un module qui ne s'initialise pas. **Tout lancement du jeu passe
par PowerShell**, ou le chemin arrive intact.

### Le menage du 23 septembre : ce qui ne reglait rien est parti (23 septembre 2026)

Signale par le proprietaire apres une soiree de recherche infructueuse sur les
zebrures : « on a fait plein de code pour le trouver qui ne sert a rien [...] on
est en train de garder plein de mini-dettes et de modifications de code qui ne
reglent rien ». Il avait raison, et l'inventaire l'a chiffre.

**CE QUI A ETE RETIRE, EN TROIS NIVEAUX :**

| | poids |
|---|---|
| `WorldseedPolyline` + `WorldseedLabel`, aucun appelant nulle part | **798 lignes** |
| `WorldseedQualiteRendu`, livre eteint (`NiveauParDefaut = INDEX_NONE`) | 169 lignes |
| `rugositeMin`, `overhangWarpM`, `archeAmplitudeM` -- termes inertes | ~390 lignes |
| **22 surcharges de ligne de commande** dont la question etait tranchee | ~430 lignes |
| **total** | **environ 1790 lignes** |

`WorldseedVoxelTerrain.cpp` passe de **3417 a 3019 lignes**, et son `BeginPlay`
cesse d'etre 580 lignes de plomberie de mesure.

**LE CRITERE QUI A SERVI, ET IL EST REUTILISABLE : ce n'est pas l'age, c'est
l'etat de la QUESTION.** On garde le levier d'une question ouverte, on retire
celui d'une question close -- sa mesure est dans ce fichier, qui est la seule
part qui vaut encore dans six mois, et git rend le bloc en une commande.

**GARDES A DESSEIN, chacun pour une raison nommee** : le harnais (photos,
arrets, banc, graine, rayon, anneaux, depart, cap, vues) ; les leviers des
questions encore OUVERTES -- `DetailOctaves`, `Voxel` et `Niveaux` pour le noir,
`CouleurRoche` pour la zebrure ; l'instrument `CarteCauses`, qui n'est pas un
levier d'A/B mais une SONDE et qui vient de nommer la cause du noir ; et `Perp`,
entrelace avec la modulation du detail par la pente.

**`WarpForce` ET `WarpFreq` N'AVAIENT PAS LE PREFIXE `Worldseed`.** Une
collision avec un parametre du moteur n'aurait rien signale. Toute surcharge de
ce depot doit porter le prefixe.

**LA NON-REGRESSION D'UN REFACTOR SE PREND AVANT, ET A TROIS NIVEAUX ICI.** Les
28 tests d'automation ont ete releves AVANT de toucher au premier fichier, puis
rejoues : 28 verts des deux cotes. Mais **ils tournent en `nullrhi`** -- ils
n'exercent ni le streaming, ni l'eau, ni le sol de fond, c'est-a-dire
exactement ce que ce menage touchait. D'ou, en plus : le jeu joue de bout en
bout avec le monde REGENERE (altitudes -371..1772 m, terres 29,2 %, au chiffre
pres comme avant) et la vue canyon02 comparee a son image de reference.

**PIEGE DE COMPILATION** : une compilation de dix secondes qui rend zero n'est
pas une preuve. Le controle est l'HORODATAGE du binaire -- et sa taille, qui est
passee de 2,72 a 2,53 Mo.

**PIEGES D'OUTILLAGE PAYES PENDANT LE MENAGE :**
- `sed -i '415a\t\tcode'` insere un **`t` litteral**, pas une tabulation.
  Employer `perl -i -pe` pour une insertion indentee.
- un motif `perl` qui emploie `.` pour filer une apostrophe la remplace par un
  point dans le REMPLACEMENT aussi : `jusqu'a` est devenu `jusqu.a`.
- retirer un bloc laisse parfois une **accolade orpheline** ou un `{ }` vide que
  le compilateur accepte sans rien dire. Les chercher explicitement apres coup.

**UNE SEULE CHOSE N'A PAS ETE RETIREE DE SA CATEGORIE** : `bPorteePerpendiculaire`.
Elle partage la norme du gradient avec la modulation du detail par la pente,
donc la retirer demanderait de demeler les deux pour un booleen. Le commentaire
du champ le dit desormais, au lieu de renvoyer a `rugositeMin` qui n'existe plus.

### La couverture de tests, et cinq facons de se tromper de mesure (23 septembre 2026)

Demande du proprietaire : revue de code, decouplage, et maximiser la couverture.
Etat de depart mesure : **41 236 lignes de code pour 3 337 de tests**, soit 8 %,
et **les passes du DEBUT de chaine n'avaient aucun oracle** -- Perlin 620 lignes,
Tectonics 629, Erosion 440, WaterBodies 748, Noise 438, Fields 213, Coast 190,
Fins 167, Ice 92. **28 -> 57 tests verts** au terme de la session.

**LE TROU N'ETAIT PAS OU ON L'ATTEND.** `Perlin` et `Noise` sont le socle de
TOUT -- relief, tectonique, erosion, diaclases, lames, cavites -- et un bruit
qui derive ne casse rien : il DEPLACE le monde en silence, et aucune mesure de
ce depot ne le verrait.

**CINQ PIEGES DE MESURE PAYES DANS LA MEME SESSION, et ce sont eux qui valent :**

1. **UNE DIFFERENCE FINIE EXIGE QUE LE PAS DEPASSE L'ULP DU TYPE A LA MAGNITUDE
   OU L'ON MESURE.** Le test de Worley a echoue sur une pente de 1,666 pour une
   fonction 1-lipschitzienne par construction. Cause : le balayage montait a
   X ~ 16 400, ou l'ULP d'un `float` vaut 9,8e-4 -- exactement le pas de 1e-3.
   `X + E` tombait sur X ou X + 2 ULP, le pas reel etait indetermine, la pente
   gonflee d'un facteur allant jusqu'a deux. Borne a 256 : **1,007**. Le meme
   biais gonflait la continuite de Perlin (2,98 contre 2,53) et passait
   inapercu sous un seuil genereux.

2. **UNE FIXTURE DOIT REPRODUIRE LA RESOLUTION DU CAS REEL** des qu'une passe
   compare une distance EN PIXELS a une longueur en metres. Les deux tests du
   littoral ont echoue sur leur temoin : la passe compare
   `T = Distance_px * MetresParPixel / Recul` a 0,35, et ma grille faisait 125 m
   par maille pour un recul de 260 -- le PREMIER pixel de terre etait deja a
   T = 0,48, la passe ne pouvait RIEN faire. Le monde reel travaille a 15,6 m.

3. **LE CODE PEUT DETROMPER SUR SA PROPRE PREMISSE.** J'attendais que 800 mm de
   pluie erodent plus que 0 : mesure, **103387,297 contre 103387,305**. Ce n'est
   pas un defaut -- `RainWeight = PrecipMm / mediane des terres`, donc un champ
   UNIFORME ne porte aucune information quel que soit son niveau. Et c'est ce
   qu'on veut : `targetMeanLandMm` a deja ete corrige sans toucher au relief.
   Le bon oracle porte sur le CONTRASTE -- la meme moitie perd 88 072 m arrosee
   contre 34 051 aride -- **et sur le niveau absolu, qui doit rester sans
   effet**. Les deux moities de la propriete, pas une.

4. **LE BUILD UNIFIE FRAPPE AUSSI LES TESTS.** `CompterNonFinis` vivait dans le
   namespace ANONYME de trois fichiers ; UBT les concatene, et la compilation
   tombe sur « la fonction a deja un corps » -- dans un fichier auquel on n'a
   pas touche. Troisieme fois dans ce depot, apres `WorldseedMetersToCm` et
   `SUB`. Les auxiliaires partages vivent desormais dans la fixture.

5. **LE PREMIER LANCEMENT APRES UN BUILD N'EST PAS COMPARABLE.** Le banc rendait
   776/1033/625 chunks apres le refactor de la diffusion contre 894/1175/680 au
   releve pris une heure plus tot : 11 % d'ecart, systematique, reproductible.
   `git stash` + rebuild + banc dans l'etat courant : le temoin SANS refactor a
   rendu 697/1008/628, puis 776/1034/625, puis 776/1033/625. Identique au
   refactor des que la mesure est CHAUDE. **Ce releve demande une passe de
   chauffe**, et les deux valeurs aberrantes etaient les deux premiers
   lancements apres compilation.

**CHAQUE TEST PORTE SON TEMOIN.** Ce depot a paye quatre fixtures muettes en une
journee : un bruit identiquement nul passerait « Perlin s'annule aux noeuds »,
une correlation buguee a zero passerait « la graine separe », une passe inerte
passerait « rien ne bouge au-dela de la portee ». On verifie donc toujours aussi
que la chose MESUREE varie.

### Un repli journalise ressemble a une mesure (24 septembre 2026)

Signale par le proprietaire en relisant un journal : « le journal annonce
`regles chargees : ... 16.0 x 8.0 km` alors que le monde fait 64 x 32, corrige
stp ». Il avait raison, et le defaut etait plus large que la ligne.

**LES TROIS CHIFFRES NE DECRIVAIENT AUCUN MONDE GENERE.** `LoadRules` batit une
geometrie depuis le fichier, puis `WorldseedPipeline::Generate` en ECRASE
aussitot NY, NX et HeightM avec la taille et la resolution de l'appelant
(`WorldseedPipeline.cpp:133-135`) -- seuls `LatSpanDeg`, `LatitudeMapping` et
`LatitudeEqualAreaBlend` survivent a la copie, et ce sont exactement les trois
champs que le menu recopie a la main. La ligne journalisait donc un REPLI
toujours remplace, sous une forme qui se lit comme une mesure.

**CE N'EST PAS UN DEFAUT D'ARRONDI, C'EST UNE ETIQUETTE FAUSSE.** `world.sizeKm`
vaut 8 et n'est pas la taille du monde : c'est la HAUTEUR DE REFERENCE du calage
metrique, dont `WorldseedVerticalScale` tire son rapport. La ligne dit desormais
`reference d'echelle 8.0 km` et ne prétend plus rien sur la taille -- laquelle
est dite par `monde genere : ... 64000 x 32000 m`, qui la tient de l'appelant.
Verifie a l'execution, les deux lignes cote a cote.

**DEUX CLES MORTES TROUVEES EN TIRANT LE FIL, et il faut le dire :**
`world.simResolution` n'est lue QU'a cet endroit et sa valeur ne sert jamais ;
`world.resolution` n'est lue NULLE PART depuis la disparition du pipeline PNG.
Les retirer est un menage legitime **mais il change l'empreinte MD5 du fichier
de regles**, donc invalide tous les mondes en cache : c'est un arbitrage du
proprietaire, pas un detail de nettoyage. Elles sont laissees en place, et le
commentaire du code le dit.

**LA REGLE GENERALE** : un journal ne doit pas afficher une grandeur qu'un autre
chemin va ecraser. Si elle sert de repli, ou bien on ne la journalise pas, ou
bien on la nomme pour ce qu'elle est. Du bruit qui ressemble a une information
coute plus cher que pas d'information -- c'est la meme famille que
« frame_ms vaut EXACTEMENT gpu_ms » et que les releves identiques au chiffre
pres : une valeur plausible qu'on lit comme une mesure.

**ET `ETAT_DES_LIEUX.md` EST SORTI DU DEPOT LE MEME JOUR**, a la demande du
proprietaire : il reste en local et figure au `.gitignore`. Les renvois vers ses
sections ne resolvent plus pour qui clone. Ce qui doit survivre a une session va
DANS CE FICHIER ou dans `README.md`.

### Un chiffre DERIVE dans un commentaire se perime en silence (24 septembre 2026)

Signale par le proprietaire apres la relecture de l'atlas : « pour les strates,
il faut caler ca sur la derniere mesure, c'est elle qui fait foi ». Trois
chiffres du bloc `strates` de `world_rules.json` decrivaient un etat qui
n'existe plus, et aucun ne pouvait se signaler.

| commentaire | il disait | la mesure dit |
|---|---|---|
| `_comment_serie` | epaisseur totale **420 m** | **253 m** -- la somme des dix epaisseurs juste en dessous, et ce que le journal rapporte a chaque generation |
| `_comment_datumM` | « ce monde va de **-351 a 1700 m** » | **-371 a 1772 m** avant le dome de calotte |
| `_comment_contrasteErosion` | « les K des **six** bancs ... moyenne du monde **0,73** » | **dix** bancs, moyenne **0,70**, K de 0,57 a 1,77 |

**LE PLUS INSTRUCTIF EST LE PREMIER : c'etait un chiffre DERIVE, pose a cote de
ce dont il derive.** La somme des epaisseurs est trois lignes plus bas ; il
suffisait d'ajouter un banc sans refaire l'addition pour que le commentaire
devienne faux, et rien -- ni compilateur, ni oracle, ni sonde -- ne pouvait le
dire. **Quand une donnee porte deja son propre total, le commentaire ne doit pas
le recopier** : soit il renvoie a la somme, soit il la cite en disant d'ou elle
vient. Ici le commentaire cite desormais la ligne de journal, qui est recalculee.

Les deux autres sont de la meme famille que la ligne de journal corrigee le
matin meme : **une valeur plausible qu'on lit comme une mesure**. Elles sont
gardees mais datees, et la mesure historique de `contrasteErosion` -- celle qui
a DECIDE le reglage -- garde son contexte (six bancs, moyenne 0,73) au lieu
d'etre reecrite : c'est le raisonnement qui vaut, pas le chiffre.

**CE QUE COUTE LA CORRECTION, ET CE QU'ELLE NE COUTE PAS.** L'empreinte de
`world_rules.json` est un MD5 du fichier ENTIER : retoucher un commentaire
invalide donc tous les mondes en cache et impose une regeneration de 210 a
260 s. **Mais le monde ne bouge pas d'un octet**, et c'est verifie et non
suppose : le corps du fichier de cache -- tout sauf l'en-tete de 68 octets, qui
porte justement l'empreinte -- rend le meme MD5 avant et apres.

C'est exactement le controle qui avait servi le matin meme, quand les sondes
annoncaient un sommet a 2032 m et la generation fraiche 1842 : deux mondes en
apparence, un seul en fait. **Devant un cache dont la cle a bouge sans que le
code change, comparer les CORPS et non les noms de fichier.**

### Un cache toujours perime : `Strncpy` mange un caractere (26 septembre 2026)

`WorldseedCache.cpp` ecrivait l'empreinte des regles par
`FCStringAnsi::Strncpy(Header.RulesHash, ..., HashChars)`. Cette fonction
**GARANTIT LE ZERO FINAL** : sur les trente-deux octets du champ, elle n'en
ecrivait que **trente et un**. Le hash relu ne pouvait donc jamais egaler le
hash courant, et `ListEntries` declarait TOUTE entree perimee -- en permanence,
et depuis toujours.

    stocke : 752173241ad26ff9f361c08013edeee   + un octet nul
    attendu: 752173241ad26ff9f361c08013edeee0

**LE DEFAUT ETAIT INVISIBLE PARCE QUE LE CHARGEMENT NE LIT PAS CE CHAMP.**
`Load` ne verifie que `Magic` et `FormatVersion`, la cle du fichier portant deja
le hash. Le cache se relisait donc parfaitement pendant que l'inventaire le
disait perime : « 3 perimee(s), produites par une autre version » au pied du
menu, sur un cache sain. **J'ai d'abord attribue ce message a un commit de
commentaires de `world_rules.json` -- c'etait faux.**

**TROUVE EN MONTANT UN TEMOIN, ET C'EST TOUT L'INTERET DU TEMOIN.** Pour
eprouver la purge automatique ajoutee le meme jour, une entree a ete corrompue
volontairement (`PipelineVersion` a 99, en-tete restant LISIBLE donc perimee et
non illisible). Le releve a rendu **deux** suppressions pour une seule
corruption : le cache sain etait parti avec elle. Sans ce temoin, la purge a
l'ouverture du menu aurait vide tout le cache a chaque lancement.

**ET UNE GARDE POSEE SUR UN RAISONNEMENT FAUX A ETE RETIREE.** J'avais protege
la purge contre une empreinte VIDE, en croyant qu'elle declarerait tout perime.
C'est l'inverse : `ListEntries` traite une empreinte vide comme « ne compare pas
les regles ». La garde etait inutile, et son commentaire affirmait le contraire
du code -- ce qui est pire que pas de commentaire.

### Un compteur qui rend zero ne mesure rien -- troisieme fois (26 septembre 2026)

L'ISM par chunk a vide les composants globaux, et `EspecesAutour` les parcourait
toujours : il annoncait **« 0 espece »** au milieu de 25 585 instances vivantes.
Meme famille que le comptage de composants d'herbe de Landscape, ou la demo du
pack rendait le meme zero que notre monde.

**LA PARADE EST LA MEME A CHAQUE FOIS** : valider la metrique sur un cas dont on
connait la reponse. Ici, le releve global disait « 25 585 vivantes » pendant que
l'outil disait zero -- les deux ne pouvaient pas avoir raison.

**ET UN RELEVE DOIT PORTER SA CONFIGURATION.** La ligne de vegetation dit
desormais quel mode tourne (« UN ISM PAR CHUNK » ou « un ISM global par
espece ») et combien de composants de chunk existent. Sans cela deux releves ne
se comparent a rien six mois plus tard.

### Trois pieges de protocole payes le meme jour (26 septembre 2026)

- **DECOUPER PAR BORNES DE LIGNES NE TIENT PAS**, et le registre le disait deja
  pour le refactor des sondes. Refait : les `sed -i` successifs sur
  `WorldseedMenuWidget.h` ont laisse un `UFUNCTION()` ORPHELIN et perdu une
  declaration, parce que chaque suppression decale les suivantes. Restaure
  depuis le commit, refait a l'editeur bloc par bloc. **Pour du C++, l'editeur
  de fichiers ; jamais une suppression par numero de ligne.**
- **UN FICHIER PRESENT N'EST PAS UN FICHIER FRAIS.** Le releve des tests a ete
  lu dans `Worldseed_2.log`, qui datait de six minutes avant la compilation : on
  aurait valide un refactor sur les chiffres d'AVANT. Controler l'horodatage,
  ou supprimer la cible avant de relancer.
- **UN A/B EN MOUVEMENT EXIGE UN TRAJET FIXE.** Deux passes de banc ont parcouru
  9 698 m et 733 m -- le personnage etant tombe dans un cas -- et leurs chiffres
  ont ete compares comme s'ils mesuraient la meme chose. `-WorldseedDepartX/Y`
  plus `-WorldseedVol=` rendent les deux moities comparables ; sans cela on
  mesure la chute, pas le reglage.

### Ce que les tests couvrent, et ce qu'ils ne couvrent pas (26 septembre 2026)

**91 -> 100 oracles.** Le trou etait exactement la ou le travail etait le plus
recent : `WorldseedVegetation` (517 lignes), `WorldseedPlateau` (729),
`WorldseedRecettes` (335) et `WorldseedParois` (275) n'avaient **aucun** test.

Le critere de choix n'est pas « quelle fonction n'est pas couverte » mais
**« quel defaut passerait inapercu »** : une plante posee deux fois se confond
avec une plante posee une fois, de l'herbe sous la mer ne se remarque qu'en
nageant, et un semis qui cesse d'etre deterministe fait SAUTER la vegetation a
chaque remaillage -- un scintillement qu'on met sur le compte du streaming.

**DEUX TEMOINS MONTES POUR DE VRAI, PUIS RETIRES.** Un test qui passe du premier
coup n'est pas prouve discriminant :

    garde du doublon desarmee  -> attendu 0 doublon, obtenu 12
    garde du substrat desarmee -> 0 galet hors estran contre 256
                                  0 plante de biome sur l'estran contre 256

Dans les deux cas les AUTRES tests sont restes verts, ce qui montre que chacun
vise bien sa garde.

**RESTE SANS ORACLE** : `AWorldseedVoxelTerrain` (3337 lignes, mais c'est un
acteur et il demande un monde), `WorldseedRvt`, l'erosion et le sapement -- dont
les invariants sont des ecarts de PENTE, donc statistiques et lies au calage --
et tout le rendu, qui ne se juge qu'a l'image.

### Un conteneur global a initialisation dynamique fait planter le MOTEUR (26 septembre 2026)

Portage du generateur de noms. Le module compile du premier coup, et l'editeur
en ligne de commande meurt en `EXCEPTION_ACCESS_VIOLATION` dans CoreUObject --
adresse `0x000004de00000040`, pile entierement en `UnknownFunction`, **juste
apres `LogDeviceProfileManager` et bien avant qu'une seule ligne de Worldseed
ne tourne**. Rien dans le journal ne designe le fichier fautif.

**LA CAUSE : TROIS CONTENEURS POSES A PORTEE DE FICHIER.**

    const TArray<FString> OrdreVoulu = { ... };      // <- dynamique
    TMap<FString, FWorldseedUnivers> Catalogue;      // <- dynamique
    TArray<FString> OrdreCharge;                     // <- dynamique

Ils se construisent au chargement de la DLL -- donc AVANT que le moteur soit
pret -- et se detruisent APRES le depart de son allocateur. Le remede est une
**statique LOCALE rendue par un accesseur** : elle se construit a son premier
appel, le moteur tourne alors, et la construction est thread-safe depuis C++11.
Un `bool` ou un `const TCHAR* const` restent legitimes a portee de fichier :
ils n'ont aucune initialisation dynamique.

**LE TEMOIN, ET IL FALLAIT LE MONTER AVANT DE CHERCHER.** La pile ne nommait
aucun de nos fichiers, donc lire le code n'aurait rien donne. Sortir les trois
fichiers neufs du dossier et recompiler coute trente secondes et tranche :

    avec les fichiers neufs   EXCEPTION_ACCESS_VIOLATION au demarrage
    sans                      **** TEST COMPLETE. EXIT CODE: 0 ****

Le defaut etait a nous, et il n'etait dans aucune des lignes qu'on aurait
relues -- il etait dans la FACON de declarer trois variables.

**ET UN SEUIL DE TEST POSE A VUE ECHOUE SUR DES DONNEES SAINES.** L'oracle des
cles exigeait « plus de 100 motifs balayes » ; le fichier en porte **85**. Il a
donc crie sur des tables parfaitement coherentes -- aucune cle manquante sur
les onze univers -- et c'est le chiffre attendu qui etait invente, pas la
mesure. Les seuils sont desormais cales sur le releve (85 motifs, 154 cles) avec
la moitie de marge, et le compte part au journal par `AddInfo` plutot que de
n'apparaitre qu'en cas d'echec.

**`AddInfo` SORT BIEN AU JOURNAL** meme quand le test passe, entre `BeginEvents`
et `EndEvents` -- c'est par la que passent les echantillons de noms, et c'est ce
qui permet de REGARDER une forme qu'aucun oracle ne sait juger. Trois choses
etablies a l'oeil et par rien d'autre : l'accord en genre (« Torvald fils de
Hakon » contre « Ragnhild fille de Sigurd »), l'elision (« de Urthruk » rendu
« les Terres brulees d'Urthruk »), et le fait qu'aucun `{` ni `~` ne survit.

**PIEGE D'OUTILLAGE** : `-ExecCmds="<commande>;Quit"` SANS `Automation` lance
l'editeur COMPLET, qui charge la carte par defaut -- plusieurs minutes, et la
commande ne sort jamais dans le delai. Pour faire imprimer quelque chose vite,
passer par un test d'automation (`-nullrhi`, trois secondes) ; la commande
console, elle, sert en jeu.

### Un A/B d'image par lancements successifs exige SON TEMOIN (28 septembre 2026)

Mesure entre deux configurations differentes : **49,02 %** de pixels changes,
ecart moyen 17,18 sur 765. Mesure entre deux lancements **RIGOUREUSEMENT
IDENTIQUES** : **60,60 %**, ecart moyen 20,03. Le signal est SOUS le plancher de
bruit -- Lumen et TSR ne convergent pas pareil d'un lancement a l'autre.

Le signe qui aurait du alerter avant le temoin : **le CIEL changeait de 43 %**,
alors qu'aucun materiau de vegetation ne peut l'atteindre. Une zone que le
traitement ne touche pas et qui bouge quand meme mesure le bruit.

**Ce qui decide alors n'est pas un pourcentage mais un CRITERE BINAIRE** -- ici
« y a-t-il du bleu, oui ou non » -- juge a l'oeil sur une image ou le sujet est
present. Le depot avait deja releve 82 % entre deux passes identiques sur une
autre scene ; c'est desormais deux fois.

### Une capture ne vaut que par son SUJET, et trois facons de le rater (28 septembre 2026)

- **`-WorldseedVue=` se pose en (0, 0) par defaut**, c'est-a-dire au CENTRE DU
  MONDE -- qui est en pleine mer sur ce monde, **sol a -349 m**. Ma premiere
  serie de quatre vues photographiait le fond de l'ocean a travers l'eau : une
  masse bleu-gris sans texture, parfaitement interpretable comme « le terrain a
  perdu ses textures ». Le journal le disait pourtant en toutes lettres, et la
  ligne `especes dans 150 m : 0` le confirmait.
- **`-WorldseedPhotos` est REQUIS**, meme pour une vue libre : `OnWorldBeginPlay`
  sort a sa premiere garde sans lui. Sans ce drapeau la partie se lance, ne
  photographie rien, et **ne quitte jamais** -- mon premier essai a tourne dix
  minutes pour rien.
- **`-WorldseedGraine=` ne prend PAS sur le chemin du lancement direct.** Demande
  a 1337, le journal repond « monde repris du cache : seed=20260909 ». Toute
  comparaison qui suppose la graine demandee est fausse ; lire la ligne.

**LE CONTROLE QUI SAUVE CES TROIS CAS TIENT EN UNE LIGNE DE JOURNAL** :
`-WorldseedEspeces=<rayon>` dit combien d'instances entourent le point de vue.
`0` signifie qu'il n'y a rien a photographier. C'est exactement ce qui manquait
a l'A/B des pans du 27 septembre, « tombe sur un arret ou aucun pan n'etait en
vue ».

### awk en mode texte mange les CRLF (28 septembre 2026)

Pour retirer un bloc de quatre-vingt-douze lignes d'un `.cpp`, `awk 'NR<a ||
NR>b'` rend un fichier en **LF seul** la ou l'original est en CRLF : le diff
devient le fichier entier. `head -n X` puis `tail -n +Y` sont fideles aux
octets -- verifie par `cmp` sur la tete ET sur la queue avant d'installer.

Et le fichier avait **une ligne en LF seul** au milieu d'un fichier CRLF,
laissee par une edition precedente : c'est ce qui a fait echouer la variante
« awk avec ORS=CRLF », qui aurait normalise cette ligne au passage. Un
`grep -c` de fin de ligne ne l'avait pas vue ; `od -c` si.

### Le cache portait la GLACE, et le pole grandissait au second lancement (28 septembre 2026)

Trouve PAR ACCIDENT, en verifiant les chiffres d'un autre chantier : deux
relevés du meme reglage donnaient deux altitudes polaires differentes. C'est le
signe que ce depot connait par coeur, et pour une fois il ne denoncait pas une
mesure fausse -- il denoncait le monde.

**LE DEFAUT.** `WorldseedIce::Apply` ecrit DANS le relief -- sa signature le dit,
`TArray<float>&` -- et l'ecriture du cache la suivait (glace ligne 667, cache
ligne 773). Le fichier gardait donc un relief **deja englace**, sur lequel la
branche de reprise reposait un second dome.

| graine 20260909 | generation | rechargement |
|---|---|---|
| bande -90..-80 | 685 m | **924 m** (+239) |
| bande -80..-70 | 595 m | **707 m** (+112) |
| sommet du monde | 1611 m | **1669 m** |

**Un joueur avait un pole trois cents metres plus haut au SECOND lancement de
son monde qu'au premier, sans un mot au journal.**

**POURQUOI RIEN NE L'AVAIT VU, ET C'EST LE POINT :**

- **le defaut n'est pas CUMULATIF.** Deux relectures successives rendent le meme
  chiffre au metre -- verifie. Il ne se lit qu'en comparant les deux CHEMINS, ce
  qu'aucune sonde ne faisait : elles mesurent un monde, pas deux facons de
  l'obtenir ;
- **le commentaire de la passe decrivait l'invariant JUSTE** -- « elle passe
  apres la mise en cache, qui garde le relief de ROCHE » -- pendant que le code
  faisait l'inverse. **Une note exacte posee sur du code faux ne protege rien**,
  et elle endort : je l'ai lue AVANT de mesurer, et elle m'a presque convaincu
  qu'il n'y avait rien a chercher.

**LE CORRECTIF REND LE CODE CONFORME A SON INTENTION**, et non l'inverse : la
branche de generation prend une copie du relief AVANT la glace, et c'est elle
qui part au cache. Un tableau copie par monde, une fois.

**ET IL NE FAUT PAS « SIMPLIFIER » EN RETIRANT L'APPEL DU CHEMIN DU CACHE** pour
y laisser un relief englace -- c'etait l'autre correctif possible, plus court
d'une ligne. Le classificateur de biomes LIT le relief : la carte des biomes
deviendrait dependante du chemin pris. Elle ne differe pas aujourd'hui, le dome
ne s'ajoutant que la ou la calotte est deja posee, mais c'est un equilibre et
non une garantie.

**LE BUMP DE VERSION EST OBLIGATOIRE, ET C'EST LE PIEGE DE CE CORRECTIF** : tout
cache anterieur porte la glace, donc la correction lui en reposerait une seconde.
`WORLDSEED_PIPELINE_VERSION` 27 -> 28.

**L'ORACLE PORTE SON PROPRE TEMOIN, et c'est lui qui detecte le defaut.**
`Worldseed.Cache.LeCachePorteLaRoche` genere un monde de 128 lignes, relit le
fichier, et exige d'abord que le relief du CACHE **DIFFERE** de celui qu'on vient
de generer -- ce qui prouve du meme coup que la glace existe et que le fichier ne
la porte pas. Sans cette exigence, un monde sans calotte ferait passer la
comparaison finale trivialement. Releve : 922 cellules englacees sur 32 768, dome
maximal 286,9 m, et **0 cellule differente entre les deux chemins**.

**TEMOIN MONTE PUIS RETIRE** : le defaut remis, le test tombe sur
« glace : 0 cellules » -- avec le cache englace, l'ecart est nul partout et le
TEMOIN crie avant meme la comparaison finale. C'est le bon ordre d'echec.

**ET L'INVARIANT SE LIT CELLULE PAR CELLULE, jamais sur le seul sommet** : un
dome pose deux fois au pole ne deplace pas forcement le maximum du monde, qui
est une montagne ailleurs -- il ne l'avait deplace que de 58 m quand la bande
polaire, elle, montait de 239.

