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

	/**
	 * LA POUSSIERE A DEUX SEUILS, PARCE QU'ELLE A DEUX REGIMES.
	 *
	 * `Sand_Dust_Storm` pose 10, qui est le maximum du pack : on place donc la
	 * TEMPETE a la moitie, comme « averse » est a la moitie de la pluie forte.
	 * Et le VOILE juste sous le plancher permanent (2,0), pour qu'il se compte.
	 *
	 * Mesure du 29 septembre 2026, clarte du lointain : Dust 2 -> +4,4 sur 124
	 * (discret, c'est le voile) ; Dust 5 -> +11,9 (le lointain s'estompe) ;
	 * Dust 10 -> +16,1 (l'horizon disparait).
	 */
	constexpr float SeuilVoile = 1.0f;
	constexpr float SeuilTempete = 5.0f;

	/**
	 * LA BRUME A DEUX SEUILS POUR LA MEME RAISON QUE LA POUSSIERE.
	 *
	 * L'echelle du pack ne donne que ses deux bornes : `Foggy` pose 10, les
	 * douze autres prereglages 1 ou 2. Il n'y a donc RIEN entre les deux a quoi
	 * se caler, et ces deux seuils sont ARBITRAIRES -- 3 pour une brume qu'on
	 * remarque, 6 pour un vrai brouillard. Ils servent a COMPARER des sites
	 * entre eux, jamais a juger dans l'absolu.
	 *
	 * ⚠ LE BLIZZARD PASSE PAR LA MEME VARIABLE. `Out.Fog` porte l'humidite ET
	 * la poudrerie, qui prend le dessus par un `max` : un site neigeux et
	 * venteux comptera donc du « brouillard » qui est en realite de la neige
	 * soufflee. Ce n'est pas une erreur de la sonde, c'est ce que le pack
	 * expose -- et il faut le savoir en lisant la colonne d'une toundra.
	 */
	constexpr float SeuilBrume = 3.0f;
	constexpr float SeuilBrouillard = 6.0f;

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
	const double CycleS = (JourMin + NuitMin) * 60.0;
	const double AnneeS = CycleS * JoursParAn;

	/** Part du cycle reel passee de jour -- deux tiers aux durees nominales. */
	const double PartJour = JourMin / FMath::Max(JourMin + NuitMin, 1e-6);

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
		TEXT("   orages   aurore   voile  sable tempe   vent  vent")
		TEXT("   brume  epais  max   nuit   jour    arc-en-ciel   | CIBLE, sans fondu"));
	L.Add(TEXT("                                             C    mm       %     %       %       %      %")
		TEXT("    (episodes/an)       %      %  /an    moy   max")
		TEXT("       %      %  0-10      %      %      %   /an   | pluie %  orages sable        X       Y (m)"));

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
	// --- DEUX SITES PAR LATITUDE, ET LE SECOND REPARE UN DEFAUT D'INSTRUMENT --
	//
	// Cette sonde prenait la cellule MEDIANE en pluie de chaque ligne, et son
	// commentaire dit pourquoi : « un echantillon arbitraire n'est pas une
	// mesure ». C'est juste pour la pluie. Mais LA CELLULE MEDIANE DE LA LIGNE
	// 30 DEGRES N'EST PAS LE SAHARA : elle ne porte presque jamais d'aridite, si
	// bien qu'en l'etat la sonde NE POUVAIT PAS VOIR UNE TEMPETE DE SABLE, quel
	// que soit le modele -- elle aurait rendu zero et l'on aurait regle le
	// mauvais bouton.
	//
	// On ajoute donc un site au DECILE INFERIEUR de pluie. Pas le minimum, qui
	// serait exactement l'extremum arbitraire que le commentaire denonce : le
	// decile est encore un rang, donc une statistique.
	struct FSite { float Lat; bool bAride; };
	const FSite Sites[] = {
		{  75.0f, false }, {  75.0f, true },
		{  60.0f, false }, {  60.0f, true },
		{  45.0f, false }, {  45.0f, true },
		{  30.0f, false }, {  30.0f, true },
		{  15.0f, false }, {  15.0f, true },
		{   0.0f, false }, {   0.0f, true },
		{ -15.0f, false }, { -15.0f, true },
		{ -30.0f, false }, { -30.0f, true },
		{ -45.0f, false }, { -45.0f, true },
		{ -60.0f, false }, { -60.0f, true },
		{ -75.0f, false }, { -75.0f, true },
	};

	for (const FSite& Site : Sites)
	{
		const float LatCible = Site.Lat;
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
			Cellule = Site.bAride
				? Terres[Terres.Num() / 10]
				: Terres[Terres.Num() / 2];
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
		FCompteur Voile, Sable, SableCible;
		FCompteur Brume, Brouillard;

		// LA BRUME SE LIT AUSSI PAR HEURE, et c'est la seule facon de voir le
		// levier diurne : une part annuelle melange la nuit et l'apres-midi, et
		// un cycle qui double la brume a l'aube s'y noie. On compte donc a part
		// les pas qui tombent la nuit et ceux qui tombent en pleine journee.
		int32 PasNuit = 0, PasJour = 0;
		int32 BrumeNuit = 0, BrumeJour = 0;

		// LE MAXIMUM ATTEINT, comme pour le vent -- et pour la meme raison.
		// Sans lui, « la brume epaisse n'arrive jamais » ne se separe pas en
		// « elle arrive et ne dure pas » et « le modele n'y monte jamais ». Les
		// deux n'appellent pas le meme remede, et c'est ce chiffre qui a montre
		// que le signal ne passait pas par `Uniformiser`.
		float BrumeMax = 0.0f;

		// --- L'ARC-EN-CIEL EST-IL SEULEMENT ATTEIGNABLE ? -------------------
		//
		// LE PACK POSE TROIS CONDITIONS, et la deuxieme est celle qui inquiete :
		// il faut de la pluie ou de la brume pour le rendre visible, un ciel
		// assez DEGAGE pour que le soleil atteigne la camera -- « if the camera
		// is under an overcast cloud layer, the effect won't appear » -- et un
		// soleil assez BAS sur l'horizon.
		//
		// ⚠ J'AI CRAINT QUE CE SOIT IMPOSSIBLE, ET LA MESURE M'A DEMENTI.
		// Notre modele lie la pluie aux nuages -- `CloudCoverage` monte avec
		// l'occurrence de pluie, et fortement -- et j'en ai deduit qu'il ne
		// pleuvrait JAMAIS sous un ciel clair, donc que l'effet serait arme
		// pour rien. Cette colonne repond : 87 episodes par an en
		// mediterraneen, 102 en foret tropicale, 97 en savane, 31 en taiga, et
		// zero dans les deserts -- ou il ne pleut pas, ce qui est correct. Le
		// moment existe parce que l'averse est BREVE et que la couverture ne
		// sature pas a chaque fois.
		//
		// LE COMPTE EST UN PLAFOND, et il faut le dire : le pack ajoute sa
		// propre condition sur l'elevation du soleil, plus fine que la fenetre
		// horaire employee ici. Ce qui est etabli est que le couple « pluie
		// visible sous un ciel degage » se rencontre, pas le nombre exact
		// d'arcs-en-ciel qu'un joueur verra.
		//
		// LE SOLEIL BAS SE LIT SUR L'HEURE, faute de mieux : la sonde ne connait
		// pas l'elevation solaire, qui demande la simulation d'UDS. Avant huit
		// heures et apres seize, le soleil est bas a toute latitude moyenne.
		FCompteur Arc;
		FWorldseedWeather Courant;

		// LE VENT N'EST PAS UN COMPTEUR, C'EST UNE AMPLITUDE -- et il faut les
		// deux colonnes : sans elles, « aucune tempete » ne se separe pas en
		// « le vent ne s'est jamais leve » et « il s'est leve et la poussiere
		// n'a pas suivi ». C'est la regle « guetter un evenement rare ne separe
		// pas les deux causes », appliquee a une sonde au lieu d'une capture.
		double VentSomme = 0.0;
		float VentMax = 0.0f;

		for (int32 N = 0; N < Pas; ++N)
		{
			const double T = static_cast<double>(N) * PasS;
			Params.TimeSeconds = static_cast<float>(T);
			Params.SeasonPhase = static_cast<float>(FMath::Fmod(T / AnneeS, 1.0));

			// L'HEURE DU JOUR, ET LA CORRESPONDANCE N'EST PAS LINEAIRE.
			//
			// En jeu l'heure vient d'UDS ; ici il faut la rejouer, et une rampe
			// lineaire sur le cycle serait FAUSSE : la journee dure trente
			// minutes reelles pour douze heures de jeu et la nuit quinze pour
			// les douze autres. Un pas de temps reel couvre donc DEUX FOIS plus
			// d'heures la nuit que le jour -- et la sonde echantillonnant en
			// temps REEL, une rampe lineaire donnerait a la nuit la moitie des
			// releves au lieu du tiers, surestimant la brume nocturne de
			// moitie.
			//
			// L'aube a six heures et le crepuscule a dix-huit sont la convention
			// NOMINALE d'UDS, exacte a l'equateur et a l'equinoxe. Avec
			// `Simulate Real Sun`, les vraies heures suivent la latitude et la
			// saison : c'est une approximation, et elle est dite.
			const double PartDuCycle = FMath::Fmod(T / CycleS, 1.0);
			Params.HeureDuJour = static_cast<float>(PartDuCycle < PartJour
				? 6.0 + 12.0 * PartDuCycle / PartJour
				: FMath::Fmod(18.0 + 12.0 * (PartDuCycle - PartJour)
					/ FMath::Max(1.0 - PartJour, 1e-6), 24.0));

			const FWorldseedWeather Cible =
				WorldseedWeatherState::Evaluate(Echantillon, ReglesPreset, Params);

			// LE MEME FONDU QU'EN JEU -- ET CETTE FOIS C'EST VRAI.
			//
			// Cette ligne calculait `pas / periode`, soit une constante de
			// temps d'une PERIODE ENTIERE, quand le jeu fond sur douze
			// secondes reelles : quinze fois plus fort. Le commentaire
			// annoncait deja « le meme fondu qu'en jeu », et c'est ce qui l'a
			// rendu invisible. La sonde rendait 9 orages par an pour 119
			// designes -- un chiffre de l'INSTRUMENT, pas du monde.
			WorldseedWeatherState::BlendTowards(Courant, Cible,
				WorldseedWeatherState::AlphaDeFondu(
					static_cast<float>(PasS), WorldseedWeatherState::FonduDefautS));

			PluieCible.Voir(Cible.Rain >= SeuilPluie);
			OrageCible.Voir(Cible.Thunder >= SeuilOrage);
			SableCible.Voir(Cible.Dust >= SeuilTempete);

			Pluie.Voir(Courant.Rain >= SeuilPluie);
			Averse.Voir(Courant.Rain >= SeuilAverse);
			Neige.Voir(Courant.Snow >= SeuilNeige);
			Degage.Voir(Courant.CloudCoverage <= SeuilDegage);
			Couvert.Voir(Courant.CloudCoverage >= SeuilCouvert);
			Orage.Voir(Courant.Thunder >= SeuilOrage);
			Aurore.Voir(Courant.Aurora >= SeuilAurore);
			Voile.Voir(Courant.Dust >= SeuilVoile);
			Sable.Voir(Courant.Dust >= SeuilTempete);

			Brume.Voir(Courant.Fog >= SeuilBrume);
			Brouillard.Voir(Courant.Fog >= SeuilBrouillard);
			BrumeMax = FMath::Max(BrumeMax, Courant.Fog);

			const bool bSoleilBas = Params.HeureDuJour < 8.0f
				|| Params.HeureDuJour >= 16.0f;
			Arc.Voir(Courant.Rain >= SeuilPluie
				&& Courant.CloudCoverage <= SeuilCouvert
				&& bSoleilBas);

			// MINUIT A SIX HEURES contre DIX A SEIZE : deux fenetres franches,
			// separees par les deux transitions ou la brume se leve et se
			// reforme. Les compter ferait tendre les deux colonnes l'une vers
			// l'autre et masquerait le cycle qu'on veut voir.
			if (Params.HeureDuJour < 6.0f)
			{
				++PasNuit;
				if (Courant.Fog >= SeuilBrume) { ++BrumeNuit; }
			}
			else if (Params.HeureDuJour >= 10.0f && Params.HeureDuJour < 16.0f)
			{
				++PasJour;
				if (Courant.Fog >= SeuilBrume) { ++BrumeJour; }
			}

			VentSomme += Courant.WindIntensity;
			VentMax = FMath::Max(VentMax, Courant.WindIntensity);
		}

		// OU C EST, en metres du monde -- une meteo qu on ne sait pas aller voir
		// n existe pas pour le joueur, exactement comme une grotte sans entree.
		const int32 Colonne = Cellule % Geo.NX;
		const float XM = (static_cast<float>(Colonne) / Geo.NX - 0.5f) * Geo.HeightM * 2.0f;
		const float YM = (static_cast<float>(MeilleureLigne) / Geo.NY - 0.5f) * Geo.HeightM;

		const float Cent = 100.0f / FMath::Max(Pas, 1);
		const FString Etiquette = Site.bAride
			? (NomBiome.Left(16) + TEXT(" (aride)"))
			: NomBiome.Left(24);

		L.Add(FString::Printf(
			TEXT("%-24s %5.0f   %-6s %5.1f %6.0f  %5.1f %5.1f  %5.1f   %5.1f  %5.1f    %4d     %4d")
			TEXT("  %6.1f %6.1f %5d  %5.1f %5.1f")
			TEXT("  %6.1f %6.1f %5.1f %6.1f %6.1f %6.2f %5d")
			TEXT("   |%6.1f %5d %5d   %7.0f %7.0f"),
			*Etiquette, Echantillon.LatitudeDeg, WorldseedKoppen::Nom(Classe),
			Echantillon.TempMeanC, Echantillon.PrecipMm,
			Pluie.Pas * Cent, Averse.Pas * Cent, Neige.Pas * Cent,
			Degage.Pas * Cent, Couvert.Pas * Cent,
			Orage.Episodes, Aurore.Episodes,
			Voile.Pas * Cent, Sable.Pas * Cent, Sable.Episodes,
			static_cast<float>(VentSomme / FMath::Max(Pas, 1)), VentMax,
			Brume.Pas * Cent, Brouillard.Pas * Cent, BrumeMax,
			BrumeNuit * 100.0f / FMath::Max(PasNuit, 1),
			BrumeJour * 100.0f / FMath::Max(PasJour, 1),
			Arc.Pas * Cent, Arc.Episodes,
			PluieCible.Pas * Cent, OrageCible.Episodes, SableCible.Episodes, XM, YM));
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
