// Worldseed - la minimap : sous-systeme, widget Slate, textures.

#include "Procedural/WorldseedMinimap.h"

#include "Procedural/WorldseedCarte.h"
#include "Procedural/WorldseedVoxelTerrain.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RenderUtils.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

/**
 * UN NAMESPACE NOMME. UBT concatene les .cpp en une seule unite de traduction,
 * ou deux namespaces ANONYMES n'en font qu'un : ce depot a casse trois fois
 * sur ce piege -- WorldseedMetersToCm, SUB, CompterNonFinis -- et la collision
 * ne depend pas du code ecrit mais du REGROUPEMENT choisi par UBT. `WorldseedFps`
 * et le namespace anonyme de `WorldseedRetourMenu` occupent deja la place.
 */
namespace WorldseedMini
{
	/** Le niveau de jeu : ailleurs, la minimap ne s'arme pas. */
	const TCHAR* const NiveauDeJeu = TEXT("L_Worldseed_Proc");

	/**
	 * LA TOUCHE. Echap ramene au menu, F1 bascule le compteur d'images ; M est
	 * libre cote C++ et c'est la touche de carte partout ailleurs.
	 */
	const FKey ToucheBascule = EKeys::M;

	/** Cote de la texture, en pixels. */
	constexpr int32 Cote = 256;

	/** Cote a l'ecran, en pixels de mise en page Slate. */
	constexpr float CoteEcran = 224.0f;

	/** Marge au coin de l'ecran. */
	constexpr float MargePx = 14.0f;

	/**
	 * SOUS LE COMPTEUR D'IMAGES, QUI EST A 1000. Celui-la est un outil de
	 * debug et doit passer au-dessus de tout ; la minimap est de l'interface
	 * de JEU. Elle se pose donc entre l'UMG ordinaire, qui arrive a 0, et lui.
	 */
	constexpr int32 ZOrdre = 100;

	/** On sonde le fond quatre fois par seconde, comme le compteur. */
	constexpr double PeriodeFondS = 0.25;

	/**
	 * LE CONE NE SE REPEINT QU'AU-DELA D'UN DEMI-DEGRE, ce qui vaut environ un
	 * pixel d'arc au bord du disque. En deca, le repeint serait invisible.
	 */
	constexpr float SeuilCapDeg = 0.5f;

	/** Les trois crans, en metres de demi-portee. */
	const float Crans[3] = { 500.0f, 2000.0f, 6000.0f };

	/**
	 * DEMI-ANGLE DU CONE, BORNE. Le champ de vision reel peut depasser
	 * quatre-vingt-dix degres ; un coin pareil couvre un quart du disque et se
	 * lit comme un projecteur, pas comme une visee.
	 */
	constexpr float DemiAngleMinDeg = 15.0f;
	constexpr float DemiAngleMaxDeg = 60.0f;

	UWorldseedMinimap* Trouver(UWorld* Monde)
	{
		return Monde ? Monde->GetSubsystem<UWorldseedMinimap>() : nullptr;
	}

	// UNE COMMANDE QUI NE REPOND RIEN NE SE DIAGNOSTIQUE PAS : le depot l'a
	// paye sur Worldseed.Ou et Worldseed.Lieux, qui se taisaient sans terrain.
	void CommandeMinimap(const TArray<FString>& Args, UWorld* Monde, FOutputDevice& Ar)
	{
		UWorldseedMinimap* const Carte = Trouver(Monde);
		if (!Carte)
		{
			Ar.Logf(TEXT("Worldseed : pas de minimap dans ce monde."));
			return;
		}
		if (!Carte->EstArme())
		{
			Ar.Logf(TEXT("Worldseed : la minimap ne s'arme que dans %s."), NiveauDeJeu);
			return;
		}

		const bool bVoulu = (Args.Num() >= 1) ? Args[0].ToBool() : !Carte->EstMontre();
		Carte->Montrer(bVoulu);
		Ar.Logf(TEXT("Worldseed : minimap %s."),
			bVoulu ? TEXT("affichee") : TEXT("cachee"));
	}

	void CommandePortee(const TArray<FString>& Args, UWorld* Monde, FOutputDevice& Ar)
	{
		UWorldseedMinimap* const Carte = Trouver(Monde);
		if (!Carte || !Carte->EstArme())
		{
			Ar.Logf(TEXT("Worldseed : la minimap ne s'arme que dans %s."), NiveauDeJeu);
			return;
		}

		if (Args.Num() < 1)
		{
			Ar.Logf(TEXT("Usage : Worldseed.Minimap.Portee <demi-portee en metres>"));
			Ar.Logf(TEXT("Portee courante : %.0f m. Crans : 500, 2000, 6000."),
				Carte->PorteeM());
			return;
		}

		Carte->PoserPortee(FCString::Atof(*Args[0]));
		Ar.Logf(TEXT("Worldseed : minimap a %.0f m de demi-portee."), Carte->PorteeM());
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GMinimap(
		TEXT("Worldseed.Minimap"),
		TEXT("Affiche ou cache la minimap. Sans argument, bascule."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&CommandeMinimap));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GPortee(
		TEXT("Worldseed.Minimap.Portee"),
		TEXT("Demi-portee de la minimap, en metres."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&CommandePortee));
}


// ------------------------------------------------------------- cycle de vie

void UWorldseedMinimap::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	bArme = InWorld.GetMapName().Contains(WorldseedMini::NiveauDeJeu);

	int32 Impose = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedMinimap="), Impose))
	{
		bMontre = (Impose != 0);
	}

	float Portee = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedMinimapPortee="), Portee))
	{
		DemiPorteeM = FMath::Clamp(Portee, 50.0f, 40000.0f);
	}

	if (bArme)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] minimap : %s, %.0f m (%s bascule, Ctrl+%s change de cran)"),
			bMontre ? TEXT("affichee") : TEXT("cachee"), DemiPorteeM,
			*WorldseedMini::ToucheBascule.GetDisplayName().ToString(),
			*WorldseedMini::ToucheBascule.GetDisplayName().ToString());
	}

	// ON ARME ICI, ON NE CONSTRUIT PAS : `OnWorldBeginPlay` precede les
	// acteurs, et le viewport de jeu n'est pas garanti pret.
}

void UWorldseedMinimap::Deinitialize()
{
	// LE WIDGET SURVIT AU MONDE SI ON NE LE RETIRE PAS : le viewport appartient
	// au moteur, pas au monde, et un contenu jamais retire s'empilerait a
	// chaque aller-retour avec le menu.
	Detruire();
	bArme = false;

	Super::Deinitialize();
}

TStatId UWorldseedMinimap::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWorldseedMinimap, STATGROUP_Tickables);
}

AWorldseedVoxelTerrain* UWorldseedMinimap::Terrain() const
{
	UWorld* const W = GetWorld();
	if (!W) { return nullptr; }
	for (TActorIterator<AWorldseedVoxelTerrain> It(W); It; ++It)
	{
		return *It;
	}
	return nullptr;
}


// ----------------------------------------------------------------- textures

UTexture2D* UWorldseedMinimap::CreerTexture(int32 Cote) const
{
	UTexture2D* const Texture = UTexture2D::CreateTransient(Cote, Cote, PF_B8G8R8A8);
	if (!Texture)
	{
		return nullptr;
	}

	Texture->SRGB = true;
	Texture->CompressionSettings = TC_EditorIcon;
	Texture->Filter = TF_Bilinear;

	// SANS CECI LE STREAMING PEUT RELACHER LE MIP SOUS NOS PIEDS, et la
	// minimap deviendrait un carre flou sans que rien ne le signale.
	Texture->NeverStream = true;

	// Un premier remplissage a zero : le widget est construit AVANT que le
	// monde soit pret, et il doit alors montrer un disque vide plutot que des
	// octets non initialises.
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* const Donnees = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memzero(Donnees, static_cast<SIZE_T>(Cote) * Cote * 4);
	Mip.BulkData.Unlock();
	Texture->UpdateResource();

	return Texture;
}

void UWorldseedMinimap::Televerser(UTexture2D* Texture, uint8* PixelsPossedes,
	int32 Cote)
{
	if (!Texture || !PixelsPossedes)
	{
		delete[] PixelsPossedes;
		return;
	}

	// `UpdateTextureRegions` ET JAMAIS `UpdateResource`. Le second DETRUIT et
	// RECREE la ressource RHI : a trente images par seconde, cela ferait
	// trente textures GPU creees et detruites, et la file de destruction
	// differee se vide en micro-saccades. Le globe porte la meme note.
	FUpdateTextureRegion2D* const Region =
		new FUpdateTextureRegion2D(0, 0, 0, 0, Cote, Cote);

	Texture->UpdateTextureRegions(0, 1, Region, Cote * 4, 4, PixelsPossedes,
		[](uint8* Donnees, const FUpdateTextureRegion2D* Regions)
		{
			delete[] Donnees;
			delete Regions;
		});
}


// ------------------------------------------------------------- le widget

void UWorldseedMinimap::Construire()
{
	if (Racine.IsValid() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	TextureFond = CreerTexture(WorldseedMini::Cote);
	TextureCone = CreerTexture(WorldseedMini::Cote);
	if (!TextureFond || !TextureCone)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] minimap : creation de texture impossible."));
		return;
	}

	// LA BROSSE EST EN `Image` ET PORTE SA TAILLE, et les deux comptent. En
	// `Box`, Slate fait du 9-slice et ETIRE le fondu d'un pixel du disque en
	// bavure ; sans `ImageSize`, la taille desiree n'est pas celle qu'on croit
	// et les quatre lettres atterrissent a cote.
	auto PoserBrosse = [](FSlateBrush& B, UTexture2D* T)
	{
		B.SetResourceObject(T);
		B.DrawAs = ESlateBrushDrawType::Image;
		B.ImageSize = FVector2D(WorldseedMini::CoteEcran, WorldseedMini::CoteEcran);
		B.TintColor = FSlateColor(FLinearColor::White);
	};

	PoserBrosse(BrosseFond, TextureFond);
	PoserBrosse(BrosseCone, TextureCone);

	const FSlateFontInfo Police = FCoreStyle::GetDefaultFontStyle("Bold", 12);
	const FLinearColor Encre(0.96f, 0.96f, 0.98f, 1.0f);

	auto Cardinal = [&Police, &Encre](const TCHAR* Texte)
	{
		return SNew(STextBlock)
			.Text(FText::FromString(Texte))
			.Font(Police)
			.ColorAndOpacity(FSlateColor(Encre))
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
	};

	TSharedRef<SWidget> Construit =
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(FMargin(0.0f, WorldseedMini::MargePx, WorldseedMini::MargePx, 0.0f))
		[
			SNew(SBox)
			.WidthOverride(WorldseedMini::CoteEcran)
			.HeightOverride(WorldseedMini::CoteEcran)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[ SAssignNew(ImageFond, SImage).Image(&BrosseFond) ]
				+ SOverlay::Slot()[ SAssignNew(ImageCone, SImage).Image(&BrosseCone) ]

				// LES QUATRE LETTRES SONT FIXES : le nord est en haut et la
				// carte ne tourne jamais, donc elles n'ont aucune raison de
				// pivoter -- et du texte penche se lit mal.
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top)[ Cardinal(TEXT("N")) ]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom)[ Cardinal(TEXT("S")) ]
				+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center)[ Cardinal(TEXT("E")) ]
				+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center)[ Cardinal(TEXT("O")) ]
			]
		];

	// SANS CECI LE WIDGET MANGE TOUS LES CLICS de la moitie haute de l'ecran.
	Construit->SetVisibility(EVisibility::HitTestInvisible);

	Racine = Construit;
	GEngine->GameViewport->AddViewportWidgetContent(Construit, WorldseedMini::ZOrdre);
	Montrer(bMontre);
}

void UWorldseedMinimap::Detruire()
{
	if (Racine.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Racine.ToSharedRef());
	}

	Racine.Reset();
	ImageFond.Reset();
	ImageCone.Reset();

	// Les brosses lachent leur texture AVANT que la UPROPERTY ne la lache :
	// une brosse qui garde un objet detruit est un pointeur pendant.
	BrosseFond.SetResourceObject(nullptr);
	BrosseCone.SetResourceObject(nullptr);

	TextureFond = nullptr;
	TextureCone = nullptr;
	bPremierePeinture = false;
}

void UWorldseedMinimap::Montrer(bool bNouvelEtat)
{
	bMontre = bNouvelEtat;
	if (Racine.IsValid())
	{
		Racine->SetVisibility(bMontre
			? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	}
}

void UWorldseedMinimap::PoserPortee(float NouvelleM)
{
	DemiPorteeM = FMath::Clamp(NouvelleM, 50.0f, 40000.0f);
}

void UWorldseedMinimap::CranSuivant()
{
	// On prend le cran le PLUS PROCHE de la portee courante, puis le suivant :
	// ainsi une portee posee a la console ne fait pas sauter le cycle.
	int32 Proche = 0;
	for (int32 I = 1; I < 3; ++I)
	{
		if (FMath::Abs(WorldseedMini::Crans[I] - DemiPorteeM)
			< FMath::Abs(WorldseedMini::Crans[Proche] - DemiPorteeM))
		{
			Proche = I;
		}
	}

	PoserPortee(WorldseedMini::Crans[(Proche + 1) % 3]);

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] minimap : portee %.0f m."), DemiPorteeM);
}


// ------------------------------------------------------------------- le tick

void UWorldseedMinimap::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bArme)
	{
		return;
	}

	// CONSTRUIRE TOT, PEINDRE TARD. Si l'on n'assemblait le widget qu'une fois
	// le terrain pret, « terrain absent » et « widget non construit »
	// deviendraient indiscernables devant un ecran vide. On pose donc un disque
	// noir des que le viewport existe, et l'on dit au journal ce qui manque.
	Construire();   // idempotent

	if (bMontre)
	{
		Rafraichir();
	}

	// LE FRONT MONTANT, ET NON L'ETAT DE LA TOUCHE : `IsInputKeyDown` ferait
	// clignoter la bascule a chaque trame ou la touche reste enfoncee.
	UWorld* const W = GetWorld();
	if (APlayerController* const PC = W ? UGameplayStatics::GetPlayerController(W, 0) : nullptr)
	{
		if (PC->WasInputKeyJustPressed(WorldseedMini::ToucheBascule))
		{
			const bool bCtrl = PC->IsInputKeyDown(EKeys::LeftControl)
				|| PC->IsInputKeyDown(EKeys::RightControl);

			if (bCtrl)
			{
				CranSuivant();
			}
			else
			{
				Basculer();
			}
		}
	}
}

void UWorldseedMinimap::Rafraichir()
{
	if (!TextureFond || !TextureCone)
	{
		return;
	}

	const AWorldseedVoxelTerrain* const T = Terrain();
	if (!T || T->MondeAltitudes().Num() == 0)
	{
		return;   // le monde n'est pas encore la : le disque reste vide
	}

	const FWorldseedReperePlayer Repere = T->ReperePlayer();
	if (!Repere.bValide)
	{
		return;
	}

	const int32 Cote = WorldseedMini::Cote;
	const int32 Octets = Cote * Cote * 4;

	// --- le fond : seulement si l'on a change de cellule ou de portee -------
	Horloge += FApp::GetDeltaTime();
	if (Horloge >= WorldseedMini::PeriodeFondS)
	{
		Horloge = 0.0;

		if (Repere.Cellule != DerniereCellule
			|| !FMath::IsNearlyEqual(DemiPorteeM, DernierePorteeM))
		{
			WorldseedCarte::FParamsFenetre P =
				WorldseedCarte::FParamsFenetre::Carree(DemiPorteeM, Cote);
			P.CentreXm = Repere.Xm;
			P.CentreYm = Repere.Ym;

			// LE FOND DU MONDE EST UNE CONSTANTE DU MONDE, et il etait recalcule
			// A CHAQUE REPEINTURE : un balayage de huit millions et demi de
			// flottants tous les quinze metres parcourus, pour une valeur qui ne
			// peut pas changer tant que le monde est le meme. On le retient, et
			// on le redemande si le monde change de taille sous nous.
			if (FondDuMondeM >= 0.0f
				|| CellulesDuFond != T->MondeAltitudes().Num())
			{
				FondDuMondeM = WorldseedCarte::FondDuMonde(T->MondeAltitudes());
				CellulesDuFond = T->MondeAltitudes().Num();
			}
			P.FondM = FondDuMondeM;

			uint8* const Pixels = new uint8[Octets];
			WorldseedCarte::PeindreFenetre(T->MondeGeometrie(), T->MondeAltitudes(),
				T->MondePartage().IsValid() ? T->MondePartage()->Biomes
					: FWorldseedBiomeMap(), P, Pixels);

			Televerser(TextureFond, Pixels, Cote);

			DerniereCellule = Repere.Cellule;
			DernierePorteeM = DemiPorteeM;

			if (!bPremierePeinture)
			{
				bPremierePeinture = true;

				// UNE LIGNE, UNE SEULE FOIS, ET ELLE SERT AU DIAGNOSTIC. Si la
				// photo montre un carre gris, elle dit tout de suite si ce sont
				// les pixels ou la brosse -- et le FACTEUR DPI explique a lui
				// seul qu'une minimap paraisse molle : a facteur 2, une texture
				// de 256 est agrandie deux fois.
				const float Dpi = GEngine && GEngine->GameViewport
					? GEngine->GameViewport->GetDPIScale() : 1.0f;

				UE_LOG(LogTemp, Log,
					TEXT("[Worldseed] minimap peinte : texture %d px, ecran %.0f px, ")
					TEXT("DPI %.2f, %.1f m/px, centre (%.0f, %.0f) m, cellule %d"),
					Cote, WorldseedMini::CoteEcran, Dpi, P.MetresParPixelX(),
					Repere.Xm, Repere.Ym, Repere.Cellule);
			}
		}
	}

	// --- le cone : des que le cap bouge -------------------------------------
	if (FMath::Abs(FMath::FindDeltaAngleDegrees(DernierCap, Repere.CapDeg))
		>= WorldseedMini::SeuilCapDeg)
	{
		// LE CHAMP DE VISION SE RELIT A CHAQUE FOIS : il change des qu'on vise
		// ou qu'on zoome, et un demi-angle mis en cache a la construction
		// mentirait des le premier coup d'oeil dans une lunette.
		float DemiAngle = 35.0f;
		if (UWorld* const W = GetWorld())
		{
			if (const APlayerCameraManager* const Cam =
				UGameplayStatics::GetPlayerCameraManager(W, 0))
			{
				DemiAngle = FMath::Clamp(Cam->GetFOVAngle() * 0.5f,
					WorldseedMini::DemiAngleMinDeg, WorldseedMini::DemiAngleMaxDeg);
			}
		}

		// LE DEMI-ANGLE SE JOURNALISE UNE FOIS. Sans lui, un cone qui parait
		// trop etroit a trois causes possibles -- un champ de vision lu de
		// travers, une borne trop serree, ou un trace fautif -- et l'on regle
		// au hasard. Avec, la question est tranchee en une ligne.
		if (DernierCap < -900.0f)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] minimap cone : cap %.1f deg, demi-angle %.1f deg"),
				Repere.CapDeg, DemiAngle);
		}

		uint8* const Pixels = new uint8[Octets];
		WorldseedCarte::PeindreCone(Pixels, Cote, Repere.CapDeg, DemiAngle, 0.82f);
		Televerser(TextureCone, Pixels, Cote);

		DernierCap = Repere.CapDeg;
	}
}
