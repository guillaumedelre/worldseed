// Worldseed - le sol de fond, et le seul que le systeme d'eau regarde.

#include "Procedural/WorldseedGroundProxy.h"

#include "ProceduralMeshComponent.h"

TArray<UPrimitiveComponent*> UWorldseedWaterTerrainComponent::GetTerrainPrimitives() const
{
	TArray<UPrimitiveComponent*> Result;

	// LE SOL DE FOND D'ABORD : il couvre toute la carte, y compris ce que le
	// joueur n'a pas encore fait charger.
	if (Ground)
	{
		Result.Add(Ground);
	}

	// PUIS LES CHUNKS DETAILLES, qui donnent a l'eau un sol EXACT autour du
	// joueur — la ou elle se regarde.
	//
	// Le defaut de cette methode rendrait toutes les primitives de l'acteur
	// PORTEUR. C'est precisement ce qu'on ne veut pas : ces chunks-la
	// appartiennent au terrain, et le sous-systeme d'eau filtre ses
	// reconstructions par acteur proprietaire. Enumerer ici des primitives
	// d'ailleurs les fait entrer dans la texture d'information sans les faire
	// entrer dans la tempete de reconstructions qui faisait clignoter l'eau.
	if (const AActor* const Terrain = DetailedTerrain.Get())
	{
		TInlineComponentArray<UPrimitiveComponent*> Chunks;
		Terrain->GetComponents(Chunks);
		for (UPrimitiveComponent* Chunk : Chunks)
		{
			if (Chunk && Chunk->IsRegistered())
			{
				Result.Add(Chunk);
			}
		}
	}

	return Result;
}

AWorldseedGroundProxy::AWorldseedGroundProxy()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GroundProxyMesh"));
	SetRootComponent(Mesh);

	// Mobile des la construction : l'acteur nait au runtime, et le plugin Water
	// refuse de composer avec une primitive Statique posee apres coup. La lecon
	// a deja coute une session entiere du cote des corps d'eau.
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->bUseAsyncCooking = false;

	// Ni collision ni ombre : le joueur ne marche jamais dessus, et ses ombres
	// seraient en desaccord avec celles des chunks detailles la ou les deux se
	// recouvrent.
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);

	WaterTerrain = CreateDefaultSubobject<UWorldseedWaterTerrainComponent>(TEXT("WaterTerrain"));

	// SEUL `Mesh` EST DONNE A L'EAU, et c'est tout l'interet de la separation.
	// `GetTerrainPrimitives` est surcharge precisement pour que le defaut --
	// « toutes les primitives de l'acteur » -- ne ramasse pas l'horizon, qui
	// est un objet d'AFFICHAGE et decrirait un sol trop bas.
	WaterTerrain->Ground = Mesh;
}

AWorldseedHorizonProxy::AWorldseedHorizonProxy()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("HorizonMesh"));
	SetRootComponent(Mesh);

	// Memes reglages que le sol de fond, et pour les memes raisons : mobile
	// parce qu'il nait au runtime, sans collision parce qu'on ne marche jamais
	// dessus, sans ombre parce qu'elle jurerait avec celle des chunks.
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->bUseAsyncCooking = false;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);

	// AUCUN COMPOSANT D'EAU ICI, ET C'EST TOUT L'INTERET DE CET ACTEUR. Ni le
	// plugin Water ni `GetTerrainPrimitives` ne le verront : il peut donc
	// s'enfoncer sous la bande creusable sans abaisser le plancher de la
	// WaterZone, qui est la plage ou la texture d'information normalise ses
	// hauteurs.
}

void AWorldseedGroundProxy::SetDetailedTerrain(AActor* Terrain)
{
	if (WaterTerrain)
	{
		WaterTerrain->DetailedTerrain = Terrain;
	}
}
