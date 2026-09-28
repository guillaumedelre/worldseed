// Worldseed - les recettes de vegetation : quel biome porte quoi, et a quel pas.

#pragma once

#include "CoreMinimal.h"

/**
 * Une espece posee par une couche, avec son poids relatif.
 *
 * LE CHEMIN EST RESOLU A LA LECTURE. Le fichier de recettes ecrit des
 * references courtes -- « SFP:SM_grass_01 » -- ou le prefixe designe une racine
 * declaree dans la section `roots`. C'est ce qui rend le fichier lisible et
 * retouchable a la main, ce pour quoi il a ete fait.
 */
struct FWorldseedEspece
{
	/** Chemin complet de l'asset, deja resolu depuis la racine. */
	FString Chemin;

	/** Poids relatif dans sa couche. */
	float Poids = 1.0f;

	/**
	 * Index dans `FWorldseedRecettes::Catalogue`, fige a la lecture.
	 *
	 * PRECALCULE ET NON CHERCHE : le semis pose des centaines de plantes par
	 * chunk, et retrouver l.index par son chemin a chaque plante serait une
	 * recherche lineaire sur deux cent trente et une entrees, dans la boucle
	 * la plus chaude du module.
	 */
	int32 IndexCatalogue = INDEX_NONE;
};

/**
 * Une couche de semis : un pas de grille, des especes, des bornes.
 *
 * UNE COUCHE EST L'UNITE DE PLACEMENT, et le registre l'a paye pour le savoir :
 * tant que la mousse et les fleurs partageaient la couche du TAPIS, elles
 * etaient semees a son pas de 150 cm, donc partout, une touffe tous les metres
 * et demi -- la mousse pesait a elle seule 13,6 % de tout le semis. On ne peut
 * pas donner sa regle propre a une plante tant qu'elle partage sa couche.
 */
struct FWorldseedCoucheRecette
{
	FString Nom;

	/** Pas de grille en CENTIMETRES, deja resolu depuis son nom. */
	float PasCm = 400.0f;

	/** Bornes de l'echelle, tirees uniformement sur les trois axes. */
	float EchelleMin = 1.0f;
	float EchelleMax = 1.0f;

	/** Pente autorisee, en degres. */
	float PenteMinDeg = 0.0f;
	float PenteMaxDeg = 90.0f;

	/** Distances de disparition de l'instance, en centimetres. */
	float CullDebutCm = 0.0f;
	float CullFinCm = 0.0f;

	/**
	 * La couche pose de la ROCHE : un solide, pas du feuillage.
	 *
	 * DEUX CONSEQUENCES, ET C'EST POURQUOI UN SEUL DRAPEAU LES PORTE. Une
	 * roche ARRETE le joueur, et elle OCCUPE LE SOL -- rien d'autre ne pousse
	 * dessous. Les deux disent la meme chose : il y a de la matiere la.
	 *
	 * IL EST SUR LA COUCHE ET NON SUR L'ESPECE, parce que c'est la couche qui
	 * porte le sens -- « eboulis », « rochers » -- quand un maillage n'est
	 * qu'une forme. Mais l'EMPRISE, elle, se mesure sur l'instance : le meme
	 * `SM_Env_scatter_rock_12` sert de BLOC a l'echelle 1,6 et de GALET a 0,4,
	 * et aucun nom de couche ne peut dire lequel des deux barre le passage.
	 *
	 * Une espece citee par au moins une couche obstacle est donc de la roche
	 * PARTOUT -- voir `EspeceObstacle` -- et c'est sa TAILLE qui decide de ce
	 * qu'elle bloque reellement.
	 */
	bool bObstacle = false;

	/** Diametre des taches, en cm. A zero, la couche couvre uniformement. */
	float TacheTailleCm = 0.0f;

	/** Part retenue dans les taches, dans [0..1]. */
	float TacheSeuil = 0.0f;

	/**
	 * Nombre d'octaves du bruit de taches. UNE = le comportement d'avant.
	 *
	 * UNE OCTAVE N'A QU'UNE TAILLE, et c'est ce qui se lit comme regulier de
	 * loin : les taches font toutes le meme diametre et se repartissent avec
	 * la meme periode. Les octaves suivantes ajoutent du detail a la moitie
	 * de la taille et au quart de l'amplitude -- le contour se decoupe, des
	 * ilots se detachent, des trouees s'ouvrent dans les pleins.
	 *
	 * ⚠ MONTER LES OCTAVES DEPLACE LA COUVERTURE A SEUIL EGAL, ET PAS DANS UN
	 * SEUL SENS. Une somme fractale se masse autour de sa MEDIANE : elle
	 * retient donc PLUS sous un demi et MOINS au-dessus. Mesure, taille
	 * 2600 cm :
	 *
	 *     seuil   1 octave   3 octaves
	 *     0,35      82,1 %     92,7 %
	 *     0,45      60,9 %     68,0 %
	 *     0,55      38,8 %     33,0 %
	 *     0,65      18,7 %      7,8 %
	 *
	 * J'AVAIS ECRIT « le meme seuil retient donc moins », ET C'ETAIT FAUX A
	 * MOITIE : vrai au-dessus de la mediane, faux en dessous. C'est la table
	 * qui l'a dit, pas le raisonnement. Ce depot a deja paye ce genre de
	 * demi-verite sous le nom « un seuil n'est pas une part » -- un seuil
	 * cense garder seize pour cent n'en gardait que 1,59.
	 *
	 * `Worldseed.Vegetation.LesTachesRetiennent` imprime cette table : on y
	 * lit le seuil qui rend la couverture voulue, au lieu de la deviner.
	 */
	int32 TacheOctaves = 1;

	/**
	 * Largeur de la transition autour du seuil, en unites de bruit [0..1].
	 * A ZERO, le seuil est franc -- le comportement d'avant, a l'identique.
	 *
	 * C'EST CE QUI REMPLACE UN OUI/NON PAR UNE DENSITE. Un seuil franc donne
	 * des taches a BORD NET : dedans tout pousse, dehors rien, et la frontiere
	 * se voit comme un decoupage. Avec une douceur, la probabilite de garder
	 * un point monte progressivement -- dense au coeur de la tache, clairseme
	 * sur ses marges, ce qui est la facon dont une clairiere se termine
	 * vraiment.
	 *
	 * ELLE NE DEPLACE PAS LA COUVERTURE MOYENNE, ou tres peu : la transition
	 * est centree sur le seuil, donc ce qu'elle retire d'un cote elle le rend
	 * de l'autre. C'est le CONTRASTE qu'elle change, pas la quantite.
	 */
	float TacheDouceur = 0.0f;

	TArray<FWorldseedEspece> Especes;

	/** Somme des poids, precalculee pour le tirage. */
	float PoidsTotal = 0.0f;
};

/** Ce qu'un biome porte. */
struct FWorldseedBiomeRecette
{
	TArray<FWorldseedCoucheRecette> Couches;
};

/**
 * Le catalogue complet, lu une fois au demarrage.
 *
 * IL VIT DANS UN JSON ET NON DANS LE CODE, et c'est le fichier lui-meme qui le
 * dit : « Ce fichier est fait pour etre RETOUCHE A LA MAIN : c'est lui qui
 * decide de l'aspect du monde, pas le code. » Le porter en dur dans le C++
 * reviendrait a demander une compilation pour changer une densite d'herbe.
 *
 * ET IL N'ENTRE PAS DANS L'EMPREINTE DU CACHE. `world_rules.json` est hache
 * pour invalider les mondes ; celui-ci ne decrit que du DECOR, il ne change ni
 * le relief, ni le climat, ni les biomes. Le mettre dans la meme empreinte
 * imposerait deux cents secondes de regeneration pour un changement d'herbe.
 */
struct WORLDSEED_API FWorldseedRecettes
{
	/** Indexe par identifiant de biome. Une entree vide = biome sans vegetation. */
	TMap<int32, FWorldseedBiomeRecette> ParBiome;

	/**
	 * Ce que porte l'ESTRAN, et il remplace la recette du biome au lieu de s'y
	 * ajouter.
	 *
	 * LA PLAGE N'EST PAS UN BIOME, C'EST UN SUBSTRAT -- `EWorldseedCover::Beach`,
	 * « une forme, pas un climat ». Une plage de desert et une plage de foret
	 * tropicale sont toutes deux du sable nu ; leur climat decide de ce qui
	 * pousse DERRIERE, pas sur l'estran lui-meme.
	 *
	 * C'EST POURQUOI ELLE EST UNE RECETTE A PART ET NON UNE COUCHE DE PLUS.
	 * Tant que le semis ne lisait que le biome, une plage recevait le tapis
	 * d'herbe de sa foret voisine -- de l'herbe sur le sable, signale en jeu.
	 * Et l'ajouter comme couche a chacun des dix-sept biomes aurait recopie la
	 * meme liste dix-sept fois, avec la garantie qu'elles divergent.
	 *
	 * VIDE, L'ESTRAN EST NU : c'est un etat valide, pas un defaut, et c'est
	 * deja plus proche du reel que de l'herbe.
	 */
	FWorldseedBiomeRecette Estran;

	/** Tous les chemins d'assets cites, dedoublonnes : l'ordre fait l'index. */
	TArray<FString> Catalogue;

	/**
	 * Parallele a `Catalogue` : vrai si l'espece est de la ROCHE.
	 *
	 * CALCULE A LA LECTURE, par union sur les couches : une espece citee par
	 * au moins une couche `obstacle` l'est partout. Ce n'est pas un compromis,
	 * c'est le seul choix coherent -- la collision se pose sur le COMPOSANT,
	 * donc sur l'espece, et il n'y en a qu'un par maillage. Le meme rocher ne
	 * peut pas etre solide en eboulis et traversable en galets.
	 *
	 * Et cela ne coute rien la ou l'on pourrait le craindre : un galet de
	 * quarante centimetres est sous la hauteur de marche du personnage, donc
	 * il se franchit sans qu'on le sente, collision ou non.
	 */
	TArray<bool> EspeceObstacle;

	/**
	 * Parallele a `Catalogue` : demi-largeur du maillage en XY, en cm, a
	 * l'echelle 1. Zero tant que personne ne l'a mesuree.
	 *
	 * ELLE NE VIENT PAS DU JSON, ET ELLE NE LE PEUT PAS : c'est une propriete
	 * de l'ASSET, que seul celui qui le charge connait. Le semeur, lui, ne
	 * manipule que des index de catalogue -- il n'a ni le maillage ni le droit
	 * de le charger, puisqu'il tourne sur un fil de travail.
	 *
	 * VIDE, L'EMPRISE NE JOUE PAS, et c'est un etat valide : un clone frais
	 * sans `Content/` seme comme avant au lieu de refuser de semer.
	 */
	TArray<float> RayonEspeceCm;

	/** Nombre total de couches, pour le releve. */
	int32 NbCouches = 0;

	/**
	 * IL Y AVAIT ICI UNE TABLE `SansRVT` -- materiau du pack -> instance
	 * corrigee -- ET ELLE EST PARTIE LE 28 SEPTEMBRE 2026, AVEC SES DIX-NEUF
	 * ASSETS. Ce qui suit est la mesure qui l'a condamnee ; ne pas la refaire.
	 *
	 * ELLE EXISTAIT parce que certains materiaux du pack echantillonnent une
	 * Runtime Virtual Texture, et que ce niveau n'en avait aucune depuis la
	 * disparition du Landscape : ils rendaient un BLEU ELECTRIQUE sur la face
	 * du dessus. Un switch STATIQUE ne se change pas a l'execution -- une
	 * `UMaterialInstanceDynamic` ne peut pas le faire, seule une instance
	 * CONSTANTE le peut -- d'ou dix-neuf assets et une table.
	 *
	 * `WorldseedNappeRvt` REMPLIT la RVT de couleur depuis le 27 septembre : la
	 * premisse est tombee. Et le releve, fait avant de supprimer quoi que ce
	 * soit, a montre que la table faisait bien moins que ce qu'elle annoncait.
	 * Les dix-neuf remplacants heritaient TOUS de l'instance d'origine -- donc
	 * textures et scalaires identiques, verifie au millieme -- et ne changeaient
	 * que des switchs :
	 *
	 *   DOUZE ne changeaient RIEN du tout, pas un seul switch. Copies conformes.
	 *     Dont les quatre `MI_Rock_*` de Biom_Dark, qui LISENT pourtant la RVT :
	 *     la table n'a donc jamais corrige leur bleu.
	 *   TROIS coupaient `UseTopVertexRVTMask` (les `MI_Cliff_*` de Biom_Green).
	 *     Seul vrai correctif de RVT, et il est desormais nuisible : le masque
	 *     doit REPRENDRE maintenant que la RVT est remplie.
	 *   QUATRE coupaient `Use Roughness Map` / `Use Specular Map` (`MI_Bamboo`,
	 *     `MI_Gray_Rock_1`, `MI_Palm_Bark`, `MI_Tree_Bark`). Cela n'avait AUCUN
	 *     rapport avec la RVT -- c'etaient les « textures parasites » du
	 *     commentaire du fichier. DECISION DU PROPRIETAIRE, 28 septembre 2026 :
	 *     le pack a raison, on lui rend sa rugosite et son speculaire.
	 *
	 * ET LE CHARGEUR AVAIT UN DEFAUT LATENT : il lisait un `.json` sous
	 * `Content/`, qui n'est pas un `.uasset` et n'entre donc pas tout seul dans
	 * un build cuit -- il aurait fallu `DirectoriesToAlwaysStageAsUFS`, absent.
	 * La table etait donc vide en build final, et personne ne l'a jamais su.
	 */

	bool EstVide() const { return ParBiome.Num() == 0; }

	/**
	 * Charge le fichier de recettes. Rend faux et remplit `OutErreur` si le
	 * fichier manque ou ne se lit pas -- ce qui n'est PAS fatal : un monde sans
	 * vegetation reste jouable, et `Content/` est exclu du depot, donc un clone
	 * frais peut tres bien n'avoir aucun des maillages cites.
	 */
	bool Charger(FString& OutErreur);

	/** Les chemins candidats, dans l'ordre d'essai. */
	static TArray<FString> CheminsCandidats();
};
