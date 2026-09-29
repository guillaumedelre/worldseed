// Worldseed - la tempete qui passe convient-elle au climat du lieu ?

#if WITH_DEV_AUTOMATION_TESTS

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedClimatePreset.h"
#include "Procedural/WorldseedOrage.h"

#include "Misc/AutomationTest.h"

/**
 * CE QUE CES ORACLES PROTEGENT.
 *
 * Le pack livrait une regle purement SAISONNIERE -- releve du 29 septembre
 * 2026 : ses quatre tables de probabilite ne portent qu'une entree chacune, a
 * probabilite 1,0, soit `Rain_Thunderstorm` trois saisons sur quatre et
 * `Snow_Blizzard` en hiver. Ce n'etait donc pas un tirage au sort, mais une
 * regle qui IGNORE LE LIEU : un blizzard en hiver a l'equateur, un orage dans
 * le desert.
 *
 * ET UN DEFAUT DE CE GENRE NE SE REMARQUE PAS. Il faut aller au bon endroit a
 * la bonne saison et attendre huit a dix-sept minutes qu'une tempete naisse.
 * C'est exactement la famille de defaut qui survit des mois -- comme les merles
 * sur la banquise que les oracles d'ambiance gardent.
 *
 * LA DECISION EST UNE FONCTION PURE, donc testable sans rien instancier : meme
 * decoupage que l'ambiance et la reptation, et pour la meme raison -- c'est la
 * DECISION qui porte les pieges, pas son effet.
 */
namespace
{
	EWorldseedOrage En(EWorldseedBiome B,
		EWorldseedSeason S = EWorldseedSeason::Summer)
	{
		return WorldseedOrage::Choisir(B, S);
	}

	const EWorldseedSeason Hiver = EWorldseedSeason::Winter;
	const EWorldseedSeason Ete = EWorldseedSeason::Summer;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedOrageLeLieuCompte,
	"Worldseed.Orage.LeLieuCompte",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedOrageLeLieuCompte::RunTest(const FString&)
{
	// LES DEUX DEFAUTS NOMMES DU PACK, ET C'EST LE COEUR DE CE CHANTIER.
	//
	// EN HIVER -- la saison ou le pack posait un blizzard PARTOUT.
	TestTrue(TEXT("l'equateur ne connait pas le blizzard, meme en hiver"),
		En(EWorldseedBiome::TropicalRainforest, Hiver) == EWorldseedOrage::Orage);
	TestTrue(TEXT("le desert chaud non plus"),
		En(EWorldseedBiome::HotDesert, Hiver) == EWorldseedOrage::Sable);

	// EN ETE -- la saison ou le pack posait un orage PARTOUT.
	TestTrue(TEXT("la calotte glaciaire ne fait pas d'orage, meme en ete"),
		En(EWorldseedBiome::IceCap, Ete) == EWorldseedOrage::Blizzard);
	TestTrue(TEXT("le desert non plus : sa tempete est de sable"),
		En(EWorldseedBiome::HotDesert, Ete) == EWorldseedOrage::Sable);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedOrageLaSaisonCompteAUSSI,
	"Worldseed.Orage.LaSaisonCompteAUSSI",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedOrageLaSaisonCompteAUSSI::RunTest(const FString&)
{
	// ON N'A PAS JETE CE QUE LE PACK FAISAIT BIEN. Pour les temperes
	// continentaux, sa regle saisonniere avait RAISON : orage l'ete, neige
	// l'hiver. La faute etait de l'appliquer AUSSI a l'equateur et au desert.
	//
	// SANS CET ORACLE, une regle qui ne regarderait QUE le biome passerait le
	// precedent en entier -- et l'on aurait remplace un defaut par l'autre.
	TestTrue(TEXT("foret temperee en ete : orage"),
		En(EWorldseedBiome::TemperateForest, Ete) == EWorldseedOrage::Orage);
	TestTrue(TEXT("foret temperee en hiver : neige"),
		En(EWorldseedBiome::TemperateForest, Hiver) == EWorldseedOrage::Neige);

	// ET LA OU LA SAISON NE DOIT PAS COMPTER, elle ne compte pas -- c'est une
	// affirmation aussi forte que l'inverse. Un hiver oceanique est DOUX et
	// arrose : il pleut, il ne neige pas.
	TestTrue(TEXT("oceanique en hiver : de la pluie, pas de la neige"),
		En(EWorldseedBiome::TemperateRainforest, Hiver) == EWorldseedOrage::Pluie);
	TestTrue(TEXT("oceanique en ete : la meme pluie"),
		En(EWorldseedBiome::TemperateRainforest, Ete) == EWorldseedOrage::Pluie);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedOrageRienSurLEauEtJamaisDeTrou,
	"Worldseed.Orage.RienSurLEauEtJamaisDeTrou",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedOrageRienSurLEauEtJamaisDeTrou::RunTest(const FString&)
{
	// L'EAU LIBRE NE RECOIT RIEN : une tempete radiale nait autour du JOUEUR, et
	// le joueur ne nage pas. Les trois couvertures d'eau, aux quatre saisons.
	for (const EWorldseedBiome B : { EWorldseedBiome::Ocean,
			EWorldseedBiome::Lake, EWorldseedBiome::River })
	{
		for (int32 s = 0; s < static_cast<int32>(EWorldseedSeason::Count); ++s)
		{
			TestTrue(*FString::Printf(
					TEXT("biome d'eau %d, saison %d : aucune"),
					static_cast<int32>(B), s),
				WorldseedOrage::Choisir(B, static_cast<EWorldseedSeason>(s))
					== EWorldseedOrage::Aucune);
		}
	}

	// ET AUCUN BIOME DE TERRE NE TOMBE DANS UN TROU. Le balayage est exhaustif
	// a dessein : un biome ajoute en fin d'enumeration -- ce que ce depot fait
	// regulierement -- doit faire QUELQUE CHOSE plutot que rien, sinon son
	// silence se lirait comme un defaut du mecanisme.
	for (int32 b = 0; b < static_cast<int32>(EWorldseedBiome::Count); ++b)
	{
		const EWorldseedBiome Biome = static_cast<EWorldseedBiome>(b);
		if (Biome == EWorldseedBiome::Ocean || Biome == EWorldseedBiome::Lake
			|| Biome == EWorldseedBiome::River)
		{
			continue;
		}
		for (int32 s = 0; s < static_cast<int32>(EWorldseedSeason::Count); ++s)
		{
			const EWorldseedOrage Choix = WorldseedOrage::Choisir(
				Biome, static_cast<EWorldseedSeason>(s));
			TestTrue(*FString::Printf(
					TEXT("biome de terre %d, saison %d : un choix reel"), b, s),
				Choix != EWorldseedOrage::Aucune
					&& Choix != EWorldseedOrage::Count);

			// ET CHAQUE CHOIX NOMME UN ASSET REEL. Une decision qui rendrait un
			// nom vide echouerait au chargement, en silence.
			const TCHAR* const Nom = WorldseedOrage::NomDAsset(Choix);
			TestTrue(*FString::Printf(
					TEXT("biome %d, saison %d : l'asset est nomme"), b, s),
				Nom != nullptr && *Nom != TEXT('\0'));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
