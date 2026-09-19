// Worldseed - le bulletin de conformite terrestre, porte du Python vers le C++.
//
// POURQUOI IL EXISTE. Un monde procedural peut etre coherent avec lui-meme et
// faux par rapport a la Terre : rien, dans la chaine, ne l'empeche de produire
// des deserts a l'equateur ou de la taiga sous les tropiques. Ce bulletin est
// le seul controle qui confronte le monde a des valeurs EXTERIEURES au projet.
//
// DEUX CONTROLES, ET ILS NE DISENT PAS LA MEME CHOSE :
//
//  1. Vingt-trois climats de VILLES REELLES passes dans NOTRE diagramme. Ils
//     viennent des prereglages d'Ultra Dynamic Sky, qui sont des releves de
//     stations citant leur source. Ce controle ne juge pas le monde : il juge
//     le CLASSIFICATEUR. S'il se trompe sur Etretat ou sur Phoenix, il se
//     trompera partout.
//
//  2. Le bulletin proprement dit : des criteres SOURCES, jamais un pourcentage
//     de biome sorti de memoire. Chaque ligne est soit une constante
//     geometrique -- la part de surface d'une sphere entre deux latitudes vaut
//     sin(L) --, soit une valeur physique etablie, soit une valeur citee.
//
// CE QUE LE PORTAGE CORRIGE. La version Python REIMPLEMENTAIT le diagramme de
// Whittaker pour son controle 1. Elle validait donc une COPIE du
// classificateur, pas le classificateur -- et le depot a une regle contre
// exactement cela : ne jamais recopier une formule dans deux fichiers. Ici, on
// appelle WorldseedBiomes::FromClimate, la meme fonction que la generation.

#include "Procedural/WorldseedProbeLibrary.h"

#include "Procedural/WorldseedProbeCommun.h"

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedClimate.h"
#include "Procedural/WorldseedRules.h"

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

FString UWorldseedProbeLibrary::ProbeTerre(int32 Seed, float HeightMeters,
	int32 ResolutionY)
{
	FWorldseedSonde S;
	if (!S.Preparer(Seed, HeightMeters, ResolutionY))
	{
		return S.Erreur;
	}

	const FWorldseedBiomeRules BioRegles =
		FWorldseedBiomeRules::FromRules(*S.Regles, S.World.Geometry);

	// --- DEUX SEUILS, ET LES CONFONDRE SERAIT UNE FAUTE ----------------------
	//
	// Notre part estivale est plus CONTRASTEE que la realite -- 0,15 a 0,21
	// entre 38 et 50 degres dans le monde genere, contre 0,24 a 0,30 mesures
	// sur les villes mediterraneennes reelles -- parce que le modele de
	// circulation est purement zonal : ni moderation maritime de la
	// saisonnalite, ni asymetrie est/ouest des bassins oceaniques. Sur Terre,
	// le mediterraneen est d'ailleurs un climat de FACADE OUEST, pas une
	// ceinture.
	//
	// Le seuil du MOTEUR vaut 0,25 -- le rapport 1/3 de Koppen ramene a deux
	// semestres. Celui des RELEVES vaut 0,31, qui separe proprement les trois
	// mediterraneens (0,243 / 0,289 / 0,299) de leurs voisins immediats,
	// Oceanic a 0,427 et Humid_Subtropical a 0,413. Les unifier ferait basculer
	// l'un ou l'autre, et le depot l'interdit sans refaire la mesure.
	FWorldseedBiomeRules ReglesReleves = BioRegles;
	ReglesReleves.MediterraneanSummerFracMax = 0.31f;

	// --- 1. LES VINGT-TROIS CLIMATS REELS -----------------------------------
	const FString Chemin = FPaths::Combine(FPaths::ProjectDir(),
		TEXT("Tools/WorldGen/rules/climats_reels.json"));

	FString Texte;
	int32 Bons = 0;
	int32 Total = 0;

	if (FFileHelper::LoadFileToString(Texte, *Chemin))
	{
		TSharedPtr<FJsonObject> Racine;
		const TSharedRef<TJsonReader<>> Lecteur = TJsonReaderFactory<>::Create(Texte);
		if (FJsonSerializer::Deserialize(Lecteur, Racine) && Racine.IsValid())
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] === 1. les climats REELS dans NOTRE diagramme ==="));
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]   %-34s %7s %9s   %-26s %s"),
				TEXT("climat reel"), TEXT("T an"), TEXT("pluie an"),
				TEXT("notre case"), TEXT("verdict"));

			for (const FAttendu& A : Attendus)
			{
				const TSharedPtr<FJsonObject>* Preset = nullptr;
				if (!Racine->TryGetObjectField(A.Cle, Preset) || !Preset) { continue; }

				double T = 0.0;
				double Mm = 0.0;
				double TMax = 0.0;
				ClimatAnnuel(*Preset, T, Mm, TMax);
				const double Ete = FractionEte(*Preset);

				const EWorldseedBiome Case = WorldseedBiomes::FromClimate(
					static_cast<float>(T), static_cast<float>(Mm),
					static_cast<float>(TMax), static_cast<float>(Ete), true, ReglesReleves);

				const bool bOk = (Case == A.A || Case == A.B);

				++Total;
				Bons += bOk ? 1 : 0;

				UE_LOG(LogTemp, Log,
					TEXT("[Worldseed]   %-34s %6.1f C %7.0f mm   %-26s %s"),
					A.Cle, T, Mm, WorldseedBiomes::Name(Case),
					bOk ? TEXT("OK") : *FString::Printf(TEXT("!! attendu %s"),
						WorldseedBiomes::Name(A.A)));
			}

			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]   -> %d sur %d climats reels tombent dans la case attendue"),
				Bons, Total);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] terre : releves reels introuvables (%s)"), *Chemin);
	}

	// --- 2. LE BULLETIN, SUR DES CRITERES SOURCES ---------------------------
	//
	// AUCUN POURCENTAGE DE BIOME SORTI DE MEMOIRE. Chaque cible est soit une
	// constante geometrique, soit une valeur physique etablie, soit citee.
	const int32 Count = S.World.Geometry.CellCount();
	int32 Terres = 0;
	double SommePluie = 0.0;
	int32 TresHumides = 0;
	int32 Tropicales = 0;
	int32 Polaires = 0;

	const bool bPluie = (S.World.Climate.PrecipMm.Num() == Count);
	const int32 NX = S.World.Geometry.NX;

	for (int32 I = 0; I < Count; ++I)
	{
		const float Lat = S.World.Geometry.LatitudeDegForRow(I / NX);
		if (FMath::Abs(Lat) < 23.4f) { ++Tropicales; }
		if (FMath::Abs(Lat) > 66.6f) { ++Polaires; }

		if (S.World.ElevationM[I] <= 0.0f) { continue; }
		++Terres;
		if (bPluie)
		{
			SommePluie += S.World.Climate.PrecipMm[I];
			if (S.World.Climate.PrecipMm[I] > 2000.0f) { ++TresHumides; }
		}
	}

	struct FLigne { const TCHAR* Nom; double Monde; double Terre; const TCHAR* Unite; };
	const double PartTerres = 100.0 * Terres / FMath::Max(Count, 1);

	TArray<FLigne> Lignes;
	Lignes.Add({ TEXT("Terres emergees, part du monde"), PartTerres, 29.2, TEXT("%") });
	Lignes.Add({ TEXT("Pluie moyenne sur les terres"),
		(Terres > 0) ? SommePluie / Terres : 0.0, 715.0, TEXT("mm/an") });
	Lignes.Add({ TEXT("Terres au-dessus de 2000 mm"),
		100.0 * TresHumides / FMath::Max(Terres, 1), 7.5, TEXT("%") });
	Lignes.Add({ TEXT("Surface tropicale (< 23,4 deg)"),
		100.0 * Tropicales / FMath::Max(Count, 1), 39.8, TEXT("%") });
	Lignes.Add({ TEXT("Surface polaire (> 66,6 deg)"),
		100.0 * Polaires / FMath::Max(Count, 1), 8.3, TEXT("%") });

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] === 2. BULLETIN DE CONFORMITE TERRESTRE ==="));
	UE_LOG(LogTemp, Log, TEXT("[Worldseed]   %-40s %10s %10s  %s"),
		TEXT("critere"), TEXT("monde"), TEXT("Terre"), TEXT("ecart"));

	for (const FLigne& L : Lignes)
	{
		const double Ecart = (L.Terre != 0.0) ? (L.Monde - L.Terre) / L.Terre * 100.0 : 0.0;
		const TCHAR* Drapeau = (FMath::Abs(Ecart) <= 15.0) ? TEXT("OK")
			: ((FMath::Abs(Ecart) <= 40.0) ? TEXT("passable") : TEXT("ECART"));
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   %-34s (%-5s) %9.1f %10.1f %+7.0f %%  %s"),
			L.Nom, L.Unite, L.Monde, L.Terre, Ecart, Drapeau);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   Sources : part emergee et pluie moyenne, valeurs physiques ")
		TEXT("etablies ; parts de surface par zone, geometrie d'une sphere (surface ")
		TEXT("entre -L et +L = sin L)."));

	return FString::Printf(TEXT("%d/%d climats reels justes ; terres %.1f %%, pluie %.0f mm"),
		Bons, Total, PartTerres, (Terres > 0) ? SommePluie / Terres : 0.0);
}
