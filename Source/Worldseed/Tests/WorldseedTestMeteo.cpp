// Worldseed - la meteo suit-elle le climat du lieu ?

#if WITH_DEV_AUTOMATION_TESTS

#include "Procedural/WorldseedClimatePreset.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedWeatherState.h"
#include "Procedural/WorldseedWeatherSignal.h"

#include "Misc/AutomationTest.h"

/**
 * CE QUE CES ORACLES PROTEGENT, ET POURQUOI ILS ARRIVENT SI TARD.
 *
 * Tout le pilotage du ciel etait ecrit depuis le portage en C++ -- prereglage
 * climatique par biome, tirage de meteo, fondu vers la cible -- et RIEN NE
 * L'APPELAIT. Ni le tick de l'acteur, coupe a dessein, ni celui du composant,
 * coupe aussi : `FeedSky` n'avait aucun appelant. La meteo tournait donc sur
 * les reglages propres d'Ultra Dynamic Sky, sans rapport avec le sol.
 *
 * SIGNALE PAR LE PROPRIETAIRE A L'USAGE -- « je vois rarement de la pluie » --
 * et trouve par le journal : `Drive` emet forcement l'une de deux lignes,
 * quelle que soit la branche prise, et aucune n'apparaissait dans les journaux
 * de partie. Aucun test ne pouvait le voir, parce qu'aucun test ne regardait
 * cette chaine.
 *
 * ON NE PEUT PAS TESTER UN MINUTEUR SANS INSTANCIER L'ACTEUR, donc ces oracles
 * ne gardent pas le cablage : ils gardent ce que le cablage sert a produire --
 * une meteo qui DISTINGUE les climats. Si `Evaluate` cessait de le faire, le
 * ciel serait branche et resterait uniforme, ce qui se verrait comme le meme
 * defaut.
 */
namespace
{
	/** Les cinq mesures qui suffisent a decrire un climat. */
	FWorldseedClimateSample Climat(float TempC, float PrecipMm,
		float AmplitudeC, float Continentalite, float LatitudeDeg)
	{
		FWorldseedClimateSample S;
		S.TempMeanC = TempC;
		S.PrecipMm = PrecipMm;
		S.SeasonalAmpC = AmplitudeC;
		S.Continentality = Continentalite;
		S.LatitudeDeg = LatitudeDeg;
		return S;
	}

	/**
	 * Part du temps ou il pleut, sur un cycle entier.
	 *
	 * ON ECHANTILLONNE LE CYCLE, on ne lit pas un instant. La meteo derive d'un
	 * signal temporel : un seul appel tomberait au hasard sur une accalmie ou
	 * sur une averse, et le test serait une loterie sur la valeur de depart.
	 */
	float PartDePluie(const FWorldseedClimateSample& Sample,
		const FWorldseedClimatePresetRules& Regles, int32 Echantillons = 400)
	{
		FWorldseedWeatherParams P;
		P.VariationPeriodS = 180.0f;
		P.SeasonPhase = 0.5f;
		P.LatSpanDeg = 180.0f;
		P.Seed = 20260909;

		int32 Mouilles = 0;
		for (int32 I = 0; I < Echantillons; ++I)
		{
			// Plusieurs cycles, pour ne pas mesurer une seule periode.
			P.TimeSeconds = static_cast<float>(I) * 7.3f;
			if (WorldseedWeatherState::Evaluate(Sample, Regles, P).Rain > 0.05f)
			{
				++Mouilles;
			}
		}
		return static_cast<float>(Mouilles) / FMath::Max(1, Echantillons);
	}

	/** Ce qu'un cycle de poussiere et de vent donne, sur un climat donne. */
	struct FReleve
	{
		float PoussiereMax = 0.0f;
		float PartVoile = 0.0f;      // part du temps au-dessus de 1,0
		float PartTempete = 0.0f;    // part du temps au-dessus de 5,0
		float VentMax = 0.0f;
		float VentMoyen = 0.0f;
		int32 PluieEtSable = 0;      // les deux a la fois : ne doit pas arriver
	};

	/**
	 * ON ECHANTILLONNE PLUSIEURS CYCLES DE POUSSIERE, pas plusieurs cycles de
	 * PLUIE -- et ce n'est pas la meme duree. Le signal de poussiere est ralenti
	 * par `poussierePeriodeFacteur` (3 par defaut), donc sa periode vaut 540 s
	 * quand celle de la pluie en vaut 180. Un pas cale sur la pluie ne verrait
	 * qu'une poignee de cycles et rendrait une loterie sur la graine.
	 */
	FReleve Releve(const FWorldseedClimateSample& Sample,
		const FWorldseedClimatePresetRules& Regles, int32 Echantillons = 3000)
	{
		FWorldseedWeatherParams P;
		P.VariationPeriodS = 180.0f;
		P.SeasonPhase = 0.5f;
		P.LatSpanDeg = 180.0f;
		P.Seed = 20260909;

		FReleve R;
		double VentSomme = 0.0;
		int32 Voile = 0, Tempete = 0;

		for (int32 I = 0; I < Echantillons; ++I)
		{
			P.TimeSeconds = static_cast<float>(I) * 31.0f;   // ~172 cycles lents
			const FWorldseedWeather W =
				WorldseedWeatherState::Evaluate(Sample, Regles, P);

			R.PoussiereMax = FMath::Max(R.PoussiereMax, W.Dust);
			R.VentMax = FMath::Max(R.VentMax, W.WindIntensity);
			VentSomme += W.WindIntensity;
			if (W.Dust > 1.0f) { ++Voile; }
			if (W.Dust > 5.0f) { ++Tempete; }
			if (W.Dust > 3.0f && W.Rain > 1.5f) { ++R.PluieEtSable; }
		}

		const float N = static_cast<float>(FMath::Max(1, Echantillons));
		R.PartVoile = Voile / N;
		R.PartTempete = Tempete / N;
		R.VentMoyen = static_cast<float>(VentSomme) / N;
		return R;
	}

	/** Les quatre climats qui decident, decrits par leurs seules mesures. */
	FWorldseedClimateSample DesertChaud()  { return Climat(22.0f, 163.0f, 18.0f, 0.85f, 30.0f); }
	FWorldseedClimateSample Gobi()         { return Climat(-0.4f, 194.0f, 38.0f, 0.95f, 43.0f); }
	FWorldseedClimateSample Calotte()      { return Climat(-18.8f, 125.0f, 25.0f, 0.90f, -75.0f); }
	FWorldseedClimateSample Tropique()     { return Climat(26.0f, 2400.0f, 3.0f, 0.15f, 4.0f); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedMeteoPoussiereSuitLAridite,
	"Worldseed.Meteo.LaPoussiereSuitLAridite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedMeteoPoussiereSuitLAridite::RunTest(const FString&)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("les regles se chargent"), R)) { AddError(Erreur); return false; }
	const FWorldseedClimatePresetRules Regles = FWorldseedClimatePresetRules::FromRules(*R);

	const float Desert = WorldseedClimatePreset::Build(DesertChaud(), Regles).DustPart;
	const float Foret = WorldseedClimatePreset::Build(Tropique(), Regles).DustPart;

	AddInfo(FString::Printf(TEXT("part de poussiere : desert %.3f, foret tropicale %.3f"),
		Desert, Foret));

	TestTrue(TEXT("un desert en donne largement"), Desert > 0.8f);

	// EXACTEMENT ZERO, ET C'EST CE QUI REND LA PART CONTINUE SURE. Si
	// `Smoothstep` ne fermait pas franchement, une foret tropicale porterait un
	// voile faible et PERMANENT -- le defaut exact de l'aurore, dont le defaut
	// du pack valait 0,12 au lieu de 0 et posait un rideau a l'equateur.
	TestEqual(TEXT("une foret tropicale n'en donne AUCUNE"), Foret, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedMeteoCalotteNePoudroiePas,
	"Worldseed.Meteo.LaCalotteNePoudroiePas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedMeteoCalotteNePoudroiePas::RunTest(const FString&)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("les regles se chargent"), R)) { AddError(Erreur); return false; }
	const FWorldseedClimatePresetRules Regles = FWorldseedClimatePresetRules::FromRules(*R);

	// CE DEFAUT A EXISTE, et il venait d'une correction juste : retirer le
	// `T > 5` en dur a rendu sa poussiere au Gobi -- c'etait le but -- et a
	// aussi donne 99,9 % de voile et 56 tempetes par an a -18,8 C. Un desert
	// polaire est aride, et il est couvert de GLACE.
	const float Glace = WorldseedClimatePreset::Build(Calotte(), Regles).DustPart;
	const float Froid = WorldseedClimatePreset::Build(Gobi(), Regles).DustPart;

	AddInfo(FString::Printf(TEXT("part de poussiere : calotte %.3f, Gobi %.3f"),
		Glace, Froid));

	TestEqual(TEXT("une calotte glaciaire ne poudroie pas"), Glace, 0.0f);

	// ET LE TEMOIN DANS L'AUTRE SENS, sans lequel « la calotte rend zero » se
	// satisferait d'un terme de gel qui aurait tout referme : le Gobi est a
	// -0,4 C de moyenne annuelle, et c'est LUI qui a motive le retrait du seuil.
	TestTrue(TEXT("mais un desert froid en donne, sinon le correctif a tout ferme"),
		Froid > 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedMeteoTempeteAtteintLeHaut,
	"Worldseed.Meteo.LaTempeteAtteintLeHautDeLEchelle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedMeteoTempeteAtteintLeHaut::RunTest(const FString&)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("les regles se chargent"), R)) { AddError(Erreur); return false; }
	const FWorldseedClimatePresetRules Regles = FWorldseedClimatePresetRules::FromRules(*R);

	// DEUX SITES, ET C'EST UN ATTENDU CORRIGE.
	//
	// La premiere version exigeait 9,5 sur le desert chaud du monde, qui recoit
	// 163 mm/an : sa part d'aridite vaut 0,908, donc son maximum ATTEIGNABLE est
	// 9,08 et le test tombait. L'attendu etait naif, pas le code -- il supposait
	// une part de 1, ce qui ne se produit que sous `poussierePleinePluieMm`.
	//
	// Ce test demande si LA CHAINE peut atteindre le haut de l'echelle, donc il
	// le demande la ou rien ne la bride : un desert hyper-aride, comme le site
	// « desert chaud (aride) » de la sonde, mesure a 64 mm/an. Le desert
	// ordinaire garde les parts, qui sont sa vraie question.
	const FWorldseedClimateSample HyperAride = Climat(24.0f, 60.0f, 18.0f, 0.9f, 25.0f);
	const FReleve H = Releve(HyperAride, Regles);
	const FReleve D = Releve(DesertChaud(), Regles);

	AddInfo(FString::Printf(
		TEXT("hyper-aride (part %.3f) : poussiere max %.2f  |  desert (part %.3f) : ")
		TEXT("max %.2f, voile %.1f %%, tempete %.1f %%, vent max %.2f"),
		WorldseedClimatePreset::Build(HyperAride, Regles).DustPart, H.PoussiereMax,
		WorldseedClimatePreset::Build(DesertChaud(), Regles).DustPart,
		D.PoussiereMax, D.PartVoile * 100.0f, D.PartTempete * 100.0f, D.VentMax));

	// LE HAUT DE L'ECHELLE DOIT ETRE ATTEIGNABLE, et il ne l'etait pas : la
	// forme d'avant soustrayait une constante a un signal dont le support est
	// BORNE a 0,9327, donc `Dust = 10` plafonnait a 8,13 quoi qu'on regle.
	// Passer par `Uniformiser` supprime la cause au lieu de compenser l'effet.
	TestTrue(TEXT("la tempete atteint le haut de l'echelle du pack"),
		H.PoussiereMax >= 9.5f);

	// ET LE VOILE EST PERMANENT, sans quoi « la tempete monte a 10 » serait
	// compatible avec un desert limpide le reste du temps -- l'inverse du reel.
	TestTrue(TEXT("et le voile est permanent"), D.PartVoile > 0.9f);

	// LA BANDE, PAS UN POINT : seule la forme voulue la satisfait. Trop rare, la
	// tempete n'existe pas pour le joueur ; trop frequente, ce n'est plus une
	// tempete. Mesure au bulletin du ciel : 1,4 % du temps, 12 episodes par an.
	TestTrue(TEXT("la tempete reste rare, mais elle existe"),
		D.PartTempete > 0.002f && D.PartTempete < 0.08f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedMeteoVentSuitLePack,
	"Worldseed.Meteo.LeVentSuitLEchelleDuPack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedMeteoVentSuitLePack::RunTest(const FString&)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("les regles se chargent"), R)) { AddError(Erreur); return false; }
	const FWorldseedClimatePresetRules Regles = FWorldseedClimatePresetRules::FromRules(*R);

	const FReleve D = Releve(DesertChaud(), Regles);

	AddInfo(FString::Printf(TEXT("vent : moyenne %.2f, maximum %.2f"),
		D.VentMoyen, D.VentMax));

	// LES DEUX BORNES VIENNENT DES PREREGLAGES LIVRES PAR LE PACK, relevees par
	// l'API : 2 pour un ciel clair, 3 pour `Overcast`, 4 pour `Snow`, et 10 pour
	// les trois etats violents. Notre plafond valait 8, une invention.
	TestTrue(TEXT("le vent atteint le 10 que le pack reserve aux tempetes"),
		D.VentMax >= 9.5f);

	// ET IL NE SOUFFLE PAS SANS CESSE. Ce defaut a existe : `Souffle` etant
	// UNIFORME par construction, une rampe lineaire rendait une moyenne au
	// MILIEU de la plage -- 6,3 sur 10 mesures, plus du double d'`Overcast`, et
	// IDENTIQUE sur les vingt-deux sites de la sonde.
	TestTrue(TEXT("mais il ne souffle pas en permanence"),
		D.VentMoyen > 1.5f && D.VentMoyen < 5.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedMeteoPoussiereNeTombePasSousLaPluie,
	"Worldseed.Meteo.LaPoussiereNeTombePasSousLaPluie",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedMeteoPoussiereNeTombePasSousLaPluie::RunTest(const FString&)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("les regles se chargent"), R)) { AddError(Erreur); return false; }
	const FWorldseedClimatePresetRules Regles = FWorldseedClimatePresetRules::FromRules(*R);

	// UNE STEPPE, parce que c'est le seul endroit ou les deux peuvent se
	// rencontrer : assez seche pour poudroyer, assez arrosee pour qu'il pleuve.
	// Un desert ne prouverait rien -- il n'y pleut jamais.
	const FReleve S = Releve(Climat(14.0f, 300.0f, 20.0f, 0.8f, 40.0f), Regles);

	AddInfo(FString::Printf(TEXT("steppe : voile %.1f %%, instants pluie+sable %d"),
		S.PartVoile * 100.0f, S.PluieEtSable));

	TestEqual(TEXT("il ne pleut jamais PENDANT une tempete de sable"),
		S.PluieEtSable, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedMeteoBlizzardMangeLaVisibilite,
	"Worldseed.Meteo.LeBlizzardMangeLaVisibilite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedMeteoBlizzardMangeLaVisibilite::RunTest(const FString&)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("les regles se chargent"), R)) { AddError(Erreur); return false; }
	const FWorldseedClimatePresetRules Regles = FWorldseedClimatePresetRules::FromRules(*R);

	// UNE TOUNDRA : assez froide pour que tout tombe en neige, assez arrosee
	// pour qu'il tombe quelque chose.
	const FWorldseedClimateSample Toundra = Climat(-8.0f, 500.0f, 25.0f, 0.8f, -62.0f);

	FWorldseedWeatherParams P;
	P.VariationPeriodS = 180.0f;
	P.LatSpanDeg = 180.0f;
	P.Seed = 20260909;

	// ON CHERCHE LE PIRE INSTANT, celui ou la neige ET le vent sont forts en
	// meme temps : c'est la definition du blizzard, et c'est un evenement rare
	// par construction. Une moyenne le noierait.
	float FogAuBlizzard = 0.0f, NeigeAlors = 0.0f, VentAlors = 0.0f;
	float FogCalme = 10.0f, NeigeCalme = 0.0f;

	// ⚠ ON BALAYE AUSSI LA SAISON, ET LA PREMIERE VERSION NE LE FAISAIT PAS.
	//
	// Elle posait `SeasonPhase = 0` en l'annotant « l'hiver austral » -- c'etait
	// faux : `SwapHemisphere` echange les saisons sous l'equateur, donc zero y
	// designe l'ETE local. La fixture ne produisait aucune neige, le test
	// echouait sur « neige 0,0 », et il aurait tout aussi bien pu PASSER par
	// absence de matiere si l'attendu avait ete tourne autrement. C'est le
	// defaut de fixture muette que ce depot paye regulierement.
	//
	// Balayer la phase rend le test independant de cette convention : quelle
	// que soit la moitie de l'annee qui porte l'hiver, on la traverse.
	for (int32 I = 0; I < 4000; ++I)
	{
		P.TimeSeconds = static_cast<float>(I) * 31.0f;
		P.SeasonPhase = static_cast<float>(I % 40) / 40.0f;
		const FWorldseedWeather W = WorldseedWeatherState::Evaluate(Toundra, Regles, P);

		const float Souffle = W.Snow * W.WindIntensity;
		if (W.Snow > 1.0f && Souffle > NeigeAlors * VentAlors)
		{
			FogAuBlizzard = W.Fog;
			NeigeAlors = W.Snow;
			VentAlors = W.WindIntensity;
		}
		// LE TEMOIN : de la neige SANS vent. Sans lui, « le brouillard monte
		// quand il neige » serait satisfait par un brouillard qui monte
		// toujours -- et c'est justement ce qu'on ne veut pas, une chute de
		// neige calme laissant voir loin.
		if (W.Snow > 1.0f && W.WindIntensity < 3.0f && W.Fog < FogCalme)
		{
			FogCalme = W.Fog;
			NeigeCalme = W.Snow;
		}
	}

	AddInfo(FString::Printf(
		TEXT("blizzard : neige %.1f vent %.1f -> brouillard %.2f  |  ")
		TEXT("neige calme %.1f -> brouillard %.2f"),
		NeigeAlors, VentAlors, FogAuBlizzard, NeigeCalme, FogCalme));

	// LE DEFAUT QUE CET ORACLE GARDE A EXISTE : le terme d'humidite du
	// brouillard porte `(1 - Occurrence)`, qui le fait BAISSER quand il
	// precipite. C'est juste pour une averse, qui lessive l'air, et faux pour
	// la neige -- `Snow_Blizzard` du pack pose Fog = 10 quand notre modele
	// rendait 0,4, son plancher.
	// LA FIXTURE DOIT AVOIR PRODUIT DE LA NEIGE, sans quoi tout ce qui suit
	// passerait -- ou echouerait -- par ABSENCE de matiere et non par la
	// propriete testee.
	if (!TestTrue(TEXT("la fixture neige vraiment"), NeigeAlors > 1.0f))
	{
		return false;
	}

	TestTrue(TEXT("un blizzard reduit la visibilite"), FogAuBlizzard > 4.0f);

	// ET PAS TOUT LE TEMPS : une chute de neige par temps calme laisse voir.
	if (NeigeCalme > 0.0f)
	{
		TestTrue(TEXT("mais une neige calme laisse voir loin"), FogCalme < 3.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedMeteoEchelleDuPack,
	"Worldseed.Meteo.LEchelleEstCelleDuPack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedMeteoEchelleDuPack::RunTest(const FString&)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("les regles se chargent"), R)) { AddError(Erreur); return false; }
	const FWorldseedClimatePresetRules Regles = FWorldseedClimatePresetRules::FromRules(*R);

	// LA GARDE CONTRE LA CLASSE DE BUG « 0..1 CONTRE 0..10 », qui a rendu la
	// pluie invisible jusqu'au 28 septembre 2026 : l'averse la plus violente du
	// monde valait 1,0, soit le TIERS de la plus legere bruine que le pack sache
	// dessiner. Les trois valeurs sont relevees dans ses assets.
	AddInfo(FString::Printf(TEXT("vent : calme %.1f, maximum %.1f ; voile %.1f"),
		Regles.VentCalme, Regles.VentMaxUds, Regles.PoussiereVoile));

	TestEqual(TEXT("le maximum du vent est le 10 du pack"), Regles.VentMaxUds, 10.0f);
	TestTrue(TEXT("le calme est de l'ordre du ciel clair du pack"),
		Regles.VentCalme >= 0.5f && Regles.VentCalme <= 3.0f);
	TestTrue(TEXT("le voile est faible mais non nul"),
		Regles.PoussiereVoile > 0.5f && Regles.PoussiereVoile < 4.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedMeteoDistingueLesClimats,
	"Worldseed.Meteo.DistingueLesClimats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedMeteoDistingueLesClimats::RunTest(const FString&)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("les regles se chargent"), R))
	{
		AddError(Erreur);
		return false;
	}
	const FWorldseedClimatePresetRules Regles =
		FWorldseedClimatePresetRules::FromRules(*R);

	// Deux climats que tout oppose, decrits par leurs seules mesures.
	const float Desert = PartDePluie(Climat(26.0f, 90.0f, 18.0f, 0.85f, 24.0f), Regles);
	const float Tropique = PartDePluie(Climat(26.0f, 2400.0f, 3.0f, 0.15f, 4.0f), Regles);

	AddInfo(FString::Printf(TEXT("part de pluie : desert chaud %.1f %%, foret tropicale %.1f %%"),
		Desert * 100.0f, Tropique * 100.0f));

	// LE SENS D'ABORD : c'est lui qui dit que la pluie suit le climat.
	TestTrue(TEXT("il pleut plus en foret tropicale qu'en desert"), Tropique > Desert);

	// PUIS L'AMPLEUR, parce qu'un ecart d'un pour cent serait invisible en jeu
	// et laisserait passer exactement le defaut signale : une meteo qui ne
	// change pas quand on traverse le monde.
	TestTrue(TEXT("et l'ecart se voit : au moins vingt points"),
		(Tropique - Desert) > 0.20f);

	// LE TEMOIN : une foret tropicale doit pleuvoir souvent, sans quoi
	// « plus qu'un desert » pourrait vouloir dire 2 % contre 0 %.
	//
	// LE SEUIL EST PASSE D'UN TIERS A UN CINQUIEME LE 28 SEPTEMBRE 2026, et ce
	// n'est pas un elargissement de complaisance : l'attendu etait cale sur un
	// modele ou la pluie se declenchait des qu'il y avait des NUAGES, ce qui
	// donnait 96 % du temps sous la pluie en foret tropicale. La frequence vient
	// desormais de la QUANTITE, et le releve terrestre place une foret tropicale
	// entre vingt et trente pour cent du temps -- on en mesure 32. C'est donc
	// l'ancien attendu qui etait faux, pas la mesure, et le garder aurait exige
	// de rendre le modele moins juste pour qu'un test passe.
	TestTrue(TEXT("la foret tropicale est arrosee au moins un cinquieme du temps"),
		Tropique > 0.20f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedMeteoNeigeAuFroid,
	"Worldseed.Meteo.NeigeAuFroid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedMeteoNeigeAuFroid::RunTest(const FString&)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("les regles se chargent"), R))
	{
		AddError(Erreur);
		return false;
	}
	const FWorldseedClimatePresetRules Regles =
		FWorldseedClimatePresetRules::FromRules(*R);

	FWorldseedWeatherParams P;
	P.VariationPeriodS = 180.0f;
	P.SeasonPhase = 0.0f;       // coeur de l'hiver
	P.LatSpanDeg = 180.0f;
	P.Seed = 20260909;

	// Meme pluie annuelle, meme latitude, MEME AMPLITUDE : seule la temperature
	// change. Sans cette precaution on comparerait deux climats sur plusieurs
	// axes a la fois, et l'on ne saurait pas lequel fait basculer la neige.
	//
	// L'AMPLITUDE EST FAIBLE, ET C'EST TOUT L'OBJET DE LA CORRECTION. La
	// premiere version de ce test posait trente degres d'ecart saisonnier : a
	// quatorze degres de MOYENNE, l'hiver tombait alors a moins un, et il
	// neigeait -- le test criait, mais c'est le modele qui avait raison et mon
	// attendu qui etait naif. Un attendu naif se CORRIGE, il ne s'elargit pas.
	// A six degres d'amplitude, l'hiver doux reste a onze : aucune neige n'y
	// est possible, et le controle redevient franc.
	const FWorldseedClimateSample Froid = Climat(-12.0f, 500.0f, 6.0f, 0.8f, 68.0f);
	const FWorldseedClimateSample Doux = Climat(14.0f, 500.0f, 6.0f, 0.8f, 68.0f);

	float NeigeFroide = 0.0f;
	float NeigeDouce = 0.0f;
	for (int32 I = 0; I < 400; ++I)
	{
		P.TimeSeconds = static_cast<float>(I) * 7.3f;
		NeigeFroide = FMath::Max(NeigeFroide,
			WorldseedWeatherState::Evaluate(Froid, Regles, P).Snow);
		NeigeDouce = FMath::Max(NeigeDouce,
			WorldseedWeatherState::Evaluate(Doux, Regles, P).Snow);
	}

	AddInfo(FString::Printf(TEXT("neige maximale : a -12 C %.2f, a +14 C %.2f"),
		NeigeFroide, NeigeDouce));

	TestTrue(TEXT("il neige quand il fait froid"), NeigeFroide > 0.05f);
	TestTrue(TEXT("et jamais a quatorze degres, A PLUIE EGALE"), NeigeDouce < 0.01f);

	return true;
}


/**
 * LA LOI DU SIGNAL D'AGITATION SE MESURE, ELLE NE SE CALCULE PAS.
 *
 * ⚠ CE TEST EXISTE PARCE QUE J'AI FAIT L'INVERSE, le 28 septembre 2026, dans
 * l'heure meme ou j'ecrivais au registre qu'un seuil n'est pas une part.
 * `Storminess` somme trois octaves : sa loi est une cloche, donc un seuil pose
 * dessus ne rend pas la fraction demandee, et il faut l'uniformiser avant. J'ai
 * pose son ecart-type par l'algebre d'une somme de trois lois uniformes --
 * 0,186 -- sans jamais le relever. Deux choses etaient fausses :
 *   - `ValueNoise` n'est PAS uniforme : il interpole deux tirages uniformes par
 *     un smoothstep, ce qui resserre la loi autour de sa moyenne ;
 *   - la somme a un SUPPORT BORNE, donc ses queues tombent bien plus vite que
 *     celles d'une cloche -- et c'est precisement dans les queues que le seuil
 *     de pluie travaille.
 * Consequence mesuree : la taiga voyait 0,5 % de precipitation pour les 6 %
 * que le modele visait, soit un facteur DOUZE.
 *
 * CE QU'IL GARDE : que `Uniformiser` rende bien une loi UNIFORME sur le signal
 * reel. Si les poids des octaves changent, ou si `ValueNoise` change de forme,
 * la table interne cesse de correspondre et ce test tombe -- au lieu que le ciel
 * se deregle en silence.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedMeteoSignalUniforme,
	"Worldseed.Meteo.LeSignalEstUniforme",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedMeteoSignalUniforme::RunTest(const FString& Parameters)
{
	// ON ECHANTILLONNE COMME LE JEU, pas au hasard : meme periode, meme pas que
	// la sonde du ciel, et plusieurs graines pour ne pas mesurer une seule
	// realisation du bruit.
	constexpr float PeriodeS = 180.0f;
	constexpr float PasS = PeriodeS / 24.0f;
	constexpr int32 ParGraine = 12960;
	const int32 Graines[] = { 20260909, 1337, 424242, 7 };

	TArray<float> Brut;
	TArray<float> Uniforme;
	Brut.Reserve(ParGraine * UE_ARRAY_COUNT(Graines));
	Uniforme.Reserve(Brut.Max());

	for (const int32 Graine : Graines)
	{
		for (int32 N = 0; N < ParGraine; ++N)
		{
			const float S = WorldseedWeatherSignal::Storminess(
				static_cast<float>(N) * PasS, PeriodeS, Graine);
			Brut.Add(S);
			Uniforme.Add(WorldseedWeatherSignal::Uniformiser(S));
		}
	}

	Brut.Sort();
	Uniforme.Sort();

	auto Quantile = [](const TArray<float>& Tri, float P)
	{
		const int32 I = FMath::Clamp(
			FMath::RoundToInt(P * (Tri.Num() - 1)), 0, Tri.Num() - 1);
		return Tri[I];
	};

	// LE RELEVE PART AU JOURNAL MEME QUAND LE TEST PASSE : c'est lui qui
	// permettra de recalibrer la table sans refaire l'instrument.
	FString LigneBrut, LigneUni;
	for (int32 K = 0; K <= 20; ++K)
	{
		const float P = static_cast<float>(K) / 20.0f;
		LigneBrut += FString::Printf(TEXT("%.4f, "), Quantile(Brut, P));
		LigneUni += FString::Printf(TEXT("%.3f "), Quantile(Uniforme, P));
	}
	AddInfo(FString::Printf(TEXT("quantiles du signal BRUT (pas de 5 %%) :\n    %s"), *LigneBrut));
	AddInfo(FString::Printf(TEXT("quantiles APRES uniformisation           :\n    %s"), *LigneUni));

	float Moyenne = 0.0f;
	for (const float S : Brut) { Moyenne += S; }
	Moyenne /= FMath::Max(Brut.Num(), 1);
	float Variance = 0.0f;
	for (const float S : Brut) { Variance += (S - Moyenne) * (S - Moyenne); }
	Variance /= FMath::Max(Brut.Num() - 1, 1);
	AddInfo(FString::Printf(
		TEXT("signal brut : moyenne %.4f, ecart-type %.4f, borne %.4f a %.4f"),
		Moyenne, FMath::Sqrt(Variance), Brut[0], Brut.Last()));

	// L'ASSERTION : apres uniformisation, le quantile P doit valoir P.
	//
	// LA TOLERANCE PORTE SUR LES DECILES ET NON SUR LA MOYENNE, parce que c'est
	// dans les QUEUES que le seuil de pluie travaille : une loi dont la moyenne
	// est juste et les queues fausses donne exactement le defaut qu'on corrige.
	float PireEcart = 0.0f;
	float PireP = 0.0f;
	for (int32 K = 1; K <= 19; ++K)
	{
		const float P = static_cast<float>(K) / 20.0f;
		const float E = FMath::Abs(Quantile(Uniforme, P) - P);
		if (E > PireEcart) { PireEcart = E; PireP = P; }
	}
	AddInfo(FString::Printf(
		TEXT("pire ecart a l'uniforme : %.4f, au quantile %.2f"), PireEcart, PireP));

	TestTrue(FString::Printf(
		TEXT("le signal uniformise est uniforme a 0,04 pres (pire ecart %.4f au quantile %.2f)"),
		PireEcart, PireP), PireEcart < 0.04f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
