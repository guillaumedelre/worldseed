// Worldseed - extraction du reseau hydrographique en polylignes.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedHydrologyRules.h"
#include "Procedural/WorldseedRules.h"

struct FWorldseedFlow;

/** Ou finit un cours d'eau. */
enum class EWorldseedMouth : uint8
{
	Ocean,
	Lake,

	/** Le cours quitte le domaine : anomalie, a signaler. */
	Border,

	/**
	 * Cuvette fermee interieure : le cours meurt sur place.
	 *
	 * Ce n'est PAS une anomalie — c'est un bassin endoreique, exactement comme
	 * la Caspienne ou le lac Tchad.
	 */
	Endorheic
};

/** Un cours d'eau, de sa source a son exutoire. */
struct WORLDSEED_API FWorldseedRiver
{
	/** Trace en cellules (X colonne, Y ligne), source vers embouchure. */
	TArray<FVector2D> PointsPx;

	/** Debit VRAI en m3/s, par point. L'exageration ne s'applique pas ici. */
	TArray<float> DischargeM3s;

	/** Largeur et profondeur en metres, par point. */
	TArray<float> WidthM;
	TArray<float> DepthM;

	/**
	 * Altitude de la SURFACE D'EAU, en metres, par point.
	 *
	 * PRISE SUR LE RELIEF COMBLE, JAMAIS SUR LE RELIEF BRUT. Le routage
	 * descend sur le relief comble : c'est lui, et lui seul, qui garantit une
	 * pente monotone de la source a l'embouchure. Poser le lit sur le relief
	 * brut ferait plonger le cours dans chaque cuvette puis remonter de l'autre
	 * cote — de l'eau qui coule vers le haut, ce qui se voit immediatement.
	 *
	 * La difference entre les deux EST le lac : la ou elle est grande, la
	 * riviere traverse une nappe, et sa surface est celle de la nappe.
	 */
	TArray<float> SurfaceM;

	/**
	 * Largeur de la CUVETTE a hauteur de surface, en metres, par point.
	 *
	 * A ne pas confondre avec WidthM, qui est la largeur hydraulique du lit.
	 * La ou le routage a comble une depression, l'eau libre s'etend jusqu'a ce
	 * que le relief remonte a son niveau — bien au-dela du lit. Mesure sur la
	 * graine 20260909 : 68 m en moyenne, 407 m au plus, pour un lit de 7,5 m.
	 *
	 * C'EST ELLE QUE LE CORPS D'EAU DOIT PORTER. La dilatation du plugin ne
	 * peut pas s'en charger : son shader enfonce explicitement sous le sol
	 * toute eau dilatee qui passerait au-dessus du terrain. Seule l'EMPRISE du
	 * corps decide ou il y a de l'eau.
	 */
	TArray<float> BasinWidthM;

	int32 StrahlerOrder = 1;
	float LengthM = 0.0f;
	EWorldseedMouth Mouth = EWorldseedMouth::Ocean;
};

namespace WorldseedRivers
{

	/**
	 * Mesure, pour chaque point, jusqu'ou la cuvette s'etend a hauteur d'eau.
	 *
	 * Sonde perpendiculairement au cours jusqu'a ce que le relief remonte au
	 * niveau de la surface, dans les deux sens. Le relief passe ici est celui
	 * qu'UNREAL AFFICHE : c'est contre lui que l'eau se decoupera.
	 */
	WORLDSEED_API void MeasureBasins(const TArray<float>& ElevationM,
		const FWorldseedGeometry& Geometry, float MaxWidthM,
		TArray<FWorldseedRiver>& Rivers);
	/**
	 * Apport de chaque cellule, en m3/s.
	 *
	 * L'accumulation devient ainsi un VRAI debit, et la geometrie hydraulique
	 * donne des largeurs en metres comparables a des cours d'eau reels :
	 * 100 m3/s tient dans 18 m, 10 000 m3/s dans 180 m.
	 */
	WORLDSEED_API void DischargeWeights(const TArray<float>& PrecipMm,
		const FWorldseedGeometry& Geometry, float RunoffCoefficient,
		TArray<float>& OutWeights);

	/**
	 * Remonte les chenaux en polylignes.
	 *
	 * ON TRACE DEPUIS L'EMBOUCHURE, en remontant toujours le plus gros affluent.
	 * Partir des sources donnerait des troncons haches a chaque confluence ;
	 * partir de l'embouchure donne le cours principal entier, de la source a la
	 * mer — exactement comme on nomme un fleuve.
	 */
	WORLDSEED_API void Extract(const FWorldseedFlow& Flow,
		const TArray<float>& ElevationM, const TArray<bool>& LakeMask,
		const FWorldseedGeometry& Geometry, const FWorldseedHydrologyRules& Rules,
		TArray<FWorldseedRiver>& OutRivers);

	WORLDSEED_API const TCHAR* MouthName(EWorldseedMouth Mouth);
}
