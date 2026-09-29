// Worldseed - ce qui rampe au sol court-il la ou il faut, et nulle part ailleurs ?

#if WITH_DEV_AUTOMATION_TESTS

#include "Procedural/WorldseedClimatePreset.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedReptation.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedWeatherState.h"

#include "Misc/AutomationTest.h"

/**
 * CE QUE CES ORACLES PROTEGENT.
 *
 * La reptation est une fonction PURE du lieu et de la meteo : elle ne touche ni
 * au monde ni au moteur, et se teste donc sans rien instancier. C'est
 * deliberement l'inverse du cablage du ciel, qui a vecu des semaines sans
 * appelant parce que personne ne pouvait le tester -- ici la DECISION est
 * separee de son EFFET, et c'est la decision qui porte tous les pieges.
 */
namespace
{
	/** Un vent donne, tout le reste au repos. */
	FWorldseedWeather Vent(float Intensite, float Neige = 0.0f)
	{
		FWorldseedWeather W;
		W.WindIntensity = Intensite;
		W.Snow = Neige;
		W.WindDirectionDeg = 90.0f;
		return W;
	}

	constexpr float SolSec = 10000.0f;     // 100 m, largement au-dessus de la mer
	constexpr float NiveauMer = 0.0f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedReptationSableCourtDansLeDesert,
	"Worldseed.Reptation.LeSableCourtDansLeDesert",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedReptationSableCourtDansLeDesert::RunTest(const FString&)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("les regles se chargent"), R)) { AddError(Erreur); return false; }
	const FWorldseedClimatePresetRules M = FWorldseedClimatePresetRules::FromRules(*R);
	const FWorldseedReptationRules Regles = FWorldseedReptationRules::FromRules(*R);

	const FWorldseedReptation Tempete = WorldseedReptation::Evaluer(
		EWorldseedBiome::HotDesert, EWorldseedCover::None,
		SolSec, NiveauMer, Vent(10.0f), M, Regles);

	const FWorldseedReptation Calme = WorldseedReptation::Evaluer(
		EWorldseedBiome::HotDesert, EWorldseedCover::None,
		SolSec, NiveauMer, Vent(2.0f), M, Regles);

	AddInfo(FString::Printf(TEXT("desert : vent 10 -> intensite %.3f ; vent 2 -> %.3f"),
		Tempete.Intensite, Calme.Intensite));

	TestTrue(TEXT("le sable court sous un vent de tempete"),
		Tempete.Matiere == EWorldseedMatiereRampante::Sable && Tempete.Intensite > 0.5f);

	// LE TEMOIN DANS L'AUTRE SENS, sans lequel « le sable court » se
	// satisferait d'un sable qui court TOUT LE TEMPS : une plage par temps
	// calme ne rampe pas, c'est le vent qui souleve le grain.
	TestTrue(TEXT("mais pas par temps calme"), !Calme.EstActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedReptationRienNeRampeSurLEau,
	"Worldseed.Reptation.RienNeRampeSurLEau",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedReptationRienNeRampeSurLEau::RunTest(const FString&)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("les regles se chargent"), R)) { AddError(Erreur); return false; }
	const FWorldseedClimatePresetRules M = FWorldseedClimatePresetRules::FromRules(*R);
	const FWorldseedReptationRules Regles = FWorldseedReptationRules::FromRules(*R);

	// C'EST LE PIEGE QUE LA TABLE DES MATIERES TEND, et il est reel : `Ocean`
	// porte un poids « aride » de 0,55 -- la convention de peinture du FOND
	// MARIN, pas du sable. Sans la garde, le sable courrait sur l'eau.
	AddInfo(FString::Printf(TEXT("poids « aride » de l'ocean : %.2f (ce n'est PAS du sable)"),
		WorldseedBiomes::SlotWeights(EWorldseedBiome::Ocean).G));

	const FWorldseedReptation SurOcean = WorldseedReptation::Evaluer(
		EWorldseedBiome::HotDesert, EWorldseedCover::Ocean,
		SolSec, NiveauMer, Vent(10.0f), M, Regles);

	const FWorldseedReptation SousLaMer = WorldseedReptation::Evaluer(
		EWorldseedBiome::HotDesert, EWorldseedCover::None,
		-500.0f, NiveauMer, Vent(10.0f), M, Regles);

	TestTrue(TEXT("rien ne rampe sur une couverture d'eau"), !SurOcean.EstActive());

	// ET L'ALTITUDE AUSSI, parce qu'une cellule peut etre sous le niveau de la
	// mer sans porter de couverture d'eau.
	TestTrue(TEXT("ni sous le niveau de la mer"), !SousLaMer.EstActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedReptationNeigePasseAvantLeSable,
	"Worldseed.Reptation.LaNeigePasseAvantLeSable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedReptationNeigePasseAvantLeSable::RunTest(const FString&)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("les regles se chargent"), R)) { AddError(Erreur); return false; }
	const FWorldseedClimatePresetRules M = FWorldseedClimatePresetRules::FromRules(*R);
	const FWorldseedReptationRules Regles = FWorldseedReptationRules::FromRules(*R);

	// UNE CALOTTE : de la neige permanente, sans qu'il neige.
	const FWorldseedReptation Calotte = WorldseedReptation::Evaluer(
		EWorldseedBiome::IceCap, EWorldseedCover::None,
		SolSec, NiveauMer, Vent(10.0f), M, Regles);

	// ET UN DESERT SOUS LA NEIGE : la neige POSEE doit gagner sur le sable,
	// puisque c'est elle qu'on voit courir. C'est le cas qui prouve l'ordre.
	const FWorldseedReptation DesertEnneige = WorldseedReptation::Evaluer(
		EWorldseedBiome::HotDesert, EWorldseedCover::None,
		SolSec, NiveauMer, Vent(10.0f, /*Neige=*/8.0f), M, Regles);

	AddInfo(FString::Printf(TEXT("calotte : matiere %d, intensite %.3f  |  desert enneige : matiere %d"),
		static_cast<int32>(Calotte.Matiere), Calotte.Intensite,
		static_cast<int32>(DesertEnneige.Matiere)));

	TestTrue(TEXT("la neige d'une calotte rampe"),
		Calotte.Matiere == EWorldseedMatiereRampante::Neige && Calotte.Intensite > 0.5f);

	TestTrue(TEXT("et elle passe AVANT le sable quand elle le recouvre"),
		DesertEnneige.Matiere == EWorldseedMatiereRampante::Neige);

	// LES DEUX MATIERES PARTAGENT LA MECANIQUE, et c'est ce qui rend le systeme
	// transportable : meme vent, meme arrachement, seule la matiere change.
	const FWorldseedReptation Sable = WorldseedReptation::Evaluer(
		EWorldseedBiome::HotDesert, EWorldseedCover::None,
		SolSec, NiveauMer, Vent(10.0f), M, Regles);
	TestEqual(TEXT("a part pleine et vent egal, les deux rampent pareil"),
		Calotte.Intensite, Sable.Intensite, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedReptationLaForetNeRampePas,
	"Worldseed.Reptation.LaForetNeRampePas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedReptationLaForetNeRampePas::RunTest(const FString&)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("les regles se chargent"), R)) { AddError(Erreur); return false; }
	const FWorldseedClimatePresetRules M = FWorldseedClimatePresetRules::FromRules(*R);
	const FWorldseedReptationRules Regles = FWorldseedReptationRules::FromRules(*R);

	// UN GRAIN LIBRE ET SEC, OU RIEN. Une foret a un sol tenu par les racines et
	// couvert de litiere ; une savane a du sable entre ses herbes, mais pas
	// assez pour qu'il coure. Le seuil est donc haut, et c'est voulu.
	for (const EWorldseedBiome B : {
			EWorldseedBiome::TropicalRainforest, EWorldseedBiome::TemperateForest,
			EWorldseedBiome::Savanna, EWorldseedBiome::Grassland,
			EWorldseedBiome::BareRock })
	{
		const FWorldseedReptation Rep = WorldseedReptation::Evaluer(
			B, EWorldseedCover::None, SolSec, NiveauMer, Vent(10.0f), M, Regles);
		TestTrue(FString::Printf(TEXT("le biome %d ne rampe pas (aride %.2f)"),
			static_cast<int32>(B), WorldseedBiomes::SlotWeights(B).G),
			!Rep.EstActive());
	}

	// ET LE TEMOIN, sans lequel « rien ne rampe » serait satisfait par une
	// fonction qui ne rend JAMAIS rien -- ce que ce depot a paye plusieurs fois
	// sur des fixtures muettes.
	const FWorldseedReptation Plage = WorldseedReptation::Evaluer(
		EWorldseedBiome::Beach, EWorldseedCover::Beach,
		SolSec, NiveauMer, Vent(10.0f), M, Regles);
	TestTrue(TEXT("mais une plage, si"),
		Plage.Matiere == EWorldseedMatiereRampante::Sable);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
