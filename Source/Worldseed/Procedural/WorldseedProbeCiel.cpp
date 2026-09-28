// Worldseed - CE QUE LE JOUEUR VERRA PASSER DANS LE CIEL, sur une annee.
//
// POURQUOI ELLE EXISTE, ET LE DEPOT A LE PRECEDENT EXACT. Le 12 septembre
// 2026, la chaine de neige etait juste de bout en bout : biome, prereglage,
// tirage, manteau blanc au sol -- et elle tombait UNE FOIS SUR QUARANTE-TROIS
// tirages, soit un episode toutes les deux heures et demie de jeu. Mecanique
// exacte, frequence inobservable. Aucune sonde ne pouvait le dire, parce
// qu'elles mesurent toutes un INSTANT ou une MOYENNE, jamais une duree vecue.
//
// CE QU'ELLE MESURE : pour une poignee de sites repartis en latitude, le temps
// qu'il fait SUR UNE ANNEE ENTIERE de jeu -- part du temps sous la pluie, sous
// la neige, ciel degage, ciel couvert, et le NOMBRE d'episodes d'orage et
// d'aurore. Le compte d'episodes compte autant que la part : deux heures de
// pluie en une fois et deux heures en douze averses ne se vivent pas pareil.
//
// ELLE N'A PAS BESOIN DU JEU, et c'est ce qui la rend rejouable en quelques
// secondes. `WorldseedWeatherState::Evaluate` est une fonction PURE de
// l'echantillon de climat, des regles et du temps : on fait donc defiler le
// temps et la saison a la main, en appelant EXACTEMENT ce que le pilote du
// ciel appelle toutes les demi-secondes. Reimplementer sa formule ici
// validerait une COPIE -- le defaut que le portage de `terre.py` avait corrige.
//
// ELLE NE JUGE PAS L'ASPECT. Qu'un rideau d'aurore soit beau, qu'un eclair
// eclaire le paysage, cela ne se voit qu'a l'image et aucune ligne de chiffres
// ne le remplacera. Elle repond a « est-ce que je vais le voir », pas a
// « est-ce que c'est joli ».

#include "Procedural/WorldseedProbeLibrary.h"

#include "Procedural/WorldseedClimate.h"
#include "Procedural/WorldseedClimatePreset.h"
#include "Procedural/WorldseedKoppen.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedWeatherState.h"

namespace
{
	/**
	 * LES SEUILS VIENNENT DES PREREGLAGES LIVRES PAR LE PACK, pas de notre
	 * jugement. Releves dans les treize types de meteo d'UDS : Rain_Light 3,
	 * Rain 7, Rain_Thunderstorm 10 ; Snow_Light 3, Snow 6, Snow_Blizzard 10.
	 * On place donc « il pleut » a la moitie de la plus legere bruine que le
	 * pack sache dessiner, et « averse » entre Rain_Light et Rain.
	 */
	constexpr float SeuilPluie = 1.5f;
	constexpr float SeuilAverse = 5.0f;
	constexpr float SeuilNeige = 1.5f;
	constexpr float SeuilOrage = 3.0f;

	/** Sur l'echelle des nuages d'UDS, ou 0,8 est un ciel pur et 8,5 un plafond. */
	constexpr float SeuilDegage = 2.0f;
	constexpr float SeuilCouvert = 6.0f;

	/** Au-dessous, le rideau ne se distingue pas du fond du ciel. */
	constexpr float SeuilAurore = 0.30f;

	/** Un compteur de duree ET d'episodes : les deux se lisent differemment. */
	struct FCompteur
	{
		int32 Pas = 0;
		int32 Episodes = 0;
		bool bDedans = false;

		void Voir(bool bActif)
		{
			if (bActif)
			{
				++Pas;
				if (!bDedans) { ++Episodes; }
			}
			bDedans = bActif;
		}
	};
}

FString UWorldseedProbeLibrary::ProbeCiel(int32 Seed, float HeightMeters,
	int32 ResolutionY)
{
	TArray<FString> L;

	FString Erreur;
	const UWorldseedRules* const Regles = WorldseedPipeline::GetRules(Erreur);
	if (Regles == nullptr)
	{
		return FString::Printf(TEXT("regles illisibles : %s"), *Erreur);
	}

	WorldseedPipeline::FResult Monde;
	if (!WorldseedPipeline::Generate(Seed, HeightMeters, ResolutionY, Monde, Erreur))
	{
		return FString::Printf(TEXT("generation impossible : %s"), *Erreur);
	}

	const FWorldseedGeometry& Geo = Monde.Geometry;
	const FWorldseedClimatePresetRules ReglesPreset =
		FWorldseedClimatePresetRules::FromRules(*Regles);

	// --- la duree d'une annee, telle que le joueur la vit --------------------
	//
	// ELLE SE LIT DANS LES REGLES, pas dans une constante : le calendrier de
	// Worldseed fait trente-six jours, et une journee plus une nuit valent
	// `dureeJourneeMin + dureeNuitMin` minutes REELLES. Ecrire « une annee »
	// sans cela ne voudrait rien dire -- c'est exactement ce qui avait laisse
	// une annee durer deux cent soixante-quatorze heures.
	const double JourMin = Regles->Num(TEXT("uds"), TEXT("dureeJourneeMin"), 30.0);
	const double NuitMin = Regles->Num(TEXT("uds"), TEXT("dureeNuitMin"), 15.0);
	constexpr double JoursParAn = 36.0;
	const double AnneeS = (JourMin + NuitMin) * 60.0 * JoursParAn;

	FWorldseedWeatherParams Params;
	Params.LatSpanDeg = Geo.LatSpanDeg;
	Params.Seed = static_cast<uint32>(Seed);

	// LE PAS DOIT RESOUDRE L'OCTAVE LA PLUS RAPIDE DU SIGNAL, pas le cycle.
	//
	// `Storminess` somme trois octaves, dont une a 6,1 fois la frequence de
	// base : elle bat donc toutes les trente secondes environ. Un premier jet
	// echantillonnait au quart du cycle -- quarante-cinq secondes -- et
	// annoncait ZERO pour cent de pluie sur des climats qui en recoivent
	// quatre : on mesurait l'echantillonnage, pas le ciel.
	const double PasS = FMath::Max(Params.VariationPeriodS / 24.0f, 1.0f);
	const int32 Pas = FMath::Clamp(FMath::RoundToInt(AnneeS / PasS), 100, 40000);

	L.Add(FString::Printf(
		TEXT("=== CE QUE LE CIEL DONNE SUR UNE ANNEE -- graine %d, %.0f km, %d lignes ==="),
		Seed, HeightMeters / 1000.0f, ResolutionY));
	L.Add(FString::Printf(
		TEXT("annee de jeu %.1f h (journee %.0f min + nuit %.0f min, %.0f jours) ; ")
		TEXT("%d pas de %.0f s"),
		AnneeS / 3600.0, JourMin, NuitMin, JoursParAn, Pas, PasS));
	L.Add(TEXT(""));
	L.Add(TEXT("site                      lat   Koppen   T an  mm/an   pluie averse  neige  degage couvert")
		TEXT("   orages   aurore   | CIBLE, sans fondu"));
	L.Add(TEXT("                                             C    mm       %     %       %       %      %")
		TEXT("    (episodes/an)     | pluie %  orages"));

	// --- les sites, repartis en latitude -------------------------------------
	//
	// ON PREND DE VRAIES CELLULES DU MONDE, jamais un climat fabrique : la
	// question posee est « que verrai-je LA », et un echantillon invente n'y
	// repondrait pas.
	//
	// ET ON PREND LA CELLULE MEDIANE EN PLUIE, PAS LA PREMIERE VENUE. Le
	// premier jet retenait le premier point emerge de la ligne : a l'equateur
	// il est tombe sur un BSh semi-aride la ou la bande porte surtout de la
	// foret tropicale, et le bulletin annoncait alors zero averse pour tout le
	// monde. Un echantillon arbitraire n'est pas une mesure -- il decrit un
	// endroit, pas une latitude.
	const float LatitudesCibles[] =
		{ 75.0f, 60.0f, 45.0f, 30.0f, 15.0f, 0.0f, -15.0f, -30.0f, -45.0f, -60.0f, -75.0f };

	for (const float LatCible : LatitudesCibles)
	{
		// La ligne dont la latitude approche le mieux la cible.
		int32 MeilleureLigne = INDEX_NONE;
		float MeilleurEcart = TNumericLimits<float>::Max();
		for (int32 J = 0; J < Geo.NY; ++J)
		{
			const float E = FMath::Abs(Geo.LatitudeDegForRow(J) - LatCible);
			if (E < MeilleurEcart) { MeilleurEcart = E; MeilleureLigne = J; }
		}
		if (MeilleureLigne == INDEX_NONE) { continue; }

		TArray<int32> Terres;
		for (int32 I = 0; I < Geo.NX; ++I)
		{
			const int32 Idx = MeilleureLigne * Geo.NX + I;
			if (Monde.ElevationM.IsValidIndex(Idx) && Monde.ElevationM[Idx] > 0.0f)
			{
				Terres.Add(Idx);
			}
		}

		int32 Cellule = INDEX_NONE;
		if (Terres.Num() > 0)
		{
			Terres.Sort([&Monde](int32 A, int32 B)
			{
				const float PA = Monde.Climate.PrecipMm.IsValidIndex(A)
					? Monde.Climate.PrecipMm[A] : 0.0f;
				const float PB = Monde.Climate.PrecipMm.IsValidIndex(B)
					? Monde.Climate.PrecipMm[B] : 0.0f;
				return PA < PB;
			});
			Cellule = Terres[Terres.Num() / 2];
		}
		if (Cellule == INDEX_NONE)
		{
			L.Add(FString::Printf(TEXT("%-24s %5.0f   -- aucune terre a cette latitude"),
				TEXT("(mer)"), LatCible));
			continue;
		}

		FWorldseedClimateSample Echantillon;
		Echantillon.TempMeanC = Monde.Climate.TempMeanC.IsValidIndex(Cellule)
			? Monde.Climate.TempMeanC[Cellule] : 15.0f;
		Echantillon.PrecipMm = Monde.Climate.PrecipMm.IsValidIndex(Cellule)
			? Monde.Climate.PrecipMm[Cellule] : 700.0f;
		Echantillon.SeasonalAmpC = Monde.Climate.SeasonalAmpC.IsValidIndex(Cellule)
			? Monde.Climate.SeasonalAmpC[Cellule] : 12.0f;
		Echantillon.Continentality = Monde.Climate.Continentality.IsValidIndex(Cellule)
			? Monde.Climate.Continentality[Cellule] : 0.5f;
		Echantillon.LatitudeDeg = Geo.LatitudeDegForRow(MeilleureLigne);
		Echantillon.SummerRainFrac = WorldseedClimate::SummerRainFraction(
			*Regles, Geo, Echantillon.LatitudeDeg);

		const uint8 IdBiome = Monde.Biomes.Index.IsValidIndex(Cellule)
			? Monde.Biomes.Index[Cellule] : 0;
		const FString NomBiome = WorldseedBiomes::Name(
			static_cast<EWorldseedBiome>(IdBiome));

		FWorldseedKoppenEntree EntreeK;
		EntreeK.PrecipAnnuelMm = Echantillon.PrecipMm;
		EntreeK.TFroidC = Echantillon.TempMeanC - Echantillon.SeasonalAmpC * 0.5f;
		EntreeK.TChaudC = Echantillon.TempMeanC + Echantillon.SeasonalAmpC * 0.5f;
		{
			const FWorldseedClimatePreset P =
				WorldseedClimatePreset::Build(Echantillon, ReglesPreset);
			for (int32 S = 0; S < 4; ++S)
			{
				EntreeK.PluieSaisonMm[S] = P.RainfallMm[S] + P.SnowfallMm[S];
			}
		}
		const EWorldseedKoppen Classe = WorldseedKoppen::Classer(
			EntreeK, ReglesPreset.KoppenPointeMensuelleC);

		// --- l'annee defile ---------------------------------------------------
		//
		// ON COMPTE DEUX FOIS, ET C'EST UN TEMOIN, PAS UN LUXE. `Courant` est ce
		// que le joueur TRAVERSE, fondu compris ; `Cible` est ce que le modele
		// DESIGNE. Les deux doivent se ressembler -- si la part de temps sous la
		// pluie s'effondre entre la cible et le vecu, ce n'est pas le climat qui
		// est en cause mais le FONDU, qui lisse des averses plus courtes que sa
		// propre constante de temps. Sans cette colonne on reglerait le climat
		// pour corriger un defaut de lissage.
		FCompteur Pluie, Averse, Neige, Degage, Couvert, Orage, Aurore;
		FCompteur PluieCible, OrageCible;
		FWorldseedWeather Courant;

		for (int32 N = 0; N < Pas; ++N)
		{
			const double T = static_cast<double>(N) * PasS;
			Params.TimeSeconds = static_cast<float>(T);
			Params.SeasonPhase = static_cast<float>(FMath::Fmod(T / AnneeS, 1.0));

			const FWorldseedWeather Cible =
				WorldseedWeatherState::Evaluate(Echantillon, ReglesPreset, Params);

			// LE MEME FONDU QU'EN JEU. Prendre la cible brute ferait sauter la
			// meteo d'un pas a l'autre et gonflerait le compte d'episodes : ce
			// qu'on veut mesurer est ce que le joueur TRAVERSE, pas ce que le
			// modele designe.
			WorldseedWeatherState::BlendTowards(Courant, Cible,
				static_cast<float>(PasS) / FMath::Max(Params.VariationPeriodS, 1.0f));

			PluieCible.Voir(Cible.Rain >= SeuilPluie);
			OrageCible.Voir(Cible.Thunder >= SeuilOrage);

			Pluie.Voir(Courant.Rain >= SeuilPluie);
			Averse.Voir(Courant.Rain >= SeuilAverse);
			Neige.Voir(Courant.Snow >= SeuilNeige);
			Degage.Voir(Courant.CloudCoverage <= SeuilDegage);
			Couvert.Voir(Courant.CloudCoverage >= SeuilCouvert);
			Orage.Voir(Courant.Thunder >= SeuilOrage);
			Aurore.Voir(Courant.Aurora >= SeuilAurore);
		}

		const float Cent = 100.0f / FMath::Max(Pas, 1);
		L.Add(FString::Printf(
			TEXT("%-24s %5.0f   %-6s %5.1f %6.0f  %5.1f %5.1f  %5.1f   %5.1f  %5.1f    %4d     %4d")
			TEXT("   |%6.1f %5d"),
			*NomBiome.Left(24), Echantillon.LatitudeDeg, WorldseedKoppen::Nom(Classe),
			Echantillon.TempMeanC, Echantillon.PrecipMm,
			Pluie.Pas * Cent, Averse.Pas * Cent, Neige.Pas * Cent,
			Degage.Pas * Cent, Couvert.Pas * Cent,
			Orage.Episodes, Aurore.Episodes,
			PluieCible.Pas * Cent, OrageCible.Episodes));
	}

	L.Add(TEXT(""));
	L.Add(TEXT("L'AURORE NE COMPTE QUE LA NUIT : UDS porte une « Daytime Aurora"));
	L.Add(TEXT("Intensity » distincte, a zero, et fait le fondu lui-meme. Le"));
	L.Add(TEXT("chiffre ci-dessus compte les episodes, dont la moitie environ"));
	L.Add(TEXT("tombe de jour et ne se verra pas."));

	const FString Texte = FString::Join(L, TEXT("\n"));
	UE_LOG(LogTemp, Log, TEXT("%s"), *Texte);
	return Texte;
}
