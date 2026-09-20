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
	 * L'ORDRE DES BITS DU CODE DE CAS D'UNE CELLULE DE TRANSITION.
	 *
	 * Echantillons de la face pleine resolution, en ligne (figure 4.16) :
	 *
	 *     0 1 2
	 *     3 4 5
	 *     6 7 8
	 *
	 * et le code parcourt le PERIMETRE, le centre en poids fort. Cet ordre n'est
	 * ecrit nulle part en clair -- il est dans une figure -- il a donc ete DERIVE
	 * des tables, exhaustivement sur les 512 cas : zero faute sur 4 096 aretes,
	 * contre 37,5 % pour l'ordre sequentiel. Rejouable par
	 * `perl Tools/Transvoxel/ordre_bits.pl`.
	 */
	constexpr int32 BitVersEchantillon[9] = { 0, 1, 2, 5, 8, 7, 6, 3, 4 };

	/**
	 * LES QUATRE ECHANTILLONS DEMI-RESOLUTION NE SONT PAS DES INCONNUES.
	 *
	 * Section 4.5 : « the voxel values for locations 0 and 9 are the same, as are
	 * those for locations 2 and A, 6 and B, and 8 and C ». C'est ce qui ramene
	 * treize echantillons a NEUF bits -- et c'est aussi ce qui fait que la face
	 * demi-resolution d'une cellule de transition porte exactement les valeurs de
	 * la grille grossiere, donc qu'elle se soude aux cellules regulieres.
	 */
	constexpr int32 DemiVersPlein[4] = { 0, 2, 6, 8 };   // 9 -> 0, A -> 2, B -> 6, C -> 8

	/**
	 * Les six faces d'un chunk : l'axe normal, son signe, et les deux tangentes.
	 *
	 * LA REGLE QUI FIXE LES TANGENTES EST `U x V = NORMALE SORTANTE`. Sans elle,
	 * trois faces sur six auraient la main inverse et leurs triangles
	 * sortiraient a l'envers -- un defaut qui ne casse rien dans la geometrie et
	 * rend le terrain noir, comme le depot l'a deja paye deux fois.
	 */
	struct FFace
	{
		int32 AxeN; int32 SigneN;   // normale sortante
		int32 AxeU; int32 AxeV;     // tangentes, toutes deux de signe +1
	};
	constexpr FFace Faces[6] =
	{
		{ 0, -1, 2, 1 },   // -X : Z x Y = -X
		{ 0, +1, 1, 2 },   // +X : Y x Z = +X
		{ 1, -1, 0, 2 },   // -Y : X x Z = -Y
		{ 1, +1, 2, 0 },   // +Y : Z x X = +Y
		{ 2, -1, 1, 0 },   // -Z : Y x X = -Z
		{ 2, +1, 0, 1 },   // +Z : X x Y = +Z
	};

	/**
	 * L'ENROULEMENT DES TRIANGLES, ET L'ARBITRE QUI LE TRANCHE.
	 *
	 * A `false`, le terrain est INVISIBLE : chaque triangle est une face
	 * arriere, donc eliminee, et le joueur voit au travers de la surface proche
	 * jusqu'au DESSOUS de la surface lointaine -- des nappes qui s'arquent
	 * au-dessus de la tete et des lambeaux dans le ciel. Photographie le
	 * 20 septembre 2026 (`falaise02`), sur un monde entierement bati.
	 *
	 * CE N'EST PAS LA PREMIERE VALEUR QU'ON A POSEE, ET LA PREMIERE MESURE
	 * ETAIT AUTO-REFERENTIELLE. `ProbeTransvoxel` compare la normale geometrique
	 * d'un triangle aux normales de SES PROPRES sommets. Or celles-ci sont
	 * recalees sur le gradient : la mesure ne peut donc que confirmer « mes
	 * triangles s'accordent avec mes normales », et elle ne dit RIEN de la
	 * convention du rastériseur d'Unreal. Elle a rendu 0,1 %, on a conclu a une
	 * inversion, et la correction qui l'a portee a 99,9 % EST la regression.
	 *
	 * L'ARBITRE JUSTE EST UN TIERS : le gradient du champ, evalue au centre de
	 * gravite du triangle, confronte aux DEUX mailleurs par la meme sonde.
	 * `ProbeVoisins`, bloc de 4 x 4 x 4 chunks, au sommet du monde ET au
	 * littoral :
	 *
	 *     mailleur du moteur   0,3 % et 0,2 % de faces accordees au gradient
	 *     mailleur maison     99,8 % et 99,8 %
	 *
	 * Et c'est le mailleur du MOTEUR qui s'affiche correctement. La convention
	 * de face avant d'Unreal est donc celle-la, et non celle que le raisonnement
	 * annoncait -- j'avais DEDUIT le sens depuis la convention supposee de
	 * GeometryCore, et la deduction etait a l'envers.
	 *
	 * REGLE : un enroulement ne se deduit jamais, et il ne se mesure pas contre
	 * soi-meme. Il se mesure contre une grandeur EXTERIEURE aux deux mailleurs,
	 * et l'on verifie qu'elle classe correctement celui dont on sait deja qu'il
	 * s'affiche bien.
	 *
	 * Elle gouverne LES DEUX FAMILLES : `Triangle` compose
	 * `bEchanger = bInverserEnroulement != bInverse`, donc basculer cette
	 * constante retourne aussi les cellules de transition, en preservant l'ecart
	 * relatif que la mesure separee avait etabli.
	 */
	constexpr bool bInverserEnroulement = true;

	/**
	 * ET LES CELLULES DE TRANSITION ONT LEUR PROPRE CONVENTION DE BASE.
	 *
	 * Mesure du 20 septembre 2026, sonde ProbeTransition : cellules regulieres
	 * 99,6 % de faces a l'endroit, cellules de transition **0,0 %**. Toutes a
	 * l'envers, sans exception -- donc ce n'est pas le bit d'inversion des tables
	 * qui est mal lu, c'est la convention de DEPART qui differe.
	 *
	 * La raison probable est le sens de la base tangentielle : j'ai pose
	 * `U x V = normale SORTANTE`, et Lengyel regarde vraisemblablement la face
	 * depuis l'exterieur du bloc grossier, c'est-a-dire depuis le bloc fin.
	 * MAIS CE N'EST QU'UNE EXPLICATION, PAS LA PREUVE : ce qui tranche est le
	 * chiffre, et un zero pour cent ne laisse aucune place au doute.
	 *
	 * IL A FALLU SEPARER LES DEUX FAMILLES POUR LE VOIR. Melangees, elles
	 * donnaient 94,7 % -- une degradation vague qu'on aurait pu mettre sur le
	 * compte d'un detail geometrique, et qui masquait une convention entierement
	 * fausse sur la petite des deux populations.
	 */
	constexpr bool bInverserEnroulementTransition = true;

	/**
	 * Le mailleur d'un chunk.
	 *
	 * IL NE REMPLIT PAS LA BOITE, IL SUIT LA SURFACE -- c'est le levier qui a
	 * fait passer le maillage de 23 a 1,6 ms par chunk, et il n'est pas question
	 * de le perdre en changeant de mailleur. Cache de coins PARESSEUX, et
	 * propagation depuis des germes.
	 */
	struct FMailleur
	{
		const FWorldseedDensity& Champ;
		const FWorldseedCaveLocal* Grottes = nullptr;

		FVector MinM = FVector::ZeroVector;
		double Voxel = 1.0;
		int32 N = 0;    // cellules par cote
		int32 NP = 0;   // coins par cote = N + 1

		/** Faces bordant un voisin PLUS FIN, donc portant des cellules de transition. */
		uint8 Masque = AucuneFace;

		/** Epaisseur de la dalle de transition, en metres. */
		double Largeur = 0.0;

		TArray<float> Valeurs;
		TArray<bool> Connu;
		int32 Evaluations = 0;

		/**
		 * Un sommet par ARETE DE GRILLE, et c'est ce qui soude le maillage.
		 *
		 * Deux espaces de cles, parce qu'il y a deux nappes et qu'elles ne
		 * partagent AUCUN sommet -- verifie dans les tables, `Tools/Transvoxel/
		 * aretes.pl` : les seize aretes d'une cellule de transition sont douze
		 * sur la face pleine resolution et quatre sur la demi, jamais une
		 * laterale.
		 *
		 *  - `SommetParArete` porte la grille GROSSIERE : cellules regulieres, et
		 *    face demi-resolution des cellules de transition. Les deux doivent
		 *    partager leurs sommets, c'est la couture qui empeche la fissure.
		 *  - `SommetParAreteFine` porte la grille FINE sur le plan de frontiere,
		 *    ou seules vivent les faces pleine resolution.
		 */
		TMap<int32, int32> SommetParArete;
		TMap<int64, int32> SommetParAreteFine;

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

		/**
		 * LA RETRACTION : les cellules regulieres cedent la place a la dalle.
		 *
		 * Une cellule de transition occupe une tranche d'epaisseur `Largeur`
		 * contre la face ; si les cellules regulieres occupaient encore tout le
		 * chunk, les deux se chevaucheraient. Lengyel (section 4.4) « scale les
		 * cellules de bord pour laisser la place a une a trois cellules de
		 * transition », et garde pour cela DEUX positions par sommet de bord,
		 * primaire et secondaire, qu'un programme de sommet choisit selon le
		 * niveau des voisins.
		 *
		 * ON S'EN ECARTE ICI, ET C'EST ASSUME. Notre diffuseur REMAILLE un chunk
		 * quand son voisinage change de niveau : on peut donc cuire directement
		 * la bonne position au lieu d'en transporter deux et de trancher sur le
		 * GPU. C'est plus simple et cela coute un remaillage quand une frontiere
		 * d'anneau se deplace -- ce que le chunk fait de toute facon.
		 *
		 * La transformation est confinee a la PREMIERE COUCHE de cellules, elle
		 * envoie le plan de frontiere sur `Largeur` et laisse tout le reste
		 * identique. Elle est donc continue partout : pas de fissure interne.
		 */
		FVector Retracter(FVector P) const
		{
			if (Masque == AucuneFace || Largeur <= 0.0)
			{
				return P;
			}

			const double Cote = Voxel;   // une cellule de ce chunk
			const double Taille = N * Voxel;

			for (int32 F = 0; F < 6; ++F)
			{
				if ((Masque & (1 << F)) == 0) { continue; }

				const FFace& Fa = Faces[F];
				const double Local = P[Fa.AxeN] - MinM[Fa.AxeN];

				// Distance a la face, mesuree vers l'interieur du chunk.
				const double D = (Fa.SigneN > 0) ? (Taille - Local) : Local;
				if (D >= Cote) { continue; }

				const double Neuf = Largeur + D * (Cote - Largeur) / Cote;
				P[Fa.AxeN] = MinM[Fa.AxeN] +
					((Fa.SigneN > 0) ? (Taille - Neuf) : Neuf);
			}
			return P;
		}

		/** Le code de cas d'une cellule reguliere : un bit par coin, arme si SOLIDE. */
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

		/** Interpole le point de traversee d'une arete, en parametre. */
		static double Parametre(float D0, float D1)
		{
			const float Ecart = D0 - D1;
			return (FMath::Abs(Ecart) > UE_SMALL_NUMBER)
				? FMath::Clamp(static_cast<double>(D0) / static_cast<double>(Ecart), 0.0, 1.0)
				: 0.5;
		}

		/**
		 * Le sommet porte par une arete de la grille GROSSIERE.
		 *
		 * Sert aux cellules regulieres ET a la face demi-resolution des cellules
		 * de transition : c'est le MEME appel, donc le meme sommet, donc la
		 * couture est acquise et non esperee.
		 */
		int32 SommetSurAreteGrossiere(int32 I0, int32 J0, int32 K0,
			int32 I1, int32 J1, int32 K1, float D0, float D1)
		{
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

			const double T = Parametre(D0, D1);
			const FVector A = PositionCoin(I0, J0, K0);
			const FVector B = PositionCoin(I1, J1, K1);
			const FVector P = Retracter(A + (B - A) * T);

			const int32 Indice = Sortie.Positions.Num();
			Sortie.Positions.Add(P * WorldseedMetersToCm);
			SommetParArete.Add(Cle, Indice);
			return Indice;
		}

		int32 SommetSurArete(int32 X, int32 Y, int32 Z, int32 C0, int32 C1,
			const float Densites[8])
		{
			return SommetSurAreteGrossiere(
				X + CoinDX[C0], Y + CoinDY[C0], Z + CoinDZ[C0],
				X + CoinDX[C1], Y + CoinDY[C1], Z + CoinDZ[C1],
				Densites[C0], Densites[C1]);
		}

		/** Pose un triangle, en respectant l'enroulement demande. */
		void Triangle(int32 A, int32 B, int32 C, bool bInverse)
		{
			// Un triangle degenere n'apporte rien et fausse la moyenne des
			// normales. Le marching cubes en produit des qu'une traversee tombe
			// pile sur un coin.
			if (A == B || B == C || A == C) { return; }

			const bool bEchanger = (bInverserEnroulement != bInverse);
			Sortie.Triangles.Add(A);
			Sortie.Triangles.Add(bEchanger ? C : B);
			Sortie.Triangles.Add(bEchanger ? B : C);
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
				Locaux[S] = SommetSurArete(X, Y, Z,
					(CodeArete >> 4) & 0x0F, CodeArete & 0x0F, Densites);
			}

			const int32 NbTriangles = Donnees.GetTriangleCount();
			for (int32 T = 0; T < NbTriangles; ++T)
			{
				Triangle(Locaux[Donnees.vertexIndex[T * 3 + 0]],
					Locaux[Donnees.vertexIndex[T * 3 + 1]],
					Locaux[Donnees.vertexIndex[T * 3 + 2]], false);
			}
			return true;
		}

		// ------------------------------------------------ cellules de transition

		/** Coordonnees de grille GROSSIERE d'un point de la face, en coins. */
		FIntVector CoinDeFace(const FFace& Fa, int32 IU, int32 IV) const
		{
			FIntVector C(0, 0, 0);
			C[Fa.AxeU] = IU;
			C[Fa.AxeV] = IV;
			C[Fa.AxeN] = (Fa.SigneN > 0) ? N : 0;
			return C;
		}

		/** Position d'un echantillon FIN sur le plan de frontiere, en metres. */
		FVector PositionFine(const FFace& Fa, int32 UF, int32 VF) const
		{
			const double Demi = Voxel * 0.5;
			FVector P = MinM;
			P[Fa.AxeU] += UF * Demi;
			P[Fa.AxeV] += VF * Demi;
			P[Fa.AxeN] += (Fa.SigneN > 0) ? (N * Voxel) : 0.0;
			return P;
		}

		/**
		 * Le sommet porte par une arete de la face PLEINE RESOLUTION.
		 *
		 * Ces sommets vivent sur le plan de frontiere, a la maille FINE : ce sont
		 * eux qui doivent coincider avec les sommets de bord du chunk voisin,
		 * plus fin. Ils ne sont JAMAIS retractes -- section 4.4 : la retraction
		 * s'applique aux cellules regulieres et aux faces demi-resolution, jamais
		 * aux faces pleine resolution, qui restent sur le plan.
		 */
		int32 SommetSurAreteFine(int32 IdFace, const FFace& Fa,
			int32 UF0, int32 VF0, int32 UF1, int32 VF1, float D0, float D1)
		{
			if (UF1 < UF0 || VF1 < VF0)
			{
				Swap(UF0, UF1); Swap(VF0, VF1); Swap(D0, D1);
			}

			const int32 Axe = (UF1 != UF0) ? 0 : 1;
			const int64 Cle = (static_cast<int64>(IdFace) << 48)
				| (static_cast<int64>(UF0) << 32)
				| (static_cast<int64>(VF0) << 16)
				| static_cast<int64>(Axe);

			if (const int32* Deja = SommetParAreteFine.Find(Cle))
			{
				return *Deja;
			}

			const double T = Parametre(D0, D1);
			const FVector A = PositionFine(Fa, UF0, VF0);
			const FVector B = PositionFine(Fa, UF1, VF1);
			const FVector P = A + (B - A) * T;

			const int32 Indice = Sortie.Positions.Num();
			Sortie.Positions.Add(P * WorldseedMetersToCm);
			SommetParAreteFine.Add(Cle, Indice);
			return Indice;
		}

		/**
		 * Triangule une cellule de transition de la face IdFace, en (U, V).
		 *
		 * Treize echantillons : neuf sur la face pleine resolution, quatre sur la
		 * demi. Les quatre derniers ne sont pas evalues -- ils VALENT les coins de
		 * la face pleine (section 4.5), ce qui ramene le cas a neuf bits et, au
		 * passage, soude la face demi-resolution aux cellules regulieres.
		 */
		bool CelluleDeTransition(int32 IdFace, int32 U, int32 V)
		{
			const FFace& Fa = Faces[IdFace];

			// --- les neuf echantillons de la face pleine resolution -----------
			float D[13];
			for (int32 K = 0; K < 9; ++K)
			{
				const int32 A = K % 3;
				const int32 B = K / 3;
				const int32 UF = 2 * U + A;
				const int32 VF = 2 * V + B;

				if ((A & 1) == 0 && (B & 1) == 0)
				{
					// Sur la grille GROSSIERE : on passe par le cache, ce qui
					// garantit la valeur IDENTIQUE a celle que lisent les
					// cellules regulieres. Sans cela, deux evaluations du meme
					// point pourraient differer d'un bit et rouvrir la fissure.
					const FIntVector C = CoinDeFace(Fa, U + A / 2, V + B / 2);
					D[K] = Coin(C.X, C.Y, C.Z);
				}
				else
				{
					D[K] = static_cast<float>(
						Champ.At(PositionFine(Fa, UF, VF), Grottes));
					++Evaluations;
				}
			}
			for (int32 K = 0; K < 4; ++K)
			{
				D[9 + K] = D[DemiVersPlein[K]];
			}

			// --- le code de cas, sur neuf bits --------------------------------
			int32 Code = 0;
			for (int32 Bit = 0; Bit < 9; ++Bit)
			{
				if (D[BitVersEchantillon[Bit]] < 0.0f) { Code |= (1 << Bit); }
			}
			if (Code == 0 || Code == 511) { return false; }

			const uint8 Classe = transitionCellClass[Code];
			const TransitionCellData& Donnees = transitionCellData[Classe & 0x7F];
			const unsigned short* const Aretes = transitionVertexData[Code];

			// LE BIT HAUT DE LA CLASSE INVERSE L'ENROULEMENT, et c'est dit en
			// toutes lettres section 4.5 : « we indicate this property by setting
			// the high bit [...] for any case that is inverted ». 211 des 512 cas
			// le portent. Un resumeur automatique m'avait affirme qu'il signalait
			// une cellule reguliere : c'etait faux, et verifiable.
			//
			// Il se compose avec la convention de BASE de la famille, qui n'est
			// pas celle des cellules regulieres -- mesure, voir
			// bInverserEnroulementTransition.
			const bool bTableInverse = (Classe & 0x80) != 0;
			const bool bInverse = (bTableInverse != bInverserEnroulementTransition);

			int32 Locaux[12];
			const int32 NbSommets = Donnees.GetVertexCount();
			for (int32 S = 0; S < NbSommets; ++S)
			{
				const unsigned short CodeArete = Aretes[S];
				const int32 C0 = (CodeArete >> 4) & 0x0F;
				const int32 C1 = CodeArete & 0x0F;

				if (C0 <= 8 && C1 <= 8)
				{
					// Arete de la face PLEINE resolution : maille fine, sur le
					// plan de frontiere.
					Locaux[S] = SommetSurAreteFine(IdFace, Fa,
						2 * U + (C0 % 3), 2 * V + (C0 / 3),
						2 * U + (C1 % 3), 2 * V + (C1 / 3),
						D[C0], D[C1]);
				}
				else
				{
					// Arete de la face DEMI resolution : c'est une arete de la
					// grille GROSSIERE sur ce meme plan, donc exactement celle
					// qu'une cellule reguliere voisine voit. Le meme appel, donc
					// le meme sommet, donc aucune fissure.
					const int32 P0 = DemiVersPlein[C0 - 9];
					const int32 P1 = DemiVersPlein[C1 - 9];
					const FIntVector G0 = CoinDeFace(Fa, U + (P0 % 3) / 2, V + (P0 / 3) / 2);
					const FIntVector G1 = CoinDeFace(Fa, U + (P1 % 3) / 2, V + (P1 / 3) / 2);
					Locaux[S] = SommetSurAreteGrossiere(
						G0.X, G0.Y, G0.Z, G1.X, G1.Y, G1.Z, D[C0], D[C1]);
				}
			}

			const int32 NbTriangles = Donnees.GetTriangleCount();
			for (int32 T = 0; T < NbTriangles; ++T)
			{
				Triangle(Locaux[Donnees.vertexIndex[T * 3 + 0]],
					Locaux[Donnees.vertexIndex[T * 3 + 1]],
					Locaux[Donnees.vertexIndex[T * 3 + 2]], bInverse);
			}
			return true;
		}
	};
}   // namespace anonyme

bool Mailler(const FWorldseedDensity& Density,
	const FWorldseedCaveLocal* Caves, const FBox& BoundsM,
	float VoxelSizeM, uint8 MasqueTransition, float LargeurTransition,
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

	const double Debut = FPlatformTime::Seconds();

	const FVector Taille = BoundsM.GetSize();
	const int32 N = FMath::Max(FMath::RoundToInt(Taille.X / VoxelSizeM), 1);

	FMailleur M(Density, Caves, Out);
	M.MinM = BoundsM.Min;
	M.Voxel = VoxelSizeM;
	M.N = N;
	M.NP = N + 1;
	M.Masque = MasqueTransition;

	// L'EPAISSEUR DE LA DALLE EST UNE FRACTION DE LA CELLULE, pas une longueur
	// absolue : elle doit suivre le niveau de detail. Lengyel note qu'une
	// epaisseur NULLE raccorde geometriquement sans fissure mais « leads to
	// severe shading problems » -- les triangles lateraux deviennent degeneres et
	// leurs normales n'ont plus de sens. On garde donc une vraie epaisseur.
	M.Largeur = FMath::Clamp(static_cast<double>(LargeurTransition), 0.0, 0.9)
		* VoxelSizeM;

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

	// --- les cellules de transition -----------------------------------------
	//
	// ON NE PROPAGE PAS ICI, ON BALAYE. La couche de transition d'une face est
	// une grille N x N a DEUX dimensions -- « transition mesh generation is a
	// two-dimensional process since the layer of transition cells on a block face
	// is only one cell thick » (section 4.5). Un balayage de mille cellules dont
	// la plupart sortent au premier test coute moins qu'un appareil de
	// propagation, et il ne peut rien manquer.
	const int32 PremierTriangleTransition = Out.Triangles.Num();
	int32 CellulesTransition = 0;
	for (int32 F = 0; F < 6; ++F)
	{
		if ((MasqueTransition & (1 << F)) == 0) { continue; }

		for (int32 V = 0; V < N; ++V)
		{
			for (int32 U = 0; U < N; ++U)
			{
				if (M.CelluleDeTransition(F, U, V)) { ++CellulesTransition; }
			}
		}
	}
	OutStats.TransitionCells = CellulesTransition;

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

	// --- L'ENROULEMENT : CE QUE CE CHIFFRE PEUT DIRE, ET CE QU'IL NE PEUT PAS -
	//
	// IL EST AUTO-REFERENTIEL, ET IL A DEJA FAIT POSER LA MAUVAISE VALEUR. On
	// compare ici la normale GEOMETRIQUE d'un triangle -- celle que son ordre de
	// sommets impose -- aux normales de SES PROPRES sommets. Or celles-ci
	// viennent d'etre recalees sur le gradient quelques lignes plus haut : le
	// chiffre ne peut donc que confirmer « mes triangles s'accordent avec mes
	// normales ». Il ne dit RIEN de la convention de face avant du rastériseur
	// d'Unreal, et le lire comme s'il la donnait a rendu, le 19 septembre 2026,
	// un terrain entierement invisible -- toutes faces arriere, donc eliminees.
	//
	// CE QU'IL SERT ENCORE A VOIR, et c'est pour cela qu'il reste : la COHERENCE
	// entre les deux familles. Melangees, elles donnaient 94,7 % ; separees,
	// regulieres 99,6 % et transition 0,0 % -- une convention de depart
	// entierement fausse sur la petite des deux populations, qu'un agregat
	// masquait. « Un agregat sur des choses de natures differentes ne se corrige
	// pas, il se decompose. » Les deux familles doivent rendre le MEME chiffre ;
	// sa valeur absolue, elle, ne prouve rien.
	//
	// L'ARBITRE DE LA CONVENTION D'AFFICHAGE EST AILLEURS : `ProbeVoisins`
	// confronte les deux mailleurs au GRADIENT DU CHAMP, qui n'appartient a
	// aucun des deux, et verifie qu'il classe bien celui dont on sait deja qu'il
	// s'affiche correctement. Voir `bInverserEnroulement`.
	{
		int32 Endroit[2] = { 0, 0 };
		int32 Total[2] = { 0, 0 };

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

			const int32 Famille = (T >= PremierTriangleTransition) ? 1 : 0;
			if (FVector::DotProduct(Face, Sortante) > 0.0) { ++Endroit[Famille]; }
			++Total[Famille];
		}

		OutStats.FacesEndroit = (Total[0] > 0)
			? static_cast<float>(Endroit[0]) / static_cast<float>(Total[0]) : 0.0f;
		OutStats.FacesEndroitTransition = (Total[1] > 0)
			? static_cast<float>(Endroit[1]) / static_cast<float>(Total[1]) : 0.0f;
	}

	OutStats.NormalMs = (FPlatformTime::Seconds() - DebutNormales) * 1000.0;
	OutStats.Vertices = NbSommets;
	OutStats.Triangles = Out.Triangles.Num() / 3;
	OutStats.FieldMs = OutStats.MeshMs;
	return true;
}

}   // namespace WorldseedTransvoxel
