// Worldseed - terrain voxel : diffusion des chunks autour du joueur.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedCaves.h"
#include "Procedural/WorldseedLithology.h"
#include "Procedural/WorldseedDensity.h"
#include "Procedural/WorldseedDiffusion.h"
#include "Procedural/WorldseedPlateau.h"
#include "Procedural/WorldseedStrata.h"
#include "Procedural/WorldseedVoxelChunk.h"
#include "Procedural/WorldseedWorldData.h"

#include <atomic>

#include "WorldseedVoxelTerrain.generated.h"

class UMaterialInterface;
class UProceduralMeshComponent;

/**
 * Un maillage de chunk en cours de fabrication sur un fil de travail.
 *
 * MEME DISCIPLINE QUE LA GENERATION DU MONDE DEPUIS LE MENU : le travailleur ne
 * detient AUCUN UObject, il remplit une structure partagee puis pose son drapeau
 * en ecriture liberee ; le fil de jeu lit le drapeau en acquisition avant de
 * toucher au resultat. Pas de verrou, et l'acteur peut mourir en vol -- le
 * travail se termine dans le vide et le pointeur partage se libere tout seul.
 */
struct FWorldseedVoxelJob
{
	FWorldseedChunkKey Key;
	FBox BoundsM = FBox(ForceInit);

	FWorldseedVoxelMesh Mesh;

	/**
	 * Les primitives de grottes qui touchent CE chunk, extraites une fois.
	 *
	 * Elles voyagent avec le travail plutot que d'etre lues depuis l'acteur :
	 * le fil de maillage ne doit rien tenir qui puisse mourir avant lui.
	 */
	FWorldseedCaveLocal Caves;
	FWorldseedVoxelStats Stats;
	bool bHasSurface = false;

	std::atomic<bool> bDone{ false };
	std::atomic<bool> bCancel{ false };
};

using FWorldseedVoxelJobPtr = TSharedPtr<FWorldseedVoxelJob, ESPMode::ThreadSafe>;

/** Ce qu'on garde d'un chunk pose. */
struct FWorldseedVoxelChunkState
{
	TObjectPtr<UProceduralMeshComponent> Mesh;

	/** Travail en cours, s'il y en a un. */
	FWorldseedVoxelJobPtr Job;

	/** Vrai si le chunk ne contient aucune surface : rien a afficher, jamais. */
	bool bEmpty = false;

	bool bHasCollision = false;

	/**
	 * Les faces de transition avec lesquelles ce chunk a ete maille.
	 *
	 * IL FAUT LE RETENIR : le masque depend du niveau des VOISINS, donc de la
	 * position du joueur. Il change sans que le chunk change de niveau, et un
	 * chunk qui garde un masque perime rouvre la fissure qu-il etait cense
	 * fermer. On le compare a chaque passe et l-on remaille quand il differe.
	 */
	uint8 Masque = 0;

	/** Pourquoi le dernier maillage n'a rien rendu. Diagnostic. */
	FWorldseedVoxelStats::ECause Cause = FWorldseedVoxelStats::ECause::Maille;
	int32 Seeds = 0;
	int32 Tris = 0;
};

/**
 * Terrain voxel diffuse autour du joueur.
 *
 * POURQUOI UN ACTEUR SEPARE DE AWorldseedTerrain. Le terrain en carte
 * d'altitude marche ; tant que le voxel n'a pas fait ses preuves, le monde doit
 * rester jouable. Les deux ne seront fondus qu'a la derniere etape, quand le
 * voxel tiendra la comparaison. Poser un acteur a cote coute une classe de plus
 * et ne casse rien.
 *
 * LA GRILLE 2D DECIDE DE LA VERTICALE, et c'est ce qui rend le streaming 3D
 * abordable : elle donne les bornes d'altitude de la surface sur l'empreinte
 * d'une colonne de chunks, donc on sait sans evaluer le champ quels etages
 * peuvent contenir quelque chose. Les etages du socle ne sont meme pas
 * consideres.
 */

/**
 * L'ECART ENTRE CE QUE LA DIFFUSION DEMANDE ET CE QUE L'ACTEUR TIENT.
 *
 * DEUX ECARTS, ET ILS N'ONT PAS LE MEME REMEDE -- c'est toute la raison d'etre
 * de ce releve. Un seul nombre les melangerait, et ce depot a paye quatre
 * corrections de suite sur un agregat qui recouvrait deux populations.
 *
 *   ORPHELIN   suivi, mais plus une feuille. Sa geometrie est DESSINEE EN
 *              DOUBLE, par-dessus celle de ses enfants -- deux surfaces du
 *              meme terrain a deux resolutions. A l'ecran : le relief qui
 *              CHANGE de detail, gresille, se dedouble.
 *
 *   MANQUANT   feuille demandee, sans maillage et sans marque de vide. Il n'y
 *              a rien a dessiner la. A l'ecran : du terrain qui APPARAIT du
 *              neant quand il arrive enfin.
 *
 * LE BANC IMMOBILE NE PEUT VOIR NI L'UN NI L'AUTRE : sans deplacement, aucun
 * chunk ne se subdivise et la diffusion ne demande jamais rien de neuf. C'est
 * pourquoi ce releve ne vaut qu'accompagne d'une marche.
 */
struct WORLDSEED_API FWorldseedStreamingReleve
{
	/** Ce que la diffusion DEMANDE a cet instant. */
	int32 Feuilles = 0;

	/** Ce que l'acteur TIENT, maillages et marques de vide compris. */
	int32 Suivis = 0;

	/**
	 * Suivis qui ne sont plus des feuilles, DANS le rayon de chargement.
	 *
	 * CEUX-LA SONT LE DEFAUT : leur geometrie recouvre celle de leurs enfants,
	 * deux surfaces du meme terrain a deux resolutions.
	 */
	int32 Orphelins = 0;

	/**
	 * Suivis qui ne sont plus des feuilles, AU-DELA du rayon de chargement.
	 *
	 * CEUX-LA SONT VOULUS, et les confondre avec les precedents ruinerait la
	 * mesure : l'hysteresis garde volontairement ce qui vient de sortir, pour
	 * qu'un demi-pas en arriere ne fasse pas tout remailler. Ils ne sont pas
	 * dessines en double -- ils n'ont pas d'enfants, ils sont seuls sur leur
	 * volume. Premier releve confondu : 872 « orphelins » dont la plus grande
	 * part n'etait que cette bande.
	 */
	int32 GardesParHysteresis = 0;

	/** Feuilles sans maillage ni marque de vide : TROU. */
	int32 Manquants = 0;

	/**
	 * Manquants que RIEN ne recouvre -- le trou reellement BEANT.
	 *
	 * ET C'EST LA DISTINCTION QUI MANQUAIT. `Manquants` compte une feuille sans
	 * maillage, sans savoir si un chunk PERIME bouche encore le trou en
	 * attendant. Avant que les orphelins ne soient relaches, le parent couvrait
	 * ses enfants en retard ; apres, il ne couvre plus rien. Le compte ne
	 * bougeait pas d'un chiffre -- 2 de moyenne dans les deux cas -- alors que
	 * l'un des deux etats montre un trou et l'autre non.
	 *
	 * C'est litteralement le cas d'ecole du depot : « quand un defaut est
	 * signale A L'OEIL et qu'aucun reglage ne deplace le chiffre, se demander
	 * si le chiffre MESURE LE DEFAUT ».
	 */
	int32 TrousDecouverts = 0;
};

/**
 * CE QUE LA CAMERA VOIT, ARBITRE PAR LE CHAMP.
 *
 * POURQUOI UN SECOND INSTRUMENT. Tout ce qui precede est NOTRE comptabilite :
 * elle peut etre parfaitement coherente avec elle-meme et fausse. Ce depot a
 * paye exactement cela -- `ProbeTransvoxel` comparait les triangles du
 * mailleur a ses propres normales, ne pouvait que se donner raison, a rendu
 * 0,1 % et fait poser la valeur inverse ; il a fallu un ARBITRE TIERS, le
 * gradient du champ, qui n'appartient a aucun des deux.
 *
 * Ici le tiers est `SurfaceHeightM` : le relief 2D, qui ne sait rien du
 * decoupage en chunks, de leur niveau ni de leur etat. Le sondage lui demande
 * « a partir d'ou ce rayon devrait-il etre DANS la roche », puis demande au
 * RENDU -- par la collision, que seul un chunk maille porte -- s'il a
 * rencontre quoi que ce soit avant. Rien rencontre, et c'est un trou.
 *
 * LA MARGE N'EST PAS UN REGLAGE DE CONFORT : le champ deplace la vraie surface
 * de `overhangAmplitudeM + detailAmplitudeM` autour du relief macro. On ne
 * declare un trou qu'une fois le rayon PLUS BAS que tout ce que ce
 * deplacement peut expliquer.
 *
 * ET LE TEMOIN EST OBLIGATOIRE : sur un monde POSE, ce sondage doit rendre
 * ZERO. S'il crie a l'arret, c'est l'instrument qui est faux -- et le croire
 * ferait chercher un defaut qui n'existe pas.
 */
struct WORLDSEED_API FWorldseedSondageDeVue
{
	/** Directions sondees dans le champ de vision. */
	int32 Rayons = 0;

	/** Directions ou le champ promet de la roche et ou le rendu ne montre rien. */
	int32 Trous = 0;

	/** Distance du trou le plus proche, en metres. */
	float PlusProcheM = 0.0f;
};

/**
 * OU EST LE JOUEUR, ET VERS OU IL REGARDE -- en NOMBRES, pas en texte.
 *
 * POURQUOI ELLE EXISTE. `ReleveJoueur` calculait deja tout cela, puis le
 * JETAIT en `FString`. Le premier consommateur qui a voulu ces nombres -- la
 * minimap, qui a besoin du centre de sa fenetre et de l'angle de son cone --
 * n'avait donc d'autre choix que de recalculer, ce qui aurait fait la
 * quatrieme copie de la position et la deuxieme du cap. Le depot a une regle
 * contre les formules recopiees, et il l'a payee assez souvent : l'amplitude
 * saisonniere recalculee dans un second fichier, le classificateur de biomes
 * reimplemente par le bulletin terrestre -- qui validait donc une COPIE du
 * classificateur.
 *
 * L'en-tete de `ReleveJoueur` argumentait deja dans ce sens : « un overlay qui
 * irait chercher tout cela lui-meme en dupliquerait les conventions ». C'est
 * vrai, et c'est exactement pour cela que le releve ne doit pas etre le SEUL
 * moyen de les obtenir.
 */
struct WORLDSEED_API FWorldseedReperePlayer
{
	/** Faux tant qu'il n'y a ni pion ni monde : tout le reste est alors nul. */
	bool bValide = false;

	/** Position dans le repere de l'ACTEUR terrain, en metres. */
	double Xm = 0.0;
	double Ym = 0.0;
	double Zm = 0.0;

	/** Azimut de la CAMERA : zero au nord, croissant vers l'est. */
	float CapDeg = 0.0f;

	/** Latitude equivalente-aire, et longitude centree sur zero. */
	float LatitudeDeg = 0.0f;
	float LongitudeDeg = 0.0f;

	/** Cellule de la grille sous le joueur, convention du sol. */
	int32 Cellule = INDEX_NONE;
};

UCLASS()
class WORLDSEED_API AWorldseedVoxelTerrain : public AActor
{
	GENERATED_BODY()

public:
	AWorldseedVoxelTerrain();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ------------------------------------------------------------ diffusion

	/** Cote d'un chunk, en metres. 32 m pour des voxels d'un metre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "8.0"))
	float ChunkSideM = 32.0f;

	/**
	 * Marge sous la bande avant de declarer le joueur hors du monde, en metres.
	 *
	 * La bande fait deja cent metres ; on n'y ajoute qu'une marge de securite
	 * pour ne pas rattraper un joueur qui explore legitimement une cavite
	 * profonde. Sous bande + marge, il n'existe aucune geometrie : ce n'est
	 * plus une chute, c'est une sortie du monde.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "0.0"))
	float PlayerRescueMarginM = 20.0f;

	/**
	 * Rayon de construction autour du joueur, en metres.
	 *
	 * PORTE DE 250 A 600 LE 19 SEPTEMBRE, SUR MESURE ET CONTRE MA PROPRE
	 * CONCLUSION. J'avais ecrit qu'elargir la distance de vue etait
	 * « impossible avec un mailleur a resolution uniforme », le debit de
	 * streaming etant le mur. C'ETAIT FAUX, et la cause etait un defaut de mon
	 * banc : il ne forcait que ce rayon-ci, pas `UnloadRadiusM` fige a 350, si
	 * bien que les chunks lointains etaient batis puis DETRUITS aussitot. Le
	 * monde tournait en boucle sur le meme millier de chunks.
	 *
	 * MESURE UNE FOIS LE BANC REPARE, machine au repos, monde 4096x2048 :
	 *
	 *   rayon   chunks   remplissage   trame        pire    memoire
	 *   250 m      809       9 s       6,42 ms      8,93    6,50 Go
	 *   400 m    2 167      18 s       6,38 ms      8,23    6,94 Go
	 *   600 m    5 085      36 s       6,61 ms      9,15    7,63 Go
	 *   800 m    9 242      62 s       6,78 ms      8,31    8,61 Go
	 *
	 * A 800 m : dix millions de triangles, 147 images par seconde, AUCUN
	 * a-coup -- la pire trame reste a 8 ms. Le cout est la MEMOIRE, pas les
	 * images. 600 m est retenu comme compromis : une mesa entiere tient dans
	 * la vue, le remplissage reste sous la minute, et l'on garde de la marge.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "32.0"))
	float LoadRadiusM = 600.0f;

	/**
	 * Rayon de destruction, volontairement plus grand que celui de construction.
	 * Sans cette hysteresis, un pas en avant et un pas en arriere sur la
	 * frontiere feraient construire et detruire le meme chunk en boucle.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "32.0"))
	float UnloadRadiusM = 840.0f;

	// ------------------------------------------------ anneaux de resolution

	/**
	 * Nombre d'anneaux au-dela du plus fin. ZERO = resolution uniforme.
	 *
	 * A ZERO, LA DIFFUSION EST RIGOUREUSEMENT CELLE D'AVANT -- c'est la
	 * propriete de surete de ce chantier, et elle se verifie : meme compte de
	 * chunks, meme geometrie. L'arbitrage du proprietaire etant « mesurer
	 * d'abord, decouper ensuite », les anneaux arrivent ETEINTS et se mesurent
	 * au banc avant qu'on fixe quoi que ce soit.
	 *
	 * A un niveau de plus, les chunks lointains font 64 m pour des voxels de
	 * 2 m : ils couvrent huit fois le volume d'un chunk fin, donc le compte
	 * cesse de croitre en R au carre.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "0", ClampMax = "4"))
	int32 NiveauMax = 0;

	/**
	 * Rayon du premier anneau -- celui qui reste a pleine resolution, en metres.
	 *
	 * Les suivants DOUBLENT : anneau L jusqu'a `RayonAnneau0M * 2^L`. Ce n'est
	 * pas un choix esthetique : la taille d'un chunk double aussi d'un niveau a
	 * l'autre, donc un rayon qui double garde a peu pres CONSTANT le nombre de
	 * chunks par anneau. Tout autre progression fait enfler un anneau au
	 * detriment des autres.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "32.0"))
	float RayonAnneau0M = 250.0f;

	/** Poids de la verticale dans le critere de niveau. A 1, comportement d.origine. */
	float PoidsZDiffusion = 1.0f;

	/**
	 * Epaisseur de la dalle de transition, en FRACTION d'une cellule du chunk.
	 *
	 * Jamais en metres : elle doit suivre le niveau de detail. Une epaisseur
	 * nulle raccorde geometriquement sans fissure mais « leads to severe shading
	 * problems » (Lengyel, section 4.3) -- les triangles lateraux degenerent et
	 * leurs normales n'ont plus de sens.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float LargeurTransition = 0.5f;

	// IL Y AVAIT ICI UN RAYON DE COLLISION, plus court que le rayon de
	// chargement, au motif que cuire une collision coute plus cher que mailler.
	// Il est RETIRE : aucune API ne permet de donner la collision a une section
	// deja creee, donc la decision prise au televersement etait definitive, et
	// le joueur qui marchait au-dela passait AU TRAVERS DU SOL. Mesure du
	// defaut : sol present de 0 a 110 m, plus rien de 120 a 250 -- la frontiere
	// tombait exactement sur l'ancien rayon. Ne pas le remettre : si la cuisson
	// coute trop cher, la reponse est de la faire de facon asynchrone.

	/**
	 * Travaux simultanes sur le pool de fils.
	 *
	 * PORTE DE 24 A 64 LE 22 SEPTEMBRE, SUR MESURE, ET C'EST UN LEVIER DE
	 * LATENCE, PAS DE TRAME. Un travail de maillage dure 8 a 9 ms quand la
	 * passe de diffusion revient toutes les 100 : au plus `MaxJobsInFlight`
	 * d'entre eux peuvent donc etre lances ET recoltes par passe, ce qui borne
	 * le debit de pose a `MaxJobsInFlight / UpdatePeriod`, quel que soit le
	 * budget de televersement.
	 *
	 * LE MODELE A ETE VALIDE PAR DEUX PREDICTIONS ANNONCEES AVANT LECTURE :
	 *
	 *   poses 16, travaux  24  ->  160/s  ->  20 s de remplissage
	 *   poses 32, travaux  24  ->  240/s  ->  15 s   (les travaux bornent)
	 *   poses 64, travaux  24  ->  240/s  ->  15 s   <- predit, verifie
	 *   poses 32, travaux  64  ->  320/s  ->  12 s   (les poses bornent)
	 *   poses 32, travaux 128  ->  320/s  ->  12 s   <- predit, verifie
	 *
	 * Les deux robinets se passent le relais : ouvrir celui qui ne borde pas
	 * ne donne RIEN, et c'est ce qu'il faut savoir avant de regler l'un ou
	 * l'autre au jugé.
	 *
	 * CE QUE CA COUTE : rien. La trame s'ameliore meme -- 4,84 a 4,30 ms, 207
	 * a 233 images par seconde, pire trame 8,15 a 6,12 -- parce que le
	 * transitoire de remplissage dure moins longtemps. Le maillage ralentit un
	 * peu par travail (8,4 a 9,3 ms, contention sur le pool), mais le debit
	 * total monte.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "1", ClampMax = "256"))
	int32 MaxJobsInFlight = 64;

	/**
	 * Chunks televerses par passe.
	 *
	 * LE MAILLAGE EST HORS DU FIL DE JEU, PAS LE TELEVERSEMENT : creer une
	 * section de ProceduralMesh touche au moteur de rendu et doit donc rester
	 * sur le fil de jeu. C'est la seule part du cout qui se paie en images.
	 *
	 * PORTE DE 6 A 16 LE 19 SEPTEMBRE, SUR MESURE. Six televersements toutes
	 * les 0,2 s plafonnaient le remplissage a TRENTE chunks par seconde, par
	 * construction -- et c'est exactement le 26 par seconde observe a 250 m,
	 * donc le plafond etait atteint. Le debit n'etait pas une limite physique,
	 * c'etait un REGLAGE, pose a une epoque ou le televersement etait suppose
	 * cher.
	 *
	 * IL NE L'EST PAS : instrumente, il coute 0,21 ms par chunk et 1,17 au
	 * pire, soit 0,2 s CUMULEES pour neuf cents chunks. Le maillage, lui, est
	 * deja sur le pool de fils, et la cuisson de collision est asynchrone
	 * depuis longtemps -- la note du depot qui affirme le contraire est
	 * perimee.
	 *
	 * PORTE DE 16 A 32 LE 22 SEPTEMBRE, avec `MaxJobsInFlight` a 64. Les deux
	 * vont ENSEMBLE : ouvrir l'un sans l'autre ne donne rien, l'autre bornant
	 * aussitot (voir la table de `MaxJobsInFlight`). A trente-deux poses par
	 * passe, le fil de jeu paie 32 x 0,16 = 5 ms toutes les 100 -- cinq pour
	 * cent -- et seulement pendant le remplissage.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "1", ClampMax = "256"))
	int32 UploadsPerPass = 32;

	/**
	 * LA CADENCE DE LA PASSE, ET C'EST ELLE LE LEVIER -- PAS LES LOTS.
	 *
	 * PORTEE DE 0,1 A 0,05 s LE 23 SEPTEMBRE, quand le proprietaire a confirme
	 * que le VOL serait un cas d'usage du jeu. A cinquante metres par seconde
	 * le streaming demande cent quatre-vingt-quinze chunks par seconde contre
	 * vingt-trois a la marche : la marge tombe de quatorze fois a 1,6, et la
	 * file ne se vide plus.
	 *
	 * J'AI ANNONCE UN MODELE, ET LA MESURE L'A REFUTE. Je predisais que seul
	 * comptait le debit, `min(poses, travaux) / periode`, donc que doubler les
	 * lots ou halver la periode reviendrait au meme. Mesure en vol a 50 m/s,
	 * 1850 m parcourus, cinq passes sur le MEME binaire -- beants moyens, ce
	 * que rien ne recouvre :
	 *
	 *     A  32 / 64  / 0,10     32,7      (en vigueur jusque-la)
	 *     B  64 / 128 / 0,10      8,4      debit double par les LOTS
	 *     C  32 / 64  / 0,05      4,3      debit double par la PERIODE
	 *     D  64 / 128 / 0,05      4,3      les deux
	 *     E  32 / 64  / 0,033     4,2      periode encore plus courte
	 *
	 * B ET C ONT LE MEME DEBIT NOMINAL ET C FAIT DEUX FOIS MIEUX. Ce n'est
	 * donc pas un debit, c'est une LATENCE : la periode separe « ce chunk
	 * devient necessaire » de « il est lance », puis « il est pret » de « il
	 * est pose ». A cinquante metres par seconde, cinquante millisecondes
	 * valent deux metres et demi de terrain.
	 *
	 * ET C, D, E SONT EQUIVALENTS : passe 0,05 s les lots ne comptent plus, et
	 * raccourcir encore ne donne rien. Le plancher de 4,2 est la latence de
	 * maillage elle-meme -- 9,3 ms par chunk -- et aucun robinet ne l'atteint.
	 * On retient donc C : la periode seule, les lots inchanges, donc un cout
	 * par passe qui reste a 32 x 0,16 = 5 ms au lieu de dix.
	 *
	 * CE QUE CA COUTE, ET IL FAUT LE DIRE : la trame moyenne ne bouge pas
	 * (5,11 -> 5,25 ms) mais le p95 monte de 6,50 a 9,42 -- des passes plus
	 * frequentes, donc un petit cout plus souvent. En echange la PIRE trame
	 * tombe de 34,2 a 21,6 ms. On echange un a-coup visible contre un cout
	 * regulier invisible, et c'est le bon sens du marche.
	 *
	 * CE QUE LA MESURE NE DIT PAS, et je ne le revendique pas : le sondage de
	 * vue -- l'arbitre tiers, celui qui interroge le champ et le rendu -- ne
	 * bouge pas (0,05 / 0,05 / 0,10 / 0,08 / 0,04 rayon sur 32). Avec une
	 * dizaine d'evenements par passe ces ecarts ne sont pas resolubles. Notre
	 * comptabilite s'ameliore d'un facteur huit ; que le JOUEUR voie moins de
	 * trous reste a etablir.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "0.01"))
	float UpdatePeriod = 0.05f;

	/** Materiau des chunks. Il lit la couleur de sommet telle quelle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel")
	TObjectPtr<UMaterialInterface> TerrainMaterial;

	// ------------------------------------------------------------ monde

	/**
	 * Monde de secours, quand rien n'attend dans la GameInstance.
	 *
	 * Sert au banc : on peut poser cet acteur dans une carte vide et obtenir un
	 * monde sans passer par le menu.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde")
	int32 FallbackSeed = 20260909;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde")
	float FallbackHeightMeters = 32000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde")
	int32 FallbackResolutionY = 2048;

	/**
	 * Tient le joueur au-dessus du relief tant que son sol n'existe pas.
	 *
	 * SANS CELA IL TOMBE, ET IL EMPORTE TOUT AVEC LUI. Mesure du premier essai :
	 * pion lache a 400 m au-dessus du sol, arrive a -9 857 m -- et comme c'est
	 * LUI qui sert d'origine a la diffusion, les 233 chunks deja mailles sont
	 * sortis du rayon et ont ete detruits derriere sa chute. Attendre qu'un
	 * chunk porte une collision pour le poser etait circulaire : il etait deja
	 * trop loin pour qu'on en construise un.
	 */
	/**
	 * Exageration verticale du relief, reprise de l'acteur qui pose celui-ci.
	 *
	 * ELLE DOIT ETRE LA MEME DES DEUX COTES : le sol de fond et l'ocean sont
	 * batis avec celle du terrain, et un champ de densite qui l'ignorerait
	 * decrirait un relief a une autre echelle verticale -- la jonction entre
	 * l'horizon et le sol proche se verrait comme une marche.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde",
		meta = (ClampMin = "0.01"))
	float HeightExaggeration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde")
	bool bHoldPlayer = true;

	// ------------------------------------------------------------ releves

	/** Etat de la diffusion, pour la sonde et le journal. */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Voxel")
	FString ReportState() const;

private:
	bool LoadWorld();

public:
	/**
	 * Reprend TEL QUEL le monde de l'acteur qui pose celui-ci.
	 *
	 * SANS CELA, LES DEUX EN GENERENT DEUX DIFFERENTS, en silence. Quand aucun
	 * monde n'attend dans l'instance de jeu -- c'est le cas d'un PIE lance
	 * depuis l'editeur, sans passer par le menu -- chaque acteur tombe sur sa
	 * generation de secours, et leurs resolutions ne sont pas les memes : 512 x
	 * 256 pour le terrain, 2048 x 1024 ici. Meme graine, relief different. Le
	 * sol de fond decrivait donc un autre monde que celui qu'on a sous les
	 * pieds, sans le moindre avertissement.
	 *
	 * A appeler AVANT FinishSpawning : BeginPlay charge le monde, et il est
	 * trop tard apres.
	 */
	void AdoptWorld(FWorldseedMondeRef InMonde, float InHeightExaggeration,
		const FWorldseedCaveNetwork& InCaves,
		const FWorldseedLithology& InLithology);

	/**
	 * Demande que le joueur naisse pres de ce point, en METRES sur la carte.
	 *
	 * A PART D'AdoptWorld, ET PAS PAR PARESSE DE SIGNATURE. Ce que transmet
	 * AdoptWorld est le MONDE -- relief, biomes, roches, cavites -- c'est-a-dire
	 * ce que la graine determine. Un point de depart est un CHOIX du joueur :
	 * deux parties sur la meme graine ont le meme monde et peuvent commencer
	 * ailleurs. Les melanger inviterait a croire que le depart se recalcule.
	 */
	void DemanderDepart(const FVector2D& XYMetres);

	// IL Y AVAIT ICI `AdoptChampsClimat`, qui transmettait a part la
	// continentalite et l'amplitude saisonniere « parce qu'elles n'expliquent,
	// elles ne generent rien ». L'argument etait bon tant que transmettre
	// voulait dire COPIER : separer ce qui sert a mailler de ce qui sert a
	// expliquer evitait une copie inutile, et disait la difference.
	//
	// Il n'a plus d'objet. Le monde arrive ENTIER et par reference, donc ces
	// deux champs ne coutent plus rien a porter, et une seconde fonction ne
	// ferait que laisser croire qu'ils viennent d'ailleurs. La distinction
	// qu'elle portait reste vraie et vit maintenant a sa place : dans le
	// commentaire des accesseurs.

	/**
	 * Le releve local sous le joueur, une chaine par ligne.
	 *
	 * IL VIT ICI ET NON DANS L'AFFICHAGE, parce que tout ce qu'il dit est la
	 * connaissance de CET acteur : la geometrie qui donne la latitude, les
	 * biomes, le climat, la lithologie, les strates, le champ de densite, les
	 * anneaux et l'etat des chunks. Un overlay qui irait chercher tout cela
	 * lui-meme en dupliquerait les conventions -- et ce depot a deja paye ce
	 * que coute une seconde lecture d'une meme grille.
	 *
	 * Rend un tableau VIDE tant que le monde n'est pas charge : l'appelant
	 * n'a alors rien a afficher, ce qui vaut mieux qu'une ligne de zeros.
	 */
	TArray<FString> ReleveJoueur() const;

	/**
	 * Position, cap et cellule du joueur, en nombres.
	 *
	 * C'est la source de `ReleveJoueur`, qui n'est plus qu'un formateur, et
	 * celle de tout consommateur qui a besoin des VALEURS -- une minimap, par
	 * exemple. `bValide` a faux signifie qu'il n'y a ni pion ni monde ; ce
	 * n'est pas une erreur, c'est l'etat normal du premier dixieme de seconde.
	 */
	FWorldseedReperePlayer ReperePlayer() const;

private:

	void UpdateChunks();

	/** Origine de la diffusion : le pion s'il existe, sinon l'acteur. */
	FVector StreamingOriginCm() const;

	/** Centre d'un chunk, en centimetres monde. */
	FVector ChunkCentreCm(const FWorldseedChunkKey& Key) const;

	/**
	 * LA DIFFUSION, ET ELLE NE CONNAIT NI CET ACTEUR NI LE MONDE.
	 *
	 * Elle decide QUI est emis, a QUEL niveau, et quelles faces de transition --
	 * soit six reglages, un champ de densite et une origine. Elle vivait ici,
	 * melee aux composants, aux travaux en vol, au pion et au materiau ; sortie,
	 * elle devient EPROUVABLE, et sa propriete de surete 2:1 -- Transvoxel ne
	 * sait coudre qu un niveau d ecart -- cesse de n avoir pour tout filet qu un
	 * avertissement au journal.
	 */
	FWorldseedDiffusion Diffusion;

	void UpdateChunksInterne();
	void LaunchJob(const FWorldseedChunkKey& Key);

	/** Televerse un maillage termine dans son composant. */
	void UploadChunk(const FWorldseedChunkKey& Key, FWorldseedVoxelChunkState& State);

	/** Couleur et teinte d'un sommet, depuis la carte des biomes. */
	void PaintVertices(FWorldseedVoxelMesh& Mesh) const;

public:
	/**
	 * Etat de la colonne de chunks qui contient ce point. Diagnostic.
	 *
	 * IL N'EXISTE AUCUN AUTRE MOYEN DE SAVOIR POURQUOI UN TROU EST LA. De
	 * l'exterieur, un chunk jamais considere, un chunk declare vide et un chunk
	 * maille a zero triangle se ressemblent tous les trois : on sonde, on ne
	 * touche rien, et on ne peut pas distinguer les trois causes -- qui
	 * appellent pourtant trois corrections differentes.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Voxel")
	FString DiagnostiquerColonne(FVector MondeCm) const;

	/**
	 * Pose le joueur a un endroit choisi, en metres dans le repere du monde.
	 *
	 * IL NE SUFFIT PAS DE DEPLACER LE PION, ET LE PROJET A DEJA PAYE CE PIEGE.
	 * Les chunks se batissent AUTOUR de lui : a l'arrivee il n'y a rien sous
	 * ses pieds, il tombe, et comme c'est lui qui donne l'origine de la
	 * diffusion il emmene la fenetre de chunks dans sa chute -- mesure, -4745 m.
	 * On reutilise donc le filet qui existe deja : le pion est TENU EN VOL
	 * jusqu'a ce que le chunk qui le porte ait une collision cuite, puis rendu
	 * a la gravite. C'est exactement ce que fait la mise en place initiale, et
	 * la refaire ici ferait diverger les deux moities.
	 *
	 * SEULE DIFFERENCE AVEC CETTE MISE EN PLACE : on ne cherche PAS de sol plat
	 * alentour. L'endroit a ete demande, on y va.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Voxel")
	void TeleporterJoueur(double XMetres, double YMetres);

	/** Ou est le joueur, et sur quoi. Pour juger une capture sans deviner. */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Voxel")
	FString OuSuisJe() const;

	/**
	 * Le monde charge, en LECTURE SEULE, pour qui doit l'interroger.
	 *
	 * L'acteur charge et possede ce monde : y donner acces en lecture est
	 * legitime et evite de le recopier. Ce qui ne le serait pas, c'est qu'il
	 * se charge lui-meme des travaux qui s'en servent -- la tournee photo a
	 * vecu ici par commodite avant d'aller dans son propre sous-systeme.
	 */
	const FWorldseedGeometry& MondeGeometrie() const { return Geometry; }
	const TArray<float>& MondeAltitudes() const { return HeightsM(); }

	/**
	 * Le monde partage lui-meme, pour qui veut son POIDS et ses PORTEURS.
	 *
	 * Le banc s'en sert, et c'est la seule raison de son existence : la
	 * memoire du processus ne sait pas dire si le monde est recopie -- elle
	 * derive de plus que ce qu'une copie couterait -- alors que le nombre de
	 * references, lui, le dit exactement.
	 */
	FWorldseedMondePtr MondePartage() const { return Monde; }
	const FWorldseedCaveNetwork& MondeGrottes() const { return CaveNetwork; }

	/** Les sites de tables, rebatis au chargement et jamais serialises. */
	const TArray<FWorldseedPlateauSite>& MondeTables() const { return Tables; }

	/** Les fonds de canyon, rebatis au chargement comme les tables. */
	const TArray<FWorldseedPlateauSite>& MondeCanyons() const { return Canyons; }

	/** Rayon de chargement effectif, en metres. Lu par le banc. */
	float RayonDeChargementM() const { return LoadRadiusM; }

	/** Chunks suivis, et travaux en vol. Le banc s en sert pour savoir
	 *  quand le streaming est STABILISE -- un compte fige et aucun travail. */
	/**
	 * Vrai quand la diffusion ne bouge plus : compte FIGE et aucun travail en vol.
	 *
	 * UNE SEULE DEFINITION POUR DEUX CONSOMMATEURS. Le banc a appris cette
	 * lecon a ses depens -- sa premiere version chauffait un nombre FIXE de
	 * secondes et rendait EXACTEMENT 552 chunks a 250 m comme a 400, donc un
	 * transitoire identique des deux cotes et un A/B qui ne comparait rien. La
	 * tournee photo est restee au delai fixe et prenait des paysages a moitie
	 * batis. La regle du depot interdit de recopier une formule dans deux
	 * fichiers : elle vit donc ici, et les deux l-appellent.
	 *
	 * `DernierCompte` est la memoire de l-appelant, mise a jour au passage.
	 */
	bool DiffusionStable(int32& DernierCompte) const
	{
		const int32 N = Chunks.Num();
		const bool bFige = (N == DernierCompte) && (TravauxEnVol() == 0);
		DernierCompte = N;
		return bFige;
	}

	int32 NombreDeChunks() const { return Chunks.Num(); }
	int32 TravauxEnVol() const;

	/**
	 * Ce que la diffusion demande, ce que l'acteur tient, et les deux ecarts.
	 *
	 * L'ACTEUR COMPTE, L'APPELANT RAPPORTE -- la regle posee apres l'extraction
	 * du sol de fond, ou module et acteur journalisaient tous deux. Une passe
	 * qui ecrit elle-meme au journal ne peut plus etre appelee a chaque trame
	 * sans le noyer, ni deux fois sans qu'on croie a un doublon.
	 *
	 * Il balaie les deux ensembles, donc son cout suit le nombre de chunks :
	 * a reserver au banc, jamais a mettre dans la passe de diffusion.
	 */
	FWorldseedStreamingReleve ReleveStreaming() const;

	/**
	 * Sonde le champ de vision et compte les trous BEANTS.
	 *
	 * Reserve au banc : il marche le long de chaque rayon sur le relief 2D,
	 * donc son cout suit le produit rayons x portee. A appeler a quelques
	 * hertz, jamais a chaque trame.
	 */
	FWorldseedSondageDeVue SonderLaVue() const;
	/**
	 * Le champ de densite du monde charge.
	 *
	 * IL REND UNE REFERENCE SUR UN CHAMP QUI PEUT NE PAS EXISTER ENCORE, d'ou
	 * la garde : le champ n'est bati qu'a l'adoption du monde, et le banc comme
	 * la tournee photo peuvent tomber sur l'acteur avant. Ils verifient tous
	 * deux `MondeAltitudes().Num()` d'abord -- mais faire reposer l'absence de
	 * dereferencement nul sur la discipline de l'appelant est exactement le
	 * genre de pari que ce depot a deja paye.
	 */
	static const FWorldseedDensity& ChampVide();
	const FWorldseedDensity& MondeChamp() const
	{
		return Density.IsValid() ? *Density : ChampVide();
	}

	/** Vrai quand le pion a ete rendu a la gravite sur un sol solide. */
	bool JoueurPose() const { return bPlayerReleased; }

	/**
	 * Les endroits du monde CHARGE qui meritent d'etre vus.
	 *
	 * Calcules a la demande depuis le relief et la roche, jamais ecrits en dur :
	 * une liste de coordonnees serait juste pour une graine et fausse pour
	 * toutes les autres.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Voxel")
	FString LieuxRemarquables() const;

private:

	void ReleaseChunk(const FWorldseedChunkKey& Key);

	/** Tient le joueur en l'air, puis le rend a la gravite quand le sol existe. */
	void HoldOrReleasePlayer();

	/** Clef du chunk qui porte un point donne, en metres repere acteur. */
	FWorldseedChunkKey KeyForPoint(double X, double Y, double Z) const;

	/**
	 * Cherche un endroit ou POSER le joueur : plat, emerge, et plein dessous.
	 *
	 * Les trois criteres comptent. Emerge, sinon on nait dans la mer. Plat,
	 * sinon on glisse. Et surtout PLEIN DESSOUS : depuis que les galeries
	 * existent, une colonne sur huit porte un vide, et le pion tombe dedans --
	 * mesure, il s'est retrouve a 47 m sous terre au premier essai.
	 *
	 * Faux si rien ne convient dans le rayon fouille.
	 */
	/**
	 * Pente toleree quand le joueur a CHOISI son point de depart.
	 *
	 * Plus permissive que les douze degres d'une naissance libre, et c'est
	 * voulu : qui a vise un versant accepte d'etre sur un versant. Le pion
	 * marche jusqu'a environ 45 degres (`GetWalkableFloorAngle`), donc
	 * vingt-cinq laisse une marge confortable et ne glisse pas.
	 */
	static constexpr float PenteDepartMaxDeg = 25.0f;

	/**
	 * Ecart d'ALTITUDE tolere autour du point choisi, en metres.
	 *
	 * POURQUOI CETTE BORNE EXISTE. `WorldseedPlacement::SolPlat` retient le PREMIER point
	 * acceptable de sa spirale : sur un versant raide, le premier sol a moins
	 * de douze degres est la plaine d'en bas. Mesure sur deux parties
	 * independantes, latitudes 73,1 et 15,3 degres -- donc sans rapport de
	 * terrain -- le joueur est ne 89 et 92 metres SOUS le point qu'il avait
	 * choisi. Qui vise un sommet naissait a son pied.
	 *
	 * Quarante metres pour trois cent quatre-vingt-quatre de fouille, soit
	 * une pente moyenne de six degres : on peut vous deplacer le long du
	 * relief, pas vous faire descendre une falaise.
	 */
	static constexpr float EcartAltitudeDepartM = 40.0f;

	UPROPERTY()
	TObjectPtr<USceneComponent> RootScene;

	FWorldseedGeometry Geometry;
	int32 WorldSeed = 0;

	/**
	 * LE MONDE, TENU PAR REFERENCE. Il n'est plus copie depuis le terrain.
	 *
	 * `AdoptWorld` recopiait sept grands tableaux -- cent quatre-vingt-cinq
	 * megaoctets sur la grille du jeu -- pour une donnee que personne ne
	 * modifie apres sa generation. Les accesseurs ci-dessous gardent les noms
	 * d'avant a la parenthese pres, donc le code appelant n'a pas change de
	 * sens ; seule la propriete a change de main.
	 */
	FWorldseedMondePtr Monde;

	static const TArray<float>& FloatsVides();

	const TArray<float>& HeightsM() const
	{
		return Monde.IsValid() ? Monde->ElevationM : FloatsVides();
	}
	const FWorldseedBiomeMap& Biomes() const
	{
		static const FWorldseedBiomeMap Vide;
		return Monde.IsValid() ? Monde->Biomes : Vide;
	}

	/** Le reseau de grottes du monde charge. */
	FWorldseedCaveNetwork CaveNetwork;

	/** Sites de tables, fonction pure du relief et de la graine. */
	TArray<FWorldseedPlateauSite> Tables;

	/** Fonds de canyon : l autre face du meme objet. */
	TArray<FWorldseedPlateauSite> Canyons;

	/** Pluie annuelle, pour appliquer aux sites les gardes de la passe. */
	const TArray<float>& PrecipMm() const
	{
		return Monde.IsValid() ? Monde->PrecipMm : FloatsVides();
	}

	/** La temperature moyenne annuelle, pour la garde de froid des plateaux. */
	const TArray<float>& TempMeanC() const
	{
		return Monde.IsValid() ? Monde->TempC : FloatsVides();
	}

	/** La serie stratigraphique, lue une fois au chargement. */
	FWorldseedStratRules StratRules;

	/** Durete par identifiant, pour la garde du socle sedimentaire. */
	TArray<float> DureteParId;
	FWorldseedLithology Lithology;

	/**
	 * Couleur de chaque roche, indexee par identifiant.
	 *
	 * COPIEE, PAS REFERENCEE : cinq couleurs tiennent dans une poignee d'octets,
	 * et cela evite de dependre de la duree de vie des regles au moment ou l'on
	 * peint un chunk.
	 */
	TArray<FLinearColor> CouleurParRoche;

	/** Libelle de chaque roche, meme indexation. Pour le releve, pas le rendu. */
	TArray<FString> NomParRoche;

	/** Destination demandee, tant que le pion n'y est pas pose. */
	bool bTeleportPose = false;
	FVector2D TeleportXYM = FVector2D::ZeroVector;

	/**
	 * Le point ou le joueur a demande a naitre, choisi dans le menu.
	 *
	 * DISTINCT DE LA TELEPORTATION, ET LA DIFFERENCE EST VOULUE. Une
	 * destination de `Worldseed.Lieux` ne se corrige JAMAIS : on va voir CETTE
	 * arche, et la deplacer de trois cents metres pour trouver du plat
	 * raterait ce qu'on venait voir. Un depart, lui, doit etre corrige : on
	 * vise « par la », a la resolution d'un clic sur un globe, et naitre sur
	 * une paroi a soixante degres ou au-dessus d'une galerie ne rend service a
	 * personne. Deux intentions, donc deux drapeaux -- les confondre donnerait
	 * tort a l'une des deux.
	 *
	 * Il n'est consomme QU'UNE FOIS : le filet de rattrapage rearme la mise en
	 * place, et rejouer le depart y ramenerait le joueur a chaque chute.
	 */
	bool bDepartDemande = false;
	FVector2D DepartXYM = FVector2D::ZeroVector;

	/** Champs continus du climat : ils n'expliquent, ils ne generent rien. */
	const TArray<float>& Continentalite() const
	{
		return Monde.IsValid() ? Monde->Continentality : FloatsVides();
	}
	const TArray<float>& SaisonAmpC() const
	{
		return Monde.IsValid() ? Monde->SeasonalAmpC : FloatsVides();
	}

	FWorldseedDensityRules DensityRules;

	/**
	 * LE CHAMP DE DENSITE, PARTAGE LUI AUSSI -- ET C'EST LA LE VRAI DEFAUT.
	 *
	 * IL ETAIT UN MEMBRE PAR VALEUR, et chaque travail de maillage en capturait
	 * l'ADRESSE. Le commentaire du lancement disait, a juste titre, « ce qui NE
	 * serait pas sur, c'est de capturer l'acteur » -- mais on capturait un
	 * pointeur DANS l'acteur, ce qui revient au meme des qu'il meurt.
	 *
	 * ET `EndPlay` ANNULE SANS ATTENDRE, ce qui est le bon choix : attendre
	 * bloquerait le fil de jeu pendant la fermeture. Un travail deja entre dans
	 * `Build` continue donc quelques millisecondes a lire une memoire que le
	 * ramasse-miettes va reprendre. La fenetre est courte -- le GC ne passe pas
	 * dans la meme trame -- mais c'est la forme exacte d'un plantage rare au
	 * changement de niveau : celui qu'on ne reproduit jamais et qu'on ne sait
	 * donc pas corriger.
	 *
	 * Une reference partagee ferme la question par CONSTRUCTION : le travail en
	 * tient une, donc le champ lui survit, et l'acteur peut mourir quand il
	 * veut. C'est exactement ce que le meme fichier fait deja pour les
	 * primitives de grottes -- « le fil de maillage ne doit rien tenir qui
	 * puisse mourir avant lui » -- et qu'il ne faisait pas pour le champ.
	 */
	TSharedPtr<const FWorldseedDensity, ESPMode::ThreadSafe> Density;

	/**
	 * Ou en est le balayage des masques perimes.
	 *
	 * IL EST BORNE PAR PASSE, ET LA MESURE L-A EXIGE. Repasser sur TOUTES les
	 * feuilles a chaque mise a jour coute `feuilles x 6 x niveaux` descentes de
	 * NiveauEn, et cela se voit : a 2400 m le p95 montait a 14 ms pour une
	 * moyenne de 6,6 -- un pic periodique, exactement la cadence de la passe.
	 * Le controle qui l-a prouve : deux anneaux a 2400 m (3 934 feuilles, deux
	 * niveaux) donnent le MEME pic que trois anneaux (2 436 feuilles, trois
	 * niveaux), alors que les comptes de chunks n-ont rien de commun. C-est donc
	 * le produit qui compte, pas le nombre de chunks -- et le niveau 3, un temps
	 * soupconne, est innocent.
	 */
	double TotalUpdateMs = 0.0;
	double WorstUpdateMs = 0.0;
	int32 UpdateCount = 0;

	/**
	 * Les chunks portent-ils leur ombre ? OUI, sauf pendant une mesure.
	 *
	 * ELLE N-EST PAS UN REGLAGE, C-EST UN INSTRUMENT. Le moteur signale a
	 * l-ecran un debordement de la file de marquage du VSM, cause par le
	 * nombre de nos chunks non-Nanite qui couvrent beaucoup de pages d-ombre.
	 * Le repli du shader est CORRECT mais plus lent ; personne ne sait
	 * combien. Cette bascule sert a le chiffrer par un A/B sur la meme
	 * binaire -- `-WorldseedOmbres=0` -- et non a livrer un monde sans ombre
	 * portee. Elle n-est donc PAS une UPROPERTY : il ne faut pas pouvoir la
	 * poser par megarde dans un Blueprint.
	 */
	bool bOmbresChunks = true;

	int32 CurseurMasque = 0;

	TMap<FWorldseedChunkKey, FWorldseedVoxelChunkState> Chunks;

	FTimerHandle UpdateTimer;

	bool bWorldAdopted = false;
	bool bWorldReady = false;
	bool bPlayerHeld = false;
	bool bPlayerReleased = false;

	/** Cumuls pour le releve. */
	int32 BuiltChunks = 0;
	int32 EmptyChunks = 0;

	/** Travaux annules puis repris. Un compteur qui monte sans fin est un signe. */
	int32 AbandonedChunks = 0;

	/** Chunks ou le mailleur n'a rien rendu malgre une traversee. */
	int32 DegenerateChunks = 0;
	int32 TotalTriangles = 0;
	double TotalMeshMs = 0.0;
	double WorstMeshMs = 0.0;

	/**
	 * Temps passe sur le FIL DE JEU a televerser les chunks.
	 *
	 * LE MAILLAGE EST DEJA HORS DU FIL DE JEU, et la cuisson de collision
	 * aussi (bUseAsyncCooking). Ce qui reste sur le fil de jeu est la
	 * creation du COMPOSANT -- un par chunk -- et son enregistrement, plus
	 * la copie du maillage. C est donc le seul candidat restant pour
	 * expliquer le debit, et il faut le MESURER avant de le supposer : le
	 * depot a une note perimee affirmant que la cuisson est synchrone, ce
	 * qui n est plus vrai depuis longtemps.
	 */
	double TotalUploadMs = 0.0;
	double WorstUploadMs = 0.0;
	int32 UploadCount = 0;

	/**
	 * Temps passe sur le FIL DE JEU a peindre les sommets, et le nombre peint.
	 *
	 * IL EXISTE PARCE QUE LA MESURE D'A COTE L'EXCLUAIT. Le chrono du
	 * televersement demarrait APRES `PaintVertices` : le « 0,21 ms par chunk »
	 * qui a justifie de porter `UploadsPerPass` de 6 a 16 ne mesurait pas
	 * cette passe, qui echantillonne le relief en BICUBIQUE pour chaque
	 * sommet -- seize lectures dispersees -- plus une descente dans la pile
	 * stratigraphique.
	 *
	 * ON COMPTE AUSSI LES SOMMETS, et ce n'est pas du luxe : un temps par
	 * CHUNK ne se compare a rien, puisqu'un chunk plat et une paroi n'en
	 * portent pas le meme nombre. Le cout par SOMMET, lui, est la grandeur
	 * qui dit si le traitement est cher ou si c'est la geometrie qui est
	 * abondante -- et ce sont deux remedes opposes.
	 */
	double TotalPaintMs = 0.0;
	double WorstPaintMs = 0.0;
	int64 TotalPaintVerts = 0;

	/**
	 * L'ENTONNOIR DE LA TEINTE DE ROCHE, et pourquoi il fallait le compter.
	 *
	 * La stratigraphie est calculee, elle coute dans l'erosion et dans le
	 * sapement, la peinture des sommets lit bien `WorldseedStrata::BancAt` --
	 * et sans eclairage une paroi de canyon est d'UNE SEULE COULEUR. Les
	 * rayures que le code promet n'atteignent pas l'ecran.
	 *
	 * Une couverture trop faible peut venir de cinq gardes differentes, et sans
	 * le compte de chacune on regle au hasard la mauvaise -- ce que ce depot a
	 * deja paye quatre fois de suite sur le routage des galeries, puis sur les
	 * mesas. L'entonnoir dit LAQUELLE mord.
	 */
	mutable int64 PeintureSommets = 0;
	mutable int64 PeintureSousLaSurface = 0;
	mutable int64 PeintureSerieActive = 0;
	mutable int64 PeintureTeintee = 0;
	mutable double PeintureProfondeurSomme = 0.0;
	mutable double PeintureProfondeurMax = -1e30;
	mutable double PeintureProfondeurMin = 1e30;

	/** Combien de sommets par banc de la serie, pour voir si elle RAYE. */
	mutable TArray<int32> PeintureParBanc;

	/**
	 * CE QUE LA PEINTURE ECRIT VRAIMENT, ET POURQUOI IL FALLAIT LE COMPTER.
	 *
	 * Huit etats ont innocente la palette : teinte unique, teinte coupee,
	 * lumieres coupees, diaclases coupees -- rien ne deplace le noir des
	 * parois. Reste une question qu'aucun de ces huit ne pose : la peinture
	 * ECRIT-ELLE seulement du noir ? Tant qu'on ne le sait pas, on cherche la
	 * cause d'une couleur dont on n'a jamais verifie qu'elle vient de nous.
	 *
	 * Ces compteurs portent sur la couleur FINALE de chaque sommet, apres
	 * toutes les branches. Le seuil est celui de la mesure a l'image -- 70 sur
	 * 255 en sRGB -- converti par la MEME table que le catalogue, donc
	 * exactement comparable a ce qu'on compte sur une capture.
	 */
	mutable int64 PeintureSombresEcrits = 0;
	mutable double PeintureLumMin = 1e30;
	mutable double PeintureLumMax = -1e30;

	/** Les sombres ecrits, ventiles par BRANCHE : c'est la piste a remonter. */
	mutable int64 PeintureSombresParCause[4] = {};
	mutable int64 PeintureParCause[4] = {};

	/**
	 * LA CARTE DES CAUSES : chaque sommet peint par la branche qui l'a decide,
	 * en aplats francs et TOUS CLAIRS.
	 *
	 * C'est le temoin de couleur du depot applique a un diagnostic : « une
	 * couleur franche ne se compare a rien, elle est la ou elle n'est pas ».
	 * Toutes les teintes de la carte sont au-dessus du seuil de noir, donc
	 * TOUT PIXEL NOIR SUR UNE CAPTURE EN CARTE DES CAUSES N'EST PAS UN SOMMET
	 * PEINT PAR NOUS -- c'est un trou, une face arriere, ou un autre acteur.
	 * La reponse est binaire, et c'est ce qu'on cherche.
	 *
	 * A regarder avec `ShowFlag.Lighting 0`, sans quoi l'eclairage assombrit
	 * les aplats et l'on ne sait plus si le noir est peint ou ombre.
	 */
	bool bCarteDesCauses = false;

	/**
	 * LA CARTE DES ANNEAUX : chaque chunk peint par son NIVEAU.
	 *
	 * VERT le niveau 0 (voxel 1 m), BLEU le 1 (2 m), ROUGE le 2 (4 m), puis
	 * JAUNE et MAGENTA si un jour il y en a plus.
	 *
	 * ELLE MONTRE D'UN COUP D'OEIL CE QU'AUCUN COMPTE NE MONTRE : ou tombent
	 * reellement les paliers, et surtout si un chunk ROUGE est pose PAR-DESSUS
	 * du VERT -- c'est-a-dire un orphelin, la geometrie dessinee deux fois. Le
	 * sondage de vue, lui, ne voit que les TROUS ; il est aveugle au doublon,
	 * qui remplit l'image au lieu de la vider.
	 *
	 * C'est le temoin de couleur du depot, une fois de plus : « une couleur
	 * franche ne se compare a rien, elle est la ou elle n'est pas ». Les cinq
	 * teintes sont franches et distinctes, donc la lecture est binaire.
	 *
	 * A regarder avec `ShowFlag.Lighting 0` : eclairee, une face raide en vert
	 * sombre et une face plate en bleu sombre se confondent, et l'on perdrait
	 * exactement ce qu'on est venu voir.
	 */
	bool bCarteDesAnneaux = false;

	double FirstFillSeconds = 0.0;
	double StartSeconds = 0.0;
};
