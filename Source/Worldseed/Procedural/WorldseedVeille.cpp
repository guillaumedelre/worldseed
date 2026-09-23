// Worldseed - la veille : surveiller le streaming pendant une partie reelle.

#include "Procedural/WorldseedVeille.h"

#include "Procedural/WorldseedVoxelTerrain.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void UWorldseedVeille::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!FParse::Param(FCommandLine::Get(), TEXT("WorldseedVeille")))
	{
		return;
	}

	FParse::Value(FCommandLine::Get(), TEXT("WorldseedVeillePeriode="), PeriodeS);
	PeriodeS = FMath::Max(PeriodeS, 0.05f);

	bArmee = true;

	// ON ARME ICI, ON NE MESURE PAS. Le sous-systeme recoit son
	// OnWorldBeginPlay AVANT les acteurs : le terrain n'existe pas encore, et
	// surtout son monde n'est pas charge. Le depot a deja paye une tournee
	// photo entiere sans une ligne de journal pour avoir construit trop tot.
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] veille : armee, echantillon toutes les %.2f s. ")
		TEXT("Elle se taira tant qu'elle ne verra rien."), PeriodeS);
}

AWorldseedVoxelTerrain* UWorldseedVeille::Terrain() const
{
	UWorld* const W = GetWorld();
	if (!W) { return nullptr; }
	for (TActorIterator<AWorldseedVoxelTerrain> It(W); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void UWorldseedVeille::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	AWorldseedVoxelTerrain* const T = Terrain();
	if (!T || T->MondeAltitudes().Num() == 0)
	{
		return;   // pas de terrain voxel ici -- le menu, par exemple
	}

	Horloge += DeltaTime;

	// --- LE TEMOIN, UNE FOIS LE MONDE POSE ---------------------------------
	//
	// Tant que le premier remplissage court, TOUT manque : mesurer la n'aurait
	// aucun sens, et crierait a chaque trame. On attend donc la stabilisation
	// -- compte de chunks fige et aucun travail en vol -- exactement comme le
	// banc, et par la MEME fonction : le depot interdit de recopier une regle
	// dans deux fichiers.
	if (!bTemoinPris)
	{
		StableS = T->DiffusionStable(DernierCompte) ? (StableS + DeltaTime) : 0.0f;

		// ON N'ATTEND PAS LA STABILITE INDEFINIMENT, ET C'EST UNE CORRECTION.
		//
		// LA PREMIERE VERSION NE MESURAIT RIEN, EN SILENCE. Elle exigeait deux
		// secondes de diffusion FIGEE avant de prendre le temoin -- et une
		// partie reelle ne les offre pas forcement : le joueur est parti d'un
		// sommet sur une pente a 22,1 degres, le pion a glisse, le streaming
		// n'a jamais cesse de bouger, et la veille s'est tue toute la session.
		// Zero ligne, ce qui ressemble trait pour trait a « rien vu ».
		//
		// GARDER L'INSTRUMENT DERRIERE UNE CONDITION QU'UNE VRAIE SESSION PEUT
		// NE JAMAIS OFFRIR est une faute de conception, et le silence qui en
		// resulte est le pire des resultats. On mesure donc de toute facon
		// passe ce delai, et l'on DIT que le temoin n'a pas pu etre pris --
		// comme le banc dit quand il mesure un transitoire au lieu de le taire.
		const bool bPose = (StableS >= 2.0f);
		if (!bPose && Horloge < AttenteMaxS) { return; }

		bTemoinPris = true;
		bTemoinValide = bPose;
		ProchainEchantillon = Horloge;
		ProchainResume = Horloge + PeriodeResumeS;

		if (!bPose)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] veille : TEMOIN NON PRIS -- la diffusion n'a ")
				TEXT("pas ete figee deux secondes en %.0f s (le pion bouge, ou ")
				TEXT("glisse). On mesure quand meme, mais un trou vu pourrait ")
				TEXT("etre un defaut de l'instrument : a lire avec cette reserve."),
				AttenteMaxS);
			return;
		}

		const FWorldseedSondageDeVue Temoin = T->SonderLaVue();
		if (Temoin.Trous == 0)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] veille : TEMOIN -- monde pose, %d rayons, ")
				TEXT("0 trou. L'instrument se tait quand il doit. A vous."),
				Temoin.Rayons);
		}
		else
		{
			bTemoinValide = false;
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] veille : TEMOIN FAUX -- %d trous sur %d rayons ")
				TEXT("sur un monde POSE. C'est l'INSTRUMENT qui est en cause, ")
				TEXT("pas le streaming : ne rien conclure de la suite."),
				Temoin.Trous, Temoin.Rayons);
		}
		return;
	}

	if (Horloge < ProchainEchantillon)
	{
		if (Horloge >= ProchainResume)
		{
			Resumer(TEXT("en cours"));
			ProchainResume = Horloge + PeriodeResumeS;
		}
		return;
	}
	ProchainEchantillon = Horloge + PeriodeS;

	// --- OU EN EST LE JOUEUR -----------------------------------------------
	//
	// LA VITESSE EST CE QUI DIT SI LA MESURE VAUT QUELQUE CHOSE. Un releve pris
	// a l'arret ne prouve rien : rien ne se subdivise, rien n'entre dans le
	// rayon. On la mesure sur les POSITIONS et non sur `GetVelocity`, qui vaut
	// zero dans plusieurs etats de deplacement.
	UWorld* const W = GetWorld();
	const APawn* const P = W ? UGameplayStatics::GetPlayerPawn(W, 0) : nullptr;
	if (!P) { return; }

	const FVector PosCm = P->GetActorLocation();
	double VitesseMs = 0.0;
	if (bPositionConnue)
	{
		const double PasM = FVector::Dist(PosCm, DernierePositionCm) / 100.0;
		DistanceParcourueM += PasM;
		VitesseMs = PasM / FMath::Max(static_cast<double>(PeriodeS), 1e-3);
	}
	DernierePositionCm = PosCm;
	bPositionConnue = true;

	float CapDeg = 0.0f;
	if (const APlayerCameraManager* const Cam =
		UGameplayStatics::GetPlayerCameraManager(W, 0))
	{
		CapDeg = static_cast<float>(Cam->GetCameraRotation().Yaw);
	}

	++Echantillons;
	if (VitesseMs > 0.5) { ++EchantillonsEnMouvement; }

	// --- LES DEUX INSTRUMENTS ----------------------------------------------
	const FWorldseedStreamingReleve R = T->ReleveStreaming();
	const FWorldseedSondageDeVue S = T->SonderLaVue();

	SommeOrphelins += R.Orphelins;
	SommeBeants += R.TrousDecouverts;
	SommeRayonsTroues += S.Trous;
	PireOrphelins = FMath::Max(PireOrphelins, R.Orphelins);
	PireBeants = FMath::Max(PireBeants, R.TrousDecouverts);

	if (S.Trous > PireRayonsTroues)
	{
		PireRayonsTroues = S.Trous;
		PireLieuCm = PosCm;
		PireCapDeg = CapDeg;
	}
	if (S.Trous > 0)
	{
		TrouLePlusProcheM = (TrouLePlusProcheM <= 0.0f)
			? S.PlusProcheM : FMath::Min(TrouLePlusProcheM, S.PlusProcheM);
	}

	// --- ET L'ON NE CRIE QUE QUAND IL Y A QUELQUE CHOSE A DIRE --------------
	//
	// Le sondage de vue passe en premier : c'est l'ARBITRE TIERS, celui qui
	// interroge le champ et le rendu sans passer par notre comptabilite. Un
	// trou beant qu'il ne voit pas est un trou hors du champ de vision -- reel,
	// mais que le joueur ne regarde pas.
	if (S.Trous > 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] veille : TROU VU -- %d rayon(s) sur %d sans ")
			TEXT("geometrie la ou le champ en promet, le plus proche a %.0f m ")
			TEXT("| joueur (%.0f, %.0f, %.0f) m, cap %.0f deg, %.1f m/s ")
			TEXT("| %d beants, %d orphelins, %d feuilles"),
			S.Trous, S.Rayons, S.PlusProcheM,
			PosCm.X / 100.0, PosCm.Y / 100.0, PosCm.Z / 100.0,
			CapDeg, VitesseMs,
			R.TrousDecouverts, R.Orphelins, R.Feuilles);
		++EvenementsCries;
	}
	else if (R.TrousDecouverts > 0)
	{
		// UN BEANT HORS DU CHAMP DE VISION N'EST PAS RIEN : il sera dans le
		// champ des qu'on tournera la tete. On le dit, mais plus bas et sans
		// pretendre qu'il a ete VU.
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] veille : %d trou(s) beant(s) hors du champ de ")
			TEXT("vision | joueur (%.0f, %.0f) m, cap %.0f deg, %.1f m/s"),
			R.TrousDecouverts, PosCm.X / 100.0, PosCm.Y / 100.0,
			CapDeg, VitesseMs);
		++EvenementsCries;
	}

	if (Horloge >= ProchainResume)
	{
		Resumer(TEXT("en cours"));
		ProchainResume = Horloge + PeriodeResumeS;
	}
}

void UWorldseedVeille::Resumer(const TCHAR* Quand) const
{
	// UNE VEILLE QUI N'A RIEN MESURE DOIT LE DIRE, ET FORT.
	//
	// C'est le defaut qui a coute la premiere session : zero echantillon, zero
	// ligne, et un journal qui ressemblait exactement a « rien vu ». Or « rien
	// vu » et « rien mesure » sont deux choses opposees, et ce depot a deja
	// paye leur confusion -- une couleur invisible a deux causes contraires,
	// le terme qui ne s'evalue jamais et le terme dont rien n'atteint l'ecran.
	if (Echantillons <= 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] veille (%s) : AUCUN ECHANTILLON PRIS en %.0f s. ")
			TEXT("Ce journal ne dit RIEN du streaming -- ne pas le lire comme ")
			TEXT("un monde sain."),
			Quand, Horloge);
		return;
	}

	// LE SILENCE DOIT SE LIRE, SANS QUOI IL NE PROUVE RIEN. « Aucune ligne »
	// peut vouloir dire « rien vu » comme « rien mesure », et ce depot a deja
	// confondu les deux -- une passe qui ne s'evalue jamais et une passe dont
	// rien n'atteint l'ecran ne se distinguent que par un COMPTE.
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] veille (%s) : %d echantillons dont %d EN MOUVEMENT, ")
		TEXT("%.0f m parcourus | trous vus : moyenne %.2f rayon(s), pire %d ")
		TEXT("| beants : moyenne %.1f, pire %d | orphelins : moyenne %.1f, pire %d ")
		TEXT("| %d evenement(s) crie(s)"),
		Quand, Echantillons, EchantillonsEnMouvement, DistanceParcourueM,
		static_cast<double>(SommeRayonsTroues) / Echantillons, PireRayonsTroues,
		static_cast<double>(SommeBeants) / Echantillons, PireBeants,
		static_cast<double>(SommeOrphelins) / Echantillons, PireOrphelins,
		EvenementsCries);

	// UN RELEVE QUI NE PORTE PAS SA CONFIGURATION NE SE COMPARE A RIEN six mois
	// plus tard, et le depot a deja compare deux releves pris dans deux etats
	// differents en croyant qu'ils l'etaient. Celui-ci dit donc s'il a eu son
	// temoin -- c'est-a-dire s'il a le droit d'etre cru.
	if (!bTemoinValide)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] veille : ce releve est SANS TEMOIN -- l'instrument ")
			TEXT("n'a pas pu etre verifie sur un monde pose. Un trou vu peut ")
			TEXT("etre un defaut de la sonde."));
	}

	if (PireRayonsTroues > 0)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] veille : le PIRE trou vu -- %d rayons, le plus ")
			TEXT("proche a %.0f m. Pour y retourner : ")
			TEXT("-WorldseedDepartX=%.0f -WorldseedDepartY=%.0f -WorldseedCap=%.0f"),
			PireRayonsTroues, TrouLePlusProcheM,
			PireLieuCm.X / 100.0, PireLieuCm.Y / 100.0, PireCapDeg);
	}

	if (EchantillonsEnMouvement * 4 < Echantillons)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] veille : ATTENTION -- %d echantillons sur %d ")
			TEXT("seulement ont ete pris EN MOUVEMENT. A l'arret rien ne se ")
			TEXT("subdivise et rien n'entre dans le rayon : ce releve ne dit ")
			TEXT("presque rien du streaming."),
			EchantillonsEnMouvement, Echantillons);
	}
}

void UWorldseedVeille::Deinitialize()
{
	if (bArmee && Echantillons > 0)
	{
		Resumer(TEXT("FIN DE PARTIE"));
	}
	Super::Deinitialize();
}
