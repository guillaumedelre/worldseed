// Worldseed - semer la vegetation sur un chunk de terrain voxel.

#pragma once

#include "CoreMinimal.h"

#include "Procedural/WorldseedRecettes.h"

struct FWorldseedBiomeMap;
struct FWorldseedGeometry;
struct FWorldseedVoxelMesh;

/**
 * L'emprise au sol d'un pan de falaise, en centimetres.
 *
 * UNE BOITE ORIENTEE, PAS UN DISQUE -- contrairement aux roches du semis. Un
 * pan est long et mince : le disque qui le contiendrait degarnirait tout
 * autour de lui, et celui qui tiendrait dedans laisserait de l'herbe traverser
 * ses deux extremites. Une roche, elle, recoit un lacet ALEATOIRE et n'a donc
 * pas d'axe privilegie -- le disque y est le bon equivalent.
 *
 * ELLE VIENT DU TERRAIN, PAS DU SEMEUR. Les pans sont poses par une passe a
 * part, AVANT la vegetation du meme chunk ; le semeur ne les connaitrait
 * jamais si on ne les lui donnait pas -- et c'est exactement ce qui a mis de
 * l'herbe au travers d'un pan, signale en jeu.
 */
struct FWorldseedEmpriseParoi
{
	FVector2D CentreCm = FVector2D::ZeroVector;

	/** Demi-cotes dans le repere du pan, echelle comprise. */
	FVector2D DemiCm = FVector2D::ZeroVector;

	/** Le lacet, precalcule : la boucle du semis est la plus chaude du module. */
	float CosLacet = 1.0f;
	float SinLacet = 0.0f;
};

/** Une plante a poser : quelle espece, ou, et comment. */
struct FWorldseedPlante
{
	/** Index dans `FWorldseedRecettes::Catalogue`. */
	int32 Espece = 0;

	/** Transformation, en CENTIMETRES dans le repere de l'acteur terrain. */
	FTransform Transform;
};

/**
 * Les reglages du semis, tout ce qui ne vient pas des recettes.
 *
 * ILS NE VIVENT NI DANS `world_rules.json` NI DANS LES RECETTES. Le premier est
 * hache pour invalider les mondes en cache -- y poser une densite d'herbe
 * imposerait deux cents secondes de regeneration pour un reglage de decor. Les
 * secondes decrivent CE QU'ON SEME, pas comment le semeur travaille.
 */
struct FWorldseedVegetationRegles
{
	/**
	 * Multiplie tous les pas de grille. Au-dessus de 1, le monde s'eclaircit.
	 *
	 * C'EST LE LEVIER DE COUT, ET IL EST QUADRATIQUE : doubler le pas divise
	 * par quatre le nombre d'instances. La densite se lit dans le PAS DE
	 * GRILLE, jamais dans le nombre d'especes d'une couche -- ajouter des
	 * maillages a une couche existante donne de la variete a cout IDENTIQUE,
	 * mesure a 344 482 instances avant comme apres l'ajout de 179 maillages.
	 */
	float PasMultiplicateur = 1.0f;

	/** A zero, aucun semis. Temoin sans recompiler. */
	float Densite = 1.0f;

	/**
	 * Plafond d'instances par chunk, toutes couches confondues.
	 *
	 * UNE CEINTURE, PAS UN REGLAGE. Un chunk de 32 m au pas du tapis (150 cm)
	 * porte environ 455 brins ; huit couches en portent plus de mille. Si une
	 * recette est un jour retouchee a la main avec un pas absurde, ce plafond
	 * evite qu'un seul chunk fasse tomber la trame -- et le journal le dit,
	 * plutot que de laisser chercher.
	 */
	int32 PlafondParChunk = 4000;

	/**
	 * Ne pas semer au-dela de cette distance du joueur, en metres.
	 *
	 * LE RAYON DE VUE VA A 2400 m, ET SEMER SI LOIN N'A PAS DE SENS : un brin
	 * d'herbe de vingt centimetres disparait bien avant, par sa propre distance
	 * de coupe (5000 a 8000 cm dans les recettes du tapis). On paierait des
	 * centaines de milliers d'instances invisibles.
	 */
	float RayonSemisM = 350.0f;

	/**
	 * Altitude sous laquelle rien ne pousse, en metres.
	 *
	 * RIEN NE POUSSE SOUS LA MER, ET LE SEMIS NE LE SAVAIT PAS. Il ne testait
	 * que la tranche du chunk et la pente : un fond de baie a moins vingt
	 * metres passait donc toutes les portes et recevait de l'herbe, des
	 * buissons et des arbres, qu'on voyait ensuite a travers l'eau.
	 *
	 * ZERO EST LE NIVEAU DE LA MER, et ce n'est pas une convention arbitraire :
	 * le plugin Water pose l'ocean en plan infini a l'altitude zero, et toute
	 * la chaine s'y cale deja -- la plage se fond de `BeachTopM` vers 0, le
	 * temoin de mer peint les sommets sous zero, et les chambres de grotte sont
	 * interdites sous cette cote.
	 *
	 * LA MARGE EST A ZERO A DESSEIN. La relever degarnirait une bande le long
	 * de tout le littoral, ce qui se verrait davantage que le defaut corrige :
	 * une plage nue sur plusieurs metres de large n'est pas plus credible que
	 * de l'herbe noyee. La laisser NEGATIVE serait le seul cas ou l'on semerait
	 * volontairement sous l'eau -- des herbiers, un jour.
	 */
	float AltitudeMinM = 0.0f;
};

/**
 * L'ENTONNOIR DU SEMIS, ET IL N'EST PAS DECORATIF.
 *
 * Une couverture trop faible peut venir de six gardes differentes, et sans le
 * compte de chacune on regle au hasard la mauvaise. Ce depot l'a paye quatre
 * fois de suite sur le routage des galeries -- quatre corrections successives,
 * toutes justes, dont aucune ne bougeait le chiffre parce que la cause etait
 * ailleurs -- puis de nouveau sur les mesas, ou quatre criteres physiques
 * laissaient un gisement sain de 4,74 % des terres et c'est un cinquieme, le
 * masque de region, qui etranglait tout a 0,14 %.
 *
 * On compte donc CHAQUE porte separement.
 */
struct FWorldseedVegetationReleve
{
	/** Points de grille examines, toutes couches confondues. */
	int64 Testes = 0;

	/** Rejetes parce que leur point tombe dans le chunk voisin. */
	int64 HorsChunk = 0;

	/** Rejetes parce que la case de la grille de sol est vide. */
	int64 CaseVide = 0;

	/** Rejetes parce que la surface lue n'est pas dans la tranche du chunk. */
	int64 TrancheZ = 0;

	/**
	 * CASES rejetees sous le niveau de la mer. Voir `AltitudeMinM`.
	 *
	 * COMPTE A PART DES CHUNKS, ET CE N'EST PAS DU ZELE : un chunk entierement
	 * immerge est refuse d'un bloc, une case l'est une par une. Additionner les
	 * deux donnerait un nombre dont on ne saurait pas s'il compte des plantes
	 * ou des chunks -- exactement l'agregat sur deux populations qui a fait
	 * regler quatre fois le mauvais bouton sur le routage des galeries.
	 */
	int64 SousLaMer = 0;

	/** CHUNKS refuses d'un bloc, entierement sous le niveau de la mer. */
	int32 ChunksSousLaMer = 0;

	/**
	 * Rejetes parce que le point n'est pas du bon substrat.
	 *
	 * UNE COUCHE DE BIOME SUR L'ESTRAN, OU UNE COUCHE D'ESTRAN AILLEURS. Les
	 * deux sont normaux -- un chunk est presque toujours a cheval -- et ce
	 * compteur n'est pas la pour signaler un defaut mais pour qu'un estran nu
	 * se distingue d'un estran qu'on aurait oublie de semer.
	 */
	int64 Substrat = 0;

	/** Rejetes par les bornes de pente de leur couche. */
	int64 Pente = 0;

	/** Rejetes par le bruit de taches de leur couche. */
	int64 Taches = 0;

	/** Rejetes par la densite globale. */
	int64 Densite = 0;

	/**
	 * Rejetes parce que leur point tombe dans l'emprise d'une ROCHE.
	 *
	 * IL COMPTE DEUX CHOSES QU'IL FAUT SAVOIR DISTINGUER D'UN ESTRAN NU OU
	 * D'UNE COUCHE VIDE : de l'herbe ecartee d'un rocher, et un rocher ecarte
	 * d'un autre rocher. Un zero sur un monde qui porte des rochers dit que
	 * les gabarits ne sont pas arrives -- `RayonEspeceCm` vide -- et non que
	 * rien ne se chevauchait.
	 */
	int64 SousLaRoche = 0;

	/**
	 * Rejetes parce que leur point tombe sous un PAN DE FALAISE.
	 *
	 * COMPTE A PART DE LA ROCHE, et ce depot sait pourquoi : un agregat sur
	 * deux populations ne se corrige pas, il se decompose. Les pans viennent
	 * d'une autre passe, ils font 77 m et il y en a quelques dizaines quand
	 * les rochers se comptent par milliers -- un seul chiffre ne dirait pas
	 * laquelle des deux gardes a mordu.
	 */
	int64 SousLaParoi = 0;

	int32 Posees = 0;
	int32 Plafonnees = 0;
	int32 HorsRayon = 0;
	int32 SansRecette = 0;
};

/**
 * LE SEMIS SUIT LE CHUNK, ET C'EST CE QUI LE REND POSSIBLE.
 *
 * POURQUOI PAS PCG, qui pourtant porte deja ce semis sur le Landscape. Trois
 * raisons mesurees, toutes au registre :
 *
 * 1. PCG echantillonne une SURFACE qu'il sait lire -- un Landscape, une forme.
 *    Un `ProceduralMeshComponent` n'en est pas une ; il resterait le lancer de
 *    rayon dans le monde, qui exige que la collision existe DEJA la ou PCG
 *    seme. Or nos chunks apparaissent et disparaissent autour du joueur, et
 *    les cellules de partition de PCG suivent leur propre calendrier : PCG
 *    semerait dans le vide, sans que rien ne le signale.
 * 2. La generation PCG a l'execution a coute cher a regler -- « quatre essais,
 *    15 876 acteurs de partition persistants et 847 Mo pour zero instance »,
 *    puis un ordre d'operations dont l'inversion fait tomber l'editeur au tick
 *    SUIVANT. Rien de tout cela n'a d'equivalent ici.
 * 3. Le chunk connait DEJA sa geometrie et ses normales : elles viennent
 *    d'etre calculees pour le mailler. Les redemander a un lancer de rayon
 *    serait payer deux fois la meme information.
 *
 * Le cycle de vie devient alors exact : les plantes naissent avec leur chunk et
 * meurent avec lui, sans qu'aucun systeme tiers ait a s'accorder avec le
 * streaming.
 */
namespace WorldseedVegetation
{
	/**
	 * Seme la vegetation d'un chunk.
	 *
	 * `Mesh` fournit la geometrie -- positions en centimetres et normales --
	 * d'ou sortent l'altitude et la pente. `Biomes` et `Geo` disent quel biome
	 * regne en chaque point, donc quelle recette appliquer. `OrigineM` est la
	 * position du joueur en metres, pour le rayon de semis.
	 */
	WORLDSEED_API void Semer(const FWorldseedVoxelMesh& Mesh,
		const FVector& OrigineChunkCm, double CoteChunkCm,
		const FWorldseedRecettes& Recettes,
		const FWorldseedBiomeMap& Biomes, const FWorldseedGeometry& Geo,
		const FWorldseedVegetationRegles& Regles, int32 Graine,
		const FVector2D& OrigineM,
		const TArray<FWorldseedEmpriseParoi>& Parois,
		TArray<FWorldseedPlante>& Out, FWorldseedVegetationReleve& Releve);
}
