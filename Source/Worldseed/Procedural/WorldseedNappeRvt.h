// Worldseed - la nappe qui REMPLIT la Runtime Virtual Texture.

#pragma once

#include "CoreMinimal.h"

struct FWorldseedBiomeMap;
struct FWorldseedGeometry;
struct FWorldseedRvtRegles;
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

	/**
	 * Les altitudes rencontrees, et combien de texels sortent du volume.
	 *
	 * L'ENCODAGE DE LA HAUTEUR EST UN INTERVALLE FIGE, et c'est le genre de
	 * reglage qui se perime en silence : le jour ou le relief depassera
	 * `HauteurBasM`/`HauteurHautM`, la hauteur cuite SATURERA -- le feuillage
	 * s'ancrera a une altitude fausse sur tout un massif, sans une erreur.
	 * Un compte a zero dit que les bornes tiennent encore ; un compte non nul
	 * dit exactement de combien il faut les elargir.
	 */
	float AltitudeMin = 0.0f;
	float AltitudeMax = 0.0f;
	int64 HorsBornes = 0;

	/**
	 * La pente la plus raide de la normale cuite, en degres.
	 *
	 * ELLE DIT SI LA NORMALE DECRIT UN RELIEF OU UN PLAN. Une normale cuite a
	 * partir d'un gradient nul -- relief non transmis, pas de grille mal lue --
	 * rendrait (0, 0, 1) partout, c'est-a-dire EXACTEMENT ce que le moteur
	 * ecrit quand l'entree n'est pas branchee : le defaut qu'on vient de
	 * corriger serait de retour, a l'identique, et aucune image ne saurait les
	 * distinguer. Une pente maximale a zero est donc une ERREUR, pas un monde
	 * plat.
	 */
	float PenteMaxDeg = 0.0f;

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
	/**
	 * LES BORNES DE L'ENCODAGE SONT CELLES DU VOLUME, ET ON LES PREND CHEZ LUI.
	 *
	 * Elles ont d'abord ete recopiees ici en deux constantes, avec un
	 * commentaire qui demandait de les tenir egales a `FWorldseedRvtRegles`.
	 * C'est exactement la forme de defaut que ce depot paye le plus souvent :
	 * deux echelles qui divergent un jour donneraient un ancrage faux PARTOUT,
	 * sans une erreur, et le commentaire ne peut rien y faire. On prend donc
	 * les regles du volume en parametre.
	 *
	 * `ExagerationZ` va avec, et l'oublier serait la meme faute d'un cran plus
	 * bas : `ElevationM` est en metres de SIMULATION, le volume est en metres
	 * du MONDE, et le terrain applique ce facteur entre les deux.
	 */
	WORLDSEED_API bool Cuire(const FWorldseedBiomeMap& Biomes,
		const FWorldseedGeometry& Geo,
		const TArray<float>& ElevationM,
		float ExagerationZ, const FWorldseedRvtRegles& RvtRegles,
		UTexture2D*& OutPoids, UTexture2D*& OutTeinte,
		UTexture2D*& OutHauteur, UTexture2D*& OutNormale,
		FWorldseedNappeRvtReleve& Releve);

	/**
	 * Pose la nappe sur l'acteur. Rend le composant, ou nullptr.
	 *
	 * ELLE ECRIT DANS LES DEUX RVT, ET LA SECONDE EST LA CONDITION DE LA
	 * PREMIERE. La note qui tenait ici disait qu'on laissait la RVT de HAUTEUR
	 * intacte « a dessein », parce qu'une nappe plate y poserait une altitude
	 * constante. Le constat etait juste -- le plan est a Z = 0, donc
	 * `WorldPosition.b` y vaut zero partout -- mais la conclusion etait fausse :
	 * il ne fallait pas renoncer a la hauteur, il fallait CESSER de la prendre
	 * sur la geometrie et la cuire comme le reste.
	 *
	 * CE QUE CETTE ABSENCE COUTAIT, mesure le 28 septembre 2026 :
	 * `M_Master_Cliff_Mat` melange la roche du pan et la couleur du sol par
	 * `MF_HeightLerp_MaterialAttribute`, dont l'entree B vient de `MF_RVT` --
	 * et `MF_RVT` echantillonne DEUX runtime virtual textures,
	 * `RVT_Landscape_Material` ET `RVT_Landscape_Height` (type WORLD_HEIGHT).
	 * Son masque ancre l'objet dans le sol en comparant l'altitude du monde a
	 * celle lue dans la RVT. Sans hauteur ecrite, ce masque ne mord jamais et
	 * le dessus des pans garde sa roche -- quels que soient les switchs, la
	 * nappe de couleur ou le materiau.
	 *
	 * LE SECOND ECHANTILLONNAGE EST CACHE DANS UNE FONCTION, et c'est ce qui a
	 * fait ecarter cette piste deux fois : le maitre n'annonce qu'UNE
	 * expression de RVT, la seconde vivant dans `MF_RVT`. Le depot a la meme
	 * note depuis septembre -- « un echantillonnage de RVT peut etre cache dans
	 * une fonction de materiau » -- et il faut descendre dans les
	 * `MaterialFunctionCall` pour le voir.
	 */
	WORLDSEED_API UStaticMeshComponent* Poser(AActor* Proprietaire,
		USceneComponent* Racine,
		TArrayView<URuntimeVirtualTexture* const> Textures,
		double LargeurM, double HauteurM,
		UTexture2D* Poids, UTexture2D* Teinte, UTexture2D* Hauteur,
		UTexture2D* Normale, const FWorldseedRvtRegles& RvtRegles);
}
