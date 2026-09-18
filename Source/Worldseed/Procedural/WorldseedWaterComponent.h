// Worldseed - pose de l'ocean dans le monde.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Procedural/WorldseedWaterBodies.h"
#include "Procedural/WorldseedRules.h"

#include "WorldseedWaterComponent.generated.h"

class UMaterialInterface;
class UProceduralMeshComponent;

/**
 * Donne un corps a la mer.
 *
 * IL NE CALCULE RIEN : l'altitude zero EST le niveau de la mer, par
 * construction — toute la chaine de generation cale son quantile dessus. Ce
 * composant n'a donc rien a chercher, il pose une surface a zero et la confie
 * au plugin Water.
 *
 * IL N'Y A PLUS DE LACS NI DE RIVIERES. L'hydrologie a ete retiree du
 * generateur le 18 septembre 2026 ; voir CLAUDE.md. Ce qui reste ici est la
 * mer, et le maillage de secours qui la dessine si le plugin ne repond pas.
 */
UCLASS(ClassGroup = (Worldseed), meta = (BlueprintSpawnableComponent))
class WORLDSEED_API UWorldseedWaterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWorldseedWaterComponent();

	/**
	 * Confie l'ocean au plugin Water d'Unreal.
	 *
	 * On y gagne les vagues, les caustiques, le rendu sous-marin, la
	 * flottabilite et la nage. Faux, ou si le plugin ne repond pas, on retombe
	 * sur une nappe en maillage procedural : correcte, mais inerte.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Eau")
	bool bUseWaterPlugin = true;

	/** Pose l'ocean : un plan a l'altitude zero, qui est le niveau de la mer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Eau")
	bool bBuildOcean = true;

	/** Nombre de subdivisions du plan d'ocean, par cote. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Eau",
		meta = (ClampMin = "1", ClampMax = "256"))
	int32 OceanSubdivisions = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Eau")
	TObjectPtr<UMaterialInterface> OceanMaterial;

	/**
	 * Construit la surface d'ocean.
	 *
	 * ElevationM ne sert qu'a UNE chose, mais elle est essentielle : le point
	 * le plus bas du monde, qui donne son epaisseur a l'ocean. Voir
	 * WorldseedWaterBodies::Build.
	 */
	void Build(const FWorldseedGeometry& Geometry, const TArray<float>& ElevationM,
		float HeightExaggeration);

	/** Efface la surface posee. */
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

private:
	/** Cree ou reutilise le composant de maillage d'un usage donne. */
	UProceduralMeshComponent* EnsureMesh(FName Name,
		TObjectPtr<UProceduralMeshComponent>& Slot, UMaterialInterface* Material);

	void BuildOcean(const FWorldseedGeometry& Geometry);

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> OceanMesh;

	int32 SectionCount = 0;

	/** Ce que le plugin Water a pose, s'il a repondu. */
	FWorldseedWaterBodies WaterBodies;

	/** Un seul coup, apres la pose. */
	FTimerHandle HealthTimer;

	/** Date du dernier rafraichissement, pour l'espacer. */
	float LastGroundRefreshTime = -BIG_NUMBER;
};
