// Worldseed - ce qui RAMPE au ras du sol : le sable, et la neige.

#include "Procedural/WorldseedReptation.h"

#include "Procedural/WorldseedApparence.h"
#include "Procedural/WorldseedClimatePreset.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedWeatherState.h"

FWorldseedReptationRules FWorldseedReptationRules::FromRules(const UWorldseedRules& Rules)
{
	FWorldseedReptationRules Out;
	auto Num = [&Rules](const TCHAR* Cle, double Defaut)
	{
		return static_cast<float>(Rules.Num(TEXT("uds"), Cle, Defaut));
	};

	Out.SablePartMin = Num(TEXT("reptationSablePartMin"), 0.7);
	Out.NeigePartMin = Num(TEXT("reptationNeigePartMin"), 0.4);
	return Out;
}

namespace WorldseedReptation
{
	FWorldseedReptation Evaluer(
		EWorldseedBiome BiomeApparent,
		EWorldseedCover Couverture,
		float ZCm, float NiveauMerCm,
		const FWorldseedWeather& Meteo,
		const FWorldseedClimatePresetRules& ReglesMeteo,
		const FWorldseedReptationRules& Regles)
	{
		FWorldseedReptation Out;
		Out.DirectionDeg = Meteo.WindDirectionDeg;

		// --- RIEN NE RAMPE SUR L'EAU ---------------------------------------
		//
		// ⚠ PIEGE QUE LA TABLE DES MATIERES TEND. `Ocean` et `Lake` portent un
		// poids « aride » de 0,55, et `Riviere` 0,45 : ce n'est PAS du sable,
		// c'est la convention de peinture du FOND MARIN. Sans cette garde, le
		// sable courrait sur l'eau -- exactement le defaut que la vegetation a
		// deja rencontre sur ce meme canal.
		//
		// Et l'on teste aussi l'ALTITUDE, parce qu'une cellule peut etre sous le
		// niveau de la mer sans porter de couverture d'eau.
		const bool bEau =
			Couverture == EWorldseedCover::Ocean ||
			Couverture == EWorldseedCover::Lake ||
			Couverture == EWorldseedCover::River ||
			Couverture == EWorldseedCover::SeaIce ||
			ZCm < NiveauMerCm;

		if (bEau)
		{
			return Out;
		}

		// --- L'ARRACHEMENT, PARTAGE AVEC LE VOILE ATMOSPHERIQUE -------------
		//
		// C'est la MEME fonction que celle qui decide de la poussiere en l'air :
		// les deux decrivent la saltation, et ce depot a une regle contre les
		// formules recopiees dans deux fichiers. Une consequence agreable : le
		// sable commence a courir au sol au moment exact ou le voile se leve.
		const float Arrachement = WorldseedWeatherState::Saltation(
			Meteo.WindIntensity, ReglesMeteo);

		if (Arrachement <= 0.0f)
		{
			return Out;
		}

		// --- LA NEIGE D'ABORD, PARCE QU'ELLE EST DESSUS ---------------------
		//
		// LA POUDRERIE EST LE MEME PHENOMENE QUE LA SALTATION. Un « ground
		// blizzard » souleve la neige DEJA AU SOL par le vent, sans qu'il neige
		// -- c'est meme ce qui le distingue d'une chute de neige. On ne regarde
		// donc pas `Meteo.Snow`, qui dit ce qui TOMBE, mais ce qui est POSE.
		//
		// DEUX SOURCES, et il faut les deux : la neige PERMANENTE d'une calotte
		// (qui ne depend pas du temps qu'il fait) et celle que la meteo vient de
		// deposer. On prend la plus grande -- une calotte sous une chute de
		// neige n'est pas deux fois plus enneigee.
		const float NeigePermanente = WorldseedApparence::PartNeige(BiomeApparent);
		const float NeigeMeteo = FMath::Clamp(Meteo.Snow / 10.0f, 0.0f, 1.0f);
		const float PartNeige = FMath::Max(NeigePermanente, NeigeMeteo);

		if (PartNeige >= Regles.NeigePartMin)
		{
			Out.Matiere = EWorldseedMatiereRampante::Neige;
			Out.Part = PartNeige;
			Out.Intensite = PartNeige * Arrachement;
			Out.Teinte = Regles.NeigeTeinte;
			return Out;
		}

		// --- SINON LE SABLE -------------------------------------------------
		//
		// LE CANAL « ARIDE » DES POIDS DE MATIERE dit a quel point le sol est nu
		// et sableux, et il existe deja : desert chaud et plage 1,00, desert
		// froid 0,60, savane 0,55, steppe 0,50, forets 0,00. On ne l'invente
		// pas, on le lit -- c'est celui que le semis de vegetation emploie.
		const float PartSable = WorldseedBiomes::SlotWeights(BiomeApparent).G;

		if (PartSable >= Regles.SablePartMin)
		{
			Out.Matiere = EWorldseedMatiereRampante::Sable;
			Out.Part = PartSable;
			Out.Intensite = PartSable * Arrachement;
			Out.Teinte = Regles.SableTeinte;
		}

		return Out;
	}
}
