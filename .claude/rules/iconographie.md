# Iconographie des écrans

**Les icônes de ce projet viennent de deux sources, et de deux seulement.**

| source | pour quoi | forme | licence |
|---|---|---|---|
| [fonts.google.com/icons](https://fonts.google.com/icons) — Material Symbols | l'**interface** : actions, navigation, états, réglages | une **police**, désignée par codepoint | Apache 2.0 |
| [game-icons.net](https://game-icons.net/) | le **monde** : lieux, relief, souterrain, faune, objets | un **PNG** exporté du SVG, chargé comme image | CC BY 3.0 — **attribution par icône** |

La ligne de partage n'est pas un goût, c'est un manque mesuré : sur les
**4284 entrées** du catalogue Material Symbols il n'existe **ni arche, ni
grotte, ni cratère, ni canyon**. Faute de mieux, la carte a d'abord désigné une
arche par `all_inclusive` — le signe ∞ — et un canyon par `terrain`, une
montagne. game-icons comble exactement ce trou : *cave entrance*, *earth crack*,
*valley*, *sea cliff*, *monument valley*.

## Les cinq règles

1. **Aucun `.uasset`.** Le fichier brut est versionné sous
   `Content/Worldseed/`, lu au démarrage, et porte sa licence à côté. Un
   `UFont` ou une `Texture2D` importée serait un binaire opaque au diff ; un
   `.ttf` et un `.png` se lisent, se remplacent à la main, et GitHub affiche le
   second dans une comparaison.
2. **Une icône se nomme par ce qu'elle désigne CHEZ NOUS**, jamais par son nom
   d'origine : `WorldseedIcone::Arche`, pas `AllInclusive`. Le nom de la source
   reste en commentaire — c'est lui qu'il faut pour en chercher une autre.
3. **Le choix se juge À L'IMAGE**, aux deux bouts du zoom. `door_open` pour une
   arche rendait un rectangle barré, illisible à vingt-deux pixels. Et une
   épingle **désigne par sa pointe**, pas par son centre.
4. **On n'embarque que les icônes employées.** La table C++ en nomme une
   quinzaine, pas quatre mille.
5. **Pas de troisième source.** S'il en faut une un jour, c'est un arbitrage à
   prendre, pas une exception à glisser.

## Comment on en ajoute une

**Material Symbols** — la police est déjà là.

1. Trouver l'icône sur le site, relever son **nom exact**.
2. Lire son codepoint dans `Content/Worldseed/Fonts/MaterialSymbols.codepoints`
   (une ligne `nom codepoint`), et l'ajouter à `WorldseedIcone` dans
   `Source/Worldseed/Procedural/WorldseedIcones.h`, avec le nom Google en
   commentaire.
3. Ajouter l'entrée à la table de `WorldseedIcones.cpp` : l'oracle
   `Worldseed.Icones.LesCodepointsExistentDansLaPolice` confronte chaque
   codepoint au catalogue livré. **Une faute d'un seul chiffre donne un autre
   glyphe, parfaitement dessiné, et rien ne la signale** — d'où cet oracle.

*Plusieurs noms partagent un glyphe* : `place` et `location_on` valent tous deux
`F1DB`, `landscape` et `terrain` tous deux `E564`. Tomber sur un codepoint déjà
employé sous un autre nom n'est donc pas une erreur.

**game-icons.net** — le SVG n'est pas lisible par Unreal.

1. Exporter en **PNG blanc sur alpha, 256 px** (le Studio du site le fait) vers
   `Content/Worldseed/Icons/`. C'est très au-delà d'un marqueur de 22 px, même
   à DPI 2. `*.png` est déjà en Git LFS.
2. **Relever l'auteur** — Lorc, Delapouite, Skoll ou l'un des cinquante autres —
   et l'inscrire dans `Content/Worldseed/Icons/PROVENANCE.md` :
   « Icons made by {author}. Available on https://game-icons.net ».
   **CC BY 3.0 sur un dépôt public n'est pas optionnel.**
3. Charger la brosse comme le moteur le fait pour ses écrans de chargement
   (`PreLoadSettingsContainer.cpp:483`) :

   ```cpp
   new FSlateDynamicImageBrush(*Chemin, Taille);
   FSlateApplication::Get().GetRenderer()->GenerateDynamicImageResource(*Chemin);
   ```

   **Sans le second appel, le fichier n'est jamais lu.** La brosse se garde :
   Slate conserve le pointeur qu'on lui donne.

## Le piège qui ne se voit qu'au build final

**Un fichier qui n'est pas un `.uasset` n'entre pas tout seul dans un build
cuit.** Tout marche dans l'éditeur et en PIE, et les icônes disparaissent du
seul build final — sans erreur. Chaque dossier doit être déclaré dans
`Config/DefaultGame.ini` :

```ini
[/Script/UnrealEd.ProjectPackagingSettings]
+DirectoriesToAlwaysStageAsUFS=(Path="Worldseed/Fonts")
+DirectoriesToAlwaysStageAsUFS=(Path="Worldseed/Icons")
```

## Si une icône manque

Ne pas détourner un glyphe qui dit autre chose, et ne pas dessiner la sienne :
chercher dans l'autre source. C'est précisément pour cela qu'il y en a deux.

---

*Pourquoi une police plutôt que des images, et ce que chaque piège a coûté :
`Content/Worldseed/Fonts/PROVENANCE.md` et la section « La police d'icônes » de
`CLAUDE.md` (23 septembre 2026).*
