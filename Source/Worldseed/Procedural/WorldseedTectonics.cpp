// Worldseed - cette passe.
//
// L'ORIGINAL PYTHON A ETE SUPPRIME : ce fichier est la SEULE
// implementation. Il ne faut plus chercher de reference ailleurs, ni supposer
// qu'un autre fichier dit la meme chose autrement.

#include "Procedural/WorldseedTectonics.h"
#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedPerlin.h"
#include "Procedural/WorldseedTrace.h"

#include "Async/ParallelFor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace WorldseedTectonics
{
	/**
	 * Le socle continental, avec sa surcharge de banc d'essai.
	 *
	 * IL EST LU A DEUX ENDROITS -- le forçage polaire et l'assemblage du
	 * relief -- et le depot a une regle contre la valeur recopiee : deux
	 * lectures divergent a la premiere retouche, et ici elles doivent
	 * imperativement s'accorder, sinon le continent polaire flotterait a une
	 * autre hauteur que les autres.
	 *
	 * La surcharge existe pour l'A/B : ce depot interdit d'editer le fichier
	 * de regles pour comparer -- une boucle qui modifie puis restaure l'a deja
	 * vide une fois, et rouvrir le fichier change son empreinte, donc regenere
	 * le monde entre les deux moities de la comparaison.
	 */
	float SocleContinentalM(const UWorldseedRules& Rules, float VerticalScale)
	{
		float Base = static_cast<float>(
			Rules.Num(TEXT("tectonics"), TEXT("continentBaseM"), 30.0));
		return Base * VerticalScale;
	}

	namespace
	{
		/**
		 * Tirage normal centre reduit par Box-Muller.
		 *
		 * DIVERGENCE ASSUMEE avec le generateur Python, qui utilise le ziggurat
		 * de numpy sur PCG64. Reproduire ce couple exactement coute tres cher
		 * pour 36 valeurs, et ce qui est calibre sur la Terre — part emergee,
		 * bandes de latitude, forcage polaire — ne depend PAS de ce tirage :
		 * la part emergee est imposee par quantile, les latitudes par la
		 * geometrie. Consequence : la graine 20260909 ne redonne pas le monde
		 * Python de reference, mais tout monde produit ici est conforme et
		 * reproductible.
		 */
		FVector2D RandomNormal2D(FRandomStream& Stream)
		{
			const float U1 = FMath::Max(Stream.GetFraction(), 1e-7f);
			const float U2 = Stream.GetFraction();
			const float R = FMath::Sqrt(-2.0f * FMath::Loge(U1));
			const float Theta = 2.0f * PI * U2;
			return FVector2D(R * FMath::Cos(Theta), R * FMath::Sin(Theta));
		}

		/** Force un pole oceanique ou continental, comme sur Terre. */
		void ForcePoles(TArray<float>& Elevation, const UWorldseedRules& Rules,
			const FWorldseedGeometry& Geo, float VerticalScale)
		{
			const float Strength = static_cast<float>(
				Rules.Num(TEXT("tectonics"), TEXT("poleForcingStrength"), 0.0));
			if (Strength <= 0.0f)
			{
				return;
			}

			const int32 NX = Geo.NX;
			const int32 NY = Geo.NY;
			const float Half = Geo.LatSpanDeg * 0.5f;
			const float OceanDepth = static_cast<float>(
				Rules.Num(TEXT("tectonics"), TEXT("oceanDepthM"), -200.0)) * VerticalScale;
			const float ContinentBase = SocleContinentalM(Rules, VerticalScale);
			const float PoleBonus = static_cast<float>(
				Rules.Num(TEXT("tectonics"), TEXT("poleContinentBonusM"), 100.0)) * VerticalScale;

			struct FPole { const TCHAR* Key; const TCHAR* RadiusKey; float Sign; };
			const FPole Poles[2] = {
				{ TEXT("northPole"), TEXT("poleForcingRadiusDegNorth"), +1.0f },
				{ TEXT("southPole"), TEXT("poleForcingRadiusDegSouth"), -1.0f },
			};

			for (const FPole& Pole : Poles)
			{
				const FString Mode = Rules.Str(TEXT("tectonics"), Pole.Key, TEXT("free"));
				if (Mode == TEXT("free"))
				{
					continue;
				}

				float Radius = static_cast<float>(Rules.Num(TEXT("tectonics"), Pole.RadiusKey, 0.0));
				if (Radius <= 0.0f)
				{
					Radius = static_cast<float>(
						Rules.Num(TEXT("tectonics"), TEXT("poleForcingRadiusDeg"), 0.0));
				}
				if (Radius <= 0.0f)
				{
					continue;
				}

				const float Target = (Mode == TEXT("ocean"))
					? OceanDepth
					: (ContinentBase + PoleBonus);

				// UN CONTINENT POLAIRE EST UN PLATEAU AVEC UN LITTORAL, PAS UN
				// DEGRADE. La rampe n'occupe que le TIERS EXTERIEUR du rayon,
				// et le plateau est plein au-dela : sinon le fond oceanique
				// n'est remonte qu'a mi-hauteur et la calotte n'emerge qu'au
				// pole meme.
				const float Entree = Half - Radius;
				const float Sortie = Entree + Radius / 3.0f;

				ParallelFor(NY, [&](int32 J)
				{
					const float SignedLat = Geo.LatitudeDegForRow(J) * Pole.Sign;
					const float W = WorldseedPerlin::Smoothstep(Entree, Sortie, SignedLat) * Strength;
					if (W <= 0.0f)
					{
						return;
					}
					for (int32 I = 0; I < NX; ++I)
					{
						float& E = Elevation[J * NX + I];
						E = E * (1.0f - W) + Target * W;
					}
				});
			}
		}

		/**
		 * SUPPRIMEE AU PASSAGE A LA CARTE SPHERIQUE.
		 *
		 * oceanBorderKm noyait une marge tout autour du monde parce qu une carte
		 * CARREE a quatre bords : sans elle, les fleuves sortaient du domaine et
		 * le joueur butait sur une falaise de fin de monde.
		 *
		 * Une sphere n a aucun bord. La longitude s enroule, et les latitudes se
		 * terminent sur des POINTS — les poles — deja traites par le forcage
		 * polaire. Garder la ceinture retirerait des terres sans raison physique
		 * et ferait rater la cible de 29,2 %.
		 */

		/** Fait remonter le fond marin en approchant de la cote. */
		void ApplyShelf(TArray<float>& Elevation, const UWorldseedRules& Rules,
			const FWorldseedGeometry& Geo)
		{
			// --- LA LARGEUR DU PLATEAU EST UNE FRACTION DU MONDE ----------
			//
			// UNE LONGUEUR EN KILOMETRES NE PEUT PAS SUIVRE LA CARTE, et le
			// commentaire des tailles du menu le disait deja : « sur 500 m le
			// plateau continental couvre 70 %% du monde et sa rampe n.aboutit
			// jamais ; les fonds remontent et l.ocean plafonne vers -98 m au
			// lieu de -300 ». C.est la meme faute que le gradient adiabatique
			// et que la portee maritime, tous deux corriges le meme jour.
			//
			// LA FRACTION EST CALIBRE, PAS SOURCE, et il faut le dire : 2,19 %%
			// de la largeur du monde, c.est douze fois le plateau terrestre
			// (70 km pour 40 000 de circonference, soit 0,175 %%). C.est une
			// exageration ASSUMEE de la maquette -- un plateau a l.echelle
			// serait invisible. La valeur reproduit exactement les 0,35 km de
			// la carte de reference de 16 km de large.
			float WidthKm = static_cast<float>(
				Rules.Num(TEXT("tectonics"), TEXT("shelfWidthKm"), 0.0));

			const float PartLargeur = static_cast<float>(
				Rules.Num(TEXT("tectonics"), TEXT("shelfPartLargeur"), 0.021875));
			if (PartLargeur > 0.0f)
			{
				WidthKm = PartLargeur * static_cast<float>(Geo.WidthM()) / 1000.0f;
			}

			if (WidthKm <= 0.0f)
			{
				return;
			}

			const int32 NX = Geo.NX;
			const int32 NY = Geo.NY;
const int32 Count = NX * NY;
			TArray<uint8> Ocean;
			Ocean.SetNumUninitialized(NX * NY);
			bool bAnyOcean = false;
			for (int32 I = 0; I < Count; ++I)
			{
				Ocean[I] = (Elevation[I] < 0.0f) ? 1 : 0;
				bAnyOcean |= (Ocean[I] != 0);
			}
			if (!bAnyOcean)
			{
				return;
			}

			TArray<float> DistPixels;
			WorldseedGrid::DistanceTransform(Ocean, NX, NY, DistPixels);

			const float KmPerPixel = Geo.MetersPerPixel() / 1000.0f;

			ParallelFor(NY, [&](int32 J)
			{
				for (int32 I = 0; I < NX; ++I)
				{
					const int32 Index = J * NX + I;
					if (!Ocean[Index])
					{
						continue;
					}
					// Sans plateau continental on passe de +120 m a -800 m en
					// une cellule : une falaise sous-marine tout autour de
					// chaque continent, qui pollue les statistiques de pente.
					const float Ramp = WorldseedPerlin::Smoothstep(
						0.0f, WidthKm, DistPixels[Index] * KmPerPixel);
					Elevation[Index] *= Ramp;
				}
			});
		}
	}

	bool Generate(const UWorldseedRules& Rules, const FWorldseedGeometry& Geo,
		int32 Seed, FWorldseedTectonicResult& Out,
		const FWorldseedProgressScope& Progress)
	{
		WORLDSEED_TRACE(Tectonique);

		if (Progress.Step(0.0f)) { return false; }
		const int32 NX = Geo.NX;
		const int32 NY = Geo.NY;
		const int32 Count = NX * NY;
		const double StartTime = FPlatformTime::Seconds();

		// MISE A L ECHELLE VERTICALE.
		//
		// Les altitudes des regles — oceanDepthM, mountainHeightM, min et
		// maxElevationM — sont METRIQUES et calibrees pour la hauteur de monde
		// de reference. Sur une carte plus petite elles ne retrecissent pas, si
		// bien que le relief garde son amplitude sur une largeur divisee :
		// tout devient falaise. Mesure sur une carte de 1 km de hauteur : 92 %
		// des sommets au-dela de l angle de roche, un terrain integralement gris.
		//
		// C est la regle d echelle enoncee par la documentation du projet : ce
		// qui est metrique suit le facteur de reduction.
		const float VerticalScale = WorldseedVerticalScale(Rules, Geo.HeightM);

		// LE FOND MARIN NE SUIT L'ECHELLE QUE VERS LE BAS, arbitre par le
		// proprietaire le 19 septembre 2026.
		//
		// POURQUOI L'EXEMPTER. La regle d'echelle existe pour que le relief
		// rapporte a la largeur reste borne, sinon tout devient falaise --
		// mesure d'epoque, 92 % des sommets au-dela de l'angle de roche sur une
		// carte de 1 km. Cet argument porte sur les pentes TERRESTRES : on ne
		// marche pas sur le fond de l'ocean, et l'eau le cache. Continuer a le
		// creuser sur une grande carte, c'est payer une profondeur que personne
		// ne verra jamais -- et la payer CHER : le plugin Water encode la
		// hauteur de toute l'eau du niveau dans UNE texture, dont la plage va
		// du point le plus bas au plus haut. A 64 km, un fond a -1250 m au lieu
		// de -300 divise par quatre la precision de chaque texel, et c'est elle
		// qui produisait les rideaux verticaux au bord des lacs.
		//
		// MAIS SEULEMENT VERS LE HAUT. Sur une PETITE carte, un fond non reduit
		// ferait de chaque cote une falaise plongeant a trois cents metres --
		// exactement le defaut que la regle previent, transpose sous l'eau. Le
		// fond suit donc l'echelle quand elle REDUIT, et cesse de la suivre
		// quand elle agrandit.
		const float SeabedScale = FMath::Min(VerticalScale, 1.0f);

		const int32 PlateCount = FMath::Max(2, Rules.Int(TEXT("tectonics"), TEXT("plateCount"), 18));
		const float LandRatio = static_cast<float>(
			Rules.Num(TEXT("tectonics"), TEXT("landRatio"), 0.292));

		// --- plaques : germes, type, vecteur de derive ----------------------
		FRandomStream Stream(Seed ^ 0x7EC70A1C);

		// Germes sur la SPHERE, pas sur la carte : deux plaques de part et
		// d autre du meridien y seraient vues comme tres eloignees alors
		// qu elles sont voisines sur le globe.
		TArray<FVector> PlateSeeds;
		TArray<FVector2D> Drift;
		PlateSeeds.Reserve(PlateCount);
		Drift.Reserve(PlateCount);

		for (int32 P = 0; P < PlateCount; ++P)
		{
			// Construction d Archimede : z uniforme puis azimut. Tirer deux
			// angles uniformes tasserait les plaques aux poles.
			const float PZ = 2.0f * Stream.GetFraction() - 1.0f;
			const float PPhi = 2.0f * PI * Stream.GetFraction();
			const float PR = FMath::Sqrt(FMath::Max(0.0f, 1.0f - PZ * PZ));
			PlateSeeds.Emplace(PR * FMath::Cos(PPhi), PR * FMath::Sin(PPhi), PZ);
		}
		for (int32 P = 0; P < PlateCount; ++P)
		{
			FVector2D D = RandomNormal2D(Stream);
			const float Len = FMath::Max(D.Size(), 1e-9f);
			D /= Len;
			D *= Stream.FRandRange(0.35f, 1.0f);
			Drift.Add(D);
		}

		// Assez de plaques continentales pour tenir le ratio vise ; le niveau
		// marin final est de toute facon fixe par quantile, ceci ne regle que
		// la FORME des continents.
		const int32 NContinental = FMath::RoundToInt(
			PlateCount * FMath::Min(0.95f, LandRatio * 1.35f));

		TArray<int32> Order;
		Order.Reserve(PlateCount);
		for (int32 P = 0; P < PlateCount; ++P)
		{
			Order.Add(P);
		}
		for (int32 I = PlateCount - 1; I > 0; --I)
		{
			Order.Swap(I, Stream.RandRange(0, I));
		}

		TArray<uint8> PlateIsContinental;
		PlateIsContinental.SetNumZeroed(PlateCount);
		for (int32 K = 0; K < NContinental && K < PlateCount; ++K)
		{
			PlateIsContinental[Order[K]] = 1;
		}

		// --- coordonnees d'interrogation, deplacees par un bruit ------------
		// Sans ce warp, les frontieres de plaques sont des segments de droite :
		// on obtient des continents en polygone et des chaines rectilignes, qui
		// se voient immediatement sur une carte.
		TArray<float> WarpX;
		TArray<float> WarpY;
		TArray<float> WarpZ;
		// --- LA SEULE FACON D'AVOIR DES FRONTIERES DE PLAQUE NON DROITES ------
		//
		// Un diagramme de Voronoi a des aretes RECTILIGNES par definition :
		// l'ensemble des points equidistants de deux germes est un plan. Aucun
		// reglage du Voronoi ne peut les courber. Le seul levier est de
		// DEPLACER LE POINT D'ECHANTILLONNAGE avant de le classer -- le
		// domain warping : on calcule le Voronoi en un point voisin, tire par
		// un bruit, et la frontiere ondule d'autant.
		//
		// CELA PORTE PLUS LOIN QUE LE MASQUE. Le soulevement Uplift est
		// calcule depuis la PROXIMITE d'une frontiere, donc il herite lui aussi
		// de cette geometrie : courber les frontieres courbe a la fois le trait
		// continent/ocean et les chaines de montagnes qui le bordent. C'est le
		// seul terme de la chaine qui agisse sur les deux sources du trait de
		// cote a la fois.
		float PlateWarp = static_cast<float>(
			Rules.Num(TEXT("tectonics"), TEXT("plateWarpStrength"), 0.0));
		if (PlateWarp > 0.0f)
		{
			float WarpFreq = static_cast<float>(
				Rules.Num(TEXT("tectonics"), TEXT("plateWarpFrequency"), 2.2));
			WorldseedPerlin::FBMSphere(WarpX, Geo, WarpFreq, 5, Seed + 4441);
			WorldseedPerlin::FBMSphere(WarpY, Geo, WarpFreq, 5, Seed + 4457);
			WorldseedPerlin::FBMSphere(WarpZ, Geo, WarpFreq, 5, Seed + 4463);
		}
		if (Progress.Step(0.12f)) { return false; }

		Out.PlateId.SetNumUninitialized(Count);
		Out.Convergence.SetNumUninitialized(Count);
		Out.IsContinental.SetNumUninitialized(Count);

		TArray<float> Proximity;
		TArray<float> RawContinental;
		Proximity.SetNumUninitialized(Count);
		RawContinental.SetNumUninitialized(Count);

		// Largeur de suture en unites de CORDE sur la sphere unite. Une unite
		// de carte en Y vaut un arc de PI ; aux largeurs en jeu, corde et arc
		// se confondent a moins de 2 %.
		// --- LA CEINTURE DE SUTURE EST UNE FRACTION DU MONDE ---------------
		//
		// mountainWidthKm etait une largeur ABSOLUE : 1,5 km de ceinture quelle
		// que soit la carte, donc 75 %% d.une carte de 2 km de haut et 4,7 %% d.une
		// de 32. Un monde ne peut pas etre le meme a toutes les tailles si ses
		// chaines occupent une part du domaine qui varie d.un facteur seize.
		//
		// CALIBRE ET NON SOURCE : 18,75 %% de la hauteur du monde, c.est bien plus
		// qu.un orogene terrestre (100 a 300 km pour 20 000 de demi-circonference,
		// soit 0,5 a 1,5 %%). L.exageration est assumee -- une chaine a l.echelle
		// ne se verrait pas. La valeur reproduit exactement les 1,5 km de la carte
		// de reference de 8 km de haut.
		float PartHauteur = static_cast<float>(
			Rules.Num(TEXT("tectonics"), TEXT("mountainPartHauteur"), 0.1875));
		if (PartHauteur <= 0.0f)
		{
			PartHauteur = static_cast<float>(
				Rules.Num(TEXT("tectonics"), TEXT("mountainWidthKm"), 1.5))
				* 1000.0f / Geo.HeightM;
		}
		const float Width = FMath::Max(PartHauteur * PI, 1e-4f);

		ParallelFor(NY, [&](int32 J)
		{
			for (int32 I = 0; I < NX; ++I)
			{
				const int32 Index = J * NX + I;

				FVector Q = WorldseedPerlin::SpherePoint(Geo, I, J);
				if (PlateWarp > 0.0f)
				{
					// Deplacement DANS L ESPACE DE LA SPHERE : continu au
					// meridien comme partout ailleurs.
					Q += FVector(WarpX[Index], WarpY[Index], WarpZ[Index]) * PlateWarp;
					Q = Q.GetSafeNormal();
				}

				// Voronoi k=2 en force brute : 18 plaques, c'est moins cher
				// qu'un arbre et parfaitement deterministe.
				float D1 = BIG_NUMBER;
				float D2 = BIG_NUMBER;
				int32 P1 = 0;
				int32 P2 = 0;
				for (int32 P = 0; P < PlateCount; ++P)
				{
					const float D = static_cast<float>(FVector::Dist(Q, PlateSeeds[P]));
					if (D < D1)
					{
						D2 = D1; P2 = P1;
						D1 = D;  P1 = P;
					}
					else if (D < D2)
					{
						D2 = D; P2 = P;
					}
				}

				Out.PlateId[Index] = P1;
				RawContinental[Index] = PlateIsContinental[P1] ? 1.0f : 0.0f;

				// Direction de P1 vers P2, projetee dans le plan TANGENT au
				// point courant : seule facon d avoir une normale de frontiere
				// qui garde un sens sur une surface courbe.
				FVector Delta = PlateSeeds[P2] - PlateSeeds[P1];
				Delta -= Q * FVector::DotProduct(Delta, Q);
				const FVector Normal = Delta.GetSafeNormal();

				// La derive reste un vecteur (est, nord) ; on la releve dans le
				// plan tangent avec les deux directions cardinales locales.
				const FVector East = FVector::CrossProduct(FVector::UpVector, Q).GetSafeNormal();
				const FVector North = FVector::CrossProduct(Q, East).GetSafeNormal();
				const FVector2D Relative = Drift[P1] - Drift[P2];
				const FVector RelativeTangent = East * Relative.Y + North * Relative.X;

				Out.Convergence[Index] = static_cast<float>(
					FVector::DotProduct(RelativeTangent, Normal));

				// (d2 - d1) vaut 0 exactement sur la frontiere.
				Proximity[Index] = FMath::Exp(-(D2 - D1) / Width);
			}
		});

		if (Progress.Step(0.35f)) { return false; }

		// --- masque continental : adouci PUIS bruite ------------------------
		// Un simple seuil donne une marche pile sur l'arete de Voronoi, que le
		// bruit de detail ne peut pas casser, d'ou des continents en polygone.
		// En rendant le MASQUE fractal, le trait de cote devient decoupe a
		// toutes les echelles.
		TArray<float> Mask = RawContinental;
		float SmoothPx = static_cast<float>(
			Rules.Num(TEXT("tectonics"), TEXT("continentSmoothKm"), 0.0))
			* 1000.0f / Geo.MetersPerPixel();

		// SURCHARGE DE BANC D'ESSAI. C'est le lissage qui donne sa PRISE au
		// bruit : le masque brut est binaire, et un bruit ajoute a une marche
		// ne peut deplacer le trait que de l'epaisseur de cette marche. Plus
		// la transition est large, plus le meme bruit deplace loin.
		if (SmoothPx >= 0.5f)
		{
			WorldseedGrid::GaussianFilter(Mask, NX, NY, SmoothPx);
		}

		const float CoastAmount = static_cast<float>(
			Rules.Num(TEXT("tectonics"), TEXT("coastNoiseAmount"), 0.0));
		if (CoastAmount > 0.0f)
		{
			const float CoastFreq = static_cast<float>(
				Rules.Num(TEXT("tectonics"), TEXT("coastNoiseFrequency"), 5.5));

			// --- LE GAIN EST CE QUI DECIDE DE LA DIMENSION FRACTALE ---------
			//
			// Il etait laisse a son defaut de 0,5, et ce n'est pas un detail de
			// dosage : pour une somme fractale, l'exposant de Hurst vaut
			// H = -log2(gain), et la dimension d'un trait de niveau vaut
			// D = 2 - H. A 0,5 on a donc H = 1 et D = 1,00 -- une cote
			// RECTILIGNE, par construction, quelle que soit l'amplitude qu'on
			// lui donne. Mesure avant : 0,971, sous la cote sud-africaine qui
			// est la plus lisse de la Terre (1,05).
			//
			// Le gain concentre l'energie : a 0,5 la sixieme octave ne porte
			// qu'un trente-deuxieme de la premiere, soit six metres de
			// deplacement du trait -- invisible. Monter le gain rend aux
			// petites echelles le poids qui fait les baies et les caps.
			//
			// Reperes publies (Mandelbrot 1967) : Grande-Bretagne 1,25,
			// Norvege et ses fjords 1,52, Afrique du Sud 1,05.
			const float CoastGain = static_cast<float>(
				Rules.Num(TEXT("tectonics"), TEXT("coastNoiseGain"), 0.5));
			const int32 CoastOctaves = Rules.Int(
				TEXT("tectonics"), TEXT("coastNoiseOctaves"), 6);

			TArray<float> CoastNoise;
			WorldseedPerlin::FBMSphere(CoastNoise, Geo, CoastFreq, CoastOctaves,
				Seed + 8821, 2.0f, CoastGain);
			if (Progress.Step(0.45f)) { return false; }
			for (int32 I = 0; I < Count; ++I)
			{
				Mask[I] += CoastAmount * CoastNoise[I];
			}
		}

		// --- assemblage du relief ------------------------------------------
		const float OceanDepth = static_cast<float>(
			Rules.Num(TEXT("tectonics"), TEXT("oceanDepthM"), -200.0)) * SeabedScale;
		const float ContinentBase = SocleContinentalM(Rules, VerticalScale);
		const float MountainHeight = static_cast<float>(
			Rules.Num(TEXT("tectonics"), TEXT("mountainHeightM"), 325.0)) * VerticalScale;
		const float RiftDepth = static_cast<float>(
			Rules.Num(TEXT("tectonics"), TEXT("riftDepthM"), 80.0)) * SeabedScale;
		const float DetailContinent = static_cast<float>(
			Rules.Num(TEXT("tectonics"), TEXT("detailAmplitudeContinentM"), 85.0)) * VerticalScale;
		const float DetailOcean = static_cast<float>(
			Rules.Num(TEXT("tectonics"), TEXT("detailAmplitudeOceanM"), 32.5)) * SeabedScale;

		const int32 Octaves = Rules.Int(TEXT("tectonics"), TEXT("octaves"), 8);
		const float BaseFreq = static_cast<float>(
			Rules.Num(TEXT("tectonics"), TEXT("baseFrequency"), 6.0));
		const float Lacunarity = static_cast<float>(
			Rules.Num(TEXT("tectonics"), TEXT("lacunarity"), 2.0));
		const float Gain = static_cast<float>(Rules.Num(TEXT("tectonics"), TEXT("gain"), 0.5));

		TArray<float> RidgeTexture;
		WorldseedPerlin::RidgedSphere(RidgeTexture, Geo, 14.0f, 5, Seed + 5501);
		if (Progress.Step(0.55f)) { return false; }

		TArray<float> Detail;
		WorldseedPerlin::DomainWarpedFBMSphere(Detail, Geo, BaseFreq, Octaves, Seed + 911,
			static_cast<float>(Rules.Num(TEXT("tectonics"), TEXT("warpStrength"), 0.35)),
			static_cast<float>(Rules.Num(TEXT("tectonics"), TEXT("warpFrequency"), 1.4)),
			Lacunarity, Gain);
		if (Progress.Step(0.80f)) { return false; }

		// Une part de bruit a CRETES melangee au fBm : le fBm seul donne des
		// collines molles, les cretes apportent les aretes et les lignes de
		// partage des eaux, qui sont ce que l'oeil lit comme "montagne".
		const float RidgeMix = static_cast<float>(
			Rules.Num(TEXT("tectonics"), TEXT("ridgedMix"), 0.0));
		if (RidgeMix > 0.0f)
		{
			TArray<float> Crests;
			WorldseedPerlin::RidgedSphere(Crests, Geo, BaseFreq * 1.7f, Octaves,
				Seed + 3313, Lacunarity, Gain);
			for (int32 I = 0; I < Count; ++I)
			{
				Detail[I] = Detail[I] * (1.0f - RidgeMix) + (Crests[I] * 2.0f - 1.0f) * RidgeMix;
			}
		}

		Out.ElevationM.SetNumUninitialized(Count);

		ParallelFor(NY, [&](int32 J)
		{
			for (int32 I = 0; I < NX; ++I)
			{
				const int32 Index = J * NX + I;

				const float Blend = WorldseedPerlin::Smoothstep(0.35f, 0.65f, Mask[Index]);
				const float Base = OceanDepth + (ContinentBase - OceanDepth) * Blend;

				const uint8 bContinental = (Blend > 0.5f) ? 1 : 0;
				Out.IsContinental[Index] = bContinental;

				const float Conv = Out.Convergence[Index];
				const float Prox = Proximity[Index];

				const float Uplift = MountainHeight * FMath::Clamp(Conv, 0.0f, 1.0f) * Prox
					* (0.45f + 0.55f * RidgeTexture[Index]);
				const float Rift = RiftDepth * FMath::Clamp(-Conv, 0.0f, 1.0f) * Prox;

				const float DetailAmp = bContinental ? DetailContinent : DetailOcean;

				Out.ElevationM[Index] = Base + Uplift - Rift + Detail[Index] * DetailAmp;
			}
		});

		if (Progress.Step(0.92f)) { return false; }

		// --- contraintes -----------------------------------------------------
		ForcePoles(Out.ElevationM, Rules, Geo, VerticalScale);

		// --- niveau de la mer par quantile : le ratio terres/mers est EXACT ---
		Out.SeaLevelShiftM = WorldseedGrid::Quantile(Out.ElevationM, 1.0f - LandRatio);
		for (float& E : Out.ElevationM)
		{
			E -= Out.SeaLevelShiftM;
		}

		ApplyShelf(Out.ElevationM, Rules, Geo);

		const float MinElev = static_cast<float>(
			Rules.Num(TEXT("world"), TEXT("minElevationM"), -300.0)) * SeabedScale;
		const float MaxElev = static_cast<float>(
			Rules.Num(TEXT("world"), TEXT("maxElevationM"), 400.0)) * VerticalScale;

		int32 LandCells = 0;
		for (float& E : Out.ElevationM)
		{
			E = FMath::Clamp(E, MinElev, MaxElev);
			if (E > 0.0f)
			{
				++LandCells;
			}
		}
		Out.MeasuredLandRatio = static_cast<float>(LandCells) / static_cast<float>(Count);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] tectonique %dx%d  seed=%d  %d plaques  mer a %.1f m  terres %.1f %% (cible %.1f)  %.0f ms"),
			NX, NY, Seed, PlateCount, Out.SeaLevelShiftM,
			Out.MeasuredLandRatio * 100.0f, LandRatio * 100.0f,
			(FPlatformTime::Seconds() - StartTime) * 1000.0);

		Progress.Step(1.0f);
		return true;
	}
}
