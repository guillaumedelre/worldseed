// Worldseed - la tournee photo : voir le monde sans outillage externe.

#include "Procedural/WorldseedPhotographe.h"

#include "Procedural/WorldseedPlateau.h"

#include "Procedural/WorldseedVoxelTerrain.h"
#include "Procedural/WorldseedUdsBridge.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * ON ATTEND QUE LE MONDE SOIT BATI, ON NE COMPTE PLUS LES SECONDES.
	 *
	 * Un delai fixe de six secondes suffisait a 250 m de rayon ; a 1200 m, avec
	 * deux mille chunks a batir, il ne suffit plus et la vue sort a moitie
	 * faite. C'est la faute que le banc avait deja commise -- sa premiere
	 * version rendait EXACTEMENT 552 chunks a 250 m comme a 400, donc un
	 * transitoire identique des deux cotes.
	 */
	/** Duree pendant laquelle la diffusion doit rester FIGEE avant de tirer. */
	constexpr float StableRequiseS = 2.0f;

	/** Au-dela, on tire quand meme et l'on DIT que la vue est partielle. */
	constexpr float PlafondAttenteS = 180.0f;

	/** Souffler apres la prise, le temps que la capture soit ecrite. */
	constexpr float ApresPhotoS = 7.2f;
}

bool UWorldseedPhotographe::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// PIE et jeu seulement : rien a photographier dans un monde d'editeur, et
	// surtout rien a piloter -- il n'y a pas de pion.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UWorldseedPhotographe::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWorldseedPhotographe, STATGROUP_Tickables);
}

AWorldseedVoxelTerrain* UWorldseedPhotographe::Terrain() const
{
	UWorld* const W = GetWorld();
	if (!W) { return nullptr; }
	for (TActorIterator<AWorldseedVoxelTerrain> It(W); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void UWorldseedPhotographe::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// --- LA TOURNEE SE DECLENCHE EN LIGNE DE COMMANDE ------------------------
	//
	// PAS PAR UN APPEL EXTERNE, ET C'EST TOUT L'INTERET. Piloter la prise de
	// vue depuis l'exterieur suppose un lien d'outillage vivant ; il tombe des
	// que l'editeur est tue et relance plusieurs fois, donc a chaque
	// compilation, et l'on redevient alors aveugle. Lue au demarrage, l'option
	// marche dans tous les cas, y compris en build final.
	//
	//     -game -WorldseedPhotos -WorldseedQuitter
	if (!FParse::Param(FCommandLine::Get(), TEXT("WorldseedPhotos")))
	{
		return;
	}

	bQuitterEnsuite = FParse::Param(FCommandLine::Get(), TEXT("WorldseedQuitter"));
	bArme = true;

	// ON ARME ICI, ON NE CONSTRUIT PAS. Le sous-systeme recoit son
	// OnWorldBeginPlay AVANT que les acteurs recoivent le leur : le terrain
	// n'existe pas encore, et surtout son monde n'est pas charge -- ce qui
	// prend une minute a la premiere generation. Construire la tournee ici
	// donnait zero vue ET AUCUN JOURNAL, parce que tout sortait sur le premier
	// test de validite. On attend donc le premier tick ou le monde est la.
}

void UWorldseedPhotographe::MidiFige()
{
	// UNE TOURNEE LONGUE TOMBE DANS LA NUIT, et alors elle ne prouve plus rien.
	// L horloge d UDS avance d une unite par 1,125 s reelle : vingt-sept arrets
	// a trente-six secondes font seize minutes, soit HUIT HEURES de jeu. La
	// moitie des vues sortait donc en bleu nuit -- et le depot a deja une regle
	// pour cela, « juger la couleur EN PLEIN JOUR », qu il avait fallu apprendre
	// sur un mur d arbustes qui rendait noir sous la pluie.
	//
	// On REECRIT l heure avant chaque prise plutot que d arreter l animation :
	// une ecriture rate proprement si le pack n est pas la, alors qu arreter
	// l horloge laisserait le monde fige pour la suite de la partie.
	if (!bUdsResolu)
	{
		bUdsResolu = true;
		Uds.Resolve(GetWorld());
		UE_LOG(LogTemp, Log, TEXT("[Worldseed] photo : ciel -- %s"),
			Uds.IsValid() ? *Uds.Describe() : TEXT("aucun UDS, heure non figee"));
	}
	if (Uds.IsValid())
	{
		Uds.WriteNumber(TEXT("Time of Day"), 1300.0);
	}
}

void UWorldseedPhotographe::Photographier(double XMetres, double YMetres,
	const FString& Nom)
{
	AWorldseedVoxelTerrain* const T = Terrain();
	if (!T) { return; }

	FWorldseedPhotoStop E;
	E.Nom = Nom.IsEmpty() ? TEXT("vue") : Nom;
	E.CibleM = FVector(XMetres, YMetres,
		T->MondeChamp().SurfaceHeightM(XMetres, YMetres));
	Tournee.Add(E);
	Demarrer();
}

bool UWorldseedPhotographe::AjouterLaVueLibre()
{
	// PLUSIEURS CAPS D'UN SEUL LANCEMENT, et ce n'est pas du confort. Une
	// relance coute le chargement du monde et le remplissage du terrain ; a
	// quatre caps depuis un meme point cela fait quatre fois ce prix pour
	// quatre images qui ne different que par une rotation. Et surtout, les
	// quatre sont alors prises dans le MEME etat du monde -- meme heure, meme
	// diffusion -- donc comparables entre elles.
	// LE QUATRIEME ARGUMENT EST INDISPENSABLE, et son defaut coute une
	// relance : `FParse::Value` s'arrete sur une VIRGULE quand
	// `bShouldStopOnSeparator` vaut vrai, ce qui est son defaut. Sans lui,
	// « 0,90,180,270 » arrive comme « 0 » -- une seule vue, sans un mot.
	FString Caps;
	if (!FParse::Value(FCommandLine::Get(), TEXT("WorldseedVue="), Caps, false)
		|| Caps.IsEmpty())
	{
		return false;
	}

	TArray<FString> Morceaux;
	Caps.ParseIntoArray(Morceaux, TEXT(","), true);
	if (Morceaux.Num() == 0) { return false; }

	const AWorldseedVoxelTerrain* const T = Terrain();
	if (!T) { return false; }

	float XM = 0.0f, YM = 0.0f, HauteurOeilM = 2.0f, TangageDeg = 0.0f;
	FParse::Value(FCommandLine::Get(), TEXT("WorldseedVueX="), XM);
	FParse::Value(FCommandLine::Get(), TEXT("WorldseedVueY="), YM);
	FParse::Value(FCommandLine::Get(), TEXT("WorldseedVueH="), HauteurOeilM);
	FParse::Value(FCommandLine::Get(), TEXT("WorldseedVueTangage="), TangageDeg);

	FString Nom;
	if (!FParse::Value(FCommandLine::Get(), TEXT("WorldseedVueNom="), Nom)
		|| Nom.IsEmpty())
	{
		Nom = TEXT("vue");
	}

	// ASSEZ LOIN POUR QUE LA VISEE SOIT UN CAP ET NON UN POINT. A quatre
	// kilometres, un ecart de placement d'un metre fait moins d'un centieme de
	// degre sur la direction regardee.
	constexpr double PorteeM = 4000.0;

	const double SolM = T->MondeChamp().SurfaceHeightM(XM, YM);
	const double OeilM = SolM + HauteurOeilM;

	// LE TANGAGE SE POSE EN DEPLACANT LA CIBLE, PAS LA CAMERA, parce que c'est
	// la cible qui donne la visee. Mais le placement, lui, lit
	// `Cible.Z + Hauteur` : on compense donc exactement, sans quoi une visee
	// plongeante poserait aussi la camera au ras du sol vise.
	const double Chute = PorteeM * FMath::Tan(FMath::DegreesToRadians(TangageDeg));

	for (const FString& M : Morceaux)
	{
		const float CapDeg = FCString::Atof(*M.TrimStartAndEnd());

		// LE CAP SUIT LA MEME CONVENTION QUE -WorldseedCap= : zero au NORD,
		// donc vers +Y, et quatre-vingt-dix a l'est. Elle est ecrite ici en
		// toutes lettres pour qu'on puisse la verifier plutot que la deduire.
		const double Cap = FMath::DegreesToRadians(static_cast<double>(CapDeg));
		const FVector2D Axe(FMath::Sin(Cap), FMath::Cos(Cap));

		FWorldseedPhotoStop E;
		E.Nom = Morceaux.Num() > 1
			? FString::Printf(TEXT("%s_cap%03d"), *Nom,
				FMath::RoundToInt(FRotator::ClampAxis(CapDeg)))
			: Nom;
		E.CibleM = FVector(XM + Axe.X * PorteeM, YM + Axe.Y * PorteeM, OeilM - Chute);
		E.DepuisM = -Axe;
		E.DistanceM = PorteeM;
		E.HauteurM = Chute;
		Tournee.Add(E);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] photo : VUE LIBRE « %s » a (%.0f, %.0f) m, sol %.0f m, ")
			TEXT("oeil %.0f m, cap %.0f deg, tangage %.1f deg"),
			*E.Nom, XM, YM, SolM, OeilM, CapDeg, TangageDeg);
	}

	return true;
}

int32 UWorldseedPhotographe::AjouterLesArches(int32 Combien)
{
	AWorldseedVoxelTerrain* const T = Terrain();
	if (!T) { return 0; }

	const FWorldseedCaveNetwork& Reseau = T->MondeGrottes();
	int32 Ajoutees = 0;

	for (int32 I = 0; I < Reseau.Arches.Num() && Ajoutees < Combien; ++I)
	{
		const FWorldseedCaveArch& A = Reseau.Arches[I];

		FWorldseedPhotoStop E;
		E.Nom = FString::Printf(TEXT("arche%02d"), I + 1);
		E.CibleM = A.CentreM;

		// ON REGARDE DANS L'AXE DU PERCEMENT, sans quoi on photographie une
		// paroi pleine et l'on conclut a tort que l'arche n'existe pas.
		E.DepuisM = A.TraversM.GetSafeNormal();

		// ET L'ON SE TIENT JUSTE EN SORTIE DU TUNNEL, PAS A DEUX CENTS METRES.
		//
		// La distance etait calee sur `EpaisseurM * 1,6`, c'est-a-dire sur la
		// largeur du CAP -- jusqu'a 200 m. A cette distance une ouverture de
		// trente metres ne fait plus que dix degres, et le jour qui passe au
		// bout d'un tunnel de quatre-vingts metres ne se distingue plus d'une
		// simple tache sombre dans une falaise. Le proprietaire a signale, sur
		// les photos, que « l'arche n'a plus l'air traversante » : la mesure dit
		// pourtant 22 posees, 22 TRAVERSANTES, 22 avec un pont. Ce n'etait donc
		// pas la geometrie, c'etait le CADRAGE.
		//
		// On se place a une soixantaine de metres de la bouche, soit la
		// demi-largeur du cap plus cette marge : l'ouverture remplit alors le
		// cadre et l'on voit, ou non, le jour au travers.
		E.DistanceM = FMath::Clamp(A.EpaisseurM * 0.5f + 60.0f, 70.0f, 130.0f);

		// A LA HAUTEUR DE L'AXE, ET NON DEUX METRES AU-DESSUS. Deux metres sur
		// une ouverture qui descend sous le niveau de la mer suffisent a viser
		// la voute plutot que le passage.
		E.HauteurM = 0.0f;
		Tournee.Add(E);

		// ET UNE SECONDE VUE, DEPUIS L'INTERIEUR DU TUNNEL.
		//
		// C'est la seule qui puisse etablir la TRAVERSEE par l'image. De
		// l'exterieur, une ouverture de trente metres au bout de quatre-vingts
		// metres de roche reste sombre meme quand elle debouche : le jour
		// arrive par une sortie qu'on ne voit pas sous cet angle. Le
		// proprietaire a signale exactement cela -- « j'ai l'impression que
		// l'arche n'est plus traversante » -- alors que la mesure dit 22 posees,
		// 22 TRAVERSANTES.
		//
		// Place a quinze metres du centre et visant le centre, on regarde dans
		// l'axe depuis le milieu du passage : la sortie opposee est alors droit
		// devant, et soit on voit le jour, soit il n'y en a pas.
		FWorldseedPhotoStop D = E;
		D.Nom = FString::Printf(TEXT("arche%02d_dedans"), I + 1);
		D.DepuisM = -E.DepuisM;
		D.DistanceM = 15.0f;
		Tournee.Add(D);

		++Ajoutees;
	}

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] photo : %d arches a la tournee"), Ajoutees);
	return Ajoutees;
}

int32 UWorldseedPhotographe::AjouterLesTables(int32 Combien)
{
	AWorldseedVoxelTerrain* const T = Terrain();
	if (!T) { return 0; }

	const TArray<FWorldseedPlateauSite>& Sites = T->MondeTables();
	int32 Ajoutees = 0;

	for (int32 I = 0; I < Sites.Num() && Ajoutees < Combien; ++I)
	{
		const FWorldseedPlateauSite& S = Sites[I];

		FWorldseedPhotoStop E;
		E.Nom = FString::Printf(TEXT("table%02d"), I + 1);
		E.CibleM = FVector(S.CentreM.X, S.CentreM.Y, S.AltitudeM);

		// LA DISTANCE EST BORNEE PAR LE TERRAIN, PAS PAR LE CADRAGE, et c'est
		// une contrainte dure. Le voxel n'existe que dans le rayon de
		// chargement -- 250 m -- et au-dela on photographie le SOL DE FOND, a
		// 63 m par maille : des formes lisses et arrondies ou l'on croit voir
		// un relief mou alors qu'on ne voit pas le relief du tout. Le depot a
		// deja perdu une heure sur ce piege avec des vues a 420-580 m.
		//
		// CONSEQUENCE ASSUMEE : on ne photographie PAS la silhouette entiere
		// d'une table de plus d'un kilometre. On cadre sa PAROI et son rebord
		// contre le ciel, ce qui reste le controle diagnostique -- un mur
		// vertical surmonte d'un trait horizontal.
		// LA FENETRE DE PRISE DE VUE EST ETROITE, ET ELLE SE CALCULE.
		//
		// Les chunks se batissent autour du JOUEUR, dans un rayon de 250 m. Ce
		// qu'on photographie doit donc etre a moins de 250 m de LUI, sans quoi
		// l'on cadre le sol de fond a 63 m par maille -- des formes lisses et
		// arrondies ou l'on croit voir un relief mou alors qu'on ne voit pas le
		// relief du tout.
		//
		// Soit D la distance au centre de la butte, dont le rayon vaut environ
		// `tables.porteeM`. Il faut D > rayon pour etre DESCENDU de la butte, et
		// D - rayon < 250 pour que le rebord soit en voxel. A 250 m de rayon, la
		// fenetre utile va donc de 250 a 500 m, et 350 la place au milieu.
		//
		// LA PREMIERE VERSION CADRAIT A 200 m ET N'A RIEN MONTRE : sur une table
		// de 1,8 km, la camera etait encore DESSUS, et les cinq vues ont rendu
		// une plaine. C'est ce qui a fait ramener les tables a l'echelle d'une
		// BUTTE -- la forme doit tenir dans la distance de vue, sinon elle
		// existe dans la donnee et pas pour le joueur.
		// LA FENETRE S'EST OUVERTE. Tant que le rayon valait 250 m, on ne
		// pouvait cadrer qu'une PAROI : au-dela c'etait le sol de fond. A
		// 600 m, une mesa entiere tient dans la vue en voxel, et c'est la
		// SILHOUETTE qui fait lire la forme -- un trait horizontal pose sur
		// un socle.
		// --- LA DIRECTION SE LIT SUR LE SITE, ELLE NE SE DEVINE PAS ---------
		//
		// IL Y AVAIT ICI UNE CONSTANTE, `FVector2D(0.82, 0.57)` -- un cap fixe,
		// le meme pour toutes les tables du monde. Les trois autres familles
		// calculent la leur : l'arche suit son axe de percement, le canyon et
		// la falaise marine suivent `VersLeBas`. Les tables etaient la SEULE
		// exception, et elles sont exactement la seule famille dont la vue ne
		// montrait rien.
		//
		// MESURE : cible annoncee « sommet 222 m, paroi 200 m », image obtenue
		// une pente de dune lisse, AUCUNE paroi. Le cap fixe posait la camera
		// du mauvais cote ; la remontee qui sort de la roche en MONTANT
		// l'amenait alors au sommet du plateau, et l'on photographiait le
		// dessus de la table en croyant cadrer sa paroi.
		//
		// LE REGISTRE ATTRIBUAIT CE MANQUE A LA DISTANCE DE VUE -- « a 250 m de
		// rayon, une table de plus d'un kilometre ne tient pas dans une vue ».
		// C'etait une explication plausible, et elle etait FAUSSE : la donnee
		// pour viser juste existait depuis le debut, dans le champ dont le
		// commentaire dit lui-meme « ON SE PLACE DU COTE BAS, sinon on
		// photographie le plateau et la paroi est DERRIERE la camera ».
		E.DepuisM = S.VersLeBas.IsNearlyZero()
			? FVector2D(0.82, 0.57) : S.VersLeBas;
		E.DistanceM = 520.0f;

		// Au PIED de la paroi, le regard vers le haut : c'est la seule position
		// d'ou une butte se lit -- un mur vertical surmonte d'un trait
		// horizontal, contre le ciel.
		E.HauteurM = -FMath::Max(S.EscarpementM - 40.0f, 30.0f);
		Tournee.Add(E);
		++Ajoutees;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] photo : %d tables a la tournee (sur %d sites)"),
		Ajoutees, Sites.Num());
	return Ajoutees;
}

int32 UWorldseedPhotographe::AjouterLesCanyons(int32 Combien)
{
	AWorldseedVoxelTerrain* const T = Terrain();
	if (!T) { return 0; }

	const TArray<FWorldseedPlateauSite>& Sites = T->MondeCanyons();
	int32 Ajoutees = 0;

	for (int32 I = 0; I < Sites.Num() && Ajoutees < Combien; ++I)
	{
		const FWorldseedPlateauSite& S = Sites[I];

		FWorldseedPhotoStop E;
		E.Nom = FString::Printf(TEXT("canyon%02d"), I + 1);
		E.CibleM = FVector(S.CentreM.X, S.CentreM.Y, S.AltitudeM);

		// LE MEME CADRAGE QUE LES FALAISES MARINES, qui sont les seules vues de
		// la tournee a montrer une vraie paroi. On vise la MI-HAUTEUR de la
		// chute et l'on se place DU COTE BAS -- sans quoi on photographie le
		// plateau et la paroi est derriere la camera. La distance suit la
		// hauteur, bornee par le rayon de chargement.
		E.CibleM.Z = S.AltitudeM - 0.5f * S.EscarpementM;
		E.DepuisM = S.VersLeBas.IsNearlyZero()
			? FVector2D(-0.57, 0.82) : S.VersLeBas;
		E.DistanceM = FMath::Clamp(S.EscarpementM * 2.2f, 90.0f, 170.0f);
		E.HauteurM = 10.0f;
		Tournee.Add(E);

		// --- ET UNE VUE LARGE, QUI N'ETAIT PAS POSSIBLE JUSQU'ICI -----------
		//
		// LE DEPOT PORTE CE MANQUE EN TOUTES LETTRES : « la silhouette entiere
		// n'a pas ete vue [...] a 250 m de rayon de chargement, une table de
		// plus d'un kilometre ne tient pas dans une vue ». Ce qui etait
		// verifie est la PAROI ; ce qui ne l'etait pas est la forme -- une
		// gorge se reconnait a ses DEUX rebords et a ce qu'il y a entre eux,
		// pas a un morceau de mur vu de pres.
		//
		// Les anneaux de resolution ont leve la contrainte : 1200 m de vue
		// pour un quart des chunks d'un rayon uniforme de 600. On se place
		// donc assez loin pour que les deux levres tiennent dans le cadre, et
		// assez HAUT pour plonger dedans -- une gorge vue de plain-pied se lit
		// comme une simple falaise.
		//
		// La cible est le REBORD et non la mi-paroi : c'est le niveau du
		// plateau qui donne l'echelle de l'entaille.
		FWorldseedPhotoStop L;
		L.Nom = FString::Printf(TEXT("canyon%02d_large"), I + 1);
		L.CibleM = FVector(S.CentreM.X, S.CentreM.Y, S.AltitudeM);
		L.DepuisM = E.DepuisM;
		L.DistanceM = FMath::Clamp(S.EscarpementM * 5.0f, 400.0f, 800.0f);

		// Environ vingt-cinq degres de plongee. Assez pour voir le fond, pas
		// assez pour que la vue devienne une carte -- le depot a deja paye la
		// vue zenithale, ou un quad pose a plat est indiscernable du sol.
		L.HauteurM = 0.45f * L.DistanceM;
		Tournee.Add(L);
		++Ajoutees;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] photo : %d parois a la tournee (sur %d sites)"),
		Ajoutees, Sites.Num());
	return Ajoutees;
}

int32 UWorldseedPhotographe::AjouterLesFalaises(int32 Combien)
{
	AWorldseedVoxelTerrain* const T = Terrain();
	if (!T) { return 0; }

	const FWorldseedGeometry& Geo = T->MondeGeometrie();
	const TArray<float>& H = T->MondeAltitudes();
	if (H.Num() != Geo.CellCount()) { return 0; }

	const int32 NX = Geo.NX;
	const int32 NY = Geo.NY;
	const double MailleM = FMath::Max(Geo.MetersPerPixel(), 1e-3f);

	// --- LES PLUS HAUTES, ET ELLES DOIVENT ETRE PRES DE LA MER ---------------
	//
	// Un ressaut au fond d'une vallee n'est pas une falaise littorale : ce
	// qu'on veut juger est la paroi qui tombe DANS l'eau, parce que c'est elle
	// que la passe littorale a creee et elle qui porte les arches marines.
	struct FCandidat
	{
		int32 Cellule = 0;
		float Chute = 0.0f;
		FVector2D VersLeBas = FVector2D::ZeroVector;
	};
	TArray<FCandidat> Candidats;

	for (int32 J = 2; J < NY - 2; ++J)
	{
		for (int32 I = 2; I < NX - 2; ++I)
		{
			const int32 C = J * NX + I;
			if (H[C] <= 5.0f) { continue; }

			float Chute = 0.0f;
			FIntPoint Vers = FIntPoint::ZeroValue;
			bool bMerProche = false;

			for (int32 DJ = -2; DJ <= 2; ++DJ)
			{
				for (int32 DI = -2; DI <= 2; ++DI)
				{
					const int32 V = (J + DJ) * NX + (I + DI);
					if (H[V] <= 0.0f) { bMerProche = true; }
					const float D = H[C] - H[V];
					if (D > Chute) { Chute = D; Vers = FIntPoint(DI, DJ); }
				}
			}

			if (!bMerProche || Chute < 25.0f) { continue; }

			FCandidat K;
			K.Cellule = C;
			K.Chute = Chute;
			K.VersLeBas = FVector2D(Vers.X, Vers.Y).GetSafeNormal();
			Candidats.Add(K);
		}
	}

	Candidats.Sort([](const FCandidat& A, const FCandidat& B)
	{
		return A.Chute > B.Chute;
	});

	int32 Ajoutees = 0;
	TArray<FVector2D> Prises;
	for (const FCandidat& K : Candidats)
	{
		if (Ajoutees >= Combien) { break; }

		const double X = (static_cast<double>(K.Cellule % NX) / NX - 0.5) * Geo.WidthM();
		const double Y = (static_cast<double>(K.Cellule / NX) / NY - 0.5) * Geo.HeightM;

		// Deux falaises voisines sont la MEME falaise.
		bool bVoisine = false;
		for (const FVector2D& P : Prises)
		{
			if (FVector2D::DistSquared(FVector2D(X, Y), P) < 3000.0 * 3000.0)
			{
				bVoisine = true;
				break;
			}
		}
		if (bVoisine) { continue; }
		Prises.Emplace(X, Y);

		FWorldseedPhotoStop E;
		E.Nom = FString::Printf(TEXT("falaise%02d"), Ajoutees + 1);
		E.CibleM = FVector(X, Y, H[K.Cellule] * 0.5);

		// ON SE MET DU COTE DE LA MER, sinon on photographie le plateau et la
		// paroi est DERRIERE la camera.
		E.DepuisM = K.VersLeBas;

		// DANS LE RAYON DE CHARGEMENT, SINON ON PHOTOGRAPHIE LE SOL DE FOND.
		// Les chunks ne se batissent que dans 250 m autour du pion ; au-dela
		// c'est la nappe d'horizon qu'on voit, qui fait 63 m par maille a
		// 64 km. Une heure perdue sur ce defaut, faute d'avoir verifie ce que
		// le cadre contenait.
		E.DistanceM = FMath::Clamp(K.Chute * 2.2f, 90.0f, 170.0f);
		E.HauteurM = 10.0f;
		Tournee.Add(E);

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] photo : falaise%02d a (%.0f, %.0f) m, sommet %.0f m, ")
			TEXT("chute %.0f m sur %.0f m"),
			Ajoutees + 1, X, Y, H[K.Cellule], K.Chute, MailleM * 2.0);
		++Ajoutees;
	}
	return Ajoutees;
}

void UWorldseedPhotographe::Demarrer()
{
	if (Tournee.Num() == 0 || EnCours()) { return; }

	// --- ON NE PHOTOGRAPHIE QUE CE QU'ON JUGE -------------------------------
	//
	// Demande du proprietaire pendant le diagnostic du noir des parois : « ce
	// n'est pas la peine pendant les tests de refaire une seance photos de
	// tout, sur les canyons ca se voit tellement qu'ils sont suffisants ».
	//
	// ET LE COUT N'EST PAS CELUI QU'ON CROIT. Un arret ne coute pas une
	// capture : il coute un REMPLISSAGE COMPLET du monde autour de sa
	// position, puisque le diffuseur relache tout ce qui sort du rayon. Onze
	// arrets, c'est onze remplissages -- une dizaine de minutes a resolution
	// uniforme. Deux arrets rendent l'A/B abordable, donc REPETABLE, et c'est
	// ce qui compte : ce depot a deja conclu sur des moities d'A/B qu'il
	// n'avait pas les moyens de refaire.
	{
		FString Filtre;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedArrets="),
				Filtre, false) && !Filtre.IsEmpty())
		{
			TArray<FString> Motifs;
			Filtre.ParseIntoArray(Motifs, TEXT(","), true);

			const int32 Avant = Tournee.Num();
			Tournee.RemoveAll([&Motifs](const FWorldseedPhotoStop& S)
			{
				for (const FString& M : Motifs)
				{
					if (S.Nom.Contains(M.TrimStartAndEnd())) { return false; }
				}
				return true;
			});

			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] photo : %d arrets retenus sur %d par « %s »"),
				Tournee.Num(), Avant, *Filtre);
		}
	}

	// UN FILTRE QUI NE GARDE RIEN DOIT LE DIRE. Sans cette ligne, une faute de
	// frappe dans le motif rendrait une tournee vide et un journal muet -- et
	// l'on chercherait le defaut dans la generation.
	if (Tournee.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] photo : aucun arret ne correspond au filtre, ")
			TEXT("tournee annulee"));
		return;
	}

	Etape = 0;
	Attente = 0;
	Horloge = 0.0f;
	DernierCompte = -1;
	StableS = 0.0f;

	AWorldseedVoxelTerrain* const T = Terrain();
	if (!T) { return; }

	const FWorldseedPhotoStop& E = Tournee[0];
	MidiFige();

	T->TeleporterJoueur(E.CibleM.X + E.DepuisM.X * E.DistanceM,
		E.CibleM.Y + E.DepuisM.Y * E.DistanceM);

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] photo : tournee de %d vues"), Tournee.Num());
}

void UWorldseedPhotographe::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bArme)
	{
		const AWorldseedVoxelTerrain* const T = Terrain();
		if (!T || T->MondeAltitudes().Num() == 0)
		{
			// Le monde n'est pas encore charge : on repassera.
			return;
		}

		bArme = false;

		// Les falaises d'abord -- c'est ce que la passe littorale cree, et la
		// condition des arches marines. Les arches ensuite.
		// DEUX DE CHAQUE, demande du proprietaire. Une tournee courte se
		// relit d'un coup d'oeil et, surtout, elle reste DANS LA JOURNEE :
		// vingt-sept arrets faisaient huit heures de jeu et la moitie des
		// vues sortait de nuit. L'heure est figee par ailleurs, mais une
		// tournee breve coute de toute facon moins cher a relancer.
		// UNE VUE DESIGNEE REMPLACE LA TOURNEE, elle ne s'y ajoute pas : on la
		// demande pour regarder UN point precis, et enchainer huit formes
		// derriere ferait perdre le cadrage qu'on vient de choisir.
		if (!AjouterLaVueLibre())
		{
			constexpr int32 ParForme = 2;
			AjouterLesFalaises(ParForme);
			AjouterLesArches(ParForme);
			AjouterLesTables(ParForme);
			AjouterLesCanyons(ParForme);
		}
		Demarrer();
	}

	if (!EnCours()) { return; }
	Horloge += DeltaTime;
	Avancer(DeltaTime);
}

void UWorldseedPhotographe::Avancer(float DeltaTime)
{
	AWorldseedVoxelTerrain* const T = Terrain();
	UWorld* const W = GetWorld();
	APawn* const Pion = W ? UGameplayStatics::GetPlayerPawn(W, 0) : nullptr;
	APlayerController* const PC = W ? UGameplayStatics::GetPlayerController(W, 0) : nullptr;
	if (!T || !Pion || !PC) { return; }

	const FWorldseedPhotoStop& E = Tournee[Etape];

	// ON VISE A CHAQUE PASSE, PAS UNE SEULE FOIS. Le pion pivote quand il
	// retombe sur le sol, et une orientation posee avant l'atterrissage est
	// perdue sans le moindre signe.
	const FVector CibleCm = T->GetActorLocation()
		+ FVector(E.CibleM.X, E.CibleM.Y, E.CibleM.Z) * WorldseedMetersToCm;
	FVector OeilCm = Pion->GetActorLocation();
	if (const APlayerCameraManager* const Cam = PC->PlayerCameraManager)
	{
		OeilCm = Cam->GetCameraLocation();
	}
	PC->SetControlRotation((CibleCm - OeilCm).Rotation());

	// LE POINT DE VUE SE TIENT A LA HAUTEUR DE LA CIBLE, PAS A CELLE DU SOL.
	// Cale sur le sol local, la camera visait cinquante-cinq degres vers le bas
	// et photographiait le dos du personnage -- l'arche etait a 119 m et le sol
	// du point de vue a 239. On part de l'altitude de la cible et l'on ne
	// remonte que si l'on se trouve DANS la roche, ce que le champ sait dire.
	if (UCharacterMovementComponent* const Move =
		Pion->FindComponentByClass<UCharacterMovementComponent>())
	{
		if (T->JoueurPose())
		{
			Move->SetMovementMode(MOVE_Flying);
			Move->Velocity = FVector::ZeroVector;

			const FVector P = Pion->GetActorLocation() - T->GetActorLocation();
			const double VX = P.X / WorldseedMetersToCm;
			const double VY = P.Y / WorldseedMetersToCm;

			FWorldseedCaveLocal Local;
			T->MondeGrottes().Query(
				FBox(FVector(VX - 30.0, VY - 30.0, E.CibleM.Z - 20.0),
					FVector(VX + 30.0, VY + 30.0, E.CibleM.Z + 240.0)), Local);

			// JAMAIS SOUS LE NIVEAU DE LA MER. La remontee qui suit sort de la
			// ROCHE, elle ne sait rien de l EAU -- l ocean est un plan a
			// l altitude zero, etranger au champ de densite. Une vue de table
			// est sortie entierement bleue, camera NOYEE, parce que le pied
			// calcule tombait a -45 m.
			double ZM = FMath::Max(E.CibleM.Z + E.HauteurM, 3.0);
			while (ZM < E.CibleM.Z + 220.0
				&& T->MondeChamp().At(FVector(VX, VY, ZM), &Local) <= 0.0)
			{
				ZM += 2.0;
			}

			FVector Pose = Pion->GetActorLocation();
			Pose.Z = T->GetActorLocation().Z + (ZM + 2.0) * WorldseedMetersToCm;
			Pion->SetActorLocation(Pose, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	// Le sol du point de vue n'est pas encore solide : on attend, et ce temps
	// ne compte pas -- sinon on declenche avant que le monde existe.
	if (!T->JoueurPose())
	{
		Horloge = 0.0f;
		return;
	}

	// LAISSER LE MONDE SE BATIR AVANT DE TIRER. Les chunks arrivent par travaux
	// asynchrones ; une photo prise des l'arrivee montre un paysage troue, et
	// l'on croit a un defaut de generation.
	//
	// ON ATTEND LA STABILISATION, PLUS UN DELAI FIXE -- signale par le
	// proprietaire : « tu prends tes photos toutes les six secondes, pourquoi
	// ne pas attendre que le monde soit genere completement ». C'est la meme
	// faute que le banc avait commise, et qu'on lui avait corrigee : sa
	// premiere version chauffait un nombre FIXE de secondes et rendait
	// EXACTEMENT 552 chunks a 250 m comme a 400 -- un transitoire identique des
	// deux cotes. Six secondes suffisaient a 250 m de rayon ; a 1200 m, avec
	// deux mille chunks a batir, elles ne suffisent plus du tout.
	//
	// Le critere est celui du banc, et il vient du MEME endroit : compte de
	// chunks fige et aucun travail en vol.
	const bool bFige = T->DiffusionStable(DernierCompte);
	StableS = bFige ? (StableS + DeltaTime) : 0.0f;

	const bool bPret = (StableS >= StableRequiseS);
	const bool bTropLong = (Horloge >= PlafondAttenteS);

	if (Attente == 0 && bTropLong && !bPret)
	{
		// ON DIT QU'ON TIRE SUR UN TRANSITOIRE PLUTOT QUE DE LE TAIRE. Une
		// photo partielle qu'on croit complete envoie chercher un defaut de
		// generation la ou il n'y en a pas.
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] photo : %s -- diffusion NON stabilisee apres %.0f s ")
			TEXT("(%d chunks, %d en vol), la vue sera partielle"),
			*E.Nom, PlafondAttenteS, T->NombreDeChunks(), T->TravauxEnVol());
	}

	if (Attente == 0 && (bPret || bTropLong))
	{
		const FString Fichier = FPaths::Combine(
			FPaths::ProjectSavedDir(), TEXT("Photos"), E.Nom + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Fichier, false, false);
		Attente = 1;

		// L'HORLOGE REPART AU TIR, PAS A L'ARRIVEE. Elle a servi jusqu'ici de
		// plafond d'attente ; si la stabilisation a pris trente secondes, elle
		// depasse deja ApresPhotoS et l'on avancerait AVANT que la capture --
		// qui est asynchrone -- soit ecrite sur le disque.
		Horloge = 0.0f;

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] photo : %s -- cible (%.0f, %.0f, %.0f) m, ")
			TEXT("depuis %.0f m dans l'axe"),
			*E.Nom, E.CibleM.X, E.CibleM.Y, E.CibleM.Z, E.DistanceM);
	}

	// ON N'AVANCE PAS TANT QU'ON N'A PAS TIRE, ET C'EST LA MOITIE QUI MANQUAIT.
	//
	// Premiere version de cette correction : le TIR attendait la stabilisation,
	// mais l'AVANCEMENT restait sur l'horloge. Un arret dont le monde n'etait
	// pas bati en sept secondes passait donc au suivant SANS PHOTO -- et le
	// releve le dit sans ambiguite : tournee de dix vues, DEUX fichiers ecrits,
	// les huit premiers arrets traverses en quatre-vingts secondes. Le
	// proprietaire voyait la camera sauter toutes les sept secondes et a
	// signale « toujours 6 s » : ce n'etait pas le delai d'avant qui avait
	// survecu, c'etait celui d'APRES qui gouvernait tout.
	//
	// Conditionner une moitie d'une boucle et pas l'autre ne corrige rien : ca
	// deplace le symptome.
	if (Attente == 0) { return; }
	if (Horloge < ApresPhotoS) { return; }

	++Etape;
	Attente = 0;
	Horloge = 0.0f;
	DernierCompte = -1;
	StableS = 0.0f;

	if (EnCours())
	{
		const FWorldseedPhotoStop& S = Tournee[Etape];
		T->TeleporterJoueur(S.CibleM.X + S.DepuisM.X * S.DistanceM,
			S.CibleM.Y + S.DepuisM.Y * S.DistanceM);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] photo : tournee terminee"));

	// --- ET LA TOURNEE DIT CE QU'ELLE A PHOTOGRAPHIE ------------------------
	//
	// Le releve du terrain n'existait que dans le BANC, jamais ici -- et c'est
	// exactement ce qui a fait mesurer l'entonnoir de la teinte de roche au
	// mauvais endroit : au point d'apparition du banc, une plaine cotiere a
	// 4,9 m ou la serie stratigraphique (267 a 520 m) ne monte jamais. Le
	// releve annoncait « bancs touches AUCUN » pour un monde qui en touche
	// dix, et c'est la sonde qui avait tort.
	//
	// Une tournee qui ne dit pas ce qu'elle a peint n'est qu'une moitie de
	// mesure : les captures montrent, le releve chiffre, et l'un sans l'autre
	// laisse deviner.
	if (const AWorldseedVoxelTerrain* const Sol = Terrain())
	{
		UE_LOG(LogTemp, Log, TEXT("[Worldseed] photo : %s"), *Sol->ReportState());
	}
	Etape = INDEX_NONE;
	if (bQuitterEnsuite)
	{
		FPlatformMisc::RequestExit(false);
	}
}
