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

#endif // WITH_DEV_AUTOMATION_TESTS
