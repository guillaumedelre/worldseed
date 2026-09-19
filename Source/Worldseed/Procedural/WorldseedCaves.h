// Worldseed - le RESEAU de grottes : chambres, liaisons, et connexite garantie.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedRules.h"

struct FWorldseedLithology;
struct FWorldseedLithologyRules;

/** Une salle. */
struct WORLDSEED_API FWorldseedCaveChamber
{
	/** Centre, en METRES dans le repere du terrain. */
	FVector CentreM = FVector::ZeroVector;
	float RadiusM = 8.0f;
};

/**
 * Une galerie, sous forme de capsule a rayon variable.
 *
 * Pourquoi une capsule et pas un tube maille : c'est une primitive de distance
 * signee, donc elle se combine par une union lisse et se derive proprement. Un
 * tube maille demanderait de le coudre au terrain, ce qui est exactement le
 * travail que le marching cubes fait deja pour nous.
 */
struct WORLDSEED_API FWorldseedCaveSegment
{
	FVector AM = FVector::ZeroVector;
	FVector BM = FVector::ZeroVector;
	float RadiusAM = 2.0f;
	float RadiusBM = 2.0f;
};

/**
 * Les primitives qui touchent un chunk donne.
 *
 * TESTER TOUTES LES PRIMITIVES A CHAQUE VOXEL EST REDHIBITOIRE : c'est du
 * O(voxels x primitives), et un monde en porte des milliers. On extrait donc
 * UNE FOIS par chunk la liste de celles dont l'emprise le touche, et le champ
 * n'evalue que celles-la. On retombe a quelques dizaines.
 */
struct WORLDSEED_API FWorldseedCaveLocal
{
	const struct FWorldseedCaveNetwork* Network = nullptr;
	TArray<int32> Chambers;
	TArray<int32> Segments;

	bool IsEmpty() const { return Chambers.Num() == 0 && Segments.Num() == 0; }
};

/**
 * Le reseau, et son index spatial.
 *
 * LA CONNEXITE EST ACQUISE PAR CONSTRUCTION, PAS VERIFIEE APRES COUP. C'est
 * tout l'objet de cette passe : du bruit 3D produit des cavernes credibles,
 * mais rien n'assure qu'elles communiquent, ni qu'une seule debouche a l'air
 * libre. Les chambres sont semees, puis reliees par un ARBRE COUVRANT MINIMAL
 * -- qui touche tous les sommets par definition -- et on ajoute ensuite
 * quelques aretes courtes pour faire des boucles, sans quoi le joueur revient
 * toujours sur ses pas.
 */
struct WORLDSEED_API FWorldseedCaveNetwork
{
	TArray<FWorldseedCaveChamber> Chambers;
	TArray<FWorldseedCaveSegment> Segments;

	/** Index spatial : une grille reguliere en XY, la bande etant mince. */
	float CellM = 64.0f;
	FIntPoint Min = FIntPoint::ZeroValue;
	FIntPoint Size = FIntPoint::ZeroValue;
	TArray<TArray<int32>> ChamberBuckets;
	TArray<TArray<int32>> SegmentBuckets;

	bool IsValid() const { return Chambers.Num() > 0; }

	/** Les primitives dont l'emprise touche cette boite. */
	void Query(const FBox& BoxM, FWorldseedCaveLocal& Out) const;

	void Reset();
};

/** Section "cavites" de world_rules.json. */
struct WORLDSEED_API FWorldseedCaveRules
{
	/** A zero, aucun reseau n'est bati et le monde retrouve son etat d'avant. */
	float ChamberSpacingM = 180.0f;

	float DepthMinM = 20.0f;
	float DepthMaxM = 90.0f;
	float ChamberRadiusMinM = 6.0f;
	float ChamberRadiusMaxM = 22.0f;

	/** Cumul annuel minimal : un karst se creuse par DISSOLUTION, il faut de l'eau. */
	float MinPrecipMm = 600.0f;

	/** Aptitude a la dissolution minimale de la roche, dans [0..1]. */
	float MinKarstifiable = 0.5f;

	float TunnelRadiusMinM = 1.5f;
	float TunnelRadiusMaxM = 4.0f;

	/** Part d'aretes courtes ajoutees a l'arbre, pour faire des boucles. */
	float LoopPct = 15.0f;

	/** Voisins candidats par chambre, pour batir le graphe avant l'arbre. */
	int32 Neighbours = 8;

	/**
	 * Rayon de raccordement de l'union lisse, en metres.
	 *
	 * A ZERO, L'UNION REDEVIENT UN MAXIMUM DUR, et les jonctions entre galerie
	 * et chambre prennent des aretes vives -- l'effet de tuyaux colles. Au-dela,
	 * le raccord s'arrondit. Le proprietaire a demande "un compromis entre
	 * maximum dur et lisse", ce qui est exactement une valeur basse de ce
	 * reglage.
	 */
	float BlendM = 2.5f;

	static FWorldseedCaveRules FromRules(const UWorldseedRules& Rules);
};

namespace WorldseedCaves
{
	/**
	 * Bâtit le reseau. Une passe MACRO, a basse resolution, une fois par monde.
	 *
	 * ELLE NE PEUT PAS SE CALCULER PAR CHUNK : une galerie traverse les
	 * frontieres, et deux chunks voisins qui en decideraient chacun de leur cote
	 * ne tomberaient pas d'accord. Le calcul non local se paie donc UNE FOIS, a
	 * basse resolution, hors du chemin critique -- et son resultat devient un
	 * parametre de la fonction de densite, qui reste sans etat.
	 */
	WORLDSEED_API void Build(const FWorldseedGeometry& Geometry,
		const TArray<float>& ElevationM, const TArray<float>& PrecipMm,
		const FWorldseedLithology& Lithology, const FWorldseedLithologyRules& LithoRules,
		const FWorldseedCaveRules& Rules, float HeightExaggeration, int32 Seed,
		FWorldseedCaveNetwork& Out);

	/**
	 * Part d'AIR due au reseau en un point, en metres. Negatif ou nul hors des
	 * cavites, positif dedans.
	 */
	WORLDSEED_API double AirAt(const FWorldseedCaveLocal& Local, const FVector& PosM,
		float BlendM);
}
