// Worldseed - le champ de densite : ce qui est roche, ce qui est air.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedRules.h"

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
	 * Bornes d'altitude de la surface macro sur une empreinte rectangulaire.
	 *
	 * C'EST CE QUI REND LE STREAMING 3D ABORDABLE. La grille 2D dit ou peut se
	 * trouver la surface ; on sait donc, sans evaluer une seule fois le champ,
	 * quels chunks peuvent la contenir et lesquels sont du plein ou du vide.
	 */
	void SurfaceRangeM(double MinX, double MinY, double MaxX, double MaxY,
		float& OutMinM, float& OutMaxM) const;

	const FWorldseedDensityRules& GetRules() const { return Rules; }

private:
	/** Creusement des galeries en un point : positif dans le vide. */
	double CaveAt(const FVector& PosM, double DepthM) const;

	FWorldseedGeometry Geometry;
	const TArray<float>* ElevationM = nullptr;
	float HeightExaggeration = 1.0f;
	int32 Seed = 0;
	FWorldseedDensityRules Rules;
};
