// Worldseed - le climat.
//
// L'ORIGINAL PYTHON A ETE SUPPRIME : ce fichier est la SEULE
// implementation. Il ne faut plus chercher de reference ailleurs, ni supposer
// qu'un autre fichier dit la meme chose autrement.

#include "Procedural/WorldseedClimate.h"
#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedPerlin.h"

#include "Async/ParallelFor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace WorldseedClimate
{
	namespace
	{
		const TCHAR* TEMP = TEXT("temperature");
		const TCHAR* PREC = TEXT("precipitation");

		/**
		 * Profil zonal de temperature au niveau de la mer.
		 *
		 * LA FORME EN COSINUS EST LA BONNE. Physiquement, l'energie solaire
		 * recue varie en moyenne annuelle a peu pres comme le cosinus de la
		 * latitude. Par la mesure ensuite, contre le profil zonal reel :
		 *
		 *     sin^2(phi)      ecart moyen 7,02 C
		 *     (phi/90)^2                  1,95 C
		 *     (phi/90)^1,8                1,11 C
		 *     cos(phi)                    0,84 C
		 *
		 * La loi en sinus carre, qui est le reflexe habituel, se trompe de 12
		 * degres a 60 deg — assez pour couvrir de calotte tout un hemisphere.
		 */
		float SeaLevelTemperature(const UWorldseedRules& Rules,
			const FWorldseedGeometry& Geo, float AbsLatitudeDeg)
		{
			const float Half = FMath::Max(Geo.LatSpanDeg * 0.5f, 1e-6f);
			const float TEq = static_cast<float>(Rules.Num(TEMP, TEXT("equatorC"), 27.0));
			const float TPole = static_cast<float>(Rules.Num(TEMP, TEXT("poleC"), -25.0));

			if (Rules.Str(TEMP, TEXT("profile"), TEXT("power")) == TEXT("cosine"))
			{
				const float P = static_cast<float>(Rules.Num(TEMP, TEXT("cosineExponent"), 1.0));
				const float Angle = (AbsLatitudeDeg / Half) * (PI * 0.5f);
				const float C = FMath::Clamp(FMath::Cos(Angle), 0.0f, 1.0f);
				return TPole + (TEq - TPole) * FMath::Pow(C, P);
			}

			const float K = static_cast<float>(Rules.Num(TEMP, TEXT("latitudeExponent"), 2.0));
			return TEq - (TEq - TPole) * FMath::Pow(AbsLatitudeDeg / Half, K);
		}
	}

	float TauxDePluieZonal(const UWorldseedRules& Rules, float AbsLatEff, float Half,
		float Cells, float Front, float ConvF, float Subs)
	{
		// LA MEME FORMULE QUE LA CHAINE, et c'est voulu : si la circulation
		// change un jour, la saisonnalite doit changer avec elle. On ne garde
		// que les termes ZONAUX -- le soulevement orographique ne migre pas
		// avec les saisons, une montagne reste ou elle est.
		const float Omega = FMath::Cos(Cells * PI * AbsLatEff / Half);
		const float FrontWeight = Front + (1.0f - Front) * (1.0f - AbsLatEff / Half);

		const float Convergence = FMath::Max(Omega, 0.0f) * FrontWeight;
		const float Subsidence = FMath::Max(-Omega, 0.0f);

		const float R = 1.0f + ConvF * Convergence;
		return R * FMath::Max(1.0f - Subs * Subsidence, 0.05f);
	}

	float SeasonalAmplitude(const UWorldseedRules& Rules, float LatNorm, float Cont)
	{
		const float AmpEq = static_cast<float>(
			Rules.Num(TEMP, TEXT("seasonalAmplitudeEquatorC"), 3.0));
		const float AmpPole = static_cast<float>(
			Rules.Num(TEMP, TEXT("seasonalAmplitudePoleC"), 45.0));

		float Amp;
		if (Rules.Str(TEMP, TEXT("seasonalAmplitudeShape"), TEXT("linear")) == TEXT("sine"))
		{
			// Le contraste saisonnier suit le SINUS de la latitude : c'est la
			// projection de la declinaison solaire. En lineaire, corriger les
			// poles casse les latitudes moyennes et inversement.
			const float Saison = FMath::Sin(LatNorm * PI * 0.5f);
			const float Frac = static_cast<float>(
				Rules.Num(TEMP, TEXT("oceanModerationFraction"), 0.65));

			// L'ocean amortit PROPORTIONNELLEMENT, il ne retranche pas un nombre
			// fixe de degres. Et l'amortissement ne porte que sur la part qui
			// depend de la latitude : a l'equateur il n'y a pas de saison a
			// amortir.
			Amp = AmpEq + (AmpPole - AmpEq) * Saison * (1.0f - Frac * (1.0f - Cont));
		}
		else
		{
			Amp = AmpEq + (AmpPole - AmpEq) * LatNorm;
			Amp -= static_cast<float>(Rules.Num(TEMP, TEXT("oceanModerationC"), 6.0))
				* (1.0f - Cont);
		}

		return FMath::Max(Amp, 1.0f);
	}

	bool Generate(const UWorldseedRules& Rules, const FWorldseedGeometry& Geo,
		int32 Seed, const TArray<float>& ElevationM, FWorldseedClimateResult& Out,
		const FWorldseedProgressScope& Progress)
	{
		const int32 NX = Geo.NX;
		const int32 NY = Geo.NY;
		const int32 Count = NX * NY;
		if (ElevationM.Num() != Count)
		{
			return false;
		}
		if (Progress.Step(0.0f)) { return false; }

		const double StartTime = FPlatformTime::Seconds();
		const float Half = FMath::Max(Geo.LatSpanDeg * 0.5f, 1e-6f);

		// Latitude par ligne : elle ne depend pas de la colonne, on la tabule.
		TArray<float> LatitudeByRow;
		LatitudeByRow.SetNumUninitialized(NY);
		for (int32 J = 0; J < NY; ++J)
		{
			LatitudeByRow[J] = Geo.LatitudeDegForRow(J);
		}

		TArray<uint8> IsWater;
		IsWater.SetNumUninitialized(Count);
		for (int32 I = 0; I < Count; ++I)
		{
			IsWater[I] = (ElevationM[I] <= 0.0f) ? 1 : 0;
		}

		// --- continentalite : 0 au bord de mer, tend vers 1 dans les terres ---
		Out.Continentality.SetNumUninitialized(Count);
		{
			TArray<uint8> NotWater;
			NotWater.SetNumUninitialized(Count);
			bool bAnyWater = false;
			for (int32 I = 0; I < Count; ++I)
			{
				NotWater[I] = IsWater[I] ? 0 : 1;
				bAnyWater |= (IsWater[I] != 0);
			}

			if (!bAnyWater)
			{
				for (float& C : Out.Continentality) { C = 1.0f; }
			}
			else
			{
				TArray<float> DistPixels;
				WorldseedGrid::DistanceTransform(NotWater, NX, NY, DistPixels);
				const float KmPerPixel = Geo.MetersPerPixel() / 1000.0f;

				// --- LA PORTEE MARITIME EST UNE FRACTION DU MONDE ------------
				//
				// UNE LONGUEUR EN KILOMETRES NE PEUT PAS SUIVRE LA CARTE, et
				// c'est la meme faute que le gradient adiabatique fige sur la
				// reference de 8 km. 0,3 km est juste sur une carte de 16 km de
				// large ; sur 64 elle est quatre fois trop courte, sur 2 elle
				// est sept fois trop longue. La continentalite sature alors
				// trop tot ou jamais, et les biomes d'interieur suivent.
				//
				// LA FRACTION, ELLE, EST UNE GRANDEUR TERRESTRE DIRECTE. Sur
				// Terre le passage maritime -> continental se fait sur 300 a
				// 800 km pour une circonference de 40 000 : entre 0,75 et 2 %
				// de la largeur du monde. La valeur retenue, 1,9 %, est le haut
				// de cette bande -- et c'est exactement ce que 0,3 km valait
				// sur la carte de reference de 16 km. Le comportement a la
				// reference est donc inchange, au chiffre pres, et il suit
				// desormais la carte partout ailleurs.
				//
				// A zero, on retombe sur la longueur en kilometres : une donnee
				// absente doit rester sans effet.
				float RangeKm = FMath::Max(static_cast<float>(
					Rules.Num(TEMP, TEXT("oceanModerationRangeKm"), 0.30)), 1e-3f);

				const float PartLargeur = static_cast<float>(
					Rules.Num(TEMP, TEXT("oceanModerationPartLargeur"), 0.019));
				if (PartLargeur > 0.0f)
				{
					RangeKm = FMath::Max(
						PartLargeur * static_cast<float>(Geo.WidthM()) / 1000.0f, 1e-3f);
				}

				for (int32 I = 0; I < Count; ++I)
				{
					Out.Continentality[I] = 1.0f - FMath::Exp(-DistPixels[I] * KmPerPixel / RangeKm);
				}
			}
		}

		// --- temperature moyenne ---------------------------------------------
		// --- LE GRADIENT SE CALE SUR LE RELIEF REEL -------------------------
		//
		// 26 degres par kilometre est QUATRE FOIS le gradient terrestre reel
		// (6,5), et ce n'est pas une erreur : c'est la compensation de la
		// « maquette » du projet. Un monde dont les montagnes sont quatre fois
		// trop basses doit refroidir quatre fois plus vite pour avoir le meme
		// climat.
		//
		// MAIS LA VALEUR ETAIT FIGEE SUR LA REFERENCE DE 8 km, alors que le
		// relief, lui, suit la carte. Une carte quatre fois plus grande a donc
		// des montagnes quatre fois plus hautes ET le meme gradient. MESURE,
		// graine 20260909 : la calotte glaciaire passait de 13,18 % des terres
		// a 16 x 8 km a 23,08 % a 64 x 32, le desert chaud de 15,51 a 6,84, la
		// pelouse alpine de 3,98 a 9,70 -- trois biomes qui n'ont AUCUNE raison
		// de dependre de la taille de la carte. Le proprietaire l'a vu avant la
		// mesure, aux couleurs du globe de l'ecran de configuration.
		//
		// ET DIVISER PAR VerticalScale NE SUFFIT PAS, essaye et mesure : le
		// relief ne suit PAS lineairement ce facteur. Altitudes maximales
		// relevees 258 / 531 / 1716 m, soit 0,49 : 1 : 3,2, quand VerticalScale
		// vaut 0,125 : 1 : 4 -- les ecretages et la boucle soulevement/erosion
		// cassent la proportion. La correction surcorrigeait donc les petites
		// cartes : calotte 10,48 -> 27,62 % a 2 x 1 km.
		//
		// ON CALE DONC SUR CE QU'ON VEUT TENIR CONSTANT, ET NON SUR UN FACTEUR
		// NOMINAL : le refroidissement du SOMMET. `lapseSommetC` est le nombre
		// de degres qui separent le niveau de la mer des plus hautes terres, et
		// le gradient s'en deduit. C'est la meme doctrine que les quantiles du
		// socle -- « un quantile ne connait pas l'echelle » -- appliquee a la
		// temperature.
		//
		// LE SOMMET SE LIT AU CENTILE 99, JAMAIS AU MAXIMUM. Un seul pixel
		// aberrant fixerait sinon le climat de tout le monde.
		float Lapse = static_cast<float>(Rules.Num(TEMP, TEXT("lapseRateCPerKm"), 26.0));
		const float SommetC = static_cast<float>(Rules.Num(TEMP, TEXT("lapseSommetC"), 13.8));
		if (SommetC > 0.0f)
		{
			TArray<float> Terres;
			Terres.Reserve(Count / 4);
			for (int32 I = 0; I < Count; ++I)
			{
				if (ElevationM[I] > 0.0f) { Terres.Add(ElevationM[I]); }
			}
			if (Terres.Num() > 0)
			{
				const float P99M = WorldseedGrid::Quantile(Terres, 0.99f);
				if (P99M > 1.0f)
				{
					Lapse = SommetC / (P99M / 1000.0f);
				}
			}
		}
		float Cooling = static_cast<float>(Rules.Num(TEMP, TEXT("continentalCoolingC"), 0.0));

		// SURCHARGE POUR L'A/B, et elle existe pour ne PAS toucher au fichier
		// de regles. Le depot a une regle contre les A/B qui l'editent en
		// place : une boucle qui modifie puis restaure a deja vide
		// world_rules.json, et un A/B qui rouvre le fichier change son
		// empreinte, donc regenere le monde entre les deux moities -- ce ne
		// serait plus le meme monde.
		float CoolingForce = -1.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedContinental="), CoolingForce)
			&& CoolingForce >= 0.0f)
		{
			Cooling = CoolingForce;
		}

		const float CoolL0 = static_cast<float>(Rules.Num(TEMP, TEXT("continentalCoolingLat0Deg"), 25.0));
		const float CoolL1 = static_cast<float>(Rules.Num(TEMP, TEXT("continentalCoolingLat1Deg"), 60.0));

		Out.TempMeanC.SetNumUninitialized(Count);
		Out.SeasonalAmpC.SetNumUninitialized(Count);
		Out.TempMinC.SetNumUninitialized(Count);
		Out.TempMaxC.SetNumUninitialized(Count);

		ParallelFor(NY, [&](int32 J)
		{
			const float Lat = LatitudeByRow[J];
			const float AbsLat = FMath::Abs(Lat);
			const float TSea = SeaLevelTemperature(Rules, Geo, AbsLat);
			const float LatNorm = AbsLat / Half;

			// Le refroidissement continental ne mord qu'aux hautes latitudes :
			// sous les tropiques un interieur est au contraire plus CHAUD, et
			// c'est la prime d'aridite qui s'en charge.
			const float CoolWeight = WorldseedPerlin::Smoothstep(CoolL0, CoolL1, AbsLat);

			for (int32 I = 0; I < NX; ++I)
			{
				const int32 Index = J * NX + I;

				const float AltitudeKm = FMath::Max(ElevationM[Index], 0.0f) / 1000.0f;
				float T = TSea - Lapse * AltitudeKm;

				if (Cooling > 0.0f)
				{
					T -= Cooling * Out.Continentality[Index] * CoolWeight;
				}

				Out.TempMeanC[Index] = T;

				const float Amp = SeasonalAmplitude(Rules, LatNorm, Out.Continentality[Index]);
				Out.SeasonalAmpC[Index] = Amp;
				Out.TempMinC[Index] = T - Amp * 0.5f;
				Out.TempMaxC[Index] = T + Amp * 0.5f;
			}
		});

		if (Progress.Step(0.08f)) { return false; }

		// --- champ de vent : trois cellules par hemisphere ---------------------
		Out.WindU.SetNumUninitialized(Count);
		Out.WindV.SetNumUninitialized(Count);
		{
			const float C1 = Half * (30.0f / 90.0f);
			const float C2 = Half * (60.0f / 90.0f);
			const float TW = Half * (7.5f / 90.0f);

			const float Jitter = static_cast<float>(Rules.Num(PREC, TEXT("windJitter"), 0.0));
			TArray<float> JU;
			TArray<float> JV;
			if (Jitter > 0.0f)
			{
				// Sans irregularite, les bandes sont des rubans parfaits.
				WorldseedPerlin::FBMSphere(JU, Geo, 3.0f, 4, Seed + 31337);
				WorldseedPerlin::FBMSphere(JV, Geo, 3.0f, 4, Seed + 31357);
			}

			ParallelFor(NY, [&](int32 J)
			{
				const float Lat = LatitudeByRow[J];
				const float AbsLat = FMath::Abs(Lat);

				const float A = WorldseedPerlin::Smoothstep(C1 - TW, C1 + TW, AbsLat);
				const float B = WorldseedPerlin::Smoothstep(C2 - TW, C2 + TW, AbsLat);
				const float WTrades = 1.0f - A;
				const float WWest = A - B;
				const float WPolar = B;

				// Alizes vers l'ouest, westerlies vers l'est, est polaires vers l'ouest.
				const float BaseU = -WTrades + WWest - WPolar;
				const float Meridional = 0.35f * (-WTrades + WWest - WPolar);
				const float BaseV = FMath::Sign(Lat) * Meridional;

				for (int32 I = 0; I < NX; ++I)
				{
					const int32 Index = J * NX + I;
					float U = BaseU;
					float V = BaseV;
					if (Jitter > 0.0f)
					{
						U += Jitter * JU[Index];
						V += Jitter * JV[Index];
					}
					const float Mag = FMath::Max(FMath::Sqrt(U * U + V * V), 1e-6f);
					Out.WindU[Index] = U / Mag;
					Out.WindV[Index] = V / Mag;
				}
			});
		}

		if (Progress.Step(0.14f)) { return false; }

		// --- advection semi-lagrangienne de l'humidite -------------------------
		const float Spacing = Geo.MetersPerPixel();

		// Capacite de l'air : Clausius-Clapeyron. L'air froid est sec par
		// construction, ce qui suffit a creer les deserts polaires sans regle
		// dediee.
		TArray<float> HMax;
		HMax.SetNumUninitialized(Count);
		{
			const float TRef = static_cast<float>(Rules.Num(PREC, TEXT("saturationRefTempC"), 20.0));
			const float TScale = FMath::Max(static_cast<float>(
				Rules.Num(PREC, TEXT("saturationScaleC"), 16.0)), 1e-3f);
			for (int32 I = 0; I < Count; ++I)
			{
				HMax[I] = FMath::Clamp(FMath::Exp((Out.TempMeanC[I] - TRef) / TScale), 0.02f, 4.0f);
			}
		}

		// Soulevement orographique : l'air pousse contre une pente montante precipite.
		Out.OrographicUplift.SetNumUninitialized(Count);
		{
			TArray<float> LandElev;
			LandElev.SetNumUninitialized(Count);
			for (int32 I = 0; I < Count; ++I)
			{
				LandElev[I] = FMath::Max(ElevationM[I], 0.0f);
			}

			TArray<float> DhDy;
			TArray<float> DhDx;
			WorldseedGrid::Gradient(LandElev, NX, NY, Spacing, DhDy, DhDx);

			TArray<float> UpliftPos;
			UpliftPos.SetNumUninitialized(Count);
			for (int32 I = 0; I < Count; ++I)
			{
				UpliftPos[I] = FMath::Max(Out.WindU[I] * DhDx[I] + Out.WindV[I] * DhDy[I], 0.0f);
			}

			const float Ref = FMath::Max(WorldseedGrid::Quantile(UpliftPos, 0.99f), 1e-6f);
			for (int32 I = 0; I < Count; ++I)
			{
				Out.OrographicUplift[I] = FMath::Clamp(UpliftPos[I] / Ref, 0.0f, 2.0f);
			}
		}

		// Mouvement vertical des cellules, sous forme ANALYTIQUE.
		//
		// On ne derive PAS la divergence numeriquement du champ de vent : la
		// gigue haute frequence y produit des convergences locales plus fortes
		// que le signal des cellules, et la normalisation ecrase alors la
		// ceinture desertique.
		//
		//   omega(phi) = cos(3 * pi * |phi| / 90)
		//     0 deg  -> +1 ascendant  : ZCIT, foret equatoriale
		//    30 deg  -> -1 descendant : ceinture desertique
		//    60 deg  -> +1 ascendant  : front polaire
		//    90 deg  -> -1 descendant : desert polaire
		Out.Convergence.SetNumUninitialized(Count);
		TArray<float> Subsidence;
		Subsidence.SetNumUninitialized(Count);
		{
			const float Cells = static_cast<float>(Rules.Num(PREC, TEXT("cellsPerHemisphere"), 3.0));
			const float Wobble = static_cast<float>(Rules.Num(PREC, TEXT("cellWobble"), 0.0));
			const float Front = static_cast<float>(Rules.Num(PREC, TEXT("polarFrontStrength"), 0.45));

			// Bornes du frein applique a la subsidence : plein effet jusqu'a la
			// premiere, nul au-dela de la seconde. Surchargeables pour l'A/B --
			// le depot interdit d'editer le fichier de regles pour comparer.
			float SubFade0 = static_cast<float>(
				Rules.Num(PREC, TEXT("subsidenceFadeLat0Deg"), 90.0));
			float SubFade1 = static_cast<float>(
				Rules.Num(PREC, TEXT("subsidenceFadeLat1Deg"), 90.0));
			float FadeForce = -1.0f;
			if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedSubFade0="), FadeForce)
				&& FadeForce >= 0.0f)
			{
				SubFade0 = FadeForce;
			}
			FadeForce = -1.0f;
			if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedSubFade1="), FadeForce)
				&& FadeForce >= 0.0f)
			{
				SubFade1 = FadeForce;
			}

			TArray<float> WobbleNoise;
			if (Wobble > 0.0f)
			{
				// Les cellules ne sont pas des rubans parfaits : la ZCIT ondule,
				// la mousson deplace le tout.
				WorldseedPerlin::FBMSphere(WobbleNoise, Geo,
					static_cast<float>(Rules.Num(PREC, TEXT("cellWobbleFrequency"), 2.4)),
					4, Seed + 6151);
			}

			ParallelFor(NY, [&](int32 J)
			{
				const float AbsLat = FMath::Abs(LatitudeByRow[J]);
				const float BaseOmega = FMath::Cos(Cells * PI * AbsLat / Half);

				// La ZCIT est bien plus energetique que le front polaire : la
				// cellule de Hadley brasse une atmosphere chaude et epaisse, la
				// cellule de Ferrel une atmosphere froide et mince. Sans cette
				// ponderation, la bande 55-60 deg serait aussi arrosee que
				// l'equateur.
				const float FrontWeight = Front + (1.0f - Front) * (1.0f - AbsLat / Half);

				// --- LA BRANCHE DESCENDANTE DE HADLEY EST BORNEE EN LATITUDE ---
				//
				// Le cosinus fait descendre l'air partout ou il est negatif,
				// c'est-a-dire de 15 a 45 degres : une ceinture seche de trente
				// degres de large, centree sur 30. La Terre n'a pas cela. Sa
				// cellule de Hadley redescend vers 20-25 et s'arrete la ; au-dela
				// de 35 on est dans la cellule de Ferrel, ou l'air REMONTE, et
				// c'est pourquoi le Sahara cede la place au climat mediterraneen
				// puis tempere au lieu de s'etendre jusqu'aux Alpes.
				//
				// MESURE DU DEGAT, graine 1337 -- pluie par bande contre la
				// Terre : 30-40 degres 255 mm contre 600, -40 a -30 246 contre
				// 600, 40-50 520 contre 700. La bande 5-12 degres, ou vit la
				// foret temperee, se retrouve a 520 mm pour un seuil de case a
				// 400 : la moitie de ses cellules bascule en STEPPE. Quatre
				// graines mesurees donnent 0,81 a 1,11 % de foret temperee --
				// l'ecart est structurel, pas geographique.
				//
				// Le frein s'efface entre deux latitudes plutot que de trancher :
				// une bordure nette dessinerait un trait de pluie sur la carte.
				const float FadeSub = 1.0f - WorldseedPerlin::Smoothstep(
					SubFade0, SubFade1, AbsLat);

				for (int32 I = 0; I < NX; ++I)
				{
					const int32 Index = J * NX + I;
					float Omega = BaseOmega;
					if (Wobble > 0.0f)
					{
						Omega = FMath::Clamp(Omega + Wobble * WobbleNoise[Index], -1.0f, 1.0f);
					}
					Out.Convergence[Index] = FMath::Max(Omega, 0.0f) * FrontWeight;
					Subsidence[Index] = FMath::Max(-Omega, 0.0f) * FadeSub;
				}
			});
		}

		// Taux de pluie : les facteurs sont des MULTIPLES de la pluie de base.
		TArray<float> Rate;
		Rate.SetNumUninitialized(Count);
		{
			const float BaseRain = static_cast<float>(Rules.Num(PREC, TEXT("baseRainRate"), 0.004));
			const float Oro = static_cast<float>(Rules.Num(PREC, TEXT("orographicFactor"), 2.5));
			const float ConvF = static_cast<float>(Rules.Num(PREC, TEXT("convergenceFactor"), 3.0));
			const float Subs = static_cast<float>(Rules.Num(PREC, TEXT("subsidenceFactor"), 0.75));

			for (int32 I = 0; I < Count; ++I)
			{
				float R = BaseRain * (1.0f + Oro * Out.OrographicUplift[I]
					+ ConvF * Out.Convergence[I]);
				// La branche descendante de Hadley supprime la pluie vers 30 deg :
				// c'est ce terme qui creuse la ceinture desertique.
				R *= FMath::Max(1.0f - Subs * Subsidence[I], 0.05f);
				Rate[I] = FMath::Clamp(R, 0.0f, 0.9f);
			}
		}

		// Melange meridien par les tourbillons : le rail des depressions.
		// L'advection suit le vent MOYEN, qui est zonal aux moyennes latitudes,
		// donc rien ne porterait l'humidite vers les poles. Sur Terre ce
		// transport est assure par les depressions barocliniques, que cette
		// resolution ne resout pas — sans terme dedie elles n'existent pas.
		const float EddyRate = static_cast<float>(Rules.Num(PREC, TEXT("eddyMixingRate"), 0.0));
		const float EddyDeg = static_cast<float>(Rules.Num(PREC, TEXT("eddyMixingSigmaDeg"), 0.0));
		// L'echelle est donnee en DEGRES, jamais en pixels : un sigma en pixels
		// ne decrirait pas le meme phenomene a deux resolutions.
		const float EddySigma = EddyDeg * (static_cast<float>(NY) / FMath::Max(Geo.LatSpanDeg, 1e-6f));

		TArray<float> KEddy;
		if (EddyRate > 0.0f && EddySigma > 0.0f)
		{
			const float Track = static_cast<float>(Rules.Num(PREC, TEXT("stormTrackLatDeg"), 55.0));
			const float Width = FMath::Max(static_cast<float>(
				Rules.Num(PREC, TEXT("stormTrackWidthDeg"), 20.0)), 1e-3f);

			KEddy.SetNumUninitialized(Count);
			for (int32 J = 0; J < NY; ++J)
			{
				const float AbsLat = FMath::Abs(LatitudeByRow[J]);
				const float T = (AbsLat - Track) / Width;
				const float K = EddyRate * FMath::Exp(-T * T);
				for (int32 I = 0; I < NX; ++I)
				{
					KEddy[J * NX + I] = K;
				}
			}
		}

		const float Evap = static_cast<float>(Rules.Num(PREC, TEXT("evaporationRate"), 0.02));
		const float ConvMoisture = static_cast<float>(
			Rules.Num(PREC, TEXT("moistureConvergenceRate"), 0.002));
		const float Step = static_cast<float>(Rules.Num(PREC, TEXT("advectionStepPx"), 1.6));
		const int32 Sweeps = Rules.Int(PREC, TEXT("advectionSweeps"), 400);
		constexpr float WaterRainDamp = 0.35f;

		TArray<float> Humidity;
		TArray<float> Advected;
		TArray<float> Mixed;
		Humidity.SetNumZeroed(Count);
		Advected.SetNumUninitialized(Count);
		Out.PrecipMm.SetNumZeroed(Count);

		for (int32 Sweep = 0; Sweep < Sweeps; ++Sweep)
		{
			// L'advection est de tres loin l'etape la plus longue : c'est elle
			// qui doit porter la finesse de la barre, et c'est ici qu'une
			// annulation doit repondre vite. Un test toutes les huit passes suffit
			// a rester sous les 100 ms de latence.
			if ((Sweep & 7) == 0)
			{
				const float Local = 0.16f + 0.78f * (static_cast<float>(Sweep)
					/ static_cast<float>(FMath::Max(Sweeps, 1)));
				if (Progress.Step(Local)) { return false; }
			}

			// Advection semi-lagrangienne : on remonte le vent d'un pas.
			ParallelFor(NY, [&](int32 J)
			{
				for (int32 I = 0; I < NX; ++I)
				{
					const int32 Index = J * NX + I;
					const float SrcJ = static_cast<float>(J) - Out.WindV[Index] * Step;
					const float SrcI = static_cast<float>(I) - Out.WindU[Index] * Step;
					Advected[Index] = WorldseedGrid::SampleBilinearClamped(Humidity, NX, NY, SrcJ, SrcI);
				}
			});
			Humidity = Advected;

			ParallelFor(NY, [&](int32 J)
			{
				for (int32 I = 0; I < NX; ++I)
				{
					const int32 Index = J * NX + I;
					float H = Humidity[Index];

					// Evaporation au-dessus de l'eau, jamais au-dessus des
					// terres : c'est ce qui fait emerger la continentalite.
					if (IsWater[Index])
					{
						H += Evap * (HMax[Index] - H);
					}

					// CONVERGENCE DE L'HUMIDITE. L'advection TRANSPORTE mais ne
					// CONCENTRE pas : un seul point amont est echantillonne, donc
					// des vents convergents n'accumulent aucune masse. Or c'est
					// exactement ce qui alimente la ZCIT.
					H += ConvMoisture * Out.Convergence[Index] * (HMax[Index] - H);

					Humidity[Index] = H;
				}
			});

			if (KEddy.Num() == Count)
			{
				// Un melange DESCENDANT LE GRADIENT, ce que fait un tourbillon.
				// Le terme NE CREE PAS d'eau, il en deplace : la capacite reste
				// gouvernee par Clausius-Clapeyron et l'humidite transportee est
				// ecretee, donc elle PRECIPITE en chemin.
				Mixed = Humidity;
				WorldseedGrid::GaussianFilter1D(Mixed, NX, NY, EddySigma, true);
				for (int32 I = 0; I < Count; ++I)
				{
					Humidity[I] += KEddy[I] * (Mixed[I] - Humidity[I]);
				}
			}

			ParallelFor(NY, [&](int32 J)
			{
				for (int32 I = 0; I < NX; ++I)
				{
					const int32 Index = J * NX + I;
					float H = FMath::Min(FMath::Max(Humidity[Index], 0.0f), HMax[Index]);

					float DPrecip = H * Rate[Index];
					if (IsWater[Index])
					{
						DPrecip *= WaterRainDamp;
					}

					Humidity[Index] = H - DPrecip;
					Out.PrecipMm[Index] += DPrecip;
				}
			});
		}

		const double AfterAdvection = FPlatformTime::Seconds();

		const float SmoothSigma = static_cast<float>(Rules.Num(PREC, TEXT("smoothSigmaPx"), 0.0));
		if (SmoothSigma > 0.0f)
		{
			WorldseedGrid::GaussianFilter(Out.PrecipMm, NX, NY, SmoothSigma);
		}

		// Mise a l'echelle en mm/an, ancree sur une valeur PHYSIQUE : la MOYENNE
		// des precipitations sur les terres emergees, environ 715 mm/an sur Terre.
		//
		// Surtout pas une normalisation par centile haut : elle se saboterait
		// elle-meme. Augmenter l'evaporation ferait monter le pic equatorial,
		// donc rabaisserait tout le reste, et le monde deviendrait plus aride
		// alors qu'on vient d'y mettre plus d'eau.
		{
			const float Target = static_cast<float>(Rules.Num(PREC, TEXT("targetMeanLandMm"), 715.0));
			const float Ceiling = static_cast<float>(Rules.Num(PREC, TEXT("maxPrecipMm"), 4000.0));

			// Deux passes : l'ecretage au plafond retire de l'eau, donc la
			// premiere mise a l'echelle manque la cible par le bas.
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				double Sum = 0.0;
				int32 LandCount = 0;
				for (int32 I = 0; I < Count; ++I)
				{
					if (!IsWater[I])
					{
						Sum += Out.PrecipMm[I];
						++LandCount;
					}
				}

				double Ref;
				if (LandCount > 0)
				{
					Ref = Sum / static_cast<double>(LandCount);
				}
				else
				{
					double All = 0.0;
					for (const float P : Out.PrecipMm) { All += P; }
					Ref = All / FMath::Max(Count, 1);
				}

				const float Scale = static_cast<float>(Target / FMath::Max(Ref, 1e-9));
				for (float& P : Out.PrecipMm)
				{
					P = FMath::Clamp(P * Scale, 0.0f, Ceiling);
				}
			}

			double Sum = 0.0;
			int32 LandCount = 0;
			for (int32 I = 0; I < Count; ++I)
			{
				if (!IsWater[I]) { Sum += Out.PrecipMm[I]; ++LandCount; }
			}
			Out.MeanLandPrecipMm = (LandCount > 0)
				? static_cast<float>(Sum / static_cast<double>(LandCount)) : 0.0f;
		}

		// Prime de chaleur aride. Un desert est plus chaud que sa latitude :
		// ciel degage, donc plus d'insolation au sol, et pas d'evaporation pour
		// en consommer une part en chaleur latente.
		//
		// APPLIQUEE APRES LES PRECIPITATIONS, ET C'EST VOULU. L'ordre est
		// causal — c'est la secheresse qui rechauffe — mais il a aussi une
		// consequence pratique : la pluie pilote l'erosion, donc le relief.
		// Calculer la prime avant ferait bouger le terrain a chaque reglage de
		// cette seule valeur.
		{
			const float Heat = static_cast<float>(Rules.Num(TEMP, TEXT("aridityHeatC"), 0.0));
			if (Heat > 0.0f)
			{
				const float RefMm = FMath::Max(static_cast<float>(
					Rules.Num(TEMP, TEXT("aridityRefMm"), 600.0)), 1e-3f);

				ParallelFor(NY, [&](int32 J)
				{
					// L'effet suit l'energie solaire recue, donc le cosinus de
					// la latitude : un desert polaire est sec mais ne recoit
					// rien a amplifier.
					const float Sun = FMath::Clamp(
						FMath::Cos(FMath::DegreesToRadians(LatitudeByRow[J])), 0.0f, 1.0f);

					for (int32 I = 0; I < NX; ++I)
					{
						const int32 Index = J * NX + I;
						const float Aridity = FMath::Clamp(
							1.0f - Out.PrecipMm[Index] / RefMm, 0.0f, 1.0f);
						const float Bonus = Heat * Aridity * Sun;
						Out.TempMeanC[Index] += Bonus;
						Out.TempMinC[Index] += Bonus;
						Out.TempMaxC[Index] += Bonus;
					}
				});
			}
		}

		const double EndTime = FPlatformTime::Seconds();
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] climat %dx%d  %d passes  pluie moyenne terres %.0f mm/an (cible %.0f)  advection %.0f ms  total %.0f ms"),
			NX, NY, Sweeps, Out.MeanLandPrecipMm,
			Rules.Num(PREC, TEXT("targetMeanLandMm"), 715.0),
			(AfterAdvection - StartTime) * 1000.0,
			(EndTime - StartTime) * 1000.0);

		Progress.Step(1.0f);
		return true;
	}
}

float WorldseedClimate::SummerRainFraction(const UWorldseedRules& Rules,
	const FWorldseedGeometry& Geo, float LatitudeDeg)
{
	const float Half = FMath::Max(Geo.LatSpanDeg * 0.5f, 1e-6f);
	const float Shift = static_cast<float>(
		Rules.Num(TEXT("precipitation"), TEXT("beltShiftDeg"), 0.0));

	// Sans migration des ceintures, il n'y a pas de saison : moitie-moitie.
	if (Shift <= 0.0f)
	{
		return 0.5f;
	}

	const float Cells = static_cast<float>(
		Rules.Num(TEXT("precipitation"), TEXT("cellsPerHemisphere"), 3.0));
	const float Front = static_cast<float>(
		Rules.Num(TEXT("precipitation"), TEXT("polarFrontStrength"), 0.45));
	const float ConvF = static_cast<float>(
		Rules.Num(TEXT("precipitation"), TEXT("convergenceFactor"), 3.0));
	const float Subs = static_cast<float>(
		Rules.Num(TEXT("precipitation"), TEXT("subsidenceFactor"), 0.75));

	// EN ETE LES CEINTURES MONTENT VERS LE POLE DE L'HEMISPHERE CHAUD. Vu du
	// point, elles s'eloignent de l'equateur : sa position RELATIVE aux
	// ceintures diminue d'autant. En hiver, l'inverse.
	const float Signe = (LatitudeDeg >= 0.0f) ? 1.0f : -1.0f;
	const float AbsEte = FMath::Min(FMath::Abs(LatitudeDeg - Shift * Signe), Half);
	const float AbsHiver = FMath::Min(FMath::Abs(LatitudeDeg + Shift * Signe), Half);

	const float Ete = TauxDePluieZonal(Rules, AbsEte, Half, Cells, Front, ConvF, Subs);
	const float Hiver = TauxDePluieZonal(Rules, AbsHiver, Half, Cells, Front, ConvF, Subs);

	const float Somme = Ete + Hiver;
	return (Somme > 1e-6f) ? Ete / Somme : 0.5f;
}
