#pragma once

#include "CoreMinimal.h"

class FWorldseedDensity;

/**
 * La cle d'un chunk : sa cellule, ET SON NIVEAU DE DETAIL.
 *
 * LES GRILLES DES NIVEAUX SONT EMBOITEES, et c'est ce qui rend les anneaux
 * possibles. Un chunk de niveau L fait `ChunkSideM * 2^L` de cote et porte
 * toujours le MEME nombre de cellules -- seule la taille du voxel double. Un
 * chunk de niveau L se decoupe donc exactement en huit chunks de niveau L-1,
 * tous alignes sur l'origine du monde.
 *
 * C'est cet emboitement qui garantit que la diffusion est une PARTITION : un
 * noeud est soit maille, soit remplace par ses huit enfants, jamais les deux.
 * Ni recouvrement -- donc pas de geometrie dessinee en double -- ni trou.
 *
 * ELLE VIT ICI, ET NON DANS L'ACTEUR, parce que c'est le vocabulaire de la
 * DIFFUSION : l'acteur en depend, l'inverse serait faux.
 */
struct FWorldseedChunkKey
{
	FIntVector C = FIntVector::ZeroValue;
	int32 Niveau = 0;

	bool operator==(const FWorldseedChunkKey& Autre) const
	{
		return C == Autre.C && Niveau == Autre.Niveau;
	}
};

FORCEINLINE uint32 GetTypeHash(const FWorldseedChunkKey& Cle)
{
	return HashCombine(GetTypeHash(Cle.C), ::GetTypeHash(Cle.Niveau));
}

/**
 * Ce que la diffusion a besoin de savoir du monde, et RIEN DE PLUS.
 *
 * Six nombres. C'est la mesure de ce que cette passe demandait reellement a
 * l'acteur de trois mille lignes dans lequel elle vivait : le reste -- les
 * composants, les travaux en vol, le pion, le materiau, le reseau de cavites --
 * ne la concerne pas.
 */
struct WORLDSEED_API FWorldseedDiffusionRegles
{
	/** Cote d'un chunk de niveau 0, en metres. */
	float ChunkSideM = 32.0f;

	/** Taille d'un voxel au niveau 0, en metres. */
	float VoxelSizeM = 1.0f;

	/** Rayon exterieur de l'anneau de niveau 0. Il double a chaque cran. */
	float RayonAnneau0M = 250.0f;

	/** Au-dela, on ne charge plus rien. */
	float LoadRadiusM = 600.0f;

	/** Epaisseur creusable sous la surface : sous elle, il n'y a rien a mailler. */
	float BandDepthM = 100.0f;

	/** Nombre d'anneaux au-dela du niveau 0. A zero, resolution uniforme. */
	int32 NiveauMax = 0;

	/**
	 * POIDS DE LA VERTICALE DANS LE CRITERE DE SUBDIVISION. A UN, rien ne change.
	 *
	 * LE CRITERE ACTUEL EST PLUS DEFENDABLE QU'IL N'EN A L'AIR, et il faut le
	 * dire avant de le toucher : « subdiviser tant que la distance est sous
	 * `R0 x 2^(L-1)` » avec un cote de chunk en `32 x 2^L` revient a
	 * `cote / distance > 64 / R0`, c'est-a-dire une TAILLE ANGULAIRE constante.
	 * C'est exactement le bon critere pour du rendu, et ce n'est pas un hasard.
	 *
	 * CE QUI GENE EN VOL N'EST DONC PAS LE CRITERE, C'EST LA GEOMETRIE. Les
	 * anneaux sont des spheres centrees sur le joueur : a neuf cents metres
	 * d'altitude, le sol a l'aplomb est a neuf cents metres, donc dans
	 * l'anneau 2, donc a quatre metres de voxel. Mesure : le niveau 0 tombe de
	 * 1277 chunks au repos a 667 en vol, pour le meme reglage. L'altitude
	 * CONSOMME le rayon, et aucun reglage de streaming n'y peut rien.
	 *
	 * Ce poids multiplie l'ecart vertical AVANT de mesurer la distance. A 0,5,
	 * neuf cents metres d'altitude comptent pour quatre cent cinquante ; a
	 * 0,25, pour deux cent vingt-cinq. A zero, seule la distance horizontale
	 * compte -- et le sol a l'aplomb est alors toujours au niveau le plus fin,
	 * ce qui est genereux et angulairement injustifie.
	 *
	 * IL NE PEUT PAS CASSER LA CONTRAINTE 2:1, et c'est ce qui le distingue du
	 * critere de rugosite retire le 23 septembre. Celui-la avait pour seuil une
	 * PENTE, donc divisee par deux a chaque cran, et aucune retouche locale ne
	 * pouvait le rendre monotone. Ici l'anisotropie est une transformation
	 * LINEAIRE de l'espace, appliquee a l'identique a tous les niveaux : le
	 * raisonnement « si A descend, ses voisins descendent » tient dans la
	 * metrique transformee comme dans l'autre. Le controle de la passe
	 * d'equilibrage reste la pour le dire si je me trompe.
	 */
	float PoidsZ = 1.0f;
};

/**
 * QUI EMET QUEL CHUNK, A QUEL NIVEAU, ET QUELLES FACES DE TRANSITION.
 *
 * C'EST UNE PARTITION, PAS UN CRITERE PAR POINT, et la distinction n'est pas
 * theorique. Le reflexe est de decider le niveau d'un chunk par sa distance a
 * l'origine ; c'est faux de deux facons, parce que deux chunks de niveaux
 * differents n'ont pas le meme centre. Un critere pose sur la seule distance
 * peut en emettre DEUX pour le meme volume -- geometrie dessinee en double --
 * ou AUCUN, c'est-a-dire un trou. La descente recursive de `Enumerer` n'a pas
 * ce defaut par construction : elle part du niveau le plus grossier et
 * remplace un noeud par ses huit enfants, ou le maille, jamais les deux.
 *
 * ELLE EST L'UNIQUE SOURCE DE VERITE DU NIVEAU. `MasqueDe` lit l'ensemble des
 * feuilles REELLEMENT emises, il ne rededuit rien : deux calculs qu'on espere
 * d'accord finiraient par armer une cellule de transition la ou il n'y a pas
 * de changement de resolution, et en oublier ailleurs -- c'est-a-dire une
 * fissure, qu'aucune mesure de couture ne saurait attribuer.
 *
 * ELLE NE CONNAIT NI ACTEUR NI MONDE : un champ de densite, six reglages, une
 * origine. C'est ce qui la rend eprouvable -- la propriete 2:1 n'avait pour
 * tout filet qu'un avertissement au journal.
 */
class WORLDSEED_API FWorldseedDiffusion
{
public:
	/** Les reglages et le champ. A rappeler quand l'un des deux change. */
	void Regler(const FWorldseedDiffusionRegles& Nouvelles,
		TSharedPtr<const FWorldseedDensity, ESPMode::ThreadSafe> Champ);

	const FWorldseedDiffusionRegles& Regles() const { return R; }

	// --- la geometrie des niveaux -------------------------------------------

	/** Cote d'un chunk de niveau L, en metres. Il double a chaque cran. */
	double CoteM(int32 Niveau) const
	{
		return static_cast<double>(R.ChunkSideM) * static_cast<double>(1 << Niveau);
	}

	/** Taille d'un voxel au niveau L. Elle double avec le cote. */
	float VoxelM(int32 Niveau) const
	{
		return R.VoxelSizeM * static_cast<float>(1 << Niveau);
	}

	/** Rayon exterieur de l'anneau de niveau L. */
	double RayonAnneauM(int32 Niveau) const
	{
		return static_cast<double>(R.RayonAnneau0M) * static_cast<double>(1 << Niveau);
	}

	/** Boite d'un chunk, en metres dans le repere du terrain. */
	FBox BoiteM(const FWorldseedChunkKey& Key) const;

	// --- la passe ------------------------------------------------------------

	/**
	 * Descend depuis un noeud et emet les feuilles, avec leur distance.
	 *
	 * `bEmissionForcee` saute les deux bornes de distance : il sert a
	 * l'equilibrage, qui doit pouvoir descendre un noeud meme si la distance
	 * l'aurait ecarte.
	 */
	void Enumerer(const FWorldseedChunkKey& Key, const FVector& OrigineM,
		TArray<TPair<FWorldseedChunkKey, double>>& Sortie,
		bool bEmissionForcee = false) const;

	/**
	 * Ramene l'ecart de niveau entre voisins a UN cran, et retient les
	 * feuilles obtenues.
	 *
	 * TRANSVOXEL NE SAIT COUDRE QU'UN NIVEAU D'ECART. Au-dela, la cellule de
	 * transition ne peut pas raccorder, et la fissure ne se signale pas : le
	 * masque s'arme quand meme, la geometrie reste combinatoirement close, et
	 * le trou ne se voit qu'a l'oeil sur une jointure precise.
	 */
	void Equilibrer(TArray<TPair<FWorldseedChunkKey, double>>& Feuilles,
		const FVector& OrigineM);

	// --- les lectures --------------------------------------------------------

	/**
	 * Le niveau REELLEMENT emis en un point, LU dans l'ensemble des feuilles.
	 * INDEX_NONE si aucune feuille ne couvre ce point.
	 */
	int32 NiveauEmis(const FVector& PointM) const;

	/** Le niveau emis, ou a defaut celui que les anneaux donneraient. */
	int32 NiveauEstime(const FVector& PointM, const FVector& OrigineM) const;

	/** Les six faces de ce chunk qui touchent un voisin PLUS FIN. */
	uint8 MasqueDe(const FWorldseedChunkKey& Key) const;

	/** Ce noeud doit-il etre remplace par ses huit enfants ? */
	bool DoitSubdiviser(const FWorldseedChunkKey& Key, const FVector& OrigineM) const;

	/**
	 * Plage d'altitude du relief sous une colonne de chunk, mise en cache.
	 *
	 * ELLE EST MISE EN CACHE PARCE QU'ELLE NE CHANGE PAS. Le relief 2D est une
	 * constante du monde ; la recalculer a chaque passe coutait 289 lectures
	 * par noeud de 256 m, et la passe de diffusion est montee a 13,65 ms a
	 * 2400 m de vue avant qu'on ne s'en apercoive. Apres cache : 1,83 ms.
	 */
	void PlageSurface(int32 CX, int32 CY, int32 Niveau,
		float& OutMinM, float& OutMaxM) const;

	/** L'ensemble des feuilles de la derniere passe. */
	const TSet<FWorldseedChunkKey>& Feuilles() const { return FeuillesCourantes; }

	/** Combien de feuilles le dernier equilibrage a ajoutees. */
	int32 AjoutsDeLEquilibrage() const { return EquilibrageAjouts; }

	/** Combien de noeuds restaient a plus d'un cran de leur voisin. */
	int32 EcartsNonResorbes() const { return Ecarts2a1; }

	/** A appeler quand le monde change : le relief mis en cache n'est plus le bon. */
	void Oublier();

private:
	FWorldseedDiffusionRegles R;
	TSharedPtr<const FWorldseedDensity, ESPMode::ThreadSafe> Champ;

	TSet<FWorldseedChunkKey> FeuillesCourantes;
	mutable TMap<FIntVector, FVector2D> CacheSurface;

	int32 EquilibrageAjouts = 0;
	int32 Ecarts2a1 = 0;
};
