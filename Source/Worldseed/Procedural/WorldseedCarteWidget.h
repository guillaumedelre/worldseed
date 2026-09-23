// Worldseed - le widget de la carte plein ecran : dessin, souris, clavier.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class UWorldseedCarteEcran;

/**
 * LA CARTE A L'ECRAN.
 *
 * POURQUOI UN `SCompoundWidget` ET NON UN `UUserWidget`. Le menu est un
 * `UUserWidget` parce qu'un mode de jeu le pose ; ici le sous-systeme ajoute
 * son contenu au viewport, comme la minimap. Et surtout ce widget DESSINE :
 * les marqueurs sont traces a la main dans `OnPaint`, ce qu'aucun `SImage` ne
 * sait faire -- et `SImage`, `SBox`, `SOverlay` n'exposent de toute facon
 * aucun gestionnaire de souris.
 *
 * IL NE TIQUE PAS, ET C'EST VOULU. `UUserWidget` est en `TickFrequency::Auto`,
 * qui coupe le tick d'un widget sans tick Blueprint -- le menu a du passer par
 * un timer pour cela. Ici la question ne se pose pas : le widget ne garde
 * aucun etat anime, il lit le sous-systeme au moment de peindre.
 *
 * LE FOND NE SE REPEINT JAMAIS. La carte est cuite une fois a la resolution de
 * la grille ; zoomer et se deplacer ne font que changer la REGION UV de la
 * brosse, ce que le GPU fait pour rien. C'est ce qui rend la carte immediate.
 */
class WORLDSEED_API SWorldseedCarte : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWorldseedCarte) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UWorldseedCarteEcran>, Carte)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// IL PREND LE FOCUS CLAVIER : sans cela Tab et Echap ne lui arriveraient
	// jamais, et Tab partirait dans la navigation entre widgets de Slate.
	virtual bool SupportsKeyboardFocus() const override { return true; }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	virtual FReply OnKeyDown(const FGeometry& MyGeometry,
		const FKeyEvent& InKeyEvent) override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry,
		const FPointerEvent& CursorEvent) const override;

private:
	TWeakObjectPtr<UWorldseedCarteEcran> Carte;

	/**
	 * LA BROSSE APPARTIENT AU WIDGET QUI LA DESSINE, et sa region UV change a
	 * chaque peinture -- d'ou `mutable`. Une brosse temporaire laisserait un
	 * pointeur pendant : Slate conserve celle qu'on lui donne.
	 */
	mutable FSlateBrush BrosseFond;

	/** Vrai entre l'enfoncement et le relachement du bouton gauche. */
	bool bGlisse = false;

	FVector2D DerniereSouris = FVector2D::ZeroVector;

	/**
	 * LE CHEMIN CUMULE, ET NON L'ECART ENTRE DEPART ET ARRIVEE. Un aller-retour
	 * revient a son point de depart : mesurer les deux extremites declarerait
	 * CLIC un glisser qui a promene la carte et l'a ramenee, et l'on poserait
	 * un repere sans l'avoir demande. Le globe du menu porte la meme note.
	 */
	float CumulGlisse = 0.0f;

	/** Vrai des la premiere peinture : la ligne de diagnostic ne sort qu'une fois. */
	mutable bool bPremierPaint = false;
};
