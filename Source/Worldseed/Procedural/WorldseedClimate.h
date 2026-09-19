// Worldseed - le climat.
//
// L'ORIGINAL PYTHON A ETE SUPPRIME : ce fichier est la SEULE
// implementation. Il ne faut plus chercher de reference ailleurs, ni supposer
// qu'un autre fichier dit la meme chose autrement.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedJob.h"
#include "Procedural/WorldseedRules.h"

/** Sortie des etapes 2 et 3 : temperature, circulation, precipitations. */
struct WORLDSEED_API FWorldseedClimateResult
{
	TArray<float> TempMeanC;        // moyenne annuelle au sol
	TArray<float> TempMinC;         // moyenne du mois le plus froid
	TArray<float> TempMaxC;         // moyenne du mois le plus chaud
	TArray<float> SeasonalAmpC;     // amplitude saisonniere
	TArray<float> PrecipMm;         // cumul annuel
	TArray<float> WindU;            // composante est
	TArray<float> WindV;            // composante nord
	TArray<float> Continentality;   // 0 au bord de mer -> 1 loin des cotes
	TArray<float> OrographicUplift; // soulevement force par le relief
	TArray<float> Convergence;      // convergence du vent (ZCIT)

	/** Moyenne des precipitations sur les terres emergees, en mm/an. */
	float MeanLandPrecipMm = 0.0f;
};

/**
 * Etapes 2 et 3 - temperature, circulation atmospherique, precipitations.
 *
 * Rien n'est peint. Le desert apparait vers 30 degres parce que l'air y arrive
 * deja essore, l'ombre pluviometrique parce qu'une chaine coupe la route de
 * l'ocean, la foret equatoriale parce que la circulation y converge.
 *
 * Trois cellules de circulation par hemisphere, comme sur Terre — alizes,
 * westerlies, est polaires — puis advection semi-lagrangienne de l'humidite.
 */
namespace WorldseedClimate
{

	/**
	 * Part de la pluie annuelle qui tombe pendant le SEMESTRE CHAUD, dans [0..1].
	 *
	 * POURQUOI ELLE EXISTE. Le climat ne portait qu'un CUMUL ANNUEL, et tout un
	 * pan des climats terrestres se definit par la SAISON de cette pluie et non
	 * par sa quantite : le climat mediterraneen, c'est un ete sec sous une annee
	 * qui ne l'est pas. Sans cette grandeur, ces terres se lisaient comme une
	 * foret temperee ou une prairie, et le bulletin terrestre le disait -- deux
	 * des trois releves Mediterranean tombaient dans la mauvaise case.
	 *
	 * ELLE NE DEMANDE AUCUNE PHYSIQUE NOUVELLE, et c'est ce qui la rend sure :
	 * la circulation est deja une fonction de la latitude,
	 * omega(phi) = cos(cellules * pi * |phi| / demi-portee), qui place la ZCIT a
	 * l'equateur, la subsidence vers 30 degres et le front polaire vers 60. Les
	 * ceintures MIGRENT avec le soleil ; il suffit donc d'evaluer la meme
	 * fonction a deux latitudes decalees de precipitation.beltShiftDeg, une fois
	 * vers le pole (ete) et une fois vers l'equateur (hiver), et de comparer les
	 * deux taux de pluie. A 38 degres, la subsidence passe SUR le point en ete et
	 * s'en ecarte en hiver : l'ete sec sort tout seul.
	 *
	 * Elle ne depend que de la latitude et des regles, donc elle se recalcule a
	 * la demande et ne pese pas sur le cache du monde.
	 */
	WORLDSEED_API float SummerRainFraction(const UWorldseedRules& Rules,
		const FWorldseedGeometry& Geo, float LatitudeDeg);

	/**
	 * Amplitude saisonniere. Exposee a dessein : l'export des prereglages
	 * Ultra Dynamic Sky a besoin exactement de cette valeur, et la formule ne
	 * doit exister qu'a UN endroit — la dupliquer fabrique des divergences
	 * invisibles.
	 */
	WORLDSEED_API float SeasonalAmplitude(const UWorldseedRules& Rules,
		float LatitudeNormalized, float Continentality);

	/** Rend faux si le calcul a ete interrompu. */
	WORLDSEED_API bool Generate(const UWorldseedRules& Rules,
		const FWorldseedGeometry& Geometry, int32 Seed,
		const TArray<float>& ElevationM, FWorldseedClimateResult& Out,
		const FWorldseedProgressScope& Progress = FWorldseedProgressScope());
}
