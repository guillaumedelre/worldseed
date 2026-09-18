// Worldseed - la lithologie : de quelle ROCHE est fait le sous-sol.

#include "Procedural/WorldseedLithology.h"

#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedPerlin.h"

#include "Async/ParallelFor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	const TCHAR* SUB = TEXT("substrat");

	int32 IdDeCle(const TArray<FWorldseedLithologyEntry>& Catalogue, const FString& Cle)
	{
		for (int32 I = 0; I < Catalogue.Num(); ++I)
		{
			if (Catalogue[I].Key == Cle) { return I; }
		}
		return INDEX_NONE;
	}
}

FWorldseedLithologyRules FWorldseedLithologyRules::FromRules(const UWorldseedRules& Rules)
{
	FWorldseedLithologyRules Out;

	// --- le catalogue, sur le meme patron que le registre des biomes ---------
	if (const TArray<TSharedPtr<FJsonValue>>* Entrees =
		Rules.Array(SUB, TEXT("lithologies")))
	{
		int32 MaxId = -1;
		for (const TSharedPtr<FJsonValue>& V : *Entrees)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			int32 Id = -1;
			if (V.IsValid() && V->TryGetObject(Obj) && Obj
				&& (*Obj)->TryGetNumberField(TEXT("id"), Id))
			{
				MaxId = FMath::Max(MaxId, Id);
			}
		}
		Out.Catalogue.SetNum(FMath::Max(MaxId + 1, 0));

		for (const TSharedPtr<FJsonValue>& V : *Entrees)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			int32 Id = -1;
			if (!V.IsValid() || !V->TryGetObject(Obj) || !Obj
				|| !(*Obj)->TryGetNumberField(TEXT("id"), Id)
				|| !Out.Catalogue.IsValidIndex(Id))
			{
				continue;
			}

			FWorldseedLithologyEntry& E = Out.Catalogue[Id];
			(*Obj)->TryGetStringField(TEXT("cle"), E.Key);
			(*Obj)->TryGetStringField(TEXT("libelle"), E.Label);

			double Nombre = 0.0;
			if ((*Obj)->TryGetNumberField(TEXT("karstifiable"), Nombre))
			{
				E.Karstifiable = static_cast<float>(Nombre);
			}
			if ((*Obj)->TryGetNumberField(TEXT("durete"), Nombre))
			{
				E.Hardness = static_cast<float>(Nombre);
			}

			const TArray<TSharedPtr<FJsonValue>>* Couleur = nullptr;
			if ((*Obj)->TryGetArrayField(TEXT("couleur"), Couleur) && Couleur->Num() >= 3)
			{
				E.Colour = FLinearColor(FColor(
					static_cast<uint8>((*Couleur)[0]->AsNumber()),
					static_cast<uint8>((*Couleur)[1]->AsNumber()),
					static_cast<uint8>((*Couleur)[2]->AsNumber()), 255));
			}
		}
	}

	// --- l'attribution --------------------------------------------------------
	Out.SocleConvergence = static_cast<float>(
		Rules.Num(SUB, TEXT("lithologieSocleConvergence"), 0.35));
	Out.SocleElevationM = static_cast<float>(
		Rules.Num(SUB, TEXT("lithologieSocleElevationM"), 150.0));
	Out.MotifFrequency = static_cast<float>(
		Rules.Num(SUB, TEXT("lithologieMotifFrequency"), 6.0));
	Out.MotifOctaves = Rules.Int(SUB, TEXT("lithologieMotifOctaves"), 3);

	Out.IdOceanique = IdDeCle(Out.Catalogue,
		Rules.Str(SUB, TEXT("lithologieOceanique"), TEXT("basalte")));
	Out.IdSocle = IdDeCle(Out.Catalogue,
		Rules.Str(SUB, TEXT("lithologieSocle"), TEXT("granite")));

	// Les roches de bassin et leurs proportions, dans l'ordre du fichier.
	if (const TArray<TSharedPtr<FJsonValue>>* Bassin =
		Rules.Array(SUB, TEXT("lithologieBassin")))
	{
		for (const TSharedPtr<FJsonValue>& V : *Bassin)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(Obj) || !Obj) { continue; }

			FString Cle;
			double Part = 0.0;
			if (!(*Obj)->TryGetStringField(TEXT("cle"), Cle)) { continue; }
			(*Obj)->TryGetNumberField(TEXT("part"), Part);

			const int32 Id = IdDeCle(Out.Catalogue, Cle);
			if (Id != INDEX_NONE && Part > 0.0)
			{
				Out.IdsSedimentaires.Add(Id);
				Out.PartsSedimentaires.Add(static_cast<float>(Part));
			}
		}
	}

	return Out;
}

const TCHAR* WorldseedLithology::Name(const FWorldseedLithologyRules& Rules, uint8 Id)
{
	return Rules.Catalogue.IsValidIndex(Id) ? *Rules.Catalogue[Id].Label : TEXT("inconnue");
}

void WorldseedLithology::Compute(const FWorldseedGeometry& Geometry,
	const TArray<float>& ElevationM, const TArray<uint8>& IsContinental,
	const TArray<float>& Convergence, const FWorldseedLithologyRules& Rules,
	int32 Seed, FWorldseedLithology& Out)
{
	const double StartTime = FPlatformTime::Seconds();

	const int32 Count = Geometry.CellCount();
	Out.Id.Reset();
	if (Count <= 0 || ElevationM.Num() != Count || Rules.Catalogue.Num() == 0)
	{
		return;
	}

	const bool bHasCont = (IsContinental.Num() == Count);
	const bool bHasConv = (Convergence.Num() == Count);

	const uint8 IdOcean = static_cast<uint8>(FMath::Max(Rules.IdOceanique, 0));
	const uint8 IdSocle = static_cast<uint8>(FMath::Max(Rules.IdSocle, 0));

	// --- le motif sedimentaire -----------------------------------------------
	//
	// UN BRUIT COHERENT, PAS UN TIRAGE PAR CELLULE. Les bassins sont des
	// ensembles etendus : un calcaire poivre au hasard dans du gres ne
	// ressemble a rien, et surtout ne donnerait jamais le massif d'un seul
	// tenant dont un reseau karstique a besoin.
	TArray<float> Motif;
	WorldseedPerlin::FBMSphere(Motif, Geometry, Rules.MotifFrequency,
		FMath::Max(Rules.MotifOctaves, 1), Seed + 7717);

	// --- les seuils, PAR QUANTILE --------------------------------------------
	//
	// Les proportions demandees doivent etre tenues quelle que soit la
	// distribution du bruit. Poser des seuils en dur sur sa valeur donnerait des
	// parts qui bougent a chaque changement d'octave ou de frequence.
	TArray<float> Seuils;
	if (Rules.IdsSedimentaires.Num() > 1)
	{
		TArray<float> Echantillon;
		Echantillon.Reserve(Count / 4 + 1);
		// ON N'ECHANTILLONNE QUE LE DOMAINE QUI RESTERA AU BASSIN. Prendre toute
		// la terre continentale incluait les cellules que la regle du socle
		// allait emporter juste apres : les quantiles portaient alors sur un
		// domaine plus large que celui auquel ils s'appliquent, et les
		// proportions demandees n'etaient pas tenues. Mesure de l'ecart avant
		// correction : 40,8 / 38,1 / 21,1 % pour 45 / 35 / 20 demandes.
		for (int32 I = 0; I < Count; ++I)
		{
			const bool bOceanique = bHasCont && IsContinental[I] == 0;
			const bool bSocle =
				(bHasConv && Convergence[I] > Rules.SocleConvergence)
				|| (ElevationM[I] > Rules.SocleElevationM);

			if (!bOceanique && !bSocle && ElevationM[I] > 0.0f)
			{
				Echantillon.Add(Motif[I]);
			}
		}

		if (Echantillon.Num() > 0)
		{
			float Somme = 0.0f;
			for (const float P : Rules.PartsSedimentaires) { Somme += P; }
			Somme = FMath::Max(Somme, 1e-6f);

			float Cumul = 0.0f;
			for (int32 K = 0; K + 1 < Rules.IdsSedimentaires.Num(); ++K)
			{
				Cumul += Rules.PartsSedimentaires[K] / Somme;
				Seuils.Add(WorldseedGrid::Quantile(Echantillon, FMath::Clamp(Cumul, 0.0f, 1.0f)));
			}
		}
	}

	Out.Id.SetNumUninitialized(Count);

	ParallelFor(Count, [&](int32 I)
	{
		// 1. La croute oceanique est basaltique. C'est vrai partout sur Terre,
		//    et ca ne demande aucun reglage.
		if (bHasCont && IsContinental[I] == 0)
		{
			Out.Id[I] = IdOcean;
			return;
		}

		// 2. Un orogene expose son SOCLE : soulevement et decapage emportent la
		//    couverture sedimentaire. La convergence dit la chaine en formation,
		//    l'altitude les reliefs anciens deja decapes.
		const bool bSocle =
			(bHasConv && Convergence[I] > Rules.SocleConvergence)
			|| (ElevationM[I] > Rules.SocleElevationM);
		if (bSocle)
		{
			Out.Id[I] = IdSocle;
			return;
		}

		// 3. Le reste est un bassin : sa roche suit le motif.
		if (Rules.IdsSedimentaires.Num() == 0)
		{
			Out.Id[I] = IdSocle;
			return;
		}

		int32 K = 0;
		while (K < Seuils.Num() && Motif[I] > Seuils[K]) { ++K; }
		Out.Id[I] = static_cast<uint8>(
			Rules.IdsSedimentaires[FMath::Min(K, Rules.IdsSedimentaires.Num() - 1)]);
	});

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] lithologie : %d roches  (%.0f ms)"),
		Rules.Catalogue.Num(), (FPlatformTime::Seconds() - StartTime) * 1000.0);
}
