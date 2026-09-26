// Worldseed - le pointage du globe sous ZOOM, la partie que la sonde ne voit pas.
//
// POURQUOI CE FICHIER EXISTE. `ProbePointage` eprouve deja l'aller-retour de la
// projection -- latitude et longitude connues, projetees vers l'image puis
// reinversees, 0,000069 degre sur 25 158 points hors limbe. Elle travaille dans
// l'espace NORMALISE : ni widget, ni `FGeometry`, ni zoom.
//
// ET LA SEULE PARTIE NON TESTEE ETAIT LA PARTIE FAUTIVE. `PointerSurLeGlobe`
// defaisait le zoom une seconde fois, apres `AbsoluteToLocal` qui l'avait deja
// defait : le reticule tombait a 1/Z de la distance du curseur au centre --
// exact a zoom 1, faux d'un facteur quatre a zoom 4. Le commentaire d'origine
// annoncait le defaut sans pouvoir le verifier : « si le reticule tombe sous le
// curseur a zoom 1 mais derive a zoom 4, c'est ici qu'est la faute. »
//
// CE QUI EST VERIFIE ICI EST UNE PREMISSE DU MOTEUR, et c'est deliberé : tout
// repose sur le fait qu'`AbsoluteToLocal` applique l'inverse de la transformee
// de RENDU accumulee, pivot compris. C'est cette prémisse qui a ete mal
// comprise ; si une version d'Unreal la change, ce test doit crier plutot que
// de laisser le reticule deriver en silence.

#if WITH_DEV_AUTOMATION_TESTS

#include "Layout/Geometry.h"

#include "Misc/AutomationTest.h"

namespace
{
	/** Le cote du globe, en unites logiques. Voir `SetDesiredSizeOverride`. */
	constexpr float CoteGlobe = 420.0f;

	/**
	 * La geometrie que `PreviewImage->GetCachedGeometry()` rend sous zoom.
	 *
	 * ON REPRODUIT LA CHAINE DU MENU, pas une geometrie quelconque : une racine
	 * a l'echelle de l'ecran, puis l'image avec sa `RenderScale` et le pivot
	 * central que `SetRenderScale` emploie par defaut. C'est ce que
	 * `SScaleBox::OnArrangeChildren` construit en passant par la surcharge
	 * `MakeChild(Widget, ...)`, laquelle injecte la transformee de rendu de
	 * l'enfant.
	 */
	FGeometry CadreDuGlobe(float Zoom, float EchelleDPI, const FVector2f& Decalage)
	{
		const FGeometry Racine = FGeometry::MakeRoot(
			FVector2f(1600.0f, 900.0f), FSlateLayoutTransform(EchelleDPI));

		return Racine.MakeChild(
			FVector2f(CoteGlobe, CoteGlobe),
			FSlateLayoutTransform(Decalage),
			FSlateRenderTransform(FScale2D(Zoom, Zoom)),
			FVector2f(0.5f, 0.5f));
	}
}

/**
 * LE TEXEL VISE NE DEPEND PAS DU ZOOM, ET C'EST TOUT LE DEFAUT CORRIGE.
 *
 * On prend un texel connu du globe, on calcule ou il s'AFFICHE a un zoom donne
 * -- c'est-a-dire la position d'ecran ou le curseur se trouverait -- puis on
 * redemande au cadre quel texel cette position designe. La reponse doit etre le
 * texel de depart, a tous les zooms.
 *
 * LE TEMOIN EST DANS LE TEST : on verifie aussi que l'ANCIENNE formule, elle,
 * echoue. Sans cela, un test qui passe ne prouverait pas qu'il regarde le bon
 * endroit -- ce depot a paye quatre fixtures muettes en une seule journee.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedPointageGlobeZoomTest,
	"Worldseed.Globe.LePointageNeDependPasDuZoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedPointageGlobeZoomTest::RunTest(const FString& Parameters)
{
	// DES TEXELS EXCENTRES, jamais le centre : au centre, toute erreur d'echelle
	// autour du centre est invisible par construction. C'est la meme raison qui
	// fait choisir des cibles excentrees ailleurs dans ce depot.
	const TArray<FVector2f> Texels = {
		FVector2f(210.0f, 210.0f),   // le centre, temoin trivial
		FVector2f(310.0f, 210.0f),
		FVector2f(210.0f, 100.0f),
		FVector2f(120.0f, 330.0f),
		FVector2f(395.0f, 205.0f),   // pres du limbe
	};
	const TArray<float> Zooms = { 1.0f, 1.5f, 2.0f, 4.0f, 6.0f };

	// DEUX ECHELLES ET UN DECALAGE : le menu tourne a DPI 0,83 sur une fenetre
	// 1600x900, et le globe n'est pas a l'origine de l'ecran -- il partage la
	// ligne avec le panneau de gauche. Un test pose a l'origine et a DPI 1
	// laisserait passer une faute de mise en page.
	const TArray<float> Dpi = { 1.0f, 0.83f };
	const FVector2f Decalage(190.0f, 120.0f);

	int32 Verifies = 0;
	double PireEcart = 0.0;
	double PireEcartAncienneFormule = 0.0;

	for (const float Echelle : Dpi)
	{
		for (const float Zoom : Zooms)
		{
			const FGeometry Cadre = CadreDuGlobe(Zoom, Echelle, Decalage);
			const FVector2D Taille = FVector2D(Cadre.GetLocalSize());
			const FVector2D Centre = Taille * 0.5;

			for (const FVector2f& T : Texels)
			{
				const FVector2D Texel(T);

				// OU CE TEXEL S'AFFICHE-T-IL ? C'est la transformee de rendu
				// accumulee qui le dit, celle-la meme que le moteur emploie
				// pour dessiner. On ne recompose pas l'echelle a la main : ce
				// serait supposer ce qu'on veut verifier.
				const FVector2D Ecran = Cadre.LocalToAbsolute(Texel);

				// Ce que le code corrige lit.
				const FVector2D Lu = Cadre.AbsoluteToLocal(Ecran);
				PireEcart = FMath::Max(PireEcart, (Lu - Texel).Size());

				// Ce que l'ANCIENNE formule lisait, pour temoin.
				const FVector2D Ancien = (Lu - Centre) / Zoom + Centre;
				PireEcartAncienneFormule =
					FMath::Max(PireEcartAncienneFormule, (Ancien - Texel).Size());

				++Verifies;
			}
		}
	}

	TestEqual(TEXT("tous les cas ont ete parcourus"), Verifies,
		Texels.Num() * Zooms.Num() * Dpi.Num());

	TestTrue(FString::Printf(
		TEXT("le texel lu est celui vise, a tous les zooms (pire ecart %.6f px)"),
		PireEcart),
		PireEcart < 0.01);

	// LE TEMOIN. L'ancienne formule doit echouer, et largement : a zoom 6 sur un
	// texel au bord, elle se trompe de plusieurs dizaines d'unites logiques. Si
	// ce controle venait a passer, c'est que la geometrie de test ne porte plus
	// le zoom -- et le test entier ne mesurerait plus rien.
	TestTrue(FString::Printf(
		TEXT("l'ancienne formule, elle, derive bien (pire ecart %.2f px)"),
		PireEcartAncienneFormule),
		PireEcartAncienneFormule > 50.0);
	return true;
}

/**
 * `AbsoluteToLocal` DEFAIT LA TRANSFORMEE DE RENDU, PIVOT COMPRIS.
 *
 * C'est la premisse du correctif, et elle se verifie seule : un point d'ecran
 * obtenu par `LocalToAbsolute` doit revenir a l'identique. On l'eprouve avec un
 * pivot NON central en plus du pivot par defaut -- si une version du moteur
 * cessait de composer le pivot, ce cas-la tomberait le premier.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedPointageGlobePivotTest,
	"Worldseed.Globe.LaGeometrieDefaitLeRendu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedPointageGlobePivotTest::RunTest(const FString& Parameters)
{
	const FGeometry Racine = FGeometry::MakeRoot(
		FVector2f(1600.0f, 900.0f), FSlateLayoutTransform(0.83f));

	const TArray<FVector2f> Pivots = {
		FVector2f(0.5f, 0.5f),   // celui de SetRenderScale
		FVector2f(0.0f, 0.0f),
		FVector2f(0.25f, 0.75f),
	};

	double PireEcart = 0.0;
	for (const FVector2f& Pivot : Pivots)
	{
		const FGeometry Cadre = Racine.MakeChild(
			FVector2f(CoteGlobe, CoteGlobe),
			FSlateLayoutTransform(FVector2f(190.0f, 120.0f)),
			FSlateRenderTransform(FScale2D(3.0f, 3.0f)), Pivot);

		for (int32 I = 0; I < 16; ++I)
		{
			const FVector2D Texel(20.0 + I * 25.0, 400.0 - I * 23.0);
			const FVector2D Retour = Cadre.AbsoluteToLocal(Cadre.LocalToAbsolute(Texel));
			PireEcart = FMath::Max(PireEcart, (Retour - Texel).Size());
		}
	}

	TestTrue(FString::Printf(
		TEXT("l'aller-retour ecran est exact quel que soit le pivot (%.6f px)"),
		PireEcart),
		PireEcart < 0.01);
	return true;
}

#endif
