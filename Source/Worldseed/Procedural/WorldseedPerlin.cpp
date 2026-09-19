// Worldseed - portage fidele de Tools/WorldGen/worldgen/noise.py.

#include "Procedural/WorldseedPerlin.h"
#include "Async/ParallelFor.h"

namespace WorldseedPerlin
{
	namespace
	{
		/** 2 * pi / 2^32, comme _TAU_OVER_2P32 cote numpy. */
		const float TauOver2P32 = static_cast<float>(2.0 * PI / 4294967296.0);

		/** Quintique de Perlin : 6t^5 - 15t^4 + 10t^3. */
		FORCEINLINE float Fade(float T)
		{
			return T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f);
		}

		/** Coordonnee de la grille unite pour un index, ou valeur fournie. */
		FORCEINLINE float GridValue(const TArray<float>* Grid, int32 Index,
			int32 Fallback, float InvN)
		{
			return Grid ? (*Grid)[Index] : static_cast<float>(Fallback) * InvN;
		}
	}

	float HashAngle(int32 IX, int32 IY, int32 Seed)
	{
		// Debordements volontaires : numpy travaille en uint32 avec
		// errstate(over="ignore"), le C++ non signe fait exactement pareil.
		uint32 H = static_cast<uint32>(IX) * 374761393u
			+ static_cast<uint32>(IY) * 668265263u;
		H = H + static_cast<uint32>(Seed);
		H ^= H >> 13;
		H = H * 1274126177u;
		H ^= H >> 16;
		H = H * 2246822519u;
		H ^= H >> 13;

		return static_cast<float>(H) * TauOver2P32;
	}

	float Perlin(float X, float Y, int32 Seed)
	{
		const float X0 = FMath::FloorToFloat(X);
		const float Y0 = FMath::FloorToFloat(Y);
		const float FX = X - X0;
		const float FY = Y - Y0;

		const int32 IX0 = static_cast<int32>(X0);
		const int32 IY0 = static_cast<int32>(Y0);
		const int32 IX1 = IX0 + 1;
		const int32 IY1 = IY0 + 1;

		auto Corner = [Seed](int32 IX, int32 IY, float DX, float DY) -> float
		{
			const float Angle = HashAngle(IX, IY, Seed);
			return FMath::Cos(Angle) * DX + FMath::Sin(Angle) * DY;
		};

		const float N00 = Corner(IX0, IY0, FX, FY);
		const float N10 = Corner(IX1, IY0, FX - 1.0f, FY);
		const float N01 = Corner(IX0, IY1, FX, FY - 1.0f);
		const float N11 = Corner(IX1, IY1, FX - 1.0f, FY - 1.0f);

		const float U = Fade(FX);
		const float V = Fade(FY);
		const float NX0 = N00 + U * (N10 - N00);
		const float NX1 = N01 + U * (N11 - N01);

		return (NX0 + V * (NX1 - NX0)) * 1.4142135f;
	}

	void FBM(TArray<float>& Out, int32 NX, int32 NY, float Frequency, int32 Octaves,
		int32 Seed, float Lacunarity, float Gain,
		const TArray<float>* GridX, const TArray<float>* GridY)
	{
		Out.SetNumZeroed(NX * NY);
		if (NX < 2 || NY < 2 || Octaves < 1)
		{
			return;
		}

		// Les DEUX axes sont normalises par (NY - 1) : cellules carrees dans
		// l'espace du bruit, X balaie 0..2 et Y balaie 0..1.
		const float InvN = 1.0f / static_cast<float>(NY - 1);
		float Amplitude = 1.0f;
		float Normalisation = 0.0f;
		float Freq = Frequency;

		for (int32 Octave = 0; Octave < Octaves; ++Octave)
		{
			const int32 OctaveSeed = Seed + Octave * 7919;
			const float Amp = Amplitude;
			const float F = Freq;

			// Chaque cellule est independante : la parallelisation ne change
			// pas le resultat, le determinisme est preserve.
			ParallelFor(NY, [&Out, GridX, GridY, NX, InvN, OctaveSeed, Amp, F](int32 J)
			{
				for (int32 I = 0; I < NX; ++I)
				{
					const int32 Index = J * NX + I;
					const float GX = GridValue(GridX, Index, I, InvN);
					const float GY = GridValue(GridY, Index, J, InvN);
					Out[Index] += Amp * Perlin(GX * F, GY * F, OctaveSeed);
				}
			});

			Normalisation += Amplitude;
			Amplitude *= Gain;
			Freq *= Lacunarity;
		}

		const float InvNorm = 1.0f / FMath::Max(Normalisation, 1e-6f);
		for (float& Value : Out)
		{
			Value *= InvNorm;
		}
	}

	void Ridged(TArray<float>& Out, int32 NX, int32 NY, float Frequency, int32 Octaves,
		int32 Seed, float Lacunarity, float Gain)
	{
		Out.SetNumZeroed(NX * NY);
		if (NX < 2 || NY < 2 || Octaves < 1)
		{
			return;
		}

		const float InvN = 1.0f / static_cast<float>(NY - 1);
		float Amplitude = 1.0f;
		float Normalisation = 0.0f;
		float Freq = Frequency;

		for (int32 Octave = 0; Octave < Octaves; ++Octave)
		{
			const int32 OctaveSeed = Seed + Octave * 6791;
			const float Amp = Amplitude;
			const float F = Freq;

			ParallelFor(NY, [&Out, NX, InvN, OctaveSeed, Amp, F](int32 J)
			{
				const float GY = static_cast<float>(J) * InvN;
				for (int32 I = 0; I < NX; ++I)
				{
					const float GX = static_cast<float>(I) * InvN;
					const float Value = 1.0f - FMath::Abs(Perlin(GX * F, GY * F, OctaveSeed));
					Out[J * NX + I] += Amp * (Value * Value);
				}
			});

			Normalisation += Amplitude;
			Amplitude *= Gain;
			Freq *= Lacunarity;
		}

		const float InvNorm = 1.0f / FMath::Max(Normalisation, 1e-6f);
		for (float& Value : Out)
		{
			Value *= InvNorm;
		}
	}

	void DomainWarpedFBM(TArray<float>& Out, int32 NX, int32 NY, float Frequency,
		int32 Octaves, int32 Seed, float WarpStrength, float WarpFrequency,
		float Lacunarity, float Gain)
	{
		TArray<float> WarpX;
		TArray<float> WarpY;
		FBM(WarpX, NX, NY, WarpFrequency, 4, Seed + 104729, Lacunarity, Gain);
		FBM(WarpY, NX, NY, WarpFrequency, 4, Seed + 104743, Lacunarity, Gain);

		// On construit les grilles deplacees, exactement comme le Python :
		//   fbm(..., grid=(gy + s * wy, gx + s * wx))
		const float InvN = 1.0f / static_cast<float>(FMath::Max(NY - 1, 1));
		TArray<float> GridX;
		TArray<float> GridY;
		GridX.SetNumUninitialized(NX * NY);
		GridY.SetNumUninitialized(NX * NY);

		ParallelFor(NY, [&](int32 J)
		{
			const float BaseY = static_cast<float>(J) * InvN;
			for (int32 I = 0; I < NX; ++I)
			{
				const int32 Index = J * NX + I;
				GridX[Index] = static_cast<float>(I) * InvN + WarpStrength * WarpX[Index];
				GridY[Index] = BaseY + WarpStrength * WarpY[Index];
			}
		});

		FBM(Out, NX, NY, Frequency, Octaves, Seed, Lacunarity, Gain, &GridX, &GridY);
	}

	// ------------------------------------------------------------------------
	//                          bruits spheriques
	// ------------------------------------------------------------------------

	namespace
	{
		/** Meme melange que le hash 2D, etendu a un troisieme axe. */
		FORCEINLINE uint32 Hash3(int32 IX, int32 IY, int32 IZ, int32 Seed)
		{
			uint32 H = static_cast<uint32>(IX) * 374761393u
				+ static_cast<uint32>(IY) * 668265263u
				+ static_cast<uint32>(IZ) * 3266489917u;
			H = H + static_cast<uint32>(Seed);
			H ^= H >> 13;
			H = H * 1274126177u;
			H ^= H >> 16;
			H = H * 2246822519u;
			H ^= H >> 13;
			return H;
		}

		/**
		 * Gradient unitaire UNIFORME sur la sphere.
		 *
		 * z tire uniformement dans [-1..1] puis un azimut : c'est la
		 * construction d'Archimede. Tirer deux angles uniformes concentrerait
		 * les gradients aux poles et le bruit y montrerait une direction
		 * privilegiee.
		 */
		FVector HashGradient3D(int32 IX, int32 IY, int32 IZ, int32 Seed)
		{
			uint32 H = Hash3(IX, IY, IZ, Seed);
			const float U1 = static_cast<float>(H) * (1.0f / 4294967296.0f);

			// Second tirage independant : on remixe le meme hash.
			H ^= H >> 15;
			H = H * 2246822519u;
			H ^= H >> 13;
			const float U2 = static_cast<float>(H) * (1.0f / 4294967296.0f);

			const float Z = 2.0f * U1 - 1.0f;
			const float Phi = 2.0f * PI * U2;
			const float R = FMath::Sqrt(FMath::Max(0.0f, 1.0f - Z * Z));

			return FVector(R * FMath::Cos(Phi), R * FMath::Sin(Phi), Z);
		}

		/**
		 * Facteur d'echelle : une frequence F doit donner F cycles d'un pole a
		 * l'autre, comme en 2D. L'arc pole a pole vaut PI sur la sphere unite,
		 * donc on echantillonne le bruit a P * F / PI.
		 */
		FORCEINLINE float SphereScale(float Frequency)
		{
			return Frequency / PI;
		}
	}

	float Perlin3D(float X, float Y, float Z, int32 Seed)
	{
		const float X0 = FMath::FloorToFloat(X);
		const float Y0 = FMath::FloorToFloat(Y);
		const float Z0 = FMath::FloorToFloat(Z);

		const float FX = X - X0;
		const float FY = Y - Y0;
		const float FZ = Z - Z0;

		const int32 IX = static_cast<int32>(X0);
		const int32 IY = static_cast<int32>(Y0);
		const int32 IZ = static_cast<int32>(Z0);

		auto Corner = [&](int32 DX, int32 DY, int32 DZ) -> float
		{
			const FVector G = HashGradient3D(IX + DX, IY + DY, IZ + DZ, Seed);
			return static_cast<float>(
				G.X * (FX - DX) + G.Y * (FY - DY) + G.Z * (FZ - DZ));
		};

		const float U = Fade(FX);
		const float V = Fade(FY);
		const float W = Fade(FZ);

		const float X00 = FMath::Lerp(Corner(0, 0, 0), Corner(1, 0, 0), U);
		const float X10 = FMath::Lerp(Corner(0, 1, 0), Corner(1, 1, 0), U);
		const float X01 = FMath::Lerp(Corner(0, 0, 1), Corner(1, 0, 1), U);
		const float X11 = FMath::Lerp(Corner(0, 1, 1), Corner(1, 1, 1), U);

		const float Y0V = FMath::Lerp(X00, X10, V);
		const float Y1V = FMath::Lerp(X01, X11, V);

		// Meme normalisation que la version 2D : le bruit par gradients sort
		// naturellement dans [-0,7..0,7] environ.
		return FMath::Lerp(Y0V, Y1V, W) * 1.4142135f;
	}

	namespace
	{
		/** Somme des amplitudes, pour ramener une fractale dans sa plage. */
		float OctaveNormalisation(int32 Octaves, float Gain)
		{
			float Somme = 0.0f;
			float A = 1.0f;
			for (int32 O = 0; O < Octaves; ++O)
			{
				Somme += A;
				A *= Gain;
			}
			return 1.0f / FMath::Max(Somme, 1e-6f);
		}
	}

	void Worley3D(float X, float Y, float Z, int32 Seed, float& OutF1, float& OutF2)
	{
		const int32 IX = FMath::FloorToInt(X);
		const int32 IY = FMath::FloorToInt(Y);
		const int32 IZ = FMath::FloorToInt(Z);

		float F1 = BIG_NUMBER;
		float F2 = BIG_NUMBER;

		// LES VINGT-SEPT VOISINES, ET PAS SEULEMENT LA CELLULE COURANTE : le
		// germe le plus proche d'un point situe pres d'un bord est dans la
		// cellule d'a cote. Se limiter a la cellule rendrait une grille
		// cubique, pas un diagramme de Voronoi.
		for (int32 DZ = -1; DZ <= 1; ++DZ)
		{
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					const int32 CX = IX + DX;
					const int32 CY = IY + DY;
					const int32 CZ = IZ + DZ;

					// Trois tirages decorreles par remixage du meme hachage,
					// exactement comme le fait HashGradient3D pour ses deux.
					uint32 H = Hash3(CX, CY, CZ, Seed);
					const float U1 = static_cast<float>(H) * (1.0f / 4294967296.0f);
					H ^= H >> 15; H = H * 2246822519u; H ^= H >> 13;
					const float U2 = static_cast<float>(H) * (1.0f / 4294967296.0f);
					H ^= H >> 15; H = H * 3266489917u; H ^= H >> 16;
					const float U3 = static_cast<float>(H) * (1.0f / 4294967296.0f);

					const float PX = static_cast<float>(CX) + U1 - X;
					const float PY = static_cast<float>(CY) + U2 - Y;
					const float PZ = static_cast<float>(CZ) + U3 - Z;

					const float D2 = PX * PX + PY * PY + PZ * PZ;
					if (D2 < F1) { F2 = F1; F1 = D2; }
					else if (D2 < F2) { F2 = D2; }
				}
			}
		}

		// Comparaison au carre dans la boucle, racine une seule fois : la
		// racine carree est la seule operation chere ici, et l'ordre des
		// distances est le meme que celui de leurs carres.
		OutF1 = FMath::Sqrt(F1);
		OutF2 = FMath::Sqrt(F2);
	}

	float Fbm3D(float X, float Y, float Z, float Frequency, int32 Octaves,
		int32 Seed, float Lacunarity, float Gain)
	{
		if (Octaves < 1)
		{
			return 0.0f;
		}

		float Amplitude = 1.0f;
		float Scale = Frequency;
		float Sum = 0.0f;

		for (int32 Octave = 0; Octave < Octaves; ++Octave)
		{
			// Meme decalage de graine par octave que les versions spheriques :
			// deux octaves qui partagent leur graine se superposent au lieu de
			// s'ajouter, et le relief y gagne des marches au lieu du grain.
			Sum += Amplitude * Perlin3D(X * Scale, Y * Scale, Z * Scale,
				Seed + Octave * 7919);

			Amplitude *= Gain;
			Scale *= Lacunarity;
		}

		return Sum * OctaveNormalisation(Octaves, Gain);
	}

	float Ridged3D(float X, float Y, float Z, float Frequency, int32 Octaves,
		int32 Seed, float Lacunarity, float Gain)
	{
		if (Octaves < 1)
		{
			return 0.0f;
		}

		float Amplitude = 1.0f;
		float Scale = Frequency;
		float Sum = 0.0f;

		for (int32 Octave = 0; Octave < Octaves; ++Octave)
		{
			const float Value = 1.0f - FMath::Abs(Perlin3D(
				X * Scale, Y * Scale, Z * Scale, Seed + Octave * 6791));

			// Le carre resserre les cretes : sans lui elles s'etalent et les
			// galeries deviennent des cavernes molles.
			Sum += Amplitude * (Value * Value);
			Amplitude *= Gain;
			Scale *= Lacunarity;
		}

		return Sum * OctaveNormalisation(Octaves, Gain);
	}

	FVector SpherePoint(const FWorldseedGeometry& Geo, int32 I, int32 J)
	{
		const float LonRad = 2.0f * PI * static_cast<float>(I)
			/ static_cast<float>(FMath::Max(Geo.NX, 1));
		const float LatRad = FMath::DegreesToRadians(Geo.LatitudeDegForRow(J));

		const float CosLat = FMath::Cos(LatRad);
		return FVector(CosLat * FMath::Cos(LonRad),
			CosLat * FMath::Sin(LonRad),
			FMath::Sin(LatRad));
	}

	void FBMSphere(TArray<float>& Out, const FWorldseedGeometry& Geo,
		float Frequency, int32 Octaves, int32 Seed, float Lacunarity, float Gain)
	{
		const int32 NX = Geo.NX;
		const int32 NY = Geo.NY;
		Out.SetNumZeroed(NX * NY);
		if (NX < 2 || NY < 2 || Octaves < 1)
		{
			return;
		}

		float Normalisation = 0.0f;
		{
			float A = 1.0f;
			for (int32 O = 0; O < Octaves; ++O) { Normalisation += A; A *= Gain; }
		}
		const float InvNorm = 1.0f / FMath::Max(Normalisation, 1e-6f);

		ParallelFor(NY, [&](int32 J)
		{
			for (int32 I = 0; I < NX; ++I)
			{
				const FVector P = SpherePoint(Geo, I, J);

				float Amplitude = 1.0f;
				float Scale = SphereScale(Frequency);
				float Sum = 0.0f;

				for (int32 Octave = 0; Octave < Octaves; ++Octave)
				{
					Sum += Amplitude * Perlin3D(
						static_cast<float>(P.X) * Scale,
						static_cast<float>(P.Y) * Scale,
						static_cast<float>(P.Z) * Scale,
						Seed + Octave * 7919);

					Amplitude *= Gain;
					Scale *= Lacunarity;
				}

				Out[J * NX + I] = Sum * InvNorm;
			}
		});
	}

	void RidgedSphere(TArray<float>& Out, const FWorldseedGeometry& Geo,
		float Frequency, int32 Octaves, int32 Seed, float Lacunarity, float Gain)
	{
		const int32 NX = Geo.NX;
		const int32 NY = Geo.NY;
		Out.SetNumZeroed(NX * NY);
		if (NX < 2 || NY < 2 || Octaves < 1)
		{
			return;
		}

		float Normalisation = 0.0f;
		{
			float A = 1.0f;
			for (int32 O = 0; O < Octaves; ++O) { Normalisation += A; A *= Gain; }
		}
		const float InvNorm = 1.0f / FMath::Max(Normalisation, 1e-6f);

		ParallelFor(NY, [&](int32 J)
		{
			for (int32 I = 0; I < NX; ++I)
			{
				const FVector P = SpherePoint(Geo, I, J);

				float Amplitude = 1.0f;
				float Scale = SphereScale(Frequency);
				float Sum = 0.0f;

				for (int32 Octave = 0; Octave < Octaves; ++Octave)
				{
					const float Value = 1.0f - FMath::Abs(Perlin3D(
						static_cast<float>(P.X) * Scale,
						static_cast<float>(P.Y) * Scale,
						static_cast<float>(P.Z) * Scale,
						Seed + Octave * 6791));

					Sum += Amplitude * (Value * Value);
					Amplitude *= Gain;
					Scale *= Lacunarity;
				}

				Out[J * NX + I] = Sum * InvNorm;
			}
		});
	}

	void DomainWarpedFBMSphere(TArray<float>& Out, const FWorldseedGeometry& Geo,
		float Frequency, int32 Octaves, int32 Seed, float WarpStrength,
		float WarpFrequency, float Lacunarity, float Gain)
	{
		const int32 NX = Geo.NX;
		const int32 NY = Geo.NY;
		Out.SetNumZeroed(NX * NY);
		if (NX < 2 || NY < 2 || Octaves < 1)
		{
			return;
		}

		float Normalisation = 0.0f;
		{
			float A = 1.0f;
			for (int32 O = 0; O < Octaves; ++O) { Normalisation += A; A *= Gain; }
		}
		const float InvNorm = 1.0f / FMath::Max(Normalisation, 1e-6f);

		const float WarpScale = SphereScale(WarpFrequency);

		ParallelFor(NY, [&](int32 J)
		{
			for (int32 I = 0; I < NX; ++I)
			{
				const FVector P = SpherePoint(Geo, I, J);
				const float WX = static_cast<float>(P.X) * WarpScale;
				const float WY = static_cast<float>(P.Y) * WarpScale;
				const float WZ = static_cast<float>(P.Z) * WarpScale;

				// Deplacement en 3D : trois champs independants, un par axe.
				// Le deplacement se fait DANS L'ESPACE DE LA SPHERE, donc il
				// reste continu au meridien comme partout ailleurs.
				const FVector Offset(
					Perlin3D(WX, WY, WZ, Seed + 104729),
					Perlin3D(WX, WY, WZ, Seed + 104743),
					Perlin3D(WX, WY, WZ, Seed + 104759));

				const FVector Q = P + Offset * WarpStrength;

				float Amplitude = 1.0f;
				float Scale = SphereScale(Frequency);
				float Sum = 0.0f;

				for (int32 Octave = 0; Octave < Octaves; ++Octave)
				{
					Sum += Amplitude * Perlin3D(
						static_cast<float>(Q.X) * Scale,
						static_cast<float>(Q.Y) * Scale,
						static_cast<float>(Q.Z) * Scale,
						Seed + Octave * 7919);

					Amplitude *= Gain;
					Scale *= Lacunarity;
				}

				Out[J * NX + I] = Sum * InvNorm;
			}
		});
	}

	float Smoothstep(float Edge0, float Edge1, float X)
	{
		if (FMath::Abs(Edge1 - Edge0) < 1e-12f)
		{
			return (X >= Edge1) ? 1.0f : 0.0f;
		}
		const float T = FMath::Clamp((X - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	void Normalize01(TArray<float>& InOut)
	{
		if (InOut.Num() == 0)
		{
			return;
		}

		float Lo = InOut[0];
		float Hi = InOut[0];
		for (const float Value : InOut)
		{
			Lo = FMath::Min(Lo, Value);
			Hi = FMath::Max(Hi, Value);
		}

		if (Hi - Lo < 1e-12f)
		{
			for (float& Value : InOut)
			{
				Value = 0.5f;
			}
			return;
		}

		const float InvRange = 1.0f / (Hi - Lo);
		for (float& Value : InOut)
		{
			Value = (Value - Lo) * InvRange;
		}
	}
}
