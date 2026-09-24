# Messages de commit — Conventional Commits 1.0.0

**Tout commit de ce dépôt suit la spécification [Conventional Commits
1.0.0](https://www.conventionalcommits.org/fr/v1.0.0/).**

```
<type>(<portée>): <description>

<corps : le POURQUOI et la MESURE>

<pied : références, co-auteurs>
```

## La ligne de sujet

Type **obligatoire**, portée **recommandée**, description à l'**impératif**, en
**minuscules**, **sans point final**, **72 caractères au plus**. Elle doit
compléter la phrase « ce commit va… ».

| type | quand l'employer |
|---|---|
| `feat` | une capacité nouvelle : un outil, une étape de la chaîne, un acteur |
| `fix` | un défaut corrigé, y compris une valeur de réglage fausse |
| `perf` | le rendu ou la génération vont plus vite, à résultat égal |
| `refactor` | le comportement ne change pas, la structure si |
| `docs` | README, CLAUDE.md, atlas, commentaires de règles |
| `build` | dépendances, `.gitignore`, `.gitattributes`, Git LFS, configuration du moteur |
| `chore` | ménage, suppression d'assets périmés, outillage annexe |
| `test` | contrôles et diagnostics |
| `revert` | annulation d'un commit précédent |

**Portées usuelles** — génération : `worldgen`, `climate`, `tectonics`,
`erosion`, `lithology`, `strates`, `cavites`, `biomes`, `surfaces`, `voxel`,
`carte`, `minimap`, `menu` ; éditeur et rendu : `landscape`, `water`, `pcg`,
`vegetation`, `uds`, `rvt`, `material` ; transverse : `rules` pour
`world_rules.json`, `docs`, `git`.

## Le point d'exclamation a un sens précis ici

Dans un projet de rendu, la rupture n'est pas une signature d'API : c'est **un
changement qui oblige à refaire quelque chose de long**. On marque donc
`type(portée)!:` et on ajoute un pied `BREAKING CHANGE:` dès qu'un commit :

- force à **régénérer le monde** — toute règle qui entre dans la tectonique,
  dans le climat avant les précipitations, dans l'érosion, ou qui change
  l'empreinte de `world_rules.json` ;
- force à **réimporter dans l'éditeur** — relief, couches peintes, carte des
  biomes, recettes de végétation ;
- change le **format d'un fichier échangé** ou du cache de monde.

**Le pied doit dire quoi REMESURER**, pas quoi relancer : le monde se régénère
tout seul dès que `WORLDSEED_PIPELINE_VERSION` change ou que l'empreinte des
règles bouge.

## Ce que la convention ne change pas, et qui compte plus qu'elle

Elle normalise la ligne de sujet ; **elle n'autorise pas à raccourcir le
corps**. Les messages de ce dépôt expliquent le **pourquoi** et citent la
**mesure qui a tranché**, y compris les pistes essayées puis rejetées pour
qu'on ne les retente pas. C'est la partie qui a de la valeur six mois plus
tard : *un sujet bien formé au-dessus d'un corps vide est une régression, pas
un progrès.*

Trois réflexes qui viennent de défauts payés comptant :

1. **Citer le chiffre, pas l'impression.** « plus rapide » ne vaut rien ;
   « 14,9 s → 3,4 s » se vérifie.
2. **Ne pas annoncer ce qu'on n'a pas vérifié.** Un corps de commit de ce dépôt
   a déjà annoncé « dix vues sur dix » alors que cinq seulement avaient été
   regardées, et un autre a comparé deux relevés pris dans deux états
   différents du code. Un fichier écrit n'est pas une vue jugée.
3. **Dire la piste abandonnée et pourquoi.** C'est ce qui empêche de la
   reprendre dans six mois.

## Exemple

```
fix(climate)!: ancrer la pluie sur la moyenne et non sur la mediane

715 mm est la MOYENNE terrestre, pas la mediane, et la distribution des pluies
est tres dissymetrique. Le monde etait 70 % trop humide -- moyenne mesuree
1217 mm -- et portait 23,7 % de ses terres au-dessus de 2000 mm quand la Terre
en a 7 a 8. Consequence visible : trop de forets, pas assez de prairies.

Essaye et rejete : monter saturationScaleC de 16 a 24, qui ameliore le score
mais s'ecarte de Clausius-Clapeyron et deplace le relief.

BREAKING CHANGE: le monde doit etre regenere. Remesurer le bulletin terrestre
(ProbeTerre) et les parts de biomes (ProbeBiomes) avant de conclure.
```

## Le pied

Terminer le message par la ligne de co-auteur demandée par l'outillage :

```
Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
```

---

*Le récit des commits — ce qu'ils ont corrigé, et les mesures qui ont tranché —
vit dans `CLAUDE.md`. Ici ne se trouve que la consigne.*
