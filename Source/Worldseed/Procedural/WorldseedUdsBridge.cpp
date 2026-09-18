// Worldseed - acces aux acteurs Ultra Dynamic Sky, par reflexion.

#include "Procedural/WorldseedUdsBridge.h"

#include "EngineUtils.h"
#include "UObject/UnrealType.h"

namespace
{
	/**
	 * Ecrit un nombre dans une propriete designee par son nom.
	 *
	 * Depuis UE5 un "float" de Blueprint est un double ; un pack plus ancien
	 * peut encore exposer un vrai float. On accepte les deux plutot que de
	 * parier sur l'un.
	 */
	bool SetNumber(AActor* Actor, FName PropertyName, double Value)
	{
		if (!Actor || PropertyName.IsNone())
		{
			return false;
		}

		FProperty* Property = Actor->GetClass()->FindPropertyByName(PropertyName);
		if (FDoubleProperty* AsDouble = CastField<FDoubleProperty>(Property))
		{
			AsDouble->SetPropertyValue_InContainer(Actor, Value);
			return true;
		}
		if (FFloatProperty* AsFloat = CastField<FFloatProperty>(Property))
		{
			AsFloat->SetPropertyValue_InContainer(Actor, static_cast<float>(Value));
			return true;
		}
		return false;
	}

	bool GetNumber(const AActor* Actor, FName PropertyName, double& OutValue)
	{
		if (!Actor || PropertyName.IsNone())
		{
			return false;
		}

		FProperty* Property = Actor->GetClass()->FindPropertyByName(PropertyName);
		if (const FDoubleProperty* AsDouble = CastField<FDoubleProperty>(Property))
		{
			OutValue = AsDouble->GetPropertyValue_InContainer(Actor);
			return true;
		}
		if (const FFloatProperty* AsFloat = CastField<FFloatProperty>(Property))
		{
			OutValue = AsFloat->GetPropertyValue_InContainer(Actor);
			return true;
		}
		return false;
	}

	/** Les plages de temperature d'UDS ont la forme d'un FVector2D. */
	bool SetRange(AActor* Actor, FName PropertyName, const FVector2D& Value)
	{
		if (!Actor || PropertyName.IsNone())
		{
			return false;
		}

		FStructProperty* Property = CastField<FStructProperty>(
			Actor->GetClass()->FindPropertyByName(PropertyName));
		if (!Property || Property->Struct != TBaseStructure<FVector2D>::Get())
		{
			return false;
		}

		*Property->ContainerPtrToValuePtr<FVector2D>(Actor) = Value;
		return true;
	}

	/** Lit une enumeration d'octet, quelle que soit sa forme de stockage. */
	bool GetEnumByte(const AActor* Actor, FName PropertyName, uint8& OutValue)
	{
		if (!Actor || PropertyName.IsNone())
		{
			return false;
		}

		FProperty* Property = Actor->GetClass()->FindPropertyByName(PropertyName);
		if (const FByteProperty* AsByte = CastField<FByteProperty>(Property))
		{
			OutValue = AsByte->GetPropertyValue_InContainer(Actor);
			return true;
		}
		if (const FEnumProperty* AsEnum = CastField<FEnumProperty>(Property))
		{
			const void* Value = AsEnum->ContainerPtrToValuePtr<void>(Actor);
			OutValue = static_cast<uint8>(
				AsEnum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value));
			return true;
		}
		return false;
	}

	FString ClassNameOf(const TWeakObjectPtr<AActor>& Actor)
	{
		return Actor.IsValid() ? Actor->GetClass()->GetName() : TEXT("-");
	}
}

bool FWorldseedUdsBridge::Resolve(UWorld* World)
{
	if (IsValid() || !World)
	{
		return IsValid();
	}

	const FString SkyPrefix = SkyClassPrefix.ToString();
	const FString WeatherPrefix = WeatherClassPrefix.ToString();

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const FString ClassName = It->GetClass()->GetName();

		// L'ORDRE COMPTE : "Ultra_Dynamic_Weather" commence par un prefixe plus
		// court que lui, et serait attrape par le test du ciel s'il passait
		// d'abord.
		if (!WeatherActor.IsValid() && ClassName.StartsWith(WeatherPrefix))
		{
			WeatherActor = *It;
		}
		else if (!SkyActor.IsValid() && ClassName.StartsWith(SkyPrefix))
		{
			SkyActor = *It;
		}
	}

	if (!IsValid())
	{
		return false;
	}

	// UDS compte en Fahrenheit par defaut, le climat en Celsius. On lit l'unite
	// plutot que de la supposer : elle se change dans le detail de l'acteur.
	uint8 Scale = 0;
	if (GetEnumByte(WeatherActor.Get(), TemperatureScaleProperty, Scale)
		|| GetEnumByte(SkyActor.Get(), TemperatureScaleProperty, Scale))
	{
		TemperatureScale = (Scale == 0)
			? ETemperatureScale::Fahrenheit : ETemperatureScale::Celsius;
	}

	// LA PERIODE DE LA SAISON EST SUPPOSEE EN MOIS. Un releve a donne
	// Season = 3,78 pour un libelle "Early Spring", ce qui correspond a une
	// echelle de douze. La valeur brute est journalisee ci-dessous : si le
	// libelle et la phase divergent, c'est ici qu'il faut corriger.
	double RawSeason = 0.0;
	const bool bHasSeason = GetNumber(WeatherActor.Get(), SeasonProperty, RawSeason)
		|| GetNumber(SkyActor.Get(), SeasonProperty, RawSeason);

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] ciel : %s  temperatures en %s  saison brute %.2f (sur %.0f)"),
		*Describe(),
		TemperatureScale == ETemperatureScale::Fahrenheit ? TEXT("Fahrenheit") : TEXT("Celsius"),
		bHasSeason ? RawSeason : -1.0, SeasonPeriod);

	return true;
}

FString FWorldseedUdsBridge::Describe() const
{
	return FString::Printf(TEXT("%s / %s"),
		*ClassNameOf(SkyActor), *ClassNameOf(WeatherActor));
}

bool FWorldseedUdsBridge::ReadSeasonPhase(float& OutPhase) const
{
	double Raw = 0.0;
	if (!GetNumber(WeatherActor.Get(), SeasonProperty, Raw)
		&& !GetNumber(SkyActor.Get(), SeasonProperty, Raw))
	{
		return false;
	}

	const float Phase = static_cast<float>(Raw) / FMath::Max(SeasonPeriod, 1e-3f);
	OutPhase = Phase - FMath::FloorToFloat(Phase);
	return true;
}

bool FWorldseedUdsBridge::WriteLatLon(float LatitudeDeg, float LongitudeDeg) const
{
	AActor* Sky = SkyActor.Get();
	if (!SetNumber(Sky, LatitudeProperty, LatitudeDeg))
	{
		return false;
	}

	SetNumber(Sky, LongitudeProperty, LongitudeDeg);
	return true;
}

bool FWorldseedUdsBridge::WriteNumber(FName PropertyName, double Value) const
{
	// UDS repartit ces variables entre l'acteur ciel et l'acteur meteo, et la
	// repartition varie d'une version a l'autre. On propose donc a l'un puis a
	// l'autre : celui qui possede la variable l'accepte, l'autre refuse.
	return SetNumber(WeatherActor.Get(), PropertyName, Value)
		|| SetNumber(SkyActor.Get(), PropertyName, Value);
}

bool FWorldseedUdsBridge::WriteRange(FName PropertyName, const FVector2D& Value) const
{
	return SetRange(WeatherActor.Get(), PropertyName, Value)
		|| SetRange(SkyActor.Get(), PropertyName, Value);
}
