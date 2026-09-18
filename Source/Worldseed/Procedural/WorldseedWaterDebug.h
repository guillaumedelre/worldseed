// Worldseed - visualisation du reseau hydrographique.

#pragma once

#include "CoreMinimal.h"

struct FWorldseedGeometry;
struct FWorldseedHydrology;

/**
 * Montre A PARTIR DE QUOI les cours d'eau sont traces.
 *
 * Tout ce qui precede se juge sur des captures d'ecran, et une capture ne dit
 * pas si l'eau est trop haute, trop etroite, ou au bon endroit mais mal
 * dessinee. Ce trace-ci repond a ces trois questions separement :
 *
 *   - BLEU   un segment confie au plugin, donc porte par une spline.
 *   - ORANGE un segment laisse au maillage procedural.
 *   - BLANC  la largeur du lit a chaque point, en travers du cours.
 *   - ROUGE  la surface d'eau est AU-DESSUS du terrain : hauteur du cordon.
 *   - VERT   la surface d'eau est sous le terrain : le lit est creuse.
 *
 * Un cours entierement rouge sur toute sa longueur n'est pas une riviere mal
 * dessinee : c'est une riviere posee SUR le relief au lieu d'y etre logee, et
 * aucun reglage de rendu n'y changera rien.
 */
namespace WorldseedWaterDebug
{
	/**
	 * Trace le reseau, et releve de combien l'eau depasse le terrain.
	 *
	 * Taken peut etre vide : tous les segments sont alors dits procéduraux.
	 */
	WORLDSEED_API void DrawRivers(UWorld* World, const FWorldseedGeometry& Geometry,
		const FWorldseedHydrology& Hydrology, const TArray<float>& ElevationM,
		float HeightExaggeration, const TArray<TArray<bool>>& Taken);

	/**
	 * Trace le rivage de chaque nappe, la ou elle DEVRAIT etre.
	 *
	 * SANS REFERENCE, UNE CAPTURE NE SE LIT PAS. Une etendue bleue au pied du
	 * joueur peut aussi bien etre un lac correctement dessine qu'un lac qui
	 * deborde de trois cents metres — rien dans l'image ne les distingue. Ce
	 * trace pose la limite attendue par-dessus le rendu : si l'eau s'arrete au
	 * magenta, elle est juste ; si elle le depasse, elle deborde, et de combien
	 * se lit a l'oeil.
	 *
	 *   - MAGENTA le rivage tel que l'hydrologie l'a extrait.
	 *   - JAUNE   la surface libre de la nappe, en travers du rivage.
	 *   - ROUGE   le terrain sous le rivage est PLUS BAS que la surface.
	 *   - VERT    il est plus haut : la nappe s'arrete la, comme prevu.
	 */
	WORLDSEED_API void DrawLakes(UWorld* World, const FWorldseedGeometry& Geometry,
		const FWorldseedHydrology& Hydrology, const TArray<float>& ElevationM,
		float HeightExaggeration);
}
