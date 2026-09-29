# Registre Worldseed — le terrain voxel

> **CE FICHIER NE SE LIT PAS EN ENTIER, IL SE CHERCHE.** Il porte le recit des
> seances : ce qui a ete mesure, ce qui a tranche, et surtout **les pistes
> abandonnees**, pour qu'on ne les retente pas. Les REGLES qu'on en a tirees
> vivent dans `CLAUDE.md`, chargé a chaque tour ; ici se trouve la PREUVE.
>
> ```
> grep -n -i '<le mot du sujet>' Docs/registre/*.md
> grep -A40 'le titre exact' Docs/registre/voxel.md
> ```
>
> **Le contenu est CUMULATIF.** Certaines notes decrivent un etat qui n'existe
> plus ; les plus dangereuses portent une marque de peremption. Avant d'agir sur
> une note, verifier que ce qu'elle nomme existe encore — une note qui survit a
> ce qu'elle decrit coute plus cher que pas de note.
>
> **Ce fichier** : mailleur, chunks, champ de densite, Transvoxel, anneaux de resolution, nappe d'horizon, collision et placement du joueur.

### Terrain voxel : ce que coute un chunk, et les trois leviers (18 septembre 2026)

Le terrain passe d'une carte d'altitude a un CHAMP DE DENSITE maille par
marching cubes : negatif dans la roche, positif dans l'air, la surface est
l'isovaleur zero. Decisions du proprietaire : voxel de **1 m**, bande creusable
de **100 m**, rendu **lisse**, raccord **Transvoxel**, remplacement du terrain
actuel.

**IL N'Y A PAS DE GRILLE 3D, ET IL NE PEUT PAS Y EN AVOIR.** A un metre de cote,
ce monde ferait **soixante-seize milliards de voxels**. Le champ est une
FONCTION batie sur la grille 2D que la chaine calcule deja, plus du bruit 3D.
Seules les modifications du joueur seront stockees.

**MESURES, 95 chunks de 32 m au point le plus haut du monde, grille 2048x1024 :**

|                                       | ms/chunk | au pire | evaluations | triangles |
|---------------------------------------|---------:|--------:|------------:|----------:|
| premiere version                       |    23,43 |   39,08 |      45 101 |   172 233 |
| + sortie rapide loin de la surface     |     9,93 |   23,05 |      45 101 |   172 807 |
| + normales moyennees des faces         |     7,83 |   15,69 |      39 304 |   172 807 |
| + suivi de surface depuis des germes   |  **1,57** |  6,66 |   **3 733** |   172 548 |

Quinze fois plus rapide **pour la meme surface a 0,15 % pres**. C'est LE
controle : une optimisation qui change la geometrie n'est pas une optimisation.

- **La sortie rapide** part du fait que loin de la surface, le bruit ne peut plus
  changer le SIGNE du champ, donc plus deplacer l'isovaleur zero. Le mailleur n'y
  lit qu'un signe : calculer la valeur exacte etait payer pour rien. Seuil =
  amplitude des surplombs + rayon des galeries.
- **Les normales au gradient du champ coutent six evaluations par sommet**, soit
  195 ms sur 943 mesurees. La moyenne des faces incidentes, ponderee par leur
  aire, tombe a **1 ms** : le marching cubes partage ses sommets le long des
  aretes, donc elle est deja continue. Le gradient reste dans l'historique git
  si l'eclairage montre un jour des facettes.
- **`FMarchingCubes::GenerateContinuation(Germes)` est le gros levier**, et il
  est dans le moteur. `Generate()` evalue chaque cellule du chunk -- 32 768 pour
  un cube de 32 m -- alors que la surface n'en traverse qu'une coque. La
  continuation part de germes et propage. Les germes viennent d'un balayage
  grossier (un point sur quatre par axe, 729 evaluations), qui tranche du meme
  coup le cas des chunks VIDES : les deux tiers du volume ne coutent plus que ce
  balayage.
  **CE QU'IL COUTE** : une poche entierement contenue entre deux points du
  balayage -- moins de 4 m ici -- n'est pas vue. Mesure : **un chunk sur 95** a
  bascule de « avec surface » a « vide », pour 259 triangles.

**GeometryCore est un module d'EXECUTION** (`Engine` et `Chaos` en dependent) :
une ligne dans le `.Build.cs` suffit, aucun plugin a activer, et cela part en
build final. `FMarchingCubes` prend une `TFunction`, sait s'annuler (`CancelF`)
et tourne depuis n'importe quel fil.

**`ProceduralMeshComponent` n'accepte que des positions en DOUBLE precision.**
Stocker des flottants n'economise rien, cela ajoute une conversion. Et les
octets rapportes par un tampon de construction ne sont PAS le cout resident :
ce tampon se reutilise d'un chunk a l'autre, le vrai cout est la copie interne
du composant (de l'ordre de 190 Ko par chunk ici).

**PIEGE DU BUILD UNIFIE, paye comptant.** `WorldseedMetersToCm` etait defini
dans quatre namespaces ANONYMES. UBT concatene les `.cpp` en une seule unite de
traduction, ou deux namespaces anonymes n'en font qu'un : l'ajout de deux
fichiers a suffi a faire tomber deux fichiers EXISTANTS dans le meme groupe, et
la compilation a casse sur un fichier que personne n'avait touche. **La
collision ne dependait pas du code ecrit mais du REGROUPEMENT choisi par UBT.**
La constante est desormais unique, dans `WorldseedRules.h`.

**MESURER SANS MCP.** Le lien MCP tombe quand l'editeur est tue et relance
plusieurs fois de suite (ce que fait chaque compilation), et il ne se retablit
pas tout seul dans la session. La sonde tourne alors en commandlet :

    UnrealEditor-Cmd.exe Worldseed.uproject -run=pythonscript -script="<fichier>" -unattended -nopause -nosplash

**L'EDITEUR DOIT ETRE ARRETE D'ABORD** : le plugin ecoute sur le port 8000, et le
commandlet echoue sur « HttpListener unable to bind to 127.0.0.1:8000 » -- une
erreur qui fait echouer tout le commandlet, pas seulement l'ecoute. Le resultat
se lit dans `Saved/Logs/Worldseed.log`, la sortie standard de PowerShell ne le
capture pas.

### Les surplombs ne se font PAS en deformant une carte d'altitude (18 septembre 2026)

Deux mecanismes essayes, mesures, et **tous deux abandonnes**. Ne pas les refaire.

**1. Deplacement VERTICAL** (`overhangAmplitudeM`, toujours en place et utile pour
le grain, mais inutile pour les surplombs). Ajouter un fBm 3D a la distance a la
surface ne replie rien : il faudrait que le gradient vertical du bruit depasse 1,
or un fBm de Perlin normalise reste tres en dessous. Mesure : **0,00 % de colonnes
franchissables meme a seize metres d'amplitude.**

**2. Deplacement HORIZONTAL du point de lecture** (`overhangWarpM`). L'idee etait
de lire le relief un peu plus loin a chaque altitude, pour qu'une falaise se
replie. **Mesure sur la collision REELLE** (relevé de planchers par sondes
verticales, 3013 colonnes communes, meme graine, meme grille) :

| | colonnes a repli | visieres 2-12 m | vide median |
|---|---|---|---|
| sans deplacement | 16,06 % | 5,11 % | 22,6 m |
| deplacement 25 m | 16,03 % | 5,18 % | 22,1 m |

**Aucun gain.** Les 16 % de colonnes ou l'on passe sous la roche viennent des
GALERIES, pas du deplacement. Ce que le deplacement fait vraiment : il remue le
relief macro — montee mediane du sommet 0,00 m mais p90 a +6,95 m, maximum
+115 m, et 371 colonnes gagnent de la roche la ou il n'y en avait pas.

**LE PIEGE DE MESURE QUI M'A FAIT CROIRE A UNE REUSSITE** : la sonde de densite
rapportait « air dans la bande 0,64 % -> 2,17 % », soit 3,4 fois plus, et j'en ai
conclu a un succes. **L'air en volume n'est pas un surplomb.** La meme sonde
disait par ailleurs « 12,35 % des colonnes ont 2 m de libre » dans les DEUX etats
— c'etait la bonne colonne du tableau, et je ne l'avais pas lue.

**MONTER LA FREQUENCE DECHIRE LE TERRAIN.** Le repli demande
`amplitude x frequence x pente > 1` ; a 25 m et 0,012 cycle/m le produit vaut
0,3, d'ou l'absence d'effet. En montant la frequence :

    0,012  ->  12,35 % de colonnes a 2 m de libre  (= l'etat sans deplacement)
    0,025  ->  18,52 %
    0,045  ->  35,80 %

mais **l'image condamne 0,045** : le deplacement n'est plus inversible, la surface
se replie sur elle-meme et part en ECAILLES DETACHEES qui flottent dans le ciel,
le pre du premier plan se delaminant en rubans qui se chevauchent. Un deplacement
de domaine ne reste une deformation valide que tant que son jacobien ne s'annule
pas ; passe cette limite, il ne plie pas la surface, il la dechire.

**Le code est garde, inerte** (`overhangWarpM: 0` dans les regles, et le terme est
court-circuite a zero), pour que la mesure reste reproductible.

**LA BONNE PISTE, pour quand on y reviendra** : ne pas deformer la surface mais
CREUSER sous elle. Un terme soustractif ne peut pas produire de lambeau, parce
qu'il enleve de la matiere a un solide au lieu de deplacer une frontiere — c'est
exactement ce que fait deja `CaveAt`, et c'est pourquoi les galeries, elles,
tiennent. Un bruit en NAPPES horizontales estompe pres de la surface donnerait des
visieres et des abris sous roche sans toucher au relief macro.

**PIEGE DE PROTOCOLE, paye comptant sur cet A/B.** Ma premiere paire d'images
etait invalide : la session A n'avait pas ete montee comme la B (auto-exposition
non coupee, SkyLight en capture temps reel recapture entre-temps). Le TEMOIN l'a
dit — le pre du premier plan, qui ne peut pas avoir bouge, avait gagne 54 % de
luminance. Recette d'A/B valable, verifiee : meme suite d'appels des deux cotes
(arret du PIE, ecriture de la regle, sonde qui relit les regles, relance,
`r.EyeAdaptationQuality 0`, meme position, meme attente), puis controle sur TROIS
temoins avant de regarder la zone d'interet — ciel 0,02 % de pixels modifies,
personnage 102,2 contre 101,0 de luminance.

**ETENDRE LA COLLISION POUR MESURER.** `collision_radius_m` vaut 120 m en jeu ;
le porter a la valeur de `load_radius_m` (250) donne de la geometrie physique sur
tout le rayon charge, donc des sondes verticales partout. Compter **~16 s** apres
le changement pour que la cuisson suive : un premier relevé fait a 10 s n'a couvert
que 662 colonnes contre 3576, et les pourcentages n'etaient pas comparables.

### Le voxel est branche sur la carte de JEU (18 septembre 2026)

Jalon M5 du plan voxel, et il manquait a tout le reste : `AWorldseedVoxelTerrain`
n'etait reference NULLE PART ailleurs que dans ses propres fichiers. Il n'existait
que pose a la main sur le banc `L_Voxel_Bench`, pendant que la carte de jeu
`L_Worldseed_Proc` -- celle que le menu ouvre -- tournait toujours sur l'ancien
mailleur en carte d'altitude, donc sans grottes.

**LA BASCULE NE DEPLACE PAS LES SERVICES, ET C'EST TOUT L'INTERET.**
`AWorldseedTerrain` ne fait pas que mailler : il batit le sol de fond -- qui
remplit l'horizon ET nourrit la texture d'information du plugin Water --, nourrit
le ciel en climat, repond aux questions de latitude et d'altitude, et pose
l'ocean. Le mailleur n'est qu'une de ses fonctions, et c'est la SEULE que le
voxel remplace. Un drapeau `bUseVoxelMesher` et trente lignes, la ou deplacer ces
services aurait demande sept cents lignes et tout l'etat qui va avec, d'un coup,
sans filet. Ca se defait en posant le drapeau a faux -- ce qui a d'ailleurs servi
a mesurer l'A/B ci-dessous.

**TROIS DEFAUTS TROUVES EN BRANCHANT, dont deux silencieux :**

- *Les deux acteurs generaient DEUX MONDES DIFFERENTS.* Sans monde en attente
  dans l'instance de jeu -- c'est le cas d'un PIE lance depuis l'editeur, sans
  passer par le menu -- chacun tombait sur sa generation de secours, et les
  resolutions ne sont pas les memes : **512 x 256 pour le terrain, 2048 x 1024
  pour le voxel**. Meme graine, relief different : le sol de fond decrivait un
  autre monde que celui qu'on a sous les pieds. Corrige par `AdoptWorld`, appele
  entre `SpawnActorDeferred` et `FinishSpawning` -- apres, il est trop tard,
  BeginPlay a deja charge.
- *L'exageration verticale n'etait pas transmise.* Le champ de densite tournait a
  1,0 quel que soit le reglage du terrain : la jonction entre l'horizon et le sol
  proche se serait vue comme une marche des que ce reglage bouge.
- *Le materiau non plus.* Les chunks voxel prenaient le gris par defaut, qui ne
  lit pas la couleur de sommet. **Visible a l'image** : relief proche
  uniformement gris, horizon colore.

**ET LE PIEGE DU BUILD UNIFIE EST REVENU**, exactement comme annonce dans la
section sur le cout d'un chunk : `SUB` etait defini dans deux namespaces ANONYMES
-- biomes et lithologie -- qu'UBT a fusionnes. Les noms de section vivent
desormais dans `WorldseedSection`, une seule fois, comme `WorldseedMetersToCm`.

**CE QUE CA COUTE.** *(Cette section a ete ECRITE FAUSSE le 18 septembre et
corrigee le 19. Les chiffres d'alors -- ancien mailleur 30,25 ms de GPU et
33,1 FPS, voxel 47,92 et 20,9, donc "+17,7 ms, 58 % de plus" -- etaient un
ARTEFACT DU BRIDAGE DE L'EDITEUR EN ARRIERE-PLAN. Je les laisse ici avec leur
correction, parce que c'est le piege qui compte, pas le chiffre.)*

**LE PIEGE.** L'editeur non focalise bride son rendu, et `PerformanceService`
rapporte alors un temps de trame plafonne par le bridage en l'attribuant au GPU.
Le signe qui aurait du alerter etait sous mes yeux : **`frame_ms` valait
EXACTEMENT `gpu_ms`** (40,23 et 40,23), ce qui ne se produit pas sur une scene
reellement limitee par le GPU. `CLAUDE.md` documentait deja le remede en
section 7 -- `PerformanceService.set_background_throttling(False)` -- et je ne
l'ai pas applique. **A poser AVANT toute mesure de performance pilotee par MCP,
sans exception.**

**LES VRAIS CHIFFRES**, bridage coupe, meme carte, meme monde, trois lectures
coincidentes de chaque cote :

| | GPU | fil de jeu | fil de rendu | trame | images/s | limite par |
|---|---|---|---|---|---|---|
| ancien mailleur | 3,44 ms | 4,47 | 3,94 | 4,47 | **223,8** | fil de jeu |
| voxel, 405 chunks | 3,41 ms | 4,76 | 4,03 | 4,76 | **210,0** | fil de jeu |

**Le voxel coute +0,29 ms sur le FIL DE JEU et rien sur le GPU** -- six pour cent
d'images en moins, pas cinquante-huit. Et la carte n'est pas limitee par le GPU :
elle l'est par le fil de jeu, a 4,8 ms pour un budget de 16,67.

**CONSEQUENCE SUR L'ARBITRAGE A3.** La conclusion d'hier -- "le LOD n'est pas
optionnel a ce rayon" -- **est annulee**. A 250 m de rayon, sans aucun niveau de
detail, la carte de jeu tourne a 210 FPS. Le LOD n'est pas necessaire a cette
distance de vue, ce qui rejoint exactement la condition posee par le
proprietaire : ne coder le LOD que si la distance de vue l'exige.

**Et le rayon de chargement n'est pas un levier utile** : mesure a 250 et a
180 m, 461 puis 384 chunks, GPU inchange. Reduire le rayon retire surtout des
chunks hors du champ de vision, qui ne coutaient rien a dessiner.

### Le joueur passait a travers le sol au-dela de 120 m (18 septembre 2026)

Signale en PIE : « le personnage tombe dans le sol au bout d'un moment de
marche ». C'etait exact, et le defaut etait dans le mailleur voxel depuis le
depart -- il ne se voyait pas sur le banc, ou l'on ne marche pas.

**LA CAUSE.** `AWorldseedVoxelTerrain` portait un `CollisionRadiusM` de 120 m,
plus court que le rayon de chargement de 250, au motif que cuire une collision
coute plus cher que mailler. Le drapeau `bCreateCollision` etait donc decide
**UNE FOIS, au televersement**, d'apres la distance a cet instant. Or
**ProceduralMeshComponent n'expose aucune facon de donner la collision a une
section deja creee** -- verifie sur toute son API : `create_mesh_section`,
`update_mesh_section`, `set_mesh_section_visible`, et rien pour la collision.
La decision etait donc definitive : un chunk pose au-dela de 120 m n'en recevait
jamais, meme quand le joueur arrivait dessus.

**MESURE DU DEFAUT**, sondes verticales tous les dix metres depuis le pion :
sol present de 0 a 110 m, **PLUS RIEN de 120 a 250 m**. La frontiere tombait
exactement sur l'ancien rayon.

**LE CORRECTIF : tout chunk maille est solide, sans exception**, et la propriete
est SUPPRIMEE pour qu'on ne la remette pas. Un chunk qu'on voit est un chunk
qu'on peut atteindre, et le rayon de CHARGEMENT borne deja le travail. Si la
cuisson coute trop cher, la reponse est de la faire de facon asynchrone --
c'est precisement ce que RealtimeMeshComponent apporterait (arbitrage A2).

Apres correction, memes sondes sur 500 m : **deux trous, a exactement plus et
moins 250 m**, c'est-a-dire la limite du rayon de chargement -- du terrain pas
encore construit, pas un defaut. Et le pion deplace par pas de 30 m jusqu'a
240 m reste en `MOVE_WALKING` a altitude constante.

**CE QUE CA COUTE** : le fil de jeu passe de 5,83 a 8,54 ms, ce qui est la
cuisson de collision. Il reste sous le budget de 16,67.

**DEUX PIEGES DE MESURE PAYES SUR CE MEME DEFAUT, et ils se ressemblent :**

- *Compter les composants ne dit rien de la collision.*
  `get_collision_enabled()` rend le reglage du COMPOSANT, toujours
  `QUERY_AND_PHYSICS` ici, et pas la presence de donnees cuites. J'avais
  rapporte « 461 chunks, dont 461 avec collision » : ce chiffre ne prouvait
  rien, et le sol etait troue. **Seule une sonde verticale dit la verite.**
- *`PerformanceService` depend enormement de ce qui est a l'ecran ET de l'etat
  du streaming.* Une lecture prise juste apres avoir teleporte le pion a donne
  **127 FPS et 7,86 ms de GPU** la ou la meme scene stabilisee en donne 21 a 33 :
  les chunks autour de lui etaient en cours de relachement, la scene etait
  presque vide. Protocole minimal : pion immobile, streaming stabilise, et
  plusieurs lectures espacees qui doivent coincider.

### La plaque noire qui clignote : une normale decidee sur UN seul sommet (18 septembre 2026)

Signale en PIE : « il y a une plaque noire au sol qui blink ». C'etait un vrai
defaut, present depuis le premier maillage voxel, et invisible sur le banc parce
qu'on n'y marche pas.

**LA DEMARCHE, parce que mes deux premieres hypotheses etaient fausses et que
c'est la sequence de tests qui a tranche :**

1. *Le sol de fond passerait au-dessus du relief voxel.* Faux : masquer
   `WorldseedGroundProxy` ne change rien a l'image.
2. *Ce serait une ombre.* Faux : `ShowFlag.DynamicShadows 0` retire l'ombre du
   personnage et laisse la plaque intacte.
3. *Le materiau recevrait des canaux vides.* Faux : les chunks portent bien
   `M_WorldseedBiome`, le mode est `BiomeColour`, et les sondes verticales
   montrent que le noir ET le vert sont tous deux des chunks `Worldseed_Voxel`.
4. **`ShowFlag.Lighting 0` : le noir disparait completement.** Donc les couleurs
   de sommet sont bonnes, et le probleme est l'ECLAIRAGE -- des normales
   inversees.

**LA CAUSE.** `WorldseedVoxelChunk` decidait le sens des normales de TOUT le
chunk d'apres le gradient du champ mesure a **un seul sommet**, `MC.Vertices[0]`,
avec un commentaire qui affirmait « la reponse est exacte ». Elle ne l'est pas :
si ce sommet tombe la ou le gradient est presque perpendiculaire a la normale, ou
sur un plafond de galerie, le produit scalaire change de signe et le chunk entier
se retourne. Il devient noir -- eclaire par-derriere -- et il CLIGNOTE, parce
qu'un chunk remaille en marchant ne retombe pas forcement du meme cote.

**LE CORRECTIF : un vote pondere.** On somme les produits scalaires sur un
echantillon de sommets repartis dans le chunk, plafonne a soixante-quatre. Un
sommet dont le gradient est faible ou presque perpendiculaire pese peu, un sommet
franc pese beaucoup -- la ponderation sort gratuitement de la somme. Cout : six
evaluations par echantillon, soit moins de dix pour cent du maillage d'un chunk,
et il ne suit pas la taille du chunk. Mesure apres correction : **1,27 ms par
chunk**, du meme ordre qu'avant.

**CE QUE CET EPISODE APPREND SUR LA METHODE.** Un seul echantillon pour decider
d'une propriete globale est un pari, meme quand il donne le bon resultat la
plupart du temps -- et un commentaire qui annonce l'exactitude ne la cree pas.
Des qu'une decision porte sur un ensemble, la mesurer sur un point unique doit
etre suspect.

**ET LE TEST QUI SEPARE GEOMETRIE, COULEUR ET ECLAIRAGE tient en une ligne :**
`ShowFlag.Lighting 0`. Si le noir disparait, ce sont les normales ou les lumieres ;
s'il reste, ce sont les couleurs de sommet ou le materiau. A faire AVANT de
soupconner quoi que ce soit d'autre -- j'ai perdu deux hypotheses faute de
commencer par la.

### Le sens d'une normale n'est PAS une propriete globale du chunk (18 septembre 2026)

Suite du defaut precedent, et il a fallu trois formes pour y arriver. Toutes
partaient de la meme idee : le gradient du champ croit vers l'air, donc il donne
le dehors. C'est juste. Ce qui etait faux, c'est de croire qu'un seul verdict
vaut pour tout un chunk.

1. **Un seul sommet**, `MC.Vertices[0]`. Une grande plaque noire, qui clignote.
2. **Un vote pondere sur soixante-quatre sommets.** La grande plaque disparait,
   mais il en reste : une majorite n'est pas une preuve, et il suffit qu'une
   partie de la surface d'un chunk soit orientee autrement pour que le vote la
   sacrifie.
3. **Chaque sommet tranche pour lui-meme.** On garde la normale moyennee des
   faces, qui est lisse et continue le long des aretes partagees, et on ne
   corrige que son SENS avec le gradient local.

**COUT MESURE** : 1,27 -> **2,62 ms par chunk**, geometrie identique au triangle
pres (96 729 des deux cotes). C'est le prix de l'exactitude, et il etait bien
place : le defaut rendait le jeu inregardable.

**UN PIEGE DE MESURE QUI A FAILLI M'EGARER.** J'ai voulu diagnostiquer en lisant
`impact_normal` d'un `line_trace`. La zone sombre rendait **-0,96** tracee depuis
la camera et **+0,96** tracee d'en haut, au meme point : **Chaos oriente la
normale FACE AU RAYON**. Cette valeur ne dit donc rien du sens de la geometrie,
et rien du tout des normales de SOMMETS, que la collision ne connait pas.

**ET IL Y AVAIT DEUX PHENOMENES SOMBRES DIFFERENTS, que j'ai confondus** -- ce
qui explique pourquoi la premiere correction semblait incomplete :

- la grande plaque sur laquelle le joueur MARCHE, a une altitude positive :
  c'etaient les normales, corrigees ici ;
- une zone sombre au loin, qui s'est revelee etre de la geometrie voxel a
  **-41 m, sous la mer** : le fond d'une baie vu a travers quarante metres
  d'eau, que l'absorption de l'ocean rend presque noir. **Ce n'est pas un
  defaut.** Le test qui l'a montre : masquer le sol de fond fait apparaitre la
  surface de l'eau avec ses reflets.

**LA LECON DE METHODE** : avant de chercher une cause, s'assurer qu'on regarde
UN seul phenomene. Deux taches noires ne sont pas forcement la meme tache noire,
et corriger la premiere donne alors l'impression de n'avoir rien corrige.

### Des TROUS dans le sol : un germe pose au mauvais endroit (19 septembre 2026)

Signale : « j'ai encore des zones bizarres, un peu comme les taches noires sauf
qu'elle n'est pas noire ». C'etait un vrai defaut, et le plus grave trouve sur
le terrain voxel : **on pouvait tomber au travers du monde**.

**CE QUE L'IMAGE MONTRAIT** : une plaque plate a bords DROITS, avec une marche,
posee au milieu des dunes, plus quelques entailles sombres. Masquer
`WorldseedGroundProxy` a tout explique d'un coup : sous la plaque il y a un
**trou carre**, on voit l'ocean au travers. Le sol de fond ne genait pas, il
BOUCHAIT -- un metre plus bas, d'ou la marche. Il n'a pas de collision, donc les
sondages le traversent et touchent le voxel derriere : **on mesure du sol la ou
on voit un trou**, et inversement.

**MESURE** : 2 colonnes sans aucun sol sur 135 chargees, soit **1,5 %**. Le trou
tombe exactement sur une cellule de 32 m -- le chunk (32, -35) -- et il
**survit a un dechargement complet suivi d'un rechargement**, donc il est
deterministe.

**LA CHAINE DE DIAGNOSTIC, et il a fallu instrumenter pour la remonter.** De
l'exterieur, un chunk jamais considere, un chunk declare vide et un chunk maille
a zero triangle se ressemblent tous les trois. D'ou `DiagnostiquerColonne`, qui
rend pour une colonne : la plage macro, les etages candidats, l'etat de chaque
chunk, le balayage rejoue et la cause enregistree.

    colonne saine   etage 0 : 322 roche / 407 air, 82 germes -> 2470 triangles
    colonne cassee  etage 0 : 324 roche / 405 air, 81 germes -> 0 triangle

**LA CAUSE : LE GERME ETAIT POSE AU COIN DE L'ARETE, PAS SUR LA SURFACE.** Le
balayage grossier teste le signe tous les 4 m et semait au point de balayage.
Or la traversee peut se trouver n'importe ou sur ces 4 m. `GenerateContinuation`
part de la cellule d'UN metre qui contient le germe ; si cette cellule est
pleine de roche, elle n'y trouve aucune traversee et **s'arrete aussitot, sans
un seul triangle**. Cela marchait dans 98,5 % des cas parce qu'avec
quatre-vingts germes il s'en trouve presque toujours un qui tombe juste -- mais
sur une surface tres PLANE, un champ de dunes, aucun ne tombe juste et le chunk
entier disparait.

Correctif : interpoler le point de traversee sur l'arete
(`T = (Iso - V) / (W - V)`) et semer LA. Jusqu'a trois germes par point au lieu
d'un, tous valides. **Apres : 0 trou sur 135 colonnes, l'ancien trou rend 2380
triangles avec le MEME nombre de germes (81), et le cout ne bouge pas
(2,89 ms/chunk contre 2,88).** Meme nombre de germes, resultat oppose : c'est la
signature d'un defaut de PLACEMENT, pas de quantite.

**UN SECOND DEFAUT TROUVE EN CHEMIN, corrige lui aussi.** `UploadChunk` faisait

    if (!Job->bHasSurface || Job->Mesh.IsEmpty()) { State.bEmpty = true; return; }

alors que `bHasSurface` est faux pour TROIS raisons : aucune traversee (une
propriete du monde), travail ANNULE, et maillage sorti vide. Seule la premiere
justifie de retenir « vide » -- et comme un chunk marque vide n'est jamais
repropose (la boucle des candidats saute toute cle presente), les deux autres
devenaient des trous permanents. Les causes sont desormais distinguees dans
`FWorldseedVoxelStats::ECause`, un travail annule n'est plus marque, et une
passe de REPRISE relance les chunks sans maillage, sans travail et non vides.
Sans cette passe on aurait seulement remplace un trou marque par un trou sans
marque.

**PIEGES DE MESURE PAYES SUR CE SEUL DEFAUT :**

- **Ma sonde et le mailleur n'evaluaient pas le meme champ.** Mon balayage de
  diagnostic appelait `Density.At(P)` sans les grottes, le mailleur
  `Density.At(P, &Caves)` avec. Il faut rejouer a l'identique, `CaveNetwork.Query`
  compris, sinon on compare deux mondes.
- **`line_trace_multi` ne depasse pas le premier bloquant** (deja note) et le sol
  de fond n'a pas de collision cuite : impossible de le sonder. Le seul controle
  qui tranche est de le MASQUER et de recapturer.
- **`ShowFlag.Lighting 0`** separe couleur et eclairage, et c'est lui qui a
  montre que le vert d'une grotte n'etait pas une teinte mais un AUTRE maillage.
- **Un pion en `MOVE_FLYING` ne se tient sur rien** : j'ai cru pendant plusieurs
  appels qu'il etait pose sur le sol de fond a 42 m alors que le terrain est a
  13 m. Lire le mode de deplacement avant d'interpreter une altitude.

**LE SOL DE FOND ENTRAIT DANS LES CAVITES. CORRIGE LE JOUR MEME** -- voir « Le
sol de fond sort du RENDU PRINCIPAL » : `SetRenderInMainPass(false)` plus
`SetRenderInDepthPass(true)`, ce qui le retire de l'IMAGE sans le retirer de
l'EAU. Le constat d'alors : Dans la salle sous le gouffre, le masquer laisse de la roche pleine
partout -- il est donc bien dessine a l'interieur. Un decor d'horizon n'a rien a
faire dans un volume ferme.

**PIEGE D'OUTILLAGE, et il m'a coute quatre essais** : un heredoc bash
`<<'PY'` mange la double barre oblique inverse, donc `'\n'` ecrit un vrai saut
de ligne dans le fichier C++ et casse la compilation sur « saut de ligne dans la
constante ». Construire la barre oblique par `chr(92)`.

### Le sol de fond sort du RENDU PRINCIPAL, pas de la passe de profondeur (19 septembre 2026)

Le decor d'horizon traversait les cavites et s'y dessinait par-dessus la roche.
Arbitre par le proprietaire sur mesure, apres A/B a l'image au meme cadrage :

    dans la salle sous le gouffre   70,2 % du cadre change   ecart moyen 35,9/255
    bouche de grotte, vers dehors    4,1 % du cadre change   ecart moyen  3,7/255

**Dix-sept fois plus de degats a l'interieur que de perte a la sortie**, et la
raison est structurelle : depuis que les trous de chunks sont corriges, le
terrain voxel couvre ENTIEREMENT les 250 m du rayon de chargement, donc ce
qu'on voit par une ouverture est presque toujours du vrai terrain. Ma crainte
de « perdre l'horizon a l'entree d'une grotte » etait exageree, et c'est la
mesure qui l'a dit -- pas le raisonnement.

**LE PIEGE QU'IL FALLAIT EVITER, ET IL AURAIT ETE SILENCIEUX.** Le premier
reflexe est `SetActorHiddenInGame(true)`. Or **ce sol de fond porte le
`UWaterTerrainComponent`, et c'est toute sa raison d'etre** : le plugin Water y
lit le relief pour sa texture d'information. Le masquer l'aurait sorti des deux
passes, et l'ocean cesse alors de se dessiner SANS le moindre avertissement --
le piege `[0 .. 0]` deja documente plus haut.

**Le levier exact est dans la source du moteur**, `PrimitiveSceneProxy.h:804` :

    ShouldRenderInDepthPass() = bRenderInMainPass || bRenderInDepthPass

La passe de base est gardee par `ShouldRenderInMainPass()`
(`BasePassRendering.cpp:2048`), la passe de profondeur par la ligne ci-dessus.
**Couper le premier en armant le second retire donc la nappe de l'IMAGE sans la
retirer de l'EAU.** `SetRenderInMainPass(false)` + `SetRenderInDepthPass(true)`.
Verifie : sous terre le drapeau principal tombe a False et celui de profondeur
reste True, en surface les deux reviennent, et la ligne
`eau : ... z [-296 .. 296] m` est inchangee.

**C'EST LA CAMERA QUI DECIDE, PAS LE PION.** En vue a la troisieme personne le
bras place l'oeil jusqu'a quatre metres derriere le personnage : il peut etre
dehors quand le personnage est dedans, et c'est l'oeil qui voit le decor.
`UGameplayStatics::GetPlayerCameraManager(World, 0)->GetCameraLocation()`.

**ET IL FAUT UNE HYSTERESIS.** Un seuil unique fait clignoter le decor : l'oeil
d'une camera a bras oscille en permanence autour de la valeur critique sur un
terrain accidente. On cache sous `GroundProxyHideDepthM` (3 m sous la nappe) et
on ne revient qu'apres avoir regagne `GroundProxyHideHysteresisM` (2 m).

Le minuteur tourne sur les DEUX chemins, voxel comme carte d'altitude : la
nappe traverse les cavites dans les deux cas. Celui du terrain en carte
d'altitude ne demarrait que si `bUseVoxelMesher` etait faux.

### Le filet qui rattrape le joueur ne jouait QU'UNE FOIS (19 septembre 2026)

Signale : « je pense que le joueur tombe ». Mesure : pion a **-4745 m**, en
chute a quarante metres par seconde, et rien ne le ramenait.

**CE QUE CE N'ETAIT PAS**, deux hypotheses eliminees par la mesure avant de
toucher au code :
- *la collision cuite en asynchrone que depasserait un corps rapide* : un
  lacher de **213 m** sur terrain deja charge atterrit proprement a 14,2 m en
  `MOVE_WALKING`. Le mecanisme existe peut-etre, il n'est pas en cause ici ;
- *un plancher manquant a l'endroit ou je l'avais pose* : la colonne y porte
  bien trois surfaces -- sol exterieur 90,2 m, plafond de galerie 85,3,
  **plancher 77,9**. Rejoue a l'identique, le pion glisse une vingtaine de
  secondes puis se stabilise a 82,0 m. **Je n'ai pas reproduit la chute
  initiale**, et il faut le dire : mon propre placement en `MOVE_WALKING` dans
  une galerie en pente reste la cause la plus probable.

**LA VRAIE FAILLE, elle, est structurelle et independante de la cause.**
`HoldOrReleasePlayer` commencait par

    if (!bHoldPlayer || bPlayerReleased || !bWorldReady) { return; }

`bPlayerReleased` se verrouille a la mise en place initiale : **apres elle, il
n'y a plus AUCUN filet**. Or le terrain n'est maille que de la surface a
`bandeM` en dessous -- cent metres. Plus bas, aucun chunk n'existe et il ne
peut RIEN y avoir : un pion qui passe sous la bande tombe indefiniment.

**LE CRITERE DU FILET VIENT DE LA BANDE ELLE-MEME, il n'est pas arbitraire.**
Sous `bandeM + marge`, le pion n'est pas en train de tomber dans un trou, il
est HORS DU MONDE -- aucune geometrie ne peut l'y attendre. On rearme alors la
mise en place initiale plutot que de reimplementer la remontee : elle sait deja
choisir du sol plat, tenir le pion EN VOL le temps du maillage, et ne le
relacher qu'une fois le chunk **solide**. Dupliquer ce travail aurait fait
diverger les deux moities.

**EPREUVE** : pion jete a -800 m en chute libre. Journal ->
`joueur a 702 m SOUS la bande de terrain -- hors du monde, on le remonte`,
puis `joueur tenu a z 1312 cm`, puis `joueur rendu a la gravite, chunk -3,-3,0
solide`. Etat final : (-80, -80, 3,9) m en `MOVE_WALKING`.

**PIEGE UHT AU PASSAGE** : `BlueprintReadWrite should not be used on private
members`. Une `UPROPERTY` posee pres d'un membre prive herite de sa section ;
la placer aupres des autres reglages editables, pas aupres du membre dont elle
parle.

### Le sol de fond : « sous la nappe » n'est PAS « sous terre » (19 septembre 2026)

**CORRECTION D'UN DEFAUT LIVRE UNE HEURE PLUS TOT**, et il se voyait en plein
jour. Le premier critere comparait l'altitude de l'oeil a celle de la NAPPE A
SON APLOMB : sous la nappe, on cachait. Or **sur une pente, la camera est
derriere ET plus bas que le personnage, donc son aplomb tombe souvent sur du
terrain PLUS HAUT qu'elle**. Mesure : camera en plein air a 83,5 m, surface a
son aplomb 97,7 m -- « 14,2 m sous la nappe », decor masque, horizon disparu, et
le monde reduit au disque de chunks charges au milieu de l'ocean.

**La vraie question n'est pas une altitude, c'est une OCCULTATION.** Un decor
d'horizon ne gene que s'il s'interpose, et il ne peut s'interposer que si l'on
est sous un plafond. Un **sondage vertical** y repond exactement, sur la
GEOMETRIE REELLE plutot que sur une approximation du relief -- et le sol de fond
n'ayant pas de collision, il ne peut pas se sonder lui-meme. Plus d'hysteresis
sur une hauteur : un anti-rebond a deux mesures concordantes suffit, parce que
la reponse est binaire.

**PIEGE DE PROTOCOLE, PAYE DEUX FOIS DANS LA MEME HEURE** : j'ai pose le pion a
une altitude FIXE pres d'un relief accidente, sans sonder le sol. A 82 m sur un
terrain qui en fait 99, on se retrouve DANS la colline -- et l'image qu'on
obtient alors (lambeaux de terrain sur fond d'ocean, decor masque) ressemble
trait pour trait a un defaut de generation. **Toujours sonder avant de poser**,
et lire le mode de deplacement avant d'interpreter une altitude.

### La distance PERPENDICULAIRE a une paroi (19 septembre 2026)

Seul acquis durable d'une tentative d'arches par bruit en nappes, par ailleurs
abandonnee et remplacee deux fois le meme jour (voir « Tailler les lames » puis
« Percer les caps a leur base »).

**LE CHAMP MESURE UNE DISTANCE VERTICALE A LA SURFACE, et sur une falaise cette
distance est enorme des le premier metre dans la roche** -- la surface a
l'aplomb se trouve loin au-dessus. Toute porte posee sur elle ne mord donc
JAMAIS sur une paroi. On divise par la norme du gradient,
`sqrt(1 + |grad H|^2)`, ce qui rend la distance vraie a la paroi au premier
ordre. **A reprendre pour toute forme de PAROI.**

### Elargir la distance de vue : ce n'est pas le rendu qui bloque (19 septembre 2026)

Question posee apres quatre tournees photo ou aucune grande forme -- mesa,
gorge, marge de plateau -- ne se lisait : combien couterait un rayon de
chargement plus grand ? Je l'avais estime a « neuf a seize fois les chunks »
sans le verifier. Mesure faite, l'estimation etait juste sur les chunks et
COMPLETEMENT A COTE sur ce qui bloque.

**D'OU VIENT LE BANC.** `PerformanceService` passe par l'outillage externe, qui
tombe des qu'on relance l'editeur plusieurs fois -- donc a chaque compilation.
Et un commandlet ne rend rien : il n'y a pas de rendu. D'ou `WorldseedBanc`, un
sous-systeme qui mesure DANS le jeu, sur ses propres `DeltaTime`. Il est au
passage a l'abri du piege le plus couteux du depot -- l'editeur non focalise
bride son rendu et l'outillage attribue le plafond au GPU -- puisqu'il n'y a
pas d'editeur a brider.

    UnrealEditor.exe Worldseed.uproject /Game/Worldseed/Maps/L_Worldseed_Proc
      -game -WorldseedBanc -WorldseedRayon=600 -WorldseedQuitter
      -windowed -resx=1600 -resy=900

**MESURE, graine 20260909, 64 x 32 km :**

| rayon | chunks | stabilise | trame | images/s | memoire |
|---|---|---|---|---|---|
| **250 m** | **814** | **oui, 31 s** | **7,46 ms** | **134** | 4,53 Go |
| 400 m | 1663 | non, plafond 180 s | 14,80 ms | 68 | 4,66 Go |
| 600 m | 1663 | non, plafond 180 s | 17,77 ms | 56 | 4,71 Go |
| 800 m | 1663 | non, plafond 180 s | 17,72 ms | 56 | 4,67 Go |

**LES TROIS DERNIERS DONNENT EXACTEMENT 1663 CHUNKS, et c'est LA reponse.** Ce
n'est pas le rayon qui les limite, c'est le DEBIT du streaming : en 180 s le
mailleur en batit 1663, quel que soit le rayon demande. Au-dela de 250 m on ne
mesure plus un monde charge, on mesure une file d'attente.

**CE N'EST DONC PAS LE COUT D'IMAGE QUI BLOQUE, C'EST LE TEMPS DE
REMPLISSAGE**, et c'est bien plus redhibitoire. A 9,2 chunks par seconde, et le
nombre requis croissant en R carre :

    rayon    chunks requis    remplissage
    250 m       814           31 s        (mesure)
    400 m     ~2 100          ~3,8 min
    600 m     ~4 700          ~8,5 min
    800 m     ~8 300          ~15 min

**Un joueur a 6 km/h franchit 600 m en SIX MINUTES.** Il distancerait le
mailleur en permanence, et definitivement. Le cout d'image suit d'ailleurs :
extrapole depuis le seul point propre -- 814 chunks pour 7,46 ms -- 4 700
chunks donneraient environ 43 ms, soit 23 images par seconde.

**VERDICT : elargir la distance de vue est impossible avec un mailleur a
RESOLUTION UNIFORME.** Les deux barrieres tombent ensemble, et la seconde est la
plus dure.

**ET CELA CHIFFRE ENFIN LE CHANTIER DES ANNEAUX**, ouvert depuis longtemps sans
justification numerique. Avec des chunks quatre fois plus larges au-dela de
250 m, le nombre cesse de croitre en R carre : un rayon de 800 m reviendrait a
l'ordre de grandeur du 250 m actuel, en remplissage comme en image. C'est la
condition pour qu'une mesa, une gorge ou une marge de plateau soient VUES.

**UNE FAUTE DE MESURE PAYEE EN CHEMIN, ET C'EST TOUJOURS LA MEME SIGNATURE.** La
premiere version du banc chauffait un nombre FIXE de secondes, et le releve a
rendu **exactement 552 chunks a 250 m comme a 400** -- alors qu'a 400 m il en
faut plus de deux mille. On mesurait un transitoire identique des deux cotes, et
l'A/B ne comparait rien. **Deux mesures identiques au chiffre pres pour deux
reglages differents : ce depot a maintenant rencontre ce signe quatre fois.** Le
banc attend desormais la STABILISATION reelle -- compte de chunks fige et aucun
travail en vol -- et DIT quand il mesure un transitoire au lieu de le taire.

**LIMITE DE CE RELEVE, dite franchement** : seul le point a 250 m est vraiment
stabilise. Les trois autres sont des instantanes a 180 s, et les chiffres de
trame qu'ils portent incluent le maillage en cours, donc ils SURESTIMENT le
cout d'un monde pose. L'extrapolation ci-dessus part du point propre, pas
d'eux.

### Le rayon de vue : j'avais conclu l'inverse, et mon banc etait faux (19 septembre 2026)

**J'AVAIS ECRIT : « elargir la distance de vue est impossible avec un mailleur a
resolution uniforme », le debit de streaming etant le mur, et il fallait les
anneaux de resolution. C'ETAIT FAUX.** A 800 m de rayon le monde se remplit en
soixante-deux secondes, tient 147 images par seconde avec dix millions de
triangles, et ne fait AUCUN a-coup. Le chantier des anneaux n'est pas necessaire
pour la distance de vue.

**LA CAUSE ETAIT UN DEFAUT DE MON PROPRE BANC.** L'option `-WorldseedRayon=`
ne forcait que `LoadRadiusM`. `UnloadRadiusM` restait fige a 350 m : au-dela,
les chunks etaient batis puis DETRUITS aussitot, et le monde tournait en boucle
sur le meme millier. Toutes les mesures a 400, 600 et 800 m portaient donc sur
une configuration cassee.

**LE SIGNE ETAIT LA, ET C'EST LA CINQUIEME FOIS DANS CE DEPOT.** Les trois
rayons rendaient EXACTEMENT 1679 chunks et 1 819 660 triangles. Un chiffre
identique au chiffre pres pour trois reglages differents n'est jamais un hasard
-- c'est un plafond cache. Le depot l'avait deja rencontre sur l'A/B des
grottes, sur le seuil des diaclases, sur le pont des arches et sur la premiere
version de ce banc. **Je l'ai vu, je l'ai meme ECRIT dans le banc, et je ne l'ai
pas applique a mon propre harnais.**

**MESURE, banc repare, machine au repos, monde 4096x2048 :**

| rayon | chunks | remplissage | trame | p95 | pire | memoire |
|---|---|---|---|---|---|---|
| 250 m | 809 | 9 s | 6,42 ms | 7,50 | 8,93 | 6,50 Go |
| 400 m | 2 167 | 18 s | 6,38 ms | 6,97 | 8,23 | 6,94 Go |
| 600 m | 5 085 | 36 s | 6,61 ms | 7,27 | 9,15 | 7,63 Go |
| 800 m | 9 242 | 62 s | 6,78 ms | 7,36 | 8,31 | 8,61 Go |

**LE COUT EST LA MEMOIRE, PAS LES IMAGES.** Quadrupler le rayon coute 0,36 ms
de trame et 2,1 Go. La pire trame ne bouge pas : il n'y a pas d'a-coup.
**Retenu : 600 m**, ou une mesa entiere tient dans la vue en voxel.

### Le debit de streaming etait un REGLAGE, pas une limite (19 septembre 2026)

Verrou 3 des quatre. Deux notes du depot etaient perimees, et il a fallu les
verifier plutot que les croire :

- **`bUseAsyncCooking` est a `true` depuis longtemps.** La note qui affirme
  qu'il est « a false partout » ne vaut plus.
- **`RealtimeMeshComponent` n'est pas installe** -- seul VibeUE est dans
  `Plugins/`. L'arbitrage A2 le prevoyait ; ce serait une dependance a ajouter,
  pas un reglage. Il n'a PAS ete utilise.

**CE QUI BRIDAIT REELLEMENT.** `UploadsPerPass` valait 6, et `UpdateChunks`
tourne toutes les `UpdatePeriod` : six televersements toutes les 0,2 s
plafonnent le remplissage a TRENTE chunks par seconde, par construction. Et
c'est exactement le vingt-six par seconde observe a 250 m -- le plafond etait
atteint.

**IL AVAIT ETE POSE QUAND LE TELEVERSEMENT ETAIT SUPPOSE CHER. IL NE L'EST
PAS**, et l'instrumentation l'a tranche : **0,21 ms par chunk, 1,17 au pire,
0,2 seconde CUMULEE pour neuf cents chunks**. Le maillage est deja sur le pool
de fils (6,5 ms par chunk, vingt-quatre en parallele sur trente-deux coeurs) et
la cuisson est asynchrone. Il ne restait donc rien de cher sur le fil de jeu.

Porte a 16 televersements toutes les 0,1 s : le remplissage a 250 m passe de
**31 a 9 secondes**, sans que la trame bouge ni que la pire trame se degrade.

**LA LECON : avant de remplacer un composant de rendu, mesurer ce qui coute.**
Le verrou 3 s'annoncait comme un chantier -- changer le composant de tout le
terrain -- et il s'est resolu en deux constantes, parce que le cout suppose
n'etait pas le cout reel.

### Le verrou 4 n'est peut-etre pas necessaire (19 septembre 2026)

Les anneaux de resolution decroissante etaient justifies par une seule chose :
un rayon de vue plus grand serait autrement inabordable. La mesure ci-dessus dit
le contraire -- 800 m tient a 147 images par seconde sans anneaux.

**CE POUR QUOI ILS RESTERAIENT UTILES**, et il faut le dire pour ne pas fermer
la porte : la MEMOIRE, qui est desormais le seul cout qui monte (2,1 Go pour
passer de 250 a 800 m), et le nombre de COMPOSANTS -- un par chunk, donc neuf
mille deux cents a 800 m. Aucun des deux n'est un probleme sur cette machine ;
les deux le deviendraient sur une machine modeste, ou si le rayon devait encore
doubler.

**Ce chantier est donc REPORTE, pas abandonne, et pour une raison mesuree et non
par manque de temps.**

### Transvoxel : la licence est levee, et le mailleur du moteur deborde (19 septembre 2026)

Verrou 4, premiere moitie. Le plan du chantier posait une condition explicite --
« a verifier avant d'ecrire une ligne : la licence des tables de Lengyel ». Elle
est levee : `github.com/EricLengyel/Transvoxel` est en **MIT**, la notice dit
`Copyright (c) 2009 Eric Lengyel`, et `transvoxel.org` declare l'algorithme
**libre de tout brevet**. Le repli sur des colliers a resolution uniforme, prevu
au cas ou, n'a pas lieu d'etre.

**UN OBSTACLE QUE L'ARBITRAGE D'ORIGINE NE CONNAISSAIT PAS.** Le projet maille
avec `FMarchingCubes` de GeometryCore, qui fait du marching cubes STANDARD.
Transvoxel n'est pas un jeu de tables qu'on lui brancherait : il ajoute une
seconde famille de cellules, les cellules de TRANSITION. Et les deux familles ne
se separent pas -- une cellule de transition attend que les regulieres voisines
lui aient laisse la place, leurs sommets de bord etant retractes d'un demi-voxel.
Mailler les unes avec le moteur et les autres a la main donnerait deux maillages
qui ne se rejoignent pas, c'est-a-dire exactement la fissure qu'on cherche a
supprimer. D'ou un mailleur COMPLET, `WorldseedTransvoxel`.

**UNE TABLE NE SE RECOPIE PAS, ELLE SE TRANSFORME PAR SCRIPT.** Une table de
correspondance fausse produit du maillage silencieusement faux, et aucune
relecture a l'oeil ne trouverait le chiffre change parmi **6 486**. Les sept
tables sont donc obtenues par transformation mecanique du fichier publie : seules
les deux definitions de structure passent dans l'en-tete, et les tables recoivent
`extern` -- sans quoi `const` a portee de namespace a une liaison **interne** et
l'edition de liens echoue sur des symboles pourtant bien definis. Le controle est
rejouable et tient en une ligne (`ThirdParty/Transvoxel/PROVENANCE.md`) : le flux
des litteraux hexadecimaux doit etre identique, aux deux `0x0F` des methodes
pres.

**UBT COMPILE TOUT `.cpp` SOUS `Source/`.** La copie de reference y avait d'abord
ete posee : elle se compilait a chaque build, pour rien, et le depot portait deux
copies des tables dont une morte. Elle vit maintenant sous `ThirdParty/` a la
racine, avec sa licence. **Une donnee tierce non compilee n'a rien a faire sous
`Source/`.**

**LE MAILLEUR DU MOTEUR DEBORDE D'UN VOXEL, ET CE N'ETAIT PAS DOCUMENTE.**
Mesure : `FMarchingCubes` sur une boite de 32 m a un metre de voxel maille
**33 cellules par axe**, donc jusqu'a `Max + 1`. **Chaque chunk du terrain
empiete donc d'un metre sur ses voisins**, et environ **neuf pour cent des
triangles du terrain sont de la geometrie dessinee deux fois.** Ce n'est pas un
defaut introduit par ce chantier, c'est l'etat en vigueur depuis le premier
maillage voxel ; il disparaitra a la bascule.

**LA LECON DE METHODE, ET C'EST LA MEME QUE CELLE DU ROUTAGE DES GALERIES.** La
sonde annoncait **9,2 % d'ecart d'aire** entre les deux mailleurs. Un tel ecart a
**deux explications exactement opposees** -- l'un RATE de la surface, l'autre en
maille EN TROP -- qui donnent le meme pourcentage et n'appellent pas du tout la
meme reponse. Corriger le mien alors que c'etait l'autre qui debordait aurait ete
le « quatre corrections qui ne bougent pas la mesure » a l'identique. La bonne
demarche est de mesurer la grandeur qui SEPARE les deux cas : le debordement hors
de la boite, 1,00 m pour le moteur et 0,00 pour le mien.

Puis un TEMOIN qui le prouve : le mailleur maison lance sur la boite **elargie
d'un voxel**, c'est-a-dire sur l'emprise que le moteur couvre reellement.

| mailleur | triangles | aire m2 | \|dens\| moy | bords hors paroi | ms |
|---|---|---|---|---|---|
| moteur | 61 276 | 20 532,9 | 0,2753 | **1 892** | 257 |
| transvoxel | 55 694 | 18 640,3 | 0,2759 | **0** | 238 |
| transvoxel, meme emprise | 61 284 | **20 531,9** | -- | -- | 265 |

**0,005 % d'ecart d'aire**, et 8 triangles sur 61 000. Les deux mailleurs
decrivent la meme surface ; les 9,2 % etaient entierement le debordement. Cout :
**+3 %** sur la meme emprise -- comparer les deux premieres lignes serait
malhonnete, elles ne maillent pas le meme volume.

**DEUX CONTROLES QUI NE COMPARENT RIEN ET JUGENT CHAQUE MAILLEUR SEUL**, ce qui
vaut mieux qu'un A/B ou l'on suppose que l'un des deux a raison :
- **|densite| aux sommets.** Un sommet est cense etre POSE sur l'isovaleur zero.
  0,2759 contre 0,2753, meme maximum a 2,2955 au dix-millieme.
- **Les aretes n'appartenant qu'a UN triangle et qui ne sont pas sur la paroi de
  la boite**, c'est-a-dire des trous : maison **0**, moteur **1 892** -- toutes
  au bord de son debordement. Zero arete non manifold des deux cotes. **C'est ce
  controle qui verra les fissures quand les cellules de transition arriveront, et
  il est pose MAINTENANT :** une mesure installee apres coup n'a pas de temoin
  d'avant le defaut qu'elle doit voir.

**L'ENROULEMENT SE MESURE, IL NE SE DEDUIT PAS.** L'ordre des sommets decide de
la face avant ; se tromper ne casse rien dans la geometrie et rend le terrain
invisible de l'exterieur, ou noir -- le depot a deja paye cette lecon deux fois
sur le mailleur du moteur. Deux conventions se superposent ici et se compensent
peut-etre : Lengyel travaille en repere DIRECT avec le solide en negatif, Unreal
en repere INDIRECT. Raisonner sur leur produit a une chance sur deux d'etre
juste. La sonde confronte donc l'enroulement geometrique aux normales issues du
**gradient**, qui ne doivent rien a aucune convention : premiere mesure **0,1 %**
de faces a l'endroit, donc inversion ; deuxieme mesure **99,9 %**. La reponse
d'une telle mesure est binaire, et elle l'a ete.

**LES SOMMETS SONT SOUDES PAR L'ARETE DE GRILLE, ET NON PAR LES TABLES DE
REUTILISATION.** Lengyel resout le meme probleme par ses figures 3.8 et 4.17, qui
disent de quelle cellule deja maillee reprendre un sommet. C'est plus rapide, et
cela exige un cache indexe par cellule dans un balayage **ordonne** -- ce qu'une
propagation depuis des germes ne fait justement pas. Or l'identite d'un sommet
est de toute facon celle de son arete. On indexe donc par l'arete, ce qui donne
le meme maillage soude sans imposer d'ordre de parcours. **L'arete doit etre
orientee du coin bas vers le coin haut**, sans quoi deux cellules voisines lui
donnent deux cles differentes, donc deux sommets, donc une fissure invisible a la
lecture du code.

**CE QUI EST FAIT, ET CE QUI NE L'EST PAS.** Ce chantier ne pose que les cellules
REGULIERES, et le code le DIT : un masque de transition non nul journalise un
avertissement au lieu d'etre traite en silence comme « pas de transition ». Le
masque n'est jamais arme tant que les anneaux n'existent pas. `voxel.transvoxel`
vaut 0 : rien ne change a l'ecran, et l'A/B se fait sans recompiler.

**RESTE A FAIRE, dans l'ordre :**
1. les cellules de TRANSITION (tables `transitionCellClass` / `transitionCellData`
   / `transitionCornerData` / `transitionVertexData`, deja en place et non lues ;
   **211 des 512 cas portent le bit haut, qui inverse l'enroulement**) ;
2. la retraction d'un demi-voxel des sommets de bord des cellules regulieres qui
   bordent une face de transition -- sans elle, les deux familles ne se
   rejoignent pas ;
3. les anneaux dans le diffuseur : cle de chunk portant un NIVEAU, rayons par
   anneau, et le masque des faces tournees vers un voisin plus grossier ;
4. la mesure au banc -- le compte de chunks a 1 200 m doit tomber bien sous les
   **9 242** que coutent 800 m aujourd'hui -- puis des photos des jointures.

### Les cellules de transition : la fissure est fermee (20 septembre 2026)

Seconde moitie du verrou 4. C'est la partie que `FMarchingCubes` ne sait pas
produire, et c'est donc pour elle que le mailleur maison existe.

**LA MESURE QUI TRANCHE, ET SON TEMOIN.** Deux chunks cote a cote -- le grossier
a 2 m de voxel, le fin a 1 m -- cousus par la POSITION comme ils le seront en
jeu, puisque ce sont deux composants distincts et qu'une fissure y est une
discontinuite de position, jamais d'indice.

| etat | triangles | sommets du plan | partages | aretes ouvertes |
|---|---|---|---|---|
| temoin, sans cellule | 2 743 | 60 | **0** | **58 (100,0 %)** |
| avec transition | 2 801 | 41 | **41** | **0 (0,0 %)** |

**L'ARETE OUVERTE EST LA DEFINITION D'UN TROU, PAS UN INDICE** : une arete qui
n'appartient qu'a UN triangle alors qu'elle est a l'interieur de la surface. Et
le temoin MONTRE la fissure qu'on supprime -- 100 % -- ce qui etait la condition
pour que la mesure prouve quelque chose. Elle avait ete posee AVANT le defaut
qu'elle doit voir, ce qui est la seule facon d'avoir un point de comparaison.

**TROIS FAITS ETABLIS, AUCUN DEVINABLE :**

- **la cellule de transition vit dans le bloc GROSSIER**, le long de sa
  frontiere avec le fin (section 4.3). J'avais ecrit l'inverse dans un en-tete,
  et c'est faux : c'est le bloc grossier qui a trop peu d'echantillons -- neuf
  valeurs fines arrivent sur une face qui n'en porte que quatre -- donc c'est a
  lui de ceder la place. Un mailleur ecrit dans l'autre sens aurait raccorde du
  cote ou il n'y a rien a raccorder ;
- **les quatre echantillons demi-resolution ne sont pas des inconnues** : ils
  VALENT les coins de la face pleine -- 9 = 0, A = 2, B = 6, C = 8 (section
  4.5). C'est ce qui ramene treize echantillons a neuf bits, et **c'est surtout
  ce qui SOUDE** la face demi-resolution aux cellules regulieres : meme arete de
  la grille grossiere, meme appel, donc le meme sommet. La couture est acquise
  par construction, pas esperee ;
- **la topologie des aretes**, interrogee dans les tables plutot que lue dans la
  figure 4.18 (`perl Tools/Transvoxel/aretes.pl`) : **douze** aretes sur la face
  pleine resolution, **quatre** sur la demi, et **AUCUNE laterale**. Les douze
  premieres sont exactement l'adjacence d'une grille 3x3 en disposition ligne
  par ligne, ce qui confirme au passage la numerotation des echantillons. Les
  deux nappes ne partagent donc aucun sommet : elles sont cousues par des
  triangles, et le mailleur tient DEUX espaces de cles de soudure.

**LA RETRACTION, ET UN ECART ASSUME A LA METHODE D'ORIGINE.** Les cellules
regulieres de bord se retractent pour laisser la place a la dalle. Lengyel garde
pour cela DEUX positions par sommet -- primaire et secondaire -- et laisse un
programme de sommet trancher selon le niveau des voisins. **Notre diffuseur
REMAILLE un chunk quand son voisinage change de niveau**, donc on cuit
directement la bonne position. C'est plus simple, et cela coute un remaillage
que le chunk fait de toute facon.

**LA LECON DE METRIQUE, ET C'EST LA MEME QUE CELLE DU ROUTAGE DES GALERIES.**
Premiere mesure d'enroulement : **94,7 %**, contre 99,9 % pour les cellules
regulieres seules. Une degradation vague, qu'on met volontiers sur le compte
d'un detail geometrique -- et qu'aucune correction n'aurait deplacee
franchement. J'ai separe les deux populations au lieu de deduire par
soustraction. La reponse est alors devenue binaire :

    regulieres  99,6 %      transition  0,0 %

**TOUTES a l'envers, sans exception** : ce n'etait donc pas le bit d'inversion
des tables qui etait mal lu, c'etait la convention de DEPART de la famille.
Probablement le sens de la base tangentielle -- j'ai pose `U x V = normale
sortante`, Lengyel regarde vraisemblablement la face depuis le bloc fin -- mais
c'est une explication, pas la preuve : ce qui tranche est le zero pour cent.
Apres correction, transition **100,0 %**.

**LIRE LA SOURCE, PAS UN RESUME DE LA SOURCE.** Un resumeur automatique lance
sur l'article de Lengyel affirmait que le bit haut de `transitionCellClass`
« indique si la cellule est reguliere ». C'est FAUX et verifiable -- il inverse
l'enroulement, la source des tables et la these le disent toutes deux en toutes
lettres. **Une source qui se trompe sur un point verifiable ne sert pas sur les
points invariables.** La these a donc ete extraite du PDF a la main : il n'y a
ni poppler ni Python sur cette machine, d'ou un extracteur ecrit en Perl
(`Compress::Raw::Zlib` est livre avec Perl ; les flux d'un PDF sont en Flate, et
les operateurs `Tj`/`TJ` donnent le texte). L'extraction est LACUNAIRE -- la
mise en page casse l'ordre de lecture et les figures ne sortent pas -- mais elle
est de premiere main.

**ATTENTION A UN PIEGE D'EXTRACTION** : une phrase tronquee se reconstruit
volontiers dans le sens qui arrange. Celle qui parle du code de cas m'aurait
conduit a l'ordre sequentiel des bits, qui est faux a 37,5 %. C'est la
derivation depuis les tables qui a tranche, pas la lecture.

**NON-REGRESSION du chemin regulier**, verifiee au chiffre pres apres coup :
aire 20 531,9 m2 contre 20 532,9 pour le moteur sur la meme emprise, soit
0,005 %, et 99,9 % d'enroulement. Le masque n'etant arme nulle part, rien ne
change a l'ecran.

**RESTE A FAIRE :**
1. **les anneaux dans le diffuseur** : cle de chunk portant un NIVEAU, rayons
   par anneau, et le calcul du masque depuis le niveau des six voisins ;
2. **`LargeurTransition` doit passer dans `world_rules.json`**, ou vivent les
   seuils. Elle est pour l'instant un parametre a valeur par defaut (0,5
   cellule) : l'y mettre aujourd'hui forcerait une regeneration du monde pour un
   reglage que personne ne lit encore ;
3. la mesure au banc, puis **des photos des jointures** -- une forme qui n'a pas
   ete vue n'est pas validee, et c'est une regle du depot.

### Les anneaux de resolution : 2400 m de vue pour un quart des chunks (20 septembre 2026)

Fin du verrou 4. Le mailleur savait deja raccorder deux resolutions sans
fissure ; il manquait le diffuseur qui les DEMANDE.

**LES GRILLES DES NIVEAUX SONT EMBOITEES, ET C'EST CE QUI REND L'AFFAIRE SURE.**
Un chunk de niveau L fait `ChunkSideM * 2^L` de cote, porte toujours le MEME
nombre de cellules -- seul le voxel double -- et se decoupe exactement en huit
chunks de niveau L-1 alignes sur la meme origine. La diffusion descend alors
depuis le niveau le plus grossier : un noeud est soit maille, soit remplace par
ses huit enfants, **jamais les deux**. Ni recouvrement, ni trou, quels que
soient les rayons.

**LE PIEGE QUE CETTE DESCENTE EVITE.** Le reflexe est de decider le niveau d'un
chunk par sa distance a l'origine. **C'est faux, et de deux facons** : deux
chunks de niveaux differents n'ont pas le meme centre, donc un critere pose sur
la seule distance peut en emettre DEUX pour le meme volume -- geometrie dessinee
en double -- ou AUCUN -- trou. La descente recursive n'a pas ce defaut par
construction, et elle ne coute rien de plus.

**MESURE, graine 20260909, monde 4096x2048, machine au repos :**

| configuration | rayon | chunks | par niveau | remplissage | trame | p95 | memoire |
|---|---|---|---|---|---|---|---|
| uniforme (temoin) | 600 m | 5 085 | -- | 36 s | 6,43 | 7,06 | 7,44 Go |
| uniforme (documente) | 800 m | 9 242 | -- | 62 s | 6,78 | 7,36 | 8,61 Go |
| un anneau | 600 m | 1 439 | 794/645 | 13 s | 6,22 | 6,84 | 6,89 Go |
| deux anneaux | 1200 m | 2 168 | 1170/567/431 | 18 s | 6,29 | 6,81 | 6,88 Go |
| **trois anneaux** | **2400 m** | **2 436** | 1170/567/427/272 | 20 s | 6,36 | 6,93 | 7,27 Go |

**Quatre fois la distance de vue, un quart des chunks, moins de memoire**, et la
trame ne bouge pas. Le compte cesse de croitre en R au carre, ce que le chantier
promettait depuis longtemps sans l'avoir jamais chiffre.

**LA PROPRIETE DE SURETE SE VERIFIE, ELLE NE SE SUPPOSE PAS.** A `NiveauMax = 0`
la diffusion rend EXACTEMENT les 5 085 chunks deja mesures a 600 m, sur le meme
binaire -- les anneaux se pilotent par `-WorldseedNiveaux=` et `-WorldseedAnneau0=`,
parce qu'un A/B dont les deux moities demandent une recompilation n'en est pas
un. Ils arrivent donc ETEINTS et ne peuvent rien casser tant qu'on ne les arme
pas.

**LE MASQUE SE RECALCULE, PARCE QU'IL BOUGE.** Les faces de transition d'un
chunk dependent du niveau de ses VOISINS, donc de la position du joueur : un
chunk garde son niveau et voit son masque changer. On le retient dans l'etat et
l'on remaille quand il differe. Sans cela il rouvrirait exactement la fissure que
la cellule de transition ferme, **et rien ne le signalerait**.

**UNE PREMIERE EXPLICATION FAUSSE, ET LA REGLE QUI A SERVI.** A 2400 m le p95
montait a 14 ms pour une moyenne de 6,6 -- un pic periodique, a la cadence exacte
de la passe. J'ai accuse mon balayage des masques et je l'ai borne a 512 feuilles
par passe : **le chiffre n'a pas bouge d'un dixieme** (13,81 contre 13,84). Le
depot interdit d'enchainer une deuxieme hypothese a l'aveugle, et c'est ce qui a
evite de regler quatre fois le mauvais bouton.

Le controle qui a innocente le niveau 3 au passage : deux anneaux a 2400 m
(3 934 feuilles, deux niveaux) donnent le MEME pic que trois anneaux (2 436
feuilles, trois niveaux). Ce n'est donc ni le nombre de chunks, ni la profondeur.

**ALORS J'AI CHRONOMETRE LA PASSE AU LIEU DE LA SUPPOSER** : 13,65 ms a 2400 m
contre 6,17 a 1200. Le pic etait bien la, et la cause est `SurfaceRangeM`, qui
echantillonne au pas de la grille -- **un noeud de 256 m demande 289 lectures**,
et la descente la rappelle pour le noeud que la boucle de tete vient
d'interroger. Or **le relief 2D ne change pas en cours de partie** : ces bornes
sont une constante du monde, pas une grandeur a recalculer dix fois par seconde.
Mises en cache par colonne et par niveau :

    passe de diffusion  13,65 -> 1,83 ms     p95  13,77 -> 6,93 ms
                                             pire 16,71 -> 8,09 ms

Le balayage borne est garde : il ne reglait pas ce pic-la, mais il reste juste --
il etale un cout qui suivrait autrement le produit feuilles x niveaux.

**ET LES JOINTURES ONT ETE REGARDEES.** Tournee photo avec deux anneaux : terrain
continu jusqu'a l'horizon, aucune fissure, aucun ciel au travers, y compris sur
une falaise cotiere ou une jointure se verrait le plus.

**UN DEFAUT PREEXISTANT VU AU PASSAGE, ET LE TEMOIN QUI L'ATTRIBUE.** Les vues
rapprochees montrent des TROUS sombres dans le sol et un aspect cotele tres
marque. La meme tournee **anneaux eteints** montre les memes trous aux memes
endroits : ce n'est donc pas les anneaux. Une partie sont des ouvertures de
cavites, qui sont voulues ; le reste est a reprendre a part. **Sans le temoin
j'aurais impute a ce chantier un defaut qui lui est anterieur.**

**RESTE A FAIRE :**
1. **fixer les rayons**, arbitrage que le proprietaire a voulu prendre APRES
   mesure -- le tableau ci-dessus est fait pour cela ;
2. `NiveauMax`, `RayonAnneau0M` et `LargeurTransition` devront passer dans
   `world_rules.json`, ou vivent les seuils, une fois les valeurs arretees ;
3. le rayon de dechargement suit le rayon de chargement par une hysteresis
   fixe : a trois anneaux il monte a 3360 m, ce qui n'a pas ete mesure a part.

### Le terrain n'etait pas dechiquete, il etait INVISIBLE (20 septembre 2026)

Signale comme « des trous dans le terrain ». C'etait un vrai defaut, et ni le
diagnostic ni la mesure qui l'avaient produit n'etaient bons.

**LA CAUSE : L'ENROULEMENT DU MAILLEUR TRANSVOXEL.** Chaque triangle etait une
face arriere, donc eliminee. Le joueur voyait au travers de la surface proche
jusqu'au **dessous** de la surface lointaine -- des nappes qui s'arquent
au-dessus de la tete, des lambeaux dans le ciel, des cones a la ligne d'eau. Ce
n'etaient pas des trous : c'etait du terrain complet, rendu a l'envers.

**LA MESURE QUI AVAIT POSE LA MAUVAISE VALEUR ETAIT AUTO-REFERENTIELLE, ET
C'EST LA LECON.** `ProbeTransvoxel` comparait la normale geometrique d'un
triangle aux normales de **ses propres sommets** -- lesquelles sont recalees sur
le gradient quelques lignes plus haut. Elle ne pouvait donc que confirmer « mes
triangles s'accordent avec mes normales », et ne disait **rien** de la
convention de face avant d'Unreal. Elle a rendu 0,1 %, on a conclu a une
inversion, et la correction qui l'a portee a 99,9 % ETAIT la regression.

**L'ARBITRE JUSTE EST UN TIERS, ET IL DOIT SAVOIR CLASSER LE CAS CONNU.**
`ProbeVoisins` confronte les DEUX mailleurs au gradient du champ, qui
n'appartient a aucun des deux :

    mailleur du moteur    0,3 % et 0,2 % de faces accordees au gradient
    mailleur maison      99,8 % et 99,8 %      (avant correction)

Et c'est le mailleur du MOTEUR qui s'affiche correctement. **Une mesure qui ne
sait pas classer le cas dont on connait deja la reponse ne peut pas trancher les
autres.** C'est le meme piege que le comptage de composants d'herbe de Landscape
(11 septembre), ou la demo du pack -- qui a pourtant un tapis visible -- donnait
le meme zero : on avait valide la metrique nulle part.

**ET J'AI RE-ELIMINE L'ENROULEMENT PAR RAISONNEMENT AVANT DE LE MESURER.** Le
commentaire du chemin moteur dit qu'il inverse ses indices « parce que
GeometryCore oriente pour un interieur POSITIF » ; j'en ai deduit le sens
attendu, conclu que ma valeur etait la bonne, et je suis reparti chercher
ailleurs pendant une heure. La deduction etait a l'envers. **Un enroulement ne
se deduit jamais, meme quand la chaine de raisonnement parait complete.**

**CE QUE LE DEFAUT N'ETAIT PAS**, et qu'il est inutile de resoupconner : le
champ (66 049 colonnes, 0 sans surface), le nombre de chunks (808 contre 809
pour le moteur, stabilises en 9 s), les anneaux (defaut identique a
`-WorldseedNiveaux=0`), les tables (0 citation hors bornes), la couture entre
chunks voisins (4 aretes ouvertes interieures sur 120 000), la longueur des
aretes (0 au-dela de trois voxels), ni l'aire (0,005 % d'ecart a emprise egale).
Le mailleur n'a jamais produit une mauvaise surface.

**TROIS CRITERES DE MAILLAGE, ET CE QUE CHACUN VOIT.** Ils ne se remplacent pas :
- *l'arete ouverte* voit une FISSURE, et rien d'autre. Un maillage reste
  combinatoirement clos quand ses triangles pointent vers le mauvais sommet ;
- *la longueur d'arete* voit cela. Un triangle de marching cubes a ses trois
  sommets sur les aretes d'UNE cellule, donc aucune arete ne peut depasser sa
  diagonale -- borne de CONSTRUCTION, qui ne suppose ni l'autre mailleur, ni le
  champ, ni l'enroulement ;
- *l'enroulement contre le champ* voit l'invisibilite, que les deux autres
  laissent passer sans un chiffre de travers.

**PIEGE DE PROTOCOLE, PAYE UNE FOIS DE PLUS.** La premiere tournee photo
tournait a `-WorldseedNiveaux=0` avec le rayon des anneaux, **1200 m a
resolution uniforme** : le monde n'etait pas bati, et l'on photographiait une
file d'attente. Le depot a la meme note pour le banc, deux fois. Toute vue
destinee a juger le terrain se prend a un rayon ou la diffusion **se stabilise
reellement** -- 250 m ici, 9 secondes -- ou dans la configuration reelle du jeu,
anneaux armes.

**CORRECTION DU REGISTRE, meme jour.** Le corps du commit
`fix(voxel): retourner l'enroulement` annonce « dix vues sur dix ». C'est
FAUX : la tournee a bien ecrit dix fichiers, mais cinq seulement ont ete
REGARDEES dans la configuration reelle -- `canyon01`, `canyon02`,
`falaise01`, `table02`, `arche02_dedans` -- plus trois au rayon de 250 m.
Toutes montrent un terrain plein et des parois qui occultent. Un fichier
ecrit n'est pas une vue jugee, et confondre les deux est exactement ce que
la regle « une forme qui n'a pas ete vue n'est pas validee » interdit.

**CE QUE CES VUES MONTRENT ET QUI RESTE A TRANCHER** : les parois de canyon
portent un reseau de fentes sombres, longues et ramifiees. Leur allure
correspond aux DIACLASES que la chaine produit a dessein -- faces de Voronoi
aplaties, ouverture minimale de deux metres -- mais cela n'a PAS ete verifie
separement. Ce sont soit la forme voulue, soit des trous residuels, et
seule une mesure le dira.

### La densite de maillage suit le relief, et elle est LIVREE ETEINTE (20 septembre 2026)

Demande ancienne : « dans une plaine nous n'avons pas besoin d'un maillage aussi
dense que sur une montagne rocheuse ». Le critere est ecrit, mesure, et **il
casse la contrainte 2:1** -- donc il arrive a zero.

**LE PREDICAT A DU DEVENIR UNIQUE D'ABORD.** `Enumerer` decidait des niveaux et
`NiveauEn` les rededuisait pour les masques, avec la MEME formule ecrite deux
fois. Tant que c'etait une distance, la copie tenait. Des que le relief entre en
jeu, deux copies divergent a la premiere retouche, et le resultat est une face
de transition armee la ou il n'y a pas de changement de resolution.

**MESURE AU BANC**, meme binaire, meme graine, rayon 1200 m, trois anneaux :

| rugosite | chunks | par niveau | triangles | 2:1 |
|---|---|---|---|---|
| 0,00 | 2165 | 1162/615/388/0 | 2 809 559 | tenu |
| 0,10 | 2157 | 1162/605/390/0 | 2 797 089 | tenu |
| **0,20** | **255** | 0/38/119/98 | **416 251** | **VIOLE** |
| 0,35 | 140 | 0/0/15/125 | 219 210 | VIOLE |

**LE GAIN EST ENORME -- moins d'un septieme des triangles -- ET INUTILISABLE EN
L'ETAT.** Transvoxel ne sait coudre QU'UN niveau d'ecart ; a 0,20 le banc trouve
un chunk de niveau 3 colle a un niveau 1.

**MA GARANTIE ETAIT FAUSSE, ET JE LA CROYAIS PROUVEE.** Prendre le relief sur
l'emprise ELARGIE d'une cellule assure bien que « si A descend, ses voisins
descendent » -- mais cela ne dit RIEN d'un cran plus bas : un enfant de B, au
niveau L-1, mesure une emprise trois fois plus petite, y trouve un relief local
fort, et descend encore pendant que A reste en haut. La recurrence saute d'un
niveau.

**C'EST LE CONTROLE POSE AVEC LE CRITERE QUI L'A DIT**, et c'est tout son objet :
une fissure ne se signale pas -- le masque s'arme quand meme, la geometrie reste
combinatoirement close, et le trou ne se voit qu'a l'oeil sur une jointure
precise. Le controle ne journalise rien quand tout va bien et crie une fois
quand la propriete tombe. **Une propriete de surete se verifie, elle ne se
suppose pas.**

**UN SIGNE DEJA VU SIX FOIS.** La premiere version rapportait l'etendue au cote
du NOEUD alors qu'elle etait relevee sur les NEUF : a 0,15 comme a 0, le banc
rendait 2165 chunks et 2 809 559 triangles AU CHIFFRE PRES. **Deux mesures
identiques pour deux reglages differents ne sont jamais un hasard.**

**LA SURFACE NE DIT PAS TOUT, et le critere le sait deja** : un aven s'ouvre sur
un PLATEAU, par definition. Juger la finesse sur le seul relief de surface
degraderait precisement les endroits ou le sous-sol est interessant. Le critere
interroge donc l'index spatial du reseau de cavites et ne degrade jamais un
noeud qui en contient.

**CE QUI RESTE A FAIRE** : un vrai equilibrage 2:1, sous la forme d'un niveau
exprime comme fonction PONCTUELLE et 1-lipschitzienne en unites de chunk. Un
equilibrage fait apres coup sur l'ensemble des feuilles ne conviendrait PAS :
`NiveauEn` est une descente ponctuelle, et elle cesserait d'accorder les masques
avec la diffusion.

`rugositeMin` n'est volontairement PAS dans `world_rules.json` : elle y
changerait l'empreinte, donc invaliderait les mondes en cache pour un reglage
inerte. Meme choix que `LargeurTransition`. `-WorldseedRugosite=` la pilote.

### Le sol de fond servait deux maitres, et c'est ce qui coutait 520 ms (21 septembre 2026)

Signale en jeu : « l'arche parait muree de l'exterieur, je traverse un faux
mur, et un ralentissement marque l'entree comme la sortie ». Deux symptomes,
une cause, et trois lecons de methode.

**CHRONOMETRER UN APPEL QUI NE FAIT QUE MARQUER NE MESURE RIEN.**
`SetRenderInMainPass` chronometre sur place rend **0,02 ms**, et l'on conclut
que la bascule est gratuite. C'est vrai et sans le moindre interet : l'appel
MARQUE l'etat de rendu sale, et la recreation du proxy de scene -- 8 388 608
sommets ici -- a lieu en FIN DE TRAME. La mesure juste se prend sur la trame
SUIVANTE : **508 a 528 ms**, pour un budget de 16,67.

**LE TEMOIN SE PREND SUR TOUTE LA SESSION, PAS AUTOUR DU SUSPECT.** Le journal
d'Unreal horodate ET numerote les trames : `(t2 - t1) / (f2 - f1)` entre deux
lignes consecutives donne le temps par trame, gratuitement et retroactivement.
Sur une partie entiere : EXACTEMENT quatre trames au-dessus de 300 ms, et
EXACTEMENT quatre bascules, sur les trames 390, 396, 804 et 94 -- les numeros
memes des bascules. Rien d'autre n'a jamais depasse 300 ms. C'est cela qui
attribue, pas la coincidence d'un horodatage.

**LE REMEDE EVIDENT CASSE L'OCEAN, ET J'AVAIS ECRIT POURQUOI UNE HEURE PLUS
TOT.** `SetMeshSectionVisible` n'appelle pas MarkRenderStateDirty
(ProceduralMeshComponent.cpp:789) : **0,00 ms**, l'a-coup disparait. Mais la
visibilite de section retire la geometrie de TOUTES les passes, celle de
PROFONDEUR comprise -- donc la nappe cesse de nourrir le plugin Water.
Signale des la premiere traversee : « avant j'avais de l'eau dans l'arche et
maintenant elle est coupee ». Mon propre commentaire, dans ce meme fichier,
annoncait que la masquer entierement « ferait cesser l'ocean de se dessiner
sans le moindre avertissement ». **Echanger un a-coup contre un ocean coupe est
un mauvais marche**, et relire ses propres avertissements avant d'essayer un
autre outil aurait coute moins cher.

**LE MONTAGE JUSTE : DEUX NAPPES.** Une pour l'EAU -- drapeaux poses une fois,
jamais touches, cout nul par construction -- et une pour l'IMAGE, decimee un
sommet sur quatre, basculee par visibilite de section. On ne peut pas les
dupliquer a l'identique : un `FProcMeshVertex` pese de l'ordre de cent
cinquante octets, donc une nappe pleine depasse le gigaoctet en copie
processeur. `GetTerrainPrimitives` etant deja surcharge, seule celle de l'eau
y figure.

**ET LA SEPARATION DEBLOQUE CE QU'AUCUN REGLAGE NE POUVAIT.** La nappe de
l'image, ne nourrissant plus rien, peut etre ENFONCEE -- ce qui etait interdit
tant qu'une seule servait les deux, l'eau croyant alors pouvoir monter d'autant.
**L'ENFONCEMENT SE DEDUIT, IL NE SE CHOISIT PAS** : le voxel ne creuse que dans
`bandeM` sous la surface, donc un decor pose sous cette bande ne peut boucher
aucune ouverture, ou qu'on soit.

**UN GAIN NON PREVU, ET IL EST GROS.** La passe principale ne dessine plus que
2 097 152 sommets de decor au lieu de 8 388 608 ; celle de l'eau n'est plus que
dans la passe de PROFONDEUR, qui n'execute pas le materiau. Releve en jeu :
**4,92 ms de trame, pire 6,83, 203 images par seconde**. Separer deux
responsabilites a retire les trois quarts du decor de la passe la plus chere.

**CORRIGER UN DEFAUT EN REVELE PARFOIS UN AUTRE QU'IL MASQUAIT.** L'a-coup
cachait une apparition brutale du fond a la sortie de l'arche : le gel de
520 ms la recouvrait. Une fois la bascule gratuite, elle s'est vue. Elle a
disparu d'elle-meme en passant le decor sous la bande creusable -- il n'y a
plus rien a faire apparaitre.

**ECARTE, POUR QU'ON NE LE RETENTE PAS** : decouper la nappe en sections et
cacher celles du champ proche. `FProceduralMeshSceneProxy::GetDynamicMeshElements`
**n'elimine PAS les sections hors champ** -- il emet un lot de dessin pour
chaque section visible, a chaque trame. Et les sections doivent tenir dans le
rayon de chargement pour ne pas laisser de trou a l'horizon : 850 m a 1200 m de
vue, soit 2775 sections, donc 2775 appels de dessin permanents.

**CE QUI RESTE OUVERT, DIT FRANCHEMENT** : a la limite du terrain detaille, le
sol qu'on voit tombe de 125 m d'un coup -- un anneau de falaise centre sur le
joueur, a 1200 m, qui le suit. Regarde depuis un sommet, rien de visible : le
relief lointain se lit comme du relief lointain, et la bande de transition
tombait sous la couche nuageuse. Ce n'est PAS une certitude pour toutes les
conditions. `-WorldseedNappeVue=<metres>` recule l'enfoncement sans recompiler.
L'autre levier, non employe, serait de porter la vue a 2400 m : la marche
resterait haute mais deux fois plus loin, pour douze pour cent de chunks en
plus -- chiffre deja mesure dans ce registre.

**PERIME AU 27 SEPTEMBRE 2026 : LES DEUX LEVIERS CITES CI-DESSUS ONT CHANGE
D'ETAT.** `-WorldseedNappeVue=` **N'EXISTE PLUS** -- retire au menage du
23 septembre, la question etant tranchee ; un A/B monte dessus le 27 a compare
DEUX FOIS LA MEME CHOSE, et c'est le journal, en annoncant le meme enfoncement
des deux cotes, qui l'a dit. Et le second levier, annonce ici « non employe »,
**EST employe** : `voxel.rayonChargementM` vaut **2400 m** dans
`world_rules.json`. Pour rouvrir la question, on REPOSE la surcharge -- quelques
lignes, et la regle du depot l'exige, un A/B qui demanderait d'editer le fichier
de regles en changerait l'empreinte donc regenererait le monde entre les deux
moities.

### Les diaclases GROSSISSENT avec la distance au lieu de s'effacer (21 septembre 2026)

Signale en jeu : « toutes les faces de la montagne a droite ne sont pas finies
d'afficher et l'on voit l'interieur, il y a des diaclases derriere la surface
de la roche non affichee ».

**CE N'EST PAS UN TROU, ET LA MESURE LE DIT.** Echantillonnage du fond des
fentes contre des references prises dans la meme image :

    ciel                 194/201/204    luminance ~200
    brume lointaine      157/170/191    luminance ~173
    roche sombre proche    4/  6/ 10    luminance ~7
    FOND DES FENTES       10/ 13/ 15    luminance ~13

Le fond des fentes est de la roche NON ECLAIREE, pas du vide : la surface est
fermee. On y distingue meme les bancs stratigraphiques sur les parois. Premier
releve fait avec une reference de « ciel » prise par erreur SUR LA MONTAGNE
(158/159/137) : il ne mesurait rien, et il a fallu le refaire.

**LE VRAI DEFAUT EST AILLEURS, ET C'EST LE PROPRIETAIRE QUI L'A ISOLE** en
marchant vers la montagne : « une grande partie des fentes se referment ». Donc
ce sont les ANNEAUX DE RESOLUTION qui les exagerent.

**LA CAUSE EST UN PROBLEME D'ECHANTILLONNAGE, PAS DE GEOMETRIE.** Une diaclase
a deux metres d'ouverture au minimum -- plancher IMPOSE par le voxel, « a 1 m
de voxel, le marching cubes ne peut pas representer une fente plus etroite que
deux voxels ». Or les anneaux maillent a 2 m puis 4 m : une fente de deux
metres y tombe SOUS la limite representable. Elle ne disparait pas pour autant,
elle s'ALIASE -- le mailleur en attrape des morceaux au hasard des cellules, et
la falaise parait dechiree.

**LE REMEDE EST CELUI DU MIP-MAPPING, et il n'a pas ete ecrit** : un detail
plus fin que la maille ne doit pas etre echantillonne, il doit etre ATTENUE.
Le terme de diaclase devrait donc s'effacer en fonction de la taille de voxel
du chunk en cours, au lieu d'etre evalue a l'identique a tous les niveaux. Le
meme argument vaut pour toute forme dont l'epaisseur approche la maille --
lames et fentes de canyon au premier chef.

**NON CORRIGE, PAR DECISION DU PROPRIETAIRE** : « les reglages sont
globalement bons, surtout que le monde n'est pas encore texture ». A reprendre
avec l'habillage.

### La peinture du decor en mer : cablee, vue, et sans effet mesurable (22 septembre 2026)

**CORRECTION DU COMMIT `99e43ee`, qui en promet plus qu'il n'a prouve.** Le
terme qui peint en mer le sol de fond sous zero est juste, il s'execute et il
atteint l'ecran -- mais **aucun des trois points de vue eprouves n'a montre
la moindre difference a l'oeil ni au compteur**, et il faut le dire.

| point de vue | altitude | verdict |
|---|---|---|
| belvedere (930, -5758) cap 240 | 69 m | aucune difference |
| sommet (-3600, 6603) cap 45/46 | 307 m | **27 contre 25 pixels sable sur 27 000** |
| fenetre forcee a 0,5 km | 69 m | montage FAUX, voir plus bas |

**CE QUI EST PROUVE, ET PAR QUOI.** Le temoin magenta
(`-WorldseedMerOpaque=2`) fait apparaitre deux bandes franches pres de
l'horizon : le chemin s'execute ET son resultat est affiche. Le compteur
journalise dit l'ampleur -- **5 939 134 sommets sur 8 388 608 sous le niveau
zero, soit 70,8 %**. La question n'est donc pas « est-ce branche » mais
« est-ce que cela se voit quelque part ».

**POURQUOI ON NE LE VOIT PAS** : au-dela de la fenetre de 12,288 km, le
relief visible depuis les points essayes est AU-DESSUS du niveau de la mer --
un plateau vert a falaises dans le cas du sommet. Il n'y a rien a repeindre.
Le terme reste une ceinture de securite, pas un correctif dont l'effet est
etabli : c'est **l'elargissement de la fenetre** qui a corrige ce que le
proprietaire voyait.

**TROIS MESURES FAUSSES SUR CE SEUL POINT, ET ELLES SE RESSEMBLENT :**

1. **Une region de mesure qui mord sur le premier plan.** Comptant les pixels
   « sable » dans une bande large, j'ai trouve 1 746 contre 83 -- vingt-et-une
   fois moins, et j'allais conclure. **Le surlignage des pixels comptes a
   montre qu'ils etaient la DUNE du premier plan et la JAMBE du personnage**,
   pas une bande dans la mer. Une difference spectaculaire sur une region mal
   bornee ne mesure que le bornage. **Localiser les pixels comptes avant de
   lire le compte.**
2. **Un A/B dont le montage pousse le defaut hors de portee du traitement.**
   Pour grossir la couture j'ai ramene la fenetre a 0,5 km : le bord tombait
   alors a 250 m, donc DANS le rayon voxel de 1 200 m -- la seule zone ou ce
   terme ne s'applique pas, et volontairement (sous les pieds on voit le fond
   a travers l'eau, et c'est ce qu'on veut).
3. **Un fichier PRESENT n'est pas un fichier FRAIS.** J'ai lu et commente une
   magnifique vue de sommet au-dessus des nuages avant de voir qu'elle datait
   de ONZE HEURES et precedait tout le chantier. Le depot avait deja la regle
   « un fichier ecrit n'est pas une vue jugee » ; en voici la variante.
   **Controler l'horodatage AVANT de regarder, et supprimer le fichier cible
   avant de relancer.**

**ET L'ECLAIRAGE INTERDIT TOUJOURS L'A/B PAR LANCEMENTS SUCCESSIFS.** Temoin
sur la dune du premier plan, hors d'atteinte du traitement : **98 sur 765**
de derive entre les deux moities. Quand la derive joue DANS le sens de
l'effet cherche, aucune conclusion n'est possible ; quand elle joue contre --
ici l'image « avec » etait plus claire, donc favorable a la classification
« sable » -- un effet observe malgre elle garde une valeur. **Dire dans quel
sens le biais joue fait partie de la mesure.**

**LE PION NE RESTE PAS OU ON LE POSE, ET LA CAUSE EST MAINTENANT VISIBLE.**
`FindFlatGround` privilegie la PLATITUDE sur la PROXIMITE : demande a
(-3641, 6516) ou la surface est a 661,4 m, il a rendu (-3600, 6603) a
**304,8 m, pente 0,2 degre -- 357 metres plus bas**. Ce n'est donc pas une
glissade : c'est la recherche qui descend chercher du plat. Consequence
pratique : **on ne peut pas viser un sommet**, et le choix du point de
naissance perd son sens des que le relief est raide. A reprendre en bornant
l'ecart d'altitude, comme `EcartAltitudeDepartM` le fait deja pour le depart
choisi au menu.

### Le point de naissance : ce n'etait pas la borne qui manquait (22 septembre 2026)

Qui visait un sommet naissait a son pied -- 89 et 92 metres plus bas sur deux
parties independantes, puis 357, puis 914, au niveau de la mer.

**LA BORNE EXISTAIT ET S'APPLIQUAIT.** `EcartAltitudeDepartM` valait quarante
metres et le chemin du point choisi l'employait. Le defaut etait une **FALAISE
DE POLITIQUE** : quarante metres, puis l'infini, en un cran. Quand la passe
bornee echouait, le repli repartait a douze degres SANS contrainte d'altitude
et prenait la plaine. **J'ai failli ajouter ce qui existait deja** -- la tache
etait notee « borner l'ecart d'altitude » et elle etait periemee.

**ARBITRAGE DU PROPRIETAIRE : echec franc.** Si rien de tenable n'existe dans
la borne, on TIENT le point vise. Ecartees apres presentation : l'elargissement
par crans (40, 120, 360, sans borne), qui gardait un echec graduel ; et le
MEILLEUR de toute la spirale, qui traitait la cause nommee par le commentaire
d'origine -- « elle retient le PREMIER point acceptable, pas le meilleur » --
mais au prix de la PROXIMITE, en naissant jusqu'a 380 m du point vise pour
gagner trois metres. **Ce qui est accepte : le pion peut naitre sur une paroi
et glisser.** La naissance LIBRE ne change pas -- l'endroit n'y a aucune
importance.

#### Le releve de reference du registre etait PERIME

Le cas documente -- (-3641, 6516), surface 661,4 m -- pointe aujourd'hui
**339 metres SOUS LA MER**. Le monde a ete regenere plusieurs fois depuis
(bumps de version, deplacement des plaques). **Un releve en coordonnees
absolues ne survit pas a une regeneration**, et c'est exactement pourquoi la
preuve de ce chantier est un TEST sur relief fabrique et non un releve : le
test, lui, dit la meme chose dans six mois.

#### Une rampe uniforme ne contient pas le cas -- troisieme fois du jour

Le premier test de borne tournait sur une rampe a 31 degres partout. La
recherche NON bornee n'y trouve rien non plus -- « rien trouve dans les 384 m »
-- donc **le temoin ne s'executait pas et le test passait sans rien comparer**.

Le defaut reel demande TROIS choses a la fois : un versant trop raide pour
qu'on y naisse, du PLAT plus bas, et le tout dans les 384 metres que la spirale
fouille. D'ou une COLLINE -- plaine a 20 m, versant a 31 degres, sommet a 260 --
et une maille de quinze metres, contre cent vingt-cinq pour la grille de la
rampe : un versant de deux cents metres n'y tiendrait pas, la bicubique
l'arrondirait en pente douce. Apres correction, le test reproduit le defaut :

    sans borne   (-208, -208) m, altitude 19,4 m, soit -121 m
    avec borne   rien trouve -- c'est l'echec franc
    sur la plaine  ecart 0,0 m, pente 0,0 deg   <- la meme borne laisse passer

Le troisieme cas compte autant que les deux autres : sans lui, « la borne
refuse » ne se distinguerait pas de « la borne refuse toujours ».

#### Un chemin d'echec ne s'eprouve pas en esperant qu'il arrive

**SIX ESSAIS EN JEU N'ONT PAS DECLENCHE L'ECHEC FRANC.** La spirale de 384 m
trouve presque toujours quelque chose dans les quarante metres -- ce qui est
une bonne propriete, et rendait le nouveau chemin non execute, donc une dette.
Releves au passage, tous sur des parois cotieres (la terre emergee la plus
proche d'un point en mer est une falaise) : pentes de 56 a 80 degres, et dans
le cas le plus serre la borne a mordu juste -- **-38 m retenus au lieu de -46**.

Remede conforme a la regle du depot -- « quand un A/B demande un reglage qui
n'a pas de surcharge, on AJOUTE la surcharge ; on ne touche pas au fichier » :
**`-WorldseedEcartDepart=`**. A 1 m, l'echec est certain et le chemin s'eprouve :

    aucun sol tenable a moins de 1 m d'altitude -- ON TIENT LE POINT VISE (pente 80,0 deg)
    point vise TENU a (-16445, 4008) m, altitude 46,2 m, ecart nul

#### Deux acquis annexes

- **`WorldseedPlacement`** : `PenteDeg`, `SolPlein` et `SolPlat` ont quitte
  l'acteur. Elles ne lisaient que le champ de densite -- ni l'acteur, ni le
  monde, ni le pion -- et les garder dans un fichier de deux mille neuf cents
  lignes les rendait intestables, alors qu'elles decident de la premiere chose
  que le joueur voit.
- **`-WorldseedDepartExact=` rapporte enfin la PENTE.** Il annoncait « 0,0 deg »
  sur n'importe quelle paroi, faute d'appeler la recherche -- or c'est le
  drapeau qu'on emploie pour inspecter des endroits impraticables. Il a servi
  de sonde pour trouver les points raides de ce chantier.

### Le mailleur legataire est parti, et ce qu'il clouait avec lui (22 septembre 2026)

**ARBITRAGE DU PROPRIETAIRE : supprimer.** Le voxel tenait le relief depuis le
18 septembre ; l'ancien mailleur en carte d'altitude restait derriere un
drapeau `bUseVoxelMesher`, comme repli si l'acteur voxel ne se posait pas.

**CE QUE LA VERIFICATION A ETABLI, et c'est elle qui a decide :**

- il n'etait joignable par **AUCUNE ligne de commande**. `-WorldseedVoxel=`
  n'est pas un interrupteur, c'est une TAILLE DE VOXEL
  (`WorldseedVoxelTerrain.cpp:452`) -- le registre laissait croire le
  contraire. Restaient le drapeau bascule a la main dans l'editeur, et un echec
  de `SpawnActorDeferred` en pratique inatteignable ;
- **rien ne l'avait exerce depuis l'A/B du 18 septembre.** Il compilait ; qu'il
  TOURNE encore n'etait pas etabli. Un filet qu'on n'eprouve pas n'est pas un
  filet, c'est une dette ;
- un argument qui le justifiait etait devenu **FAUX** : l'en-tete invoquait
  « la grille d'altitudes existe en double -- huit megaoctets », ce qui n'est
  plus vrai depuis le monde partage du matin meme.

**ET LA TACHE ELLE-MEME ETAIT PERIMEE, comme celle du point de naissance.**
Elle disait « garder ou non ce filet » ; en mesurant, le filet s'est revele
inatteignable. **Deux taches perimees dans la meme journee : un carnet se
remesure avant d'etre execute.**

#### Ce qu'il clouait, et c'est la vraie raison de le retirer

`ComputeVertexAppearance` avait **exactement DEUX consommateurs** -- lui et le
sol de fond. L'en-tete de l'acteur disait pourquoi cela comptait : « deux
versions de cette regle finiraient par diverger, et la difference se verrait
exactement la ou les deux maillages se rencontrent ». C'etait juste, et cela le
CLOUAIT : aucun des deux ne pouvait partir sans recopier la regle.

Le legataire supprime, il n'en reste qu'un. Le calcul vit desormais dans
`WorldseedApparence`, avec `FWorldseedAppearance` -- qui decrit precisement ce
qu'il produit -- et `FWorldseedSurfaceRegles` pour les huit seuils qu'il lit.

**ATTENTION : il existe un SECOND calcul d'apparence**,
`AWorldseedVoxelTerrain::PaintVertices`. Il ne fait PAS la meme chose -- il
peint en trois dimensions et connait la roche -- donc ce n'est pas une
duplication a resorber. Mais les deux se rencontrent la ou le terrain proche
rejoint l'horizon, et une divergence s'y verrait.

#### Une fixture muette d'une forme NOUVELLE : le registre non charge

Le premier test d'apparence comparait la sortie a
`WorldseedBiomes::Colour(Ocean)`. Trois assertions negatives ont echoue -- un
sommet a **dix metres d'altitude** sortait « de la mer ». Ce n'etait pas la
regle qui debordait : **les couleurs de biome viennent du REGISTRE**, charge
depuis `world_rules.json`, qu'un monde fictif ne charge pas. Toutes les teintes
valaient la meme valeur par defaut, et l'egalite etait vraie partout.

Le verdict se lit donc sur le **TEMOIN MAGENTA**, ecrit en dur dans le calcul :
il ne doit rien au registre, donc il dit exactement quelle BRANCHE a ete prise.
C'est la raison d'etre de ce temoin en jeu -- « une couleur franche ne se
compare a rien, elle est la ou elle n'est pas » -- et elle vaut aussi dans un
test.

**QUATRIEME FORME DE LA MEME FAUTE DANS LA JOURNEE**, apres les biomes vides
(deux `nullptr` egaux), le comblement sans cuvette, et la rampe uniforme sans
plat. Celle-ci est la plus sournoise : la fixture etait garnie, le calcul
juste, et c'est une DONNEE EXTERIEURE non chargee qui rendait la comparaison
aveugle.

#### Ce qui est mesure

    WorldseedTerrain.cpp   2138 -> 1445 lignes   (-32 %)
    en-tete                 730 ->  600
    tests                     23 ->   25

Non-regression relevee AVANT de toucher au fichier, identique au chiffre pres
aux deux etapes -- elle ne figure meme pas dans le diff :

    sol de fond : 5939134 sommets sur 8388608 sous zero (70,8 %)
    sol de fond : section terre 628117 / 1238478, section mer 1484098 / 2949684
    eau : hauteurs d'eau [-371 .. 371] m, texture d'info 4096x4096
    voxel : 2390 chunks, 3140011 triangles

#### CE QUI RESTE, ET POURQUOI JE M'ARRETE LA

`BuildGroundProxy` (655 lignes) et `UpdateGroundProxyVisibility` (135)
appartiennent a `AWorldseedGroundProxy`, qui existe deja et possede deja les
composants. **Mais ce n'est PAS un deplacement mecanique** : le calcul de
geometrie et le CYCLE DE VIE DES ACTEURS sont entrelaces sur les 655 lignes --
`SpawnActor` aux lignes 1023 et 1121, `CreateMeshSection` aux lignes 1041 et
1320, avec du calcul de grille entre les deux. Le faire proprement demande de
decider qui possede quoi, pas de couper au bon endroit.

C'est un chantier a part entiere, avec sa propre verification : le sol de fond
nourrit la texture d'information du plugin Water, et le piege `[0 .. 0]` --
l'ocean qui cesse de se dessiner SANS le moindre avertissement -- est le plus
silencieux du depot.

### L'horizon etait NOYE, pas marche : la rampe pilotee par la camera (22 septembre 2026)

La tache #19 parlait d'« une marche de 125 m au bord du terrain detaille ». Le
defaut etait tout autre, et bien plus gros : **l'enfoncement de la nappe vue
faisait passer 16,3 % des terres SOUS LE NIVEAU DE LA MER**, ou l'ocean les
recouvrait. Vu depuis un sommet, une vallee verte avec sa plage se lisait comme
une baie.

**LE MECANISME.** La nappe vue s'enfonce de `bandeM + marge` -- cent vingt-cinq
metres -- pour passer sous tout ce que le voxel peut creuser. Mais elle porte le
relief du monde ENTIER : tout ce qui culmine plus bas que l'enfoncement se
retrouve sous l'altitude zero, et l'ocean est un plan a zero. Mesure :
**398 839 sommets de terre sur 2 449 474**. Courbe relevee au passage -- part
des terres noyees selon l'enfoncement : 25 m -> 7,1 % ; 50 -> 9,1 ; 75 -> 11,3 ;
100 -> 13,8 ; **125 -> 16,3** ; 150 -> 18,7.

**POURQUOI LA NOTE DU 21 CONCLUAIT « RIEN DE VISIBLE DEPUIS UN SOMMET », ET
POURQUOI C'ETAIT LE PIRE ENDROIT OU REGARDER.** Le sol lointain ne reapparait
qu'a `R.(1 + 125/h)` ou h est la hauteur de l'oeil : **1,5 km depuis un oeil a
500 m, mais 7,2 km depuis un oeil a 25 m**. Plus on est HAUT, moins la bande
cachee est large. Le pire cas est donc le joueur debout dans une plaine, pas le
belvedere -- et c'est depuis le belvedere qu'on avait juge.

#### Deux plafonds essayes et MESURES COMME MAUVAIS

Arbitrage rendu par le proprietaire : plafonner l'enfoncement au niveau de la
mer, `z' = max(z - 125, marge)`. L'argument etait bon sur le papier -- aucune
chambre n'existe sous soixante-six metres d'altitude, donc une nappe arretee a
la marge de mer reste sous tout plancher de cavite. **La mesure l'a refute.**

    lointain, part d'eau     cap 270    cap 225
    avant (125 m uniforme)    73,7 %     51,4 %
    plafond a 5 m             77,3 %     53,7 %     <- PIRE
    plafond a 40 m            67,8 %     65,3 %     <- se contredit
    verite (sans enfoncement) 63,0 %     38,9 %

**UNE PLATE-FORME A HAUTEUR DE RIVAGE N'EST PAS UNE TERRE.** A trois
kilometres, cinq metres d'altitude ne se distinguent pas de l'eau -- et la
houle passe par-dessus. Le plafond sauvait la terre du noyage et la rendait
indiscernable de la mer, donc *plus* d'eau a l'ecran qu'avant. Remonte a
quarante metres, il ameliore un cap et degrade l'autre : **un critere qui change
de verdict selon le cadrage ne decide de rien.**

#### Ce qui marche : la rampe, et pourquoi elle n'a pas les memes bornes

Les deux exigences ne portent pas sur la meme DISTANCE, et c'est tout le
probleme -- les opposer etait l'erreur. Pres du joueur, la nappe doit passer
sous la bande creusable ; au loin, elle doit etre a son altitude vraie. Un
deplacement de sommets pilote par la position de la CAMERA les concilie :
enfoncement entier jusqu'au rayon de vue, nul au-dela de deux fois ce rayon.

    lointain, part d'eau     cap 270    cap 225
    rampe                     61,4 %     39,1 %
    verite                    63,0 %     38,9 %

**0,2 et 1,6 point de la verite, sur les deux caps, sans contradiction.**

**LE PLAFOND DE MER RESTE, ET IL CHANGE DE ROLE.** Il ne sert plus a sauver les
terres -- la rampe le fait mieux -- mais a **EPINGLER LE RIVAGE**. C'est ce qui
supprime le seul risque serieux de la rampe.

**LE COUT, mesure sur le meme binaire** (`-WorldseedRampe=0/1`) : trame **4,37 ms
armee contre 4,34 coupee**, soit la derive machine. Le deplacement de sommets
sur deux millions de sommets ne se paie pas.

#### Les deux risques, eprouves au lieu d'etre supposes

- **LA RESPIRATION.** La rampe suit la camera, donc un point du monde change
  d'altitude dessinee quand on marche -- environ un metre tous les dix metres.
  Mesure sur 200 m parcourus, silhouette du relief lointain : **10,8 px de
  deplacement avec la rampe contre 13,0 px sans**, c'est-a-dire MOINS que la
  parallaxe de la marche elle-meme. Le risque ne se materialise pas. Le rivage,
  lui, ne bouge pas du tout : le plafond de mer l'epingle.
- **L'ARCHE.** Pres du joueur la rampe enfonce autant qu'avant, donc rien ne
  change aux cavites. Verifie sur les quatre vues d'arche de la tournee, rampe
  armee puis coupee : **aucune n'est muree, dans aucun etat**.

**ET J'AI PRESENTE LA REGRESSION DE L'ARCHE COMME JUGEABLE SUR UNE IMAGE FIXE.**
Le proprietaire l'a releve : « tu me parles d'une problematique sur la fermeture
de l'arche que je ne vois pas dans les differents cas d'usage ». Il avait raison.
Ce que l'enfoncement a ferme le 21 septembre est un **SURGISSEMENT** du fond a
la sortie d'une cavite -- un defaut qui n'existe qu'ENTRE deux images. Aucune
capture ne pouvait le montrer. **Quand un risque est temporel, le dire, et ne
pas laisser croire qu'une planche de vignettes en decide.**

#### Comment la rampe est montee

- **Le materiau porte le deplacement, le C++ porte le plafond.**
  `M_WorldseedBiome` (VERSIONNE, `Content/Worldseed/Materials/`) recoit dix-huit
  noeuds : distance XY a la camera, rampe lineaire entre deux parametres
  scalaires, multipliee par un plafond PAR SOMMET lu dans le canal **UV3**.
- **UV3 etait libre** : la nappe n'en posait que trois. Et les chunks du terrain,
  qui partagent ce materiau, n'en posent AUCUN -- leur section se cree sans
  tableau d'UV. Une porte `NappeRampeActive`, a zero dans le materiau de base,
  garantit de toute facon qu'eux ne bougent pas : seule la nappe recoit une
  instance dynamique qui l'arme.
- **Le plafond ne peut pas vivre dans le materiau** : il demande l'altitude
  VRAIE du sommet, que celui-ci ne porte pas une fois deplace. D'ou le canal.
- **`ReglerRampeDeLaNappe` est appelee DEUX fois, et il le faut.** La nappe se
  batit AVANT que l'acteur voxel soit pondu : au premier appel le rayon de vue
  n'est pas connu et l'on pose le defaut de classe (600 m), le second appel le
  corrige (1200). Une seule pose aurait cale la rampe sur un rayon qui n'est pas
  celui du jeu, **et rien ne l'aurait signale** -- le journal dit desormais
  laquelle des deux parle.

#### L'outillage ajoute, reutilisable

- **`-WorldseedVue=<caps separes par des virgules>`** avec `-WorldseedVueX/Y/H/
  Tangage/Nom=` : une vue LIBRE, cadree a la main, au lieu de la tournee des
  formes. Les quatre familles d'arrets visent des FORMES et se placent a
  90-170 m d'elles -- exactement ce qu'il faut pour juger une geometrie, et
  exactement ce qui ne montre JAMAIS l'horizon. Aucune mecanique nouvelle : un
  arret est deja « se poser en Cible + Depuis * Distance puis viser Cible »,
  donc une vue libre est un arret dont la cible est posee a quatre kilometres
  dans l'axe du cap.
- **PIEGE PAYE : `FParse::Value` s'arrete sur une VIRGULE.** Son quatrieme
  argument `bShouldStopOnSeparator` vaut VRAI par defaut : « 0,90,180,270 »
  arrive comme « 0 », une seule vue, **sans un mot**. Passer `false`.
- `-WorldseedNappeMarge=` et `-WorldseedRampe=` pour les A/B sans recompiler.

#### Trois pieges de mesure, tous de la meme famille

1. **L'ECART RGB A LA VERITE MELANGE DEUX POPULATIONS.** Le plafond a 5 m
   eloignait l'image du temoin (37,7 -> 52,4 sur 765) et j'ai failli conclure ;
   c'etait un agregat sur du relief ET de la couleur d'eau. Decompose en « part
   du lointain qui se lit comme de l'eau », le verdict devient lisible.
2. **ET CE SECOND CRITERE ETAIT LUI-MEME CONFONDU.** Ne plus enfoncer le FOND
   MARIN -- effet de bord du plafond -- le remonte de cent vingt-cinq metres,
   donc l'eau change de couleur par la profondeur. Le compte mesurait en partie
   cela. Il n'est redevenu franc qu'avec la rampe, qui laisse le fond a sa vraie
   profondeur dans les deux moities.
3. **UN TEMOIN DE RAYON DOIT COUVRIR LA DISTANCE QU'ON PHOTOGRAPHIE.** Premier
   A/B monte a `-WorldseedRayon=300` : au-dela de 300 m ce n'est plus du voxel
   mais la nappe, donc la vue large ne comparait rien. Deja consigne pour la
   fenetre d'eau ramenee a 0,5 km ; refait.

**PIEGE D'OUTILLAGE, SILENCIEUX : Git Bash reecrit les chemins de carte.**
Lancer le jeu depuis bash avec `/Game/Worldseed/Maps/L_Worldseed_Proc` donne
`LoadMap: C:/Program Files/Git/Game/...` puis `Failed to load package`. MSYS
prend tout argument commencant par `/` pour un chemin POSIX. Le jeu demarre, ne
charge rien, QUITTE -- et le journal ne porte **aucune ligne `[Worldseed]`**, ce
qui ressemble trait pour trait a un module qui ne s'initialise pas. **Tout
lancement du jeu passe par PowerShell.**

**ET LES MATERIAUX S'EDITENT SANS MCP**, ce qui compte parce que le lien tombe a
chaque relance de l'editeur. `unreal.MaterialEditingLibrary` en commandlet
(`-run=pythonscript`) cree les expressions, les relie, branche la propriete et
recompile. Le script est idempotent PAR CONSTAT -- il cherche son propre
parametre et sort s'il le trouve -- jamais par destruction : le depot a deja paye
une greffe posee deux fois sur la RVT.

**L'ORACLE AJOUTE** : `Worldseed.Nappe.PlafondDeMer`. Il garde l'INVARIANT et
non le chiffre -- le pourcentage bouge a chaque regeneration, la propriete non :
le plafond n'eleve jamais un sommet (donc aucun fond marin ne peut emerger,
question posee par le proprietaire) et ne laisse aucune terre passer sous la
marge. Temoin monte puis retire : rendre l'enfoncement uniforme, comme avant, le
fait tomber sur « on n'enfonce plus du tout to be 0, but it was 125 ».

### Les parois en terrasses : c'etaient les NORMALES, et le test etait a une ligne (22 septembre 2026)

Tache #21. Les parois de canyon sortaient en gradins, visibles de pres comme de
loin. Le registre proposait trois hypotheses jamais mesurees. **Les trois etaient
fausses, et la bonne reponse tenait dans le test que ce depot s'impose EN
PREMIER depuis le 18 septembre -- que je n'ai fait qu'en quatrieme.**

    ShowFlag.Lighting 0   ->  la paroi est un gris PARFAITEMENT UNIFORME

La geometrie est lisse. Les terrasses ne sont pas dans le relief, elles sont
dans l'ECLAIRAGE -- donc dans les normales.

#### Ce qui a ete essaye, et ce que chaque essai a rendu

Metrique : amplitude RMS de l'ondulation verticale de luminance sur une bande de
paroi, tendance retiree. Meme monde, meme graine, meme cadrage, meme heure.

    avant -- normales moyennees sur les faces          1,49   reference
    voxel porte a 2 m                                  6,63   le defaut SUIT LA GRILLE
    mailleur du moteur au lieu de Transvoxel           1,49   ce n'est pas le mailleur
    deux octaves de bruit les plus fins retires        1,81   ce n'est pas l'aliasing
    portes du champ rendues perpendiculaires           1,49   ce n'est pas la porte
    NORMALES PRISES AU GRADIENT DU CHAMP               1,24   retenu, -17 %
    gradient + un octave retire                        1,33   la combinaison n'aide pas

**LE MAILLEUR RENDAIT 1,49 DES DEUX COTES, AU CENTIEME PRES.** C'est le signe
que ce depot connait par coeur -- deux mesures identiques pour deux reglages
differents -- et pour une fois il disait vrai : les deux mailleurs echantillonnent
le meme champ et calculent leurs normales de la meme facon, donc ils ne
pouvaient pas differer.

#### Pourquoi la moyenne des faces bande, et pourquoi le gradient ne coute rien

La normale moyennee sur les faces incidentes est lisse le long des aretes
partagees -- c'est ce qui l'avait fait choisir le 18 septembre, a 1 ms par chunk
contre 195 pour le gradient. Mais elle ne decrit que la TRIANGULATION. Sur une
surface raide, le marching cubes produit des triangles en lamelles dont
l'orientation suit la grille : la normale herite de la cellule, et la bande suit
la taille du voxel. D'ou 1,49 a un metre et 6,63 a deux.

**ET LE GRADIENT ETAIT DEJA LA.** Six evaluations du champ par sommet, calculees
depuis le 18 septembre pour trancher le SENS de la normale -- et dont on jetait
le vecteur. L'employer tel quel remplace une grandeur qui decrit le maillage par
une grandeur qui decrit le CHAMP, lisse par construction, **pour zero evaluation
de plus**. Trame mesuree : 4,40 ms contre 4,37, soit la derive machine.

Le registre l'avait annonce en le retirant : « le gradient reste dans
l'historique git si l'eclairage montre un jour des facettes ». Ce jour etait
celui-la, et la note a suffi a retrouver le chemin.

**LA MOYENNE DES FACES RESTE LE REPLI, et il en faut un** : la ou le champ est
plat -- le plateau de la sortie rapide, deux formes qui s'annulent -- le gradient
est nul et ne dit rien. `GetSafeNormal` rend alors le vecteur nul et l'on garde
ce que la geometrie sait.

#### La porte perpendiculaire est ECRITE et ETEINTE

Le champ mesure `z - H(x, y)`, une distance VERTICALE, et ses deux portes -- la
sortie rapide et la bande ou le bruit s'applique -- sont des seuils dessus. Sur
une paroi de pente vingt, quatorze metres de distance verticale sont atteints en
soixante-dix centimetres horizontalement, **soit moins d'un voxel** : la porte
tombe DANS la cellule que le mailleur interpole, et le champ y saute de toute
l'amplitude du bruit. La discontinuite est reelle, le remede -- diviser par la
norme du gradient, comme le code des arches depuis le 19 septembre -- est juste.

**Il ne deplace pas le defaut cherche : 1,49 contre 1,49.** Cout 9,91 ms par
chunk contre 9,71. Livree ETEINTE, comme `overhangWarpM` et `rugositeMin` avant
elle : le code est juste, la mesure reste reproductible par `-WorldseedPerp=1`,
et le jour ou une forme de paroi butera sur cette discontinuite il sera ecrit.

#### La lecon, et elle est deja dans ce fichier

**J'AI PERDU TROIS HYPOTHESES FAUTE DE COMMENCER PAR `ShowFlag.Lighting 0`.**
La note du 18 septembre dit exactement cela : « A faire AVANT de soupconner quoi
que ce soit d'autre -- j'ai perdu deux hypotheses faute de commencer par la. »
Elle a ete ecrite apres avoir perdu deux hypotheses ; je viens d'en perdre trois
en ne la lisant pas. **Un registre ne sert que s'il est consulte AVANT de
chercher, pas apres avoir trouve.**

Corollaire de methode, plus general : devant un defaut visuel, la premiere
question n'est pas « quel terme le produit » mais **« dans quelle passe vit-il »**
-- geometrie, normales, couleur, eclairage. Une ligne de commande de rendu
repond a celle-la, et elle elimine les trois quarts des suspects avant qu'on
n'ouvre un fichier.

#### Ce qui reste, dit franchement

- **L'amelioration est partielle** : -17 %, les gradins sont adoucis, pas
  supprimes. La piste suivante est la continuite de `SampleUVCubic` -- un
  cubique C1 a une derivee SECONDE discontinue, donc la normale change de pente
  a chaque maille de 31 m. Le depot la listait deja comme un oracle jamais
  ecrit. **Non enchainee a dessein** : la regle interdit une quatrieme
  hypothese a l'aveugle.
- **Les blocs noirs perfores du bord gauche** sont un defaut DISTINCT, present
  a l'identique dans les sept etats mesures, et jamais diagnostique.
- **Outillage** : `-WorldseedNormales=0/1`, `-WorldseedPerp=0/1`,
  `-WorldseedDetailOctaves=` pour rejouer chaque ligne du tableau sans
  recompiler.

#### Les terrasses ne sont PAS les bancs -- et les bancs, eux, ne se voient pas

Question du proprietaire, et elle etait la bonne a poser : « ces strates ne
sont-elles pas normales et dues a la stratification des differents types de
roche et de la couleur de texture qui leur est attribuee ? » Le doute etait
d'autant plus fonde qu'une session precedente avait DEJA trouve un cotele de
couleur a cet endroit exact du code -- « LE COTELE DU MONDE VENAIT D'ICI » --
corrige depuis par une marge egale a l'amplitude du deplacement.

**TROIS MESURES INDEPENDANTES DISENT NON**, et elles ne peuvent pas toutes se
tromper dans le meme sens :

    sans eclairage (ShowFlag.Lighting 0)   la paroi est un gris UNIFORME
    teinte de roche COUPEE                 les terrasses sont IDENTIQUES
    voxel porte a 2 m                      l'ondulation passe de 1,49 a 6,63

La premiere suffirait : une bande de COULEUR survit a l'extinction des lumieres,
c'est meme ainsi que la session precedente avait nomme la sienne. La troisieme
ferme la porte autrement : un banc sedimentaire fait dix-huit a trente-cinq
metres d'epaisseur, une grandeur METRIQUE qui ne peut pas doubler parce qu'on a
double le voxel.

**MAIS LA QUESTION EN A OUVERT UNE VRAIE.** La teinte de roche change 96,5 % des
pixels de cette vue -- elle peint donc bel et bien la paroi -- et pourtant, sans
eclairage, cette paroi est d'UNE SEULE COULEUR. Le code lit la serie
(`WorldseedStrata::BancAt`), attribue une teinte par roche, et son commentaire
promet « exactement ce qui donne au Grand Canyon ses rayures : les bancs durs
font les corniches, les tendres les talus, et chacun a sa couleur ».

**Ces rayures ne sont pas la.** La stratigraphie est calculee, elle coute dans
l'erosion et dans le sapement, elle est lue par la peinture des sommets -- et
elle n'atteint pas l'ecran. Piste la plus probable, NON VERIFIEE : `Profondeur`
se mesure contre la surface MACRO a l'aplomb du sommet, or sur une paroi le
sommet EST la surface a son propre aplomb, donc la profondeur y vaut zero et la
teinte de roche ne mord jamais -- elle ne sert qu'aux cavites et aux
dessous de surplomb.

Le registre avait deja ecrit, le 19 septembre : « Rien n'a ete REGARDE depuis la
stratigraphie. Les parois rayees sont branchees et compilent ; elles n'ont pas
ete photographiees. » Elles le sont maintenant, et elles ne rayent rien.

### Le decouplage commence : la diffusion sort de l'acteur (23 septembre 2026)

`AWorldseedVoxelTerrain` portait **six responsabilites en 3019 lignes et
46 methodes publiques** -- chargement du monde, diffusion/LOD, maillage,
placement du joueur, acces aux donnees, diagnostic -- et il etait INTESTABLE par
construction : instancier un acteur demande un monde.

**LA MESURE A DECIDE DU PREMIER DECOUPAGE.** Sur les 411 lignes de la diffusion,
elle ne touchait que **six membres** de l'acteur : `FeuillesCourantes`,
`NiveauMax`, `CacheSurface`, `LoadRadiusM`, `DensityRules`, `Density`. Ni
composants, ni travaux en vol, ni pion, ni materiau. `FWorldseedDiffusion` prend
donc un champ, six reglages et une origine.

    WorldseedVoxelTerrain.cpp   3019 -> 2357 lignes
    WorldseedVoxelTerrain.h      893 ->  788

**ET LES DEUX PROPRIETES DE SURETE ONT ENFIN UN ORACLE**, elles qui n'avaient
qu'un avertissement au journal : la diffusion est une PARTITION (0 point couvert
par deux feuilles sur 3000 sondes) et l'ecart de niveau entre voisins est BORNE
A UN (verifie sur 1, 2 et 3 anneaux). Transvoxel ne sait coudre qu'un cran, et
la fissure qui en resulte ne se signale pas : le masque s'arme quand meme et la
geometrie reste combinatoirement close.

**RESTE A DECOUPLER**, dans l'ordre de ce que la revue a mesure :
`BuildGroundProxy` (782 lignes, appartient a `AWorldseedGroundProxy` qui n'en
fait que 99), `RebuildWidget` (666), `HoldOrReleasePlayer` (427) et le placement
du joueur, `Lithology::Compute` (419). Et `WaterBodies` (748 lignes) reste sans
oracle : il demande un monde instancie.

### Deux reliefs, et le joueur enterre : une ecriture de parametre MUETTE (27 septembre 2026)

Signale en jeu : « il y a 2 heightmap j'ai l'impression, mon joueur s'enfonce
dans le sol et ca le fait dans plusieurs autres endroits ». Les deux symptomes
sont le meme defaut, et il etait invisible depuis des jours.

**LA CAUSE.** Le decor d'horizon garde ses sommets a l'altitude VRAIE ; c'est
un DEPLACEMENT DE SOMMETS, dans le materiau, qui le fait passer sous la bande
creusable -- entier pres de la camera, nul au loin (voir « L'horizon etait
NOYE, pas marche », 22 septembre). Cette rampe n'avait ete posee que dans
**`M_WorldseedBiome`**. Or `AWorldseedTerrain::ChooseTerrainMaterial` rend
**`GroundMaterial`** des que l'habillage est un pack de textures -- donc
`MI_WorldseedGround_Orasot`, derive de `M_WorldseedGround`, qui n'a ni sortie
`WorldPositionOffset` ni parametre `NappeRampe*`. Depuis que l'habillage
Orasot est le SEUL (26 septembre), le decor recevait donc un materiau sans
rampe.

**ET RIEN N'A PROTESTE, PARCE QUE `SetScalarParameterValue` EST MUET.** Pose
sur un parametre qu'un materiau ne DECLARE PAS, il ne rend rien, ne journalise
rien, et ne fait rien. Les trois lignes de `ReglerRampeDeLaNappe` sont
devenues des no-op, et le journal a continue d'annoncer « rampe ARMEE » a
chaque partie : **il rapportait ce qu'on avait DEMANDE, pas ce qui avait
pris**. Meme famille que « un repli journalise ressemble a une mesure », et
c'est la troisieme fois que ce depot se fait avoir par un chiffre plausible.

**CE QUE CELA DONNAIT.** Le decor, reste a l'altitude macro et maille a 31 m,
percait le terrain voxel -- lequel s'en ecarte de `overhangAmplitudeM`, HUIT
METRES, plus l'erreur de corde d'une maille de 31 m sur une pente. Assez pour
enterrer un personnage de 1,80 m. Le HUD disait `sol +0 m` parce que la
COLLISION, elle, etait juste : la nappe n'a pas de collision, on marchait sur
le voxel en voyant le decor.

#### L'A/B qui a nomme le coupable en trois captures

Meme point (30523, -3305), meme cap, meme binaire, `-WorldseedDepartExact=1` :

    reference              la camera est DANS une surface lisse, sans vegetation
    -WorldseedParois=0     IDENTIQUE  -> ce ne sont pas les pans de falaise
    -WorldseedSansNappe    le personnage est debout sur la pente, tout est normal

**Un seul levier a change l'image, et c'est celui du sol de fond.** Le depot a
cette note depuis septembre -- « le seul controle qui tranche est de le MASQUER
et de recapturer » -- et elle a servi telle quelle.

#### Trois mesures fausses ou vides en chemin, et il faut les dire

1. **`-WorldseedNappeVue=` N'EXISTE PLUS.** Il a ete retire au menage du
   23 septembre, mais **trois commentaires le citent encore** comme s'il etait
   la, dont un dans un test. Mon A/B « enfoncement 0 contre 400 » a donc
   compare deux fois la meme chose -- et c'est le JOURNAL qui l'a dit, en
   annoncant 125 m des deux cotes. *Une note qui survit a ce qu'elle decrit
   coute plus cher que pas de note*, et le depot le disait deja pour
   `WorldseedLabel`.
2. **LA TOURNEE PHOTO EST DEVENUE IMPRATICABLE.** Lancee pour balayer plusieurs
   terrains d'un coup, elle s'est arretee apres cinq vues et n'a plus rien
   produit pendant treize minutes. Un arret coute un REMPLISSAGE COMPLET du
   diffuseur ; le registre le chiffrait a « une dizaine de minutes » pour un
   rayon de 1200 m, et le rayon vaut 2400 aujourd'hui. Abandonnee.
3. **LE POURCENTAGE DE PIXELS CHANGES NE MESURE PAS CE QU'ON CROIT.** J'ai
   chiffre « 87,3 % du cadre couvert a tort » avant correction, puis mesure
   21,7 % apres au meme point et **89 % a un autre point**. Le second chiffre
   n'est pas une regression : a 308 m d'altitude, retirer le decor retire tout
   le paysage lointain, qui est son ROLE. **Cette metrique melange ce que le
   decor occulte a tort et ce qu'il montre a raison**, et le depot interdit
   deja les agregats sur deux populations. Le critere qui tranche est binaire
   et se lit a l'oeil : **le personnage est-il visible, debout, sur un sol
   vegetalise ?** Avant : non, la camera etait dans la surface. Apres : oui,
   aux deux points.

#### Le correctif : UNE fonction, pas une copie

`MF_WorldseedNappeRampe` porte la rampe -- UV3 pour le plafond par sommet,
distance XY a la camera, `clamp((Fin - d) / (Fin - Debut))`, les trois
parametres, et `(0, 0, -1)` en sortie. `M_WorldseedGround` et
`M_WorldseedBiome` l'appellent tous deux sur `WorldPositionOffset`.

**POURQUOI PAS UNE COPIE DANS CHACUN.** `ChooseTerrainMaterial` peut rendre
TROIS materiaux, et chacun peut habiller le decor. Recopier la formule dans
chacun, c'est reconstruire exactement la divergence qui vient de couter ce
defaut -- et le depot a la regle : *ne jamais recopier une formule dans deux
fichiers*.

**LA PORTE ARRIVE FERMEE**, et il le faut : les chunks du terrain partagent ce
materiau et ne posent AUCUN UV. `NappeRampeActive` vaut zero par defaut ;
seule la nappe recoit une instance dynamique qui l'arme.

#### Le garde-fou, qui est la vraie lecon

`ReglerRampeDeLaNappe` **RELIT** desormais ce qu'elle vient d'ecrire :
`GetScalarParameterValue` rend FAUX quand le parametre n'existe pas sur la
chaine d'instances. A l'echec, une ERREUR nomme le materiau fautif et decrit
le symptome. Le journal dit maintenant *sur QUEL materiau* la rampe a pris :

    sol de fond : rampe ARMEE et RELUE sur MI_WorldseedGround_Orasot --
    entiere jusqu'a 2400 m, nulle au-dela de 4800 m (rayon de vue 2400 m)

**REGLE GENERALE : toute ecriture de parametre de materiau depuis le C++ se
relit.** L'API ne signale pas un nom inconnu, et le defaut qui en resulte est
visuel, diffus, et n'appartient a aucun fichier qu'on penserait a relire.

#### L'oracle, et son temoin

`Worldseed.Nappe.LesMateriauxPortentLaRampe` cree une instance DYNAMIQUE de
chaque materiau d'habillage -- exactement ce que fait le jeu, interroger le
maitre ne prouverait rien sur la chaine d'instances -- et exige que les trois
parametres existent, que la porte arrive ETEINTE, et que la rampe ait une
course (`Fin > Debut`, sans quoi elle enfoncerait AU LOIN au lieu de pres).

**TEMOIN MONTE PUIS RETIRE** : renommer `NappeRampeActive` en
`NappeRampeActiveTEMOIN` dans la fonction fait tomber le test sur
« Expected 'MI_WorldseedGround_Orasot declare NappeRampeActive ... ' to be
true ». Il discrimine.

**ET LE TEMOIN A TROUVE UN SECOND DEFAUT.** Sous ce renommage,
`M_WorldseedBiome` declarait ENCORE `NappeRampeActive` : son ancienne chaine,
devenue orpheline en branchant la fonction, continuait de declarer ses
parametres. **Une chaine morte qui masque un test coute plus cher qu'une
chaine morte** -- elle a ete elaguee, le materiau passe de 25 expressions a 7.

#### UNE ERREUR QUE J'AI FAITE, ET CE QU'ELLE APPREND

**L'elagage a supprime le `RuntimeVirtualTextureOutput` de
`M_WorldseedBiome`.** Mon critere -- « est orphelin ce qui n'alimente aucune
PROPRIETE du materiau » -- est faux : une **sortie personnalisee**
(`UMaterialExpressionCustomOutput` : sortie RVT, sortie d'herbe de Landscape,
rendu de cheveux) n'est branchee sur aucune propriete et n'en est pas moins
TERMINALE. Le noeud a ete remis avec ses deux entrees (couleur de sommet vers
la couleur, position du monde vers la hauteur) et relu.

**Les racines d'un graphe de materiau sont ses proprietes ET ses sorties
personnalisees.** Un elagage qui ne compte que les premieres detruit
silencieusement la moitie d'un graphe -- et un `.uasset` ne se relit pas dans
un diff.

#### Verifie

- **118 oracles verts, 0 rouge** (117 avant, plus le nouveau).
- **Deux points regardes a l'image**, avant et apres : (30523, -3305) sur une
  pente de 45 degres, et (30977, -3508) en savane a 7 degres. Le personnage y
  est debout, pieds au sol, sur un terrain vegetalise.
- **Le journal nomme le materiau** sur lequel la rampe a pris.

#### Reste ouvert

- ~~**Les trois commentaires qui citent `-WorldseedNappeVue=`** designent un
  drapeau supprime. Non corriges.~~ **FAIT, et cette ligne etait elle-meme
  perimee en etant ecrite** : il n'en reste plus qu'UN dans la source
  (`WorldseedNappe.cpp:337`), deja reformule au passe et portant la mesure qui
  a tranche ; la citation du registre, elle, a recu sa marque de peremption.
  Quatre lignes plus haut, ce meme fichier ecrit qu'« une note qui survit a ce
  qu'elle decrit coute plus cher que pas de note » -- une note de TACHE qui
  survit a son execution en est la variante, et c'est la seconde fois de la
  semaine apres la tache du point de naissance et celle du mailleur legataire.
  **Un carnet se remesure avant d'etre execute, et avant d'etre recopie.**
- **La tournee photo ne tient plus** a 2400 m de rayon de vue. Elle est le seul
  outil qui balaie plusieurs formes d'un coup, et elle est hors service.
- **Aucune mesure ne dit OU le decor percait le terrain, a l'echelle du
  monde.** Le mecanisme le dit -- partout ou le voxel descend sous le relief
  macro, donc a peu pres partout -- mais ce n'est pas chiffre. Une sonde qui
  compare la surface voxel au relief macro sur N colonnes le dirait.

