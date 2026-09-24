# Material Symbols — la police d'icones du projet

**LA REGLE EST DANS `.claude/rules/iconographie.md`**, et elle nomme DEUX
sources : Material Symbols pour l'interface, game-icons.net pour le monde. Ce
fichier-ci ne couvre que la premiere, et dit d'ou elle vient. (Il a longtemps
porte « toutes les icones viennent de cette police » : c'etait vrai le jour ou
elle est arrivee, et faux des qu'une seconde source a ete admise. Deux textes
qui se contredisent valent moins qu'un seul.)

Une police d'icones plutot que des images : un glyphe est vectoriel, donc net a
toutes les tailles et a tous les facteurs DPI, il se teinte comme du texte, et
il ne coute ni asset, ni atlas, ni import.

**C'est aussi ce que fait Epic** : l'editeur d'Unreal rend ses propres icones
avec `FontAwesome.ttf` et une table de glyphes nommes,
`Engine/Source/Editor/EditorStyle/Public/EditorFontGlyphs.h`, dont chaque entree
est un simple `FText::FromString(FString(TEXT("\xf100")))`. Notre
`WorldseedIcones.h` suit ce modele.

## Ce qui est ici

| fichier | octets | SHA-256 |
|---|---|---|
| `MaterialSymbolsOutlined.ttf` | 10 678 048 | `3E0CB55EC6AC697B2F47609F4EFDF9A016E7672B719061BC6AB57BBCE6E3EEA3` |
| `MaterialSymbols.codepoints` | 79 029 | `C18564F64D7D92DD3A6895A2C59EA69ADFB56D6F553BCBBC88811C328159D715` |
| `LICENSE` | 11 357 | `58D1E17FFE5109A7AE296CAAFCADFDBE6A7D176F0BC4AB01E12A689B0499D8BD` |

Telecharges le 23 septembre 2026 depuis
`https://raw.githubusercontent.com/google/material-design-icons/master/` :

    variablefont/MaterialSymbolsOutlined[FILL,GRAD,opsz,wght].ttf   -> MaterialSymbolsOutlined.ttf
    variablefont/MaterialSymbolsOutlined[FILL,GRAD,opsz,wght].codepoints -> MaterialSymbols.codepoints
    LICENSE

**LE FICHIER EST RENOMME A DESSEIN.** Le nom d'origine porte des crochets et des
virgules, qui sont des caracteres de motif pour les globs, pour `FParse` et pour
la moitie des outils en ligne de commande. Un chemin de police n'a pas besoin de
ce risque.

**Licence Apache 2.0**, donc redistribuable dans un depot public comme dans un
jeu compile, a condition de conserver le `LICENSE` ci-contre. C'est pourquoi il
est versionne avec la police et non renvoye a un lien.

## Ce qu'il faut savoir avant d'y toucher

**C'est une police VARIABLE**, avec quatre axes — `FILL`, `GRAD`, `opsz`,
`wght`. Slate n'expose pas les axes variables : FreeType instancie l'axe par
defaut, ce qui donne le style « contour, non rempli, graisse 400 ». C'est
exactement le style voulu. **Si le rendu decevait**, le repli est
`font/MaterialIcons-Regular.ttf` du meme depot -- 349 Ko, statique, 2100 icones
au lieu de 4284 -- et il ne demanderait que de changer le nom de fichier et les
codepoints.

**4284 icones, et plusieurs noms partagent un glyphe.** Mesure :
`place` et `location_on` valent tous deux `F1DB` ; `landscape` et `terrain`
valent tous deux `E564`. Chercher un nom dans `.codepoints` peut donc rendre un
codepoint deja employe sous un autre nom -- ce n'est pas une erreur.

**On n'embarque PAS les 4284 codepoints dans le code.** `WorldseedIcones.h` ne
nomme que les icones reellement employees. Le fichier `.codepoints` reste ici
pour deux raisons : c'est la source ou l'on va chercher la suivante, et c'est
contre lui que l'oracle `Worldseed.Icones.*` verifie que chaque codepoint de
notre table existe vraiment dans la police livree.

## Le piege du packaging

**Un `.ttf` n'est pas un `.uasset` : il n'est pas embarque tout seul dans un
build cuit.** Il faut le declarer dans `Config/DefaultGame.ini` :

    [/Script/UnrealEd.ProjectPackagingSettings]
    +DirectoriesToAlwaysStageAsUFS=(Path="Worldseed/Fonts")

Sans cette ligne tout marche dans l'editeur et en PIE, et les icones
disparaissent -- silencieusement -- dans le seul build final.
