// Worldseed - le banc : mesurer le cout du terrain sans outillage externe.

#include "Procedural/WorldseedBanc.h"

#include "Procedural/WorldseedVoxelTerrain.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformMemory.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// Les quatre fils. GGameThreadTime / GRenderThreadTime / GRHIThreadTime sont
// des globales de RenderCore ; RHIGetGPUFrameCycles vient du RHI. C'est la
// meme source que `stat unit`, sans dependre de son affichage.
#include "RenderTimer.h"
#include "DynamicRHI.h"

void UWorldseedBanc::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!FParse::Param(FCommandLine::Get(), TEXT("WorldseedBanc")))
	{
		return;
	}

	bQuitterEnsuite = FParse::Param(FCommandLine::Get(), TEXT("WorldseedQuitter"));
	bProfilGPU = FParse::Param(FCommandLine::Get(), TEXT("WorldseedProfilGPU"));
	bArme = true;

	// LA MARCHE EST OPTIONNELLE, ET SON ABSENCE REND LE BANC D'AVANT.
	FParse::Value(FCommandLine::Get(), TEXT("WorldseedMarche="), MarcheS);
	FParse::Value(FCommandLine::Get(), TEXT("WorldseedMarcheCap="), MarcheCapDeg);

	// ON ARME ICI, ON NE MESURE PAS. Le sous-systeme recoit son OnWorldBeginPlay
	// AVANT les acteurs : le terrain n'existe pas encore, et surtout son monde
	// n'est pas charge. Meme piege que pour la tournee photo, et il avait coute
	// une tournee entiere sans une ligne de journal.
}

AWorldseedVoxelTerrain* UWorldseedBanc::Terrain() const
{
	UWorld* const W = GetWorld();
	if (!W) { return nullptr; }
	for (TActorIterator<AWorldseedVoxelTerrain> It(W); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void UWorldseedBanc::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// LE PROFIL EST LA SEULE CHOSE QUI SURVIT A `bFini` : la capture se fait a
	// la trame suivante et son vidage est asynchrone. On tient donc le jeu en
	// vie le temps qu'il faut, puis on quitte.
	if (bProfilDemande)
	{
		if (FPlatformTime::Seconds() - DebutAttenteProfil >= AttenteProfilS)
		{
			bProfilDemande = false;
			if (bQuitterEnsuite)
			{
				if (UWorld* const W = GetWorld())
				{
					UKismetSystemLibrary::QuitGame(W, nullptr,
						EQuitPreference::Quit, false);
				}
			}
		}
		return;
	}

	if (bFini) { return; }

	AWorldseedVoxelTerrain* const T = Terrain();
	if (!T || T->MondeAltitudes().Num() == 0)
	{
		return;   // le monde n'est pas encore charge : on repassera
	}

	Horloge += DeltaTime;

	if (bArme)
	{
		// --- ON ATTEND LA STABILISATION, PAS UN DELAI ---------------------
		//
		// PREMIERE VERSION FAUSSE, ET ELLE NE SE VOYAIT PAS. Elle chauffait un
		// nombre fixe de secondes, et le releve a rendu EXACTEMENT 552 chunks
		// a 250 m comme a 400 -- alors qu'a 400 m il en faut environ 1700. Le
		// remplissage est limite par le DEBIT des travaux, pas par le rayon :
		// on mesurait donc un transitoire identique des deux cotes, et l'A/B
		// ne comparait rien. Le signe qui trahit est toujours le meme dans ce
		// depot : DEUX MESURES IDENTIQUES AU CHIFFRE PRES pour deux reglages
		// differents.
		//
		// Le vrai critere est que le streaming ne bouge plus : compte de
		// chunks fige ET aucun travail en vol.
		const int32 N = T->NombreDeChunks();
		const bool bFige = (N == DernierCompte) && (T->TravauxEnVol() == 0);
		DernierCompte = N;
		StableS = bFige ? (StableS + DeltaTime) : 0.0f;

		if (StableS < ChauffeS && Horloge < PlafondAttenteS)
		{
			return;
		}
		if (Horloge >= PlafondAttenteS)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] banc : streaming NON stabilise apres %.0f s ")
				TEXT("(%d chunks, %d en vol) -- la mesure porte sur un transitoire"),
				PlafondAttenteS, N, T->TravauxEnVol());
		}
		bArme = false;
		bEnCours = true;
		DebutMesure = Horloge;
		Trames.Reset();
		Trames.Reserve(2048);
		SommeJeuMs = SommeRenduMs = SommeRhiMs = SommeGpuMs = 0.0;
		Echantillons = 0;

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] banc : rayon %.0f m -- %d chunks stabilises en %.0f s, ")
			TEXT("mesure sur %.0f s"),
			T->RayonDeChargementM(), T->NombreDeChunks(), Horloge, MesureS);
		return;
	}

	if (!bEnCours) { return; }

	// LA TRAME EST MESUREE PAR LE JEU, SUR SON PROPRE DeltaTime. C'est ce qui
	// met la mesure a l'abri du bridage de l'editeur en arriere-plan -- le piege
	// qui a coute au depot une journee entiere de conclusions fausses, avec des
	// chiffres plausibles, stables et reproductibles.
	Trames.Add(DeltaTime * 1000.0f);

	// --- ET LE DETAIL PAR FIL, PARCE QU'UN TOTAL NE SE CORRIGE PAS ---------
	//
	// Un agregat ne designe pas de coupable : ce depot l'a paye quatre fois de
	// suite sur le routage des galeries, ou aucune des quatre corrections n'a
	// bouge le chiffre parce qu'il melangeait deux populations. La trame
	// entiere est le meme genre de nombre. Ces quatre-la disent OU elle passe.
	//
	// Les globales sont en cycles ; `ToMilliseconds` les convertit. C'est
	// exactement ce que fait `FStatUnitData::DrawStat` (UnrealClient.cpp:384
	// et suivantes), sans dependre de l'affichage d'un stat a l'ecran.
	SommeJeuMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
	SommeRenduMs += FPlatformTime::ToMilliseconds(GRenderThreadTime);
	SommeRhiMs += FPlatformTime::ToMilliseconds(GRHIThreadTime);
	SommeGpuMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
	++Echantillons;

	if (bEnMarche)
	{
		MesurerLaMarche(T);
		if (Horloge - DebutMarche >= MarcheS)
		{
			Conclure();
		}
		return;
	}

	if (Horloge - DebutMesure >= MesureS)
	{
		// LA MARCHE VIENT APRES LA MESURE IMMOBILE, JAMAIS PENDANT. Les deux
		// repondent a deux questions : ce que coute un monde POSE, et ce que
		// le streaming rate quand on AVANCE. Les melanger rendrait une trame
		// moyenne qui n'est ni l'une ni l'autre -- et ce depot a deja conclu
		// sur un agregat recouvrant deux populations.
		if (MarcheS > 0.0f && !bEnMarche)
		{
			bEnMarche = true;
			DebutMarche = Horloge;
			if (const APawn* const P = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
			{
				DepartMarcheCm = P->GetActorLocation();
			}
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] banc : MARCHE %.0f s au cap %.0f deg -- ")
					TEXT("on mesure ce que le streaming rate en avancant"),
				MarcheS, MarcheCapDeg);
			return;
		}
		Conclure();
	}
}

void UWorldseedBanc::MesurerLaMarche(AWorldseedVoxelTerrain* T)
{
	UWorld* const W = GetWorld();
	if (!W || !T) { return; }

	// ON POUSSE L'ENTREE DE DEPLACEMENT, on ne teleporte pas : c'est le
	// personnage qui marche, avec sa vitesse, ses pentes et ses collisions.
	APawn* const P = UGameplayStatics::GetPlayerPawn(W, 0);
	if (!P) { return; }

	const FVector Dir = FRotator(0.0f, MarcheCapDeg, 0.0f).Vector();
	P->AddMovementInput(Dir, 1.0f);

	// ET L'ON COMPTE LES DEUX ECARTS SEPAREMENT. Un chunk EN TROP et un chunk
	// EN MOINS sont deux defauts opposes : le premier dessine deux fois, le
	// second ne dessine rien. Un seul nombre les recouvrirait.
	const FWorldseedStreamingReleve R = T->ReleveStreaming();
	SommeOrphelins += R.Orphelins;
	SommeManquants += R.Manquants;
	SommeHysteresis += R.GardesParHysteresis;
	PireOrphelins = FMath::Max(PireOrphelins, R.Orphelins);
	PireManquants = FMath::Max(PireManquants, R.Manquants);
	PireHysteresis = FMath::Max(PireHysteresis, R.GardesParHysteresis);
	++EchantillonsMarche;
}

void UWorldseedBanc::Conclure()
{
	bEnCours = false;
	bFini = true;

	AWorldseedVoxelTerrain* const T = Terrain();

	float Moyenne = 0.0f;
	float P95 = 0.0f;
	float Pire = 0.0f;
	if (Trames.Num() > 0)
	{
		double Somme = 0.0;
		for (const float MsT : Trames)
		{
			Somme += MsT;
			Pire = FMath::Max(Pire, MsT);
		}
		Moyenne = static_cast<float>(Somme / Trames.Num());

		TArray<float> Triees = Trames;
		Triees.Sort();
		P95 = Triees[FMath::Clamp(
			FMath::RoundToInt(0.95f * (Triees.Num() - 1)), 0, Triees.Num() - 1)];
	}

	const FPlatformMemoryStats Mem = FPlatformMemory::GetStats();

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] === BANC ==="));
	if (T)
	{
		UE_LOG(LogTemp, Log, TEXT("[Worldseed]   rayon de chargement %.0f m"),
			T->RayonDeChargementM());
		T->ReportState();
	}
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   trame : moyenne %.2f ms (%.0f images/s), p95 %.2f ms, ")
		TEXT("pire %.2f ms, sur %d trames"),
		Moyenne, (Moyenne > 0.0f) ? 1000.0f / Moyenne : 0.0f, P95, Pire, Trames.Num());
	// --- OU PASSE LA TRAME ---------------------------------------------------
	//
	// LA PREMIERE QUESTION D'UNE MESURE DE PERFORMANCE EST « LIMITE PAR QUOI »,
	// et le banc ne savait pas y repondre : il ne rapportait que le total.
	// Optimiser le GPU d'une trame limitee par le fil de jeu ne change rien, et
	// l'inverse non plus.
	if (Echantillons > 0)
	{
		const double Jeu = SommeJeuMs / Echantillons;
		const double Rendu = SommeRenduMs / Echantillons;
		const double Rhi = SommeRhiMs / Echantillons;
		const double Gpu = SommeGpuMs / Echantillons;

		const double Max = FMath::Max3(FMath::Max(Jeu, Rendu), Rhi, Gpu);
		const TCHAR* Limite =
			(Max == Gpu) ? TEXT("le GPU") :
			(Max == Rendu) ? TEXT("le fil de RENDU") :
			(Max == Rhi) ? TEXT("le fil RHI") : TEXT("le fil de JEU");

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   fils : jeu %.2f ms | rendu %.2f | RHI %.2f | ")
			TEXT("GPU %.2f  ->  limite par %s"),
			Jeu, Rendu, Rhi, Gpu, Limite);

		// LE SIGNE QUI TRAHIT UNE MESURE BRIDEE, ET IL A DEJA COUTE UNE JOURNEE
		// A CE DEPOT. Quand la trame vaut EXACTEMENT le temps GPU, elle n'est
		// pas limitee par le GPU : elle est plafonnee par autre chose -- le
		// bridage de l'editeur en arriere-plan, ou une limite d'images par
		// seconde. Sur une scene reellement limitee par le GPU, la trame
		// depasse toujours un peu le temps GPU. Le banc tourne en jeu, donc il
		// ne devrait jamais voir ce cas ; s'il le voit, il le DIT plutot que de
		// laisser tirer une conclusion fausse sur des chiffres plausibles.
		if (Moyenne > 0.0f && FMath::Abs(Moyenne - Gpu) < 0.01)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed]   ATTENTION : trame et GPU identiques au ")
				TEXT("centieme (%.2f ms). La trame est PLAFONNEE, pas limitee ")
				TEXT("par le GPU -- ne rien conclure de ce releve."),
				Moyenne);
		}
	}

	// --- CE QUE LE STREAMING RATE EN AVANCANT --------------------------------
	//
	// DEUX ECARTS, JAMAIS UN SEUL NOMBRE. L'orphelin dessine DEUX FOIS le meme
	// terrain, le manquant n'en dessine AUCUN : ils se corrigent a l'oppose, et
	// les additionner rendrait un agregat qu'aucune correction ne deplacerait.
	//
	// ET LA DISTANCE PARCOURUE EST DITE, parce qu'une marche bloquee contre une
	// paroi rendrait zero ecart sans rien prouver -- elle ressemblerait trait
	// pour trait a un streaming sain.
	if (EchantillonsMarche > 0 && T)
	{
		double ParcouruM = 0.0;
		if (const APawn* const P = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
		{
			ParcouruM = FVector::Dist2D(P->GetActorLocation(), DepartMarcheCm) / 100.0;
		}

		const FWorldseedStreamingReleve F = T->ReleveStreaming();

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   MARCHE %.0f s au cap %.0f deg -- %.0f m parcourus ")
			TEXT("(%.1f m/s), %d releves"),
			MarcheS, MarcheCapDeg, ParcouruM,
			(MarcheS > 0.0f) ? ParcouruM / MarcheS : 0.0, EchantillonsMarche);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   ORPHELINS (plus feuilles mais DANS le rayon, ")
			TEXT("geometrie en DOUBLE) : moyenne %.0f, pire %d"),
			static_cast<double>(SommeOrphelins) / EchantillonsMarche, PireOrphelins);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   temoin -- gardes par l'HYSTERESIS (hors rayon, ")
			TEXT("VOULUS) : moyenne %.0f, pire %d"),
			static_cast<double>(SommeHysteresis) / EchantillonsMarche, PireHysteresis);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   MANQUANTS (feuilles sans maillage, TROU) : ")
			TEXT("moyenne %.0f, pire %d"),
			static_cast<double>(SommeManquants) / EchantillonsMarche, PireManquants);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   a l'arrivee : %d feuilles demandees, %d chunks ")
			TEXT("suivis, %d orphelins, %d gardes par hysteresis, %d manquants"),
			F.Feuilles, F.Suivis, F.Orphelins, F.GardesParHysteresis, F.Manquants);

		if (ParcouruM < 10.0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed]   ATTENTION : %.0f m parcourus seulement -- le ")
				TEXT("pion n'a pas marche (bloque, ou aucun controleur). Ce ")
				TEXT("releve ne mesure PAS le streaming en mouvement."),
				ParcouruM);
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   memoire physique utilisee %.2f Go"),
		Mem.UsedPhysical / (1024.0 * 1024.0 * 1024.0));

	// --- LE MONDE, SON POIDS ET SES PORTEURS -------------------------------
	//
	// DEUX CHIFFRES PARCE QUE LA MEMOIRE DU PROCESSUS NE SAIT PAS REPONDRE.
	// Elle derive de trois cents megaoctets d'un lancement a l'autre -- releve
	// sur cinq passes identiques : 7,60 a 7,99 Go -- donc les deux cent vingt
	// megaoctets qu'une duplication du monde ajouterait s'y noient. Ceux-ci
	// sont DETERMINISTES : le poids ne depend que de la grille, et le nombre
	// de porteurs dit combien de fois ce poids est paye.
	//
	// CE QUE LE COMPTE VAUT, ET IL FAUT SAVOIR LIRE LE TROIS. Deux porteurs
	// sont les acteurs -- le terrain et l'acteur voxel -- et le TROISIEME est
	// la variable ci-dessous, qui tient une reference le temps de la lire. Un
	// jeu lance depuis le menu en ajoute un, l'instance de jeu, et chaque
	// travail de maillage en vol un de plus. Ce que ce chiffre ne doit plus
	// jamais faire, c'est monter avec le nombre d'acteurs qui LISENT le monde :
	// c'etait le defaut, et il coutait une copie entiere par lecteur.
	if (const FWorldseedMondePtr Monde = T->MondePartage())
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   monde partage : %.1f Mo de tableaux, %d porteurs"),
			Monde->OctetsApprox() / (1024.0 * 1024.0),
			Monde.GetSharedReferenceCount());
	}
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   Lecture : le budget est 16,67 ms. La trame est mesuree ")
		TEXT("PAR LE JEU sur son propre DeltaTime, donc a l'abri du bridage de ")
		TEXT("l'editeur en arriere-plan -- piege qui a deja coute une journee de ")
		TEXT("conclusions fausses a ce depot."));

	// --- LA VENTILATION DU GPU, SI ON L'A DEMANDEE -------------------------
	//
	// ELLE VIENT ICI ET NULLE PART AILLEURS : apres la stabilisation et apres
	// la fenetre de mesure, donc sur une scene qui ne bouge plus. Lancee
	// pendant le remplissage, elle capturerait une trame ou le streaming
	// travaille encore et l'on attribuerait au rendu ce qui est du transitoire.
	if (bProfilGPU)
	{
		if (UWorld* const W = GetWorld())
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]   ventilation GPU demandee -- la capture arrive ")
				TEXT("a la trame suivante, on laisse %.0f s au vidage"),
				AttenteProfilS);
			GEngine->Exec(W, TEXT("ProfileGPU"));
			bProfilDemande = true;
			DebutAttenteProfil = FPlatformTime::Seconds();
			// ON NE QUITTE PAS MAINTENANT : le Tick s'en chargera une fois le
			// delai ecoule. `bFini` reste vrai, donc plus aucune mesure.
			return;
		}
	}

	if (bQuitterEnsuite)
	{
		if (UWorld* const W = GetWorld())
		{
			UKismetSystemLibrary::QuitGame(W, nullptr,
				EQuitPreference::Quit, false);
		}
	}
}
