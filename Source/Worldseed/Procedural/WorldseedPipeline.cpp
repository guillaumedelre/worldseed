// Worldseed - chaine de generation du monde, pilotee par world_rules.json.

	/**
	 * Largeur de cuvette au plus, en metres.
	 *
	 * Mesuree APRES creusement : un chenal bien creuse rend ce plafond sans
	 * objet, puisque les berges arretent la sonde d'elles-memes. S'il mord
	 * encore, c'est que le troncon est une mare et non un cours — le releve
	 * "a la butee" le dit, graine par graine.
	 */
	constexpr float MaxRiverBasinWidthM = 600.0f;

#include "Procedural/WorldseedPipeline.h"

#include "Procedural/WorldseedRiverCarve.h"
#include "Procedural/WorldseedRivers.h"
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

			{
				FString HydError;
				if (const UWorldseedRules* HydRules = GetRules(HydError))
				{
					WorldseedHydrology::Generate(Out.ElevationM, Out.Climate.PrecipMm,
						Geometry, FWorldseedHydrologyRules::FromRules(*HydRules), Out.Hydrology);

		// LE LIT SE CREUSE APRES L'HYDROLOGIE, ET AVANT LES BIOMES.
		//
		// Apres, parce qu'il a besoin du trace et de la surface libre. Avant,
		// parce que la classification lit l'altitude : le chenal creuse doit
		// porter sa couverture d'eau, pas la bande qui le debordait.
		//
		// Le cache est ecrit AVANT l'hydrologie : le creusement se rejoue donc
		// a chaque chargement, sur les deux chemins. Il est idempotent, il ne
		// fait que descendre le relief.
		WorldseedRiverCarve::Apply(Out.Hydrology.Rivers, Out.Hydrology.LakeMask,
			Geometry, FWorldseedCarveRules(), Out.ElevationM);
				}
			}

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
						Out.Hydrology.LakeMask, Out.Hydrology.RiverMask,
						FWorldseedBiomeRules::FromRules(*BioRules), Out.Biomes);
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
		FWorldseedErosionReport ErosionReport;
		if (!WorldseedErosion::Run(*Rules, Geometry, Out.Climate.PrecipMm,
			Out.ElevationM, ErosionReport, ErosionScope))
		{
			OutError = TEXT("generation interrompue");
			return false;
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
		WorldseedCache::Save(CacheKey, Rules->SourceHash, ToCache);

		WorldseedHydrology::Generate(Out.ElevationM, Out.Climate.PrecipMm, Geometry,
			FWorldseedHydrologyRules::FromRules(*Rules), Out.Hydrology);

		// LE LIT SE CREUSE APRES L'HYDROLOGIE, ET AVANT LES BIOMES.
		//
		// Apres, parce qu'il a besoin du trace et de la surface libre. Avant,
		// parce que la classification lit l'altitude : le chenal creuse doit
		// porter sa couverture d'eau, pas la bande qui le debordait.
		//
		// Le cache est ecrit AVANT l'hydrologie : le creusement se rejoue donc
		// a chaque chargement, sur les deux chemins. Il est idempotent, il ne
		// fait que descendre le relief.
		WorldseedRiverCarve::Apply(Out.Hydrology.Rivers, Out.Hydrology.LakeMask,
			Geometry, FWorldseedCarveRules(), Out.ElevationM);

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
					Out.Hydrology.LakeMask, Out.Hydrology.RiverMask,
					FWorldseedBiomeRules::FromRules(*BioRules), Out.Biomes);
			}
		}


		return true;
	}
}
