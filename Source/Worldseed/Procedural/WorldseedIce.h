// Worldseed - la calotte glaciaire a une EPAISSEUR.

#pragma once

#include "CoreMinimal.h"

struct FWorldseedGeometry;
class UWorldseedRules;

/**
 * LA GLACE S'ACCUMULE, ET C'EST CE QUI FAIT UNE CALOTTE.
 *
 * POURQUOI CETTE PASSE EXISTE. Le classificateur attribuait deja la calotte au
 * bon endroit -- 9,2 % des terres, pour 10 sur Terre, et 100 % de couverture
 * au-dela de 80 degres sud -- mais elle restait PLATE : altitude moyenne 28 a
 * 38 m. Or l'Antarctique culmine a 2300 m de moyenne, et pas parce que la
 * roche y est haute : parce que la neige qui tombe ne fond jamais et
 * s'entasse sur deux kilometres. Un plateau a trente metres n'est pas une
 * calotte, c'est une plaine cotiere gelee. Le proprietaire l'a vu au globe
 * avant toute mesure -- il cherchait une vraie calotte et voyait du vert.
 *
 * LE PROFIL N'EST PAS ARBITRAIRE. Une calotte en equilibre prend la forme d'un
 * dome parabolique : l'epaisseur croit comme la RACINE de la distance au bord.
 * C'est la solution de Vialov, et elle vient de ce que la glace flue d'autant
 * plus vite qu'elle est epaisse -- le dome s'etale jusqu'a ce que son poids
 * equilibre sa viscosite. Une rampe lineaire donnerait un cone, une constante
 * une falaise de glace sur tout le pourtour.
 *
 * ELLE PASSE APRES LA CLASSIFICATION, ET C'EST L'ORDRE JUSTE. La glace est une
 * CONSEQUENCE du climat, pas une cause : c'est parce que le mois le plus chaud
 * reste sous zero qu'elle s'accumule. La calculer avant ferait remonter le
 * terrain, donc baisser la temperature, donc etendre la calotte -- une boucle
 * que rien ne fermerait. Le depot a deja pose cette regle sur la prime
 * d'aridite : on choisit l'ordre d'apres la causalite reelle.
 *
 * ELLE NE CHANGE PAS LA PART EMERGEE. Elle n'ajoute de la hauteur que la ou il
 * y a DEJA de la terre classee en calotte ; aucune cellule ne passe de mer a
 * terre, donc le calage a 29,2 % tient sans recalculer le niveau de la mer.
 */
namespace WorldseedIce
{
	struct FRules
	{
		/**
		 * Epaisseur au centre du dome, AVANT mise a l'echelle verticale.
		 *
		 * Elle se lit comme les autres valeurs metriques du fichier : celle
		 * d'un monde de REFERENCE, que VerticalScale porte a la taille de la
		 * carte. Le depot a paye deux fois l'oubli de ce facteur -- la prime
		 * du pole continental, puis le seuil alpin.
		 */
		float MaxThicknessM = 0.0f;

		/**
		 * Distance au bord, en kilometres, a laquelle le dome atteint son
		 * epaisseur maximale. Au-dela il est plat, comme un vrai plateau
		 * glaciaire : sans ce plafond, une grande calotte gonflerait
		 * indefiniment en son centre.
		 */
		float DomeRangeKm = 1.0f;

		static FRules FromRules(const UWorldseedRules& Rules, const FWorldseedGeometry& Geo);
	};

	/**
	 * Epaissit le relief sous la calotte. BiomeIndex est la carte classee ;
	 * ElevationM est modifie en place.
	 *
	 * Rend l'epaisseur maximale reellement posee, pour le journal -- une passe
	 * qui ne dit pas ce qu'elle a fait ne se diagnostique pas.
	 */
	WORLDSEED_API float Apply(TArray<float>& ElevationM,
		const TArray<uint8>& BiomeIndex, const FWorldseedGeometry& Geo,
		const FRules& Rules);
}
