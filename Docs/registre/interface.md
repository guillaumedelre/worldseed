# Registre Worldseed — les ecrans

> **CE FICHIER NE SE LIT PAS EN ENTIER, IL SE CHERCHE.** Il porte le recit des
> seances : ce qui a ete mesure, ce qui a tranche, et surtout **les pistes
> abandonnees**, pour qu'on ne les retente pas. Les REGLES qu'on en a tirees
> vivent dans `CLAUDE.md`, chargé a chaque tour ; ici se trouve la PREUVE.
>
> ```
> grep -n -i '<le mot du sujet>' Docs/registre/*.md
> grep -A40 'le titre exact' Docs/registre/interface.md
> ```
>
> **Le contenu est CUMULATIF.** Certaines notes decrivent un etat qui n'existe
> plus ; les plus dangereuses portent une marque de peremption. Avant d'agir sur
> une note, verifier que ce qu'elle nomme existe encore — une note qui survit a
> ce qu'elle decrit coute plus cher que pas de note.
>
> **Ce fichier** : menu et ecran de configuration, globe, carte plein ecran, minimap, icones, generateur de noms, Slate.

### Se téléporter au bon endroit : la grille de biomes n'est pas la carte (13 septembre 2026)

Vérifier un biome en jeu demande d'y arriver, et trois pièges s'enchaînent.

**1. LA CONVENTION DE LA GRILLE, calibrée et non devinée.** `BP_WorldseedClimat`
lit une grille 128×128 (cellule 6250 cm, demi-étendue 400 000). Sa **ligne 0 est
au SUD** et sa colonne donne X :

    i = (x + 400000) / 6250      j = (y + 400000) / 6250      Grille[j][i]

Les quatre combinaisons possibles ont été testées en jeu sur une même cellule de
taïga : seule celle-là rend 5. **Ne pas déduire cette convention de la carte
PNG** — voir le point 3.

**2. LA TOUNDRA NE VALIDE RIEN, et elle m'a trompé.** J'ai cru ma conversion
bonne parce qu'un point de toundra tombait bien en toundra. Mais **la toundra
existe aux DEUX pôles** : une erreur de signe en Y y tombe quand même. Pour
calibrer un repère, choisir un biome ASYMÉTRIQUE, ou tester explicitement les
variantes.

**3. LA GRILLE CLIMATIQUE PORTE LA DOMINANTE SUR 200 m, PAS LE PIXEL.** Elle
déclare donc des biomes terrestres au-dessus de l'eau proche du rivage, et ce
n'est pas un défaut. Mesure sur 46 sondages en jeu : **72 % d'accord terre/mer,
0 cas de terre émergée déclarée mer, 13 cas de fond marin déclaré terre** — dont
douze à **32 m de profondeur ou moins**, c'est-à-dire le plateau côtier.
**J'ai failli conclure à un désalignement de la grille** ; c'est la distribution
des profondeurs qui a montré qu'il n'en était rien. Pour poser un pion,
demander un point dont la grille dit le bon biome **et** dont le sol est
réellement émergé (`line_trace_single`, Z > 0).

**4. Et le marais est intestable** : 0,01 % des terres, soit **169 pixels dans
tout le monde**, son point le plus intérieur à 3 m d'une frontière. L'habiller
était largement théorique.

### L'atlas se refait par son bloc de données (13 septembre 2026)

`Docs/atlas-worldseed.html` porte un `const D = {...}` qui alimente ses neuf
schémas. Seuls **trois champs décrivent notre monde** et se périment à chaque
régénération : `zonal` (13 bandes de latitude), `biomes` (parts, ocean exclu
mais lacs et rivières INCLUS) et `bandes` (le diagramme de Whittaker, relu depuis
`world_rules.json`). Les champs `reels`, `stations` et `profilTerre` sont des
références terrestres et ne bougent pas.

**Piège à ne pas rater** : la table `couleurs` doit couvrir TOUS les biomes de
`biomes`. La régénération a fait apparaître `Marais` pour la première fois, sans
couleur — le schéma l'aurait tracé en indéfini. Contrôler l'appariement des deux
listes après chaque mise à jour.

Le script de mise à jour est dans le scratchpad de la session ; il se refait en
quelques minutes à partir de `manifest.json`, des trois PNG de climat et des
règles.

### La carte par défaut du projet (13 septembre 2026)

`Config/DefaultEngine.ini` pointait encore `GameDefaultMap` et `EditorStartupMap`
sur `/Game/ThirdPerson/Lvl_ThirdPerson`, la scène du modèle Epic : l'éditeur et
le PIE s'ouvraient donc sur une démo vide.

**ELLES POINTENT AUJOURD'HUI `/Game/Worldseed/Maps/L_Menu`**, et non
`L_Worldseed` comme cette note l'a longtemps dit : le menu est devenu le point
d'entrée du jeu quand la graine y est choisie, et c'est lui qui transporte le
monde jusqu'à `L_Worldseed_Proc`. Corrigé le 20 septembre 2026, après
vérification dans le fichier.

`Lvl_ThirdPerson` a été supprimée, avec ses acteurs externes — vérifié sans
aucun référenceur hors de ses propres `__ExternalActors__`. **`GlobalDefaultGameMode`
reste `BP_ThirdPersonGameMode`** : ce Blueprint porte notre pion et nos entrées,
il ne faut pas le confondre avec la carte de démonstration.

### L'ecran de configuration : une seule taille, et le globe au centre (20 septembre 2026)

Quatre demandes du proprietaire dans la meme session, et chacune a decouvert un
defaut que personne ne cherchait.

**LE MENU DES TAILLES EST SUPPRIME. Sept tailles etaient offertes, et elles ne
se valaient pas.** Les regles de FORMES sont metriques et grandes -- maille de
tirage des diaclases 4 km, longueur d'onde des regions de tables 3,6 km,
espacement des arches 2,5 km -- tandis que ce depot avait deja chiffre le seuil
ou l'intersection de criteres rares cesse d'etre une **loterie sur la graine**
pour redevenir une proportion : environ cent vingt taches.

    64 x 32 km  ->  128 mailles de tirage
    32 x 16 km  ->   32
    16 x  8 km  ->    8
     2 x  1 km  ->  moins d'une : le monde entier tient dans UNE maille

C'est donc la seule taille ou les diaclases, les tables et les canyons sont
statistiques et non tires au sort, la seule ou tout le calage est fait, et la
seule ou le relief est credible -- 1713 m de sommet contre 531 a 16 km.
**Et les petites tailles ne sont pas perdues** : leur usage reel etait d'iterer
vite, et les sondes comme le banc prennent deja une taille en parametre. Cette
capacite ne dependait pas du menu, et elle lui survit.

**`world.sizeKm` RESTE A 8**, et il ne faut pas le confondre avec la taille du
monde : c'est la HAUTEUR DE REFERENCE du calage metrique, dont `VerticalScale`
tire son rapport. Les deux se sont longtemps trouvees egales, ce qui masquait
la distinction.

**TROIS DEFAUTS DE L'ECRAN, TOUS VUS A L'IMAGE ET SEULEMENT A L'IMAGE :**

- **`PackHint` n'avait AUCUNE taille de police**, donc vingt-quatre points par
  defaut. Cette note discrete faisait six lignes de titre sous la liste. Le
  defaut etait ANCIEN ; il ne se voyait pas tant que la colonne etait large, et
  il est devenu le plus gros bloc de l'ecran des qu'elle est passee a 300.
  **Un `UTextBlock` construit directement, sans passer par le fabricant commun,
  echappe a tous les reglages de l'ecran.**
- **La graine s'affichait « 1,337 » pour 1337.** `FText::AsNumber` y mettait le
  separateur de milliers de la locale, et la relecture le refuse :
  `Raw.IsNumeric()` rend faux sur la virgule, donc le champ etait **IGNORE en
  silence**. Le monde partait juste parce que `Params.Seed` portait deja la
  bonne valeur -- mais taper une graine avec un separateur ne prenait pas. Une
  graine est un IDENTIFIANT, pas une quantite.
- **La piste vide des barres de biomes etait peinte en clair** : 0,2 % et
  16,9 % avaient la meme longueur APPARENTE, une piste pleine largeur avec un
  lisere colore dedans. Ce qu'on compare est la LONGUEUR de la couleur.

**LA BARRE EST RAPPORTEE AU PLUS GRAND BIOME, PAS A CENT.** Rapportee a cent,
la premiere barre ferait un cinquieme de la largeur et les dernieres seraient
invisibles. Le chiffre reste absolu -- il porte la valeur ; la barre ne porte
que la comparaison. Et **elle prend la couleur du biome, celle-la meme que le
globe peint a cote** : on lit un pourcentage, on leve les yeux, et on voit ou
il se trouve.

**REVENIR AU MENU : UN SOUS-SYSTEME, PAS UNE ACTION D'ENTREE.** Le chemin
normal d'Unreal -- un asset Enhanced Input, ou une fonction Exec sur le
PlayerController -- exige de toucher a `Content/*`, qui est exclu du depot par
`.gitignore`. Un binding pose la ne partirait pas sur GitHub, et ce depot a
deja perdu des reglages pour cette raison exacte (le PlayerStart qui revenait a
sa position, les acteurs d'eclairage disparus entre deux sessions).
`UWorldseedRetourMenu` est un `UTickableWorldSubsystem`, il ne s'arme que dans
`L_Worldseed_Proc`, et il se desarme AVANT d'ouvrir -- sans quoi deux tics de
plus empilent deux `OpenLevel` sur la meme pression.

**LES ETAPES AFFICHEES PENDANT LA GENERATION DISAIENT FAUX.** Cinq passes
n'avaient aucun libelle et retombaient sur celui de la passe precedente, et les
deux passes de climat portaient le meme. Un libelle d'avancement qui ment est
pire que pas de libelle : on croit savoir ou en est la chaine.

### Le globe saccadait : deux causes, et une seule etait la bonne (20 septembre 2026)

Signale : « attention le globe saccade dans sa rotation !! ». C'etait un vrai
defaut, et le diagnostic a trouve DEUX choses -- dont une seule expliquait le
symptome.

**LA CAUSE REELLE : UN PAS DE ROTATION FIXE SUR UN TIMER IRREGULIER.** Le code
avancait de `vitesse x periode` a chaque declenchement. Or **un timer d'Unreal
est servi PAR LE TICK DU MONDE** : il ne peut pas battre plus vite que la
trame, et il RATTRAPE quand il a pris du retard -- deux declenchements dans le
meme tick. Releve des intervalles REELS pour une periode demandee de 16,67 ms :

    min 1,58 ms   mediane 16,66   p95 18,15   max 302,32

Le globe avancait donc du MEME angle sur 1,6 ms que sur 302. Le remede ne
change pas la vitesse moyenne, il la rend constante : multiplier par le temps
ECOULE (`FPlatformTime::Seconds()`), avec un plafond pour qu'un arret du jeu ne
fasse pas faire un tour complet au retour.

**LE SECOND DEFAUT ETAIT REEL MAIS SANS RAPPORT, et c'est la lecon.** Le releve
disait « rendu par le processeur » alors que `M_WorldseedGlobe` existe. Cause :
`BakeGlobe()` etait appele UNE LIGNE AVANT que `WorldGeometry` soit posee, et il
commence par comparer `CachedHeights.Num()` a `WorldGeometry.CellCount()` -- il
confrontait donc les altitudes du NOUVEAU monde a la geometrie du PRECEDENT,
sortait a sa premiere garde, et **cette garde sort sans un mot**. Le materiau
n'etait pas un choix, il etait inatteignable depuis toujours.

**J'EN AI CONCLU QUE LE PROCESSEUR COUTAIT LA SACCADE. C'ETAIT FAUX**, et le
chronometre l'a dit :

    voie          redessin     aspect
    materiau      0,001 ms     ocean presque noir, bandes de latitude opaques
                               et larges, relief sans ombrage
    processeur    0,224 ms     bathymetrie, relief ombre, cercles fins

0,224 ms, c'est **1,4 % d'un budget de 16,67**. Le lance-de-rayon ne coutait
rien de perceptible et rendait nettement mieux : il reste le DEFAUT, et
`-WorldseedGlobeGPU` rearme la voie graphique. **Chronometrer la voie soupconnee
AVANT de la remplacer aurait evite le detour** -- la regle existait deja,
« mesurer avant de corriger, meme quand l'hypothese est seduisante ».

**PIEGE DE MESURE PAYE AU PASSAGE : le premier releve a mesure les trois
premieres secondes**, pendant lesquelles le monde n'existe pas encore et
`RedrawGlobe` sort a sa premiere garde. Il annoncait « rendu par le processeur »
pour un globe qui ne tournait pas. **On mesure le traitement quand il a lieu,
jamais avant** -- le releve ne demarre plus qu'une fois `CachedHeights` rempli.

**ET L'ESPACE AUTOUR DU GLOBE ETAIT OPAQUE.** L'alpha de la texture etait fige a
255 : le disque portait un CARRE de fond. Invisible a 420 pixels dans un panneau
de meme teinte, c'est devenu une boite posee sur l'ecran des que le globe a pris
tout le corps. Fondu sur un pixel au bord, sans quoi un disque de sept cents
pixels montre son escalier.

### Choisir ou naitre : trois pieges, dont deux dans l'outillage (21 septembre 2026)

Le globe du menu laisse desormais CHOISIR le point de depart, au clic ou dans
une liste de lieux remarquables. Le chantier tenait en peu de code -- tout
existait -- mais il a paye trois pieges qui resserviront.

**UN ACTEUR QUI LIT L'INSTANCE DE JEU NE LA LIT PEUT-ETRE JAMAIS.**
`AWorldseedVoxelTerrain::LoadWorld` interroge bien le `GameInstance`, et j'y ai
branche la lecture du depart. Elle n'a jamais servi : **ce chemin n'est pris
que si PERSONNE ne donne de monde au voxel**, c'est-a-dire un PIE lance depuis
l'editeur sans passer par le menu. Des que `L_Menu` a joue, c'est
`AWorldseedTerrain` qui consomme l'instance et transmet par `AdoptWorld`. Rien
ne le signalait : le monde arrivait juste, et le joueur naissait ailleurs.
**Le journal, lui, le disait en deux lignes** -- « monde repris du TERRAIN »
puis « terre emergee la plus proche a (8, 8) m », soit une recherche partie de
l'origine alors que le menu avait demande (-2766, 2297). Devant une donnee qui
« ne passe pas », lire le journal pour savoir QUEL chemin a ete pris avant de
relire le code du chemin qu'on croit pris.

**`PrintWindow` SANS `PW_CLIENTONLY` DESSINE LA BARRE DE TITRE**, dans un
bitmap qu'on a dimensionne sur le CLIENT. L'image est donc decalee d'une
trentaine de pixels vers le bas par rapport aux coordonnees client, et tout
clic vise d'apres elle tombe trop haut. **Le defaut est invisible tant qu'on ne
fait que REGARDER** : il ne mord que le jour ou l'on POINTE. Le drapeau vaut
`1 | 2` -- `PW_CLIENTONLY | PW_RENDERFULLCONTENT`, le second restant
indispensable pour une surface Direct3D.

**NE PAS PILOTER LA SOURIS PENDANT QUE LE PROPRIETAIRE MANIPULE L'ECRAN.** J'ai
passe plusieurs essais a expliquer des « glissers parasites » par la mise en
veille des fenetres non focalisees -- hypothese plausible, chiffree (32 % des
pixels du globe changes entre le reperage et le clic), et FAUSSE. C'etait lui
qui tournait le globe. Avant d'injecter des clics, le dire, ou lui laisser la
main et se contenter de lire le journal.

**Trois points de conception qui valent d'etre gardes :**
- le CLIC se distingue du glisser par le chemin **CUMULE**, jamais par l'ecart
  entre depart et arrivee : un aller-retour revient a son point de depart et
  serait declare clic ;
- `SetSelectedIndex` sur une liste deroulante **declenche le gestionnaire**, et
  ramener la liste a son entree vide apres un clic effacerait le choix qu'on
  vient de poser. Slate distingue les deux : `ESelectInfo::Direct` vient du
  code, `OnMouseClick` de la main ;
- une ROTATION AUTOMATIQUE devient une cible mobile des qu'on pointe. A cinq
  degres par seconde, la meme terre n'est plus sous le curseur deux secondes
  plus tard. Elle s'arrete a la premiere prise en main.

**ET PARTAGER UNE FORMULE GARANTIT L'ACCORD, PAS LA JUSTESSE.** Le rendu du
globe, le pointage et le reticule appellent le meme code (regle du depot), mais
une projection partagee et fausse serait partagee et fausse. D'ou
**`ProbePointage`**, un aller-retour : latitude et longitude connues, projetees
vers l'image puis reinversees. Hors limbe, 0,000069 degre sur 25 158 points et
zero incoherent ; le partage presque exact entre face visible (25 158) et face
cachee (25 242) valide au passage le test de visibilite. **Le limbe est mal
conditionne PAR NATURE** -- la derivee de l'arc sinus y diverge -- d'ou deux
chiffres rendus, celui du disque entier et celui du disque franc, plutot qu'un
seul qui flatterait ou accablerait.

### La carte de `ProbeCarte` a sa LIGNE 0 AU SUD (22 septembre 2026)

Pour choisir un point de vue depuis la carte -- un sommet qui domine la mer,
par exemple -- il faut savoir la lire. Deux conventions, et se tromper met
tous les reperes en pleine mer.

    U = X / 64000 + 0.5        colonne = U * largeur
    V = Y / 32000 + 0.5        ligne   = V * hauteur      <- ligne 0 au SUD

**LA LONGITUDE EST LINEAIRE EN U, LA LATITUDE NE L'EST PAS EN V.** La carte
est EQUIVALENTE-AIRE : `lat = asin(2V - 1)`. Verifie sur un point releve en
jeu : (2262, -3842) donne U 0,535 -> 12,7 E et V 0,380 -> asin(-0,240) =
13,9 S, exactement ce que le releve affiche. **Convertir V en latitude par un
produit donnerait 21,6 S au lieu de 13,9.**

**ET LE SENS VERTICAL NE SE DEDUIT PAS, IL SE MESURE.** Les deux poles sont
blancs, donc une calotte ne tranche rien -- meme piege que la toundra qui
existe aux deux hemispheres, deja consigne. Le controle qui tranche est un
point dont on connait le TERRAIN en jeu :

    temoin    (2262, -3842)  Savane sur Granite
      ligne 0 = nord -> R 48 G 80 B124   ocean      FAUX
      ligne 0 = sud  -> R128 G152 B 70   vert olive JUSTE
    belvedere (930, -5758)   Roche nue sur Gres
      ligne 0 = nord -> R117 G146 B174   ocean      FAUX
      ligne 0 = sud  -> R214 G190 B126   sable      JUSTE

**CE QUE LA CARTE PERMET, ET QUI VALAIT LE DETOUR** : choisir un point de vue
par CALCUL au lieu de le chercher en relancant le jeu. Balayer huit caps
depuis un sommet et mesurer la distance a la premiere cellule bleue donne
d'un coup la direction ou regarder -- « mer a 1 000 m au NE » -- et un profil
le long de ce cap dit si la mer reste ouverte au-dela de la couture. Trois
relances economisees par point de vue.

Les altitudes utilisables se lisent dans le journal sans rien recalculer : la
passe des cavites journalise chaque DOLINE et chaque GOUFFRE avec sa position
ET son altitude. C'est la qu'on trouve un sommet a 662 m.

### La minimap, et pourquoi une capture de scene n'avait aucune chance (23 septembre 2026)

Demande : partir du tutoriel Epic « Complete Map and Mini-map » et l'adapter,
avec les points cardinaux et le cone de visee en plus. Le tutoriel est
entierement en Blueprint ; rien de ce qu'il montre ne se transpose ici, et pour
une raison qui n'est pas de style.

**UNE CAMERA ORTHOGRAPHIQUE NE VERRAIT PRESQUE RIEN.** Le terrain voxel n'existe
que dans `rayonChargementM` -- 2400 m -- et au-dela c'est la nappe d'horizon,
enfoncee sous la bande creusable et sortie du rendu principal depuis le
22 septembre. Elle filmerait un disque de terrain pose sur un decor deforme, et
couterait une passe de rendu par image. **Le monde, lui, est DEJA EN MEMOIRE** :
`ProbeCarte` savait deja le peindre. Il ne manquait qu'une fenetre glissante.

**LA TOURNEE PHOTO NE PEUT PAS PHOTOGRAPHIER UN WIDGET, ET CE N'EST PAS UN
REGLAGE.** `WorldseedPhotographe.cpp:767` appelle `RequestScreenshot(fichier,
false, false)`, et le deuxieme parametre est `bInShowUI` (`UnrealClient.h:219`).
A faux, le moteur lit la **cible de rendu du viewport**, alors qu'un widget pose
par `AddViewportWidgetContent` est composite APRES, dans le back-buffer. Deux
consequences :
- le commentaire de `WorldseedFpsOverlay.cpp:140-148` -- « la tournee photo doit
  pouvoir eteindre le compteur, sans quoi il se retrouve sur toutes les vues » --
  est **perime**. Ce qui s'y retrouvait, ce sont les messages moteur via
  `AddOnScreenDebugMessage`, qui passent par le debug canvas ; le `STextBlock`
  du compteur n'y a jamais ete ;
- pour juger une interface, on **capture la fenetre** par `System.Drawing` depuis
  PowerShell -- la voie deja employee pour l'ecran de configuration --, avec
  `PrintWindow(h, dc, 3)` : `PW_CLIENTONLY | PW_RENDERFULLCONTENT`. Sans le
  premier, la barre de titre entre dans un bitmap dimensionne sur le CLIENT et
  tout glisse d'une trentaine de pixels.

**TROIS CONVENTIONS DE CELLULE COEXISTAIENT, ET DEUX SE CONTREDISAIENT D'UNE
DEMI-CELLULE.** `WorldseedPeinture` tronque (`Floor`), `ReleveJoueur` arrondissait
(`RoundToInt`) : huit metres d'ecart sur la grille du jeu, donc **un biome NOMME
qui n'etait pas le biome PEINT sous les pieds**. Le defaut ne se voyait pas parce
que rien n'affichait les deux cote a cote ; une minimap le fait. Et le
commentaire disait « au plus proche voisin » en faisant l'inverse : la cellule k
couvre `U * NX` dans [k, k+1[, donc son centre est en k + 0,5 -- c'est `Floor`
qui designe le centre le plus proche, `RoundToInt` designe le BORD. **Une
formule fausse sous un commentaire juste est le pire des deux mondes.** La
convention vit desormais dans `FWorldseedGeometry::CelluleDepuisMetres`.

**`FMath::Frac` EST BASE SUR `Floor`, `FMath::Fractional` SUR `Trunc`** -- le
second rend du negatif pour un X negatif, donc un index hors du tableau. Les
deux noms se ressemblent, un seul convient.

**LE TAMPON DE `UpdateTextureRegions` NE DOIT JAMAIS ETRE UN MEMBRE.** Le fil de
rendu le lit APRES le retour de la fonction : un tampon reutilise serait reecrit
sous ses yeux, et cela marcherait quatre-vingt-dix-neuf fois sur cent -- la pire
facon d'echouer. Chaque peinture alloue le sien, libere par le rappel. Et
`UpdateTextureRegions`, jamais `UpdateResource`, qui detruit et recree la
ressource RHI a chaque appel.

**UN LISERÉ SE MESURE EN PIXELS, PAS EN FRACTION D'ANGLE.** Celui du cone valait
30 % du demi-angle : a 45 degres d'ouverture cela faisait treize degres de chaque
cote, le CORPS ne commencait qu'au tiers, et a 28 % de blanc il disparaissait sur
du sable comme sur de l'eau claire. **On lisait deux aretes au lieu d'un cone**,
et seule l'image l'a dit -- aucun test ne voit cela.

**UN ATTENDU NAIF SE CORRIGE, IL NE S'ELARGIT PAS.** Le test du disque comparait
son aire a pi/4 = 0,785. Mesure : 0,738. C'est l'attendu qui avait tort -- le
disque est trace au rayon `Res/2 - 0,5` pour que son fondu tienne dans le tampon,
et le seuil `alpha > 127` coupe ce fondu en son milieu. Rayon effectif
`Res/2 - 1`, soit 0,7375 : huit dix-millemes du mesure. Elargir la tolerance
aurait fait passer le test sans rien apprendre.

**LE CONTROLE D'ORIENTATION QUI N'EST PAS AUTO-REFERENTIEL.** Mes tests
comparent la carte a elle-meme ; le seul qui la confronte au MONDE 3D est de
MARCHER. `-WorldseedBanc -WorldseedMarche=90 -WorldseedMarcheCap=0`, deux
captures espacees : en allant au nord, le lac et la cote DESCENDENT dans le
disque. Le depot a deja paye une mesure auto-referentielle -- `ProbeTransvoxel`
comparait les triangles a leurs propres normales et a fait poser l'enroulement a
l'envers.

**ET LE CONE SUIT LA CAMERA, PAS LE PION.** Deja tranche pour le sol de fond ; la
marche au nord camera tournee vers l'est le montre en une image.

**CE QUE LES TESTS ONT TRANCHE EN PASSANT** : `LEnroulementNeCoupePasAuMeridien`
passe, donc la discontinuite verticale visible sur une fenetre a cheval sur le
meridien de bordure ne vient PAS de la peinture -- **le monde lui-meme porte une
couture a son meridien**. A reprendre a part.

**RESTE OUVERT** : la carte plein ecran, qui reutilisera `PeindreFenetre` mais
demandera un second mode d'echantillonnage -- a 1024 px pour 64 km, c'est quatre
cellules par pixel, ou le plus-proche-voisin scintille et perd des iles. D'ou le
`MetresParPixel` deja present. Moyenne de bloc pour l'altitude, vote majoritaire
pour les identifiants : **on ne moyenne jamais un identifiant**, 307 points faux
sur 17956 deja payes.

### La carte plein ecran, et une police d'icones pour tout le projet (23 septembre 2026)

La carte est livree : Tab l'ouvre, la molette zoome, le glisser deplace, le clic
pose un repere et Ctrl+clic y teleporte. Cuisson **4096 x 2048 en 27 a 46 ms**,
une fois par partie, **220 images par seconde** la carte ouverte -- elle ne
repeint rien, elle deplace une region UV.

**LA QUESTION OUVERTE PLUS HAUT EST TRANCHEE, ET DANS L'AUTRE SENS.** Le second
mode d'echantillonnage annonce n'a pas lieu d'etre : cuite a la RESOLUTION DE LA
GRILLE, la carte a **un pixel par cellule**, donc elle n'agrege rien et ne peut
perdre aucune ile. L'agregation reste ecrite, et elle sert -- mais a la MINIMAP.

**UN DEFAUT VIVANT TROUVE EN CHEMIN.** Le cran de 6 km de la minimap prend
46,9 m par pixel pour une maille de 15,6 : **trois cellules par pixel en
prelevement ponctuel**, donc un relief qui fourmille en marchant et des iles qui
clignotent. Et `FondDuMonde` y etait rebalaye a CHAQUE repeinture -- 8,4 millions
de flottants tous les quinze metres parcourus.

**`Geo.MetersPerPixel()` N'EST PAS LA TAILLE D'UNE CELLULE.** Elle vaut
`HeightM / (NY - 1)` : l'espacement des NOEUDS, celui qu'interpole `SampleUV`.
Une CELLULE, au sens de `CelluleDepuisMetres` -- la convention unique du sol --
vaut `HeightM / NY`. L'ecart est de 0,05 % sur la grille du jeu et de **6,7 %**
sur celle des tests : assez pour qu'un bloc de deux cellules en lise quatre.
C'est l'oracle qui l'a dit, du premier coup.

#### Une carte du monde ne se cale pas sur la HAUTEUR de l'ecran

**Signale a l'image apres la livraison : « la projection de la carte est
coupee ».** C'etait exact, et le journal le chiffrait : 29,6 m par pixel sur un
widget de 1921 px font **56 862 m visibles pour un monde qui en fait 64 000**.
Onze pour cent de la largeur restaient hors champ QUOI QU'ON FASSE, et le centre
etait colle a sa borne -- il n'existait aucun reglage permettant de voir le
monde entier.

**Le monde est en 2:1, un ecran en 16:9.** Caler l'echelle sur la hauteur
remplit l'ecran et deborde forcement en largeur ; le dezoom maximal se cale donc
sur le cote le plus CONTRAIGNANT des deux -- un maximum, pas la hauteur.

**ET LA CARTE NE REMPLIT ALORS PLUS L'ECRAN, ce qui est la contrepartie
necessaire.** Il reste des bandes en haut et en bas, et les remplir de texture
etirerait la derniere ligne en bavures. On dessine donc la carte dans le
rectangle du monde REELLEMENT visible -- l'intersection de la vue et du monde --
sur un fond qui porte la couleur du hors-monde du peintre. Les deux coins de ce
rectangle passent par la MEME projection, deux fois : avec la fenetre de vue
pour savoir ou dessiner, avec celle de la cuisson pour savoir quels texels y
mettre.

Apres correction : 33,3 m/px, centre (0, 0), le monde bord a bord.

**LA LECON DEPASSE LA CARTE** : des que deux rapports d'aspect se rencontrent,
« remplir » et « tout montrer » s'excluent. Il faut choisir, et le dire. Aucun
test ne l'aurait vu -- la projection etait juste, l'echelle aussi ; c'est le
CADRAGE qui etait faux, et cela ne se voit qu'a l'image.

#### Slate : cinq murs, tous silencieux

- **`FInputModeUIOnly` coupe `WasInputKeyJustPressed`**, dont dependent la
  minimap ET le retour au menu. On reste en `GameAndUI` et l'on ferme la camera
  par `SetIgnoreLookInput` / `SetIgnoreMoveInput` -- **ce sont des COMPTEURS** :
  un `true` de trop laisse le pion gele, et le defaut survit a la carte.
- **Le tiling d'une brosse et une region UV personnalisee s'EXCLUENT.** Des que
  `ESlateBrushTileType` pose `TileU` (`ElementBatcher.cpp:762`), le batcher
  recalcule les UV depuis la taille locale et ignore la region. C'est aussi lui,
  et non `AddressX`, qui decide de l'enroulement. La carte est donc bornee en
  longitude ; l'enroulement demandera deux images cote a cote.
- **Une `FBox2f` construite par defaut est INVALIDE**, et le batcher teste
  `bIsValid` avant de la lire (`ElementBatcher.cpp:741`). « Toute la texture »
  s'ecrit par ses deux coins, jamais `FBox2f()`.
- **`TAttribute<T>::CreateUObject` n'existe pas**, malgre la symetrie avec les
  delegues : c'est `MakeAttributeUObject`. Et l'on ne declare pas un type
  IMBRIQUE (`SConstraintCanvas::FSlot`) en avant -- d'ou le padding lie.
- **Le widget fait 1921 x 1080 pour une fenetre de 1600 x 900**, parce que Slate
  travaille en pixels LOGIQUES et que le facteur DPI valait 0,83. Melanger les
  deux donne une echelle fausse d'un facteur DPI. Toute borne de zoom se calcule
  en pixels REELS, et l'on borne la vue **avec la geometrie qui sert a peindre**,
  jamais avec celle du viewport.

#### La police d'icones

Material Symbols, 4284 icones, 10,2 Mo en LFS, chargee par
`FStandaloneCompositeFont` -- la voie que le moteur emploie pour ses ecrans de
chargement (`PreLoadSettingsContainer.cpp:240`), les constructeurs de
`FSlateFontInfo` qui prennent un chemin etant deprecies depuis 5.6. **C'est ce
que fait Epic** : l'editeur rend ses icones avec `FontAwesome.ttf` et une table
de glyphes nommes (`EditorFontGlyphs.h`).

- **Un `.ttf` n'est pas un `.uasset` : rien ne l'embarque dans un build cuit**
  sans `+DirectoriesToAlwaysStageAsUFS`. Tout marche alors dans l'editeur et en
  PIE, et les icones disparaissent du SEUL build final.
- **Plusieurs noms partagent un glyphe** : `place` et `location_on` valent tous
  deux F1DB, `landscape` et `terrain` tous deux E564.
- **Un codepoint se recopie a la main depuis un fichier de 4284 lignes**, et une
  faute d'un chiffre donne un AUTRE glyphe, parfaitement dessine. L'oracle les
  confronte au catalogue livre ; temoin monte puis retire, il tombe sur
  « Expected 61915, but it was 61916 ».
- **Choisir un glyphe se fait A L'IMAGE.** `door_open` pour une arche rendait un
  rectangle barre, illisible a vingt-deux pixels ; `all_inclusive` -- deux arcs
  et un vide -- se lit du premier coup. Et **une epingle designe par sa POINTE**,
  pas par son centre : centree, elle montre un point dix pixels plus bas.

#### Quatre lecons de mesure

- **UNE ESTIMATION DE COUT NE VAUT PAS UNE MESURE.** La cuisson etait annoncee a
  150 ms, sur un raisonnement soigne et une source qui n'existait pas dans le
  depot. Mesure : **27 ms**. Toute une branche du plan -- fil de travail, capture
  de pointeur partage, ecran d'attente -- tombait avec ce chiffre. La sonde a ete
  ecrite AVANT la premiere ligne de Slate, exprès pour cela.
- **SANS PYRAMIDE DE MIPS, LE LISERE DE COTE SORT POINTILLE.** La carte est cuite
  a 4096 et affichee dans 1900 pixels ; le GPU preleve alors un texel sur deux et
  les ilots se brisent. Moyenner des COULEURS DEJA PEINTES n'est pas moyenner un
  identifiant -- c'est ce que fait un mipmap -- et la moyenne se fait en sRGB
  DELIBEREMENT : en lumiere, un trait tres sombre ne pese presque rien et le
  lisere disparaitrait.
- **L'ECART AGREGE ENTRE DEUX IMAGES NE TRANCHAIT RIEN** : 48,8 % de pixels
  differents a 2,13 de reduction... contre 43,0 % a 1,07, ou il ne devrait
  presque rien se passer. La mesure melangeait l'effet de la reduction et le
  contraste propre de l'image. Seul le recadrage agrandi, regarde a l'oeil, a
  departage.
- **UN TEMOIN DONT LE VERDICT DEPEND DE L'ENDROIT CHOISI NE TEMOIGNE DE RIEN.**
  Celui de la moyenne de bloc comparait un bloc a son centre, et il a echoue --
  non parce que la moyenne etait fausse, mais parce que les trois harmoniques du
  relief de la fixture s'y compensent a moins d'un metre. Il prend desormais le
  pire ecart sur toute la grille. La tolerance, elle, n'a pas bouge.

#### Deux pieges d'outillage

- **`-run=Automation` n'existe pas** : « Failed to find commandlet class
  AutomationCommandlet ». C'est `-ExecCmds="Automation RunTests X;Quit"` seul.
- **PILOTER LE CLAVIER ET LA SOURIS D'UNE MACHINE OU QUELQU'UN TRAVAILLE NE
  MARCHE PAS.** La premiere verification par injection a parfaitement fonctionne
  -- clic, molette, Tab, retour au jeu. La seconde n'a RIEN recu : une autre
  fenetre avait le focus, `SetForegroundWindow` a echoue, et l'image montrait une
  carte qui ne se fermait pas -- ce qui ressemble trait pour trait a une
  regression alors que rien n'avait bouge. **Le journal tranche en une ligne**
  (pas de « repere pose »), et la parade est une surcharge de ligne de commande,
  comme tout le reste du harnais.

**RESTE OUVERT** : l'enroulement de la carte en longitude (deux images), et une
legende pour les cinq genres de marqueurs. Les 316 gouffres et dolines ne
s'affichent qu'au-dela de 12 m par pixel -- a l'echelle du monde, ils feraient un
voile gris.

### Le generateur de noms : cinq outils compares, un seul utilisable (26 septembre 2026)

Le generateur grammatical porte du projet Godot a ete REMPLACE, sur decision
du proprietaire, par le moteur d'**Azgaar's Fantasy Map Generator** -- chaine
de Markov a pseudo-syllabes, **43 bases, 9 194 mots**, licence MIT.

| outil | licence | verdict |
|---|---|---|
| **Azgaar FMG** | **MIT**, bases ecrites par l'auteur | **retenu** |
| js.fantasy-names | MIT **usurpe** | ecarte, voir ci-dessous |
| fantasy-name-api | **aucune** | ecarte : sans licence = tous droits reserves |
| fantasygen | ISC, mais 7 dicos thematiques | sans interet |
| nameforge | MIT, mais plugin Obsidian | sans interet |

**LE POINT QUI TRANCHE EST JURIDIQUE, PAS ESTHETIQUE.** `js.fantasy-names` est
techniquement le plus riche des cinq -- 907 generateurs, 10,6 Mo, dont 116
pour les lieux -- mais son contenu est un SCRAPE de fantasynamegenerators.com,
dont les conditions sont explicites : « All other original content [...]
cannot be copied, sold or redistributed without permission », et seuls les
NOMS PRODUITS sont libres d'emploi. Le depot redistribue precisement ce qui ne
peut pas l'etre, sous une etiquette MIT qu'il n'avait pas a donner. Worldseed
est public : l'embarquer nous mettrait en tort. **Une licence affichee sur un
depot ne dit rien des droits sur son CONTENU.**

**CE QU'ON PERD, ET IL FAUT LE SAVOIR.** La grammaire remplacee produisait des
SYNTAGMES francais -- « les Marches de Silael », « Villey-sur-Ance », « le
Golfe de Port-Rouge » -- et accordait le patronyme nordique (« Ragnhild fille
de Sigurd » contre « Torvald fils de Hakon »). Combinatoire mesuree avant
remplacement : **39 187 noms de region, 4 458 villages, 1 691 910
personnages**. Azgaar rend un MOT, et ses noms d'Etat se font par suffixe
agglutine (`-ia`, `-land`, `-terre`, `-maa`, `-orszag`, « Guo ») et non par
article. Le fichier est garde inactif en
`Content/Worldseed/Data/noms-grammaire.json`.

**LE PORTAGE EST FIDELE Y COMPRIS DANS SES BIZARRERIES**, et chacune est
signalee dans le code. La plus notable : une ligne du decoupage en syllabes
compare un BOOLEEN a une CHAINE, donc elle est TOUJOURS FAUSSE en JavaScript
strict -- le portage TypeScript d'Azgaar a du la caster pour compiler et son
commentaire la nomme « original quirky behavior ». La rendre « juste »
changerait le decoupage de tous les corpus, donc tous les noms du jeu.

**⚠ L'INDEX D'ORIGINE DE CHAQUE BASE EST PORTANT.** Le systeme de suffixes
d'Etat teste des NUMEROS -- `2` pour le francais, `> 32 && < 42` pour les onze
bases de fantasy, qui n'en recoivent aucun. Renumeroter ne casserait rien a la
compilation : les noms sortiraient, simplement avec les mauvaises
terminaisons. Un oracle le garde.

**ET LES 32 BASES REELLES SONT DES CORPUS DE TOPONYMES** -- « Achern »,
« Aichhalden » sont des communes allemandes. Un « personnage » allemand sort
donc « Kellingen Openalbhau », qui sonne comme deux villages. C'est le
comportement de l'original, sans consequence pour Worldseed dont le besoin
porte sur les LIEUX ; a savoir le jour ou l'on nommera des gens.

### Le globe, les frontieres et les noms : cinq defauts, une seule famille (26 septembre 2026)

Session ouverte sur « il y a un soucis avec comment est projete la texture du
monde sur le globe [...] pourrait-elle etre re-echantillonnee pour matcher le
niveau de zoom ? ». Le proprietaire proposait de cuire une texture
equirectangulaire. **La mesure a montre que ce n'etait pas necessaire**, et
elle a trouve en chemin quatre autres defauts que personne ne cherchait.

#### UN COUT CONSTANT TRAHIT UN TRAVAIL PAR CELLULE DANS UNE PASSE PAR PIXEL

`FillPixels` recalculait l'altitude maximale du monde a CHAQUE redessin, par
un parcours complet du relief : 8,4 millions de flottants, 33 Mo, relus
soixante fois par seconde pour retrouver un nombre fige depuis la generation.

**LE SIGNE QUI L'A DEMASQUE.** Le surcout du relief plein valait **+4 ms a
512 comme a 1024** -- il ne dependait donc PAS du nombre de pixels dessines.
Un cout constant ne sort pas d'une boucle par pixel. C'est un signe a ranger
a cote des deux autres que ce depot connait : « deux mesures identiques pour
deux reglages differents » et « frame_ms vaut EXACTEMENT gpu_ms ».

    texture   relief         avant      apres
    512       reduit 1024    1,138 ms   0,913 ms
    512       PLEIN 4096     5,101 ms   1,135 ms
    1024      PLEIN 4096     8,115 ms   4,152 ms
    2048      PLEIN 4096    19,446 ms  15,759 ms

**ET CE CHIFFRE A ANNULE UNE OPTIMISATION ENTIERE.** `BuildPreviewField`
reduisait le relief de 4096 x 2048 a 1024 x 512 EN PERMANENCE, au motif que
« le globe fait cinq echantillonnages bilineaires par pixel [...] a 32 Mo
elles vont chercher en memoire centrale et l'image passe de 1,7 a 8,0 ms ».
Le chiffre etait reel et attribuait le cout au mauvais terme. Une fois le
parcours sorti de la boucle, lire le relief PLEIN ne coute plus que **0,22 ms
de plus**. La reduction jetait les quinze seiziemes de la donnee pour rien.

Meme lecon que le verrou 3 du streaming, resolu en deux constantes : **avant
de remplacer un composant, mesurer ce qui coute REELLEMENT**.

#### UN ZOOM SLATE ETIRE, IL NE REECHANTILLONNE PAS

Le zoom du globe etait une `SetRenderScale` posee sur l'image : un ETIREMENT
de la texture deja peinte. A six fois, les 512 texels s'etalaient sur plus de
trois mille pixels -- un texel devenait un carre de six, et le globe partait
en marches d'escalier.

**LE FAIT QUI DEBLOQUE TOUT : le lance-de-rayon fait un travail CONSTANT par
pixel.** Son cout est en Res au carre et ne depend PAS de l'etendue couverte
-- propriete de CONSTRUCTION de la boucle, pas une mesure. Dessiner une
portion plus petite du globe sur le meme nombre de pixels ne coute donc pas
un cycle de plus. Mesure : **4,114 ms a zoom 1, 5,261 a zoom 4**, l'ecart
venant de ce qu'on voit plus de TERRE et moins d'espace.

Le zoom vit desormais dans `FCadreGlobe::RayonApparent`. Comme la projection
et son INVERSE lisent ce meme champ, le pointage suit sans qu'on lui dise
rien -- et l'image Slate ne porte plus aucune transformee, donc le globe ne
deborde plus de son cadre.

**CE QUI A ETE ECARTE, ET POURQUOI.** La texture equirectangulaire existe
(`WorldseedGlobeBake`, 0,001 ms) et avait ete ecartee le 20 septembre pour la
QUALITE. Son argument de cout ne vaut pas dans un ecran de menu ou rien
d'autre ne tourne. Et l'argument « elle permettrait d'imprimer les noms »
tombe aussi : un overlay Slate donne du texte vectoriel net, sans
l'etirement qu'une projection equirectangulaire inflige aux hautes latitudes.

#### UNE SONDE QUI FABRIQUE SES ENTREES NE VALIDE PAS LE CHEMIN REEL

Les frontieres de region avaient ete mesurees -- 1667 pixels de pays, 15472
de region -- et le chiffre etait juste. Mais il venait de `ProbeCarteEcran`,
qui **GENERE SON PROPRE MONDE** : elle validait le PEINTRE et ne pouvait rien
dire du transport.

Or `HandlePlayClicked` compose le monde transmis au jeu champ par champ, et
`Regions` n'y figurait pas. En jeu il n'y avait donc **ni frontieres ni
noms**, et ce qu'on prenait pour des frontieres sur la carte etait le lisere
de cote. Rien ne le signalait : un decoupage vide est un etat valide que tous
les consommateurs degradent proprement.

    REGLE : quand une donnee traverse plusieurs etages, le controle doit etre
    pose SUR LE CHEMIN QUE LE JEU EMPRUNTE, pas sur une reconstitution.

Meme famille que « serialiser ne suffit pas, il faut TRANSVASER » (22
septembre). Le remede est le meme : un COMPTE journalise depuis le
consommateur final -- ici « carte : noms -- decoupage present/ABSENT ».

#### UN COMMENTAIRE FAUX PEUT COUPER UNE FONCTION EN PERMANENCE

Les frontieres du globe etaient coupees a CHAQUE image par une condition
`!bUsePreview` que j'avais posee moi-meme, sur une premisse fausse : je
croyais l'apercu provisoire -- « une generation rapide a basse resolution »,
disait mon commentaire -- alors que `BuildPreviewField` REDUISAIT le monde
FINI, une fois et pour de bon. `bUsePreview` etait donc vrai EN PERMANENCE.

**Le compte l'a trouve en une ligne** : un compteur qui distingue les deux
causes OPPOSEES d'un trait absent -- la passe qui ne tourne pas, et la passe
qui tourne sans rien trouver. Les deux n'appellent pas le meme remede, et a
l'oeil elles sont indiscernables.

Trois commentaires perimes ont ete corriges au passage, dont un qui annoncait
le lance-de-rayon comme « un repli, il ne sert que si le materiau est
introuvable » alors que le journal dit « rendu par le processeur » a chaque
partie.

#### LE CENTRE DE GRAVITE D'UNE FORME CONCAVE LUI EST EXTERIEUR

Pour poser le NOM d'une region, le barycentre ne convient pas : un croissant,
une region qui epouse une baie, un bassin en fer a cheval autour d'un massif
ont leur centre DEHORS. Mesure : **6 regions sur 47 et 1 pays sur 8**, soit
une etiquette sur huit en pleine mer ou chez la voisine.

`FWorldseedRegion::AncrageM` vaut le centre quand il tombe dans la forme, et
sinon la cellule la plus proche de lui. **Il ne traverse pas le cache**,
comme les noms : il se recalcule dans `Nommer`, rejouee dans les DEUX
branches -- ce qui evite un champ de plus et une regeneration pour tout le
monde.

**UN SEUIL D'AFFICHAGE SE POSE SUR LA FORME, PAS SUR L'ECHELLE.** Un seuil en
metres par pixel ferait apparaitre les quarante-sept noms d'un coup ; on
compare donc le diametre APPARENT de chaque forme a la place qu'un nom
occupe. Une grande region apparait tot, une petite attend qu'on s'approche.
Mesure : 12 noms a 33 m/px (le monde entier, donc surtout des pays), 9 a
9 m/px (un continent, donc ses regions).

**ET LA MINIMAP NE RECOIT PAS D'ETIQUETTES**, a dessein : son disque fait 224
pixels pour un rayon de 1,5 km quand une region en fait 3,5, donc l'ancrage y
est presque toujours hors cadre. Sur une carte on cherche « ou est-ce », sur
une minimap « ou suis-je » : elle porte un bandeau « Region, Pays », qui
repond toujours.

**PAS DE NOMS SUR LE GLOBE**, a dessein : une sphere ecrase ses meridiens
vers le limbe, un nom y serait comprime jusqu'a l'illisible, et la moitie du
monde est de toute facon cachee.

#### PIEGES D'OUTILLAGE

- **L'ecran de configuration se capture SANS `-WorldseedMenuAuto`.** Le menu
  appelle `StartGeneration` sans condition a sa construction : il genere donc
  seul et RESTE affiche, au lieu d'enchainer dans le jeu. C'est ce qui
  manquait pour photographier le globe, jamais vu jusque-la.
- **Une capture prise trop tot ne montre pas ce qu'on croit.** Le script
  attend la fenetre PUIS dort ; la premiere carte a ete capturee sept
  secondes AVANT son ouverture, et l'image montrait le jeu. Controler
  l'horodatage du journal contre celui de la capture.
- **Le proprietaire peut manipuler l'ecran pendant une capture.** Deux globes
  sont sortis zoomes sans qu'aucune molette ait ete pilotee, et une carte
  s'est ouverte sans `-WorldseedCarteAuto`. Le depot a deja cette note pour
  la souris ; elle vaut pour le clavier. D'ou `-WorldseedGlobeZoom=`, qui
  permet de capturer a un zoom CONNU.
- **Il n'y a pas de Python sur cette machine**, et `sed`/`perl` sur du C++
  reste interdit -- employer l'editeur de fichiers. Un heredoc bash casse
  aussi sur les apostrophes : pour ajouter un long texte a un fichier,
  l'ecrire ailleurs puis le concatener.

### L'ecran d'entree devient un menu de jeu : deux pieges (26 septembre 2026)

Demande du proprietaire : des icones a la place du de et des autres actions, et
un agencement qui ressemble moins a un tableau de bord. Le chantier lui-meme est
dans le commit ; deux pieges en sortent, et ils resserviront.

**UN GLYPHE D'ICONE N'A PAS LA TAILLE D'UNE LETTRE DE MEME CORPS.** Material
Symbols occupe **tout son cadratin** quand une fonte de texte n'en remplit
guere que la hauteur d'x. Les dix-huit points qui convenaient au caractere
Unicode « ⚄ » donnent donc **vingt-quatre pixels de dessin dans un bouton qui
en fait trente**, marges deduites : le de debordait de son fond. Regle retenue
-- le glyphe se pose a **trois points de plus que le mot** qu'il accompagne, et
les boutons a icone seule emploient un corps de **treize**. Cela ne se voit
qu'au GROSSISSEMENT : a l'echelle de la capture, un glyphe qui deborde de deux
pixels passe pour une bordure epaisse.

**`GameUserSettings.ini` SURVIT ET PRIME SUR LA LIGNE DE COMMANDE.** La
premiere capture de la session est sortie en **5120 x 1369 au lieu de
1600 x 900**, et a qualite degradee -- le fichier portait encore
`sg.ResolutionQuality=0` d'un vieux balayage ET `FullscreenMode=1`
(plein ecran sans bordure, donc toute la surface des deux moniteurs). **Ni
`-windowed`, ni `-resx=`, ni `-resy=` n'y changent rien.** Ce registre avait
deja la regle -- « un balayage doit remettre a zero l'etat persistant entre
chaque passe » -- mais elle etait ecrite pour les mesures de performance ; elle
vaut pour **toute capture**. Supprimer
`Saved/Config/WindowsEditor/GameUserSettings.ini` avant chaque lancement.

**ET LE SIGNE QUI TRAHIT UNE FENETRE DE TRAVERS EST LA TAILLE RENDUE PAR LA
CAPTURE**, que le script imprime : `1600 x 900` attendu, autre chose = le
fichier de reglages a parle. Juger une mise en page a un rapport d'aspect de
3,7:1 quand le jeu tourne en 16:9 ne prouve rien.

**UNE BORDURE POSEE SUR UN WIDGET QUI REPOND AU CLIC NE LE PROTEGE PAS.** Slate
ne fait consommer l'evenement qu'aux widgets qui le GERENT : un `UBorder` est
bien touche par le test de survol, puis laisse l'evenement remonter. Un panneau
flottant sur le globe ne barre donc RIEN -- appuyer sur son fond posait un
repere sur la terre qu'il cache, et un glisser parti de la faisait tourner le
monde. Il faut tester explicitement la geometrie des panneaux
(`FGeometry::IsUnderLocation`, qui prend la position ABSOLUE que portent les
evenements de souris). La geometrie en cache d'un widget `Collapsed` est vide,
donc un panneau replie ne barre rien sans qu'on ait a tester sa visibilite.

*Corollaire* : un relachement doit exiger qu'un appui ait ete ACCEPTE. Sans
cela, le « up » qui suit une pression refusee trouve le compteur de glissement
a la valeur du geste PRECEDENT -- zero -- et conclut au clic.

**VERIFIE EN JEU, AVEC LE TEMOIN QUI REND LA MESURE LISIBLE** : clics injectes,
globe a zoom 4 (c'est la seulement que les panneaux recouvrent vraiment le
globe), clic sur le globe -> reticule a 1,6 px, clic sur le bandeau -> aucun
reticule, et le repere precedent intact au dixieme de pixel. Sans le premier,
« aucun reticule » ne se distinguerait pas d'un clic que la fenetre n'aurait
pas recu.

### La carte : ce que « re-echantillonner comme le globe » voulait dire (27 septembre 2026)

Deux demandes dans la meme phrase -- « je galere a trouver ma position sur la
carte en plein ecran, serait il possible quand elle s'ouvre de zoomer par
defaut sur la position du joueur » et « appliquer un re-echantillonnage comme
sur le globe, avec l'ombre et le relief ». La premiere etait claire ; la
seconde, prise au pied de la lettre, ne valait rien -- et c'est la lecon.

**LA LECTURE LITTERALE, ECRITE PUIS ANNULEE.** La carte cuisait le monde entier
a la resolution de la grille puis ETIRAIT cette texture ; je l'ai remplace par
une repeinture de la VUE a chaque changement, comme le globe. Le chemin a ete
ecrit, mesure, regarde -- et il ne rend RIEN :

| | avant (etirement) | apres (repeinture) |
|---|---|---|
| relief a 5 m/px | identique | **identique** |
| bords de biome | lisses (interpolation) | **en escalier** |
| glisser | gratuit | **19,2 ms**, ramene a 10,9 au quart |
| texture | 32 Mo | 5,8 Mo |
| ouverture | 68 ms | 22 ms |

**LA DONNEE S'ARRETE A LA CELLULE, 15,6 m, DANS LES DEUX CAS.** Interpoler la
COULEUR deja peinte ou interpoler l'ALTITUDE puis peindre donne le meme relief ;
la seule difference est que la premiere lisse aussi les IDENTIFIANTS, ce qui
FLATTE les bords de biome. Le chemin « propre » etait donc visuellement pire.
Annule. Ne pas le reprendre sans une donnee plus fine a montrer.

**CE QUI MANQUAIT VRAIMENT ETAIT LE CONTRASTE.** La carte modulait sa couleur
par `0,72 + 0,56 . Lambert`, pose « doux » pour qu'un ombrage qui va jusqu'au
noir ne mange pas les biomes. Le globe emploie `0,22 + 0,88` : le rapport entre
un versant eclaire et un versant a l'ombre passe de **1,8 a 5,0**. Mesure de
l'ecart-type de la luminance sur une zone de TERRE, meme graine :

    globe               moyenne  61,6   ecart-type  28,9
    carte, force 1,0             85,0               27,1
    carte, force 0,0            109,6               21,8

La carte rejoint le contraste du globe ; l'ecart de moyenne qui reste est
l'assombrissement du LIMBE, qui n'a de sens que sur une sphere.
`FParamsFenetre::ForceOmbrage` interpole entre les deux formules et vaut 1 ;
la minimap suit, une seule definition. Trois forces capturees au meme cadrage
avant de trancher.

**METHODE : QUAND UNE DEMANDE NOMME UN MECANISME, VERIFIER QUE C'EST BIEN LUI.**
« Re-echantillonner » designait un mecanisme ; ce qui se voyait etait un
CONTRASTE. J'ai passe une heure a implementer le premier avant de mesurer que
le relief ne bougeait pas d'un pixel. Le controle qui aurait du venir en
premier tient en deux captures au meme cadrage.

**UN ORACLE QUI TOMBE PARCE QU'UN AUTRE REGLAGE A BOUGE.**
`Worldseed.Carte.LaReductionSeDeclencheQuandIlLeFaut` compare la variation
totale de l'image agregee a celle prise au point. L'ombrage est IDENTIQUE dans
les deux branches -- ses quatre echantillons vont au relief, au pas d'une
cellule, quelle que soit l'agregation -- donc il n'apporte que du bruit COMMUN.
A force 1,0 ce bruit a noye l'ecart : **23589 contre 23561, soit 0,12 %, signe
inverse**. Le test coupe desormais l'ombrage. Meme famille que « une mesure dont
la reference derive avec le reglage qu'on teste ne mesure rien ».

**L'OUVERTURE EST UNE FRACTION DU MONDE**, un huitieme de sa largeur -- huit
kilometres ici, un peu plus que les 2400 m de rayon que le terrain charge
montre. Pas une distance en dur : ce depot a paye deux fois la constante
metrique juste a une echelle et fausse a l'autre.

### `-WorldseedVue=` REGARDE VERS L'EXTERIEUR (27 septembre 2026)

Deux tournees photo perdues, et la cause est dans la signature de l'option.
`-WorldseedVue=<caps>` pose la camera a `-WorldseedVueX/Y` et vise une cible a
QUATRE KILOMETRES dans la direction du cap. **Se poser sur un pan de falaise le
met donc hors du cadre, par construction** : les quatre vues « centrees sur un
pan » n'en montraient aucun -- de l'ocean, une calotte, un personnage face a la
mer. Pour photographier un objet, on RECULE de cent metres et l'on vise.

**ET LE BANC NE JOUE PAS LE MEME MONDE QUE LA TOURNEE.** Le banc ouvre
`L_Worldseed_Proc` DIRECTEMENT et retombe sur la graine de secours ; une partie
lancee par le menu porte celle du menu. Les coordonnees relevees par l'un ne
designent rien chez l'autre. C'est une variante du piege deja consigne -- « les
deux acteurs generaient DEUX MONDES DIFFERENTS » -- et elle se lit au journal :
`monde repris du cache : seed=...`.

**LE REMEDE EST CELUI DES BOUCHES DE GROTTE** : journaliser la position. Le
releve des parois cite desormais les CINQ PLUS HAUTS pans -- pas les cinq
premiers, echantillon arbitraire qui est tombe sur cinq pans a Z negatif -- et
donne le point de PRISE DE VUE, deja recule, pas la position du pan.

**UN CHIFFRE SORTI DE LA : 40 pivots sur 42 sont sous le niveau de la mer.** Le
pivot d'un maillage de ce pack est connu pour etre incoherent -- ce depot l'a
paye en sondant la collision d'un pan sur son pivot, qui tombait dans le vide --
donc ce chiffre ne prouve PAS que quarante pans sont noyes. Il dit qu'il faut
aller voir. **Reste ouvert.**

### Le terrain sans texture : un reglage pose dans UNE SEULE branche (28 septembre 2026)

Signale en jeu : « le terrain a perdu son RVT, il n'a plus de texture ».
L'hypothese etait la RVT ; ce n'etait pas elle, et la mesure l'a ecartee avant
que je touche a quoi que ce soit.

**LA COULEUR DU TERRAIN NE LIT JAMAIS LA RVT.** Releve dans les deux materiaux :
`M_WorldseedBiome` prend sa `BaseColor` de la COULEUR DE SOMMET,
`M_WorldseedGround` d'un `Lerp` sur ses quatre textures. Ils l'ECRIVENT ; ce
sont le feuillage et les pans qui la LISENT. Les deux graphes etaient d'ailleurs
intacts -- 7 et 50 expressions, `PoidsDepuisTexture` a False par defaut, toutes
les proprietes branchees.

**LA CAUSE.** `Colouring` vaut `BiomeColour` par defaut depuis l'origine, et
`TexturePack` n'etait pose que dans la branche « monde repris du menu »
d'`AdoptWorld` -- la ou vivait le lien « habillage choisi -> coloration » avant
que les six packs ne soient ramenes au seul Orasot. Un lancement DIRECT sur
`L_Worldseed_Proc` -- le harnais, un PIE sans passer par le menu -- ne prend pas
cette branche : le journal dit « aucun monde en attente, generation de secours »,
et le sol rend des couleurs de biome A PLAT.

**C'EST LA TROISIEME FOIS, ET LE FICHIER LE DISAIT TRENTE LIGNES PLUS BAS** :
« L'HABILLAGE FORCE S'APPLIQUE ICI, sur les DEUX chemins de chargement. Pose
plus haut, dans la seule branche "monde repris du menu", il ne servait jamais en
lancement direct -- exactement le defaut deja paye sur le point de naissance. »
**Un reglage qui doit valoir partout se pose dans le DEFAUT, jamais dans une
branche.**

Le temoin qui distingue les deux moities est au journal : la rampe s'arme sur
`M_WorldseedBiome` avant, sur `MI_WorldseedGround_Orasot` apres.

### La banquise se voit enfin sur la carte (29 septembre 2026)

Signale : « et la banquise au pole nord ? ». Elle etait CALCULEE juste depuis
toujours, et n'existait que sur le globe du menu.

**LA MESURE D'ABORD, parce qu'elle dit que le calcul n'est pas en cause :**

    80-90 deg   100,0 % de la mer de la bande    (0,0 % emerge : aucune terre)
    70-80 deg    59,9 %                          (9,3 % emerge, dont 53,9 % de calotte)
    le monde      2,83 % de toute la mer         (Terre : 3 a 5 selon la saison)

Le critere est le bon -- la mer prend en glace quand son mois le plus chaud
reste sous `SeaIceTempC`, seuil tenu SEPARE de celui de la calotte parce que
l'eau salee gele vers -1,8 degre et non a zero.

**LE DEFAUT ETAIT DANS UNE SEULE LIGNE, ET IL TOUCHAIT TROIS ECRANS.**
`WorldseedCarte::CouleurCellule` ne lisait `Cover` que **au-dessus de zero** ;
sous zero elle rendait un degrade bathymetrique et ignorait la banquise. Or
la carte plein ecran, la minimap ET `ProbeCarte` passent tous par ce peintre :
une correction les sert tous les trois. Le globe, lui, a son propre chemin --
d'ou l'incoherence, **blanc sur un ecran et bleu sur l'autre, pour le meme
monde**.

**ET CE N'EST PAS `AppearanceBiome` QUI POUVAIT LA RENDRE.** Elle ne traduit
que `Rock` et `Beach` ; pour toutes les autres couvertures -- l'ocean compris
-- elle rend le biome CLIMATIQUE, defini PARTOUT, meme sous la mer. C'est
exactement le piege qui avait fait annoncer a la nappe RVT « 8 388 608 terre /
0 mer » sur un monde a 71 % d'ocean. **La banquise se lit sur la COUVERTURE,
et nulle part ailleurs.**

**LA TEINTE VIENT DE `CoverColour`, LA MEME FONCTION QUE LE GLOBE.** Deux
blancs poses a la main finiraient par diverger, et l'ecart se verrait
precisement la ou l'on compare les deux ecrans -- ce qu'on vient de fermer.

**AUCUN DEGRADE SOUS ELLE** : la banquise flotte et cache le fond. Sa lisiere
est franche dans la nature, et un fondu la ferait lire comme un haut-fond.

**VU A L'IMAGE, et mesure sur la carte du monde** :

    bande polaire NORD   R 234 V 241 B 248   <- CoverColour(SeaIce), attendu 236/241/249
    mer voisine               125     155     182   le haut-fond clair
    plein ocean                58      89     130   l'abysse
    bande polaire SUD         242     246     250   <- la CALOTTE, donc de la TERRE

Les deux glaces se distinguent aussi l'une de l'autre, la glace de mer etant
legerement plus bleutee -- ce qui est correct.

**L'ORACLE GARDE L'ECART, PAS LA TEINTE.** `Worldseed.Carte.LaBanquiseSeVoit`
compare la clarte de la banquise a celle du haut-fond **sur une mer PEU
PROFONDE**, c'est-a-dire le cas le plus dur, celui ou le degrade est le plus
clair donc le plus proche du blanc. Temoin monte puis retire : la branche
cassee rend **« banquise 0,531, haut-fond 0,531, ecart 0,000 »** -- la banquise
indiscernable du haut-fond, le defaut d'avant, ecrit par le test lui-meme.
Apres correction : **0,869 contre 0,531, ecart 0,338**.

**TROIS VOIES ONT ETE PRESENTEES ET ECARTEES POUR L'INSTANT**, et il faut les
garder pour ne pas les redecouvrir :

| voie | ce qu'elle coute |
|---|---|
| une NAPPE de banquise praticable, a Z proche de zero, avec collision | le patron existe trois fois (sol de fond, nappe RVT, chunks) et elle pourrait se bosseler en cretes de compression et recevoir DLWE ; reste a trancher nappe unique du monde (memoire : le sol de fond pese deja 2 M sommets) ou streaming a ecrire. **Non chiffre.** |
| peindre l'EAU par un masque cuit, greffe dans le materiau du plugin Water | quasi gratuit, mais **on ne marche pas dessus** -- on traverse et l'on tombe a l'eau -- et la surface ondulerait comme de l'eau sous une couleur de glace |
| la mailler en VOXEL | le diffuseur ne maille qu'UNE bande, autour du relief macro : sous la mer c'est le FOND. Il faudrait une seconde bande, ou remonter la surface a zero et perdre le fond marin sous la glace. **Chantier lourd.** |

**CE QUI RESTE OUVERT, ET C'EST LA QUESTION D'ORIGINE** : en jeu, la banquise
n'a ni geometrie ni materiau. Le joueur peut marcher sur une ile arctique --
il y a 9,3 % de terres entre 70 et 80 degres, blanches par DLWE depuis le
28 septembre -- arriver au rivage, et voir de l'eau BLEUE la ou la carte et le
globe montrent du blanc.

**ET UNE INCONNUE NON MESUREE** : on ne sait pas si le personnage sait nager.
Si non, une banquise praticable serait le SEUL moyen d'atteindre le pole, ce
qui change son interet.

