// Worldseed - le compteur d'images a l'ecran, pour le debug.

#include "Procedural/WorldseedFpsOverlay.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

/**
 * UN NAMESPACE NOMME, ET CE N'EST PAS UN DETAIL DE STYLE.
 *
 * UBT concatene les .cpp en une seule unite de traduction, ou deux namespaces
 * ANONYMES n'en font qu'un. Le depot a deja casse deux fois sur ce piege --
 * WorldseedMetersToCm, puis SUB entre biomes et lithologie -- et la collision
 * ne dependait pas du code ecrit mais du REGROUPEMENT choisi par UBT. Or
 * NiveauDuJeu existe deja dans le namespace anonyme de WorldseedRetourMenu.cpp.
 */
namespace WorldseedFps
{
	/** Le niveau de jeu : ailleurs, le compteur ne s'arme pas. */
	const TCHAR* const NiveauDeJeu = TEXT("L_Worldseed_Proc");

	/**
	 * LA TOUCHE, ET POURQUOI PAS ECHAP. Echap ramene deja au menu
	 * (WorldseedRetourMenu). F1 est libre : ce projet n'a ni menu de pause ni
	 * inventaire, et les raccourcis F1-F4 du viewport n'appartiennent qu'au
	 * mode EDITION, pas au jeu.
	 */
	const FKey ToucheBascule = EKeys::F1;

	/**
	 * ON REPUBLIE QUATRE FOIS PAR SECONDE. Plus souvent, les chiffres dansent
	 * et deviennent illisibles ; moins souvent, on rate la reaction a ce que
	 * l'on vient de changer.
	 */
	constexpr double PeriodeS = 0.25;

	/** Duree pendant laquelle la pire trame reste affichee si rien ne la bat. */
	constexpr double PireMaintienS = 3.0;

	/** Le budget d'une trame, qui donne la couleur. 60 images par seconde. */
	constexpr float BudgetMs = 1000.0f / 60.0f;

	/**
	 * LA MARGE, CALEE SUR LA PILE DU MOTEUR. Celle-ci commence a
	 * MessageStartY = GIsEditor ? 45 : 100 (UnrealEngine.cpp:13615-13624).
	 * Une ligne de 13 points fait environ 18 pixels : posee a 6, elle se
	 * termine vers 24, donc bien au-dessus du releve meteo dans les deux cas.
	 */
	constexpr float MargeXPx = 10.0f;
	constexpr float MargeYPx = 6.0f;

	/** Au-dessus de toute UI de jeu : c'est un outil de debug. */
	constexpr int32 ZOrdre = 1000;

	UWorldseedFpsOverlay* Trouver(UWorld* Monde)
	{
		return Monde ? Monde->GetSubsystem<UWorldseedFpsOverlay>() : nullptr;
	}

	// UNE COMMANDE QUI NE REPOND RIEN NE SE DIAGNOSTIQUE PAS -- le depot l'a
	// deja note pour Worldseed.Ou et Worldseed.Lieux, qui se taisaient sans
	// terrain : impossible alors de distinguer « la commande n'existe pas » de
	// « elle n'a rien trouve a dire ».
	void CommandeFps(const TArray<FString>& Args, UWorld* Monde, FOutputDevice& Ar)
	{
		UWorldseedFpsOverlay* const Compteur = Trouver(Monde);
		if (!Compteur)
		{
			Ar.Logf(TEXT("Worldseed : pas de compteur dans ce monde."));
			return;
		}
		if (!Compteur->EstArme())
		{
			Ar.Logf(TEXT("Worldseed : le compteur ne s'arme que dans %s."), NiveauDeJeu);
			return;
		}

		// Sans argument on bascule ; avec, on impose l'etat. Les lancements
		// scriptes veulent imposer, une session de debug veut basculer.
		const bool bVoulu = (Args.Num() >= 1)
			? Args[0].ToBool()
			: !Compteur->EstMontre();

		Compteur->Montrer(bVoulu);
		Ar.Logf(TEXT("Worldseed : compteur d'images %s (touche %s)."),
			bVoulu ? TEXT("affiche") : TEXT("cache"),
			*ToucheBascule.GetDisplayName().ToString());
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GFps(
		TEXT("Worldseed.Fps"),
		TEXT("Affiche ou cache le compteur d'images. Sans argument, bascule."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&CommandeFps));
}

void UWorldseedFpsOverlay::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// ON NE S'ARME QUE DANS LE NIVEAU DE JEU. Le menu est une UI statique : un
	// compteur d'images n'y dit rien d'utile, et il se poserait par-dessus le
	// globe.
	bArme = InWorld.GetMapName().Contains(WorldseedFps::NiveauDeJeu);

	// --- L'ETAT DE DEPART S'IMPOSE EN LIGNE DE COMMANDE ---------------------
	//
	// POURQUOI CETTE SURCHARGE EXISTE. Le depot en a plusieurs du meme genre
	// -- WorldseedRayon, WorldseedNiveaux, WorldseedTransvoxel -- et la regle
	// est ecrite : quand un reglage doit etre pilote depuis l'exterieur, on
	// AJOUTE la surcharge plutot que de toucher a un fichier. Elle sert deux
	// fois ici : la TOURNEE PHOTO doit pouvoir eteindre le compteur, sans quoi
	// il se retrouve sur toutes les vues qui servent justement a juger le
	// rendu ; et elle rend la bascule eprouvable sans clavier.
	int32 Impose = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedFps="), Impose))
	{
		bMontre = (Impose != 0);
	}

	if (bArme)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] compteur d'images : %s (%s bascule, ou Worldseed.Fps)"),
			bMontre ? TEXT("affiche") : TEXT("cache"),
			*WorldseedFps::ToucheBascule.GetDisplayName().ToString());
	}

	// ON ARME ICI, ON NE CONSTRUIT PAS. Le sous-systeme recoit son
	// OnWorldBeginPlay AVANT les acteurs, et le viewport de jeu n'est pas
	// garanti pret. Le depot a deja paye ce piege sur la tournee photo, qui
	// rendait zero vue ET aucune ligne de journal parce que tout sortait sur
	// le premier test de validite.
}

void UWorldseedFpsOverlay::Deinitialize()
{
	// LE WIDGET SURVIT AU MONDE SI ON NE LE RETIRE PAS. Le viewport de jeu
	// appartient au moteur, pas au monde : un contenu ajoute et jamais retire
	// resterait a l'ecran apres un retour au menu, et s'empilerait a chaque
	// aller-retour.
	Detruire();
	bArme = false;

	Super::Deinitialize();
}

void UWorldseedFpsOverlay::Construire()
{
	if (Racine.IsValid() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	TSharedRef<SWidget> Construit =
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(FMargin(WorldseedFps::MargeXPx, WorldseedFps::MargeYPx, 0.0f, 0.0f))
		[
			SAssignNew(Texte, STextBlock)
			// UNE POLICE A CHASSE FIXE, PARCE QUE LES CHIFFRES CHANGENT. En
			// police proportionnelle, un 1 est plus etroit qu'un 8 : le label
			// tressaute a chaque republication et devient penible a lire.
			// Mono ne bouge pas.
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", 13))
			// UNE OMBRE PLUTOT QU'UN FOND OPAQUE. Un texte blanc est illisible
			// sur un desert en plein jour ; un fond masquerait le monde. Le
			// moteur fait exactement cela pour ses propres messages
			// (SmallTextItem.EnableShadow).
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f))
			.Text(FText::FromString(TEXT("-- FPS")))
		];

	// LE LABEL NE DOIT PAS MANGER LES CLICS. Le conteneur couvre tout l'ecran ;
	// sans cela il avalerait chaque clic du jeu.
	Construit->SetVisibility(EVisibility::HitTestInvisible);

	Racine = Construit;
	GEngine->GameViewport->AddViewportWidgetContent(Construit, WorldseedFps::ZOrdre);

	Montrer(bMontre);
}

void UWorldseedFpsOverlay::Detruire()
{
	if (Racine.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Racine.ToSharedRef());
	}
	Racine.Reset();
	Texte.Reset();
}

void UWorldseedFpsOverlay::Montrer(bool bNouvelEtat)
{
	const bool bChange = (bMontre != bNouvelEtat);
	bMontre = bNouvelEtat;

	if (Racine.IsValid())
	{
		Racine->SetVisibility(bMontre
			? EVisibility::HitTestInvisible
			: EVisibility::Collapsed);
	}

	// UNE BASCULE QUI NE LAISSE PAS DE TRACE NE SE DIAGNOSTIQUE PAS. Si la
	// touche ne repondait plus, cette ligne dirait aussitot de quel cote
	// chercher : absente du journal, c'est que l'entree n'arrive pas ; presente
	// mais sans effet a l'ecran, c'est le widget. Le depot a deja paye quatre
	// corrections inutiles, sur le routage des galeries, pour avoir cherche
	// sans avoir separe les deux causes.
	if (bChange)
	{
		UE_LOG(LogTemp, Log, TEXT("[Worldseed] compteur d'images : %s"),
			bMontre ? TEXT("affiche") : TEXT("cache"));
	}
}

void UWorldseedFpsOverlay::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bArme)
	{
		return;
	}

	Construire();   // idempotent : ne fait rien une fois le widget pose

	// --- LA TRAME REELLE, PAS CELLE DU MONDE --------------------------------
	//
	// Le DeltaTime d'un tickable est celui du MONDE, donc il suit la dilatation
	// temporelle : sous slomo 0.5 le compteur annoncerait la moitie des images
	// alors que le rendu n'a pas bouge d'un dixieme de milliseconde.
	// FApp::GetDeltaTime() est la duree de la trame APPLICATIVE.
	const double TrameS = FApp::GetDeltaTime();
	const float TrameMs = static_cast<float>(TrameS * 1000.0);

	FenetreS += TrameS;
	++FenetreTrames;

	PireDepuisS += TrameS;
	if (TrameMs > PireMs || PireDepuisS >= WorldseedFps::PireMaintienS)
	{
		PireMs = TrameMs;
		PireDepuisS = 0.0;
	}

	if (FenetreS >= WorldseedFps::PeriodeS && FenetreTrames > 0)
	{
		Publier(static_cast<float>(FenetreS * 1000.0 / FenetreTrames));
		FenetreS = 0.0;
		FenetreTrames = 0;
	}

	// --- LA BASCULE ---------------------------------------------------------
	UWorld* const W = GetWorld();
	APlayerController* const PC = W ? UGameplayStatics::GetPlayerController(W, 0) : nullptr;
	if (PC && PC->WasInputKeyJustPressed(WorldseedFps::ToucheBascule))
	{
		Basculer();
	}
}

void UWorldseedFpsOverlay::Publier(float MoyenneMs)
{
	if (!Texte.IsValid() || MoyenneMs <= 0.0f)
	{
		return;
	}

	const float Images = 1000.0f / MoyenneMs;

	Texte->SetText(FText::FromString(FString::Printf(
		TEXT("%3.0f FPS   %6.2f ms   pire %6.2f"), Images, MoyenneMs, PireMs)));

	// LA COULEUR SUIT LA MOYENNE, PAS LE PIC. Elle dit le REGIME -- tient-on le
	// budget ? -- tandis que la pire trame, elle, reste lisible en chiffres.
	// Une couleur pilotee par le pic clignoterait sans rien apprendre.
	const FLinearColor Teinte =
		(MoyenneMs <= WorldseedFps::BudgetMs)         ? FLinearColor(0.55f, 1.0f, 0.55f)
		: (MoyenneMs <= WorldseedFps::BudgetMs * 2.0f) ? FLinearColor(1.0f, 0.8f, 0.3f)
		                                               : FLinearColor(1.0f, 0.4f, 0.4f);
	Texte->SetColorAndOpacity(Teinte);
}

TStatId UWorldseedFpsOverlay::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWorldseedFpsOverlay, STATGROUP_Tickables);
}
