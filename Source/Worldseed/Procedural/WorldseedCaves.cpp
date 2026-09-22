// Worldseed - le RESEAU de grottes : chambres, liaisons, et connexite garantie.

#include "Procedural/WorldseedCaves.h"

#include "Procedural/WorldseedFins.h"

#include "Algo/Reverse.h"

#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedPerlin.h"
#include "Procedural/WorldseedLithology.h"
#include "Procedural/WorldseedTrace.h"

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
	Out.RouteNodeCap = Rules.Int(TEXT("cavites"), TEXT("routagePlafondNoeuds"), 150000);

	Out.EntranceSlopeDeg = Num(TEXT("entreePenteMinDeg"), 35.0);
	Out.EntrancePerChambers = Num(TEXT("entreeParChambres"), 10.0);
	Out.EntranceDepthM = Num(TEXT("entreeEnfoncementM"), 14.0);
	Out.EntranceSpacingM = Num(TEXT("entreeEspacementM"), 120.0);
	Out.ShaftSlopeMaxDeg = Num(TEXT("gouffrePenteMaxDeg"), 20.0);
	Out.ShaftTopRadiusM = Num(TEXT("gouffreRayonHautM"), 2.5);
	Out.ShaftBottomRadiusM = Num(TEXT("gouffreRayonBasM"), 6.0);
	Out.ShaftWanderM = Num(TEXT("gouffreOndulationM"), 3.0);

	Out.DolineRoofMaxM = Num(TEXT("dolinePlafondMaxM"), 12.0);
	Out.DolineFlareRatio = Num(TEXT("dolineEvasement"), 1.8);
	Out.DolineRimNoiseM = Num(TEXT("dolineBordOndulationM"), 6.0);
	Out.SeaMarginM = Num(TEXT("niveauMerMargeM"), 5.0);

	Out.ArchCrestMaxM = Num(TEXT("archeLargeurCreteMaxM"), 80.0);
	Out.ArchNeckMinM = Num(TEXT("archeColMinM"), 40.0);
	Out.ArchNeckMaxM = Num(TEXT("archeColMaxM"), 120.0);
	Out.ArchSeaReachM = Num(TEXT("archePorteeMerM"), 400.0);
	Out.ArchOpeningHeightM = Num(TEXT("archeOuvertureHauteurM"), 38.0);
	Out.ArchOpeningWidthM = Num(TEXT("archeOuvertureLargeurM"), 30.0);
	Out.ArchOpeningShare = Num(TEXT("archeOuverturePartFalaise"), 0.45);
	Out.ArchCliffMinM = Num(TEXT("archeFalaiseMinM"), 45.0);
	Out.ArchRoofReachM = Num(TEXT("archePorteeToitM"), 22.0);
	Out.ArchBelowSummitM = Num(TEXT("archeSousSommetM"), 20.0);
	Out.ArchRadiusMinM = Num(TEXT("archeRayonMinM"), 6.0);
	Out.ArchRadiusMaxM = Num(TEXT("archeRayonMaxM"), 14.0);
	Out.ArchBridgeMinM = Num(TEXT("archePontMinM"), 7.0);
	Out.ArchBridgeMaxM = Num(TEXT("archePontMaxM"), 12.0);
	Out.ArchBridgeMaxRatio = Num(TEXT("archePontRatioMax"), 0.55);
	Out.ArchHardnessMaxM = Num(TEXT("archeDureteMax"), 0.9);
	Out.ArchProbeSpacingM = Num(TEXT("archeSondageEspacementM"), 250.0);
	Out.ArchSpacingM = Num(TEXT("archeEspacementM"), 1500.0);
	Out.ArchSummitMarginM = Num(TEXT("archeMargeSommetM"), 10.0);
	Out.ArchCount = FMath::RoundToInt(Num(TEXT("archeNombre"), 24.0));

	// LE CHAMP DE LAMES EST LU PAR LES DEUX PASSES, ET PAR LE MEME CHARGEUR.
	// Le champ de densite CREUSE les fentes, cette passe POSE une arche au
	// milieu du mur : si les deux divergeaient d'un metre, l'arche percerait a
	// cote de la lame -- un defaut qu'aucune mesure agregee ne verrait.
	Out.Fins = FWorldseedFinRules::FromRules(Rules);
	return Out;
}

// --------------------------------------------------------------------------

void FWorldseedCaveNetwork::ReconstruireIndex()
{
	const int32 Cases = Size.X * Size.Y;
	ChamberBuckets.Reset();
	SegmentBuckets.Reset();
	if (Cases <= 0)
	{
		return;
	}
	ChamberBuckets.SetNum(Cases);
	SegmentBuckets.SetNum(Cases);

	auto Ranger = [this](TArray<TArray<int32>>& Buckets, const FBox& B, int32 Index)
	{
		const int32 I0 = FMath::Clamp(FMath::FloorToInt(B.Min.X / CellM) - Min.X, 0, Size.X - 1);
		const int32 I1 = FMath::Clamp(FMath::FloorToInt(B.Max.X / CellM) - Min.X, 0, Size.X - 1);
		const int32 J0 = FMath::Clamp(FMath::FloorToInt(B.Min.Y / CellM) - Min.Y, 0, Size.Y - 1);
		const int32 J1 = FMath::Clamp(FMath::FloorToInt(B.Max.Y / CellM) - Min.Y, 0, Size.Y - 1);
		for (int32 J = J0; J <= J1; ++J)
		{
			for (int32 I = I0; I <= I1; ++I)
			{
				Buckets[J * Size.X + I].Add(Index);
			}
		}
	};

	for (int32 I = 0; I < Chambers.Num(); ++I)
	{
		const FWorldseedCaveChamber& C = Chambers[I];
		Ranger(ChamberBuckets,
			FBox(C.CentreM - FVector(C.RadiusM), C.CentreM + FVector(C.RadiusM)), I);
	}
	for (int32 I = 0; I < Segments.Num(); ++I)
	{
		const FWorldseedCaveSegment& S = Segments[I];
		const float R = FMath::Max(S.RadiusAM, S.RadiusBM);
		FBox B(ForceInit);
		B += S.AM; B += S.BM;
		Ranger(SegmentBuckets, B.ExpandBy(R), I);
	}
}

void FWorldseedCaveNetwork::Reset()
{
	Chambers.Reset();
	Segments.Reset();
	Arches.Reset();
	Puits.Reset();
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
		 * Rayon de la galerie en cours de routage, en metres.
		 *
		 * IL FAUT LE RAYON REEL, PAS LE MAXIMUM DU CATALOGUE. Premiere version
		 * fautive : l'interdit employait TunnelRadiusMaxM pour toutes les
		 * galeries, donc quatre metres, alors qu'une galerie donnee en fait 1,5
		 * a 4. On barrait ainsi des passages qu'un tunnel etroit franchit sans
		 * peine -- et comme la bande utile est coincee entre la surface et le
		 * niveau de la mer, deux metres de trop suffisent a la fermer. Mesure du
		 * defaut : 7 liaisons sur 31 declarees SANS ISSUE.
		 */
		float RayonCourant = 4.0f;

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
			if (Profondeur <= RayonCourant + 1.0)
			{
				return TNumericLimits<double>::Max();
			}

			// RIEN NE SE CREUSE SOUS LA MER : une galerie noyee serait rendue
			// sous-marine par le plugin Water, ce que personne n'a decide.
			if (P.Z - RayonCourant < Rules->SeaMarginM)
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
	/** Pourquoi un routage a echoue. Deux causes, deux remedes opposes. */
	enum class EEchec : uint8
	{
		Aucun,
		/** La file s'est VIDEE : il n'existe aucun chemin dans le couloir. */
		SansIssue,
		/** Le plafond de noeuds a ete atteint : le chemin existe peut-etre. */
		TropLong,
	};

	TArray<FVector> Router(const FContexteRoutage& Ctx, const FVector& Depart,
		const FVector& Arrivee, int32 PlafondNoeuds, EEchec& OutEchec)
	{
		OutEchec = EEchec::Aucun;
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
		if (!bTrouve)
		{
			// LA DISTINCTION COMPTE, et elle appelle des remedes opposes : une
			// file vide dit qu'AUCUN chemin n'existe dans le couloir -- il faut
			// l'elargir ou relacher une contrainte ; un plafond atteint dit que
			// la recherche a manque de souffle -- il faut le relever. Confondre
			// les deux fait regler le mauvais bouton.
			OutEchec = (File.Num() == 0) ? EEchec::SansIssue : EEchec::TropLong;
			return Chemin;
		}

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

	/**
	 * Creuse un puits vertical a rayon variable, de Haut vers Bas.
	 *
	 * UNE SEULE FONCTION POUR LA DOLINE ET POUR L'AVEN, et c'est voulu : les
	 * deux sont le meme objet -- une chaine de capsules le long d'un axe
	 * vertical ondulant -- et ne different que par le SENS de la variation de
	 * rayon. La doline s'evase vers le haut (entonnoir d'effondrement), l'aven
	 * vers le bas (cloche de dissolution). Les ecrire deux fois aurait fait
	 * diverger deux moities qui doivent rester identiques.
	 *
	 * L'AXE ONDULE : ni un effondrement ni une dissolution ne suivent un trait
	 * a la regle. Le bruit porte sur l'AXE et non sur le rayon, ce qui deforme
	 * le contour sans creuser plus profond.
	 */
	void CreuserPuits(FWorldseedCaveNetwork& Out, const FVector& CentreM,
		double Haut, double Bas, double RayonHaut, double RayonBas,
		double Ondulation, double PasM, int32 Seed)
	{
		const int32 Tranches = FMath::Max(3, FMath::CeilToInt((Haut - Bas) / PasM));
		FVector Precedent = FVector::ZeroVector;

		for (int32 T = 0; T <= Tranches; ++T)
		{
			const double F = static_cast<double>(T) / Tranches;
			const double Z = FMath::Lerp(Haut, Bas, F);

			const double Ox = Ondulation * WorldseedPerlin::Perlin3D(
				static_cast<float>(CentreM.X * 0.04),
				static_cast<float>(Z * 0.06), 0.0f, Seed);
			const double Oy = Ondulation * WorldseedPerlin::Perlin3D(
				0.0f, static_cast<float>(Z * 0.06),
				static_cast<float>(CentreM.Y * 0.04), Seed + 61);

			const FVector Point(CentreM.X + Ox, CentreM.Y + Oy, Z);
			if (T > 0)
			{
				FWorldseedCaveSegment S;
				S.AM = Precedent;
				S.BM = Point;
				S.RadiusAM = static_cast<float>(FMath::Lerp(RayonHaut, RayonBas,
					(T - 1.0) / Tranches));
				S.RadiusBM = static_cast<float>(FMath::Lerp(RayonHaut, RayonBas, F));
				Out.Segments.Add(S);
			}
			Precedent = Point;
		}
	}

/**
 * Le chantier des grottes : l'etat que les dix temps se partagent.
 *
 * POURQUOI UNE STRUCTURE ET NON DIX FONCTIONS LIBRES. La passe porte une
 * trentaine de grandeurs qui traversent les temps -- le contexte de routage,
 * les aretes de l'arbre, les bornes de troncons, les bouches deja posees, les
 * composantes du graphe. Les faire circuler en parametres donnerait des
 * signatures de vingt arguments, ce qui est moins lisible que le probleme.
 * Les nommer ICI, une fois, dit exactement ce qui est commun -- et c'est la
 * moitie de l'interet du decoupage.
 *
 * REFACTOR PUR : aucune ligne de logique n'a ete reecrite, seulement deplacee.
 * Le controle est un releve identique au chiffre pres, avant et apres.
 */
struct FChantierGrottes
{
	// --- ce qu'on recoit, et qu'on ne possede pas ------------------------
	const FWorldseedGeometry& Geometry;
	const TArray<float>& ElevationM;
	const TArray<float>& PrecipMm;
	const FWorldseedLithology& Lithology;
	const FWorldseedLithologyRules& LithoRules;
	const FWorldseedCaveRules& Rules;
	const float HeightExaggeration;
	const int32 Seed;
	FWorldseedCaveNetwork& Out;

	// --- la grille ---------------------------------------------------------
	const int32 NX;
	const int32 NY;
	const int32 Count;
	const bool bHasPrecip;
	const bool bHasLitho;
	const float MetresParPixel;

	/** Generateur deterministe : le monde doit se rejouer a l'identique. */
	FTirage Tirage;

	// --- 1 et 2 : le semis --------------------------------------------------
	TArray<int32> Candidates;
	int32 Essais = 0;
	float Espacement = 0.0f;

	/** Nombre de cases de l'index spatial, pose par le semis. */
	int32 Cases = 0;
	int32 N = 0;

	// --- 3 a 5 : le graphe et son routage -----------------------------------
	FContexteRoutage Ctx;
	TArray<TPair<int32, int32>> Aretes;
	TArray<bool> AreteGardee;
	int32 ABoucler = 0;
	int32 Abandonnees = 0;

	/** Bornes des troncons, et lesquels sont un repli. Lues par le controle. */
	TArray<bool> EstRepli;
	TArray<int32> DebutTroncon;

	/**
	 * Fin des GALERIES dans le tableau des segments.
	 *
	 * Elle borne le controle de percement, qui doit s'arreter avant les
	 * ouvertures : celles-ci percent le sol A DESSEIN.
	 */
	int32 FinDesGaleries = 0;

	// --- les composantes ----------------------------------------------------
	TArray<int32> Racine;
	TMap<int32, TArray<int32>> ParComposante;

	// --- ce que les formes d'ouverture se partagent -------------------------
	TArray<float> DY;
	TArray<float> DX;
	TArray<FVector2D> BouchesPosees;
	TArray<bool> ChambreOuverte;
	TSet<int32> ComposantesOuvertes;
	int32 Souhaitees = 0;

	int32 Entrees = 0;
	int32 Gouffres = 0;
	int32 Dolines = 0;
	int32 Arches = 0;

	FChantierGrottes(const FWorldseedGeometry& InGeometry,
		const TArray<float>& InElevationM, const TArray<float>& InPrecipMm,
		const FWorldseedLithology& InLithology,
		const FWorldseedLithologyRules& InLithoRules,
		const FWorldseedCaveRules& InRules, float InHeightExaggeration,
		int32 InSeed, FWorldseedCaveNetwork& InOut)
		: Geometry(InGeometry)
		, ElevationM(InElevationM)
		, PrecipMm(InPrecipMm)
		, Lithology(InLithology)
		, LithoRules(InLithoRules)
		, Rules(InRules)
		, HeightExaggeration(InHeightExaggeration)
		, Seed(InSeed)
		, Out(InOut)
		, NX(InGeometry.NX)
		, NY(InGeometry.NY)
		, Count(InGeometry.CellCount())
		, bHasPrecip(InPrecipMm.Num() == InGeometry.CellCount())
		, bHasLitho(InLithology.IsValid(InGeometry.CellCount()))
		, MetresParPixel(FMath::Max(InGeometry.MetersPerPixel(), 1e-3f))
		, Tirage(InSeed + 5521)
	{
	}

	/**
	 * Une paroi ne sert qu'une fois.
	 *
	 * SANS CE TEST, DEUX CHAMBRES VOISINES ELISENT LA MEME PAROI : elles
	 * cherchent chacune l'escarpement le plus raide de leur voisinage, et ces
	 * voisinages se recouvrent. Partage par les trois formes d'ouverture, d'ou
	 * sa place ici plutot que dans l'une d'elles.
	 */
	bool TropPres(double X, double Y) const
	{
		for (const FVector2D& B : BouchesPosees)
		{
			if (FVector2D::Distance(FVector2D(X, Y), B) < Rules.EntranceSpacingM)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Racine de la composante d'une chambre, avec compression de chemin.
	 *
	 * ELLE SERT AUX QUATRE FORMES D'OUVERTURE, pas seulement au decoupage en
	 * composantes : chacune doit savoir si le reseau qu'elle perce est deja
	 * ouvert. D'ou sa place ici plutot qu'en lambda locale.
	 */
	int32 Trouver(int32 I)
	{
		while (Racine[I] != I) { Racine[I] = Racine[Racine[I]]; I = Racine[I]; }
		return I;
	}

	/** 1 et 2 : ou une grotte peut exister, et les chambres. */
	void Semer();

	/** 3 et 4 : l'arbre couvrant minimal, puis quelques boucles. */
	void Relier();

	/** 5 : les galeries, routees en A* sous contraintes. */
	void Creuser();

	/** Il n'y a pas UN reseau mais plusieurs : on les separe. */
	void Composantes();

	/** La doline : la ou le plafond cede. */
	void Effondrer();

	/** L'aven : la ou l'eau s'infiltre a travers un plateau. */
	void Dissoudre();

	/** La bouche de falaise, qui est la GARANTIE d'acces. */
	void Ouvrir();

	/** L'arche marine : un cap perce a sa base. */
	void Percer();

	/** 6 : l'index spatial, pour que le champ n'interroge pas tout. */
	void Indexer();

	/** 7 : le controle qui dit si le routage a servi a quelque chose. */
	void Verifier();
};

}

void FChantierGrottes::Semer()
{
	// --- 1. OU une grotte peut exister ---------------------------------------
	//
	// C'EST LA ROCHE QUI DECIDE, PAS LE BIOME. Un karst se creuse par
	// dissolution : il lui faut du calcaire et de l'eau. Le climat n'entre donc
	// que par la pluie, qui est un champ continu -- jamais par l'etiquette de
	// biome, qui ne sert qu'a lier des assets.
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

	Espacement = Rules.ChamberSpacingM;
	const float CelluleM = FMath::Max(Espacement, 1.0f);
	Out.CellM = CelluleM;

	const double DemiLargeur = Geometry.WidthM() * 0.5;
	const double DemiHauteur = Geometry.HeightM * 0.5;
	Out.Min = FIntPoint(FMath::FloorToInt(-DemiLargeur / CelluleM),
		FMath::FloorToInt(-DemiHauteur / CelluleM));
	Out.Size = FIntPoint(
		FMath::CeilToInt(DemiLargeur / CelluleM) - Out.Min.X + 2,
		FMath::CeilToInt(DemiHauteur / CelluleM) - Out.Min.Y + 2);

	Cases = Out.Size.X * Out.Size.Y;
	TArray<TArray<int32>> Hachage;
	Hachage.SetNum(Cases);

	auto CaseDe = [this](const FVector& P) -> int32
	{
		const int32 I = FMath::Clamp(FMath::FloorToInt(P.X / Out.CellM) - Out.Min.X, 0, Out.Size.X - 1);
		const int32 J = FMath::Clamp(FMath::FloorToInt(P.Y / Out.CellM) - Out.Min.Y, 0, Out.Size.Y - 1);
		return J * Out.Size.X + I;
	};

	// Autant d'essais que de cellules candidates : au-dela le refus domine et on
	// n'ajoute plus rien.
	Essais = Candidates.Num();
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

		// DEUX BORNES, ET J'EN AVAIS OUBLIE UNE. Le plancher ne doit pas passer
		// sous la mer -- c'etait la seule verifiee -- mais le PLAFOND ne doit pas
		// sortir du sol non plus. Une chambre a douze metres de profondeur avec
		// seize metres de rayon depasse de quatre : c'est un puits a ciel ouvert,
		// et aucune mesure ne le voyait puisque le controle de percement
		// n'echantillonne que les GALERIES.
		//
		// La condition revient a exiger que la profondeur tiree depasse le rayon
		// tire ; la refuser au semis vaut mieux que de la rattraper apres, ou il
		// faudrait choisir entre deplacer la chambre et la retrecir.
		if (P.Z - Rayon < Rules.SeaMarginM)
		{
			continue;
		}
		if (P.Z + Rayon > Surface - Rules.TunnelRadiusMaxM)
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

	N = Out.Chambers.Num();
	if (N < 2)
	{
		UE_LOG(LogTemp, Log, TEXT("[Worldseed] grottes : %d chambre(s), rien a relier"), N);
		return;
	}
}

void FChantierGrottes::Relier()
{
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
	// Le contexte de routage est monte AVANT l'arbre : c'est lui qui sait lire
	// la surface, et l'arbre en a besoin pour juger ce qui est creusable.
	Ctx.Geo = &Geometry;
	Ctx.ElevationM = &ElevationM;
	Ctx.Litho = &Lithology;
	Ctx.LithoRules = &LithoRules;
	Ctx.Rules = &Rules;
	Ctx.Exageration = HeightExaggeration;

	// L'ARBRE DOIT SAVOIR CE QUI EST CREUSABLE, sans quoi il choisit la liaison
	// la plus COURTE et non la plus praticable -- et l'A* se retrouve ensuite a
	// devoir creuser sous une baie, ce qui n'existe pas. Une arete dont la
	// droite franchit un terrain trop bas pour loger une galerie entre la
	// surface et le niveau de la mer est donc lourdement penalisee : le tri la
	// rejette d'office s'il existe une autre route, et ne la garde qu'en dernier
	// recours, ou la connexite l'emporte sur le realisme.
	const float HauteurUtile = Rules.SeaMarginM + 2.0f * Rules.TunnelRadiusMaxM + 2.0f;
	auto CoutArete = [&](const FVector& A2, const FVector& B2) -> float
	{
		const float D = FVector::Dist(A2, B2);
		const int32 Pas = FMath::Max(4, FMath::CeilToInt(D / 40.0f));
		for (int32 K = 0; K <= Pas; ++K)
		{
			const FVector P = FMath::Lerp(A2, B2, static_cast<float>(K) / Pas);
			if (Ctx.SurfaceA(P.X, P.Y) < HauteurUtile)
			{
				return D * 1000.0f;
			}
		}
		return D;
	};

	TArray<float> MeilleureArete;
	TArray<int32> Parent;
	TArray<bool> Dedans;
	MeilleureArete.Init(TNumericLimits<float>::Max(), N);
	Parent.Init(INDEX_NONE, N);
	Dedans.Init(false, N);
	MeilleureArete[0] = 0.0f;

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
			const float D = CoutArete(Out.Chambers[Meilleur].CentreM, Out.Chambers[I].CentreM);
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
	ABoucler = FMath::RoundToInt(Aretes.Num() * Rules.LoopPct / 100.0f);
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
}

void FChantierGrottes::Creuser()
{
	// --- 5. les galeries, ROUTEES ---------------------------------------------
	//
	// Une capsule DROITE entre deux chambres ignore tout : elle peut ressortir a
	// l'air libre en franchissant une colline, traverser du granite comme du
	// calcaire, et monter d'une pente impraticable. L'A* encode ces trois regles
	// dans son COUT -- interdit au-dessus du sol, tres cher pres de la surface,
	// surcout de la roche dure, penalite de denivele -- et n'a plus qu'a obeir.
	int32 Droites = 0;
	int32 SansIssue = 0;
	int32 TropLong = 0;
	// On note ou commencent les troncons de chaque liaison, pour pouvoir compter
	// les percements SEPAREMENT selon qu'ils viennent d'un chemin route ou d'un
	// repli sur la droite. Sans cette separation on mesure un melange, et aucune
	// correction ne semble mordre -- ce qui est exactement ce qui vient de se
	// produire quatre fois de suite.
	Out.Segments.Reserve(Aretes.Num() * 4);

	for (const TPair<int32, int32>& E : Aretes)
	{
		const FVector A = Out.Chambers[E.Key].CentreM;
		const FVector B = Out.Chambers[E.Value].CentreM;
		const float RA = Tirage.Entre(Rules.TunnelRadiusMinM, Rules.TunnelRadiusMaxM);
		const float RB = Tirage.Entre(Rules.TunnelRadiusMinM, Rules.TunnelRadiusMaxM);

		Ctx.RayonCourant = FMath::Max(RA, RB);

		EEchec Echec = EEchec::Aucun;
		TArray<FVector> Chemin = Router(Ctx, A, B, Rules.RouteNodeCap, Echec);
		if (Echec == EEchec::SansIssue) { ++SansIssue; }
		else if (Echec == EEchec::TropLong) { ++TropLong; }
		DebutTroncon.Add(Out.Segments.Num());
		EstRepli.Add(Chemin.Num() < 2);
		AreteGardee.Add(true);

		if (Chemin.Num() < 2 && Echec == EEchec::SansIssue)
		{
			// ON ABANDONNE LA LIAISON, ET C'EST LA BONNE REPONSE.
			//
			// "Sans issue" ne veut pas dire que la recherche a manque de
			// souffle : elle a EPUISE le couloir et prouve qu'aucun chemin
			// n'existe. Le terrain entre les deux chambres est trop bas pour
			// loger une galerie entre la surface et le niveau de la mer.
			//
			// Les deux bricolages essayes avant celui-ci sont pires, et mesures
			// comme tels : draper la galerie sous la surface la fait passer sous
			// la mer -- ce que la regle interdit -- et la borner au-dessus de la
			// mer la fait percer le sol sur 70,43 % de ses points.
			//
			// ABANDONNER N'EST PAS RENONCER A LA CONNEXITE, c'est reconnaitre
			// qu'il y a PLUSIEURS reseaux. Deux massifs separes par une baie ont
			// deux systemes karstiques distincts : c'est vrai sur Terre, et
			// chacun garde sa connexite interne par construction. Il suffit alors
			// de donner une entree a chacun.
			DebutTroncon.Add(Out.Segments.Num());
			EstRepli.Add(false);
			AreteGardee.Add(false);
			++Abandonnees;
			continue;
		}

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
			// Ce repli ne sert plus qu'aux liaisons trop longues pour le
			// plafond de noeuds, jamais a celles qui n'ont pas d'issue : draper
			// sous la surface suffit, puisqu'un chemin existe.
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

	// LE CONTROLE DE PERCEMENT NE DOIT VOIR QUE LES GALERIES. Les entrees qui
	// suivent percent le sol A DESSEIN -- c'est leur definition. Laisser le
	// dernier troncon courir jusqu'a la fin du tableau les faisait compter
	// comme des defauts : mesure 0,38 % la ou le routage est a 0,00. C'est le
	// meme melange de deux populations qui avait deja fait regler quatre fois
	// le mauvais bouton.
	FinDesGaleries = Out.Segments.Num();
}

void FChantierGrottes::Composantes()
{
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
	// --- LES COMPOSANTES : il n'y a plus UN reseau, mais PLUSIEURS -----------
	//
	// Abandonner les liaisons sans issue separe le graphe. Chaque morceau garde
	// sa connexite interne -- c'est l'arbre couvrant qui la lui donne -- mais il
	// lui faut SA PROPRE ENTREE, sans quoi il reste inaccessible. Un reseau
	// qu'on ne peut pas atteindre ne vaut pas mieux qu'un reseau qui n'existe
	// pas, et c'est exactement le defaut qu'on vient de corriger.
	Racine.SetNum(N);
	for (int32 I = 0; I < N; ++I) { Racine[I] = I; }

	for (int32 K = 0; K < Aretes.Num(); ++K)
	{
		if (!AreteGardee.IsValidIndex(K) || !AreteGardee[K]) { continue; }
		const int32 Ra = Trouver(Aretes[K].Key);
		const int32 Rb = Trouver(Aretes[K].Value);
		if (Ra != Rb) { Racine[Ra] = Rb; }
	}

	for (int32 I = 0; I < N; ++I)
	{
		ParComposante.FindOrAdd(Trouver(I)).Add(I);
	}

	// --- CE QUE LES TROIS PASSES SE PARTAGENT --------------------------------
	//
	// TROIS FORMES D'OUVERTURE, ET ELLES NE SE VALENT PAS. Deux se FORMENT
	// toutes seules, par geologie : la doline la ou le plafond cede, l'aven la
	// ou l'eau s'infiltre a travers un plateau. La troisieme, la bouche de
	// falaise, est la GARANTIE : elle existe pour qu'aucun reseau ne reste
	// mure. Les deux premieres ne doivent donc rien devoir a un budget, et la
	// troisieme doit tenir compte de ce qu'elles ont deja ouvert.
	WorldseedGrid::Gradient(ElevationM, NX, NY, MetresParPixel, DY, DX);

	// LES OUVERTURES DEJA POSEES, pour qu'une paroi ne serve qu'une fois, et
	// pour qu'un aven ne perce pas le bord d'une doline.
	ChambreOuverte.Init(false, N);


	Souhaitees = (Rules.EntrancePerChambers > 0.0f)
		? FMath::Max(1, FMath::RoundToInt(N / Rules.EntrancePerChambers)) : 0;
	// AU MOINS UNE PAR COMPOSANTE : le nombre demande est un PLANCHER de
	// densite, pas un plafond d'accessibilite.
}

void FChantierGrottes::Effondrer()
{
	// --- 5 bis. LES DOLINES D'EFFONDREMENT -----------------------------------
	//
	// LA SEULE FORME KARSTIQUE QUI SE VOIE DE LOIN. La bouche s'ouvre dans un
	// versant, l'aven perce un plateau : il faut les avoir trouves pour les
	// voir. Une doline EST un accident du paysage.
	//
	// ELLE NE SE POSE PAS, ELLE SE DEDUIT. Le plafond d'une salle porte ce qui
	// le surmonte ; sous une certaine epaisseur il cede, la surface s'affaisse
	// en entonnoir jusqu'au vide, et les parois s'eboulent jusqu'a leur angle
	// de repos -- d'ou une ouverture plus LARGE que la salle. Le critere est
	// `profondeur - rayon`, et rien d'autre.
	if (Rules.DolineRoofMaxM > 0.0f && Rules.DolineFlareRatio > 1.0f)
	{
		for (int32 Ic = 0; Ic < Out.Chambers.Num(); ++Ic)
		{
			const FWorldseedCaveChamber& Ch = Out.Chambers[Ic];
			const double Sol = Ctx.SurfaceA(Ch.CentreM.X, Ch.CentreM.Y);
			const double Sommet = Ch.CentreM.Z + Ch.RadiusM;
			const double Plafond = Sol - Sommet;

			if (Plafond <= 0.0 || Plafond > Rules.DolineRoofMaxM) { continue; }
			if (TropPres(Ch.CentreM.X, Ch.CentreM.Y)) { continue; }

			const double Haut = Sol + 2.0;
			const double Bas = Sommet - Ch.RadiusM * 0.3;
			const double RayonHaut = Ch.RadiusM * Rules.DolineFlareRatio;

			CreuserPuits(Out, Ch.CentreM, Haut, Bas, RayonHaut, Ch.RadiusM,
				Rules.DolineRimNoiseM, 3.0, Seed + 5101);

			{
				FWorldseedCavePuits P;
				P.CentreM = Ch.CentreM;
				P.SolM = static_cast<float>(Sol);
				P.HautM = static_cast<float>(Haut);
				P.BasM = static_cast<float>(Bas);
				P.RayonHautM = static_cast<float>(RayonHaut);
				P.RayonBasM = Ch.RadiusM;
				P.bDoline = true;
				Out.Puits.Add(P);
			}

			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] grottes : DOLINE a (%.0f, %.0f) m, altitude %.0f m, ")
				TEXT("plafond %.0f m, ouverture %.0f m de large, %.0f m de creux"),
				Ch.CentreM.X, Ch.CentreM.Y, Sol, Plafond, RayonHaut * 2.0, Sol - Bas);

			BouchesPosees.Add(FVector2D(Ch.CentreM.X, Ch.CentreM.Y));
			ChambreOuverte[Ic] = true;
			ComposantesOuvertes.Add(Trouver(Ic));
			++Dolines;
		}
	}
}

void FChantierGrottes::Dissoudre()
{
	// --- 5 ter. LES GOUFFRES, OU AVENS ---------------------------------------
	//
	// LA DEUXIEME FORME QUI SE FORME TOUTE SEULE, et elle est complementaire de
	// la doline : plafond MINCE, il cede et fait une cuvette ; plafond EPAIS
	// mais plateau au-dessus, l'eau s'y infiltre pendant des millenaires et
	// creuse un puits. Une chambre deja ouverte par une doline n'a donc pas
	// d'aven -- le plafond a cede, il n'a pas eu le temps de se dissoudre.
	//
	// ELLE EST SORTIE DE LA BOUCLE DES ENTREES, ET C'EST TOUT L'OBJET DE CETTE
	// PASSE. L'aven y etait fabrique, donc plafonne par le budget des entrees :
	// six pour tout le monde quoi qu'il arrive, et il fallait en plus qu'aucune
	// falaise ne l'ait devance. Mesure avant : UN seul aven dans le monde. Un
	// aven ne se creuse pas parce qu'il manquait un acces, il se creuse parce
	// que le terrain au-dessus est plat.
	if (Rules.ShaftSlopeMaxDeg > 0.0f)
	{
		const float PentePlateau =
			FMath::Tan(FMath::DegreesToRadians(Rules.ShaftSlopeMaxDeg));

		for (int32 Ic = 0; Ic < Out.Chambers.Num(); ++Ic)
		{
			if (ChambreOuverte[Ic]) { continue; }

			const FWorldseedCaveChamber& Ch = Out.Chambers[Ic];
			const int32 Col = FMath::Clamp(
				FMath::FloorToInt((Ch.CentreM.X / Geometry.WidthM() + 0.5) * NX), 0, NX - 1);
			const int32 Row = FMath::Clamp(
				FMath::FloorToInt((Ch.CentreM.Y / Geometry.HeightM + 0.5) * NY), 0, NY - 1);
			const int32 Cell = Row * NX + Col;

			// LE CRITERE EST A L'APLOMB, PAS ALENTOUR. Premiere version fautive :
			// je cherchais d'abord une falaise dans un rayon de cent
			// quatre-vingts metres et ne posais un aven que si je n'en trouvais
			// AUCUNE. Un tel voisinage en contient presque toujours une.
			const float Pente = FMath::Sqrt(
				DX[Cell] * DX[Cell] + DY[Cell] * DY[Cell]);
			if (Pente > PentePlateau) { continue; }

			const double Sol = Ctx.SurfaceA(Ch.CentreM.X, Ch.CentreM.Y);
			const double Haut = Sol + Rules.ShaftTopRadiusM * 0.5;
			const double Bas = Ch.CentreM.Z + Ch.RadiusM * 0.5;
			if (Haut - Bas < 8.0) { continue; }
			if (TropPres(Ch.CentreM.X, Ch.CentreM.Y)) { continue; }

			// LE PROFIL EN CLOCHE : etroit en surface, evase dessous. C'est la
			// forme meme de l'aven -- la dissolution a le moins travaille en
			// haut, et l'eau a stagne en bas. Un puits cylindrique se lit tout
			// de suite comme un forage. C'est exactement l'inverse de
			// l'entonnoir de la doline, et les deux se distinguent d'un coup
			// d'oeil pour cette seule raison.
			CreuserPuits(Out, Ch.CentreM, Haut, Bas,
				Rules.ShaftTopRadiusM, Rules.ShaftBottomRadiusM,
				Rules.ShaftWanderM, 4.0, Seed + 3313);

			{
				FWorldseedCavePuits P;
				P.CentreM = Ch.CentreM;
				P.SolM = static_cast<float>(Sol);
				P.HautM = static_cast<float>(Haut);
				P.BasM = static_cast<float>(Bas);
				P.RayonHautM = Rules.ShaftTopRadiusM;
				P.RayonBasM = Rules.ShaftBottomRadiusM;
				P.bDoline = false;
				Out.Puits.Add(P);
			}

			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] grottes : GOUFFRE a (%.0f, %.0f) m, altitude %.0f m, ")
				TEXT("%.0f m de haut, pente du plateau %.0f deg"),
				Ch.CentreM.X, Ch.CentreM.Y, Sol, Haut - Bas,
				FMath::RadiansToDegrees(FMath::Atan(Pente)));

			BouchesPosees.Add(FVector2D(Ch.CentreM.X, Ch.CentreM.Y));
			ChambreOuverte[Ic] = true;
			ComposantesOuvertes.Add(Trouver(Ic));
			++Gouffres;
		}
	}
}

void FChantierGrottes::Ouvrir()
{
	// --- 5 quater. LES BOUCHES DE FALAISE, QUI SONT LA GARANTIE --------------
	//
	// CE QUI PRECEDE S'EST FORME TOUT SEUL, ET NE GARANTIT RIEN. Un reseau
	// entier peut n'avoir ni plafond mince ni plateau : il resterait mure. La
	// bouche de falaise existe pour cela, et son compte tient desormais compte
	// des ouvertures deja faites -- le nombre demande est un PLANCHER de
	// densite, pas un quota a remplir coute que coute.
	int32 SansOuverture = 0;
	for (const TPair<int32, TArray<int32>>& Paire : ParComposante)
	{
		if (!ComposantesOuvertes.Contains(Paire.Key)) { ++SansOuverture; }
	}

	// LA BOUCHE DE FALAISE GARDE SON PROPRE PLANCHER, arbitre par le
	// proprietaire le 19 septembre 2026.
	//
	// Premiere version : le plancher de densite etait commun aux trois formes,
	// donc les avens et les dolines le consommaient. Mesure -- 6 bouches avant,
	// UNE apres. Or ce n'est pas une ouverture parmi d'autres : c'est la SEULE
	// des trois ou l'on entre EN MARCHANT. Dans un aven comme dans une doline,
	// on tombe. Compter les trois dans le meme budget revenait a traiter comme
	// interchangeables deux experiences de jeu qui ne le sont pas.
	//
	// Le plancher n'est donc plus reduit par ce qui s'est ouvert tout seul ; il
	// reste releve par les reseaux encore mures, ce qui est la garantie.
	const int32 EntreesVoulues = (Souhaitees > 0)
		? FMath::Max(Souhaitees, SansOuverture) : 0;

	// SANS CE RELEVE ON NE SAIT PAS POURQUOI UN RESEAU RESTE MURE, et les trois
	// causes appellent trois corrections OPPOSEES : aucun escarpement dans le
	// voisinage est un fait du relief, une bouche qui tomberait sous la mer est
	// une borne qu-on a posee, un routage sans issue est un manque de roche
	// entre la bouche et la chambre. Corriger au hasard, c-est le « quatre
	// corrections qui ne bougent pas la mesure » du routage des galeries.
	int32 RejetPente = 0;
	int32 RejetMer = 0;
	int32 RejetChemin = 0;
	bool bBudgetEpuise = false;

	if (EntreesVoulues > 0)
	{
		const float PenteMin = FMath::Tan(FMath::DegreesToRadians(Rules.EntranceSlopeDeg));

		// SANS CE RELEVE ON NE SAIT PAS POURQUOI UN RESEAU RESTE MURE, et les
		// trois causes appellent trois corrections OPPOSEES : aucun escarpement
		// dans le voisinage est un fait du relief, une bouche qui tomberait sous
		// la mer est une borne qu-on a posee, un routage sans issue est un
		// manque de roche entre la bouche et la chambre. Corriger au hasard,
		// c-est le « quatre corrections qui ne bougent pas la mesure » du
		// routage des galeries.

		// Une bouche par chambre au plus, et on commence par les chambres les
		// moins profondes : ce sont elles qui ont une chance d'atteindre un
		// versant sans creuser la moitie du massif.
		// ON TOURNE ENTRE LES COMPOSANTES avant d'en resservir une : sans cela,
		// la composante la plus haute raflerait toutes les entrees et les autres
		// resteraient murees.
		TArray<TArray<int32>> Files;
		for (TPair<int32, TArray<int32>>& Paire : ParComposante)
		{
			Paire.Value.Sort([&](int32 A2, int32 B2)
			{
				return Out.Chambers[A2].CentreM.Z > Out.Chambers[B2].CentreM.Z;
			});
			Files.Add(Paire.Value);
		}
		// LES COMPOSANTES ENCORE MUREES PASSENT DEVANT. La bouche de falaise est
		// la garantie d'accessibilite ; elle doit servir d'abord aux reseaux que
		// ni doline ni aven n'ont ouverts.
		Files.Sort([&](const TArray<int32>& A2, const TArray<int32>& B2)
		{
			const bool OA = A2.Num() > 0 && ChambreOuverte.IsValidIndex(A2[0])
				&& ComposantesOuvertes.Contains(Trouver(A2[0]));
			const bool OB = B2.Num() > 0 && ChambreOuverte.IsValidIndex(B2[0])
				&& ComposantesOuvertes.Contains(Trouver(B2[0]));
			if (OA != OB) { return !OA; }
			return A2.Num() > B2.Num();
		});

		TArray<int32> Ordre;
		for (int32 Rang = 0; ; ++Rang)
		{
			bool bEncore = false;
			for (const TArray<int32>& F : Files)
			{
				if (F.IsValidIndex(Rang)) { Ordre.Add(F[Rang]); bEncore = true; }
			}
			if (!bEncore) { break; }
		}

		// LA POSE D'UNE BOUCHE EST EXTRAITE, POUR POUVOIR ETRE REJOUEE.
		//
		// Elle sert deux fois et dans deux regimes opposes : la passe normale,
		// bornee par un budget de DENSITE, et la passe de dernier recours, qui
		// est une GARANTIE et n'a donc ni budget ni seuil partage. Les ecrire
		// deux fois ferait diverger deux moities qui doivent rester identiques
		// -- c'est l'argument qui avait deja impose une seule fonction pour la
		// doline et l'aven.
		auto PoserBouche = [&](int32 Ic, float SeuilPente) -> bool
		{
			// Une chambre deja percee par une doline ou un aven n'a pas besoin
			// d'une bouche en plus.
			if (ChambreOuverte[Ic]) { return false; }
			const FWorldseedCaveChamber& C = Out.Chambers[Ic];

			// On cherche, autour de la chambre, la cellule la plus RAIDE.
			const int32 Col0 = FMath::Clamp(
				FMath::FloorToInt((C.CentreM.X / Geometry.WidthM() + 0.5) * NX), 0, NX - 1);
			const int32 Row0 = FMath::Clamp(
				FMath::FloorToInt((C.CentreM.Y / Geometry.HeightM + 0.5) * NY), 0, NY - 1);

			const int32 Rayon = FMath::Max(2, FMath::CeilToInt(Espacement / MetresParPixel));

			float MeilleurePente = SeuilPente;
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
			// Aucun escarpement dans le voisinage : cette chambre n'aura pas de
			// bouche propre. Elle reste reliee au reseau, qui a son acces
			// ailleurs -- c'est tout l'objet de l'arbre couvrant.
			if (MeilleureCellule == INDEX_NONE) { ++RejetPente; return false; }

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

			if (Fond.Z - RayonBouche < Rules.SeaMarginM) { ++RejetMer; return false; }

			Ctx.RayonCourant = RayonBouche;
			EEchec EchecEntree = EEchec::Aucun;
			TArray<FVector> Acces = Router(Ctx, Fond, C.CentreM, Rules.RouteNodeCap, EchecEntree);
			if (Acces.Num() < 2)
			{
				++RejetChemin;
				return false;
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
			ChambreOuverte[Ic] = true;
			ComposantesOuvertes.Add(Trouver(Ic));
			++Entrees;
			return true;
		};

		// --- la passe normale, bornee par un budget de DENSITE ---------------
		for (const int32 Ic : Ordre)
		{
			if (Entrees >= EntreesVoulues) { bBudgetEpuise = true; break; }
			PoserBouche(Ic, PenteMin);
		}

		// --- LE DERNIER RECOURS : UNE GARANTIE N'A PAS DE BUDGET -------------
		//
		// Mesure qui a impose cette passe : un reseau sur quatorze restait MURE,
		// et l'entonnoir dit pourquoi -- 49 chambres sans escarpement assez
		// raide, contre 11 sous la mer et une seule sans chemin. Le budget,
		// lui, n'avait PAS mordu (108 bouches posees sur 227 autorisees) : ma
		// premiere intuition l'accusait, et elle etait fausse.
		//
		// La cause est donc un SEUIL, pas une quantite. Or le seuil de pente
		// sert a choisir de BEAUX escarpements quand on a le choix ; il n'a
		// aucune raison d'interdire la seule entree d'un reseau qui n'en a pas
		// d'autre. Le depot a deja tranche deux fois dans ce sens -- l'aven
		// plafonne par le budget des entrees, puis le plancher de densite
		// commun aux trois formes : UNE GARANTIE NE PARTAGE NI BUDGET NI SEUIL
		// AVEC UNE CIBLE DE DENSITE.
		//
		// On relache donc la pente par crans, pour les seules composantes
		// encore murees, et l'on DIT de combien il a fallu relacher. Une
		// entree obtenue a vingt degres reste une entree ou l'on marche ; une
		// obtenue en silence serait une regle perdue.
		{
			static const float Crans[3] = { 0.70f, 0.45f, 0.25f };

			for (const TArray<int32>& F : Files)
			{
				if (F.Num() == 0) { continue; }
				if (ComposantesOuvertes.Contains(Trouver(F[0]))) { continue; }

				bool bOuverte = false;
				for (int32 Cran = 0; Cran < 3 && !bOuverte; ++Cran)
				{
					for (const int32 Ic : F)
					{
						if (PoserBouche(Ic, PenteMin * Crans[Cran]))
						{
							UE_LOG(LogTemp, Warning,
								TEXT("[Worldseed] grottes : reseau ouvert EN DERNIER ")
								TEXT("RECOURS, pente relachee a %.0f %% du seuil ")
								TEXT("(%.0f deg au lieu de %.0f)"),
								Crans[Cran] * 100.0f,
								FMath::RadiansToDegrees(FMath::Atan(PenteMin * Crans[Cran])),
								Rules.EntranceSlopeDeg);
							bOuverte = true;
							break;
						}
					}
				}
			}
		}
	}

	// --- LA GARANTIE SE VERIFIE, ELLE NE SE SUPPOSE PAS ----------------------
	//
	// Toute cette passe existe pour qu'aucun reseau ne reste mure. Or la bouche
	// de falaise peut ECHOUER -- aucun escarpement assez raide dans le
	// voisinage -- et l'echec est silencieux. Sans ce compte, on croirait la
	// garantie tenue parce que le code a tourne.
	{
		int32 Murees = 0;
		for (const TPair<int32, TArray<int32>>& Paire : ParComposante)
		{
			bool bOuverte = false;
			for (const int32 Ic : Paire.Value)
			{
				if (ChambreOuverte[Ic]) { bOuverte = true; break; }
			}
			if (!bOuverte) { ++Murees; }
		}

		if (Murees > 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] grottes : bouches -- %d chambres sans escarpement, ")
				TEXT("%d sous la mer, %d sans chemin, budget %d/%d%s"),
				RejetPente, RejetMer, RejetChemin, Entrees, EntreesVoulues,
				bBudgetEpuise ? TEXT(" EPUISE") : TEXT(""));

			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] grottes : %d reseau(x) sur %d restent MURES -- ")
				TEXT("aucun escarpement ni plateau exploitable"),
				Murees, ParComposante.Num());
		}
		else
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] grottes : les %d reseaux ont au moins une ouverture"),
				ParComposante.Num());
		}
	}
}

void FChantierGrottes::Percer()
{
	// --- 5 quinquies. LES ARCHES MARINES, FORME D'ETRETAT --------------------
	//
	// TROISIEME VERSION, ET LES DEUX PREMIERES SE SONT TROMPEES D'OBJET.
	//
	// La premiere cherchait une crete mince n'importe ou : mesure, 229 cretes
	// assez minces dans tout le monde et TOUTES en granite ou en basalte, zero
	// en roche sedimentaire. La seconde fabriquait la lame -- des joints
	// paralleles dans le gres, comme a Arches National Park -- et elle
	// marchait : dix arches posees, dix traversantes, neuf sous un pont. Mais
	// A L'IMAGE ELLES N'EXISTAIENT PAS, et la photo a dit pourquoi en un coup :
	// le point de vue place a l'altitude de l'arche se retrouvait DANS la
	// roche. L'arche etait a 119 m, le sol au-dessus a 239. Un trou de vingt
	// metres enterre sous un plateau, au fond d'une fente de seize : il
	// n'existait aucun endroit d'ou la regarder.
	//
	// CE QUI MANQUAIT N'ETAIT PAS LA LAME, C'ETAIT LE VIDE AUTOUR. Une arche se
	// lit parce qu'on voit le CIEL au travers, et le proprietaire l'avait dit
	// en donnant Etretat pour exemple : ce n'est pas un trou dans un mur, c'est
	// un PROMONTOIRE perce a sa base, vu contre la mer, avec une masse enorme
	// au-dessus. La regle "pont mince sinon c'est un tunnel" etait fausse --
	// a Etretat le pont fait tout la falaise.
	//
	// Le critere est donc entierement different : un cap dont le COL est
	// etroit, avec de la MER des deux cotes. La passe littorale les fabrique
	// deja -- son bruit de recul creuse des anses et laisse des caps, et c'est
	// pour cela qu'il existe.
	if (Rules.ArchCount > 0 && Rules.ArchNeckMaxM > 0.0f)
	{
		TArray<FVector2D> ArchesPosees;

		auto MacroEn = [&](double PX, double PY)
		{
			double Uc = PX / Geometry.WidthM() + 0.5;
			Uc -= FMath::FloorToDouble(Uc);
			const double Vc = FMath::Clamp(PY / Geometry.HeightM + 0.5, 0.0, 1.0);
			return static_cast<double>(WorldseedGrid::SampleUV(ElevationM, NX, NY,
				static_cast<float>(Uc), static_cast<float>(Vc))) * HeightExaggeration;
		};

		const double ZBas = Rules.SeaMarginM - 2.0;

		// --- LES CANDIDATS : DU LITTORAL, ET RIEN D'AUTRE --------------------
		//
		// On balaie les cellules terrestres qui touchent la mer. Un cap est par
		// definition entoure d'eau : chercher ailleurs serait chercher un col
		// de montagne, et l'on retomberait sur le tunnel de la version d'avant.
		// PRES DE LA MER, PAS ADJACENT A ELLE, et la nuance a coute une passe
		// entiere a zero. La passe littorale GARANTIT que la cellule qui touche
		// l'eau est la plateforme d'abrasion, a trois metres : exiger
		// "adjacente a la mer ET haute de cinquante metres" etait donc une
		// contradiction, et le releve l'a dit d'un coup -- zero cap examine.
		// Le cap qu'on cherche est derriere la plateforme, en haut de la face.
		TArray<uint8> Terre;
		Terre.SetNumUninitialized(Count);
		for (int32 I = 0; I < Count; ++I)
		{
			Terre[I] = (ElevationM[I] > 0.0f) ? 1 : 0;
		}
		TArray<float> DistMer;
		WorldseedGrid::DistanceTransform(Terre, NX, NY, DistMer);

		TArray<int32> Littoral;
		Littoral.Reserve(Count / 32 + 1);
		for (int32 C = 0; C < Count; ++C)
		{
			const double H = ElevationM[C] * HeightExaggeration;
			if (H < Rules.ArchCliffMinM) { continue; }
			if (DistMer[C] * MetresParPixel > Rules.ArchSeaReachM) { continue; }
			Littoral.Add(C);
		}

		// Les plus HAUTS d'abord : un cap qui porte cent metres de falaise fait
		// une arche qu'on voit de loin, un cap de dix metres fait un trou.
		Littoral.Sort([this](int32 A, int32 B)
		{
			return ElevationM[A] > ElevationM[B];
		});

		int32 Examines = 0;
		int32 ColTropLarge = 0;
		int32 ToitTropCourt = 0;
		int32 PasDeMer = 0;
		int32 RocheRefusee = 0;

		for (const int32 Cell : Littoral)
		{
			if (Arches >= Rules.ArchCount) { break; }

			const double X = (static_cast<double>(Cell % NX) / NX - 0.5)
				* Geometry.WidthM();
			const double Y = (static_cast<double>(Cell / NX) / NY - 0.5)
				* Geometry.HeightM;

			bool bDejaPrise = false;
			for (const FVector2D& A : ArchesPosees)
			{
				if (FVector2D::DistSquared(FVector2D(X, Y), A)
					< Rules.ArchSpacingM * Rules.ArchSpacingM)
				{
					bDejaPrise = true;
					break;
				}
			}
			if (bDejaPrise) { continue; }
			++Examines;

			// --- LE COL, MESURE A LA HAUTEUR DE L'OUVERTURE ------------------
			//
			// ET C'EST LA TOUTE LA DIFFERENCE AVEC LA VERSION D'AVANT, qui
			// mesurait la crete VINGT METRES SOUS LE SOMMET -- donc au coeur du
			// massif, la ou il est large. Ici on mesure au RAS DE L'EAU, la ou
			// la mer a deja mange les flancs : c'est la que le cap est mince,
			// et c'est la qu'on perce.
			// --- L'ARCHE SE DIMENSIONNE SUR LA FALAISE QUI LA PORTE ---------
			//
			// UN CAP S'AMINCIT EN MONTANT, et l'oublier vide l'arche de sa
			// masse : une ouverture de trente-huit metres dans un cap de
			// cinquante ne laissait de roche qu'au centre, si bien que le pont
			// mesurait ZERO des quinze metres d'ecart -- sur des arches
			// pourtant surmontees de cinquante a quatre-vingts metres de
			// falaise. Une petite pointe merite une petite arche.
			const double Falaise = ElevationM[Cell] * HeightExaggeration;
			const double HauteurOuverture = FMath::Clamp(
				(Falaise - ZBas) * Rules.ArchOpeningShare,
				12.0, static_cast<double>(Rules.ArchOpeningHeightM));
			const double ZHaut = ZBas + HauteurOuverture;

			const double ZCol = ZBas + HauteurOuverture * 0.5;
			const double PasM = 4.0;
			const double PorteeM = 400.0;

			double Rayons[8];
			for (int32 D = 0; D < 8; ++D)
			{
				const double A = D * (UE_DOUBLE_PI / 4.0);
				const double CX = FMath::Cos(A);
				const double CY = FMath::Sin(A);
				double R = 0.0;
				while (R < PorteeM
					&& MacroEn(X + CX * (R + PasM), Y + CY * (R + PasM)) >= ZCol)
				{
					R += PasM;
				}
				Rayons[D] = R;
			}

			double Col = PorteeM * 2.0;
			int32 Mince = 0;
			for (int32 D = 0; D < 4; ++D)
			{
				const double L = Rayons[D] + Rayons[D + 4] + PasM;
				if (L < Col) { Col = L; Mince = D; }
			}
			// UN COL DE QUATRE METRES N'EST PAS UN CAP, c'est un caillou isole
			// que le bruit a laisse : le percer le ferait disparaitre. Il faut
			// de quoi poser les deux pieds de l'arche.
			if (Col < Rules.ArchNeckMinM || Col > Rules.ArchNeckMaxM)
			{
				++ColTropLarge;
				continue;
			}

			const double Angle = Mince * (UE_DOUBLE_PI / 4.0);
			const FVector2D Travers(FMath::Cos(Angle), FMath::Sin(Angle));

			// --- DE LA MER DES DEUX COTES, ET C'EST CE QUI FAIT LE CAP -------
			//
			// Sans ce test on percerait un simple RESSERREMENT de la cote, et
			// l'arche donnerait sur de la terre : on ne verrait rien au travers.
			// C'est exactement le defaut que la version d'avant n'a jamais su
			// voir, faute de regarder ce qu'il y a DERRIERE le trou.
			// CE QUI COMPTE EST QU'ON VOIE A TRAVERS, pas qu'il y ait de l'eau.
			// Exiger l'altitude zero des deux cotes echouait partout : la
			// plateforme d'abrasion est a trois metres et s'etend sur une
			// centaine, donc le sol derriere un cap est du rivage, pas de la
			// mer. Le vrai critere est que le sol y passe SOUS le pied de
			// l'ouverture -- alors le ciel se voit au travers, qu'il y ait de
			// l'eau ou du galet.
			const double Marge = Col * 0.5 + 30.0;
			const double SeuilVue = ZBas + 6.0;
			const double CoteA = MacroEn(X + Travers.X * Marge, Y + Travers.Y * Marge);
			const double CoteB = MacroEn(X - Travers.X * Marge, Y - Travers.Y * Marge);
			if (CoteA > SeuilVue || CoteB > SeuilVue) { ++PasDeMer; continue; }

			// --- LE SECOND SONDAGE, A LA HAUTEUR DU PONT ---------------------
			//
			// LE PREMIER NE REGARDE QUE LE RAS DE L'EAU, et cela ne suffit pas :
			// un cap s'amincit en montant, donc une pointe effilee peut avoir
			// un col parfait a deux metres et n'etre plus qu'une lame a
			// quarante. Mesure sans ce test : le pont tombait a ZERO sur la
			// moitie des arches, toujours a quinze ou vingt metres du centre,
			// c'est-a-dire la ou le cap s'etait deja retreci.
			//
			// On mesure donc la PORTEE DU TOIT : jusqu'ou, le long du
			// percement, la roche reste au-dessus de la cle de voute. C'est
			// exactement la grandeur qui manquait -- pas une largeur au centre,
			// mais une longueur de part et d'autre.
			const double ZToit = ZHaut + Rules.ArchBridgeMinM;
			double PorteeToit = PorteeM;
			for (int32 Sens = -1; Sens <= 1; Sens += 2)
			{
				double T = 0.0;
				while (T < Col)
				{
					const double TT = T + 4.0;
					if (MacroEn(X + Travers.X * TT * Sens,
						Y + Travers.Y * TT * Sens) < ZToit)
					{
						break;
					}
					T = TT;
				}
				PorteeToit = FMath::Min(PorteeToit, T);
			}

			if (PorteeToit < Rules.ArchRoofReachM) { ++ToitTropCourt; continue; }

			// --- LA ROCHE ----------------------------------------------------
			//
			// Etretat est de la CRAIE, et les arches marines se font dans la
			// roche tendre ou dans le basalte. Le granite donne des domes : on
			// l'ecarte, et on laisse tout le reste.
			const uint8 Roche = Lithology.Id.IsValidIndex(Cell)
				? Lithology.Id[Cell] : 0;
			const float Durete = LithoRules.Catalogue.IsValidIndex(Roche)
				? LithoRules.Catalogue[Roche].Hardness : 1.0f;
			if (Durete > Rules.ArchHardnessMaxM) { ++RocheRefusee; continue; }

			// --- CREUSER L'OGIVE ---------------------------------------------
			//
			// LARGE EN BAS, ETROITE EN HAUT. Une ouverture a section constante
			// se lit comme un tunnel fore ; une arche marine est evasee au pied
			// -- c'est la ou la houle travaille -- et se referme en voute.
			// L'ouverture descend SOUS le niveau de la mer, pour que les pieds
			// de l'arche plongent dans l'eau au lieu de reposer sur un seuil.
			// LA LARGEUR SUIT LE COL AUSSI : une ouverture aussi large que le
			// cap ne laisse pas de pieds a l'arche, elle la decapite.
			const double Largeur = FMath::Min(
				static_cast<double>(Rules.ArchOpeningWidthM), Col * 0.45);
			const double DemiLongueur = Col * 0.5 + Largeur + 8.0;
			const int32 Etages = 6;

			for (int32 E = 0; E < Etages; ++E)
			{
				const double F = static_cast<double>(E) / (Etages - 1);
				const double ZE = FMath::Lerp(ZBas, ZHaut, F);

				// Demi-largeur : pleine au pied, elle se ferme en cosinus vers
				// la cle de voute.
				const double RE = Largeur * 0.5
					* FMath::Cos(F * UE_DOUBLE_PI * 0.5) + 1.5;

				const int32 Tranches = FMath::Max(3,
					FMath::CeilToInt(2.0 * DemiLongueur / 5.0));
				FVector Precedent = FVector::ZeroVector;
				for (int32 T = 0; T <= Tranches; ++T)
				{
					const double G = static_cast<double>(T) / Tranches;
					const double S = FMath::Lerp(-DemiLongueur, DemiLongueur, G);

					const double O = 2.0 * WorldseedPerlin::Perlin3D(
						static_cast<float>(S * 0.04),
						static_cast<float>(ZE * 0.05), 0.0f, Seed + 7717 + E);

					const FVector Point(X + Travers.X * S - Travers.Y * O,
						Y + Travers.Y * S + Travers.X * O, ZE + O * 0.4);
					if (T > 0)
					{
						FWorldseedCaveSegment Seg;
						Seg.AM = Precedent;
						Seg.BM = Point;
						Seg.RadiusAM = static_cast<float>(RE);
						Seg.RadiusBM = static_cast<float>(RE);
						Out.Segments.Add(Seg);
					}
					Precedent = Point;
				}
			}

			ArchesPosees.Emplace(X, Y);
			++Arches;

			FWorldseedCaveArch Fiche;
			Fiche.CentreM = FVector(X, Y, (ZBas + ZHaut) * 0.5);
			Fiche.TraversM = Travers;
			Fiche.EpaisseurM = static_cast<float>(Col);
			Fiche.RayonM = static_cast<float>(Largeur * 0.5);
			Fiche.PontM = static_cast<float>(Falaise - ZHaut);
			Out.Arches.Add(Fiche);

			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] grottes : ARCHE MARINE a (%.0f, %.0f) m, ")
				TEXT("cap de %.0f m perce sur %.0f x %.0f m, toit %.0f m, ")
				TEXT("falaise %.0f m, %s"),
				X, Y, Col, Largeur, HauteurOuverture, PorteeToit, Falaise,
				WorldseedLithology::Name(LithoRules, Roche));
		}

		// SANS CE RELEVE ON NE SAIT PAS POURQUOI IL N'Y A PAS D'ARCHE, et les
		// trois causes appellent trois corrections opposees : un col trop large
		// est un fait du littoral, l'absence de mer derriere un mauvais choix de
		// site, une roche refusee un reglage.
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] grottes : arches -- %d caps examines, %d cols hors ")
			TEXT("bornes, %d sans vue au travers, %d toits trop courts, ")
			TEXT("%d refuses sur la roche, %d POSEES"),
			Examines, ColTropLarge, PasDeMer, ToitTropCourt, RocheRefusee, Arches);
	}
}

void FChantierGrottes::Indexer()
{
	// --- 6. l'index spatial ---------------------------------------------------
	//
	// LE RESEAU SAIT S'INDEXER LUI-MEME, et c'est ce qui permet au cache de le
	// relire sans le reconstruire : il ecrit les primitives, pose la grille,
	// puis appelle la meme fonction que ce temps-ci. Ecrite deux fois, elle
	// aurait diverge a la premiere retouche -- le depot a une regle contre les
	// formules recopiees dans deux fichiers.
	Out.ReconstruireIndex();
}

void FChantierGrottes::Verifier()
{
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
				? DebutTroncon[L + 1] : FinDesGaleries;
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

}

void WorldseedCaves::Build(const FWorldseedGeometry& Geometry,
	const TArray<float>& ElevationM, const TArray<float>& PrecipMm,
	const FWorldseedLithology& Lithology, const FWorldseedLithologyRules& LithoRules,
	const FWorldseedCaveRules& Rules, float HeightExaggeration, int32 Seed,
	FWorldseedCaveNetwork& Out)
{
	WORLDSEED_TRACE(Cavites);

	const double StartTime = FPlatformTime::Seconds();

	Out.Reset();

	if (Geometry.CellCount() <= 0 || ElevationM.Num() != Geometry.CellCount()
		|| Rules.ChamberSpacingM <= 0.0f)
	{
		return;
	}

	// --- LES DIX TEMPS, ET L'ORDRE EST PORTANT -----------------------------
	//
	// On ne peut pas router avant d'avoir relie, ni ouvrir avant de savoir
	// combien de reseaux existent, ni indexer avant que toutes les primitives
	// soient posees. Ecrit ainsi, l'ordre se LIT ; dans une fonction de mille
	// deux cent cinquante lignes, il fallait le reconstituer.
	FChantierGrottes Chantier(Geometry, ElevationM, PrecipMm, Lithology,
		LithoRules, Rules, HeightExaggeration, Seed, Out);

	Chantier.Semer();
	if (Out.Chambers.Num() == 0)
	{
		return;
	}

	Chantier.Relier();
	Chantier.Creuser();
	Chantier.Composantes();

	// LES TROIS FORMES D'OUVERTURE, ET ELLES NE SE VALENT PAS. Deux se FORMENT
	// toutes seules, par geologie ; la bouche de falaise est la GARANTIE, et
	// son compte tient donc compte de ce que les deux autres ont deja ouvert.
	// D'ou cet ordre, qui n'est pas negociable.
	Chantier.Effondrer();
	Chantier.Dissoudre();
	Chantier.Ouvrir();
	Chantier.Percer();

	Chantier.Indexer();
	Chantier.Verifier();

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] grottes : %d chambres, %d liaisons (%d de boucle), ")
		TEXT("%d troncons, %d abandonnees, %d reseaux, ")
		TEXT("%d bouches, %d GOUFFRES, %d DOLINES, %d ARCHES  (%.0f ms)"),
		Out.Chambers.Num(), Chantier.Aretes.Num(), Chantier.ABoucler,
		Out.Segments.Num(), Chantier.Abandonnees, Chantier.ParComposante.Num(),
		Chantier.Entrees, Chantier.Gouffres, Chantier.Dolines, Chantier.Arches,
		(FPlatformTime::Seconds() - StartTime) * 1000.0);
}
