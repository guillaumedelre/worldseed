// Worldseed - le bruit.
//
// L'ORIGINAL PYTHON A ETE SUPPRIME : ce fichier est la SEULE
// implementation. Il ne faut plus chercher de reference ailleurs, ni supposer
// qu'un autre fichier dit la meme chose autrement.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedRules.h"

/**
 * Bruits deterministes, portage au bit pres du module Python du generateur.
 *
 * Perlin par gradients sur reseau entier, hache a partir de la graine : aucune
 * table pre-calculee, donc aucune periodicite, et le meme couple (graine,
 * frequence) redonne toujours exactement le meme champ.
 *
 * Toute l'arithmetique est volontairement en float 32 bits et uint32, comme du
 * cote numpy. Passer en double donnerait un champ LEGEREMENT different.
 *
 * GRILLE RECTANGULAIRE. La carte a la forme de la surface deroulee d'une
 * sphere, donc NX = 2 NY. Les coordonnees de bruit sont normalisees par
 * (NY - 1) sur LES DEUX AXES : une cellule reste ainsi carree dans l'espace du
 * bruit, et X balaie 0..2 pendant que Y balaie 0..1. Normaliser chaque axe par
 * sa propre taille etirerait le relief exactement comme l'etait le globe.
 */
namespace WorldseedPerlin
{
	/** Angle de gradient pseudo-aleatoire, stable, pour chaque noeud du reseau. */
	WORLDSEED_API float HashAngle(int32 IX, int32 IY, int32 Seed);

	/** Bruit de Perlin 2D, coordonnees deja mises a l'echelle. Environ [-1..1]. */
	WORLDSEED_API float Perlin(float X, float Y, int32 Seed);

	/**
	 * Somme fractale sur une grille NX x NY indexee J * NX + I.
	 * GridX / GridY fournissent des coordonnees deja deplacees ; a nullptr, la
	 * grille normalisee est utilisee.
	 */
	WORLDSEED_API void FBM(TArray<float>& Out, int32 NX, int32 NY, float Frequency,
		int32 Octaves, int32 Seed, float Lacunarity = 2.0f, float Gain = 0.5f,
		const TArray<float>* GridX = nullptr, const TArray<float>* GridY = nullptr);

	/** Bruit a cretes, dans [0..1]. Donne les aretes et lignes de partage. */
	WORLDSEED_API void Ridged(TArray<float>& Out, int32 NX, int32 NY, float Frequency,
		int32 Octaves, int32 Seed, float Lacunarity = 2.0f, float Gain = 0.5f);

	/**
	 * fBm dont les coordonnees sont deplacees par un autre fBm. C'est ce qui
	 * casse l'aspect "patates regulieres" et donne des cotes decoupees.
	 */
	WORLDSEED_API void DomainWarpedFBM(TArray<float>& Out, int32 NX, int32 NY,
		float Frequency, int32 Octaves, int32 Seed, float WarpStrength,
		float WarpFrequency, float Lacunarity = 2.0f, float Gain = 0.5f);

	// ------------------------------------------------------------------------
	// Bruits echantillonnes SUR LA SPHERE.
	//
	// Un bruit 2D sur une carte deroulee ne se raccorde pas entre la colonne 0
	// et la derniere : on voit une couture verticale au meridien d'origine.
	// En evaluant un bruit 3D au point de la sphere correspondant a chaque
	// cellule, la longitude 0 et la longitude 360 tombent litteralement sur LE
	// MEME POINT — il n'y a plus de raccord a faire, la continuite est
	// structurelle.
	//
	// Contrepartie assumee : ces fonctions n'ont pas d'equivalent dans
	// noise.py, qui est 2D par construction. La parite au bit pres avec le
	// generateur Python est donc rompue pour les champs concernes.
	// ------------------------------------------------------------------------

	/** Bruit de Perlin 3D par gradients haches. Environ [-1..1]. */
	WORLDSEED_API float Perlin3D(float X, float Y, float Z, int32 Seed);

	// --------------------------------------------------- fractales PONCTUELLES
	//
	// LES VARIANTES CI-DESSOUS RENDENT UNE VALEUR, PAS UNE GRILLE, et c'est
	// toute la difference. FBMSphere et consorts remplissent un tableau de
	// NX * NY cellules : c'est ce qu'il faut pour une etape de la chaine, qui
	// traite le monde entier d'un coup. Un champ de densite voxel, lui, est
	// interroge en un POINT quelconque de l'espace, des millions de fois, et
	// depuis plusieurs fils a la fois. Il ne peut rien faire d'une grille.
	//
	// Ces deux fonctions sont pures : aucun etat, aucune allocation, la graine
	// est explicite. Elles s'appellent donc sans precaution depuis un fil de
	// travail.

	/**
	 * Somme fractale 3D en un point. Environ [-1..1].
	 *
	 * La frequence est en CYCLES PAR UNITE des coordonnees recues : appeler
	 * avec des metres et une frequence de 1/40 donne un motif de quarante
	 * metres. C'est a l'appelant de choisir son unite et de s'y tenir.
	 */
	WORLDSEED_API float Fbm3D(float X, float Y, float Z, float Frequency,
		int32 Octaves, int32 Seed, float Lacunarity = 2.0f, float Gain = 0.5f);

	/**
	 * Bruit a cretes 3D en un point, dans [0..1].
	 *
	 * Les cretes valent 1. C'est ce qui en fait l'outil des GALERIES : les
	 * surfaces ou le bruit approche 1 forment des tubes qui se croisent et se
	 * ramifient, la ou un fBm seuille donnerait des poches isolees.
	 */
	/**
	 * Bruit cellulaire de Worley : distances aux DEUX germes les plus proches.
	 *
	 * POURQUOI CETTE FONCTION EXISTE, ET CE QU'ELLE SERT A FAIRE. Perlin donne
	 * des blobs, le bruit a cretes donne des tubes ; aucun des deux ne sait
	 * faire un reseau de FRACTURES. Worley seme un germe par cellule et decrit
	 * l'espace par ses distances ; la difference F2 - F1 s'annule exactement sur
	 * la frontiere entre deux germes, donc sur la surface de Voronoi. Seuiller
	 * cette difference donne un reseau de PAROIS FINES, fermees et connexes --
	 * c'est la forme d'un systeme de diaclases.
	 *
	 * LA CONNEXITE EST ACQUISE PAR CONSTRUCTION, comme pour l'arbre couvrant des
	 * chambres : les faces d'un diagramme de Voronoi se touchent toutes par
	 * leurs aretes. On n'a donc pas a verifier qu'une fissure en rejoint une
	 * autre, ce qui serait un calcul non local.
	 *
	 * COUT : vingt-sept cellules visitees par evaluation. C'est l'operation la
	 * plus chere du champ de densite, elle doit etre gardee par des tests
	 * bon marche.
	 */
	WORLDSEED_API void Worley3D(float X, float Y, float Z, int32 Seed,
		float& OutF1, float& OutF2);

	WORLDSEED_API float Ridged3D(float X, float Y, float Z, float Frequency,
		int32 Octaves, int32 Seed, float Lacunarity = 2.0f, float Gain = 0.5f);

	/**
	 * Point de la sphere unite correspondant a une cellule de carte.
	 * La latitude passe par la correspondance equivalente-aire, donc les
	 * bandes climatiques restent a leur place.
	 */
	WORLDSEED_API FVector SpherePoint(const FWorldseedGeometry& Geo, int32 I, int32 J);

	/**
	 * Somme fractale sur la sphere. La frequence garde son sens : F cycles
	 * d'un pole a l'autre, comme dans la version plane.
	 */
	WORLDSEED_API void FBMSphere(TArray<float>& Out, const FWorldseedGeometry& Geo,
		float Frequency, int32 Octaves, int32 Seed,
		float Lacunarity = 2.0f, float Gain = 0.5f);

	/** Bruit a cretes sur la sphere, dans [0..1]. */
	WORLDSEED_API void RidgedSphere(TArray<float>& Out, const FWorldseedGeometry& Geo,
		float Frequency, int32 Octaves, int32 Seed,
		float Lacunarity = 2.0f, float Gain = 0.5f);

	/** fBm spherique dont le point d'echantillonnage est deplace en 3D. */
	WORLDSEED_API void DomainWarpedFBMSphere(TArray<float>& Out,
		const FWorldseedGeometry& Geo, float Frequency, int32 Octaves, int32 Seed,
		float WarpStrength, float WarpFrequency,
		float Lacunarity = 2.0f, float Gain = 0.5f);

	/** Interpolation lisse classique, bornee [0..1]. */
	WORLDSEED_API float Smoothstep(float Edge0, float Edge1, float X);

	/** Ramene un champ dans [0..1] ; champ constant -> 0.5. */
	WORLDSEED_API void Normalize01(TArray<float>& InOut);
}
