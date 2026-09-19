// Worldseed - sonde de couture : les cellules de transition ferment-elles la fissure ?
//
// UNE QUESTION PAR FICHIER. ProbeVoxel demande ce que COUTE un chunk,
// ProbeTransvoxel si le mailleur maison dit la MEME CHOSE que celui du moteur ;
// celle-ci demande si DEUX CHUNKS DE RESOLUTIONS DIFFERENTES se rejoignent. Elle
// ne se lit pas sur un chunk isole, et c'est ce qui la rend distincte des deux
// autres.

#include "Procedural/WorldseedProbeLibrary.h"

#include "Procedural/WorldseedProbeCommun.h"

#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedTransvoxel.h"
#include "Procedural/WorldseedVoxelChunk.h"

namespace
{
	/**
	 * Ce qu'on releve sur la couture de deux maillages cousus.
	 *
	 * L'ARETE OUVERTE EST LE SEUL CRITERE, et il ne suppose rien. Dans un
	 * maillage sain, toute arete interieure appartient a EXACTEMENT DEUX
	 * triangles. Une arete qui n'en a qu'un est un bord : legitime a la peripherie
	 * du domaine maille, et nulle part ailleurs. Sur le plan partage par deux
	 * chunks, une arete ouverte EST la fissure -- pas un indice, la chose meme.
	 */
	struct FCouture
	{
		int32 Sommets = 0;
		int32 Triangles = 0;

		/** Sommets poses exactement sur le plan partage. */
		int32 SommetsSurLePlan = 0;

		/** Aretes dont les deux bouts sont sur le plan, et leur etat. */
		int32 AretesSurLePlan = 0;
		int32 AretesOuvertesSurLePlan = 0;

		/** Combien de sommets du plan sont partages par les DEUX morceaux. */
		int32 SommetsPartages = 0;
	};

	/** Quantification d'une position, pour souder deux maillages independants. */
	FIntVector Grain(const FVector& PosCm)
	{
		// Un seizieme de centimetre. Les deux cotes calculent la MEME
		// interpolation depuis les MEMES valeurs aux MEMES points, donc les
		// doubles devraient coincider au bit pres ; le grain n'est la que pour
		// que la mesure ne depende pas de cette esperance.
		return FIntVector(
			FMath::RoundToInt(PosCm.X * 16.0),
			FMath::RoundToInt(PosCm.Y * 16.0),
			FMath::RoundToInt(PosCm.Z * 16.0));
	}

	/**
	 * Coud deux maillages par la POSITION et compte ce qui se passe sur le plan.
	 *
	 * On ne peut pas souder par indice : les deux chunks sont mailles
	 * separement et ne partagent aucune numerotation. C'est d'ailleurs
	 * exactement la situation du jeu -- deux composants distincts -- et une
	 * fissure y est une discontinuite de POSITION, pas d'indice.
	 */
	void Coudre(const FWorldseedVoxelMesh& A, const FWorldseedVoxelMesh& B,
		int32 AxePlan, double PlanCm, double TolCm, FCouture& R)
	{
		TMap<FIntVector, int32> Index;
		TArray<FVector> Positions;
		TArray<uint8> Origine;   // bit 1 = vu dans A, bit 2 = vu dans B
		TArray<int32> Triangles;

		auto Ajouter = [&](const FWorldseedVoxelMesh& M, uint8 Bit)
		{
			TArray<int32> Remap;
			Remap.SetNumUninitialized(M.Positions.Num());
			for (int32 I = 0; I < M.Positions.Num(); ++I)
			{
				const FIntVector G = Grain(M.Positions[I]);
				if (int32* Deja = Index.Find(G))
				{
					Remap[I] = *Deja;
					Origine[*Deja] |= Bit;
				}
				else
				{
					const int32 Neuf = Positions.Num();
					Positions.Add(M.Positions[I]);
					Origine.Add(Bit);
					Index.Add(G, Neuf);
					Remap[I] = Neuf;
				}
			}
			for (int32 T = 0; T + 2 < M.Triangles.Num(); T += 3)
			{
				Triangles.Add(Remap[M.Triangles[T]]);
				Triangles.Add(Remap[M.Triangles[T + 1]]);
				Triangles.Add(Remap[M.Triangles[T + 2]]);
			}
		};

		Ajouter(A, 1);
		Ajouter(B, 2);

		R.Sommets = Positions.Num();
		R.Triangles = Triangles.Num() / 3;

		auto SurLePlan = [&](int32 I)
		{
			return FMath::Abs(Positions[I][AxePlan] - PlanCm) <= TolCm;
		};

		for (int32 I = 0; I < Positions.Num(); ++I)
		{
			if (SurLePlan(I))
			{
				++R.SommetsSurLePlan;
				if (Origine[I] == 3) { ++R.SommetsPartages; }
			}
		}

		TMap<uint64, int32> Aretes;
		Aretes.Reserve(Triangles.Num());
		auto Noter = [&Aretes](int32 X, int32 Y)
		{
			const uint64 Lo = static_cast<uint64>(FMath::Min(X, Y));
			const uint64 Hi = static_cast<uint64>(FMath::Max(X, Y));
			++Aretes.FindOrAdd((Lo << 32) | Hi, 0);
		};

		for (int32 T = 0; T + 2 < Triangles.Num(); T += 3)
		{
			Noter(Triangles[T], Triangles[T + 1]);
			Noter(Triangles[T + 1], Triangles[T + 2]);
			Noter(Triangles[T + 2], Triangles[T]);
		}

		for (const TPair<uint64, int32>& P : Aretes)
		{
			const int32 X = static_cast<int32>(P.Key >> 32);
			const int32 Y = static_cast<int32>(P.Key & 0xFFFFFFFFull);
			if (!SurLePlan(X) || !SurLePlan(Y)) { continue; }

			++R.AretesSurLePlan;
			if (P.Value == 1) { ++R.AretesOuvertesSurLePlan; }
		}
	}
}

FString UWorldseedProbeLibrary::ProbeTransition(int32 Seed, float HeightMeters,
	int32 ResolutionY, int32 ChunkSideM, float LargeurTransition)
{
	WorldseedPipeline::FResult World;
	FString Error;

	if (!WorldseedPipeline::Generate(Seed, HeightMeters, ResolutionY, World, Error))
	{
		const FString Message = FString::Printf(TEXT("generation impossible : %s"), *Error);
		UE_LOG(LogTemp, Error, TEXT("[Sonde] %s"), *Message);
		return Message;
	}

	const UWorldseedRules* Rules = WorldseedPipeline::GetRules(Error);
	if (!Rules)
	{
		return FString::Printf(TEXT("regles illisibles : %s"), *Error);
	}

	const FWorldseedDensityRules DensityRules = FWorldseedDensityRules::FromRules(*Rules);

	FWorldseedDensity Density;
	Density.Init(World.Geometry, World.ElevationM, 1.0f, Seed, DensityRules);

	// SANS ELLE LA MESURE EST SILENCIEUSEMENT FAUSSE : KarstifiableAt rend 1
	// partout et l'on mesure le monde d'avant en croyant mesurer celui d'apres.
	Density.SetLithology(World.Lithology, FWorldseedLithologyRules::FromRules(*Rules));

	// --- trouver de la TERRE, et la plus accidentee -------------------------
	const int32 NX = World.Geometry.NX;
	int32 Meilleure = INDEX_NONE;
	float PlusHaut = 0.0f;
	for (int32 Cell = 0; Cell < World.ElevationM.Num(); ++Cell)
	{
		if (World.ElevationM[Cell] > PlusHaut)
		{
			PlusHaut = World.ElevationM[Cell];
			Meilleure = Cell;
		}
	}
	if (Meilleure == INDEX_NONE)
	{
		return TEXT("aucune terre emergee dans ce monde");
	}

	const double MetresParCellule = World.Geometry.MetersPerPixel();
	const int32 NY = World.Geometry.NY;
	const double CentreX = (static_cast<double>(Meilleure % NX) - NX * 0.5) * MetresParCellule;
	const double CentreY = (static_cast<double>(Meilleure / NX) - NY * 0.5) * MetresParCellule;

	// --- le couple de chunks ------------------------------------------------
	//
	// LE GROSSIER A GAUCHE, LE FIN A DROITE, et c'est le grossier qui porte la
	// cellule de transition sur sa face +X. Lengyel place les cellules de
	// transition DANS le bloc demi-resolution (section 4.3) : c'est lui qui a
	// trop peu d'echantillons -- neuf valeurs fines arrivent sur une face qui
	// n'en porte que quatre -- donc c'est a lui de ceder la place et de
	// raccorder.
	const double Cote = FMath::Max(ChunkSideM, 8);
	const double VoxelFin = DensityRules.VoxelSizeM;
	const double VoxelGros = VoxelFin * 2.0;

	float SurfaceMin = 0.0f;
	float SurfaceMax = 0.0f;
	Density.SurfaceRangeM(CentreX - Cote, CentreY, CentreX + Cote, CentreY + Cote,
		SurfaceMin, SurfaceMax);

	const int32 EtageBas = FMath::FloorToInt(
		(SurfaceMin - DensityRules.BandDepthM) / Cote);
	const int32 EtageHaut = FMath::FloorToInt(SurfaceMax / Cote);

	// Le plan partage : la face +X du chunk grossier.
	const double PlanM = CentreX;
	const double PlanCm = PlanM * WorldseedMetersToCm;

	FCouture Temoin;
	FCouture Avec;
	int32 CellulesTransition = 0;
	int32 TrianglesTransition = 0;
	double EnroulementCumule = 0.0;
	double EnroulementTransCumule = 0.0;
	int32 EnroulementComptes = 0;
	int32 EtagesMesures = 0;

	FWorldseedVoxelMesh Gros0, Gros1, Fin;
	FWorldseedVoxelStats S0, S1, SF;

	for (int32 CZ = EtageBas; CZ <= EtageHaut; ++CZ)
	{
		const FBox BoiteGrosse(
			FVector(PlanM - Cote, CentreY, CZ * Cote),
			FVector(PlanM, CentreY + Cote, (CZ + 1) * Cote));
		const FBox BoiteFine(
			FVector(PlanM, CentreY, CZ * Cote),
			FVector(PlanM + Cote, CentreY + Cote, (CZ + 1) * Cote));

		// Le chunk FIN, maille normalement.
		const bool bFin = WorldseedTransvoxel::Mailler(
			Density, nullptr, BoiteFine, VoxelFin,
			WorldseedTransvoxel::AucuneFace, 0.0f, Fin, SF, nullptr);

		// Le chunk GROSSIER, deux fois : sans transition (le temoin), puis avec.
		const bool bT = WorldseedTransvoxel::Mailler(
			Density, nullptr, BoiteGrosse, VoxelGros,
			WorldseedTransvoxel::AucuneFace, 0.0f, Gros0, S0, nullptr);

		const bool bA = WorldseedTransvoxel::Mailler(
			Density, nullptr, BoiteGrosse, VoxelGros,
			WorldseedTransvoxel::PlusX, LargeurTransition, Gros1, S1, nullptr);

		if (!bFin || !bT || !bA) { continue; }

		++EtagesMesures;
		CellulesTransition += S1.TransitionCells;
		TrianglesTransition += (S1.Triangles - S0.Triangles);
		EnroulementCumule += S1.FacesEndroit;
		EnroulementTransCumule += S1.FacesEndroitTransition;
		++EnroulementComptes;

		Coudre(Gros0, Fin, 0, PlanCm, 0.05, Temoin);
		Coudre(Gros1, Fin, 0, PlanCm, 0.05, Avec);
	}

	if (EtagesMesures == 0)
	{
		return TEXT("aucun etage ne porte de surface des deux cotes du plan");
	}

	const double PartTemoin = (Temoin.AretesSurLePlan > 0)
		? 100.0 * Temoin.AretesOuvertesSurLePlan / Temoin.AretesSurLePlan : 0.0;
	const double PartAvec = (Avec.AretesSurLePlan > 0)
		? 100.0 * Avec.AretesOuvertesSurLePlan / Avec.AretesSurLePlan : 0.0;

	UE_LOG(LogTemp, Log, TEXT("[Sonde] === COUTURE ENTRE DEUX RESOLUTIONS ==="));
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   %d etages, chunk de %.0f m, voxel %.2f m contre %.2f m, ")
		TEXT("dalle de transition %.2f cellule"),
		EtagesMesures, Cote, VoxelGros, VoxelFin, LargeurTransition);
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   %-22s %10s %10s %12s %12s"),
		TEXT("etat"), TEXT("triangles"), TEXT("sommets"),
		TEXT("partages"), TEXT("ouvertes"));
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   %-22s %10d %10d %12d %8d (%.1f %%)"),
		TEXT("temoin, sans cellule"), Temoin.Triangles, Temoin.SommetsSurLePlan,
		Temoin.SommetsPartages, Temoin.AretesOuvertesSurLePlan, PartTemoin);
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   %-22s %10d %10d %12d %8d (%.1f %%)"),
		TEXT("avec transition"), Avec.Triangles, Avec.SommetsSurLePlan,
		Avec.SommetsPartages, Avec.AretesOuvertesSurLePlan, PartAvec);

	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   %d cellules de transition triangulees, %+d triangles  |  ")
		TEXT("enroulement a l'endroit : regulieres %.1f %%, TRANSITION %.1f %%"),
		CellulesTransition, TrianglesTransition,
		EnroulementComptes > 0 ? 100.0 * EnroulementCumule / EnroulementComptes : 0.0,
		EnroulementComptes > 0 ? 100.0 * EnroulementTransCumule / EnroulementComptes : 0.0);

	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   Lecture : « ouvertes » compte les aretes du plan partage ")
		TEXT("qui n'appartiennent qu'a UN triangle -- c'est la definition d'un ")
		TEXT("trou, et elle ne suppose rien. « partages » compte les sommets du ")
		TEXT("plan que les DEUX chunks posent au meme endroit : c'est la couture ")
		TEXT("elle-meme, et elle doit monter."));

	const FString Resume = FString::Printf(
		TEXT("aretes ouvertes sur la couture %d (%.1f %%) contre %d (%.1f %%) sans ")
		TEXT("transition ; sommets partages %d contre %d ; %d cellules, %+d triangles"),
		Avec.AretesOuvertesSurLePlan, PartAvec,
		Temoin.AretesOuvertesSurLePlan, PartTemoin,
		Avec.SommetsPartages, Temoin.SommetsPartages,
		CellulesTransition, TrianglesTransition);

	UE_LOG(LogTemp, Log, TEXT("[Sonde] %s"), *Resume);
	return Resume;
}
