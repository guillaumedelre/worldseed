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

#include "Procedural/WorldseedClimatePreset.h"
#include "Procedural/WorldseedClimatsReels.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedRules.h"

namespace
{
	const TCHAR* const NomSaisonMeteo[4] =
		{ TEXT("hiver"), TEXT("printemps"), TEXT("ete"), TEXT("automne") };

	/**
	 * La latitude d'un releve, qu'AUCUN fichier ne donne.
	 *
	 * ET C'EST UNE LIMITE, PAS UN DETAIL. `Build` module la pluie par l'ITCZ
	 * entre les tropiques et par le regime mediterraneen entre 30 et 45 degres ;
	 * sans latitude, ces deux termes ne peuvent pas etre reproduits. On la
	 * DEDUIT donc de la temperature moyenne -- la seule grandeur du releve qui
	 * varie avec elle -- par une interpolation grossiere : 0 degre de latitude
	 * vers 27 C, 90 degres vers -25 C.
	 *
	 * CE QUE CELA COUTE : les lignes des climats tropicaux et mediterraneens
	 * portent une incertitude que les autres n'ont pas, et l'en-tete du releve
	 * le dit. Ajouter la latitude a `climats_reels.json` la leverait -- c'est
	 * une donnee de reference, elle a sa place dans le fichier de reference.
	 */
	float LatitudePlausibleDeg(float TmoyC)
	{
		const float T = FMath::Clamp(TmoyC, -25.0f, 27.0f);
		return 90.0f * (27.0f - T) / 52.0f;
	}
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
	L.Add(TEXT("LIMITE : la latitude est DEDUITE de la temperature, les releves"));
	L.Add(TEXT("ne la portant pas. Les climats tropicaux et mediterraneens, ou"));
	L.Add(TEXT("l'ITCZ et la modulation saisonniere mordent, sont donc moins surs."));
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
		Sample.LatitudeDeg = LatitudePlausibleDeg(R.TmoyC);

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
