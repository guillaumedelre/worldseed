// Worldseed - suivi et annulation d'une generation en cours.

#pragma once

#include "CoreMinimal.h"
#include <atomic>

/** Etape courante, pour afficher un libelle utile a cote de la barre. */
enum class EWorldseedStage : uint8
{
	Idle = 0,
	Rules,
	Tectonics,
	Climate,
	Erosion,
	Finalizing,
	Done,
	Cancelled,
	Failed,
};

/**
 * Etat partage entre le thread de jeu et le thread de calcul.
 *
 * Le worker n'a AUCUNE reference vers le widget : il ecrit dans cet objet,
 * detenu par un TSharedPtr thread-safe. Si le joueur quitte le menu pendant un
 * calcul, le widget disparait sans que le worker n'ait rien a savoir — il
 * termine, ou s'arrete sur l'annulation, et l'objet meurt avec la derniere
 * reference.
 *
 * La remise du resultat suit la discipline habituelle : le worker remplit
 * Result PUIS positionne bDone en release ; le thread de jeu lit bDone en
 * acquire AVANT de toucher a Result. Aucun verrou n'est necessaire.
 */
struct WORLDSEED_API FWorldseedJob
{
	/** Avancement dans [0..1]. */
	std::atomic<float> Progress{ 0.0f };

	/** Etape courante (EWorldseedStage). */
	std::atomic<uint8> Stage{ static_cast<uint8>(EWorldseedStage::Idle) };

	/** Demande d'arret, posee par le thread de jeu. */
	std::atomic<bool> bCancelRequested{ false };

	/** Le worker a fini, quelle qu'en soit l'issue. */
	std::atomic<bool> bDone{ false };

	/** Vrai si le calcul avait ete interrompu. */
	std::atomic<bool> bWasCancelled{ false };

	/** Message d'erreur, valide seulement une fois bDone. */
	FString Error;

	/** A appeler depuis le worker aux points d'arret surs. */
	FORCEINLINE bool ShouldStop() const
	{
		return bCancelRequested.load(std::memory_order_relaxed);
	}

	FORCEINLINE void Report(float InProgress, EWorldseedStage InStage)
	{
		Progress.store(FMath::Clamp(InProgress, 0.0f, 1.0f), std::memory_order_relaxed);
		Stage.store(static_cast<uint8>(InStage), std::memory_order_relaxed);
	}

	FORCEINLINE EWorldseedStage GetStage() const
	{
		return static_cast<EWorldseedStage>(Stage.load(std::memory_order_relaxed));
	}

	/** Libelle lisible de l'etape, pour l'interface. */
	static FString StageLabel(EWorldseedStage InStage);
};

using FWorldseedJobPtr = TSharedPtr<FWorldseedJob, ESPMode::ThreadSafe>;

/**
 * Tranche d'avancement attribuee a une etape.
 *
 * Chaque etape raisonne en fraction LOCALE de 0 a 1 sans savoir quelle part du
 * total elle represente : c'est l'appelant qui fixe sa tranche. Sans cela,
 * ajouter une etape obligerait a rerepartir tous les pourcentages a la main
 * dans chaque module.
 */
struct WORLDSEED_API FWorldseedProgressScope
{
	FWorldseedJob* Job = nullptr;
	float Base = 0.0f;
	float Span = 1.0f;
	EWorldseedStage Stage = EWorldseedStage::Idle;

	/** Signale l'avancement local et rend vrai s'il faut s'arreter. */
	FORCEINLINE bool Step(float LocalFraction) const
	{
		if (!Job)
		{
			return false;
		}
		Job->Report(Base + Span * FMath::Clamp(LocalFraction, 0.0f, 1.0f), Stage);
		return Job->ShouldStop();
	}

	FORCEINLINE bool ShouldStop() const
	{
		return Job && Job->ShouldStop();
	}
};
