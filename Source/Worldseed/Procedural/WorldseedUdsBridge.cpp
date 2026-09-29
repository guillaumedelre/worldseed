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
		// LES ENTIERS SE LISENT AUSSI, et leur absence faisait mentir un releve.
		// UDW expose ses trois etats d'ambiance en INT -- `Environment Sound
		// Time Integer`, `... Weather Integer`, `... Wind Integer` -- et le
		// journal les annoncait « illisibles » pour des variables parfaitement
		// presentes. Du bruit qu'on lit comme une information, exactement ce que
		// ce depot a deja retire une fois sur `Simulate Real Sun`.
		if (const FIntProperty* AsInt = CastField<FIntProperty>(Property))
		{
			OutValue = AsInt->GetPropertyValue_InContainer(Actor);
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

bool FWorldseedUdsBridge::ReadYearLength(double& OutDays, FString& OutName) const
{
	// LA LONGUEUR DE L'ANNEE N'EST PAS SUR L'ACTEUR mais sur l'objet que porte
	// sa variable `Calendar` -- et elle y est CALCULEE au demarrage : dans
	// l'asset au repos, `Number of Days in Year` vaut zero, y compris pour le
	// calendrier gregorien livre par UDS, qui fonctionne. Il faut donc la lire
	// en jeu, sur l'instance, et non dans le fichier.
	for (AActor* const Acteur : { SkyActor.Get(), WeatherActor.Get() })
	{
		if (!Acteur) { continue; }
		FObjectProperty* const Prop = CastField<FObjectProperty>(
			Acteur->GetClass()->FindPropertyByName(TEXT("Calendar")));
		if (!Prop) { continue; }

		UObject* const Calendrier = Prop->GetObjectPropertyValue_InContainer(Acteur);
		if (!Calendrier) { continue; }

		OutName = Calendrier->GetName();
		if (FDoubleProperty* const Jours = CastField<FDoubleProperty>(
				Calendrier->GetClass()->FindPropertyByName(TEXT("Number of Days in Year"))))
		{
			OutDays = Jours->GetPropertyValue_InContainer(Calendrier);
			return true;
		}
		if (FIntProperty* const JoursInt = CastField<FIntProperty>(
				Calendrier->GetClass()->FindPropertyByName(TEXT("Number of Days in Year"))))
		{
			OutDays = JoursInt->GetPropertyValue_InContainer(Calendrier);
			return true;
		}
		// Le calendrier est la, sa longueur ne se lit pas : on rend son nom.
		OutDays = 0.0;
		return true;
	}
	return false;
}

bool FWorldseedUdsBridge::CallFunction(FName FunctionName) const
{
	// POSER UNE VARIABLE PAR REFLEXION NE DECLENCHE AUCUN RAPPEL. Une variable
	// Blueprint repliquee avec RepNotify porte un `OnRep_`, que le moteur
	// n'appelle que sur une replication reelle -- et c'est souvent LUI qui fait
	// le travail. Mesure : « Animate Time of Day » pose a vrai, lu a vrai, et
	// l'heure figee a 1300,0000 apres cinq secondes et demie.
	//
	// SANS ARGUMENT, a dessein : passer une pile de parametres mal formee a
	// `ProcessEvent` corromprait la memoire. On n'appelle donc que des fonctions
	// qui n'en prennent pas, et l'on verifie ce point avant l'appel.
	for (AActor* const Acteur : { SkyActor.Get(), WeatherActor.Get() })
	{
		if (!Acteur) { continue; }
		if (UFunction* const Fn = Acteur->FindFunction(FunctionName))
		{
			if (Fn->NumParms != 0)
			{
				continue;
			}
			Acteur->ProcessEvent(Fn, nullptr);
			return true;
		}
	}
	return false;
}

bool FWorldseedUdsBridge::ChangerAmbiance(UObject* NouveauSon, float FonduS,
	bool bChargementAsync) const
{
	AActor* const Acteur = WeatherActor.Get();
	if (!Acteur)
	{
		return false;
	}

	UFunction* const Fn = Acteur->FindFunction(TEXT("Change Environment Sound"));
	if (!Fn)
	{
		return false;
	}

	// --- ON RECONNAIT LA SIGNATURE AVANT DE LA REMPLIR ----------------------
	//
	// Trois parametres exactement, et un de chaque type. On apparie par TYPE et
	// non par nom : les noms Blueprint portent des espaces (« New Sound »,
	// « Fade Duration », « Async Load Source ») que la reflexion assainit d'une
	// version a l'autre, alors que les types, eux, ne bougent pas. Et l'on
	// COMPTE : un pack qui ajouterait un quatrieme argument doit nous trouver
	// muets, pas approximatifs.
	FObjectProperty* Son = nullptr;
	FProperty* Fondu = nullptr;
	FBoolProperty* Async = nullptr;
	int32 Nombre = 0;

	for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		FProperty* const P = *It;
		if (P->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			continue;
		}
		++Nombre;

		if (FObjectProperty* const O = CastField<FObjectProperty>(P))
		{
			if (!Son) { Son = O; continue; }
		}
		else if (CastField<FDoubleProperty>(P) || CastField<FFloatProperty>(P))
		{
			if (!Fondu) { Fondu = P; continue; }
		}
		else if (FBoolProperty* const B = CastField<FBoolProperty>(P))
		{
			if (!Async) { Async = B; continue; }
		}
		// Un parametre d'un type inattendu : on renonce plutot que de deviner.
		return false;
	}

	if (Nombre != 3 || !Son || !Fondu || !Async)
	{
		return false;
	}

	// LE SON DOIT ETRE DU TYPE QUE LA FONCTION ATTEND. Lui passer autre chose
	// ferait un `Cast` nul cote Blueprint -- donc une ambiance coupee en
	// silence, exactement le genre d'echec muet que ce pont existe pour eviter.
	if (NouveauSon && Son->PropertyClass && !NouveauSon->IsA(Son->PropertyClass))
	{
		return false;
	}

	// --- LA PILE, INITIALISEE PUIS DETRUITE ---------------------------------
	uint8* const Pile = static_cast<uint8*>(FMemory_Alloca(Fn->ParmsSize));
	FMemory::Memzero(Pile, Fn->ParmsSize);
	for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		It->InitializeValue_InContainer(Pile);
	}

	Son->SetObjectPropertyValue_InContainer(Pile, NouveauSon);
	Async->SetPropertyValue_InContainer(Pile, bChargementAsync);
	if (FDoubleProperty* const D = CastField<FDoubleProperty>(Fondu))
	{
		D->SetPropertyValue_InContainer(Pile, FonduS);
	}
	else if (FFloatProperty* const F = CastField<FFloatProperty>(Fondu))
	{
		F->SetPropertyValue_InContainer(Pile, FonduS);
	}

	Acteur->ProcessEvent(Fn, Pile);

	for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		It->DestroyValue_InContainer(Pile);
	}
	return true;
}

bool FWorldseedUdsBridge::WriteBool(FName PropertyName, bool bValue) const
{
	for (AActor* const Acteur : { WeatherActor.Get(), SkyActor.Get() })
	{
		if (!Acteur) { continue; }
		if (FBoolProperty* const Prop = CastField<FBoolProperty>(
				Acteur->GetClass()->FindPropertyByName(PropertyName)))
		{
			Prop->SetPropertyValue_InContainer(Acteur, bValue);
			return true;
		}
	}
	return false;
}

bool FWorldseedUdsBridge::ReadNumber(FName PropertyName, double& OutValue) const
{
	return GetNumber(WeatherActor.Get(), PropertyName, OutValue)
		|| GetNumber(SkyActor.Get(), PropertyName, OutValue);
}

bool FWorldseedUdsBridge::ReadBool(FName PropertyName, bool& OutValue) const
{
	// MEME ORDRE QUE L'ECRITURE -- meteo d'abord, ciel ensuite -- pour qu'une
	// variable portee par les deux soit lue la ou elle est ecrite.
	for (const AActor* const Acteur : { WeatherActor.Get(), SkyActor.Get() })
	{
		if (!Acteur) { continue; }
		if (const FBoolProperty* const Prop = CastField<FBoolProperty>(
				Acteur->GetClass()->FindPropertyByName(PropertyName)))
		{
			OutValue = Prop->GetPropertyValue_InContainer(Acteur);
			return true;
		}
	}
	return false;
}

bool FWorldseedUdsBridge::ReadClockRunning(bool& OutRunning) const
{
	// DEUX ORTHOGRAPHES, comme ailleurs dans ce pont : UDS expose ses variables
	// Blueprint avec des espaces, que certaines versions suppriment.
	for (const TCHAR* Nom : { TEXT("Animate Time of Day"), TEXT("AnimateTimeOfDay") })
	{
		for (AActor* const Acteur : { SkyActor.Get(), WeatherActor.Get() })
		{
			if (!Acteur) { continue; }
			if (const FBoolProperty* const Prop = CastField<FBoolProperty>(
					Acteur->GetClass()->FindPropertyByName(FName(Nom))))
			{
				OutRunning = Prop->GetPropertyValue_InContainer(Acteur);
				return true;
			}
		}
	}
	return false;
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
