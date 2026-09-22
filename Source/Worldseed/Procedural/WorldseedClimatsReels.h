// Worldseed - les vingt-trois releves de stations reelles, et la case attendue.

#pragma once

#include "CoreMinimal.h"

#include "Procedural/WorldseedBiomes.h"

/**
 * UN RELEVE DE STATION REELLE, RAMENE AUX QUATRE GRANDEURS QUE LE
 * CLASSIFICATEUR LIT.
 *
 * D'OU VIENNENT CES VINGT-TROIS RELEVES. Ce sont les prereglages climatiques
 * livres avec Ultra Dynamic Sky, chacun citant sa station et sa source. Ils
 * sont la SEULE reference EXTERIEURE au projet : un monde procedural peut etre
 * parfaitement coherent avec lui-meme et faux par rapport a la Terre, et rien
 * d'interne ne peut le dire.
 */
struct WORLDSEED_API FWorldseedReleveReel
{
	/** La cle du prereglage, qui sert aussi d'identifiant au releve. */
	FString Cle;

	float TmoyC = 0.0f;
	float PluieMm = 0.0f;

	/** Moyenne de la saison la plus chaude, approximation du mois le plus chaud. */
	float TMaxC = 0.0f;

	/** Part des precipitations tombant au semestre chaud. */
	float FractionEte = 0.5f;

	/**
	 * Les deux cases acceptables, et il en faut deux.
	 *
	 * Certains climats reels sont a cheval par nature -- `Subpolar_Oceanic`
	 * tient de la taiga comme de la foret temperee, `Hot_Semi-Arid` du desert
	 * comme de la savane. Exiger une case unique ferait echouer le releve sur
	 * une frontiere qui n'a pas de raison d'etre tranchee.
	 */
	EWorldseedBiome AttenduA = EWorldseedBiome::Tundra;
	EWorldseedBiome AttenduB = EWorldseedBiome::Tundra;

	bool Accepte(EWorldseedBiome Case) const
	{
		return Case == AttenduA || Case == AttenduB;
	}
};

/**
 * LE BULLETIN TERRESTRE, EXTRAIT POUR AVOIR DEUX CONSOMMATEURS ET UNE SEULE
 * IMPLEMENTATION.
 *
 * Il vivait dans un namespace ANONYME de `WorldseedProbeTerre.cpp`, donc
 * inatteignable depuis un test. Le recopier aurait valide une COPIE de la
 * lecture des releves plutot que la lecture elle-meme -- exactement le defaut
 * que le portage de `terre.py` avait corrige en sortant
 * `WorldseedBiomes::FromClimate` de la boucle de `Classify` : la version Python
 * REIMPLEMENTAIT le diagramme de Whittaker et validait donc une copie du
 * classificateur, pas le classificateur.
 *
 * La sonde et le test appellent desormais les memes fonctions. Une divergence
 * entre eux est devenue impossible, ce qui est la seule garantie qui vaille.
 */
namespace WorldseedClimatsReels
{
	/**
	 * Le seuil de part estivale propre aux RELEVES, distinct de celui du moteur.
	 *
	 * DEUX SEUILS, ET LES CONFONDRE SERAIT UNE FAUTE. Notre part estivale est
	 * plus CONTRASTEE que la realite -- 0,15 a 0,21 entre 38 et 50 degres dans
	 * le monde genere, contre 0,24 a 0,30 mesures sur les villes
	 * mediterraneennes reelles -- parce que le modele de circulation est
	 * purement zonal : ni moderation maritime de la saisonnalite, ni asymetrie
	 * est/ouest des bassins oceaniques. Sur Terre, le mediterraneen est
	 * d'ailleurs un climat de FACADE OUEST, pas une ceinture.
	 *
	 * Le seuil du MOTEUR vaut 0,25 -- le rapport 1/3 de Koppen ramene a deux
	 * semestres. Celui-ci vaut 0,31, qui separe proprement les trois
	 * mediterraneens (0,243 / 0,289 / 0,299) de leurs voisins immediats,
	 * Oceanic a 0,427 et Humid_Subtropical a 0,413. Les unifier ferait basculer
	 * l'un ou l'autre, et le depot l'interdit sans refaire la mesure.
	 */
	WORLDSEED_API extern const float SeuilEteReleves;

	/** Le chemin du fichier de releves, relatif au projet. */
	WORLDSEED_API FString Chemin();

	/**
	 * Lit les vingt-trois releves et leur attache la case attendue.
	 *
	 * Rend faux si le fichier manque ou ne se lit pas ; `OutErreur` dit
	 * laquelle des deux. Les releves absents du fichier sont simplement omis,
	 * donc le compte rendu dit toujours combien ont ete lus.
	 */
	WORLDSEED_API bool Charger(TArray<FWorldseedReleveReel>& Out, FString& OutErreur);

	/**
	 * La case que NOTRE classificateur donne a ce releve.
	 *
	 * Le seuil des releves est applique ICI et nulle part ailleurs : un
	 * appelant qui l'oublierait ferait basculer les trois mediterraneens sans
	 * qu'aucun message ne le dise.
	 */
	WORLDSEED_API EWorldseedBiome Classer(const FWorldseedReleveReel& Releve,
		const FWorldseedBiomeRules& Regles);
}
