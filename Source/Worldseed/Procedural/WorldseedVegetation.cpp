// Worldseed - semer la vegetation sur un chunk de terrain voxel.

#include "Procedural/WorldseedVegetation.h"

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedParois.h"   // le hachage de tirage, partage
#include "Procedural/WorldseedPerlin.h"
#include "Procedural/WorldseedRules.h"    // FWorldseedGeometry
#include "Procedural/WorldseedVoxelChunk.h"

namespace
{
	/**
	 * Une grille de hauteurs construite une fois par chunk.
	 *
	 * POURQUOI ELLE EXISTE : SANS ELLE LE SEMIS EST QUADRATIQUE. Un chunk porte
	 * jusqu'a quelques dizaines de milliers de sommets, et une couche de tapis y
	 * pose environ 455 points. Chercher pour chaque point le sommet le plus
	 * proche coute leur produit -- des millions d'operations par chunk, sur un
	 * budget de trame de 16,67 ms.
	 *
	 * On range donc les sommets UNE FOIS dans une grille XY, en gardant le plus
	 * HAUT de chaque case et sa normale. Chaque point de semis lit ensuite sa
	 * case en temps constant.
	 *
	 * LE PLUS HAUT, ET C'EST UNE APPROXIMATION ASSUMEE : un chunk peut porter
	 * deux surfaces superposees -- le sol d'une grotte sous le sol exterieur.
	 * On seme alors sur celle du dessus, ce qui est le comportement voulu pour
	 * de la vegetation. Une plante dans une grotte demanderait un autre semis,
	 * et ce n'est pas ce chantier.
	 */
	struct FGrilleSol
	{
		int32 Cotes = 0;
		double PasCm = 0.0;
		FVector2D OrigineCm = FVector2D::ZeroVector;
		TArray<float> Z;
		TArray<FVector3f> N;

		/**
		 * ON RASTERISE LES TRIANGLES, ON NE RANGE PAS LES SOMMETS.
		 *
		 * LA PREMIERE VERSION GARDAIT LE Z MAXIMUM DES SOMMETS DE CHAQUE CASE,
		 * ET ELLE FAISAIT FLOTTER LA VEGETATION. Le defaut se voyait a l'oeil
		 * et le HUD le chiffrait : « sol +1 m » sur une pente de 26 degres. La
		 * cause est arithmetique -- une case de deux metres sur une pente de
		 * 26 degres porte un denivele de 0,98 m, et prendre le maximum pose
		 * donc chaque plante au point le PLUS HAUT de sa case, jamais a
		 * l'endroit ou elle est. C'etait bien « une autre carte d'altitude » :
		 * la mienne, quantifiee vers le haut.
		 *
		 * Rasteriser donne l'altitude EXACTE du plan du triangle au centre de
		 * chaque case, et la lecture bilineaire supprime ce qu'il reste de
		 * marche. Le cout est du meme ordre -- on parcourt les triangles au
		 * lieu des sommets, soit deux fois moins d'elements.
		 *
		 * ON GARDE LE MAXIMUM ENTRE TRIANGLES, et c'est un choix : un chunk
		 * peut porter deux surfaces superposees -- le sol d'une grotte sous le
		 * sol exterieur. On seme sur celle du dessus.
		 */
		void Batir(const FWorldseedVoxelMesh& Mesh,
			const FVector& OrigineChunkCm, double CoteChunkCm)
		{
			// UNE CASE PAR VOXEL : plus fin ne dit rien de plus, le maillage ne
			// porte pas de detail sous le metre.
			Cotes = FMath::Clamp(FMath::RoundToInt(CoteChunkCm / 100.0), 8, 128);
			PasCm = CoteChunkCm / static_cast<double>(Cotes);
			OrigineCm = FVector2D(OrigineChunkCm.X, OrigineChunkCm.Y);

			Z.Init(-TNumericLimits<float>::Max(), Cotes * Cotes);
			N.Init(FVector3f(0.0f, 0.0f, 1.0f), Cotes * Cotes);

			for (int32 T = 0; T + 2 < Mesh.Triangles.Num(); T += 3)
			{
				const int32 IA = Mesh.Triangles[T];
				const int32 IB = Mesh.Triangles[T + 1];
				const int32 IC = Mesh.Triangles[T + 2];
				if (!Mesh.Positions.IsValidIndex(IA) || !Mesh.Positions.IsValidIndex(IB)
					|| !Mesh.Positions.IsValidIndex(IC))
				{
					continue;
				}

				const FVector& A = Mesh.Positions[IA];
				const FVector& B = Mesh.Positions[IB];
				const FVector& C = Mesh.Positions[IC];

				// L'AIRE PROJETEE EN XY, qui est aussi le determinant du
				// systeme barycentrique. Un triangle VERTICAL la rend nulle :
				// il ne couvre aucune case, et c'est exactement ce qu'on veut
				// -- une paroi n'a pas d'altitude de sol.
				const double Det = (B.X - A.X) * (C.Y - A.Y) - (C.X - A.X) * (B.Y - A.Y);
				if (FMath::Abs(Det) < 1.0)
				{
					continue;
				}

				const double MinX = FMath::Min3(A.X, B.X, C.X) - OrigineCm.X;
				const double MaxX = FMath::Max3(A.X, B.X, C.X) - OrigineCm.X;
				const double MinY = FMath::Min3(A.Y, B.Y, C.Y) - OrigineCm.Y;
				const double MaxY = FMath::Max3(A.Y, B.Y, C.Y) - OrigineCm.Y;

				const int32 X0 = FMath::Clamp(FMath::FloorToInt32(MinX / PasCm), 0, Cotes - 1);
				const int32 X1 = FMath::Clamp(FMath::CeilToInt32(MaxX / PasCm), 0, Cotes - 1);
				const int32 Y0 = FMath::Clamp(FMath::FloorToInt32(MinY / PasCm), 0, Cotes - 1);
				const int32 Y1 = FMath::Clamp(FMath::CeilToInt32(MaxY / PasCm), 0, Cotes - 1);

				for (int32 CY = Y0; CY <= Y1; ++CY)
				{
					for (int32 CX = X0; CX <= X1; ++CX)
					{
						// Le CENTRE de la case, en coordonnees du chunk.
						const double PX = OrigineCm.X + (CX + 0.5) * PasCm;
						const double PY = OrigineCm.Y + (CY + 0.5) * PasCm;

						const double U = ((PX - A.X) * (C.Y - A.Y)
							- (C.X - A.X) * (PY - A.Y)) / Det;
						const double V = ((B.X - A.X) * (PY - A.Y)
							- (PX - A.X) * (B.Y - A.Y)) / Det;
						if (U < 0.0 || V < 0.0 || U + V > 1.0)
						{
							continue;
						}

						const double ZPlan = A.Z + U * (B.Z - A.Z) + V * (C.Z - A.Z);
						const int32 K = CY * Cotes + CX;
						if (static_cast<float>(ZPlan) > Z[K])
						{
							Z[K] = static_cast<float>(ZPlan);
							if (Mesh.Normals.IsValidIndex(IA))
							{
								// Les normales de sommet sont deja lissees ;
								// celle du triangle serait facettee.
								N[K] = FVector3f(Mesh.Normals[IA]);
							}
						}
					}
				}
			}
		}

		/**
		 * Altitude et normale en un point, INTERPOLEES entre les quatre cases
		 * voisines. Sans cette interpolation, la vegetation se poserait en
		 * escalier d'un metre, ce qui est le defaut qu'on vient de corriger a
		 * une echelle plus fine.
		 */
		bool Lire(double XCm, double YCm, float& OutZ, FVector3f& OutN) const
		{
			const double FX = (XCm - OrigineCm.X) / PasCm - 0.5;
			const double FY = (YCm - OrigineCm.Y) / PasCm - 0.5;
			const int32 X0 = FMath::FloorToInt32(FX);
			const int32 Y0 = FMath::FloorToInt32(FY);
			const double TX = FX - X0;
			const double TY = FY - Y0;

			double Somme = 0.0;
			double Poids = 0.0;
			FVector3f Normale(0.0f, 0.0f, 0.0f);

			for (int32 DY = 0; DY <= 1; ++DY)
			{
				for (int32 DX = 0; DX <= 1; ++DX)
				{
					const int32 CX = X0 + DX;
					const int32 CY = Y0 + DY;
					if (CX < 0 || CX >= Cotes || CY < 0 || CY >= Cotes)
					{
						continue;
					}
					const int32 K = CY * Cotes + CX;
					if (Z[K] <= -TNumericLimits<float>::Max() * 0.5f)
					{
						continue;
					}
					// UNE CASE VIDE NE COMPTE PAS, elle ne tire pas la moyenne
					// vers le bas : on repondere avec ce qu'on a. Au bord d'un
					// chunk, trois cases sur quatre peuvent manquer.
					const double P = (DX ? TX : 1.0 - TX) * (DY ? TY : 1.0 - TY);
					if (P <= 0.0)
					{
						continue;
					}
					Somme += Z[K] * P;
					Poids += P;
					Normale += N[K] * static_cast<float>(P);
				}
			}

			if (Poids <= 0.0)
			{
				return false;
			}
			OutZ = static_cast<float>(Somme / Poids);
			OutN = Normale.GetSafeNormal();
			if (OutN.IsNearlyZero())
			{
				OutN = FVector3f(0.0f, 0.0f, 1.0f);
			}
			return true;
		}
	};
}

void WorldseedVegetation::Semer(const FWorldseedVoxelMesh& Mesh,
	const FVector& OrigineChunkCm, double CoteChunkCm,
	const FWorldseedRecettes& Recettes,
	const FWorldseedBiomeMap& Biomes, const FWorldseedGeometry& Geo,
	const FWorldseedVegetationRegles& Regles, int32 Graine,
	const FVector2D& OrigineM,
	TArray<FWorldseedPlante>& Out, FWorldseedVegetationReleve& Releve)
{
	if (Recettes.EstVide() || Regles.Densite <= 0.0f || Mesh.Positions.Num() == 0)
	{
		return;
	}

	// --- LE RAYON DE SEMIS, TESTE AVANT TOUT LE RESTE -----------------------
	//
	// Il borne le travail a la distance ou une plante est encore visible, bien
	// en deca du rayon de vue. On le teste sur le CENTRE du chunk : un chunk
	// fait 32 m, le rayon des centaines, donc trancher au chunk suffit et evite
	// de batir la grille pour rien.
	const FVector2D CentreM(
		(OrigineChunkCm.X + CoteChunkCm * 0.5) / 100.0,
		(OrigineChunkCm.Y + CoteChunkCm * 0.5) / 100.0);
	if (FVector2D::Distance(CentreM, OrigineM) > Regles.RayonSemisM)
	{
		++Releve.HorsRayon;
		return;
	}

	FGrilleSol Sol;
	Sol.Batir(Mesh, OrigineChunkCm, CoteChunkCm);

	// UNE FOIS PAR CHUNK, PAS PAR PLANTE : la conversion ne depend que des
	// regles, et cette boucle tourne sur des centaines de cases.
	const double NiveauMerCm = Regles.AltitudeMinM * WorldseedMetersToCm;

	// TOUT LE CHUNK PEUT ETRE SOUS L'EAU, et c'est le cas le plus frequent sur
	// un monde couvert a 70,8 % par la mer. Le rejeter d'un coup evite de
	// batir une grille de cases pour les refuser une par une -- la borne haute
	// du chunk suffit, aucune plante ne peut etre plus haute que lui.
	if (OrigineChunkCm.Z + CoteChunkCm < NiveauMerCm)
	{
		++Releve.ChunksSousLaMer;
		return;
	}

	// LE BIOME SE LIT AU CENTRE DU CHUNK, PAS PAR POINT. Un chunk fait 32 m et
	// la carte des biomes a une maille de 15,6 m : lire le biome pour chacune
	// des centaines de plantes coute cher pour ne changer, au plus, qu'au bord
	// d'un chunk. C'est le meme compromis que la grille climatique de
	// `BP_WorldseedClimat`, qui porte la dominante sur 200 m.
	const bool bBiomesPrets = (Biomes.Index.Num() == Geo.CellCount());
	if (!bBiomesPrets)
	{
		++Releve.SansRecette;
		return;
	}
	const int32 Cellule = Geo.CelluleDepuisMetres(CentreM.X, CentreM.Y);
	if (!Biomes.Index.IsValidIndex(Cellule))
	{
		++Releve.SansRecette;
		return;
	}
	const int32 IdBiome = static_cast<int32>(Biomes.Index[Cellule]);

	const FWorldseedBiomeRecette* const Recette = Recettes.ParBiome.Find(IdBiome);
	if (!Recette)
	{
		// Biome sans vegetation : l'ocean, la roche a nu selon les recettes.
		// Ce n'est pas un defaut, et le releve le distingue d'un echec.
		++Releve.SansRecette;
		return;
	}

	// --- DEUX RECETTES SE PARTAGENT LE CHUNK, PAR SUBSTRAT ------------------
	//
	// L'ESTRAN N'EST PAS UN BIOME, et c'est pourquoi il ne peut pas etre choisi
	// comme l'est la recette du climat. On balaye donc les couches des DEUX, et
	// chaque POINT decide de celle a laquelle il a droit.
	//
	// LE SUBSTRAT SE LIT AU POINT, PAS AU CENTRE DU CHUNK -- contrairement au
	// biome juste au-dessus, et c'est delibere : une bande littorale fait de
	// l'ordre de `BeachWidthM`, soit 37 m, quand un chunk en fait 32. Trancher
	// au centre donnerait des chunks entiers tout plage ou tout foret, donc un
	// trait de cote en MARCHES D'ESCALIER de trente-deux metres. Le biome, lui,
	// varie a l'echelle du climat et supporte tres bien la maille du chunk.
	//
	// La lecture reste un acces tableau, pas un calcul : c'est le meme cout que
	// le test de pente ou celui des taches, qui sont deja par point.
	const bool bCouverturePrete = (Biomes.Cover.Num() == Geo.CellCount());

	TArray<const FWorldseedCoucheRecette*, TInlineAllocator<16>> Couches;
	TArray<bool, TInlineAllocator<16>> SurEstran;
	for (const FWorldseedCoucheRecette& C : Recette->Couches)
	{
		Couches.Add(&C);
		SurEstran.Add(false);
	}
	// SANS CARTE DE COUVERTURE, PAS D'ESTRAN : on garde alors exactement le
	// comportement d'avant plutot que de semer des rochers partout.
	if (bCouverturePrete)
	{
		for (const FWorldseedCoucheRecette& C : Recettes.Estran.Couches)
		{
			Couches.Add(&C);
			SurEstran.Add(true);
		}
	}

	// --- LA ROCHE PASSE D'ABORD, ET TOUT L'ORDRE EN DECOULE ----------------
	//
	// Une emprise ne peut ecarter que ce qui vient APRES elle. Les couches
	// obstacle sont donc semees en premier, et les autres se rangent ensuite
	// dans ce qu'elles laissent.
	//
	// LE CANAL DE HACHAGE RESTE CELUI DE L'ORDRE D'ORIGINE, et c'est ce qui
	// rend ce reordonnancement gratuit : il porte l'indice de couche pour que
	// deux couches de meme pas ne tombent pas aux memes points, et le calculer
	// sur l'ordre de PASSAGE aurait redistribue tout le decor du monde --
	// chaque plante ailleurs -- pour un changement qui ne porte que sur la
	// roche. On trie les couches, jamais leur identite.
	TArray<int32, TInlineAllocator<16>> Ordre;
	Ordre.Reserve(Couches.Num());
	for (int32 I = 0; I < Couches.Num(); ++I)
	{
		if (Couches[I]->bObstacle) { Ordre.Add(I); }
	}
	const int32 NbObstacles = Ordre.Num();
	for (int32 I = 0; I < Couches.Num(); ++I)
	{
		if (!Couches[I]->bObstacle) { Ordre.Add(I); }
	}

	/**
	 * L'emprise d'une roche posee : ou elle est, et jusqu'ou elle occupe.
	 *
	 * UN DISQUE ET NON LA BOITE DU MAILLAGE. L'instance recoit un lacet
	 * ALEATOIRE, donc aucun des deux demi-cotes n'est privilegie : la moyenne
	 * des deux est le disque equivalent d'une boite tournee au hasard. Prendre
	 * le plus grand laisserait un anneau nu autour d'un rocher allonge ;
	 * prendre le plus petit laisserait de l'herbe traverser sa longueur.
	 */
	struct FEmprise
	{
		FVector2D Centre = FVector2D::ZeroVector;
		float RayonCm = 0.0f;
	};
	TArray<FEmprise, TInlineAllocator<64>> Emprises;

	// SANS GABARITS, PAS D'EMPRISE. `RayonEspeceCm` est rempli par qui charge
	// les maillages ; un clone frais sans `Content/` seme comme avant plutot
	// que de refuser de semer.
	const bool bEmprisesPretes = (Recettes.RayonEspeceCm.Num() == Recettes.Catalogue.Num());

	for (int32 Rang = 0; Rang < Ordre.Num(); ++Rang)
	{
		const int32 IdxCouche = Ordre[Rang];
		const bool bCoucheObstacle = (Rang < NbObstacles);
		const FWorldseedCoucheRecette& Couche = *Couches[IdxCouche];
		const bool bCoucheEstran = SurEstran[IdxCouche];
		if (Couche.Especes.Num() == 0 || Couche.PoidsTotal <= 0.0f)
		{
			continue;
		}

		const double PasCm = FMath::Max(50.0f,
			Couche.PasCm * Regles.PasMultiplicateur);

		// LA GRILLE EST CELLE DU MONDE, comme pour les parois : meme maille,
		// meme hachage, meme tirage quel que soit le decoupage en chunks et
		// quel que soit le niveau d'anneau. Un semis cale sur les triangles
		// ferait SAUTER la vegetation a chaque remaillage.
		const int32 MX0 = FMath::FloorToInt32(OrigineChunkCm.X / PasCm);
		const int32 MY0 = FMath::FloorToInt32(OrigineChunkCm.Y / PasCm);
		const int32 MX1 = FMath::CeilToInt32((OrigineChunkCm.X + CoteChunkCm) / PasCm);
		const int32 MY1 = FMath::CeilToInt32((OrigineChunkCm.Y + CoteChunkCm) / PasCm);

		// Le canal du hachage porte l'indice de couche : sans lui, deux couches
		// de meme pas tomberaient exactement aux memes points et l'on verrait
		// les especes appariees deux a deux.
		const int32 Canal = 100 + IdxCouche * 8;

		for (int32 MX = MX0; MX <= MX1; ++MX)
		{
			for (int32 MY = MY0; MY <= MY1; ++MY)
			{
				if (Out.Num() >= Regles.PlafondParChunk)
				{
					++Releve.Plafonnees;
					return;
				}

				++Releve.Testes;

				const double CX = (static_cast<double>(MX)
					+ WorldseedParois::Tirage(MX, MY, Canal, Graine)) * PasCm;
				const double CY = (static_cast<double>(MY)
					+ WorldseedParois::Tirage(MX, MY, Canal + 1, Graine)) * PasCm;

				// Une maille n'appartient qu'a UN chunk : celui qui contient
				// son point. C'est ce test, et lui seul, qui interdit de semer
				// deux fois la meme plante quand deux chunks se touchent.
				if (CX < OrigineChunkCm.X || CX >= OrigineChunkCm.X + CoteChunkCm
					|| CY < OrigineChunkCm.Y || CY >= OrigineChunkCm.Y + CoteChunkCm)
				{
					++Releve.HorsChunk;
					continue;
				}

				if (Regles.Densite < 1.0f
					&& WorldseedParois::Tirage(MX, MY, Canal + 2, Graine) > Regles.Densite)
				{
					++Releve.Densite;
					continue;
				}

				float ZCm = 0.0f;
				FVector3f Normale(0.0f, 0.0f, 1.0f);
				if (!Sol.Lire(CX, CY, ZCm, Normale))
				{
					++Releve.CaseVide;
					continue;
				}

				// Le chunk est une TRANCHE : sans ce test, un chunk aerien
				// sement sur la surface d'un chunk situe bien plus bas, et l'on
				// verrait la meme herbe posee plusieurs fois.
				if (ZCm < OrigineChunkCm.Z || ZCm >= OrigineChunkCm.Z + CoteChunkCm)
				{
					++Releve.TrancheZ;
					continue;
				}

				// --- RIEN NE POUSSE SOUS LA MER ----------------------------
				//
				// LE SOL EST LU, DONC LE TEST TOMBE ICI ET PAS AVANT : c'est
				// `Sol.Lire` qui donne l'altitude, et la lire coute bien plus
				// que cette comparaison. Le placer plus haut demanderait de
				// deviner la hauteur du sol depuis la seule position de la
				// case, ce qui est faux des qu'il y a du relief.
				//
				// L'ALTITUDE EST CELLE DU MONDE, sans conversion cachee : les
				// sommets du maillage sont en centimetres dans le repere de
				// l'acteur terrain, et cet acteur est a l'origine -- c'est deja
				// ce dont la peinture se sert pour lire la carte des biomes.
				if (ZCm < NiveauMerCm)
				{
					++Releve.SousLaMer;
					continue;
				}

				// --- CHAQUE POINT VA A LA RECETTE DE SON SUBSTRAT ----------
				//
				// POSE APRES LA LECTURE DU SOL, comme le test du niveau de la
				// mer, et pour la meme raison : les points rejetes plus haut
				// n'ont pas a payer cette lecture.
				//
				// RIEN NE POUSSE SUR LE SABLE, et c'est ce qui manquait : tant
				// que seul le biome decidait, une plage recevait le tapis
				// d'herbe de la foret qui la borde -- de l'herbe sur le sable,
				// signale en jeu. L'estran porte desormais SA recette, faite de
				// quelques rochers et de rien d'autre.
				if (bCouverturePrete)
				{
					const int32 CelluleDuPoint = Geo.CelluleDepuisMetres(
						CX / WorldseedMetersToCm, CY / WorldseedMetersToCm);
					const bool bEstran = Biomes.Cover.IsValidIndex(CelluleDuPoint)
						&& (static_cast<EWorldseedCover>(Biomes.Cover[CelluleDuPoint])
							== EWorldseedCover::Beach);
					if (bEstran != bCoucheEstran)
					{
						++Releve.Substrat;
						continue;
					}
				}
				else if (bCoucheEstran)
				{
					++Releve.Substrat;
					continue;
				}

				// --- LA PENTE, LUE SANS ARC COSINUS ------------------------
				//
				// Elle vient de la composante verticale de la normale, comme
				// partout ailleurs dans ce projet. Le seuil se lit CONTRE LA
				// DISTRIBUTION DU MONDE : la pente mediane des terres vaut
				// 30,6 degres, et un plafond pose « a l'intuition terrestre »
				// a 25 degres ne gardait que 36,7 % des terres -- il
				// deshabillait les deux tiers du relief. Les recettes portent
				// donc des plafonds calibres (38 a 42 degres).
				const float CosPente = FMath::Abs(Normale.Z);
				const float CosMin = FMath::Cos(FMath::DegreesToRadians(
					FMath::Clamp(Couche.PenteMaxDeg, 0.0f, 90.0f)));
				const float CosMax = FMath::Cos(FMath::DegreesToRadians(
					FMath::Clamp(Couche.PenteMinDeg, 0.0f, 90.0f)));
				if (CosPente < CosMin || CosPente > CosMax)
				{
					++Releve.Pente;
					continue;
				}

				// --- LES TACHES -------------------------------------------
				//
				// Un bruit COHERENT EN ESPACE, seuille : ce qui donne des
				// touffes plutot qu'un semis uniforme. C'est ce que le graphe
				// PCG faisait avec `PCGSpatialNoise`, et c'est ce qui evite
				// que les fleurs poussent des qu'il y a de l'herbe.
				if (Couche.TacheTailleCm > 1.0f && Couche.TacheSeuil > 0.0f)
				{
					// UNE SOMME FRACTALE, ET A UNE OCTAVE C'EST LE PERLIN
					// D'AVANT. Une seule octave n'a qu'une TAILLE : les taches
					// font toutes le meme diametre et reviennent a la meme
					// periode, ce qui se lit comme regulier des qu'on prend du
					// recul. Les octaves suivantes decoupent le contour,
					// detachent des ilots et ouvrent des trouees.
					const float F = 1.0f / Couche.TacheTailleCm;
					const float Bruit = WorldseedPerlin::FbmPoint(
						static_cast<float>(CX), static_cast<float>(CY), F,
						Couche.TacheOctaves, Graine + IdxCouche);
					const float V = Bruit * 0.5f + 0.5f;

					if (Couche.TacheDouceur <= 0.0f)
					{
						// LE SEUIL FRANC RESTE LE DEFAUT, a l'identique : une
						// recette qui ne demande pas de douceur ne doit pas
						// voir son monde bouger.
						if (V < Couche.TacheSeuil)
						{
							++Releve.Taches;
							continue;
						}
					}
					else
					{
						// --- LA TACHE DEVIENT UNE DENSITE ------------------
						//
						// Un seuil franc donne un bord NET -- dedans tout
						// pousse, dehors rien -- et cette frontiere se lit
						// comme un decoupage. Ici la probabilite de garder
						// monte en douceur a la traversee du seuil : dense au
						// coeur de la tache, clairseme sur ses marges.
						//
						// LA TRANSITION EST CENTREE SUR LE SEUIL, donc ce
						// qu'elle retire d'un cote elle le rend de l'autre :
						// elle change le CONTRASTE, pas la couverture.
						const float Bas = Couche.TacheSeuil - Couche.TacheDouceur * 0.5f;
						const float T = FMath::Clamp(
							(V - Bas) / Couche.TacheDouceur, 0.0f, 1.0f);
						const float P = T * T * (3.0f - 2.0f * T);

						// LE CANAL 6 EST LIBRE DANS LA FOULEE DE HUIT : 0 et 1
						// portent le decalage dans la maille, 2 la densite,
						// 3 l'espece, 4 l'echelle, 5 le lacet. En reprendre un
						// APPARIERAIT deux tirages -- les plantes gardees
						// seraient exactement celles d'une autre decision.
						if (WorldseedParois::Tirage(MX, MY, Canal + 6, Graine) > P)
						{
							++Releve.Taches;
							continue;
						}
					}
				}

				// --- L'ESPECE, TIREE SELON LES POIDS -----------------------
				const float Cible = WorldseedParois::Tirage(MX, MY, Canal + 3, Graine)
					* Couche.PoidsTotal;
				int32 Choisie = 0;
				float Cumul = 0.0f;
				for (int32 E = 0; E < Couche.Especes.Num(); ++E)
				{
					Cumul += Couche.Especes[E].Poids;
					if (Cible <= Cumul)
					{
						Choisie = E;
						break;
					}
				}

				const float Echelle = FMath::Lerp(Couche.EchelleMin, Couche.EchelleMax,
					WorldseedParois::Tirage(MX, MY, Canal + 4, Graine));

				// LE LACET EST LIBRE, LE RESTE NE L'EST PAS : une plante pousse
				// vers le HAUT, pas perpendiculairement au sol. Un arbre aligne
				// sur la normale d'un versant se lit immediatement comme un
				// arbre couche.
				const FRotator Rot(0.0f,
					WorldseedParois::Tirage(MX, MY, Canal + 5, Graine) * 360.0f, 0.0f);

				FWorldseedPlante Plante;
				Plante.Espece = Couche.Especes[Choisie].IndexCatalogue;
				if (Plante.Espece == INDEX_NONE)
				{
					continue;
				}

				// --- CE QUE LA ROCHE OCCUPE -------------------------------
				//
				// SIGNALE EN JEU : de l'herbe poussait A TRAVERS les rochers.
				// Une roche est de la matiere, pas une image posee sur le sol.
				//
				// LE TEST PORTE SUR LE POINT, PAS SUR LE FEUILLAGE. Une touffe
				// dont le centre est hors de la roche peut encore la deborder
				// de quelques centimetres, et c'est voulu : exclure sur le
				// rayon du feuillage creuserait un anneau nu bien plus visible
				// que le debord qu'il corrige.
				const FVector2D PointCm(CX, CY);
				if (Emprises.Num() > 0)
				{
					bool bOccupe = false;
					for (const FEmprise& E : Emprises)
					{
						if (FVector2D::DistSquared(PointCm, E.Centre)
							<= static_cast<double>(E.RayonCm) * E.RayonCm)
						{
							bOccupe = true;
							break;
						}
					}
					if (bOccupe)
					{
						++Releve.SousLaRoche;
						continue;
					}
				}

				Plante.Transform = FTransform(Rot,
					FVector(CX, CY, static_cast<double>(ZCm)),
					FVector(Echelle, Echelle, Echelle));
				Out.Add(Plante);
				++Releve.Posees;

				// LES ROCHES S'ECARTENT ENTRE ELLES AUSSI, et c'est le meme
				// test : deux blocs qui se traversent se lisent aussi mal que
				// de l'herbe dans un bloc. Comme les couches obstacle passent
				// en premier, chacune voit celles qui l'ont precedee.
				if (bCoucheObstacle && bEmprisesPretes)
				{
					const float R = Recettes.RayonEspeceCm[Plante.Espece] * Echelle;
					if (R > 1.0f)
					{
						Emprises.Add({ PointCm, R });
					}
				}
			}
		}
	}
}
