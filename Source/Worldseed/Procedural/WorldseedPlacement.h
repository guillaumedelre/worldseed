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
/**
 * Comment le choix du depart s'est termine.
 *
 * LES TROIS ISSUES NE SE RESSEMBLENT PAS, et les confondre a deja coute. On a
 * DEPLACE le joueur vers du plat, on a TENU son point, ou l'on n'a rien trouve
 * du tout : les deux dernieres se lisaient pareil -- « pose sur place » --
 * alors que l'une est un choix et l'autre un echec.
 */
enum class EWorldseedDepart : uint8
{
	/** Aucune recherche : on tient le point vise, pente non bornee. */
	Exact,

	/** La recherche a trouve du plat, et le joueur y a ete deplace. */
	SolPlatTrouve,

	/**
	 * ECHEC FRANC : rien de tenable dans la borne, on TIENT le point vise.
	 *
	 * ARBITRAGE DU PROPRIETAIRE, 22 septembre 2026. Il y avait ici un repli
	 * vers la recherche LARGE -- douze degres, aucune borne d'altitude -- et
	 * c'etait une FALAISE DE POLITIQUE : quarante metres, puis l'infini, en un
	 * cran. Qui visait un sommet naissait a son pied, 182 metres plus bas.
	 */
	PointViseTenu,

	/** Rien trouve, et le point n'etait pas choisi : on pose sur place. */
	Echec,
};

/** Ce que la recherche du depart demande a savoir. */
struct WORLDSEED_API FWorldseedDepartRegles
{
	/** Le point vient-il du menu ou de la ligne de commande ? */
	bool bChoisi = false;

	/** `-WorldseedDepartExact=1` : on tient le point, quoi qu il en coute. */
	bool bExact = false;

	/** Pente maximale pour un depart CHOISI, en degres. */
	float PenteChoisieMaxDeg = 25.0f;

	/** Ecart d altitude tolere pour un depart CHOISI, en metres. */
	float EcartAltitudeMaxM = 40.0f;

	/** Pente maximale pour une naissance LIBRE, ou l endroit importe peu. */
	float PenteLibreMaxDeg = 12.0f;

	/** Amplitude dont le champ deplace la surface : le plancher en depend. */
	float MargeDeplacementM = 12.0f;
};

/** Ou naitre, et par quel chemin on y est arrive. */
struct WORLDSEED_API FWorldseedDepart
{
	double XM = 0.0;
	double YM = 0.0;
	float SurfaceM = 0.0f;
	float PenteDeg = 0.0f;

	EWorldseedDepart Issue = EWorldseedDepart::Echec;

	/** A-t-on trouve une terre emergee, et ou ? */
	bool bTerreTrouvee = false;
	double TerreXM = 0.0;
	double TerreYM = 0.0;

	/** L altitude du point DEMANDE, avant toute correction. */
	float SurfaceDemandeeM = 0.0f;

	/**
	 * La colonne est-elle CREUSE sous le point retenu ?
	 *
	 * Elle ne fait pas echouer le choix -- l echec franc tient le point quoi
	 * qu il arrive -- mais l appelant doit pouvoir le DIRE : le joueur peut
	 * tomber dans une cavite, et personne ne saurait pourquoi.
	 */
	bool bColonneCreuse = false;
};

namespace WorldseedPlacement
{
	/**
	 * OU NAITRE. Le point, l issue, et de quoi la journaliser.
	 *
	 * L ORDRE EST LA REGLE, et chaque etape a sa raison :
	 *
	 *   1. la TERRE EMERGEE la plus proche -- ce monde est de l ocean a 70,8 %,
	 *      et laisser la fouille fine, qui ne porte qu a 384 m, ramener un
	 *      joueur du large ne marcherait jamais ;
	 *   2. puis l un des TROIS regimes : exact, choisi, ou libre.
	 *
	 * ELLE NE CONNAIT NI PION NI COMPOSANT : un champ, une grille, des
	 * altitudes et six reglages. C est ce qui la rend eprouvable -- l echec
	 * franc demandait jusqu ici de forcer `-WorldseedEcartDepart=1` en jeu pour
	 * seulement voir le chemin s executer.
	 */
	WORLDSEED_API FWorldseedDepart Choisir(const FWorldseedDensity& Champ,
		const FWorldseedGeometry& Geo, const TArray<float>& Heights,
		const FVector2D& DemandeM, const FWorldseedDepartRegles& R);
}
