// Worldseed - les champs CONTINUS du sol : humidite et ensoleillement.

#include "Procedural/WorldseedFields.h"

#include "Procedural/WorldseedFlow.h"
#include "Procedural/WorldseedGrid.h"

#include "Async/ParallelFor.h"

namespace
{
	const TCHAR* SOL = TEXT("ground");

	/** Ramene une grille sur [0..1] par ses centiles, bords ecretes. */
	void NormaliserParCentiles(TArray<float>& Field, float Basse, float Haute)
	{
		if (Field.Num() == 0)
		{
			return;
		}

		// PAR CENTILES ET NON PAR MIN/MAX. Une seule cellule aberrante — le fond
		// d'un talweg qui draine un quart du continent — ecraserait tout le reste
		// vers zero. L'erosion de ce depot normalise deja ainsi, et c'est ce qui
		// rend son resultat independant de la resolution.
		TArray<float> Trie = Field;
		const float Lo = WorldseedGrid::Quantile(Trie, Basse);
		const float Hi = WorldseedGrid::Quantile(Trie, Haute);
		const float Span = FMath::Max(Hi - Lo, 1e-6f);

		for (float& V : Field)
		{
			V = FMath::Clamp((V - Lo) / Span, 0.0f, 1.0f);
		}
	}
}

FWorldseedGroundRules FWorldseedGroundRules::FromRules(const UWorldseedRules& Rules)
{
	FWorldseedGroundRules Out;
	Out.SoilMoistureRefMm = static_cast<float>(
		Rules.Num(SOL, TEXT("soilMoistureRefMm"), 1200.0));
	Out.SoilWetnessWeight = static_cast<float>(
		Rules.Num(SOL, TEXT("soilWetnessWeight"), 0.40));
	return Out;
}

void WorldseedFields::Compute(const FWorldseedGeometry& Geometry,
	const TArray<float>& ElevationM, const TArray<float>& PrecipMm,
	const FWorldseedGroundRules& Rules, FWorldseedGroundFields& Out)
{
	const double StartTime = FPlatformTime::Seconds();

	const int32 NX = Geometry.NX;
	const int32 NY = Geometry.NY;
	const int32 Count = Geometry.CellCount();

	Out.Reset();
	if (ElevationM.Num() != Count || Count <= 0)
	{
		return;
	}

	const float SpacingM = FMath::Max(Geometry.MetersPerPixel(), 1e-3f);
	const bool bHasPrecip = (PrecipMm.Num() == Count);

	// --- humidite du sol ------------------------------------------------------
	//
	// INDICE D'HUMIDITE TOPOGRAPHIQUE, Beven et Kirkby 1979 : ln(a / tan beta),
	// ou a est la surface drainee en amont et beta la pente locale. C'est LA
	// grandeur standard pour dire ou l'eau s'accumule dans un versant, et elle
	// explique ce que ni la pluie ni la pente ne disent seules : un fond de
	// vallon est humide sous un climat sec, une crete est seche sous la pluie.
	//
	// L'ACCUMULATION EST PONDEREE PAR LA PLUIE, pas par le simple comptage de
	// cellules : ce qui arrive dans un talweg, c'est de l'eau, et deux bassins
	// de meme surface sous des pluies differentes n'en fournissent pas autant.
	{
		TArray<float> Weight;
		Weight.SetNumUninitialized(Count);
		if (bHasPrecip)
		{
			for (int32 I = 0; I < Count; ++I)
			{
				Weight[I] = FMath::Max(PrecipMm[I], 0.0f);
			}
		}
		else
		{
			for (int32 I = 0; I < Count; ++I) { Weight[I] = 1.0f; }
		}

		// LE RELIEF DE SORTIE, JAMAIS CELUI DE LA SIMULATION. C'est la lecon la
		// plus chere de ce depot : le detail fractal est ajoute APRES l'erosion,
		// et tout ce qui est cale sur le relief d'avant se retrouve tantot
		// enterre, tantot suspendu. L'eau, elle, coule sur le terrain qu'on voit.
		FWorldseedFlow Flow;
		WorldseedFlow::Compute(ElevationM, Weight, NX, NY, 0.0f, 1e-4f, Flow);

		TArray<float> Pente;
		WorldseedFlow::SlopeToReceiver(Flow.FilledM, NX, NY, SpacingM, Pente);

		Out.SoilMoisture01.SetNumUninitialized(Count);

		const bool bHasAccum = (Flow.Accumulation.Num() == Count);
		const bool bHasPente = (Pente.Num() == Count);

		TArray<float> Twi;
		Twi.SetNumUninitialized(Count);
		ParallelFor(Count, [&](int32 I)
		{
			const float A = bHasAccum ? FMath::Max(Flow.Accumulation[I], 0.0f) : 0.0f;
			// La pente plancher evite la division par zero d'un plat parfait, et
			// vaut un demi-metre de denivele par cellule : au-dela, l'indice
			// dirait qu'un plateau draine l'infini.
			const float TanBeta = bHasPente ? FMath::Max(Pente[I], 1e-3f) : 1e-3f;
			Twi[I] = FMath::Loge((A + 1.0f) / TanBeta);
		});
		NormaliserParCentiles(Twi, 0.02f, 0.98f);

		const float W = FMath::Clamp(Rules.SoilWetnessWeight, 0.0f, 1.0f);
		const float RefMm = FMath::Max(Rules.SoilMoistureRefMm, 1.0f);

		ParallelFor(Count, [&](int32 I)
		{
			const float Pluie01 = bHasPrecip
				? FMath::Clamp(PrecipMm[I] / RefMm, 0.0f, 1.0f)
				: 0.5f;

			// UN MELANGE, PAS UNE SOMME. Additionner ferait deborder un talweg
			// deja arrose et saturerait tout un versant humide a 1,0 ; le
			// melange garde la pluie comme plafond et laisse la topographie
			// deplacer l'humidite A L'INTERIEUR de ce que le climat autorise.
			Out.SoilMoisture01[I] = FMath::Clamp(
				Pluie01 * (1.0f - W) + Pluie01 * Twi[I] * W * 2.0f, 0.0f, 1.0f);
		});
	}

	// --- ensoleillement, l'adret et l'ubac ------------------------------------
	//
	// Le soleil MOYEN d'un lieu, sur l'annee, est a l'elevation 90 - |latitude|
	// et pointe vers l'equateur. Son vecteur unitaire se reduit donc a
	// (0, -sin(lat), cos(lat)) dans un repere ou +Y est le nord : au sud dans
	// l'hemisphere nord, au nord dans l'autre, et au zenith a l'equateur.
	//
	// ON RAPPORTE AU TERRAIN PLAT DE LA MEME LATITUDE. Ce qu'on veut, c'est la
	// difference entre deux versants VOISINS, pas le fait qu'il y a plus de
	// soleil au Sahara qu'au Groenland : ca, la temperature le dit deja. D'ou
	// 0,5 sur du plat, plus en adret, moins en ubac.
	{
		TArray<float> DY;
		TArray<float> DX;
		WorldseedGrid::Gradient(ElevationM, NX, NY, SpacingM, DY, DX);

		Out.SunExposure01.SetNumUninitialized(Count);

		ParallelFor(NY, [&](int32 Row)
		{
			const float LatRad = FMath::DegreesToRadians(Geometry.LatitudeDegForRow(Row));
			const float SunNorth = -FMath::Sin(LatRad);
			const float SunUp = FMath::Cos(LatRad);

			// Le plat de reference s'annule au pole, ou le soleil rase le sol.
			// On le borne pour ne pas diviser par zero et ne pas donner a une
			// pente polaire un rapport absurde.
			const float PlatRef = FMath::Max(SunUp, 0.05f);

			for (int32 Col = 0; Col < NX; ++Col)
			{
				const int32 I = Row * NX + Col;

				// Normale du terrain : (-dZ/dx, -dZ/dy, 1) normalisee.
				const float NxComp = -DX[I];
				const float NyComp = -DY[I];
				const float Norme = FMath::Sqrt(NxComp * NxComp + NyComp * NyComp + 1.0f);

				const float Cos = (NyComp * SunNorth + SunUp) / Norme;
				Out.SunExposure01[I] = FMath::Clamp(0.5f * Cos / PlatRef, 0.0f, 1.0f);
			}
		});
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] champs du sol : humidite et ensoleillement  (%.0f ms)"),
		(FPlatformTime::Seconds() - StartTime) * 1000.0);
}
