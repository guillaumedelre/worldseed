// Worldseed - ecran d'entree : choix du seed et de la taille, apercu.

#include "Procedural/WorldseedMenuWidget.h"
#include "Procedural/WorldseedGameInstance.h"
#include "Procedural/WorldseedCache.h"
#include "Procedural/WorldseedGlobe.h"
#include "Procedural/WorldseedGrid.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedRules.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Async/Async.h"

namespace
{
	/**
	 * Tailles proposees, en metres.
	 *
	 * 8000 est la taille de REFERENCE : world_rules.json est calibre pour elle.
	 * Trois regles y sont metriques — shelfWidthKm 0,35, oceanBorderKm 0,375,
	 * mountainWidthKm 1,5 — et ne retrecissent pas avec la carte. Sur 500 m, le
	 * plateau continental couvre 70 % du monde et sa rampe n'aboutit jamais :
	 * les fonds remontent et l'ocean plafonne vers -98 m au lieu de -300. Les
	 * tailles inferieures restent utiles pour iterer vite, mais seules les
	 * valeurs relevees a 8 km sont conformes a la Terre.
	 */
	static const TArray<float> MapSizeChoices = {
		500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f, 32000.0f };

	/**
	 * Plafond de la grille de SIMULATION.
	 *
	 * Le commentaire d'origine -- « au-dela, le maillage devient trop lourd
	 * sans decoupage en chunks » -- est PERIME : le terrain est desormais
	 * maille en voxels diffuses autour du joueur, et la grille de simulation
	 * n'est plus ce qui se dessine. Ce qui plafonne maintenant, c'est le COUT
	 * de la chaine : 2048x1024 coute 59 s sur un monde de 64 km, dont 39 pour
	 * l'erosion couplee. Doubler la grille quadruplerait ce chiffre.
	 *
	 * CONSEQUENCE A CONNAITRE : sur une grande carte, la maille de simulation
	 * s'elargit. 31,3 m a 64 km contre 7,8 m a 16 km. Le detail fin ne vient
	 * plus de la grille mais de la couche voxel, qui travaille au metre.
	 */
	constexpr int32 MaxResolution = 1024;

	/** Resolution visee : environ un quad tous les 2 m, plafonnee. */
	int32 ResolutionForSize(float SizeMeters)
	{
		return FMath::Clamp(FMath::RoundToInt(SizeMeters * 0.5f), 128, MaxResolution);
	}

	FString LabelForSize(float SizeMeters)
	{
		if (FMath::IsNearlyEqual(SizeMeters, 8000.0f))
		{
			// LA TAILLE DE REFERENCE N'EST PLUS LA PLUS GRANDE. Huit kilometres
			// reste la hauteur sur laquelle le calage metrique est fait --
			// `world.sizeKm`, dont `VerticalScale` tire son rapport -- mais la
			// carte peut desormais etre quatre fois plus grande, et le relief
			// suit alors automatiquement : -1244 a 1205 m mesures a 64 x 32 km,
			// contre -331 a 405 a 16 x 8.
			return FString::Printf(TEXT("%.0f x %.0f km  (reference)"),
				SizeMeters / 500.0f, SizeMeters / 1000.0f);
		}
		if (SizeMeters >= 1000.0f)
		{
			return FString::Printf(TEXT("%.0f x %.0f km"), SizeMeters / 500.0f, SizeMeters / 1000.0f);
		}
		return FString::Printf(TEXT("%.0f x %.0f m"), SizeMeters * 2.0f, SizeMeters);
	}
}

namespace
{
	/** Palette. Une seule source pour toutes les couleurs de l ecran. */
	const FLinearColor ColBackground(0.030f, 0.035f, 0.048f, 1.0f);
	const FLinearColor ColPanel(0.072f, 0.082f, 0.104f, 0.94f);
	const FLinearColor ColTextPrimary(0.92f, 0.93f, 0.96f, 1.0f);
	const FLinearColor ColTextMuted(0.52f, 0.56f, 0.64f, 1.0f);
	const FLinearColor ColAccent(0.34f, 0.62f, 0.88f, 1.0f);
	const FLinearColor ColSeparator(1.0f, 1.0f, 1.0f, 0.08f);

	/** Largeur maximale du panneau. Au-dela, on centre au lieu d etirer. */
	// Deux colonnes demandent plus large qu un empilement vertical.
	constexpr float PanelWidth = 980.0f;
	constexpr float GlobeSize = 420.0f;
}

TSharedRef<SWidget> UWorldseedMenuWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		// ------------------------------------------------------------------
		// Mise en page en DEUX COLONNES, comme les ecrans de creation de monde
		// des jeux : l'apercu occupe la gauche, les reglages la droite, et
		// l'action principale barre le bas sur toute la largeur.
		//
		// L'empilement vertical precedent avait un defaut structurel : chaque
		// nouvelle rangee poussait le bouton d'entree vers le bas jusqu'a le
		// faire sortir de l'ecran. En colonnes, l'ecran reste court quoi qu'on
		// ajoute, et la hierarchie visuelle est immediate.
		// ------------------------------------------------------------------

		auto MakeText = [this](const TCHAR* Name, const FString& Content,
			int32 Size, const FLinearColor& Color) -> UTextBlock*
		{
			UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), Name);
			T->SetText(FText::FromString(Content));
			FSlateFontInfo Font = T->GetFont();
			Font.Size = Size;
			T->SetFont(Font);
			T->SetColorAndOpacity(FSlateColor(Color));
			return T;
		};

		auto MakeButton = [this, &MakeText](const TCHAR* Name, const TCHAR* LabelName,
			const FString& Label, int32 Size, const FLinearColor& Tint) -> UButton*
		{
			UButton* B = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
			B->SetBackgroundColor(Tint);
			B->AddChild(MakeText(LabelName, Label, Size, ColTextPrimary));
			return B;
		};

		auto MakeRule = [this](const TCHAR* Name) -> UBorder*
		{
			UBorder* S = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
			S->SetBrushColor(ColSeparator);
			S->SetPadding(FMargin(0.0f, 0.5f));
			return S;
		};

		/** Intitule de section : petites capitales, discret, au-dessus du reglage. */
		auto MakeSectionLabel = [this, &MakeText](const TCHAR* Name, const FString& Label)
		{
			return MakeText(Name, Label, 10, ColTextMuted);
		};

		/** Ligne de mesure : intitule a gauche, valeur alignee a droite. */
		auto MakeStatRow = [this, &MakeText](const TCHAR* RowName, const TCHAR* LabelName,
			const TCHAR* ValueName, const FString& Label) -> TPair<UHorizontalBox*, UTextBlock*>
		{
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(
				UHorizontalBox::StaticClass(), RowName);

			UTextBlock* L = MakeText(LabelName, Label, 11, ColTextMuted);
			if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(L))
			{
				S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}

			UTextBlock* V = MakeText(ValueName, TEXT("—"), 11, ColTextPrimary);
			V->SetJustification(ETextJustify::Right);
			Row->AddChildToHorizontalBox(V);

			return TPair<UHorizontalBox*, UTextBlock*>(Row, V);
		};

		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
			UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
		WidgetTree->RootWidget = Root;

		UBorder* Background = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), TEXT("Background"));
		Background->SetBrushColor(ColBackground);
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Root->AddChild(Background)))
		{
			S->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			S->SetOffsets(FMargin(0.0f));
		}

		// Le defilement reste la seule reponse honnete a une fenetre trop
		// courte : aucun ancrage ne peut afficher un contenu plus grand que la
		// place disponible sans en masquer une partie.
		UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>(
			UScrollBox::StaticClass(), TEXT("Scroll"));
		Scroll->SetOrientation(Orient_Vertical);
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Root->AddChild(Scroll)))
		{
			S->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			S->SetOffsets(FMargin(0.0f, 20.0f, 0.0f, 20.0f));
		}

		USizeBox* PanelSize = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), TEXT("PanelSize"));
		PanelSize->SetWidthOverride(PanelWidth);
		if (UScrollBoxSlot* S = Cast<UScrollBoxSlot>(Scroll->AddChild(PanelSize)))
		{
			S->SetHorizontalAlignment(HAlign_Center);
			S->SetPadding(FMargin(16.0f, 0.0f));
		}

		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), TEXT("Panel"));
		Panel->SetBrushColor(ColPanel);
		Panel->SetPadding(FMargin(30.0f, 26.0f));
		PanelSize->AddChild(Panel);

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("Column"));
		Panel->AddChild(Column);

		// ------------------------------------------------------ en-tete ----
		{
			UTextBlock* Title = MakeText(TEXT("Title"), TEXT("WORLDSEED"), 38, ColTextPrimary);
			Column->AddChildToVerticalBox(Title);

			UTextBlock* Sub = MakeText(TEXT("Subtitle"),
				TEXT("tectonique  ·  climat  ·  erosion"), 11, ColTextMuted);
			if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(Sub))
			{
				S->SetPadding(FMargin(2.0f, 2.0f, 0.0f, 16.0f));
			}

			if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(MakeRule(TEXT("RuleTop"))))
			{
				S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 20.0f));
			}
		}

		// ------------------------------------------------- corps, 2 colonnes
		UHorizontalBox* Body = WidgetTree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(), TEXT("Body"));
		Column->AddChildToVerticalBox(Body);

		// --- colonne gauche : le globe -------------------------------------
		{
			// Le SizeBox est a l'EXTERIEUR : un ScaleBox place dans un slot
			// auto-dimensionne ne sait pas annoncer sa taille desiree et se
			// reduit a zero. Le globe disparaissait purement.
			USizeBox* GlobeBox = WidgetTree->ConstructWidget<USizeBox>(
				USizeBox::StaticClass(), TEXT("GlobeBox"));
			GlobeBox->SetWidthOverride(GlobeSize);
			GlobeBox->SetHeightOverride(GlobeSize);

			UScaleBox* GlobeScale = WidgetTree->ConstructWidget<UScaleBox>(
				UScaleBox::StaticClass(), TEXT("GlobeScale"));
			GlobeScale->SetStretch(EStretch::ScaleToFit);

			PreviewImage = WidgetTree->ConstructWidget<UImage>(
				UImage::StaticClass(), TEXT("PreviewImage"));
			PreviewImage->SetDesiredSizeOverride(FVector2D(GlobeSize, GlobeSize));

			GlobeScale->AddChild(PreviewImage);
			GlobeBox->AddChild(GlobeScale);

			if (UHorizontalBoxSlot* S = Body->AddChildToHorizontalBox(GlobeBox))
			{
				S->SetPadding(FMargin(0.0f, 0.0f, 28.0f, 0.0f));
				S->SetVerticalAlignment(VAlign_Top);
			}
		}

		// --- colonne droite : reglages et mesures --------------------------
		{
			UVerticalBox* Side = WidgetTree->ConstructWidget<UVerticalBox>(
				UVerticalBox::StaticClass(), TEXT("Side"));
			if (UHorizontalBoxSlot* S = Body->AddChildToHorizontalBox(Side))
			{
				S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}

			// --- graine ---------------------------------------------------
			Side->AddChildToVerticalBox(MakeSectionLabel(TEXT("SeedLabel"), TEXT("GRAINE")));

			UHorizontalBox* SeedRow = WidgetTree->ConstructWidget<UHorizontalBox>(
				UHorizontalBox::StaticClass(), TEXT("SeedRow"));
			if (UVerticalBoxSlot* S = Side->AddChildToVerticalBox(SeedRow))
			{
				S->SetPadding(FMargin(0.0f, 5.0f, 0.0f, 18.0f));
			}

			SeedBox = WidgetTree->ConstructWidget<UEditableTextBox>(
				UEditableTextBox::StaticClass(), TEXT("SeedBox"));
			if (UHorizontalBoxSlot* S = SeedRow->AddChildToHorizontalBox(SeedBox))
			{
				S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				S->SetVerticalAlignment(VAlign_Center);
			}

			RandomButton = MakeButton(TEXT("RandomButton"), TEXT("RandomLabel"),
				TEXT("Aleatoire"), 12, FLinearColor(0.18f, 0.20f, 0.26f, 1.0f));
			if (UHorizontalBoxSlot* S = SeedRow->AddChildToHorizontalBox(RandomButton))
			{
				S->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
			}

			// --- taille ---------------------------------------------------
			Side->AddChildToVerticalBox(
				MakeSectionLabel(TEXT("SizeLabel"), TEXT("TAILLE DU MONDE")));

			SizeCombo = WidgetTree->ConstructWidget<UComboBoxString>(
				UComboBoxString::StaticClass(), TEXT("SizeCombo"));
			if (UVerticalBoxSlot* S = Side->AddChildToVerticalBox(SizeCombo))
			{
				S->SetPadding(FMargin(0.0f, 5.0f, 0.0f, 20.0f));
			}

			// --- pack de textures -----------------------------------------
			Side->AddChildToVerticalBox(
				MakeSectionLabel(TEXT("PackLabel"), TEXT("HABILLAGE DU SOL")));

			PackCombo = WidgetTree->ConstructWidget<UComboBoxString>(
				UComboBoxString::StaticClass(), TEXT("PackCombo"));
			if (UVerticalBoxSlot* S = Side->AddChildToVerticalBox(PackCombo))
			{
				S->SetPadding(FMargin(0.0f, 5.0f, 0.0f, 4.0f));
			}

			// Ce que le pack couvre, sous la liste : trois packs sur quatre ont
			// un trou, et le savoir avant de lancer evite de chercher pourquoi
			// un desert est reste en aplat.
			PackHint = WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), TEXT("PackHint"));
			PackHint->SetColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.58f, 0.64f, 1.0f)));
			PackHint->SetAutoWrapText(true);
			if (UVerticalBoxSlot* S = Side->AddChildToVerticalBox(PackHint))
			{
				S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 20.0f));
			}

			if (UVerticalBoxSlot* S = Side->AddChildToVerticalBox(MakeRule(TEXT("RuleStats"))))
			{
				S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 14.0f));
			}

			// --- mesures --------------------------------------------------
			// Un tableau intitule/valeur plutot qu'une phrase : on compare d'un
			// coup d'oeil deux generations successives.
			Side->AddChildToVerticalBox(
				MakeSectionLabel(TEXT("StatsLabel"), TEXT("MESURES")));

			auto AddStat = [&](const TCHAR* RowName, const TCHAR* LabelName,
				const TCHAR* ValueName, const FString& Label) -> UTextBlock*
			{
				TPair<UHorizontalBox*, UTextBlock*> Made =
					MakeStatRow(RowName, LabelName, ValueName, Label);
				if (UVerticalBoxSlot* S = Side->AddChildToVerticalBox(Made.Key))
				{
					S->SetPadding(FMargin(0.0f, 5.0f, 0.0f, 0.0f));
				}
				return Made.Value;
			};

			StatGridValue = AddStat(TEXT("RowGrid"), TEXT("LblGrid"),
				TEXT("ValGrid"), TEXT("Grille"));
			StatScaleValue = AddStat(TEXT("RowScale"), TEXT("LblScale"),
				TEXT("ValScale"), TEXT("Resolution"));
			StatLandValue = AddStat(TEXT("RowLand"), TEXT("LblLand"),
				TEXT("ValLand"), TEXT("Terres emergees"));
			StatElevationValue = AddStat(TEXT("RowElev"), TEXT("LblElev"),
				TEXT("ValElev"), TEXT("Altitudes"));

			// Conserve pour la compatibilite : l'ancienne ligne unique devient
			// une note discrete sous le tableau.
			InfoText = MakeText(TEXT("InfoText"), TEXT(""), 10, ColTextMuted);
			if (UVerticalBoxSlot* S = Side->AddChildToVerticalBox(InfoText))
			{
				S->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
			}
		}

		// -------------------------------------------------- barre d'etat ---
		{
			ProgressGroup = WidgetTree->ConstructWidget<UVerticalBox>(
				UVerticalBox::StaticClass(), TEXT("ProgressGroup"));
			if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(ProgressGroup))
			{
				S->SetPadding(FMargin(0.0f, 20.0f, 0.0f, 0.0f));
			}

			USizeBox* BarBox = WidgetTree->ConstructWidget<USizeBox>(
				USizeBox::StaticClass(), TEXT("BarBox"));
			BarBox->SetHeightOverride(5.0f);

			ProgressBar = WidgetTree->ConstructWidget<UProgressBar>(
				UProgressBar::StaticClass(), TEXT("ProgressBar"));
			ProgressBar->SetPercent(0.0f);
			ProgressBar->SetFillColorAndOpacity(ColAccent);
			BarBox->AddChild(ProgressBar);
			ProgressGroup->AddChildToVerticalBox(BarBox);

			UHorizontalBox* StatusRow = WidgetTree->ConstructWidget<UHorizontalBox>(
				UHorizontalBox::StaticClass(), TEXT("StatusRow"));
			if (UVerticalBoxSlot* S = ProgressGroup->AddChildToVerticalBox(StatusRow))
			{
				S->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
			}

			StatusText = MakeText(TEXT("StatusText"), TEXT(""), 11, ColTextMuted);
			if (UHorizontalBoxSlot* S = StatusRow->AddChildToHorizontalBox(StatusText))
			{
				S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				S->SetVerticalAlignment(VAlign_Center);
			}

			CancelButton = MakeButton(TEXT("CancelButton"), TEXT("CancelLabel"),
				TEXT("Annuler"), 11, FLinearColor(0.26f, 0.14f, 0.14f, 1.0f));
			StatusRow->AddChildToHorizontalBox(CancelButton);
		}

		// ------------------------------------------- action principale -----
		{
			USizeBox* PlayBox = WidgetTree->ConstructWidget<USizeBox>(
				USizeBox::StaticClass(), TEXT("PlayBox"));
			PlayBox->SetHeightOverride(56.0f);

			PlayButton = MakeButton(TEXT("PlayButton"), TEXT("PlayLabel"),
				TEXT("ENTRER DANS LE MONDE"), 18, ColAccent * 0.55f);
			PlayBox->AddChild(PlayButton);

			if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(PlayBox))
			{
				S->SetPadding(FMargin(0.0f, 22.0f, 0.0f, 0.0f));
			}
		}

		// ------------------------------------------------- pied de page ----
		// La gestion du cache est de la MAINTENANCE : elle ne doit jamais
		// concurrencer l'action principale du regard. Bande discrete, texte
		// attenue, boutons compacts.
		{
			if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(MakeRule(TEXT("RuleFooter"))))
			{
				S->SetPadding(FMargin(0.0f, 20.0f, 0.0f, 10.0f));
			}

			UHorizontalBox* Footer = WidgetTree->ConstructWidget<UHorizontalBox>(
				UHorizontalBox::StaticClass(), TEXT("Footer"));
			Column->AddChildToVerticalBox(Footer);

			CacheText = MakeText(TEXT("CacheText"), TEXT(""), 10, ColTextMuted);
			if (UHorizontalBoxSlot* S = Footer->AddChildToHorizontalBox(CacheText))
			{
				S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				S->SetVerticalAlignment(VAlign_Center);
			}

			ClearObsoleteButton = MakeButton(TEXT("ClearObsoleteButton"),
				TEXT("ClearObsoleteLabel"), TEXT("Purger les perimees"), 10,
				FLinearColor(0.16f, 0.18f, 0.23f, 1.0f));
			if (UHorizontalBoxSlot* S = Footer->AddChildToHorizontalBox(ClearObsoleteButton))
			{
				S->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
			}

			ClearAllButton = MakeButton(TEXT("ClearAllButton"), TEXT("ClearAllLabel"),
				TEXT("Tout vider"), 10, FLinearColor(0.24f, 0.13f, 0.13f, 1.0f));
			if (UHorizontalBoxSlot* S = Footer->AddChildToHorizontalBox(ClearAllButton))
			{
				S->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
			}
		}
	}

	return Super::RebuildWidget();
}

void UWorldseedMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (SizeCombo)
	{
		SizeCombo->ClearOptions();
		for (const float Size : MapSizeChoices)
		{
			SizeCombo->AddOption(LabelForSize(Size));
		}
		SizeCombo->SetSelectedOption(LabelForSize(Params.MapSizeMeters));
		SizeCombo->OnSelectionChanged.AddDynamic(this, &UWorldseedMenuWidget::HandleSizeChanged);
	}

	if (PackCombo)
	{
		PackCombo->ClearOptions();
		for (int32 I = 0; I < static_cast<int32>(EWorldseedTexturePack::Count); ++I)
		{
			PackCombo->AddOption(
				WorldseedTexturePack::Label(static_cast<EWorldseedTexturePack>(I)).ToString());
		}
		PackCombo->SetSelectedOption(WorldseedTexturePack::Label(SelectedPack).ToString());
		PackCombo->OnSelectionChanged.AddDynamic(this, &UWorldseedMenuWidget::HandlePackChanged);

		if (PackHint)
		{
			PackHint->SetText(WorldseedTexturePack::Description(SelectedPack));
		}
	}

	if (SeedBox)
	{
		SeedBox->SetText(FText::AsNumber(Params.Seed));
		SeedBox->OnTextCommitted.AddDynamic(this, &UWorldseedMenuWidget::HandleSeedCommitted);
	}

	if (PlayButton)
	{
		PlayButton->OnClicked.AddDynamic(this, &UWorldseedMenuWidget::HandlePlayClicked);
	}

	if (RandomButton)
	{
		RandomButton->OnClicked.AddDynamic(this, &UWorldseedMenuWidget::HandleRandomSeedClicked);
	}

	if (CancelButton)
	{
		CancelButton->OnClicked.AddDynamic(this, &UWorldseedMenuWidget::HandleCancelClicked);
	}

	if (ClearObsoleteButton)
	{
		ClearObsoleteButton->OnClicked.AddDynamic(
			this, &UWorldseedMenuWidget::HandleClearObsoleteClicked);
	}

	if (ClearAllButton)
	{
		ClearAllButton->OnClicked.AddDynamic(
			this, &UWorldseedMenuWidget::HandleClearAllClicked);
	}

	// Sans cela le widget est SelfHitTestInvisible et ne recoit aucun evenement
	// souris : le glisser sur le globe ne marcherait pas. Les boutons et champs
	// enfants restent prioritaires, ils sont au-dessus.
	SetVisibility(ESlateVisibility::Visible);

	SetProgressVisible(false);
	UpdateCacheInfo();
	StartGeneration();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(GlobeTimerHandle, this,
			&UWorldseedMenuWidget::HandleGlobeTimer, GlobeRedrawPeriod, true);
	}
}

void UWorldseedMenuWidget::PullFormIntoParams()
{
	if (SeedBox)
	{
		const FString Raw = SeedBox->GetText().ToString().TrimStartAndEnd();
		if (Raw.IsNumeric())
		{
			Params.Seed = FCString::Atoi(*Raw);
		}
	}

	if (SizeCombo)
	{
		const FString Selected = SizeCombo->GetSelectedOption();
		for (const float Size : MapSizeChoices)
		{
			if (LabelForSize(Size) == Selected)
			{
				Params.MapSizeMeters = Size;
				break;
			}
		}
	}

	Params.Resolution = ResolutionForSize(Params.MapSizeMeters);
}

void UWorldseedMenuWidget::StartGeneration()
{
	PullFormIntoParams();
	CancelGeneration();

	// Les regles instancient un UObject : le chargement DOIT rester sur le
	// thread de jeu. Une fois en cache, le worker n'y touche qu'en lecture.
	FString RulesError;
	if (!WorldseedPipeline::GetRules(RulesError))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Worldseed] regles indisponibles : %s"), *RulesError);
		if (StatusText)
		{
			StatusText->SetText(FText::FromString(TEXT("Regles introuvables")));
		}
		return;
	}

	CurrentJob = MakeShared<FWorldseedJob, ESPMode::ThreadSafe>();
	PendingResult = MakeShared<WorldseedPipeline::FResult, ESPMode::ThreadSafe>();

	if (PlayButton) { PlayButton->SetIsEnabled(false); }
	if (ProgressBar) { ProgressBar->SetPercent(0.0f); }
	SetProgressVisible(true);

	FWorldseedJobPtr Job = CurrentJob;
	TSharedPtr<WorldseedPipeline::FResult, ESPMode::ThreadSafe> Result = PendingResult;
	const int32 JobSeed = Params.Seed;
	const float JobHeight = Params.MapSizeMeters;
	const int32 JobResolution = Params.Resolution;

	Async(EAsyncExecution::ThreadPool, [Job, Result, JobSeed, JobHeight, JobResolution]()
	{
		FString LocalError;
		const bool bOk = WorldseedPipeline::Generate(
			JobSeed, JobHeight, JobResolution, *Result, LocalError, Job.Get());

		const bool bCancelled = Job->ShouldStop();
		Job->Error = LocalError;
		Job->bWasCancelled.store(bCancelled, std::memory_order_relaxed);
		Job->Stage.store(static_cast<uint8>(
			bOk ? EWorldseedStage::Done
				: (bCancelled ? EWorldseedStage::Cancelled : EWorldseedStage::Failed)),
			std::memory_order_relaxed);

		// Release : tout ce qui precede est visible par le thread de jeu des
		// qu'il aura lu bDone en acquire.
		Job->bDone.store(true, std::memory_order_release);
	});
}

void UWorldseedMenuWidget::CancelGeneration()
{
	if (CurrentJob.IsValid())
	{
		CurrentJob->bCancelRequested.store(true, std::memory_order_relaxed);
	}

	// On lache nos references sans attendre : le worker s'arretera de lui-meme
	// au prochain point de controle. Bloquer ici figerait l'interface, ce qui
	// est exactement ce qu'on cherche a eviter.
	CurrentJob.Reset();
	PendingResult.Reset();

	SetProgressVisible(false);
	if (PlayButton) { PlayButton->SetIsEnabled(CachedHeights.Num() > 0); }
}

void UWorldseedMenuWidget::SetProgressVisible(bool bVisible)
{
	if (ProgressGroup)
	{
		// Collapsed et non Hidden : Hidden garderait la place reservee et
		// laisserait un trou sous la liste des tailles.
		ProgressGroup->SetVisibility(bVisible
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UWorldseedMenuWidget::PollGeneration()
{
	if (!CurrentJob.IsValid())
	{
		return;
	}

	const float Fraction = CurrentJob->Progress.load(std::memory_order_relaxed);
	if (ProgressBar)
	{
		ProgressBar->SetPercent(Fraction);
	}
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(FString::Printf(TEXT("%s  %.0f %%"),
			*FWorldseedJob::StageLabel(CurrentJob->GetStage()), Fraction * 100.0f)));
	}

	if (!CurrentJob->bDone.load(std::memory_order_acquire))
	{
		return;
	}

	const EWorldseedStage Final = CurrentJob->GetStage();
	if (Final == EWorldseedStage::Done && PendingResult.IsValid())
	{
		CachedHeights = MoveTemp(PendingResult->ElevationM);
		CachedTempC = MoveTemp(PendingResult->Climate.TempMeanC);
		CachedPrecipMm = MoveTemp(PendingResult->Climate.PrecipMm);
		CachedSeasonalAmpC = MoveTemp(PendingResult->Climate.SeasonalAmpC);
		CachedContinentality = MoveTemp(PendingResult->Climate.Continentality);
		CachedBiomes = MoveTemp(PendingResult->Biomes);
		BuildPreviewField();

		// La voie graphique d'abord ; BuildPreviewField n'aura servi qu'au repli.
		BakeGlobe();
		WorldGeometry = PendingResult->Geometry;
		Params.Resolution = PendingResult->Geometry.NY;
		LastLandRatio = PendingResult->LandRatio;
		LastMinElevationM = PendingResult->MinElevationM;
		LastMaxElevationM = PendingResult->MaxElevationM;

		UpdateInfoText();
		UpdateCacheInfo();
		if (ProgressBar) { ProgressBar->SetPercent(1.0f); }
	}
	else if (Final == EWorldseedStage::Failed && StatusText)
	{
		StatusText->SetText(FText::FromString(
			FString::Printf(TEXT("Echec : %s"), *CurrentJob->Error)));
	}

	CurrentJob.Reset();
	PendingResult.Reset();
	SetProgressVisible(false);
	if (PlayButton) { PlayButton->SetIsEnabled(CachedHeights.Num() > 0); }
}

void UWorldseedMenuWidget::UpdateCacheInfo()
{
	if (!CacheText)
	{
		return;
	}

	FString RulesHash;
	FString Error;
	if (UWorldseedRules* LoadedRules = WorldseedPipeline::GetRules(Error))
	{
		RulesHash = LoadedRules->SourceHash;
	}

	const TArray<FWorldseedCacheEntry> Entries = WorldseedCache::ListEntries(RulesHash);
	int32 Obsolete = 0;
	for (const FWorldseedCacheEntry& Entry : Entries)
	{
		if (!Entry.bCompatible)
		{
			++Obsolete;
		}
	}

	const float SizeMo = WorldseedCache::TotalSize() / (1024.0f * 1024.0f);

	FString Line = FString::Printf(TEXT("Cache : %d carte(s), %.1f Mo"),
		Entries.Num(), SizeMo);
	if (Obsolete > 0)
	{
		// On dit POURQUOI elles sont perimees : le code a change, ou les regles.
		Line += FString::Printf(
			TEXT("  —  %d perimee(s), produites par une autre version"), Obsolete);
	}
	CacheText->SetText(FText::FromString(Line));

	if (ClearObsoleteButton) { ClearObsoleteButton->SetIsEnabled(Obsolete > 0); }
	if (ClearAllButton) { ClearAllButton->SetIsEnabled(Entries.Num() > 0); }
}

void UWorldseedMenuWidget::HandleClearObsoleteClicked()
{
	FString RulesHash;
	FString Error;
	if (UWorldseedRules* LoadedRules = WorldseedPipeline::GetRules(Error))
	{
		RulesHash = LoadedRules->SourceHash;
	}
	WorldseedCache::ClearObsolete(RulesHash);
	UpdateCacheInfo();
}

void UWorldseedMenuWidget::HandleClearAllClicked()
{
	WorldseedCache::ClearAll();
	UpdateCacheInfo();
}

void UWorldseedMenuWidget::UpdateInfoText()
{
	if (!InfoText)
	{
		return;
	}

	const float QuadMeters = WorldGeometry.MetersPerPixel();
	const int64 Triangles = static_cast<int64>(WorldGeometry.NX - 1)
		* static_cast<int64>(WorldGeometry.NY - 1) * 2;

	if (StatGridValue)
	{
		StatGridValue->SetText(FText::FromString(FString::Printf(
			TEXT("%d x %d"), WorldGeometry.NX, WorldGeometry.NY)));
	}
	if (StatScaleValue)
	{
		StatScaleValue->SetText(FText::FromString(FString::Printf(
			TEXT("%.1f m / cellule"), QuadMeters)));
	}
	if (StatLandValue)
	{
		// La cible terrestre est rappelee a cote de la mesure : c est le
		// controle le plus utile de toute la chaine.
		StatLandValue->SetText(FText::FromString(FString::Printf(
			TEXT("%.1f %%   (Terre 29,2)"), LastLandRatio * 100.0f)));
	}
	if (StatElevationValue)
	{
		StatElevationValue->SetText(FText::FromString(FString::Printf(
			TEXT("%.0f a %.0f m"), LastMinElevationM, LastMaxElevationM)));
	}

	InfoText->SetText(FText::FromString(FString::Printf(
		TEXT("%lld triangles a pleine resolution"), Triangles)));
}

bool UWorldseedMenuWidget::BakeGlobe()
{
	GlobeMaterial = nullptr;
	GlobeTextures = FWorldseedGlobeTextures();

	if (WorldGeometry.NX < 2 || CachedHeights.Num() != WorldGeometry.CellCount())
	{
		return false;
	}

	// Le materiau est un ASSET : il peut manquer d'un depot a l'autre. On le
	// charge sans y croire, et le lance-de-rayon processeur reste la voie de
	// secours — le globe s'affiche toujours, simplement moins bien.
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Worldseed/Materials/M_WorldseedGlobe.M_WorldseedGlobe"));
	if (!Base)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] M_WorldseedGlobe absent : globe rendu par le processeur"));
		return false;
	}

	// --- geometrie ------------------------------------------------------------
	// Comme pour le rendu processeur : les dimensions viennent du MONDE, les
	// latitudes de reference viennent des regles.
	FWorldseedGeometry Geometry = WorldGeometry;
	if (!Rules)
	{
		FString Error;
		Rules = UWorldseedRules::LoadRules(Error);
	}

	float TropicDeg = 23.44f;
	float PolarCircleDeg = 66.56f;

	FWorldseedGlobeBakeSettings BakeSettings;
	if (Rules)
	{
		Geometry.LatSpanDeg = Rules->Geometry.LatSpanDeg;
		Geometry.LatitudeMapping = Rules->Geometry.LatitudeMapping;
		Geometry.LatitudeEqualAreaBlend = Rules->Geometry.LatitudeEqualAreaBlend;

		TropicDeg = static_cast<float>(Rules->Num(TEXT("world"), TEXT("tropicDeg"), 23.44));
		PolarCircleDeg = static_cast<float>(
			Rules->Num(TEXT("world"), TEXT("polarCircleDeg"), 66.56));
		BakeSettings.DeepOceanM = static_cast<float>(
			Rules->Num(TEXT("world"), TEXT("minElevationM"), -300.0));
	}

	// Deux mille quarante-huit de large : le globe fait 512 pixels et n'en
	// montre qu'un hemisphere, mais le limbe y comprime fortement les
	// meridiens. Deux fois sur-echantillonne, le bord reste net.
	BakeSettings.Width = 2048;

	GlobeTextures = WorldseedGlobeBake::Build(this, CachedHeights,
		CachedBiomes.Index, CachedBiomes.Cover, Geometry, BakeSettings);

	if (!GlobeTextures.IsValid())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] cuisson du globe impossible : repli sur le processeur"));
		return false;
	}

	GlobeMaterial = UMaterialInstanceDynamic::Create(Base, this);
	if (!GlobeMaterial)
	{
		return false;
	}

	GlobeMaterial->SetTextureParameterValue(TEXT("AlbedoTex"), GlobeTextures.Albedo);
	GlobeMaterial->SetTextureParameterValue(TEXT("NormalTex"), GlobeTextures.Normal);
	GlobeMaterial->SetScalarParameterValue(TEXT("LatSpanDeg"), Geometry.LatSpanDeg);
	GlobeMaterial->SetScalarParameterValue(TEXT("TropicDeg"), TropicDeg);
	GlobeMaterial->SetScalarParameterValue(TEXT("PolarCircleDeg"), PolarCircleDeg);
	GlobeMaterial->SetScalarParameterValue(TEXT("ShowLatitudeLines"), 1.0f);
	GlobeMaterial->SetScalarParameterValue(TEXT("LongitudeOffset"), GlobeLongitudeDeg);
	GlobeMaterial->SetScalarParameterValue(TEXT("Tilt"), GlobeTiltDeg);

	if (PreviewImage)
	{
		PreviewImage->SetBrushFromMaterial(GlobeMaterial);
		PreviewImage->SetDesiredSizeOverride(FVector2D(GlobeSize, GlobeSize));
	}

	// La texture du repli n'a plus de raison d'etre : la liberer evite de
	// garder en vie un megaoctet dont plus personne ne se sert.
	PreviewTexture = nullptr;

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] globe pilote par la carte graphique"));
	return true;
}

void UWorldseedMenuWidget::RedrawGlobe()
{
	if (WorldGeometry.NX < 2 || CachedHeights.Num() != WorldGeometry.CellCount())
	{
		return;
	}

	// --- voie graphique ------------------------------------------------------
	// Faire tourner le globe ne demande plus que deux scalaires : le monde est
	// deja dans la carte graphique. Ni lecture de heightfield, ni televersement
	// de texture, ni ressource recreee.
	if (GlobeMaterial)
	{
		GlobeMaterial->SetScalarParameterValue(TEXT("LongitudeOffset"), GlobeLongitudeDeg);
		GlobeMaterial->SetScalarParameterValue(TEXT("Tilt"), GlobeTiltDeg);
		return;
	}

	// Les regles portent la geometrie latitudinale et les latitudes de
	// reference : sans elles le globe placerait mal tropiques et cercles
	// polaires, qui sont justement ce qu'on vient verifier.
	if (!Rules)
	{
		FString Error;
		Rules = UWorldseedRules::LoadRules(Error);
		if (!Rules)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Worldseed] regles indisponibles : %s"), *Error);
		}
	}

	// LA GEOMETRIE VIENT DU MONDE, PAS DES REGLES. Les regles decrivent la
	// resolution de REFERENCE (4098 x 2049), alors que le menu genere a la
	// resolution choisie. Prendre celle des regles rendait le heightfield
	// incoherent avec sa geometrie, et le rendu abandonnait en silence.
	// C'est le heightfield REDUIT qu'on dessine : voir PreviewHeights.
	const bool bUsePreview = (PreviewGeometry.NX >= 2)
		&& (PreviewHeights.Num() == PreviewGeometry.CellCount());

	const TArray<float>& GlobeHeights = bUsePreview ? PreviewHeights : CachedHeights;
	FWorldseedGeometry Geometry = bUsePreview ? PreviewGeometry : WorldGeometry;

	WorldseedGlobe::FGlobeSettings GlobeSettings;
	if (Rules)
	{
		// Des regles on ne tire que les latitudes de reference et la
		// correspondance latitude/ligne, jamais les dimensions de grille.
		Geometry.LatSpanDeg = Rules->Geometry.LatSpanDeg;
		Geometry.LatitudeMapping = Rules->Geometry.LatitudeMapping;
		Geometry.LatitudeEqualAreaBlend = Rules->Geometry.LatitudeEqualAreaBlend;

		GlobeSettings.TropicDeg = static_cast<float>(
			Rules->Num(TEXT("world"), TEXT("tropicDeg"), 23.44));
		GlobeSettings.PolarCircleDeg = static_cast<float>(
			Rules->Num(TEXT("world"), TEXT("polarCircleDeg"), 66.56));
		GlobeSettings.DeepOceanM = static_cast<float>(
			Rules->Num(TEXT("world"), TEXT("minElevationM"), -300.0));
	}
	GlobeSettings.LongitudeOffsetDeg = GlobeLongitudeDeg;
	GlobeSettings.TiltDeg = GlobeTiltDeg;

	// Premiere fois : on cree la texture. Ensuite on ne fait que reecrire ses
	// pixels, sinon la rotation fabriquerait une UTexture2D par frame.
	//
	// CE CHEMIN EST DESORMAIS UN REPLI : il ne sert que si le materiau du globe
	// est introuvable. Voir BakeGlobe.
	if (!PreviewTexture)
	{
		PreviewTexture = WorldseedGlobe::Render(
			GlobeHeights, Geometry, GlobeSettings, 512);

		if (!PreviewTexture)
		{
			// Ce cas etait muet, et il a coute une session entiere a trouver.
			UE_LOG(LogTemp, Error,
				TEXT("[Worldseed] globe non rendu : %d valeurs pour une grille %dx%d (%d attendues)"),
				GlobeHeights.Num(), Geometry.NX, Geometry.NY, Geometry.CellCount());
			return;
		}

		if (PreviewImage)
		{
			PreviewImage->SetBrushFromTexture(PreviewTexture, false);
			PreviewImage->SetDesiredSizeOverride(FVector2D(GlobeSize, GlobeSize));
		}
		return;
	}

	WorldseedGlobe::RenderInto(PreviewTexture, GlobeHeights,
		Geometry, GlobeSettings);
}

void UWorldseedMenuWidget::BuildPreviewField()
{
	PreviewHeights.Reset();
	PreviewGeometry = FWorldseedGeometry();

	if (WorldGeometry.NX < 2 || CachedHeights.Num() != WorldGeometry.CellCount())
	{
		return;
	}

	// Rien a reduire si le monde est deja plus fin que l'apercu ne le demande :
	// on laisse alors le rendu lire le heightfield d'origine.
	if (WorldGeometry.NX <= GlobePreviewMaxWidth)
	{
		return;
	}

	// Le rapport deux pour un de la carte est conserve : la correspondance
	// latitude/ligne depend de V, pas du nombre de lignes, donc les bandes
	// climatiques restent a leur place.
	const int32 DstNX = GlobePreviewMaxWidth;
	const int32 DstNY = FMath::Max(GlobePreviewMaxWidth / 2, 2);

	const double StartTime = FPlatformTime::Seconds();

	WorldseedGrid::Downsample(CachedHeights, WorldGeometry.NX, WorldGeometry.NY,
		DstNX, DstNY, PreviewHeights);

	PreviewGeometry = WorldGeometry;
	PreviewGeometry.NX = DstNX;
	PreviewGeometry.NY = DstNY;

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] apercu reduit : %dx%d -> %dx%d  (%.1f Mo -> %.1f Mo, %.0f ms)"),
		WorldGeometry.NX, WorldGeometry.NY, DstNX, DstNY,
		CachedHeights.Num() * sizeof(float) / (1024.0f * 1024.0f),
		PreviewHeights.Num() * sizeof(float) / (1024.0f * 1024.0f),
		(FPlatformTime::Seconds() - StartTime) * 1000.0);
}

void UWorldseedMenuWidget::HandleGlobeTimer()
{
	// La rotation automatique s'efface devant le geste du joueur : reprendre a
	// tourner sous ses doigts pendant qu'il oriente le globe serait desagreable.
	PollGeneration();

	if (!bDraggingGlobe && AutoSpinDegPerSecond != 0.0f)
	{
		GlobeLongitudeDeg = FMath::Fmod(
			GlobeLongitudeDeg + AutoSpinDegPerSecond * GlobeRedrawPeriod, 360.0f);
	}

	RedrawGlobe();
}

void UWorldseedMenuWidget::NativeDestruct()
{
	// Le worker ne referencera plus rien d'utile : on lui demande de s'arreter
	// et on lache nos pointeurs. Il finira seul et l'etat partage mourra avec
	// sa derniere reference.
	CancelGeneration();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GlobeTimerHandle);
	}
	Super::NativeDestruct();
}

FReply UWorldseedMenuWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}

	bDraggingGlobe = true;
	LastDragPosition = InMouseEvent.GetScreenSpacePosition();

	// CaptureMouse garde les evenements meme si le curseur sort du widget :
	// sans cela un glisser un peu ample lacherait le globe en cours de route.
	return FReply::Handled().CaptureMouse(TakeWidget());
}

FReply UWorldseedMenuWidget::NativeOnMouseMove(const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	if (!bDraggingGlobe)
	{
		return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
	}

	const FVector2D Current = InMouseEvent.GetScreenSpacePosition();
	const FVector2D Delta = Current - LastDragPosition;
	LastDragPosition = Current;

	// 0,35 degre par pixel : un glisser de la largeur du globe fait environ un
	// demi-tour, ce qui se manipule bien.
	GlobeLongitudeDeg = FMath::Fmod(
		GlobeLongitudeDeg - static_cast<float>(Delta.X) * 0.35f + 360.0f, 360.0f);

	// L'inclinaison est bornee : au-dela on regarde le pole par-dessus et la
	// reprojection devient illisible.
	GlobeTiltDeg = FMath::Clamp(
		GlobeTiltDeg + static_cast<float>(Delta.Y) * 0.35f, -80.0f, 80.0f);

	return FReply::Handled();
}

FReply UWorldseedMenuWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
	}

	bDraggingGlobe = false;
	return FReply::Handled().ReleaseMouseCapture();
}

void UWorldseedMenuWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	bDraggingGlobe = false;
	Super::NativeOnMouseCaptureLost(CaptureLostEvent);
}

void UWorldseedMenuWidget::HandleSeedCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	StartGeneration();
}

void UWorldseedMenuWidget::HandleSizeChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	StartGeneration();
}

void UWorldseedMenuWidget::HandlePackChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	for (int32 I = 0; I < static_cast<int32>(EWorldseedTexturePack::Count); ++I)
	{
		const EWorldseedTexturePack Pack = static_cast<EWorldseedTexturePack>(I);
		if (WorldseedTexturePack::Label(Pack).ToString() == SelectedItem)
		{
			SelectedPack = Pack;
			break;
		}
	}

	if (PackHint)
	{
		PackHint->SetText(WorldseedTexturePack::Description(SelectedPack));
	}

	// AUCUNE REGENERATION : le monde est le meme, seul son habillage change.
	UE_LOG(LogTemp, Log, TEXT("[Worldseed] habillage du sol : %s"),
		*WorldseedTexturePack::Label(SelectedPack).ToString());
}

void UWorldseedMenuWidget::HandleCancelClicked()
{
	CancelGeneration();
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(TEXT("Annule — change les parametres")));
	}
	if (ProgressBar)
	{
		ProgressBar->SetPercent(0.0f);
	}
	SetProgressVisible(false);
}

void UWorldseedMenuWidget::HandleRandomSeedClicked()
{
	// Tirage ponctuel a la demande du joueur : ici FMath::Rand est legitime,
	// c'est le CHOIX du seed. La generation elle-meme reste deterministe.
	Params.Seed = FMath::Rand();
	if (SeedBox)
	{
		SeedBox->SetText(FText::AsNumber(Params.Seed));
	}
	StartGeneration();
}

void UWorldseedMenuWidget::HandlePlayClicked()
{
	PullFormIntoParams();

	if (UWorldseedGameInstance* GI =
		UWorldseedGameInstance::GetWorldseedGameInstance(this))
	{
		FWorldseedWorldData ToPlay;
		ToPlay.Seed = Params.Seed;
		ToPlay.Geometry = WorldGeometry;
		ToPlay.ElevationM = CachedHeights;
		ToPlay.TempC = CachedTempC;
		ToPlay.PrecipMm = CachedPrecipMm;
		ToPlay.SeasonalAmpC = CachedSeasonalAmpC;
		ToPlay.Continentality = CachedContinentality;
		ToPlay.Biomes = CachedBiomes;
		ToPlay.TexturePack = SelectedPack;
		GI->StoreWorld(ToPlay);

		UE_LOG(LogTemp, Log, TEXT("[Worldseed] menu -> seed=%d  %.1f x %.1f km  %dx%d"),
			Params.Seed, WorldGeometry.WidthM() / 1000.0f, WorldGeometry.HeightM / 1000.0f,
			WorldGeometry.NX, WorldGeometry.NY);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] GameInstance absent : le terrain utilisera ses valeurs par defaut"));
	}

	// Le nouveau PlayerController repart sur les valeurs par defaut, mais on
	// rend la main explicitement : si le niveau cible reutilisait ce PC, un
	// InputModeUIOnly residuel bloquerait toutes les entrees de jeu.
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}

	UGameplayStatics::OpenLevel(this, GameLevelName);
}
