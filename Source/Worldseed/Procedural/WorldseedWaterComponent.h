// Worldseed - pose de l'ocean, des lacs et des cours d'eau dans le monde.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Procedural/WorldseedHydrology.h"
#include "Procedural/WorldseedWaterBodies.h"
#include "Procedural/WorldseedRules.h"

#include "WorldseedWaterComponent.generated.h"

class UMaterialInterface;
class UProceduralMeshComponent;

/**
 * Donne un corps aux nappes et aux cours d'eau calcules par l'hydrologie.
 *
 * IL NE CALCULE RIEN : il recoit un FWorldseedHydrology tout fait et le
 * transforme en maillages. Ce decoupage permet de sonder l'hydrologie sans
 * moteur de rendu, et de changer la representation sans toucher a l'algorithme.
 *
 * TOUT EST CONSTRUIT EN UNE FOIS, sans streaming. Le calcul le justifie :
 * soixante rivieres de cent vingt points et une quarantaine de lacs tiennent
 * dans quelques dizaines de milliers de triangles — moins qu'un seul chunk de
 * terrain a pleine resolution. Decouper l'eau en morceaux ajouterait de la
 * machinerie pour rien.
 */
UCLASS(ClassGroup = (Worldseed), meta = (BlueprintSpawnableComponent))
class WORLDSEED_API UWorldseedWaterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWorldseedWaterComponent();

	/**
	 * Confie l'ocean et les lacs au plugin Water d'Unreal.
	 *
	 * On y gagne les vagues, les caustiques, le rendu sous-marin, la
	 * flottabilite et la nage. Faux, ou si le plugin ne repond pas, on retombe
	 * sur des nappes en maillage procedural : correctes, mais inertes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Eau")
	bool bUseWaterPlugin = true;

	/** Pose l'ocean : un plan a l'altitude zero, qui est le niveau de la mer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Eau")
	bool bBuildOcean = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Eau")
	bool bBuildLakes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Eau")
	bool bBuildRivers = true;

	/**
	 * Donne aux cascades leur propre maillage.
	 *
	 * ELLES EXISTENT DEJA DANS LE RUBAN DES RIVIERES — le trace suit la ligne de
	 * plus grande pente, donc il passe forcement par la chute. Mais noyees dans
	 * le cours, elles s'y lisent comme une nappe tres inclinee et non comme une
	 * chute. Les sortir dans un maillage a part permet de les eclairer
	 * autrement : ecume blanche plutot qu'eau calme.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Eau")
	bool bBuildWaterfalls = true;

	/**
	 * Elargissement de la chute par rapport au lit, sans dimension.
	 *
	 * Une chute s'evase en tombant : garder la largeur du lit donnerait un
	 * ruban qui se contente de pencher.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Eau",
		meta = (ClampMin = "1.0", EditCondition = "bBuildWaterfalls"))
	float WaterfallWidthScale = 1.6f;

	/**
	 * Enfoncement du lit sous le terrain, en metres.
	 *
	 * Le ruban suit le relief ; sans un leger retrait, il coincide avec le sol
	 * et les deux surfaces clignotent l'une a travers l'autre.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Eau",
		meta = (ClampMin = "0.0"))
	float RiverBedOffsetM = 0.15f;

	/** Nombre de subdivisions du plan d'ocean, par cote. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Eau",
		meta = (ClampMin = "1", ClampMax = "256"))
	int32 OceanSubdivisions = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Eau")
	TObjectPtr<UMaterialInterface> OceanMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Eau")
	TObjectPtr<UMaterialInterface> LakeMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Eau")
	TObjectPtr<UMaterialInterface> RiverMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Eau")
	TObjectPtr<UMaterialInterface> WaterfallMaterial;

	/**
	 * Construit toutes les surfaces d'eau du monde.
	 *
	 * SampleHeightM rend l'altitude du terrain en metres pour une cellule
	 * donnee : les rivieres en ont besoin pour epouser le fond de vallee.
	 */
	void Build(const FWorldseedGeometry& Geometry, const FWorldseedHydrology& Hydrology,
		const TArray<float>& ElevationM, float HeightExaggeration);

	/** Efface toutes les surfaces posees. */
	void Clear();

	/**
	 * A appeler quand le terrain a change de forme ou d'etendue.
	 *
	 * Le systeme d'eau lit le sol pour savoir ou l'eau rencontre la terre ; un
	 * terrain qui arrive par morceaux doit donc le lui redire.
	 */
	void NotifyGroundChanged();

	/**
	 * Delai avant le releve d'etat de l'eau, en secondes.
	 *
	 * Le temps que la zone rende sa texture d'information au moins une fois :
	 * avant cela ses bornes ne veulent rien dire.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Eau",
		meta = (ClampMin = "0.5"))
	float HealthDelayS = 5.0f;

	/**
	 * Trace le reseau hydrographique en lignes de debogage.
	 *
	 * Bleu : confie au plugin. Orange : maillage procedural. Blanc : largeur
	 * du lit. Rouge : la surface d'eau passe AU-DESSUS du terrain. Vert : elle
	 * passe dessous. Voir WorldseedWaterDebug.h.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Eau")
	bool bDrawRiverTrace = true;

	/**
	 * Delai minimal entre deux reconstructions de la texture d'information.
	 *
	 * ELLE NE SE RECONSTRUIT PAS GRATUITEMENT : le moteur y redessine tout le
	 * sol et tous les corps d'eau. La redemander a chaque chunk revenait a la
	 * refaire en continu tant que le joueur marche — et comme elle renonce et
	 * se replanifie quand les shaders ne sont pas prets, elle n'arrivait jamais
	 * a converger au demarrage. D'ou une eau tres longue a apparaitre.
	 *
	 * Le sol de fond couvre desormais toute la carte des la generation : ces
	 * rafraichissements ne servent plus qu'a affiner le rivage pres du joueur,
	 * ce qui n'a aucune raison d'etre fait plus d'une fois par seconde.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Eau",
		meta = (ClampMin = "0.0"))
	float GroundRefreshPeriodS = 1.0f;

	UFUNCTION(BlueprintPure, Category = "Worldseed|Eau")
	int32 GetWaterSectionCount() const { return SectionCount; }

	/**
	 * Position monde du sommet de chaque cascade, la plus haute d'abord.
	 *
	 * De quoi aller les voir sans les chercher : sur une grande carte, cent
	 * quarante chutes se perdent dans le relief.
	 */
	UFUNCTION(BlueprintPure, Category = "Worldseed|Eau")
	const TArray<FVector>& GetWaterfallLocations() const { return WaterfallTops; }

private:
	/** Cree ou reutilise le composant de maillage d'un usage donne. */
	UProceduralMeshComponent* EnsureMesh(FName Name,
		TObjectPtr<UProceduralMeshComponent>& Slot, UMaterialInterface* Material);

	void BuildOcean(const FWorldseedGeometry& Geometry);
	void BuildLakes(const FWorldseedGeometry& Geometry,
		const FWorldseedHydrology& Hydrology, float HeightExaggeration);
	void BuildRivers(const FWorldseedGeometry& Geometry,
		const FWorldseedHydrology& Hydrology, const TArray<float>& ElevationM,
		float HeightExaggeration);

	void BuildWaterfalls(const FWorldseedGeometry& Geometry,
		const FWorldseedHydrology& Hydrology, const TArray<float>& ElevationM,
		float HeightExaggeration);

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> OceanMesh;

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> LakeMesh;

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> RiverMesh;

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> WaterfallMesh;

	/** Sommets des chutes, en coordonnees monde. */
	TArray<FVector> WaterfallTops;

	int32 SectionCount = 0;

	/** Ce que le plugin Water a pose, s'il a repondu. */
	FWorldseedWaterBodies WaterBodies;

	/** Un seul coup, apres la pose. */
	FTimerHandle HealthTimer;

	/** Date du dernier rafraichissement, pour l'espacer. */
	float LastGroundRefreshTime = -BIG_NUMBER;
};
