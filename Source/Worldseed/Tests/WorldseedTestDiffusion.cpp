// Worldseed - la diffusion : une PARTITION, et un ecart de niveau borne a UN.
// Les deux proprietes dont depend l'absence de fissure, et qui n'avaient pour
// tout filet qu'un avertissement au journal.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedDensity.h"
#include "Procedural/WorldseedDiffusion.h"

#include "Misc/AutomationTest.h"

namespace
{
	/**
	 * Une diffusion posee sur un champ de relief, prete a enumerer.
	 *
	 * L'ORDRE DE DECLARATION COMPTE : `Init` ne COPIE ni la geometrie ni le
	 * relief, il les REFERENCE. Declares avant le champ, ils sont construits
	 * avant lui et detruits apres -- la duree de vie est acquise par la
	 * structure plutot que par la discipline de l'appelant.
	 */
	struct FBancDiffusion
	{
		FWorldseedGeometry Geo;
		TArray<float> Relief;
		FWorldseedDensityRules ReglesChamp;
		TSharedPtr<FWorldseedDensity, ESPMode::ThreadSafe> Champ;
		FWorldseedDiffusion D;

		explicit FBancDiffusion(int32 NiveauMax, float RayonAnneau0M = 200.0f,
			float LoadRadiusM = 900.0f)
		{
			Geo = WorldseedTest::Geometrie(32);
			Relief = WorldseedTest::Relief(Geo);
			Champ = MakeShared<FWorldseedDensity, ESPMode::ThreadSafe>();
			Champ->Init(Geo, Relief, 1.0f, 4242, ReglesChamp);

			FWorldseedDiffusionRegles R;
			R.ChunkSideM = 32.0f;
			R.VoxelSizeM = 1.0f;
			R.RayonAnneau0M = RayonAnneau0M;
			R.LoadRadiusM = LoadRadiusM;
			R.BandDepthM = ReglesChamp.BandDepthM;
			R.NiveauMax = NiveauMax;
			D.Regler(R, Champ);
		}

		/** La passe complete, telle que l'acteur l'enchaine. */
		TArray<TPair<FWorldseedChunkKey, double>> Diffuser(const FVector& OrigineM)
		{
			TArray<TPair<FWorldseedChunkKey, double>> Feuilles;

			// On part des noeuds du niveau le plus grossier qui couvrent la
			// boule de chargement, exactement comme l'acteur.
			const int32 NHaut = FMath::Max(D.Regles().NiveauMax, 0);
			const double CoteHaute = D.CoteM(NHaut);
			const double Rayon = D.Regles().LoadRadiusM;

			const int32 XMin = FMath::FloorToInt((OrigineM.X - Rayon) / CoteHaute);
			const int32 XMax = FMath::FloorToInt((OrigineM.X + Rayon) / CoteHaute);
			const int32 YMin = FMath::FloorToInt((OrigineM.Y - Rayon) / CoteHaute);
			const int32 YMax = FMath::FloorToInt((OrigineM.Y + Rayon) / CoteHaute);

			for (int32 CY = YMin; CY <= YMax; ++CY)
			{
				for (int32 CX = XMin; CX <= XMax; ++CX)
				{
					float SMin = 0.0f, SMax = 0.0f;
					D.PlageSurface(CX, CY, NHaut, SMin, SMax);
					const int32 ZBas = FMath::FloorToInt(
						(SMin - ReglesChamp.BandDepthM) / CoteHaute);
					const int32 ZHaut = FMath::FloorToInt(SMax / CoteHaute);
					for (int32 CZ = ZBas; CZ <= ZHaut; ++CZ)
					{
						D.Enumerer(FWorldseedChunkKey{
							FIntVector(CX, CY, CZ), NHaut }, OrigineM, Feuilles);
					}
				}
			}

			D.Equilibrer(Feuilles, OrigineM);
			return Feuilles;
		}
	};

	/** L'origine du banc : au-dessus du relief, pas au hasard. */
	FVector OrigineDeBanc(const FBancDiffusion& B)
	{
		const double S = B.Champ->SurfaceHeightM(0.0, 0.0);
		return FVector(0.0, 0.0, S);
	}
}

/**
 * LA DIFFUSION EST UNE PARTITION : NI RECOUVREMENT, NI TROU.
 *
 * C'est la propriete qui justifie la descente recursive plutot qu'un critere
 * par point, et le commentaire de la classe dit pourquoi le reflexe inverse
 * est faux : deux chunks de niveaux differents n'ont pas le meme centre, donc
 * un critere pose sur la seule distance peut emettre DEUX feuilles pour le
 * meme volume -- geometrie dessinee en double -- ou AUCUNE, c'est-a-dire un
 * trou. Aucun des deux ne se signale : le premier coute des triangles que
 * personne ne compte, le second se voit a l'oeil sur une jointure precise.
 *
 * ON SONDE DES POINTS, pas des cles : c'est la seule facon de voir un
 * recouvrement entre niveaux differents, dont les cles ne sont pas
 * comparables.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestDiffusionPartition,
	"Worldseed.Diffusion.EstUnePartition",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestDiffusionPartition::RunTest(const FString& Parameters)
{
	FBancDiffusion B(2);
	const FVector Origine = OrigineDeBanc(B);
	const TArray<TPair<FWorldseedChunkKey, double>> Feuilles = B.Diffuser(Origine);

	if (!TestTrue(TEXT("TEMOIN : la diffusion emet des feuilles"), Feuilles.Num() > 50))
	{
		return false;
	}

	// Combien de feuilles contiennent chaque point sonde ? Exactement une, ou
	// zero si le point tombe hors de la boule chargee.
	int32 Recouvrements = 0, Couverts = 0, Sondes = 0;

	for (int32 I = 0; I < 3000; ++I)
	{
		// Des points repartis dans la boule, a coordonnees irrationnelles :
		// jamais sur une frontiere de chunk, ou l'appartenance est ambigue par
		// construction et non par defaut.
		const double A = I * 0.6180339887 * 2.0 * PI;
		const double Rho = 700.0 * FMath::Sqrt(FMath::Fmod(I * 0.4142135624, 1.0));
		const FVector P = Origine + FVector(
			Rho * FMath::Cos(A) + 0.137,
			Rho * FMath::Sin(A) + 0.271,
			(FMath::Fmod(I * 0.3027756377, 1.0) - 0.5) * 40.0 + 0.313);

		int32 Contenants = 0;
		for (const TPair<FWorldseedChunkKey, double>& F : Feuilles)
		{
			if (B.D.BoiteM(F.Key).IsInsideOrOn(P)) { ++Contenants; }
		}

		++Sondes;
		if (Contenants > 1) { ++Recouvrements; }
		if (Contenants == 1) { ++Couverts; }
	}

	AddInfo(FString::Printf(
		TEXT("%d feuilles ; sur %d points sondes, %d couverts par EXACTEMENT une, ")
		TEXT("%d par plusieurs"),
		Feuilles.Num(), Sondes, Couverts, Recouvrements));

	TestEqual(TEXT("aucun point n'est couvert par deux feuilles"), Recouvrements, 0);
	TestTrue(TEXT("TEMOIN : la plupart des points sont couverts"),
		Couverts > Sondes / 2);

	return true;
}

/**
 * APRES EQUILIBRAGE, AUCUN VOISIN N'EST A PLUS D'UN CRAN.
 *
 * TRANSVOXEL NE SAIT COUDRE QU'UN NIVEAU D'ECART, et la fissure qui en resulte
 * ne se signale PAS : le masque s'arme quand meme, la geometrie reste
 * combinatoirement close -- donc le controle d'aretes ouvertes passe -- et le
 * trou ne se voit qu'a l'oeil sur une jointure precise. C'est pourquoi cette
 * propriete n'avait, jusqu'ici, qu'un avertissement au journal pour tout
 * filet.
 *
 * ON SONDE DEPUIS LE COTE FIN, comme la passe elle-meme. Sonder depuis la
 * feuille GROSSIERE n'est pas complet : la face d'un chunk de niveau 3 touche
 * jusqu'a soixante-quatre chunks de niveau 0, et un point par face n'en voit
 * qu'un -- mesure du defaut a l'epoque, 87 violations sur 512 feuilles en
 * regime etabli.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestDiffusion2a1,
	"Worldseed.Diffusion.EcartDeNiveauBorneAUn",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestDiffusion2a1::RunTest(const FString& Parameters)
{
	static const FVector Normales[6] =
	{
		FVector(-1, 0, 0), FVector(1, 0, 0),
		FVector(0, -1, 0), FVector(0, 1, 0),
		FVector(0, 0, -1), FVector(0, 0, 1),
	};

	// Trois profondeurs d'anneaux, parce que la propriete doit tenir pour
	// TOUTE configuration -- et que c'est en montant a trois niveaux que le
	// defaut d'origine se voyait.
	for (const int32 NiveauMax : { 1, 2, 3 })
	{
		FBancDiffusion B(NiveauMax);
		const FVector Origine = OrigineDeBanc(B);
		const TArray<TPair<FWorldseedChunkKey, double>> Feuilles = B.Diffuser(Origine);

		TSet<int32> NiveauxPresents;
		for (const TPair<FWorldseedChunkKey, double>& F : Feuilles)
		{
			NiveauxPresents.Add(F.Key.Niveau);
		}

		int32 Violations = 0, PireEcart = 0;
		for (const TPair<FWorldseedChunkKey, double>& F : Feuilles)
		{
			const double Cote = B.D.CoteM(F.Key.Niveau);
			const FVector Centre = B.D.BoiteM(F.Key).GetCenter();
			for (const FVector& N : Normales)
			{
				const int32 NV = B.D.NiveauEmis(Centre + N * Cote);
				if (NV == INDEX_NONE) { continue; }
				const int32 Ecart = FMath::Abs(NV - F.Key.Niveau);
				PireEcart = FMath::Max(PireEcart, Ecart);
				if (Ecart > 1) { ++Violations; }
			}
		}

		AddInfo(FString::Printf(
			TEXT("%d anneaux : %d feuilles sur %d niveaux, ecart maximal %d, ")
			TEXT("%d feuilles ajoutees par l'equilibrage"),
			NiveauMax, Feuilles.Num(), NiveauxPresents.Num(), PireEcart,
			B.D.AjoutsDeLEquilibrage()));

		// LE TEMOIN QUI REND LA MESURE SIGNIFIANTE : s'il n'y avait qu'un seul
		// niveau emis, l'ecart serait trivialement nul et le test ne
		// prouverait rien.
		TestTrue(FString::Printf(
			TEXT("%d anneaux : TEMOIN, plusieurs niveaux coexistent"), NiveauMax),
			NiveauxPresents.Num() >= 2);

		TestEqual(FString::Printf(
			TEXT("%d anneaux : aucun voisin a plus d'un cran"), NiveauMax),
			Violations, 0);

		TestEqual(FString::Printf(
			TEXT("%d anneaux : l'equilibrage ne laisse aucun ecart"), NiveauMax),
			B.D.EcartsNonResorbes(), 0);
	}

	return true;
}

/**
 * LE MASQUE DIT EXACTEMENT QUELLES FACES TOUCHENT UN VOISIN PLUS FIN.
 *
 * IL LIT L'ENSEMBLE EMIS, IL NE LE REDEDUIT PAS, et c'est ce qui a decide de
 * l'architecture : tant que la partition etait une fonction du POINT, une
 * descente ponctuelle pouvait la reproduire ; l'equilibrage regarde les
 * VOISINS, donc le niveau d'une feuille depend de ses voisines. Deux calculs
 * qu'on espere d'accord armeraient une cellule de transition la ou il n'y a
 * pas de changement de resolution, et en oublieraient ailleurs.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestDiffusionMasque,
	"Worldseed.Diffusion.LeMasqueSuitLesFeuilles",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestDiffusionMasque::RunTest(const FString& Parameters)
{
	static const FVector Normales[6] =
	{
		FVector(-1, 0, 0), FVector(1, 0, 0),
		FVector(0, -1, 0), FVector(0, 1, 0),
		FVector(0, 0, -1), FVector(0, 0, 1),
	};

	FBancDiffusion B(2);
	const FVector Origine = OrigineDeBanc(B);
	const TArray<TPair<FWorldseedChunkKey, double>> Feuilles = B.Diffuser(Origine);

	int32 Desaccords = 0, Armees = 0, AuNiveauZero = 0;

	for (const TPair<FWorldseedChunkKey, double>& F : Feuilles)
	{
		const uint8 Masque = B.D.MasqueDe(F.Key);

		if (F.Key.Niveau <= 0)
		{
			// Un chunk de niveau 0 n'a aucun voisin plus fin, par definition.
			if (Masque != 0) { ++AuNiveauZero; }
			continue;
		}

		const double Cote = B.D.CoteM(F.Key.Niveau);
		const FVector Centre = B.D.BoiteM(F.Key).GetCenter();
		for (int32 Face = 0; Face < 6; ++Face)
		{
			const int32 NV = B.D.NiveauEmis(Centre + Normales[Face] * Cote);
			const bool bDevraitEtreArmee = (NV != INDEX_NONE && NV < F.Key.Niveau);
			const bool bArmee = (Masque & (1 << Face)) != 0;

			if (bArmee != bDevraitEtreArmee) { ++Desaccords; }
			if (bArmee) { ++Armees; }
		}
	}

	AddInfo(FString::Printf(
		TEXT("%d faces armees sur %d feuilles"), Armees, Feuilles.Num()));

	TestEqual(TEXT("un chunk de niveau 0 n'arme aucune face"), AuNiveauZero, 0);
	TestEqual(TEXT("le masque s'accorde avec l'ensemble emis"), Desaccords, 0);

	// TEMOIN : sans faces armees, le test ne prouverait rien -- il faut que des
	// transitions existent pour que leur exactitude ait un sens.
	TestTrue(TEXT("TEMOIN : des faces de transition sont bien armees"), Armees > 0);

	return true;
}

/**
 * SANS ANNEAUX, LA RESOLUTION EST UNIFORME -- ET LA PASSE EST DETERMINISTE.
 *
 * `NiveauMax = 0` doit rendre exactement la diffusion d'avant les anneaux :
 * « une donnee absente doit rester sans effet » est une regle de ce depot, et
 * c'est elle qui rend l'A/B des anneaux possible sur le MEME binaire. Le
 * determinisme, lui, est la condition du remaillage : un chunk n'est refait
 * que si son masque CHANGE, donc une passe instable remaillerait le monde
 * entier a chaque tour.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestDiffusionUniforme,
	"Worldseed.Diffusion.SansAnneauxEtStable",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestDiffusionUniforme::RunTest(const FString& Parameters)
{
	// --- anneaux eteints -----------------------------------------------------
	{
		FBancDiffusion B(0);
		const FVector Origine = OrigineDeBanc(B);
		const TArray<TPair<FWorldseedChunkKey, double>> Feuilles = B.Diffuser(Origine);

		int32 HorsNiveauZero = 0, Masques = 0;
		for (const TPair<FWorldseedChunkKey, double>& F : Feuilles)
		{
			if (F.Key.Niveau != 0) { ++HorsNiveauZero; }
			if (B.D.MasqueDe(F.Key) != 0) { ++Masques; }
		}

		AddInfo(FString::Printf(TEXT("sans anneaux : %d feuilles, toutes au niveau 0"),
			Feuilles.Num()));

		TestTrue(TEXT("TEMOIN : la diffusion emet"), Feuilles.Num() > 50);
		TestEqual(TEXT("toutes les feuilles sont au niveau 0"), HorsNiveauZero, 0);
		TestEqual(TEXT("aucune face de transition n'est armee"), Masques, 0);
		TestEqual(TEXT("et l'equilibrage n'a rien a faire"),
			B.D.AjoutsDeLEquilibrage(), 0);
	}

	// --- deux passes identiques ---------------------------------------------
	{
		FBancDiffusion A(2), C(2);
		const FVector Origine = OrigineDeBanc(A);

		const TArray<TPair<FWorldseedChunkKey, double>> FA = A.Diffuser(Origine);
		const TArray<TPair<FWorldseedChunkKey, double>> FC = C.Diffuser(Origine);

		TestEqual(TEXT("deux passes emettent le meme nombre de feuilles"),
			FA.Num(), FC.Num());

		int32 Ecarts = 0;
		for (int32 I = 0; I < FA.Num() && I < FC.Num(); ++I)
		{
			if (!(FA[I].Key == FC[I].Key)) { ++Ecarts; }
		}
		TestEqual(TEXT("dans le meme ordre, aux memes cles"), Ecarts, 0);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
