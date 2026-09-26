// Worldseed - le pointage du globe sous ZOOM, la partie que la sonde ne voit pas.
//
// POURQUOI CE FICHIER EXISTE. `ProbePointage` eprouve deja l'aller-retour de la
// projection -- latitude et longitude connues, projetees vers l'image puis
// reinversees, 0,000069 degre sur 25 158 points hors limbe. Mais elle travaille
// a zoom 1, et le zoom est precisement ce qui a deja casse ce pointage une fois.
//
// ⚠ LE ZOOM A CHANGE DE NATURE, ET CE FICHIER AVEC LUI. Il fut une
// `SetRenderScale` posee sur l'image Slate : un ETIREMENT de la texture deja
// peinte, qui rendait le globe flou -- a six fois, un texel devenait un carre
// de six pixels -- et que `PointerSurLeGlobe` defaisait une seconde fois apres
// `AbsoluteToLocal`, si bien que le reticule tombait a 1/Z de la distance du
// curseur au centre. Le test d'alors gardait cette chaine-la.
//
// Le zoom vit desormais dans la PROJECTION (`FCadreGlobe::RayonApparent`) : on
// ne dessine plus le meme globe plus gros, on dessine une portion plus petite
// du globe sur le meme nombre de pixels. Chaque pixel redevient un echantillon
// vrai, le cout ne bouge pas -- le lance-de-rayon fait un travail constant par
// pixel -- et l'image Slate ne porte plus aucune transformee.
//
// CE QUI DOIT ETRE GARDE N'EST DONC PLUS LE MEME. Ce n'est plus « le texel lu
// est celui vise malgre la RenderScale », c'est « la projection et son inverse
// restent d'accord A TOUS LES ZOOMS ». Elles lisent le meme champ, donc elles
// ne PEUVENT pas diverger -- mais c'est exactement le genre de propriete qu'on
// croit acquise jusqu'au jour ou quelqu'un ecrit `RayonDisque` a la place.

#if WITH_DEV_AUTOMATION_TESTS

#include "Layout/Geometry.h"
#include "Procedural/WorldseedGlobe.h"

#include "Misc/AutomationTest.h"

namespace
{
	/** Le cote du globe, en unites logiques. Voir `SetDesiredSizeOverride`. */
	constexpr float CoteGlobe = 420.0f;
}

/**
 * LA PROJECTION ET SON INVERSE RESTENT D'ACCORD A TOUS LES ZOOMS.
 *
 * On part d'une latitude et d'une longitude connues, on demande ou elles
 * tombent dans le cadre, puis on redemande au cadre quelle position du globe
 * cette place designe. La reponse doit etre le point de depart, que le globe
 * soit dessine entier ou agrandi six fois.
 *
 * LE TEMOIN EST DANS LE TEST, et il porte sur la faute qu'on peut REELLEMENT
 * commettre ici : ecrire la constante `RayonDisque` la ou il faut le rayon
 * APPARENT. C'est ce que faisaient les deux fonctions avant ce chantier, et
 * cela donne un point qui derive d'un facteur Z -- donc, a zoom 6, un reticule
 * a six fois la bonne distance du centre.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedProjectionGlobeZoomTest,
	"Worldseed.Globe.LaProjectionTientAToutZoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedProjectionGlobeZoomTest::RunTest(const FString& Parameters)
{
	// DES POINTS EXCENTRES, jamais le seul centre : au centre, toute erreur
	// d'echelle autour du centre est invisible par construction. On evite en
	// revanche le limbe exact, mal conditionne PAR NATURE -- la derivee de
	// l'arc sinus y diverge, et ce depot rend deja deux chiffres separes pour
	// cette raison dans `ProbePointage`.
	const TArray<TPair<float, float>> Points = {
		{ 0.0f, 0.0f },       // le centre du disque a bascule nulle : temoin trivial
		{ 12.0f, 25.0f },
		{ -34.0f, 350.0f },
		{ 58.0f, 95.0f },
		{ -67.0f, 190.0f },
		{ 5.0f, 300.0f },
	};
	const TArray<float> Zooms = { 1.0f, 1.5f, 2.0f, 4.0f, 6.0f };

	int32 Verifies = 0;
	double PireEcartDeg = 0.0;
	double PireEcartTemoin = 0.0;

	for (const float Zoom : Zooms)
	{
		WorldseedGlobe::FGlobeSettings Reglages;
		Reglages.Zoom = Zoom;
		Reglages.TiltDeg = 18.0f;
		Reglages.LongitudeOffsetDeg = 40.0f;

		const WorldseedGlobe::FCadreGlobe Cadre = WorldseedGlobe::CadreGlobe(Reglages);

		for (const TPair<float, float>& P : Points)
		{
			float X = 0.0f;
			float Y = 0.0f;
			if (!WorldseedGlobe::CadreDepuisLatLon(P.Key, P.Value, Cadre, X, Y))
			{
				// Face cachee : il n'y a rien a pointer, et c'est correct.
				continue;
			}

			const WorldseedGlobe::FPointeGlobe Retour =
				WorldseedGlobe::PointerCadre(X, Y, Cadre);

			if (!TestTrue(TEXT("le point projete retombe sur le globe"),
				Retour.bSurLeGlobe))
			{
				continue;
			}

			// La longitude s'enroule : 359,9 et 0,1 sont voisines.
			double EcartLon = FMath::Abs(Retour.LongitudeDeg - P.Value);
			EcartLon = FMath::Min(EcartLon, 360.0 - EcartLon);

			PireEcartDeg = FMath::Max(PireEcartDeg,
				FMath::Max(static_cast<double>(FMath::Abs(Retour.LatitudeDeg - P.Key)),
					EcartLon));

			// LE TEMOIN : la meme lecture faite avec la CONSTANTE au lieu du
			// rayon apparent, c'est-a-dire la faute que ce chantier a otee.
			WorldseedGlobe::FCadreGlobe Faux = Cadre;
			Faux.RayonApparent = WorldseedGlobe::RayonDisque;
			const WorldseedGlobe::FPointeGlobe Derive =
				WorldseedGlobe::PointerCadre(X, Y, Faux);

			double EcartFaux = FMath::Abs(Derive.LongitudeDeg - P.Value);
			EcartFaux = FMath::Min(EcartFaux, 360.0 - EcartFaux);
			PireEcartTemoin = FMath::Max(PireEcartTemoin,
				FMath::Max(static_cast<double>(
					FMath::Abs(Derive.LatitudeDeg - P.Key)), EcartFaux));

			++Verifies;
		}
	}

	AddInfo(FString::Printf(
		TEXT("%d points verifies sur %d zooms, pire ecart %.6f deg"),
		Verifies, Zooms.Num(), PireEcartDeg));

	TestTrue(TEXT("des points ont ete verifies a chaque zoom"),
		Verifies >= Zooms.Num() * 3);

	TestTrue(FString::Printf(
		TEXT("l'aller-retour est exact a tous les zooms (pire ecart %.6f deg)"),
		PireEcartDeg),
		PireEcartDeg < 0.01);

	// SANS CE CONTROLE, LE TEST NE PROUVERAIT RIEN. Les deux fonctions lisent
	// le meme champ : elles s'accorderaient meme sur une valeur absurde. Ce
	// qu'on verifie ici, c'est que la valeur employee est bien celle qui porte
	// le zoom -- donc qu'une regression vers la constante se verrait.
	TestTrue(FString::Printf(
		TEXT("la constante, elle, ferait deriver le point (pire ecart %.2f deg)"),
		PireEcartTemoin),
		PireEcartTemoin > 5.0);
	return true;
}

/**
 * DU PIXEL CLIQUE AU PIXEL DU RETICULE, LA BOUCLE COMPLETE.
 *
 * C'est le trajet reel d'un repere, et aucun test ne le parcourait :
 *
 *     pixel -> CadreDepuisUV -> PointerCadre -> lat/lon        (le clic)
 *     lat/lon -> CadreDepuisLatLon -> PixelDepuisCadre         (le dessin)
 *
 * Le repere n'est PAS garde en pixels -- le globe tourne et se zoome, donc un
 * pixel retenu serait faux des la trame suivante -- il est garde en latitude
 * et longitude, et redessine. Le reticule ne tombe donc sous le curseur que
 * si les deux moities emploient LE MEME CADRE.
 *
 * ⚠ CE TEST N'AURAIT PAS ATTRAPE LE DEFAUT QU'IL DOCUMENTE, et il faut le
 * dire. Celui-ci etait que `PointerSurLeGlobe` fabriquait son cadre en
 * recopiant des champs a la main et OUBLIAIT le zoom : les deux moities
 * etaient justes, elles ne parlaient simplement pas du meme globe. Un test
 * qui pose lui-meme le cadre ne peut pas voir cela -- c'est la limite de
 * toute mesure qui fabrique ses entrees. Ce que ce test garde est la chaine
 * de CONVERSION, jamais eprouvee ; ce qui garde l'autre defaut est
 * structurel, `UWorldseedMenuWidget::PoserLaProjection`.
 *
 * LE TEMOIN, lui, chiffre ce que l'oubli coutait : la meme boucle avec le
 * cadre du zoom UN pour le clic et le cadre zoome pour le dessin.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedReticuleBoucleTest,
	"Worldseed.Globe.LeReticuleRevientAuPixelClique",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedReticuleBoucleTest::RunTest(const FString& Parameters)
{
	constexpr int32 Res = 1024;

	// DES PIXELS EXCENTRES, mais dans le disque : au centre, toute erreur
	// d'echelle autour du centre est invisible par construction.
	const TArray<FVector2f> Pixels = {
		FVector2f(512.0f, 512.0f),
		FVector2f(640.0f, 470.0f),
		FVector2f(430.0f, 600.0f),
		FVector2f(512.0f, 330.0f),
		FVector2f(700.0f, 700.0f),
	};

	double PireEcart = 0.0;
	double PireEcartTemoin = 0.0;
	int32 Verifies = 0;

	for (const float Zoom : { 1.0f, 2.0f, 4.0f, 6.0f })
	{
		WorldseedGlobe::FGlobeSettings Reglages;
		Reglages.Zoom = Zoom;
		Reglages.TiltDeg = 18.0f;
		Reglages.LongitudeOffsetDeg = 40.0f;

		const WorldseedGlobe::FCadreGlobe Cadre = WorldseedGlobe::CadreGlobe(Reglages);

		// Le cadre que l'ancien pointage fabriquait : tout sauf le zoom.
		WorldseedGlobe::FGlobeSettings Oublie = Reglages;
		Oublie.Zoom = 1.0f;
		const WorldseedGlobe::FCadreGlobe CadreOublie =
			WorldseedGlobe::CadreGlobe(Oublie);

		for (const FVector2f& P : Pixels)
		{
			// --- le clic ---------------------------------------------------
			float CX = 0.0f;
			float CY = 0.0f;
			WorldseedGlobe::CadreDepuisUV(
				P.X / static_cast<float>(Res - 1),
				P.Y / static_cast<float>(Res - 1), CX, CY);

			const WorldseedGlobe::FPointeGlobe Vise =
				WorldseedGlobe::PointerCadre(CX, CY, Cadre);
			if (!Vise.bSurLeGlobe)
			{
				continue;   // l'espace autour du disque : rien a poser
			}

			// --- le dessin -------------------------------------------------
			float RX = 0.0f;
			float RY = 0.0f;
			if (!TestTrue(TEXT("le point vise se redessine sur la face visible"),
				WorldseedGlobe::CadreDepuisLatLon(
					Vise.LatitudeDeg, Vise.LongitudeDeg, Cadre, RX, RY)))
			{
				continue;
			}

			float PX = 0.0f;
			float PY = 0.0f;
			WorldseedGlobe::PixelDepuisCadre(RX, RY, Res, PX, PY);
			PireEcart = FMath::Max(PireEcart,
				static_cast<double>(FVector2f(PX - P.X, PY - P.Y).Size()));

			// LE TEMOIN : le clic lu sur le cadre NON zoome, le dessin sur le
			// bon. C'est exactement ce que faisait le menu.
			const WorldseedGlobe::FPointeGlobe Derive =
				WorldseedGlobe::PointerCadre(CX, CY, CadreOublie);
			float TX = 0.0f;
			float TY = 0.0f;
			if (Derive.bSurLeGlobe
				&& WorldseedGlobe::CadreDepuisLatLon(
					Derive.LatitudeDeg, Derive.LongitudeDeg, Cadre, TX, TY))
			{
				float QX = 0.0f;
				float QY = 0.0f;
				WorldseedGlobe::PixelDepuisCadre(TX, TY, Res, QX, QY);
				PireEcartTemoin = FMath::Max(PireEcartTemoin,
					static_cast<double>(FVector2f(QX - P.X, QY - P.Y).Size()));
			}

			++Verifies;
		}
	}

	AddInfo(FString::Printf(
		TEXT("%d points verifies, pire ecart %.4f px ; zoom oublie : %.1f px"),
		Verifies, PireEcart, PireEcartTemoin));

	TestTrue(TEXT("des points ont ete verifies"), Verifies >= 8);

	// Un demi-pixel : le reticule fait une vingtaine de pixels de rayon, donc
	// c'est trois ordres de grandeur sous ce qui se verrait.
	TestTrue(FString::Printf(
		TEXT("le reticule revient au pixel clique (%.4f px)"), PireEcart),
		PireEcart < 0.5);

	// SANS CE CONTROLE LE TEST NE PROUVERAIT RIEN : les deux moities lisent le
	// meme cadre, donc elles s'accorderaient meme sur un cadre absurde.
	TestTrue(FString::Printf(
		TEXT("oublier le zoom deplace bien le reticule (%.1f px)"),
		PireEcartTemoin),
		PireEcartTemoin > 20.0);
	return true;
}

/**
 * LE DISQUE GRANDIT AVEC LE ZOOM, ET C'EST TOUT CE QUE LE ZOOM FAIT.
 *
 * Un meme point du globe doit s'eloigner du centre du cadre proportionnellement
 * au zoom. C'est la propriete qui distingue « agrandir le globe » de « tourner
 * le globe » : si elle tombait, le zoom deplacerait la geometrie au lieu de la
 * mettre a l'echelle, et le relief glisserait sous le reticule.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedGlobeZoomEchelleTest,
	"Worldseed.Globe.LeZoomEstUneMiseALEchelle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedGlobeZoomEchelleTest::RunTest(const FString& Parameters)
{
	WorldseedGlobe::FGlobeSettings Base;
	Base.TiltDeg = 18.0f;
	Base.LongitudeOffsetDeg = 40.0f;

	const WorldseedGlobe::FCadreGlobe Un = WorldseedGlobe::CadreGlobe(Base);

	double PireEcartRelatif = 0.0;
	int32 Verifies = 0;

	for (const float Zoom : { 1.5f, 2.0f, 4.0f, 6.0f })
	{
		WorldseedGlobe::FGlobeSettings Zoome = Base;
		Zoome.Zoom = Zoom;
		const WorldseedGlobe::FCadreGlobe Cadre = WorldseedGlobe::CadreGlobe(Zoome);

		for (int32 I = 0; I < 12; ++I)
		{
			const float Lat = -60.0f + I * 10.0f;
			const float Lon = 20.0f + I * 7.0f;

			float X1 = 0.0f, Y1 = 0.0f, XZ = 0.0f, YZ = 0.0f;
			if (!WorldseedGlobe::CadreDepuisLatLon(Lat, Lon, Un, X1, Y1)
				|| !WorldseedGlobe::CadreDepuisLatLon(Lat, Lon, Cadre, XZ, YZ))
			{
				continue;
			}

			const double R1 = FMath::Sqrt(X1 * X1 + Y1 * Y1);
			const double RZ = FMath::Sqrt(XZ * XZ + YZ * YZ);
			if (R1 < KINDA_SMALL_NUMBER)
			{
				// Le centre reste le centre : rien a mettre a l'echelle.
				continue;
			}

			PireEcartRelatif = FMath::Max(PireEcartRelatif,
				FMath::Abs(RZ / R1 - static_cast<double>(Zoom)) / Zoom);
			++Verifies;
		}
	}

	TestTrue(TEXT("des points ont ete verifies"), Verifies > 20);
	TestTrue(FString::Printf(
		TEXT("la distance au centre suit exactement le zoom (ecart relatif %.6f)"),
		PireEcartRelatif),
		PireEcartRelatif < 1e-4);
	return true;
}

/**
 * `AbsoluteToLocal` DEFAIT LA TRANSFORMEE DE RENDU, PIVOT COMPRIS.
 *
 * C'est la premisse sur laquelle `PointerSurLeGlobe` repose pour passer de
 * l'ecran au texel. Elle ne porte plus de zoom -- celui-ci a demenage dans la
 * projection -- mais elle porte toujours l'echelle DPI et la mise en page, et
 * une version du moteur qui cesserait de composer le pivot ferait deriver le
 * reticule en silence.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedPointageGlobePivotTest,
	"Worldseed.Globe.LaGeometrieDefaitLeRendu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedPointageGlobePivotTest::RunTest(const FString& Parameters)
{
	const FGeometry Racine = FGeometry::MakeRoot(
		FVector2f(1600.0f, 900.0f), FSlateLayoutTransform(0.83f));

	const TArray<FVector2f> Pivots = {
		FVector2f(0.5f, 0.5f),
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
