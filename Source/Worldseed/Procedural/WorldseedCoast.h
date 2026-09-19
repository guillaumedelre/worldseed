// Worldseed - le recul de falaise, qui donne son relief a la roche tendre.

#pragma once

#include "CoreMinimal.h"

#include "Procedural/WorldseedLithology.h"

class UWorldseedRules;
struct FWorldseedGeometry;

/**
 * Section "littoral" de world_rules.json.
 *
 * POURQUOI CETTE PASSE EXISTE, ET CE QU'ELLE CORRIGE. Le modele d'erosion en
 * place est FLUVIAL : l'incision par puissance de courant donne a chaque roche
 * la pente qu'elle peut tenir, et la roche tendre en tient une TRES faible.
 * Mesure du denivele local sur 63 m, monde de 64 x 32 km : basalte 51,8 m,
 * granite 33,4 -- mais gres 12,8, calcaire 12,8, schiste 13,1, soit onze
 * degres. Autrement dit, dans ce monde, la roche tendre n'a aucune falaise.
 *
 * C'EST FAUX POUR UN LITTORAL, ET C'EST MEME L'INVERSE. Les falaises les plus
 * spectaculaires de la Terre sont dans la roche TENDRE : Etretat et Douvres
 * sont de la craie. La raison est qu'une falaise marine ne nait pas d'un
 * equilibre de pente mais d'un SAPEMENT : la houle creuse une encoche au pied,
 * la masse sus-jacente s'effondre, et le front recule en restant vertical. Plus
 * la roche est tendre, plus il recule vite -- donc plus la falaise est haute,
 * puisqu'elle mord loin dans les terres.
 *
 * Ce mecanisme manquait entierement. Sans lui, aucune arche marine n'est
 * possible : une arche se lit parce qu'on voit le ciel au travers, ce qui
 * demande que le sol TOMBE autour d'elle.
 */
struct WORLDSEED_API FWorldseedCoastRules
{
	/**
	 * Portee du remodelage depuis le trait de cote, en metres.
	 *
	 * C'est la distance dont le front a recule. Elle decide de la HAUTEUR de la
	 * falaise autant que de sa position : en mordant plus loin dans les terres,
	 * le front rencontre un relief plus haut.
	 */
	float ReachM = 260.0f;

	/** Part de la portee occupee par la plateforme d'abrasion, dans [0..1]. */
	float PlatformFraction = 0.35f;

	/**
	 * Largeur de la FACE, en part de la portee.
	 *
	 * ELLE DOIT TENIR DANS UNE MAILLE DE SIMULATION, sinon la falaise n'est
	 * qu'une rampe. A 64 km la maille fait 31 m ; 0,15 de portee en fait 39.
	 * Le voxel, qui travaille au metre, rend alors une paroi franche.
	 */
	float FaceFraction = 0.15f;

	/** Altitude de la plateforme, en metres. Au-dessus de zero : elle reste terre. */
	float PlatformM = 3.0f;

	/** Force du remodelage, dans [0..1]. A zero, le monde retrouve son etat d'avant. */
	float Strength = 0.9f;

	/**
	 * Amplitude de la modulation par la roche.
	 *
	 * LA ROCHE TENDRE RECULE PLUS, et c'est tout l'objet de la passe. A zero,
	 * toutes les cotes reculent pareil et l'on perd la raison d'etre du terme.
	 */
	float RockContrast = 0.8f;

	/** Frequence du bruit qui fait varier le recul le long de la cote. */
	float NoiseFrequency = 0.0012f;

	/** Amplitude de ce bruit, en part de la portee. */
	float NoiseAmount = 0.45f;

	bool IsActive() const { return ReachM > 0.0f && Strength > 0.0f; }

	static FWorldseedCoastRules FromRules(const UWorldseedRules& Rules);
};

namespace WorldseedCoast
{
	/**
	 * Fait reculer le front de falaise, en place.
	 *
	 * ELLE NE FAIT QUE BAISSER LE TERRAIN, JAMAIS LE MONTER, et ce n'est pas une
	 * precaution mais la forme meme du mecanisme : la mer ENLEVE de la matiere.
	 * C'est aussi ce qui rend la passe sure -- pas de bosse au raccord, pas de
	 * couture a la limite de portee, et la part emergee ne bouge pas puisque la
	 * plateforme reste au-dessus de zero.
	 */
	WORLDSEED_API void Build(const FWorldseedGeometry& Geometry,
		const FWorldseedLithology& Lithology,
		const FWorldseedLithologyRules& LithoRules,
		const FWorldseedCoastRules& Rules, int32 Seed,
		TArray<float>& ElevationM);
}
