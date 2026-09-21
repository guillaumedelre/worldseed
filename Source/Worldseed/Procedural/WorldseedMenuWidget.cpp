// Worldseed - ecran d'entree : choix du seed et de la taille, apercu.

#include "Procedural/WorldseedMenuWidget.h"
#include "Procedural/WorldseedGameInstance.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Procedural/WorldseedBiomes.h"
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
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
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
	 * LA SEULE TAILLE OFFERTE, en metres de HAUTEUR : 64 x 32 km.
	 *
	 * Sept tailles etaient proposees, de 1 x 0,5 a 64 x 32 km, et elles ne se
	 * valaient pas. Les regles de FORMES sont metriques et grandes -- maille de
	 * tirage des diaclases 4 km, longueur d'onde des regions de tables 3,6 km,
	 * espacement des arches 2,5 km -- tandis que ce depot a deja chiffre le
	 * seuil ou l'intersection de criteres rares cesse d'etre une LOTERIE SUR LA
	 * GRAINE pour redevenir une proportion : environ cent vingt taches.
	 *
	 *     64 x 32 km  ->  128 mailles de tirage
	 *     32 x 16 km  ->   32
	 *     16 x  8 km  ->    8
	 *      2 x  1 km  ->  moins d'une : le monde entier tient dans une maille
	 *
	 * C'est donc la seule taille ou les diaclases, les tables et les canyons
	 * sont STATISTIQUES et non tires au sort. C'est aussi celle ou tout le
	 * calage est fait, et celle ou le relief est credible -- 1713 m de sommet
	 * contre 531 a 16 km.
	 *
	 * ET LES PETITES TAILLES NE SONT PAS PERDUES. Leur usage reel etait
	 * d'iterer vite -- douze secondes de generation contre deux cent soixante-
	 * dix -- et les sondes comme le banc prennent deja une taille en parametre :
	 * `probe_biomes(graine, 1000.0, 500)`. Cette capacite ne dependait pas du
	 * menu, et elle lui survit.
	 *
	 * ET LES DEUX BORNES SE REJOIGNENT EXACTEMENT SUR CETTE TAILLE, ce qui
	 * n'etait pas prevu. Le critere statistique donne un PLANCHER : au-dessous
	 * de 64 x 32 il y a moins de cent vingt mailles, et la couverture des
	 * formes saute du simple au triple d'une graine a l'autre. `VerticalScale`,
	 * lui, donne un PLAFOND -- il vaut `hauteur / (sizeKm x 1000)` borne a 4,0,
	 * atteint PILE a 32 km de hauteur. Au-dela, le facteur cesse de suivre et
	 * le relief redevient plat en proportion : 2051 m sur 128 km au lieu de
	 * 2051 sur 64.
	 *
	 *     taille       mailles de 4 km   VerticalScale   maille de simulation
	 *     16 x  8 km            8            1,0              3,9 m
	 *     32 x 16 km           32            2,0              7,8 m
	 *     64 x 32 km          128            4,0             15,6 m
	 *     96 x 48 km          288      4,0 (plafonne)        23,4 m
	 *    128 x 64 km          512      4,0 (plafonne)        31,2 m
	 *
	 * Offrir plus grand demanderait donc de LEVER ce plafond, et ce n'est pas
	 * un reglage : le depot a deja mesure ce que coute une amplitude qui ne
	 * suit pas la largeur -- « 92 % des sommets au-dela de l'angle de roche,
	 * un terrain integralement gris ». Arbitrage du proprietaire, 20 septembre
	 * 2026, apres ce tableau : on garde cette seule taille.
	 *
	 * `world.sizeKm` RESTE A 8 : ce n'est pas la taille du monde mais la
	 * HAUTEUR DE REFERENCE du calage metrique, dont VerticalScale tire son
	 * rapport. Les deux se sont longtemps trouvees egales, ce qui masquait la
	 * distinction.
	 */
	constexpr float WorldseedTailleDuMondeM = 32000.0f;

	/**
	 * Plafond de la grille de SIMULATION.
	 *
	 * Le commentaire d'origine -- « au-dela, le maillage devient trop lourd
	 * sans decoupage en chunks » -- est PERIME : le terrain est desormais
	 * maille en voxels diffuses autour du joueur, et la grille de simulation
	 * n'est plus ce qui se dessine. Ce qui plafonne est le COUT de la chaine.
	 *
	 * PORTE DE 1024 A 2048 LE 19 SEPTEMBRE, SUR MESURE ET NON SUR ESTIMATION.
	 * Le commentaire precedent annoncait « doubler la grille quadruplerait ce
	 * chiffre » sans l'avoir verifie. Mesure faite, graine 20260909 sur
	 * 64 x 32 km :
	 *
	 *                          2048x1024 (31 m)   4096x2048 (16 m)
	 *   generation                    52 s             250 s   (x4,8)
	 *   cellules a plus de 45 deg     9,9 %            16,4 %
	 *   pente mediane en zone        10,2 deg          14,3
	 *   ecart d'altitude des sommets  2,2 %             1,4
	 *   part emergee                 29,20 %           29,20
	 *
	 * SOIXANTE-CINQ POUR CENT DE TERRAIN ESCARPE EN PLUS, et c'est tout
	 * l'objet : a 31 m de maille, un canyon de 150 m ne fait que cinq cellules
	 * et sort LISSE quoi qu'on creuse. Le voxel a un metre n'y change rien --
	 * il pose du grain SUR une forme deja adoucie. Quatre tournees photo n'ont
	 * montre que des creux doux pour cette raison.
	 *
	 * LE COUT TOMBE PRESQUE ENTIEREMENT SUR LE CLIMAT -- 42 s par passe contre
	 * 8,8 -- et il se paie UNE FOIS : le monde part ensuite au cache.
	 *
	 * CONSEQUENCE A CONNAITRE : sur une grande carte la maille s'elargit quand
	 * meme. 15,6 m a 64 km contre 3,9 a 16 km.
	 */
	constexpr int32 MaxResolution = 2048;

	/** Resolution visee : environ un quad tous les 2 m, plafonnee. */
	int32 ResolutionForSize(float SizeMeters)
	{
		return FMath::Clamp(FMath::RoundToInt(SizeMeters * 0.5f), 128, MaxResolution);
	}
}

namespace
{
	/** Palette. Une seule source pour toutes les couleurs de l ecran. */
	const FLinearColor ColBackground(0.030f, 0.035f, 0.048f, 1.0f);
	const FLinearColor ColTextPrimary(0.92f, 0.93f, 0.96f, 1.0f);
	const FLinearColor ColTextMuted(0.52f, 0.56f, 0.64f, 1.0f);
	const FLinearColor ColAccent(0.34f, 0.62f, 0.88f, 1.0f);
	const FLinearColor ColSeparator(1.0f, 1.0f, 1.0f, 0.08f);

	/**
	 * LES COULEURS DE BOUTON, PAR ROLE ET NON PAR ENDROIT.
	 *
	 * Elles etaient ecrites en clair a chaque appel, et elles avaient derive :
	 * DEUX gris-bleu -- (0.18, 0.20, 0.26) pour le de, (0.16, 0.18, 0.23) pour
	 * « Purger les perimees » -- et DEUX rouges -- (0.26, 0.14, 0.14) pour
	 * « Annuler », (0.24, 0.13, 0.13) pour « Tout vider » -- alors que ces
	 * quatre boutons ne jouent que DEUX roles. L'ecart est trop faible pour se
	 * voir et trop reel pour etre voulu : c'est la signature d'un litteral
	 * recopie de memoire. Une constante nommee ne derive pas.
	 *
	 * Quatre roles, et la hierarchie se lit a la couleur : ce qui est neutre,
	 * ce qui engage le calcul, ce qui fait entrer dans le monde, ce qui
	 * detruit.
	 */
	const FLinearColor ColBoutonNeutre(0.18f, 0.20f, 0.26f, 1.0f);
	const FLinearColor ColBoutonPrimaire(0.15f, 0.28f, 0.40f, 1.0f);
	const FLinearColor ColBoutonDanger(0.26f, 0.14f, 0.14f, 1.0f);
	const FLinearColor ColBoutonAction = ColAccent * 0.55f;

	/**
	 * LE BAREME TYPOGRAPHIQUE. Cinq roles, cinq tailles, et rien d'autre.
	 *
	 * L'ecran employait deja cinq tailles, mais sans regle : les boutons
	 * secondaires etaient tantot en 11 -- le de, « Annuler » -- tantot en 10
	 * -- « Purger », « Tout vider ». Et surtout, les deux CONTROLES DE SAISIE
	 * n'etaient pas regles du tout : le champ de graine et la liste
	 * d'habillage tombaient sur la taille par defaut du style Slate, bien plus
	 * grosse que le bouton pose juste a cote. C'est le meme defaut que
	 * PackHint, qui s'affichait en vingt-quatre points faute de passer par le
	 * fabricant commun -- « un widget construit directement echappe a tous les
	 * reglages de l'ecran ».
	 *
	 * La regle desormais : UN CONTROLE EST UN CONTROLE. Champ, liste et
	 * boutons partagent TypoControle ; seule l'action finale s'en detache.
	 */
	constexpr int32 TypoTitre = 32;
	constexpr int32 TypoAction = 16;
	constexpr int32 TypoControle = 12;
	constexpr int32 TypoTexte = 11;
	constexpr int32 TypoLegende = 10;

	/**
	 * LA TAILLE D'UNE ICONE N'EST PAS UNE TAILLE DE TEXTE, et elle n'ecorne
	 * donc pas le bareme : un glyphe pictural doit se LIRE COMME UN DESSIN,
	 * pas s'aligner sur la hauteur d'x des mots voisins. A douze points le de
	 * serait un pate ; a dix-huit on distingue ses points.
	 */
	constexpr int32 TypoIcone = 18;

	/**
	 * La hauteur commune des controles de l'en-tete.
	 *
	 * Sans elle, chacun prenait la hauteur que lui donnait son contenu : le
	 * champ de graine, le de et la liste d'habillage se retrouvaient a trois
	 * hauteurs differentes sur une meme ligne. C'est ce qui se voyait le plus.
	 */
	constexpr float HauteurControle = 30.0f;

	/**
	 * Le de porte un libelle court ; sans largeur imposee il se reduirait a
	 * une pastille a cote de « Generer », et la ligne perdrait son assise.
	 */
	constexpr float LargeurBoutonDe = 46.0f;

	/**
	 * Le fond du volet de statistiques, POSE SUR LE GLOBE.
	 *
	 * Opaque, il ferait une colonne collee sur l'image et on perdrait le
	 * benefice de la surimpression ; absent, le texte clair disparaitrait sur
	 * la calotte glaciaire et sur les deserts, qui sont clairs eux aussi.
	 * Quatre-vingt-cinq pour cent laisse deviner le monde dessous tout en
	 * gardant un contraste de lecture sur n'importe quel biome.
	 */
	const FLinearColor ColVolet(0.030f, 0.035f, 0.048f, 0.85f);

	/**
	 * Les largeurs de l'ecran. AUCUNE NE BORNE LE GLOBE -- il prend ce qui
	 * reste, et c'est tout le propos de la disposition.
	 *
	 * `MargeEcran` est le seul retrait entre le bord de la fenetre et le
	 * contenu : l'ecran n'a plus de panneau centre, il OCCUPE la fenetre.
	 */
	constexpr float MargeEcran = 34.0f;
	constexpr float LargeurGraine = 150.0f;
	constexpr float LargeurHabillage = 230.0f;
	constexpr float LargeurVolet = 270.0f;
	constexpr float LargeurAction = 300.0f;

	/**
	 * La taille NATURELLE du globe, celle que le `UScaleBox` met ensuite a
	 * l'echelle du cadre. Elle ne fixe donc plus une dimension a l'ecran --
	 * elle fixe le RAPPORT, qui doit rester carre pour que le globe soit rond,
	 * et la resolution a laquelle le lance-de-rayon processeur travaille quand
	 * le materiau manque.
	 */
	constexpr float GlobeSize = 420.0f;
}

TSharedRef<SWidget> UWorldseedMenuWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		// ------------------------------------------------------------------
		// EN-TETE / CORPS / PIED, et le globe occupe TOUT le corps.
		//
		// Les trois colonnes precedentes partageaient la largeur a parts
		// egales entre ce qu'on regle, ce qu'on regarde et ce qu'on lit. Or
		// ces trois choses n'ont pas le meme poids : deux reglages tiennent
		// dans une barre, la lecture est une MARGE, et le monde est le sujet.
		// La disposition le dit desormais --
		//
		//     [ en-tete : le titre, et les deux reglages, en une barre ]
		//     [                                                       ]
		//     [        LE GLOBE, plein cadre        [ statistiques ]  ]
		//     [                                                       ]
		//     [ pied : le cache a gauche, l'action a droite           ]
		//
		// -- et les statistiques passent EN SURIMPRESSION plutot qu'a cote :
		// elles decrivent le monde qu'elles recouvrent, et les poser dans une
		// colonne propre aurait repris au globe la largeur qu'on vient de lui
		// donner.
		//
		// PLUS DE DEFILEMENT. L'ecran tient desormais dans la fenetre par
		// construction : le corps est le seul element qui s'etire, et il
		// s'etire a ce qui reste. Une barre de defilement n'aurait plus rien
		// a faire defiler, et elle aurait surtout empeche le globe de
		// connaitre sa propre hauteur -- un enfant de `UScrollBox` recoit une
		// hauteur INFINIE et se dimensionne sur son contenu.
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

		/**
		 * La police de l'ecran, a une taille donnee.
		 *
		 * Elle est lue sur le DEFAUT de UTextBlock -- exactement celle que
		 * MakeText emploie -- et non prise a FCoreStyle. Les deux ne sont pas
		 * forcement la meme famille, et un controle de saisie qui porterait
		 * une autre fonte que les textes voisins serait precisement
		 * l'incoherence qu'on vient corriger.
		 */
		auto PoliceEcran = [](int32 Size) -> FSlateFontInfo
		{
			FSlateFontInfo F = GetDefault<UTextBlock>()->GetFont();
			F.Size = Size;
			return F;
		};

		/**
		 * Enveloppe un controle pour lui imposer la hauteur commune.
		 *
		 * Largeur nulle = on ne l'impose pas, le controle prend la sienne.
		 */
		auto MakeControlBox = [this](const TCHAR* Name, UWidget* Inner,
			float Width) -> USizeBox*
		{
			USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(
				USizeBox::StaticClass(), Name);
			Box->SetHeightOverride(HauteurControle);
			if (Width > 0.0f)
			{
				Box->SetWidthOverride(Width);
			}
			Box->AddChild(Inner);
			return Box;
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
			return MakeText(Name, Label, TypoLegende, ColTextMuted);
		};

		/** Ligne de mesure : intitule a gauche, valeur alignee a droite. */
		auto MakeStatRow = [this, &MakeText](const TCHAR* RowName, const TCHAR* LabelName,
			const TCHAR* ValueName, const FString& Label) -> TPair<UHorizontalBox*, UTextBlock*>
		{
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(
				UHorizontalBox::StaticClass(), RowName);

			UTextBlock* L = MakeText(LabelName, Label, TypoTexte, ColTextMuted);
			if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(L))
			{
				S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}

			UTextBlock* V = MakeText(ValueName, TEXT("--"), TypoTexte, ColTextPrimary);
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

		// La colonne maitresse occupe la fenetre entiere, moins une marge.
		// C'est elle qui distribue la hauteur : en-tete et pied prennent ce
		// qu'il leur faut, le corps prend le reste.
		UVerticalBox* Shell = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("Shell"));
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Root->AddChild(Shell)))
		{
			S->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			S->SetOffsets(FMargin(MargeEcran));
		}

		// =========================================================== EN-TETE
		{
			UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(
				UHorizontalBox::StaticClass(), TEXT("Header"));
			Shell->AddChildToVerticalBox(Header);

			// --- le titre, a gauche ---------------------------------------
			{
				UVerticalBox* Marque = WidgetTree->ConstructWidget<UVerticalBox>(
					UVerticalBox::StaticClass(), TEXT("Marque"));
				if (UHorizontalBoxSlot* S = Header->AddChildToHorizontalBox(Marque))
				{
					S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
					S->SetVerticalAlignment(VAlign_Center);
				}

				Marque->AddChildToVerticalBox(
					MakeText(TEXT("Title"), TEXT("WORLDSEED"), TypoTitre, ColTextPrimary));

				UTextBlock* Sub = MakeText(TEXT("Subtitle"),
					TEXT("tectonique  -  climat  -  erosion"), TypoTexte, ColTextMuted);
				if (UVerticalBoxSlot* S = Marque->AddChildToVerticalBox(Sub))
				{
					S->SetPadding(FMargin(2.0f, 2.0f, 0.0f, 0.0f));
				}
			}

			// --- la graine, a droite --------------------------------------
			//
			// Intitule AU-DESSUS du champ et non a cote : les deux reglages de
			// l'en-tete gardent ainsi la meme silhouette, et un intitule pose
			// a gauche obligerait a reserver la largeur du plus long des deux.
			{
				UVerticalBox* Graine = WidgetTree->ConstructWidget<UVerticalBox>(
					UVerticalBox::StaticClass(), TEXT("GraineGroupe"));
				if (UHorizontalBoxSlot* S = Header->AddChildToHorizontalBox(Graine))
				{
					S->SetPadding(FMargin(24.0f, 0.0f, 0.0f, 0.0f));
					S->SetVerticalAlignment(VAlign_Center);
				}

				Graine->AddChildToVerticalBox(
					MakeSectionLabel(TEXT("SeedLabel"), TEXT("GRAINE")));

				UHorizontalBox* SeedRow = WidgetTree->ConstructWidget<UHorizontalBox>(
					UHorizontalBox::StaticClass(), TEXT("SeedRow"));
				if (UVerticalBoxSlot* S = Graine->AddChildToVerticalBox(SeedRow))
				{
					S->SetPadding(FMargin(0.0f, 5.0f, 0.0f, 0.0f));
				}

				SeedBox = WidgetTree->ConstructWidget<UEditableTextBox>(
					UEditableTextBox::StaticClass(), TEXT("SeedBox"));

				// LE CHAMP NE PASSE PAS PAR MakeText, donc rien ne reglait sa
				// police : il gardait celle du style, bien plus grosse que le
				// bouton pose juste a cote. C'est ce qui donnait a l'en-tete
				// son air depareille.
				{
					FEditableTextBoxStyle Style = SeedBox->GetWidgetStyle();
					Style.SetFont(PoliceEcran(TypoControle));
					SeedBox->SetWidgetStyle(Style);
				}

				if (UHorizontalBoxSlot* S = SeedRow->AddChildToHorizontalBox(
					MakeControlBox(TEXT("SeedBoxSize"), SeedBox, LargeurGraine)))
				{
					S->SetVerticalAlignment(VAlign_Center);
				}

				RandomButton = MakeButton(TEXT("RandomButton"), TEXT("RandomLabel"),
					TEXT("⚄"), TypoIcone, ColBoutonNeutre);
				// UNE ICONE SEULE NE SE LIT PAS, et c'est le prix du glyphe :
				// le mot « De » disait ce que faisait le bouton, le dessin
				// demande d'avoir reconnu un de ET devine ce qu'il fait ici.
				// L'infobulle rend cette phrase, sans reprendre la place que
				// l'icone vient de liberer.
				RandomButton->SetToolTipText(
					FText::FromString(TEXT("Tirer une graine au hasard")));

				if (UHorizontalBoxSlot* S = SeedRow->AddChildToHorizontalBox(
					MakeControlBox(TEXT("RandomBox"), RandomButton, LargeurBoutonDe)))
				{
					S->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
					S->SetVerticalAlignment(VAlign_Center);
				}

				// GENERER. L'action existait deja -- taper Entree dans le champ
				// relance la generation -- mais rien ne le disait, et le seul
				// declencheur visible, le de, impose une graine tiree au sort.
				// Qui voulait LA SIENNE n'avait aucun bouton a viser.
				GenerateButton = MakeButton(TEXT("GenerateButton"),
					TEXT("GenerateLabel"), TEXT("Générer"), TypoControle,
					ColBoutonPrimaire);
				GenerateButton->SetToolTipText(
					FText::FromString(TEXT("Generer le monde avec la graine saisie")));
				if (UHorizontalBoxSlot* S = SeedRow->AddChildToHorizontalBox(
					MakeControlBox(TEXT("GenerateBox"), GenerateButton, 0.0f)))
				{
					S->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
					S->SetVerticalAlignment(VAlign_Center);
				}
			}

			// --- l'habillage du sol, a droite -----------------------------
			{
				UVerticalBox* Habillage = WidgetTree->ConstructWidget<UVerticalBox>(
					UVerticalBox::StaticClass(), TEXT("HabillageGroupe"));
				if (UHorizontalBoxSlot* S = Header->AddChildToHorizontalBox(Habillage))
				{
					S->SetPadding(FMargin(24.0f, 0.0f, 0.0f, 0.0f));
					S->SetVerticalAlignment(VAlign_Center);
				}

				Habillage->AddChildToVerticalBox(
					MakeSectionLabel(TEXT("PackLabel"), TEXT("HABILLAGE DU SOL")));

				PackCombo = WidgetTree->ConstructWidget<UComboBoxString>(
					UComboBoxString::StaticClass(), TEXT("PackCombo"));

				// LA POLICE DE LA LISTE N'A AUCUN SETTER PUBLIC.
				//
				// UComboBoxString expose un getter, et rien pour ecrire :
				// InitFont est PROTECTED -- prevu pour une classe derivee --
				// et la propriete Font est publique mais depreciee « use the
				// getter ». Il n'y a donc que trois voies : deriver une UCLASS
				// entiere pour appeler InitFont, ecrire par reflexion, ou
				// ecrire la propriete en assumant la depreciation. La
				// troisieme est celle que le code du moteur emploie lui-meme,
				// elle tient en une ligne, et elle DIT ce qu'elle fait.
				//
				// Elle doit rester ICI, avant que le widget Slate ne soit
				// construit : la declaration previent que la valeur n'est lue
				// qu'a la construction. Posee plus tard, elle serait ignoree
				// en silence et la liste garderait la grosse police du style
				// pendant que son voisin serait en douze.
				PRAGMA_DISABLE_DEPRECATION_WARNINGS
				PackCombo->Font = PoliceEcran(TypoControle);
				PRAGMA_ENABLE_DEPRECATION_WARNINGS

				if (UVerticalBoxSlot* S = Habillage->AddChildToVerticalBox(
					MakeControlBox(TEXT("PackSize"), PackCombo, LargeurHabillage)))
				{
					S->SetPadding(FMargin(0.0f, 5.0f, 0.0f, 0.0f));
				}
			}

			if (UVerticalBoxSlot* S = Shell->AddChildToVerticalBox(MakeRule(TEXT("RuleTop"))))
			{
				S->SetPadding(FMargin(0.0f, 18.0f, 0.0f, 0.0f));
			}
		}

		// ============================================================= CORPS
		//
		// Un OVERLAY et non une boite : ses enfants se SUPERPOSENT, chacun
		// avec son propre alignement. Le globe prend tout le cadre, les
		// statistiques se posent dessus, calees a droite.
		UOverlay* Body = WidgetTree->ConstructWidget<UOverlay>(
			UOverlay::StaticClass(), TEXT("Body"));
		Body->SetClipping(EWidgetClipping::ClipToBounds);
		if (UVerticalBoxSlot* S = Shell->AddChildToVerticalBox(Body))
		{
			S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			S->SetPadding(FMargin(0.0f, 14.0f, 0.0f, 14.0f));
		}

		// --- le monde, plein cadre -----------------------------------------
		{
			// `ScaleToFit` garde le globe ROND quoi qu'il arrive : il met a
			// l'echelle sur la plus petite des deux dimensions. Une fenetre
			// large donne donc un globe haut comme le corps, une fenetre
			// haute un globe large comme lui -- et jamais un ovale.
			UScaleBox* GlobeScale = WidgetTree->ConstructWidget<UScaleBox>(
				UScaleBox::StaticClass(), TEXT("GlobeScale"));
			GlobeScale->SetStretch(EStretch::ScaleToFit);

			PreviewImage = WidgetTree->ConstructWidget<UImage>(
				UImage::StaticClass(), TEXT("PreviewImage"));
			PreviewImage->SetDesiredSizeOverride(FVector2D(GlobeSize, GlobeSize));

			GlobeScale->AddChild(PreviewImage);

			if (UOverlaySlot* S = Body->AddChildToOverlay(GlobeScale))
			{
				S->SetHorizontalAlignment(HAlign_Fill);
				S->SetVerticalAlignment(VAlign_Fill);
			}
		}

		// --- les statistiques, en surimpression a droite --------------------
		{
			UBorder* Volet = WidgetTree->ConstructWidget<UBorder>(
				UBorder::StaticClass(), TEXT("Volet"));
			// LE FOND EST SEMI-TRANSPARENT A DESSEIN : opaque, il ferait une
			// colonne posee sur l'image ; absent, le texte clair disparaitrait
			// sur la calotte glaciaire et sur les deserts.
			Volet->SetBrushColor(ColVolet);
			Volet->SetPadding(FMargin(18.0f, 16.0f));
			if (UOverlaySlot* S = Body->AddChildToOverlay(Volet))
			{
				S->SetHorizontalAlignment(HAlign_Right);
				S->SetVerticalAlignment(VAlign_Fill);
			}

			USizeBox* VoletSize = WidgetTree->ConstructWidget<USizeBox>(
				USizeBox::StaticClass(), TEXT("VoletSize"));
			VoletSize->SetWidthOverride(LargeurVolet);
			Volet->AddChild(VoletSize);

			UVerticalBox* Side = WidgetTree->ConstructWidget<UVerticalBox>(
				UVerticalBox::StaticClass(), TEXT("Side"));
			VoletSize->AddChild(Side);

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
			InfoText = MakeText(TEXT("InfoText"), TEXT(""), TypoLegende, ColTextMuted);
			if (UVerticalBoxSlot* S = Side->AddChildToVerticalBox(InfoText))
			{
				S->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
			}

			// --- depart du joueur -----------------------------------------
			//
			// IL VIENT AVANT LES BIOMES, et pas apres : la liste des biomes
			// fait quinze lignes et pousserait ce bloc hors de l'ecran, alors
			// que c'est une ACTION -- la derniere avant d'entrer dans le
			// monde -- et qu'une action qu'on ne voit pas n'existe pas.
			if (UVerticalBoxSlot* S = Side->AddChildToVerticalBox(MakeRule(TEXT("RuleDepart"))))
			{
				S->SetPadding(FMargin(0.0f, 18.0f, 0.0f, 14.0f));
			}

			Side->AddChildToVerticalBox(MakeSectionLabel(
				TEXT("DepartLabel"), TEXT("DEPART DU JOUEUR")));

			// La note DIT quoi faire tant que rien n'est choisi, et POURQUOI
			// quand un clic est refuse. Sans elle, un clic en mer ne
			// produirait rien du tout et se lirait comme un ecran casse.
			DepartHint = MakeText(TEXT("DepartHint"),
				TEXT("cliquez une terre sur le globe"), TypoLegende, ColTextMuted);
			if (UVerticalBoxSlot* S = Side->AddChildToVerticalBox(DepartHint))
			{
				S->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
			}

			DepartLatValue = AddStat(TEXT("RowDepLat"), TEXT("LblDepLat"),
				TEXT("ValDepLat"), TEXT("Latitude"));
			DepartBiomeValue = AddStat(TEXT("RowDepBio"), TEXT("LblDepBio"),
				TEXT("ValDepBio"), TEXT("Biome"));
			DepartAltValue = AddStat(TEXT("RowDepAlt"), TEXT("LblDepAlt"),
				TEXT("ValDepAlt"), TEXT("Altitude"));

			// La liste des lieux que la chaine a nommes : arches, avens,
			// dolines, tables. Choisir une entree pose le repere et amene le
			// lieu face a l'observateur.
			LieuxCombo = WidgetTree->ConstructWidget<UComboBoxString>(
				UComboBoxString::StaticClass(), TEXT("LieuxCombo"));

			// Meme contrainte que le selecteur d'habillage : la police doit
			// etre posee AVANT la construction du widget Slate, et le seul
			// chemin public est la propriete depreciee.
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			LieuxCombo->Font = PoliceEcran(TypoControle);
			PRAGMA_ENABLE_DEPRECATION_WARNINGS

			LieuxCombo->AddOption(TEXT("-- au hasard --"));
			LieuxCombo->SetSelectedIndex(0);
			LieuxCombo->OnSelectionChanged.AddDynamic(
				this, &UWorldseedMenuWidget::HandleLieuChanged);

			if (UVerticalBoxSlot* S = Side->AddChildToVerticalBox(
				MakeControlBox(TEXT("LieuxSize"), LieuxCombo, 0.0f)))
			{
				S->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
			}

			// --- statistiques du monde ------------------------------------
			//
			// LA BARRE PORTE LA COULEUR DU BIOME, celle-la meme que le globe
			// peint dessous. C'est ce qui rend la colonne lisible : on lit un
			// pourcentage, on glisse le regard a gauche, et on voit OU il se
			// trouve. Une barre d'accent uniforme aurait oblige a relire le
			// nom a chaque ligne, et la couleur n'aurait rien dit.
			//
			// AUTANT DE LIGNES QUE LE REGISTRE COMPTE DE BIOMES, construites
			// une fois pour toutes ici, et REMPLIES PAR RANG au lieu d'etre
			// reordonnees : la ligne 0 recoit le biome le plus etendu du monde
			// en cours. Deplacer des enfants dans leur boite a chaque
			// generation serait la seule alternative, et elle est plus chere
			// pour un resultat identique.
			//
			// Les lignes en trop sont REPLIEES -- `Collapsed` et non `Hidden` :
			// le second garde la place reservee et laisserait des trous dans la
			// liste.
			if (UVerticalBoxSlot* S = Side->AddChildToVerticalBox(MakeRule(TEXT("RuleBiomes"))))
			{
				S->SetPadding(FMargin(0.0f, 18.0f, 0.0f, 14.0f));
			}

			Side->AddChildToVerticalBox(MakeSectionLabel(
				TEXT("BiomesLabel"), TEXT("BIOMES, PART DES TERRES")));

			// Ce qui tient la place tant qu'aucun monde n'existe. Sans lui,
			// l'intitule surplomberait le vide, ce qui se lit comme un ecran
			// casse plutot que comme un ecran en attente.
			BiomesHint = MakeText(TEXT("BiomesHint"),
				TEXT("choisissez une graine, puis generez"), TypoLegende, ColTextMuted);
			if (UVerticalBoxSlot* S = Side->AddChildToVerticalBox(BiomesHint))
			{
				S->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
			}

			constexpr int32 NbBiomes = static_cast<int32>(EWorldseedBiome::Count);
			BiomeRows.Reserve(NbBiomes);
			BiomeNames.Reserve(NbBiomes);
			BiomeValues.Reserve(NbBiomes);
			BiomeBars.Reserve(NbBiomes);

			for (int32 I = 0; I < NbBiomes; ++I)
			{
				UVerticalBox* Row = WidgetTree->ConstructWidget<UVerticalBox>(
					UVerticalBox::StaticClass(),
					*FString::Printf(TEXT("BiomeRow%02d"), I));
				Row->SetVisibility(ESlateVisibility::Collapsed);
				if (UVerticalBoxSlot* S = Side->AddChildToVerticalBox(Row))
				{
					S->SetPadding(FMargin(0.0f, 7.0f, 0.0f, 0.0f));
				}

				UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>(
					UHorizontalBox::StaticClass(),
					*FString::Printf(TEXT("BiomeHead%02d"), I));
				Row->AddChildToVerticalBox(Head);

				UTextBlock* Nom = MakeText(*FString::Printf(TEXT("BiomeNom%02d"), I),
					TEXT(""), TypoLegende, ColTextPrimary);
				if (UHorizontalBoxSlot* S = Head->AddChildToHorizontalBox(Nom))
				{
					S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
					S->SetVerticalAlignment(VAlign_Center);
				}

				UTextBlock* Val = MakeText(*FString::Printf(TEXT("BiomeVal%02d"), I),
					TEXT(""), TypoLegende, ColTextMuted);
				Val->SetJustification(ETextJustify::Right);
				Head->AddChildToHorizontalBox(Val);

				// TROIS PIXELS, et c'est delibere : la barre est un REPERE DE
				// COMPARAISON, le chiffre est la donnee. Une barre epaisse
				// prendrait le regard a la valeur qu'elle illustre.
				USizeBox* BarBox = WidgetTree->ConstructWidget<USizeBox>(
					USizeBox::StaticClass(),
					*FString::Printf(TEXT("BiomeBarBox%02d"), I));
				BarBox->SetHeightOverride(3.0f);
				if (UVerticalBoxSlot* S = Row->AddChildToVerticalBox(BarBox))
				{
					S->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
				}

				UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>(
					UProgressBar::StaticClass(),
					*FString::Printf(TEXT("BiomeBar%02d"), I));
				Bar->SetPercent(0.0f);

				// LA PISTE VIDE DOIT S'EFFACER. Le style par defaut la peint
				// en clair : a l'image, la barre de 0,2 % et celle de 16,9
				// avaient la meme longueur APPARENTE -- une piste pleine
				// largeur avec un lisere colore dedans. Ce qu'on compare, ce
				// n'est pas le remplissage, c'est la LONGUEUR de la couleur,
				// donc le reste doit disparaitre dans le fond.
				//
				// Par le GETTER et le SETTER : l'acces direct a `WidgetStyle`
				// est deprecie en 5.8, et ce projet compile en avertissements
				// fatals.
				{
					FProgressBarStyle Style = Bar->GetWidgetStyle();
					Style.BackgroundImage.TintColor =
						FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.06f));
					Bar->SetWidgetStyle(Style);
				}

				BarBox->AddChild(Bar);

				BiomeRows.Add(Row);
				BiomeNames.Add(Nom);
				BiomeValues.Add(Val);
				BiomeBars.Add(Bar);
			}
		}

		// -------------------------------------------------- barre d'etat ---
		{
			ProgressGroup = WidgetTree->ConstructWidget<UVerticalBox>(
				UVerticalBox::StaticClass(), TEXT("ProgressGroup"));
			if (UVerticalBoxSlot* S = Shell->AddChildToVerticalBox(ProgressGroup))
			{
				S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 14.0f));
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

			StatusText = MakeText(TEXT("StatusText"), TEXT(""), TypoTexte, ColTextMuted);
			if (UHorizontalBoxSlot* S = StatusRow->AddChildToHorizontalBox(StatusText))
			{
				S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				S->SetVerticalAlignment(VAlign_Center);
			}

			CancelButton = MakeButton(TEXT("CancelButton"), TEXT("CancelLabel"),
				TEXT("Annuler"), TypoControle, ColBoutonDanger);
			StatusRow->AddChildToHorizontalBox(
				MakeControlBox(TEXT("CancelBox"), CancelButton, 0.0f));
		}

		// ============================================================== PIED
		//
		// Le cache est de la MAINTENANCE et l'entree dans le monde est
		// l'ACTION ; les mettre sur la meme ligne n'en fait pas des egaux --
		// c'est la taille et la couleur qui les departagent, et le bord droit
		// qui porte l'action, la ou le regard finit sa course.
		{
			if (UVerticalBoxSlot* S = Shell->AddChildToVerticalBox(MakeRule(TEXT("RuleFooter"))))
			{
				S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
			}

			UHorizontalBox* Footer = WidgetTree->ConstructWidget<UHorizontalBox>(
				UHorizontalBox::StaticClass(), TEXT("Footer"));
			Shell->AddChildToVerticalBox(Footer);

			CacheText = MakeText(TEXT("CacheText"), TEXT(""), TypoLegende, ColTextMuted);
			if (UHorizontalBoxSlot* S = Footer->AddChildToHorizontalBox(CacheText))
			{
				S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				S->SetVerticalAlignment(VAlign_Center);
			}

			ClearObsoleteButton = MakeButton(TEXT("ClearObsoleteButton"),
				TEXT("ClearObsoleteLabel"), TEXT("Purger les perimees"),
				TypoControle, ColBoutonNeutre);
			if (UHorizontalBoxSlot* S = Footer->AddChildToHorizontalBox(
				MakeControlBox(TEXT("ClearObsoleteBox"), ClearObsoleteButton, 0.0f)))
			{
				S->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
				S->SetVerticalAlignment(VAlign_Center);
			}

			ClearAllButton = MakeButton(TEXT("ClearAllButton"), TEXT("ClearAllLabel"),
				TEXT("Tout vider"), TypoControle, ColBoutonDanger);
			if (UHorizontalBoxSlot* S = Footer->AddChildToHorizontalBox(
				MakeControlBox(TEXT("ClearAllBox"), ClearAllButton, 0.0f)))
			{
				S->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
				S->SetVerticalAlignment(VAlign_Center);
			}

			USizeBox* PlayBox = WidgetTree->ConstructWidget<USizeBox>(
				USizeBox::StaticClass(), TEXT("PlayBox"));
			PlayBox->SetHeightOverride(52.0f);
			PlayBox->SetWidthOverride(LargeurAction);

			PlayButton = MakeButton(TEXT("PlayButton"), TEXT("PlayLabel"),
				TEXT("ENTRER DANS LE MONDE"), TypoAction, ColBoutonAction);
			PlayBox->AddChild(PlayButton);

			if (UHorizontalBoxSlot* S = Footer->AddChildToHorizontalBox(PlayBox))
			{
				S->SetPadding(FMargin(28.0f, 0.0f, 0.0f, 0.0f));
				S->SetVerticalAlignment(VAlign_Center);
			}
		}
	}

	return Super::RebuildWidget();
}

void UWorldseedMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// --- le parcours complet, en une ligne de commande ---------------------
	if (FParse::Param(FCommandLine::Get(), TEXT("WorldseedMenuAuto")))
	{
		int32 Graine = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedGraine="), Graine))
		{
			Params.Seed = Graine;
		}
		bAutoJouer = true;
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] menu : parcours automatique, graine %d"), Params.Seed);
		StartGeneration();
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
		// UNE GRAINE EST UN IDENTIFIANT, PAS UNE QUANTITE. `FText::AsNumber`
		// y mettait le separateur de milliers de la locale -- « 1,337 » pour
		// 1337 -- et la relecture le refuse : `Raw.IsNumeric()` rend faux sur
		// la virgule, donc le champ etait IGNORE en silence. Le monde partait
		// juste parce que `Params.Seed` portait deja la bonne valeur ; taper
		// une graine avec un separateur, elle, ne prenait pas.
		SeedBox->SetText(FText::FromString(FString::FromInt(Params.Seed)));
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

	if (GenerateButton)
	{
		GenerateButton->OnClicked.AddDynamic(this, &UWorldseedMenuWidget::HandleGenerateClicked);
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

	// LA TAILLE N'EST PLUS UN CHOIX : voir WorldseedTailleDuMondeM.
	Params.MapSizeMeters = WorldseedTailleDuMondeM;
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
		CachedLithologyId = MoveTemp(PendingResult->Lithology.Id);

		// LA GEOMETRIE D'ABORD, ET C'EST PORTANT.
		//
		// `BakeGlobe` et `BuildPreviewField` commencent tous deux par
		// comparer `CachedHeights.Num()` a `WorldGeometry.CellCount()`.
		// Poser la geometrie APRES eux -- ce que faisait ce code -- leur
		// donnait la geometrie du monde PRECEDENT face aux altitudes du
		// NOUVEAU : la garde ne pouvait que sortir, et elle sort SANS UN MOT.
		//
		// Consequence mesuree, et elle est lourde : le globe n'a jamais ete
		// cuit en textures, donc il est reste sur le lance-de-rayon
		// PROCESSEUR -- quatre cent vingt sur quatre cent vingt pixels, avec
		// un echantillonnage bilineaire du relief par pixel, SOIXANTE FOIS
		// PAR SECONDE sur le fil de jeu. Releve des intervalles du timer dans
		// cet etat : 7,40 ms au plus court, 303,35 au plus long pour une
		// periode demandee de 16,67. C'est ce qui faisait saccader la
		// rotation, bien avant le pas fixe corrige par ailleurs.
		//
		// Le defaut etait invisible parce que la voie de secours REND une
		// image correcte : on voyait un globe juste, et rien ne disait qu'il
		// coutait mille fois son prix.
		WorldGeometry = PendingResult->Geometry;

		// LES LIEUX SE LISENT ICI, ET PAS AILLEURS. Plus haut, WorldGeometry
		// serait encore celle du monde PRECEDENT et les latitudes seraient
		// fausses ; plus bas, PendingResult est relache. Arches, puits et
		// tables ne figurent dans aucun des caches -- c'est la derniere
		// occasion de les voir. Le terrain, lui, les rebatit a l'identique
		// cote jeu, puisqu'ils sont deterministes.
		RemplirLieux(*PendingResult);

		// UN NOUVEAU MONDE EFFACE L'ANCIEN DEPART. Le garder poserait le
		// repere a une latitude qui, sur cette carte-ci, peut tomber en pleine
		// mer -- et le joueur naitrait quelque part sans l'avoir choisi.
		PoserDepart(0.0f, 0.0f, INDEX_NONE);

		BuildPreviewField();

		// La voie graphique ensuite ; BuildPreviewField n'aura servi qu'au repli.
		BakeGlobe();

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

	// ON APPUIE SUR LE MEME BOUTON QUE LE JOUEUR, et c-est la condition pour
	// que le test vaille : une voie de traverse ne prouverait rien du parcours
	// reel. HandlePlayClicked depose le monde et change de niveau.
	if (bAutoJouer && CachedHeights.Num() > 0)
	{
		bAutoJouer = false;
		HandlePlayClicked();
	}
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

	UpdateBiomeStats();
}

void UWorldseedMenuWidget::UpdateBiomeStats()
{
	if (BiomeRows.Num() == 0)
	{
		return;
	}

	// --- ce qu'on affiche, et ce qu'on laisse de cote ----------------------
	//
	// `LandSharePct` est indexe par biome sur TOUT le registre, couvertures
	// comprises -- ocean, lac, riviere. Ces trois-la ne sont pas attribues
	// depuis le retrait de l'hydrologie, donc ils sortiraient a zero ; mais
	// c'est le zero qui les elimine, pas leur nature, et le jour ou l'un d'eux
	// revient il apparaitra ici tout seul.
	//
	// LE SEUIL EST A UN DIXIEME DE POINT, c'est-a-dire la precision de ce
	// qu'on ecrit. Afficher « 0,0 % » sous une barre vide occuperait une ligne
	// pour dire qu'il n'y a rien a dire.
	struct FPart
	{
		int32 Id = 0;
		float Pct = 0.0f;
	};

	TArray<FPart> Parts;
	Parts.Reserve(BiomeRows.Num());

	float Max = 0.0f;
	for (int32 I = 0; I < static_cast<int32>(EWorldseedBiome::Count); ++I)
	{
		const float Pct = CachedBiomes.LandSharePct[I];
		if (Pct >= 0.05f)
		{
			Parts.Add({ I, Pct });
			Max = FMath::Max(Max, Pct);
		}
	}

	Parts.Sort([](const FPart& A, const FPart& B) { return A.Pct > B.Pct; });

	if (BiomesHint)
	{
		BiomesHint->SetVisibility(Parts.Num() > 0
			? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}

	// LA BARRE EST RAPPORTEE AU PLUS GRAND BIOME, PAS A CENT. Rapportee a
	// cent, la premiere barre ferait un cinquieme de la largeur et les
	// dernieres seraient invisibles : on ne distinguerait plus 0,3 % de 2,8.
	// Le chiffre, lui, reste absolu -- c'est lui qui porte la valeur, la barre
	// ne porte que la comparaison.
	const float Echelle = Max > 0.0f ? Max : 1.0f;

	for (int32 Rang = 0; Rang < BiomeRows.Num(); ++Rang)
	{
		const bool bUtilise = Rang < Parts.Num();

		if (BiomeRows[Rang])
		{
			BiomeRows[Rang]->SetVisibility(bUtilise
				? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
		if (!bUtilise)
		{
			continue;
		}

		const EWorldseedBiome Biome = static_cast<EWorldseedBiome>(Parts[Rang].Id);
		const FLinearColor Teinte = WorldseedBiomes::Colour(Biome);

		if (BiomeNames[Rang])
		{
			BiomeNames[Rang]->SetText(FText::FromString(WorldseedBiomes::Name(Biome)));
		}
		if (BiomeValues[Rang])
		{
			BiomeValues[Rang]->SetText(FText::FromString(
				FString::Printf(TEXT("%.1f %%"), Parts[Rang].Pct)));
		}
		if (BiomeBars[Rang])
		{
			BiomeBars[Rang]->SetPercent(Parts[Rang].Pct / Echelle);
			BiomeBars[Rang]->SetFillColorAndOpacity(Teinte);
		}
	}
}

bool UWorldseedMenuWidget::BakeGlobe()
{
	GlobeMaterial = nullptr;
	GlobeTextures = FWorldseedGlobeTextures();

	// --- LE PROCESSEUR EST LA VOIE PAR DEFAUT, ET C'EST UNE MESURE QUI L'A
	// --- DECIDE, PAS UN GOUT ---------------------------------------------
	//
	// Cette fonction cuit le monde en textures pour que la carte graphique
	// fasse tourner le globe a partir de deux scalaires. C'etait presente
	// comme la « voie par defaut » et le lance-de-rayon processeur comme un
	// repli degrade. L'A/B dit l'inverse.
	//
	//     voie          redessin        aspect
	//     materiau      0,001 ms        ocean presque noir, bandes de
	//                                   latitude opaques et larges, relief
	//                                   sans ombrage
	//     processeur    0,228 ms        bathymetrie, relief ombre, cercles
	//                                   fins -- le globe qu'on veut montrer
	//
	// 0,228 ms, c'est UN VIRGULE QUATRE POUR CENT d'un budget de 16,67. Le
	// processeur ne coute donc rien de perceptible, et il rend nettement
	// mieux : il devient le defaut.
	//
	// CE QUE J'AVAIS SUPPOSE ET QUI ETAIT FAUX. La rotation saccadait, le
	// releve disait « rendu par le processeur », et j'en ai conclu que le
	// cout par image en etait la cause. Il ne l'etait pas : la saccade venait
	// entierement du pas de rotation FIXE sur un timer irregulier (voir
	// `HandleGlobeTimer`). Chronometrer la voie soupconnee AVANT de la
	// remplacer aurait evite le detour -- le depot a deja la regle, « mesurer
	// avant de corriger, meme quand l'hypothese est seduisante ».
	//
	// LE DEFAUT D'ORDRE TROUVE EN CHEMIN RESTE CORRIGE, lui, et il etait
	// reel : `WorldGeometry` etait posee APRES cet appel, donc la garde
	// ci-dessous sortait toujours, sans un mot. Le materiau n'etait pas un
	// choix, il etait inatteignable.
	//
	// `-WorldseedGlobeGPU` rearme la voie graphique. Un A/B entre les deux
	// rendus ne doit RIEN demander d'autre qu'un argument : editer un fichier
	// de reglages changerait son empreinte, donc regenererait le monde entre
	// les deux moities, et l'on ne comparerait plus la meme chose. Ce depot a
	// paye cette lecon deux fois, dont une en vidant `world_rules.json`.
	if (!FParse::Param(FCommandLine::Get(), TEXT("WorldseedGlobeGPU")))
	{
		return false;
	}

	if (WorldGeometry.NX < 2 || CachedHeights.Num() != WorldGeometry.CellCount())
	{
		return false;
	}

	// Le materiau est un ASSET : il peut manquer d'un depot a l'autre. On le
	// charge sans y croire, et le lance-de-rayon processeur reprend la main
	// sans que rien ne se voie -- c'est lui le defaut, voir plus haut.
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

		// LE RETICULE NE SE DESSINE PAS SUR LA VOIE GRAPHIQUE, et il faut le
		// dire plutot que de laisser croire a une panne. Le materiau du globe
		// vit dans un .uasset : y ajouter un parametre demanderait de la
		// chirurgie d'asset par MCP, un lien que ce depot sait tomber a chaque
		// relance de l'editeur. Le POINTAGE, lui, marche dans les deux cas --
		// il ne depend que de la rotation, pas de la voie de rendu.
		static bool bDejaDit = false;
		if (Repere.bActif && !bDejaDit)
		{
			bDejaDit = true;
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] globe : le depart est retenu mais le reticule ")
				TEXT("n'est pas dessine sur la voie graphique (-WorldseedGlobeGPU)"));
		}
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

	// LES BIOMES DONNENT SA COULEUR AU GLOBE. Sans eux il teintait par
	// ALTITUDE, et cette teinte mentait : une calotte glaciaire posee a trente
	// metres s'affichait au vert des plaines, et les sommets blancs n'etaient
	// pas de la neige mais de la hauteur. Ils doivent decrire LA MEME grille
	// que les altitudes, d'ou le meme choix apercu/plein.
	const TArray<uint8>& GlobeBiomes = bUsePreview ? PreviewBiomes : CachedBiomes.Index;
	const TArray<uint8>* GlobeBiomesPtr =
		(GlobeBiomes.Num() == GlobeHeights.Num()) ? &GlobeBiomes : nullptr;

	const TArray<uint8>& GlobeCover = bUsePreview ? PreviewCover : CachedBiomes.Cover;
	const TArray<uint8>* GlobeCoverPtr =
		(GlobeCover.Num() == GlobeHeights.Num()) ? &GlobeCover : nullptr;

	WorldseedGlobe::FGlobeSettings GlobeSettings;

	// Des regles on ne tire que les latitudes de reference et la
	// correspondance latitude/ligne, jamais les dimensions de grille. Le
	// POINTAGE appelle la meme fonction : les deux doivent voir la meme
	// latitude sous le meme pixel.
	AppliquerLatitudes(Geometry);

	if (Rules)
	{
		GlobeSettings.TropicDeg = static_cast<float>(
			Rules->Num(TEXT("world"), TEXT("tropicDeg"), 23.44));
		GlobeSettings.PolarCircleDeg = static_cast<float>(
			Rules->Num(TEXT("world"), TEXT("polarCircleDeg"), 66.56));
		GlobeSettings.DeepOceanM = static_cast<float>(
			Rules->Num(TEXT("world"), TEXT("minElevationM"), -300.0));
	}
	GlobeSettings.LongitudeOffsetDeg = GlobeLongitudeDeg;
	GlobeSettings.TiltDeg = GlobeTiltDeg;
	GlobeSettings.Repere = Repere;

	// Premiere fois : on cree la texture. Ensuite on ne fait que reecrire ses
	// pixels, sinon la rotation fabriquerait une UTexture2D par frame.
	//
	// CE CHEMIN EST DESORMAIS UN REPLI : il ne sert que si le materiau du globe
	// est introuvable. Voir BakeGlobe.
	if (!PreviewTexture)
	{
		PreviewTexture = WorldseedGlobe::Render(
			GlobeHeights, Geometry, GlobeSettings, 512, GlobeBiomesPtr, GlobeCoverPtr);

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
		Geometry, GlobeSettings, GlobeBiomesPtr, GlobeCoverPtr);
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

	// LES BIOMES SE REDUISENT AU PLUS PROCHE VOISIN, jamais par Downsample :
	// celui-ci fait une MOYENNE, juste pour des altitudes et faux pour un code
	// de biome -- la moyenne de « desert » et de « toundra » designe un biome
	// qui n'existe nulle part sur la carte.
	WorldseedGrid::DownsampleNearest(CachedBiomes.Index, WorldGeometry.NX,
		WorldGeometry.NY, DstNX, DstNY, PreviewBiomes);
	WorldseedGrid::DownsampleNearest(CachedBiomes.Cover, WorldGeometry.NX,
		WorldGeometry.NY, DstNX, DstNY, PreviewCover);

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
	PollGeneration();

	// --- le pas de rotation suit le TEMPS REEL, pas la periode demandee ----
	//
	// Un timer d'Unreal est servi PAR LE TICK DU MONDE : il ne peut pas se
	// declencher plus souvent que la trame, et quand la trame est plus courte
	// que la periode il se declenche un tick sur deux ou sur trois, selon
	// l'alignement. Avancer d'un pas FIXE a chaque declenchement -- ce que
	// faisait ce code -- donne donc une vitesse angulaire qui suit le
	// battement du timer et non l'horloge : c'est exactement une SACCADE, et
	// elle se voit d'autant mieux que la trame est rapide.
	//
	// LE REMEDE NE CHANGE PAS LA VITESSE MOYENNE, il la rend constante : on
	// multiplie par le temps ECOULE. Le premier declenchement n'a pas de
	// predecesseur, d'ou le repli sur la periode nominale.
	const double Maintenant = FPlatformTime::Seconds();
	const bool bPremier = (DernierTicGlobe <= 0.0);
	const double Ecoule = bPremier
		? static_cast<double>(GlobeRedrawPeriod) : (Maintenant - DernierTicGlobe);
	DernierTicGlobe = Maintenant;

	// Un arret du jeu, un changement de carte ou un point d'arret rendent un
	// ecart enorme : on le borne, sinon le globe ferait un tour complet d'un
	// coup au retour.
	const float Delta = FMath::Clamp(static_cast<float>(Ecoule), 0.0f, 0.25f);

	// --- releve, une seule fois par ouverture de l'ecran ------------------
	//
	// Il dit ce que la periode demandee ne dit pas : a quel rythme le timer
	// est REELLEMENT servi. Sans lui, « le globe saccade » reste une
	// impression et la correction ci-dessus une hypothese -- et ce depot a
	// une regle contre les hypotheses enchainees a l'aveugle. Il rapporte au
	// passage par quelle voie le globe est rendu : le lance-de-rayon
	// processeur coute mille fois le parametre scalaire du materiau, et ce
	// serait une TOUT AUTRE cause pour le meme symptome.
	//
	// IL NE DEMARRE QU'UNE FOIS LE MONDE PRET, et c'est la lecon du premier
	// releve : pris des l'ouverture, il a mesure les trois premieres secondes
	// -- pendant lesquelles `RedrawGlobe` sort a sa premiere garde et le
	// materiau n'est pas encore cuit. Il annoncait donc « rendu par le
	// processeur » pour un globe qui ne tournait pas encore. On mesure le
	// traitement quand il a lieu, jamais avant.
	if (!bPremier && CachedHeights.Num() > 0 && IntervallesGlobe.Num() < NbReleveGlobe)
	{
		IntervallesGlobe.Add(Ecoule * 1000.0);

		if (IntervallesGlobe.Num() == NbReleveGlobe)
		{
			TArray<double> Tri = IntervallesGlobe;
			Tri.Sort();
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] globe : periode demandee %.2f ms, reelle min %.2f ")
				TEXT("mediane %.2f p95 %.2f max %.2f sur %d declenchements (rendu %s)"),
				GlobeRedrawPeriod * 1000.0f, Tri[0], Tri[Tri.Num() / 2],
				Tri[FMath::Min(Tri.Num() - 1, (Tri.Num() * 95) / 100)], Tri.Last(),
				Tri.Num(), GlobeMaterial ? TEXT("par le materiau") : TEXT("par le processeur"));
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] globe : redessin %.3f ms en moyenne sur %d appels"),
				NbRedraw > 0 ? CumulRedrawMs / NbRedraw : 0.0, NbRedraw);
		}
	}

	// La rotation automatique s'efface devant le geste du joueur : reprendre a
	// tourner sous ses doigts pendant qu'il oriente le globe serait desagreable.
	//
	// ET ELLE NE REVIENT PLUS, DES LA PREMIERE PRISE EN MAIN. Tant que le
	// globe n'etait qu'a regarder, tourner tout seul etait une invitation.
	// Depuis qu'on y CHOISIT un point, c'est une cible mobile : a cinq degres
	// par seconde, viser une ile demande de compenser le mouvement, et la
	// meme terre n'est plus sous le curseur deux secondes plus tard. Une fois
	// que le joueur a saisi le globe, il decide de son orientation.
	if (!bDraggingGlobe && !bGlobePrisEnMain && AutoSpinDegPerSecond != 0.0f)
	{
		GlobeLongitudeDeg = FMath::Fmod(
			GlobeLongitudeDeg + AutoSpinDegPerSecond * Delta, 360.0f);
	}

	// CE QUE COUTE UN REDESSIN, et c'est la grandeur qui tranche entre les
	// deux voies : le materiau ne pose que deux scalaires, le lance-de-rayon
	// processeur calcule 420 x 420 pixels avec un echantillonnage bilineaire
	// du relief. Sans ce chiffre, « le processeur coute cher » reste une
	// affirmation.
	const double AvantRedraw = FPlatformTime::Seconds();
	RedrawGlobe();
	CumulRedrawMs += (FPlatformTime::Seconds() - AvantRedraw) * 1000.0;
	++NbRedraw;
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
	bGlobePrisEnMain = true;
	LastDragPosition = InMouseEvent.GetScreenSpacePosition();

	// Le compteur repart a zero : c'est lui qui, au relachement, dira si le
	// geste etait un clic ou un glisser.
	CumulGlisse = 0.0f;

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

	// ON CUMULE LE CHEMIN PARCOURU, PAS L'ECART ENTRE DEPART ET ARRIVEE. Un
	// aller-retour revient au point de depart : mesurer les deux extremites
	// declarerait CLIC un glisser qui a fait tourner le globe et l'a ramene,
	// et le repere sauterait sous le curseur sans qu'on l'ait demande.
	CumulGlisse += static_cast<float>(Delta.Size());

	// 0,35 degre par pixel : un glisser de la largeur du globe fait environ un
	// demi-tour, ce qui se manipule bien.
	GlobeLongitudeDeg = FMath::Fmod(
		GlobeLongitudeDeg - static_cast<float>(Delta.X) * 0.35f + 360.0f, 360.0f);

	// L.inclinaison est bornee : au-dela on regarde le pole par-dessus et la
	// reprojection devient illisible.
	//
	// LE SIGNE EST CELUI D.UNE BOULE QU.ON ROULE, pas celui d.une camera.
	// Tirer vers le BAS fait basculer le pole nord vers soi, comme si l.on
	// posait le doigt sur le globe pour le faire tourner. C.est la convention
	// de tout globe manipulable, et l.inverse se ressent immediatement comme
	// une inversion -- signale par le proprietaire.
	GlobeTiltDeg = FMath::Clamp(
		GlobeTiltDeg - static_cast<float>(Delta.Y) * 0.35f, -80.0f, 80.0f);

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

	// UN GESTE QUI N'A PAS BOUGE EST UN CLIC, et il choisit ou naitre. Le
	// glisser reste le geste par defaut : on ne pose un repere que si l'on
	// n'a manifestement pas voulu tourner le globe.
	if (CumulGlisse <= SeuilClicPx)
	{
		float LatitudeDeg = 0.0f;
		float LongitudeDeg = 0.0f;
		int32 Cellule = INDEX_NONE;

		if (PointerSurLeGlobe(InMouseEvent.GetScreenSpacePosition(),
			LatitudeDeg, LongitudeDeg, Cellule))
		{
			PoserDepart(LatitudeDeg, LongitudeDeg, Cellule);

			// Le choix a la main l'emporte sur la liste : on la ramene a son
			// entree vide, sans quoi elle afficherait un lieu qui n'est plus
			// celui du repere.
			if (LieuxCombo && LieuxCombo->GetOptionCount() > 0
				&& LieuxCombo->GetSelectedIndex() != 0)
			{
				LieuxCombo->SetSelectedIndex(0);
			}
		}
	}

	return FReply::Handled().ReleaseMouseCapture();
}

void UWorldseedMenuWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	bDraggingGlobe = false;
	Super::NativeOnMouseCaptureLost(CaptureLostEvent);
}

FReply UWorldseedMenuWidget::NativeOnMouseWheel(const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	// LE PAS EST MULTIPLICATIF, PAS ADDITIF. Un pas constant donne un zoom
	// nerveux de pres et mou de loin ; un facteur donne le meme ressenti a
	// toutes les echelles, ce qui est la convention de tout zoom.
	const float Delta = InMouseEvent.GetWheelDelta();
	if (FMath::IsNearlyZero(Delta))
	{
		return Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
	}

	// Les bornes ne sont pas un gout : en dessous de 1 le globe flotte dans un
	// cadre vide, et au-dela de 6 on depasse la resolution de la texture cuite
	// et l'on ne grossit plus que des texels.
	GlobeZoom = FMath::Clamp(GlobeZoom * FMath::Pow(1.15f, Delta), 1.0f, 6.0f);
	ApplyGlobeZoom();
	return FReply::Handled();
}

void UWorldseedMenuWidget::ApplyGlobeZoom()
{
	if (!PreviewImage)
	{
		return;
	}

	// L'ECHELLE EST UNE TRANSFORMATION DE RENDU, donc elle ne touche pas a la
	// mise en page : le cadre du globe garde sa taille et sa place, et la
	// colonne des reglages ne bouge pas quand on zoome. C'est le SizeBox qui
	// decoupe, ce qui donne un vrai hublot plutot qu'un globe qui deborde.
	PreviewImage->SetRenderScale(FVector2D(GlobeZoom, GlobeZoom));
}

void UWorldseedMenuWidget::HandleSeedCommitted(const FText& Text, ETextCommit::Type CommitMethod)
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
		SeedBox->SetText(FText::FromString(FString::FromInt(Params.Seed)));
	}
	StartGeneration();
}

void UWorldseedMenuWidget::HandleGenerateClicked()
{
	// RIEN DE PLUS QUE LA TOUCHE ENTREE, ET C'EST VOULU : HandleSeedCommitted
	// ne fait pas autre chose. StartGeneration lit lui-meme le formulaire
	// (PullFormIntoParams) puis annule la generation en cours avant de
	// relancer -- on peut donc cliquer a tout moment, y compris pendant un
	// calcul, ce qui est exactement ce qu'on attend d'un bouton « Generer »
	// quand on vient de changer la graine.
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
		ToPlay.LithologyId = CachedLithologyId;
		ToPlay.TexturePack = SelectedPack;
		ToPlay.SpawnXYM = DepartXYM;
		ToPlay.bHasSpawn = bDepartChoisi;
		GI->StoreWorld(ToPlay);

		if (bDepartChoisi)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] menu : depart demande a (%.0f, %.0f) m, ")
				TEXT("latitude %.1f deg"),
				DepartXYM.X, DepartXYM.Y, Repere.LatitudeDeg);
		}

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


// ------------------------------------------------ choisir ou naitre

void UWorldseedMenuWidget::AppliquerLatitudes(FWorldseedGeometry& Geometry) const
{
	// LA GEOMETRIE VIENT DU MONDE, LES LATITUDES VIENNENT DES REGLES. Les
	// regles decrivent une resolution de REFERENCE, pas celle qu'on a
	// generee : y prendre NX et NY rendrait le heightfield incoherent avec sa
	// grille. On n'en tire donc que la correspondance latitude/ligne.
	//
	// Ces trois lignes vivent ICI et nulle part ailleurs : le rendu du globe
	// et le pointage doivent subir le MEME patch, sinon le point qu'on choisit
	// et le point qu'on voit ne sont pas a la meme latitude.
	if (Rules)
	{
		Geometry.LatSpanDeg = Rules->Geometry.LatSpanDeg;
		Geometry.LatitudeMapping = Rules->Geometry.LatitudeMapping;
		Geometry.LatitudeEqualAreaBlend = Rules->Geometry.LatitudeEqualAreaBlend;
	}
}

FWorldseedGeometry UWorldseedMenuWidget::GeometrieCarte() const
{
	FWorldseedGeometry G = WorldGeometry;
	AppliquerLatitudes(G);
	return G;
}

bool UWorldseedMenuWidget::PointerSurLeGlobe(const FVector2D& PositionEcran,
	float& OutLatitudeDeg, float& OutLongitudeDeg, int32& OutCellule) const
{
	OutCellule = INDEX_NONE;

	if (!PreviewImage || WorldGeometry.NX < 2
		|| CachedHeights.Num() != WorldGeometry.CellCount())
	{
		return false;
	}

	// LA GEOMETRIE DE L'IMAGE, PAS CELLE DU WIDGET. Les gestionnaires de
	// souris recoivent le cadre de l'ecran entier ; le globe n'en occupe
	// qu'un carre, et rapporter le clic au mauvais cadre le decalerait de
	// toute la largeur du panneau de gauche.
	const FGeometry& Cadre = PreviewImage->GetCachedGeometry();
	const FVector2D Taille = Cadre.GetLocalSize();
	if (Taille.X <= KINDA_SMALL_NUMBER || Taille.Y <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector2D Locale = Cadre.AbsoluteToLocal(PositionEcran);

	// DEFAIRE LE ZOOM, ET C'EST LE POINT LE PLUS INCERTAIN DE TOUTE LA CHAINE.
	// `SetRenderScale` est une transformation de RENDU : elle agrandit l'image
	// autour de son pivot -- le centre par defaut -- sans toucher a la mise en
	// page, donc sans toucher au cadre que `GetCachedGeometry` rend. Le texel
	// T s'affiche en (T - centre) * zoom + centre ; on inverse.
	//
	// Le controle qui tranche est a l'image : si le reticule tombe sous le
	// curseur a zoom 1 mais derive a zoom 4, c'est ici qu'est la faute.
	const FVector2D Centre = Taille * 0.5;
	const float Zoom = FMath::Max(GlobeZoom, KINDA_SMALL_NUMBER);
	const FVector2D Texel = (Locale - Centre) / static_cast<double>(Zoom) + Centre;

	float CadreX = 0.0f;
	float CadreY = 0.0f;
	WorldseedGlobe::CadreDepuisUV(
		static_cast<float>(Texel.X / Taille.X),
		static_cast<float>(Texel.Y / Taille.Y), CadreX, CadreY);

	WorldseedGlobe::FGlobeSettings Reglages;
	Reglages.LongitudeOffsetDeg = GlobeLongitudeDeg;
	Reglages.TiltDeg = GlobeTiltDeg;

	const WorldseedGlobe::FPointeGlobe Pointe = WorldseedGlobe::PointerCadre(
		CadreX, CadreY, WorldseedGlobe::CadreGlobe(Reglages));

	if (!Pointe.bSurLeGlobe)
	{
		return false;
	}

	OutLatitudeDeg = Pointe.LatitudeDeg;
	OutLongitudeDeg = Pointe.LongitudeDeg;

	OutCellule = CelluleDe(Pointe.LatitudeDeg, Pointe.LongitudeDeg);
	return true;
}

int32 UWorldseedMenuWidget::CelluleDe(float LatitudeDeg, float LongitudeDeg) const
{
	const FWorldseedGeometry G = GeometrieCarte();
	if (G.NX < 2 || G.NY < 2)
	{
		return INDEX_NONE;
	}

	const float U = LongitudeDeg / 360.0f;
	const float V = G.VForLatitudeDeg(LatitudeDeg);

	// LA LIGNE SE PREND PAR ARRONDI SUR NY - 1, et non par troncature sur NY.
	// La ligne J porte les valeurs calculees a la latitude
	// LatitudeDegForRow(J), c'est-a-dire a V = J / (NY - 1) : c'est le plus
	// proche de CETTE suite qu'il faut, pas la case d'un decoupage en NY
	// tranches. L'ecart ne vaut qu'une demi-cellule -- huit metres sur ce
	// monde -- mais il est gratuit a eviter.
	//
	// LA COLONNE S'ENROULE, ELLE : un monde n'a pas de bord est-ouest, et une
	// longitude de 359,9 degres doit tomber a cote de zero, pas hors grille.
	const int32 I = ((FMath::FloorToInt(U * G.NX) % G.NX) + G.NX) % G.NX;
	const int32 J = FMath::Clamp(FMath::RoundToInt(V * (G.NY - 1)), 0, G.NY - 1);

	return J * G.NX + I;
}

void UWorldseedMenuWidget::PoserDepart(float LatitudeDeg, float LongitudeDeg,
	int32 Cellule)
{
	// Le motif passe par une FString et non par un TCHAR* : un Printf rend un
	// temporaire dont le tampon meurt a la fin de l'expression.
	auto Effacer = [this](const FString& Pourquoi)
	{
		bDepartChoisi = false;
		Repere.bActif = false;
		if (DepartHint) { DepartHint->SetText(FText::FromString(Pourquoi)); }
		if (DepartLatValue) { DepartLatValue->SetText(FText::FromString(TEXT("--"))); }
		if (DepartBiomeValue) { DepartBiomeValue->SetText(FText::FromString(TEXT("--"))); }
		if (DepartAltValue) { DepartAltValue->SetText(FText::FromString(TEXT("--"))); }
	};

	if (!CachedHeights.IsValidIndex(Cellule))
	{
		Effacer(TEXT("cliquez une terre sur le globe"));
		return;
	}

	const float AltitudeM = CachedHeights[Cellule];

	// LE CLIC EN MER EST REFUSE, et il le dit. Reporter en silence le point
	// sur la cote la plus proche pourrait deplacer le joueur de vingt
	// kilometres sans qu'il l'ait demande ni le sache.
	//
	// MAIS UN REFUS N'EFFACE PAS UN CHOIX DEJA FAIT : c'est le clic NOUVEAU
	// qui est refuse, pas l'ancien. Perdre son depart parce qu'on a vise trois
	// pixels a cote du trait de cote serait une punition, pas une protection
	// -- et le globe tourne pendant qu'on vise.
	if (AltitudeM <= 0.0f)
	{
		const FString Motif = FString::Printf(
			TEXT("en mer (%.0f m de fond)"), -AltitudeM);

		if (bDepartChoisi)
		{
			if (DepartHint)
			{
				DepartHint->SetText(FText::FromString(
					Motif + TEXT(" -- le depart precedent est garde")));
			}
		}
		else
		{
			Effacer(Motif + TEXT(" -- choisissez une terre"));
		}
		return;
	}

	const FWorldseedGeometry G = GeometrieCarte();
	const float V = G.VForLatitudeDeg(LatitudeDeg);

	// Le repere de la carte : U enroule sur la largeur, V borne sur la
	// hauteur, l'origine au centre. C'est la convention que le champ de
	// densite emploie pour lire la grille, donc celle que le terrain
	// comprendra sans traduction.
	DepartXYM = FVector2D(
		(LongitudeDeg / 360.0f - 0.5f) * G.WidthM(),
		(V - 0.5f) * G.HeightM);
	bDepartChoisi = true;

	Repere.bActif = true;
	Repere.LatitudeDeg = LatitudeDeg;
	Repere.LongitudeDeg = LongitudeDeg;

	const TCHAR* NomBiome = TEXT("--");
	if (CachedBiomes.Index.IsValidIndex(Cellule) && CachedBiomes.Cover.IsValidIndex(Cellule))
	{
		NomBiome = WorldseedBiomes::Name(WorldseedBiomes::AppearanceBiome(
			CachedBiomes.Index[Cellule], CachedBiomes.Cover[Cellule]));
	}

	if (DepartHint)
	{
		DepartHint->SetText(FText::FromString(
			TEXT("cliquez ailleurs pour deplacer le depart")));
	}
	if (DepartLatValue)
	{
		// L'HEMISPHERE PLUTOT QU'UN SIGNE : « -47 deg » se lit mal, et le
		// globe montre justement deux calottes qu'il faut pouvoir distinguer.
		DepartLatValue->SetText(FText::FromString(FString::Printf(
			TEXT("%.1f %s"), FMath::Abs(LatitudeDeg),
			LatitudeDeg >= 0.0f ? TEXT("N") : TEXT("S"))));
	}
	if (DepartBiomeValue)
	{
		DepartBiomeValue->SetText(FText::FromString(NomBiome));
	}
	if (DepartAltValue)
	{
		DepartAltValue->SetText(FText::FromString(
			FString::Printf(TEXT("%.0f m"), AltitudeM)));
	}
}

void UWorldseedMenuWidget::RemplirLieux(const WorldseedPipeline::FResult& Resultat)
{
	LieuxXYM.Reset();
	if (!LieuxCombo)
	{
		return;
	}

	LieuxCombo->ClearOptions();

	// L'ENTREE VIDE EST LA PREMIERE, ET ELLE PORTE LE COMPORTEMENT D'AVANT :
	// sans choix, le terrain decide seul, comme il l'a toujours fait. Son
	// index nourrit quand meme le tableau, pour que liste et positions se
	// correspondent un pour un -- un decalage d'un cran se lirait comme une
	// erreur de projection alors que ce serait une erreur de comptage.
	LieuxCombo->AddOption(TEXT("-- au hasard --"));
	LieuxXYM.Add(FVector2D::ZeroVector);

	const FWorldseedGeometry G = GeometrieCarte();
	if (G.NX < 2 || CachedHeights.Num() != G.CellCount())
	{
		LieuxCombo->SetSelectedIndex(0);
		return;
	}

	auto Ajouter = [&](const TCHAR* Famille, int32 Rang, const FVector2D& XY,
		float Detail, const TCHAR* Unite)
	{
		const float V = XY.Y / G.HeightM + 0.5f;
		const float Lat = G.LatitudeDegForV(V);
		const float Lon = (XY.X / G.WidthM() + 0.5f) * 360.0f;

		const int32 Cellule = CelluleDe(Lat, Lon);

		// UN LIEU NE VAUT D'ETRE OFFERT QUE SI L'ON PEUT Y NAITRE. Une forme
		// dont le point le plus haut passe sous l'eau enverrait le joueur
		// nager, et le refus du clic en mer n'aurait alors plus de sens.
		if (!CachedHeights.IsValidIndex(Cellule) || CachedHeights[Cellule] <= 0.0f)
		{
			return;
		}

		LieuxCombo->AddOption(FString::Printf(TEXT("%s %d  --  %.0f %s  %s"),
			Famille, Rang, Detail, Unite,
			*FString::Printf(TEXT("%.0f%s"), FMath::Abs(Lat),
				Lat >= 0.0f ? TEXT("N") : TEXT("S"))));
		LieuxXYM.Add(XY);
	};

	// ON NE LISTE PAS TOUT, ET C'EST UN CHOIX. Un monde de 64 x 32 km a deja
	// porte 574 gouffres et 154 dolines : une liste de sept cents entrees ne
	// se parcourt pas, elle se subit. On garde les plus REMARQUABLES de
	// chaque famille -- la plus grande arche, le puits le plus profond, la
	// table a la plus haute paroi -- ce qui est exactement ce qu'on cherche
	// quand on veut aller voir quelque chose.
	constexpr int32 ParFamille = 10;

	{
		TArray<const FWorldseedCaveArch*> Tri;
		for (const FWorldseedCaveArch& A : Resultat.Caves.Arches) { Tri.Add(&A); }
		Tri.Sort([](const FWorldseedCaveArch& A, const FWorldseedCaveArch& B)
			{ return A.RayonM > B.RayonM; });

		int32 Rang = 0;
		for (const FWorldseedCaveArch* A : Tri)
		{
			if (Rang >= ParFamille) { break; }
			Ajouter(TEXT("Arche"), Rang + 1,
				FVector2D(A->CentreM.X, A->CentreM.Y), A->RayonM * 2.0f, TEXT("m"));
			++Rang;
		}
	}

	{
		// Dolines et avens sont tries ENSEMBLE mais nommes separement : ce
		// sont deux formes, et le sens du profil est tout ce qui les separe.
		TArray<const FWorldseedCavePuits*> Tri;
		for (const FWorldseedCavePuits& P : Resultat.Caves.Puits) { Tri.Add(&P); }
		Tri.Sort([](const FWorldseedCavePuits& A, const FWorldseedCavePuits& B)
			{ return (A.HautM - A.BasM) > (B.HautM - B.BasM); });

		int32 RangDoline = 0;
		int32 RangAven = 0;
		for (const FWorldseedCavePuits* P : Tri)
		{
			int32& Rang = P->bDoline ? RangDoline : RangAven;
			if (Rang >= ParFamille) { continue; }
			Ajouter(P->bDoline ? TEXT("Doline") : TEXT("Aven"), Rang + 1,
				FVector2D(P->CentreM.X, P->CentreM.Y),
				P->HautM - P->BasM, TEXT("m de chute"));
			++Rang;
		}
	}

	{
		TArray<const FWorldseedPlateauSite*> Tri;
		for (const FWorldseedPlateauSite& T : Resultat.Tables) { Tri.Add(&T); }
		Tri.Sort([](const FWorldseedPlateauSite& A, const FWorldseedPlateauSite& B)
			{ return A.EscarpementM > B.EscarpementM; });

		int32 Rang = 0;
		for (const FWorldseedPlateauSite* T : Tri)
		{
			if (Rang >= ParFamille) { break; }
			Ajouter(TEXT("Table"), Rang + 1, T->CentreM, T->EscarpementM,
				TEXT("m de paroi"));
			++Rang;
		}
	}

	LieuxCombo->SetSelectedIndex(0);

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] menu : %d lieux offerts (%d arches, %d puits, %d tables produits)"),
		LieuxXYM.Num() - 1, Resultat.Caves.Arches.Num(),
		Resultat.Caves.Puits.Num(), Resultat.Tables.Num());
}

void UWorldseedMenuWidget::HandleLieuChanged(FString SelectedItem,
	ESelectInfo::Type SelectionType)
{
	if (!LieuxCombo)
	{
		return;
	}

	// UNE SELECTION POSEE PAR LE CODE N'EST PAS UN CHOIX DU JOUEUR, et Slate
	// le dit : `Direct` vient de SetSelectedIndex, `OnMouseClick` de la main.
	// Sans ce test, ramener la liste a son entree vide apres un clic sur le
	// globe effacerait le repere qu'on vient tout juste de poser -- et de
	// meme, remplir la liste apres une generation l'effacerait aussi.
	if (SelectionType == ESelectInfo::Direct)
	{
		return;
	}

	const int32 Index = LieuxCombo->GetSelectedIndex();

	// L'entree vide rend la main au terrain.
	if (Index <= 0 || !LieuxXYM.IsValidIndex(Index))
	{
		PoserDepart(0.0f, 0.0f, INDEX_NONE);
		return;
	}

	const FWorldseedGeometry G = GeometrieCarte();
	const FVector2D XY = LieuxXYM[Index];

	const float Lat = G.LatitudeDegForV(XY.Y / G.HeightM + 0.5f);
	const float Lon = (XY.X / G.WidthM() + 0.5f) * 360.0f;

	PoserDepart(Lat, Lon, CelluleDe(Lat, Lon));

	// ON AMENE LE LIEU FACE A L'OBSERVATEUR, sinon on choisit un point qu'on
	// ne voit pas : le repere ne se dessine que sur l'hemisphere visible, et
	// l'ecran ne changerait donc pas du tout.
	//
	// Le centre du disque montre la longitude du decalage et la latitude
	// OPPOSEE a l'inclinaison -- c'est ce que rend PointerCadre en (0, 0).
	GlobeLongitudeDeg = FMath::Fmod(Lon + 360.0f, 360.0f);
	GlobeTiltDeg = FMath::Clamp(-Lat, -80.0f, 80.0f);
}
