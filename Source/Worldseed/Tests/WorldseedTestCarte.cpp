// Worldseed - la carte peinte : son orientation, son enroulement, son cone.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/WorldseedTestMondeFictif.h"

#include "Procedural/WorldseedCarte.h"
#include "Procedural/WorldseedGrid.h"

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
	WorldseedCarte::FParamsFenetre P =
		WorldseedCarte::FParamsFenetre::Carree(2000.0, 64);
	P.CentreXm = 1000.0;
	P.CentreYm = -500.0;

	double XHaut = 0.0, YHaut = 0.0;
	double XBas = 0.0, YBas = 0.0;
	double XGauche = 0.0, YGauche = 0.0;
	double XDroite = 0.0, YDroite = 0.0;

	WorldseedCarte::MetresDuPixel(P, P.ResX / 2, 0, XHaut, YHaut);
	WorldseedCarte::MetresDuPixel(P, P.ResX / 2, P.ResY - 1, XBas, YBas);
	WorldseedCarte::MetresDuPixel(P, 0, P.ResY / 2, XGauche, YGauche);
	WorldseedCarte::MetresDuPixel(P, P.ResX - 1, P.ResY / 2, XDroite, YDroite);

	// 1. La ligne 0 est au NORD, donc elle porte le Y le plus grand.
	TestTrue(TEXT("la ligne 0 est plus au nord que la derniere"), YHaut > YBas);

	// 2. L'est est a DROITE, et non l'inverse.
	TestTrue(TEXT("la derniere colonne est plus a l'est que la premiere"),
		XDroite > XGauche);

	// 3. L'amplitude vaut la portee, AVEC SON SIGNE. C'est cette affirmation
	//    qu'une inversion double ne peut pas passer.
	const double Attendu = 2.0 * P.DemiPorteeXm - P.MetresParPixelX();
	TestEqual(TEXT("du nord au sud, on couvre bien la portee"),
		YHaut - YBas, Attendu, 0.001);
	TestEqual(TEXT("d'ouest en est, on couvre bien la portee"),
		XDroite - XGauche, Attendu, 0.001);

	// Le centre de la fenetre tombe bien sur le centre demande : sans le
	// demi-pixel, la carte glisserait d'un demi-pas sous le joueur.
	double XC = 0.0, YC = 0.0, XC2 = 0.0, YC2 = 0.0;
	WorldseedCarte::MetresDuPixel(P, P.ResX / 2 - 1, P.ResY / 2 - 1, XC, YC);
	WorldseedCarte::MetresDuPixel(P, P.ResX / 2, P.ResY / 2, XC2, YC2);
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

	WorldseedCarte::FParamsFenetre P =
		WorldseedCarte::FParamsFenetre::Carree(1000.0, 48);
	P.bDisque = false;            // on compare des images, pas des disques
	P.bLisereCote = false;        // il depend des voisins : une variable de moins
	P.FondM = WorldseedCarte::FondDuMonde(Monde.ElevationM);

	const int32 Octets = P.ResX * P.ResY * 4;
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

	WorldseedCarte::FParamsFenetre P =
		WorldseedCarte::FParamsFenetre::Carree(1000.0, 48);
	P.bDisque = false;
	P.bLisereCote = false;
	P.FondM = WorldseedCarte::FondDuMonde(Monde.ElevationM);

	const int32 Octets = P.ResX * P.ResY * 4;
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

	const int32 VidePole = CompterVide(AuPole, P.ResX);
	const int32 VideMilieu = CompterVide(AuMilieu, P.ResX);

	// La moitie haute est hors monde, a un pixel pres.
	const int32 Attendu = (P.ResY / 2) * P.ResX;
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

	WorldseedCarte::FParamsFenetre P =
		WorldseedCarte::FParamsFenetre::Carree(1000.0, 64);
	P.bLisereCote = false;
	P.FondM = WorldseedCarte::FondDuMonde(Monde.ElevationM);

	const int32 Octets = P.ResX * P.ResY * 4;
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
		return I[(Y * P.ResX + X) * 4 + 3];
	};

	TestEqual(TEXT("le centre est opaque"),
		static_cast<int32>(Alpha(Rond, P.ResX / 2, P.ResY / 2)), 255);
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
	for (int32 I = 0; I < P.ResX * P.ResY; ++I)
	{
		if (Rond[I * 4 + 3] > 127) { ++Opaques; }
	}
	const float Part = static_cast<float>(Opaques)
		/ static_cast<float>(P.ResX * P.ResY);

	const float RayonEffectif = static_cast<float>(P.ResX) * 0.5f - 1.0f;
	const float Attendu = PI * RayonEffectif * RayonEffectif
		/ static_cast<float>(P.ResX * P.ResY);
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


/**
 * UNE FENETRE RECTANGULAIRE GARDE DES PIXELS CARRES.
 *
 * La carte plein ecran est un rectangle -- 16:9 a l'ecran, 2:1 pour le monde --
 * et une fenetre etiree fausserait l'ombrage, qui suppose des metres isotropes,
 * comme le disque, qui suppose un cercle. La fabrique `Rectangle` deduit la
 * demi-hauteur du nombre de pixels : il ne doit exister AUCUNE facon d'exprimer
 * une carte etiree.
 *
 * DEUX TEMOINS, ET LE SECOND EST CELUI QUI COMPTE. Un peintre qui ignorerait
 * completement `ResY` -- en le prenant pour `ResX`, ce qui est l'erreur qu'on
 * commet en migrant du carre au rectangle -- passerait la premiere affirmation
 * haut la main. Il ne passe pas l'echange des deux axes.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestCarteRectangle,
	"Worldseed.Carte.LaFenetreRectangulaireGardeDesPixelsCarres",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestCarteRectangle::RunTest(const FString& Parameters)
{
	const WorldseedCarte::FParamsFenetre Large =
		WorldseedCarte::FParamsFenetre::Rectangle(8000.0, 320, 160);

	TestEqual(TEXT("le pas est le meme sur les deux axes"),
		Large.MetresParPixelX(), Large.MetresParPixelY(), 1e-9);
	TestTrue(TEXT("et la fenetre se declare carree en pixels"), Large.PixelsCarres());

	// L'amplitude couverte vaut la portee moins un pas, PAR AXE -- meme forme
	// que l'oracle du nord, qui la verifie deja pour le carre.
	auto Amplitudes = [](const WorldseedCarte::FParamsFenetre& P,
		double& OutEstOuest, double& OutNordSud)
	{
		double X0 = 0.0, Y0 = 0.0, X1 = 0.0, Y1 = 0.0;
		WorldseedCarte::MetresDuPixel(P, 0, P.ResY / 2, X0, Y0);
		WorldseedCarte::MetresDuPixel(P, P.ResX - 1, P.ResY / 2, X1, Y1);
		OutEstOuest = X1 - X0;

		WorldseedCarte::MetresDuPixel(P, P.ResX / 2, 0, X0, Y0);
		WorldseedCarte::MetresDuPixel(P, P.ResX / 2, P.ResY - 1, X1, Y1);
		OutNordSud = Y0 - Y1;
	};

	double EstOuest = 0.0, NordSud = 0.0;
	Amplitudes(Large, EstOuest, NordSud);

	TestEqual(TEXT("d'ouest en est, on couvre la demi-largeur doublee"),
		EstOuest, 2.0 * Large.DemiPorteeXm - Large.MetresParPixelX(), 0.001);
	TestEqual(TEXT("du nord au sud, on couvre la demi-hauteur doublee"),
		NordSud, 2.0 * Large.DemiPorteeYm - Large.MetresParPixelY(), 0.001);
	TestTrue(TEXT("une fenetre large couvre plus d'est en ouest que du nord au sud"),
		EstOuest > NordSud * 1.9);

	// TEMOIN 1 : a resolutions egales, le rectangle EST le carre. Une
	// regression qui ne toucherait que le chemin rectangulaire ne peut pas se
	// cacher derriere.
	const WorldseedCarte::FParamsFenetre R =
		WorldseedCarte::FParamsFenetre::Rectangle(1234.0, 64, 64);
	const WorldseedCarte::FParamsFenetre C =
		WorldseedCarte::FParamsFenetre::Carree(1234.0, 64);
	TestEqual(TEXT("TEMOIN : meme demi-portee en Y"), R.DemiPorteeYm, C.DemiPorteeYm, 1e-9);
	TestEqual(TEXT("TEMOIN : meme pas"), R.MetresParPixelX(), C.MetresParPixelX(), 1e-9);

	// TEMOIN 2 : echanger les deux resolutions doit ECHANGER les amplitudes.
	const WorldseedCarte::FParamsFenetre Haute =
		WorldseedCarte::FParamsFenetre::Rectangle(4000.0, 160, 320);
	double EstOuest2 = 0.0, NordSud2 = 0.0;
	Amplitudes(Haute, EstOuest2, NordSud2);

	TestEqual(TEXT("TEMOIN : l'echange rend la largeur de l'autre"),
		EstOuest2, NordSud, 0.001);
	TestEqual(TEXT("TEMOIN : et la hauteur de l'autre"),
		NordSud2, EstOuest, 0.001);

	return true;
}


/**
 * L'ALLER-RETOUR ENTRE UN PIXEL ET DES METRES, Y COMPRIS SUR LE MERIDIEN.
 *
 * `PixelDuMetre` sert quatre choses -- la region UV, les marqueurs, le clic, le
 * zoom centre sur le curseur -- et une erreur de signe y donnerait une carte
 * qui a l'air juste jusqu'a ce qu'on clique. L'aller-retour est le seul
 * controle qui ne suppose rien de la convention : il exige seulement que les
 * deux fonctions soient reciproques.
 *
 * ET IL SE FAIT SUR LE MERIDIEN DE BORDURE, parce que c'est la que la fonction
 * fait quelque chose de particulier : un point a l'autre bout du monde doit y
 * etre ramene a son representant LE PLUS PROCHE, faute de quoi le marqueur du
 * joueur disparait de la carte des qu'on approche la couture.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestCarteAllerRetour,
	"Worldseed.Carte.LAllerRetourPixelMetre",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestCarteAllerRetour::RunTest(const FString& Parameters)
{
	const FWorldseedGeometry Geo = WorldseedTest::Geometrie(64);
	const double LargeurM = static_cast<double>(Geo.WidthM());

	WorldseedCarte::FParamsFenetre P =
		WorldseedCarte::FParamsFenetre::Rectangle(3000.0, 320, 180);
	P.CentreXm = 1500.0;
	P.CentreYm = -700.0;

	// --- l'aller-retour, sur une grille de pixels ---------------------------
	double PireEcart = 0.0;
	for (int32 PY = 0; PY < P.ResY; PY += 17)
	{
		for (int32 PX = 0; PX < P.ResX; PX += 23)
		{
			double Xm = 0.0, Ym = 0.0;
			WorldseedCarte::MetresDuPixel(P, PX, PY, Xm, Ym);

			double RX = 0.0, RY = 0.0;
			const bool bDedans = WorldseedCarte::PixelDuMetre(P, LargeurM, Xm, Ym, RX, RY);

			TestTrue(TEXT("un pixel de la fenetre s'y retrouve"), bDedans);
			PireEcart = FMath::Max(PireEcart,
				FMath::Max(FMath::Abs(RX - PX), FMath::Abs(RY - PY)));
		}
	}
	TestTrue(*FString::Printf(TEXT("l'aller-retour est exact (pire ecart %.3g px)"),
		PireEcart), PireEcart < 1e-6);

	// --- le meridien : la fenetre est a cheval sur la couture ---------------
	WorldseedCarte::FParamsFenetre Couture =
		WorldseedCarte::FParamsFenetre::Rectangle(3000.0, 320, 180);
	Couture.CentreXm = LargeurM * 0.5 - 500.0;   // 500 m avant le bord est

	// Un point situe 1000 m plus a l'est a DEPASSE la couture : en coordonnees
	// brutes il se retrouve a l'extreme OUEST du monde, et une projection naive
	// le renverrait a des milliers de pixels hors de l'ecran.
	const double XApres = -LargeurM * 0.5 + 500.0;
	double CX = 0.0, CY = 0.0;
	const bool bVu = WorldseedCarte::PixelDuMetre(Couture, LargeurM, XApres,
		Couture.CentreYm, CX, CY);

	TestTrue(TEXT("un point au-dela de la couture reste dans la fenetre"), bVu);
	TestEqual(TEXT("et il tombe a mille metres a l'est du centre"),
		CX, static_cast<double>(Couture.ResX) * 0.5 - 0.5
			+ 1000.0 / Couture.MetresParPixelX(), 0.001);

	// TEMOIN 1 : sans largeur de monde, l'enroulement est desarme et le meme
	// point sort de la fenetre. Sans cette ligne, on ne saurait pas si
	// l'enroulement fait quelque chose ou si le point etait deja dedans.
	double SX = 0.0, SY = 0.0;
	const bool bSansEnroulement = WorldseedCarte::PixelDuMetre(Couture, 0.0,
		XApres, Couture.CentreYm, SX, SY);
	TestFalse(TEXT("TEMOIN : sans enroulement, le point est hors champ"),
		bSansEnroulement);

	// TEMOIN 2 : deux points distincts donnent deux pixels distincts. Une
	// projection degeneree -- qui rendrait toujours le centre -- passerait
	// l'aller-retour pixel vers pixel sans broncher.
	double AX = 0.0, AY = 0.0, BX = 0.0, BY = 0.0;
	WorldseedCarte::PixelDuMetre(P, LargeurM, P.CentreXm + 900.0, P.CentreYm, AX, AY);
	WorldseedCarte::PixelDuMetre(P, LargeurM, P.CentreXm, P.CentreYm + 900.0, BX, BY);
	TestTrue(TEXT("TEMOIN : l'est deplace en X, le nord en Y"),
		AX > BX + 10.0 && BY < AY - 10.0);

	return true;
}


/**
 * L'OMBRAGE NE COMPARE PAS DEUX POINTS PLUS SERRES QU'UN PIXEL.
 *
 * A trois cellules par pixel -- le cran de six kilometres de la minimap --
 * comparer deux points distants d'UNE cellule echantillonne une frequence que
 * l'image ne porte plus : le relief sort en poivre et sel, et l'on cherche un
 * defaut de relief pour ce qui n'est qu'un pas d'echantillonnage.
 *
 * LE TEMOIN PORTE SUR L'AUTRE BOUT DE LA PLAGE, et il est indispensable : on
 * ne borne que vers le HAUT. Quand un pixel est plus FIN qu'une cellule -- le
 * cran de cinq cents metres -- suivre le pixel rapprocherait les deux points
 * sans rien gagner, le relief n'ayant pas ce detail.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestCarteOmbrage,
	"Worldseed.Carte.LOmbrageNeDepassePasLaFrequenceDuPixel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestCarteOmbrage::RunTest(const FString& Parameters)
{
	const FWorldseedGeometry Geo = WorldseedTest::Geometrie(64);
	const double Cellule = static_cast<double>(Geo.MetersPerPixel());

	// Quatre cellules par pixel : l'ombrage doit suivre le PIXEL.
	const WorldseedCarte::FParamsFenetre Gros =
		WorldseedCarte::FParamsFenetre::Carree(Cellule * 4.0 * 32.0, 64);
	TestEqual(TEXT("a quatre cellules par pixel, le pas suit le pixel"),
		WorldseedCarte::PasOmbrageMetres(Gros, Geo), Gros.MetresParPixelX(), 1e-6);
	TestTrue(TEXT("et il vaut donc plusieurs cellules"),
		WorldseedCarte::PasOmbrageMetres(Gros, Geo) > Cellule * 3.5);

	// TEMOIN : en agrandissement, le pas RESTE a la cellule.
	const WorldseedCarte::FParamsFenetre Fin =
		WorldseedCarte::FParamsFenetre::Carree(Cellule * 0.25 * 32.0, 64);
	TestEqual(TEXT("TEMOIN : a un quart de cellule par pixel, le pas reste la cellule"),
		WorldseedCarte::PasOmbrageMetres(Fin, Geo), Cellule, 1e-6);

	return true;
}


/**
 * A LA RESOLUTION DE LA GRILLE, UN PIXEL EST UNE CELLULE -- EXACTEMENT.
 *
 * C'est la propriete qui fonde toute la carte plein ecran : cuite a `NX` par
 * `NY`, elle n'agrege rien, ne vote pour rien, et ne peut donc ni inventer une
 * couleur ni perdre une ile. Si l'alignement glissait d'un demi-pixel, tout
 * cela redeviendrait faux -- silencieusement, parce qu'une carte decalee d'une
 * cellule reste une carte parfaitement plausible.
 *
 * LE TEMOIN EST LA RESOLUTION VOISINE : a `NX + 1` pixels, l'alignement DOIT
 * rompre. Sans lui, un test qui rendrait « aligne » quoi qu'il arrive
 * passerait.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestCarteAlignement,
	"Worldseed.Carte.LaCuissonEstAlignee",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestCarteAlignement::RunTest(const FString& Parameters)
{
	const FWorldseedGeometry Geo = WorldseedTest::Geometrie(32);

	auto CompterAlignes = [&Geo](int32 ResX) -> int32
	{
		const WorldseedCarte::FParamsFenetre P =
			WorldseedCarte::FParamsFenetre::Rectangle(
				static_cast<double>(Geo.WidthM()) * 0.5, ResX, Geo.NY);

		int32 Alignes = 0;
		int32 Testes = 0;
		for (int32 J = 0; J < Geo.NY; J += 7)
		{
			for (int32 I = 0; I < Geo.NX; I += 11)
			{
				double Xm = 0.0, Ym = 0.0;
				WorldseedCarte::MetresDuPixel(P, I, J, Xm, Ym);

				// LA LIGNE 0 EST AU NORD, donc elle porte la DERNIERE ligne de
				// la grille, dont la ligne 0 est au sud.
				const int32 Attendu = (Geo.NY - 1 - J) * Geo.NX + I;
				++Testes;
				if (Geo.CelluleDepuisMetres(Xm, Ym) == Attendu)
				{
					++Alignes;
				}
			}
		}
		return (Testes > 0 && Alignes == Testes) ? Testes : -Alignes;
	};

	TestTrue(TEXT("a la resolution de la grille, chaque pixel est sa cellule"),
		CompterAlignes(Geo.NX) > 0);

	// TEMOIN : un pixel de plus, et le pas n'est plus celui de la grille.
	TestTrue(TEXT("TEMOIN : a NX + 1 pixels, l'alignement rompt"),
		CompterAlignes(Geo.NX + 1) <= 0);

	return true;
}


/**
 * ON NE MOYENNE JAMAIS UN IDENTIFIANT : LE BIOME D'UN BLOC EST LE MAJORITAIRE.
 *
 * Et le vote porte sur le COUPLE (biome, couverture). Voter separement
 * fabriquerait une paire qui n'existe dans aucune cellule du bloc -- « foret »
 * majoritaire plus « neige » majoritaire, quand toute la neige etait sur les
 * cellules de toundra --, ce qui est la meme faute que la moyenne
 * d'identifiants sous un autre costume.
 *
 * LE MONDE EST FABRIQUE POUR LA QUESTION, et pas repris de la fixture : il faut
 * un bloc dont on connaisse la majorite ET dont la moyenne des identifiants
 * DIFFERE de cette majorite, faute de quoi le test ne distingue pas le defaut
 * qu'il vise.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestCarteVote,
	"Worldseed.Carte.OnNeMoyenneJamaisUnIdentifiant",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestCarteVote::RunTest(const FString& Parameters)
{
	const FWorldseedGeometry Geo = WorldseedTest::Geometrie(16);
	const int32 NX = Geo.NX;
	const int32 Total = Geo.CellCount();

	// LA TAILLE D.UNE CELLULE, et non l.espacement des noeuds : `MetersPerPixel`
	// divise par NY - 1, la convention du sol par NY. Voir `CellulesParPixel`.
	const double Cellule = static_cast<double>(Geo.WidthM()) / Geo.NX;

	// Toutes les terres a la meme altitude : ici on ne juge que le VOTE.
	TArray<float> Relief;
	Relief.Init(100.0f, Total);

	FWorldseedBiomeMap Biomes;
	Biomes.Index.Init(2, Total);
	Biomes.Cover.Init(0, Total);

	// Un bloc de 2 x 2 : trois cellules de biome 3, une de biome 11. La
	// MAJORITE est 3 ; la MOYENNE des identifiants vaudrait 5, qui n'est le
	// biome d'aucune des quatre.
	const int32 J = 8;
	const int32 I = 6;
	Biomes.Index[J * NX + I]           = 3;
	Biomes.Index[J * NX + I + 1]       = 3;
	Biomes.Index[(J + 1) * NX + I]     = 3;
	Biomes.Index[(J + 1) * NX + I + 1] = 11;

	// Le centre du bloc tombe sur le coin partage des quatre cellules.
	const double Xm = (static_cast<double>(I + 1) / NX - 0.5)
		* static_cast<double>(Geo.WidthM());
	const double Ym = (static_cast<double>(J + 1) / Geo.NY - 0.5)
		* static_cast<double>(Geo.HeightM);

	const WorldseedCarte::FBlocCellule Bloc = WorldseedCarte::AgregerBloc(
		Geo, Relief, Biomes, Xm, Ym, Cellule * 2.0, Cellule * 2.0);

	TestEqual(TEXT("le bloc a bien lu quatre cellules"), Bloc.NbCellules, 4);
	TestEqual(TEXT("le biome rendu est le MAJORITAIRE"),
		static_cast<int32>(Bloc.BiomeMajoritaire), 3);

	// TEMOIN 1 : la moyenne des identifiants vaut 5, et ce n'est PAS ce qu'on
	// rend. Sans cette ligne, le test ne distinguerait pas le bug qu'il vise.
	TestNotEqual(TEXT("TEMOIN : et ce n'est pas la moyenne des identifiants"),
		static_cast<int32>(Bloc.BiomeMajoritaire), 5);

	// --- le vote CONJOINT ---------------------------------------------------
	//
	// Deux cellules « 3 sans neige », deux cellules « 7 avec neige » : a
	// egalite, le depart se fait sur la cle la plus basse, donc (3, 0). Un vote
	// separe pourrait rendre (3, 1) -- une paire qu'aucune cellule ne porte.
	Biomes.Index[J * NX + I]           = 3;  Biomes.Cover[J * NX + I]           = 0;
	Biomes.Index[J * NX + I + 1]       = 3;  Biomes.Cover[J * NX + I + 1]       = 0;
	Biomes.Index[(J + 1) * NX + I]     = 7;  Biomes.Cover[(J + 1) * NX + I]     = 1;
	Biomes.Index[(J + 1) * NX + I + 1] = 7;  Biomes.Cover[(J + 1) * NX + I + 1] = 1;

	const WorldseedCarte::FBlocCellule Conjoint = WorldseedCarte::AgregerBloc(
		Geo, Relief, Biomes, Xm, Ym, Cellule * 2.0, Cellule * 2.0);

	const bool bPaireReelle =
		(Conjoint.BiomeMajoritaire == 3 && Conjoint.CoverMajoritaire == 0)
		|| (Conjoint.BiomeMajoritaire == 7 && Conjoint.CoverMajoritaire == 1);
	TestTrue(TEXT("la paire rendue existe dans le bloc"), bPaireReelle);
	TestEqual(TEXT("et le departage est deterministe : la cle la plus basse"),
		static_cast<int32>(Conjoint.BiomeMajoritaire), 3);

	// TEMOIN 2 : les cellules noyees ne votent pas. On noie les deux cellules
	// de biome 3 ; la majorite doit basculer sur 7, qui emerge encore.
	Relief[J * NX + I] = -10.0f;
	Relief[J * NX + I + 1] = -10.0f;

	const WorldseedCarte::FBlocCellule Noye = WorldseedCarte::AgregerBloc(
		Geo, Relief, Biomes, Xm, Ym, Cellule * 2.0, Cellule * 2.0);
	TestEqual(TEXT("TEMOIN : sous l'eau, une cellule ne vote pas"),
		static_cast<int32>(Noye.BiomeMajoritaire), 7);

	return true;
}


/**
 * L'ALTITUDE D'UN BLOC EST SA MOYENNE -- LA MEME QUE CELLE DE `Downsample`.
 *
 * Le depot a deja une reduction par moyenne de blocs, et son commentaire dit
 * pourquoi : « moyenner conserve l'altitude moyenne de chaque bloc et supprime
 * le crenelage, la ou le point-sampling garde un pixel au hasard et fait
 * scintiller le relief ». En ecrire une seconde serait la recopie que ce depot
 * interdit ; on ne l'APPELLE pas -- elle travaille sur une grille entiere, pas
 * sur un bloc arbitraire -- mais l'oracle epingle les deux l'une a l'autre, ce
 * qui est plus fort qu'un appel.
 *
 * LE TEMOIN SE PREND SUR LE RELIEF SINUSOIDAL DE LA FIXTURE, jamais sur une
 * rampe : sur une rampe, la moyenne d'un bloc EGALE la valeur de son centre, et
 * le temoin serait muet -- la fixture porte d'ailleurs cette note pour une
 * raison voisine.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestCarteMoyenne,
	"Worldseed.Carte.LAltitudeEstUneMoyenneDeBloc",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestCarteMoyenne::RunTest(const FString& Parameters)
{
	const FWorldseedGeometry Geo = WorldseedTest::Geometrie(32);
	const TArray<float> Relief = WorldseedTest::Relief(Geo);
	const FWorldseedBiomeMap Vide;
	const double Cellule = static_cast<double>(Geo.WidthM()) / Geo.NX;

	// Un bloc de 4 x 4 cellules, bien a l'interieur du monde.
	const int32 I0 = 12;
	const int32 J0 = 12;
	const double Xm = (static_cast<double>(I0 + 2) / Geo.NX - 0.5)
		* static_cast<double>(Geo.WidthM());
	const double Ym = (static_cast<double>(J0 + 2) / Geo.NY - 0.5)
		* static_cast<double>(Geo.HeightM);

	const WorldseedCarte::FBlocCellule Bloc = WorldseedCarte::AgregerBloc(
		Geo, Relief, Vide, Xm, Ym, Cellule * 4.0, Cellule * 4.0);

	TestEqual(TEXT("le bloc lit seize cellules"), Bloc.NbCellules, 16);

	double Somme = 0.0;
	for (int32 J = J0; J < J0 + 4; ++J)
	{
		for (int32 I = I0; I < I0 + 4; ++I)
		{
			Somme += static_cast<double>(Relief[J * Geo.NX + I]);
		}
	}
	const double Attendue = Somme / 16.0;

	TestEqual(TEXT("l'altitude rendue est la moyenne arithmetique"),
		static_cast<double>(Bloc.AltitudeMoyenneM), Attendue, 0.001);

	// L'EPINGLE A `Downsample` : la meme grandeur, calculee par la reduction
	// que le depot emploie deja partout ailleurs.
	TArray<float> Reduit;
	WorldseedGrid::Downsample(Relief, Geo.NX, Geo.NY, Geo.NX / 4, Geo.NY / 4, Reduit);
	const float ParDownsample = Reduit[(J0 / 4) * (Geo.NX / 4) + (I0 / 4)];

	TestEqual(TEXT("et c'est celle de WorldseedGrid::Downsample"),
		static_cast<double>(Bloc.AltitudeMoyenneM),
		static_cast<double>(ParDownsample), 0.001);

	// TEMOIN : moyenner N'EST PAS prelever au centre. Sur une rampe les deux
	// coincideraient et ce test ne prouverait rien.
	//
	// ET IL SE MESURE SUR TOUTE LA GRILLE, PAS SUR CE BLOC-CI. Premiere
	// version : la comparaison portait sur le seul bloc ci-dessus, et elle a
	// echoue -- non parce que la moyenne etait fausse, mais parce que les trois
	// harmoniques du relief s'y compensent a moins d'un metre. Un temoin dont
	// le verdict depend de l'endroit qu'on a choisi ne temoigne de rien ; celui
	// -ci prend le pire ecart sur tous les blocs alignes.
	float PireEcart = 0.0f;
	for (int32 J = 0; J + 4 <= Geo.NY; J += 4)
	{
		for (int32 I = 0; I + 4 <= Geo.NX; I += 4)
		{
			double S = 0.0;
			for (int32 DJ = 0; DJ < 4; ++DJ)
			{
				for (int32 DI = 0; DI < 4; ++DI)
				{
					S += static_cast<double>(Relief[(J + DJ) * Geo.NX + I + DI]);
				}
			}
			PireEcart = FMath::Max(PireEcart, FMath::Abs(
				Relief[(J + 2) * Geo.NX + I + 2] - static_cast<float>(S / 16.0)));
		}
	}

	TestTrue(*FString::Printf(
		TEXT("TEMOIN : moyenner change la valeur (pire ecart %.1f m)"), PireEcart),
		PireEcart > 1.0f);

	return true;
}


/**
 * LA REDUCTION SE DECLENCHE QUAND IL LE FAUT, ET SEULEMENT ALORS.
 *
 * Deux affirmations, et il faut les deux. A une cellule par pixel, agreger et
 * ne pas agreger doivent rendre des images BIT-IDENTIQUES : c'est le temoin
 * « elle ne se declenche pas a tort », et c'est aussi ce qui garantit que la
 * carte du monde cuite a la resolution de la grille n'agrege rien. A quatre
 * cellules par pixel, l'image doit CHANGER et devenir plus LISSE -- sans quoi
 * on aurait ajoute un chemin de code qui ne fait rien.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestCarteReduction,
	"Worldseed.Carte.LaReductionSeDeclencheQuandIlLeFaut",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestCarteReduction::RunTest(const FString& Parameters)
{
	const FWorldseedWorldData Monde = WorldseedTest::Monde(64);
	const double Cellule = static_cast<double>(Monde.Geometry.WidthM()) / Monde.Geometry.NX;
	constexpr int32 Res = 64;

	auto Peindre = [&Monde](double DemiM, int32 Cote,
		WorldseedCarte::FParamsFenetre::EAgregation Mode, TArray<uint8>& Out)
	{
		WorldseedCarte::FParamsFenetre P =
			WorldseedCarte::FParamsFenetre::Carree(DemiM, Cote);
		P.bDisque = false;
		P.bLisereCote = false;
		P.Agregation = Mode;
		P.FondM = WorldseedCarte::FondDuMonde(Monde.ElevationM);

		Out.SetNumUninitialized(Cote * Cote * 4);
		WorldseedCarte::PeindreFenetre(Monde.Geometry, Monde.ElevationM,
			Monde.Biomes, P, Out.GetData());
	};

	// --- une cellule par pixel : les deux chemins doivent coincider ---------
	TArray<uint8> Fin1, Fin2;
	Peindre(Cellule * 0.5 * Res, Res,
		WorldseedCarte::FParamsFenetre::EAgregation::Auto, Fin1);
	Peindre(Cellule * 0.5 * Res, Res,
		WorldseedCarte::FParamsFenetre::EAgregation::Jamais, Fin2);

	TestTrue(TEXT("a une cellule par pixel, l'agregation ne se declenche pas"),
		Fin1 == Fin2);

	// --- quatre cellules par pixel : elle doit se declencher ET lisser -------
	TArray<uint8> Gros1, Gros2;
	Peindre(Cellule * 2.0 * Res, Res,
		WorldseedCarte::FParamsFenetre::EAgregation::Auto, Gros1);
	Peindre(Cellule * 2.0 * Res, Res,
		WorldseedCarte::FParamsFenetre::EAgregation::Jamais, Gros2);

	TestFalse(TEXT("a quatre cellules par pixel, elle change l'image"),
		Gros1 == Gros2);

	// LA VARIATION TOTALE : la somme des ecarts entre voisins. Une image
	// agregee doit etre plus DOUCE que la meme prise au point.
	auto Variation = [](const TArray<uint8>& I, int32 Cote) -> int64
	{
		int64 V = 0;
		for (int32 Y = 0; Y < Cote; ++Y)
		{
			for (int32 X = 1; X < Cote; ++X)
			{
				const int32 A = (Y * Cote + X) * 4;
				const int32 B = (Y * Cote + X - 1) * 4;
				V += FMath::Abs(static_cast<int32>(I[A]) - static_cast<int32>(I[B]))
					+ FMath::Abs(static_cast<int32>(I[A + 1]) - static_cast<int32>(I[B + 1]))
					+ FMath::Abs(static_cast<int32>(I[A + 2]) - static_cast<int32>(I[B + 2]));
			}
		}
		return V;
	};

	const int64 VAgregee = Variation(Gros1, Res);
	const int64 VPonctuelle = Variation(Gros2, Res);

	TestTrue(*FString::Printf(
		TEXT("l'image agregee est plus douce (%lld contre %lld)"),
		VAgregee, VPonctuelle), VAgregee < VPonctuelle);

	return true;
}


/**
 * CHAQUE NIVEAU DE LA PYRAMIDE EST LA MOYENNE 2 x 2 DU PRECEDENT.
 *
 * Sans elle, la carte cuite a 4096 et affichee dans 1900 pixels sort avec un
 * lisere de cote POINTILLE -- le GPU preleve un texel sur deux. Verifie a
 * l'image sur la planche de `ProbeCarteEcran` avant de l'ecrire.
 *
 * DEUX TEMOINS. Les tailles doivent suivre la regle des mipmaps jusqu'a un
 * pixel : une pyramide amputee d'un niveau, ou qui s'arrete trop tot, se voit
 * la. Et la valeur d'un niveau doit VRAIMENT etre la moyenne de quatre
 * voisins : une pyramide qui se contenterait de PRELEVER un texel sur deux --
 * exactement le defaut qu'on corrige -- passerait le controle des tailles sans
 * broncher.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestCarteMips,
	"Worldseed.Carte.LaPyramideMoyenneEtDescendJusquAUnPixel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestCarteMips::RunTest(const FString& Parameters)
{
	// Une base RECTANGULAIRE et non carree : c'est la forme du monde, et une
	// pyramide qui confondrait les deux axes s'y verrait.
	constexpr int32 BaseX = 16;
	constexpr int32 BaseY = 8;

	TArray<uint8> Base;
	Base.SetNumUninitialized(BaseX * BaseY * 4);
	for (int32 Y = 0; Y < BaseY; ++Y)
	{
		for (int32 X = 0; X < BaseX; ++X)
		{
			uint8* const P = Base.GetData() + (Y * BaseX + X) * 4;
			// Un damier FRANC : la moyenne d'un bloc 2 x 2 vaut alors le milieu
			// exact, et un prelevement rendrait l'un des deux extremes.
			const uint8 V = ((X + Y) % 2 == 0) ? 0 : 200;
			P[0] = V; P[1] = V; P[2] = V; P[3] = 255;
		}
	}

	TArray<TArray<uint8>> Niveaux;
	WorldseedCarte::CuireReductions(Base.GetData(), BaseX, BaseY, Niveaux);

	// 16x8 -> 8x4 -> 4x2 -> 2x1 -> 1x1 : quatre niveaux, et le dernier est un
	// seul pixel.
	TestEqual(TEXT("la pyramide descend jusqu'a un pixel"), Niveaux.Num(), 4);
	if (Niveaux.Num() != 4)
	{
		return false;
	}

	const int32 TaillesX[4] = { 8, 4, 2, 1 };
	const int32 TaillesY[4] = { 4, 2, 1, 1 };
	for (int32 K = 0; K < 4; ++K)
	{
		TestEqual(*FString::Printf(TEXT("niveau %d : le bon nombre d'octets"), K + 1),
			Niveaux[K].Num(), TaillesX[K] * TaillesY[K] * 4);
	}

	// TEMOIN 1 : sur un damier franc, le premier niveau vaut le MILIEU -- 100,
	// la moyenne de 0 et 200 -- et non l'un des deux extremes. Un prelevement
	// rendrait 0 ou 200.
	for (int32 I = 0; I < TaillesX[0] * TaillesY[0]; ++I)
	{
		TestEqual(TEXT("TEMOIN : le niveau 1 moyenne, il ne preleve pas"),
			static_cast<int32>(Niveaux[0][I * 4]), 100);
	}

	// TEMOIN 2 : l'alpha traverse intact. Une pyramide qui ne reduirait que les
	// trois premiers canaux laisserait un niveau transparent, et la carte
	// disparaitrait a certaines echelles seulement.
	TestEqual(TEXT("TEMOIN : l'alpha survit a la reduction"),
		static_cast<int32>(Niveaux.Last()[3]), 255);

	// Une base d'un seul pixel n'a aucun niveau a produire : le cas limite ne
	// doit ni boucler sans fin ni rendre un tableau vide de taille nulle.
	TArray<TArray<uint8>> Aucun;
	WorldseedCarte::CuireReductions(Base.GetData(), 1, 1, Aucun);
	TestEqual(TEXT("un pixel seul n'a pas de reduction"), Aucun.Num(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
