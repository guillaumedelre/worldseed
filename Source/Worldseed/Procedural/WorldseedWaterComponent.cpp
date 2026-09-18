// Worldseed - pose de l'ocean dans le monde.

#include "Procedural/WorldseedWaterComponent.h"

#include "Procedural/WorldseedWaterMesh.h"

#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "TimerManager.h"

namespace
{
	/**
	 * Debord du plan d'ocean au-dela de la carte.
	 *
	 * L'horizon doit rester de l'eau quand on regarde vers le large : un plan
	 * s'arretant pile au bord de la carte laisserait voir le vide par-dessus.
	 */
	constexpr float OceanOversize = 3.0f;

	/** L'eau ne porte pas le joueur et n'a pas a etre testee par les rayons. */
	void MakeNonColliding(UProceduralMeshComponent* Mesh)
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->bUseAsyncCooking = false;
	}
}

UWorldseedWaterComponent::UWorldseedWaterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UProceduralMeshComponent* UWorldseedWaterComponent::EnsureMesh(FName Name,
	TObjectPtr<UProceduralMeshComponent>& Slot, UMaterialInterface* Material)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	if (!Slot)
	{
		Slot = NewObject<UProceduralMeshComponent>(Owner, Name);
		Slot->SetupAttachment(Owner->GetRootComponent());
		MakeNonColliding(Slot);
		Slot->RegisterComponent();
	}

	Slot->ClearAllMeshSections();
	if (Material)
	{
		Slot->SetMaterial(0, Material);
	}
	return Slot;
}

void UWorldseedWaterComponent::Clear()
{
	LastGroundRefreshTime = -BIG_NUMBER;
	WorldseedWaterBodies::Clear(WaterBodies);
	if (OceanMesh)
	{
		OceanMesh->ClearAllMeshSections();
	}
	SectionCount = 0;
}

void UWorldseedWaterComponent::Build(const FWorldseedGeometry& Geometry,
	const TArray<float>& ElevationM, float HeightExaggeration)
{
	Clear();

	if (Geometry.NX < 2)
	{
		return;
	}

	const double StartTime = FPlatformTime::Seconds();

	// LE POINT LE PLUS BAS DU MONDE, qui donne son epaisseur a l'ocean. Zero
	// si le relief n'atteint jamais la mer : la pose y mettra son plancher.
	//
	// CE N'EST PLUS UNE PRECAUTION, C'EST LA CONDITION DU RENDU. La zone
	// d'eau normalise toutes ses hauteurs dans l'intervalle en Z de ses corps ;
	// l'ocean etant desormais le SEUL, un ocean sans epaisseur donne un
	// intervalle nul et l'eau cesse de se dessiner.
	float SeabedM = 0.0f;
	for (const float M : ElevationM)
	{
		SeabedM = FMath::Min(SeabedM, M);
	}

	// --- le plugin Water d'abord ---------------------------------------------
	// S'il repond, il prend l'ocean et le maillage n'a plus lieu d'etre.
	bool bPluginTookSurfaces = false;
	if (bUseWaterPlugin)
	{
		bPluginTookSurfaces = WorldseedWaterBodies::Build(
			GetWorld(), Geometry, HeightExaggeration, SeabedM, WaterBodies);
	}

	if (bBuildOcean && !bPluginTookSurfaces)
	{
		BuildOcean(Geometry);
	}

	// ZERO SECTION N'EST PAS UN ECHEC : c'est le cas nominal, celui ou le
	// plugin a pris la surface. D'ou le libelle avant le compte.
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] ocean pose : %s, %d section(s) procedurale(s)  (%.0f ms)"),
		bPluginTookSurfaces ? TEXT("plugin Water") : TEXT("maillage procedural"),
		SectionCount, (FPlatformTime::Seconds() - StartTime) * 1000.0);

	// LE RELEVE VIENT APRES COUP, ET C'EST TOUT L'INTERET.
	//
	// Au moment de la pose, la zone n'a encore rien rendu : ses bornes de
	// hauteur et son plancher de sol valent leurs valeurs d'usine, et un
	// releve pris ici ne dirait rien. Quelques secondes plus tard, ils disent
	// exactement ce que la texture d'information a vu.
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().SetTimer(HealthTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				WorldseedWaterBodies::LogHealth(GetWorld(), WaterBodies);
			}),
			HealthDelayS, false);
	}
}

void UWorldseedWaterComponent::NotifyGroundChanged()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// On ESPACE plutot qu'on ne compte : ce qui coute n'est pas le nombre de
	// chunks arrives, c'est la frequence a laquelle on redessine la texture.
	const float Now = World->GetTimeSeconds();
	if (Now - LastGroundRefreshTime < GroundRefreshPeriodS)
	{
		return;
	}
	LastGroundRefreshTime = Now;

	WorldseedWaterBodies::NotifyGroundChanged(WaterBodies);
}

void UWorldseedWaterComponent::BuildOcean(const FWorldseedGeometry& Geometry)
{
	UProceduralMeshComponent* Mesh = EnsureMesh(TEXT("Ocean"), OceanMesh, OceanMaterial);
	if (!Mesh)
	{
		return;
	}

	FWorldseedMeshBuffer Buffer;

	// L'ALTITUDE ZERO EST LE NIVEAU DE LA MER par construction : toute la chaine
	// de generation cale son quantile dessus. L'ocean n'a donc rien a chercher,
	// il se pose a zero.
	WorldseedWaterMesh::BuildPlane(
		Geometry.WidthM() * WorldseedMetersToCm * OceanOversize,
		Geometry.HeightM * WorldseedMetersToCm * OceanOversize,
		0.0f, OceanSubdivisions, Buffer);

	if (Buffer.IsEmpty())
	{
		return;
	}

	Mesh->CreateMeshSection(0, Buffer.Vertices, Buffer.Triangles, Buffer.Normals,
		Buffer.UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	++SectionCount;
}
