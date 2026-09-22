// Worldseed - le mailleur Transvoxel : l'enroulement, la cloture, la couture.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedDensity.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedTransvoxel.h"
#include "Procedural/WorldseedVoxelChunk.h"

#include "Misc/AutomationTest.h"

/**
 * POURQUOI CE FICHIER EXISTE, ET CE QUE LE DEPOT A PAYE SANS LUI.
 *
 * L'enroulement du mailleur a ete FAUX deux fois, et les deux fois il a fallu
 * un humain devant une image pour s'en apercevoir. La premiere, tout le terrain
 * etait rendu en faces arriere, donc eliminees : le joueur voyait au travers de
 * la surface proche jusqu'au DESSOUS de la surface lointaine -- des nappes
 * arquees au-dessus de la tete, des lambeaux dans le ciel. Signale comme « des
 * trous dans le terrain », ce n'etait pas un trou : c'etait du terrain complet,
 * a l'envers.
 *
 * ET LA MESURE QUI AVAIT POSE LA MAUVAISE VALEUR ETAIT AUTO-REFERENTIELLE.
 * `ProbeTransvoxel` comparait la normale geometrique d'un triangle aux normales
 * de SES PROPRES sommets -- lesquelles sont recalees sur le gradient quelques
 * lignes plus haut. Elle ne pouvait que confirmer « mes triangles s'accordent
 * avec mes normales », et ne disait RIEN de la convention de face avant.
 *
 * L'ARBITRE EST DONC UN TIERS : le GRADIENT du champ de densite, qui croit vers
 * l'air et n'appartient a aucun mailleur. Il ne doit rien a une convention
 * d'enroulement, ni a la main du repere, ni aux normales que l'un ou l'autre a
 * bien voulu ecrire.
 *
 * AUCUN MONDE N'EST GENERE ICI. Le champ se bat sur le relief de la fixture --
 * une fonction lisse et asymetrique -- et sur les reglages PAR DEFAUT du champ
 * de densite. Ces tests echouent donc quand le CODE casse, jamais quand une
 * valeur physique de `world_rules.json` bouge.
 */
namespace
{
	/** Un champ de densite bati sur le relief de la fixture, sans roche ni cavites. */
	struct FChampFictif
	{
		// L'ORDRE DE DECLARATION COMPTE : `Init` ne COPIE ni la geometrie ni le
		// relief, il les reference. Declares avant le champ, ils sont construits
		// avant lui et detruits apres : la duree de vie est acquise par la
		// structure plutot que par la discipline de l'appelant.
		FWorldseedGeometry Geo;
		TArray<float> Relief;
		FWorldseedDensityRules Regles;
		FWorldseedDensity Densite;

		explicit FChampFictif(float VoxelM)
		{
			Geo = WorldseedTest::Geometrie(16);
			Relief = WorldseedTest::Relief(Geo);
			Regles.VoxelSizeM = VoxelM;
			Densite.Init(Geo, Relief, 1.0f, 4242, Regles);
		}

		/** Une boite de `CoteM` posee en (XM, YM) et A CHEVAL sur la surface. */
		FBox BoiteSurLaSurface(double XM, double YM, double CoteM) const
		{
			// LE Z SE DEDUIT DU CHAMP, IL NE SE CHOISIT PAS. Une boite posee a
			// une altitude fixe pres d'un relief qu'on n'a pas sonde tombe dans
			// la roche ou dans le vide, et le mailleur rend alors « pas de
			// surface » -- ce qui se lit comme un defaut du mailleur. Ce depot
			// a paye ce piege deux fois en une heure, en jeu.
			const double S = Densite.SurfaceHeightM(XM + CoteM * 0.5, YM + CoteM * 0.5);
			const double Z = FMath::FloorToDouble(S / CoteM) * CoteM - CoteM * 0.5;
			return FBox(FVector(XM, YM, Z), FVector(XM + CoteM, YM + CoteM, Z + CoteM));
		}

		FVector Gradient(const FVector& PosM) const
		{
			const double H = 0.25 * Regles.VoxelSizeM;
			return FVector(
				Densite.At(FVector(PosM.X + H, PosM.Y, PosM.Z))
					- Densite.At(FVector(PosM.X - H, PosM.Y, PosM.Z)),
				Densite.At(FVector(PosM.X, PosM.Y + H, PosM.Z))
					- Densite.At(FVector(PosM.X, PosM.Y - H, PosM.Z)),
				Densite.At(FVector(PosM.X, PosM.Y, PosM.Z + H))
					- Densite.At(FVector(PosM.X, PosM.Y, PosM.Z - H)));
		}
	};

	/**
	 * Part des faces dont l'enroulement est celui qu'UNREAL affiche a l'endroit.
	 *
	 * LE SENS A ETE MESURE, PAS DEDUIT, ET J'AVAIS DEDUIT L'INVERSE. Le premier
	 * jet de ce test posait `dot(cross(B-A, C-A), gradient) > 0` -- le gradient
	 * croit vers l'air, donc vers le dehors, donc un triangle « a l'endroit »
	 * devrait s'y accorder. Le raisonnement est complet et il est FAUX : Unreal
	 * travaille en repere INDIRECT, si bien que l'enroulement visible donne un
	 * produit vectoriel dirige vers l'INTERIEUR du solide.
	 *
	 * LA MESURE, et elle est binaire :
	 *
	 *     maillage tel qu'il est rendu     0,05 % de faces avec dot > 0
	 *     le meme, indices retournes      99,95 %
	 *
	 * Elle concorde avec ce que `ProbeVoisins` avait releve sur le mailleur du
	 * MOTEUR -- 0,2 et 0,3 % -- lequel s'affiche correctement. C'est d'ailleurs
	 * ce qui avait denonce la sonde auto-referentielle : elle rendait 99,9 % sur
	 * un terrain invisible.
	 *
	 * **Un enroulement ne se deduit jamais, meme quand la chaine de
	 * raisonnement parait complete.** Le depot l'avait ecrit ; je viens de le
	 * repayer.
	 *
	 * LES FACES PRESQUE PERPENDICULAIRES AU GRADIENT SONT ECARTEES, et c'est
	 * necessaire des qu'une dalle de transition entre en jeu : ses triangles
	 * LATERAUX cousent les deux nappes, donc leur normale est a peu pres
	 * orthogonale au gradient et le signe du produit n'y porte AUCUNE
	 * information. Les compter ferait du bruit un verdict. `OutEcartees` dit
	 * combien, pour qu'on ne puisse pas cacher un defaut en en ecartant
	 * beaucoup.
	 */
	double PartAccordeeAUnreal(const FChampFictif& C, const FWorldseedVoxelMesh& M,
		int32& OutJugees, int32& OutEcartees)
	{
		int32 Accord = 0;
		OutJugees = 0;
		OutEcartees = 0;

		for (int32 T = 0; T + 2 < M.Triangles.Num(); T += 3)
		{
			const FVector A = M.Positions[M.Triangles[T]];
			const FVector B = M.Positions[M.Triangles[T + 1]];
			const FVector D = M.Positions[M.Triangles[T + 2]];

			const FVector Face = FVector::CrossProduct(B - A, D - A);
			if (Face.IsNearlyZero()) { ++OutEcartees; continue; }

			const FVector Grad = C.Gradient((A + B + D) / (3.0 * WorldseedMetersToCm));
			if (Grad.IsNearlyZero()) { ++OutEcartees; continue; }

			const double Cos = FVector::DotProduct(Face.GetSafeNormal(),
				Grad.GetSafeNormal());
			if (FMath::Abs(Cos) < 0.1) { ++OutEcartees; continue; }

			++OutJugees;
			if (Cos < 0.0) { ++Accord; }
		}
		return OutJugees > 0 ? static_cast<double>(Accord) / OutJugees : 0.0;
	}

	/**
	 * Souder par la POSITION, jamais par l'indice.
	 *
	 * Deux chunks sont deux composants distincts : une fissure entre eux est une
	 * discontinuite de POSITION, et leurs indices n'ont aucun rapport. La cle
	 * quantifie a dix micrometres -- bien en dessous de ce que le mailleur peut
	 * produire de significatif, bien au-dessus du dernier bit d'un double.
	 */
	struct FSoudure
	{
		TMap<FIntVector3, int32> ParPosition;
		TArray<FVector> Positions;
		TArray<int32> Triangles;

		static FIntVector3 Cle(const FVector& PosCm)
		{
			return FIntVector3(
				static_cast<int32>(FMath::RoundToDouble(PosCm.X * 1000.0)),
				static_cast<int32>(FMath::RoundToDouble(PosCm.Y * 1000.0)),
				static_cast<int32>(FMath::RoundToDouble(PosCm.Z * 1000.0)));
		}

		int32 Ajouter(const FVector& PosCm)
		{
			if (const int32* Vu = ParPosition.Find(Cle(PosCm))) { return *Vu; }
			const int32 I = Positions.Add(PosCm);
			ParPosition.Add(Cle(PosCm), I);
			return I;
		}

		void Coudre(const FWorldseedVoxelMesh& M)
		{
			for (int32 T = 0; T + 2 < M.Triangles.Num(); T += 3)
			{
				Triangles.Add(Ajouter(M.Positions[M.Triangles[T]]));
				Triangles.Add(Ajouter(M.Positions[M.Triangles[T + 1]]));
				Triangles.Add(Ajouter(M.Positions[M.Triangles[T + 2]]));
			}
		}
	};

	/**
	 * Aretes n'appartenant qu'a UN triangle, hors des parois.
	 *
	 * L'ARETE OUVERTE EST LA DEFINITION D'UN TROU, PAS UN INDICE. Mais une
	 * surface coupee par une boite en produit legitimement tout le long de ses
	 * parois : les compter serait mesurer la boite. On ne retient donc que les
	 * aretes INTERIEURES, celles dont les deux extremites ne partagent aucun
	 * plan de coupe.
	 *
	 * `bSeulementLePlanX` restreint au plan de couture, pour que le compte
	 * porte sur la jointure et sur rien d'autre.
	 */
	void CompterAretes(const FSoudure& S, const TArray<double>& PlansXCm,
		double MinYCm, double MaxYCm, double MinZCm, double MaxZCm,
		bool bSeulementLaCouture, double CoutureXCm,
		int32& OutOuvertes, int32& OutNonManifold)
	{
		const double Eps = 0.05;   // un demi-millimetre

		auto SurUnPlan = [&](const FVector& P) -> bool
		{
			for (double X : PlansXCm)
			{
				if (FMath::Abs(P.X - X) < Eps) { return true; }
			}
			return FMath::Abs(P.Y - MinYCm) < Eps || FMath::Abs(P.Y - MaxYCm) < Eps
				|| FMath::Abs(P.Z - MinZCm) < Eps || FMath::Abs(P.Z - MaxZCm) < Eps;
		};

		TMap<uint64, int32> Aretes;
		auto Noter = [&Aretes](int32 X, int32 Y)
		{
			const uint64 Lo = static_cast<uint64>(FMath::Min(X, Y));
			const uint64 Hi = static_cast<uint64>(FMath::Max(X, Y));
			++Aretes.FindOrAdd((Lo << 32) | Hi, 0);
		};

		for (int32 T = 0; T + 2 < S.Triangles.Num(); T += 3)
		{
			Noter(S.Triangles[T], S.Triangles[T + 1]);
			Noter(S.Triangles[T + 1], S.Triangles[T + 2]);
			Noter(S.Triangles[T + 2], S.Triangles[T]);
		}

		OutOuvertes = 0;
		OutNonManifold = 0;
		for (const TPair<uint64, int32>& Paire : Aretes)
		{
			const FVector& A = S.Positions[static_cast<int32>(Paire.Key >> 32)];
			const FVector& B = S.Positions[static_cast<int32>(Paire.Key & 0xFFFFFFFF)];

			if (bSeulementLaCouture)
			{
				if (FMath::Abs(A.X - CoutureXCm) > Eps
					|| FMath::Abs(B.X - CoutureXCm) > Eps)
				{
					continue;
				}
				// Sur la couture, seules les parois Y et Z coupent encore.
				const bool bBord =
					(FMath::Abs(A.Y - MinYCm) < Eps && FMath::Abs(B.Y - MinYCm) < Eps)
					|| (FMath::Abs(A.Y - MaxYCm) < Eps && FMath::Abs(B.Y - MaxYCm) < Eps)
					|| (FMath::Abs(A.Z - MinZCm) < Eps && FMath::Abs(B.Z - MinZCm) < Eps)
					|| (FMath::Abs(A.Z - MaxZCm) < Eps && FMath::Abs(B.Z - MaxZCm) < Eps);
				if (bBord) { continue; }
			}
			else if (SurUnPlan(A) && SurUnPlan(B))
			{
				continue;
			}

			if (Paire.Value == 1) { ++OutOuvertes; }
			else if (Paire.Value > 2) { ++OutNonManifold; }
		}
	}
}

/**
 * L'ENROULEMENT S'ACCORDE AU GRADIENT, ET LE TEMOIN PROUVE QUE LA MESURE
 * DISCRIMINE.
 *
 * Le temoin est le MEME maillage aux indices retournes. Sans lui, un controle
 * qui rendrait « 100 % » par construction -- parce qu'il compare une grandeur a
 * elle-meme -- passerait pour une preuve : c'est exactement ce qui est arrive a
 * `ProbeTransvoxel`. Une mesure qui ne sait pas classer le cas dont on connait
 * deja la reponse ne peut pas trancher les autres.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestTransvoxelFaceAvant,
	"Worldseed.Transvoxel.FaceAvant",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestTransvoxelFaceAvant::RunTest(const FString& Parameters)
{
	FChampFictif C(1.0f);
	FWorldseedVoxelMesh M;
	FWorldseedVoxelStats Stats;

	const FBox B = C.BoiteSurLaSurface(0.0, 0.0, 32.0);
	if (!TestTrue(TEXT("la boite porte une surface"),
		WorldseedTransvoxel::Mailler(C.Densite, nullptr, B, 1.0f,
			WorldseedTransvoxel::AucuneFace, 0.5f, M, Stats)))
	{
		return false;
	}
	if (!TestTrue(TEXT("le maillage est substantiel"), M.TriangleCount() > 100))
	{
		return false;
	}

	int32 Jugees = 0, Ecartees = 0;
	const double Part = PartAccordeeAUnreal(C, M, Jugees, Ecartees);
	AddInfo(FString::Printf(
		TEXT("%d triangles, %.2f %% de %d faces jugees a l'endroit (%d ecartees)"),
		M.TriangleCount(), Part * 100.0, Jugees, Ecartees));

	TestTrue(TEXT("assez de faces jugees pour conclure"), Jugees > 100);
	TestTrue(TEXT("on n'a pas ecarte l'essentiel"),
		Jugees > (Jugees + Ecartees) * 8 / 10);
	TestTrue(TEXT("l'enroulement est celui qu'Unreal affiche"), Part > 0.99);

	// LE TEMOIN : le meme maillage, retourne. La reponse d'une telle mesure est
	// BINAIRE -- 0,05 contre 99,95 a la premiere execution de ce test -- donc un
	// temoin qui ne s'effondre pas denonce la mesure, pas le maillage.
	FWorldseedVoxelMesh Envers = M;
	for (int32 T = 0; T + 2 < Envers.Triangles.Num(); T += 3)
	{
		Swap(Envers.Triangles[T + 1], Envers.Triangles[T + 2]);
	}

	int32 JugeesEnvers = 0, EcarteesEnvers = 0;
	const double PartEnvers = PartAccordeeAUnreal(C, Envers, JugeesEnvers, EcarteesEnvers);
	AddInfo(FString::Printf(TEXT("temoin retourne : %.2f %%"), PartEnvers * 100.0));
	TestTrue(TEXT("le temoin retourne est rejete"), PartEnvers < 0.01);

	return true;
}

/**
 * LE MAILLAGE EST CLOS, ET SES ARETES TIENNENT DANS UNE CELLULE.
 *
 * DEUX CRITERES QUI NE SE REMPLACENT PAS. L'arete ouverte voit une FISSURE, et
 * rien d'autre : un maillage reste combinatoirement clos quand ses triangles
 * pointent vers le mauvais sommet. La LONGUEUR voit cela -- un triangle de
 * marching cubes a ses trois sommets sur les aretes d'UNE cellule, donc aucune
 * arete ne peut depasser la diagonale de cette cellule. C'est une borne de
 * CONSTRUCTION, qui ne suppose ni l'autre mailleur, ni le champ, ni la
 * convention d'enroulement.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestTransvoxelCloture,
	"Worldseed.Transvoxel.Cloture",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestTransvoxelCloture::RunTest(const FString& Parameters)
{
	FChampFictif C(1.0f);
	FWorldseedVoxelMesh M;
	FWorldseedVoxelStats Stats;

	const FBox B = C.BoiteSurLaSurface(0.0, 0.0, 32.0);
	if (!TestTrue(TEXT("la boite porte une surface"),
		WorldseedTransvoxel::Mailler(C.Densite, nullptr, B, 1.0f,
			WorldseedTransvoxel::AucuneFace, 0.5f, M, Stats)))
	{
		return false;
	}

	FSoudure S;
	S.Coudre(M);

	const FBox Cm(B.Min * WorldseedMetersToCm, B.Max * WorldseedMetersToCm);
	const TArray<double> Plans = { Cm.Min.X, Cm.Max.X };

	int32 Ouvertes = 0, NonManifold = 0;
	CompterAretes(S, Plans, Cm.Min.Y, Cm.Max.Y, Cm.Min.Z, Cm.Max.Z,
		/*bSeulementLaCouture=*/false, 0.0, Ouvertes, NonManifold);

	AddInfo(FString::Printf(TEXT("%d triangles, %d sommets soudes"),
		M.TriangleCount(), S.Positions.Num()));

	TestEqual(TEXT("aucune arete interieure ouverte"), Ouvertes, 0);
	TestEqual(TEXT("aucune arete non manifold"), NonManifold, 0);

	// LA BORNE DE CONSTRUCTION : la diagonale d'une cellule, soit racine de
	// trois voxels. Elle ne vaut QUE pour les cellules regulieres -- une dalle
	// de transition enjambe la retraction, et ses triangles sont plus longs.
	const double Borne = FMath::Sqrt(3.0) * 1.0 * WorldseedMetersToCm * 1.001;
	double PlusLongue = 0.0;
	for (int32 T = 0; T + 2 < S.Triangles.Num(); T += 3)
	{
		for (int32 K = 0; K < 3; ++K)
		{
			const FVector& A = S.Positions[S.Triangles[T + K]];
			const FVector& D = S.Positions[S.Triangles[T + (K + 1) % 3]];
			PlusLongue = FMath::Max(PlusLongue, FVector::Distance(A, D));
		}
	}
	AddInfo(FString::Printf(TEXT("plus longue arete %.2f cm, borne %.2f"),
		PlusLongue, Borne));
	TestTrue(TEXT("aucune arete ne depasse la diagonale d'une cellule"),
		PlusLongue <= Borne);

	return true;
}

/**
 * LA CELLULE DE TRANSITION FERME LA FISSURE, ET LE TEMOIN LA MONTRE.
 *
 * C'est la seule chose que `FMarchingCubes` ne sait pas produire, donc la seule
 * raison d'etre de ce mailleur -- et jusqu'ici elle n'etait verifiee que par un
 * releve lu a la main.
 *
 * LE TEMOIN EST LA MOITIE QUI COMPTE. Sans lui, « zero arete ouverte » peut
 * vouloir dire « la couture est fermee » comme « ma mesure ne regarde pas au
 * bon endroit ». Avec lui, on sait que le compte SAIT voir la fissure, puisque
 * sans la cellule de transition il la voit entiere. Une mesure posee apres coup
 * n'a pas de temoin d'avant le defaut qu'elle doit voir.
 *
 * LA CELLULE VIT DANS LE BLOC GROSSIER, le long de sa frontiere avec le fin
 * (Lengyel, section 4.3) : c'est lui qui a trop peu d'echantillons -- neuf
 * valeurs fines arrivent sur une face qui n'en porte que quatre -- donc c'est a
 * lui de ceder la place. D'ou le masque pose sur le grossier, jamais sur le fin.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestTransvoxelCouture,
	"Worldseed.Transvoxel.Couture",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestTransvoxelCouture::RunTest(const FString& Parameters)
{
	// DEUX CHAMPS, UN PAR RESOLUTION : `VoxelSizeM` entre dans les reglages du
	// champ, pas seulement dans l'appel au mailleur.
	FChampFictif Grossier(2.0f);
	FChampFictif Fin(1.0f);

	// Meme taille physique, resolutions differentes : le fin couvre donc
	// ENTIEREMENT la face du grossier, ce qui est la situation d'une frontiere
	// d'anneau et ce qui rend le compte de couture complet.
	const FBox BGros = Grossier.BoiteSurLaSurface(-32.0, 0.0, 32.0);
	const FBox BFin(FVector(0.0, BGros.Min.Y, BGros.Min.Z),
		FVector(32.0, BGros.Max.Y, BGros.Max.Z));

	FWorldseedVoxelMesh MFin;
	FWorldseedVoxelStats SFin;
	if (!TestTrue(TEXT("le bloc fin porte une surface"),
		WorldseedTransvoxel::Mailler(Fin.Densite, nullptr, BFin, 1.0f,
			WorldseedTransvoxel::AucuneFace, 0.5f, MFin, SFin)))
	{
		return false;
	}

	const double CoutureCm = 0.0;
	const FBox Union(FVector(BGros.Min.X, BGros.Min.Y, BGros.Min.Z),
		FVector(BFin.Max.X, BGros.Max.Y, BGros.Max.Z));
	const FBox UnionCm(Union.Min * WorldseedMetersToCm, Union.Max * WorldseedMetersToCm);
	const TArray<double> Plans = { UnionCm.Min.X, UnionCm.Max.X };

	// --- LE TEMOIN : le grossier SANS cellule de transition ------------------
	FWorldseedVoxelMesh MSans;
	FWorldseedVoxelStats SSans;
	if (!TestTrue(TEXT("le bloc grossier porte une surface"),
		WorldseedTransvoxel::Mailler(Grossier.Densite, nullptr, BGros, 2.0f,
			WorldseedTransvoxel::AucuneFace, 0.5f, MSans, SSans)))
	{
		return false;
	}

	int32 OuvertesTemoin = 0, NonManifoldTemoin = 0;
	{
		FSoudure S;
		S.Coudre(MSans);
		S.Coudre(MFin);
		CompterAretes(S, Plans, UnionCm.Min.Y, UnionCm.Max.Y,
			UnionCm.Min.Z, UnionCm.Max.Z, /*bSeulementLaCouture=*/true,
			CoutureCm, OuvertesTemoin, NonManifoldTemoin);

		AddInfo(FString::Printf(
			TEXT("temoin sans transition : %d aretes ouvertes sur la couture"),
			OuvertesTemoin));
	}

	// SANS LA CELLULE, LA COUTURE DOIT ETRE OUVERTE. C'est ce qui donne son sens
	// au zero d'apres : si le temoin fermait deja, le compte ne mesurerait rien.
	if (!TestTrue(TEXT("le temoin MONTRE la fissure"), OuvertesTemoin > 0))
	{
		return false;
	}

	// --- ET AVEC LA CELLULE DE TRANSITION -----------------------------------
	FWorldseedVoxelMesh MGros;
	FWorldseedVoxelStats SGros;
	if (!TestTrue(TEXT("le bloc grossier se maille avec sa transition"),
		WorldseedTransvoxel::Mailler(Grossier.Densite, nullptr, BGros, 2.0f,
			WorldseedTransvoxel::PlusX, 0.5f, MGros, SGros)))
	{
		return false;
	}

	FSoudure S;
	S.Coudre(MGros);
	S.Coudre(MFin);

	int32 Ouvertes = 0, NonManifold = 0;
	CompterAretes(S, Plans, UnionCm.Min.Y, UnionCm.Max.Y,
		UnionCm.Min.Z, UnionCm.Max.Z, /*bSeulementLaCouture=*/true,
		CoutureCm, Ouvertes, NonManifold);

	AddInfo(FString::Printf(
		TEXT("avec transition : %d aretes ouvertes, %d non manifold"),
		Ouvertes, NonManifold));

	TestEqual(TEXT("la couture est fermee"), Ouvertes, 0);
	TestEqual(TEXT("et elle n'est pas non manifold"), NonManifold, 0);

	// L'ENROULEMENT DE LA FAMILLE DE TRANSITION SE JUGE A PART, et c'est la
	// lecon du routage des galeries appliquee : melangee aux regulieres, elle
	// donnait 94,7 % -- une degradation vague qu'aucune correction n'aurait
	// deplacee franchement. Separee, la reponse est devenue binaire : 0,0 %,
	// donc TOUTES a l'envers, donc la convention de DEPART de la famille et non
	// le bit d'inversion des tables.
	// ON SEPARE LES DEUX FAMILLES AU LIEU DE DEDUIRE PAR SOUSTRACTION. La dalle
	// occupe `LargeurTransition` cellule depuis la face, soit un metre ici ; on
	// prend deux metres de marge pour englober les cellules regulieres
	// retractees. Un agregat sur des choses de natures differentes ne se
	// corrige pas, il se decompose -- c'est ce qui avait fait passer un
	// « 94,7 % » vague pour un detail geometrique alors que la famille de
	// transition etait ENTIEREMENT a l'envers.
	FWorldseedVoxelMesh Dalle, Loin;
	for (int32 T = 0; T + 2 < MGros.Triangles.Num(); T += 3)
	{
		const FVector A = MGros.Positions[MGros.Triangles[T]];
		const FVector B = MGros.Positions[MGros.Triangles[T + 1]];
		const FVector D = MGros.Positions[MGros.Triangles[T + 2]];
		const double XM = (A.X + B.X + D.X) / (3.0 * WorldseedMetersToCm);

		FWorldseedVoxelMesh& Cible = (XM > -2.0) ? Dalle : Loin;
		const int32 I = Cible.Positions.Num();
		Cible.Positions.Add(A);
		Cible.Positions.Add(B);
		Cible.Positions.Add(D);
		Cible.Triangles.Add(I);
		Cible.Triangles.Add(I + 1);
		Cible.Triangles.Add(I + 2);
	}

	int32 JLoin = 0, ELoin = 0, JDalle = 0, EDalle = 0, JSans = 0, ESans = 0;
	const double PartLoin = PartAccordeeAUnreal(Grossier, Loin, JLoin, ELoin);
	const double PartDalle = PartAccordeeAUnreal(Grossier, Dalle, JDalle, EDalle);
	const double PartSans = PartAccordeeAUnreal(Grossier, MSans, JSans, ESans);

	AddInfo(FString::Printf(
		TEXT("dalle de transition : %.2f %% de %d jugees (%d ecartees)"),
		PartDalle * 100.0, JDalle, EDalle));
	AddInfo(FString::Printf(
		TEXT("regulieres loin de la couture : %.2f %% de %d jugees"),
		PartLoin * 100.0, JLoin));
	AddInfo(FString::Printf(
		TEXT("le MEME bloc sans transition : %.2f %% de %d jugees"),
		PartSans * 100.0, JSans));

	// LA DALLE EST JUGEE DANS L'ABSOLU : c'est la famille qu'on vient d'ajouter,
	// et la seule question qui vaille est de savoir si elle porte le MEME
	// enroulement que les autres ou l'INVERSE. La reponse est binaire, et elle
	// l'a toujours ete dans ce depot.
	TestTrue(TEXT("la dalle porte le meme enroulement, pas l'inverse"),
		PartDalle > 0.99);

	// LES REGULIERES SE JUGENT CONTRE LE MEME BLOC SANS TRANSITION, JAMAIS
	// CONTRE UN SEUIL ABSOLU.
	//
	// A deux metres de voxel, ce bloc rend 98,6 % la ou le meme test a un metre
	// en rend 99,95 : le gradient AU CENTROIDE est un moins bon temoin quand la
	// cellule approche la longueur d'onde du detail -- 2,5 m d'amplitude a
	// douze metres de periode. C'est une propriete de l'ORACLE a resolution
	// grossiere, pas du mailleur, et c'est le meme phenomene d'echantillonnage
	// que les diaclases qui s'aliasent aux anneaux.
	//
	// Benir un 98,6 % ecrit en dur reviendrait donc a graver une limite de la
	// mesure dans un test. Ce qu'on veut savoir est autre chose, et cela se
	// compare : **poser la dalle ne doit rien degrader de ce qui existait**.
	AddInfo(FString::Printf(TEXT("ecart du a la transition : %.2f point"),
		(PartSans - PartLoin) * 100.0));
	TestTrue(TEXT("la transition ne degrade pas les regulieres"),
		PartLoin > PartSans - 0.005);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
