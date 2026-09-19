// Worldseed - mailleur Transvoxel : cellules regulieres et cellules de transition.

#pragma once

#include "CoreMinimal.h"

class FWorldseedDensity;
struct FWorldseedCaveLocal;
struct FWorldseedVoxelMesh;
struct FWorldseedVoxelStats;

/**
 * Le mailleur maison, celui que le plan du chantier annoncait pour le Transvoxel.
 *
 * POURQUOI ECRIRE UN MAILLEUR ALORS QUE LE MOTEUR EN A UN.
 * FMarchingCubes (GeometryCore) fait du marching cubes STANDARD, et il le fait
 * bien -- c'est lui qui maille le terrain aujourd'hui. Mais Transvoxel n'est pas
 * un jeu de tables qu'on brancherait dessus : il ajoute une seconde famille de
 * cellules, les cellules de TRANSITION, qui raccordent une face fine a un voisin
 * deux fois plus grossier. FMarchingCubes ne sait pas les produire, et ne peut
 * pas apprendre a le faire depuis l'exterieur.
 *
 * ET LES DEUX FAMILLES NE SE SEPARENT PAS. Une cellule de transition attend que
 * les cellules regulieres voisines lui aient laisse la place -- leurs sommets de
 * bord sont RETRACTES d'un demi-voxel vers l'interieur. Mailler les regulieres
 * avec le moteur et les transitions a la main donnerait donc deux maillages qui
 * ne se rejoignent pas : exactement la fissure qu'on cherche a supprimer.
 *
 * CE FICHIER EST DONC LE MAILLEUR COMPLET, et il est verifie CONTRE le moteur :
 * a resolution uniforme, sans aucune transition, il doit rendre la meme surface
 * que FMarchingCubes. C'est le seul controle qui vaille avant d'aller plus loin
 * -- un mailleur maison qui deplace la geometrie n'est pas un mailleur, c'est un
 * defaut.
 */
namespace WorldseedTransvoxel
{
	/**
	 * Les six faces d'un chunk, en bits, pour dire lesquelles regardent un
	 * voisin PLUS GROSSIER et reclament donc une cellule de transition.
	 *
	 * Zero = resolution uniforme, aucune transition : c'est le cas de tout le
	 * terrain tant que les anneaux de resolution ne sont pas poses, et c'est
	 * dans cet etat que le mailleur se compare au moteur.
	 */
	enum EFace : uint8
	{
		AucuneFace = 0,
		MoinsX = 1 << 0,
		PlusX  = 1 << 1,
		MoinsY = 1 << 2,
		PlusY  = 1 << 3,
		MoinsZ = 1 << 4,
		PlusZ  = 1 << 5,
	};

	/**
	 * Maille l'isovaleur zero du champ dans une boite, en metres.
	 *
	 * MasqueTransition dit quelles faces bordent un voisin deux fois plus
	 * grossier. A zero, le maillage est purement regulier.
	 *
	 * Rend faux si la boite ne porte aucune surface, ou si ShouldStop a demande
	 * l'arret. Caves peut etre nul.
	 */
	WORLDSEED_API bool Mailler(const FWorldseedDensity& Density,
		const FWorldseedCaveLocal* Caves, const FBox& BoundsM,
		float VoxelSizeM, uint8 MasqueTransition,
		FWorldseedVoxelMesh& Out, FWorldseedVoxelStats& OutStats,
		TFunction<bool()> ShouldStop = nullptr);
}
