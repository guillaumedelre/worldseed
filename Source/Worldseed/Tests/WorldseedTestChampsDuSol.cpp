// Worldseed - l'humidite du sol et l'ensoleillement : deux parts, donc deux
// grandeurs qui doivent rester dans [0,1] et repondre dans le bon sens.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedFields.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedRules.h"

#include "Misc/AutomationTest.h"

namespace
{
	int32 CompterHorsPart(const TArray<float>& A)
	{
		int32 N = 0;
		for (const float V : A)
		{
			if (FMath::IsNaN(V) || !FMath::IsFinite(V) || V < 0.0f || V > 1.0f)
			{
				++N;
			}
		}
		return N;
	}

	double Moyenne(const TArray<float>& A)
	{
		if (A.Num() == 0) { return 0.0; }
		double S = 0.0;
		for (const float V : A) { S += V; }
		return S / A.Num();
	}
}

/**
 * LES DEUX CHAMPS SONT DES PARTS, ET ILS COUVRENT TOUTE LA GRILLE.
 *
 * Ils nourrissent les courbes de tolerance par espece -- humidite du sol,
 * ensoleillement -- qui sont la doctrine de placement de la vegetation arretee
 * par le proprietaire : « des courbes sur les champs continus, et non des
 * listes par biome ». Une valeur hors [0,1] y deviendrait une tolerance
 * negative ou saturee, donc une espece absente ou partout, sans une erreur.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestChampsDuSolBornes,
	"Worldseed.ChampsDuSol.SontDesParts",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestChampsDuSolBornes::RunTest(const FString& Parameters)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("world_rules.json se lit"), R))
	{
		AddError(Erreur);
		return false;
	}

	const FWorldseedGeometry G = WorldseedTest::Geometrie(48);
	const TArray<float> Relief = WorldseedTest::Relief(G);
	TArray<float> Pluie;
	Pluie.Init(900.0f, G.CellCount());

	FWorldseedGroundFields Champs;
	WorldseedFields::Compute(G, Relief, Pluie,
		FWorldseedGroundRules::FromRules(*R), Champs);

	TestTrue(TEXT("les deux champs couvrent la grille"),
		Champs.IsValid(G.CellCount()));
	TestEqual(TEXT("l'humidite du sol reste dans [0,1]"),
		CompterHorsPart(Champs.SoilMoisture01), 0);
	TestEqual(TEXT("l'ensoleillement reste dans [0,1]"),
		CompterHorsPart(Champs.SunExposure01), 0);

	const double MH = Moyenne(Champs.SoilMoisture01);
	const double ME = Moyenne(Champs.SunExposure01);
	AddInfo(FString::Printf(
		TEXT("moyennes -- humidite %.3f, ensoleillement %.3f"), MH, ME));

	// TEMOINS : deux champs identiquement nuls, ou identiquement pleins,
	// passeraient tout ce qui precede. Ce depot a paye quatre fixtures muettes
	// en une journee.
	TestTrue(TEXT("TEMOIN : l'humidite n'est ni nulle ni saturee partout"),
		MH > 0.01 && MH < 0.99);
	TestTrue(TEXT("TEMOIN : l'ensoleillement non plus"),
		ME > 0.01 && ME < 0.99);

	return true;
}

/**
 * PLUS IL PLEUT, PLUS LE SOL EST HUMIDE.
 *
 * Le sens du terme, et rien d'autre. S'il s'inversait, la vegetation
 * s'installerait dans les deserts et manquerait dans les forets -- et comme
 * les parts de biomes sont calees a part, aucune metrique du depot ne le
 * dirait : le bulletin terrestre lit le CLIMAT, pas l'humidite du sol.
 *
 * ON COMPARE DEUX MONDES IDENTIQUES A LA PLUIE PRES, ce qui isole le terme.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestChampsDuSolSens,
	"Worldseed.ChampsDuSol.LaPluieHumidifie",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestChampsDuSolSens::RunTest(const FString& Parameters)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("world_rules.json se lit"), R))
	{
		AddError(Erreur);
		return false;
	}

	const FWorldseedGroundRules GR = FWorldseedGroundRules::FromRules(*R);
	const FWorldseedGeometry G = WorldseedTest::Geometrie(48);
	const TArray<float> Relief = WorldseedTest::Relief(G);

	TArray<float> Aride, Arrosee;
	Aride.Init(100.0f, G.CellCount());
	Arrosee.Init(2500.0f, G.CellCount());

	FWorldseedGroundFields Sec, Humide;
	WorldseedFields::Compute(G, Relief, Aride, GR, Sec);
	WorldseedFields::Compute(G, Relief, Arrosee, GR, Humide);

	const double MSec = Moyenne(Sec.SoilMoisture01);
	const double MHumide = Moyenne(Humide.SoilMoisture01);

	AddInfo(FString::Printf(
		TEXT("humidite moyenne -- 100 mm : %.3f, 2500 mm : %.3f"), MSec, MHumide));

	TestTrue(TEXT("un monde arrose a un sol plus humide qu'un monde aride"),
		MHumide > MSec);

	// L'ENSOLEILLEMENT NE DOIT PAS SUIVRE LA PLUIE : il se lit sur le relief --
	// pente et exposition. S'il bougeait ici, les deux champs seraient couples
	// alors qu'ils decrivent deux choses independantes.
	const double ESec = Moyenne(Sec.SunExposure01);
	const double EHumide = Moyenne(Humide.SunExposure01);
	AddInfo(FString::Printf(
		TEXT("ensoleillement moyen -- %.4f contre %.4f"), ESec, EHumide));
	TestTrue(TEXT("l'ensoleillement ne depend pas de la pluie"),
		FMath::Abs(ESec - EHumide) < 1.0e-4);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
