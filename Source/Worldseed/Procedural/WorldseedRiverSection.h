// Worldseed - coupe transversale d'un cours d'eau, sur demande.

#pragma once

#include "CoreMinimal.h"

struct FWorldseedGeometry;
struct FWorldseedHydrology;

/**
 * Repond a trois questions que ni une moyenne ni une capture ne tranchent :
 * le chenal existe-t-il, l'eau le remplit-elle, et ou la berge la coupe.
 *
 * POURQUOI UNE SONDE PONCTUELLE. Un releve global dit "lame de 1,1 m en
 * moyenne" — ce qui ne designe ni quelle riviere, ni quel endroit, ni
 * pourquoi. Une capture d'ecran, elle, ne porte aucune coordonnee : rien n'y
 * relie ce qu'on voit a ce qui est mesure. Entre les deux, il manquait le
 * moyen de demander "ici, que se passe-t-il ?".
 *
 * La coupe se prend PERPENDICULAIREMENT au cours le plus proche, sur le
 * relief tel qu'Unreal l'affiche — celui contre lequel l'eau se decoupe.
 */
namespace WorldseedRiverSection
{
	/**
	 * Trouve le point du relief vise depuis une camera, sans collision.
	 *
	 * ON MARCHE LE HEIGHTFIELD PLUTOT QUE DE TRACER. Les chunks lointains
	 * n'ont pas de collision — seul le niveau de detail le plus fin en porte —
	 * donc une trace physique ne toucherait rien au-dela de quelques centaines
	 * de metres. Marcher le relief sonde en plus exactement les donnees qu'on
	 * cherche a comprendre, et non une approximation de collision.
	 *
	 * Faux si le rayon ne rencontre jamais le sol.
	 */
	WORLDSEED_API bool TraceGround(const TArray<float>& ElevationM,
		const FWorldseedGeometry& Geometry, float HeightExaggeration,
		const FVector& From, const FVector& Direction, FVector& OutHit);

	/**
	 * Releve et trace la coupe du cours le plus proche d'un point.
	 *
	 * Ecrit le profil dans le journal et le dessine en place : gris le relief,
	 * bleu la lame d'eau, rouge la ou le sol emerge de l'eau.
	 */
	WORLDSEED_API void Probe(UWorld* World, const TArray<float>& ElevationM,
		const FWorldseedGeometry& Geometry, const FWorldseedHydrology& Hydrology,
		float HeightExaggeration, const FVector& WorldPoint);

	/**
	 * Prend la coupe de TOUS les points du reseau, et en fait un releve.
	 *
	 * LA SONDE MANUELLE NE SERT QU'A REGARDER. Les donnees sont toutes la des
	 * la generation : demander sept coupes a la main quand on peut en prendre
	 * mille cinq cents revient a echantillonner au hasard un defaut qu'on
	 * pourrait cerner exactement.
	 *
	 * Le releve donne la distribution — lame, largeur mouillee, position de
	 * berge — puis NOMME les pires cas avec leurs coordonnees, pour qu'on
	 * puisse aller les voir au lieu de les chercher.
	 */
	WORLDSEED_API void Sweep(const TArray<float>& ElevationM,
		const FWorldseedGeometry& Geometry, const FWorldseedHydrology& Hydrology);
}
