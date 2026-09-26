# Les tables de noms

## `noms.json` — 43 bases, 9 194 mots

Extrait de **[Azgaar's Fantasy Map Generator](https://github.com/Azgaar/Fantasy-Map-Generator)**,
fichier `src/data/name-bases.ts`, le 26 septembre 2026.

**Licence MIT** — l'obligation d'attribution est satisfaite par ce fichier :

> MIT License
>
> Copyright (c) 2017 Azgaar
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

### Ce que contient chaque base

| champ | sens |
|---|---|
| `nom` | le libellé d'Azgaar — `French`, `Nordic`, `Dwarven`… |
| `i` | son index d'origine, **conservé** : le système de suffixes d'État le teste par NUMÉRO (`base === 2` pour le français, `base > 32 && base < 42` pour les bases fantasy). Renuméroter casserait les suffixes en silence. |
| `min` / `max` | longueur du mot à produire |
| `dupl` | les lettres que CETTE base autorise à doubler — `lt` en allemand, `nlrs` en français, `aliuszrox` en draconique. C'est ce qui distingue une langue d'une autre autant que son corpus. |
| `mots` | le corpus, 109 à 276 mots par base |

**Trente-deux bases réelles** (dont `French`, `Nordic`, `Arabic`, `Japanese`,
`Inuit`, `Nahuatl`, `Swahili`, `Quechua`…) et **onze bases de fantasy**
(`Elven`, `Dark Elven`, `Dwarven`, `Goblin`, `Orc`, `Giant`, `Draconic`,
`Arachnid`, `Serpents`, `Human Generic`, `Levantine`).

### La conversion se rejoue

`Tools/Noms/extraire.pl` relit `name-bases.ts` et réécrit ce JSON. **Une table
recopiée à la main est une table fausse** — 9 194 mots ne se relisent pas à
l'œil, et c'est la même règle que pour les tables Transvoxel
(`ThirdParty/Transvoxel/PROVENANCE.md`).

---

## Ce qui a été remplacé, et ce qu'on y a perdu

Jusqu'au 26 septembre 2026, ce dossier portait `noms-grammaire.json` : le
portage en C++ du générateur du projet Godot `terrain-3d` — onze univers de
sonorités et une **grammaire à motifs** (`{grand} de {~}`) qui produisait des
syntagmes français :

```
les Marches de Silael   ·   Villey-sur-Ance   ·   le Golfe de Port-Rouge
Ragnhild fille de Sigurd   ·   Torvald fils de Hakon
```

**Remplacé sur décision du propriétaire, et supprimé à sa demande.** Sa
combinatoire avait été mesurée avant l'arbitrage : 39 187 noms de région,
4 458 villages, 1 691 910 personnages.

Ce qu'Azgaar apporte en échange : une variété de racines sans limite, 43
langues au lieu de 11. Ce qu'il ne fait pas : le **syntagme**. Il rend un MOT,
et ses noms d'État se forment par suffixe agglutiné (`-ia`, `-land`, `-terre`,
`-maa`, `-orszag`, « Guo »), jamais par article. Il ne connaît pas non plus le
genre.

⚠ **Le fichier n'a jamais été commité : il n'est pas récupérable par git.** Ce
qui en survit est ce paragraphe et la section « Le générateur de noms : cinq
outils comparés » de `CLAUDE.md`, qui garde la mesure et le raisonnement. Le
refaire demanderait de rouvrir le projet Godot `terrain-3d`, où la grammaire
d'origine, elle, existe toujours.
