// Worldseed - maillage d'un chunk de voxels par surface implicite.

#pragma once

#include "CoreMinimal.h"

class FWorldseedDensity;
struct FWorldseedCaveLocal;

/**
 * Un maillage de chunk, dans la forme attendue par ProceduralMeshComponent.
 *
 * Les positions sont en CENTIMETRES dans le repere de l'acteur terrain : le
 * champ de densite raisonne en metres, la conversion se fait ici, une fois,
 * sur les sommets. Voir le commentaire de FWorldseedDensity::At.
 */
struct WORLDSEED_API FWorldseedVoxelMesh
{
	TArray<FVector> Positions;
	TArray<int32> Triangles;
	TArray<FVector> Normals;

	/** Remplies par l'appelant, depuis ComputeVertexAppearance. */
	TArray<FLinearColor> Colours;
	TArray<FVector2D> TintRG;
	TArray<FVector2D> TintB;

	bool IsEmpty() const { return Triangles.Num() == 0; }
	int32 TriangleCount() const { return Triangles.Num() / 3; }

	void Reset()
	{
		Positions.Reset();
		Triangles.Reset();
		Normals.Reset();
		Colours.Reset();
		TintRG.Reset();
		TintB.Reset();
	}

	/** Octets reellement occupes, pour les releves. */
	int32 BytesUsed() const;
};

/** Ce qu'un maillage a coute, pour pouvoir le mesurer sans le deviner. */
struct WORLDSEED_API FWorldseedVoxelStats
{
	double FieldMs = 0.0;
	double MeshMs = 0.0;
	double NormalMs = 0.0;
	int32 FieldSamples = 0;
	int32 Vertices = 0;
	int32 Triangles = 0;

	/**
	 * Pourquoi le maillage n'a rien rendu.
	 *
	 * TROIS CAUSES SE CACHAIENT DERRIERE UN SEUL "false", ET ELLES N'APPELLENT
	 * PAS LA MEME REPONSE. Un chunk sans traversee est VIDE et doit etre
	 * retenu comme tel ; un chunk dont le travail a ete ANNULE doit etre
	 * repris ; un chunk que le mailleur n'a pas su remplir est un defaut qu'il
	 * faut au moins pouvoir compter. L'appelant marquait les trois "vide pour
	 * toujours" et ne les reproposait jamais -- d'ou des trous permanents dans
	 * le sol, mesures a 1,5 % des colonnes chargees.
	 */
	/** Germes semes par le balayage grossier. Zero explique tout ; beaucoup, rien. */
	int32 Seeds = 0;

	enum class ECause : uint8 { Maille, SansTraversee, Annule, MaillageVide };
	ECause Cause = ECause::Maille;
};

namespace WorldseedVoxelChunk
{
	/**
	 * Maille l'isovaleur zero du champ dans une boite, en metres.
	 *
	 * POURQUOI LE MAILLEUR DU MOTEUR ET PAS LE NOTRE. GeometryCore livre
	 * FMarchingCubes, qui prend une fonction implicite, tourne depuis n'importe
	 * quel fil et sait s'annuler en cours de route. C'est exactement le contrat
	 * dont un chunk a besoin, et il est deja eprouve. Le mailleur maison
	 * n'arrivera qu'avec le Transvoxel, pour les raccords entre resolutions --
	 * et il ne remplacera que CET appel.
	 *
	 * bParallelCompute est laisse a FAUX a dessein : la parallelisation se fait
	 * par CHUNK, un travail par fil. Deux niveaux de parallelisme imbriques se
	 * disputent le meme pool et rendent les mesures illisibles.
	 *
	 * Rend faux si la boite ne contient aucune surface, ou si ShouldStop a
	 * demande l'arret.
	 */
	/**
	 * Caves peut etre nul : le chunk est alors maille sans reseau de grottes.
	 * La liste est EXTRAITE UNE FOIS par chunk et capturee dans la lambda du
	 * mailleur -- tester toutes les primitives a chaque voxel serait du
	 * O(voxels x primitives).
	 */
	WORLDSEED_API bool Build(const FWorldseedDensity& Density,
		const FWorldseedCaveLocal* Caves, const FBox& BoundsM,
		float VoxelSizeM, FWorldseedVoxelMesh& Out, FWorldseedVoxelStats& OutStats,
		TFunction<bool()> ShouldStop = nullptr);
}
