# Tables de l'algorithme Transvoxel -- copie de reference

Ce dossier porte le fichier **tel qu'il a ete telecharge**, avec sa licence.
Il n'est **pas compile** : c'est exactement pourquoi il vit ici et non sous
`Source/`. UBT compile tout `.cpp` qu'il trouve sous `Source/`, et une premiere
version de ce chantier l'y avait laisse -- il se compilait donc a chaque build,
pour rien, et le depot portait deux copies des tables dont une morte.

| | |
|---|---|
| origine | `https://github.com/EricLengyel/Transvoxel` |
| recupere le | 19 septembre 2026 |
| licence | MIT, `Copyright (c) 2009 Eric Lengyel` (fichier `LICENSE` ci-contre) |
| brevets | l'auteur declare l'algorithme **libre de tout brevet** (transvoxel.org) |

## Ce qui est reellement compile

`Source/Worldseed/Procedural/WorldseedTransvoxelTables.{h,cpp}`, obtenus par
**transformation scriptee** de `Transvoxel.cpp` -- jamais par recopie :

- les deux definitions de structure sont passees dans l'en-tete ;
- les sept tables ont recu `extern`, sans quoi `const` a portee de namespace a
  une liaison **interne** et l'edition de liens echouerait sur des symboles
  pourtant bien definis ;
- tout le reste est identique a l'octet pres.

**Le controle qui le prouve**, et qu'on peut rejouer :

```sh
diff <(grep -o '0x[0-9A-Fa-f]*' ThirdParty/Transvoxel/Transvoxel.cpp) \
     <(grep -o '0x[0-9A-Fa-f]*' Source/Worldseed/Procedural/WorldseedTransvoxelTables.cpp)
```

Il ne doit rendre que **deux `0x0F`** en moins : les masques des methodes
`GetTriangleCount`, partis dans l'en-tete avec leurs structures. Les **6 486**
autres litteraux sont identiques.

Une table de correspondance fausse produirait du maillage silencieusement faux,
et aucune relecture a l'oeil ne trouverait le chiffre change : c'est pour cela
que la verification est mecanique et que la copie de reference est conservee.
