// Worldseed - circulation atmospherique dominante.

#include "Procedural/WorldseedWind.h"
#include "Procedural/WorldseedPerlin.h"

namespace WorldseedWind
{
	void Prevailing(float LatitudeDeg, float LatSpanDeg,
		float& OutEast, float& OutNorth)
	{
		// Formule de WorldseedClimate, a l'identique : frontieres a 30 et 60
		// degres d'un demi-span, adoucies sur 7,5 degres.
		const float Half = FMath::Max(LatSpanDeg * 0.5f, 1e-6f);
		const float C1 = Half * (30.0f / 90.0f);
		const float C2 = Half * (60.0f / 90.0f);
		const float TW = Half * (7.5f / 90.0f);

		const float AbsLat = FMath::Abs(LatitudeDeg);
		const float A = WorldseedPerlin::Smoothstep(C1 - TW, C1 + TW, AbsLat);
		const float B = WorldseedPerlin::Smoothstep(C2 - TW, C2 + TW, AbsLat);

		const float Trades = 1.0f - A;
		const float Westerlies = A - B;
		const float Polar = B;

		const float East = -Trades + Westerlies - Polar;
		const float North = FMath::Sign(LatitudeDeg) * 0.35f * East;

		const float Mag = FMath::Max(FMath::Sqrt(East * East + North * North), 1e-6f);
		OutEast = East / Mag;
		OutNorth = North / Mag;
	}

	float PrevailingYawDeg(float LatitudeDeg, float LatSpanDeg)
	{
		float East = 0.0f;
		float North = 0.0f;
		Prevailing(LatitudeDeg, LatSpanDeg, East, North);

		return FMath::UnwindDegrees(FMath::RadiansToDegrees(FMath::Atan2(North, East)));
	}
}
