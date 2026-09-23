// Worldseed - la peinture des sommets : elle n'ecrit JAMAIS de noir, et la
// carte des causes le prouve d'un coup d'oeil.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedDensity.h"
#include "Procedural/WorldseedPeinture.h"
#include "Procedural/WorldseedVoxelChunk.h"
#include "Procedural/WorldseedWorldData.h"

#include "Misc/AutomationTest.h"

namespace
{
	/**
	 * Un maillage et de quoi le peindre, sur le monde de la fixture.
	 *
	 * L'ORDRE DE DECLARATION COMPTE : le contexte de peinture porte des
	 * REFERENCES, donc tout ce qu'il vise doit vivre plus longtemps que lui.
	 * Les membres declares avant sont construits avant et detruits apres.
	 */
	struct FBancPeinture
	{
		FWorldseedWorldData Monde;
		FWorldseedDensityRules ReglesChamp;
		FWorldseedDensity Champ;
		FWorldseedLithology Litho;
		TArray<FLinearColor> CouleurParRoche;
		TArray<float> DureteParId;
		FWorldseedStratRules Strates;
		FWorldseedVoxelMesh Maillage;

		explicit FBancPeinture(int32 NY = 32)
		{
			Monde = WorldseedTest::Monde(NY);

			ReglesChamp.OverhangAmplitudeM = 0.0f;
			ReglesChamp.DetailAmplitudeM = 0.0f;
			Champ.Init(Monde.Geometry, Monde.ElevationM, 1.0f, 4242, ReglesChamp);

			Litho.Id = Monde.LithologyId;

			// HUIT ROCHES, TOUTES CLAIRES ET TOUTES DIFFERENTES. Des teintes
			// identiques rendraient le test muet sur la branche choisie ; des
			// teintes sombres masqueraient le fait qu'on veut prouver -- que la
			// peinture n'ecrit jamais de noir.
			for (int32 K = 0; K < 8; ++K)
			{
				const float T = 0.45f + 0.06f * K;
				CouleurParRoche.Add(FLinearColor(T, T * 0.9f, T * 0.8f));
				DureteParId.Add(0.30f + 0.08f * K);
			}
		}

		/**
		 * Une colonne de sommets qui traverse la surface, du ciel au sous-sol.
		 *
		 * ELLE TRAVERSE A DESSEIN : la peinture change de branche avec la
		 * PROFONDEUR, donc un maillage entierement au-dessus ou entierement en
		 * dessous n'en exercerait qu'une. Ce depot a paye quatre fixtures
		 * muettes en une journee ; celle-ci couvre les trois branches.
		 */
		void PoserUneColonne(double XM, double YM, int32 Nb, double PasM)
		{
			const double Surface = Champ.SurfaceHeightM(XM, YM);
			Maillage.Positions.Reset();
			for (int32 I = 0; I < Nb; ++I)
			{
				const double Z = Surface + 20.0 - I * PasM;
				Maillage.Positions.Add(FVector(XM * 100.0, YM * 100.0, Z * 100.0));
			}
		}

		/** Un semis de sommets sur toute la carte, a des profondeurs variees. */
		void PoserUnSemis(int32 Nb)
		{
			Maillage.Positions.Reset();
			const double LargeurM = Monde.Geometry.WidthM();
			const double HauteurM = Monde.Geometry.HeightM;
			for (int32 I = 0; I < Nb; ++I)
			{
				const double U = FMath::Fmod(I * 0.6180339887, 1.0) - 0.5;
				const double V = FMath::Fmod(I * 0.4142135624, 1.0) - 0.5;
				const double XM = U * LargeurM * 0.98;
				const double YM = V * HauteurM * 0.98;
				const double S = Champ.SurfaceHeightM(XM, YM);
				const double Z = S + 30.0 - FMath::Fmod(I * 0.3027756377, 1.0) * 120.0;
				Maillage.Positions.Add(FVector(XM * 100.0, YM * 100.0, Z * 100.0));
			}
		}

		FWorldseedPeintureContexte Contexte(bool bCarte = false, float Fondu = 12.0f) const
		{
			return FWorldseedPeintureContexte{
				Monde.Biomes, Monde.Geometry, Champ, Litho, CouleurParRoche, DureteParId,
				Strates, Fondu, 0.0f, 4242, bCarte };
		}
	};
}

/**
 * LA PEINTURE N'ECRIT JAMAIS DE NOIR.
 *
 * C'EST LE FAIT QUI A RETOURNE LE DIAGNOSTIC DU 22 SEPTEMBRE. Devant des
 * parois rayees de bandes sombres, huit etats ont ete mesures -- schiste
 * reteinte, serie uniforme, teinte de roche coupee, lumieres eteintes -- sans
 * qu'aucun ne deplace le contraste. La question qui manquait n'etait pas
 * « quel terme fait ce noir » mais « ECRIT-on seulement du noir ? ». Mesure en
 * jeu : luminance ECRITE de 66 a 234 sur 255, 0,05 % sous le seuil -- contre
 * 18,5 % de pixels noirs a l'ecran. Le noir est l'OMBRE des gradins.
 *
 * SANS CET ORACLE, CE FAIT SE REPERD. Il suffirait qu'une teinte de roche
 * sombre entre au catalogue pour que la conclusion cesse d'etre vraie, et
 * personne ne le saurait avant la prochaine soiree de diagnostic.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestPeintureJamaisDeNoir,
	"Worldseed.Peinture.NEcritJamaisDeNoir",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestPeintureJamaisDeNoir::RunTest(const FString& Parameters)
{
	FBancPeinture B;
	B.PoserUnSemis(4000);

	FWorldseedPeintureReleve R;
	WorldseedPeinture::Sommets(B.Maillage, B.Contexte(), R);

	TestEqual(TEXT("chaque sommet recoit une couleur"),
		B.Maillage.Colours.Num(), B.Maillage.Positions.Num());

	// On relit les couleurs ECRITES plutot que de croire le releve : un
	// compteur peut mentir, le tableau non.
	double LumMin = 1e30, LumMax = -1e30;
	int32 NonFinies = 0, HorsBornes = 0;
	for (const FLinearColor& C : B.Maillage.Colours)
	{
		if (C.R != C.R || C.G != C.G || C.B != C.B) { ++NonFinies; }
		if (C.R < 0.0f || C.G < 0.0f || C.B < 0.0f) { ++HorsBornes; }
		const double L = C.GetLuminance();
		LumMin = FMath::Min(LumMin, L);
		LumMax = FMath::Max(LumMax, L);
	}

	AddInfo(FString::Printf(
		TEXT("%d sommets peints, luminance ECRITE de %.3f a %.3f ; ")
		TEXT("le releve dit %.3f a %.3f"),
		B.Maillage.Colours.Num(), LumMin, LumMax, R.LumMin, R.LumMax));

	TestEqual(TEXT("aucune couleur NaN"), NonFinies, 0);
	TestEqual(TEXT("aucune composante negative"), HorsBornes, 0);

	// LE SEUIL EST CELUI DU RELEVE EN JEU : la luminance d'un gris 70/255.
	const double Seuil = FLinearColor(FColor(70, 70, 70, 255)).GetLuminance();
	TestTrue(TEXT("la peinture n'ecrit jamais sous le seuil de noir"),
		LumMin >= Seuil);

	// ET LE RELEVE DIT LA MEME CHOSE QUE LE TABLEAU -- sinon c'est le releve
	// qu'il faudrait corriger, et c'est lui que le jeu journalise.
	TestTrue(TEXT("le releve s'accorde avec ce qui est ecrit"),
		FMath::Abs(R.LumMin - LumMin) < 1.0e-6
			&& FMath::Abs(R.LumMax - LumMax) < 1.0e-6);

	// TEMOIN : une peinture qui rendrait tout blanc passerait tout ce qui
	// precede. Il faut qu'elle VARIE.
	TestTrue(TEXT("TEMOIN : les couleurs ne sont pas toutes identiques"),
		LumMax - LumMin > 0.02);

	return true;
}

/**
 * LA BRANCHE COMPTEE EST CELLE QUI A ECRIT, PAS CELLE QU'ON A TRAVERSEE.
 *
 * Le fondu peut ramener au BIOME un sommet dont on venait de lire le banc : il
 * suffit que la profondeur n'ait pas encore franchi la marge. Compter la
 * branche PARCOURUE donnerait alors un entonnoir juste -- « la serie est
 * active ici » -- et une carte des causes FAUSSE, qui montrerait de la roche la
 * ou l'on voit du biome.
 *
 * ON LE PROUVE PAR LE FONDU : a fondu nul, la roche ne mord jamais et TOUT doit
 * etre compte en biome, quelle que soit la profondeur.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestPeintureBranche,
	"Worldseed.Peinture.LaBrancheCompteeEstCelleQuiAEcrit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestPeintureBranche::RunTest(const FString& Parameters)
{
	FBancPeinture B;
	B.PoserUnSemis(4000);

	// --- fondu NUL : la roche ne mord jamais ---------------------------------
	{
		FWorldseedPeintureReleve R;
		WorldseedPeinture::Sommets(B.Maillage, B.Contexte(false, 0.0f), R);

		const int64 Biome = R.ParCause[static_cast<int32>(WorldseedPeinture::ECause::Biome)];
		const int64 Roche = R.ParCause[static_cast<int32>(WorldseedPeinture::ECause::Roche2D)];
		const int64 Banc = R.ParCause[static_cast<int32>(WorldseedPeinture::ECause::Banc)];

		AddInfo(FString::Printf(
			TEXT("fondu nul : biome %lld, roche 2D %lld, banc %lld"),
			Biome, Roche, Banc));

		TestEqual(TEXT("fondu nul : tout est compte en biome"),
			Biome, static_cast<int64>(B.Maillage.Positions.Num()));
		TestEqual(TEXT("fondu nul : aucune roche"), Roche + Banc, static_cast<int64>(0));
	}

	// --- fondu franc : la roche mord sous la surface -------------------------
	{
		FWorldseedPeintureReleve R;
		WorldseedPeinture::Sommets(B.Maillage, B.Contexte(false, 12.0f), R);

		int64 Total = 0;
		for (int32 K = 0; K < WorldseedPeinture::NbCauses; ++K) { Total += R.ParCause[K]; }

		const int64 Roche = R.ParCause[static_cast<int32>(WorldseedPeinture::ECause::Roche2D)]
			+ R.ParCause[static_cast<int32>(WorldseedPeinture::ECause::Banc)];

		AddInfo(FString::Printf(
			TEXT("fondu 12 m : %lld sommets sous la surface, %lld peints en roche, ")
			TEXT("%lld teintes"),
			R.SousLaSurface, Roche, R.Teintee));

		TestEqual(TEXT("les causes partitionnent les sommets"),
			Total, static_cast<int64>(B.Maillage.Positions.Num()));
		TestEqual(TEXT("le releve compte autant de sommets que le maillage"),
			R.Sommets, static_cast<int64>(B.Maillage.Positions.Num()));

		// LE TEMOIN QUI DONNE SON SENS AU TEST PRECEDENT : si la roche ne
		// mordait jamais, « fondu nul ne peint aucune roche » serait vrai
		// trivialement.
		TestTrue(TEXT("TEMOIN : a fondu franc, la roche peint reellement"), Roche > 0);

		// ET LA BRANCHE N'EST PAS CELLE QU'ON A TRAVERSEE : il y a strictement
		// plus de sommets SOUS LA SURFACE que de sommets peints en roche,
		// puisque le fondu en ramene une part au biome.
		TestTrue(TEXT("sous la surface, mais pas tous peints en roche"),
			R.SousLaSurface > Roche);
	}

	return true;
}

/**
 * LA CARTE DES CAUSES EST TOUTE CLAIRE, ET C'EST CE QUI LA REND CONCLUANTE.
 *
 * Elle peint chaque sommet par la BRANCHE qui a decide sa couleur, en aplats
 * francs. Si l'un d'eux etait sombre, un pixel noir sur la carte resterait
 * ambigu -- est-ce la branche, ou autre chose ? Avec cette palette, tout pixel
 * noir est par construction quelque chose qu'on ne peint pas, et c'est
 * exactement ce raisonnement qui a permis de conclure que le noir des parois
 * etait de l'ombre.
 *
 * C'est le temoin de couleur du depot applique a un diagnostic : « une couleur
 * franche ne se compare a rien, elle est la ou elle n'est pas ».
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestPeintureCarteDesCauses,
	"Worldseed.Peinture.LaCarteDesCausesEstToutEnClair",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestPeintureCarteDesCauses::RunTest(const FString& Parameters)
{
	// --- les quatre aplats, un par un ---------------------------------------
	const double Seuil = FLinearColor(FColor(70, 70, 70, 255)).GetLuminance();

	for (int32 K = 0; K < WorldseedPeinture::NbCauses; ++K)
	{
		const WorldseedPeinture::ECause Cause = static_cast<WorldseedPeinture::ECause>(K);
		const FLinearColor A = WorldseedPeinture::Aplat(Cause, 0, 10);

		AddInfo(FString::Printf(TEXT("aplat « %s » : luminance %.3f"),
			WorldseedPeinture::NomDeCause(Cause), A.GetLuminance()));

		TestTrue(FString::Printf(TEXT("l'aplat « %s » est clair"),
			WorldseedPeinture::NomDeCause(Cause)),
			A.GetLuminance() >= Seuil);
	}

	// --- et les teintes de banc aussi, sur tout le tour de roue -------------
	double PireBanc = 1e30;
	for (int32 Banc = 0; Banc < 10; ++Banc)
	{
		PireBanc = FMath::Min(PireBanc,
			static_cast<double>(WorldseedPeinture::Aplat(
				WorldseedPeinture::ECause::Banc, Banc, 10).GetLuminance()));
	}
	AddInfo(FString::Printf(TEXT("le plus sombre des dix bancs : %.3f"), PireBanc));
	TestTrue(TEXT("aucun banc n'a d'aplat sombre"), PireBanc >= Seuil);

	// --- et la carte, une fois peinte, n'a aucun sommet sombre --------------
	FBancPeinture B;
	B.PoserUnSemis(4000);

	FWorldseedPeintureReleve R;
	WorldseedPeinture::Sommets(B.Maillage, B.Contexte(true), R);

	int32 Sombres = 0;
	for (const FLinearColor& C : B.Maillage.Colours)
	{
		if (C.GetLuminance() < Seuil) { ++Sombres; }
	}

	AddInfo(FString::Printf(
		TEXT("carte des causes : %d sommets, %d sous le seuil"),
		B.Maillage.Colours.Num(), Sombres));

	TestEqual(TEXT("la carte des causes n'a aucun sommet sombre"), Sombres, 0);

	// TEMOIN : une carte d'une seule couleur serait claire aussi, et ne
	// distinguerait rien. Il faut que les branches se separent A L'OEIL.
	TSet<uint32> Teintes;
	for (const FLinearColor& C : B.Maillage.Colours)
	{
		Teintes.Add(C.ToFColor(false).ToPackedRGBA());
	}
	AddInfo(FString::Printf(TEXT("et %d teintes distinctes"), Teintes.Num()));
	TestTrue(TEXT("TEMOIN : la carte distingue plusieurs branches"),
		Teintes.Num() >= 2);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
