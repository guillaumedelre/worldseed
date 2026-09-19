// Worldseed - le RESEAU de grottes : chambres, liaisons, et connexite garantie.

#include "Procedural/WorldseedCaves.h"

#include "Algo/Reverse.h"

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

	/**
	 * Simplification de Douglas-Peucker, en TROIS dimensions.
	 *
	 * Le chemin sort de l'A* avec un point par cellule : une galerie de trois
	 * cents metres en compte cinquante, donc autant de capsules a evaluer par
	 * voxel. La simplification garde les points qui PORTENT LA FORME et jette
	 * les alignes. On ne tronque surtout pas la liste a intervalle regulier :
	 * cela couperait les coudes, qui sont precisement ce qu'on veut garder.
	 */
	void Douglas(const TArray<FVector>& P, int32 A, int32 B, double Tol, TArray<int32>& Garde)
	{
		if (B <= A + 1) { return; }

		double Pire = -1.0;
		int32 Index = INDEX_NONE;
		for (int32 I = A + 1; I < B; ++I)
		{
			double T = 0.0;
			const double D = DistanceAuSegment(P[I], P[A], P[B], T);
			if (D > Pire) { Pire = D; Index = I; }
		}

		if (Pire > Tol && Index != INDEX_NONE)
		{
			Douglas(P, A, Index, Tol, Garde);
			Garde.Add(Index);
			Douglas(P, Index, B, Tol, Garde);
		}
	}

	TArray<FVector> Simplifier(const TArray<FVector>& Points, double Tol)
	{
		if (Points.Num() <= 2 || Tol <= 0.0) { return Points; }

		TArray<int32> Garde;
		Garde.Add(0);
		Douglas(Points, 0, Points.Num() - 1, Tol, Garde);
		Garde.Add(Points.Num() - 1);
		Garde.Sort();

		TArray<FVector> Out;
		Out.Reserve(Garde.Num());
		for (const int32 I : Garde) { Out.Add(Points[I]); }
		return Out;
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

	Out.RouteCellM = Num(TEXT("routageCelluleM"), 6.0);
	Out.RouteCorridorM = Num(TEXT("routageCouloirM"), 48.0);
	Out.RouteSurfaceM = Num(TEXT("routageSurfaceM"), 25.0);
	Out.RouteSlopeCost = Num(TEXT("routagePenteCout"), 1.6);
	Out.RouteRockCost = Num(TEXT("routageRocheCout"), 2.0);
	Out.RouteShareBonus = Num(TEXT("routageMutualisation"), 0.45);
	Out.RouteSimplifyM = Num(TEXT("routageSimplifieM"), 3.0);

	Out.EntranceSlopeDeg = Num(TEXT("entreePenteMinDeg"), 35.0);
	Out.EntrancePerChambers = Num(TEXT("entreeParChambres"), 10.0);
	Out.EntranceDepthM = Num(TEXT("entreeEnfoncementM"), 14.0);
	Out.EntranceSpacingM = Num(TEXT("entreeEspacementM"), 120.0);
	Out.SeaMarginM = Num(TEXT("niveauMerMargeM"), 5.0);
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

namespace
{
	/**
	 * Le contexte de routage : tout ce que le cout d'une cellule a besoin de
	 * savoir. Il est monte une fois et partage par toutes les galeries.
	 */
	struct FContexteRoutage
	{
		const FWorldseedGeometry* Geo = nullptr;
		const TArray<float>* ElevationM = nullptr;
		const FWorldseedLithology* Litho = nullptr;
		const FWorldseedLithologyRules* LithoRules = nullptr;
		const FWorldseedCaveRules* Rules = nullptr;
		float Exageration = 1.0f;

		/**
		 * Les cellules deja empruntees par une galerie.
		 *
		 * C'EST CE QUI FAIT UN RESEAU PLUTOT QU'UN PLAT DE SPAGHETTIS : deux
		 * liaisons voisines empruntent un tronc commun au lieu de creuser deux
		 * tubes paralleles. Le partage se paie d'une simple remise sur le cout.
		 */
		TSet<FIntVector> Empruntees;

		FVector CentreDe(const FIntVector& C) const
		{
			const double A = Rules->RouteCellM;
			return FVector((C.X + 0.5) * A, (C.Y + 0.5) * A, (C.Z + 0.5) * A);
		}

		FIntVector CelluleDe(const FVector& P) const
		{
			const double A = Rules->RouteCellM;
			return FIntVector(FMath::FloorToInt(P.X / A), FMath::FloorToInt(P.Y / A),
				FMath::FloorToInt(P.Z / A));
		}

		/**
		 * Altitude de la surface a l'aplomb d'un point, exageration comprise.
		 *
		 * BILINEAIRE, ET C'EST LA MEME LECTURE QUE LE CHAMP DE DENSITE. La
		 * premiere version lisait au PLUS PROCHE VOISIN : sur une grille dont
		 * une cellule fait trente et un metres, l'ecart avec la surface reellement
		 * maillee se compte en dizaines de metres sur un versant. Le routage
		 * croyait donc creuser sous terre la ou il sortait, et le controle
		 * croyait mesurer la meme surface que le joueur voit. Mesure du defaut :
		 * 23,3 % des points de galerie au-dessus du sol, insensible a toutes les
		 * corrections tentees -- corridor, plafond, simplification -- parce
		 * qu'aucune ne touchait a la cause.
		 */
		float SurfaceA(double X, double Y) const
		{
			double U = X / Geo->WidthM() + 0.5;
			U -= FMath::FloorToDouble(U);
			const double V = FMath::Clamp(Y / Geo->HeightM + 0.5, 0.0, 1.0);
			return WorldseedGrid::SampleUV(*ElevationM, Geo->NX, Geo->NY,
				static_cast<float>(U), static_cast<float>(V)) * Exageration;
		}

		float KarstA(double X, double Y) const
		{
			if (!Litho || !Litho->IsValid(Geo->CellCount())) { return 1.0f; }
			double U = X / Geo->WidthM() + 0.5;
			U -= FMath::FloorToDouble(U);
			const double V = FMath::Clamp(Y / Geo->HeightM + 0.5, 0.0, 1.0);
			const int32 Col = FMath::Clamp(FMath::FloorToInt(U * Geo->NX), 0, Geo->NX - 1);
			const int32 Row = FMath::Clamp(FMath::FloorToInt(V * Geo->NY), 0, Geo->NY - 1);
			const uint8 R = Litho->Id[Row * Geo->NX + Col];
			return LithoRules->Catalogue.IsValidIndex(R)
				? LithoRules->Catalogue[R].Karstifiable : 1.0f;
		}

		/**
		 * Cout d'occuper une cellule. C'EST ICI QUE LES REGLES S'EXPRIMENT, et
		 * nulle part ailleurs : l'A* ne fait qu'obeir a ce nombre.
		 */
		double CoutDe(const FIntVector& C) const
		{
			const FVector P = CentreDe(C);
			const double Profondeur = SurfaceA(P.X, P.Y) - P.Z;

			// L'INTERDIT PORTE SUR LE PLAFOND DE LA GALERIE, PAS SUR SON AXE.
			// Premiere version fautive : elle interdisait au centre de sortir du
			// sol, ce qui laissait le dessus du tube percer allegrement -- une
			// galerie de quatre metres de rayon dont l'axe est a trois metres
			// sous la surface est a ciel ouvert. Mesure du defaut : 22,78 % des
			// points de galerie au-dessus du sol APRES routage, contre 24,93 %
			// sans routage du tout -- autant dire que le routage ne servait a
			// rien.
			if (Profondeur <= Rules->TunnelRadiusMaxM + 1.0)
			{
				return TNumericLimits<double>::Max();
			}

			// RIEN NE SE CREUSE SOUS LA MER : une galerie noyee serait rendue
			// sous-marine par le plugin Water, ce que personne n'a decide.
			if (P.Z - Rules->TunnelRadiusMaxM < Rules->SeaMarginM)
			{
				return TNumericLimits<double>::Max();
			}

			double Cout = 1.0;

			// Pres de la surface : tres cher, de facon continue. Un mur net
			// ferait buter l'A* ; une rampe le fait plonger de lui-meme.
			if (Profondeur < Rules->RouteSurfaceM)
			{
				const double Manque = (Rules->RouteSurfaceM - Profondeur) / FMath::Max(Rules->RouteSurfaceM, 1.0f);
				Cout += 12.0 * Manque * Manque;
			}

			// La roche dure se creuse mal : le calcaire est bon marche.
			Cout += Rules->RouteRockCost * (1.0 - KarstA(P.X, P.Y));

			if (Empruntees.Contains(C))
			{
				Cout *= (1.0 - FMath::Clamp(Rules->RouteShareBonus, 0.0f, 0.95f));
			}
			return Cout;
		}
	};

	/**
	 * Vrai si toute la polyligne, PLAFOND COMPRIS, reste sous la surface.
	 *
	 * C'est le garde-fou de la simplification : sans lui, reduire le chemin
	 * remplace un coude qui contournait une colline par une corde qui la
	 * traverse, et le routage n'a servi a rien.
	 */
	bool SousTerrePartout(const FContexteRoutage& Ctx, const TArray<FVector>& P, float Rayon)
	{
		for (int32 K = 0; K + 1 < P.Num(); ++K)
		{
			const int32 Pas = FMath::Max(2, FMath::CeilToInt(FVector::Dist(P[K], P[K + 1]) / 3.0));
			for (int32 I = 0; I <= Pas; ++I)
			{
				const FVector Q = FMath::Lerp(P[K], P[K + 1], static_cast<float>(I) / Pas);
				if (Q.Z + Rayon > Ctx.SurfaceA(Q.X, Q.Y))
				{
					return false;
				}
			}
		}
		return true;
	}

	/**
	 * A* sur une grille grossiere, BORNEE A UN COULOIR autour de la droite.
	 *
	 * Sans le couloir, la recherche couvrirait la boite englobante des deux
	 * chambres et le cout exploserait pour un detour que personne ne veut. Avec,
	 * il reste de quoi contourner un obstacle sans partir a l'aventure.
	 *
	 * Rend un chemin vide si la recherche echoue ou depasse son plafond : c'est
	 * a l'appelant de decider quoi en faire, et il le signale.
	 */
	TArray<FVector> Router(const FContexteRoutage& Ctx, const FVector& Depart,
		const FVector& Arrivee, int32 PlafondNoeuds)
	{
		const FIntVector CD = Ctx.CelluleDe(Depart);
		const FIntVector CA = Ctx.CelluleDe(Arrivee);
		const double Couloir = Ctx.Rules->RouteCorridorM;
		const double Maille = Ctx.Rules->RouteCellM;
		const double Pente = Ctx.Rules->RouteSlopeCost;

		TMap<FIntVector, double> G;
		TMap<FIntVector, FIntVector> Parent;
		TArray<TPair<double, FIntVector>> File;

		auto Heuristique = [&](const FIntVector& C)
		{
			return FVector::Dist(Ctx.CentreDe(C), Arrivee);
		};

		G.Add(CD, 0.0);
		File.HeapPush(TPair<double, FIntVector>(Heuristique(CD), CD),
			[](const TPair<double, FIntVector>& A, const TPair<double, FIntVector>& B)
			{ return A.Key < B.Key; });

		int32 Visites = 0;
		bool bTrouve = false;

		while (File.Num() > 0 && Visites < PlafondNoeuds)
		{
			TPair<double, FIntVector> Tete;
			File.HeapPop(Tete,
				[](const TPair<double, FIntVector>& A, const TPair<double, FIntVector>& B)
				{ return A.Key < B.Key; });
			const FIntVector C = Tete.Value;
			++Visites;

			if (C == CA) { bTrouve = true; break; }

			const double GC = G.FindRef(C);

			for (int32 dz = -1; dz <= 1; ++dz)
			{
				for (int32 dy = -1; dy <= 1; ++dy)
				{
					for (int32 dx = -1; dx <= 1; ++dx)
					{
						if (dx == 0 && dy == 0 && dz == 0) { continue; }
						const FIntVector N(C.X + dx, C.Y + dy, C.Z + dz);

						// Hors du couloir : on n'y va pas.
						double T = 0.0;
						if (DistanceAuSegment(Ctx.CentreDe(N), Depart, Arrivee, T) > Couloir)
						{
							continue;
						}

						const double Local = Ctx.CoutDe(N);
						if (Local >= TNumericLimits<double>::Max() * 0.5) { continue; }

						const double Pas = FMath::Sqrt(double(dx * dx + dy * dy + dz * dz)) * Maille;
						const double Denivele = FMath::Abs(double(dz)) * Maille * Pente;
						const double Candidat = GC + Pas * Local + Denivele;

						const double* Ancien = G.Find(N);
						if (!Ancien || Candidat < *Ancien)
						{
							G.Add(N, Candidat);
							Parent.Add(N, C);
							File.HeapPush(TPair<double, FIntVector>(Candidat + Heuristique(N), N),
								[](const TPair<double, FIntVector>& A, const TPair<double, FIntVector>& B)
								{ return A.Key < B.Key; });
						}
					}
				}
			}
		}

		TArray<FVector> Chemin;
		if (!bTrouve) { return Chemin; }

		FIntVector C = CA;
		TArray<FIntVector> Cellules;
		while (true)
		{
			Cellules.Add(C);
			const FIntVector* P = Parent.Find(C);
			if (!P) { break; }
			C = *P;
		}
		Algo::Reverse(Cellules);

		Chemin.Reserve(Cellules.Num());
		for (const FIntVector& K : Cellules) { Chemin.Add(Ctx.CentreDe(K)); }
		return Chemin;
	}
}

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
		// MEME LECTURE QUE LE CHAMP DE DENSITE : bilineaire, et non la valeur
		// brute de la cellule. Placer une chambre d'apres l'une et la juger
		// d'apres l'autre laisse un ecart de plusieurs dizaines de metres sur
		// un versant.
		double Uc = X / Geometry.WidthM() + 0.5;
		Uc -= FMath::FloorToDouble(Uc);
		const double Vc = FMath::Clamp(Y / Geometry.HeightM + 0.5, 0.0, 1.0);
		const double Surface = WorldseedGrid::SampleUV(ElevationM, NX, NY,
			static_cast<float>(Uc), static_cast<float>(Vc)) * HeightExaggeration;

		const float Profondeur = Tirage.Entre(Rules.DepthMinM, Rules.DepthMaxM);
		const FVector P(X, Y, Surface - Profondeur);
		const float Rayon = Tirage.Entre(Rules.ChamberRadiusMinM, Rules.ChamberRadiusMaxM);

		// Une chambre dont le plancher passe sous la mer est refusee : la
		// contrainte est posee ICI, au semis, et non corrigee apres coup.
		if (P.Z - Rayon < Rules.SeaMarginM)
		{
			continue;
		}

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

	// --- 5. les galeries, ROUTEES ---------------------------------------------
	//
	// Une capsule DROITE entre deux chambres ignore tout : elle peut ressortir a
	// l'air libre en franchissant une colline, traverser du granite comme du
	// calcaire, et monter d'une pente impraticable. L'A* encode ces trois regles
	// dans son COUT -- interdit au-dessus du sol, tres cher pres de la surface,
	// surcout de la roche dure, penalite de denivele -- et n'a plus qu'a obeir.
	FContexteRoutage Ctx;
	Ctx.Geo = &Geometry;
	Ctx.ElevationM = &ElevationM;
	Ctx.Litho = &Lithology;
	Ctx.LithoRules = &LithoRules;
	Ctx.Rules = &Rules;
	Ctx.Exageration = HeightExaggeration;

	int32 Droites = 0;
	// On note ou commencent les troncons de chaque liaison, pour pouvoir compter
	// les percements SEPAREMENT selon qu'ils viennent d'un chemin route ou d'un
	// repli sur la droite. Sans cette separation on mesure un melange, et aucune
	// correction ne semble mordre -- ce qui est exactement ce qui vient de se
	// produire quatre fois de suite.
	TArray<bool> EstRepli;
	TArray<int32> DebutTroncon;
	Out.Segments.Reserve(Aretes.Num() * 4);

	for (const TPair<int32, int32>& E : Aretes)
	{
		const FVector A = Out.Chambers[E.Key].CentreM;
		const FVector B = Out.Chambers[E.Value].CentreM;
		const float RA = Tirage.Entre(Rules.TunnelRadiusMinM, Rules.TunnelRadiusMaxM);
		const float RB = Tirage.Entre(Rules.TunnelRadiusMinM, Rules.TunnelRadiusMaxM);

		TArray<FVector> Chemin = Router(Ctx, A, B, 40000);
		DebutTroncon.Add(Out.Segments.Num());
		EstRepli.Add(Chemin.Num() < 2);

		if (Chemin.Num() < 2)
		{
			// L'A* a echoue ou depasse son plafond -- c'est le cas des liaisons
			// les plus longues. On NE PEUT PAS abandonner la liaison : la
			// connexite est tout l'objet de cette passe. Mais on ne peut pas non
			// plus se contenter d'une droite.
			//
			// MESURE QUI A IMPOSE CE REPLI-CI. Separees, les deux populations
			// disent tout : les galeries ROUTEES ont 0,00 % de leurs points
			// au-dessus du sol, les REPLIEES EN DROITE 39,20 % -- et comme elles
			// sont les plus longues, elles portaient 58 % des points. Le chiffre
			// global, 22,8 %, cachait donc un routage parfait derriere un repli
			// defaillant, et quatre corrections successives n'y avaient rien
			// change.
			//
			// Le repli DRAPE la droite sous le terrain : a chaque echantillon, on
			// abaisse l'altitude autant qu'il faut pour que le PLAFOND de la
			// galerie reste enfoui. Ce n'est pas un itineraire intelligent -- il
			// ne contourne rien -- mais il ne perce plus, et il relie.
			++Droites;
			Chemin.Reset();

			const int32 Pas = FMath::Max(2, FMath::CeilToInt(FVector::Dist(A, B) / Rules.RouteCellM));
			const float Rayon = FMath::Max(RA, RB);
			for (int32 K = 0; K <= Pas; ++K)
			{
				FVector P = FMath::Lerp(A, B, static_cast<float>(K) / Pas);
				const double Plafond = Ctx.SurfaceA(P.X, P.Y) - Rayon - Rules.RouteSurfaceM * 0.25;
				P.Z = FMath::Min(P.Z, Plafond);
				Chemin.Add(P);
			}

			// Les extremites doivent retomber dans leurs chambres.
			Chemin[0] = A;
			Chemin.Last() = B;

			const TArray<FVector> Reduit = Simplifier(Chemin, Rules.RouteSimplifyM);
			if (SousTerrePartout(Ctx, Reduit, Rayon))
			{
				Chemin = Reduit;
			}
		}
		else
		{
			Chemin[0] = A;
			Chemin.Last() = B;

			// LA SIMPLIFICATION PEUT DEFAIRE LE ROUTAGE, et c'est la seconde
			// cause du defaut. Douglas-Peucker supprime les points intermediaires
			// et remplace un coude par une corde -- or c'est precisement ce coude
			// qui contournait la colline. On simplifie donc SOUS CONDITION : le
			// chemin reduit n'est retenu que s'il reste entierement sous terre.
			const TArray<FVector> Reduit = Simplifier(Chemin, Rules.RouteSimplifyM);
			if (SousTerrePartout(Ctx, Reduit, FMath::Max(RA, RB)))
			{
				Chemin = Reduit;
			}

			// Les cellules du chemin deviennent empruntees : la galerie suivante
			// aura interet a les reprendre.
			for (const FVector& P : Chemin)
			{
				Ctx.Empruntees.Add(Ctx.CelluleDe(P));
			}
		}

		for (int32 K = 0; K + 1 < Chemin.Num(); ++K)
		{
			const float T0 = static_cast<float>(K) / FMath::Max(Chemin.Num() - 1, 1);
			const float T1 = static_cast<float>(K + 1) / FMath::Max(Chemin.Num() - 1, 1);
			FWorldseedCaveSegment S;
			S.AM = Chemin[K];
			S.BM = Chemin[K + 1];
			S.RadiusAM = FMath::Lerp(RA, RB, T0);
			S.RadiusBM = FMath::Lerp(RA, RB, T1);
			Out.Segments.Add(S);
		}
	}

	// --- 5 bis. LES ENTREES ---------------------------------------------------
	//
	// SANS ELLES LE RESEAU EST HERMETIQUE, et c'est exactement ce qu'il etait :
	// chambres a vingt metres sous terre au moins, routage qui INTERDIT au
	// plafond d'atteindre la surface, bruit qui s'estompe a vingt-cinq metres du
	// sol. La connexite garantie etait donc purement interne -- tout communiquait
	// avec tout, et rien avec le dehors.
	//
	// UNE GROTTE S'OUVRE SUR UN ESCARPEMENT, et c'est une raison de geometrie
	// avant d'etre une question de gout : en penetrant horizontalement dans un
	// versant raide, on gagne de la profondeur en quelques metres. La meme
	// galerie sur un terrain plat resterait a fleur de sol sur des dizaines de
	// metres et eventrerait le paysage.
	int32 Entrees = 0;
	const int32 EntreesVoulues = (Rules.EntrancePerChambers > 0.0f)
		? FMath::Max(1, FMath::RoundToInt(N / Rules.EntrancePerChambers)) : 0;

	if (EntreesVoulues > 0)
	{
		TArray<float> DY;
		TArray<float> DX;
		WorldseedGrid::Gradient(ElevationM, NX, NY, MetresParPixel, DY, DX);

		const float PenteMin = FMath::Tan(FMath::DegreesToRadians(Rules.EntranceSlopeDeg));

		// Une bouche par chambre au plus, et on commence par les chambres les
		// moins profondes : ce sont elles qui ont une chance d'atteindre un
		// versant sans creuser la moitie du massif.
		TArray<int32> Ordre;
		for (int32 I = 0; I < N; ++I) { Ordre.Add(I); }
		Ordre.Sort([&](int32 A2, int32 B2)
		{
			return Out.Chambers[A2].CentreM.Z > Out.Chambers[B2].CentreM.Z;
		});

		// LES BOUCHES DEJA POSEES, pour qu'une paroi ne serve qu'une fois. On
		// ECARTE LES CANDIDATES PENDANT LA RECHERCHE plutot qu'apres : refuser
		// a la fin ferait simplement perdre l'entree, alors qu'ecarter en cours
		// de route laisse la recherche trouver le SECOND escarpement du
		// voisinage, qui fait tres bien l'affaire.
		TArray<FVector2D> BouchesPosees;

		for (const int32 Ic : Ordre)
		{
			if (Entrees >= EntreesVoulues) { break; }
			const FWorldseedCaveChamber& C = Out.Chambers[Ic];

			// On cherche, autour de la chambre, la cellule la plus RAIDE.
			const int32 Col0 = FMath::Clamp(
				FMath::FloorToInt((C.CentreM.X / Geometry.WidthM() + 0.5) * NX), 0, NX - 1);
			const int32 Row0 = FMath::Clamp(
				FMath::FloorToInt((C.CentreM.Y / Geometry.HeightM + 0.5) * NY), 0, NY - 1);
			const int32 Rayon = FMath::Max(2, FMath::CeilToInt(Espacement / MetresParPixel));

			float MeilleurePente = PenteMin;
			int32 MeilleureCellule = INDEX_NONE;
			for (int32 dj = -Rayon; dj <= Rayon; ++dj)
			{
				const int32 Rj = Row0 + dj;
				if (Rj < 0 || Rj >= NY) { continue; }
				for (int32 di = -Rayon; di <= Rayon; ++di)
				{
					const int32 Ci = (Col0 + di + NX) % NX;
					const int32 Cell2 = Rj * NX + Ci;
					if (ElevationM[Cell2] <= Rules.SeaMarginM) { continue; }

					const FVector2D Ici(
						(static_cast<double>(Ci) / NX - 0.5) * Geometry.WidthM(),
						(static_cast<double>(Rj) / NY - 0.5) * Geometry.HeightM);

					bool bDejaPrise = false;
					for (const FVector2D& B2 : BouchesPosees)
					{
						if (FVector2D::Distance(Ici, B2) < Rules.EntranceSpacingM)
						{
							bDejaPrise = true;
							break;
						}
					}
					if (bDejaPrise) { continue; }

					const float Pente = FMath::Sqrt(DX[Cell2] * DX[Cell2] + DY[Cell2] * DY[Cell2]);
					if (Pente > MeilleurePente)
					{
						MeilleurePente = Pente;
						MeilleureCellule = Cell2;
					}
				}
			}
			if (MeilleureCellule == INDEX_NONE) { continue; }

			const int32 Ce = MeilleureCellule % NX;
			const int32 Re = MeilleureCellule / NX;
			const double Xe = (static_cast<double>(Ce) / NX - 0.5) * Geometry.WidthM();
			const double Ye = (static_cast<double>(Re) / NY - 0.5) * Geometry.HeightM;
			const double Ze = Ctx.SurfaceA(Xe, Ye);

			// LA BOUCHE EST A LA JONCTION DE LA PAROI ET DU SOL, et on s'enfonce
			// vers l'AMONT : c'est la direction ou le terrain monte, donc celle
			// qui enfouit la galerie le plus vite.
			const float Gx = DX[MeilleureCellule];
			const float Gy = DY[MeilleureCellule];
			const float Norme = FMath::Max(FMath::Sqrt(Gx * Gx + Gy * Gy), 1e-4f);
			const FVector Amont(Gx / Norme, Gy / Norme, 0.0);

			const float RayonBouche = Rules.TunnelRadiusMaxM;
			const FVector Bouche(Xe, Ye, Ze - RayonBouche * 0.5);
			FVector Fond = Bouche + Amont * Rules.EntranceDepthM;
			Fond.Z = FMath::Min(Fond.Z,
				Ctx.SurfaceA(Fond.X, Fond.Y) - RayonBouche - Rules.RouteSurfaceM * 0.5);

			if (Fond.Z - RayonBouche < Rules.SeaMarginM) { continue; }

			TArray<FVector> Acces = Router(Ctx, Fond, C.CentreM, 40000);
			if (Acces.Num() < 2)
			{
				continue;
			}
			Acces[0] = Fond;
			Acces.Last() = C.CentreM;

			// Le troncon de bouche, lui, PERCE volontairement : c'est l'ouverture.
			FWorldseedCaveSegment Ouverture;
			Ouverture.AM = Bouche;
			Ouverture.BM = Fond;
			Ouverture.RadiusAM = RayonBouche;
			Ouverture.RadiusBM = RayonBouche;
			Out.Segments.Add(Ouverture);

			for (int32 K = 0; K + 1 < Acces.Num(); ++K)
			{
				FWorldseedCaveSegment S;
				S.AM = Acces[K];
				S.BM = Acces[K + 1];
				S.RadiusAM = RayonBouche;
				S.RadiusBM = RayonBouche;
				Out.Segments.Add(S);
			}
			// LA POSITION DE CHAQUE BOUCHE EST JOURNALISEE, et ce n'est pas du
			// debogage : une grotte qu'on ne sait pas trouver n'existe pas pour
			// le joueur. C'est aussi ce que le gameplay voudra interroger pour
			// y poser du butin ou une ambiance sonore.
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] grottes : entree %d a (%.0f, %.0f) m, altitude %.0f m, ")
				TEXT("pente %.0f deg"),
				Entrees + 1, Bouche.X, Bouche.Y, Bouche.Z,
				FMath::RadiansToDegrees(FMath::Atan(MeilleurePente)));

			BouchesPosees.Add(FVector2D(Bouche.X, Bouche.Y));
			++Entrees;
		}
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

	// --- 7. LE CONTROLE QUI DIT SI LE ROUTAGE A SERVI A QUELQUE CHOSE ---------
	//
	// Une galerie qui perce le sol est le defaut que l'A* est cense empecher.
	// On echantillonne chaque troncon et on compte les points dont le PLAFOND --
	// le dessus de la galerie -- passe au-dessus de la surface. Sans ce chiffre
	// au journal, on croit le routage bon parce qu'il a tourne.
	{
		int32 Points[2] = { 0, 0 };
		int32 Perces[2] = { 0, 0 };

		for (int32 L = 0; L < DebutTroncon.Num(); ++L)
		{
			const int32 Fin = (L + 1 < DebutTroncon.Num())
				? DebutTroncon[L + 1] : Out.Segments.Num();
			const int32 Bac = EstRepli[L] ? 1 : 0;

			for (int32 I = DebutTroncon[L]; I < Fin; ++I)
			{
				const FWorldseedCaveSegment& S = Out.Segments[I];
				const int32 Pas = FMath::Max(2, FMath::CeilToInt(FVector::Dist(S.AM, S.BM) / 4.0));
				for (int32 K = 0; K <= Pas; ++K)
				{
					const float T = static_cast<float>(K) / Pas;
					const FVector P = FMath::Lerp(S.AM, S.BM, T);
					const float R = FMath::Lerp(S.RadiusAM, S.RadiusBM, T);
					++Points[Bac];
					if (P.Z + R > Ctx.SurfaceA(P.X, P.Y)) { ++Perces[Bac]; }
				}
			}
		}

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] grottes : ROUTEES %d points dont %d au-dessus du sol (%.2f %%) ")
			TEXT("| REPLIEES %d points dont %d (%.2f %%)"),
			Points[0], Perces[0], 100.0f * Perces[0] / FMath::Max(Points[0], 1),
			Points[1], Perces[1], 100.0f * Perces[1] / FMath::Max(Points[1], 1));
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] grottes : %d chambres, %d liaisons (%d de boucle), ")
		TEXT("%d troncons, %d repliees, %d ENTREES  (%.0f ms)"),
		Out.Chambers.Num(), Aretes.Num(), ABoucler,
		Out.Segments.Num(), Droites, Entrees,
		(FPlatformTime::Seconds() - StartTime) * 1000.0);
}
