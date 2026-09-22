// Worldseed - les vingt-trois climats reels dans notre diagramme.

#if WITH_DEV_AUTOMATION_TESTS

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedClimatsReels.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedRules.h"

#include "Misc/AutomationTest.h"

/**
 * LE SEUL CONTROLE QUI CONFRONTE LE MONDE A DES VALEURS EXTERIEURES AU PROJET.
 *
 * Un monde procedural peut etre parfaitement coherent avec lui-meme et faux par
 * rapport a la Terre : rien d'interne ne peut le dire. Les vingt-trois
 * prereglages climatiques d'Ultra Dynamic Sky sont des RELEVES DE STATIONS
 * REELLES, chacun citant sa source -- c'est la seule reference disponible, et
 * elle ne depend d'aucun de nos reglages.
 *
 * CE TEST DEPEND DE `world_rules.json`, ET C'EST UNE EXCEPTION ASSUMEE.
 * La regle de la fixture est qu'un test doit echouer quand le CODE casse, pas
 * quand une valeur physique bouge -- d'ou les mondes fictifs partout ailleurs.
 * Ici c'est l'inverse qui est voulu : le bulletin terrestre EST une mesure de
 * calage, et un recalibrage des seuils de biome DOIT rouvrir la question des
 * vingt-trois stations. Le test est donc ecrit pour qu'un tel echec soit
 * ACTIONNABLE : il nomme la station qui a bascule et dans quelle case, au lieu
 * d'annoncer qu'un compte a bouge.
 *
 * ET IL N'APPELLE AUCUNE COPIE. La lecture des releves, la table des attendus
 * et le seuil propre aux releves vivaient dans un namespace ANONYME de la
 * sonde, donc inatteignables : les recopier ici aurait valide une copie de la
 * lecture plutot que la lecture elle-meme. Ils ont ete extraits dans
 * `WorldseedClimatsReels`, que la sonde et ce test appellent tous deux -- meme
 * correction que celle du portage de `terre.py`, un cran plus haut.
 */
namespace
{
	/**
	 * Les trois echecs CONNUS, chacun avec sa raison.
	 *
	 * Ils ne sont pas masques : ils sont NOMMES. Un echec attendu qu'on se
	 * contente de soustraire d'un compte devient invisible le jour ou il change
	 * de nature, et un echec nouveau se fondrait dans le meme chiffre.
	 *
	 * - `Subarctic-Severe_Winter` tombe en toundra. Le portage applique la
	 *   LIMITE DES ARBRES, que la version Python n'appliquait pas du tout :
	 *   elle passait ce releve en ne le testant pas. Notre approximation du
	 *   mois le plus chaud par la moyenne de la saison d'ete est plus froide
	 *   que le vrai maximum mensuel, d'ou la bascule. Le releve est d'ailleurs
	 *   un cas dur connu -- il est PLUS FROID en moyenne (-11,6 C) que la
	 *   toundra polaire (-8,4) et porte pourtant de la taiga.
	 *
	 * - `Mediterranean_Cool_Summer` tombe en foret subtropicale humide. C'EST
	 *   UN DEFAUT REVELE PAR LE PORTAGE, pas un artefact : la foret
	 *   subtropicale humide, ajoutee APRES le mediterraneen, capture la case
	 *   avant que la surcharge mediterraneenne ne s'applique -- et celle-ci ne
	 *   regarde que `TemperateForest`, `Grassland` et `Steppe`. A reprendre.
	 *
	 * - `Cold_Semi-Arid` tombe en foret temperee. 610 mm a 6 degres EST une
	 *   foret dans un diagramme de Whittaker ; le releve gonfle sa pluie en
	 *   comptant l'equivalent-eau de la neige. Probablement un artefact du
	 *   releve, a ne pas corriger par un seuil.
	 */
	const TCHAR* const EchecsConnus[] = {
		TEXT("Subarctic-Severe_Winter"),
		TEXT("Mediterranean_Cool_Summer"),
		TEXT("Cold_Semi-Arid"),
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestClimatsReels,
	"Worldseed.Terre.ClimatsReels",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestClimatsReels::RunTest(const FString& Parameters)
{
	FString ErreurRegles;
	const UWorldseedRules* const Regles = WorldseedPipeline::GetRules(ErreurRegles);
	if (!TestNotNull(TEXT("world_rules.json se lit"), Regles))
	{
		AddError(FString::Printf(TEXT("regles illisibles : %s"), *ErreurRegles));
		return false;
	}

	// LA GEOMETRIE N'ENTRE QUE PAR DEUX PORTES, et aucune ne concerne les
	// releves : `HeightM` met a l'echelle le seuil d'altitude alpine, et `NY`
	// dimensionne une table de part estivale PAR LIGNE que les releves
	// n'empruntent pas -- ils apportent la leur. On prend malgre tout la
	// hauteur du monde de jeu, pour que ce test et la sonde lisent des regles
	// rigoureusement identiques.
	FWorldseedGeometry Geo;
	Geo.NY = 1024;
	Geo.NX = 2048;
	Geo.HeightM = 32000.0f;
	Geo.LatSpanDeg = 180.0f;
	Geo.LatitudeMapping = TEXT("equalArea");
	Geo.LatitudeEqualAreaBlend = 1.0f;

	const FWorldseedBiomeRules BioRegles =
		FWorldseedBiomeRules::FromRules(*Regles, Geo);

	TArray<FWorldseedReleveReel> Releves;
	FString ErreurReleves;
	if (!TestTrue(TEXT("les releves reels se lisent"),
		WorldseedClimatsReels::Charger(Releves, ErreurReleves)))
	{
		AddError(ErreurReleves);
		return false;
	}

	// LE FICHIER DOIT LES PORTER TOUS LES VINGT-TROIS. Un releve absent est
	// simplement omis par le chargeur : sans ce controle, un fichier ampute
	// rendrait « tout passe » sur les quelques stations restantes.
	if (!TestEqual(TEXT("les vingt-trois releves sont la"), Releves.Num(), 23))
	{
		return false;
	}

	TSet<FString> Attendus;
	for (const TCHAR* Cle : EchecsConnus) { Attendus.Add(Cle); }

	TSet<FString> Observes;
	for (const FWorldseedReleveReel& R : Releves)
	{
		const EWorldseedBiome Case = WorldseedClimatsReels::Classer(R, BioRegles);
		if (!R.Accepte(Case))
		{
			Observes.Add(R.Cle);
			AddInfo(FString::Printf(TEXT("%-34s %6.1f C %7.0f mm  ete %.3f  -> %s"),
				*R.Cle, R.TmoyC, R.PluieMm, R.FractionEte,
				WorldseedBiomes::Name(Case)));
		}
	}

	AddInfo(FString::Printf(TEXT("%d sur %d climats reels dans la case attendue"),
		Releves.Num() - Observes.Num(), Releves.Num()));

	// --- LE VERDICT SE LIT STATION PAR STATION, JAMAIS SUR UN COMPTE ---------
	//
	// Un simple « au moins vingt sur vingt-trois » laisserait passer un
	// ECHANGE : une station qui casse pendant qu'une autre se repare. Ce depot
	// a deja vu exactement cela -- l'ajout de la foret subtropicale humide a
	// fait passer deux releves et basculer deux autres, pour un score inchange
	// de 19 sur 23. Le compte n'avait rien vu.
	for (const FString& Cle : Observes)
	{
		if (!Attendus.Contains(Cle))
		{
			AddError(FString::Printf(
				TEXT("REGRESSION : %s ne tombe plus dans sa case attendue. ")
				TEXT("Si le changement est voulu, corriger la table des attendus ")
				TEXT("dans WorldseedClimatsReels.cpp ; sinon, c'est un seuil de ")
				TEXT("biome qui a bouge."), *Cle));
		}
	}

	for (const FString& Cle : Attendus)
	{
		if (!Observes.Contains(Cle))
		{
			AddError(FString::Printf(
				TEXT("PROGRES NON ENREGISTRE : %s tombe desormais juste. ")
				TEXT("Le retirer de EchecsConnus, et dire dans le commit ce qui ")
				TEXT("l'a corrige."), *Cle));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
