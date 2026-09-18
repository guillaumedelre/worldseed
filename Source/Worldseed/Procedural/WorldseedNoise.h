// Worldseed - generation de relief deterministe.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WorldseedNoise.generated.h"

class UTexture2D;

/**
 * Parametres de generation. Deux appels avec la meme structure produisent
 * exactement le meme relief : c'est ce qui garantit que l'apercu du menu
 * correspond au terrain de jeu.
 */
USTRUCT(BlueprintType)
struct WORLDSEED_API FWorldseedTerrainParams
{
	GENERATED_BODY()

	// ------------------------------------------------------------------ base

	/** Graine du monde. Seul champ a changer pour obtenir un autre monde. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed")
	int32 Seed = 1337;

	/** Cote de la carte, en metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed",
		meta = (ClampMin = "100.0", UIMin = "250.0", UIMax = "8000.0"))
	float MapSizeMeters = 1000.0f;

	/** Nombre de sommets par cote. 512 => ~2 m de resolution sur 1 km. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed",
		meta = (ClampMin = "16", UIMin = "64", UIMax = "2048"))
	int32 Resolution = 512;

	/** Amplitude verticale maximale, en metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Relief",
		meta = (ClampMin = "1.0"))
	float HeightScale = 350.0f;

	// --------------------------------------------------------------- fractal

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Relief",
		meta = (ClampMin = "1", ClampMax = "12"))
	int32 Octaves = 7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Relief",
		meta = (ClampMin = "0.1"))
	float BaseFrequency = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Relief",
		meta = (ClampMin = "1.1"))
	float Lacunarity = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Relief",
		meta = (ClampMin = "0.05", ClampMax = "0.95"))
	float Gain = 0.5f;

	/** Dosage des chaines (ridged) par rapport aux plaines (fBm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Relief",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MountainAmount = 0.6f;

	/** Attenuation des bords, pour une masse de terre isolee. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Relief",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ContinentFalloff = 0.7f;

	// -------------------------------------------------------- domain warping

	/**
	 * Deplace les coordonnees d'echantillonnage par un second champ de bruit.
	 * Deplie les formes rondes du fBm en plis et coulees d'allure geologique.
	 * 0 desactive.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Warping",
		meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float WarpStrength = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Warping",
		meta = (ClampMin = "0.1"))
	float WarpFrequency = 1.5f;

	// ------------------------------------------------------------ tectonique

	/**
	 * Plaques de Voronoi : le soulevement se concentre sur leurs frontieres,
	 * ce qui donne des chaines lineaires et arquees au lieu de taches.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Tectonique",
		meta = (ClampMin = "2", ClampMax = "64"))
	int32 PlateCount = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Tectonique",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TectonicAmount = 0.45f;

	/** Plus la valeur est haute, plus les chaines sont etroites. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Tectonique",
		meta = (ClampMin = "1.0", ClampMax = "40.0"))
	float TectonicSharpness = 12.0f;

	// ------------------------------------------------------ erosion thermique

	/** Nombre de passes. Adoucit les sommets et cree les eboulis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Erosion",
		meta = (ClampMin = "0", ClampMax = "200"))
	int32 ThermalIterations = 25;

	/** Angle de stabilite des eboulis, en degres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Erosion",
		meta = (ClampMin = "5.0", ClampMax = "70.0"))
	float TalusAngleDegrees = 34.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Erosion",
		meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float ThermalRate = 0.5f;

	// ---------------------------------------------------- erosion hydraulique

	/**
	 * Nombre de gouttes simulees. C'est ce parametre qui fait emerger les
	 * vallees et les reseaux de drainage ramifies.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Erosion",
		meta = (ClampMin = "0", ClampMax = "1000000"))
	int32 DropletCount = 120000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Erosion",
		meta = (ClampMin = "1", ClampMax = "200"))
	int32 DropletLifetime = 48;

	/** 0 = la goutte suit strictement la pente, 1 = elle garde son elan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Erosion",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Inertia = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Erosion",
		meta = (ClampMin = "0.1"))
	float SedimentCapacityFactor = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Erosion")
	float MinSedimentCapacity = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Erosion",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ErodeSpeed = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Erosion",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DepositSpeed = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Erosion",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EvaporateSpeed = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Erosion",
		meta = (ClampMin = "0.1"))
	float Gravity = 4.0f;

	/** Rayon du pinceau d'erosion, en cellules. Evite les trous ponctuels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Erosion",
		meta = (ClampMin = "1", ClampMax = "8"))
	int32 ErosionBrushRadius = 3;

	/** Egalite stricte : sert a valider le cache de heightfield. */
	bool operator==(const FWorldseedTerrainParams& Other) const;
	bool operator!=(const FWorldseedTerrainParams& Other) const { return !(*this == Other); }
};

/**
 * Contexte pre-calcule. Les decalages sont tires une seule fois depuis un
 * FRandomStream, jamais depuis FMath::Rand : ce dernier a un etat global
 * partage et casserait la reproductibilite.
 */
struct WORLDSEED_API FWorldseedNoiseContext
{
	TArray<FVector2D> OctaveOffsets;
	FVector2D MountainMaskOffset = FVector2D::ZeroVector;
	FVector2D ContinentOffset = FVector2D::ZeroVector;
	FVector2D WarpOffsetA = FVector2D::ZeroVector;
	FVector2D WarpOffsetB = FVector2D::ZeroVector;
	TArray<FVector2D> PlateCenters;

	void Init(const FWorldseedTerrainParams& Params);
};

UCLASS()
class WORLDSEED_API UWorldseedNoise : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Altitude normalisee [0..1] AVANT erosion, en coordonnees de carte [0..1].
	 * Attention : apres erosion le terrain ne suit plus cette fonction, il faut
	 * echantillonner le heightfield. Utile pour sonder la forme de base.
	 */
	UFUNCTION(BlueprintPure, Category = "Worldseed|Noise")
	static float SampleHeight01(const FWorldseedTerrainParams& Params, float U, float V);

	/** Meme calcul, avec un contexte deja construit. */
	static float SampleHeight01Fast(const FWorldseedTerrainParams& Params,
		const FWorldseedNoiseContext& Ctx, float U, float V);

	/**
	 * Pipeline complet : bruit warpe + tectonique, puis erosion thermique,
	 * puis erosion hydraulique. Remplit un heightfield Resolution x Resolution
	 * en metres, indexe Y * Resolution + X.
	 */
	static void GenerateHeightfield(const FWorldseedTerrainParams& Params,
		TArray<float>& OutHeightsMeters);

	/** Echantillonnage bilineaire d'un heightfield, en coordonnees [0..1]. */
	static float SampleHeightfieldBilinear(const TArray<float>& Heights,
		int32 Resolution, float U, float V);

	/**
	 * Texture d'apercu. Recalcule le pipeline complet : a n'utiliser que si on
	 * ne dispose pas deja du heightfield.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Noise")
	static UTexture2D* CreatePreviewTexture(const FWorldseedTerrainParams& Params,
		int32 PreviewResolution = 256);

	/**
	 * Apercu construit a partir d'un heightfield deja calcule : c'est cette
	 * voie qui garantit que la miniature est exactement le monde.
	 *
	 * Rendu en relief ombre plus teinte par altitude, et non en niveaux de
	 * gris : sur un terrain erode, une carte de hauteurs brute ne laisse
	 * quasiment pas voir les vallees ni les reseaux de drainage, qui sont
	 * pourtant tout l'interet des passes d'erosion.
	 */
	static UTexture2D* CreatePreviewTextureFromHeightfield(
		const TArray<float>& Heights, int32 Resolution, float MapSizeMeters,
		int32 PreviewResolution);

	/** Latitude en degres [-90..90] pour une coordonnee de carte V [0..1]. */
	UFUNCTION(BlueprintPure, Category = "Worldseed|Climat")
	static float MapVToLatitude(float V, float LatitudeSpanDegrees = 180.0f);
};
