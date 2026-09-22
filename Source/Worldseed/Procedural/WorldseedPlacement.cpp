// Worldseed - ou poser le joueur : pente, sol plein, recherche de sol plat.

#include "Procedural/WorldseedPlacement.h"

#include "Procedural/WorldseedDensity.h"

namespace WorldseedPlacement
{
	float PenteDeg(const FWorldseedDensity& Champ, double XM, double YM)
	{
		const float HX = Champ.SurfaceHeightM(XM + SondePenteM, YM)
			- Champ.SurfaceHeightM(XM - SondePenteM, YM);
		const float HY = Champ.SurfaceHeightM(XM, YM + SondePenteM)
			- Champ.SurfaceHeightM(XM, YM - SondePenteM);

		const float Pente = FMath::Sqrt(HX * HX + HY * HY)
			/ (2.0f * static_cast<float>(SondePenteM));
		return FMath::RadiansToDegrees(FMath::Atan(Pente));
	}

	bool SolPlein(const FWorldseedDensity& Champ, double XM, double YM,
		double SurfaceM)
	{
		for (double Profondeur = 1.0; Profondeur <= ProfondeurPleinM;
			Profondeur += 2.0)
		{
			if (Champ.At(FVector(XM, YM, SurfaceM - Profondeur)) > 0.0)
			{
				return false;
			}
		}
		return true;
	}

	bool SolPlat(const FWorldseedDensity& Champ, const FVector2D& AutourM,
		float PenteMaxDeg, float EcartAltitudeMaxM, float AltitudeRefM,
		double& OutX, double& OutY, float& OutSurfaceM, float& OutPenteDeg)
	{
		constexpr double PasM = 16.0;
		constexpr int32 Anneaux = 24;

		auto Convient = [&Champ, PenteMaxDeg, EcartAltitudeMaxM, AltitudeRefM]
			(double X, double Y, float& Surface, float& Pente) -> bool
		{
			Surface = Champ.SurfaceHeightM(X, Y);
			if (Surface < 2.0f)
			{
				return false;   // sous la mer, ou tout juste au bord
			}

			// LA BORNE D'ALTITUDE PASSE AVANT LA PENTE, parce qu'elle est
			// beaucoup plus selective et qu'elle coute une soustraction quand
			// l'autre coute quatre echantillonnages du champ.
			if (EcartAltitudeMaxM > 0.0f
				&& FMath::Abs(Surface - AltitudeRefM) > EcartAltitudeMaxM)
			{
				return false;
			}

			Pente = PenteDeg(Champ, X, Y);
			if (Pente > PenteMaxDeg)
			{
				return false;
			}

			return SolPlein(Champ, X, Y, Surface);
		};

		for (int32 Anneau = 0; Anneau <= Anneaux; ++Anneau)
		{
			for (int32 DY = -Anneau; DY <= Anneau; ++DY)
			{
				for (int32 DX = -Anneau; DX <= Anneau; ++DX)
				{
					// Seulement le bord de l'anneau : l'interieur a deja ete vu.
					if (Anneau > 0 && FMath::Abs(DX) != Anneau
						&& FMath::Abs(DY) != Anneau)
					{
						continue;
					}

					const double X = AutourM.X + DX * PasM;
					const double Y = AutourM.Y + DY * PasM;

					float Surface = 0.0f;
					float Pente = 0.0f;
					if (Convient(X, Y, Surface, Pente))
					{
						OutX = X;
						OutY = Y;
						OutSurfaceM = Surface;
						OutPenteDeg = Pente;
						return true;
					}
				}
			}
		}
		return false;
	}
}
