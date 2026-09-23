// Worldseed - le widget de la carte plein ecran.

#include "Procedural/WorldseedCarteWidget.h"

#include "Procedural/WorldseedCarte.h"
#include "Procedural/WorldseedCarteEcran.h"
#include "Procedural/WorldseedIcones.h"
#include "Procedural/WorldseedVoxelTerrain.h"

#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace WorldseedCarteUI
{
	/** Taille des glyphes de marqueur, en pixels de mise en page. */
	constexpr float TailleIconePx = 22.0f;

	/** Le pas de zoom est MULTIPLICATIF : meme ressenti a toutes les echelles. */
	constexpr float PasZoom = 1.18f;

	/** Au-dela de ce chemin cumule, le geste est un glisser et non un clic. */
	constexpr float SeuilClicPx = 6.0f;

	/** Couleur par genre de lieu. */
	FLinearColor Teinte(FWorldseedMarqueur::EGenre Genre)
	{
		switch (Genre)
		{
		case FWorldseedMarqueur::EGenre::Arche:   return FLinearColor(1.00f, 0.86f, 0.42f);
		case FWorldseedMarqueur::EGenre::Gouffre: return FLinearColor(0.72f, 0.55f, 1.00f);
		case FWorldseedMarqueur::EGenre::Doline:  return FLinearColor(1.00f, 0.60f, 0.45f);
		case FWorldseedMarqueur::EGenre::Table:   return FLinearColor(0.60f, 0.85f, 1.00f);
		case FWorldseedMarqueur::EGenre::Canyon:  return FLinearColor(0.55f, 1.00f, 0.75f);
		default:                                  return FLinearColor::White;
		}
	}

	uint32 Glyphe(FWorldseedMarqueur::EGenre Genre)
	{
		switch (Genre)
		{
		case FWorldseedMarqueur::EGenre::Arche:   return WorldseedIcone::Arche;
		case FWorldseedMarqueur::EGenre::Gouffre: return WorldseedIcone::Gouffre;
		case FWorldseedMarqueur::EGenre::Doline:  return WorldseedIcone::Doline;
		case FWorldseedMarqueur::EGenre::Table:   return WorldseedIcone::Table;
		case FWorldseedMarqueur::EGenre::Canyon:  return WorldseedIcone::Canyon;
		default:                                  return WorldseedIcone::Repere;
		}
	}
}


void SWorldseedCarte::Construct(const FArguments& InArgs)
{
	Carte = InArgs._Carte;

	BrosseFond.DrawAs = ESlateBrushDrawType::Image;
	BrosseFond.TintColor = FSlateColor(FLinearColor::White);

	// Un rectangle plein : la brosse blanche du style de base, teintee au
	// dessin. Une brosse sans ressource ne dessinerait rien.
	BrosseVide = *FCoreStyle::Get().GetBrush("WhiteBrush");

	// ON MANGE LES CLICS, contrairement a la minimap qui est `HitTestInvisible`
	// « sans quoi le widget mange tous les clics ». Ici c'est l'inverse qu'on
	// veut : un clic qui raterait la carte partirait au jeu, sous elle.
	SetVisibility(EVisibility::Visible);
}


int32 SWorldseedCarte::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	UWorldseedCarteEcran* const C = Carte.Get();
	if (!C)
	{
		return LayerId;
	}

	const FVector2D Taille = AllottedGeometry.GetLocalSize();
	if (Taille.X < 8.0 || Taille.Y < 8.0)
	{
		return LayerId;
	}

	// ON BORNE AVEC LA GEOMETRIE QUI SERT A PEINDRE, et c'est la seule qui
	// compte. L'ouverture borne deja, mais sur la taille du VIEWPORT : si le
	// widget n'occupe pas exactement cette surface, la vue deborde du monde et
	// les bords de la texture s'etirent en bavures -- vu a l'image, une bande
	// rayee en haut de l'ecran. Ici la question ne se pose plus.
	C->BornerVue(Taille, AllottedGeometry.Scale);

	const FIntPoint Tex = C->TailleCuisson();
	const WorldseedCarte::FParamsFenetre Vue = C->ParamsVue(Taille);
	const double LargeurMonde = C->LargeurMondeM();

	if (!bPremierPaint)
	{
		bPremierPaint = true;

		// UNE LIGNE, UNE SEULE FOIS. Devant une carte qui bave sur un bord,
		// elle dit tout de suite si c'est la vue qui deborde ou la texture qui
		// manque -- et le facteur DPI explique a lui seul une echelle deux fois
		// fausse sur un ecran dense.
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] carte peinte : widget %.0f x %.0f (DPI %.2f), ")
			TEXT("%.1f m/px, centre (%.0f, %.0f) m, texture %d x %d"),
			Taille.X, Taille.Y, AllottedGeometry.Scale, C->MetresParPixel,
			C->CentreVueM.X, C->CentreVueM.Y, Tex.X, Tex.Y);
	}

	// --- le vide autour du monde ---------------------------------------------
	//
	// LA CARTE NE REMPLIT PAS FORCEMENT L'ECRAN, et c'est ce qui permet de voir
	// le monde ENTIER. Il est en 2:1, l'ecran en 16:9 : cale sur la hauteur, on
	// perdait onze pour cent de la largeur ; cale sur la largeur, il reste des
	// bandes en haut et en bas. Elles portent la meme couleur que le hors-monde
	// du peintre -- au-dela d'un pole il n'y a rien, et cela doit se voir.
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(), &BrosseVide,
		ESlateDrawEffect::None, FLinearColor(0.06f, 0.06f, 0.08f, 1.0f));

	// --- le fond -------------------------------------------------------------
	if (Tex.X > 0 && Tex.Y > 0)
	{
		// LA REGION UV SE DEDUIT DE LA MEME PROJECTION QUE TOUT LE RESTE. La
		// texture a ete cuite avec sa propre fenetre ; on demande donc a quels
		// texels correspondent les deux coins de la vue. Recalculer l'inversion
		// nord/sud ici en serait une SECONDE ecriture, et l'en-tete de
		// `WorldseedCarte.h` l'interdit en toutes lettres.
		const WorldseedCarte::FParamsFenetre Cuisson =
			WorldseedCarte::FParamsFenetre::Rectangle(LargeurMonde * 0.5, Tex.X, Tex.Y);

		// LE RECTANGLE DE MONDE REELLEMENT VISIBLE : l'intersection de la vue
		// et du monde, et non la vue entiere. Au-dela des bords de la texture
		// l'adressage est en `Clamp` : peindre la vue entiere y etirerait la
		// derniere ligne en bavures -- ce qu'on voyait avant de borner.
		const double DemiLargeurMonde = LargeurMonde * 0.5;
		const double DemiHauteurMonde = LargeurMonde * 0.25;   // le monde est en 2:1

		const double X0 = FMath::Max(Vue.CentreXm - Vue.DemiPorteeXm, -DemiLargeurMonde);
		const double X1 = FMath::Min(Vue.CentreXm + Vue.DemiPorteeXm, DemiLargeurMonde);
		const double Y0 = FMath::Max(Vue.CentreYm - Vue.DemiPorteeYm, -DemiHauteurMonde);
		const double Y1 = FMath::Min(Vue.CentreYm + Vue.DemiPorteeYm, DemiHauteurMonde);

		if (X1 > X0 && Y1 > Y0)
		{
			// LES DEUX PROJECTIONS, SUR LES MEMES DEUX COINS : l'une dit ou
			// dessiner a l'ecran, l'autre quels texels y mettre. Y1 est le
			// coin HAUT, la ligne 0 etant au nord.
			double EcranG = 0.0, EcranH = 0.0, EcranD = 0.0, EcranB = 0.0;
			WorldseedCarte::PixelDuMetre(Vue, 0.0, X0, Y1, EcranG, EcranH);
			WorldseedCarte::PixelDuMetre(Vue, 0.0, X1, Y0, EcranD, EcranB);

			double UG = 0.0, VH = 0.0, UD = 0.0, VB = 0.0;
			WorldseedCarte::PixelDuMetre(Cuisson, 0.0, X0, Y1, UG, VH);
			WorldseedCarte::PixelDuMetre(Cuisson, 0.0, X1, Y0, UD, VB);

			// `PixelDuMetre` rend le CENTRE d'un pixel ; un rectangle et une
			// region UV veulent leurs BORDS, d'ou le demi-pixel.
			const FBox2f Region(
				FVector2f(static_cast<float>((UG + 0.5) / Tex.X),
					static_cast<float>((VH + 0.5) / Tex.Y)),
				FVector2f(static_cast<float>((UD + 0.5) / Tex.X),
					static_cast<float>((VB + 0.5) / Tex.Y)));

			const FVector2D Coin(EcranG + 0.5, EcranH + 0.5);
			const FVector2D Dim(EcranD - EcranG, EcranB - EcranH);

			// UNE `FBox2f` PAR DEFAUT EST INVALIDE, et le batcher teste
			// `bIsValid` avant de la lire : construite par ses deux coins,
			// celle-ci l'est.
			BrosseFond.SetResourceObject(C->BrosseCarte()->GetResourceObject());
			BrosseFond.ImageSize = Dim;
			BrosseFond.SetUVRegion(Region);

			FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
				AllottedGeometry.ToPaintGeometry(Dim, FSlateLayoutTransform(Coin)),
				&BrosseFond, ESlateDrawEffect::None, FLinearColor::White);
		}
	}

	// --- les marqueurs -------------------------------------------------------
	const int32 CoucheMarqueurs = LayerId + 1;
	const FSlateFontInfo Police = WorldseedIcones::Police(WorldseedCarteUI::TailleIconePx);
	const TSharedRef<FSlateFontMeasure> Mesure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	auto PoserGlyphe = [&](const FVector2D& Position, uint32 Codepoint,
		const FLinearColor& Couleur, bool bPointeEnBas = false)
	{
		const FText Texte = WorldseedIcones::Glyphe(Codepoint);
		const FVector2D Dim = Mesure->Measure(Texte.ToString(), Police);

		// CENTRE SUR LE LIEU -- sauf pour une EPINGLE, qui designe par sa
		// POINTE. Centree, elle montre un point situe une dizaine de pixels
		// plus bas que celui qu'on a choisi : vu a l'image, et c'est toute la
		// difference entre un repere et un a-peu-pres.
		const FVector2D Coin = bPointeEnBas
			? FVector2D(Position.X - Dim.X * 0.5, Position.Y - Dim.Y)
			: Position - Dim * 0.5;

		// L'OMBRE D'ABORD : un glyphe clair sur une mer claire ou un glyphe
		// sombre sur une plage disparait sans elle.
		FSlateDrawElement::MakeText(OutDrawElements, CoucheMarqueurs,
			AllottedGeometry.ToPaintGeometry(Dim, FSlateLayoutTransform(Coin + FVector2D(1.5, 1.5))),
			Texte, Police, ESlateDrawEffect::None, FLinearColor(0.0f, 0.0f, 0.0f, 0.75f));

		FSlateDrawElement::MakeText(OutDrawElements, CoucheMarqueurs + 1,
			AllottedGeometry.ToPaintGeometry(Dim, FSlateLayoutTransform(Coin)),
			Texte, Police, ESlateDrawEffect::None, Couleur);
	};

	for (const FWorldseedMarqueur& M : C->Marqueurs())
	{
		// LE FILTRE PAR ECHELLE, et il n'est pas cosmetique : le monde porte
		// plus de cinq cents gouffres. Vus tous ensemble a l'echelle du monde,
		// ils ne font pas une carte, ils font un voile.
		if (C->MetresParPixel > M.EchelleMaxM)
		{
			continue;
		}

		double PX = 0.0, PY = 0.0;
		if (!WorldseedCarte::PixelDuMetre(Vue, LargeurMonde, M.PositionM.X,
			M.PositionM.Y, PX, PY))
		{
			continue;   // hors de l'ecran : rien a dessiner
		}

		PoserGlyphe(FVector2D(PX, PY), WorldseedCarteUI::Glyphe(M.Genre),
			WorldseedCarteUI::Teinte(M.Genre));
	}

	// --- le repere du joueur -------------------------------------------------
	if (C->ARepere())
	{
		double PX = 0.0, PY = 0.0;
		if (WorldseedCarte::PixelDuMetre(Vue, LargeurMonde, C->RepereM().X,
			C->RepereM().Y, PX, PY))
		{
			PoserGlyphe(FVector2D(PX, PY), WorldseedIcone::Repere,
				FLinearColor(1.0f, 0.35f, 0.35f), true);
		}
	}

	// --- le joueur, et son cap -----------------------------------------------
	//
	// UN TRIANGLE TRACE, ET NON UN GLYPHE : il doit TOURNER avec le regard, et
	// Slate ne sait pas faire pivoter du texte. Les lignes, elles, se placent
	// ou l'on veut.
	if (const AWorldseedVoxelTerrain* const T = C->Terrain())
	{
		const FWorldseedReperePlayer R = T->ReperePlayer();
		double PX = 0.0, PY = 0.0;
		if (R.bValide && WorldseedCarte::PixelDuMetre(Vue, LargeurMonde, R.Xm, R.Ym, PX, PY))
		{
			// Zero au NORD, croissant vers l'EST ; la ligne 0 de l'ecran est au
			// nord et l'axe des lignes DESCEND, d'ou (sin, -cos).
			const float A = FMath::DegreesToRadians(R.CapDeg);
			const FVector2D Avant(FMath::Sin(A), -FMath::Cos(A));
			const FVector2D Cote(-Avant.Y, Avant.X);
			const FVector2D P(PX, PY);

			TArray<FVector2D> Points;
			Points.Add(P + Avant * 13.0);
			Points.Add(P - Avant * 7.0 + Cote * 8.0);
			Points.Add(P - Avant * 3.0);
			Points.Add(P - Avant * 7.0 - Cote * 8.0);
			Points.Add(P + Avant * 13.0);

			FSlateDrawElement::MakeLines(OutDrawElements, CoucheMarqueurs + 2,
				AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None,
				FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), true, 4.0f);
			FSlateDrawElement::MakeLines(OutDrawElements, CoucheMarqueurs + 3,
				AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None,
				FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), true, 2.0f);
		}
	}

	return CoucheMarqueurs + 4;
}


// -------------------------------------------------------------- le clavier

FReply SWorldseedCarte::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	UWorldseedCarteEcran* const C = Carte.Get();
	if (!C)
	{
		return FReply::Unhandled();
	}

	const FKey Touche = InKeyEvent.GetKey();
	if (Touche == EKeys::Tab || Touche == EKeys::Escape)
	{
		// ECHAP EST UNE PILE : on note la trame, pour que le retour au menu ne
		// consomme pas la MEME pression -- l'ordre de tick ne se suppose pas.
		if (Touche == EKeys::Escape)
		{
			C->NoterEchapConsomme();
		}

		C->Fermer();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}


// ---------------------------------------------------------------- la souris

FReply SWorldseedCarte::OnMouseButtonDown(const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	bGlisse = true;
	CumulGlisse = 0.0f;
	DerniereSouris = MouseEvent.GetScreenSpacePosition();

	// La capture garde les evenements meme si le curseur sort du widget : sans
	// elle, un glisser un peu ample lacherait la carte en cours de route.
	return FReply::Handled().CaptureMouse(SharedThis(this));
}


FReply SWorldseedCarte::OnMouseMove(const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	UWorldseedCarteEcran* const C = Carte.Get();
	if (!bGlisse || !C)
	{
		return FReply::Unhandled();
	}

	const FVector2D Maintenant = MouseEvent.GetScreenSpacePosition();
	const FVector2D Delta = Maintenant - DerniereSouris;
	DerniereSouris = Maintenant;
	CumulGlisse += static_cast<float>(Delta.Size());

	// ON TIRE LA CARTE, on ne deplace pas une camera : le point saisi doit
	// rester sous le doigt. D'ou le signe oppose en X -- et en Y le signe est
	// encore inverse, l'ecran descendant quand le nord monte.
	const double Echelle = C->MetresParPixel / FMath::Max(MyGeometry.Scale, 0.01f);
	C->CentreVueM.X -= Delta.X * Echelle;
	C->CentreVueM.Y += Delta.Y * Echelle;
	C->BornerVue(MyGeometry.GetLocalSize(), MyGeometry.Scale);

	return FReply::Handled();
}


FReply SWorldseedCarte::OnMouseButtonUp(const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	bGlisse = false;

	UWorldseedCarteEcran* const C = Carte.Get();
	if (C && CumulGlisse <= WorldseedCarteUI::SeuilClicPx)
	{
		const FVector2D Locale = MyGeometry.AbsoluteToLocal(
			MouseEvent.GetScreenSpacePosition());

		// LA GEOMETRIE DU WIDGET, PAS L'ECRAN : le menu porte deja cette note,
		// et un clic rapporte au mauvais cadre se decale de toute une marge.
		const WorldseedCarte::FParamsFenetre Vue = C->ParamsVue(MyGeometry.GetLocalSize());

		double Xm = 0.0, Ym = 0.0;
		WorldseedCarte::MetresDuPixel(Vue,
			FMath::FloorToInt(Locale.X), FMath::FloorToInt(Locale.Y), Xm, Ym);

		if (MouseEvent.IsControlDown())
		{
			if (AWorldseedVoxelTerrain* const T = C->Terrain())
			{
				UE_LOG(LogTemp, Log,
					TEXT("[Worldseed] carte : teleportation vers (%.0f, %.0f) m"), Xm, Ym);
				T->TeleporterJoueur(Xm, Ym);
				C->Fermer();
			}
		}
		else
		{
			C->PoserRepere(FVector2D(Xm, Ym));
		}
	}

	return FReply::Handled().ReleaseMouseCapture();
}


FReply SWorldseedCarte::OnMouseWheel(const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	UWorldseedCarteEcran* const C = Carte.Get();
	const float Delta = MouseEvent.GetWheelDelta();
	if (!C || FMath::IsNearlyZero(Delta))
	{
		return FReply::Unhandled();
	}

	const FVector2D Taille = MyGeometry.GetLocalSize();
	const FVector2D Locale = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	// LE POINT SOUS LE CURSEUR NE DOIT PAS BOUGER. On le retient AVANT, on
	// change l'echelle, et l'on ramene le centre pour qu'il retombe dessous.
	// Un zoom centre sur l'ecran oblige a rattraper a la main ce qu'on
	// regardait, et se ressent immediatement comme mou.
	const WorldseedCarte::FParamsFenetre Avant = C->ParamsVue(Taille);
	double AvantX = 0.0, AvantY = 0.0;
	WorldseedCarte::MetresDuPixel(Avant,
		FMath::FloorToInt(Locale.X), FMath::FloorToInt(Locale.Y), AvantX, AvantY);

	// LE PAS EST MULTIPLICATIF, PAS ADDITIF : un pas constant donne un zoom
	// nerveux de pres et mou de loin.
	C->MetresParPixel *= FMath::Pow(WorldseedCarteUI::PasZoom, -Delta);
	C->BornerVue(Taille, MyGeometry.Scale);

	const WorldseedCarte::FParamsFenetre Apres = C->ParamsVue(Taille);
	double ApresX = 0.0, ApresY = 0.0;
	WorldseedCarte::MetresDuPixel(Apres,
		FMath::FloorToInt(Locale.X), FMath::FloorToInt(Locale.Y), ApresX, ApresY);

	C->CentreVueM.X += AvantX - ApresX;
	C->CentreVueM.Y += AvantY - ApresY;
	C->BornerVue(Taille, MyGeometry.Scale);

	return FReply::Handled();
}


FCursorReply SWorldseedCarte::OnCursorQuery(const FGeometry& MyGeometry,
	const FPointerEvent& CursorEvent) const
{
	return FCursorReply::Cursor(bGlisse ? EMouseCursor::GrabHandClosed
		: EMouseCursor::GrabHand);
}
