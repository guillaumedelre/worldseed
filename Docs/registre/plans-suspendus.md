# Registre Worldseed — plans suspendus

> **CE FICHIER NE SE LIT PAS EN ENTIER, IL SE CHERCHE.** Il garde les plans
> DETAILLES de chantiers ouverts puis suspendus : les etapes prevues, les
> predictions a falsifier, et les incertitudes qui n'ont pas ete levees.
>
> **Un plan n'est pas un recit de seance.** Ce qui a ete MESURE vit dans les
> autres fichiers du registre ; ici se trouve ce qui etait PREVU et ne l'a pas
> ete. C'est la moitie qu'on perd le plus vite, et celle qui coute le plus a
> refaire.
>
> ```
> grep -n '^## ' Docs/registre/plans-suspendus.md
> ```
>
> **ATTENTION, DOUBLEMENT PERISSABLE.** Un plan decrit un etat du code a une
> date, et il contient des etapes qui ont pu etre FAITES depuis. Avant
> d'executer quoi que ce soit : verifier que ce qu'il nomme existe encore, et
> chercher dans le registre si l'etape a deja ete jouee. **Un carnet se remesure
> avant d'etre execute** — ce depot a execute deux taches perimees le meme jour.

---

## Tempetes de sable et sable qui vole (plan du 29 septembre 2026)

**ETAT : le VOLET A est FAIT et livre** — voir, dans `climat-ciel.md`, « Le
sable vole : ce qui l'empechait, et ce qui ne l'empechait PAS » et « La
poussiere sort du vent ». **Le VOLET B est SUSPENDU par decision du
proprietaire** — voir « Les nappes rasantes : POURQUOI ON S'ARRETE, et par ou
reprendre », qui porte le diagnostic complet et les cinq hypotheses eliminees.

Ce qui suit est le plan d'origine, garde pour le detail du volet B (les trois
voies Niagara comparees) et pour les douze incertitudes de la fin, dont
plusieurs n'ont pas ete levees.


## Contexte

Question posée : peut-on utiliser Ultra Dynamic Sky / Ultra Dynamic Weather pour
des tempêtes de sable, ou au moins pour voir du sable voler ?

**Oui — et c'est déjà à moitié branché.** Le pack livre deux préréglages,
`Sand_Dust_Calm` et `Sand_Dust_Storm`, un système Niagara `Dust` complet, quatre
matériaux `Post_Process_Wind_Fog`, un `Wind_Debris` et six nappes sonores. Notre
C++ écrit déjà `Dust` sur l'échelle 0..10 (`WorldseedSkyDriverComponent.cpp:410`)
ainsi que `Wind Intensity` et `Wind Direction`.

On ne le voit jamais pour des raisons mesurables, et la principale n'est pas
celle qu'on croyait.

### Arbitrages rendus par le propriétaire

1. **Portée** : le voile atmosphérique UDS **et** le sable qui rampe au ras du
   sol. Le dépôt de poussière sur les surfaces (DLWE) est **écarté**.
2. **Fréquence** : voile permanent faible dans les climats arides + tempêtes
   rares et violentes.
3. **Étendue** : tous les climats arides, **chauds et froids** — on retire le
   `TempMeanC > 5.0f` en dur qui exclut aujourd'hui le Gobi et la Patagonie.

---

## Ce que la mesure a changé, et il faut le lire avant le reste

### La découverte qui recadre tout

Relevé dans les treize préréglages du pack : **`Sand_Dust_Calm` et
`Sand_Dust_Storm` posent TOUS DEUX `Dust = 10`.** Ce qui les sépare est
**`Wind Intensity` : 1 contre 10**. Ni l'un ni l'autre ne surcharge
`Cloud Coverage`, et tous deux posent `Fog = 1` (neutre).

**Le vent est donc l'axe de la tempête, la poussière en est l'effet** — ce qui
est aussi la physique : le sable ne se soulève qu'au-delà d'une vitesse de
friction seuil (saltation), et le flux croît comme le **cube** de cette vitesse
(Bagnold, 1941). La forme arbitrée (voile permanent + tempêtes rares) est
conservée, mais elle doit être **pilotée par le vent**, pas parallèle à lui.

> **⚠ RÉSERVE DE MÉTHODE.** Ces valeurs viennent d'un **parsing binaire des
> `.uasset`**, ce que `CLAUDE.md` interdit explicitement. Le parseur a été
> validé sur un témoin connu — il retrouve exactement le relevé déjà consigné
> (`Rain_Light 3`, `Rain 7`, `Sand_Dust_Storm dust 10`) — donc l'information est
> crédible. **Mais aucune de ces valeurs ne doit être gravée comme `SOURCE`
> dans `world_rules.json` avant d'avoir été confirmée par le commandlet Python
> de l'étape A2.** Une valeur plausible lue comme une mesure est le piège que ce
> dépôt paye le plus souvent.

### La cause la plus probable du « je n'en vois jamais »

UDW porte **dix variables `<curseur> - Manual Override`** — une par curseur.
**Nous n'en posons qu'une**, `Thunder/Lightning` (`:425`), et le commentaire du
code dit exactement pourquoi il le faut : *« sans elle, UDS reprend la main
depuis son propre système de types et notre écriture serait écrasée au tick
suivant »*. **Ce raisonnement vaut mot pour mot pour `Dust` et
`Wind Intensity`.** Que la pluie soit visible ne prouve rien : ce sont dix
booléens indépendants.

Et il y a plus : `Enable Dust Particles`, `Enable Post Process Wind Fog` (le
voile est un **post-process distinct** des particules, relié par
`PPWF Intensity from Dust`), et un drapeau **dérivé** `Dusty` — trait pour trait
le `Using Either Aurora` qui avait coûté une séance le 28 septembre.

### Quatre autres défauts trouvés en chemin

| défaut | où | conséquence |
|---|---|---|
| le seuil de poussière est posé sur le signal **brut** | `WorldseedWeatherState.cpp:152` | poussière 24,9 % du temps, et `Dust = 10` **inatteignable** (max réel 8,13, le support du signal est borné à 0,9327) |
| les surcharges manuelles sont armées **après** les valeurs | `:410` et `:412` contre le bloc `:423` | une trame perdue au premier passage ; le tonnerre, lui, écrit **après** (`:444`) |
| le vent plafonne à 8 et dépend de la **pluie** | `:229` | en désert (`Occurrence = 0`) le vent monte à 3,80 — **moins qu'un ciel gris**, quand le pack réserve 10 à ses trois états violents |
| **`-WorldseedCielClair` ne fige plus l'horloge** | `WorldseedTerrain.cpp:42` puis `SkyDriver:328-335` | `ArmerHorloge` repose le drapeau à vrai. La garde d'A/B est morte depuis le 28 septembre, silencieusement |

L'unité du vent est documentée **par le pack** : UDW porte une variable
`Knots at Wind Intensity 10` et une fonction `Get Wind Speed in Real Units`.

---

## VOLET A — le voile atmosphérique

### A0. Prouver que `-WorldseedCielClair` fige encore l'horloge

**D'abord, parce que tout critère par capture en dépend.** Relancer une fois avec
`-WorldseedPhotos -WorldseedCielClair -WorldseedQuitter` et chercher dans
`Saved/Logs/Worldseed.log` : si `ciel d'inspection : … horloge figee` **et**
`horloge : armee -- journee 30 min` apparaissent ensemble, le conflit est réel.

**Correctif** : sortir tôt de `ArmerHorloge` si le drapeau est posé, **et le
journaliser** (`horloge : NON armee -- ciel d'inspection`). Un silence se lirait
« tout va bien ».

**Vérification** : la ligne de journal. Aucun oracle possible — il faut un acteur.

### A1. Le témoin `-WorldseedPoussiereForce=<0..10>`

Patron exact : `WorldseedSkyDriverComponent.cpp:207-239` (`TemoinOrage`,
`bTemoinLu`). Poser la valeur **entre `BlendTowards` (`:191`) et `PushWeather`
(`:241`)** — sur la cible, le lissage écrêterait le témoin lui-même.

**L'état forcé est celui de `Sand_Dust_Storm`, mesuré** :

```cpp
Current.Dust          = TemoinPoussiere;
Current.WindIntensity = FMath::Max(Current.WindIntensity, TemoinPoussiere);
Current.Fog           = 1.0f;   // les DEUX presets : 1, la valeur neutre
Current.Rain          = 0.0f;   // une tempete de sable ne mouille pas
Current.Snow          = 0.0f;
// CloudCoverage : ON NE LA TOUCHE PAS -- ni Sand_Dust_Calm ni Sand_Dust_Storm
// ne la surchargent. Le temoin d'ORAGE la monte a 9 (:238) : le copier ici
// fabriquerait une chimere a l'envers.
```

`Rain = Snow = 0` n'est pas décoratif : sans cela on obtiendrait pluie **et**
sable, la chimère symétrique de « des éclairs sous un ciel bleu ».

**⚠ L'ÉTAPE DE MESURE QUE LA DÉCOUVERTE CI-DESSUS IMPOSE.** Puisque le pack pose
`Dust = 10` dans ses deux préréglages, il est possible que `Dust` **commute** au
lieu de **doser**, et que l'intensité visuelle vienne entièrement du vent. Donc
**balayer le témoin à 0, 2, 5 et 10**, même point, même heure, et comparer :

- les quatre images diffèrent → `Dust` dose, la rampe de l'étape A4 a du sens ;
- 2, 5 et 10 sont identiques → `Dust` commute, et **la rampe doit porter sur le
  VENT**, `Dust` restant à 10 dès que la part d'aridité est non nulle. La
  formule d'A4 change alors, et il faut le savoir avant de l'écrire.

**La capture** (`-WorldseedPhotos` est REQUIS, `-WorldseedVue=` se pose en (0,0),
en pleine mer) :

```
UnrealEditor.exe Worldseed.uproject /Game/Worldseed/Maps/L_Worldseed_Proc
  -game -WorldseedPhotos -WorldseedCielClair -WorldseedHeure=1200
  -WorldseedPoussiereForce=10 -WorldseedDepartX=<X aride> -WorldseedDepartY=<Y aride>
  -WorldseedVue=1 -WorldseedCap=90 -WorldseedEspeces=150 -WorldseedQuitter
```

Les coordonnées viennent de `ProbeCiel`, qui rend déjà `X` et `Y` par site
(`WorldseedProbeCiel.cpp:276-289`) — **à 2048 lignes, comme le jeu**, sinon on
vise un autre monde.

**Trois critères, dont un négatif** : (1) voile ocre lisible à `=10` ; (2) **rien
à `=0`**, au même point et à la même heure — sans ce cas négatif on ne sait pas
si l'on photographie du sable ou la savane ocre, le dépôt a déjà compté 1794
pixels « orange » qui étaient le sol ; (3) le bandeau
(`WorldseedWeatherReadout.cpp:61-62`) doit dire `poussiere 10.00` et `vent 10.0`.
Si le bandeau le dit et que l'écran ne le montre pas, le défaut est en aval.

Jugement **binaire** — y a-t-il un voile, oui ou non. Le plancher de bruit d'un
A/B d'image entre deux lancements est de 60 % de pixels changés.

### A2. Les interrupteurs maîtres

`Tools/UE/inventaire_poussiere.py`, sur le patron de
`Tools/UE/calendrier_worldseed.py`. **Éditeur arrêté** (le plugin occupe le port
8000 — il l'est déjà, aucun processus `UnrealEditor` ne tourne) :

```
UnrealEditor-Cmd.exe Worldseed.uproject -run=pythonscript
  -script="Tools/UE/inventaire_poussiere.py" -unattended -nopause -nosplash
```

Le script doit, dans cet ordre :

1. charger `/Game/Worldseed/Maps/L_Worldseed_Proc` pour lire **l'instance posée**
   et pas seulement le défaut de classe — imprimer **les deux colonnes**, une
   différence est elle-même une information ;
2. **balayer par `dir()` ET par liste nommée explicite, en COMPTANT** — un
   inventaire tronqué à dix-huit entrées a déjà coûté le diagnostic d'une
   séance. Imprimer `entrees rendues : N sur M`, et **`ABSENT` quand une lecture
   échoue** : une absence doit être bruyante ;
3. lire les valeurs des treize préréglages de météo, **pour confirmer le relevé
   binaire** avant qu'il ne serve de `SOURCE` dans les règles ;
4. lire `Knots at Wind Intensity 10` et l'imprimer en nœuds **et** en m/s ;
5. lire `Dust Niagara System` et imprimer son chemin — c'est l'entrée du volet B ;
6. **ne rien écrire.** C'est un relevé ; l'écriture reste en C++, où elle est
   relue.

Noms à interroger nommément : `Enable Dust Particles`, `Dust - Manual Override`,
`Wind Intensity - Manual Override`, `Enable Post Process Wind Fog`,
`PPWF Intensity from Dust`, `Dusty`, `Currently Dusty`, `Dust Amount`,
`Dust Bias`, `Max Dust Coverage`, `Dust Color`, `Dust Spawn Rate Scale`,
`Dust Clear Speed when Calm` / `when Windy`, `Knots at Wind Intensity 10`,
`Dust Niagara System`.

**Ce que le C++ pose ensuite**, sous `if (bControle)` (`:423-443`), chacun suivi
de `CallFunction("OnRep_<nom>")` et **seulement si `NumParms == 0`** (le pont le
vérifie déjà, `WorldseedUdsBridge.cpp:240`) : `Enable Dust Particles`,
`Dust - Manual Override`, `Wind Intensity - Manual Override`, et
`Enable Post Process Wind Fog` si l'inventaire le confirme.

**Et corriger l'ordre** : `Ecrire(NameDust)` est ligne 410, le bloc des
surcharges ligne 423. La valeur part **avant** que la surcharge soit armée. Le
tonnerre fait juste — il écrit ligne 444, sous le commentaire « LA SURCHARGE
MANUELLE S'ARME UNE FOIS, ET AVANT LA VALEUR ». Descendre `Dust` et
`Wind Intensity` sous le bloc.

**À mesurer, pas à supposer** : poser `Dust - Manual Override` peut
court-circuiter le lissage propre d'UDW (`Dust Clear Duration`,
`Dust Clear Speed when Calm/Windy`). A/B par `-WorldseedPoussiereSurcharge=0|1`.

**Au passage** : la ligne `« les 13 variables … acceptent l'ecriture »` (`:522`)
porte un compte **écrit en dur**. Le dériver de `Refusees` et d'un compteur,
plutôt que d'ajouter des booléens en laissant « 13 » mentir.

### A3. Les clés de règles

`Tools/WorldGen/rules/world_rules.json`, section `uds` (`:966-1010`), plus
`FWorldseedClimatePresetRules` (`WorldseedClimatePreset.h:115`, lu en `.cpp:47`).

`dustPrecipMaxMm: 250.0` est remplacée par deux bornes (A5), et six clés
s'ajoutent. Convention : `SOURCE` / `CALIBRE` / `ARBITRAIRE` en **premier mot**
du `_comment_`.

```jsonc
"poussierePleinePluieMm": 120.0,   // CALIBRE
"poussiereNullePluieMm":  350.0,   // CALIBRE
"poussiereVoile":           1.5,   // ARBITRAIRE (arbitrage : voile permanent faible)
"poussiereVentSeuil":       6.5,   // SOURCE (saltation ; Overcast = 3 sur cette echelle)
"poussiereVentMordant":     3.0,   // SOURCE (Bagnold : le flux croit comme le CUBE)
"poussierePeriodeFacteur":  3.0,   // CALIBRE contre le fondu de 12 s
"ventCalme":                2.0,   // SOURCE (Clear_Skies = 2)
"ventMordantAgitation":     8.0,   // CALIBRE
"ventPluie":                2.0,   // repris de la formule en vigueur
"ventMaxUds":              10.0    // SOURCE (Thunderstorm, Blizzard, Sand_Dust_Storm)
```

Les deux `_comment_` (poussière, vent) doivent porter : que les deux préréglages
de sable ne diffèrent **que par le vent** ; que l'ancienne forme seuillait le
signal **brut** et plafonnait à 8,13 ; que l'unité du vent est un index 0..10
converti en nœuds par `Knots at Wind Intensity 10` ; qu'UDW **ajoute ses propres
rafales** (`Wind Gust Multiplier`) donc notre valeur est la composante **lente**,
à ne pas modéliser deux fois ; et que **le dépôt au sol est écarté par
arbitrage** alors que les deux préréglages posent `Material Dust Coverage = 1` —
c'est un choix, pas un oubli.

**Coût** : l'empreinte est un MD5 du fichier **entier**, donc régénération de 210
à 260 s. **Mais le monde ne bouge pas d'un octet** — rien ici n'entre dans la
génération. **Vérification** : comparer le **corps** du fichier de cache (tout
sauf l'en-tête de 68 octets, qui porte justement l'empreinte) avant et après :
même MD5. Devant un cache dont la clé a bougé sans que le code change, comparer
les corps, pas les noms de fichier.

### A4. La forme : le vent d'abord, la poussière ensuite

`WorldseedWeatherState.cpp` — poussière `:152-157` et vent `:224-229`
**échangent leur ordre**, le vent se calculant d'abord.

```cpp
// Un signal SEPARE et PLUS LENT : une tempete de sable dure des heures, et
// elle ne doit pas coincider avec les averses (graine decalee, comme l'aurore).
// Ralentir ne deplace aucun quantile : la table d'Uniformiser reste valide.
const float Souffle = WorldseedWeatherSignal::Uniformiser(
    WorldseedWeatherSignal::Storminess(
        Params.TimeSeconds,
        Params.VariationPeriodS * PresetRules.PoussierePeriodeFacteur,
        Params.Seed ^ 0x53414E44u));            // « SAND »

Out.WindIntensity = FMath::Clamp(
    PresetRules.VentCalme
  + PresetRules.VentMordantAgitation * Souffle
  + PresetRules.VentPluie * Occurrence,
    0.5f, PresetRules.VentMaxUds);

// LA SALTATION A UN SEUIL, ET LE FLUX CROIT COMME LE CUBE.
const float Saltation = FMath::Clamp(
    (Out.WindIntensity - PresetRules.PoussiereVentSeuil)
        / FMath::Max(PresetRules.VentMaxUds - PresetRules.PoussiereVentSeuil, 1e-3f),
    0.0f, 1.0f);

// La pluie rabat la poussiere -- meme fait physique que le brouillard qui se
// leve quand il ne pleut pas, vingt lignes plus haut.
Out.Dust = PoussierePart * (1.0f - Occurrence) * FMath::Max(
    PresetRules.PoussiereVoile,
    PresetRules.VentMaxUds * FMath::Pow(Saltation, PresetRules.PoussiereVentMordant));
```

**`Dust = 10` redevient atteignable par CONSTRUCTION**, pas par réglage :
`Uniformiser` rend exactement `1.0` au sommet du support
(`WorldseedWeatherSignal.cpp:71-79`), donc `Souffle = 1` ⇒ `WindIntensity = 10`
⇒ `Saltation = 1` ⇒ `Dust = 10`. L'ancienne forme était condamnée par une
soustraction affine sur un signal **borné**.

**Prédictions à FALSIFIER par la sonde** (`PoussierePart = 1`,
`Occurrence = 0`) : voile au plancher 79,5 % du temps ; `Dust ≥ 3` 14,5 % ;
`≥ 5` 9,0 % ; **`≥ 7` (tempête) 4,9 %** ; `≥ 9` 1,5 %. **Le nombre d'épisodes
par an est incalculable à la main** — seule la sonde répond, et j'écris ces
chiffres *avant* la mesure pour qu'ils puissent être démentis.

**Et corriger le seuil d'affichage en même temps, sous peine de mentir à chaque
ligne.** `VisibleDust = 0.5f` (`:38`) testé avant `Fog` et `CloudCoverage`
(`:49`) : avec un plancher permanent de 1,5, **tout désert afficherait
« POUSSIERE » 100 % du temps** et « degage » deviendrait inatteignable en climat
aride. Deux seuils : `3.0f` pour « TEMPETE DE SABLE », et un libellé « voile de
sable » entre `poussiereVoile` et 3.

### A5. L'étendue : une part continue, et le froid n'en prive plus

`WorldseedClimatePreset.cpp:319` et `.h:85` — `bool bDustPresent` devient
`float DustPart`. **Trois sites d'appel seulement** (déclaration, écriture,
lecture en `WorldseedWeatherState.cpp:155`), soit cinq lignes.

```cpp
Out.DustPart = 1.0f - WorldseedPerlin::Smoothstep(
    Rules.PoussierePleinePluieMm, Rules.PoussiereNullePluieMm, Sample.PrecipMm);
```

Le `T > 5.0f` disparaît : ce qui soulève du sable est un sol **nu et sec**, pas
un sol chaud — les loess de Chine du Nord viennent du Gobi (−0,4 °C, 194 mm),
pas du Sahara.

**Pourquoi une part et non un booléen** : à 250 mm en dur, une cellule à 249 mm
portait le voile entier et sa voisine à 251 rien du tout. Sur la maille de 15,6 m
du monde, le joueur traversait un **mur** de poussière — vingt mètres à
six km/h. Et la part est **sûre** parce que `Smoothstep` rend *exactement* zéro
au-delà de `poussiereNullePluieMm` : une forêt tropicale reste rigoureusement à
zéro, donc le défaut de l'aurore à 0,12 — un voile faible partout — ne peut pas
se reproduire.

**Écarté par arbitrage, à noter dans le `_comment_`** : la savane en saison
sèche (harmattan). `DustPart` se calcule sur le cumul **annuel** ; la rendre
saisonnière via `Preset.RainfallMm[S]` coûterait trois lignes et ferait poudroyer
la savane six mois par an. Plus juste physiquement, mais non demandé.

### A6. La sonde : `ProbeCiel` doit enfin voir la poussière

**Défaut d'instrument à corriger D'ABORD.** La sonde prend la cellule **médiane
en pluie** de chaque ligne de latitude (`:186`) — or la cellule médiane de la
ligne 30° n'est pas le Sahara, et ne portera presque jamais `DustPart > 0`. **En
l'état, `ProbeCiel` ne peut pas voir une tempête de sable, quel que soit le
modèle.** Ajouter une **seconde passe** sur la cellule au **décile inférieur**
de pluie — pas le minimum, qui serait l'extremum arbitraire que le commentaire
`:144-149` dénonce — étiquetée `(aride)`. Onze sites deviennent vingt-deux.

Colonnes : `voile %` (`Dust ≥ 1,0`), `sable %` (`≥ 5,0`), **`tempetes/an`**,
`vent moy` / `vent max`, et **les colonnes CIBLE** (même seuil, sans fondu).

**La cible est obligatoire ici plus qu'ailleurs** : la tempête est le phénomène
le plus bref et le plus rare du modèle, donc celui que le fondu de 12 s a le plus
de chances de décapiter. Sans elle on réglerait `poussiereVentMordant` pour
compenser un défaut de lissage — ce qui est arrivé aux orages (9 annoncés pour
119 désignés). **Et le vent, pas seulement la poussière** : sans les deux
colonnes de vent, « aucune tempête » ne se sépare pas en « le vent ne s'est
jamais levé » et « il s'est levé et la poussière n'a pas suivi ».

**Valider la colonne sur un cas connu** : une forêt tropicale doit rendre zéro
partout. Un compteur qui rend zéro ne mesure rien tant qu'on ne l'a pas vu
rendre autre chose.

### A7. Sept oracles, chacun avec son témoin

Dans `Source/Worldseed/Tests/WorldseedTestMeteo.cpp` (129 → 136 oracles).
Pour chacun : **on monte le défaut, on vérifie que le test tombe, on retire.**

| oracle | exige | témoin qui le fait tomber |
|---|---|---|
| `LaPoussiereSuitLAridite` | ≈1 à 90 mm, **exactement 0** à 2400 mm | porter `poussiereNullePluieMm` au-dessus de 2400 |
| `LaPoussiereNIgnorePlusLeFroid` | Gobi (−2 °C, 120 mm) rend `Dust > 0` | remettre `&& (T > 5.0f)` |
| `LaTempeteAtteintLeHautDeLEchelle` | `max(Dust) ≥ 9,5` | remettre l'ancienne forme → plafonne à **8,13** |
| `LaTempeteEstRare` | `Dust ≥ 7` entre **1 % et 8 %**, **et** voile > 70 % — une **bande** | `poussiereVentMordant = 1` (linéaire) → dépasse 8 % |
| `LeVentAtteintLEchelleDuPack` | `max ≥ 9,5`, `min ≈ ventCalme` | remettre le plafond `8.0f` |
| `LaPoussiereNeTombePasSousLaPluie` | jamais `Dust ≥ 3` et `Rain ≥ 1,5` ensemble | retirer `(1 - Occurrence)` |
| `LEchelleEstCelleDuPack` | `UdsEchelle == 10`, `ventMaxUds == 10`, `ventCalme == 2` | passer `ventCalme` à 0,2 |

**Ce qu'aucun oracle ne peut garder** : que les interrupteurs maîtres soient
posés et que quelque chose atteigne l'écran — cela demande un acteur. Comme pour
`FeedSky` sans appelant, le seul contrôle est la **ligne de journal** d'A2, plus
une capture.

Si un attendu tombe, on **corrige le réglage**, on n'élargit pas la borne — sauf
en écrivant *dans le test* pourquoi l'attendu était faux.

---

## VOLET B — le sable qui rampe au ras du sol

### B1. Ce qui est scriptable, et ce qui ne l'est pas

**Scriptable** : créer un `NiagaraSystem` vide, et régler les paramètres
utilisateur d'un système existant (`spawn_system_attached`,
`set_variable_float`, `set_variable_linear_color`).

**PAS scriptable : le GRAPHE.** Ajouter un émetteur, empiler des modules, câbler
des entrées relève du C++ **éditeur interne**, sans binding Python.
`UNiagaraEditorLibrary` n'expose que des aides marginales. **Un système Niagara
complet ne se crée donc pas par script** — l'inverse des matériaux, et c'est
pourquoi `neige_dlwe.py` existe et qu'aucun équivalent Niagara n'existe.

> **À vérifier en trente secondes avant de bâtir dessus**, parce que c'est de la
> connaissance d'API et non une mesure : un commandlet qui imprime
> `len(dir(unreal.NiagaraEditorLibrary))` et la liste, **en comptant les
> entrées**. Si une API d'émetteur existe que j'ignore, la voie (b) redevient
> ouverte et ce volet change.

### B2. La voie retenue : le Niagara du pack, aplati

**Ce qui décide** : `/Game/UltraDynamicSky/Particles/Dust` expose **53
paramètres utilisateur**, et ce sont exactement les bons —
`User.Spawn Box Height`, `User.World Spawn Offset`, `User.Max Spawn Distance`,
`User.General Velocity`, `User.Twirl Velocity`,
`User.Stick Particles to Surface`, `User.Particle Collision Enabled`,
`User.Sprite Scale`, `User.Spawn Rate`. Le système est **déjà** centré caméra,
**déjà** capable de collision et de « coller à la surface ».

**« Du sable qui rampe » n'est pas un système à écrire : c'est ce système,
aplati.** Un second `UNiagaraComponent` attaché au pion, `Spawn Box Height`
~150 cm, `World Spawn Offset` Z +20 cm, `Max Spawn Distance` ~2500 cm, vélocité
horizontale alignée sur `WindDirectionDeg`, `Twirl` faible (le sable serpente, il
ne tourbillonne pas), `Stick Particles to Surface` vrai.

**Pour** : zéro asset créé ; le matériau, la texture `Dust_Alpha` et le module
de vélocité sont déjà réglés par le pack ; le chemin ne se code pas en dur mais
**se lit** sur la variable `Dust Niagara System` d'UDW, donc le pont dégrade
proprement si le pack manque — sa doctrine.

**Contre** : l'asset n'est pas versionné (mais sans UDS il n'y a plus de ciel du
tout, donc la dépendance est déjà totale) ; le système est conçu pour tomber du
ciel.

**Incertain, à mesurer** : que `Stick Particles to Surface` fonctionne sur un
`ProceduralMeshComponent` de terrain voxel — la collision Niagara passe par les
champs de distance ou par des traces, et notre terrain n'a peut-être pas de
*mesh distance field*. **C'est le seul point qui peut faire échouer cette voie.**
Repli : `World Spawn Offset` + `Ceiling Check Height`, suffisant sur une nappe
plate, pas sur un versant.

**Repli (c), si les grains de `Dust` sont trop fins pour se voir au sol** :
`Wind_Debris`, qui a les mêmes paramètres plus `User.Debris Texture`, et qu'UDW
pilote déjà par le vent (`Enable Wind Debris`,
`Wind Debris Wind Velocity`). Son seul défaut est de porter
`Debris_Twigs_and_Leaves` — des feuilles. **Une texture, elle, se crée par
script** : notre unique asset serait `T_Worldseed_SableGrains` sous
`Content/Worldseed/Textures/`, versionné, importé par commandlet, idempotent par
constat. Il faudrait neutraliser les
`Wind Debris <Saison> Multiplier`.

**Repli (b), en dernier recours** : notre propre système sous
`Content/Worldseed/`, fait à la main. Versionné donc rejouable par
`git checkout`, mais **un asset créé à la main ne se relit pas** — on ne peut pas
prouver par script qu'il est dans l'état attendu — et le pack a déjà fait
plusieurs jours de réglage (collision, LOD, *scalability*).

**Écartés, pour qu'on ne les reprenne pas** : une nappe à **WPO** (le dépôt a
fait tomber l'éditeur deux fois sur du WPO ; et une nappe plate *z-fighterait*
sur un terrain voxel et ignorerait les surplombs) ; un **décal animé** (se lirait
comme une texture qui glisse, pas comme des grains en vol).

**Branchement** : ajouter `"Niagara"` aux `PublicDependencyModuleNames` de
`Source/Worldseed/Worldseed.Build.cs` — le module n'y est pas du tout.

### B3. Le déclenchement : rien à inventer

`WorldseedBiomes::SlotWeights(Biome)` rend déjà les quatre poids de matière
(`herbe, aride, roche, mousse`). **Le canal `.G` — « aride » — est exactement
« à quel point le sol est nu et sableux ici »** : désert chaud 1,00, plage 1,00,
désert froid 0,60, savane 0,55, steppe 0,50, forêts 0,00.
`AppearanceBiome` recompose déjà `Beach` et `BareRock` depuis l'axe `Cover`.

```cpp
const int32 Cellule = Geo.CelluleDepuisMetres(XM, YM);   // AU POINT, pas au chunk
float PartSable = WorldseedBiomes::SlotWeights(B).G;

// ⚠ PIEGE QUE LA TABLE TEND : Ocean et Lac portent aride = 0,55, Riviere 0,45.
// Ce n'est PAS du sable, c'est la convention de peinture du fond marin. Sans
// cette garde, le sable ramperait SUR L'EAU.
if (C == Ocean || C == Lake || C == River || C == SeaIce || ZCm < NiveauMerCm)
    PartSable = 0.0f;
```

**Lire AU POINT** — règle de l'estran : une bande littorale fait ~37 m quand un
chunk en fait 32, et trancher au centre donnerait un trait de côte en marches
d'escalier. `WorldseedVegetation.cpp:468-473` fait déjà exactement cette
lecture ; on l'imite.

**Où** : `AWorldseedTerrain::FeedSky` (`WorldseedTerrain.cpp:620-647`)
échantillonne déjà le climat sous le joueur. Lecture jumelle au même point, même
minuteur, **aucun tick nouveau**. Ponte = `PartSable × Saltation × Echelle`.

**Oracle** : `LeSableNeRampeQueSurDuSable` — 1,00 sur `HotDesert` et
`Cover::Beach`, **0,00 sur `Cover::Ocean` malgré son poids aride de 0,55**, 0,00
en forêt. **Témoin** : retirer la garde d'eau → l'océan rend 0,55 → tombe.

**Second critère, à l'image** : une capture sur le rivage d'un désert côtier. Le
sable doit courir sur la plage et **s'arrêter à l'eau**.

### B4. Le coût

Budget : trame ~4,4 ms sur 16,67, **goulot sur le fil de RENDU**. La simulation
est GPU (`GPU_WeatherParticles`, `User.GPU Buffer Period`), donc **c'est le
surdessin qu'il faut craindre, pas la simulation** : une nappe translucide au ras
de l'œil couvre l'écran plusieurs fois.

**Trois relevés, pas deux** : `=0` (témoin), `=10` avec le voile seul, `=10` avec
la nappe rampante. Sans celui du milieu on ne saurait pas lequel coûte.

```
... -WorldseedBanc -WorldseedVol=<ms> -WorldseedHeure=1200
    -WorldseedDepartX=<X aride> -WorldseedDepartY=<Y aride>
    -WorldseedPoussiereForce=10   (puis =0)
```

**Trois conditions** : trajet **fixe** (deux passes ont déjà parcouru 9 698 m et
733 m et été comparées comme si elles mesuraient la même chose) ; même heure ;
**site réellement aride**, sinon `PartSable = 0` et l'on mesure deux fois la même
chose.

**Critère** : l'écart sur la ligne des quatre fils. Si le fil de rendu monte de
plus de 1 ms, le levier est `User.Sprite Scale` et `User.Max Spawn Distance`,
pas `User.Spawn Rate`.

---

## Ordre et point de bascule

```
A0 horloge figee ? ──────────────► indispensable a TOUTE capture
A2 inventaire (commandlet) ──┐
A1 temoin + BALAYAGE 0/2/5/10 ┴──► CAPTURE 1 : la chaine atteint-elle l'ecran,
   │                                 et `Dust` DOSE-t-il ou COMMUTE-t-il ?
   ▼  [si non : les quatre interrupteurs maitres + leurs OnRep_]
A3 cles ──► A4 vent->poussiere ──► A5 part continue ──► A7 oracles ──► A6 sonde ──► A8 guet

B1 verifier l'API Niagara (compter les entrees)
B3 PartSable ← ne depend PAS du volet A
B2 Niagara local, voie (a) ──► CAPTURE 2 : le sable rampe-t-il ?
B4 banc, trois releves
```

**Le volet A est livrable seul** et répond déjà à « voir du sable voler ». **Le
volet B aussi**, une fois A1 et A4 posés.

**Le point de bascule est la CAPTURE 1.** Si le voile atteint l'écran, A3–A6 sont
du réglage mesuré. Sinon, il faut fermer A2 avant d'écrire une ligne de formule
— et le balayage 0/2/5/10 décide de la forme même d'A4.

---

## Vérification, de bout en bout

1. **136 oracles verts** (`-ExecCmds="Automation RunTests Worldseed;Quit"`,
   `-nullrhi`), chacun des sept nouveaux ayant vu son témoin le faire tomber.
2. **Le corps du cache inchangé** (MD5 hors en-tête de 68 octets) : le monde n'a
   pas bougé.
3. **Capture 1** : voile à `=10`, rien à `=0`, bandeau concordant, et le
   balayage 0/2/5/10 tranché.
4. **Capture 2** : du sable qui court au ras du sol, qui s'arrête à l'eau.
5. **Le bulletin de `ProbeCiel`** confronté aux six prédictions d'A4, colonne
   CIBLE à côté du VÉCU — un facteur trois entre les deux accuse le fondu, pas le
   climat.
6. **Le banc**, trois relevés, écart sur les quatre fils.
7. **Une partie jouée** dans un désert, sans témoin, pour regarder.

---

## Les commits

1. `test(uds): relever l'echelle de sable et de vent dans les assets du pack`
2. `fix(uds): -WorldseedCielClair ne figeait plus l'horloge` (si A0 le confirme)
3. `feat(uds): un temoin -WorldseedPoussiereForce, et l'etat coherent du pack`
4. `fix(uds): armer les interrupteurs maitres de la poussiere et leurs OnRep`
5. `feat(uds)!: la poussiere sort du VENT, et le vent atteint l'echelle du pack`
6. `feat(climate)!: la poussiere est une PART, et le froid n'en prive plus`
7. `test(uds): sept oracles de poussiere et de vent, chacun avec son temoin`
8. `feat(uds): ProbeCiel mesure le voile, la tempete et le vent, sur des sites ARIDES`
9. `feat(uds): le sable rampe au ras du sol, sur du sable seulement`
10. `perf(uds): ce que coute la nappe de sable, sur les quatre fils`

Les `!` portent un pied `BREAKING CHANGE:` disant **quoi remesurer** —
`ProbeCiel` seule, le monde ne bougeant pas. Chaque corps cite **le chiffre** et
**dit la piste abandonnée** : la saison sèche de la savane, le dépôt par DLWE,
la nappe à WPO, le décal animé.

---

## Ce qui reste incertain, et doit être mesuré avant d'être décidé

1. **Lequel des quatre interrupteurs maîtres arrive éteint.** Ils **existent** et
   ont des `OnRep_` ; leurs **défauts, non**. Seul A2 le dira.
2. **Si `Dust` dose ou commute** — le pack pose 10 dans ses deux préréglages. Le
   balayage 0/2/5/10 décide de la forme d'A4.
3. **Si `Dusty` est un drapeau dérivé** qu'UDW recalcule depuis sa machine
   d'états plutôt que depuis notre curseur : le piège de l'aurore en habit neuf.
   Il faudrait alors appeler une fonction d'UDW, ce qui n'est possible que si
   elle est sans paramètre.
4. **Les six pourcentages d'A4** ignorent le fondu de 12 s, qui peut les faire
   chuter d'un facteur trois — c'est arrivé aux orages.
5. **Le nombre de tempêtes par an** : incalculable à la main.
6. **Si `poussierePeriodeFacteur = 3` suffit.** Balayer 2/3/5 **dans la sonde**,
   jamais en éditant `world_rules.json` — c'est ainsi que ce fichier a été vidé.
7. **La valeur en nœuds de `Knots at Wind Intensity 10`.**
8. **Si `Stick Particles to Surface` marche sur le terrain voxel** — le seul
   point qui peut faire échouer la voie (a).
9. **Si `NiagaraEditorLibrary` expose plus que je ne le crois** : connaissance
   d'API, pas mesure.
10. **Le surdessin de la nappe rampante.**
11. **L'effet de bord du vent sur la végétation.** Porter le vent de 3,8 à 10
    **triple l'amplitude du mouvement du feuillage**. Le crash est couvert
    (`r.Velocity.EnableVertexDeformation=0`, `DefaultEngine.ini:168`) ; **l'aspect
    ne l'est pas.** Des arbres tordus dans un désert sans arbres n'est pas un
    problème ; la même tempête en steppe ou en méditerranéen, si. À **regarder à
    l'image**, pas à déduire.
12. **Les valeurs des treize préréglages** viennent d'un parsing binaire des
    `.uasset`, validé sur un témoin connu mais contraire à la règle du projet.
    **À confirmer par A2 avant d'être gravé comme `SOURCE`.**
