// Worldseed - les lames de gres, et les fentes qui les decoupent.

#pragma once

#include "CoreMinimal.h"

class UWorldseedRules;

/**
 * Section "lames" de world_rules.json.
 *
 * POURQUOI CE CHAMP EXISTE. Les arches de gres -- Arches National Park en est
 * l'archetype -- ne sont PAS taillees par la pente. Elles naissent de joints
 * verticaux PARALLELES, espaces de quelques dizaines de metres, que l'eau
 * elargit : la roche restee entre deux joints devient une LAME, un mur mince et
 * long, et c'est seulement dans une lame qu'une ouverture traversante peut se
 * creuser.
 *
 * SANS CE MECANISME IL N'Y A PAS D'ARCHE ICI, et ce n'est pas une opinion.
 * Mesure sur 6327 sondages du monde de 64 x 32 km : 229 cretes assez minces
 * pour etre percees, dont 135 en granite et 94 en basalte -- et ZERO en
 * calcaire, gres ou schiste. La raison est mecanique : la boucle soulevement /
 * erosion donne au granite 33,5 degres de pente et au gres 8,7 ; une pente
 * raide fait une crete mince, une pente douce n'en fait aucune. La roche des
 * arches reelles est donc precisement celle qui, chez nous, n'a plus de relief.
 *
 * ET CE N'EST PAS UN BRUIT CELLULAIRE. Les diaclases de la roche insoluble
 * emploient un Worley, dont les cellules donnent un nid d'abeille de parois :
 * tres bien pour un chaos de blocs, inutile ici. Des lames demandent des
 * fentes PARALLELES, donc une fonction periodique le long d'un seul axe, dont
 * la direction tourne lentement a l'echelle du kilometre.
 */
struct WORLDSEED_API FWorldseedFinRules
{
	/**
	 * Distance entre deux fentes, en metres.
	 *
	 * ELLE FIXE L'EPAISSEUR DE LA LAME, qui vaut espacement moins largeur de
	 * fente. C'est donc elle, et elle seule, qui decide si l'on percera une
	 * arche ou un tunnel.
	 */
	float SpacingM = 70.0f;

	/** Largeur de la fente en surface, en metres. */
	float SlotM = 16.0f;

	/**
	 * Profondeur de la fente sous la surface, en metres.
	 *
	 * L'ouverture se referme lineairement avec la profondeur, ce qui donne des
	 * parois tres redressees -- cinquante metres de chute pour huit de
	 * demi-largeur -- et non un V evase.
	 */
	float DepthM = 50.0f;

	/** Ondulation laterale des fentes, en metres. A zero, elles sont tirees au cordeau. */
	float WanderM = 12.0f;

	/**
	 * Cote d'une case d'orientation, en metres.
	 *
	 * LA DIRECTION DOIT ETRE CONSTANTE PAR MORCEAUX, et ce n'est pas un choix
	 * de forme mais une necessite. Une direction qui varie continument dans
	 * U = X.cos(theta(X,Y)) + Y.sin(theta(X,Y)) multiplie la coordonnee
	 * ABSOLUE du monde : le gradient de U vaut alors 1 + X.d(theta)/ds, soit
	 * environ CINQ a trente kilometres de l'origine, et l'espacement reel des
	 * fentes tombe a quatorze metres pour soixante-dix demandes -- plus aucune
	 * lame. Constant par morceaux, |grad U| vaut exactement 1.
	 *
	 * La case doit etre nettement plus large qu'une tache de lames, pour que la
	 * discontinuite tombe la ou la zone est deja nulle, donc invisible.
	 */
	float TurnCellM = 16000.0f;

	/** Vitesse de rotation de la direction des lames, en cycles par metre. */
	float TurnFrequency = 0.00011f;

	/** Frequence du masque de zone, en cycles par metre. */
	float ZoneFrequency = 0.00025f;

	/**
	 * Seuil du masque de zone.
	 *
	 * UN SEUIL N'EST PAS UNE PART, et le projet a deja paye cette confusion sur
	 * les diaclases : le bruit de Perlin n'est pas uniforme, il se masse autour
	 * de zero. La part reellement retenue se MESURE, elle ne se deduit pas.
	 */
	float ZoneThreshold = 0.45f;

	/**
	 * Fenetre de durete de la roche, dans [0..1].
	 *
	 * LE GRES ET LUI SEUL. Le calcaire a deja son karst, le granite et le
	 * basalte ont leurs diaclases, et le schiste ne fait pas de lames. La
	 * fenetre par defaut -- 0,50 a 0,70 -- ne retient que le gres du catalogue
	 * (0,55), en laissant la place a un catalogue different.
	 */
	float HardnessMin = 0.50f;
	float HardnessMax = 0.70f;

	/** Marge au-dessus du niveau de la mer sous laquelle on ne creuse pas. */
	float SeaMarginM = 5.0f;

	bool IsActive() const { return SlotM > 0.0f && SpacingM > SlotM && DepthM > 0.0f; }

	static FWorldseedFinRules FromRules(const UWorldseedRules& Rules);
};

/**
 * Le champ de lames, en fonctions PURES.
 *
 * Il est lu par DEUX consommateurs qui ne se connaissent pas : le champ de
 * densite, qui CREUSE les fentes, et la passe des cavites, qui POSE une arche
 * au milieu d'une lame. Les faire diverger donnerait des arches a cote des
 * lames -- le genre de defaut qu'aucune mesure agregee ne voit.
 */
namespace WorldseedFins
{
	/** Direction locale des lames, en radians. Elle tourne a l'echelle du km. */
	WORLDSEED_API double Direction(double X, double Y,
		const FWorldseedFinRules& Rules, int32 Seed);

	/** Coordonnee EN TRAVERS des lames, ondulation comprise. */
	WORLDSEED_API double Across(double X, double Y,
		const FWorldseedFinRules& Rules, int32 Seed);

	/** Force de la zone dans [0..1]. Zero : pas de lames ici. */
	WORLDSEED_API float Zone(double X, double Y,
		const FWorldseedFinRules& Rules, int32 Seed);

	/**
	 * Demi-ouverture de la fente moins la distance a son axe.
	 *
	 * Positif dans la fente, donc dans l'air -- meme convention que les autres
	 * termes soustractifs du champ.
	 */
	WORLDSEED_API double SlotAt(double X, double Y, double DepthM,
		const FWorldseedFinRules& Rules, int32 Seed);

	/**
	 * Centre de la lame la plus proche, le long de l'axe EN TRAVERS.
	 *
	 * C'est la que l'arche se pose : au milieu du mur, jamais dans la fente.
	 * Rend le decalage a appliquer a (X, Y) pour y tomber.
	 */
	WORLDSEED_API double DecalageVersCentre(double X, double Y,
		const FWorldseedFinRules& Rules, int32 Seed);
}
