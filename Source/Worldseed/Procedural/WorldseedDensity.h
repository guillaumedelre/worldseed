// Worldseed - le champ de densite : ce qui est roche, ce qui est air.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedFins.h"

struct FWorldseedCaveLocal;

/**
 * Reglages du champ, lus dans la section "voxel" de world_rules.json.
 *
 * AUCUNE VALEUR N'EST EN DUR DANS LE CODE. C'est la regle du projet, et les
 * surfaces viennent d'en souffrir : leurs seuils vivaient en C++ pendant que le
 * fichier de regles en portait d'autres, et les deux moities ont diverge sans
 * que rien ne le signale.
 */
struct WORLDSEED_API FWorldseedDensityRules
{
	/** Cote d'un voxel, en metres. Fixe la finesse du creusement. */
	float VoxelSizeM = 1.0f;

	/**
	 * Mailler avec Transvoxel plutot qu.avec FMarchingCubes du moteur.
	 *
	 * A RESOLUTION UNIFORME LES DEUX DOIVENT RENDRE LA MEME SURFACE, et c.est
	 * precisement ce que ce drapeau sert a verifier : il rend l.A/B possible
	 * sans recompiler, donc comparable. Transvoxel n.apporte rien de visible
	 * tant que les anneaux de resolution ne sont pas poses -- il apporte le
	 * mailleur qui saura les raccorder sans fissure, ce que le mailleur du
	 * moteur ne peut pas faire.
	 */
	bool bTransvoxel = false;

	/**
	 * Les anneaux de resolution, arbitres par le proprietaire le 20 septembre.
	 *
	 * ILS VIVENT DANS LES REGLES PARCE QUE CE SONT DES SEUILS, et la regle du
	 * depot est nette la-dessus. Le prix a payer est qu-en changer force une
	 * regeneration du monde, l-empreinte du fichier portant sur son contenu
	 * entier : c-est acceptable pour des valeurs qu-on arrete une fois.
	 *
	 * La ligne de commande reste prioritaire, pour que le banc puisse comparer
	 * deux configurations sans recompiler.
	 */
	int32 NiveauMax = 0;
	float RayonAnneau0M = 300.0f;
	float LoadRadiusM = 0.0f;
	float LargeurTransition = 0.5f;

	/**
	 * Epaisseur de la bande creusable sous la surface, en metres.
	 *
	 * Sous elle, la densite est forcee au plein : le socle n'est JAMAIS maille.
	 * Ce n'est pas une economie de rendu — le marching cubes ne maille que les
	 * changements de signe — c'est une borne sur la comptabilite des chunks,
	 * qui autrement s'etendrait sur les six cents metres d'amplitude du monde.
	 */
	float BandDepthM = 100.0f;

	/**
	 * Amplitude du deplacement 3D de la surface, en metres.
	 *
	 * C'EST LUI QUI CREE LES SURPLOMBS, et il le fait sans terme special : en
	 * ajoutant un bruit 3D a la distance a la surface, celle-ci cesse d'etre
	 * une fonction de x et y. La ou le bruit depasse la pente locale, le
	 * terrain se replie et l'on peut passer dessous.
	 *
	 * Le terme n'agit que la ou il peut changer le signe, donc dans une bande
	 * de cette amplitude autour de la surface : inutile de l'estomper.
	 */
	float OverhangAmplitudeM = 8.0f;

	/**
	 * Amplitude du DETAIL, en metres.
	 *
	 * IL OCCUPE UNE BANDE QUE RIEN NE COUVRAIT. Le terme de surplomb part de
	 * 62 m de longueur d'onde et descend a 15 en trois octaves : il donne le
	 * grain du relief, pas sa texture. Sous quinze metres il n'y avait plus
	 * rien, et la maille de simulation en fait 31 a 64 km -- d'ou un monde qui
	 * se lit en grands polygones.
	 */
	float DetailAmplitudeM = 2.5f;

	float DetailFrequency = 0.085f;
	int32 DetailOctaves = 4;

	/**
	 * Pente de reference du detail, et son plancher.
	 *
	 * UNE PLAGE ET UN FOND DE VALLEE SONT LISSES DANS LA NATURE. Deux metres
	 * de bosses y donnent un champ de taupinieres, et sur une plage cela
	 * decoupe le trait de cote en flaques. Une paroi, elle, est rugueuse : le
	 * detail suit donc la pente, avec un plancher pour que le plat ne soit pas
	 * parfaitement lisse non plus.
	 */
	float DetailPenteMin = 0.15f;
	float DetailPenteRef = 0.6f;

	/** Hauteur sur laquelle le detail s'efface pres du niveau de la mer. */
	float DetailCoteM = 12.0f;

	/** Frequence du deplacement, en cycles par metre. */
	float OverhangFrequency = 0.016f;

	int32 OverhangOctaves = 3;

	/**
	 * Deplacement HORIZONTAL du point ou l'on lit le relief, en metres.
	 *
	 * C'EST LUI QUI FAIT LES VRAIS SURPLOMBS, et le terme vertical ci-dessus
	 * n'y arrive pas -- mesure : 0,00 % de colonnes franchissables meme a
	 * seize metres d'amplitude, parce qu'un fBm de Perlin normalise n'atteint
	 * jamais le gradient vertical de 1 qu'il faudrait.
	 *
	 * Le principe est different : a chaque altitude, on va lire le relief un
	 * peu PLUS LOIN, et le decalage tourne avec Z. Sur un terrain plat cela ne
	 * change presque rien -- le relief y est le meme a vingt metres pres. Sur
	 * une falaise, deux altitudes voisines lisent des endroits dont les
	 * altitudes different de dizaines de metres : la surface se replie, et l'on
	 * peut passer dessous. Les surplombs naissent donc exactement la ou ils
	 * sont credibles, sans qu'on ait eu a le demander.
	 */
	float OverhangWarpM = 25.0f;

	/** Frequence du deplacement horizontal, en cycles par metre. */
	float OverhangWarpFrequency = 0.012f;

	/**
	 * Frequence des galeries, en cycles par metre.
	 *
	 * Une valeur de 1/120 donne des tubes d'une centaine de metres de portee.
	 */
	float CaveFrequency = 0.008f;

	int32 CaveOctaves = 2;

	/**
	 * Seuil au-dela duquel une crete devient un vide, dans [0..1].
	 *
	 * LE REGLAGE LE PLUS SENSIBLE DU LOT. Le bruit a cretes passe la plupart de
	 * son temps loin de 1 ; ne creuser qu'au-dessus d'un seuil eleve donne des
	 * tubes fins et rares, un seuil bas donne un gruyere. A regler a l'image,
	 * et a mesurer par la part de volume creuse.
	 */
	float CaveThreshold = 0.86f;

	/** Rayon de raccordement de l'union lisse des cavites, en metres. */
	float CaveBlendM = 2.5f;

	/** Rayon des galeries, en metres, pour un depassement de seuil maximal. */
	float CaveRadiusM = 9.0f;

	/**
	 * Profondeur sous laquelle une galerie peut s'ouvrir, en metres.
	 *
	 * Sans elle, les tubes perforent le sol partout et le monde ressemble a une
	 * ecumoire. Avec, les entrees de grotte restent rares et se mefient des
	 * hasards du relief.
	 */
	float CaveSurfaceFadeM = 25.0f;

	// --- arches et abris sous roche ---------------------------------------------

	/**
	 * Profondeur PERPENDICULAIRE sous laquelle une arche peut se creuser, en metres.
	 *
	 * PERPENDICULAIRE, ET LA PRECISION N'EST PAS UN DETAIL. Le champ mesure une
	 * distance VERTICALE a la surface -- `Z - H(x,y)` -- et sur une falaise
	 * cette distance est enorme des le premier metre dans la roche, puisque la
	 * surface a l'aplomb se trouve loin au-dessus. Une porte posee sur elle ne
	 * mordrait donc JAMAIS la ou les arches se forment. On divise par la norme
	 * du gradient, `sqrt(1 + |grad H|^2)`, ce qui rend la distance vraie a la
	 * paroi au premier ordre.
	 */
	float ArchDepthM = 12.0f;

	/**
	 * Pente minimale pour qu'une arche se creuse, en degres.
	 *
	 * UNE ARCHE EST UNE FORME DE PAROI, PAS DE PLAINE, et ce n'est pas un gout :
	 * une nappe d'air creusee sous un terrain PLAT detacherait la calotte qui
	 * la surmonte -- un bloc flottant, exactement le defaut que la deformation
	 * produisait. Creusee dans un VERSANT, la meme nappe mord dans la paroi et
	 * son plafond reste accroche a la colline derriere. La pente est donc la
	 * condition qui rend le creusement sur.
	 */
	float ArchSlopeMinDeg = 38.0f;

	/**
	 * Frequence horizontale des nappes, en cycles par metre.
	 *
	 * Elle donne l'ETENDUE d'une arche : une valeur de 1/60 fait des poches
	 * d'une vingtaine de metres, ce qu'il faut pour percer un eperon.
	 */
	float ArchFrequencyXY = 0.017f;

	/**
	 * Frequence VERTICALE des nappes, en cycles par metre.
	 *
	 * ELLE DOIT ETRE BIEN PLUS GRANDE QUE L'HORIZONTALE, et c'est tout le
	 * principe. Un bruit isotrope fait des bulles ; comprimer sa periode en Z
	 * fait des NAPPES -- larges, minces, horizontales. Une nappe qui mord dans
	 * un versant donne une visiere ; une nappe qui traverse un eperon donne une
	 * arche. Le rapport entre les deux frequences est la forme.
	 */
	float ArchFrequencyZ = 0.080f;

	int32 ArchOctaves = 2;

	/** Seuil au-dela duquel la nappe devient un vide, dans [-1..1]. */
	float ArchThreshold = 0.55f;

	/** Profondeur maximale du creusement, en metres. */
	float ArchAmplitudeM = 5.0f;

	// --- diaclases ------------------------------------------------------------

	/**
	 * Ouverture maximale d'une diaclase, en metres.
	 *
	 * DEUX METRES N'EST PAS UN CHOIX ESTHETIQUE, C'EST UN PLANCHER IMPOSE PAR LE
	 * VOXEL. Une diaclase reelle s'ouvre de quelques centimetres a un metre ;
	 * a un metre de voxel, le marching cubes ne peut tout simplement pas
	 * representer une fente plus etroite que deux voxels -- elle disparaitrait
	 * ou se reduirait a un chapelet d'artefacts. On ne retient donc que les
	 * fissures ELARGIES, celles que la meteorisation a ouvertes au point qu'un
	 * homme y passe. Ce sont aussi les seules qui interessent le jeu.
	 */
	float JointApertureM = 2.4f;

	/**
	 * Taille d'une maille du reseau de fractures, en metres.
	 *
	 * C'est l'espacement entre deux diaclases d'une meme famille. Le granite
	 * terrestre se debite en blocs metriques a decametriques ; on vise le haut
	 * de cette plage, sans quoi le massif serait decoupe en confettis.
	 */
	float JointCellM = 42.0f;

	/**
	 * Aplatissement vertical du reseau, dans [0..1].
	 *
	 * IL FAIT TOUTE LA DIFFERENCE ENTRE UNE FRACTURE ET UNE BULLE. Un Voronoi
	 * isotrope donne des cellules rondes, donc des parois orientees dans tous
	 * les sens ; en comprimant l'axe Z avant de l'evaluer, les cellules
	 * deviennent des PRISMES hauts et les parois des plans quasi verticaux --
	 * ce qu'est une diaclase. A 1, on retrouve des bulles.
	 */
	float JointAnisoZ = 0.22f;

	/**
	 * Profondeur au-dela de laquelle une diaclase est refermee, en metres.
	 *
	 * L'INVERSE DU FONDU DES GALERIES, ET C'EST VOULU. Un karst se creuse en
	 * profondeur et s'estompe pres du sol ; une diaclase fait le contraire. La
	 * roche en profondeur est sous la charge de tout ce qui la surmonte, et
	 * cette contrainte referme les joints ; c'est la decompression et la
	 * meteorisation, pres de la surface, qui les ouvrent. Une fissure de
	 * granite est donc une forme de SURFACE, et elle s'ouvre a l'air libre --
	 * c'est par la qu'on y entre.
	 */
	float JointDepthM = 45.0f;

	/**
	 * Frequence du masque de zone, en cycles par metre.
	 *
	 * SANS CE MASQUE, TOUT LE GRANITE DU MONDE SERAIT TRANCHE. Un reseau de
	 * diaclases ouvertes est un accident local -- un chaos de blocs, un
	 * escarpement decomprime -- pas l'etat ordinaire d'un massif. Le masque
	 * decide OU le reseau s'ouvre, et le laisse ferme partout ailleurs.
	 */
	float JointZoneFrequency = 0.0016f;

	/**
	 * Seuil du masque de zone, dans [-1..1].
	 *
	 * C'EST UN SEUIL ET NON UNE PART, et la distinction a ete payee comptant.
	 * Ce reglage s'appelait d'abord "part de la roche ou les diaclases
	 * s'ouvrent" et valait 0,16 ; le code en tirait un seuil en supposant le
	 * bruit UNIFORME sur [-1..1]. Un Perlin ne l'est pas : il se masse autour
	 * de zero et n'atteint presque jamais ses bornes. Mesure : le seuil 0,68
	 * cense garder 16 % n'en gardait que 1,59. Le nom mentait.
	 *
	 * COURBE RELEVEE sur la graine 20260909, part de la roche insoluble emergee :
	 *   0,15 -> 32,53 %   0,35 -> 14,44 %   0,55 -> 5,00 %
	 *   0,25 -> 21,52 %   0,45 ->  9,25 %   0,68 ->  1,59 %
	 *
	 * Retenu 0,45, arbitre par le proprietaire : neuf pour cent du granite. Un
	 * chaos de blocs reste un accident local, pas l'etat ordinaire d'un massif,
	 * mais a 0,55 il se trouvait trop rarement pour etre une rencontre.
	 */
	float JointZoneThreshold = 0.45f;

	/**
	 * LE TIRAGE : cote de la maille de region, en metres.
	 *
	 * UNE DIACLASE OUVERTE EST UNE PROVINCE, PAS UNE TEXTURE. Le masque de
	 * bruit seul repartit les fentes sur tout le granite du monde, en plus ou
	 * moins dense : il n'a aucune facon de dire « ici oui, la non ». Le tirage
	 * le fait -- une region porte un reseau ou n'en porte aucun, et la reponse
	 * est un HACHAGE de la maille et de la graine, donc deterministe et
	 * reproductible sans rien stocker.
	 *
	 * LA TAILLE EST CONTRAINTE PAR UN PIEGE QUE LE DEPOT A DEJA PAYE sur les
	 * mesas : « l'intersection de deux ensembles peu nombreux est une LOTERIE
	 * sur la graine, pas une proportion ». A 4 km, ce monde de 64 x 32 porte
	 * 128 mailles -- assez pour que la part tiree soit une proportion et non un
	 * coup de des. Une maille de 16 km en donnerait huit, et la couverture
	 * sauterait du simple au triple d'une graine a l'autre.
	 */
	float JointRegionM = 4000.0f;

	/**
	 * LE TIRAGE : part des regions qui portent un reseau, dans [0..1].
	 *
	 * C'est la seule grandeur de cette section qui soit VRAIMENT une part : le
	 * hachage est uniforme par construction, contrairement au Perlin du masque.
	 * Une region sur trois a 0,33.
	 */
	float JointRegionPart = 0.33f;

	/**
	 * LA PENTE MINIMALE pour qu'un joint s'ouvre, en degres.
	 *
	 * SOURCE. Un joint de decompression s'ouvre la ou la roche est DECHARGEE et
	 * exposee : un escarpement, une crete, une paroi que l'erosion vient de
	 * degager. Sous une plaine, la meme roche porte la charge de sa couverture
	 * et ses joints restent serres -- et le peu qui s'ouvre se comble de sol et
	 * de vegetation. C'est le meme argument qui fait des mesas une forme ARIDE
	 * et des parois verticales une affaire de terrain nu.
	 *
	 * Le critere se lit sur le relief macro, jamais sur l'etiquette de biome --
	 * meme regle que le karst et que les plateaux.
	 */
	float JointPenteMinDeg = 22.0f;

	/**
	 * Largeur du fondu de pente, en degres.
	 *
	 * SANS LUI LE RESEAU S'ARRETE SUR UNE COURBE DE NIVEAU, ce qui se voit : un
	 * champ de fentes qui se termine net a mi-versant ne ressemble a rien. Le
	 * fondu etale l'extinction sur quelques degres, et comme il multiplie
	 * l'OUVERTURE, les dernieres fentes se referment au lieu de disparaitre.
	 */
	float JointPenteFonduDeg = 8.0f;

	/**
	 * Profondeur a laquelle la couleur devient celle de la ROCHE, en metres.
	 *
	 * SOUS TERRE, CE N'EST PLUS LE BIOME QUI HABILLE. La couleur des sommets
	 * etait calculee en 2D pure -- on lisait le biome de la colonne et on
	 * peignait -- donc une paroi de grotte a quarante metres sous une prairie
	 * rendait VERTE. Constate a l'image dans la salle sous le gouffre.
	 *
	 * Le fondu est genereux a dessein : la surface REELLE s'ecarte de la
	 * surface macro de plusieurs metres (amplitude des surplombs), donc un
	 * fondu court trancherait au mauvais endroit sur les falaises et les
	 * visieres. A douze metres, tout ce qui est franchement souterrain est de
	 * la roche et tout ce qui est en surface garde son biome.
	 */
	float RockColourFadeM = 12.0f;

	/**
	 * Le champ de lames de gres, charge par le MEME appel que le reste.
	 *
	 * IL EST ICI ET PAS AILLEURS POUR QU'ON NE PUISSE PAS L'OUBLIER. Pose en
	 * membre separe du champ de densite, il aurait fallu l'assigner a chaque
	 * endroit ou une densite est construite -- le terrain, chacune des sondes --
	 * et un seul oubli aurait rendu le terme inerte SANS AUCUN SIGNE. C'est
	 * exactement ce qui s'est produit avec la lithologie non branchee dans
	 * probe_voxel : la sonde annoncait 2,67 ms/chunk et mesurait le monde
	 * d'avant. En passant par les regles, il n'y a qu'un seul chemin.
	 */
	FWorldseedFinRules Fins;

	static FWorldseedDensityRules FromRules(const UWorldseedRules& Rules);
};

/**
 * Le champ de densite du monde.
 *
 * CONVENTION DE SIGNE, ET ELLE EST STRUCTURANTE : la valeur est NEGATIVE dans
 * la roche et POSITIVE dans l'air, et la surface est l'isovaleur zero. C'est la
 * convention d'une fonction de distance signee, celle qu'attend FMarchingCubes.
 *
 * IL N'Y A AUCUNE GRILLE 3D EN MEMOIRE, et il ne peut pas y en avoir : a un
 * metre de cote, un monde de 16 x 8 km sur six cents metres d'amplitude ferait
 * soixante-seize MILLIARDS de voxels. Le champ est donc une FONCTION, evaluee a
 * la demande, batie sur la grille 2D deja calculee par la chaine plus le bruit
 * 3D. Seules les modifications du joueur seront stockees, et elles sont eparses
 * par nature.
 *
 * L'objet ne possede pas le relief : il garde une reference sur le tableau du
 * terrain. Il est donc valable tant que le terrain l'est, et il est SANS ETAT
 * MUTABLE — donc interrogeable depuis plusieurs fils de maillage a la fois.
 */
class WORLDSEED_API FWorldseedDensity
{
public:
	FWorldseedDensity() = default;

	/**
	 * ElevationM et Geometry doivent survivre a cet objet : rien n'est copie.
	 * HeightExaggeration reprend celle du terrain, sans quoi le champ et le
	 * relief affiche ne decriraient pas le meme monde.
	 */
	void Init(const FWorldseedGeometry& InGeometry, const TArray<float>& InElevationM,
		float InHeightExaggeration, int32 InSeed, const FWorldseedDensityRules& InRules);

	/**
	 * Branche la lithologie : sans elle, aucune diaclase n'est creusee.
	 *
	 * POURQUOI C'EST UN APPEL SEPARE ET NON UN PARAMETRE DE Init. La lithologie
	 * est facultative -- le champ doit rester evaluable sans elle, ne serait-ce
	 * que pour les sondes qui n'en ont pas besoin -- et surtout elle n'a pas la
	 * meme duree de vie : le relief vient du cache du monde, la roche est
	 * recalculee. Les melanger dans une seule signature obligerait chaque
	 * appelant a fournir les deux.
	 *
	 * Le tableau d'identifiants n'est PAS copie ; il doit survivre a cet objet.
	 * La petite table d'aptitudes, elle, l'est : elle tient en quelques flottants
	 * et cela evite de dependre aussi de la duree de vie des regles.
	 */
	void SetLithology(const struct FWorldseedLithology& InLithology,
		const struct FWorldseedLithologyRules& InRules);

	bool IsValid() const;

	/**
	 * Densite en un point, en METRES dans le repere de l'acteur terrain.
	 *
	 * POURQUOI DES METRES ET NON DES CENTIMETRES : le bruit travaille sur des
	 * frequences par metre, et les coordonnees d'un monde de seize kilometres
	 * en centimetres approchent le million — la ou un float perd ses decimales.
	 * La conversion en centimetres se fait au dernier moment, sur les sommets.
	 */
	double At(const FVector& PosM) const { return At(PosM, nullptr); }

	/**
	 * Densite en un point, avec les primitives de grottes qui touchent le chunk.
	 *
	 * POURQUOI LA LISTE ARRIVE PAR PARAMETRE ET NON PAR MEMBRE. Ce champ est
	 * partage par tous les fils de maillage et doit rester SANS ETAT MUTABLE.
	 * La liste, elle, est propre a un chunk : elle est extraite une fois par le
	 * mailleur et capturee dans sa lambda. Tester toutes les primitives a chaque
	 * voxel serait du O(voxels x primitives), redhibitoire des quelques milliers
	 * de capsules.
	 */
	double At(const FVector& PosM, const FWorldseedCaveLocal* Caves) const;

	/** Altitude de la surface macro en un point, exageration comprise. */
	float SurfaceHeightM(double X, double Y) const;

	/**
	 * La meme surface, mais en BILINEAIRE et sans exageration.
	 *
	 * RESERVEE AU CALCUL DE LA PENTE, et c'est une economie qui compte : la
	 * pente du detail demande quatre echantillons par evaluation du champ, soit
	 * SOIXANTE-QUATRE prises en bicubique. Elle n'a aucun besoin de continuite
	 * C1 -- elle module une amplitude, elle ne dessine rien. Mesure de l'ecart :
	 * 5,33 ms par chunk avec la bicubique partout.
	 */
	float SurfacePenteM(double X, double Y) const;

	/**
	 * Bornes d'altitude de la surface macro sur une empreinte rectangulaire.
	 *
	 * C'EST CE QUI REND LE STREAMING 3D ABORDABLE. La grille 2D dit ou peut se
	 * trouver la surface ; on sait donc, sans evaluer une seule fois le champ,
	 * quels chunks peuvent la contenir et lesquels sont du plein ou du vide.
	 */
	void SurfaceRangeM(double MinX, double MinY, double MaxX, double MaxY,
		float& OutMinM, float& OutMaxM) const;

	const FWorldseedDensityRules& GetRules() const { return Rules; }

	/**
	 * LA GARDE DE ZONE DES DIACLASES : 0 = fermee, 1 = pleinement ouverte.
	 *
	 * ELLE EST PUBLIQUE A DESSEIN, ET C'EST UNE REGLE DU DEPOT. La sonde
	 * reimplementait ce masque -- « meme masque que le champ, a la lettre » --
	 * donc elle validait une COPIE du mecanisme et non le mecanisme. Tant qu'il
	 * n'y avait qu'un appel a Perlin la copie tenait ; a la premiere garde
	 * ajoutee au champ, la sonde aurait continue a rendre l'ancien chiffre sans
	 * le dire. C'est exactement le « temoin non branche » que ce depot a deja
	 * paye sur probe_voxel et sur la verification des arches.
	 *
	 * ELLE NE PORTE PAS LA ROCHE : l'appelant l'a deja, et c'est la garde la
	 * moins chere -- une lecture de tableau -- donc elle reste en tete.
	 *
	 * Le produit des trois facteurs module l'OUVERTURE et non un booleen : une
	 * fente qui s'eteint se referme au lieu de disparaitre.
	 */
	float DiaclaseZoneAt(double X, double Y) const;

	/** Le TIRAGE seul : 1 si la region est tiree, 0 sinon. Discret par nature. */
	float DiaclaseTirageAt(double X, double Y) const;

	/** Le MASQUE de bruit seul, deja fondu sur ses bords. */
	float DiaclaseMasqueAt(double X, double Y) const;

	/** La PENTE seule, deja fondue. Quatre lectures de grille : la plus chere. */
	float DiaclasePenteAt(double X, double Y) const;

private:
	/** Creusement des galeries en un point : positif dans le vide. */
	double CaveAt(const FVector& PosM, double DepthM) const;

	/** Ouverture des diaclases en un point : positif dans le vide. */
	double JointAt(const FVector& PosM, double DepthM) const;

	/**
	 * Creusement des arches et abris sous roche : positif dans le vide.
	 *
	 * DepthM est la profondeur VERTICALE ; la fonction en tire elle-meme la
	 * distance perpendiculaire, dont elle a besoin et que l'appelant n'a pas.
	 */
	double ArchAt(const FVector& PosM, double DepthM) const;

	/**
	 * Aptitude de la roche a se dissoudre sous ce point, dans [0..1].
	 *
	 * LECTURE AU PLUS PROCHE VOISIN, JAMAIS EN BILINEAIRE. La lithologie est un
	 * champ d'IDENTIFIANTS : interpoler entre du calcaire et du granite
	 * fabriquerait une roche qui n'existe pas. Le projet a deja paye ce piege
	 * sur la carte des biomes lue par PCG -- 1,71 % des points affectes a un
	 * biome absent -- et la lecon vaut pour tout champ categoriel.
	 */
	float KarstifiableAt(double X, double Y) const;

	/**
	 * Durete de la roche sous ce point, dans [0..1]. Meme lecture categorielle.
	 *
	 * ELLE NE SE DEDUIT PAS DE LA KARSTIFICATION. Le gres se dissout peu (0,15)
	 * et le granite pas du tout (0,00), mais leurs duretes sont 0,55 et 0,95 :
	 * ce sont deux axes independants, et c'est la DURETE qui dit ou des lames
	 * peuvent se decouper.
	 */
	float DureteAt(double X, double Y) const;

	/** Decoupe des lames de gres par fentes paralleles : positif dans le vide. */
	double FinAt(const FVector& PosM, double DepthM) const;

	const TArray<uint8>* LithologyId = nullptr;
	TArray<float> KarstifiableParId;
	TArray<float> DureteParId;

	FWorldseedGeometry Geometry;
	const TArray<float>* ElevationM = nullptr;
	float HeightExaggeration = 1.0f;
	int32 Seed = 0;
	FWorldseedDensityRules Rules;
};
