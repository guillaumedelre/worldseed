// Worldseed - le banc : mesurer le cout du terrain sans outillage externe.

#include "Procedural/WorldseedBanc.h"

#include "Procedural/WorldseedVoxelTerrain.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformMemory.h"
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
	bArme = true;

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

	if (Horloge - DebutMesure >= MesureS)
	{
		Conclure();
	}
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

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   memoire physique utilisee %.2f Go"),
		Mem.UsedPhysical / (1024.0 * 1024.0 * 1024.0));
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   Lecture : le budget est 16,67 ms. La trame est mesuree ")
		TEXT("PAR LE JEU sur son propre DeltaTime, donc a l'abri du bridage de ")
		TEXT("l'editeur en arriere-plan -- piege qui a deja coute une journee de ")
		TEXT("conclusions fausses a ce depot."));

	if (bQuitterEnsuite)
	{
		if (UWorld* const W = GetWorld())
		{
			UKismetSystemLibrary::QuitGame(W, nullptr,
				EQuitPreference::Quit, false);
		}
	}
}
