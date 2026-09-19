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
	 * Les six faces d'un chunk, en bits, pour dire lesquelles reclament une
	 * cellule de transition.
	 *
	 * CE SONT LES FACES QUI BORDENT UN VOISIN PLUS FIN, ET NON L'INVERSE.
	 * J'avais ecrit le contraire, et c'est faux : Lengyel place les cellules de
	 * transition DANS LE BLOC GROSSIER, le long de sa frontiere avec le bloc
	 * pleine resolution (section 4.3). C'est le bloc grossier qui doit ceder de
	 * la place et raccorder, parce que c'est lui qui a trop peu d'echantillons
	 * -- neuf valeurs fines arrivent sur une face qui n'en porte que quatre.
	 *
	 * Se tromper de sens ici aurait produit un mailleur cherchant a raccorder du
	 * cote ou il n'y a rien a raccorder, et la fissure serait restee entiere.
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
	 * MasqueTransition dit quelles faces bordent un voisin deux fois plus FIN.
	 * A zero, le maillage est purement regulier.
	 *
	 * LargeurTransition est l'epaisseur de la dalle de transition, en FRACTION
	 * d'une cellule de ce chunk -- jamais en metres, sans quoi elle cesserait de
	 * suivre le niveau de detail. Les cellules regulieres de bord se retractent
	 * d'autant pour lui ceder la place. Une epaisseur nulle raccorde
	 * geometriquement sans fissure mais « leads to severe shading problems »
	 * (Lengyel, section 4.3) : les triangles lateraux degenerent et leurs
	 * normales n'ont plus de sens.
	 *
	 * Rend faux si la boite ne porte aucune surface, ou si ShouldStop a demande
	 * l'arret. Caves peut etre nul.
	 */
	WORLDSEED_API bool Mailler(const FWorldseedDensity& Density,
		const FWorldseedCaveLocal* Caves, const FBox& BoundsM,
		float VoxelSizeM, uint8 MasqueTransition, float LargeurTransition,
		FWorldseedVoxelMesh& Out, FWorldseedVoxelStats& OutStats,
		TFunction<bool()> ShouldStop = nullptr);
}
