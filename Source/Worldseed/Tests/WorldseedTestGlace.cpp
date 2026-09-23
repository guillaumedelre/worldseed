// Worldseed - la calotte glaciaire : elle AJOUTE de la glace sur la calotte, et
// nulle part ailleurs.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedIce.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedRules.h"

#include "Misc/AutomationTest.h"

namespace
{
	/** Une calotte aux deux poles, et rien au milieu. */
	TArray<uint8> Calotte(const FWorldseedGeometry& G, int32 Bandes)
	{
		TArray<uint8> B;
		B.Init(static_cast<uint8>(EWorldseedBiome::Grassland), G.CellCount());
		for (int32 J = 0; J < G.NY; ++J)
		{
			const bool bPolaire = (J < Bandes) || (J >= G.NY - Bandes);
			if (!bPolaire) { continue; }
			for (int32 I = 0; I < G.NX; ++I)
			{
				B[J * G.NX + I] = static_cast<uint8>(EWorldseedBiome::IceCap);
			}
		}
		return B;
	}
}

/**
 * LA GLACE MONTE, ELLE NE CREUSE PAS -- ET SEULEMENT SOUS LA CALOTTE.
 *
 * Une passe qui abaisserait, meme d'un metre, convertirait de la terre en mer
 * au voisinage du rivage, et la part emergee est traitee comme un invariant du
 * projet -- 29,2 %, calee, et dont dependent toutes les parts de biomes par
 * effet de somme nulle. Et si elle debordait hors de la calotte, un dome de
 * plusieurs centaines de metres se poserait sur une prairie.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestGlaceMonteSeulement,
	"Worldseed.Glace.MonteEtSeulementSousLaCalotte",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestGlaceMonteSeulement::RunTest(const FString& Parameters)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("world_rules.json se lit"), R))
	{
		AddError(Erreur);
		return false;
	}

	const FWorldseedGeometry G = WorldseedTest::Geometrie(64);
	const TArray<uint8> Biomes = Calotte(G, 8);

	WorldseedIce::FRules IR = WorldseedIce::FRules::FromRules(*R, G);
	// On impose une epaisseur, pour que le test ne dependre pas du calage.
	IR.MaxThicknessM = 400.0f;

	TArray<float> Relief = WorldseedTest::Relief(G);
	const TArray<float> Avant = Relief;

	const float Epaisseur = WorldseedIce::Apply(Relief, Biomes, G, IR);

	int32 Abaissees = 0, HorsCalotteModifiees = 0, SousCalotteMontees = 0;
	float PlusGrandeMontee = 0.0f;

	for (int32 I = 0; I < Relief.Num(); ++I)
	{
		const float D = Relief[I] - Avant[I];
		const bool bCalotte =
			static_cast<EWorldseedBiome>(Biomes[I]) == EWorldseedBiome::IceCap;

		if (D < -1.0e-3f) { ++Abaissees; }
		if (!bCalotte && FMath::Abs(D) > 1.0e-3f) { ++HorsCalotteModifiees; }
		if (bCalotte && D > 1.0e-3f) { ++SousCalotteMontees; }
		PlusGrandeMontee = FMath::Max(PlusGrandeMontee, D);
	}

	AddInfo(FString::Printf(
		TEXT("epaisseur rendue %.1f m, plus grande montee %.1f m, ")
		TEXT("%d cellules montees sous la calotte"),
		Epaisseur, PlusGrandeMontee, SousCalotteMontees));

	TestEqual(TEXT("aucune cellule n'est abaissee"), Abaissees, 0);
	TestEqual(TEXT("rien ne bouge hors de la calotte"), HorsCalotteModifiees, 0);

	// TEMOIN : sans lui, une passe qui ne ferait RIEN passerait les deux
	// lignes ci-dessus -- c'est la fixture muette, payee quatre fois dans ce
	// depot en une seule journee.
	TestTrue(TEXT("TEMOIN : la glace monte reellement quelque part"),
		SousCalotteMontees > 0);
	TestTrue(TEXT("et elle ne depasse pas l'epaisseur demandee"),
		PlusGrandeMontee <= IR.MaxThicknessM + 1.0f);

	return true;
}

/**
 * SANS CALOTTE, OU SANS EPAISSEUR, LA PASSE EST INERTE.
 *
 * « Une donnee absente doit rester sans effet » est une regle de ce depot, et
 * elle a une raison : c'est ce qui permet de retirer un terme en posant son
 * reglage a zero, et de comparer les deux moities d'un A/B sur le MEME
 * binaire. Une passe qui agirait quand meme rendrait tout temoin faux.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestGlaceInerte,
	"Worldseed.Glace.InerteQuandElleNADeQuoiAgir",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestGlaceInerte::RunTest(const FString& Parameters)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("world_rules.json se lit"), R))
	{
		AddError(Erreur);
		return false;
	}

	const FWorldseedGeometry G = WorldseedTest::Geometrie(64);
	const TArray<float> Reference = WorldseedTest::Relief(G);

	// --- epaisseur nulle, calotte presente ----------------------------------
	{
		WorldseedIce::FRules IR = WorldseedIce::FRules::FromRules(*R, G);
		IR.MaxThicknessM = 0.0f;
		TArray<float> Relief = Reference;
		const float E = WorldseedIce::Apply(Relief, Calotte(G, 8), G, IR);

		TestEqual(TEXT("epaisseur nulle : rien n'est rendu"), E, 0.0f);
		TestTrue(TEXT("epaisseur nulle : le relief est intact"),
			Relief == Reference);
	}

	// --- epaisseur posee, mais aucune calotte -------------------------------
	{
		WorldseedIce::FRules IR = WorldseedIce::FRules::FromRules(*R, G);
		IR.MaxThicknessM = 400.0f;
		TArray<uint8> SansCalotte;
		SansCalotte.Init(static_cast<uint8>(EWorldseedBiome::Grassland),
			G.CellCount());

		TArray<float> Relief = Reference;
		const float E = WorldseedIce::Apply(Relief, SansCalotte, G, IR);

		TestEqual(TEXT("aucune calotte : rien n'est rendu"), E, 0.0f);
		TestTrue(TEXT("aucune calotte : le relief est intact"),
			Relief == Reference);
	}

	// --- TEMOIN : avec les deux, elle agit ----------------------------------
	{
		WorldseedIce::FRules IR = WorldseedIce::FRules::FromRules(*R, G);
		IR.MaxThicknessM = 400.0f;
		TArray<float> Relief = Reference;
		const float E = WorldseedIce::Apply(Relief, Calotte(G, 8), G, IR);

		TestTrue(TEXT("TEMOIN : avec calotte ET epaisseur, elle agit"), E > 0.0f);
		TestFalse(TEXT("TEMOIN : et le relief change"), Relief == Reference);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
