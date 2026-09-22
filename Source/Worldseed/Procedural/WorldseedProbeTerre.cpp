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
#include "Procedural/WorldseedClimatsReels.h"

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedClimate.h"
#include "Procedural/WorldseedRules.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

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

	// --- 1. LES VINGT-TROIS CLIMATS REELS -----------------------------------
	//
	// LA LECTURE DES RELEVES ET LA TABLE DES ATTENDUS VIVENT AILLEURS, dans
	// `WorldseedClimatsReels`, parce qu'un test d'automation les appelle aussi.
	// Les garder ici, dans un namespace anonyme, obligeait le test a en ecrire
	// une COPIE -- donc a valider une copie de la lecture plutot que la lecture
	// elle-meme. C'est exactement le defaut que le portage de `terre.py` avait
	// corrige sur le classificateur, et il n'y a pas de raison de le refaire un
	// cran plus haut.
	TArray<FWorldseedReleveReel> Releves;
	FString ErreurReleves;
	int32 Bons = 0;
	int32 Total = 0;

	if (WorldseedClimatsReels::Charger(Releves, ErreurReleves))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] === 1. les climats REELS dans NOTRE diagramme ==="));
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   %-34s %7s %9s   %-26s %s"),
			TEXT("climat reel"), TEXT("T an"), TEXT("pluie an"),
			TEXT("notre case"), TEXT("verdict"));

		for (const FWorldseedReleveReel& R : Releves)
		{
			const EWorldseedBiome Case =
				WorldseedClimatsReels::Classer(R, BioRegles);
			const bool bOk = R.Accepte(Case);

			++Total;
			Bons += bOk ? 1 : 0;

			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]   %-34s %6.1f C %7.0f mm   %-26s %s"),
				*R.Cle, R.TmoyC, R.PluieMm, WorldseedBiomes::Name(Case),
				bOk ? TEXT("OK") : *FString::Printf(TEXT("!! attendu %s"),
					WorldseedBiomes::Name(R.AttenduA)));
		}

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   -> %d sur %d climats reels tombent dans la case attendue"),
			Bons, Total);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Worldseed] terre : %s"), *ErreurReleves);
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
