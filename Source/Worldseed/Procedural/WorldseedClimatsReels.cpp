// Worldseed - lecture des vingt-trois releves de stations reelles.

#include "Procedural/WorldseedClimatsReels.h"

#include "Procedural/WorldseedKoppen.h"

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

		/**
		 * Latitude typique du climat, en degres NORD.
		 *
		 * POURQUOI ELLE VIT ICI ET NON DANS LE JSON. Ce fichier est un releve
		 * de STATIONS -- ce que le thermometre et le pluviometre ont mesure --
		 * et la latitude n'en fait pas partie : les prereglages d'Ultra Dynamic
		 * Sky citent leur ville en commentaire, pas en donnee. C'est donc une
		 * connaissance que NOUS apportons sur le climat, exactement comme la
		 * case de Whittaker attendue deux colonnes plus haut, et elle a sa
		 * place dans la meme table.
		 *
		 * CE SONT DES CENTRES DE PLAGE DE KOPPEN, pas les stations exactes,
		 * qu'on n'a pas. Un mediterraneen vit entre 30 et 45 degres, un
		 * tropical humide entre 0 et 10, un polaire au-dela de 66 : ces bornes
		 * sont etablies, et le milieu de la plage est l'estimation la moins
		 * arbitraire disponible. Les villes citees quand elles sont connues --
		 * Londres 51,5, Athenes 38,0, Singapour 1,3, Iakoutsk 62,0 -- servent
		 * d'ancrage.
		 *
		 * TOUTES POSITIVES, et il le faut : les releves nomment leurs saisons
		 * « Winter », « Spring »... c'est-a-dire l'hemisphere NORD. `Build`
		 * echange les saisons sous l'equateur ; une latitude australe ferait
		 * donc comparer notre ete a leur hiver.
		 */
		float LatDeg;

		/**
		 * La classe de Koppen que le NOM du releve annonce.
		 *
		 * C'est l'ATTENDU du classificateur, au meme titre que la case de
		 * Whittaker deux colonnes plus haut : « Mediterranean_Hot_Summer » EST un
		 * Csa, « Hot_Desert » un BWh. Les noms des prereglages d'Ultra Dynamic Sky
		 * suivent la nomenclature de Koppen, ce qui donne gratuitement une verite
		 * terrain -- et c'est ce qui permet de mesurer un taux de classement au
		 * lieu de le supposer.
		 */
		EWorldseedKoppen Koppen;
	};

	const FAttendu Attendus[] = {
		{ TEXT("Polar_Ice_Cap"), EWorldseedBiome::Tundra, EWorldseedBiome::ColdDesert, 78.0f, EWorldseedKoppen::EF },
		{ TEXT("Polar_Tundra"), EWorldseedBiome::Tundra, EWorldseedBiome::Tundra, 70.0f, EWorldseedKoppen::ET },
		{ TEXT("Subarctic"), EWorldseedBiome::Taiga, EWorldseedBiome::Taiga, 60.0f, EWorldseedKoppen::Dfc },
		{ TEXT("Subarctic-Severe_Winter"), EWorldseedBiome::Taiga, EWorldseedBiome::Taiga, 62.0f, EWorldseedKoppen::Dfd },
		{ TEXT("Subpolar_Oceanic"), EWorldseedBiome::Taiga, EWorldseedBiome::TemperateForest, 64.0f, EWorldseedKoppen::Cfc },
		{ TEXT("Oceanic"), EWorldseedBiome::TemperateForest, EWorldseedBiome::TemperateForest, 51.5f, EWorldseedKoppen::Cfb },
		{ TEXT("Humid_Subtropical"), EWorldseedBiome::SubtropicalForest, EWorldseedBiome::SubtropicalForest, 33.0f, EWorldseedKoppen::Cfa },
		{ TEXT("Humid_Subtropical-Dry_Winter"), EWorldseedBiome::SubtropicalForest, EWorldseedBiome::SubtropicalForest, 28.0f, EWorldseedKoppen::Cwa },
		{ TEXT("Hot_Summer_Continental"), EWorldseedBiome::TemperateForest, EWorldseedBiome::TemperateForest, 42.0f, EWorldseedKoppen::Dfa },
		{ TEXT("Warm_Summer_Continental"), EWorldseedBiome::TemperateForest, EWorldseedBiome::TemperateForest, 46.0f, EWorldseedKoppen::Dfb },
		{ TEXT("Mediterranean_Hot_Summer"), EWorldseedBiome::Mediterranean, EWorldseedBiome::Mediterranean, 38.0f, EWorldseedKoppen::Csa },
		{ TEXT("Mediterranean_Cool_Summer"), EWorldseedBiome::Mediterranean, EWorldseedBiome::Mediterranean, 40.0f, EWorldseedKoppen::Csb },
		{ TEXT("Mediterranean_Cold_Summer"), EWorldseedBiome::Mediterranean, EWorldseedBiome::Mediterranean, 43.0f, EWorldseedKoppen::Csc },
		{ TEXT("Hot_Desert"), EWorldseedBiome::HotDesert, EWorldseedBiome::HotDesert, 25.0f, EWorldseedKoppen::BWh },
		{ TEXT("Cold_Desert"), EWorldseedBiome::ColdDesert, EWorldseedBiome::ColdDesert, 40.0f, EWorldseedKoppen::BWk },
		{ TEXT("Hot_Semi-Arid"), EWorldseedBiome::HotDesert, EWorldseedBiome::Savanna, 28.0f, EWorldseedKoppen::BSh },
		{ TEXT("Cold_Semi-Arid"), EWorldseedBiome::Steppe, EWorldseedBiome::ColdDesert, 42.0f, EWorldseedKoppen::BSk },
		{ TEXT("Tropical_Rainforest"), EWorldseedBiome::TropicalRainforest, EWorldseedBiome::TropicalRainforest, 2.0f, EWorldseedKoppen::Af },
		{ TEXT("Tropical_Monsoon"), EWorldseedBiome::TropicalRainforest, EWorldseedBiome::TropicalRainforest, 14.0f, EWorldseedKoppen::Am },
		{ TEXT("Tropical_Savanna-Dry_Winter"), EWorldseedBiome::Savanna, EWorldseedBiome::Savanna, 12.0f, EWorldseedKoppen::Aw },
		{ TEXT("Tropical_Savanna-Dry_Summer"), EWorldseedBiome::Savanna, EWorldseedBiome::Savanna, 10.0f, EWorldseedKoppen::As },
		{ TEXT("Subtropical_Highland"), EWorldseedBiome::TemperateForest, EWorldseedBiome::SubtropicalForest, 19.0f, EWorldseedKoppen::Cfb },
		{ TEXT("Subtropical_Highland-Dry_Winter"), EWorldseedBiome::TemperateForest, EWorldseedBiome::SubtropicalForest, 15.0f, EWorldseedKoppen::Cwb },
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
			R.LatitudeDeg = A.LatDeg;
			R.Koppen = A.Koppen;

			// LE DETAIL PAR SAISON, pour le bulletin METEO. Les noms suivent
			// l'ordre de `EWorldseedSeason`, et non celui du fichier, qui
			// commence lui aussi par l'hiver -- les deux coincident, mais la
			// table le dit au lieu de le supposer.
			// ON EMPLOIE LA TABLE `Saisons` DU FICHIER, et non une seconde :
			// mon premier jet en declarait une locale, que le compilateur a
			// refusee -- « masque la declaration globale ». Il avait raison
			// deux fois, la duplication etant aussi ce que le depot interdit.
			float TmoyMin = TNumericLimits<float>::Max();
			float TmoyMax = TNumericLimits<float>::Lowest();
			for (int32 S = 0; S < 4; ++S)
			{
				const FString Prefixe(Saisons[S]);
				R.CouvertPct[S] = static_cast<float>(
					Champ(*Preset, Prefixe + TEXT(" Cloudy Percentage")));
				R.PluieSaisonMm[S] = static_cast<float>(
					Champ(*Preset, Prefixe + TEXT(" Rainfall (mm)")));
				R.NeigeSaisonMm[S] = static_cast<float>(
					Champ(*Preset, Prefixe + TEXT(" Snowfall (mm)")));

				const float Moyenne = 0.5f * static_cast<float>(
					Champ(*Preset, Prefixe + TEXT(" Average High Temp (C)"))
					+ Champ(*Preset, Prefixe + TEXT(" Average Low Temp (C)")));
				TmoyMin = FMath::Min(TmoyMin, Moyenne);
				TmoyMax = FMath::Max(TmoyMax, Moyenne);
			}
			R.AmplitudeC = TmoyMax - TmoyMin;

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
