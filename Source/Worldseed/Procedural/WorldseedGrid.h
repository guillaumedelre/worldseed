// Worldseed - operations de grille equivalentes a celles de scipy.ndimage.

#pragma once

#include "CoreMinimal.h"

/**
 * Portage des quelques primitives scipy dont depend le generateur Python.
 * Elles sont reimplementees plutot qu'approximees : un flou different ou un
 * quantile approche decalerait le trait de cote et donc la part de terres
 * emergees, qui est une valeur calibree sur la Terre.
 *
 * Toutes travaillent sur une grille NX x NY indexee J * NX + I, NX etant l'axe
 * des longitudes et NY celui des latitudes.
 */
namespace WorldseedGrid
{
	/**
	 * Flou gaussien separable, equivalent a scipy.ndimage.gaussian_filter avec
	 * truncate=4.0 et mode="nearest".
	 */
	WORLDSEED_API void GaussianFilter(TArray<float>& InOut, int32 NX, int32 NY,
		float SigmaPixels);

	/**
	 * Flou gaussien sur UN seul axe, equivalent a gaussian_filter1d avec
	 * mode="nearest". bAlongRows filtre le long de J, l'axe de la latitude.
	 *
	 * mode="nearest" et non un enroulement : un pole n'est pas voisin de
	 * l'autre pole, et enrouler ferait passer l'humidite australe dans
	 * l'Arctique.
	 */
	WORLDSEED_API void GaussianFilter1D(TArray<float>& InOut, int32 NX, int32 NY,
		float SigmaPixels, bool bAlongRows);

	/**
	 * Transformee de distance euclidienne exacte, equivalente a
	 * scipy.ndimage.distance_transform_edt : pour chaque cellule NON NULLE du
	 * masque, distance en pixels vers la cellule nulle la plus proche.
	 *
	 * Algorithme de Felzenszwalb et Huttenlocher : exact et lineaire, la
	 * version naive serait inutilisable a cette taille.
	 */
	WORLDSEED_API void DistanceTransform(const TArray<uint8>& Mask, int32 NX, int32 NY,
		TArray<float>& OutDistancePixels);

	/**
	 * Gradients (d/dY, d/dX) en unites par metre, equivalent a np.gradient :
	 * differences centrees a l'interieur, unilaterales sur les bords.
	 */
	WORLDSEED_API void Gradient(const TArray<float>& Field, int32 NX, int32 NY,
		float SpacingM, TArray<float>& OutDY, TArray<float>& OutDX);

	/**
	 * Reduit une grille par MOYENNE DE BLOCS.
	 *
	 * Pas par selection d'un point sur N : moyenner conserve l'altitude moyenne
	 * de chaque bloc et supprime le crenelage, la ou le point-sampling garde un
	 * pixel au hasard et fait scintiller le relief des qu'on tourne.
	 *
	 * Sans effet si la destination est plus grande ou egale a la source.
	 */
	WORLDSEED_API void Downsample(const TArray<float>& Source, int32 SrcNX, int32 SrcNY,
		int32 DstNX, int32 DstNY, TArray<float>& Out);

	/**
	 * Echantillonnage bilineaire a des coordonnees de grille flottantes, bords
	 * en mode "nearest". Equivalent a map_coordinates d'ordre 1.
	 */
	WORLDSEED_API float SampleBilinearClamped(const TArray<float>& Field,
		int32 NX, int32 NY, float PosJ, float PosI);

	/**
	 * Echantillonnage bilineaire en coordonnees normalisees [0..1].
	 * U suit la longitude et s'ENROULE — la carte fait le tour de la sphere ;
	 * V suit la latitude et se borne, un pole n'ayant pas de voisin au-dela.
	 */
	WORLDSEED_API float SampleUV(const TArray<float>& Field, int32 NX, int32 NY,
		float U, float V);

	/**
	 * Quantile avec interpolation lineaire, la methode par defaut de numpy.
	 * C'est lui qui fixe le niveau de la mer, donc la part de terres emergees.
	 */
	/**
	 * Echantillonnage BICUBIQUE (Catmull-Rom), meme convention que SampleUV.
	 *
	 * UNE BILINEAIRE EST C0 : sa derivee saute au bord de chaque maille, et le
	 * marching cubes rend ces sauts comme des ARETES. A 64 km la maille de
	 * simulation fait 31 m, donc le monde se lit comme un pavage de grands
	 * triangles. Catmull-Rom est C1 et passe par les points de la grille : les
	 * aretes disparaissent sans que le relief macro soit deplace.
	 */
	WORLDSEED_API float SampleUVCubic(const TArray<float>& Field, int32 NX, int32 NY,
		float U, float V);

	WORLDSEED_API float Quantile(const TArray<float>& Values, float Q);
}
