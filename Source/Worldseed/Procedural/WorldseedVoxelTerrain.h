// Worldseed - terrain voxel : diffusion des chunks autour du joueur.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedDensity.h"
#include "Procedural/WorldseedVoxelChunk.h"

#include <atomic>

#include "WorldseedVoxelTerrain.generated.h"

class UMaterialInterface;
class UProceduralMeshComponent;

/**
 * Un maillage de chunk en cours de fabrication sur un fil de travail.
 *
 * MEME DISCIPLINE QUE LA GENERATION DU MONDE DEPUIS LE MENU : le travailleur ne
 * detient AUCUN UObject, il remplit une structure partagee puis pose son drapeau
 * en ecriture liberee ; le fil de jeu lit le drapeau en acquisition avant de
 * toucher au resultat. Pas de verrou, et l'acteur peut mourir en vol -- le
 * travail se termine dans le vide et le pointeur partage se libere tout seul.
 */
struct FWorldseedVoxelJob
{
	FIntVector Key = FIntVector::ZeroValue;
	FBox BoundsM = FBox(ForceInit);

	FWorldseedVoxelMesh Mesh;
	FWorldseedVoxelStats Stats;
	bool bHasSurface = false;

	std::atomic<bool> bDone{ false };
	std::atomic<bool> bCancel{ false };
};

using FWorldseedVoxelJobPtr = TSharedPtr<FWorldseedVoxelJob, ESPMode::ThreadSafe>;

/** Ce qu'on garde d'un chunk pose. */
struct FWorldseedVoxelChunkState
{
	TObjectPtr<UProceduralMeshComponent> Mesh;

	/** Travail en cours, s'il y en a un. */
	FWorldseedVoxelJobPtr Job;

	/** Vrai si le chunk ne contient aucune surface : rien a afficher, jamais. */
	bool bEmpty = false;

	bool bHasCollision = false;
};

/**
 * Terrain voxel diffuse autour du joueur.
 *
 * POURQUOI UN ACTEUR SEPARE DE AWorldseedTerrain. Le terrain en carte
 * d'altitude marche ; tant que le voxel n'a pas fait ses preuves, le monde doit
 * rester jouable. Les deux ne seront fondus qu'a la derniere etape, quand le
 * voxel tiendra la comparaison. Poser un acteur a cote coute une classe de plus
 * et ne casse rien.
 *
 * LA GRILLE 2D DECIDE DE LA VERTICALE, et c'est ce qui rend le streaming 3D
 * abordable : elle donne les bornes d'altitude de la surface sur l'empreinte
 * d'une colonne de chunks, donc on sait sans evaluer le champ quels etages
 * peuvent contenir quelque chose. Les etages du socle ne sont meme pas
 * consideres.
 */
UCLASS()
class WORLDSEED_API AWorldseedVoxelTerrain : public AActor
{
	GENERATED_BODY()

public:
	AWorldseedVoxelTerrain();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ------------------------------------------------------------ diffusion

	/** Cote d'un chunk, en metres. 32 m pour des voxels d'un metre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "8.0"))
	float ChunkSideM = 32.0f;

	/** Rayon de construction autour du joueur, en metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "32.0"))
	float LoadRadiusM = 250.0f;

	/**
	 * Rayon de destruction, volontairement plus grand que celui de construction.
	 * Sans cette hysteresis, un pas en avant et un pas en arriere sur la
	 * frontiere feraient construire et detruire le meme chunk en boucle.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "32.0"))
	float UnloadRadiusM = 350.0f;

	/**
	 * Rayon au-dela duquel un chunk n'a pas de collision, en metres.
	 *
	 * CUIRE UNE COLLISION COUTE PLUS CHER QUE MAILLER. Le joueur ne peut
	 * toucher que ce qui est pres de lui ; au-dela, le maillage se regarde mais
	 * ne se heurte pas.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "0.0"))
	float CollisionRadiusM = 120.0f;

	/** Travaux simultanes sur le pool de fils. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxJobsInFlight = 12;

	/**
	 * Chunks televerses par passe.
	 *
	 * LE MAILLAGE EST HORS DU FIL DE JEU, PAS LE TELEVERSEMENT : creer une
	 * section de ProceduralMesh touche au moteur de rendu et doit donc rester
	 * sur le fil de jeu. C'est la seule part du cout qui se paie en images.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "1", ClampMax = "64"))
	int32 UploadsPerPass = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "0.05"))
	float UpdatePeriod = 0.2f;

	/** Materiau des chunks. Il lit la couleur de sommet telle quelle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel")
	TObjectPtr<UMaterialInterface> TerrainMaterial;

	// ------------------------------------------------------------ monde

	/**
	 * Monde de secours, quand rien n'attend dans la GameInstance.
	 *
	 * Sert au banc : on peut poser cet acteur dans une carte vide et obtenir un
	 * monde sans passer par le menu.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde")
	int32 FallbackSeed = 20260909;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde")
	float FallbackHeightMeters = 8000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde")
	int32 FallbackResolutionY = 1024;

	/**
	 * Tient le joueur au-dessus du relief tant que son sol n'existe pas.
	 *
	 * SANS CELA IL TOMBE, ET IL EMPORTE TOUT AVEC LUI. Mesure du premier essai :
	 * pion lache a 400 m au-dessus du sol, arrive a -9 857 m -- et comme c'est
	 * LUI qui sert d'origine a la diffusion, les 233 chunks deja mailles sont
	 * sortis du rayon et ont ete detruits derriere sa chute. Attendre qu'un
	 * chunk porte une collision pour le poser etait circulaire : il etait deja
	 * trop loin pour qu'on en construise un.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde")
	bool bHoldPlayer = true;

	// ------------------------------------------------------------ releves

	/** Etat de la diffusion, pour la sonde et le journal. */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Voxel")
	FString ReportState() const;

private:
	bool LoadWorld();

	void UpdateChunks();

	/** Origine de la diffusion : le pion s'il existe, sinon l'acteur. */
	FVector StreamingOriginCm() const;

	/** Centre d'un chunk, en centimetres monde. */
	FVector ChunkCentreCm(const FIntVector& Key) const;

	/** Boite d'un chunk, en metres dans le repere de l'acteur. */
	FBox ChunkBoundsM(const FIntVector& Key) const;

	/** Lance le maillage d'un chunk sur le pool de fils. */
	void LaunchJob(const FIntVector& Key);

	/** Televerse un maillage termine dans son composant. */
	void UploadChunk(const FIntVector& Key, FWorldseedVoxelChunkState& State);

	/** Couleur et teinte d'un sommet, depuis la carte des biomes. */
	void PaintVertices(FWorldseedVoxelMesh& Mesh) const;

	void ReleaseChunk(const FIntVector& Key);

	/** Tient le joueur en l'air, puis le rend a la gravite quand le sol existe. */
	void HoldOrReleasePlayer();

	/** Clef du chunk qui porte un point donne, en metres repere acteur. */
	FIntVector KeyForPoint(double X, double Y, double Z) const;

	UPROPERTY()
	TObjectPtr<USceneComponent> RootScene;

	FWorldseedGeometry Geometry;
	TArray<float> HeightsM;
	FWorldseedBiomeMap Biomes;
	int32 WorldSeed = 0;

	FWorldseedDensityRules DensityRules;
	FWorldseedDensity Density;

	TMap<FIntVector, FWorldseedVoxelChunkState> Chunks;

	FTimerHandle UpdateTimer;

	bool bWorldReady = false;
	bool bPlayerHeld = false;
	bool bPlayerReleased = false;

	/** Cumuls pour le releve. */
	int32 BuiltChunks = 0;
	int32 EmptyChunks = 0;
	int32 TotalTriangles = 0;
	double TotalMeshMs = 0.0;
	double WorstMeshMs = 0.0;
	double FirstFillSeconds = 0.0;
	double StartSeconds = 0.0;
};
