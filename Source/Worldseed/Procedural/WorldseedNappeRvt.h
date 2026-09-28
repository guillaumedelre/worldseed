// Worldseed - la nappe qui REMPLIT la Runtime Virtual Texture.

#pragma once

#include "CoreMinimal.h"

struct FWorldseedBiomeMap;
struct FWorldseedGeometry;
class AActor;
class USceneComponent;
class UStaticMeshComponent;
class UTexture2D;
class URuntimeVirtualTexture;

/**
 * UN ECRIVAIN DEDIE, DE LA TAILLE DU MONDE, QUI NE SE DESSINE JAMAIS.
 *
 * LE PROBLEME QU'ELLE RESOUT. Le pack Orasot accorde ses maillages a son sol
 * par une RVT : le terrain y ECRIT sa couleur, le feuillage et le DESSUS DES
 * PANS DE FALAISE la RELISENT. Chez nous la RVT restait vide, et une RVT vide
 * rend du NOIR -- d'ou la masse noire signalee sur le dessus des rochers.
 *
 * ET LE TERRAIN NE PEUT PAS ETRE CET ECRIVAIN. Releve dans la source du
 * moteur : `FProceduralMeshSceneProxy` n'implemente pas `DrawStaticElements`
 * et ne connait pas le mot `RuntimeVirtualTexture`. Or la passe RVT se batit a
 * partir des lots STATIQUES. Poser `RuntimeVirtualTextures` sur un tel
 * composant compile, s'applique proprement, et PERSONNE ne le lit.
 *
 * TROIS VOIES ONT ETE PESEES, ET LA MESURE A TRANCHE :
 *
 *   1. garder la substitution `UseRVT = False` sur les pans. Cout nul, mais le
 *      dessus ne s'accorde jamais au biome ;
 *   2. porter la couleur du sol dans les donnees PAR INSTANCE de l'ISM des
 *      pans. Quasi gratuit, mais une seule teinte pour un pan de 77 m, une
 *      chirurgie de graphe sur le maitre du pack -- qui a deja fait tomber
 *      l'editeur deux fois -- et cela ne repare QUE les pans ;
 *   3. porter tout le terrain sur un composant a lots statiques. C'est la
 *      solution du pack, a pleine resolution, et elle a UN cout cache : le
 *      terrain STREAME, donc chaque chunk pose ou relache invaliderait des
 *      pages de RVT, qu'il faudrait redessiner en permanence.
 *
 * D'OU CELLE-CI. Un temoin magenta a d'abord PROUVE la premisse -- un plan
 * statique de 600 m a suffi a rendre le dessus des pans magenta, donc un
 * ecrivain a lots statiques remplit bien cette RVT. Mais il a prouve plus que
 * ca : l'ecrivain n'a pas besoin d'etre le terrain. Une nappe DEDIEE, posee
 * une fois et qui ne bouge JAMAIS, voit ses pages calculees une seule fois et
 * gardees. Elle est en `Never` -- jamais dessinee dans la passe principale --
 * donc elle ne coute rien a l'image. Et elle repare TOUS les lecteurs de la
 * RVT d'un coup, pas seulement le dessus des pans.
 *
 * ELLE NE RECOPIE AUCUNE FORMULE. Les poids des quatre matieres et la teinte
 * sortent de `WorldseedBiomes::SlotWeights` et `WorldseedApparence::
 * TeinteNormalisee`, exactement comme le peintre du terrain voxel ; le melange
 * des quatre textures reste celui de `M_WorldseedGround`, atteint par un
 * commutateur STATIQUE qui dit seulement d'ou viennent les poids -- du sommet
 * pour le sol, d'une texture pour la nappe. Deux melangeurs divergeraient, et
 * l'ecart se verrait exactement la ou le dessus d'un pan touche le sol.
 */
struct FWorldseedNappeRvtReleve
{
	/** Taille des deux textures cuites, en texels. */
	int32 Largeur = 0;
	int32 Hauteur = 0;

	/** Ce qu'elles pesent ensemble, en mega-octets. */
	double Mo = 0.0;

	/** Combien de texels decrivent de la terre, et combien de la mer. */
	int64 Terre = 0;
	int64 Mer = 0;

	/**
	 * Combien de texels marins ont ete repris a un voisin terrestre.
	 *
	 * ON COMPTE CE QU'ON DILATE. Un filtre bilineaire porte sur une cellule
	 * de chaque cote : sans cette dilatation, un pan pose sur le rivage
	 * verrait sa teinte tiree vers le bleu de l'ocean. C'est la meme parade
	 * que l'exclusion de l'ocean dans le melange du peintre voxel -- et un
	 * compte a zero dirait que la parade ne s'execute pas, ce qu'aucune image
	 * ne montrerait.
	 */
	int64 Dilates = 0;

	/** Les bornes de ce qu'on ECRIT, avant meme de regarder l'image. */
	float PoidsMax = 0.0f;
	float TeinteMax = 0.0f;

	/** Temps de cuisson, en millisecondes. */
	double Ms = 0.0;
};

namespace WorldseedNappeRvt
{
	/** Ce que la teinte normalisee peut atteindre, donc le facteur d'encodage. */
	inline constexpr float TeinteMax = 2.5f;

	/**
	 * Cuit les deux textures de la nappe depuis la carte des biomes.
	 *
	 * Un texel par cellule de simulation, dans la convention du SOL
	 * (`FWorldseedGeometry::UVDepuisMetres`) : la ligne 0 est au SUD et la
	 * colonne 0 au meridien de bordure, donc la copie est directe.
	 */
	WORLDSEED_API bool Cuire(const FWorldseedBiomeMap& Biomes,
		const FWorldseedGeometry& Geo,
		UTexture2D*& OutPoids, UTexture2D*& OutTeinte,
		FWorldseedNappeRvtReleve& Releve);

	/**
	 * Pose la nappe sur l'acteur. Rend le composant, ou nullptr.
	 *
	 * `Textures` sont les RVT posees sur le monde : la nappe n'ecrit QUE dans
	 * celle qui porte la couleur. Elle laisse la RVT de HAUTEUR intacte --
	 * elle n'en connait pas le contenu, et y ecrire une altitude plate
	 * changerait l'ancrage du feuillage sans qu'on l'ait mesure.
	 */
	WORLDSEED_API UStaticMeshComponent* Poser(AActor* Proprietaire,
		USceneComponent* Racine,
		TArrayView<URuntimeVirtualTexture* const> Textures,
		double LargeurM, double HauteurM,
		UTexture2D* Poids, UTexture2D* Teinte);
}
