// Worldseed - sonde Transvoxel : les deux mailleurs rendent-ils la meme surface ?
//
// LES SONDES SONT ECLATEES PAR QUESTION. ProbeVoxel demande ce que COUTE un
// chunk ; celle-ci demande si le mailleur maison dit la MEME CHOSE que celui du
// moteur. Ce sont deux questions distinctes, elles ont donc deux fichiers --
// c'est ce que l'assainissement du 19 septembre avait laisse a faire pour
// ProbeLithology et ProbeArches, et il n'y a pas de raison de recommencer a
// empiler.

#include "Procedural/WorldseedProbeLibrary.h"

#include "Procedural/WorldseedProbeCommun.h"

#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedTransvoxel.h"
#include "Procedural/WorldseedVoxelChunk.h"

namespace
{
	/** Ce qu'on releve sur UN maillage, sans jamais le comparer a l'autre. */
	struct FReleve
	{
		int32 Chunks = 0;
		int32 Triangles = 0;
		int32 Sommets = 0;

		/** Aire de la surface, en metres carres. LA grandeur geometrique. */
		double AireM2 = 0.0;

		/** |densite| aux sommets : ils sont censes etre POSES sur l'isovaleur. */
		double EcartMoyen = 0.0;
		double EcartMax = 0.0;
		int64 SommetsPeses = 0;

		/** Aretes n'appartenant qu'a UN triangle : des trous, sauf sur la paroi. */
		int32 AretesDeBord = 0;
		int32 AretesHorsParoi = 0;

		/** Aretes partagees par plus de deux triangles : maillage non manifold. */
		int32 AretesTordues = 0;

		/** De combien le maillage sort de la boite du chunk, en metres. */
		double DebordMax = 0.0;

		double TotalMs = 0.0;
		int64 Evaluations = 0;
		double EnroulementCumule = 0.0;
		int32 EnroulementComptes = 0;
	};

	/**
	 * Les aretes de bord, et pourquoi elles disent quelque chose.
	 *
	 * Dans un maillage sain, toute arete INTERIEURE appartient a exactement deux
	 * triangles. Une arete qui n'en a qu'un est un BORD : sur un chunk, c'est
	 * normal a la paroi de la boite, puisque la surface y est coupee net. Partout
	 * ailleurs, c'est un trou.
	 *
	 * C'est ce controle qui verra les fissures le jour ou les cellules de
	 * transition arriveront, et c'est pour cela qu'il est pose maintenant : une
	 * mesure qu'on installe APRES avoir introduit le defaut qu'elle doit voir
	 * n'a pas de temoin.
	 */
	void Compter(const FWorldseedVoxelMesh& Maillage, const FBox& BoiteM,
		double Tolerance, FReleve& R)
	{
		TMap<uint64, int32> Aretes;
		Aretes.Reserve(Maillage.Triangles.Num());

		auto Noter = [&Aretes](int32 A, int32 B)
		{
			const uint64 Lo = static_cast<uint64>(FMath::Min(A, B));
			const uint64 Hi = static_cast<uint64>(FMath::Max(A, B));
			++Aretes.FindOrAdd((Lo << 32) | Hi, 0);
		};

		// LE DEBORDEMENT, ET POURQUOI IL EST LA GRANDEUR QUI TRANCHE.
		// Un ecart d'aire entre deux mailleurs a deux explications opposees : l'un
		// RATE de la surface, ou l'autre en maille EN TROP, au-dela de sa boite,
		// empietant sur le chunk voisin. Les deux donnent le meme pourcentage et
		// n'appellent pas du tout la meme reponse. Comparer les bornes du maillage
		// a celles de la boite separe les deux cas en une seule mesure.
		for (const FVector& PosCm : Maillage.Positions)
		{
			const FVector P = PosCm / WorldseedMetersToCm;
			R.DebordMax = FMath::Max(R.DebordMax,
				FMath::Max3(
					FMath::Max(BoiteM.Min.X - P.X, P.X - BoiteM.Max.X),
					FMath::Max(BoiteM.Min.Y - P.Y, P.Y - BoiteM.Max.Y),
					FMath::Max(BoiteM.Min.Z - P.Z, P.Z - BoiteM.Max.Z)));
		}

		for (int32 T = 0; T + 2 < Maillage.Triangles.Num(); T += 3)
		{
			const int32 A = Maillage.Triangles[T];
			const int32 B = Maillage.Triangles[T + 1];
			const int32 C = Maillage.Triangles[T + 2];
			Noter(A, B); Noter(B, C); Noter(C, A);

			// Aire du triangle : la moitie de la norme du produit vectoriel. Les
			// positions sont en centimetres, d'ou la conversion au carre.
			const FVector U = Maillage.Positions[B] - Maillage.Positions[A];
			const FVector V = Maillage.Positions[C] - Maillage.Positions[A];
			R.AireM2 += 0.5 * FVector::CrossProduct(U, V).Size()
				/ (WorldseedMetersToCm * WorldseedMetersToCm);
		}

		for (const TPair<uint64, int32>& Paire : Aretes)
		{
			if (Paire.Value == 1)
			{
				++R.AretesDeBord;

				// Sur la paroi de la boite, ou ailleurs ? Les deux extremites
				// doivent toucher une meme face pour que le bord soit legitime.
				const int32 A = static_cast<int32>(Paire.Key >> 32);
				const int32 B = static_cast<int32>(Paire.Key & 0xFFFFFFFFull);
				const FVector PA = Maillage.Positions[A] / WorldseedMetersToCm;
				const FVector PB = Maillage.Positions[B] / WorldseedMetersToCm;

				const bool bSurParoi =
					(FMath::IsNearlyEqual(PA.X, BoiteM.Min.X, Tolerance) && FMath::IsNearlyEqual(PB.X, BoiteM.Min.X, Tolerance)) ||
					(FMath::IsNearlyEqual(PA.X, BoiteM.Max.X, Tolerance) && FMath::IsNearlyEqual(PB.X, BoiteM.Max.X, Tolerance)) ||
					(FMath::IsNearlyEqual(PA.Y, BoiteM.Min.Y, Tolerance) && FMath::IsNearlyEqual(PB.Y, BoiteM.Min.Y, Tolerance)) ||
					(FMath::IsNearlyEqual(PA.Y, BoiteM.Max.Y, Tolerance) && FMath::IsNearlyEqual(PB.Y, BoiteM.Max.Y, Tolerance)) ||
					(FMath::IsNearlyEqual(PA.Z, BoiteM.Min.Z, Tolerance) && FMath::IsNearlyEqual(PB.Z, BoiteM.Min.Z, Tolerance)) ||
					(FMath::IsNearlyEqual(PA.Z, BoiteM.Max.Z, Tolerance) && FMath::IsNearlyEqual(PB.Z, BoiteM.Max.Z, Tolerance));

				if (!bSurParoi)
				{
					++R.AretesHorsParoi;
				}
			}
			else if (Paire.Value > 2)
			{
				++R.AretesTordues;
			}
		}
	}

	/** |densite| aux sommets : le controle qui ne compare rien et juge tout seul. */
	void Peser(const FWorldseedVoxelMesh& Maillage, const FWorldseedDensity& Champ,
		FReleve& R)
	{
		for (const FVector& PosCm : Maillage.Positions)
		{
			const FVector PosM = PosCm / WorldseedMetersToCm;
			const double Ecart = FMath::Abs(Champ.At(PosM, nullptr));
			R.EcartMoyen += Ecart;
			R.EcartMax = FMath::Max(R.EcartMax, Ecart);
			++R.SommetsPeses;
		}
	}
}

FString UWorldseedProbeLibrary::ProbeTransvoxel(int32 Seed, float HeightMeters,
	int32 ResolutionY, int32 ChunkSideM, int32 ChunksPerSide)
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

	// SANS ELLE LA MESURE EST SILENCIEUSEMENT FAUSSE, et le depot l'a deja paye :
	// KarstifiableAt rend 1 partout, le terme de diaclase sort au premier test,
	// et l'on mesure le monde d'avant en croyant mesurer celui d'apres.
	Density.SetLithology(World.Lithology, FWorldseedLithologyRules::FromRules(*Rules));

	// --- trouver de la TERRE, et la plus accidentee -------------------------
	const int32 NX = World.Geometry.NX;
	const int32 NY = World.Geometry.NY;
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
	const double CentreX = (static_cast<double>(Meilleure % NX) - NX * 0.5) * MetresParCellule;
	const double CentreY = (static_cast<double>(Meilleure / NX) - NY * 0.5) * MetresParCellule;

	const double Cote = FMath::Max(ChunkSideM, 4);
	const int32 Colonnes = FMath::Clamp(ChunksPerSide, 1, 8);
	const double Depart = -Cote * Colonnes * 0.5;
	const double Voxel = DensityRules.VoxelSizeM;

	FReleve Moteur;
	FReleve Trans;

	// LE TEMOIN QUI TRANCHE L'ECART D'AIRE.
	//
	// Le mailleur du moteur deborde d'un voxel de sa boite -- mesure, et non
	// suppose. Si c'est bien LA cause de l'ecart d'aire, alors le mailleur
	// maison lance sur une boite ELARGIE D'UN VOXEL doit retrouver l'aire du
	// moteur. Sinon, c'est que le mien rate vraiment de la surface, et c'est une
	// tout autre affaire. Un temoin qui separe les deux vaut mieux que quatre
	// corrections qui ne bougent pas la mesure.
	FReleve TransLarge;

	FWorldseedVoxelMesh MaillageA;
	FWorldseedVoxelMesh MaillageB;
	FWorldseedVoxelMesh MaillageC;
	FWorldseedVoxelStats StatsA;
	FWorldseedVoxelStats StatsB;
	FWorldseedVoxelStats StatsC;

	for (int32 CY = 0; CY < Colonnes; ++CY)
	{
		for (int32 CX = 0; CX < Colonnes; ++CX)
		{
			const double MinX = CentreX + Depart + CX * Cote;
			const double MinY = CentreY + Depart + CY * Cote;

			float SurfaceMin = 0.0f;
			float SurfaceMax = 0.0f;
			Density.SurfaceRangeM(MinX, MinY, MinX + Cote, MinY + Cote,
				SurfaceMin, SurfaceMax);

			const int32 EtageBas = FMath::FloorToInt(
				(SurfaceMin - DensityRules.BandDepthM) / Cote);
			const int32 EtageHaut = FMath::FloorToInt(SurfaceMax / Cote);

			for (int32 CZ = EtageBas; CZ <= EtageHaut; ++CZ)
			{
				const FBox Boite(
					FVector(MinX, MinY, CZ * Cote),
					FVector(MinX + Cote, MinY + Cote, (CZ + 1) * Cote));

				// --- le moteur ---
				double Debut = FPlatformTime::Seconds();
				const bool bA = WorldseedVoxelChunk::Build(
					Density, nullptr, Boite, Voxel, MaillageA, StatsA, nullptr, false);
				Moteur.TotalMs += (FPlatformTime::Seconds() - Debut) * 1000.0;
				Moteur.Evaluations += StatsA.FieldSamples;

				// --- le notre ---
				Debut = FPlatformTime::Seconds();
				const bool bB = WorldseedTransvoxel::Mailler(
					Density, nullptr, Boite, Voxel,
					WorldseedTransvoxel::AucuneFace, MaillageB, StatsB, nullptr);
				Trans.TotalMs += (FPlatformTime::Seconds() - Debut) * 1000.0;
				Trans.Evaluations += StatsB.FieldSamples;

				// UN CHUNK OU LES DEUX NE S'ACCORDENT MEME PAS SUR L'EXISTENCE
				// D'UNE SURFACE EST DEJA UN ECART, et il faut qu'il se voie :
				// ne compter que les chunks ou les deux ont produit quelque
				// chose masquerait exactement le defaut qu'on cherche.
				if (bA != bB)
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[Sonde] transvoxel : DESACCORD sur la presence de surface ")
						TEXT("au chunk (%.0f, %.0f, %.0f) -- moteur %s, transvoxel %s"),
						MinX, MinY, CZ * Cote,
						bA ? TEXT("oui") : TEXT("non"),
						bB ? TEXT("oui") : TEXT("non"));
				}

				if (bA)
				{
					++Moteur.Chunks;
					Moteur.Triangles += MaillageA.TriangleCount();
					Moteur.Sommets += MaillageA.Positions.Num();
					Compter(MaillageA, Boite, Voxel * 0.25, Moteur);
					Peser(MaillageA, Density, Moteur);
				}
				// --- le notre, sur la boite du moteur ---
				const FBox Elargie(Boite.Min, Boite.Max + FVector(Voxel));
				Debut = FPlatformTime::Seconds();
				const bool bC = WorldseedTransvoxel::Mailler(
					Density, nullptr, Elargie, Voxel,
					WorldseedTransvoxel::AucuneFace, MaillageC, StatsC, nullptr);
				TransLarge.TotalMs += (FPlatformTime::Seconds() - Debut) * 1000.0;
				if (bC)
				{
					++TransLarge.Chunks;
					TransLarge.Triangles += MaillageC.TriangleCount();
					Compter(MaillageC, Elargie, Voxel * 0.25, TransLarge);
				}

				if (bB)
				{
					++Trans.Chunks;
					Trans.Triangles += MaillageB.TriangleCount();
					Trans.Sommets += MaillageB.Positions.Num();
					Compter(MaillageB, Boite, Voxel * 0.25, Trans);
					Peser(MaillageB, Density, Trans);
					Trans.EnroulementCumule += StatsB.FacesEndroit;
					++Trans.EnroulementComptes;
				}
			}
		}
	}

	auto Moyenne = [](const FReleve& R)
	{
		return R.SommetsPeses > 0 ? R.EcartMoyen / R.SommetsPeses : 0.0;
	};

	const double EcartAire = (Moteur.AireM2 > 0.0)
		? FMath::Abs(Trans.AireM2 - Moteur.AireM2) / Moteur.AireM2 * 100.0 : 0.0;

	UE_LOG(LogTemp, Log, TEXT("[Sonde] === TRANSVOXEL contre le mailleur du moteur ==="));
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   %-12s %10s %10s %14s %12s %12s %10s %8s"),
		TEXT("mailleur"), TEXT("chunks"), TEXT("triangles"), TEXT("aire m2"),
		TEXT("|dens| moy"), TEXT("|dens| max"), TEXT("bords"), TEXT("ms"));
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   %-12s %10d %10d %14.1f %12.4f %12.4f %10d %8.0f"),
		TEXT("moteur"), Moteur.Chunks, Moteur.Triangles, Moteur.AireM2,
		Moyenne(Moteur), Moteur.EcartMax, Moteur.AretesDeBord, Moteur.TotalMs);
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   %-12s %10d %10d %14.1f %12.4f %12.4f %10d %8.0f"),
		TEXT("transvoxel"), Trans.Chunks, Trans.Triangles, Trans.AireM2,
		Moyenne(Trans), Trans.EcartMax, Trans.AretesDeBord, Trans.TotalMs);

	const double EcartLarge = (Moteur.AireM2 > 0.0)
		? FMath::Abs(TransLarge.AireM2 - Moteur.AireM2) / Moteur.AireM2 * 100.0 : 0.0;

	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   %-12s %10d %10d %14.1f %12s %12s %10d %8.0f"),
		TEXT("+1 voxel"), TransLarge.Chunks, TransLarge.Triangles, TransLarge.AireM2,
		TEXT("-"), TEXT("-"), TransLarge.AretesDeBord, TransLarge.TotalMs);
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   « +1 voxel » = le mailleur maison sur la boite ELARGIE d'un ")
		TEXT("voxel, c'est-a-dire sur l'emprise que le moteur maille REELLEMENT. ")
		TEXT("Ecart d'aire au moteur : %.3f %%"),
		EcartLarge);

	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   debordement hors de la boite du chunk : moteur %.2f m, ")
		TEXT("transvoxel %.2f m (voxel %.2f m)  --  positif = le mailleur empiete ")
		TEXT("sur le chunk voisin"),
		Moteur.DebordMax, Trans.DebordMax, Voxel);

	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   ecart d'aire %.3f %%  |  aretes hors paroi : moteur %d, ")
		TEXT("transvoxel %d  |  aretes tordues : moteur %d, transvoxel %d"),
		EcartAire, Moteur.AretesHorsParoi, Trans.AretesHorsParoi,
		Moteur.AretesTordues, Trans.AretesTordues);

	const double Enroulement = Trans.EnroulementComptes > 0
		? Trans.EnroulementCumule / Trans.EnroulementComptes * 100.0 : 0.0;
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   enroulement transvoxel : %.1f %% des faces a l'endroit ")
		TEXT("-- il faut 100, et 0 veut dire qu'il suffit d'inverser"),
		Enroulement);

	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   Lecture : c'est l'AIRE qui doit coincider, pas le nombre de ")
		TEXT("triangles -- deux maillages corrects de la meme surface n'ont aucune ")
		TEXT("raison d'avoir le meme decoupage. Et |densite| aux sommets juge chaque ")
		TEXT("mailleur SEUL : un sommet pose sur l'isovaleur y vaut zero."));

	const FString Resume = FString::Printf(
		TEXT("aire %.1f contre %.1f m2 (%.3f %%), |dens| %.4f contre %.4f, ")
		TEXT("bords hors paroi %d contre %d, enroulement %.0f %%, %.0f contre %.0f ms"),
		Trans.AireM2, Moteur.AireM2, EcartAire,
		Moyenne(Trans), Moyenne(Moteur),
		Trans.AretesHorsParoi, Moteur.AretesHorsParoi,
		Enroulement, Trans.TotalMs, Moteur.TotalMs);

	UE_LOG(LogTemp, Log, TEXT("[Sonde] %s"), *Resume);
	return Resume;
}
