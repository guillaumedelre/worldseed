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

	float Uniformiser(float Storm)
	{
		// LA FONCTION DE REPARTITION DU SIGNAL, RELEVEE ET NON CALCULEE.
		//
		// Vingt-et-une valeurs, un quantile tous les cinq pour cent, mesurees
		// par `Worldseed.Meteo.LeSignalEstUniforme` sur quatre graines et une
		// annee de jeu chacune. Interpoler entre elles rend la fonction de
		// repartition empirique, donc une loi uniforme en sortie.
		// Releve du 28 septembre 2026 : moyenne 0,5011, ecart-type 0,1616, et un
		// support BORNE a [0,0435 ; 0,9327]. L'ecart-type calcule valait 0,186,
		// soit quinze pour cent de trop -- et surtout la vraie loi n'a pas de
		// queues gaussiennes, ce qui est tout ce qui compte ici : au quantile
		// 0,95 le signal vaut 0,7642 quand la cloche posee le placait a 0,918.
		static const float Quantiles[] = {
			0.0435f, 0.2315f, 0.2855f, 0.3244f, 0.3558f, 0.3832f, 0.4098f,
			0.4342f, 0.4575f, 0.4801f, 0.5027f, 0.5250f, 0.5474f, 0.5707f,
			0.5946f, 0.6194f, 0.6469f, 0.6785f, 0.7152f, 0.7642f, 0.9327f
		};
		constexpr int32 N = UE_ARRAY_COUNT(Quantiles);

		const float X = FMath::Clamp(Storm, Quantiles[0], Quantiles[N - 1]);
		for (int32 K = 1; K < N; ++K)
		{
			if (X <= Quantiles[K])
			{
				const float Large = Quantiles[K] - Quantiles[K - 1];
				const float Part = (Large > 1e-6f) ? (X - Quantiles[K - 1]) / Large : 0.0f;
				return (static_cast<float>(K - 1) + Part) / static_cast<float>(N - 1);
			}
		}
		return 1.0f;
	}

	float Haze(float TimeSeconds, float PeriodS, int32 Seed)
	{
		// Plus lent que l'agitation : une nappe de brouillard tient, quand une
		// averse passe.
		const float T = TimeSeconds / FMath::Max(PeriodS * 1.2f, 1.0f);
		return FMath::Clamp(ValueNoise(T, static_cast<uint32>(Seed) + 104729u), 0.0f, 1.0f);
	}
}
