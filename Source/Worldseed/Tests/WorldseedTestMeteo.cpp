// Worldseed - la meteo suit-elle le climat du lieu ?

#if WITH_DEV_AUTOMATION_TESTS

#include "Procedural/WorldseedClimatePreset.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedWeatherState.h"

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
	TestTrue(TEXT("la foret tropicale est arrosee plus d'un tiers du temps"),
		Tropique > 0.33f);

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

#endif // WITH_DEV_AUTOMATION_TESTS
