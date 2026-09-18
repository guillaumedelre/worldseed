// Worldseed - suivi et annulation d'une generation en cours.

#include "Procedural/WorldseedJob.h"

FString FWorldseedJob::StageLabel(EWorldseedStage InStage)
{
	switch (InStage)
	{
	case EWorldseedStage::Rules:      return TEXT("Lecture des regles");
	case EWorldseedStage::Tectonics:  return TEXT("Tectonique : plaques et relief");
	case EWorldseedStage::Climate:    return TEXT("Climat : circulation et precipitations");
	case EWorldseedStage::Erosion:    return TEXT("Erosion : incision et versants");
	case EWorldseedStage::Finalizing: return TEXT("Finalisation");
	case EWorldseedStage::Done:       return TEXT("Termine");
	case EWorldseedStage::Cancelled:  return TEXT("Annule");
	case EWorldseedStage::Failed:     return TEXT("Echec");
	default:                          return TEXT("En attente");
	}
}
