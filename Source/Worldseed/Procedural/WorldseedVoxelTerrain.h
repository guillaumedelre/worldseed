// Worldseed - terrain voxel : diffusion des chunks autour du joueur.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedCaves.h"
#include "Procedural/WorldseedLithology.h"
#include "Procedural/WorldseedDensity.h"
#include "Procedural/WorldseedPlateau.h"
#include "Procedural/WorldseedStrata.h"
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

	/**
	 * Les primitives de grottes qui touchent CE chunk, extraites une fois.
	 *
	 * Elles voyagent avec le travail plutot que d'etre lues depuis l'acteur :
	 * le fil de maillage ne doit rien tenir qui puisse mourir avant lui.
	 */
	FWorldseedCaveLocal Caves;
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

	/** Pourquoi le dernier maillage n'a rien rendu. Diagnostic. */
	FWorldseedVoxelStats::ECause Cause = FWorldseedVoxelStats::ECause::Maille;
	int32 Seeds = 0;
	int32 Tris = 0;
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

	/**
	 * Marge sous la bande avant de declarer le joueur hors du monde, en metres.
	 *
	 * La bande fait deja cent metres ; on n'y ajoute qu'une marge de securite
	 * pour ne pas rattraper un joueur qui explore legitimement une cavite
	 * profonde. Sous bande + marge, il n'existe aucune geometrie : ce n'est
	 * plus une chute, c'est une sortie du monde.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "0.0"))
	float PlayerRescueMarginM = 20.0f;

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

	// IL Y AVAIT ICI UN RAYON DE COLLISION, plus court que le rayon de
	// chargement, au motif que cuire une collision coute plus cher que mailler.
	// Il est RETIRE : aucune API ne permet de donner la collision a une section
	// deja creee, donc la decision prise au televersement etait definitive, et
	// le joueur qui marchait au-dela passait AU TRAVERS DU SOL. Mesure du
	// defaut : sol present de 0 a 110 m, plus rien de 120 a 250 -- la frontiere
	// tombait exactement sur l'ancien rayon. Ne pas le remettre : si la cuisson
	// coute trop cher, la reponse est de la faire de facon asynchrone.

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
	float FallbackHeightMeters = 32000.0f;

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
	/**
	 * Exageration verticale du relief, reprise de l'acteur qui pose celui-ci.
	 *
	 * ELLE DOIT ETRE LA MEME DES DEUX COTES : le sol de fond et l'ocean sont
	 * batis avec celle du terrain, et un champ de densite qui l'ignorerait
	 * decrirait un relief a une autre echelle verticale -- la jonction entre
	 * l'horizon et le sol proche se verrait comme une marche.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde",
		meta = (ClampMin = "0.01"))
	float HeightExaggeration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde")
	bool bHoldPlayer = true;

	// ------------------------------------------------------------ releves

	/** Etat de la diffusion, pour la sonde et le journal. */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Voxel")
	FString ReportState() const;

private:
	bool LoadWorld();

public:
	/**
	 * Reprend TEL QUEL le monde de l'acteur qui pose celui-ci.
	 *
	 * SANS CELA, LES DEUX EN GENERENT DEUX DIFFERENTS, en silence. Quand aucun
	 * monde n'attend dans l'instance de jeu -- c'est le cas d'un PIE lance
	 * depuis l'editeur, sans passer par le menu -- chaque acteur tombe sur sa
	 * generation de secours, et leurs resolutions ne sont pas les memes : 512 x
	 * 256 pour le terrain, 2048 x 1024 ici. Meme graine, relief different. Le
	 * sol de fond decrivait donc un autre monde que celui qu'on a sous les
	 * pieds, sans le moindre avertissement.
	 *
	 * A appeler AVANT FinishSpawning : BeginPlay charge le monde, et il est
	 * trop tard apres.
	 */
	void AdoptWorld(int32 InSeed, const FWorldseedGeometry& InGeometry,
		const TArray<float>& InHeightsM, const FWorldseedBiomeMap& InBiomes,
		float InHeightExaggeration, const FWorldseedCaveNetwork& InCaves,
		const FWorldseedLithology& InLithology);

private:

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

public:
	/**
	 * Etat de la colonne de chunks qui contient ce point. Diagnostic.
	 *
	 * IL N'EXISTE AUCUN AUTRE MOYEN DE SAVOIR POURQUOI UN TROU EST LA. De
	 * l'exterieur, un chunk jamais considere, un chunk declare vide et un chunk
	 * maille a zero triangle se ressemblent tous les trois : on sonde, on ne
	 * touche rien, et on ne peut pas distinguer les trois causes -- qui
	 * appellent pourtant trois corrections differentes.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Voxel")
	FString DiagnostiquerColonne(FVector MondeCm) const;

	/**
	 * Pose le joueur a un endroit choisi, en metres dans le repere du monde.
	 *
	 * IL NE SUFFIT PAS DE DEPLACER LE PION, ET LE PROJET A DEJA PAYE CE PIEGE.
	 * Les chunks se batissent AUTOUR de lui : a l'arrivee il n'y a rien sous
	 * ses pieds, il tombe, et comme c'est lui qui donne l'origine de la
	 * diffusion il emmene la fenetre de chunks dans sa chute -- mesure, -4745 m.
	 * On reutilise donc le filet qui existe deja : le pion est TENU EN VOL
	 * jusqu'a ce que le chunk qui le porte ait une collision cuite, puis rendu
	 * a la gravite. C'est exactement ce que fait la mise en place initiale, et
	 * la refaire ici ferait diverger les deux moities.
	 *
	 * SEULE DIFFERENCE AVEC CETTE MISE EN PLACE : on ne cherche PAS de sol plat
	 * alentour. L'endroit a ete demande, on y va.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Voxel")
	void TeleporterJoueur(double XMetres, double YMetres);

	/** Ou est le joueur, et sur quoi. Pour juger une capture sans deviner. */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Voxel")
	FString OuSuisJe() const;

	/**
	 * Le monde charge, en LECTURE SEULE, pour qui doit l'interroger.
	 *
	 * L'acteur charge et possede ce monde : y donner acces en lecture est
	 * legitime et evite de le recopier. Ce qui ne le serait pas, c'est qu'il
	 * se charge lui-meme des travaux qui s'en servent -- la tournee photo a
	 * vecu ici par commodite avant d'aller dans son propre sous-systeme.
	 */
	const FWorldseedGeometry& MondeGeometrie() const { return Geometry; }
	const TArray<float>& MondeAltitudes() const { return HeightsM; }
	const FWorldseedCaveNetwork& MondeGrottes() const { return CaveNetwork; }

	/** Les sites de tables, rebatis au chargement et jamais serialises. */
	const TArray<FWorldseedPlateauSite>& MondeTables() const { return Tables; }

	/** Les fonds de canyon, rebatis au chargement comme les tables. */
	const TArray<FWorldseedPlateauSite>& MondeCanyons() const { return Canyons; }
	const FWorldseedDensity& MondeChamp() const { return Density; }

	/** Vrai quand le pion a ete rendu a la gravite sur un sol solide. */
	bool JoueurPose() const { return bPlayerReleased; }

	/**
	 * Les endroits du monde CHARGE qui meritent d'etre vus.
	 *
	 * Calcules a la demande depuis le relief et la roche, jamais ecrits en dur :
	 * une liste de coordonnees serait juste pour une graine et fausse pour
	 * toutes les autres.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Voxel")
	FString LieuxRemarquables() const;

private:

	void ReleaseChunk(const FIntVector& Key);

	/** Tient le joueur en l'air, puis le rend a la gravite quand le sol existe. */
	void HoldOrReleasePlayer();

	/** Clef du chunk qui porte un point donne, en metres repere acteur. */
	FIntVector KeyForPoint(double X, double Y, double Z) const;

	/**
	 * Cherche un endroit ou POSER le joueur : plat, emerge, et plein dessous.
	 *
	 * Les trois criteres comptent. Emerge, sinon on nait dans la mer. Plat,
	 * sinon on glisse. Et surtout PLEIN DESSOUS : depuis que les galeries
	 * existent, une colonne sur huit porte un vide, et le pion tombe dedans --
	 * mesure, il s'est retrouve a 47 m sous terre au premier essai.
	 *
	 * Faux si rien ne convient dans le rayon fouille.
	 */
	bool FindFlatGround(const FVector2D& AroundM, double& OutX, double& OutY,
		float& OutSurfaceM, float& OutSlopeDeg) const;

	UPROPERTY()
	TObjectPtr<USceneComponent> RootScene;

	FWorldseedGeometry Geometry;
	TArray<float> HeightsM;
	FWorldseedBiomeMap Biomes;
	int32 WorldSeed = 0;

	/** Le reseau de grottes du monde charge. */
	FWorldseedCaveNetwork CaveNetwork;

	/** Sites de tables, fonction pure du relief et de la graine. */
	TArray<FWorldseedPlateauSite> Tables;

	/** Fonds de canyon : l autre face du meme objet. */
	TArray<FWorldseedPlateauSite> Canyons;

	/** La serie stratigraphique, lue une fois au chargement. */
	FWorldseedStratRules StratRules;

	/** Durete par identifiant, pour la garde du socle sedimentaire. */
	TArray<float> DureteParId;
	FWorldseedLithology Lithology;

	/**
	 * Couleur de chaque roche, indexee par identifiant.
	 *
	 * COPIEE, PAS REFERENCEE : cinq couleurs tiennent dans une poignee d'octets,
	 * et cela evite de dependre de la duree de vie des regles au moment ou l'on
	 * peint un chunk.
	 */
	TArray<FLinearColor> CouleurParRoche;

	/** Libelle de chaque roche, meme indexation. Pour le releve, pas le rendu. */
	TArray<FString> NomParRoche;

	/** Destination demandee, tant que le pion n'y est pas pose. */
	bool bTeleportPose = false;
	FVector2D TeleportXYM = FVector2D::ZeroVector;

	FWorldseedDensityRules DensityRules;
	FWorldseedDensity Density;

	TMap<FIntVector, FWorldseedVoxelChunkState> Chunks;

	FTimerHandle UpdateTimer;

	bool bWorldAdopted = false;
	bool bWorldReady = false;
	bool bPlayerHeld = false;
	bool bPlayerReleased = false;

	/** Cumuls pour le releve. */
	int32 BuiltChunks = 0;
	int32 EmptyChunks = 0;

	/** Travaux annules puis repris. Un compteur qui monte sans fin est un signe. */
	int32 AbandonedChunks = 0;

	/** Chunks ou le mailleur n'a rien rendu malgre une traversee. */
	int32 DegenerateChunks = 0;
	int32 TotalTriangles = 0;
	double TotalMeshMs = 0.0;
	double WorstMeshMs = 0.0;
	double FirstFillSeconds = 0.0;
	double StartSeconds = 0.0;
};
