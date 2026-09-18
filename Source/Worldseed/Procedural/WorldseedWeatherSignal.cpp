// Worldseed - signal temporel deterministe qui fait varier la meteo.

#include "Procedural/WorldseedWeatherSignal.h"

namespace WorldseedWeatherSignal
{
	namespace
	{
		/** Hash entier -> [0,1). Meme esprit que le bruit du monde : pas de table. */
		float Hash01(uint32 X)
		{
			X ^= X >> 16;
			X *= 0x7feb352du;
			X ^= X >> 15;
			X *= 0x846ca68bu;
			X ^= X >> 16;
			return static_cast<float>(X & 0x00FFFFFFu) / 16777216.0f;
		}

		/** Bruit de valeur 1D, lisse aux raccords. */
		float ValueNoise(float T, uint32 Seed)
		{
			const float Floor = FMath::FloorToFloat(T);
			const int32 Cell = static_cast<int32>(Floor);
			const float Frac = T - Floor;

			const float A = Hash01(static_cast<uint32>(Cell) * 374761393u + Seed);
			const float B = Hash01(static_cast<uint32>(Cell + 1) * 374761393u + Seed);

			return FMath::Lerp(A, B, Frac * Frac * (3.0f - 2.0f * Frac));
		}
	}

	float Storminess(float TimeSeconds, float PeriodS, int32 Seed)
	{
		// TROIS OCTAVES. La lente porte le regime de plusieurs minutes, les
		// rapides donnent les eclaircies et les rafales. Avec une seule, la
		// meteo monterait et descendrait comme une sinusoide — ce qui se
		// reconnait tout de suite comme artificiel.
		const float T = TimeSeconds / FMath::Max(PeriodS, 1.0f);
		const uint32 S = static_cast<uint32>(Seed);

		return FMath::Clamp(
			  0.55f * ValueNoise(T, S)
			+ 0.30f * ValueNoise(T * 2.7f, S + 7919u)
			+ 0.15f * ValueNoise(T * 6.1f, S + 6791u),
			0.0f, 1.0f);
	}

	float Haze(float TimeSeconds, float PeriodS, int32 Seed)
	{
		// Plus lent que l'agitation : une nappe de brouillard tient, quand une
		// averse passe.
		const float T = TimeSeconds / FMath::Max(PeriodS * 1.2f, 1.0f);
		return FMath::Clamp(ValueNoise(T, static_cast<uint32>(Seed) + 104729u), 0.0f, 1.0f);
	}
}
