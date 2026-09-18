// Worldseed - outils de polyligne : longueur, simplification, contour.

#pragma once

#include "CoreMinimal.h"

/**
 * Geometrie de polylignes, sans aucune notion d'hydrologie.
 *
 * CONVENTION DE COORDONNEES : X est la colonne (i), Y est la ligne (j), en
 * cellules de grille. Le portage Python raisonne en (j, i) ; on inverse ici une
 * fois pour toutes afin que X reste l'axe horizontal, comme partout ailleurs
 * dans le moteur.
 */

/** Ce qu'il faut savoir d'un contour ferme pour juger s'il tient debout. */
struct WORLDSEED_API FWorldseedRing
{
	int32 Points = 0;

	/** Perimetre, dans l'unite des points. La boucle est FERMEE. */
	float Perimeter = 0.0f;

	/** Aire signee : positive dans le sens direct, negative dans l'autre. */
	float SignedArea = 0.0f;

	/**
	 * Plus petite distance entre deux sommets NON VOISINS.
	 *
	 * C'est elle qui dit si le contour se frole quelque part — une chose que
	 * ni le perimetre ni l'aire ne trahissent, et que l'offset qui dilate ne
	 * pardonne pas.
	 */
	float MinVertexGap = 0.0f;

	/** Sommets dont les deux aretes sont exactement alignees. */
	int32 Collinear = 0;
};
namespace WorldseedPolyline
{
	/** Longueur cumulee, dans l'unite des points. */
	WORLDSEED_API float Length(const TArray<FVector2D>& Points);

	/**
	 * Mesure un contour ferme : points, perimetre, aire signee.
	 *
	 * UNE SEULE FONCTION POUR TOUTES LES ETAPES. Comparer une reduction a un
	 * rentrage n'a de sens que si les deux sont mesures pareil — perimetre
	 * boucle comprise, aire par la meme formule. Deux mesures ecrites deux fois
	 * se contredisent tot ou tard, et l'on passe alors son temps a chercher un
	 * defaut dans la geometrie qui est en fait dans l'instrument.
	 */
	WORLDSEED_API FWorldseedRing Measure(const TArray<FVector2D>& Ring);

	/**
	 * Douglas-Peucker, avec densite minimale garantie.
	 *
	 * DOUGLAS-PEUCKER SEUL SUPPRIME TOUS LES POINTS D'UN TRONCON RECTILIGNE :
	 * une riviere de 5 km peut se reduire a quatre sommets, et la courbe devient
	 * un polygone. MaxSpacing reimpose un point tous les N le long du trace
	 * d'origine, ce qui garde le cours colle au fond de vallee.
	 *
	 * MaxSpacing a zero desactive ce garde-fou.
	 */
	WORLDSEED_API TArray<FVector2D> Simplify(const TArray<FVector2D>& Points,
		float Tolerance, float MaxSpacing);

	/**
	 * Douglas-Peucker a tolerance CROISSANTE jusqu'a tenir dans le quota.
	 *
	 * NE JAMAIS DECIMER UNE POLYLIGNE A PAS FIXE : cela jette des sommets au
	 * hasard et peut couper une baie en deux. Douglas-Peucker garde les points
	 * qui portent la forme et supprime ceux qui ne disent rien.
	 */
	WORLDSEED_API TArray<FVector2D> Reduce(const TArray<FVector2D>& Points,
		int32 MaxPoints, float StartTolerance = 0.5f);

	/**
	 * Decimation a pas fixe, en gardant toujours le dernier point.
	 *
	 * A RESERVER AUX TRACES DEJA SIMPLIFIES dont on veut preserver la DENSITE.
	 * Reduce, qui elargit la tolerance de Douglas-Peucker, supprimerait en
	 * priorite les points des troncons droits — c'est-a-dire exactement ceux que
	 * MaxSpacing venait de reimposer pour garder le cours colle au fond de
	 * vallee. Sur une riviere, le pas fixe conserve la forme ; sur un contour de
	 * lac, ou la forme importe plus que la densite, c'est Reduce qu'il faut.
	 */
	WORLDSEED_API TArray<FVector2D> Decimate(const TArray<FVector2D>& Points,
		int32 MaxPoints);

	/**
	 * Contour ferme et ORDONNE d'un masque binaire.
	 *
	 * POURQUOI PAS UN TRI ANGULAIRE DES CELLULES DE BORD. Cela ne marche que
	 * pour une forme en etoile : des qu'un lac a un bras ou une baie, un meme
	 * rayon coupe le bord plusieurs fois, le tri entremele les points proches et
	 * lointains, et le polygone zigzague a travers le lac — ce qui se voit en
	 * jeu comme un mur d'eau vertical au milieu de la nappe.
	 *
	 * On suit donc les ARETES entre cellules interieures et exterieures, en
	 * gardant l'interieur a gauche. Le resultat est une boucle fermee, ordonnee
	 * et sans croisement, quelle que soit la forme. En cas de plusieurs
	 * composantes, la plus longue est rendue.
	 *
	 * Mask est indexe Row * Width + Col.
	 */

	/**
	 * Comble les pincements diagonaux d'un masque.
	 *
	 * C'EST LA PRECONDITION DE TraceOutline, et elle n'etait pas tenue.
	 *
	 * Le traceur enregistre UN successeur par coin de grille. La ou deux
	 * cellules ne se touchent que par un coin, ce coin porte DEUX aretes de
	 * bord sortantes, et la seconde ecrase la premiere : une branche entiere du
	 * contour disparait. Le parcours s'arrete alors n'importe ou, et le contour
	 * rendu n'est pas ferme — mais tout l'aval le referme quand meme, par une
	 * corde qui peut traverser la nappe de part en part.
	 *
	 * Mesure sur la graine 20260909 : une nappe de 35 ha rendue avec une corde
	 * de fermeture de 7,2 km, et une autre de 24 ha dont le contour n'enfermait
	 * que 13 ha.
	 *
	 * Ajouter une cellule a chaque pincement rend la region connexe par les
	 * ARETES, ce que le traceur suppose. Le cout est d'une cellule par
	 * pincement — sans commune mesure avec les hectares qu'on recupere.
	 *
	 * Renvoie le nombre de cellules ajoutees.
	 */
	WORLDSEED_API int32 FillDiagonalPinches(TArray<bool>& Mask, int32 Width, int32 Height);

	/**
	 * Enveloppe convexe d'un nuage de points, en sens direct.
	 *
	 * POURQUOI UNE NAPPE N'A PAS BESOIN DE MIEUX.
	 *
	 * Un ocean n'a pas de forme : c'est un plan a une altitude, et le relief
	 * decoupe son trait de cote. Un lac est le meme phenomene a une autre
	 * altitude ; son polygone ne sert donc pas a DESSINER son rivage, mais
	 * seulement a BORNER la zone ou son niveau s'applique.
	 *
	 * Tout ce qu'on demande a cette borne est d'etre simple, bien orientee, et
	 * un peu plus large que la cuvette. Une enveloppe convexe l'est par
	 * construction — pas de pincement, pas d'auto-intersection, pas de lamelle,
	 * et quelques dizaines de points au lieu de plusieurs centaines. Toute la
	 * machinerie de tracage fin qu'on a corrigee ne dessinait rien de visible.
	 *
	 * Le prix a payer est nomme : une depression voisine, plus basse que la
	 * nappe et prise dans l'enveloppe, se remplirait a tort.
	 */
	WORLDSEED_API TArray<FVector2D> ConvexHull(const TArray<FVector2D>& Points);
 
	WORLDSEED_API TArray<FVector2D> TraceOutline(const TArray<bool>& Mask,
		int32 Width, int32 Height);

	/**
	 * Rend un contour ferme SIMPLE : sans doublon ni croisement.
	 *
	 * POURQUOI CELA COMPTE. Le plugin Water triangule la nappe d'un lac avec
	 * une regle de remplissage par enroulement, qui encaisse un contour qui se
	 * croise. Mais il DILATE ce meme contour pour permettre a l'eau d'aller
	 * chercher la berge, et l'offset, lui, ne l'encaisse pas : il echoue, le
	 * maillage dilate reste vide, et l'eau s'arrete net au bord de la nappe.
	 * C'est le petit mur qu'on voit au bord d'un lac.
	 *
	 * Le contour brut sort simple du traceur, mais la reduction le casse : une
	 * corde tendue entre deux points eloignes peut traverser une anse. Ce
	 * nettoyage retire la boucle ainsi formee, en gardant la plus grande part.
	 *
	 * MinSpacing fusionne les sommets trop proches. TouchEpsilon dit a partir
	 * de quelle distance deux aretes NON VOISINES se genent — il doit rester
	 * tres petit : sur une nappe etroite, les deux berges SONT proches, et
	 * c'est sa forme, pas un defaut. Un seuil trop large recoud le lac en
	 * travers et le fait disparaitre.
	 *
	 * Renvoie le nombre de croisements defaits, pour qu'un contour retors se
	 * signale au lieu de se deviner.
	 */
	WORLDSEED_API int32 MakeSimple(TArray<FVector2D>& Points, float MinSpacing,
		float TouchEpsilon);
}
