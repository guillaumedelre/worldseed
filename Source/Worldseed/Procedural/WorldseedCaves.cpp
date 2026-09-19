// Worldseed - le RESEAU de grottes : chambres, liaisons, et connexite garantie.

#include "Procedural/WorldseedCaves.h"

#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedLithology.h"

namespace
{
	/**
	 * Union LISSE d'Inigo Quilez, ecrite du cote de l'air.
	 *
	 * Un maximum dur entre deux vides produit une arete vive a leur jonction :
	 * l'effet est celui de tuyaux colles. Le melange polynomial arrondit le
	 * raccord sur un rayon K, et DEGENERE EXACTEMENT VERS LE MAXIMUM DUR quand
	 * K tend vers zero -- ce qui en fait un reglage continu entre les deux, et
	 * non un choix binaire.
	 */
	double UnionLisse(double A, double B, double K)
	{
		if (K <= 1e-4)
		{
			return FMath::Max(A, B);
		}
		const double H = FMath::Clamp(0.5 + 0.5 * (A - B) / K, 0.0, 1.0);
		return FMath::Lerp(B, A, H) + K * H * (1.0 - H);
	}

	/** Distance d'un point au segment [A,B], et sa coordonnee le long du segment. */
	double DistanceAuSegment(const FVector& P, const FVector& A, const FVector& B,
		double& OutT)
	{
		const FVector AB = B - A;
		const double Len2 = AB.SizeSquared();
		OutT = (Len2 > 1e-6) ? FMath::Clamp(FVector::DotProduct(P - A, AB) / Len2, 0.0, 1.0) : 0.0;
		return FVector::Dist(P, A + AB * OutT);
	}

	/** Generateur deterministe : le monde doit se rejouer a l'identique. */
	struct FTirage
	{
		FRandomStream Flux;
		explicit FTirage(int32 Graine) : Flux(Graine) {}
		float Entre(float A, float B) { return Flux.FRandRange(A, B); }
		int32 Index(int32 N) { return (N > 0) ? Flux.RandRange(0, N - 1) : 0; }
	};
}

// --------------------------------------------------------------------------

FWorldseedCaveRules FWorldseedCaveRules::FromRules(const UWorldseedRules& Rules)
{
	const TCHAR* CAV = TEXT("cavites");
	auto Num = [&Rules, CAV](const TCHAR* Key, double Fallback)
	{
		return static_cast<float>(Rules.Num(CAV, Key, Fallback));
	};

	FWorldseedCaveRules Out;
	Out.ChamberSpacingM = Num(TEXT("chambreEspacementM"), 180.0);
	Out.DepthMinM = Num(TEXT("profondeurMinM"), 20.0);
	Out.DepthMaxM = Num(TEXT("profondeurMaxM"), 90.0);
	Out.ChamberRadiusMinM = Num(TEXT("chambreRayonMinM"), 6.0);
	Out.ChamberRadiusMaxM = Num(TEXT("chambreRayonMaxM"), 22.0);
	Out.MinPrecipMm = Num(TEXT("pluieMinMm"), 600.0);
	Out.MinKarstifiable = Num(TEXT("karstifiableMin"), 0.5);
	Out.TunnelRadiusMinM = Num(TEXT("galerieRayonMinM"), 1.5);
	Out.TunnelRadiusMaxM = Num(TEXT("galerieRayonMaxM"), 4.0);
	Out.LoopPct = Num(TEXT("bouclesPct"), 15.0);
	Out.Neighbours = Rules.Int(CAV, TEXT("voisinsCandidats"), 8);
	Out.BlendM = Num(TEXT("raccordM"), 2.5);
	return Out;
}

// --------------------------------------------------------------------------

void FWorldseedCaveNetwork::Reset()
{
	Chambers.Reset();
	Segments.Reset();
	ChamberBuckets.Reset();
	SegmentBuckets.Reset();
	Size = FIntPoint::ZeroValue;
}

void FWorldseedCaveNetwork::Query(const FBox& BoxM, FWorldseedCaveLocal& Out) const
{
	Out.Network = this;
	Out.Chambers.Reset();
	Out.Segments.Reset();

	if (Size.X <= 0 || Size.Y <= 0)
	{
		return;
	}

	const int32 I0 = FMath::Clamp(FMath::FloorToInt(BoxM.Min.X / CellM) - Min.X, 0, Size.X - 1);
	const int32 I1 = FMath::Clamp(FMath::FloorToInt(BoxM.Max.X / CellM) - Min.X, 0, Size.X - 1);
	const int32 J0 = FMath::Clamp(FMath::FloorToInt(BoxM.Min.Y / CellM) - Min.Y, 0, Size.Y - 1);
	const int32 J1 = FMath::Clamp(FMath::FloorToInt(BoxM.Max.Y / CellM) - Min.Y, 0, Size.Y - 1);

	for (int32 J = J0; J <= J1; ++J)
	{
		for (int32 I = I0; I <= I1; ++I)
		{
			const int32 B = J * Size.X + I;
			if (ChamberBuckets.IsValidIndex(B))
			{
				for (const int32 K : ChamberBuckets[B]) { Out.Chambers.AddUnique(K); }
			}
			if (SegmentBuckets.IsValidIndex(B))
			{
				for (const int32 K : SegmentBuckets[B]) { Out.Segments.AddUnique(K); }
			}
		}
	}
}

// --------------------------------------------------------------------------

double WorldseedCaves::AirAt(const FWorldseedCaveLocal& Local, const FVector& PosM,
	float BlendM)
{
	if (!Local.Network || Local.IsEmpty())
	{
		return -1.0;
	}

	// On part de "tres loin dans la roche" et on unit les primitives locales.
	double Air = -1e6;

	for (const int32 I : Local.Chambers)
	{
		const FWorldseedCaveChamber& C = Local.Network->Chambers[I];
		Air = UnionLisse(Air, C.RadiusM - FVector::Dist(PosM, C.CentreM), BlendM);
	}

	for (const int32 I : Local.Segments)
	{
		const FWorldseedCaveSegment& S = Local.Network->Segments[I];
		double T = 0.0;
		const double D = DistanceAuSegment(PosM, S.AM, S.BM, T);
		const double R = FMath::Lerp(S.RadiusAM, S.RadiusBM, T);
		Air = UnionLisse(Air, R - D, BlendM);
	}

	return Air;
}

// --------------------------------------------------------------------------

void WorldseedCaves::Build(const FWorldseedGeometry& Geometry,
	const TArray<float>& ElevationM, const TArray<float>& PrecipMm,
	const FWorldseedLithology& Lithology, const FWorldseedLithologyRules& LithoRules,
	const FWorldseedCaveRules& Rules, float HeightExaggeration, int32 Seed,
	FWorldseedCaveNetwork& Out)
{
	const double StartTime = FPlatformTime::Seconds();

	Out.Reset();

	const int32 NX = Geometry.NX;
	const int32 NY = Geometry.NY;
	const int32 Count = Geometry.CellCount();

	if (Count <= 0 || ElevationM.Num() != Count || Rules.ChamberSpacingM <= 0.0f)
	{
		return;
	}

	const bool bHasPrecip = (PrecipMm.Num() == Count);
	const bool bHasLitho = Lithology.IsValid(Count);
	const float MetresParPixel = FMath::Max(Geometry.MetersPerPixel(), 1e-3f);

	// --- 1. OU une grotte peut exister ---------------------------------------
	//
	// C'EST LA ROCHE QUI DECIDE, PAS LE BIOME. Un karst se creuse par
	// dissolution : il lui faut du calcaire et de l'eau. Le climat n'entre donc
	// que par la pluie, qui est un champ continu -- jamais par l'etiquette de
	// biome, qui ne sert qu'a lier des assets.
	TArray<int32> Candidates;
	Candidates.Reserve(Count / 8);
	for (int32 I = 0; I < Count; ++I)
	{
		if (ElevationM[I] <= 0.0f) { continue; }
		if (bHasPrecip && PrecipMm[I] < Rules.MinPrecipMm) { continue; }
		if (bHasLitho)
		{
			const uint8 R = Lithology.Id[I];
			if (!LithoRules.Catalogue.IsValidIndex(R)
				|| LithoRules.Catalogue[R].Karstifiable < Rules.MinKarstifiable)
			{
				continue;
			}
		}
		Candidates.Add(I);
	}

	if (Candidates.Num() == 0)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] grottes : aucune terre karstifiable assez arrosee"));
		return;
	}

	// --- 2. les chambres, par echantillonnage a distance minimale -------------
	//
	// Un tirage uniforme donne des amas et des vides ; l'espacement minimal
	// garantit une repartition reguliere, qui est ce qu'on veut d'un reseau.
	// On refuse par une grille de hachage plutot qu'en comparant a toutes les
	// chambres deja posees, sans quoi le cout serait quadratique.
	FTirage Tirage(Seed + 5521);

	const float Espacement = Rules.ChamberSpacingM;
	const float CelluleM = FMath::Max(Espacement, 1.0f);
	Out.CellM = CelluleM;

	const double DemiLargeur = Geometry.WidthM() * 0.5;
	const double DemiHauteur = Geometry.HeightM * 0.5;
	Out.Min = FIntPoint(FMath::FloorToInt(-DemiLargeur / CelluleM),
		FMath::FloorToInt(-DemiHauteur / CelluleM));
	Out.Size = FIntPoint(
		FMath::CeilToInt(DemiLargeur / CelluleM) - Out.Min.X + 2,
		FMath::CeilToInt(DemiHauteur / CelluleM) - Out.Min.Y + 2);

	const int32 Cases = Out.Size.X * Out.Size.Y;
	TArray<TArray<int32>> Hachage;
	Hachage.SetNum(Cases);

	auto CaseDe = [&Out](const FVector& P) -> int32
	{
		const int32 I = FMath::Clamp(FMath::FloorToInt(P.X / Out.CellM) - Out.Min.X, 0, Out.Size.X - 1);
		const int32 J = FMath::Clamp(FMath::FloorToInt(P.Y / Out.CellM) - Out.Min.Y, 0, Out.Size.Y - 1);
		return J * Out.Size.X + I;
	};

	// Autant d'essais que de cellules candidates : au-dela le refus domine et on
	// n'ajoute plus rien.
	const int32 Essais = Candidates.Num();
	for (int32 E = 0; E < Essais; ++E)
	{
		const int32 Cell = Candidates[Tirage.Index(Candidates.Num())];
		const int32 Col = Cell % NX;
		const int32 Row = Cell / NX;

		const double X = (static_cast<double>(Col) / NX - 0.5) * Geometry.WidthM();
		const double Y = (static_cast<double>(Row) / NY - 0.5) * Geometry.HeightM;
		const double Surface = ElevationM[Cell] * HeightExaggeration;

		const float Profondeur = Tirage.Entre(Rules.DepthMinM, Rules.DepthMaxM);
		const FVector P(X, Y, Surface - Profondeur);
		const float Rayon = Tirage.Entre(Rules.ChamberRadiusMinM, Rules.ChamberRadiusMaxM);

		// Refus : trop pres d'une chambre deja posee ?
		bool bTropPres = false;
		const int32 Ci = FMath::Clamp(FMath::FloorToInt(P.X / CelluleM) - Out.Min.X, 0, Out.Size.X - 1);
		const int32 Cj = FMath::Clamp(FMath::FloorToInt(P.Y / CelluleM) - Out.Min.Y, 0, Out.Size.Y - 1);
		for (int32 dj = -1; dj <= 1 && !bTropPres; ++dj)
		{
			for (int32 di = -1; di <= 1 && !bTropPres; ++di)
			{
				const int32 Ii = Ci + di;
				const int32 Jj = Cj + dj;
				if (Ii < 0 || Jj < 0 || Ii >= Out.Size.X || Jj >= Out.Size.Y) { continue; }
				for (const int32 K : Hachage[Jj * Out.Size.X + Ii])
				{
					if (FVector::Dist(P, Out.Chambers[K].CentreM) < Espacement)
					{
						bTropPres = true;
						break;
					}
				}
			}
		}
		if (bTropPres) { continue; }

		FWorldseedCaveChamber C;
		C.CentreM = P;
		C.RadiusM = Rayon;
		const int32 Index = Out.Chambers.Add(C);
		Hachage[CaseDe(P)].Add(Index);
	}

	const int32 N = Out.Chambers.Num();
	if (N < 2)
	{
		UE_LOG(LogTemp, Log, TEXT("[Worldseed] grottes : %d chambre(s), rien a relier"), N);
		return;
	}

	// --- 3. l'arbre couvrant minimal ------------------------------------------
	//
	// LA CONNEXITE EST ACQUISE PAR CONSTRUCTION. Un arbre couvrant touche tous
	// les sommets, par definition : il n'y a donc RIEN a verifier apres coup, et
	// aucune poche isolee possible entre chambres. C'est tout l'interet par
	// rapport au bruit, qui produit des cavernes credibles sans jamais garantir
	// qu'elles communiquent.
	//
	// Prim sur le graphe complet serait en N carre ; on se contente des voisins
	// les plus proches, ce qui suffit largement a un semis regulier.
	TArray<float> MeilleureArete;
	TArray<int32> Parent;
	TArray<bool> Dedans;
	MeilleureArete.Init(TNumericLimits<float>::Max(), N);
	Parent.Init(INDEX_NONE, N);
	Dedans.Init(false, N);
	MeilleureArete[0] = 0.0f;

	TArray<TPair<int32, int32>> Aretes;
	Aretes.Reserve(N);

	for (int32 Pas = 0; Pas < N; ++Pas)
	{
		int32 Meilleur = INDEX_NONE;
		float MeilleurCout = TNumericLimits<float>::Max();
		for (int32 I = 0; I < N; ++I)
		{
			if (!Dedans[I] && MeilleureArete[I] < MeilleurCout)
			{
				MeilleurCout = MeilleureArete[I];
				Meilleur = I;
			}
		}
		if (Meilleur == INDEX_NONE) { break; }

		Dedans[Meilleur] = true;
		if (Parent[Meilleur] != INDEX_NONE)
		{
			Aretes.Add(TPair<int32, int32>(Parent[Meilleur], Meilleur));
		}

		for (int32 I = 0; I < N; ++I)
		{
			if (Dedans[I]) { continue; }
			const float D = FVector::Dist(Out.Chambers[Meilleur].CentreM, Out.Chambers[I].CentreM);
			if (D < MeilleureArete[I])
			{
				MeilleureArete[I] = D;
				Parent[I] = Meilleur;
			}
		}
	}

	// --- 4. des boucles ------------------------------------------------------
	//
	// Un arbre n'a aucun cycle : le joueur revient donc TOUJOURS sur ses pas.
	// On ajoute quelques aretes courtes, choisies parmi les paires les plus
	// proches qui ne sont pas deja reliees.
	const int32 ABoucler = FMath::RoundToInt(Aretes.Num() * Rules.LoopPct / 100.0f);
	if (ABoucler > 0)
	{
		TSet<uint64> Existantes;
		auto Cle = [](int32 A, int32 B) -> uint64
		{
			const uint64 Lo = FMath::Min(A, B);
			const uint64 Hi = FMath::Max(A, B);
			return (Hi << 32) | Lo;
		};
		for (const TPair<int32, int32>& E : Aretes) { Existantes.Add(Cle(E.Key, E.Value)); }

		TArray<TTuple<float, int32, int32>> Candidates2;
		for (int32 I = 0; I < N; ++I)
		{
			for (int32 J = I + 1; J < N; ++J)
			{
				if (Existantes.Contains(Cle(I, J))) { continue; }
				const float D = FVector::Dist(Out.Chambers[I].CentreM, Out.Chambers[J].CentreM);
				if (D < Espacement * 2.5f)
				{
					Candidates2.Add(MakeTuple(D, I, J));
				}
			}
		}
		Candidates2.Sort([](const TTuple<float, int32, int32>& A,
			const TTuple<float, int32, int32>& B) { return A.Get<0>() < B.Get<0>(); });

		for (int32 K = 0; K < FMath::Min(ABoucler, Candidates2.Num()); ++K)
		{
			Aretes.Add(TPair<int32, int32>(Candidates2[K].Get<1>(), Candidates2[K].Get<2>()));
		}
	}

	// --- 5. les galeries ------------------------------------------------------
	Out.Segments.Reserve(Aretes.Num());
	for (const TPair<int32, int32>& E : Aretes)
	{
		FWorldseedCaveSegment S;
		S.AM = Out.Chambers[E.Key].CentreM;
		S.BM = Out.Chambers[E.Value].CentreM;
		S.RadiusAM = Tirage.Entre(Rules.TunnelRadiusMinM, Rules.TunnelRadiusMaxM);
		S.RadiusBM = Tirage.Entre(Rules.TunnelRadiusMinM, Rules.TunnelRadiusMaxM);
		Out.Segments.Add(S);
	}

	// --- 6. l'index spatial ---------------------------------------------------
	Out.ChamberBuckets.SetNum(Cases);
	Out.SegmentBuckets.SetNum(Cases);

	auto Ranger = [&Out](TArray<TArray<int32>>& Buckets, const FBox& B, int32 Index)
	{
		const int32 I0 = FMath::Clamp(FMath::FloorToInt(B.Min.X / Out.CellM) - Out.Min.X, 0, Out.Size.X - 1);
		const int32 I1 = FMath::Clamp(FMath::FloorToInt(B.Max.X / Out.CellM) - Out.Min.X, 0, Out.Size.X - 1);
		const int32 J0 = FMath::Clamp(FMath::FloorToInt(B.Min.Y / Out.CellM) - Out.Min.Y, 0, Out.Size.Y - 1);
		const int32 J1 = FMath::Clamp(FMath::FloorToInt(B.Max.Y / Out.CellM) - Out.Min.Y, 0, Out.Size.Y - 1);
		for (int32 J = J0; J <= J1; ++J)
		{
			for (int32 I = I0; I <= I1; ++I)
			{
				Buckets[J * Out.Size.X + I].Add(Index);
			}
		}
	};

	for (int32 I = 0; I < Out.Chambers.Num(); ++I)
	{
		const FWorldseedCaveChamber& C = Out.Chambers[I];
		Ranger(Out.ChamberBuckets,
			FBox(C.CentreM - FVector(C.RadiusM), C.CentreM + FVector(C.RadiusM)), I);
	}
	for (int32 I = 0; I < Out.Segments.Num(); ++I)
	{
		const FWorldseedCaveSegment& S = Out.Segments[I];
		const float R = FMath::Max(S.RadiusAM, S.RadiusBM);
		FBox B(ForceInit);
		B += S.AM; B += S.BM;
		Ranger(Out.SegmentBuckets, B.ExpandBy(R), I);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] grottes : %d chambres, %d galeries (%d de boucle), ")
		TEXT("index %dx%d de %.0f m  (%.0f ms)"),
		Out.Chambers.Num(), Out.Segments.Num(), ABoucler,
		Out.Size.X, Out.Size.Y, Out.CellM,
		(FPlatformTime::Seconds() - StartTime) * 1000.0);
}
