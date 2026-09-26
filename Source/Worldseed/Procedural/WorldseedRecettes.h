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

	/** Diametre des taches, en cm. A zero, la couche couvre uniformement. */
	float TacheTailleCm = 0.0f;

	/** Part retenue dans les taches, dans [0..1]. */
	float TacheSeuil = 0.0f;

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

	/** Nombre total de couches, pour le releve. */
	int32 NbCouches = 0;

	/**
	 * Materiau d'origine -> instance SANS Runtime Virtual Texture.
	 *
	 * POURQUOI CETTE TABLE EXISTE. Dix-neuf des cent cinquante-quatre materiaux
	 * cites par les recettes ont un switch STATIQUE de RVT arme -- `UseRVT`,
	 * `UseTopLayerRvt`, `UseTopVertexRVTMask`. Ils echantillonnent une Runtime
	 * Virtual Texture que ce niveau n'a pas, depuis que le Landscape a disparu,
	 * et rendent alors un BLEU ELECTRIQUE sur la face concernee : signale en
	 * jeu, la face SUPERIEURE des rochers de desert etait bleue quand leurs
	 * flancs restaient corrects.
	 *
	 * ET CELA NE SE CORRIGE PAS A L'EXECUTION. Un switch statique compile deux
	 * shaders differents ; une `UMaterialInstanceDynamic` ne peut pas le
	 * changer, seule une instance CONSTANTE le peut. Les dix-neuf instances
	 * corrigees sont donc des assets, crees en editeur, et cette table dit
	 * lequel remplace lequel.
	 *
	 * LA CLE EST LE CHEMIN COMPLET, PAS LE NOM : trois packs differents ont un
	 * `MI_Rock_1`, et les confondre poserait le mauvais materiau sur deux
	 * rochers sur trois.
	 */
	TMap<FString, FString> SansRVT;

	/**
	 * Charge la table des instances sans RVT. Sans effet si le fichier manque
	 * -- on garde alors les materiaux du pack, bleu compris, plutot que de
	 * refuser de semer.
	 */
	void ChargerSansRVT();

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
