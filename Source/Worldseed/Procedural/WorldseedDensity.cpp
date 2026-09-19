// Worldseed - le champ de densite : ce qui est roche, ce qui est air.

#include "Procedural/WorldseedDensity.h"

#include "Procedural/WorldseedLithology.h"

#include "Procedural/WorldseedCaves.h"
#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedPerlin.h"

namespace
{
	const TCHAR* VOX = TEXT("voxel");
}

FWorldseedDensityRules FWorldseedDensityRules::FromRules(const UWorldseedRules& Rules)
{
	FWorldseedDensityRules Out;

	auto Num = [&Rules](const TCHAR* Key, double Fallback)
	{
		return static_cast<float>(Rules.Num(VOX, Key, Fallback));
	};
	auto Int = [&Rules](const TCHAR* Key, int32 Fallback)
	{
		return Rules.Int(VOX, Key, Fallback);
	};

	Out.VoxelSizeM = Num(TEXT("voxelSizeM"), 1.0);
	Out.BandDepthM = Num(TEXT("bandDepthM"), 100.0);

	Out.OverhangAmplitudeM = Num(TEXT("overhangAmplitudeM"), 8.0);
	Out.OverhangFrequency = Num(TEXT("overhangFrequency"), 0.016);
	Out.OverhangOctaves = Int(TEXT("overhangOctaves"), 3);
	Out.OverhangWarpM = Num(TEXT("overhangWarpM"), 25.0);
	Out.OverhangWarpFrequency = Num(TEXT("overhangWarpFrequency"), 0.012);

	Out.CaveFrequency = Num(TEXT("caveFrequency"), 0.008);
	Out.CaveOctaves = Int(TEXT("caveOctaves"), 2);
	Out.CaveThreshold = Num(TEXT("caveThreshold"), 0.86);
	Out.CaveRadiusM = Num(TEXT("caveRadiusM"), 9.0);
	Out.CaveSurfaceFadeM = Num(TEXT("caveSurfaceFadeM"), 25.0);
	Out.CaveBlendM = static_cast<float>(Rules.Num(TEXT("cavites"), TEXT("raccordM"), 2.5));

	Out.JointApertureM = Num(TEXT("diaclaseOuvertureM"), 2.4);
	Out.JointCellM = Num(TEXT("diaclaseMailleM"), 42.0);
	Out.JointAnisoZ = Num(TEXT("diaclaseAplatissementZ"), 0.22);
	Out.JointDepthM = Num(TEXT("diaclaseProfondeurM"), 45.0);
	Out.JointZoneFrequency = Num(TEXT("diaclaseZoneFrequence"), 0.0016);
	Out.JointZoneThreshold = Num(TEXT("diaclaseZoneSeuil"), 0.45);

	Out.RockColourFadeM = Num(TEXT("couleurRocheFonduM"), 12.0);

	Out.ArchDepthM = Num(TEXT("archeProfondeurM"), 12.0);
	Out.ArchSlopeMinDeg = Num(TEXT("archePenteMinDeg"), 38.0);
	Out.ArchFrequencyXY = Num(TEXT("archeFrequenceXY"), 0.017);
	Out.ArchFrequencyZ = Num(TEXT("archeFrequenceZ"), 0.080);
	Out.ArchOctaves = Int(TEXT("archeOctaves"), 2);
	Out.ArchThreshold = Num(TEXT("archeSeuil"), 0.55);
	Out.ArchAmplitudeM = Num(TEXT("archeAmplitudeM"), 5.0);

	return Out;
}

void FWorldseedDensity::Init(const FWorldseedGeometry& InGeometry,
	const TArray<float>& InElevationM, float InHeightExaggeration, int32 InSeed,
	const FWorldseedDensityRules& InRules)
{
	Geometry = InGeometry;
	ElevationM = &InElevationM;
	HeightExaggeration = FMath::Max(InHeightExaggeration, 0.01f);
	Seed = InSeed;
	Rules = InRules;
}

void FWorldseedDensity::SetLithology(const FWorldseedLithology& InLithology,
	const FWorldseedLithologyRules& InRules)
{
	LithologyId = &InLithology.Id;

	KarstifiableParId.Reset();
	KarstifiableParId.Reserve(InRules.Catalogue.Num());
	for (const FWorldseedLithologyEntry& E : InRules.Catalogue)
	{
		KarstifiableParId.Add(E.Karstifiable);
	}
}

float FWorldseedDensity::KarstifiableAt(double X, double Y) const
{
	if (!LithologyId || KarstifiableParId.Num() == 0)
	{
		// Sans lithologie branchee, on declare la roche soluble : aucune
		// diaclase ne s'ouvre, et le monde reste celui d'avant. Le defaut d'une
		// donnee absente doit etre l'ABSENCE d'effet, jamais un effet arbitraire.
		return 1.0f;
	}

	const int32 NX = Geometry.NX;
	const int32 NY = Geometry.NY;
	if (LithologyId->Num() != NX * NY)
	{
		return 1.0f;
	}

	// Meme convention de repere que SampleUV : X enroule, Y est borne.
	const double U = X / Geometry.WidthM() + 0.5;
	const double V = Y / Geometry.HeightM + 0.5;

	int32 I = FMath::FloorToInt(U * NX);
	I = ((I % NX) + NX) % NX;
	const int32 J = FMath::Clamp(FMath::FloorToInt(V * NY), 0, NY - 1);

	const uint8 Id = (*LithologyId)[J * NX + I];
	return KarstifiableParId.IsValidIndex(Id) ? KarstifiableParId[Id] : 1.0f;
}

double FWorldseedDensity::ArchAt(const FVector& PosM, double DepthM) const
{
	if (Rules.ArchAmplitudeM <= 0.0f || Rules.ArchOctaves < 1)
	{
		return -1.0;
	}

	// --- GARDE BON MARCHE : une borne VERTICALE genereuse --------------------
	//
	// La vraie porte est perpendiculaire, mais la calculer demande quatre
	// echantillons du relief. On elimine d'abord le gros du volume avec une
	// borne verticale large : a la pente la plus raide qu'on accepte, la
	// distance verticale vaut au plus `profondeur * norme du gradient`, et on
	// prend une norme de cinq, soit environ 79 degres.
	if (DepthM < 0.0 || DepthM > Rules.ArchDepthM * 5.0)
	{
		return -1.0;
	}

	// --- LA PENTE ET LA DISTANCE VRAIE ---------------------------------------
	//
	// Le gradient du relief donne les deux d'un coup : la pente, qui decide si
	// une arche peut se former ici, et la norme, qui convertit la distance
	// VERTICALE en distance PERPENDICULAIRE. Sans cette conversion la porte ne
	// mordrait jamais sur une falaise -- c'est justement la qu'on la veut.
	const double E = 2.0;
	const double Hx = SurfaceHeightM(PosM.X + E, PosM.Y)
		- SurfaceHeightM(PosM.X - E, PosM.Y);
	const double Hy = SurfaceHeightM(PosM.X, PosM.Y + E)
		- SurfaceHeightM(PosM.X, PosM.Y - E);
	const double PenteXY = FMath::Sqrt(Hx * Hx + Hy * Hy) / (2.0 * E);

	if (PenteXY < FMath::Tan(FMath::DegreesToRadians(Rules.ArchSlopeMinDeg)))
	{
		return -1.0;
	}

	const double Norme = FMath::Sqrt(1.0 + PenteXY * PenteXY);
	const double Perp = DepthM / Norme;
	if (Perp > Rules.ArchDepthM)
	{
		return -1.0;
	}

	// --- LA NAPPE ------------------------------------------------------------
	//
	// Frequence verticale bien plus grande que l'horizontale : le bruit devient
	// une pile de nappes larges et minces au lieu d'un champ de bulles. C'est
	// ce rapport, et lui seul, qui fait la forme.
	const float N = WorldseedPerlin::Fbm3D(
		static_cast<float>(PosM.X) * Rules.ArchFrequencyXY,
		static_cast<float>(PosM.Y) * Rules.ArchFrequencyXY,
		static_cast<float>(PosM.Z) * Rules.ArchFrequencyZ,
		1.0f, Rules.ArchOctaves, Seed + 7717);

	if (N <= Rules.ArchThreshold)
	{
		return -1.0;
	}

	// Le creusement s'efface en profondeur : une arche est une forme d'EROSION,
	// elle travaille depuis la paroi vers l'interieur.
	const double Fondu = 1.0 - Perp / Rules.ArchDepthM;
	const double Force = (N - Rules.ArchThreshold)
		/ FMath::Max(1.0 - Rules.ArchThreshold, 0.01);

	return Rules.ArchAmplitudeM * Force * Fondu;
}

double FWorldseedDensity::JointAt(const FVector& PosM, double DepthM) const
{
	if (Rules.JointApertureM <= 0.0f || Rules.JointCellM <= 0.0f)
	{
		return -1.0;
	}

	// --- LES TROIS GARDES BON MARCHE, DANS L'ORDRE DE LEUR COUT --------------
	//
	// Le Worley visite vingt-sept cellules : c'est l'operation la plus chere du
	// champ. Elle ne doit tourner que sur le volume ou une diaclase est
	// possible, et trois tests tres bon marche l'y ramenent.

	// 1. LA ROCHE. Une diaclase est la cavite de ce qui NE se dissout PAS : le
	//    calcaire fait des grottes, le granite fait des fractures. Les deux
	//    formes sont donc exclusives par construction, et c'est la lithologie
	//    qui les repartit -- pas un reglage.
	const float Fracturable = 1.0f - KarstifiableAt(PosM.X, PosM.Y);
	if (Fracturable <= 0.05f)
	{
		return -1.0;
	}

	// 2. LA PROFONDEUR. Fondu lineaire, referme sous JointDepthM.
	if (DepthM > Rules.JointDepthM)
	{
		return -1.0;
	}
	const double FonduProfondeur = 1.0 - FMath::Max(0.0, DepthM) / Rules.JointDepthM;

	// 3. LA ZONE. Un bruit basse frequence decide ou le reseau s'ouvre. Il est
	//    en DEUX dimensions a dessein : un chaos de blocs est une zone du
	//    paysage, pas une poche isolee dans la masse.
	//
	//    UN PERLIN 2D A UNE OCTAVE, ET C'EST UN CHOIX DE COUT AUTANT QUE DE
	//    FORME. Cette garde s'evalue sur TOUT le granite de la bande, donc des
	//    millions de fois par chunk ; en fBm 3D a deux octaves elle demandait
	//    seize evaluations de gradient, contre quatre ici. Et une octave suffit
	//    a ce qu'on lui demande : des taches larges aux bords flous, pas du
	//    detail. Mesure : 3,65 ms/chunk avec le fBm, contre 2,62 sans diaclases
	//    du tout.
	const float Bruit = WorldseedPerlin::Perlin(
		static_cast<float>(PosM.X) * Rules.JointZoneFrequency,
		static_cast<float>(PosM.Y) * Rules.JointZoneFrequency,
		Seed + 4451);

	// Le bruit sort dans [-1..1] a peu pres uniformement autour de zero ; le
	// seuil place la part voulue au-dessus de lui.
	if (Bruit <= Rules.JointZoneThreshold)
	{
		return -1.0;
	}
	const double Zone = FMath::Min(1.0, (Bruit - Rules.JointZoneThreshold) / 0.25);

	// --- LE RESEAU LUI-MEME ---------------------------------------------------
	//
	// F2 - F1 s'annule sur la frontiere entre deux germes. L'axe Z est comprime
	// avant l'evaluation, ce qui etire les cellules en prismes : leurs parois
	// deviennent des plans quasi verticaux, c'est-a-dire des diaclases et non
	// des bulles.
	const float Echelle = 1.0f / Rules.JointCellM;
	float F1 = 0.0f;
	float F2 = 0.0f;
	WorldseedPerlin::Worley3D(
		static_cast<float>(PosM.X) * Echelle,
		static_cast<float>(PosM.Y) * Echelle,
		static_cast<float>(PosM.Z) * Echelle * Rules.JointAnisoZ,
		Seed + 4457, F1, F2);

	// Distance a la paroi, ramenee en metres.
	const double DistanceParoiM = (F2 - F1) * Rules.JointCellM * 0.5;

	const double DemiOuverture =
		0.5 * Rules.JointApertureM * Fracturable * Zone * FonduProfondeur;

	return DemiOuverture - DistanceParoiM;
}

bool FWorldseedDensity::IsValid() const
{
	return ElevationM != nullptr
		&& Geometry.NX >= 2
		&& ElevationM->Num() == Geometry.CellCount();
}

float FWorldseedDensity::SurfaceHeightM(double X, double Y) const
{
	if (!IsValid())
	{
		return 0.0f;
	}

	// LA LONGITUDE S'ENROULE, LA LATITUDE SE BORNE. C'est la meme convention
	// que GetHeightAtWorldXY et que SampleUV : le monde est une sphere
	// deroulee, ses bords est et ouest sont le meme meridien.
	const double WidthM = Geometry.WidthM();
	const double HeightM = Geometry.HeightM;

	double U = X / WidthM + 0.5;
	U -= FMath::FloorToDouble(U);
	const double V = FMath::Clamp(Y / HeightM + 0.5, 0.0, 1.0);

	const float Raw = WorldseedGrid::SampleUV(*ElevationM, Geometry.NX, Geometry.NY,
		static_cast<float>(U), static_cast<float>(V));

	return Raw * HeightExaggeration;
}

void FWorldseedDensity::SurfaceRangeM(double MinX, double MinY, double MaxX,
	double MaxY, float& OutMinM, float& OutMaxM) const
{
	OutMinM = 0.0f;
	OutMaxM = 0.0f;
	if (!IsValid())
	{
		return;
	}

	// ON ECHANTILLONNE LA GRILLE, PAS LE CHAMP. Un pas d'une cellule suffit :
	// la surface macro ne peut pas varier plus vite que sa propre resolution,
	// et c'est precisement ce que cette borne doit encadrer.
	const double StepM = FMath::Max(Geometry.MetersPerPixel(), 1.0f);

	float Lo = TNumericLimits<float>::Max();
	float Hi = TNumericLimits<float>::Lowest();

	for (double Y = MinY; Y <= MaxY + StepM * 0.5; Y += StepM)
	{
		for (double X = MinX; X <= MaxX + StepM * 0.5; X += StepM)
		{
			const float H = SurfaceHeightM(FMath::Min(X, MaxX), FMath::Min(Y, MaxY));
			Lo = FMath::Min(Lo, H);
			Hi = FMath::Max(Hi, H);
		}
	}

	// Le deplacement VERTICAL porte la surface de son amplitude, dans les deux
	// sens. Le deplacement HORIZONTAL, lui, fait lire le relief jusqu'a sa
	// portee plus loin : la plage doit donc couvrir le voisinage elargi, sans
	// quoi le streaming manquerait les chunks ou la surface s'est repliee.
	if (Rules.OverhangWarpM > 0.0f)
	{
		const double Marge = Rules.OverhangWarpM;
		for (double Y = MinY - Marge; Y <= MaxY + Marge + StepM * 0.5; Y += StepM)
		{
			for (double X = MinX - Marge; X <= MaxX + Marge + StepM * 0.5; X += StepM)
			{
				const float H = SurfaceHeightM(X, Y);
				Lo = FMath::Min(Lo, H);
				Hi = FMath::Max(Hi, H);
			}
		}
	}

	OutMinM = Lo - Rules.OverhangAmplitudeM;
	OutMaxM = Hi + Rules.OverhangAmplitudeM;
}

double FWorldseedDensity::CaveAt(const FVector& PosM, double DepthM) const
{
	if (Rules.CaveRadiusM <= 0.0f || Rules.CaveThreshold >= 1.0f)
	{
		return 0.0;
	}

	// PAS DE GALERIE PRES DE LA SURFACE NI SOUS LA BANDE. La premiere borne
	// evite que le sol ne soit perfore partout ; la seconde ferme le fond, sans
	// quoi une galerie rencontrerait le plein force et s'y terminerait par un
	// plancher parfaitement plat.
	const double Haut = WorldseedPerlin::Smoothstep(
		0.0f, Rules.CaveSurfaceFadeM, static_cast<float>(DepthM));
	const double Bas = 1.0 - WorldseedPerlin::Smoothstep(
		Rules.BandDepthM * 0.75f, Rules.BandDepthM, static_cast<float>(DepthM));

	const double Fondu = Haut * Bas;
	if (Fondu <= 0.0)
	{
		return 0.0;
	}

	const float Crete = WorldseedPerlin::Ridged3D(
		static_cast<float>(PosM.X), static_cast<float>(PosM.Y),
		static_cast<float>(PosM.Z), Rules.CaveFrequency, Rules.CaveOctaves,
		Seed + 40961);

	const double Depassement = Crete - Rules.CaveThreshold;
	if (Depassement <= 0.0)
	{
		return 0.0;
	}

	// Le depassement est ramene sur [0..1] avant de donner un rayon : sans
	// cela le rayon dependrait du seuil choisi, et regler l'un deregler
	// l'autre.
	const double Normalise = Depassement / FMath::Max(1.0 - Rules.CaveThreshold, 1e-4);
	return Normalise * Rules.CaveRadiusM * Fondu;
}

double FWorldseedDensity::At(const FVector& PosM, const FWorldseedCaveLocal* Caves) const
{
	if (!IsValid())
	{
		return 1.0;
	}

	float Surface = SurfaceHeightM(PosM.X, PosM.Y);

	// --- surplombs, par deplacement HORIZONTAL ------------------------------
	//
	// A chaque altitude on va lire le relief un peu plus loin, et le decalage
	// tourne avec Z. Sur du plat cela ne change presque rien ; sur une falaise,
	// deux altitudes voisines lisent des endroits dont l'altitude differe de
	// dizaines de metres, et la surface se replie.
	//
	// LE DEPLACEMENT N'EST CALCULE QUE PRES DE LA SURFACE, et c'est une
	// economie qui compte : les deux tiers des evaluations tombent loin d'elle,
	// dans le plein ou dans l'air, ou le relief exact n'a aucune importance.
	// La borne est large -- la bande entiere -- pour qu'un point juste au-dessus
	// d'une falaise ne bascule pas d'un cote a l'autre du test.
	if (Rules.OverhangWarpM > 0.0f
		&& FMath::Abs(PosM.Z - Surface) < Rules.BandDepthM)
	{
		const float FX = static_cast<float>(PosM.X);
		const float FY = static_cast<float>(PosM.Y);
		const float FZ = static_cast<float>(PosM.Z);

		const double DecalageX = Rules.OverhangWarpM * WorldseedPerlin::Fbm3D(
			FX, FY, FZ, Rules.OverhangWarpFrequency, 2, Seed + 9001);
		const double DecalageY = Rules.OverhangWarpM * WorldseedPerlin::Fbm3D(
			FX, FY, FZ, Rules.OverhangWarpFrequency, 2, Seed + 9002);

		Surface = SurfaceHeightM(PosM.X + DecalageX, PosM.Y + DecalageY);
	}

	// Distance signee a la surface macro : negative sous terre.
	double D = PosM.Z - Surface;

	// --- le RESEAU, et il se lit AVANT la sortie rapide ----------------------
	//
	// La sortie rapide ne connait que la portee du BRUIT -- une vingtaine de
	// metres. Le reseau, lui, ouvre de l'air jusqu'a quatre-vingt-dix metres
	// sous la surface : evalue apres elle, il aurait ete purement et simplement
	// ignore, et les chambres n'auraient jamais existe.
	//
	// IL EST CREUSE, JAMAIS AJOUTE : on enleve de la matiere a un solide, ce qui
	// ne peut pas produire de lambeau flottant. C'est la difference de fond avec
	// les surplombs par deformation, qui dechiraient la surface.
	double Air = -1.0;
	if (Caves && !Caves->IsEmpty())
	{
		Air = WorldseedCaves::AirAt(*Caves, PosM, Rules.CaveBlendM);
	}

	// --- sortie rapide ------------------------------------------------------
	//
	// LOIN DE LA SURFACE, LE BRUIT NE PEUT PLUS CHANGER LE SIGNE, donc il ne
	// peut plus deplacer l'isovaleur zero : le calculer serait payer pour un
	// resultat que personne ne regarde. Le deplacement des surplombs est borne
	// par son amplitude, le creusement des galeries par leur rayon ; au-dela de
	// la somme des deux, la distance macro decide seule.
	//
	// Ce n'est pas une approximation du resultat : la valeur rendue diffère de
	// la valeur exacte, mais seulement la ou le marching cubes ne s'en sert que
	// pour un test de signe. Pres de la surface -- la ou il interpole -- le
	// champ complet est calcule.
	//
	// MESURE QUI A MOTIVE CETTE SORTIE : 45 082 evaluations par chunk de 32 m a
	// 0,52 microseconde piece, soit 23,4 ms par chunk. Dans un chunk de 32 m
	// centre sur le relief, la bande utile n'en fait que 17.
	const double PorteeUtile = Rules.OverhangAmplitudeM + Rules.CaveRadiusM;
	if (D > PorteeUtile || D < -PorteeUtile - Rules.BandDepthM)
	{
		return FMath::Max(D, Air);
	}

	// --- surplombs ----------------------------------------------------------
	// Le bruit 3D deplace la surface ; la ou son gradient depasse la pente du
	// terrain, celle-ci se replie et devient franchissable par-dessous.
	//
	// MEME RAISONNEMENT QUE LA SORTIE RAPIDE, applique au seul terme de
	// surplomb : il ne deplace la surface que dans une bande de son amplitude.
	// Au fond de la bande creusable -- l'essentiel du volume -- il ne peut rien
	// changer, et c'est trois des cinq bruits du calcul. La marge de 20 % evite
	// de rogner les surplombs qui atteignent tout juste l'amplitude.
	const bool bPresDeLaSurface =
		FMath::Abs(D) <= Rules.OverhangAmplitudeM * 1.2;

	if (bPresDeLaSurface && Rules.OverhangAmplitudeM > 0.0f && Rules.OverhangOctaves > 0)
	{
		D -= Rules.OverhangAmplitudeM * WorldseedPerlin::Fbm3D(
			static_cast<float>(PosM.X), static_cast<float>(PosM.Y),
			static_cast<float>(PosM.Z), Rules.OverhangFrequency,
			Rules.OverhangOctaves, Seed + 1733);
	}

	const double DepthM = -D;

	// --- le socle -----------------------------------------------------------
	// Sous la bande, plein, et sans transition : aucun changement de signe donc
	// aucune surface, donc rien a mailler.
	if (DepthM > Rules.BandDepthM)
	{
		return FMath::Max(-1.0, Air);
	}

	// --- galeries -----------------------------------------------------------
	const double Vide = CaveAt(PosM, DepthM);
	if (Vide > 0.0)
	{
		// On PREND LE MAXIMUM plutot qu'on n'additionne : une galerie creuse le
		// vide, elle ne repousse pas la roche. L'addition ferait remonter le
		// sol au-dessus du tube.
		D = FMath::Max(D, Vide);
	}

	// --- arches et abris sous roche -----------------------------------------
	// ON CREUSE, ON NE DEFORME PAS, et c'est toute la difference avec la piste
	// abandonnee. Un terme soustractif enleve de la matiere a un solide : il ne
	// peut pas produire de lambeau flottant. Deplacer la surface, si -- mesure
	// a l'epoque : des ecailles detachees dans le ciel des que le deplacement
	// cessait d'etre inversible.
	const double Voute = ArchAt(PosM, DepthM);
	if (Voute > 0.0)
	{
		D = FMath::Max(D, Voute);
	}

	// --- diaclases ----------------------------------------------------------
	// L'autre forme de cavite, et celle de la roche qui NE se dissout PAS. Elle
	// ne peut pas coexister avec la precedente au meme endroit : le test de
	// lithologie les separe.
	const double Fissure = JointAt(PosM, DepthM);
	if (Fissure > 0.0)
	{
		D = FMath::Max(D, Fissure);
	}

	// Le bruit ci-dessus donne le GRAIN -- des conduits credibles, mais sans
	// garantie qu'ils communiquent. Le reseau, lui, garantit la connexite.
	return FMath::Max(D, Air);
}
