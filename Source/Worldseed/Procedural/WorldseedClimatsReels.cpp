// Worldseed - lecture des vingt-trois releves de stations reelles.

#include "Procedural/WorldseedClimatsReels.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	const TCHAR* const Saisons[4] = { TEXT("Winter"), TEXT("Spring"),
		TEXT("Summer"), TEXT("Autumn") };

	double Champ(const TSharedPtr<FJsonObject>& O, const FString& Cle)
	{
		double V = 0.0;
		O->TryGetNumberField(Cle, V);
		return V;
	}

	/**
	 * Temperature moyenne annuelle et cumul de precipitations d'un releve.
	 *
	 * TROIS PIEGES DE LECTURE, tous payes en lisant les prereglages plutot
	 * qu'en raisonnant de tete. "Rainfall (mm)" est un cumul MENSUEL, pas
	 * saisonnier -- on multiplie donc par trois. "Snowfall (mm)" est un
	 * EQUIVALENT-EAU, donc il s'ajoute a la pluie. Et les temperatures sont des
	 * MOYENNES haute et basse, pas des extremes : leur demi-somme est la
	 * moyenne de la saison.
	 */
	void ClimatAnnuel(const TSharedPtr<FJsonObject>& P, double& OutT,
		double& OutMm, double& OutTMax)
	{
		double SommeT = 0.0;
		double SommeMm = 0.0;
		OutTMax = -1e9;

		for (const TCHAR* S : Saisons)
		{
			const double Haut = Champ(P, FString(S) + TEXT(" Average High Temp (C)"));
			const double Bas = Champ(P, FString(S) + TEXT(" Average Low Temp (C)"));
			const double Moy = 0.5 * (Haut + Bas);

			SommeT += Moy;
			OutTMax = FMath::Max(OutTMax, Moy);

			SommeMm += 3.0 * (Champ(P, FString(S) + TEXT(" Rainfall (mm)"))
				+ Champ(P, FString(S) + TEXT(" Snowfall (mm)")));
		}

		OutT = SommeT / 4.0;
		OutMm = SommeMm;
	}

	/** Part des precipitations tombant au semestre chaud. */
	double FractionEte(const TSharedPtr<FJsonObject>& P)
	{
		auto Cumul = [&P](const TCHAR* S)
		{
			return Champ(P, FString(S) + TEXT(" Rainfall (mm)"))
				+ Champ(P, FString(S) + TEXT(" Snowfall (mm)"));
		};
		// L'ETE PESE PLEIN, LES SAISONS MOYENNES A MOITIE. Prendre simplement
		// printemps + ete donne un semestre franc, mais ce n'est pas ce que
		// mesure le modele : les equinoxes sont a cheval, et les compter entiers
		// d'un cote decale toute la comparaison.
		const double Chaud = Cumul(TEXT("Summer"))
			+ 0.5 * (Cumul(TEXT("Spring")) + Cumul(TEXT("Autumn")));
		const double Tout = Cumul(TEXT("Winter")) + Cumul(TEXT("Spring"))
			+ Cumul(TEXT("Summer")) + Cumul(TEXT("Autumn"));
		return (Tout > 0.0) ? Chaud / Tout : 0.5;
	}

	/**
	 * Ce que la Terre repond pour chaque releve, EN ENUMERATION.
	 *
	 * PAS EN CHAINE D'AFFICHAGE, et le premier jet l'a paye : comparer
	 * "Foret temperee mixte" au libelle rend "Foret tempEREe mixte" different
	 * de lui-meme des qu'un accent s'en mele. Quatre releves sur vingt-trois
	 * passaient -- exactement ceux dont le nom n'a pas d'accent. Un libelle est
	 * fait pour etre LU, pas pour servir de cle ; le depot a deja la meme regle
	 * pour les identifiants de biome et de roche.
	 */
	struct FAttendu
	{
		const TCHAR* Cle;
		EWorldseedBiome A;
		EWorldseedBiome B;   // seconde case acceptable, ou la meme
	};

	const FAttendu Attendus[] = {
		{ TEXT("Polar_Ice_Cap"), EWorldseedBiome::Tundra, EWorldseedBiome::ColdDesert },
		{ TEXT("Polar_Tundra"), EWorldseedBiome::Tundra, EWorldseedBiome::Tundra },
		{ TEXT("Subarctic"), EWorldseedBiome::Taiga, EWorldseedBiome::Taiga },
		{ TEXT("Subarctic-Severe_Winter"), EWorldseedBiome::Taiga, EWorldseedBiome::Taiga },
		{ TEXT("Subpolar_Oceanic"), EWorldseedBiome::Taiga, EWorldseedBiome::TemperateForest },
		{ TEXT("Oceanic"), EWorldseedBiome::TemperateForest, EWorldseedBiome::TemperateForest },
		{ TEXT("Humid_Subtropical"), EWorldseedBiome::SubtropicalForest, EWorldseedBiome::SubtropicalForest },
		{ TEXT("Humid_Subtropical-Dry_Winter"), EWorldseedBiome::SubtropicalForest, EWorldseedBiome::SubtropicalForest },
		{ TEXT("Hot_Summer_Continental"), EWorldseedBiome::TemperateForest, EWorldseedBiome::TemperateForest },
		{ TEXT("Warm_Summer_Continental"), EWorldseedBiome::TemperateForest, EWorldseedBiome::TemperateForest },
		{ TEXT("Mediterranean_Hot_Summer"), EWorldseedBiome::Mediterranean, EWorldseedBiome::Mediterranean },
		{ TEXT("Mediterranean_Cool_Summer"), EWorldseedBiome::Mediterranean, EWorldseedBiome::Mediterranean },
		{ TEXT("Mediterranean_Cold_Summer"), EWorldseedBiome::Mediterranean, EWorldseedBiome::Mediterranean },
		{ TEXT("Hot_Desert"), EWorldseedBiome::HotDesert, EWorldseedBiome::HotDesert },
		{ TEXT("Cold_Desert"), EWorldseedBiome::ColdDesert, EWorldseedBiome::ColdDesert },
		{ TEXT("Hot_Semi-Arid"), EWorldseedBiome::HotDesert, EWorldseedBiome::Savanna },
		{ TEXT("Cold_Semi-Arid"), EWorldseedBiome::Steppe, EWorldseedBiome::ColdDesert },
		{ TEXT("Tropical_Rainforest"), EWorldseedBiome::TropicalRainforest, EWorldseedBiome::TropicalRainforest },
		{ TEXT("Tropical_Monsoon"), EWorldseedBiome::TropicalRainforest, EWorldseedBiome::TropicalRainforest },
		{ TEXT("Tropical_Savanna-Dry_Winter"), EWorldseedBiome::Savanna, EWorldseedBiome::Savanna },
		{ TEXT("Tropical_Savanna-Dry_Summer"), EWorldseedBiome::Savanna, EWorldseedBiome::Savanna },
		{ TEXT("Subtropical_Highland"), EWorldseedBiome::TemperateForest, EWorldseedBiome::SubtropicalForest },
		{ TEXT("Subtropical_Highland-Dry_Winter"), EWorldseedBiome::TemperateForest, EWorldseedBiome::SubtropicalForest },
	};
}

namespace WorldseedClimatsReels
{
	const float SeuilEteReleves = 0.31f;

	FString Chemin()
	{
		// LA DONNEE VIT DANS `rules/`, PAS DANS UN DOSSIER DE SORTIE. Elle a
		// longtemps habite `Saved/WorldGen/<date>/`, ou un menage l'aurait
		// emportee : une reference exterieure au projet n'a rien a faire dans
		// ce qu'on regenere.
		return FPaths::Combine(FPaths::ProjectDir(),
			TEXT("Tools/WorldGen/rules/climats_reels.json"));
	}

	bool Charger(TArray<FWorldseedReleveReel>& Out, FString& OutErreur)
	{
		Out.Reset();

		FString Texte;
		if (!FFileHelper::LoadFileToString(Texte, *Chemin()))
		{
			OutErreur = FString::Printf(
				TEXT("releves reels introuvables (%s)"), *Chemin());
			return false;
		}

		TSharedPtr<FJsonObject> Racine;
		const TSharedRef<TJsonReader<>> Lecteur = TJsonReaderFactory<>::Create(Texte);
		if (!FJsonSerializer::Deserialize(Lecteur, Racine) || !Racine.IsValid())
		{
			OutErreur = FString::Printf(
				TEXT("releves reels illisibles (%s)"), *Chemin());
			return false;
		}

		for (const FAttendu& A : Attendus)
		{
			const TSharedPtr<FJsonObject>* Preset = nullptr;
			if (!Racine->TryGetObjectField(A.Cle, Preset) || !Preset) { continue; }

			double T = 0.0, Mm = 0.0, TMax = 0.0;
			ClimatAnnuel(*Preset, T, Mm, TMax);

			FWorldseedReleveReel R;
			R.Cle = A.Cle;
			R.TmoyC = static_cast<float>(T);
			R.PluieMm = static_cast<float>(Mm);
			R.TMaxC = static_cast<float>(TMax);
			R.FractionEte = static_cast<float>(FractionEte(*Preset));
			R.AttenduA = A.A;
			R.AttenduB = A.B;
			Out.Add(MoveTemp(R));
		}

		return true;
	}

	EWorldseedBiome Classer(const FWorldseedReleveReel& Releve,
		const FWorldseedBiomeRules& Regles)
	{
		FWorldseedBiomeRules ReglesReleves = Regles;
		ReglesReleves.MediterraneanSummerFracMax = SeuilEteReleves;

		return WorldseedBiomes::FromClimate(Releve.TmoyC, Releve.PluieMm,
			Releve.TMaxC, Releve.FractionEte, true, ReglesReleves);
	}
}
