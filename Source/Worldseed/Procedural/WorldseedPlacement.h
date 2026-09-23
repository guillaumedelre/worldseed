// Worldseed - ou poser le joueur : pente, sol plein, recherche de sol plat.

#pragma once

#include "CoreMinimal.h"

class FWorldseedDensity;
struct FWorldseedGeometry;

/**
 * OU POSER QUELQU'UN SUR UN TERRAIN VOXEL.
 *
 * POURQUOI CE N'EST PLUS UNE METHODE DE L'ACTEUR. Ces trois fonctions ne
 * lisent QUE le champ de densite : ni l'acteur, ni le monde, ni le pion. Les
 * garder dans `AWorldseedVoxelTerrain` -- deux mille quatre cents lignes -- les
 * rendait inatteignables depuis un test, alors qu'elles decident de l'endroit
 * ou le joueur nait, c'est-a-dire de la premiere chose qu'il voit.
 *
 * Elles sortent donc, et la recherche se prouve desormais sur un relief
 * FABRIQUE dont on connait la pente au degre pres, plutot qu'en relancant le
 * jeu et en lisant le journal.
 */
namespace WorldseedPlacement
{
	/** Ecart des sondes de pente, en metres. */
	inline constexpr double SondePenteM = 6.0;

	/** Profondeur sur laquelle on exige du plein sous les pieds, en metres. */
	inline constexpr double ProfondeurPleinM = 12.0;

	/**
	 * Pente du terrain en un point, en degres.
	 *
	 * Lue sur la SURFACE du champ et non sur la grille 2D : celle-ci a des
	 * cellules de quinze metres quand le voxel travaille au metre, et une
	 * paroi taillee par le voxel n'y figure pas.
	 */
	WORLDSEED_API float PenteDeg(const FWorldseedDensity& Champ,
		double XM, double YM);

	/**
	 * Vrai s'il y a de la roche sous les pieds sur toute la hauteur d'une
	 * galerie typique.
	 *
	 * UNE COLONNE SUR HUIT PORTE UNE GALERIE dans ce monde, et naitre
	 * au-dessus revient a tomber dedans. Un plancher de deux metres au-dessus
	 * d'un vide ne tient pas.
	 */
	WORLDSEED_API bool SolPlein(const FWorldseedDensity& Champ,
		double XM, double YM, double SurfaceM);

	/**
	 * Cherche un sol praticable autour d'un point, en spirale carree.
	 *
	 * Elle retient le PREMIER point acceptable, donc le plus proche, et non le
	 * meilleur du monde. `EcartAltitudeMaxM` nul signifie « pas de borne ».
	 *
	 * Rend faux si rien ne convient dans les 384 metres fouilles.
	 */
	WORLDSEED_API bool SolPlat(const FWorldseedDensity& Champ,
		const FVector2D& AutourM, float PenteMaxDeg,
		float EcartAltitudeMaxM, float AltitudeRefM,
		double& OutX, double& OutY, float& OutSurfaceM, float& OutPenteDeg);

	/**
	 * La cellule de TERRE EMERGEE la plus proche, sur la grille 2D.
	 *
	 * ELLE PRECEDE `SolPlat`, ELLE NE LE REMPLACE PAS, et les deux repondent a
	 * deux questions differentes : celle-ci cherche le bon CONTINENT sur la
	 * grille du monde, celle-la le bon PIED sur le champ de densite. Poser un
	 * joueur en pleine mer et laisser `SolPlat` fouiller un voisinage de
	 * trois cent quatre-vingts metres ne le ramenerait pas a terre.
	 *
	 * ELLE EXIGE UN VOISINAGE EMERGE, pas seulement une cellule : sinon elle
	 * retient volontiers un recif ou une pointe de sable, ou rien ne tient.
	 */
	WORLDSEED_API bool TerreEmergeeLaPlusProche(const FWorldseedGeometry& Geo,
		const TArray<float>& Heights, const FVector2D& AutourM,
		float MargeDeplacementM, double& OutX, double& OutY);
}
