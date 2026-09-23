// Worldseed - la carte plein ecran : cycle de vie, cuisson, vue, marqueurs.

#include "Procedural/WorldseedCarteEcran.h"

#include "Procedural/WorldseedCarteWidget.h"
#include "Procedural/WorldseedCaves.h"
#include "Procedural/WorldseedMinimap.h"
#include "Procedural/WorldseedPlateau.h"
#include "Procedural/WorldseedVoxelTerrain.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RenderUtils.h"
#include "TextureResource.h"

/**
 * UN NAMESPACE NOMME. UBT concatene les .cpp en une seule unite de traduction,
 * ou deux namespaces ANONYMES n'en font qu'un : ce depot a casse quatre fois
 * sur ce piege, et trois de ces fois dans des fichiers que personne n'avait
 * touches.
 */
namespace WorldseedCarteUISub
{
	/** Le niveau de jeu : ailleurs, la carte ne s'arme pas. */
	const TCHAR* const NiveauDeJeu = TEXT("L_Worldseed_Proc");

	/** Tab : M et Ctrl+M vont a la minimap, Echap au menu, F1 au compteur. */
	const FKey ToucheBascule = EKeys::Tab;

	/**
	 * AU-DESSUS DE LA MINIMAP (100), SOUS LE COMPTEUR D'IMAGES (1000). Le
	 * compteur est un outil de mesure et doit rester lisible par-dessus tout ;
	 * la carte, elle, recouvre l'interface de jeu.
	 */
	constexpr int32 ZOrdre = 200;

	/** Nombre de gouffres et de dolines au-dela duquel la carte se tait. */
	constexpr double EchellePuitsM = 12.0;
	constexpr double EchelleTablesM = 60.0;

	UWorldseedCarteEcran* Trouver(UWorld* Monde)
	{
		return Monde ? Monde->GetSubsystem<UWorldseedCarteEcran>() : nullptr;
	}

	// UNE COMMANDE QUI NE REPOND RIEN NE SE DIAGNOSTIQUE PAS : le depot l'a
	// paye sur Worldseed.Ou et Worldseed.Lieux, qui se taisaient sans terrain.
	void CommandeCarte(const TArray<FString>& Args, UWorld* Monde, FOutputDevice& Ar)
	{
		UWorldseedCarteEcran* const C = Trouver(Monde);
		if (!C)
		{
			Ar.Logf(TEXT("Worldseed : pas de carte dans ce monde."));
			return;
		}
		if (!C->EstArme())
		{
			Ar.Logf(TEXT("Worldseed : la carte ne s'arme que dans %s."), NiveauDeJeu);
			return;
		}

		const bool bVoulu = (Args.Num() >= 1) ? Args[0].ToBool() : !C->EstOuverte();
		bVoulu ? C->Ouvrir() : C->Fermer();
		Ar.Logf(TEXT("Worldseed : carte %s."),
			bVoulu ? TEXT("ouverte") : TEXT("fermee"));
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GCarte(
		TEXT("Worldseed.Carte"),
		TEXT("Ouvre ou ferme la carte plein ecran. Sans argument, bascule."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&CommandeCarte));
}


// ------------------------------------------------------------- cycle de vie

void UWorldseedCarteEcran::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	bArme = InWorld.GetMapName().Contains(WorldseedCarteUISub::NiveauDeJeu);

	int32 Res = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedCarteRes="), Res))
	{
		PlafondCuisson = FMath::Clamp(Res, 256, 8192);
	}

	// UN WIDGET NE SE PHOTOGRAPHIE PAS PAR LA TOURNEE : `RequestScreenshot` lit
	// la cible de rendu du viewport, alors que Slate est composite apres, dans
	// le back-buffer. On capture donc la FENETRE depuis l'exterieur -- et il
	// faut alors pouvoir ouvrir la carte sans clavier.
	FParse::Value(FCommandLine::Get(), TEXT("WorldseedCarteAuto="), DelaiAutoS);

	// Un zoom impose, pour juger une echelle precise sans piloter la molette.
	FParse::Value(FCommandLine::Get(), TEXT("WorldseedCarteEchelle="), EchelleAutoM);

	// UN REPERE POSE SANS CLIQUER, et ce n'est pas une commodite : piloter la
	// souris d'une machine ou quelqu'un travaille ne marche pas -- la fenetre
	// ne prend pas le focus, les evenements partent ailleurs, et l'on croit a
	// une regression. Deux cles SEPAREES, parce que `FParse::Value` s'arrete
	// sur une virgule : « 12,34 » lui arrive comme « 12 ».
	float RX = 0.0f, RY = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedRepereX="), RX)
		&& FParse::Value(FCommandLine::Get(), TEXT("WorldseedRepereY="), RY))
	{
		Repere = FVector2D(RX, RY);
		bRepere = true;
	}

	if (bArme)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] carte : %s ouvre et ferme, clic pose un repere, ")
			TEXT("Ctrl+clic teleporte (cuisson plafonnee a %d px)"),
			*WorldseedCarteUISub::ToucheBascule.GetDisplayName().ToString(),
			PlafondCuisson);
	}

	// ON ARME ICI, ON NE CUIT PAS : `OnWorldBeginPlay` precede les acteurs, et
	// le monde n'est pas charge. La cuisson attend la premiere ouverture.
}


void UWorldseedCarteEcran::Deinitialize()
{
	// LE WIDGET SURVIT AU MONDE SI ON NE LE RETIRE PAS : le viewport appartient
	// au moteur. Et l'entree doit etre rendue -- sinon on revient au menu avec
	// un curseur visible et un pion gele.
	if (bOuverte)
	{
		Fermer();
	}

	bArme = false;
	Super::Deinitialize();
}


TStatId UWorldseedCarteEcran::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWorldseedCarteEcran, STATGROUP_Tickables);
}


AWorldseedVoxelTerrain* UWorldseedCarteEcran::Terrain() const
{
	UWorld* const W = GetWorld();
	if (!W) { return nullptr; }
	for (TActorIterator<AWorldseedVoxelTerrain> It(W); It; ++It)
	{
		return *It;
	}
	return nullptr;
}


double UWorldseedCarteEcran::LargeurMondeM() const
{
	const AWorldseedVoxelTerrain* const T = Terrain();
	return T ? static_cast<double>(T->MondeGeometrie().WidthM()) : 0.0;
}


void UWorldseedCarteEcran::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bArme || bOuverte)
	{
		// FERMEE, C'EST LE SOUS-SYSTEME QUI ECOUTE ; OUVERTE, C'EST LE WIDGET.
		// Deux chemins, et il en faut deux : `FInputModeGameAndUI` laisse les
		// touches arriver au controleur, mais le widget a le focus et les
		// recoit en premier -- les sonder des deux cotes ferait basculer la
		// carte deux fois sur une seule pression.
		return;
	}

	// L'ouverture automatique du harnais : elle attend que le monde soit la,
	// puis n'agit qu'une fois.
	if (DelaiAutoS > 0.0f)
	{
		HorlogeAuto += DeltaTime;
		if (HorlogeAuto >= DelaiAutoS)
		{
			// ON RESSAIE TANT QUE LE MONDE N'EST PAS LA. Premiere version : le
			// delai etait consomme meme quand `Ouvrir` echouait faute de monde
			// charge -- un delai une seconde trop court, et la carte ne
			// s'ouvrait jamais, sans qu'on sache si c'etait le harnais ou elle.
			Ouvrir();
			if (!bOuverte)
			{
				return;
			}

			DelaiAutoS = 0.0f;

			if (EchelleAutoM > 0.0f)
			{
				MetresParPixel = EchelleAutoM;
				UE_LOG(LogTemp, Log,
					TEXT("[Worldseed] carte : echelle imposee a %.1f m/px"),
					MetresParPixel);
			}
			return;
		}
	}

	UWorld* const W = GetWorld();
	if (APlayerController* const PC = W ? UGameplayStatics::GetPlayerController(W, 0) : nullptr)
	{
		if (PC->WasInputKeyJustPressed(WorldseedCarteUISub::ToucheBascule))
		{
			Ouvrir();
		}
	}
}


// ----------------------------------------------------------------- la vue

WorldseedCarte::FParamsFenetre UWorldseedCarteEcran::ParamsVue(
	const FVector2D& TailleEcranPx) const
{
	const int32 RX = FMath::Max(FMath::RoundToInt(TailleEcranPx.X), 1);
	const int32 RY = FMath::Max(FMath::RoundToInt(TailleEcranPx.Y), 1);

	WorldseedCarte::FParamsFenetre P = WorldseedCarte::FParamsFenetre::Rectangle(
		MetresParPixel * RX * 0.5, RX, RY);
	P.CentreXm = CentreVueM.X;
	P.CentreYm = CentreVueM.Y;
	return P;
}


void UWorldseedCarteEcran::BornerVue(const FVector2D& TailleEcranPx, float EchelleDPI)
{
	const AWorldseedVoxelTerrain* const T = Terrain();
	if (!T || TailleEcranPx.Y < 1.0)
	{
		return;
	}

	const FWorldseedGeometry& Geo = T->MondeGeometrie();
	const double Hauteur = static_cast<double>(Geo.HeightM);
	const double Cellule = static_cast<double>(Geo.WidthM()) / FMath::Max(Geo.NX, 1);

	// LE PLAFOND DE ZOOM EST PHYSIQUE, PAS UN GOUT : la donnee s'arrete a la
	// cellule. Au-dela d'un texel par pixel REEL -- d'ou le facteur DPI -- on
	// ne grossirait plus que des texels, sans rien montrer de plus.
	const double PlusFin = Cellule / FMath::Max(EchelleDPI, 0.01f);

	// LE DEZOOM MAXIMAL MONTRE LE MONDE ENTIER, et il se cale sur le cote le
	// plus CONTRAIGNANT -- donc un maximum, pas la hauteur.
	//
	// Cale sur la seule hauteur, comme il l'etait, on ne voyait jamais que
	// 56 862 m de large sur 64 000 : onze pour cent du monde restaient hors
	// champ, quoi qu'on fasse, parce que le monde est en 2:1 et l'ecran en
	// 16:9. Signale a l'image : « la projection est coupee ».
	const double Largeur0 = static_cast<double>(Geo.WidthM());
	const double PlusLarge = FMath::Max(Hauteur / TailleEcranPx.Y,
		Largeur0 / TailleEcranPx.X);

	MetresParPixel = FMath::Clamp(MetresParPixel, PlusFin, FMath::Max(PlusLarge, PlusFin));

	// LA VUE NE DOIT PAS DEBORDER DU MONDE, SUR AUCUN DES DEUX AXES. Au-dela
	// des bords de la texture, l'adressage est en `Clamp` : la derniere ligne
	// et la derniere colonne s'etirent en longues bavures -- vu a l'image des
	// la premiere capture en jeu, parce que la vue s'ouvrait centree sur un
	// joueur proche du pole et debordait de huit kilometres.
	//
	// EN LONGITUDE ON BORNE AUSSI, alors que le monde s'enroule : le tiling
	// d'une brosse Slate et une region UV personnalisee s'excluent -- des que
	// `ESlateBrushTileType` pose `TileU`, le batcher recalcule les UV depuis la
	// taille locale et notre region est ignoree. L'enroulement demandera deux
	// images cote a cote ; en attendant, on s'arrete au bord.
	const double Largeur = static_cast<double>(Geo.WidthM());
	const double DemiVueX = MetresParPixel * TailleEcranPx.X * 0.5;
	const double DemiVueY = MetresParPixel * TailleEcranPx.Y * 0.5;

	const double MargeX = FMath::Max(Largeur * 0.5 - DemiVueX, 0.0);
	const double MargeY = FMath::Max(Hauteur * 0.5 - DemiVueY, 0.0);

	CentreVueM.X = FMath::Clamp(CentreVueM.X, -MargeX, MargeX);
	CentreVueM.Y = FMath::Clamp(CentreVueM.Y, -MargeY, MargeY);
}


void UWorldseedCarteEcran::PoserRepere(const FVector2D& PositionM)
{
	Repere = PositionM;
	bRepere = true;

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] carte : repere pose a (%.0f, %.0f) m"),
		PositionM.X, PositionM.Y);
}


// -------------------------------------------------------------- la cuisson

bool UWorldseedCarteEcran::Cuire()
{
	if (Texture)
	{
		return true;   // idempotent : le monde ne change pas en cours de partie
	}

	const AWorldseedVoxelTerrain* const T = Terrain();
	if (!T || T->MondeAltitudes().Num() == 0)
	{
		return false;
	}

	const FWorldseedGeometry& Geo = T->MondeGeometrie();

	// LA TAILLE SE DERIVE DE LA GRILLE, elle ne se fige pas. Un pixel par
	// cellule : rien n'est agrege, rien n'est vote, aucune ile ne se perd. Le
	// plafond n'est la que pour une grille demesuree -- au-dela, l'agregation
	// prend le relais et la carte reste juste, seulement moins fine.
	const int32 TexX = FMath::Min(Geo.NX, PlafondCuisson);
	const int32 TexY = FMath::Max(TexX / 2, 1);

	WorldseedCarte::FParamsFenetre P = WorldseedCarte::FParamsFenetre::Rectangle(
		static_cast<double>(Geo.WidthM()) * 0.5, TexX, TexY);
	P.bDisque = false;
	P.FondM = WorldseedCarte::FondDuMonde(T->MondeAltitudes());

	TArray<uint8> Base;
	Base.SetNumUninitialized(static_cast<SIZE_T>(TexX) * TexY * 4);

	const double T0 = FPlatformTime::Seconds();
	WorldseedCarte::PeindreFenetre(Geo, T->MondeAltitudes(),
		T->MondePartage().IsValid() ? T->MondePartage()->Biomes : FWorldseedBiomeMap(),
		P, Base.GetData());

	// SANS PYRAMIDE, LE LISERE DE COTE SORT POINTILLE des que la carte est
	// reduite -- mesure a l'image sur la planche de `ProbeCarteEcran`. Le GPU
	// preleve alors un texel sur deux.
	TArray<TArray<uint8>> Niveaux;
	WorldseedCarte::CuireReductions(Base.GetData(), TexX, TexY, Niveaux);
	const double MsPeinture = (FPlatformTime::Seconds() - T0) * 1000.0;

	// ON MONTE LA TEXTURE A LA MAIN, parce que `CreateTransient` ne cree QU'UN
	// mip. Tout est ecrit une seule fois, a la creation : pas de
	// `UpdateTextureRegions`, donc pas de tampon a faire vivre au-dela de
	// l'appel -- le piege du fil de rendu ne se pose pas ici.
	Texture = NewObject<UTexture2D>(this, NAME_None, RF_Transient);
	if (!Texture)
	{
		return false;
	}

	FTexturePlatformData* const Donnees = new FTexturePlatformData();
	Donnees->SizeX = TexX;
	Donnees->SizeY = TexY;
	Donnees->PixelFormat = PF_B8G8R8A8;
	Texture->SetPlatformData(Donnees);

	auto AjouterMip = [Donnees](const uint8* Pixels, int32 X, int32 Y)
	{
		FTexture2DMipMap* const Mip = new FTexture2DMipMap(X, Y);
		Donnees->Mips.Add(Mip);
		Mip->BulkData.Lock(LOCK_READ_WRITE);
		void* const Dst = Mip->BulkData.Realloc(static_cast<int64>(X) * Y * 4);
		FMemory::Memcpy(Dst, Pixels, static_cast<SIZE_T>(X) * Y * 4);
		Mip->BulkData.Unlock();
	};

	AjouterMip(Base.GetData(), TexX, TexY);
	int32 MX = TexX, MY = TexY;
	for (const TArray<uint8>& N : Niveaux)
	{
		MX = FMath::Max(MX >> 1, 1);
		MY = FMath::Max(MY >> 1, 1);
		AjouterMip(N.GetData(), MX, MY);
	}

	Texture->SRGB = true;
	Texture->CompressionSettings = TC_EditorIcon;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;

	// SANS CECI LE STREAMING PEUT RELACHER LES MIPS SOUS NOS PIEDS, et la carte
	// deviendrait floue sans que rien ne le signale.
	Texture->NeverStream = true;
	Texture->UpdateResource();

	Brosse.SetResourceObject(Texture);
	Brosse.DrawAs = ESlateBrushDrawType::Image;

	Cuisson = FIntPoint(TexX, TexY);

	const double Mo = static_cast<double>(TexX) * TexY * 4.0 / (1024.0 * 1024.0);
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] carte cuite : %d x %d en %.0f ms (%.1f Mo, %d niveaux, %.1f m/px)"),
		TexX, TexY, MsPeinture, Mo, Donnees->Mips.Num(), P.MetresParPixelX());

	// --- les lieux ----------------------------------------------------------
	//
	// BATIS UNE FOIS. Ils sont deja en memoire -- le reseau de cavites les
	// conserve depuis l'arbitrage B8, precisement pour qu'on puisse les
	// retrouver au runtime.
	Lieux.Reset();

	const FWorldseedCaveNetwork& Grottes = T->MondeGrottes();
	for (const FWorldseedCaveArch& A : Grottes.Arches)
	{
		Lieux.Add({ FVector2D(A.CentreM.X, A.CentreM.Y),
			FWorldseedMarqueur::EGenre::Arche, 1e9 });
	}

	// LES PUITS NE SE MONTRENT QUE DE PRES : le monde en porte plus de cinq
	// cents. A l'echelle du monde entier, ils ne font pas une carte, ils font
	// un voile gris.
	for (const FWorldseedCavePuits& Pu : Grottes.Puits)
	{
		Lieux.Add({ FVector2D(Pu.CentreM.X, Pu.CentreM.Y),
			Pu.bDoline ? FWorldseedMarqueur::EGenre::Doline
				: FWorldseedMarqueur::EGenre::Gouffre,
			WorldseedCarteUISub::EchellePuitsM });
	}

	for (const FWorldseedPlateauSite& S : T->MondeTables())
	{
		Lieux.Add({ S.CentreM, FWorldseedMarqueur::EGenre::Table,
			WorldseedCarteUISub::EchelleTablesM });
	}
	for (const FWorldseedPlateauSite& S : T->MondeCanyons())
	{
		Lieux.Add({ S.CentreM, FWorldseedMarqueur::EGenre::Canyon,
			WorldseedCarteUISub::EchelleTablesM });
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] carte : %d lieux (%d arches, %d puits, %d tables, %d canyons)"),
		Lieux.Num(), Grottes.Arches.Num(), Grottes.Puits.Num(),
		T->MondeTables().Num(), T->MondeCanyons().Num());

	return true;
}


// ---------------------------------------------------------- ouvrir, fermer

void UWorldseedCarteEcran::Ouvrir()
{
	if (bOuverte || !bArme || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	if (!Cuire())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] carte : le monde n'est pas encore charge."));
		return;
	}

	// LA PREMIERE VUE MONTRE LE MONDE ENTIER, centre sur le joueur : on ouvre
	// une carte pour se situer, pas pour chercher ou l'on est.
	//
	// EN PIXELS LOGIQUES, ET C'EST CE QUE SLATE EMPLOIE. Le viewport rend des
	// pixels REELS ; melanger les deux donnerait une echelle fausse d'un
	// facteur DPI, donc une carte deux fois trop grande sur un ecran dense.
	const float Dpi = FMath::Max(GEngine->GameViewport->GetDPIScale(), 0.01f);
	const FVector2D Physiques = GEngine->GameViewport->Viewport
		? FVector2D(GEngine->GameViewport->Viewport->GetSizeXY())
		: FVector2D(1920.0, 1080.0);
	const FVector2D Taille = Physiques / Dpi;

	if (const AWorldseedVoxelTerrain* const T = Terrain())
	{
		const FWorldseedReperePlayer R = T->ReperePlayer();
		if (R.bValide)
		{
			CentreVueM = FVector2D(R.Xm, R.Ym);
		}
		// LE MONDE ENTIER, donc le cote le plus contraignant des deux -- voir
		// `BornerVue`. Sur la hauteur seule, onze pour cent de la largeur
		// restaient hors champ.
		const FWorldseedGeometry& G = T->MondeGeometrie();
		MetresParPixel = FMath::Max(
			static_cast<double>(G.HeightM) / FMath::Max(Taille.Y, 1.0),
			static_cast<double>(G.WidthM()) / FMath::Max(Taille.X, 1.0));
	}

	// ON BORNE DES L'OUVERTURE, et pas seulement au glisser : sans cela une
	// vue centree sur un joueur proche d'un pole s'ouvre en debordant du monde,
	// et les bords de la texture s'etirent en longues bavures -- vu a l'image
	// des la premiere capture en jeu.
	BornerVue(Taille, Dpi);

	TSharedRef<SWorldseedCarte> Construit = SNew(SWorldseedCarte).Carte(this);
	Widget = Construit;
	Racine = Construit;

	GEngine->GameViewport->AddViewportWidgetContent(Construit,
		WorldseedCarteUISub::ZOrdre);

	UWorld* const W = GetWorld();
	if (APlayerController* const PC = W ? UGameplayStatics::GetPlayerController(W, 0) : nullptr)
	{
		// `GameAndUI` ET NON `UIOnly`, et ce n'est pas un detail : `UIOnly`
		// coupe `WasInputKeyJustPressed`, dont dependent la minimap ET le
		// retour au menu. On ferme donc la camera et le deplacement a la main.
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetWidgetToFocus(Construit);
		PC->SetInputMode(Mode);
		PC->bShowMouseCursor = true;

		// CE SONT DES COMPTEURS, PAS DES DRAPEAUX : un `true` de trop laisse le
		// pion gele apres la fermeture, et le defaut survit a la carte.
		PC->SetIgnoreLookInput(true);
		PC->SetIgnoreMoveInput(true);
	}

	// La minimap ferait double emploi, et se poserait PAR-DESSUS la carte.
	if (UWorldseedMinimap* const Mini = W ? W->GetSubsystem<UWorldseedMinimap>() : nullptr)
	{
		bMinimapAvant = Mini->EstMontre();
		Mini->Montrer(false);
	}

	bOuverte = true;
	FSlateApplication::Get().SetKeyboardFocus(Construit);
}


void UWorldseedCarteEcran::Fermer()
{
	if (!bOuverte)
	{
		return;
	}

	if (Racine.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Racine.ToSharedRef());
	}
	Racine.Reset();
	Widget.Reset();

	RestaurerEntree();

	UWorld* const W = GetWorld();
	if (UWorldseedMinimap* const Mini = W ? W->GetSubsystem<UWorldseedMinimap>() : nullptr)
	{
		// L'ETAT D'AVANT, et non `true` : le joueur l'avait peut-etre coupee.
		Mini->Montrer(bMinimapAvant);
	}

	bOuverte = false;
}


void UWorldseedCarteEcran::RestaurerEntree()
{
	UWorld* const W = GetWorld();
	APlayerController* const PC = W ? UGameplayStatics::GetPlayerController(W, 0) : nullptr;
	if (!PC)
	{
		return;
	}

	// IL N'EXISTE PAS DE `GetInputMode` : on ne peut pas remettre ce qu'on a
	// trouve, seulement poser ce que le jeu attend. C'est exactement l'etat que
	// le menu pose en lancant la partie -- si le jeu gagne un jour une seconde
	// interface, c'est ici que cela cassera.
	PC->SetInputMode(FInputModeGameOnly());
	PC->bShowMouseCursor = false;

	// Les compteurs, appaires un pour un avec l'ouverture.
	PC->SetIgnoreLookInput(false);
	PC->SetIgnoreMoveInput(false);
}
