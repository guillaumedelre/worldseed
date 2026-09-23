#pragma once

#include "CoreMinimal.h"

/**
 * TENIR DES PROPORTIONS DEMANDEES, SUR UNE DISTRIBUTION QUELCONQUE.
 *
 * UN SEUIL N'EST PAS UNE PART, et ce depot l'a paye comptant. Le reglage des
 * diaclases s'appelait `diaclaseZonePct` et valait 0,16 ; le code en tirait un
 * seuil en supposant le bruit UNIFORME sur [-1..1]. Un Perlin ne l'est pas, il
 * se masse autour de zero : le seuil 0,68 cense garder 16 % n'en gardait que
 * 1,59. Meme famille d'erreur que les 715 mm pris pour une mediane.
 *
 * LA REPONSE EST LE QUANTILE, et il ne suppose rien de la distribution : on
 * demande « la valeur sous laquelle tombent 45 % de l'echantillon », et l'on
 * obtient 45 %, que le bruit soit uniforme, gaussien ou tordu. Les parts
 * demandees sont alors tenues quelle que soit la frequence ou le nombre
 * d'octaves du bruit -- ce qui compte, puisque ces deux reglages bougent.
 *
 * ET L'ON ECHANTILLONNE SUR LE DOMAINE AUQUEL LES SEUILS S'APPLIQUENT, jamais
 * plus large. C'est la lecon payee deux fois par la lithologie : le premier
 * calage du socle demandait 45 / 35 / 20 et rendait 40,8 / 38,1 / 21,1, pour
 * avoir echantillonne toute la terre continentale alors que la regle du socle
 * en emportait une partie juste apres. Ce module ne peut pas s'en charger --
 * c'est l'appelant qui sait quel domaine il trie -- mais c'est la faute a
 * eviter chaque fois qu'on l'emploie.
 */
namespace WorldseedParts
{
	/**
	 * Les seuils qui decoupent un echantillon selon des parts demandees.
	 *
	 * Rend `NbClasses - 1` seuils croissants : `Classe()` les lit ensuite. Les
	 * parts n'ont pas a etre normalisees, la somme s'en charge ; une somme
	 * nulle est plancheee plutot que de diviser par zero.
	 *
	 * Rend un tableau VIDE quand il n'y a rien a decouper -- echantillon vide,
	 * ou moins de deux classes. `Classe()` rend alors 0 partout, donc la
	 * premiere classe : un decoupage impossible degenere en « tout pareil »,
	 * jamais en lecture hors bornes.
	 */
	WORLDSEED_API void Seuils(const TArray<float>& Echantillon,
		const TArray<float>& Parts, int32 NbClasses, TArray<float>& OutSeuils);

	/**
	 * La classe d'une valeur, lue dans des seuils croissants.
	 *
	 * Rend un indice dans `[0 .. Seuils.Num()]`, donc `NbClasses - 1` au plus
	 * quand les seuils viennent de `Seuils()`. L'appelant garde la
	 * responsabilite de le borner a sa propre table : c'est lui qui sait
	 * combien elle porte d'entrees, et un fichier de regles peut toujours en
	 * decrire moins que de parts.
	 */
	WORLDSEED_API int32 Classe(float Valeur, const TArray<float>& Seuils);
}
