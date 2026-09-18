// Worldseed - creusement du lit des cours d'eau dans le relief.

#pragma once

#include "CoreMinimal.h"

struct FWorldseedGeometry;
struct FWorldseedRiver;

/** Ce qui regle le creusement. */
struct WORLDSEED_API FWorldseedCarveRules
{
	/**
	 * Hauteur de berge au-dessus de la surface d'eau, en metres.
	 *
	 * LE CHENAL SE DIMENSIONNE PAR LE COURS, PAS PAR UNE CONSTANTE.
	 *
	 * Creuser huit metres partout donnait une lame de 1,1 m au fond d'une
	 * tranchee : les rivieres paraissaient a sec, et le moindre relief reste
	 * dans le lit emergeait de l'eau en coupant le cours. Un ruisseau creuse un
	 * petit lit, un fleuve un grand.
	 *
	 * Le fond se pose donc a la profondeur d'eau PLUS cette marge, et l'eau
	 * remplit exactement ce qu'on a creuse pour elle. La marge est ce qui
	 * depasse : elle donne la berge qui confine le cours.
	 */
	float FreeboardM = 1.5f;


	/**
	 * Bornes de la lame d'eau dans le chenal, en metres.
	 *
	 * LA HAUTEUR D'EAU N'EST PAS UNE CONSTANTE. L'hydrologie calcule deja une
	 * profondeur par point a partir du debit : un ruisseau et un fleuve ne
	 * portent pas la meme lame, et poser partout la meme donnait des rivieres
	 * dont la profondeur ne correspondait a rien.
	 *
	 * Le plancher garantit que l'eau se voie meme sur un filet — mesure sur
	 * la graine 20260909, quarante-deux coupes : les profondeurs hydrauliques
	 * vont de 0,6 a 1,4 m, et une lame d'un metre se lit comme une flaque et
	 * non comme une riviere. Ce plancher est un choix de RENDU, pas une
	 * correction : l'hydrologie a raison, c'est l'oeil qui demande plus ; le plafond
	 * evite qu'un fleuve ne remplisse le chenal au-dela de ses berges.
	 */
	float MinWaterDepthM = 2.0f;
	float MaxWaterDepthM = 6.0f;


	/**
	 * Largeur de berge du chenal, de chaque cote du lit, en metres.
	 *
	 * ELLE RESTE PETITE, DELIBEREMENT. Ce n'est pas elle qui decide ou l'eau
	 * s'arrete — c'est la descente de la surface. Hors du chenal le terrain
	 * reste intact, donc au-dessus de l'eau descendue, et c'est la que la
	 * sonde bute.
	 *
	 * Deux cellules au moins : en dessous, la paroi devient une marche d'une
	 * cellule, donc une tranchee et non un U.
	 */
	float MinBankM = 16.0f;

	/**
	 * Nombre de passes creusement / re-mesure.
	 *
	 * C'EST UNE CONVERGENCE, PAS UNE REPETITION. La premiere passe mesure la
	 * cuvette sur le relief naturel et y creuse un chenal. Ce chenal cree des
	 * BERGES : a la passe suivante, la sonde les rencontre bien plus pres, et
	 * le creusement se resserre d'autant. De proche en proche, une vallee large
	 * de cent metres devient un lit net.
	 *
	 * Une seule passe ne peut pas y arriver : elle ne connait que la cuvette
	 * naturelle, qui est precisement celle qu'on veut remplacer.
	 */
	int32 Passes = 3;

	/** Largeur de cuvette au plus, en metres, pour le sondage. */
	float MaxBasinWidthM = 600.0f;

	/**
	 * Pente au-dela de laquelle on ne creuse plus, en degres.
	 *
	 * Creuser un ressaut de soixante-seize metres y taillerait un canyon
	 * vertical. Ce sont aussi les segments que la spline ne prend pas.
	 */
	float MaxSlopeDeg = 30.0f;
};

/**
 * Creuse le lit des cours d'eau, pour que l'eau y tienne.
 *
 * POURQUOI C'EST NECESSAIRE, ET CE QUE CELA NE REGLE PAS.
 *
 * Le plugin Water ne dessine de l'eau que la ou sa surface DEPASSE le sol.
 * Or le lit vient du relief comble, et le relief affiche porte un detail que
 * l'hydrologie n'a pas vu : mesure sur la graine 20260909, sur 1470 noeuds, le
 * terrain passe au-dessus de la surface d'eau sur 58 % du reseau. Sur toute
 * cette part, la riviere existe, son corps d'eau est sain, et rien ne se voit.
 *
 * Creuser un chenal sous la surface rend ces troncons. Cela ne rend PAS les
 * 42 % restants, ou le terrain est deja sous la surface : ce sont de vraies
 * cuvettes noyees, des mares, et une nappe s'y impose plutot qu'un ruban.
 *
 * ON NE FAIT QUE DESCENDRE LE RELIEF. Jamais le remonter : un minimum ne peut
 * pas casser la decroissance vers l'aval, et l'operation reste idempotente —
 * la rejouer sur un relief deja creuse ne change rien.
 */
namespace WorldseedRiverCarve
{
	/**
	 * Applique le creusement a un relief, en place.
	 *
	 * LakeMask peut etre vide. S'il ne l'est pas, les cellules de nappe sont
	 * epargnees : y creuser un chenal viderait le lac.
	 */
	WORLDSEED_API void Apply(TArray<FWorldseedRiver>& Rivers,
		const TArray<bool>& LakeMask, const FWorldseedGeometry& Geometry,
		const FWorldseedCarveRules& Rules, TArray<float>& ElevationM);
}
