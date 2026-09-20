// Worldseed - suivi et annulation d'une generation en cours.

#include "Procedural/WorldseedJob.h"

FString FWorldseedJob::StageLabel(EWorldseedStage InStage)
{
	switch (InStage)
	{
	case EWorldseedStage::Rules:        return TEXT("Lecture des regles");
	case EWorldseedStage::Tectonics:    return TEXT("Tectonique : plaques, sutures et relief");
	case EWorldseedStage::Lithology:    return TEXT("Lithologie : de quelle roche est le sous-sol");
	case EWorldseedStage::Climate:      return TEXT("Climat : circulation, pluies et temperatures");
	case EWorldseedStage::Erosion:      return TEXT("Erosion : soulevement, incision et bancs");
	case EWorldseedStage::Relief:       return TEXT("Relief : littoral, tables, canyons et corniches");
	case EWorldseedStage::ClimateFinal: return TEXT("Climat : seconde passe, sur le relief final");
	case EWorldseedStage::Biomes:       return TEXT("Biomes : classement de Whittaker");
	case EWorldseedStage::Caves:        return TEXT("Cavites : reseaux, gouffres, dolines et arches");
	case EWorldseedStage::Finalizing:   return TEXT("Finalisation");
	case EWorldseedStage::Done:       return TEXT("Termine");
	case EWorldseedStage::Cancelled:  return TEXT("Annule");
	case EWorldseedStage::Failed:     return TEXT("Echec");
	default:                          return TEXT("En attente");
	}
}
