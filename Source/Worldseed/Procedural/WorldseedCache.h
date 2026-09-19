// Worldseed - cache disque des mondes deja calcules.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedWorldData.h"

/**
 * VERSION DE LA CHAINE DE GENERATION.
 *
 * A INCREMENTER DES QU'UN CHANGEMENT DE CODE MODIFIE LE RELIEF PRODUIT :
 * nouvelle etape, formule corrigee, mise a l'echelle differente. Les mondes en
 * cache portent la version qui les a produits ; ceux d'une autre version sont
 * refuses au chargement et signales comme perimes.
 *
 * L'empreinte de world_rules.json couvre les REGLAGES, ce compteur couvre le
 * CODE. Sans lui, corriger une formule laisserait recharger les anciens mondes
 * en croyant tester la correction — et rien ne le signalerait.
 *
 * Historique :
 *   1 - tectonique + climat + erosion, carte spherique
 *   2 - mise a l'echelle verticale des regles metriques sous la taille de reference
 *   3 - flou par boites iterees, erosion thermique en collecte, quantile par selection
 *   4 - amplitude saisonniere transportee (le relief lui-meme est inchange)
 *   5 - continentalite transportee, pour l'ecart jour/nuit du ciel
 *   6 - hydrologie retiree : plus de creusement de lit apres chargement
 *   7 - la lithologie est calculee et TRANSPORTEE, donc son code entre dans ce
 *       compteur : une correction de l'attribution des roches doit desormais
 *       invalider les mondes en cache, sinon ils rendent l'ancienne carte des
 *       roches sans le signaler. Paye comptant le jour de son ajout.
 *   8 a 12 - crans dont la raison est dans leur commit ; cet historique avait
 *       cesse d'etre tenu, ce qui est un defaut : le lire ne dit plus ce que
 *       chaque cran a change.
 *  13 - le plateau disseque (WorldseedPlateau) : une passe de plus abaisse le
 *       relief entre l'erosion et le second climat, donc les mondes en cache
 *       n'ont ni mesas ni canyons et doivent etre refaits.
 */
#define WORLDSEED_PIPELINE_VERSION 13

/** Ce qu'on sait d'un monde en cache sans le decompresser. */
struct WORLDSEED_API FWorldseedCacheEntry
{
	FString FileName;
	int32 Seed = 0;
	int32 NX = 0;
	int32 NY = 0;
	float HeightM = 0.0f;
	int32 PipelineVersion = 0;
	FString RulesHash;
	int64 SizeBytes = 0;

	/** Faux si la version de chaine ou les regles ont change depuis. */
	bool bCompatible = false;
};

/**
 * Cache de mondes.
 *
 * METTRE EN CACHE N'EST PAS CUIRE. Cuire deplacerait la generation hors-ligne
 * et le runtime ne saurait plus produire un monde. Ici le runtime sait toujours
 * tout generer : il saute simplement le calcul quand il a deja la reponse. La
 * generation par graine reste la source de verite, le fichier n'est qu'un
 * raccourci.
 *
 * Les metadonnees sont ecrites EN CLAIR en tete de fichier, avant la partie
 * compressee : lister le cache ne demande donc pas de tout decompresser.
 */
namespace WorldseedCache
{
	constexpr uint32 Magic = 0x57534557;   // "WSEW"
	/**
	 * 5 : la lithologie voyage avec le monde.
	 *
	 * Elle depend de grandeurs que seule la tectonique connait, et le cache ne
	 * les portait pas. Le passage a 5 REFUSE tous les mondes deja en cache : ils
	 * se regenerent une fois, en une trentaine de secondes chacun.
	 */
	constexpr uint32 FormatVersion = 5;

	/** Cle unique pour un jeu de parametres et une version de chaine. */
	WORLDSEED_API FString MakeKey(int32 Seed, float HeightMeters, int32 ResolutionY,
		const FString& RulesHash);

	WORLDSEED_API FString PathForKey(const FString& Key);

	/** Charge un monde. Faux si absent, corrompu, perime ou d'un autre format. */
	WORLDSEED_API bool Load(const FString& Key, FWorldseedWorldData& Out);

	WORLDSEED_API bool Save(const FString& Key, const FString& RulesHash,
		const FWorldseedWorldData& World);

	/** Inventaire du cache, avec le verdict de compatibilite de chaque entree. */
	WORLDSEED_API TArray<FWorldseedCacheEntry> ListEntries(const FString& CurrentRulesHash);

	/** Supprime SEULEMENT les mondes devenus incompatibles. */
	WORLDSEED_API int32 ClearObsolete(const FString& CurrentRulesHash);

	/** Supprime tout. */
	WORLDSEED_API int32 ClearAll();

	WORLDSEED_API int64 TotalSize();
}
