// Worldseed - chaine de generation du monde, pilotee par world_rules.json.

#include "Procedural/WorldseedPipeline.h"

#include "Procedural/WorldseedCoast.h"
#include "Procedural/WorldseedFins.h"
#include "Procedural/WorldseedPlateau.h"
#include "Procedural/WorldseedStrata.h"

#include "Procedural/WorldseedCache.h"
#include "Procedural/WorldseedErosion.h"
#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedTectonics.h"

#include "UObject/StrongObjectPtr.h"

namespace WorldseedPipeline
{
	namespace
	{
		/**
		 * Les regles sont relues une seule fois par session. TStrongObjectPtr
		 * les protege du GC : un UObject nu garde en statique serait ramasse.
		 */
		TStrongObjectPtr<UWorldseedRules>& CachedRules()
		{
			static TStrongObjectPtr<UWorldseedRules> Instance;
			return Instance;
		}

		/**
		 * Pas de masque d'eau douce : il n'y a plus d'hydrologie.
		 *
		 * WorldseedBiomes::Classify degrade proprement sur un tableau vide —
		 * voir bHasLakes / bHasRivers : les biomes se classent alors sur le
		 * climat seul, et l'axe de couverture ne porte plus que l'ocean.
		 */
		const TArray<bool> NoWaterMask;
	}

	void ReloadRules()
	{
		// LES REGLES SONT RELUES UNE FOIS PAR SESSION, et c'est le bon choix
		// en jeu : le fichier ne bouge pas pendant une partie. Mais pour
		// REGLER une valeur, relancer l'editeur a chaque essai coute une
		// minute la ou la mesure en coute une seconde. D'ou cette porte,
		// reservee au reglage.
		CachedRules().Reset();
	}

	UWorldseedRules* GetRules(FString& OutError)
	{
		OutError.Reset();

		if (CachedRules().IsValid())
		{
			return CachedRules().Get();
		}

		UWorldseedRules* Loaded = UWorldseedRules::LoadRules(OutError);
		if (Loaded)
		{
			CachedRules().Reset(Loaded);
		}
		return Loaded;
	}

	bool Generate(int32 Seed, float HeightMeters, int32 ResolutionY,
		FResult& Out, FString& OutError, FWorldseedJob* Job)
	{
		// Repartition des tranches. La climatologie prend la plus grosse part :
		// ses 400 passes d'advection pesent bien plus que la tectonique.
		// Ordre documente de la chaine : tectonique, climat, erosion, climat.
		// La seconde passe de climat est indispensable — l erosion a creuse des
		// vallees et rabote des sommets, donc les temperatures et les ombres
		// pluviometriques de la premiere passe ne decrivent plus le terrain.
		const FWorldseedProgressScope TectonicScope{ Job, 0.02f, 0.23f, EWorldseedStage::Tectonics };
		const FWorldseedProgressScope Climate1Scope{ Job, 0.25f, 0.25f, EWorldseedStage::Climate };
		const FWorldseedProgressScope ErosionScope{ Job, 0.50f, 0.22f, EWorldseedStage::Erosion };
		const FWorldseedProgressScope Climate2Scope{ Job, 0.72f, 0.26f, EWorldseedStage::Climate };

		const double StartTime = FPlatformTime::Seconds();

		if (Job) { Job->Report(0.0f, EWorldseedStage::Rules); }
		UWorldseedRules* Rules = GetRules(OutError);
		if (!Rules)
		{
			return false;
		}

		// On part de la geometrie du fichier, et on ne surcharge que ce que le
		// joueur a choisi dans le menu.
		FWorldseedGeometry Geometry = Rules->Geometry;
		Geometry.NY = FMath::Clamp(ResolutionY, 33, 4097);
		Geometry.NX = Geometry.NY * 2;
		Geometry.HeightM = FMath::Max(HeightMeters, 1.0f);

		// --- cache ------------------------------------------------------------
		// Le monde est entierement determine par la graine, les dimensions et les
		// regles : s il a deja ete calcule, le refaire est du gaspillage pur.
		// Dix-huit secondes contre quelques dizaines de millisecondes.
		const FString CacheKey = WorldseedCache::MakeKey(
			Seed, Geometry.HeightM, Geometry.NY, Rules->SourceHash);

		FWorldseedWorldData Cached;
		if (WorldseedCache::Load(CacheKey, Cached))
		{
			Geometry = Cached.Geometry;
			Out.Geometry = Geometry;
			Out.ElevationM = MoveTemp(Cached.ElevationM);
			Out.Climate.TempMeanC = MoveTemp(Cached.TempC);
			Out.Climate.PrecipMm = MoveTemp(Cached.PrecipMm);
			Out.Climate.SeasonalAmpC = MoveTemp(Cached.SeasonalAmpC);
			Out.Climate.Continentality = MoveTemp(Cached.Continentality);
			Out.Lithology.Id = MoveTemp(Cached.LithologyId);
			Out.bHasClimate = (Out.Climate.TempMeanC.Num() == Geometry.CellCount());
			Out.bFromCache = true;

			Out.MinElevationM = BIG_NUMBER;
			Out.MaxElevationM = -BIG_NUMBER;
			int32 CachedLand = 0;
			for (const float E : Out.ElevationM)
			{
				Out.MinElevationM = FMath::Min(Out.MinElevationM, E);
				Out.MaxElevationM = FMath::Max(Out.MaxElevationM, E);
				if (E > 0.0f) { ++CachedLand; }
			}
			Out.LandRatio = static_cast<float>(CachedLand) / FMath::Max(Geometry.CellCount(), 1);

			// --- biomes ---------------------------------------------------------
			// La temperature du mois le plus chaud n'est pas transportee : elle se
			// RECONSTITUE exactement, le modele climatique la definissant comme la
			// moyenne annuelle plus une demi-amplitude. La stocker serait redondant.
			{
				const int32 Cells = Geometry.CellCount();
				TArray<float> TempMaxC;
				if (Out.Climate.TempMaxC.Num() == Cells)
				{
					TempMaxC = Out.Climate.TempMaxC;
				}
				else if (Out.Climate.TempMeanC.Num() == Cells
					&& Out.Climate.SeasonalAmpC.Num() == Cells)
				{
					TempMaxC.SetNumUninitialized(Cells);
					for (int32 I = 0; I < Cells; ++I)
					{
						TempMaxC[I] = Out.Climate.TempMeanC[I] + Out.Climate.SeasonalAmpC[I] * 0.5f;
					}
				}

				FString BioError;
				if (const UWorldseedRules* BioRules = GetRules(BioError))
				{
					WorldseedBiomes::Classify(Geometry, Out.ElevationM,
						Out.Climate.TempMeanC, TempMaxC, Out.Climate.PrecipMm,
						NoWaterMask, NoWaterMask,
						FWorldseedBiomeRules::FromRules(*BioRules, Geometry), Out.Biomes);

					WorldseedFields::Compute(Geometry, Out.ElevationM,
						Out.Climate.PrecipMm,
						FWorldseedGroundRules::FromRules(*BioRules), Out.Ground);

					// LE RESEAU VIENT APRES LA LITHOLOGIE ET LE CLIMAT, et c'est
					// l'ordre qui compte : c'est la ROCHE qui dit ou un karst peut
					// se creuser, et la PLUIE qui dit s'il y a de quoi dissoudre.
					WorldseedCaves::Build(Geometry, Out.ElevationM, Out.Climate.PrecipMm,
						Out.Lithology, FWorldseedLithologyRules::FromRules(*BioRules),
						FWorldseedCaveRules::FromRules(*BioRules), 1.0f, Seed, Out.Caves);

					// LE RELEVE VIENT ICI ET NULLE PART AILLEURS : c'est la seule
					// place ou TOUTES les cles ont ete demandees. Pose plus haut,
					// il ne voyait rien de ce que la classification et les champs
					// du sol allaient lire -- premiere version silencieuse pour
					// cette raison, alors qu'une cle etait cassee expres.
					BioRules->ReportMissingKeys();
				}
			}

			if (Job) { Job->Report(1.0f, EWorldseedStage::Done); }

			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] monde repris du cache : seed=%d  %dx%d  (%.0f ms)"),
				Seed, Geometry.NX, Geometry.NY,
				(FPlatformTime::Seconds() - StartTime) * 1000.0);
			return true;
		}

		FWorldseedTectonicResult Tectonic;
		if (!WorldseedTectonics::Generate(*Rules, Geometry, Seed, Tectonic, TectonicScope))
		{
			OutError = TEXT("generation interrompue");
			return false;
		}

		if (Tectonic.ElevationM.Num() != Geometry.CellCount())
		{
			OutError = TEXT("la tectonique n'a pas produit de heightfield exploitable");
			return false;
		}

		// LA LITHOLOGIE SE LIT SUR LA TECTONIQUE, ET MAINTENANT. Elle a besoin de
		// la croute et de la convergence, que seule cette etape connait ; et le
		// faire AVANT l'erosion est correct, l'erosion ne transformant pas du
		// granite en calcaire.
		WorldseedLithology::Compute(Geometry, Tectonic.ElevationM,
			Tectonic.IsContinental, Tectonic.Convergence,
			FWorldseedLithologyRules::FromRules(*Rules), Seed, Out.Lithology);

		Out.ElevationM = MoveTemp(Tectonic.ElevationM);
		Out.Geometry = Geometry;
		Out.SeaLevelShiftM = Tectonic.SeaLevelShiftM;
		Out.LandRatio = Tectonic.MeasuredLandRatio;

		Out.MinElevationM = BIG_NUMBER;
		Out.MaxElevationM = -BIG_NUMBER;
		for (const float E : Out.ElevationM)
		{
			Out.MinElevationM = FMath::Min(Out.MinElevationM, E);
			Out.MaxElevationM = FMath::Max(Out.MaxElevationM, E);
		}

		// --- etapes 2 et 3 : temperature, circulation, precipitations ---------
		if (!WorldseedClimate::Generate(*Rules, Geometry, Seed, Out.ElevationM,
			Out.Climate, Climate1Scope))
		{
			OutError = TEXT("generation interrompue");
			return false;
		}

		// --- etape 4a : erosion ------------------------------------------------
		// C est elle qui rend le relief ORGANIQUE. Le bruit a cretes du melange
		// tectonique (ridgedMix) produit des aretes coupantes par construction ;
		// sans erosion elles restent en l etat et le paysage parait dechire.
		// LA ROCHE ENTRE DANS L'EROSION, arbitre par le proprietaire le
		// 19 septembre 2026. Jusqu'ici la durete du catalogue n'etait lue par
		// personne : le relief etait organique mais INDIFFERENCIE, sa raideur
		// un accident du bruit plutot qu'une consequence de la roche. La
		// lithologie est deja calculee a ce stade -- elle vient de la
		// tectonique, avant l'erosion -- il ne manquait que le branchement.
		// Le relief AVANT erosion, pour mesurer ce que chaque roche a perdu.
		// L'ALTITUDE MOYENNE PAR ROCHE NE MESURE PAS L'EROSION : le granite est
		// haut parce que la regle d'attribution le pose sur les orogenes, pas
		// parce qu'il resiste. Seule la PERTE dit si l'erosion differentielle
		// mord -- et elle ne se voit qu'en comparant avant et apres.
		TArray<float> AvantErosion = Out.ElevationM;

		TArray<float> Erodabilite;
		WorldseedLithology::Erodibility(Out.Lithology,
			FWorldseedLithologyRules::FromRules(*Rules),
			static_cast<float>(Rules->Num(TEXT("erosion"), TEXT("duretePoids"), 0.0)),
			Erodabilite);

		// --- LE SOULEVEMENT, TIRE DE LA TECTONIQUE ---------------------------
		//
		// La convergence des plaques est deja calculee et n'etait lue par
		// personne a ce stade : `> 0` collision, `< 0` rift. C'est la source
		// naturelle du soulevement, et elle place les montagnes la ou la
		// tectonique les met -- pas la ou un bruit les a posees.
		//
		// IL NE S'APPLIQUE QU'AU CONTINENTAL. Soulever le plancher oceanique
		// remonterait les fonds et changerait la part des terres a chaque
		// passe, ce qui n'a aucun sens physique et ruinerait le calage.
		const float SoulContinental = static_cast<float>(
			Rules->Num(TEXT("erosion"), TEXT("soulevementContinentalM"), 0.0));
		const float SoulConvergence = static_cast<float>(
			Rules->Num(TEXT("erosion"), TEXT("soulevementConvergenceM"), 0.0));

		TArray<float> Soulevement;
		if ((SoulContinental > 0.0f || SoulConvergence > 0.0f)
			&& Tectonic.IsContinental.Num() == Out.ElevationM.Num()
			&& Tectonic.Convergence.Num() == Out.ElevationM.Num())
		{
			Soulevement.SetNumUninitialized(Out.ElevationM.Num());
			for (int32 I = 0; I < Soulevement.Num(); ++I)
			{
				if (Tectonic.IsContinental[I] == 0)
				{
					Soulevement[I] = 0.0f;
					continue;
				}
				const float Conv = FMath::Max(0.0f, Tectonic.Convergence[I]);
				Soulevement[I] = SoulContinental + SoulConvergence * Conv;
			}
		}

		// --- L'EROSION LIT LES BANCS -----------------------------------------
		//
		// SANS CELA, LES GRADINS NE PEUVENT PAS EXISTER. Le tableau ci-dessus
		// fige l'erodabilite de la roche de surface INITIALE ; une fois la
		// surface descendue de deux cents metres, elle decrit un monde qui
		// n'est plus la. Or une corniche nait precisement de ce que la surface
		// atteint un banc DUR apres un banc TENDRE.
		//
		// LA SERIE SE LIT SUR LA LITHOLOGIE ET AVANT LA BOUCLE : le datum est
		// une surface geologique, l'erosion ne le deplace pas. Seule la
		// LECTURE suit la surface.
		FWorldseedErodibilite ErodStrates;
		WorldseedStrata::PreparerErodibilite(Geometry, Out.Lithology,
			FWorldseedLithologyRules::FromRules(*Rules),
			FWorldseedStratRules::FromRules(*Rules,
				FWorldseedLithologyRules::FromRules(*Rules)),
			static_cast<float>(Rules->Num(TEXT("erosion"), TEXT("duretePoids"), 0.0)),
			Seed, ErodStrates);

		FWorldseedErosionReport ErosionReport;
		if (!WorldseedErosion::Run(*Rules, Geometry, Out.Climate.PrecipMm, Erodabilite,
			Soulevement, ErodStrates.IsActive() ? &ErodStrates : nullptr,
			Out.ElevationM, ErosionReport, ErosionScope))
		{
			OutError = TEXT("generation interrompue");
			return false;
		}

		// --- CE QUE CHAQUE ROCHE A PERDU -------------------------------------
		if (Out.Lithology.Id.Num() == Out.ElevationM.Num())
		{
			const FWorldseedLithologyRules LR = FWorldseedLithologyRules::FromRules(*Rules);
			TArray<double> Perte; Perte.Init(0.0, LR.Catalogue.Num());
			TArray<int32> Compte; Compte.Init(0, LR.Catalogue.Num());

			for (int32 I = 0; I < Out.ElevationM.Num(); ++I)
			{
				if (AvantErosion[I] <= 0.0f) { continue; }
				const uint8 R = Out.Lithology.Id[I];
				if (!Perte.IsValidIndex(R)) { continue; }
				Perte[R] += AvantErosion[I] - Out.ElevationM[I];
				++Compte[R];
			}

			FString Ligne;
			for (int32 R = 0; R < LR.Catalogue.Num(); ++R)
			{
				if (Compte[R] == 0) { continue; }
				Ligne += FString::Printf(TEXT("  %s %.2f m"),
					*LR.Catalogue[R].Label, Perte[R] / Compte[R]);
			}
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] erosion : perte moyenne par roche --%s"), *Ligne);

			// LA PENTE PAR ROCHE EST LA SEULE VRAIE SIGNATURE. A l'equilibre
			// `U = K . A^m . S^n`, donc `S = (U / K.A^m)^(1/n)` : a soulevement
			// egal, une roche dure tient une pente plus RAIDE. C'est cela qu'on
			// vient chercher, et ni l'altitude ni la perte ne le disent -- les
			// deux sont dominees par la POSITION que la regle d'attribution
			// donne a chaque roche.
			TArray<float> PenteY, PenteX;
			WorldseedGrid::Gradient(Out.ElevationM, Geometry.NX, Geometry.NY,
				Geometry.MetersPerPixel(), PenteY, PenteX);

			TArray<double> Pente; Pente.Init(0.0, LR.Catalogue.Num());
			TArray<int32> N2; N2.Init(0, LR.Catalogue.Num());
			for (int32 I = 0; I < Out.ElevationM.Num(); ++I)
			{
				if (Out.ElevationM[I] <= 0.0f) { continue; }
				const uint8 R = Out.Lithology.Id[I];
				if (!Pente.IsValidIndex(R)) { continue; }
				Pente[R] += FMath::RadiansToDegrees(FMath::Atan(
					FMath::Sqrt(PenteX[I] * PenteX[I] + PenteY[I] * PenteY[I])));
				++N2[R];
			}

			FString L2;
			for (int32 R = 0; R < LR.Catalogue.Num(); ++R)
			{
				if (N2[R] == 0) { continue; }
				L2 += FString::Printf(TEXT("  %s %.1f deg (durete %.2f)"),
					*LR.Catalogue[R].Label, Pente[R] / N2[R], LR.Catalogue[R].Hardness);
			}
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] erosion : pente moyenne par roche --%s"), *L2);
		}

		// --- recalage du niveau marin apres erosion --------------------------
		//
		// L erosion N EST PAS neutre pour le trait de cote. L incision retire de
		// la matiere, mais l erosion thermique en fait glisser depuis les terres
		// vers les cellules immergees voisines : les littoraux avancent. Mesure
		// sur la graine 1337 : la part emergee passait de 29,2 a 37,3 %.
		//
		// Or 29,2 % est une valeur CALIBREE SUR LA TERRE, que la documentation
		// traite comme un invariant. On refait donc passer le quantile apres
		// erosion. C est un ecart avec l ordre exact du generateur Python, mais
		// il preserve la propriete a laquelle ce generateur tient.
		{
			const float LandRatio = static_cast<float>(
				Rules->Num(TEXT("tectonics"), TEXT("landRatio"), 0.292));
			const float Shift = WorldseedGrid::Quantile(Out.ElevationM, 1.0f - LandRatio);
			for (float& E : Out.ElevationM)
			{
				E -= Shift;
			}
			Out.SeaLevelShiftM += Shift;
		}

		// --- etape 4b : le recul de falaise ------------------------------------
		//
		// APRES LE RECALAGE DU NIVEAU MARIN, donc sur un trait de cote
		// definitif, et AVANT le climat, pour que les biomes voient le nouveau
		// relief. L'ordre n'est pas indifferent : pose apres les biomes, la
		// passe creerait des falaises couvertes de la vegetation d'une plaine.
		{
			const FWorldseedCoastRules CoastRules =
				FWorldseedCoastRules::FromRules(*Rules);
			WorldseedCoast::Build(Geometry, Out.Lithology,
				FWorldseedLithologyRules::FromRules(*Rules), CoastRules, Seed,
				Out.ElevationM);
		}

		// --- etape 4c : le plateau disseque, donc les mesas et les canyons -----
		//
		// APRES LE LITTORAL, et l'ordre porte deux raisons. Le trait de cote
		// est alors definitif, donc le drainage que la passe calcule trouve ses
		// exutoires au bon endroit. Et les deux passes ne se marchent pas
		// dessus : celle-ci exige `altitudeMinM` au-dessus de la mer, quand le
		// recul de falaise travaille precisement sur la bande cotiere.
		//
		// AVANT LE CLIMAT, comme le littoral et pour la meme raison : un relief
		// remanie apres la classification porterait la vegetation d'une plaine
		// sur des escarpements de cent cinquante metres.
		{
			WorldseedPlateau::Build(Geometry, Out.Lithology,
				FWorldseedLithologyRules::FromRules(*Rules),
				Out.Climate.PrecipMm,
				FWorldseedPlateauRules::FromRules(*Rules),
				FWorldseedFinRules::FromRules(*Rules),
				FWorldseedStratRules::FromRules(*Rules,
					FWorldseedLithologyRules::FromRules(*Rules)), Seed,
				Out.ElevationM, &Out.Tables);
		}

		// --- seconde passe de climat sur le relief erode -----------------------
		if (!WorldseedClimate::Generate(*Rules, Geometry, Seed, Out.ElevationM,
			Out.Climate, Climate2Scope))
		{
			OutError = TEXT("generation interrompue");
			return false;
		}
		Out.bHasClimate = (Out.Climate.TempMeanC.Num() == Geometry.CellCount());

		// L erosion a deplace de la matiere : les extremes ont change.
		Out.MinElevationM = BIG_NUMBER;
		Out.MaxElevationM = -BIG_NUMBER;
		int32 LandCells = 0;
		for (const float E : Out.ElevationM)
		{
			Out.MinElevationM = FMath::Min(Out.MinElevationM, E);
			Out.MaxElevationM = FMath::Max(Out.MaxElevationM, E);
			if (E > 0.0f) { ++LandCells; }
		}
		Out.LandRatio = static_cast<float>(LandCells) / FMath::Max(Geometry.CellCount(), 1);

		if (Job) { Job->Report(1.0f, EWorldseedStage::Finalizing); }

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] monde genere : seed=%d  %.0f x %.0f m  %dx%d  altitudes %.0f..%.0f m  terres %.1f %%  (%.0f ms)"),
			Seed, Geometry.WidthM(), Geometry.HeightM, Geometry.NX, Geometry.NY,
			Out.MinElevationM, Out.MaxElevationM, Out.LandRatio * 100.0f,
			(FPlatformTime::Seconds() - StartTime) * 1000.0);

		FWorldseedWorldData ToCache;
		ToCache.Geometry = Geometry;
		ToCache.Seed = Seed;
		ToCache.ElevationM = Out.ElevationM;
		ToCache.TempC = Out.Climate.TempMeanC;
		ToCache.PrecipMm = Out.Climate.PrecipMm;
		ToCache.SeasonalAmpC = Out.Climate.SeasonalAmpC;
		ToCache.Continentality = Out.Climate.Continentality;
		ToCache.LithologyId = Out.Lithology.Id;
		WorldseedCache::Save(CacheKey, Rules->SourceHash, ToCache);

		// --- biomes ---------------------------------------------------------
		// La temperature du mois le plus chaud n'est pas transportee : elle se
		// RECONSTITUE exactement, le modele climatique la definissant comme la
		// moyenne annuelle plus une demi-amplitude. La stocker serait redondant.
		{
			const int32 Cells = Geometry.CellCount();
			TArray<float> TempMaxC;
			if (Out.Climate.TempMaxC.Num() == Cells)
			{
				TempMaxC = Out.Climate.TempMaxC;
			}
			else if (Out.Climate.TempMeanC.Num() == Cells
				&& Out.Climate.SeasonalAmpC.Num() == Cells)
			{
				TempMaxC.SetNumUninitialized(Cells);
				for (int32 I = 0; I < Cells; ++I)
				{
					TempMaxC[I] = Out.Climate.TempMeanC[I] + Out.Climate.SeasonalAmpC[I] * 0.5f;
				}
			}

			FString BioError;
			if (const UWorldseedRules* BioRules = GetRules(BioError))
			{
				WorldseedBiomes::Classify(Geometry, Out.ElevationM,
					Out.Climate.TempMeanC, TempMaxC, Out.Climate.PrecipMm,
					NoWaterMask, NoWaterMask,
					FWorldseedBiomeRules::FromRules(*BioRules, Geometry), Out.Biomes);

				WorldseedFields::Compute(Geometry, Out.ElevationM,
					Out.Climate.PrecipMm,
					FWorldseedGroundRules::FromRules(*BioRules), Out.Ground);

				WorldseedCaves::Build(Geometry, Out.ElevationM, Out.Climate.PrecipMm,
					Out.Lithology, FWorldseedLithologyRules::FromRules(*BioRules),
					FWorldseedCaveRules::FromRules(*BioRules), 1.0f, Seed, Out.Caves);

				BioRules->ReportMissingKeys();
			}
		}


		return true;
	}
}
