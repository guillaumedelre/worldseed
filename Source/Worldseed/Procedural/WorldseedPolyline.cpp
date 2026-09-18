// Worldseed - outils de polyligne : longueur, simplification, contour.

#include "Procedural/WorldseedPolyline.h"


namespace
{
	/**
	 * Les segments [A,B] et [C,D] se croisent-ils ailleurs qu'a leurs bouts ?
	 *
	 * Le test est strict aux extremites : deux segments voisins partagent un
	 * point par construction, et cela n'est pas un croisement.
	 */
	bool SegmentsCross(const FVector2D& A, const FVector2D& B,
		const FVector2D& C, const FVector2D& D)
	{
		auto Side = [](const FVector2D& P, const FVector2D& Q, const FVector2D& R)
		{
			return (Q.X - P.X) * (R.Y - P.Y) - (Q.Y - P.Y) * (R.X - P.X);
		};

		const double D1 = Side(A, B, C);
		const double D2 = Side(A, B, D);
		const double D3 = Side(C, D, A);
		const double D4 = Side(C, D, B);

		return ((D1 > 0.0) != (D2 > 0.0)) && ((D3 > 0.0) != (D4 > 0.0));
	}

	/**
	 * Les segments [A,B] et [C,D] se genent-ils ?
	 *
	 * PAS SEULEMENT S'ILS SE TRAVERSENT. Un sommet pose SUR une arete, ou deux
	 * aretes qui se frolent a quelques centimetres, ne franchissent aucun test
	 * strict — mais l'offset qui dilate le contour, lui, travaille en virgule
	 * fixe et les voit confondus. Le polygone lui parait alors non simple, et
	 * il ne rend rien. Un contour peut donc etre sans croisement au sens strict
	 * et malgre tout indilatable ; c'est precisement ce qui restait.
	 */
	bool SegmentsInterfere(const FVector2D& A, const FVector2D& B,
		const FVector2D& C, const FVector2D& D, float Epsilon)
	{
		if (SegmentsCross(A, B, C, D))
		{
			return true;
		}

		FVector P, Q;
		FMath::SegmentDistToSegmentSafe(
			FVector(A, 0.0), FVector(B, 0.0), FVector(C, 0.0), FVector(D, 0.0), P, Q);

		return FVector::DistSquared(P, Q) < FMath::Square(Epsilon);
	}
}
namespace WorldseedPolyline
{
	namespace
	{
		/** Distance d'un point au segment porteur AB, version droite infinie. */
		float DistanceToLine(const FVector2D& P, const FVector2D& A, const FVector2D& B)
		{
			const FVector2D AB = B - A;
			const float Norm = static_cast<float>(AB.Size());

			if (Norm < 1e-9f)
			{
				return static_cast<float>((P - A).Size());
			}

			const float Cross = static_cast<float>(AB.X * (A.Y - P.Y) - (A.X - P.X) * AB.Y);
			return FMath::Abs(Cross) / Norm;
		}

		/** Clef d'un coin de grille, pour chainer les aretes de contour. */
		FORCEINLINE int64 CornerKey(int32 X, int32 Y, int32 Stride)
		{
			return static_cast<int64>(Y) * Stride + X;
		}
	}

	float Length(const TArray<FVector2D>& Points)
	{
		float Total = 0.0f;
		for (int32 I = 1; I < Points.Num(); ++I)
		{
			Total += static_cast<float>((Points[I] - Points[I - 1]).Size());
		}
		return Total;
	}

	TArray<FVector2D> Simplify(const TArray<FVector2D>& Points,
		float Tolerance, float MaxSpacing)
	{
		const int32 Num = Points.Num();
		if (Num < 3 || Tolerance <= 0.0f)
		{
			return Points;
		}

		TArray<bool> Keep;
		Keep.Init(false, Num);
		Keep[0] = true;
		Keep[Num - 1] = true;

		// Pile explicite plutot que recursion : une riviere peut compter des
		// dizaines de milliers de points, et la pile d'appels y passerait.
		TArray<TPair<int32, int32>> Stack;
		Stack.Emplace(0, Num - 1);

		while (Stack.Num() > 0)
		{
			const TPair<int32, int32> Range = Stack.Pop();
			const int32 Lo = Range.Key;
			const int32 Hi = Range.Value;
			if (Hi <= Lo + 1)
			{
				continue;
			}

			float Worst = -1.0f;
			int32 WorstIndex = Lo + 1;
			for (int32 K = Lo + 1; K < Hi; ++K)
			{
				const float D = DistanceToLine(Points[K], Points[Lo], Points[Hi]);
				if (D > Worst)
				{
					Worst = D;
					WorstIndex = K;
				}
			}

			if (Worst > Tolerance)
			{
				Keep[WorstIndex] = true;
				Stack.Emplace(Lo, WorstIndex);
				Stack.Emplace(WorstIndex, Hi);
			}
		}

		if (MaxSpacing > 0.0f)
		{
			const int32 Step = FMath::Max(FMath::RoundToInt(MaxSpacing), 1);
			for (int32 I = 0; I < Num; I += Step)
			{
				Keep[I] = true;
			}
			Keep[Num - 1] = true;
		}

		TArray<FVector2D> Out;
		Out.Reserve(Num);
		for (int32 I = 0; I < Num; ++I)
		{
			if (Keep[I])
			{
				Out.Add(Points[I]);
			}
		}
		return Out;
	}

	TArray<FVector2D> Reduce(const TArray<FVector2D>& Points, int32 MaxPoints,
		float StartTolerance)
	{
		if (Points.Num() <= MaxPoints || MaxPoints < 2)
		{
			return Points;
		}

		TArray<FVector2D> Result = Points;
		float Tolerance = FMath::Max(StartTolerance, 1e-4f);

		// Vingt-cinq elargissements a 1,3 couvrent un facteur 400 : largement de
		// quoi ramener n'importe quel contour sous le quota.
		for (int32 Pass = 0; Pass < 25; ++Pass)
		{
			Result = Simplify(Points, Tolerance, 0.0f);
			if (Result.Num() <= MaxPoints)
			{
				return Result;
			}
			Tolerance *= 1.3f;
		}

		return Result;
	}

	TArray<FVector2D> Decimate(const TArray<FVector2D>& Points, int32 MaxPoints)
	{
		if (Points.Num() <= MaxPoints || MaxPoints < 2)
		{
			return Points;
		}

		const int32 Step = FMath::Max(Points.Num() / MaxPoints, 1);

		TArray<FVector2D> Out;
		Out.Reserve(MaxPoints + 1);
		for (int32 I = 0; I < Points.Num(); I += Step)
		{
			Out.Add(Points[I]);
		}

		// L'embouchure ne doit jamais sauter : c'est elle qui touche l'eau.
		if (Out.Last() != Points.Last())
		{
			Out.Add(Points.Last());
		}
		return Out;
	}


	int32 FillDiagonalPinches(TArray<bool>& Mask, int32 Width, int32 Height)
	{
		if (Width < 2 || Height < 2 || Mask.Num() != Width * Height)
		{
			return 0;
		}

		int32 Filled = 0;

		// Combler un pincement peut en creer un autre chez le voisin : on
		// repasse jusqu'a stabilite. La convergence est garantie, chaque passe
		// ne faisant qu'ajouter des cellules.
		bool bChanged = true;
		while (bChanged)
		{
			bChanged = false;

			for (int32 Row = 0; Row + 1 < Height; ++Row)
			{
				for (int32 Col = 0; Col + 1 < Width; ++Col)
				{
					const int32 TL = Row * Width + Col;
					const int32 TR = TL + 1;
					const int32 BL = TL + Width;
					const int32 BR = BL + 1;

					// Deux cellules opposees dedans, les deux autres dehors :
					// le coin central est un pincement.
					if (Mask[TL] && Mask[BR] && !Mask[TR] && !Mask[BL])
					{
						Mask[TR] = true;
						++Filled;
						bChanged = true;
					}
					else if (Mask[TR] && Mask[BL] && !Mask[TL] && !Mask[BR])
					{
						Mask[TL] = true;
						++Filled;
						bChanged = true;
					}
				}
			}
		}

		return Filled;
	}
	TArray<FVector2D> TraceOutline(const TArray<bool>& Mask, int32 Width, int32 Height)
	{
		TArray<FVector2D> Best;
		if (Width < 1 || Height < 1 || Mask.Num() != Width * Height)
		{
			return Best;
		}

		auto Inside = [&Mask, Width, Height](int32 Col, int32 Row)
		{
			return Col >= 0 && Col < Width && Row >= 0 && Row < Height
				&& Mask[Row * Width + Col];
		};

		// Chaque arete de bord devient un pas oriente d'un coin de grille au
		// suivant, interieur a gauche. Les coins vont de 0 a Width inclus.
		const int32 Stride = Width + 1;
		TMap<int64, int64> NextCorner;
		NextCorner.Reserve(Width * Height / 4 + 8);

		auto AddEdge = [&NextCorner, Stride](int32 X0, int32 Y0, int32 X1, int32 Y1)
		{
			NextCorner.Add(CornerKey(X0, Y0, Stride), CornerKey(X1, Y1, Stride));
		};

		for (int32 Row = 0; Row < Height; ++Row)
		{
			for (int32 Col = 0; Col < Width; ++Col)
			{
				if (!Inside(Col, Row))
				{
					continue;
				}

				if (!Inside(Col, Row - 1)) { AddEdge(Col + 1, Row, Col, Row); }
				if (!Inside(Col - 1, Row)) { AddEdge(Col, Row, Col, Row + 1); }
				if (!Inside(Col, Row + 1)) { AddEdge(Col, Row + 1, Col + 1, Row + 1); }
				if (!Inside(Col + 1, Row)) { AddEdge(Col + 1, Row + 1, Col + 1, Row); }
			}
		}

		// Une cuvette peut avoir des iles : plusieurs boucles. On garde la plus
		// longue, qui est le rivage exterieur.
		TSet<int64> Visited;
		for (const TPair<int64, int64>& Entry : NextCorner)
		{
			if (Visited.Contains(Entry.Key))
			{
				continue;
			}

			TArray<FVector2D> Loop;
			int64 Current = Entry.Key;

			while (!Visited.Contains(Current))
			{
				Visited.Add(Current);

				const int32 X = static_cast<int32>(Current % Stride);
				const int32 Y = static_cast<int32>(Current / Stride);
				Loop.Emplace(static_cast<float>(X), static_cast<float>(Y));

				const int64* Next = NextCorner.Find(Current);
				if (!Next)
				{
					break;
				}
				Current = *Next;
			}

			// LA BOUCLE DOIT REVENIR A SON POINT DE DEPART.
			//
			// Si elle s'arrete ailleurs, le contour n'est pas ferme — et tout
			// l'aval le refermera quand meme, par une corde tendue entre les
			// deux bouts. Sur un ruban de plusieurs kilometres, cette corde
			// traverse la nappe entiere. Mieux vaut le dire que de rendre un
			// contour qui ment sur sa nature.
			if (Loop.Num() > 2 && Current != Entry.Key)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[Worldseed] contour non ferme : %d points, depart %lld, arrivee %lld"),
					Loop.Num(), Entry.Key, Current);
			}

			if (Loop.Num() > Best.Num())
			{
				Best = MoveTemp(Loop);
			}
		}

		return Best;
	}

	int32 MakeSimple(TArray<FVector2D>& Points, float MinSpacing, float TouchEpsilon)
	{
		// --- les doublons d'abord -------------------------------------------
		// Un segment de longueur nulle n'a pas de direction : l'offset ne sait
		// pas de quel cote le pousser, et tout le reste en decoule.
		const float MinSq = FMath::Square(FMath::Max(MinSpacing, KINDA_SMALL_NUMBER));
		for (int32 I = Points.Num() - 1; I >= 0 && Points.Num() > 3; --I)
		{
			const FVector2D& Next = Points[(I + 1) % Points.Num()];
			if (FVector2D::DistSquared(Points[I], Next) < MinSq)
			{
				Points.RemoveAt(I, EAllowShrinking::No);
			}
		}

		// --- puis les croisements -------------------------------------------
		// A chaque croisement entre les segments I et J, la portion comprise
		// entre eux forme une boucle. On la retire, et l'on recommence : en
		// defaire une peut en reveler une autre.
		int32 Undone = 0;
		bool bChanged = true;
		while (bChanged && Points.Num() > 3)
		{
			bChanged = false;
			const int32 N = Points.Num();

			for (int32 I = 0; I < N && !bChanged; ++I)
			{
				for (int32 J = I + 2; J < N; ++J)
				{
					// Le dernier segment touche le premier : ce n'est pas un
					// croisement, c'est la fermeture de la boucle.
					if (I == 0 && J == N - 1)
					{
						continue;
					}

					if (!SegmentsInterfere(Points[I], Points[(I + 1) % N],
						Points[J], Points[(J + 1) % N], TouchEpsilon))
					{
						continue;
					}

					// Deux parts se font face : celle entre I+1 et J, et le
					// reste. On garde la plus longue — c'est le lac, l'autre
					// n'est que l'excursion.
					const int32 InnerCount = J - I;
					if (InnerCount * 2 <= N)
					{
						Points.RemoveAt(I + 1, InnerCount, EAllowShrinking::No);
					}
					else
					{
						TArray<FVector2D> Kept;
						Kept.Reserve(InnerCount);
						for (int32 K = I + 1; K <= J; ++K)
						{
							Kept.Add(Points[K]);
						}
						Points = MoveTemp(Kept);
					}

					++Undone;
					bChanged = true;
					break;
				}
			}
		}

		return Undone;
	}

	FWorldseedRing Measure(const TArray<FVector2D>& Ring)
	{
		FWorldseedRing Out;
		Out.Points = Ring.Num();
		if (Ring.Num() < 3)
		{
			return Out;
		}

		double Perimeter = 0.0;
		double TwiceArea = 0.0;
		for (int32 I = 0, N = Ring.Num(); I < N; ++I)
		{
			const FVector2D& Here = Ring[I];
			const FVector2D& Next = Ring[(I + 1) % N];

			Perimeter += (Next - Here).Size();
			TwiceArea += (Here.X * Next.Y) - (Next.X * Here.Y);
		}

		// Les deux grandeurs que l'offset regarde et que les autres taisent.
		double Closest = TNumericLimits<double>::Max();
		for (int32 I = 0, N = Ring.Num(); I < N; ++I)
		{
			const FVector2D& Prev = Ring[(I + N - 1) % N];
			const FVector2D& Next = Ring[(I + 1) % N];
			const FVector2D A = (Ring[I] - Prev).GetSafeNormal();
			const FVector2D B = (Next - Ring[I]).GetSafeNormal();
			if (FMath::Abs(A.X * B.Y - A.Y * B.X) < 1.0e-6)
			{
				++Out.Collinear;
			}

			for (int32 J = I + 2; J < N; ++J)
			{
				if (I == 0 && J == N - 1)
				{
					continue;
				}
				Closest = FMath::Min(Closest,
					static_cast<double>(FVector2D::Distance(Ring[I], Ring[J])));
			}
		}
		Out.MinVertexGap = static_cast<float>(Closest);

		Out.Perimeter = static_cast<float>(Perimeter);
		Out.SignedArea = static_cast<float>(TwiceArea * 0.5);
		return Out;
	}
}

namespace WorldseedPolyline
{
	TArray<FVector2D> ConvexHull(const TArray<FVector2D>& Points)
	{
		TArray<FVector2D> Hull;
		if (Points.Num() < 3)
		{
			return Hull;
		}

		// Chaine monotone d'Andrew : on trie, puis on balaie une fois vers
		// l'aller et une fois vers le retour en depilant tout virage a droite.
		TArray<FVector2D> Sorted = Points;
		Sorted.Sort([](const FVector2D& A, const FVector2D& B)
			{ return (A.X != B.X) ? (A.X < B.X) : (A.Y < B.Y); });

		auto Turn = [](const FVector2D& O, const FVector2D& A, const FVector2D& B)
		{
			return (A.X - O.X) * (B.Y - O.Y) - (A.Y - O.Y) * (B.X - O.X);
		};

		Hull.Reserve(Sorted.Num() * 2);

		for (const FVector2D& P : Sorted)
		{
			while (Hull.Num() >= 2
				&& Turn(Hull[Hull.Num() - 2], Hull.Last(), P) <= 0.0)
			{
				Hull.Pop(EAllowShrinking::No);
			}
			Hull.Add(P);
		}

		const int32 LowerCount = Hull.Num() + 1;
		for (int32 I = Sorted.Num() - 2; I >= 0; --I)
		{
			const FVector2D& P = Sorted[I];
			while (Hull.Num() >= LowerCount
				&& Turn(Hull[Hull.Num() - 2], Hull.Last(), P) <= 0.0)
			{
				Hull.Pop(EAllowShrinking::No);
			}
			Hull.Add(P);
		}

		// Le dernier point repete le premier.
		Hull.Pop(EAllowShrinking::No);
		return Hull;
	}
}
