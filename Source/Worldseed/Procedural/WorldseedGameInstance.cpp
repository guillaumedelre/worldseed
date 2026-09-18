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
	World = InWorld;
	bHasWorld = InWorld.IsValid();

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] monde mis en cache : seed=%d  %dx%d  %d valeurs  saisons=%d  (valide=%d)"),
		World.Seed, World.Geometry.NX, World.Geometry.NY, World.ElevationM.Num(),
		World.SeasonalAmpC.Num(), bHasWorld ? 1 : 0);
}

bool UWorldseedGameInstance::TryGetWorld(FWorldseedWorldData& OutWorld) const
{
	if (!bHasWorld || !World.IsValid())
	{
		return false;
	}

	OutWorld = World;
	return true;
}
