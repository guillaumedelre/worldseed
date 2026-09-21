// Worldseed - la calotte glaciaire a une EPAISSEUR.

#include "Procedural/WorldseedIce.h"

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedRules.h"

namespace WorldseedIce
{
	FRules FRules::FromRules(const UWorldseedRules& Rules, const FWorldseedGeometry& Geo)
	{
		FRules Out;

		// La section porte le meme nom que la passe, comme partout ailleurs.
		Out.MaxThicknessM = static_cast<float>(
			Rules.Num(TEXT("glace"), TEXT("epaisseurMaxM"), 0.0))
			* WorldseedVerticalScale(Rules, Geo.HeightM);

		Out.DomeRangeKm = static_cast<float>(
			Rules.Num(TEXT("glace"), TEXT("porteeDomeKm"), 1.0));

		return Out;
	}

	float Apply(TArray<float>& ElevationM, const TArray<uint8>& BiomeIndex,
		const FWorldseedGeometry& Geo, const FRules& Rules)
	{
		const int32 Count = ElevationM.Num();
		if (Rules.MaxThicknessM <= 0.0f || BiomeIndex.Num() != Count)
		{
			return 0.0f;
		}

		const int32 NX = Geo.NX;
		const int32 NY = Geo.NY;

		// Le masque de la calotte. La transformee rend, pour chaque cellule NON
		// NULLE, la distance a la plus proche cellule NULLE : en la nourrissant
		// avec la calotte, on obtient directement la distance au BORD de la
		// calotte, qui est ce que le profil du dome demande.
		TArray<uint8> Masque;
		Masque.SetNumZeroed(Count);
		int32 Cellules = 0;
		for (int32 I = 0; I < Count; ++I)
		{
			if (static_cast<EWorldseedBiome>(BiomeIndex[I]) == EWorldseedBiome::IceCap)
			{
				Masque[I] = 1;
				++Cellules;
			}
		}
		if (Cellules == 0)
		{
			return 0.0f;
		}

		TArray<float> DistPx;
		WorldseedGrid::DistanceTransform(Masque, NX, NY, DistPx);

		// La portee du dome se donne en KILOMETRES et se compare a une distance
		// en PIXELS : sans cette conversion, le profil dependrait de la
		// resolution de la grille, et le meme monde n'aurait pas la meme
		// calotte en 1024 et en 2048.
		const float MetresParPixel = FMath::Max(Geo.MetersPerPixel(), 1e-3f);
		const float PorteePx = FMath::Max(
			Rules.DomeRangeKm * 1000.0f / MetresParPixel, 1.0f);

		float EpaisseurMax = 0.0f;
		for (int32 I = 0; I < Count; ++I)
		{
			if (Masque[I] == 0)
			{
				continue;
			}

			// PROFIL DE VIALOV : l'epaisseur croit comme la RACINE de la
			// distance au bord. Plafonne a un, sinon le centre d'une grande
			// calotte gonflerait sans fin.
			const float T = FMath::Clamp(DistPx[I] / PorteePx, 0.0f, 1.0f);
			const float Epaisseur = Rules.MaxThicknessM * FMath::Sqrt(T);

			ElevationM[I] += Epaisseur;
			EpaisseurMax = FMath::Max(EpaisseurMax, Epaisseur);
		}

		return EpaisseurMax;
	}
}
