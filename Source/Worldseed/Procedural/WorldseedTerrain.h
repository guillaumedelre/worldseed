// Worldseed - terrain runtime decoupe en chunks, depuis le monde spherique.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedClimatePreset.h"
#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedCaves.h"
#include "Procedural/WorldseedLithology.h"
#include "Procedural/WorldseedVoxelTerrain.h"
#include "Procedural/WorldseedTexturePack.h"
#include "WorldseedTerrain.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

/** Ce que les couleurs de sommet transportent jusqu'au materiau. */
UENUM(BlueprintType)
enum class EWorldseedTerrainColouring : uint8
{
	/**
	 * Poids de quatre couches dans RGBA : roche, vegetation, sable, neige.
	 *
	 * Le materiau melange des textures selon ces poids. Riche a l'oeil, mais
	 * il ne distingue que quatre matieres : une savane et une steppe y sont
	 * identiques.
	 */
	LayerWeights	UMETA(DisplayName = "Poids de couches"),

	/**
	 * Couleur du biome, directement dans RGB.
	 *
	 * Les dix-neuf biomes se lisent alors d'un coup d'oeil. Demande un materiau
	 * qui affiche la couleur de sommet telle quelle — voir BiomeMaterial.
	 */
	BiomeColour		UMETA(DisplayName = "Couleur de biome"),

	/**
	 * Poids de quatre MATIERES dans RGBA, teinte du biome dans UV1/UV2.
	 *
	 * Le materiau melange quatre textures de sol par ces poids, puis les teinte.
	 * C'est ce qui permet a une savane et a une prairie de partager la meme
	 * texture d'herbe sans se ressembler — et donc a un pack de quatre textures
	 * d'habiller dix-neuf biomes.
	 */
	TexturePack		UMETA(DisplayName = "Pack de textures"),
};


/** Ce que le terrain doit ecrire dans chaque sommet, selon le mode courant. */
struct FWorldseedAppearance
{
	bool bTexturePack = false;
	bool bColourByBiome = false;
	bool bHasClimate = false;
	bool bHasCover = false;
};

/** Un morceau de terrain construit, avec le niveau de detail employe. */
USTRUCT()
struct FWorldseedChunk
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Mesh = nullptr;

	/** Pas d'echantillonnage du heightfield. 1 = pleine resolution. */
	int32 Stride = 0;
};

/**
 * Terrain procedural construit au runtime, decoupe en chunks.
 *
 * POURQUOI PAS WORLD PARTITION. WP streame des acteurs qui EXISTENT DEJA sur
 * disque : on les place dans l'editeur, ils sont sauvegardes en external
 * actors, et WP choisit lesquels charger. Ici le monde nait d'une graine au
 * lancement : il n'y a aucun acteur auteure, donc rien a streamer. WP n'aurait
 * pas de travail. Il redeviendrait le bon outil le jour ou les mondes seraient
 * CUITS hors-ligne — ce qui contredirait l'exigence de generation runtime.
 *
 * La carte est la surface deroulee d'une sphere : 360 degres de longitude sur
 * 180 de latitude, donc deux fois plus large que haute.
 */
UCLASS()
class WORLDSEED_API AWorldseedTerrain : public AActor
{
	GENERATED_BODY()

public:
	AWorldseedTerrain();

	// ------------------------------------------------------------- monde

	/** Graine utilisee si aucun monde n'attend dans le GameInstance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde")
	int32 FallbackSeed = 20260909;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde",
		meta = (ClampMin = "100.0"))
	float FallbackHeightMeters = 32000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde",
		meta = (ClampMin = "64", ClampMax = "2048"))
	int32 FallbackResolutionY = 1024;

	/** Exageration verticale. 1 = altitudes reelles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde",
		meta = (ClampMin = "0.1"))
	float HeightExaggeration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde")
	TObjectPtr<UMaterialInterface> TerrainMaterial;

	// ----------------------------------------------------------- couches

	/**
	 * Pente, en degres, a partir de laquelle la roche prend le dessus.
	 * Les deux bornes forment la zone de transition.
	 */
	/** Ce que les couleurs de sommet transportent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches")
	EWorldseedTerrainColouring Colouring = EWorldseedTerrainColouring::BiomeColour;

	/** Materiau employe en mode couleur de biome. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches")
	TObjectPtr<UMaterialInterface> BiomeMaterial;

	/**
	 * Un materiau par pack de textures.
	 *
	 * Ce sont des INSTANCES d'un meme materiau maitre, chacune avec ses quatre
	 * textures. Changer de pack ne change donc ni le maillage ni les couleurs
	 * de sommet : seul le materiau pose sur les chunks change.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches")
	TMap<EWorldseedTexturePack, TObjectPtr<UMaterialInterface>> PackMaterials;

	/** Pack choisi dans le menu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches")
	EWorldseedTexturePack TexturePack = EWorldseedTexturePack::BiomeColour;

	/**
	 * Part d'eau melee a la couleur du biome, de 0 a 1.
	 *
	 * A ZERO, LE BIOME SEUL : on lit la savane jusque sur la plage, ce qui est
	 * l'interet d'avoir separe les deux axes. A UN, la mer recouvre tout et on
	 * retrouve l'ancien comportement. Entre les deux, on voit le rivage ET dans
	 * quel biome il s'inscrit.
	 *
	 * L'axe de couverture ne porte plus que l'ocean : les lacs et les rivieres
	 * ont ete retires du generateur le 18 septembre 2026.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CoverTint = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches",
		meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float RockSlopeStartDeg = 26.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches",
		meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float RockSlopeFullDeg = 42.0f;

	/** Altitude, en metres, jusqu ou la plage remonte depuis le rivage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches")
	float BeachTopM = 12.0f;

	/** Temperature moyenne, en degres, sous laquelle la neige tient. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches")
	float SnowTempC = -2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches")
	float SnowTempFullC = -8.0f;

	/** Precipitations, en mm/an, bornant l aridite et la luxuriance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches")
	float AridMm = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches")
	float LushMm = 900.0f;

	// ------------------------------------------------------------ chunks

	/** Cote d'un chunk, en cellules du heightfield. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Chunks",
		meta = (ClampMin = "16", ClampMax = "512"))
	int32 ChunkCells = 64;

	/** Rayon de construction autour du joueur, en metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Chunks",
		meta = (ClampMin = "100.0"))
	float LoadRadiusM = 2500.0f;

	/**
	 * Rayon de destruction. Volontairement plus grand que le chargement :
	 * sans cette hysteresis, un joueur qui fait un pas en avant et en arriere
	 * sur la frontiere ferait construire et detruire le meme chunk en boucle.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Chunks",
		meta = (ClampMin = "100.0"))
	float UnloadRadiusM = 3500.0f;

	/**
	 * Distances de bascule de niveau de detail, en metres. Le pas double a
	 * chaque palier : 1, 2, 4, 8 cellules.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Chunks")
	TArray<float> LodDistancesM = { 400.0f, 900.0f, 1800.0f };

	/**
	 * Chunks construits par passe. Batir tout d'un coup ferait un gel de
	 * plusieurs secondes a l'entree dans le monde.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Chunks",
		meta = (ClampMin = "1", ClampMax = "64"))
	int32 ChunkBuildBudget = 4;

	/** Periode de reevaluation de la liste des chunks, en secondes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Chunks",
		meta = (ClampMin = "0.05"))
	float UpdatePeriod = 0.25f;

	/**
	 * Profondeur de la jupe de bordure, en metres. Deux chunks voisins de
	 * niveaux differents ne partagent pas leurs sommets de bord : une fissure
	 * apparait. Une jupe verticale la bouche sans avoir a raccorder les
	 * maillages, ce qui couterait bien plus cher.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Chunks",
		meta = (ClampMin = "0.0"))
	float SkirtDepthM = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Chunks")
	bool bCreateCollision = true;

	/** Construit le sol de fond couvrant tout le monde. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Chunks")
	bool bBuildGroundProxy = true;

	/**
	 * Largeur du sol de fond, en sommets.
	 *
	 * Cale sur la texture d'information de l'eau, qui fait 512 pixels de cote
	 * pour toute la zone : au-dela on paierait un detail qu'elle ne sait pas
	 * lire, en deca c'est elle qu'on gaspillerait.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Chunks",
		meta = (ClampMin = "32", ClampMax = "1024", EditCondition = "bBuildGroundProxy"))
	int32 GroundProxyWidth = 512;

	/**
	 * Enfoncement du sol de fond sous le terrain detaille, en metres.
	 *
	 * Les deux maillages decrivent le meme relief : sans ce retrait ils se
	 * disputeraient le meme plan et scintilleraient. Sous les chunks, le fond
	 * ne se voit jamais ; au-dela, il est seul et le retrait ne se remarque pas.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Chunks",
		meta = (ClampMin = "0.0", EditCondition = "bBuildGroundProxy"))
	float GroundProxyDropM = 1.0f;

	/**
	 * Retirer le sol de fond du rendu quand l'oeil est SOUS la surface.
	 *
	 * UN DECOR D'HORIZON N'A RIEN A FAIRE DANS UN VOLUME FERME. La nappe
	 * grossiere traverse les salles et les galeries et s'y dessine par-dessus
	 * la roche : mesure, elle occupait 70,2 % du cadre dans la salle sous le
	 * gouffre. Ce qu'on perd en la coupant est mesure aussi : depuis une bouche
	 * de grotte, regard vers le dehors, 4,1 % du cadre seulement -- parce que
	 * le terrain voxel couvre entierement les 250 m du rayon de chargement, et
	 * que ce qu'on voit par une ouverture est presque toujours du vrai terrain.
	 * Dix-sept fois plus de degats a l'interieur que de perte a la sortie.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Chunks",
		meta = (EditCondition = "bBuildGroundProxy"))
	bool bHideGroundProxyUnderground = true;

	/**
	 * Hauteur sondee au-dessus de l'oeil pour chercher un plafond, en metres.
	 *
	 * Le sondage doit depasser le relief le plus epais qu'on puisse avoir
	 * au-dessus de la tete. Cinq cents metres couvrent largement l'amplitude
	 * du monde ; au-dela on paierait un rayon qui ne rencontre jamais rien.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Chunks",
		meta = (ClampMin = "10.0", EditCondition = "bHideGroundProxyUnderground"))
	float GroundProxyRoofProbeM = 500.0f;

	/** Inverse l'ordre des triangles si le terrain apparait retourne. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Chunks")
	bool bFlipWinding = false;

	// ------------------------------------------------------------ joueur

	/**
	 * Confier le relief proche au mailleur VOXEL plutot qu'a la carte d'altitude.
	 *
	 * POURQUOI LA BASCULE PREND CETTE FORME. Cet acteur ne fait pas que mailler :
	 * il batit le sol de fond — qui remplit l'horizon ET nourrit la texture
	 * d'information du plugin Water —, nourrit le ciel en climat, repond aux
	 * questions de latitude et d'altitude, et pose l'ocean. Le mailleur n'est
	 * qu'une de ses fonctions, et c'est la SEULE que le voxel remplace.
	 *
	 * Deplacer ces services vers l'acteur voxel aurait demande de deplacer sept
	 * cents lignes et tout l'etat qui va avec, d'un coup, sans filet. Les laisser
	 * ici et n'echanger que le mailleur tient en trente lignes, se verifie a
	 * l'image, et se defait en posant ce drapeau a faux.
	 *
	 * Ce qui est ASSUME le temps de la bascule : les deux acteurs lisent le monde
	 * chacun de leur cote, donc la grille d'altitudes existe en double — environ
	 * huit megaoctets a 2048 x 1024. A supprimer quand l'ancien mailleur partira.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel")
	bool bUseVoxelMesher = true;

	/** Classe de l'acteur voxel a poser. Vide : la classe native. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel")
	TSubclassOf<AWorldseedVoxelTerrain> VoxelTerrainClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Joueur")
	bool bPlacePlayerAfterGenerate = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Joueur")
	float PlayerClearanceCm = 150.0f;

	// ------------------------------------------------------------- API

	/** Recharge le monde et repart de zero. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Worldseed")
	void Rebuild();

	UFUNCTION(BlueprintCallable, Category = "Worldseed")
	void PlacePlayerOnTerrain();

	UFUNCTION(BlueprintPure, Category = "Worldseed")
	float GetHeightAtWorldXY(float WorldX, float WorldY) const;

	UFUNCTION(BlueprintPure, Category = "Worldseed")
	void GetLonLatAtWorldXY(float WorldX, float WorldY,
		float& OutLongitudeDeg, float& OutLatitudeDeg) const;

	/**
	 * Climat au point donne. Faux si le monde n'en porte pas.
	 *
	 * Rend un echantillon complet plutot que trois nombres separes : c'est
	 * l'objet que le ciel consomme, et l'assembler ici evite que chaque
	 * appelant le reconstitue a sa facon.
	 */
	bool SampleClimateAtWorldXY(float WorldX, float WorldY,
		FWorldseedClimateSample& OutSample) const;

	/** Nombre de chunks actuellement construits. */
	UFUNCTION(BlueprintPure, Category = "Worldseed")
	int32 GetLoadedChunkCount() const { return Chunks.Num(); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Recupere le monde du GameInstance, ou le genere en secours. */
	bool AcquireWorld();

	/** Reevalue quels chunks doivent exister, et a quel niveau. */
	void UpdateChunks();

	/** Pose l'acteur voxel qui prend le relief proche en charge. */
	void SpawnVoxelTerrain();

	/** Rebatit le reseau de grottes quand le monde vient du menu. */
	void RebuildCaveNetwork();

	/** Echantillonne le climat sous le joueur et le pousse au ciel. */
	void FeedSky(float DeltaSeconds);

	/** Construit ou reconstruit un chunk au niveau demande. */
	void BuildChunk(const FIntPoint& Key, int32 Stride);

	/** Detruit un chunk et libere son composant. */
	void ReleaseChunk(const FIntPoint& Key);

	/** Position de reference pour le chargement : le joueur, sinon l'acteur. */
	FVector GetStreamingOrigin() const;

	/** Pas d'echantillonnage pour une distance donnee. */
	int32 StrideForDistance(float DistanceM) const;

	/** Centre monde d'un chunk, en centimetres. */
	FVector2D ChunkCenterCm(const FIntPoint& Key) const;

	/**
	 * Apparence d'un sommet : poids de matiere ou couleur, plus la teinte.
	 *
	 * Partagee entre les chunks detailles et le sol de fond : deux versions de
	 * cette regle finiraient par diverger, et la difference se verrait
	 * exactement la ou les deux maillages se rencontrent.
	 */
	void ComputeVertexAppearance(int32 Cell, float HeightM, const FVector& Normal,
		const FWorldseedAppearance& Mode, FLinearColor& OutColour,
		FVector2D& OutTintRG, FVector2D& OutTintB) const;

	/**
	 * Construit le SOL DE FOND : tout le monde, en basse resolution.
	 *
	 * DEUX RAISONS, ET LA SECONDE EST LA PLUS IMPORTANTE.
	 *
	 * Les chunks detailles ne couvrent qu'un rayon autour du joueur — quinze
	 * pour cent d'une carte de seize kilometres. Au-dela, le monde s'arretait
	 * net.
	 *
	 * Et surtout : le systeme d'eau lit le SOL pour savoir ou l'eau rencontre la
	 * terre. Il ne voyait donc du sol que sur ces quinze pour cent, et appliquait
	 * partout ailleurs une profondeur forfaitaire — d'ou ces grandes zones
	 * sombres bordees d'aretes droites, qui sont les frontieres des chunks
	 * charges et non des cotes.
	 */
	void BuildGroundProxy();

	/** Retire ou rend le sol de fond selon que l'oeil est sous terre ou non. */
	void UpdateGroundProxyVisibility();

	FTimerHandle ProxyTimer;
	bool bGroundProxyHidden = false;

	/** Mesures concordantes avant de basculer. Anti-rebond au bord d'un plafond. */
	int32 ProxyVotes = 0;

	/** Le materiau correspondant au mode d'apparence courant. */
	UMaterialInterface* ChooseTerrainMaterial(const FWorldseedAppearance& Mode) const;

	UPROPERTY()
	TObjectPtr<USceneComponent> RootScene;

	UPROPERTY()
	TMap<FIntPoint, FWorldseedChunk> Chunks;

	/**
	 * Le sol de fond, sur son propre acteur.
	 *
	 * IL PORTE AUSSI LE UWaterTerrainComponent, et c'est toute la raison de son
	 * existence separee : le plugin Water redemande sa texture d'information des
	 * qu'un composant de l'acteur porteur salit son etat de rendu. Sur ce
	 * terrain-ci, cela voudrait dire a chaque chunk. Voir WorldseedGroundProxy.h.
	 */
	UPROPERTY()
	TObjectPtr<class AWorldseedGroundProxy> GroundProxy;

	/** Heightfield en metres, 0 au niveau de la mer. Indexe J * NX + I. */
	TArray<float> HeightsM;

	/** Climat, meme indexation. Pilote les couleurs de biome. */
	TArray<float> TempC;
	TArray<float> PrecipMm;

	/**
	 * Ecart annuel froid/chaud, meme indexation.
	 *
	 * Vient du modele climatique, qui seul connait la continentalite : a
	 * latitude egale, un coeur de continent gele l'hiver la ou une cote reste
	 * douce. C'est cette grille qui regle les saisons d'Ultra Dynamic Sky.
	 */
	TArray<float> SeasonalAmpC;

	/**
	 * Continentalite, meme indexation : 0 au bord de mer, 1 loin des cotes.
	 *
	 * Elle ouvre l'ecart jour/nuit du prereglage climatique — un interieur de
	 * continent perd la nuit ce qu'il a gagne le jour, une cote non.
	 */
	TArray<float> ContinentalityGrid;

	FWorldseedGeometry Geometry;

	/** Graine du monde charge. Ensemence aussi le signal meteo. */
	int32 WorldSeed = 0;

	FTimerHandle UpdateTimer;

	/** Le pilotage du ciel, qui ne sait rien du terrain. */
	UPROPERTY(VisibleAnywhere, Category = "Worldseed|Ciel")
	TObjectPtr<class UWorldseedSkyDriverComponent> SkyDriver;

	/** La pose de l'ocean. */
	UPROPERTY(VisibleAnywhere, Category = "Worldseed|Eau")
	TObjectPtr<class UWorldseedWaterComponent> Water;

	/** Les 19 biomes du monde charge. */
	FWorldseedBiomeMap Biomes;

	/**
	 * Le reseau de grottes du monde charge.
	 *
	 * Il n'est pas transporte par le menu -- il se rebatit depuis la lithologie
	 * et le climat -- et il est transmis tel quel au mailleur voxel, pour que
	 * les deux acteurs decrivent bien le meme sous-sol.
	 */
	FWorldseedCaveNetwork Caves;

	/**
	 * De quelle roche est fait le sous-sol.
	 *
	 * ELLE NE SERT PAS QU'A SEMER LES CHAMBRES : le champ de densite s'en sert
	 * aussi pour decider ou s'ouvrent les DIACLASES, qui sont la cavite de la
	 * roche insoluble. Le karst et la fracture se partagent ainsi le monde
	 * selon la roche, et jamais selon un reglage.
	 */
	FWorldseedLithology Lithology;

	/** L'acteur voxel pose par cet acteur, quand bUseVoxelMesher est vrai. */
	UPROPERTY()
	TObjectPtr<AWorldseedVoxelTerrain> VoxelTerrain;

	int32 ChunksX = 0;
	int32 ChunksY = 0;
};

