// Worldseed - la carte peinte : son orientation, son enroulement, son cone.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedCarte.h"

#include "Misc/AutomationTest.h"

/**
 * LE NORD EST EN HAUT -- et c'est le test qui compte le plus de ce fichier.
 *
 * Trois signes s'enchainent entre un lacet de camera et un pixel d'ecran, et
 * se tromper sur l'un d'eux donne une carte qui a l'air juste jusqu'a ce qu'on
 * marche. Le depot a une regle pour cela : le sens vertical SE MESURE, il ne
 * se deduit pas -- et pas sur une calotte polaire, qui existe aux deux poles.
 *
 * ON N'ASSERTIONNE PAS SUR DES COULEURS, qui traversent le biome, l'ombrage et
 * la conversion en octets : on assertionne sur la PROJECTION SEULE, celle que
 * la boucle de peinture appelle reellement.
 *
 * LES TROIS AFFIRMATIONS SE SERVENT MUTUELLEMENT DE TEMOIN, et c'est ce qui
 * rend le test sur : une inversion appliquee DEUX fois, donc annulee, passe la
 * premiere une fois sur deux mais echoue TOUJOURS la troisieme, parce que
 * l'amplitude en sort avec le mauvais signe. Une inversion appliquee au mauvais
 * axe echoue la deuxieme.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestCarteNord,
	"Worldseed.Carte.LeNordEstEnHaut",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestCarteNord::RunTest(const FString& Parameters)
{
	WorldseedCarte::FParamsFenetre P;
	P.CentreXm = 1000.0;
	P.CentreYm = -500.0;
	P.DemiPorteeM = 2000.0;
	P.Res = 64;

	double XHaut = 0.0, YHaut = 0.0;
	double XBas = 0.0, YBas = 0.0;
	double XGauche = 0.0, YGauche = 0.0;
	double XDroite = 0.0, YDroite = 0.0;

	WorldseedCarte::MetresDuPixel(P, P.Res / 2, 0, XHaut, YHaut);
	WorldseedCarte::MetresDuPixel(P, P.Res / 2, P.Res - 1, XBas, YBas);
	WorldseedCarte::MetresDuPixel(P, 0, P.Res / 2, XGauche, YGauche);
	WorldseedCarte::MetresDuPixel(P, P.Res - 1, P.Res / 2, XDroite, YDroite);

	// 1. La ligne 0 est au NORD, donc elle porte le Y le plus grand.
	TestTrue(TEXT("la ligne 0 est plus au nord que la derniere"), YHaut > YBas);

	// 2. L'est est a DROITE, et non l'inverse.
	TestTrue(TEXT("la derniere colonne est plus a l'est que la premiere"),
		XDroite > XGauche);

	// 3. L'amplitude vaut la portee, AVEC SON SIGNE. C'est cette affirmation
	//    qu'une inversion double ne peut pas passer.
	const double Attendu = 2.0 * P.DemiPorteeM - P.MetresParPixel();
	TestEqual(TEXT("du nord au sud, on couvre bien la portee"),
		YHaut - YBas, Attendu, 0.001);
	TestEqual(TEXT("d'ouest en est, on couvre bien la portee"),
		XDroite - XGauche, Attendu, 0.001);

	// Le centre de la fenetre tombe bien sur le centre demande : sans le
	// demi-pixel, la carte glisserait d'un demi-pas sous le joueur.
	double XC = 0.0, YC = 0.0, XC2 = 0.0, YC2 = 0.0;
	WorldseedCarte::MetresDuPixel(P, P.Res / 2 - 1, P.Res / 2 - 1, XC, YC);
	WorldseedCarte::MetresDuPixel(P, P.Res / 2, P.Res / 2, XC2, YC2);
	TestEqual(TEXT("le centre de la fenetre est le centre demande"),
		(XC + XC2) * 0.5, P.CentreXm, 0.001);
	TestEqual(TEXT("idem en Y"), (YC + YC2) * 0.5, P.CentreYm, 0.001);

	return true;
}


/**
 * UNE FENETRE A CHEVAL SUR LE MERIDIEN DE BORDURE NE SE COUPE PAS.
 *
 * Le monde s'enroule en longitude : les deux bords sont LE MEME MERIDIEN. Deux
 * fenetres centrees sur l'un et sur l'autre doivent donc rendre exactement la
 * meme image.
 *
 * CE QUE CE TEST ATTRAPE : un `Clamp` a la place du modulo -- qui etale la
 * derniere colonne sur toute une moitie de la minimap, donnant un mur d'un seul
 * biome assez plausible pour qu'on aille chercher un defaut de generation --
 * et des bornes entieres precalculees sans modulo, qui lisent la ligne
 * SUIVANTE et, si l'indice passe negatif, sortent du tableau.
 *
 * LE TEMOIN EST LA TROISIEME FENETRE, et sans lui le test ne prouverait rien :
 * un peintre qui ignorerait completement son centre rendrait deux images
 * identiques et passerait la premiere affirmation haut la main. C'est la
 * « fixture muette » que ce depot a payee quatre fois en une journee.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestCarteEnroulement,
	"Worldseed.Carte.LEnroulementNeCoupePasAuMeridien",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestCarteEnroulement::RunTest(const FString& Parameters)
{
	const FWorldseedWorldData Monde = WorldseedTest::Monde(64);
	const FWorldseedGeometry& Geo = Monde.Geometry;
	const double Largeur = static_cast<double>(Geo.WidthM());

	WorldseedCarte::FParamsFenetre P;
	P.DemiPorteeM = 1000.0;
	P.Res = 48;
	P.bDisque = false;            // on compare des images, pas des disques
	P.bLisereCote = false;        // il depend des voisins : une variable de moins
	P.FondM = WorldseedCarte::FondDuMonde(Monde.ElevationM);

	const int32 Octets = P.Res * P.Res * 4;
	TArray<uint8> BordOuest, BordEst, Milieu;
	BordOuest.SetNumUninitialized(Octets);
	BordEst.SetNumUninitialized(Octets);
	Milieu.SetNumUninitialized(Octets);

	P.CentreYm = 0.0;

	P.CentreXm = -Largeur * 0.5;
	WorldseedCarte::PeindreFenetre(Geo, Monde.ElevationM, Monde.Biomes, P,
		BordOuest.GetData());

	P.CentreXm = Largeur * 0.5;
	WorldseedCarte::PeindreFenetre(Geo, Monde.ElevationM, Monde.Biomes, P,
		BordEst.GetData());

	P.CentreXm = 0.0;
	WorldseedCarte::PeindreFenetre(Geo, Monde.ElevationM, Monde.Biomes, P,
		Milieu.GetData());

	int32 Differents = 0;
	int32 DifferentsTemoin = 0;
	for (int32 I = 0; I < Octets; ++I)
	{
		if (BordOuest[I] != BordEst[I]) { ++Differents; }
		if (BordOuest[I] != Milieu[I]) { ++DifferentsTemoin; }
	}

	TestEqual(TEXT("les deux bords du monde sont le meme meridien"),
		Differents, 0);

	TestTrue(TEXT("TEMOIN : une fenetre ailleurs est bien differente"),
		DifferentsTemoin > 0);

	return true;
}


/**
 * AU-DELA D'UN POLE IL N'Y A RIEN, et cela doit se voir.
 *
 * La latitude ne s'enroule PAS -- un pole n'a pas de voisin au-dela -- et elle
 * ne doit pas non plus se BORNER : borner y dessinerait un terrain raye qui
 * laisse croire que le monde continue. On peint donc un vide, etat distinct du
 * hors-disque, qui est transparent.
 *
 * LE TEMOIN EST LA FENETRE DU MILIEU : sans lui, un peintre qui declarerait
 * tout « hors monde » passerait la premiere affirmation.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestCartePoles,
	"Worldseed.Carte.LesPolesNeSEnroulentPas",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestCartePoles::RunTest(const FString& Parameters)
{
	const FWorldseedWorldData Monde = WorldseedTest::Monde(64);
	const FWorldseedGeometry& Geo = Monde.Geometry;
	const double Hauteur = static_cast<double>(Geo.HeightM);

	WorldseedCarte::FParamsFenetre P;
	P.DemiPorteeM = 1000.0;
	P.Res = 48;
	P.bDisque = false;
	P.bLisereCote = false;
	P.FondM = WorldseedCarte::FondDuMonde(Monde.ElevationM);

	const int32 Octets = P.Res * P.Res * 4;
	TArray<uint8> AuPole, AuMilieu;
	AuPole.SetNumUninitialized(Octets);
	AuMilieu.SetNumUninitialized(Octets);

	// Centree PILE sur le pole : la moitie haute de la fenetre est hors monde.
	P.CentreXm = 0.0;
	P.CentreYm = Hauteur * 0.5;
	WorldseedCarte::PeindreFenetre(Geo, Monde.ElevationM, Monde.Biomes, P,
		AuPole.GetData());

	P.CentreYm = 0.0;
	WorldseedCarte::PeindreFenetre(Geo, Monde.ElevationM, Monde.Biomes, P,
		AuMilieu.GetData());

	// Le vide a une teinte connue : on la reconnait plutot que de la deviner.
	auto CompterVide = [](const TArray<uint8>& Image, int32 Res) -> int32
	{
		const FColor Vide = FLinearColor(0.06f, 0.06f, 0.08f).ToFColor(true);
		int32 N = 0;
		for (int32 I = 0; I < Res * Res; ++I)
		{
			if (Image[I * 4 + 0] == Vide.B && Image[I * 4 + 1] == Vide.G
				&& Image[I * 4 + 2] == Vide.R)
			{
				++N;
			}
		}
		return N;
	};

	const int32 VidePole = CompterVide(AuPole, P.Res);
	const int32 VideMilieu = CompterVide(AuMilieu, P.Res);

	// La moitie haute est hors monde, a un pixel pres.
	const int32 Attendu = (P.Res / 2) * P.Res;
	TestEqual(TEXT("la moitie au-dela du pole est du vide"), VidePole, Attendu);

	TestEqual(TEXT("TEMOIN : au milieu du monde, aucun vide"), VideMilieu, 0);

	return true;
}


/**
 * LE DISQUE EST ROND, et c'est lui qui donne sa forme a la minimap sans
 * qu'aucun masque Slate n'intervienne : l'alpha du tampon suffit.
 *
 * LE TEMOIN EST LE MODE CARRE : si l'option disque etait un coup d'epee dans
 * l'eau, les deux comptes seraient egaux et le test ne discriminerait rien.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestCarteDisque,
	"Worldseed.Carte.LeDisqueEstRond",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestCarteDisque::RunTest(const FString& Parameters)
{
	const FWorldseedWorldData Monde = WorldseedTest::Monde(64);

	WorldseedCarte::FParamsFenetre P;
	P.DemiPorteeM = 1000.0;
	P.Res = 64;
	P.bLisereCote = false;
	P.FondM = WorldseedCarte::FondDuMonde(Monde.ElevationM);

	const int32 Octets = P.Res * P.Res * 4;
	TArray<uint8> Rond, Carre;
	Rond.SetNumUninitialized(Octets);
	Carre.SetNumUninitialized(Octets);

	P.bDisque = true;
	WorldseedCarte::PeindreFenetre(Monde.Geometry, Monde.ElevationM, Monde.Biomes,
		P, Rond.GetData());

	P.bDisque = false;
	WorldseedCarte::PeindreFenetre(Monde.Geometry, Monde.ElevationM, Monde.Biomes,
		P, Carre.GetData());

	auto Alpha = [&P](const TArray<uint8>& I, int32 X, int32 Y) -> uint8
	{
		return I[(Y * P.Res + X) * 4 + 3];
	};

	TestEqual(TEXT("le centre est opaque"),
		static_cast<int32>(Alpha(Rond, P.Res / 2, P.Res / 2)), 255);
	TestEqual(TEXT("le coin est transparent"),
		static_cast<int32>(Alpha(Rond, 0, 0)), 0);

	TestEqual(TEXT("TEMOIN : en mode carre, le coin est opaque"),
		static_cast<int32>(Alpha(Carre, 0, 0)), 255);

	// L'AIRE DU DISQUE, ET L'ATTENDU N'EST PAS pi/4. Premiere version : je
	// comparais a pi/4 = 0,785, l'aire d'un disque de rayon Res/2. Mesure :
	// 0,738 -- et c'est l'attendu qui etait naif. Le disque est trace au rayon
	// `Res/2 - 0,5`, pour que son fondu d'un pixel tienne dans le tampon, et le
	// seuil `alpha > 127` retire encore un demi-pixel puisqu'il coupe le fondu
	// en son milieu. Le rayon effectif vaut donc `Res/2 - 1`, ce qui donne
	// 0,7375 pour Res = 64 : l'ecart au mesure est de huit dix-millemes.
	//
	// Une tolerance elargie aurait « fait passer » le test sans rien apprendre.
	int32 Opaques = 0;
	for (int32 I = 0; I < P.Res * P.Res; ++I)
	{
		if (Rond[I * 4 + 3] > 127) { ++Opaques; }
	}
	const float Part = static_cast<float>(Opaques)
		/ static_cast<float>(P.Res * P.Res);

	const float RayonEffectif = static_cast<float>(P.Res) * 0.5f - 1.0f;
	const float Attendu = PI * RayonEffectif * RayonEffectif
		/ static_cast<float>(P.Res * P.Res);
	TestEqual(TEXT("le disque couvre l'aire de son rayon effectif"),
		Part, Attendu, 0.01f);

	return true;
}


/**
 * LE CONE SUIT LE CAP, ET C'EST TROIS FAUTES QU'UN SEUL TEST ATTRAPE : le
 * signe de `azimut = 90 - lacet`, l'inversion nord/sud de l'image, et le
 * miroir est/ouest. Ce sont les seules qu'on puisse commettre la.
 *
 * Le cone etant peint dans SON PROPRE tampon, il est une fonction pure : ce
 * test ne demande ni monde, ni Slate, ni partie en cours.
 *
 * DEUX TEMOINS, ET LES DEUX SONT INDISPENSABLES. Le compte de pixels allumes,
 * parce que le barycentre d'un ensemble VIDE ne contredit personne -- un cone
 * qui n'allumerait rien passerait les quatre affirmations de direction. Et la
 * distinction mutuelle des quatre barycentres, parce qu'un cone qui ignorerait
 * son azimut les passerait aussi.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestCarteCone,
	"Worldseed.Minimap.LeConeSuitLeCap",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestCarteCone::RunTest(const FString& Parameters)
{
	const int32 Res = 64;
	const float DemiAngle = 30.0f;

	TArray<uint8> Image;
	Image.SetNumUninitialized(Res * Res * 4);

	struct FCas { float CapDeg; const TCHAR* Nom; };
	const FCas Cas[4] = {
		{ 0.0f, TEXT("nord") }, { 90.0f, TEXT("est") },
		{ 180.0f, TEXT("sud") }, { 270.0f, TEXT("ouest") }
	};

	double BX[4] = { 0, 0, 0, 0 };
	double BY[4] = { 0, 0, 0, 0 };
	int32 Comptes[4] = { 0, 0, 0, 0 };

	const float Centre = static_cast<float>(Res) * 0.5f;

	for (int32 N = 0; N < 4; ++N)
	{
		WorldseedCarte::PeindreCone(Image.GetData(), Res, Cas[N].CapDeg,
			DemiAngle, 0.9f);

		double SX = 0.0, SY = 0.0, Poids = 0.0;
		for (int32 Y = 0; Y < Res; ++Y)
		{
			for (int32 X = 0; X < Res; ++X)
			{
				const uint8 A = Image[(Y * Res + X) * 4 + 3];
				if (A == 0) { continue; }

				// La marque centrale appartient a toutes les directions : elle
				// tirerait les quatre barycentres vers le milieu.
				const float DX = static_cast<float>(X) + 0.5f - Centre;
				const float DY = static_cast<float>(Y) + 0.5f - Centre;
				if (FMath::Sqrt(DX * DX + DY * DY) <= 4.0f) { continue; }

				SX += DX * A;
				SY += DY * A;
				Poids += A;
				++Comptes[N];
			}
		}

		if (Poids > 0.0)
		{
			BX[N] = SX / Poids;
			BY[N] = SY / Poids;
		}
	}

	// TEMOIN 1 : le cone allume quelque chose, et a peu pres ce qu'il doit.
	// Un ensemble vide passerait toutes les affirmations de direction.
	for (int32 N = 0; N < 4; ++N)
	{
		TestTrue(*FString::Printf(TEXT("TEMOIN : le cone %s allume des pixels"),
			Cas[N].Nom), Comptes[N] > 50);
	}

	// Les quatre directions. En coordonnees d'image, la ligne DESCEND : le nord
	// est donc un Y negatif.
	TestTrue(TEXT("cap nord : le cone monte"), BY[0] < -2.0 && FMath::Abs(BX[0]) < 2.0);
	TestTrue(TEXT("cap est : le cone va a droite"), BX[1] > 2.0 && FMath::Abs(BY[1]) < 2.0);
	TestTrue(TEXT("cap sud : le cone descend"), BY[2] > 2.0 && FMath::Abs(BX[2]) < 2.0);
	TestTrue(TEXT("cap ouest : le cone va a gauche"), BX[3] < -2.0 && FMath::Abs(BY[3]) < 2.0);

	// TEMOIN 2 : les quatre barycentres sont distincts. Un cone qui ignorerait
	// son azimut rendrait quatre fois le meme.
	for (int32 A = 0; A < 4; ++A)
	{
		for (int32 B = A + 1; B < 4; ++B)
		{
			const double D = FMath::Sqrt(FMath::Square(BX[A] - BX[B])
				+ FMath::Square(BY[A] - BY[B]));
			TestTrue(*FString::Printf(
				TEXT("TEMOIN : %s et %s ne pointent pas au meme endroit"),
				Cas[A].Nom, Cas[B].Nom), D > 4.0);
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
