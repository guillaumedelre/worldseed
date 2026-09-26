// Worldseed - le generateur de noms : chaine de Markov sur 43 corpus.

#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

/**
 * UNE BASE DE NOMS : un corpus, et les reglages qui le font sonner.
 *
 * `Dupl` merite un mot : ce sont les lettres que CETTE langue autorise a
 * doubler -- `lt` en allemand, `nlrs` en francais, `aliuszrox` en draconique.
 * C'est ce qui distingue une langue d'une autre presque autant que son corpus,
 * et c'est pour cela que le reglage est PAR BASE et non global.
 *
 * ⚠ `Index` EST L'INDEX D'ORIGINE D'AZGAAR, ET IL EST PORTANT. Le systeme de
 * suffixes d'Etat teste des NUMEROS -- `Base == 2` pour le francais,
 * `Base > 32 && Base < 42` pour les bases de fantasy, qui n'en recoivent
 * aucun. Renumeroter casserait les suffixes EN SILENCE : les noms sortiraient,
 * simplement avec les mauvaises terminaisons.
 */
struct FWorldseedBaseDeNoms
{
	/** Le libelle d'Azgaar : « French », « Nordic », « Dwarven »... */
	FString Nom;

	/** Son index d'origine. Portant, voir ci-dessus. */
	int32 Index = 0;

	/** Longueurs voulues pour le mot produit. */
	int32 Min = 5;
	int32 Max = 12;

	/** Les lettres que cette langue autorise a doubler. */
	FString Dupl;

	/** Le corpus : 109 a 276 mots selon la base. */
	TArray<FString> Mots;

	/**
	 * La chaine de Markov, batie a la demande depuis `Mots`.
	 *
	 * Cle = la lettre precedente, ou la chaine VIDE pour un debut de mot.
	 * Valeur = les pseudo-syllabes qui peuvent suivre. Une syllabe VIDE est le
	 * marqueur de fin de mot -- c'est ainsi que l'original encode la
	 * terminaison, et la generation s'arrete en la tirant.
	 */
	TMap<FString, TArray<FString>> Chaine;

	bool bChainePrete = false;
};

/**
 * LE GENERATEUR DE NOMS, PORTE D'AZGAAR'S FANTASY MAP GENERATOR (MIT).
 *
 * Une CHAINE DE MARKOV sur des pseudo-syllabes, entrainee sur 43 corpus de
 * 109 a 276 mots -- 9 194 au total. Le decoupage en syllabes, la generation,
 * le post-traitement et les suffixes d'Etat sont portes a l'identique depuis
 * `src/generators/names-generator.ts` ; seule la source d'alea change.
 *
 * --- L'ALEA EST FOURNI, JAMAIS PRIS, ET C'EST LE POINT QUI DECIDE DE TOUT ----
 *
 * L'original tire sur `Math.random()`. Ici chaque fonction recoit son
 * `FRandomStream`, et l'appelant le graine sur le monde. Sans cela un lieu
 * changerait de nom a chaque fois qu'on rouvre la carte -- le joueur
 * reviendrait sur ses pas et trouverait un autre nom au meme endroit.
 *
 * C'est exactement la propriete que ce depot tient partout : meme graine, meme
 * monde. Un nom qui la trahirait se verrait plus qu'un rocher deplace, parce
 * qu'un nom, on s'en souvient.
 *
 * ET LE GRAINAGE SE FAIT SUR LA POSITION, jamais sur le tirage qui a pose la
 * chose nommee : consommer de l'alea dans une passe de semis decalerait tout
 * ce qui vient apres, et le monde entier changerait pour un nom.
 *
 * --- CE QUI A ETE REMPLACE, ET CE QU'ON Y PERD -------------------------------
 *
 * Ce module portait jusqu'au 26 septembre 2026 une GRAMMAIRE a motifs, portee
 * du projet Godot `terrain-3d` : `{grand} de {~}` donnait « les Marches de
 * Silael », « Villey-sur-Ance », « le Golfe de Port-Rouge ». Elle offrait
 * 39 187 noms de region -- mesure faite avant remplacement.
 *
 * DECISION DU PROPRIETAIRE : remplacer par Azgaar seul. Ce qu'on gagne est une
 * variete de racines sans limite et 43 langues au lieu de 11 ; ce qu'on perd
 * est le SYNTAGME francais -- Azgaar rend un MOT, et ses noms d'Etat se font
 * par suffixe agglutine (`-ia`, `-land`, `-terre`) et non par article. La
 * grammaire est gardee inactive dans `Content/Worldseed/Data/noms-grammaire.json`.
 */
namespace WorldseedNoms
{
	/**
	 * Charge les corpus. Sans effet s'ils le sont deja.
	 *
	 * RIEN N'EST FATAL : un monde sans noms reste jouable, et le journal le dit
	 * plutot que de laisser chercher. Rend faux et remplit `OutErreur` si le
	 * fichier manque ou ne se lit pas.
	 */
	WORLDSEED_API bool Charger(FString& OutErreur);

	/** Les libelles des bases, DANS L'ORDRE D'INDEX. Charge au besoin. */
	WORLDSEED_API const TArray<FString>& Ordre();

	/** Une base par son libelle, ou nul. Charge au besoin. */
	WORLDSEED_API const FWorldseedBaseDeNoms* Trouver(const FString& Nom);

	/**
	 * UN MOT, tire dans la chaine de la base donnee.
	 *
	 * C'est la brique de tout le reste : un nom de personne, un village et la
	 * racine d'un nom d'Etat sortent tous d'ici. `Min` et `Max` a zero prennent
	 * les valeurs de la base.
	 */
	WORLDSEED_API FString Mot(const FString& Base, FRandomStream& Rng,
		int32 Min = 0, int32 Max = 0);

	/** La variante COURTE, pour ce qui doit tenir sur une carte. */
	WORLDSEED_API FString MotCourt(const FString& Base, FRandomStream& Rng);

	/**
	 * UN NOM D'ETAT : une racine, plus le suffixe que sa langue appelle.
	 *
	 * `-ia` par defaut, `-land` en germanique et nordique, `-terre` en
	 * francais, `-maa` en finnois, `-orszag` en hongrois, `-yurt` en turc,
	 * « Guo » en chinois... et RIEN pour les onze bases de fantasy, qui portent
	 * deja leur couleur dans le mot.
	 *
	 * Passer une racine vide en fait tirer une.
	 */
	WORLDSEED_API FString Etat(const FString& Base, const FString& Racine,
		FRandomStream& Rng);

	/**
	 * Un nom de personne : un prenom, et un nom de famille si `bComplet`.
	 *
	 * ⚠ AZGAAR NE CONNAIT PAS LE GENRE. Rien dans ses corpus ne distingue le
	 * feminin du masculin. Le parametre n'existe donc plus : c'est une perte
	 * assumee du remplacement, et la grammaire qu'il remplace, elle, accordait
	 * le patronyme nordique -- « Ragnhild fille de Sigurd ».
	 *
	 * ⚠ ET LES 32 BASES REELLES SONT DES CORPUS DE TOPONYMES. « Achern »,
	 * « Aichhalden » sont des communes allemandes : un « personnage » allemand
	 * sort donc « Kellingen Openalbhau », qui sonne comme deux villages. C'est
	 * le comportement de l'original, qui emploie les memes corpus pour ses
	 * bourgs et ses habitants. Sans consequence pour Worldseed, dont le besoin
	 * porte sur les LIEUX ; a savoir le jour ou l'on nommera des gens.
	 */
	WORLDSEED_API FString Personne(const FString& Base, bool bComplet,
		FRandomStream& Rng);

	/** Vide les corpus. Les tests enchainent plusieurs etats dans un processus. */
	WORLDSEED_API void ViderLeCache();
}
