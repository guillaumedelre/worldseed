// Worldseed - le RESEAU de grottes : chambres, liaisons, et connexite garantie.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedFins.h"

struct FWorldseedLithology;
struct FWorldseedLithologyRules;

/** Une salle. */
struct WORLDSEED_API FWorldseedCaveChamber
{
	/** Centre, en METRES dans le repere du terrain. */
	FVector CentreM = FVector::ZeroVector;
	float RadiusM = 8.0f;
};

/**
 * Une galerie, sous forme de capsule a rayon variable.
 *
 * Pourquoi une capsule et pas un tube maille : c'est une primitive de distance
 * signee, donc elle se combine par une union lisse et se derive proprement. Un
 * tube maille demanderait de le coudre au terrain, ce qui est exactement le
 * travail que le marching cubes fait deja pour nous.
 */
struct WORLDSEED_API FWorldseedCaveSegment
{
	FVector AM = FVector::ZeroVector;
	FVector BM = FVector::ZeroVector;
	float RadiusAM = 2.0f;
	float RadiusBM = 2.0f;
};

/**
 * Les primitives qui touchent un chunk donne.
 *
 * TESTER TOUTES LES PRIMITIVES A CHAQUE VOXEL EST REDHIBITOIRE : c'est du
 * O(voxels x primitives), et un monde en porte des milliers. On extrait donc
 * UNE FOIS par chunk la liste de celles dont l'emprise le touche, et le champ
 * n'evalue que celles-la. On retombe a quelques dizaines.
 */
struct WORLDSEED_API FWorldseedCaveLocal
{
	const struct FWorldseedCaveNetwork* Network = nullptr;
	TArray<int32> Chambers;
	TArray<int32> Segments;

	bool IsEmpty() const { return Chambers.Num() == 0 && Segments.Num() == 0; }
};

/**
 * Le reseau, et son index spatial.
 *
 * LA CONNEXITE EST ACQUISE PAR CONSTRUCTION, PAS VERIFIEE APRES COUP. C'est
 * tout l'objet de cette passe : du bruit 3D produit des cavernes credibles,
 * mais rien n'assure qu'elles communiquent, ni qu'une seule debouche a l'air
 * libre. Les chambres sont semees, puis reliees par un ARBRE COUVRANT MINIMAL
 * -- qui touche tous les sommets par definition -- et on ajoute ensuite
 * quelques aretes courtes pour faire des boucles, sans quoi le joueur revient
 * toujours sur ses pas.
 */
/**
 * Une arche posee : ou elle est, et par ou elle traverse.
 *
 * CONSERVEE PARCE QU'UNE FORME QU'ON NE SAIT PAS RETROUVER N'EXISTE PAS. Les
 * segments seuls decrivent la geometrie mais ne disent pas laquelle est une
 * arche ; sans cette liste il n'y a aucun moyen de verifier que le trou
 * TRAVERSE, ni d'y envoyer le joueur, ni d'y accrocher du butin. C'est
 * l'arbitrage B8 du proprietaire : la sortie de la passe macro est
 * interrogeable au runtime.
 */
struct WORLDSEED_API FWorldseedCaveArch
{
	/** Centre de l'ouverture, en metres. */
	FVector CentreM = FVector::ZeroVector;

	/** Direction du percement, EN TRAVERS de la lame. Normalisee. */
	FVector2D TraversM = FVector2D(1.0, 0.0);

	float EpaisseurM = 0.0f;
	float RayonM = 0.0f;
	float PontM = 0.0f;
};

struct WORLDSEED_API FWorldseedCaveNetwork
{
	TArray<FWorldseedCaveChamber> Chambers;
	TArray<FWorldseedCaveSegment> Segments;

	/** Les arches, pour qu'on puisse les retrouver et les verifier. */
	TArray<FWorldseedCaveArch> Arches;

	/** Index spatial : une grille reguliere en XY, la bande etant mince. */
	float CellM = 64.0f;
	FIntPoint Min = FIntPoint::ZeroValue;
	FIntPoint Size = FIntPoint::ZeroValue;
	TArray<TArray<int32>> ChamberBuckets;
	TArray<TArray<int32>> SegmentBuckets;

	bool IsValid() const { return Chambers.Num() > 0; }

	/** Les primitives dont l'emprise touche cette boite. */
	void Query(const FBox& BoxM, FWorldseedCaveLocal& Out) const;

	void Reset();
};

/** Section "cavites" de world_rules.json. */
struct WORLDSEED_API FWorldseedCaveRules
{
	/** A zero, aucun reseau n'est bati et le monde retrouve son etat d'avant. */
	float ChamberSpacingM = 180.0f;

	float DepthMinM = 20.0f;
	float DepthMaxM = 90.0f;
	float ChamberRadiusMinM = 6.0f;
	float ChamberRadiusMaxM = 22.0f;

	/** Cumul annuel minimal : un karst se creuse par DISSOLUTION, il faut de l'eau. */
	float MinPrecipMm = 600.0f;

	/** Aptitude a la dissolution minimale de la roche, dans [0..1]. */
	float MinKarstifiable = 0.5f;

	float TunnelRadiusMinM = 1.5f;
	float TunnelRadiusMaxM = 4.0f;

	/** Part d'aretes courtes ajoutees a l'arbre, pour faire des boucles. */
	float LoopPct = 15.0f;

	/** Voisins candidats par chambre, pour batir le graphe avant l'arbre. */
	int32 Neighbours = 8;

	// --- routage des galeries ------------------------------------------------

	/**
	 * Cote d'une cellule de routage, en metres.
	 *
	 * L'A* ne tourne PAS a la resolution du voxel : une grille grossiere suffit
	 * a decider d'un itineraire, et son cout croit au cube. La galerie reelle
	 * est ensuite une chaine de capsules posee sur le chemin trouve.
	 */
	float RouteCellM = 6.0f;

	/**
	 * Demi-largeur du couloir de recherche autour de la droite, en metres.
	 *
	 * L'A* est borne a ce couloir : sans lui, la grille couvrirait la boite
	 * englobante des deux chambres et le cout exploserait pour un detour que
	 * personne ne veut. Avec, il reste de quoi contourner un obstacle.
	 */
	float RouteCorridorM = 48.0f;

	/**
	 * Profondeur sous laquelle une galerie cesse d'etre penalisee, en metres.
	 *
	 * C'EST LA REGLE QUI EMPECHE DE PERCER LE SOL. Une capsule droite entre une
	 * chambre peu profonde et une chambre profonde peut ressortir a l'air libre ;
	 * un cout eleve pres de la surface fait plonger l'itineraire.
	 */
	float RouteSurfaceM = 25.0f;

	/** Penalite par metre de denivele : c'est elle qui rend la galerie praticable. */
	float RouteSlopeCost = 1.6f;

	/** Surcout de la roche dure, rapporte a son aptitude a la dissolution. */
	float RouteRockCost = 2.0f;

	/**
	 * Remise accordee a une cellule deja empruntee, dans [0..1].
	 *
	 * Elle mutualise les galeries : deux liaisons voisines empruntent un tronc
	 * commun au lieu de creuser deux tubes paralleles. C'est ce qui fait la
	 * difference entre un reseau et un plat de spaghettis.
	 */
	float RouteShareBonus = 0.45f;

	/** Tolerance de simplification du chemin, en metres. */
	float RouteSimplifyM = 3.0f;

	/**
	 * Plafond de noeuds explores par liaison.
	 *
	 * Il protege d'une recherche pathologique, au prix d'un repli. Le distinguer
	 * de l'echec "sans issue" est essentiel : l'un se corrige en relevant ce
	 * plafond, l'autre en relachant une contrainte, et confondre les deux fait
	 * regler le mauvais bouton.
	 */
	int32 RouteNodeCap = 150000;

	// --- entrees ---------------------------------------------------------------

	/**
	 * Pente minimale d'une bouche de grotte, en degres.
	 *
	 * UNE GROTTE S'OUVRE SUR UN ESCARPEMENT, et ce n'est pas une question de
	 * gout : en penetrant horizontalement dans un versant raide, on gagne de la
	 * profondeur en quelques metres. Sur un terrain plat, la meme galerie
	 * resterait a fleur de sol sur des dizaines de metres et eventrerait le
	 * paysage. La pente est donc la condition, pas la decoration.
	 */
	float EntranceSlopeDeg = 35.0f;

	/** Une entree pour tant de chambres. A zero, le reseau reste ferme. */
	float EntrancePerChambers = 10.0f;

	/** Enfoncement horizontal de la bouche dans le versant, en metres. */
	float EntranceDepthM = 14.0f;

	// --- gouffres ---------------------------------------------------------------

	/**
	 * Pente maximale au-dessus d'un gouffre, en degres.
	 *
	 * UN AVEN S'OUVRE SUR UN PLATEAU, une bouche sur une falaise : ce sont les
	 * deux formes d'entree d'un karst, et le TERRAIN decide de laquelle. La
	 * premiere demande du plat -- un puits vertical sur un versant raide
	 * deboucherait en biais et ne ressemblerait a rien -- la seconde demande un
	 * escarpement. L'une est donc le repli naturel de l'autre.
	 */
	float ShaftSlopeMaxDeg = 20.0f;

	/** Rayon du gouffre a son ouverture, en metres. */
	float ShaftTopRadiusM = 2.5f;

	/**
	 * Rayon du gouffre a sa base, en metres.
	 *
	 * PLUS LARGE QUE L'OUVERTURE, et c'est la forme meme de l'aven : etroit en
	 * surface parce que la dissolution y a le moins travaille, evase dessous ou
	 * l'eau a stagne. Un puits cylindrique se lit tout de suite comme un forage.
	 */
	float ShaftBottomRadiusM = 6.0f;

	/** Amplitude de l'ondulation de l'axe, en metres. Zero donne un forage. */
	float ShaftWanderM = 3.0f;

	// --- dolines ----------------------------------------------------------------

	/**
	 * Epaisseur de plafond sous laquelle une salle s'effondre, en metres.
	 *
	 * UNE DOLINE D'EFFONDREMENT N'EST PAS UNE FORME QU'ON POSE, C'EST UNE
	 * CONSEQUENCE. Le plafond d'une salle porte le poids de ce qui le
	 * surmonte ; quand il devient trop mince pour cette charge, il cede, et la
	 * surface s'affaisse en entonnoir jusqu'au vide. Le critere est donc
	 * l'epaisseur de roche entre le sommet de la salle et le sol --
	 * profondeur moins rayon -- et rien d'autre.
	 *
	 * C'EST AUSSI LA LECON DU GOUFFRE, APPLIQUEE. L'aven avait d'abord ete
	 * fabrique DANS la boucle des entrees, donc plafonne par leur budget : six
	 * pour tout le monde, quoi qu'il arrive. Une doline ne se forme pas parce
	 * qu'il manquait un acces, elle se forme parce que le plafond est mince.
	 * Elle est donc comptee a part, et rien ne la limite que la geologie.
	 */
	float DolineRoofMaxM = 12.0f;

	/**
	 * Evasement de l'entonnoir : rayon en surface rapporte a celui de la salle.
	 *
	 * LA DOLINE EST PLUS LARGE QUE LA SALLE QUI L'A FAITE, toujours : les
	 * parois de l'entonnoir s'eboulent jusqu'a leur angle de repos, ce qui
	 * elargit l'ouverture bien au-dela du vide initial. Un puits de meme
	 * diametre que la salle serait un trou de forage, pas un effondrement.
	 */
	float DolineFlareRatio = 1.8f;

	/**
	 * Amplitude de l'irregularite du bord, en metres.
	 *
	 * Le contour d'un effondrement n'est pas un cercle : il suit les fractures
	 * de la roche. A zero, on obtient un cratere de compas.
	 */
	float DolineRimNoiseM = 6.0f;

	/**
	 * Distance minimale entre deux bouches, en metres.
	 *
	 * SANS ELLE, DEUX CHAMBRES VOISINES ELISENT LA MEME PAROI. Elles cherchent
	 * chacune la cellule la plus raide de leur voisinage, et ces voisinages se
	 * recouvrent : le meme escarpement gagne deux fois, et l'on pose deux
	 * bouches superposees. Constate sur deux entrees a la meme position au
	 * metre pres.
	 */
	float EntranceSpacingM = 120.0f;

	/**
	 * Marge au-dessus du niveau de la mer, en metres.
	 *
	 * RIEN NE SE CREUSE SOUS LA MER. Une chambre sous le niveau marin est noyee
	 * par le plugin Water, qui applique son rendu sous-marin a tout ce qui passe
	 * sous zero ; ce n'est pas absurde physiquement, mais personne ne l'a decide
	 * et ca complique tout. On s'en tient donc au-dessus.
	 */
	float SeaMarginM = 5.0f;

	/**
	 * Rayon de raccordement de l'union lisse, en metres.
	 *
	 * A ZERO, L'UNION REDEVIENT UN MAXIMUM DUR, et les jonctions entre galerie
	 * et chambre prennent des aretes vives -- l'effet de tuyaux colles. Au-dela,
	 * le raccord s'arrondit. Le proprietaire a demande "un compromis entre
	 * maximum dur et lisse", ce qui est exactement une valeur basse de ce
	 * reglage.
	 */
	float BlendM = 2.5f;

	// --- arches ---------------------------------------------------------------

	/**
	 * Largeur de crete au-dela de laquelle on ne perce pas, en metres.
	 *
	 * C'EST LE CRITERE QUI FAIT LA DIFFERENCE ENTRE UNE ARCHE ET UN TUNNEL, et
	 * il n'est pas negociable : une arche est une ouverture TRAVERSANTE sous un
	 * pont de roche continu. Percer une colline de deux cents metres donne un
	 * tunnel, pas une arche. La forme demande donc une LAME, et c'est son
	 * absence qui avait fait abandonner le chantier sur le monde de 16 km --
	 * zero site sur 402 points emerges. Sur 64 x 32 km, apres la boucle
	 * soulevement / erosion : 37,76 % des sites passent sous 80 m.
	 */
	float ArchCrestMaxM = 80.0f;

	/** Profondeur sous le sommet ou l'on mesure la crete, en metres. */
	float ArchBelowSummitM = 20.0f;

	float ArchRadiusMinM = 6.0f;
	float ArchRadiusMaxM = 14.0f;

	/**
	 * Epaisseur du pont de roche au-dessus de l'ouverture, en metres.
	 *
	 * C'EST CE QUI SE VOIT, PLUS ENCORE QUE L'OUVERTURE. Un trou de trente
	 * metres sous cent metres de roche se lit comme un tunnel ; le meme trou
	 * sous dix metres se lit comme une arche. Le rapport compte donc autant que
	 * la valeur, d'ou le plafond relatif ci-dessous.
	 */
	float ArchBridgeMinM = 4.0f;
	float ArchBridgeMaxM = 12.0f;

	/** Pont rapporte a la hauteur de l'ouverture. Au-dela, c'est un tunnel. */
	float ArchBridgeMaxRatio = 0.55f;

	/**
	 * Durete maximale de la roche, dans [0..1].
	 *
	 * UNE ARCHE N'EST PAS UNE FORME DE DISSOLUTION, donc le critere n'est pas
	 * la karstification : c'est de l'erosion differentielle et de la
	 * desquamation. Le gres en est la roche type -- Arches National Park est
	 * entierement en gres --, le calcaire en porte aussi. Le granite, lui,
	 * donne des domes et des chaos de blocs, pas des arches. A 1,0, la porte
	 * est ouverte a toutes les roches.
	 */
	float ArchHardnessMaxM = 0.7f;

	/**
	 * Distance entre deux SONDAGES de crete, en metres.
	 *
	 * ON SONDE SOUVENT ET ON POSE RAREMENT, et confondre les deux espacements
	 * fausse la mesure autant que le resultat. Sans sondage espace, chaque
	 * cellule du monde est examinee -- y compris en plein versant, ou la crete
	 * a vingt metres sous le point mesure est evidemment la montagne entiere.
	 * Releve de la premiere version : 433 057 sites et 99,44 % de cretes trop
	 * larges, quand la sonde, qui espace ses points et ne garde que des
	 * sommets, en trouvait 37,76 % d'assez minces.
	 */
	float ArchProbeSpacingM = 250.0f;

	/** Distance minimale entre deux arches, en metres. */
	float ArchSpacingM = 1500.0f;

	/** Nombre vise. A zero, aucune arche n'est posee. */
	int32 ArchCount = 24;

	/**
	 * Marge retranchee au sommet de la grille macro, en metres.
	 *
	 * LE CHAMP DE DENSITE DEPLACE LA SURFACE, et la passe des cavites ne le
	 * sait pas : elle ne lit que la grille de simulation, tandis que le voxel y
	 * ajoute jusqu'a overhangAmplitudeM de deplacement vertical. Un pont calcule
	 * sur la grille peut donc se retrouver EN L'AIR. Mesure avant cette marge :
	 * huit arches sur dix sans roche au-dessus de l'ouverture.
	 */
	float ArchSummitMarginM = 10.0f;

	/** Le champ de lames : l'arche se pose au milieu d'un mur, pas ailleurs. */
	FWorldseedFinRules Fins;

	static FWorldseedCaveRules FromRules(const UWorldseedRules& Rules);
};

namespace WorldseedCaves
{
	/**
	 * Bâtit le reseau. Une passe MACRO, a basse resolution, une fois par monde.
	 *
	 * ELLE NE PEUT PAS SE CALCULER PAR CHUNK : une galerie traverse les
	 * frontieres, et deux chunks voisins qui en decideraient chacun de leur cote
	 * ne tomberaient pas d'accord. Le calcul non local se paie donc UNE FOIS, a
	 * basse resolution, hors du chemin critique -- et son resultat devient un
	 * parametre de la fonction de densite, qui reste sans etat.
	 */
	WORLDSEED_API void Build(const FWorldseedGeometry& Geometry,
		const TArray<float>& ElevationM, const TArray<float>& PrecipMm,
		const FWorldseedLithology& Lithology, const FWorldseedLithologyRules& LithoRules,
		const FWorldseedCaveRules& Rules, float HeightExaggeration, int32 Seed,
		FWorldseedCaveNetwork& Out);

	/**
	 * Part d'AIR due au reseau en un point, en metres. Negatif ou nul hors des
	 * cavites, positif dedans.
	 */
	WORLDSEED_API double AirAt(const FWorldseedCaveLocal& Local, const FVector& PosM,
		float BlendM);
}
