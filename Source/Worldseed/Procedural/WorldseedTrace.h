// Worldseed - marqueurs de profilage : rendre ce projet visible a Insights.

#pragma once

#include "ProfilingDebugging/CpuProfilerTrace.h"

/**
 * UN MARQUEUR DE PORTEE POUR UNREAL INSIGHTS.
 *
 * POURQUOI CET EN-TETE EXISTE, ET CE QU'IL CORRIGE. Jusqu'au 22 septembre 2026
 * ce depot ne portait AUCUN marqueur de profilage -- zero dans quarante mille
 * lignes. Consequence : quand on ouvrait une trace, tout Worldseed etait
 * INVISIBLE ; on ne voyait que les marqueurs du moteur, et la seule mesure
 * disponible etait un agregat.
 *
 * C'est exactement ce qui a rendu possible le piege le plus couteux du projet.
 * L'editeur non focalise bride son rendu, l'outillage rapportait alors une
 * trame plafonnee en l'attribuant au GPU, et « une journee entiere de
 * conclusions de performance a ete batie dessus » -- avec des chiffres
 * plausibles, stables et reproductibles. Un agregat ne se corrige pas, il se
 * DECOMPOSE : c'est la regle que ce depot a deja ecrite quatre fois, apres les
 * quatre corrections du routage des galeries qui n'ont jamais bouge le
 * chiffre, apres le releve du cache qui melangeait quatre passes, et apres le
 * tri du drainage qu'un total seul ne pouvait pas designer.
 *
 * CE QUE CA COUTE : rien. En build final, `TRACE_CPUPROFILER_EVENT_SCOPE` se
 * reduit a du vide (CpuProfilerTrace.h:532) ; en developpement il ne coute que
 * si une trace tourne. Il n'y a donc aucun arbitrage a prendre -- on en pose
 * partout ou une passe a un nom.
 *
 * POURQUOI UNE MACRO A NOUS PLUTOT QUE CELLE D'EPIC EN DIRECT. Pour que le
 * PREFIXE soit impose par le code et non retenu de memoire. Dans une trace,
 * une passe nommee `Erosion` se perd au milieu de celles du moteur ;
 * `Worldseed_Erosion` se filtre d'un mot. Le nom du marqueur doit se lire
 * comme la ligne de journal correspondante.
 *
 * USAGE, en premiere ligne du corps de la passe :
 *
 *     WORLDSEED_TRACE(Erosion);
 *
 * Le nom est un IDENTIFIANT, pas une chaine : la macro d'Epic le transforme
 * elle-meme en texte, et lui passer `TEXT("...")` ajouterait des guillemets
 * dans la trace.
 */
#define WORLDSEED_TRACE(Nom) TRACE_CPUPROFILER_EVENT_SCOPE(Worldseed_##Nom)
