// Worldseed - le bulletin METEO : notre ciel ressemble-t-il a celui de la Terre ?
//
// POURQUOI IL MANQUAIT. `ProbeTerre` confronte deja vingt-trois climats de
// villes reelles a notre diagramme de Whittaker -- mais il ne juge que la CASE
// ou chacun tombe. Or les memes releves portent, saison par saison, le
// POURCENTAGE DE CIEL COUVERT, la pluie et la neige : la matiere premiere d'un
// controle de meteo, que personne n'avait jamais confrontee a notre conversion.
//
// CE QU'IL JUGE, ET CE QU'IL NE JUGE PAS. Il juge la TRADUCTION -- des
// millimetres vers un prereglage UDS -- et non le monde : les climats d'entree
// sont ceux de la Terre, pas les notres. Un ecart ici se corrige dans la
// section `uds` de world_rules.json, jamais dans le climat.
//
// ON APPELLE LA MEME FONCTION QUE LE JEU. `WorldseedClimatePreset::Build` est
// ce que le pilote du ciel emploie a chaque demi-seconde. Recopier sa formule
// ici validerait une COPIE -- exactement le defaut que le portage de `terre.py`
// avait corrige, sa version Python reimplementant le diagramme de Whittaker.

#include "Procedural/WorldseedProbeLibrary.h"

#include "Procedural/WorldseedClimate.h"
#include "Procedural/WorldseedClimatePreset.h"
#include "Procedural/WorldseedClimatsReels.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedRules.h"

namespace
{
	const TCHAR* const NomSaisonMeteo[4] =
		{ TEXT("hiver"), TEXT("printemps"), TEXT("ete"), TEXT("automne") };

}

FString UWorldseedProbeLibrary::ProbeMeteo()
{
	TArray<FString> L;
	L.Add(TEXT("=== BULLETIN METEO : la traduction du climat vers UDS ==="));

	FString Erreur;
	const UWorldseedRules* const Regles = WorldseedPipeline::GetRules(Erreur);
	if (!Regles)
	{
		L.Add(FString::Printf(TEXT("regles illisibles : %s"), *Erreur));
		return FString::Join(L, TEXT("\n"));
	}

	TArray<FWorldseedReleveReel> Releves;
	if (!WorldseedClimatsReels::Charger(Releves, Erreur))
	{
		L.Add(Erreur);
		return FString::Join(L, TEXT("\n"));
	}

	const FWorldseedClimatePresetRules ReglesPreset =
		FWorldseedClimatePresetRules::FromRules(*Regles);

	L.Add(FString::Printf(
		TEXT("%d releves -- cloudyFloorPct %.0f  cloudySpanPct %.0f  cloudyPrecipScaleMm %.0f"),
		Releves.Num(), ReglesPreset.CloudyFloorPct, ReglesPreset.CloudySpanPct,
		ReglesPreset.CloudyPrecipScaleMm));
	L.Add(TEXT("La latitude vient de la table des climats, pas du releve : voir"));
	L.Add(TEXT("FAttendu::LatDeg. Ce sont des centres de plage de Koppen, donc une"));
	L.Add(TEXT("approximation -- mais elle arme l'ITCZ et le regime mediterraneen,"));
	L.Add(TEXT("qu'une latitude deduite de la temperature laissait muets."));
	L.Add(TEXT(""));
	L.Add(TEXT("climat                              saison      mm reel  mm calc   couvert reel  calc   ecart"));
	L.Add(TEXT("----------------------------------------------------------------------------------------------"));

	double SommeEcartCouvert = 0.0;
	double SommeEcartMm = 0.0;
	int32 N = 0;
	float PireCouvert = 0.0f;
	FString PireNom;

	// Le pire ecart par climat, pour ne pas noyer le lecteur sous 92 lignes.
	for (const FWorldseedReleveReel& R : Releves)
	{
		FWorldseedClimateSample Sample;
		Sample.TempMeanC = R.TmoyC;
		Sample.PrecipMm = R.PluieMm;
		Sample.SeasonalAmpC = R.AmplitudeC;
		// LA CONTINENTALITE SE LIT SUR L'AMPLITUDE, faute de mieux : c'est elle
		// que la continentalite ouvre dans notre modele, donc l'inverser est la
		// deduction la moins arbitraire disponible. Trente degres d'ecart
		// saisonnier est un continental franc.
		Sample.Continentality = FMath::Clamp(R.AmplitudeC / 30.0f, 0.0f, 1.0f);
		Sample.LatitudeDeg = R.LatitudeDeg;

		const FWorldseedClimatePreset P =
			WorldseedClimatePreset::Build(Sample, ReglesPreset);

		int32 PireSaison = 0;
		float PireEcart = 0.0f;
		for (int32 S = 0; S < 4; ++S)
		{
			const float MmReel = R.PluieSaisonMm[S] + R.NeigeSaisonMm[S];
			const float MmCalc = P.RainfallMm[S] + P.SnowfallMm[S];
			const float Ecart = P.CloudyPct[S] - R.CouvertPct[S];

			SommeEcartCouvert += FMath::Abs(Ecart);
			SommeEcartMm += FMath::Abs(MmCalc - MmReel);
			++N;

			if (FMath::Abs(Ecart) > FMath::Abs(PireEcart))
			{
				PireEcart = Ecart;
				PireSaison = S;
			}
			if (FMath::Abs(Ecart) > FMath::Abs(PireCouvert))
			{
				PireCouvert = Ecart;
				PireNom = FString::Printf(TEXT("%s %s"), *R.Cle, NomSaisonMeteo[S]);
			}
		}

		const int32 S = PireSaison;
		L.Add(FString::Printf(
			TEXT("%-34s  %-9s %8.1f %8.1f   %10.0f %6.1f  %+6.1f"),
			*R.Cle, NomSaisonMeteo[S],
			R.PluieSaisonMm[S] + R.NeigeSaisonMm[S],
			P.RainfallMm[S] + P.SnowfallMm[S],
			R.CouvertPct[S], P.CloudyPct[S], PireEcart));
	}

	L.Add(TEXT("----------------------------------------------------------------------------------------------"));
	L.Add(FString::Printf(
		TEXT("ecart absolu moyen sur %d saisons-climats : couvert %.1f points, pluie %.1f mm/mois"),
		N, SommeEcartCouvert / FMath::Max(1, N), SommeEcartMm / FMath::Max(1, N)));

	// --- LA FRACTION ESTIVALE : LA PREDIT-ON SEULEMENT ? ---------------------
	//
	// POURQUOI CE CONTROLE EXISTE. Une mesure hors moteur a montre que
	// repartir la pluie d'apres la part tombant au semestre chaud ramenerait
	// l'ecart de 25,7 a 15,0 mm par mois. Mais elle employait la fraction
	// estivale REELLE des releves -- c'est donc un PLAFOND, atteignable
	// seulement si notre modele sait predire cette fraction. Le verifier avant
	// de coder evite de viser une cible hors de portee.
	{
		FWorldseedGeometry Geo;
		Geo.LatSpanDeg = 180.0f;

		double SommeEcartFe = 0.0;
		double SommeFeNotre = 0.0;
		double SommeFeReelle = 0.0;
		for (const FWorldseedReleveReel& R : Releves)
		{
			const float Notre = WorldseedClimate::SummerRainFraction(
				*Regles, Geo, R.LatitudeDeg);
			SommeEcartFe += FMath::Abs(Notre - R.FractionEte);
			SommeFeNotre += Notre;
			SommeFeReelle += R.FractionEte;
		}
		const int32 M = FMath::Max(1, Releves.Num());
		L.Add(FString::Printf(
			TEXT("fraction estivale : la notre %.3f en moyenne, le reel %.3f, ecart moyen %.3f"),
			SommeFeNotre / M, SommeFeReelle / M, SommeEcartFe / M));
		L.Add(TEXT("  (0,5 = pluie egale entre semestres ; 1 = tout au semestre chaud)"));

		// LE DETAIL PAR CLIMAT, TRIE PAR LATITUDE. Une moyenne juste et un
		// ecart individuel de 0,185 veulent dire que les erreurs se compensent ;
		// c'est en les rangeant par latitude qu'on voit si elles suivent une
		// structure -- un biais par ceinture -- ou si elles sont du bruit.
		L.Add(TEXT(""));
		L.Add(TEXT("  climat                            lat    reel   notre   ecart"));
		TArray<const FWorldseedReleveReel*> Tri;
		for (const FWorldseedReleveReel& R : Releves) { Tri.Add(&R); }
		Tri.Sort([](const FWorldseedReleveReel& A, const FWorldseedReleveReel& B)
		{
			return A.LatitudeDeg < B.LatitudeDeg;
		});
		for (const FWorldseedReleveReel* R : Tri)
		{
			const float Notre = WorldseedClimate::SummerRainFraction(
				*Regles, Geo, R->LatitudeDeg);
			L.Add(FString::Printf(TEXT("  %-32s %5.1f  %6.3f  %6.3f  %+6.3f"),
				*R->Cle, R->LatitudeDeg, R->FractionEte, Notre, Notre - R->FractionEte));
		}
	}
	L.Add(FString::Printf(TEXT("pire ecart de couverture : %+.1f points  (%s)"),
		PireCouvert, *PireNom));
	L.Add(TEXT(""));
	L.Add(TEXT("COMMENT LIRE CE BULLETIN. Un ecart POSITIF veut dire que notre ciel"));
	L.Add(TEXT("est plus couvert que le releve, un NEGATIF plus degage. Un biais de"));
	L.Add(TEXT("meme signe partout se corrige par cloudyFloorPct ; un ecart qui"));
	L.Add(TEXT("CROIT avec la pluie, par cloudySpanPct ; un ecart qui ne depend que"));
	L.Add(TEXT("des saisons SECHES demande autre chose -- la couverture y tient a"));
	L.Add(TEXT("l'humidite de l'annee, que la formule ne connait pas."));

	return FString::Join(L, TEXT("\n"));
}
