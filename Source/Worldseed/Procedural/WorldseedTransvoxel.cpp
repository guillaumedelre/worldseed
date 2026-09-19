// Worldseed - mailleur Transvoxel : cellules regulieres et cellules de transition.

#include "Procedural/WorldseedTransvoxel.h"

#include "Procedural/WorldseedCaves.h"
#include "Procedural/WorldseedDensity.h"
#include "Procedural/WorldseedTransvoxelTables.h"
#include "Procedural/WorldseedVoxelChunk.h"

namespace WorldseedTransvoxel
{
namespace
{
	// --- LES HUIT COINS D'UNE CELLULE, figure 3.7 de Lengyel -----------------
	//
	// L'indice d'un coin vaut DX + 2.DY + 4.DZ, et c'est cette numerotation-la
	// que les tables supposent. La recopier ici plutot que de la recalculer
	// evite d'avoir a se souvenir de la convention a chaque lecture.
	constexpr int32 CoinDX[8] = { 0, 1, 0, 1, 0, 1, 0, 1 };
	constexpr int32 CoinDY[8] = { 0, 0, 1, 1, 0, 0, 1, 1 };
	constexpr int32 CoinDZ[8] = { 0, 0, 0, 0, 1, 1, 1, 1 };

	/**
	 * L'ENROULEMENT DES TRIANGLES, ET POURQUOI IL EST MESURE ET NON DEVINE.
	 *
	 * L'ordre des sommets decide de la face avant. Se tromper ne casse rien de
	 * visible dans la geometrie -- les triangles sont tous la -- mais le terrain
	 * disparait vu de l'exterieur, ou devient noir. Le depot a deja paye cette
	 * lecon deux fois sur le mailleur du moteur.
	 *
	 * Deux conventions se superposent ici, et elles se compensent peut-etre :
	 * Lengyel travaille en repere DIRECT avec le solide en negatif, Unreal en
	 * repere INDIRECT. Raisonner sur le produit des deux est exactement le genre
	 * de deduction qui a une chance sur deux d'etre juste.
	 *
	 * On MESURE donc. `FacesEndroit`, dans les statistiques, rend la part des
	 * triangles dont l'enroulement s'accorde avec la normale sortante -- laquelle
	 * est etablie par le GRADIENT du champ et ne doit rien a aucune convention.
	 * Zero ou cent pour cent, la reponse est nette ; entre les deux, c'est autre
	 * chose qui ne va pas.
	 */
	// MESURE, sonde ProbeTransvoxel du 19 septembre 2026 : a `true`, 0,1 % des
	// faces seulement s.accordaient avec la normale sortante. La reponse d.une
	// telle mesure est binaire, et elle l.a ete -- ce n.est donc pas un reglage
	// a affiner mais une convention a poser, et la voici posee.
	constexpr bool bInverserEnroulement = false;

	/**
	 * Le mailleur d'un chunk.
	 *
	 * IL NE REMPLIT PAS LA BOITE, IL SUIT LA SURFACE -- c'est le levier qui a
	 * fait passer le maillage de 23 a 1,6 ms par chunk, et il n'est pas question
	 * de le perdre en changeant de mailleur. Deux mecanismes s'y emploient :
	 *
	 *   - le cache de coins est PARESSEUX. Un chunk de 32 m a 33^3 = 35 937
	 *     coins ; la surface n'en touche qu'une coque. On n'evalue que ce qu'on
	 *     lit, et le compteur d'evaluations reste la grandeur qui explique le
	 *     cout ;
	 *   - la propagation part de germes et ne visite que les cellules
	 *     traversees, comme GenerateContinuation du moteur -- avec la meme
	 *     limite, dite franchement : une poche entierement contenue entre deux
	 *     points du balayage grossier n'est pas vue.
	 *
	 * Les deux ensemble donnent le meme profil de cout que le mailleur du
	 * moteur, ce qui est la condition pour que la comparaison ait un sens.
	 */
	struct FMailleur
	{
		const FWorldseedDensity& Champ;
		const FWorldseedCaveLocal* Grottes = nullptr;

		FVector MinM = FVector::ZeroVector;
		double Voxel = 1.0;
		int32 N = 0;    // cellules par cote
		int32 NP = 0;   // coins par cote = N + 1

		TArray<float> Valeurs;
		TArray<bool> Connu;
		int32 Evaluations = 0;

		/**
		 * Un sommet par ARETE DE GRILLE, et c'est ce qui soude le maillage.
		 *
		 * Lengyel resout le meme probleme par ses tables de REUTILISATION
		 * (figures 3.8 et 4.17), qui disent de quelle cellule deja maillee
		 * reprendre un sommet. C'est plus rapide, et cela demande de tenir un
		 * cache indexe par cellule dans un balayage ORDONNE -- ce qu'une
		 * propagation depuis des germes ne fait justement pas.
		 *
		 * Or l'identite d'un sommet est de toute facon celle de son arete : deux
		 * cellules voisines qui partagent une arete partagent le sommet qui s'y
		 * trouve. On indexe donc par l'arete, ce qui donne exactement le meme
		 * maillage soude sans imposer d'ordre de parcours.
		 */
		TMap<int32, int32> SommetParArete;

		FWorldseedVoxelMesh& Sortie;

		FMailleur(const FWorldseedDensity& InChamp, const FWorldseedCaveLocal* InGrottes,
			FWorldseedVoxelMesh& InSortie)
			: Champ(InChamp), Grottes(InGrottes), Sortie(InSortie)
		{
		}

		int32 IndexCoin(int32 I, int32 J, int32 K) const
		{
			return (K * NP + J) * NP + I;
		}

		FVector PositionCoin(int32 I, int32 J, int32 K) const
		{
			return FVector(MinM.X + I * Voxel, MinM.Y + J * Voxel, MinM.Z + K * Voxel);
		}

		float Coin(int32 I, int32 J, int32 K)
		{
			const int32 Idx = IndexCoin(I, J, K);
			if (!Connu[Idx])
			{
				Valeurs[Idx] = static_cast<float>(
					Champ.At(PositionCoin(I, J, K), Grottes));
				Connu[Idx] = true;
				++Evaluations;
			}
			return Valeurs[Idx];
		}

		/** Le code de cas d'une cellule : un bit par coin, arme si le coin est SOLIDE. */
		int32 CodeDeCas(int32 X, int32 Y, int32 Z, float Densites[8])
		{
			int32 Code = 0;
			for (int32 C = 0; C < 8; ++C)
			{
				Densites[C] = Coin(X + CoinDX[C], Y + CoinDY[C], Z + CoinDZ[C]);

				// NEGATIF = ROCHE. C'est la convention du champ de densite du
				// projet, et c'est aussi celle de Lengyel : les tables sont donc
				// utilisables telles quelles, sans inversion du code de cas.
				if (Densites[C] < 0.0f)
				{
					Code |= (1 << C);
				}
			}
			return Code;
		}

		/**
		 * Le sommet porte par l'arete reliant deux coins de la cellule.
		 *
		 * Rend un indice dans Sortie.Positions, en creant le sommet a la
		 * premiere rencontre seulement.
		 */
		int32 SommetSurArete(int32 X, int32 Y, int32 Z, int32 C0, int32 C1,
			const float Densites[8])
		{
			int32 I0 = X + CoinDX[C0], J0 = Y + CoinDY[C0], K0 = Z + CoinDZ[C0];
			int32 I1 = X + CoinDX[C1], J1 = Y + CoinDY[C1], K1 = Z + CoinDZ[C1];
			float D0 = Densites[C0];
			float D1 = Densites[C1];

			// ON ORIENTE L'ARETE DU COIN BAS VERS LE COIN HAUT. Sans cela, la
			// meme arete vue depuis deux cellules voisines donnerait deux cles
			// differentes, donc deux sommets au lieu d'un -- une fissure
			// invisible a la lecture du code et tres visible a l'image.
			if (I1 < I0 || J1 < J0 || K1 < K0)
			{
				Swap(I0, I1); Swap(J0, J1); Swap(K0, K1);
				Swap(D0, D1);
			}

			const int32 Axe = (I1 != I0) ? 0 : ((J1 != J0) ? 1 : 2);
			const int32 Cle = IndexCoin(I0, J0, K0) * 3 + Axe;

			if (const int32* Deja = SommetParArete.Find(Cle))
			{
				return *Deja;
			}

			const float Ecart = D0 - D1;
			const double T = (FMath::Abs(Ecart) > UE_SMALL_NUMBER)
				? FMath::Clamp(static_cast<double>(D0) / static_cast<double>(Ecart), 0.0, 1.0)
				: 0.5;

			const FVector A = PositionCoin(I0, J0, K0);
			const FVector B = PositionCoin(I1, J1, K1);
			const FVector P = A + (B - A) * T;

			const int32 Indice = Sortie.Positions.Num();
			Sortie.Positions.Add(P * WorldseedMetersToCm);
			SommetParArete.Add(Cle, Indice);
			return Indice;
		}

		/** Triangule une cellule reguliere. Rend faux si elle ne porte pas de surface. */
		bool CelluleReguliere(int32 X, int32 Y, int32 Z)
		{
			float Densites[8];
			const int32 Code = CodeDeCas(X, Y, Z, Densites);

			if (Code == 0 || Code == 255)
			{
				return false;   // tout air, ou tout roche
			}

			const uint8 Classe = regularCellClass[Code];
			const RegularCellData& Donnees = regularCellData[Classe];
			const unsigned short* const Aretes = regularVertexData[Code];

			int32 Locaux[12];
			const int32 NbSommets = Donnees.GetVertexCount();
			for (int32 S = 0; S < NbSommets; ++S)
			{
				// Octet bas du code d'arete : les deux extremites, figure 3.7.
				// L'octet haut porte la reutilisation, dont on n'a pas besoin --
				// voir SommetParArete.
				const unsigned short CodeArete = Aretes[S];
				const int32 C0 = (CodeArete >> 4) & 0x0F;
				const int32 C1 = CodeArete & 0x0F;
				Locaux[S] = SommetSurArete(X, Y, Z, C0, C1, Densites);
			}

			const int32 NbTriangles = Donnees.GetTriangleCount();
			for (int32 T = 0; T < NbTriangles; ++T)
			{
				const int32 A = Locaux[Donnees.vertexIndex[T * 3 + 0]];
				const int32 B = Locaux[Donnees.vertexIndex[T * 3 + 1]];
				const int32 C = Locaux[Donnees.vertexIndex[T * 3 + 2]];

				// Un triangle degenere n'apporte rien et fausse la moyenne des
				// normales. Le marching cubes en produit des qu'une traversee
				// tombe pile sur un coin.
				if (A == B || B == C || A == C)
				{
					continue;
				}

				Sortie.Triangles.Add(A);
				if (bInverserEnroulement)
				{
					Sortie.Triangles.Add(C);
					Sortie.Triangles.Add(B);
				}
				else
				{
					Sortie.Triangles.Add(B);
					Sortie.Triangles.Add(C);
				}
			}
			return true;
		}
	};
}   // namespace anonyme

bool Mailler(const FWorldseedDensity& Density,
	const FWorldseedCaveLocal* Caves, const FBox& BoundsM,
	float VoxelSizeM, uint8 MasqueTransition,
	FWorldseedVoxelMesh& Out, FWorldseedVoxelStats& OutStats,
	TFunction<bool()> ShouldStop)
{
	Out.Reset();
	OutStats = FWorldseedVoxelStats();

	if (!Density.IsValid() || VoxelSizeM <= 0.0f || !BoundsM.IsValid)
	{
		OutStats.Cause = FWorldseedVoxelStats::ECause::Annule;
		return false;
	}

	// LES CELLULES DE TRANSITION NE SONT PAS ENCORE ECRITES, ET ON LE DIT.
	//
	// Les traiter silencieusement comme « pas de transition » serait le pire des
	// deux mondes : le raccord manquerait et rien ne l'annoncerait. Le masque
	// n'est jamais arme tant que les anneaux de resolution ne sont pas poses, ce
	// garde-fou ne se declenche donc pas -- il est la pour le jour ou quelqu'un
	// armerait le masque avant que la suite n'existe.
	if (MasqueTransition != AucuneFace)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] transvoxel : masque de transition 0x%02X demande, ")
			TEXT("mais les cellules de transition ne sont pas encore ecrites -- ")
			TEXT("le chunk est maille en resolution uniforme, donc AVEC fissure."),
			MasqueTransition);
	}

	const double Debut = FPlatformTime::Seconds();

	const FVector Taille = BoundsM.GetSize();
	const int32 N = FMath::Max(FMath::RoundToInt(Taille.X / VoxelSizeM), 1);

	FMailleur M(Density, Caves, Out);
	M.MinM = BoundsM.Min;
	M.Voxel = VoxelSizeM;
	M.N = N;
	M.NP = N + 1;
	M.Valeurs.SetNumUninitialized(M.NP * M.NP * M.NP);
	M.Connu.Init(false, M.NP * M.NP * M.NP);

	// --- germes -------------------------------------------------------------
	//
	// MEME BALAYAGE GROSSIER QUE LE MAILLEUR DU MOTEUR, et c'est volontaire :
	// c'est ce qui rend les deux comparables. Un point sur quatre par axe, et
	// l'on interpole le point de traversee sur l'arete plutot que de semer au
	// coin -- le depot a paye cette difference en TROUS DANS LE SOL, un chunk
	// entier disparaissant sur un champ de dunes parce qu'aucun germe ne tombait
	// dans une cellule reellement traversee.
	constexpr int32 PasGrossier = 4;
	const int32 NG = FMath::Max(
		FMath::CeilToInt(static_cast<double>(N) / PasGrossier), 1);

	TArray<FIntVector> File;
	TSet<FIntVector> Vues;

	auto Empiler = [&](int32 X, int32 Y, int32 Z)
	{
		if (X < 0 || Y < 0 || Z < 0 || X >= N || Y >= N || Z >= N) { return; }
		const FIntVector C(X, Y, Z);
		if (!Vues.Contains(C)) { Vues.Add(C); File.Add(C); }
	};

	int32 Germes = 0;
	for (int32 K = 0; K <= NG; ++K)
	{
		for (int32 J = 0; J <= NG; ++J)
		{
			for (int32 I = 0; I <= NG; ++I)
			{
				const int32 GI = FMath::Min(I * PasGrossier, N);
				const int32 GJ = FMath::Min(J * PasGrossier, N);
				const int32 GK = FMath::Min(K * PasGrossier, N);
				const float V = M.Coin(GI, GJ, GK);
				const bool bDedans = V < 0.0f;

				auto Semer = [&](int32 DI, int32 DJ, int32 DK)
				{
					const int32 HI = FMath::Min(GI + DI * PasGrossier, N);
					const int32 HJ = FMath::Min(GJ + DJ * PasGrossier, N);
					const int32 HK = FMath::Min(GK + DK * PasGrossier, N);
					if (HI == GI && HJ == GJ && HK == GK) { return; }

					const float W = M.Coin(HI, HJ, HK);
					if ((W < 0.0f) == bDedans) { return; }

					const float Ecart = W - V;
					const double T = (FMath::Abs(Ecart) > UE_SMALL_NUMBER)
						? FMath::Clamp(static_cast<double>(-V) / static_cast<double>(Ecart), 0.0, 1.0)
						: 0.5;

					++Germes;

					// La cellule qui CONTIENT la traversee, plus ses voisines de
					// face : l'interpolation sur une arete de quatre metres place
					// le point au bon endroit a un voxel pres, et un germe pose
					// une cellule a cote ne propage rien du tout.
					const int32 CX = FMath::Clamp(
						FMath::FloorToInt(GI + (HI - GI) * T), 0, N - 1);
					const int32 CY = FMath::Clamp(
						FMath::FloorToInt(GJ + (HJ - GJ) * T), 0, N - 1);
					const int32 CZ = FMath::Clamp(
						FMath::FloorToInt(GK + (HK - GK) * T), 0, N - 1);

					Empiler(CX, CY, CZ);
					Empiler(CX - 1, CY, CZ); Empiler(CX + 1, CY, CZ);
					Empiler(CX, CY - 1, CZ); Empiler(CX, CY + 1, CZ);
					Empiler(CX, CY, CZ - 1); Empiler(CX, CY, CZ + 1);
				};

				Semer(1, 0, 0);
				Semer(0, 1, 0);
				Semer(0, 0, 1);
			}
		}
	}

	OutStats.Seeds = Germes;

	if (Germes == 0)
	{
		OutStats.Cause = FWorldseedVoxelStats::ECause::SansTraversee;
		OutStats.FieldSamples = M.Evaluations;
		OutStats.MeshMs = (FPlatformTime::Seconds() - Debut) * 1000.0;
		return false;
	}

	// --- propagation ---------------------------------------------------------
	Out.Positions.Reserve(4096);
	Out.Triangles.Reserve(8192);

	for (int32 Tete = 0; Tete < File.Num(); ++Tete)
	{
		if (ShouldStop && (Tete % 256 == 0) && ShouldStop())
		{
			OutStats.Cause = FWorldseedVoxelStats::ECause::Annule;
			return false;
		}

		const FIntVector C = File[Tete];
		if (M.CelluleReguliere(C.X, C.Y, C.Z))
		{
			Empiler(C.X - 1, C.Y, C.Z); Empiler(C.X + 1, C.Y, C.Z);
			Empiler(C.X, C.Y - 1, C.Z); Empiler(C.X, C.Y + 1, C.Z);
			Empiler(C.X, C.Y, C.Z - 1); Empiler(C.X, C.Y, C.Z + 1);
		}
	}

	OutStats.FieldSamples = M.Evaluations;
	OutStats.MeshMs = (FPlatformTime::Seconds() - Debut) * 1000.0;

	if (ShouldStop && ShouldStop())
	{
		OutStats.Cause = FWorldseedVoxelStats::ECause::Annule;
		return false;
	}
	if (Out.Triangles.Num() == 0 || Out.Positions.Num() == 0)
	{
		OutStats.Cause = FWorldseedVoxelStats::ECause::MaillageVide;
		return false;
	}

	// --- normales ------------------------------------------------------------
	//
	// RIGOUREUSEMENT LE MEME CALCUL QUE LE MAILLEUR DU MOTEUR, et c'est
	// delibere : la comparaison doit isoler le MAILLEUR, et non melanger deux
	// changements. Moyenne des faces ponderee par l'aire pour la DIRECTION,
	// gradient du champ SOMMET PAR SOMMET pour le SENS -- les deux lecons sont
	// deja ecrites en toutes lettres dans WorldseedVoxelChunk.cpp.
	const double DebutNormales = FPlatformTime::Seconds();
	const int32 NbSommets = Out.Positions.Num();

	Out.Normals.SetNumZeroed(NbSommets);

	for (int32 T = 0; T + 2 < Out.Triangles.Num(); T += 3)
	{
		const int32 A = Out.Triangles[T];
		const int32 B = Out.Triangles[T + 1];
		const int32 C = Out.Triangles[T + 2];

		const FVector Face = FVector::CrossProduct(
			Out.Positions[B] - Out.Positions[A],
			Out.Positions[C] - Out.Positions[A]);

		Out.Normals[A] += Face;
		Out.Normals[B] += Face;
		Out.Normals[C] += Face;
	}

	for (int32 I = 0; I < NbSommets; ++I)
	{
		if (!Out.Normals[I].Normalize())
		{
			Out.Normals[I] = FVector::UpVector;
		}
	}

	{
		const double H = FMath::Max(VoxelSizeM * 0.5f, 0.05f);
		for (int32 I = 0; I < NbSommets; ++I)
		{
			const FVector P = Out.Positions[I] / WorldseedMetersToCm;
			const FVector Gradient(
				Density.At(FVector(P.X + H, P.Y, P.Z), Caves) - Density.At(FVector(P.X - H, P.Y, P.Z), Caves),
				Density.At(FVector(P.X, P.Y + H, P.Z), Caves) - Density.At(FVector(P.X, P.Y - H, P.Z), Caves),
				Density.At(FVector(P.X, P.Y, P.Z + H), Caves) - Density.At(FVector(P.X, P.Y, P.Z - H), Caves));

			if (FVector::DotProduct(Gradient, Out.Normals[I]) < 0.0)
			{
				Out.Normals[I] = -Out.Normals[I];
			}
		}
		OutStats.FieldSamples += NbSommets * 6;
	}

	// --- L'ENROULEMENT, MESURE ET NON SUPPOSE -------------------------------
	//
	// Les normales ci-dessus sortent du GRADIENT : elles pointent vers l'air, et
	// ne doivent rien a une convention d'enroulement. Comparer la normale
	// GEOMETRIQUE d'un triangle -- celle que son ordre de sommets impose -- a ces
	// normales-la dit donc si l'enroulement est le bon, et ne coute pas une
	// evaluation de champ de plus.
	{
		int32 Endroit = 0;
		int32 Total = 0;
		for (int32 T = 0; T + 2 < Out.Triangles.Num(); T += 3)
		{
			const int32 A = Out.Triangles[T];
			const int32 B = Out.Triangles[T + 1];
			const int32 C = Out.Triangles[T + 2];

			const FVector Face = FVector::CrossProduct(
				Out.Positions[B] - Out.Positions[A],
				Out.Positions[C] - Out.Positions[A]);

			const FVector Sortante =
				Out.Normals[A] + Out.Normals[B] + Out.Normals[C];

			if (FVector::DotProduct(Face, Sortante) > 0.0) { ++Endroit; }
			++Total;
		}
		OutStats.FacesEndroit = (Total > 0)
			? static_cast<float>(Endroit) / static_cast<float>(Total) : 0.0f;
	}

	OutStats.NormalMs = (FPlatformTime::Seconds() - DebutNormales) * 1000.0;
	OutStats.Vertices = NbSommets;
	OutStats.Triangles = Out.Triangles.Num() / 3;
	OutStats.FieldMs = OutStats.MeshMs;
	return true;
}

}   // namespace WorldseedTransvoxel
