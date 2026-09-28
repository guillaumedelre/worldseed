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
