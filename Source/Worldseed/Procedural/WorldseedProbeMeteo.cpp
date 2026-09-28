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

		// LA MEME FRACTION QUE LE JEU TRANSMET, calculee par la meme fonction :
		// le bulletin doit juger la chaine telle qu'elle tourne, et non une
		// variante qui lui ressemble.
		{
			FWorldseedGeometry G;
			G.LatSpanDeg = 180.0f;
			Sample.SummerRainFrac = WorldseedClimate::SummerRainFraction(
				*Regles, G, R.LatitudeDeg);
		}

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

	// --- LE MEILLEUR EXPOSANT, CHERCHE ICI ET NON EN EDITANT LES REGLES ------
	//
	// Ce depot a une regle contre les A/B qui rouvrent `world_rules.json` : son
	// empreinte change, donc le monde se regenere entre les deux moities et
	// l'on ne compare plus la meme chose. Une session y a meme VIDE le fichier,
	// une restauration posee en fin de commande n'ayant pas survecu a un
	// depassement de delai. La sonde balaie donc elle-meme.
	{
		L.Add(TEXT(""));
		L.Add(TEXT("balayage du mordant (uds.saisonExposant) -- ecart de pluie :"));
		float Meilleur = -1.0f;
		float MeilleurEcart = TNumericLimits<float>::Max();
		FString Ligne;
		for (float K = 1.0f; K <= 3.61f; K += 0.3f)
		{
			FWorldseedClimatePresetRules Essai = ReglesPreset;
			Essai.SeasonContrastExponent = K;

			double Somme = 0.0;
			int32 Compte = 0;
			for (const FWorldseedReleveReel& R : Releves)
			{
				FWorldseedClimateSample S;
				S.TempMeanC = R.TmoyC;
				S.PrecipMm = R.PluieMm;
				S.SeasonalAmpC = R.AmplitudeC;
				S.Continentality = FMath::Clamp(R.AmplitudeC / 30.0f, 0.0f, 1.0f);
				S.LatitudeDeg = R.LatitudeDeg;
				FWorldseedGeometry G;
				G.LatSpanDeg = 180.0f;
				S.SummerRainFrac = WorldseedClimate::SummerRainFraction(
					*Regles, G, R.LatitudeDeg);

				const FWorldseedClimatePreset P =
					WorldseedClimatePreset::Build(S, Essai);
				for (int32 Sa = 0; Sa < 4; ++Sa)
				{
					Somme += FMath::Abs((P.RainfallMm[Sa] + P.SnowfallMm[Sa])
						- (R.PluieSaisonMm[Sa] + R.NeigeSaisonMm[Sa]));
					++Compte;
				}
			}
			const float E = static_cast<float>(Somme / FMath::Max(1, Compte));
			Ligne += FString::Printf(TEXT("  %.1f->%.1f"), K, E);
			if (E < MeilleurEcart) { MeilleurEcart = E; Meilleur = K; }
		}
		L.Add(Ligne);
		L.Add(FString::Printf(
			TEXT("  meilleur : %.1f  (%.1f mm/mois) -- en vigueur : %.1f"),
			Meilleur, MeilleurEcart, ReglesPreset.SeasonContrastExponent));
	}

	// --- ET CE QUE COUTERAIT DE RECALIBRER LA COURBE DE COUVERTURE -----------
	//
	// L'ARBITRAGE N'EST PAS UNE MOYENNE, ET C'EST TOUT L'OBJET DE CE BLOC. Une
	// echelle de pluie plus longue ameliore l'ecart MOYEN, et le paie sur les
	// climats SECS -- dont le ciel se couvre alors qu'il devrait rester
	// degage. Or le desert est justement la ou notre formule tombe juste
	// aujourd'hui, et ou un ciel gris se remarque le plus. On rend donc les
	// deux chiffres : la moyenne, et ce que les climats arides encaissent.
	{
		L.Add(TEXT(""));
		L.Add(TEXT("balayage de l'echelle de pluie (uds.cloudyPrecipScaleMm) :"));
		L.Add(TEXT("   echelle   ecart moyen   ecart sur les climats ARIDES (< 300 mm/an)"));
		for (float Echelle = 60.0f; Echelle <= 181.0f; Echelle += 20.0f)
		{
			FWorldseedClimatePresetRules Essai = ReglesPreset;
			Essai.CloudyPrecipScaleMm = Echelle;

			double Tout = 0.0, Arides = 0.0;
			int32 NTout = 0, NArides = 0;
			for (const FWorldseedReleveReel& R : Releves)
			{
				FWorldseedClimateSample S;
				S.TempMeanC = R.TmoyC;
				S.PrecipMm = R.PluieMm;
				S.SeasonalAmpC = R.AmplitudeC;
				S.Continentality = FMath::Clamp(R.AmplitudeC / 30.0f, 0.0f, 1.0f);
				S.LatitudeDeg = R.LatitudeDeg;
				FWorldseedGeometry G;
				G.LatSpanDeg = 180.0f;
				S.SummerRainFrac = WorldseedClimate::SummerRainFraction(
					*Regles, G, R.LatitudeDeg);

				const FWorldseedClimatePreset P = WorldseedClimatePreset::Build(S, Essai);
				for (int32 Sa = 0; Sa < 4; ++Sa)
				{
					const double E = FMath::Abs(P.CloudyPct[Sa] - R.CouvertPct[Sa]);
					Tout += E; ++NTout;
					if (R.PluieMm < 300.0f) { Arides += E; ++NArides; }
				}
			}
			L.Add(FString::Printf(TEXT("   %6.0f   %9.1f   %14.1f%s"),
				Echelle, Tout / FMath::Max(1, NTout), Arides / FMath::Max(1, NArides),
				FMath::IsNearlyEqual(Echelle, ReglesPreset.CloudyPrecipScaleMm, 0.5f)
					? TEXT("   <- en vigueur") : TEXT("")));
		}
	}

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

FString UWorldseedProbeLibrary::ProbeKoppen(int32 Seed, float HeightMeters,
	int32 ResolutionY)
{
	WorldseedPipeline::ReloadRules();

	WorldseedPipeline::FResult Monde;
	FString Erreur;
	if (!WorldseedPipeline::Generate(Seed, HeightMeters, ResolutionY, Monde, Erreur))
	{
		return FString::Printf(TEXT("generation impossible : %s"), *Erreur);
	}
	const UWorldseedRules* const Regles = WorldseedPipeline::GetRules(Erreur);
	if (!Regles) { return FString::Printf(TEXT("regles illisibles : %s"), *Erreur); }
	if (!Monde.bHasClimate) { return TEXT("le climat n'a pas tourne"); }

	const FWorldseedClimatePresetRules ReglesPreset =
		FWorldseedClimatePresetRules::FromRules(*Regles);

	TArray<FWorldseedReleveReel> Releves;
	WorldseedClimatsReels::Charger(Releves, Erreur);

	const FWorldseedGeometry& Geo = Monde.Geometry;
	const int32 Total = Geo.CellCount();

	TArray<int32> Compte;
	Compte.SetNumZeroed(static_cast<int32>(EWorldseedKoppen::Count));
	int32 Terres = 0;

	for (int32 I = 0; I < Total; ++I)
	{
		// LES TERRES SEULEMENT : la mer a bien un climat -- c'est meme lui que
		// le joueur subit en bateau -- mais la comparaison qui suit porte sur
		// des parts de TERRES emergees, seules valeurs terrestres publiees.
		if (Monde.ElevationM[I] <= 0.0f) { continue; }
		++Terres;

		const int32 Row = I / Geo.NX;
		const float Lat = Geo.LatitudeDegForRow(Row);

		const FWorldseedClimateResult& Cl = Monde.Climate;
		FWorldseedKoppenEntree E = WorldseedKoppen::DepuisChamps(
			Cl.TempMeanC[I], Cl.PrecipMm[I],
			Cl.SeasonalAmpC.IsValidIndex(I) ? Cl.SeasonalAmpC[I] : 12.0f,
			WorldseedClimate::SummerRainFraction(*Regles, Geo, Lat),
			ReglesPreset.SeasonContrastExponent);

		// LES EXTREMES DU MONDE PLUTOT QUE LEUR RECONSTRUCTION -- ET C'EST LA
		// MEME CHOSE, verifie dans `WorldseedClimate` : la chaine les pose a
		// `TempMeanC -/+ Amp/2`, exactement ce que `DepuisChamps` recalcule, et
		// la prime d'aridite s'ajoute aux trois de facon identique. On les lit
		// donc ici par exactitude, pas par gain.
		//
		// CE QUI VEUT DIRE QUE L'ECART ENTRE LES 4,3 POINTS ATTENDUS ET LES 7,5
		// MESURES NE VIENT PAS DE LA : il vient de la reconstruction de la PLUIE
		// saisonniere, l'estimation etant partie des pluies reelles des releves
		// quand la chaine les tire d'un cumul annuel et d'une fraction estivale.
		if (Cl.TempMinC.IsValidIndex(I) && Cl.TempMaxC.IsValidIndex(I))
		{
			E.TFroidC = Cl.TempMinC[I];
			E.TChaudC = Cl.TempMaxC[I];
		}

		const EWorldseedKoppen K = WorldseedKoppen::Classer(
			E, ReglesPreset.KoppenPointeMensuelleC);
		++Compte[static_cast<int32>(K)];
	}

	// LA REFERENCE TERRESTRE, en part des terres emergees. Valeurs usuelles de
	// la litterature, arrondies : elles servent d'ORDRE DE GRANDEUR, pas de
	// cible -- nos continents ne sont pas ceux de la Terre, et le depot a deja
	// une regle contre les scores qui jugent au lieu de comparer.
	struct FRef { EWorldseedKoppen K; float Pct; };
	static const FRef Terre[] = {
		{ EWorldseedKoppen::Af,  6.0f }, { EWorldseedKoppen::Am,  4.0f },
		{ EWorldseedKoppen::Aw, 11.0f }, { EWorldseedKoppen::As,  1.0f },
		{ EWorldseedKoppen::BWh, 9.0f }, { EWorldseedKoppen::BWk, 5.0f },
		{ EWorldseedKoppen::BSh, 6.0f }, { EWorldseedKoppen::BSk, 8.0f },
		{ EWorldseedKoppen::Csa, 1.5f }, { EWorldseedKoppen::Csb, 0.8f },
		{ EWorldseedKoppen::Csc, 0.1f }, { EWorldseedKoppen::Cwa, 3.0f },
		{ EWorldseedKoppen::Cwb, 1.5f }, { EWorldseedKoppen::Cfa, 6.0f },
		{ EWorldseedKoppen::Cfb, 4.0f }, { EWorldseedKoppen::Cfc, 0.3f },
		{ EWorldseedKoppen::Dfa, 2.0f }, { EWorldseedKoppen::Dfb, 5.0f },
		{ EWorldseedKoppen::Dfc, 9.0f }, { EWorldseedKoppen::Dfd, 1.0f },
		{ EWorldseedKoppen::ET,  8.0f }, { EWorldseedKoppen::EF,  8.0f },
	};

	TArray<FString> L;
	L.Add(FString::Printf(
		TEXT("=== CLASSES DE KOPPEN SUR LES TERRES -- graine %d, %.0f km, %d lignes ==="),
		Seed, HeightMeters / 1000.0f, ResolutionY));
	L.Add(FString::Printf(TEXT("%d cellules emergees sur %d"), Terres, Total));
	L.Add(TEXT(""));
	L.Add(TEXT("classe   part des terres   Terre   ecart   releve"));
	L.Add(TEXT("---------------------------------------------------------"));

	const float Inv = (Terres > 0) ? 100.0f / Terres : 0.0f;
	double SommeEcart = 0.0;
	int32 SansReleve = 0;
	int32 Jamais = 0;

	for (const FRef& R : Terre)
	{
		const float Part = Compte[static_cast<int32>(R.K)] * Inv;
		const bool bReleve = Releves.ContainsByPredicate(
			[&R](const FWorldseedReleveReel& X) { return X.Koppen == R.K; });
		if (!bReleve && Compte[static_cast<int32>(R.K)] > 0)
		{
			SansReleve += Compte[static_cast<int32>(R.K)];
		}
		if (Compte[static_cast<int32>(R.K)] == 0) { ++Jamais; }
		SommeEcart += FMath::Abs(Part - R.Pct);

		L.Add(FString::Printf(TEXT("%-7s %13.2f %%  %5.1f %%  %+6.1f   %s"),
			WorldseedKoppen::Nom(R.K), Part, R.Pct, Part - R.Pct,
			bReleve ? TEXT("oui") : TEXT("AUCUN")));
	}

	L.Add(TEXT("---------------------------------------------------------"));
	L.Add(FString::Printf(
		TEXT("ecart absolu moyen a la Terre : %.2f points sur 22 classes"),
		SommeEcart / 22.0));
	L.Add(FString::Printf(
		TEXT("classes JAMAIS atteintes : %d sur 22 -- leur releve ne sert jamais"),
		Jamais));

	// LE CHIFFRE QUI DECIDE SI LE CLASSEMENT EST UTILISABLE : une cellule dont
	// la classe n'a pas de releve retomberait sur la courbe, et le melange des
	// deux voies se verrait comme une couture climatique.
	L.Add(FString::Printf(
		TEXT("cellules sans releve pour leur classe : %.2f %% des terres"),
		SansReleve * Inv));
	return FString::Join(L, TEXT("\n"));
}
