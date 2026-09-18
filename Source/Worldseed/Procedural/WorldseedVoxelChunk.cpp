// Worldseed - maillage d'un chunk de voxels par surface implicite.

#include "Procedural/WorldseedVoxelChunk.h"

#include "Procedural/WorldseedDensity.h"

#include "Generators/MarchingCubes.h"

#include <atomic>

int32 FWorldseedVoxelMesh::BytesUsed() const
{
	return Positions.GetAllocatedSize() + Triangles.GetAllocatedSize()
		+ Normals.GetAllocatedSize() + Colours.GetAllocatedSize()
		+ TintRG.GetAllocatedSize() + TintB.GetAllocatedSize();
}

namespace WorldseedVoxelChunk
{
	bool Build(const FWorldseedDensity& Density, const FBox& BoundsM,
		float VoxelSizeM, FWorldseedVoxelMesh& Out, FWorldseedVoxelStats& OutStats,
		TFunction<bool()> ShouldStop)
	{
		using namespace UE::Geometry;

		Out.Reset();
		OutStats = FWorldseedVoxelStats();

		if (!Density.IsValid() || VoxelSizeM <= 0.0f || !BoundsM.IsValid)
		{
			return false;
		}

		// Le compteur d'evaluations est LE chiffre qui explique le cout : le
		// temps de maillage ne suit ni le volume de la boite ni le nombre de
		// triangles, il suit le nombre de fois ou le champ a ete interroge.
		std::atomic<int32> Samples{ 0 };
		const double FieldStart = FPlatformTime::Seconds();

		FMarchingCubes MC;
		MC.Implicit = [&Density, &Samples](const FVector3d& P) -> double
		{
			Samples.fetch_add(1, std::memory_order_relaxed);
			return Density.At(FVector(P));
		};
		MC.IsoValue = 0.0;
		MC.CubeSize = VoxelSizeM;
		MC.Bounds = FAxisAlignedBox3d(
			FVector3d(BoundsM.Min.X, BoundsM.Min.Y, BoundsM.Min.Z),
			FVector3d(BoundsM.Max.X, BoundsM.Max.Y, BoundsM.Max.Z));

		// Un seul niveau de parallelisme : voir l'en-tete.
		MC.bParallelCompute = false;

		// SingleLerp suffit sur un champ lisse. LerpSteps et Bisection ne
		// gagnent que sur des champs a fort gradient -- et coutent une
		// evaluation de plus par arete, c'est-a-dire la grandeur qui domine.
		MC.RootMode = ERootfindingModes::SingleLerp;

		if (ShouldStop)
		{
			MC.CancelF = [ShouldStop]() { return ShouldStop(); };
		}

		// --- germes -------------------------------------------------------------
		//
		// ON NE REMPLIT PAS LA BOITE, ON SUIT LA SURFACE.
		//
		// Generate() evalue le champ dans chaque cellule du chunk : 32 768 pour
		// un cube de 32 m a un metre. Or la surface n'en traverse qu'une coque,
		// de l'ordre du millier. GenerateContinuation part de germes et propage
		// de cellule en cellule tant qu'elle trouve la surface -- le cout suit
		// alors l'AIRE, pas le volume.
		//
		// Les germes viennent d'un balayage grossier, un point sur quatre par
		// axe : 729 evaluations au lieu de 32 768, et il tranche du meme coup le
		// cas des chunks vides, qui n'ont alors plus rien a payer.
		//
		// CE QU'ON PERD, ET IL FAUT LE SAVOIR : une poche entierement contenue
		// entre deux points du balayage -- moins de quatre metres ici -- n'est
		// pas vue, donc pas maillee. Les galeries font neuf metres de rayon au
		// plus, elles passent largement ; une bulle de deux metres, non.
		constexpr int32 PasGrossier = 4;

		const FVector Taille = BoundsM.GetSize();
		const double PasM = VoxelSizeM * PasGrossier;
		const int32 NX = FMath::Max(FMath::CeilToInt(Taille.X / PasM), 1);
		const int32 NY = FMath::Max(FMath::CeilToInt(Taille.Y / PasM), 1);
		const int32 NZ = FMath::Max(FMath::CeilToInt(Taille.Z / PasM), 1);

		TArray<double> Grossier;
		Grossier.SetNumUninitialized((NX + 1) * (NY + 1) * (NZ + 1));

		auto Index = [NX, NY](int32 I, int32 J, int32 K)
		{
			return (K * (NY + 1) + J) * (NX + 1) + I;
		};
		auto Point = [&](int32 I, int32 J, int32 K)
		{
			return FVector(
				FMath::Min(BoundsM.Min.X + I * PasM, BoundsM.Max.X),
				FMath::Min(BoundsM.Min.Y + J * PasM, BoundsM.Max.Y),
				FMath::Min(BoundsM.Min.Z + K * PasM, BoundsM.Max.Z));
		};

		for (int32 K = 0; K <= NZ; ++K)
		{
			for (int32 J = 0; J <= NY; ++J)
			{
				for (int32 I = 0; I <= NX; ++I)
				{
					Grossier[Index(I, J, K)] = Density.At(Point(I, J, K));
				}
			}
		}
		Samples.fetch_add(Grossier.Num(), std::memory_order_relaxed);

		TArray<FVector3d> Germes;
		for (int32 K = 0; K <= NZ; ++K)
		{
			for (int32 J = 0; J <= NY; ++J)
			{
				for (int32 I = 0; I <= NX; ++I)
				{
					const double V = Grossier[Index(I, J, K)];
					const bool bDedans = V < MC.IsoValue;

					// Un seul voisin suffit a signaler la traversee ; on ne
					// regarde que vers l'avant pour ne pas semer deux fois la
					// meme arete.
					auto Traverse = [&](int32 DI, int32 DJ, int32 DK)
					{
						if (I + DI > NX || J + DJ > NY || K + DK > NZ)
						{
							return false;
						}
						const double W = Grossier[Index(I + DI, J + DJ, K + DK)];
						return (W < MC.IsoValue) != bDedans;
					};

					if (Traverse(1, 0, 0) || Traverse(0, 1, 0) || Traverse(0, 0, 1))
					{
						const FVector P = Point(I, J, K);
						Germes.Emplace(P.X, P.Y, P.Z);
					}
				}
			}
		}

		if (Germes.Num() == 0)
		{
			// Aucune traversee : le chunk est tout roche ou tout air. C'est le
			// cas le plus frequent, et il ne coute plus que le balayage.
			OutStats.FieldSamples = Samples.load(std::memory_order_relaxed);
			OutStats.MeshMs = (FPlatformTime::Seconds() - FieldStart) * 1000.0;
			return false;
		}

		MC.GenerateContinuation(Germes);

		const double MeshDone = FPlatformTime::Seconds();
		OutStats.FieldSamples = Samples.load(std::memory_order_relaxed);
		OutStats.MeshMs = (MeshDone - FieldStart) * 1000.0;

		if (ShouldStop && ShouldStop())
		{
			return false;
		}
		if (MC.Triangles.Num() == 0 || MC.Vertices.Num() == 0)
		{
			return false;
		}

		// --- transfert ----------------------------------------------------------
		const int32 VertexCount = MC.Vertices.Num();
		Out.Positions.SetNumUninitialized(VertexCount);
		Out.Normals.SetNumUninitialized(VertexCount);

		for (int32 I = 0; I < VertexCount; ++I)
		{
			const FVector3d& V = MC.Vertices[I];
			Out.Positions[I] = FVector(V.X, V.Y, V.Z) * WorldseedMetersToCm;
		}

		Out.Triangles.SetNumUninitialized(MC.Triangles.Num() * 3);
		for (int32 T = 0; T < MC.Triangles.Num(); ++T)
		{
			const FIndex3i& Tri = MC.Triangles[T];

			// ORDRE INVERSE : le marching cubes de GeometryCore oriente ses
			// faces pour un champ dont l'interieur est POSITIF. Le notre est
			// negatif dans la roche -- convention de distance signee -- donc
			// les faces sortiraient a l'envers et le terrain serait invisible
			// de l'exterieur, sans le moindre message.
			Out.Triangles[T * 3 + 0] = Tri.A;
			Out.Triangles[T * 3 + 1] = Tri.C;
			Out.Triangles[T * 3 + 2] = Tri.B;
		}

		// --- normales -----------------------------------------------------------
		//
		// MOYENNE DES FACES, ET NON GRADIENT DU CHAMP.
		//
		// Le gradient est la normale EXACTE de la surface que le maillage
		// approche, et c'est tentant. Mais il coute six evaluations du champ par
		// sommet : mesure, 195 ms sur 943, soit 21 % du temps de maillage pour
		// une difference que la densite du maillage rend tenue -- le marching
		// cubes partage ses sommets le long des aretes, donc la moyenne des
		// faces incidentes est deja continue.
		//
		// Si un jour l'eclairage montre des facettes, le gradient est dans
		// l'historique git, avec son cout.
		const double NormalStart = FPlatformTime::Seconds();

		for (int32 I = 0; I < VertexCount; ++I)
		{
			Out.Normals[I] = FVector::ZeroVector;
		}

		for (int32 T = 0; T + 2 < Out.Triangles.Num(); T += 3)
		{
			const int32 A = Out.Triangles[T];
			const int32 B = Out.Triangles[T + 1];
			const int32 C = Out.Triangles[T + 2];

			// Produit vectoriel NON normalise : sa longueur vaut le double de
			// l'aire du triangle, ce qui pondere la moyenne par la surface. Les
			// slivers du marching cubes -- il en produit beaucoup -- ne pesent
			// alors presque rien, et c'est exactement ce qu'on veut.
			const FVector Face = FVector::CrossProduct(
				Out.Positions[B] - Out.Positions[A],
				Out.Positions[C] - Out.Positions[A]);

			Out.Normals[A] += Face;
			Out.Normals[B] += Face;
			Out.Normals[C] += Face;
		}

		for (int32 I = 0; I < VertexCount; ++I)
		{
			if (!Out.Normals[I].Normalize())
			{
				Out.Normals[I] = FVector::UpVector;
			}
		}

		// --- LE SENS, ET COMMENT ON LE SAIT -------------------------------------
		//
		// L'ordre des sommets d'un triangle decide du sens de sa normale, et la
		// convention d'enroulement de GeometryCore n'est pas celle d'Unreal. Se
		// tromper ne casse RIEN de visible dans la geometrie -- les faces
		// restent tournees vers la camera -- mais le terrain devient NOIR :
		// l'eclairage croit qu'il regarde ailleurs. Mesure du premier essai :
		// seules les aretes des ressauts accrochaient la lumiere.
		//
		// Plutot que de figer une convention et d'esperer, on la MESURE : le
		// gradient du champ croit vers l'air, donc il donne le dehors.
		//
		// ET ON L'ORIENTE SOMMET PAR SOMMET, pas une fois pour tout le chunk.
		// Deux versions ont echoue avant celle-ci, et leur echec disait la meme
		// chose : le sens des normales n'est PAS une propriete globale du chunk
		// sur laquelle on peut voter.
		//
		//   1. Un seul sommet, le numero zero. Si ce sommet tombe la ou le
		//      gradient est presque perpendiculaire a la normale, le chunk
		//      ENTIER se retourne, devient noir -- eclaire par-derriere -- et
		//      clignote, un chunk remaille en marchant ne retombant pas
		//      forcement du meme cote.
		//   2. Un vote pondere sur soixante-quatre sommets. Beaucoup mieux : la
		//      grande plaque noire a disparu. Mais il en restait, parce qu'une
		//      majorite n'est pas une preuve -- il suffit qu'une partie de la
		//      surface d'un chunk soit orientee autrement pour que le vote la
		//      sacrifie.
		//
		// La forme correcte est locale : chaque sommet a SON gradient, et il
		// tranche pour lui seul. On garde la normale moyennee des faces, qui est
		// lisse et continue le long des aretes partagees, et on ne corrige que
		// son SENS. Cout : six evaluations par sommet, mesure ci-dessous.
		{
			const double H = FMath::Max(VoxelSizeM * 0.5f, 0.05f);

			for (int32 I = 0; I < VertexCount; ++I)
			{
				const FVector3d& V = MC.Vertices[I];
				const FVector Gradient(
					Density.At(FVector(V.X + H, V.Y, V.Z)) - Density.At(FVector(V.X - H, V.Y, V.Z)),
					Density.At(FVector(V.X, V.Y + H, V.Z)) - Density.At(FVector(V.X, V.Y - H, V.Z)),
					Density.At(FVector(V.X, V.Y, V.Z + H)) - Density.At(FVector(V.X, V.Y, V.Z - H)));

				if (FVector::DotProduct(Gradient, Out.Normals[I]) < 0.0)
				{
					Out.Normals[I] = -Out.Normals[I];
				}
			}

			OutStats.FieldSamples += VertexCount * 6;
		}

		OutStats.NormalMs = (FPlatformTime::Seconds() - NormalStart) * 1000.0;

		OutStats.Vertices = VertexCount;
		OutStats.Triangles = MC.Triangles.Num();

		// MeshMs ne couvre plus que le marching cubes : les normales sont
		// desormais calculees APRES, et comptees a part. Les additionner serait
		// compter le maillage deux fois.
		OutStats.FieldMs = OutStats.MeshMs;
		return true;
	}
}
