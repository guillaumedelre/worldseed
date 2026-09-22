// Worldseed - transport du monde genere entre les niveaux.

#include "Procedural/WorldseedGameInstance.h"
#include "Engine/World.h"

UWorldseedGameInstance* UWorldseedGameInstance::GetWorldseedGameInstance(
	const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = WorldContextObject->GetWorld();
	return World ? Cast<UWorldseedGameInstance>(World->GetGameInstance()) : nullptr;
}

void UWorldseedGameInstance::StoreWorld(const FWorldseedWorldData& InWorld)
{
	if (!InWorld.IsValid())
	{
		Monde.Reset();
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] monde refuse : il n'est pas valide"));
		return;
	}

	// L'UNIQUE COPIE DE TOUTE LA CHAINE, et elle a lieu ici parce que le menu
	// tient encore son resultat. A partir de cet instant le monde est partage
	// et immuable : le terrain, l'acteur voxel et chaque travail de maillage
	// n'en prendront qu'une reference.
	Monde = MakeShared<const FWorldseedWorldData, ESPMode::ThreadSafe>(InWorld);

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] monde mis en cache : seed=%d  %dx%d  %d valeurs  saisons=%d"),
		Monde->Seed, Monde->Geometry.NX, Monde->Geometry.NY,
		Monde->ElevationM.Num(), Monde->SeasonalAmpC.Num());
}
