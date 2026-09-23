// Worldseed - les lames de gres : des fentes PARALLELES, et la coordonnee qui
// les traverse doit avancer d'un metre par metre. C'est tout le mecanisme.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedFins.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedRules.h"

#include "Misc/AutomationTest.h"

namespace
{
	/**
	 * Des points LOIN de l'origine, et a l'ECART des frontieres de case.
	 *
	 * Loin de l'origine, parce que c'est la que le defaut d'origine vivait :
	 * une coordonnee absolue de 32 000 m multipliee par une orientation qui
	 * tourne. A l'ecart des frontieres, parce que l'orientation est constante
	 * PAR MORCEAUX -- la discontinuite y est voulue, et le commentaire de la
	 * passe dit pourquoi : la case fait 16 km, bien plus large qu'une tache de
	 * lames, donc la coupure tombe la ou la zone est deja nulle.
	 */
	template <typename F>
	void BalayerLoinDesCoupures(double TailleCase, int32 N, F&& Visiteur)
	{
		for (int32 I = 0; I < N; ++I)
		{
			const int32 CaseX = (I % 5) - 2;
			const int32 CaseY = ((I / 5) % 5) - 2;

			// 10 % a 90 % de la case : jamais sur une coupure.
			const double FX = 0.1 + 0.8 * FMath::Fmod(I * 0.6180339887, 1.0);
			const double FY = 0.1 + 0.8 * FMath::Fmod(I * 0.4142135624, 1.0);

			Visiteur((CaseX + FX) * TailleCase, (CaseY + FY) * TailleCase);
		}
	}
}

/**
 * LA COORDONNEE QUI TRAVERSE LES LAMES AVANCE D'UN METRE PAR METRE.
 *
 * C'EST L'ORACLE QUI GARDE LE DEFAUT LE PLUS CHER DE CETTE PASSE, et il a ete
 * paye a l'image avant d'etre compris. Ecrire
 * `U = X.cos(theta(X,Y)) + Y.sin(theta(X,Y))` pour des bandes qui tournent
 * doucement est mathematiquement FAUX : theta multiplie la coordonnee ABSOLUE
 * du monde, qui monte a 32 000 m, si bien que
 * `|grad U| = 1 + X.d(theta)/ds` vaut environ CINQ loin de l'origine.
 * Consequence mesuree : l'espacement reel des fentes tombait a quatorze metres
 * pour soixante-dix demandes, et il ne restait aucune lame.
 *
 * Le remede est une orientation constante PAR MORCEAUX, et il se verifie
 * exactement ici : `|grad Across|` doit valoir UN. Rien d'autre ne l'attrape --
 * l'espacement demande est un reglage, le resultat une image, et entre les
 * deux ce facteur cinq est invisible.
 *
 * LE PAS EST DE DIX METRES, non de un : ce depot a deja mesure une pente
 * fausse parce que le pas de difference finie tombait sous l'ULP du type a la
 * magnitude ou l'on mesure. A 32 000 m, dix metres laissent toute la marge.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestLamesGradient,
	"Worldseed.Lames.LaTraverseeAvanceDUnParUn",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestLamesGradient::RunTest(const FString& Parameters)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("world_rules.json se lit"), R))
	{
		AddError(Erreur);
		return false;
	}

	const FWorldseedFinRules R2 = FWorldseedFinRules::FromRules(*R);
	constexpr int32 Seed = 20260909;
	constexpr double Pas = 10.0;

	double PireEcartAUn = 0.0;
	double NormeMin = BIG_NUMBER, NormeMax = 0.0;

	BalayerLoinDesCoupures(R2.TurnCellM, 600, [&](double X, double Y)
	{
		const double U = WorldseedFins::Across(X, Y, R2, Seed);
		const double DX = (WorldseedFins::Across(X + Pas, Y, R2, Seed) - U) / Pas;
		const double DY = (WorldseedFins::Across(X, Y + Pas, R2, Seed) - U) / Pas;

		const double Norme = FMath::Sqrt(DX * DX + DY * DY);
		NormeMin = FMath::Min(NormeMin, Norme);
		NormeMax = FMath::Max(NormeMax, Norme);
		PireEcartAUn = FMath::Max(PireEcartAUn, FMath::Abs(Norme - 1.0));
	});

	AddInfo(FString::Printf(
		TEXT("|grad Across| de %.4f a %.4f -- ecart maximal a un : %.4f"),
		NormeMin, NormeMax, PireEcartAUn));

	// La tolerance couvre le WanderM, qui fait serpenter les fentes a dessein
	// et ajoute une derivee petite. Elle ne couvre PAS un facteur cinq.
	TestTrue(TEXT("|grad Across| vaut un, donc l'espacement demande est tenu"),
		PireEcartAUn < 0.35);

	return true;
}

/**
 * L'ESPACEMENT DES FENTES EST CELUI QU'ON DEMANDE.
 *
 * C'est la consequence observable du test precedent, et elle merite son propre
 * oracle : le gradient peut valoir un tout en ayant une periode fausse si la
 * fonction de decoupe changeait. On compte les fentes traversees sur une
 * longue coupe et l'on remonte a la periode.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestLamesEspacement,
	"Worldseed.Lames.LEspacementEstCeluiDemande",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestLamesEspacement::RunTest(const FString& Parameters)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("world_rules.json se lit"), R))
	{
		AddError(Erreur);
		return false;
	}

	const FWorldseedFinRules R2 = FWorldseedFinRules::FromRules(*R);
	constexpr int32 Seed = 20260909;

	// Une coupe DANS une case, donc sans coupure d'orientation.
	const double Y0 = R2.TurnCellM * 0.5;
	const double Debut = R2.TurnCellM * 0.15;
	const double Fin = R2.TurnCellM * 0.85;

	const double U0 = WorldseedFins::Across(Debut, Y0, R2, Seed);
	const double U1 = WorldseedFins::Across(Fin, Y0, R2, Seed);

	const double Parcourue = FMath::Abs(U1 - U0);
	const double Attendue = Fin - Debut;

	AddInfo(FString::Printf(
		TEXT("sur %.0f m de coupe, la traversee parcourt %.0f m -- soit %.0f fentes ")
		TEXT("a %.0f m d'espacement"),
		Attendue, Parcourue, Parcourue / R2.SpacingM, R2.SpacingM));

	TestTrue(TEXT("TEMOIN : la traversee avance"), Parcourue > 1.0);
	TestTrue(TEXT("elle parcourt la distance reellement franchie, a 35 % pres"),
		FMath::Abs(Parcourue - Attendue) < 0.35 * Attendue);

	return true;
}

/**
 * LA ZONE EST UNE PART, ET LE CREUSEMENT N'EXISTE QUE DEDANS.
 *
 * `Zone` module l'ouverture ; hors zone elle doit valoir zero, sinon les
 * lames se creuseraient partout au lieu de former des taches. Et elle doit
 * rester dans [0,1] : un multiplicateur hors bornes inverserait le terme ou le
 * doublerait, ce qui se lit comme une forme et non comme un defaut.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestLamesZone,
	"Worldseed.Lames.LaZoneEstUnePart",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestLamesZone::RunTest(const FString& Parameters)
{
	FString Erreur;
	const UWorldseedRules* const R = WorldseedPipeline::GetRules(Erreur);
	if (!TestNotNull(TEXT("world_rules.json se lit"), R))
	{
		AddError(Erreur);
		return false;
	}

	const FWorldseedFinRules R2 = FWorldseedFinRules::FromRules(*R);
	constexpr int32 Seed = 20260909;

	int32 HorsBornes = 0, NonFinies = 0, Ouvertes = 0, Fermees = 0;
	int32 CreuseHorsZone = 0;

	BalayerLoinDesCoupures(R2.TurnCellM, 2000, [&](double X, double Y)
	{
		const float Z = WorldseedFins::Zone(X, Y, R2, Seed);

		if (FMath::IsNaN(Z) || !FMath::IsFinite(Z)) { ++NonFinies; }
		if (Z < 0.0f || Z > 1.0f) { ++HorsBornes; }
		if (Z > 0.0f) { ++Ouvertes; } else { ++Fermees; }

		// A profondeur utile, hors zone, rien ne doit se creuser.
		if (Z <= 0.0f
			&& WorldseedFins::SlotAt(X, Y, R2.DepthM * 0.5, R2, Seed) > 0.0)
		{
			++CreuseHorsZone;
		}
	});

	AddInfo(FString::Printf(
		TEXT("sur 2000 points : %d en zone, %d hors zone"), Ouvertes, Fermees));

	TestEqual(TEXT("aucune valeur NaN"), NonFinies, 0);
	TestEqual(TEXT("la zone reste dans [0,1]"), HorsBornes, 0);
	TestEqual(TEXT("rien ne se creuse hors de la zone"), CreuseHorsZone, 0);

	// LES DEUX TEMOINS : une zone partout nulle passerait les trois lignes
	// ci-dessus, une zone partout pleine les deux premieres.
	TestTrue(TEXT("TEMOIN : la zone s'ouvre quelque part"), Ouvertes > 0);
	TestTrue(TEXT("TEMOIN : et se ferme ailleurs"), Fermees > 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
