// Worldseed - sonde du voisinage : deux chunks de MEME niveau se rejoignent-ils ?
//
// UNE QUESTION PAR FICHIER, et celle-ci n'avait jamais ete posee. ProbeTransvoxel
// compare un chunk ISOLE au mailleur du moteur -- meme surface a 0,005 % pres.
// ProbeTransition demande si un chunk GROSSIER et un chunk FIN se rejoignent --
// zero arete ouverte contre 58. Aucune des deux ne regarde la configuration que
// le jeu emploie REELLEMENT et en permanence : des chunks tous de MEME taille,
// masque de transition NUL, poses cote a cote.
//
// C'est le seul cas qu'aucun temoin ne couvre, et c'est celui qui dechire.

#include "Procedural/WorldseedProbeLibrary.h"

#include "Procedural/WorldseedProbeCommun.h"

#include "Procedural/WorldseedVoxelChunk.h"

namespace
{
	/**
	 * Ce qu'on releve sur un BLOC de chunks cousus par la position.
	 *
	 * L'ARETE OUVERTE EST LE SEUL CRITERE, et il ne suppose rien : dans un
	 * maillage sain, toute arete interieure appartient a EXACTEMENT DEUX
	 * triangles. Une arete qui n'en a qu'un est un bord.
	 *
	 * TOUT L'ART EST DANS LE CLASSEMENT DE CES BORDS. Un bloc de chunks est
	 * decoupe dans un monde plus grand : la surface en sort par ses six faces
	 * exterieures, et les aretes ouvertes qu'on y trouve sont LEGITIMES. Celles
	 * qui ne touchent aucune face exterieure ne le sont pas -- ce sont des trous,
	 * et sur un bloc de chunks identiques elles ne peuvent etre qu'aux jointures.
	 */
	struct FBloc
	{
		int32 Chunks = 0;
		int32 ChunksVides = 0;
		int64 TrianglesAvantCouture = 0;

		int32 Sommets = 0;
		int32 Triangles = 0;

		/** Faces dont l.enroulement s.accorde avec le GRADIENT du champ. */
		int32 FacesJugees = 0;
		int32 FacesEndroit = 0;

		int32 AretesLongues = 0;
		double PlusLongueM = 0.0;
		FVector PlusLongueOuM = FVector::ZeroVector;

		int32 Aretes = 0;
		int32 AretesOuvertes = 0;
		int32 AretesOuvertesInterieures = 0;

		/** Le pire trou, pour pouvoir aller le regarder. */
		FVector PireM = FVector::ZeroVector;

		double Ms = 0.0;
	};

	/** Quantification d'une position, pour souder des maillages independants. */
	FIntVector Grain(const FVector& PosCm)
	{
		return FIntVector(
			FMath::RoundToInt(PosCm.X * 16.0),
			FMath::RoundToInt(PosCm.Y * 16.0),
			FMath::RoundToInt(PosCm.Z * 16.0));
	}
}

FString UWorldseedProbeLibrary::ProbeVoisins(int32 Seed, float HeightMeters,
	int32 ResolutionY, int32 Cotes, float CoteM, float CibleXM, float CibleYM)
{
	FWorldseedSonde S;
	if (!S.Preparer(Seed, HeightMeters, ResolutionY))
	{
		UE_LOG(LogTemp, Error, TEXT("[Sonde] %s"), *S.Erreur);
		return S.Erreur;
	}

	const FWorldseedGeometry& Geo = S.World.Geometry;

	// --- se placer sur de la TERRE, et la plus accidentee -------------------
	int32 Meilleure = INDEX_NONE;
	float PlusHaut = 0.0f;
	for (int32 C = 0; C < S.World.ElevationM.Num(); ++C)
	{
		if (S.World.ElevationM[C] > PlusHaut) { PlusHaut = S.World.ElevationM[C]; Meilleure = C; }
	}
	if (Meilleure == INDEX_NONE) { return TEXT("aucune terre emergee"); }

	const double MpP = Geo.MetersPerPixel();
	double CX = (static_cast<double>(Meilleure % Geo.NX) - Geo.NX * 0.5) * MpP;
	double CY = (static_cast<double>(Meilleure / Geo.NX) - Geo.NY * 0.5) * MpP;

	// ON MESURE LA OU LE DEFAUT A ETE VU, PAS LA OU C.EST COMMODE. Le sommet du
	// monde est le point le plus accidente, donc le plus flatteur pour un
	// mailleur ; la forme signalee est au LITTORAL. Le depot a deja paye de
	// mesurer une forme rare au mauvais endroit et d.en conclure qu.elle
	// n.existait pas.
	if (CibleXM != 0.0f || CibleYM != 0.0f)
	{
		CX = CibleXM;
		CY = CibleYM;
		PlusHaut = S.Densite.SurfaceHeightM(CX, CY);
	}

	const double Cote = FMath::Max(static_cast<double>(CoteM), 4.0);
	const int32 NC = FMath::Clamp(Cotes, 2, 6);

	// Le bloc est ALIGNE SUR LA GRILLE DES CHUNKS, comme le diffuseur le fait :
	// des boites posees ailleurs ne partageraient pas leurs plans et la mesure
	// ne dirait rien du jeu.
	const double BaseX = FMath::FloorToDouble(CX / Cote) * Cote - (NC / 2) * Cote;
	const double BaseY = FMath::FloorToDouble(CY / Cote) * Cote - (NC / 2) * Cote;
	const double BaseZ = FMath::FloorToDouble(PlusHaut / Cote) * Cote - (NC / 2) * Cote;

	const FVector BlocMinM(BaseX, BaseY, BaseZ);
	const FVector BlocMaxM(BaseX + NC * Cote, BaseY + NC * Cote, BaseZ + NC * Cote);

	// --- une passe par mailleur ---------------------------------------------
	//
	// LE MEME BLOC, LES MEMES BOITES, LE MEME CHAMP. Le seul ecart entre les
	// deux passes est le mailleur, donc tout ecart de resultat lui revient.
	FBloc Resultat[2];

	for (int32 Passe = 0; Passe < 2; ++Passe)
	{
		const bool bTransvoxel = (Passe == 1);
		FBloc& R = Resultat[Passe];
		const double Debut = FPlatformTime::Seconds();

		TMap<FIntVector, int32> Index;
		TArray<FVector> Positions;
		TArray<int32> Triangles;

		FWorldseedVoxelMesh Maillage;
		FWorldseedVoxelStats Stats;

		for (int32 KZ = 0; KZ < NC; ++KZ)
		{
			for (int32 JY = 0; JY < NC; ++JY)
			{
				for (int32 IX = 0; IX < NC; ++IX)
				{
					const FVector Min(BaseX + IX * Cote, BaseY + JY * Cote, BaseZ + KZ * Cote);
					const FBox Boite(Min, Min + FVector(Cote, Cote, Cote));

					++R.Chunks;

					// MASQUE NUL ET RESOLUTION UNIFORME : c'est exactement ce que
					// le diffuseur produit quand aucun anneau n'est arme, donc
					// exactement ce que le jeu affiche.
					const bool bPlein = WorldseedVoxelChunk::Build(
						S.Densite, nullptr, Boite, S.DensiteRegles.VoxelSizeM,
						Maillage, Stats, nullptr, bTransvoxel, 0, 0.5f);

					if (!bPlein) { ++R.ChunksVides; continue; }

					R.TrianglesAvantCouture += Maillage.TriangleCount();

					TArray<int32> Remap;
					Remap.SetNumUninitialized(Maillage.Positions.Num());
					for (int32 I = 0; I < Maillage.Positions.Num(); ++I)
					{
						const FIntVector G = Grain(Maillage.Positions[I]);
						if (const int32* Deja = Index.Find(G))
						{
							Remap[I] = *Deja;
						}
						else
						{
							const int32 Neuf = Positions.Num();
							Positions.Add(Maillage.Positions[I]);
							Index.Add(G, Neuf);
							Remap[I] = Neuf;
						}
					}
					for (int32 T = 0; T + 2 < Maillage.Triangles.Num(); T += 3)
					{
						Triangles.Add(Remap[Maillage.Triangles[T]]);
						Triangles.Add(Remap[Maillage.Triangles[T + 1]]);
						Triangles.Add(Remap[Maillage.Triangles[T + 2]]);
					}
				}
			}
		}

		R.Sommets = Positions.Num();
		R.Triangles = Triangles.Num() / 3;

		// --- L.ENROULEMENT, JUGE CONTRE LE CHAMP ET NON CONTRE L.AUTRE ------
		//
		// LES DEUX MAILLEURS SONT JUGES PAR LE MEME ARBITRE, QUI N.EST NI L.UN
		// NI L.AUTRE. Le gradient du champ de densite pointe vers l.air : c.est
		// le dehors, et il ne doit rien a une convention d.enroulement, ni a la
		// main du repere, ni aux normales que l.un ou l.autre a bien voulu
		// ecrire. Comparer la normale GEOMETRIQUE d.un triangle -- celle que son
		// ordre de sommets impose -- a cette direction-la dit donc lequel des
		// deux est a l.endroit, sans supposer que l.autre ait raison.
		//
		// C.EST LE CONTROLE QUI MANQUAIT. Celui de ProbeTransvoxel compare un
		// mailleur A SES PROPRES normales : il est auto-coherent, donc il ne peut
		// pas voir une convention fausse partagee par les deux moities.
		{
			const double H = 0.25 * S.DensiteRegles.VoxelSizeM;
			const int32 Pas = FMath::Max(Triangles.Num() / (3 * 20000), 1) * 3;

			for (int32 T = 0; T + 2 < Triangles.Num(); T += Pas)
			{
				const FVector A = Positions[Triangles[T]];
				const FVector B = Positions[Triangles[T + 1]];
				const FVector C = Positions[Triangles[T + 2]];

				const FVector Face = FVector::CrossProduct(B - A, C - A);
				if (Face.IsNearlyZero()) { continue; }

				const FVector P = (A + B + C) / (3.0 * WorldseedMetersToCm);
				const FVector Grad(
					S.Densite.At(FVector(P.X + H, P.Y, P.Z)) - S.Densite.At(FVector(P.X - H, P.Y, P.Z)),
					S.Densite.At(FVector(P.X, P.Y + H, P.Z)) - S.Densite.At(FVector(P.X, P.Y - H, P.Z)),
					S.Densite.At(FVector(P.X, P.Y, P.Z + H)) - S.Densite.At(FVector(P.X, P.Y, P.Z - H)));
				if (Grad.IsNearlyZero()) { continue; }

				++R.FacesJugees;
				if (FVector::DotProduct(Face, Grad) > 0.0) { ++R.FacesEndroit; }
			}
		}

		// --- les aretes ------------------------------------------------------
		TMap<uint64, int32> Aretes;
		Aretes.Reserve(Triangles.Num());
		auto Noter = [&Aretes](int32 X, int32 Y)
		{
			const uint64 Lo = static_cast<uint64>(FMath::Min(X, Y));
			const uint64 Hi = static_cast<uint64>(FMath::Max(X, Y));
			++Aretes.FindOrAdd((Lo << 32) | Hi, 0);
		};

		// --- LA LONGUEUR D.ARETE, CRITERE AUTONOME ET SANS TEMOIN ----------
		//
		// Un triangle de marching cubes a ses trois sommets sur les aretes
		// d.UNE cellule : aucune de ses aretes ne peut donc depasser la
		// diagonale de cette cellule, soit racine de trois voxels. C.est une
		// borne de CONSTRUCTION, pas une esperance -- elle ne suppose ni
		// l.autre mailleur, ni le champ, ni la convention d.enroulement.
		//
		// Elle voit precisement ce qu.une arete ouverte ne peut PAS voir : un
		// maillage reste combinatoirement clos quand ses triangles pointent
		// vers le mauvais sommet, et ne trahit alors que par la LONGUEUR.
		const double Borne = 3.0 * S.DensiteRegles.VoxelSizeM * WorldseedMetersToCm;

		for (int32 T = 0; T + 2 < Triangles.Num(); T += 3)
		{
			Noter(Triangles[T], Triangles[T + 1]);
			Noter(Triangles[T + 1], Triangles[T + 2]);
			Noter(Triangles[T + 2], Triangles[T]);
		}

		// Un centimetre de tolerance : les sommets de bord sont poses A LA MAIN
		// sur le plan par l'interpolation, donc ils y tombent exactement, mais la
		// mesure ne doit pas dependre de cette esperance.
		const double TolCm = 1.0;
		const FVector MinCm = BlocMinM * WorldseedMetersToCm;
		const FVector MaxCm = BlocMaxM * WorldseedMetersToCm;

		auto SurLeBord = [&](int32 I)
		{
			const FVector& P = Positions[I];
			for (int32 A = 0; A < 3; ++A)
			{
				if (FMath::Abs(P[A] - MinCm[A]) <= TolCm) { return true; }
				if (FMath::Abs(P[A] - MaxCm[A]) <= TolCm) { return true; }
			}
			return false;
		};

		for (const TPair<uint64, int32>& P : Aretes)
		{
			++R.Aretes;

			const int32 X = static_cast<int32>(P.Key >> 32);
			const int32 Y = static_cast<int32>(P.Key & 0xFFFFFFFFull);

			const double L = FVector::Dist(Positions[X], Positions[Y]);
			if (L > Borne)
			{
				++R.AretesLongues;
				if (L > R.PlusLongueM * WorldseedMetersToCm)
				{
					R.PlusLongueM = L / WorldseedMetersToCm;
					R.PlusLongueOuM = Positions[X] / WorldseedMetersToCm;
				}
			}

			if (P.Value != 1) { continue; }
			++R.AretesOuvertes;

			// UNE ARETE OUVERTE QUI TOUCHE UNE FACE EXTERIEURE DU BLOC EST
			// LEGITIME : le bloc est decoupe dans un monde plus grand, la surface
			// en sort. Celle qui n'en touche aucune est un TROU.
			if (SurLeBord(X) || SurLeBord(Y)) { continue; }

			++R.AretesOuvertesInterieures;
			if (R.AretesOuvertesInterieures == 1)
			{
				R.PireM = Positions[X] / WorldseedMetersToCm;
			}
		}

		R.Ms = (FPlatformTime::Seconds() - Debut) * 1000.0;
	}

	const FBloc& Moteur = Resultat[0];
	const FBloc& Trans = Resultat[1];

	UE_LOG(LogTemp, Log, TEXT("[Sonde] === DEUX CHUNKS DE MEME NIVEAU SE REJOIGNENT-ILS ? ==="));
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   bloc de %d x %d x %d chunks de %.0f m, coin (%.0f, %.0f, %.0f) m, ")
		TEXT("masque de transition NUL"),
		NC, NC, NC, Cote, BaseX, BaseY, BaseZ);

	auto Dire = [](const TCHAR* Nom, const FBloc& R)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Sonde]   %-11s %d chunks (%d vides)  |  %lld triangles avant couture, ")
			TEXT("%d apres  |  %d sommets  |  %.0f ms"),
			Nom, R.Chunks, R.ChunksVides, R.TrianglesAvantCouture, R.Triangles,
			R.Sommets, R.Ms);
		UE_LOG(LogTemp, Log,
			TEXT("[Sonde]   %-11s %d aretes, %d ouvertes dont %d INTERIEURES AU BLOC"),
			TEXT(""), R.Aretes, R.AretesOuvertes, R.AretesOuvertesInterieures);
		UE_LOG(LogTemp, Log,
			TEXT("[Sonde]   %-11s ENROULEMENT contre le gradient du champ : %.1f %% ")
			TEXT("a l-endroit sur %d faces jugees"),
			TEXT(""), R.FacesJugees > 0 ? 100.0 * R.FacesEndroit / R.FacesJugees : 0.0,
			R.FacesJugees);
		UE_LOG(LogTemp, Log,
			TEXT("[Sonde]   %-11s %d aretes PLUS LONGUES QUE TROIS VOXELS, la pire %.1f m ")
			TEXT("a (%.0f, %.0f, %.0f) m"),
			TEXT(""), R.AretesLongues, R.PlusLongueM,
			R.PlusLongueOuM.X, R.PlusLongueOuM.Y, R.PlusLongueOuM.Z);
		if (R.AretesOuvertesInterieures > 0)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Sonde]   %-11s premier trou a (%.1f, %.1f, %.1f) m"),
				TEXT(""), R.PireM.X, R.PireM.Y, R.PireM.Z);
		}
	};

	Dire(TEXT("moteur"), Moteur);
	Dire(TEXT("transvoxel"), Trans);

	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   Lecture : une arete ouverte INTERIEURE au bloc est un trou, ")
		TEXT("et sur un bloc de chunks tous identiques elle ne peut etre qu'a une ")
		TEXT("JOINTURE. Le mailleur du moteur deborde d'un voxel, donc il recouvre ")
		TEXT("ses jointures et doit rendre zero ; le mailleur maison ne deborde pas ")
		TEXT("et c'est precisement ce que cette sonde eprouve."));

	const FString Resume = FString::Printf(
		TEXT("bloc %dx%dx%d de %.0f m : moteur %d aretes ouvertes interieures ")
		TEXT("(%lld tri), transvoxel %d (%lld tri)"),
		NC, NC, NC, Cote,
		Moteur.AretesOuvertesInterieures, Moteur.TrianglesAvantCouture,
		Trans.AretesOuvertesInterieures, Trans.TrianglesAvantCouture);

	UE_LOG(LogTemp, Log, TEXT("[Sonde] %s"), *Resume);
	return Resume;
}
