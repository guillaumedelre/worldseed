// Worldseed - le son d'ambiance joue-t-il la ou il faut, et nulle part ailleurs ?

#if WITH_DEV_AUTOMATION_TESTS

#include "Procedural/WorldseedAmbiance.h"
#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedRules.h"

#include "Misc/AutomationTest.h"

/**
 * CE QUE CES ORACLES PROTEGENT.
 *
 * La seule source d'ambiance du pack est une FORET. La jouer au mauvais endroit
 * ne produit pas un silence -- ce qui se remarquerait -- mais des merles sur la
 * banquise, ce qui ne se remarque que si l'on y va. Un defaut de placement
 * sonore est donc exactement le genre de chose qui survit des mois : audible
 * seulement la ou personne ne passe.
 *
 * ET LE PIEGE PRINCIPAL EST UN PIEGE CONNU DE CE DEPOT. Le biome CLIMATIQUE est
 * defini PARTOUT, mer comprise -- au large d'une foret tropicale il vaut
 * « foret tropicale », puisqu'il decrit la bande climatique de l'eau. Lire le
 * biome sans lire la COUVERTURE ferait chanter les oiseaux en pleine mer, et
 * c'est exactement ce qui avait fait annoncer a la nappe RVT « 8 388 608 terre
 * / 0 mer » sur un monde couvert d'ocean a 71 %.
 *
 * LA DECISION EST UNE FONCTION PURE, donc testable sans rien instancier --
 * meme decoupage que la reptation, et pour la meme raison : c'est la decision
 * qui porte les pieges, pas son effet.
 */
namespace
{
	EWorldseedAmbiance Sur(EWorldseedBiome B,
		EWorldseedCover C = EWorldseedCover::None)
	{
		return WorldseedAmbiance::Choisir(B, C);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedAmbianceLaForetChante,
	"Worldseed.Ambiance.LaForetChante",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedAmbianceLaForetChante::RunTest(const FString&)
{
	// LES QUATRE FORETS HUMIDES sont le seul endroit ou la source du pack --
	// des feuillus, des oiseaux chanteurs, un vent de canopee -- est chez elle.
	for (const EWorldseedBiome B : {
			EWorldseedBiome::TropicalRainforest,
			EWorldseedBiome::TemperateRainforest,
			EWorldseedBiome::TemperateForest,
			EWorldseedBiome::SubtropicalForest })
	{
		TestEqual(*FString::Printf(TEXT("biome %d : foret dense"), static_cast<int32>(B)),
			static_cast<int32>(Sur(B)),
			static_cast<int32>(EWorldseedAmbiance::ForetDense));
	}

	for (const EWorldseedBiome B : {
			EWorldseedBiome::Taiga,
			EWorldseedBiome::TropicalDryForest,
			EWorldseedBiome::Marsh,
			EWorldseedBiome::Mediterranean })
	{
		TestEqual(*FString::Printf(TEXT("biome %d : foret claire"), static_cast<int32>(B)),
			static_cast<int32>(Sur(B)),
			static_cast<int32>(EWorldseedAmbiance::ForetClaire));
	}

	for (const EWorldseedBiome B : {
			EWorldseedBiome::Savanna,
			EWorldseedBiome::Grassland,
			EWorldseedBiome::Steppe })
	{
		TestEqual(*FString::Printf(TEXT("biome %d : lisiere"), static_cast<int32>(B)),
			static_cast<int32>(Sur(B)),
			static_cast<int32>(EWorldseedAmbiance::Lisiere));
	}

	// ET LE CONTRE-EXEMPLE, SANS LEQUEL LE TEST NE DISTINGUE RIEN. Une fonction
	// qui rendrait « foret dense » PARTOUT passerait les trois boucles du
	// dessus si elles etaient seules a juger -- ce depot a paye quatre fixtures
	// muettes en une journee, dont deux qui comparaient un vide a un vide.
	TestNotEqual(TEXT("le desert chaud ne rend pas la meme chose qu'une foret"),
		static_cast<int32>(Sur(EWorldseedBiome::HotDesert)),
		static_cast<int32>(Sur(EWorldseedBiome::TropicalRainforest)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedAmbianceLeDesertSeTait,
	"Worldseed.Ambiance.LeDesertSeTait",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedAmbianceLeDesertSeTait::RunTest(const FString&)
{
	// AUCUN DE CES ENDROITS N'A D'ARBRES, et c'est le critere : la source du
	// pack fait entendre du vent dans des BRANCHES. La toundra est le cas qui
	// se discute -- elle a de vrais oiseaux l'ete -- mais elle se definit par
	// l'absence d'arbres, ce qui est precisement ce qu'on entendrait.
	for (const EWorldseedBiome B : {
			EWorldseedBiome::HotDesert,
			EWorldseedBiome::ColdDesert,
			EWorldseedBiome::IceCap,
			EWorldseedBiome::Tundra,
			EWorldseedBiome::Alpine,
			EWorldseedBiome::BareRock })
	{
		TestEqual(*FString::Printf(TEXT("biome %d : aucune ambiance"),
				static_cast<int32>(B)),
			static_cast<int32>(Sur(B)),
			static_cast<int32>(EWorldseedAmbiance::Aucune));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedAmbianceLOceanNaPasDOiseaux,
	"Worldseed.Ambiance.LOceanNaPasDOiseaux",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedAmbianceLOceanNaPasDOiseaux::RunTest(const FString&)
{
	// C'EST L'ORACLE QUI COMPTE LE PLUS, et c'est celui qu'un test naif
	// n'ecrirait pas : on lui donne le biome LE PLUS CHANTANT du monde, et une
	// couverture d'eau. Le biome climatique etant defini partout, il vaut bien
	// « foret tropicale » a deux cents metres du rivage -- lire le biome seul
	// ferait donc chanter les merles en nageant.
	for (const EWorldseedCover C : {
			EWorldseedCover::Ocean,
			EWorldseedCover::Lake,
			EWorldseedCover::River,
			EWorldseedCover::SeaIce })
	{
		TestEqual(*FString::Printf(TEXT("couverture %d sur foret tropicale : silence"),
				static_cast<int32>(C)),
			static_cast<int32>(Sur(EWorldseedBiome::TropicalRainforest, C)),
			static_cast<int32>(EWorldseedAmbiance::Aucune));
	}

	// LE TEMOIN QUI PROUVE QUE LA COUVERTURE EST BIEN CE QUI DECIDE : le MEME
	// biome, sans couverture, doit chanter. Sans cette ligne, une fonction qui
	// rendrait « aucune » partout passerait le test.
	TestEqual(TEXT("temoin : la meme foret, a sec, chante"),
		static_cast<int32>(Sur(EWorldseedBiome::TropicalRainforest)),
		static_cast<int32>(EWorldseedAmbiance::ForetDense));

	// L'ESTRAN SE TAIT AUSSI, faute de la bonne source : le pack ne livre ni
	// ressac ni oiseau de mer, et jouer la foret sur le sable ferait chanter
	// des passereaux au bord de l'eau.
	TestEqual(TEXT("l'estran se tait"),
		static_cast<int32>(Sur(EWorldseedBiome::TropicalRainforest,
			EWorldseedCover::Beach)),
		static_cast<int32>(EWorldseedAmbiance::Aucune));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedAmbianceLaParoiDescendDunCran,
	"Worldseed.Ambiance.LaParoiDescendDunCran",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedAmbianceLaParoiDescendDunCran::RunTest(const FString&)
{
	// UNE PAROI N'EST PAS HORS DU MONDE. Un versant raide au milieu d'une
	// foret tropicale reste climatiquement de la foret, et les oiseaux du
	// versant d'en face s'entendent : c'est exactement le raisonnement qui a
	// fait de `Rock` une COUVERTURE et non un biome, pour que la carte ne perde
	// pas le climat de la paroi.
	TestEqual(TEXT("paroi en foret dense -> foret claire"),
		static_cast<int32>(Sur(EWorldseedBiome::TropicalRainforest,
			EWorldseedCover::Rock)),
		static_cast<int32>(EWorldseedAmbiance::ForetClaire));

	TestEqual(TEXT("paroi en foret claire -> lisiere"),
		static_cast<int32>(Sur(EWorldseedBiome::Taiga, EWorldseedCover::Rock)),
		static_cast<int32>(EWorldseedAmbiance::Lisiere));

	TestEqual(TEXT("paroi en lisiere -> silence"),
		static_cast<int32>(Sur(EWorldseedBiome::Savanna, EWorldseedCover::Rock)),
		static_cast<int32>(EWorldseedAmbiance::Aucune));

	// ET LA PAROI NE RESSUSCITE JAMAIS UN SILENCE : un versant de desert reste
	// muet, sans quoi la descente d'un cran deviendrait une montee.
	TestEqual(TEXT("paroi en desert -> silence"),
		static_cast<int32>(Sur(EWorldseedBiome::HotDesert, EWorldseedCover::Rock)),
		static_cast<int32>(EWorldseedAmbiance::Aucune));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedAmbianceChaqueFamilleAUnChemin,
	"Worldseed.Ambiance.ChaqueFamilleAUnChemin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedAmbianceChaqueFamilleAUnChemin::RunTest(const FString&)
{
	// CET ORACLE DEPEND DU FICHIER DE REGLES, ET C'EST ASSUME : c'est un test
	// de CABLAGE, pas de calcul. Une famille sans chemin ne produirait aucune
	// erreur -- elle jouerait le silence, exactement comme `Aucune` -- donc le
	// defaut serait muet, et c'est pour cela qu'il faut un test.
	FString Erreur;
	const UWorldseedRules* const Regles = WorldseedPipeline::GetRules(Erreur);
	if (!Regles)
	{
		AddError(FString::Printf(TEXT("regles illisibles : %s"), *Erreur));
		return false;
	}

	const FWorldseedAmbianceRegles R = FWorldseedAmbianceRegles::FromRules(*Regles);

	for (const EWorldseedAmbiance F : {
			EWorldseedAmbiance::Lisiere,
			EWorldseedAmbiance::ForetClaire,
			EWorldseedAmbiance::ForetDense })
	{
		TestTrue(*FString::Printf(TEXT("« %s » a un chemin"),
				WorldseedAmbiance::Nom(F)),
			!R.Chemin(F).IsEmpty());
	}

	// ET `Aucune` N'EN A PAS, ce qui n'est pas un oubli mais la facon dont le
	// pack arrete une ambiance : « stop environment sounds by calling it with
	// no environment sound asset selected ».
	TestTrue(TEXT("« aucune » n'a pas de chemin, et c'est ainsi qu'on l'arrete"),
		R.Chemin(EWorldseedAmbiance::Aucune).IsEmpty());

	// LES TROIS CHEMINS SONT DISTINCTS. Trois familles pointant sur le meme
	// asset rendraient les dosages inoperants sans qu'une ligne ne bouge -- le
	// genre de faute qu'un copier-coller de cle produit et qu'on n'entend pas.
	TestTrue(TEXT("les trois chemins different"),
		R.Chemin(EWorldseedAmbiance::Lisiere) != R.Chemin(EWorldseedAmbiance::ForetClaire)
		&& R.Chemin(EWorldseedAmbiance::ForetClaire) != R.Chemin(EWorldseedAmbiance::ForetDense)
		&& R.Chemin(EWorldseedAmbiance::Lisiere) != R.Chemin(EWorldseedAmbiance::ForetDense));

	// LE FONDU DOIT ETRE LONG. A zero, la coupure se remarquerait a chaque
	// frontiere de biome ; au-dela d'une minute, on n'entendrait jamais le
	// changement. Les bornes sont larges : c'est une garde, pas un calage.
	TestTrue(*FString::Printf(TEXT("fondu %.1f s dans [1, 60]"), R.FonduS),
		R.FonduS >= 1.0f && R.FonduS <= 60.0f);

	AddInfo(FString::Printf(TEXT("dense=%s claire=%s lisiere=%s fondu=%.1f s"),
		*R.Chemin(EWorldseedAmbiance::ForetDense),
		*R.Chemin(EWorldseedAmbiance::ForetClaire),
		*R.Chemin(EWorldseedAmbiance::Lisiere), R.FonduS));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
