// Worldseed - la tournee photo : voir le monde sans outillage externe.

#include "Procedural/WorldseedPhotographe.h"

#include "Procedural/WorldseedPlateau.h"

#include "Procedural/WorldseedVoxelTerrain.h"
#include "Procedural/WorldseedUdsBridge.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/** Laisser le monde se batir avant de tirer, puis souffler apres. */
	constexpr float AvantPhotoS = 6.0f;
	constexpr float ApresPhotoS = 7.2f;
}

bool UWorldseedPhotographe::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// PIE et jeu seulement : rien a photographier dans un monde d'editeur, et
	// surtout rien a piloter -- il n'y a pas de pion.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UWorldseedPhotographe::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWorldseedPhotographe, STATGROUP_Tickables);
}

AWorldseedVoxelTerrain* UWorldseedPhotographe::Terrain() const
{
	UWorld* const W = GetWorld();
	if (!W) { return nullptr; }
	for (TActorIterator<AWorldseedVoxelTerrain> It(W); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void UWorldseedPhotographe::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// --- LA TOURNEE SE DECLENCHE EN LIGNE DE COMMANDE ------------------------
	//
	// PAS PAR UN APPEL EXTERNE, ET C'EST TOUT L'INTERET. Piloter la prise de
	// vue depuis l'exterieur suppose un lien d'outillage vivant ; il tombe des
	// que l'editeur est tue et relance plusieurs fois, donc a chaque
	// compilation, et l'on redevient alors aveugle. Lue au demarrage, l'option
	// marche dans tous les cas, y compris en build final.
	//
	//     -game -WorldseedPhotos -WorldseedQuitter
	if (!FParse::Param(FCommandLine::Get(), TEXT("WorldseedPhotos")))
	{
		return;
	}

	bQuitterEnsuite = FParse::Param(FCommandLine::Get(), TEXT("WorldseedQuitter"));
	bArme = true;

	// ON ARME ICI, ON NE CONSTRUIT PAS. Le sous-systeme recoit son
	// OnWorldBeginPlay AVANT que les acteurs recoivent le leur : le terrain
	// n'existe pas encore, et surtout son monde n'est pas charge -- ce qui
	// prend une minute a la premiere generation. Construire la tournee ici
	// donnait zero vue ET AUCUN JOURNAL, parce que tout sortait sur le premier
	// test de validite. On attend donc le premier tick ou le monde est la.
}

void UWorldseedPhotographe::MidiFige()
{
	// UNE TOURNEE LONGUE TOMBE DANS LA NUIT, et alors elle ne prouve plus rien.
	// L horloge d UDS avance d une unite par 1,125 s reelle : vingt-sept arrets
	// a trente-six secondes font seize minutes, soit HUIT HEURES de jeu. La
	// moitie des vues sortait donc en bleu nuit -- et le depot a deja une regle
	// pour cela, « juger la couleur EN PLEIN JOUR », qu il avait fallu apprendre
	// sur un mur d arbustes qui rendait noir sous la pluie.
	//
	// On REECRIT l heure avant chaque prise plutot que d arreter l animation :
	// une ecriture rate proprement si le pack n est pas la, alors qu arreter
	// l horloge laisserait le monde fige pour la suite de la partie.
	if (!bUdsResolu)
	{
		bUdsResolu = true;
		Uds.Resolve(GetWorld());
		UE_LOG(LogTemp, Log, TEXT("[Worldseed] photo : ciel -- %s"),
			Uds.IsValid() ? *Uds.Describe() : TEXT("aucun UDS, heure non figee"));
	}
	if (Uds.IsValid())
	{
		Uds.WriteNumber(TEXT("Time of Day"), 1300.0);
	}
}

void UWorldseedPhotographe::Photographier(double XMetres, double YMetres,
	const FString& Nom)
{
	AWorldseedVoxelTerrain* const T = Terrain();
	if (!T) { return; }

	FWorldseedPhotoStop E;
	E.Nom = Nom.IsEmpty() ? TEXT("vue") : Nom;
	E.CibleM = FVector(XMetres, YMetres,
		T->MondeChamp().SurfaceHeightM(XMetres, YMetres));
	Tournee.Add(E);
	Demarrer();
}

int32 UWorldseedPhotographe::AjouterLesArches()
{
	AWorldseedVoxelTerrain* const T = Terrain();
	if (!T) { return 0; }

	const FWorldseedCaveNetwork& Reseau = T->MondeGrottes();
	int32 Ajoutees = 0;

	for (int32 I = 0; I < Reseau.Arches.Num(); ++I)
	{
		const FWorldseedCaveArch& A = Reseau.Arches[I];

		FWorldseedPhotoStop E;
		E.Nom = FString::Printf(TEXT("arche%02d"), I + 1);
		E.CibleM = A.CentreM;

		// ON REGARDE DANS L'AXE DU PERCEMENT, sans quoi on photographie une
		// paroi pleine et l'on conclut a tort que l'arche n'existe pas.
		E.DepuisM = A.TraversM.GetSafeNormal();
		E.DistanceM = FMath::Clamp(A.EpaisseurM * 1.6f, 80.0f, 200.0f);
		Tournee.Add(E);
		++Ajoutees;
	}

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] photo : %d arches a la tournee"), Ajoutees);
	return Ajoutees;
}

int32 UWorldseedPhotographe::AjouterLesTables(int32 Combien)
{
	AWorldseedVoxelTerrain* const T = Terrain();
	if (!T) { return 0; }

	const TArray<FWorldseedPlateauSite>& Sites = T->MondeTables();
	int32 Ajoutees = 0;

	for (int32 I = 0; I < Sites.Num() && Ajoutees < Combien; ++I)
	{
		const FWorldseedPlateauSite& S = Sites[I];

		FWorldseedPhotoStop E;
		E.Nom = FString::Printf(TEXT("table%02d"), I + 1);
		E.CibleM = FVector(S.CentreM.X, S.CentreM.Y, S.AltitudeM);

		// LA DISTANCE EST BORNEE PAR LE TERRAIN, PAS PAR LE CADRAGE, et c'est
		// une contrainte dure. Le voxel n'existe que dans le rayon de
		// chargement -- 250 m -- et au-dela on photographie le SOL DE FOND, a
		// 63 m par maille : des formes lisses et arrondies ou l'on croit voir
		// un relief mou alors qu'on ne voit pas le relief du tout. Le depot a
		// deja perdu une heure sur ce piege avec des vues a 420-580 m.
		//
		// CONSEQUENCE ASSUMEE : on ne photographie PAS la silhouette entiere
		// d'une table de plus d'un kilometre. On cadre sa PAROI et son rebord
		// contre le ciel, ce qui reste le controle diagnostique -- un mur
		// vertical surmonte d'un trait horizontal.
		// LA FENETRE DE PRISE DE VUE EST ETROITE, ET ELLE SE CALCULE.
		//
		// Les chunks se batissent autour du JOUEUR, dans un rayon de 250 m. Ce
		// qu'on photographie doit donc etre a moins de 250 m de LUI, sans quoi
		// l'on cadre le sol de fond a 63 m par maille -- des formes lisses et
		// arrondies ou l'on croit voir un relief mou alors qu'on ne voit pas le
		// relief du tout.
		//
		// Soit D la distance au centre de la butte, dont le rayon vaut environ
		// `tables.porteeM`. Il faut D > rayon pour etre DESCENDU de la butte, et
		// D - rayon < 250 pour que le rebord soit en voxel. A 250 m de rayon, la
		// fenetre utile va donc de 250 a 500 m, et 350 la place au milieu.
		//
		// LA PREMIERE VERSION CADRAIT A 200 m ET N'A RIEN MONTRE : sur une table
		// de 1,8 km, la camera etait encore DESSUS, et les cinq vues ont rendu
		// une plaine. C'est ce qui a fait ramener les tables a l'echelle d'une
		// BUTTE -- la forme doit tenir dans la distance de vue, sinon elle
		// existe dans la donnee et pas pour le joueur.
		E.DepuisM = FVector2D(0.82, 0.57);
		E.DistanceM = 350.0f;

		// Au PIED de la paroi, le regard vers le haut : c'est la seule position
		// d'ou une butte se lit -- un mur vertical surmonte d'un trait
		// horizontal, contre le ciel.
		E.HauteurM = -FMath::Max(S.EscarpementM - 40.0f, 30.0f);
		Tournee.Add(E);
		++Ajoutees;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] photo : %d tables a la tournee (sur %d sites)"),
		Ajoutees, Sites.Num());
	return Ajoutees;
}

int32 UWorldseedPhotographe::AjouterLesCanyons(int32 Combien)
{
	AWorldseedVoxelTerrain* const T = Terrain();
	if (!T) { return 0; }

	const TArray<FWorldseedPlateauSite>& Sites = T->MondeCanyons();
	int32 Ajoutees = 0;

	for (int32 I = 0; I < Sites.Num() && Ajoutees < Combien; ++I)
	{
		const FWorldseedPlateauSite& S = Sites[I];

		FWorldseedPhotoStop E;
		E.Nom = FString::Printf(TEXT("canyon%02d"), I + 1);
		E.CibleM = FVector(S.CentreM.X, S.CentreM.Y, S.AltitudeM);

		// DE PRES, ET AU RAS DU PLANCHER. Un canyon ne se photographie pas de
		// loin : a 350 m on est deja sur le plateau, donc on voit une rayure.
		// Au fond, les parois sortent du cadre et c est ce qui le fait lire.
		E.DepuisM = FVector2D(-0.57, 0.82);
		E.DistanceM = 120.0f;
		E.HauteurM = 6.0f;
		Tournee.Add(E);
		++Ajoutees;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] photo : %d canyons a la tournee (sur %d sites)"),
		Ajoutees, Sites.Num());
	return Ajoutees;
}

int32 UWorldseedPhotographe::AjouterLesFalaises(int32 Combien)
{
	AWorldseedVoxelTerrain* const T = Terrain();
	if (!T) { return 0; }

	const FWorldseedGeometry& Geo = T->MondeGeometrie();
	const TArray<float>& H = T->MondeAltitudes();
	if (H.Num() != Geo.CellCount()) { return 0; }

	const int32 NX = Geo.NX;
	const int32 NY = Geo.NY;
	const double MailleM = FMath::Max(Geo.MetersPerPixel(), 1e-3f);

	// --- LES PLUS HAUTES, ET ELLES DOIVENT ETRE PRES DE LA MER ---------------
	//
	// Un ressaut au fond d'une vallee n'est pas une falaise littorale : ce
	// qu'on veut juger est la paroi qui tombe DANS l'eau, parce que c'est elle
	// que la passe littorale a creee et elle qui porte les arches marines.
	struct FCandidat
	{
		int32 Cellule = 0;
		float Chute = 0.0f;
		FVector2D VersLeBas = FVector2D::ZeroVector;
	};
	TArray<FCandidat> Candidats;

	for (int32 J = 2; J < NY - 2; ++J)
	{
		for (int32 I = 2; I < NX - 2; ++I)
		{
			const int32 C = J * NX + I;
			if (H[C] <= 5.0f) { continue; }

			float Chute = 0.0f;
			FIntPoint Vers = FIntPoint::ZeroValue;
			bool bMerProche = false;

			for (int32 DJ = -2; DJ <= 2; ++DJ)
			{
				for (int32 DI = -2; DI <= 2; ++DI)
				{
					const int32 V = (J + DJ) * NX + (I + DI);
					if (H[V] <= 0.0f) { bMerProche = true; }
					const float D = H[C] - H[V];
					if (D > Chute) { Chute = D; Vers = FIntPoint(DI, DJ); }
				}
			}

			if (!bMerProche || Chute < 25.0f) { continue; }

			FCandidat K;
			K.Cellule = C;
			K.Chute = Chute;
			K.VersLeBas = FVector2D(Vers.X, Vers.Y).GetSafeNormal();
			Candidats.Add(K);
		}
	}

	Candidats.Sort([](const FCandidat& A, const FCandidat& B)
	{
		return A.Chute > B.Chute;
	});

	int32 Ajoutees = 0;
	TArray<FVector2D> Prises;
	for (const FCandidat& K : Candidats)
	{
		if (Ajoutees >= Combien) { break; }

		const double X = (static_cast<double>(K.Cellule % NX) / NX - 0.5) * Geo.WidthM();
		const double Y = (static_cast<double>(K.Cellule / NX) / NY - 0.5) * Geo.HeightM;

		// Deux falaises voisines sont la MEME falaise.
		bool bVoisine = false;
		for (const FVector2D& P : Prises)
		{
			if (FVector2D::DistSquared(FVector2D(X, Y), P) < 3000.0 * 3000.0)
			{
				bVoisine = true;
				break;
			}
		}
		if (bVoisine) { continue; }
		Prises.Emplace(X, Y);

		FWorldseedPhotoStop E;
		E.Nom = FString::Printf(TEXT("falaise%02d"), Ajoutees + 1);
		E.CibleM = FVector(X, Y, H[K.Cellule] * 0.5);

		// ON SE MET DU COTE DE LA MER, sinon on photographie le plateau et la
		// paroi est DERRIERE la camera.
		E.DepuisM = K.VersLeBas;

		// DANS LE RAYON DE CHARGEMENT, SINON ON PHOTOGRAPHIE LE SOL DE FOND.
		// Les chunks ne se batissent que dans 250 m autour du pion ; au-dela
		// c'est la nappe d'horizon qu'on voit, qui fait 63 m par maille a
		// 64 km. Une heure perdue sur ce defaut, faute d'avoir verifie ce que
		// le cadre contenait.
		E.DistanceM = FMath::Clamp(K.Chute * 2.2f, 90.0f, 170.0f);
		E.HauteurM = 10.0f;
		Tournee.Add(E);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] photo : falaise%02d a (%.0f, %.0f) m, sommet %.0f m, ")
			TEXT("chute %.0f m sur %.0f m"),
			Ajoutees + 1, X, Y, H[K.Cellule], K.Chute, MailleM * 2.0);
		++Ajoutees;
	}
	return Ajoutees;
}

void UWorldseedPhotographe::Demarrer()
{
	if (Tournee.Num() == 0 || EnCours()) { return; }

	Etape = 0;
	Attente = 0;
	Horloge = 0.0f;

	AWorldseedVoxelTerrain* const T = Terrain();
	if (!T) { return; }

	const FWorldseedPhotoStop& E = Tournee[0];
	MidiFige();

	T->TeleporterJoueur(E.CibleM.X + E.DepuisM.X * E.DistanceM,
		E.CibleM.Y + E.DepuisM.Y * E.DistanceM);

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] photo : tournee de %d vues"), Tournee.Num());
}

void UWorldseedPhotographe::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bArme)
	{
		const AWorldseedVoxelTerrain* const T = Terrain();
		if (!T || T->MondeAltitudes().Num() == 0)
		{
			// Le monde n'est pas encore charge : on repassera.
			return;
		}

		bArme = false;

		// Les falaises d'abord -- c'est ce que la passe littorale cree, et la
		// condition des arches marines. Les arches ensuite.
		AjouterLesFalaises(6);
		AjouterLesArches();
		AjouterLesTables(6);
		AjouterLesCanyons(6);
		Demarrer();
	}

	if (!EnCours()) { return; }
	Horloge += DeltaTime;
	Avancer();
}

void UWorldseedPhotographe::Avancer()
{
	AWorldseedVoxelTerrain* const T = Terrain();
	UWorld* const W = GetWorld();
	APawn* const Pion = W ? UGameplayStatics::GetPlayerPawn(W, 0) : nullptr;
	APlayerController* const PC = W ? UGameplayStatics::GetPlayerController(W, 0) : nullptr;
	if (!T || !Pion || !PC) { return; }

	const FWorldseedPhotoStop& E = Tournee[Etape];

	// ON VISE A CHAQUE PASSE, PAS UNE SEULE FOIS. Le pion pivote quand il
	// retombe sur le sol, et une orientation posee avant l'atterrissage est
	// perdue sans le moindre signe.
	const FVector CibleCm = T->GetActorLocation()
		+ FVector(E.CibleM.X, E.CibleM.Y, E.CibleM.Z) * WorldseedMetersToCm;
	FVector OeilCm = Pion->GetActorLocation();
	if (const APlayerCameraManager* const Cam = PC->PlayerCameraManager)
	{
		OeilCm = Cam->GetCameraLocation();
	}
	PC->SetControlRotation((CibleCm - OeilCm).Rotation());

	// LE POINT DE VUE SE TIENT A LA HAUTEUR DE LA CIBLE, PAS A CELLE DU SOL.
	// Cale sur le sol local, la camera visait cinquante-cinq degres vers le bas
	// et photographiait le dos du personnage -- l'arche etait a 119 m et le sol
	// du point de vue a 239. On part de l'altitude de la cible et l'on ne
	// remonte que si l'on se trouve DANS la roche, ce que le champ sait dire.
	if (UCharacterMovementComponent* const Move =
		Pion->FindComponentByClass<UCharacterMovementComponent>())
	{
		if (T->JoueurPose())
		{
			Move->SetMovementMode(MOVE_Flying);
			Move->Velocity = FVector::ZeroVector;

			const FVector P = Pion->GetActorLocation() - T->GetActorLocation();
			const double VX = P.X / WorldseedMetersToCm;
			const double VY = P.Y / WorldseedMetersToCm;

			FWorldseedCaveLocal Local;
			T->MondeGrottes().Query(
				FBox(FVector(VX - 30.0, VY - 30.0, E.CibleM.Z - 20.0),
					FVector(VX + 30.0, VY + 30.0, E.CibleM.Z + 240.0)), Local);

			// JAMAIS SOUS LE NIVEAU DE LA MER. La remontee qui suit sort de la
			// ROCHE, elle ne sait rien de l EAU -- l ocean est un plan a
			// l altitude zero, etranger au champ de densite. Une vue de table
			// est sortie entierement bleue, camera NOYEE, parce que le pied
			// calcule tombait a -45 m.
			double ZM = FMath::Max(E.CibleM.Z + E.HauteurM, 3.0);
			while (ZM < E.CibleM.Z + 220.0
				&& T->MondeChamp().At(FVector(VX, VY, ZM), &Local) <= 0.0)
			{
				ZM += 2.0;
			}

			FVector Pose = Pion->GetActorLocation();
			Pose.Z = T->GetActorLocation().Z + (ZM + 2.0) * WorldseedMetersToCm;
			Pion->SetActorLocation(Pose, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	// Le sol du point de vue n'est pas encore solide : on attend, et ce temps
	// ne compte pas -- sinon on declenche avant que le monde existe.
	if (!T->JoueurPose())
	{
		Horloge = 0.0f;
		return;
	}

	// LAISSER LE MONDE SE BATIR AVANT DE TIRER. Les chunks arrivent par travaux
	// asynchrones ; une photo prise des l'arrivee montre un paysage troue, et
	// l'on croit a un defaut de generation.
	if (Attente == 0 && Horloge >= AvantPhotoS)
	{
		const FString Fichier = FPaths::Combine(
			FPaths::ProjectSavedDir(), TEXT("Photos"), E.Nom + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Fichier, false, false);
		Attente = 1;

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] photo : %s -- cible (%.0f, %.0f, %.0f) m, ")
			TEXT("depuis %.0f m dans l'axe"),
			*E.Nom, E.CibleM.X, E.CibleM.Y, E.CibleM.Z, E.DistanceM);
	}

	if (Horloge < ApresPhotoS) { return; }

	++Etape;
	Attente = 0;
	Horloge = 0.0f;

	if (EnCours())
	{
		const FWorldseedPhotoStop& S = Tournee[Etape];
		T->TeleporterJoueur(S.CibleM.X + S.DepuisM.X * S.DistanceM,
			S.CibleM.Y + S.DepuisM.Y * S.DistanceM);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] photo : tournee terminee"));
	Etape = INDEX_NONE;
	if (bQuitterEnsuite)
	{
		FPlatformMisc::RequestExit(false);
	}
}
