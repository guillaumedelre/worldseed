// Worldseed - les mesas et les canyons : ce que la passe n'a pas le droit de
// faire au relief.
//
// SEPT CENT VINGT-NEUF LIGNES SANS AUCUN ORACLE jusqu'ici, et le registre le
// signalait : « la passe littorale, les mesas et canyons » figuraient en tete
// de ce qui restait sans test. Or elle modifie le relief EN PLACE, apres
// l'erosion et avant la seconde passe de climat -- donc tout ce qui suit,
// temperature et biomes compris, voit ce qu'elle a fait.
//
// LES DEUX INVARIANTS TESTES ICI SONT CEUX QUE SON PROPRE EN-TETE PROMET, et
// ils ne se verifient pas a l'oeil : un relief legerement releve ou quelques
// cellules de terre passees sous la mer ne se distinguent de rien.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedFins.h"
#include "Procedural/WorldseedLithology.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedPlateau.h"
#include "Procedural/WorldseedStrata.h"

#include "Misc/AutomationTest.h"

namespace
{
	/**
	 * Un relief de PLATEAU, et non celui de la fixture commune.
	 *
	 * IL FAUT QUE LA PASSE AIT DE QUOI MORDRE. Ses gardes sont nombreuses --
	 * altitude, pluie, relief local, durete, masque de region -- et le registre
	 * garde la lecon : « une couverture trop faible peut venir de six gardes
	 * differentes ». Un relief qui n'en passerait aucune rendrait un test qui
	 * ne mesure rien, et qui passerait quoi qu'il arrive.
	 *
	 * On pose donc une vaste table haute et plate, ce que la passe cherche
	 * precisement : « une mesa est le RESTE d'une plaine, et sans plaine il n'y
	 * a rien a dissequer ».
	 */
	TArray<float> ReliefDePlateau(const FWorldseedGeometry& G)
	{
		TArray<float> H;
		H.SetNumUninitialized(G.CellCount());
		for (int32 J = 0; J < G.NY; ++J)
		{
			for (int32 I = 0; I < G.NX; ++I)
			{
				const float U = static_cast<float>(I) / static_cast<float>(G.NX);
				const float V = static_cast<float>(J) / static_cast<float>(G.NY - 1);

				// Un plateau a 400 m, tres legerement ondule pour que le relief
				// local ne soit pas rigoureusement nul, et une bordure basse.
				const float Bord = FMath::Min(
					FMath::Min(U, 1.0f - U), FMath::Min(V, 1.0f - V));
				H[J * G.NX + I] = 400.0f
					+ 12.0f * FMath::Sin(U * 8.0f * PI)
					+ 9.0f * FMath::Cos(V * 6.0f * PI + 0.4f)
					- 900.0f * FMath::Max(0.0f, 0.12f - Bord);
			}
		}
		return H;
	}

	/** Une lithologie entierement sedimentaire tendre : ce que la passe veut. */
	FWorldseedLithology LithoTendre(const FWorldseedGeometry& G,
		const FWorldseedLithologyRules& Regles)
	{
		// ON CHERCHE LA ROCHE DANS LE CATALOGUE PLUTOT QUE D'ECRIRE UN INDEX.
		// Les identifiants de roche sont des CLES -- le registre l'ecrit pour
		// les biomes comme pour les roches -- et en figer un ici ferait passer
		// le test sur la mauvaise roche le jour ou le catalogue bouge.
		uint8 Choisie = 0;
		float MeilleureDurete = 1.0f;
		for (int32 I = 0; I < Regles.Catalogue.Num(); ++I)
		{
			const float D = Regles.Catalogue[I].Hardness;
			if (D > 0.2f && D < 0.7f && D < MeilleureDurete)
			{
				MeilleureDurete = D;
				Choisie = static_cast<uint8>(I);
			}
		}

		FWorldseedLithology L;
		L.Id.Init(Choisie, G.CellCount());
		return L;
	}
}

/**
 * LA PASSE NE RELEVE JAMAIS LE RELIEF, ET C'EST SON EN-TETE QUI LE PROMET :
 * « Modifie ElevationM EN PLACE, et seulement vers le bas ».
 *
 * POURQUOI CE SENS UNIQUE COMPTE. Elle tourne APRES l'erosion et AVANT la
 * seconde passe de climat : une cellule relevee refroidirait, changerait de
 * biome, et deplacerait la vegetation -- sans qu'aucune mesure ne pointe vers
 * cette passe. Et une bosse au raccord avec le relief voisin ne se voit a
 * l'image que si l'on sait ou regarder.
 *
 * LE TEST PORTE AUSSI SUR L'ACTIVITE : un `Build` qui ne ferait RIEN
 * satisferait « jamais releve » sans rien prouver. On exige donc qu'il ait
 * abaisse quelque chose.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedPlateauSensUniqueTest,
	"Worldseed.Plateau.LaPasseNeReleveJamais",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedPlateauSensUniqueTest::RunTest(const FString& Parameters)
{
	const FWorldseedGeometry G = WorldseedTest::Geometrie(96);
	const TArray<float> Avant = ReliefDePlateau(G);

	FString Erreur;
	const UWorldseedRules* const Source = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("les regles du projet se chargent"), Source))
	{
		return false;
	}

	// LES VRAIES REGLES, ET NON LES DEFAUTS DES STRUCTURES : le catalogue de
	// roches vient du JSON, et sans lui la recherche de roche tendre ne
	// trouverait rien -- le test tournerait sur une lithologie arbitraire et
	// ne prouverait pas ce qu-il annonce.
	const FWorldseedPlateauRules Regles = FWorldseedPlateauRules::FromRules(*Source);
	const FWorldseedFinRules FinRegles = FWorldseedFinRules::FromRules(*Source);
	const FWorldseedLithologyRules LithoRegles = FWorldseedLithologyRules::FromRules(*Source);
	const FWorldseedStratRules StratRegles = FWorldseedStratRules::FromRules(*Source, LithoRegles);
	const FWorldseedLithology Litho = LithoTendre(G, LithoRegles);

	// UN CLIMAT ARIDE, parce que la passe le demande : « un escarpement
	// vertical est une forme ARIDE -- sous la pluie le sol se forme, la
	// vegetation s'installe et la paroi s'adoucit en versant ».
	TArray<float> Pluie;
	Pluie.Init(120.0f, G.CellCount());
	TArray<float> Temp;
	Temp.Init(22.0f, G.CellCount());

	TArray<float> Apres = Avant;
	WorldseedPlateau::Build(G, Litho, LithoRegles, Pluie, Temp,
		Regles, FinRegles, StratRegles, 20260909, Apres);

	int32 Relevees = 0;
	int32 Abaissees = 0;
	float PireHausse = 0.0f;
	for (int32 I = 0; I < Avant.Num(); ++I)
	{
		const float D = Apres[I] - Avant[I];
		if (D > 0.01f)
		{
			++Relevees;
			PireHausse = FMath::Max(PireHausse, D);
		}
		else if (D < -0.01f)
		{
			++Abaissees;
		}
	}

	TestEqual(FString::Printf(
		TEXT("aucune cellule relevee (pire hausse %.2f m)"), PireHausse),
		Relevees, 0);

	// LE TEMOIN D'ACTIVITE : sans lui, une passe inerte passerait.
	TestTrue(FString::Printf(
		TEXT("la passe a bien creuse quelque chose (%d cellules)"), Abaissees),
		Abaissees > 0);
	return true;
}

/**
 * AUCUNE TERRE NE DEVIENT MER, ET C'EST UN INVARIANT DU PROJET.
 *
 * La part emergee est calibree a 29,2 pour cent et « traitee comme un
 * invariant » : tout le calage terrestre -- parts de biomes, pluie moyenne,
 * bulletin des climats reels -- repose dessus. Le plancher du fond de canyon
 * existe pour cela, et son commentaire le dit sans detour : sans lui, « un
 * escarpement de cent soixante-dix metres creuse depuis une table qui n'en
 * fait que quatre-vingts enverrait le fond SOUS le niveau de la mer, donc
 * convertirait de la terre en mer ».
 *
 * CE QUE CA COUTERAIT SANS ORACLE : la part emergee derive de quelques
 * dixiemes, les parts de biomes suivent, et l'on chercherait la cause dans le
 * climat -- ce jeu etant a SOMME NULLE, agrandir une part en retire une autre.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedPlateauPartEmergeeTest,
	"Worldseed.Plateau.AucuneTerreNeDevientMer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedPlateauPartEmergeeTest::RunTest(const FString& Parameters)
{
	const FWorldseedGeometry G = WorldseedTest::Geometrie(96);
	const TArray<float> Avant = ReliefDePlateau(G);

	FString Erreur;
	const UWorldseedRules* const Source = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("les regles du projet se chargent"), Source))
	{
		return false;
	}

	// LES VRAIES REGLES, ET NON LES DEFAUTS DES STRUCTURES : le catalogue de
	// roches vient du JSON, et sans lui la recherche de roche tendre ne
	// trouverait rien -- le test tournerait sur une lithologie arbitraire et
	// ne prouverait pas ce qu-il annonce.
	const FWorldseedPlateauRules Regles = FWorldseedPlateauRules::FromRules(*Source);
	const FWorldseedFinRules FinRegles = FWorldseedFinRules::FromRules(*Source);
	const FWorldseedLithologyRules LithoRegles = FWorldseedLithologyRules::FromRules(*Source);
	const FWorldseedStratRules StratRegles = FWorldseedStratRules::FromRules(*Source, LithoRegles);
	const FWorldseedLithology Litho = LithoTendre(G, LithoRegles);

	TArray<float> Pluie;
	Pluie.Init(120.0f, G.CellCount());
	TArray<float> Temp;
	Temp.Init(22.0f, G.CellCount());

	TArray<float> Apres = Avant;
	WorldseedPlateau::Build(G, Litho, LithoRegles, Pluie, Temp,
		Regles, FinRegles, StratRegles, 20260909, Apres);

	int32 TerresAvant = 0;
	int32 TerresApres = 0;
	int32 Noyees = 0;
	for (int32 I = 0; I < Avant.Num(); ++I)
	{
		if (Avant[I] > 0.0f) { ++TerresAvant; }
		if (Apres[I] > 0.0f) { ++TerresApres; }
		if (Avant[I] > 0.0f && Apres[I] <= 0.0f) { ++Noyees; }
	}

	// LE TEMOIN EST DANS LE COMPTE DE DEPART : un relief entierement immerge
	// rendrait « zero noyee » sans que cela veuille dire quoi que ce soit.
	TestTrue(FString::Printf(
		TEXT("le relief de depart porte des terres (%d cellules)"), TerresAvant),
		TerresAvant > Avant.Num() / 4);

	TestEqual(FString::Printf(
		TEXT("aucune cellule de terre ne passe sous la mer (%d -> %d)"),
		TerresAvant, TerresApres),
		Noyees, 0);
	return true;
}

#endif
