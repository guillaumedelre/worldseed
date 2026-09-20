// Worldseed - la tournee photo : voir le monde sans outillage externe.

#pragma once

#include "CoreMinimal.h"

#include "Procedural/WorldseedUdsBridge.h"
#include "Subsystems/WorldSubsystem.h"

#include "WorldseedPhotographe.generated.h"

class AWorldseedVoxelTerrain;

/**
 * Une etape de tournee : ou se placer, et quoi regarder.
 *
 * SE PLACER ET VISER SONT DEUX CHOSES. Une arche ne se juge pas en se tenant
 * dedans : il faut reculer et regarder DANS L'AXE du percement, sinon on
 * photographie une paroi. Le projet a deja paye l'equivalent avec les
 * silhouettes de vegetation jugees a la verticale -- une bache posee au sol y
 * ressemblait a une teinte parfaite.
 */
struct FWorldseedPhotoStop
{
	FString Nom;
	FVector CibleM = FVector::ZeroVector;
	FVector2D DepuisM = FVector2D(1.0, 0.0);
	float DistanceM = 110.0f;
	float HauteurM = 2.0f;
};

/**
 * Le photographe : il conduit une tournee et declenche lui-meme.
 *
 * POURQUOI IL EXISTE. Le lien d'outillage externe tombe des que l'editeur est
 * tue et relance plusieurs fois -- donc a chaque compilation -- et l'on
 * redevient alors aveugle. Or ce projet a appris a ses depens que certains
 * defauts ne se voient QUE par l'image : un monde en grands polygones, une
 * arche parfaitement traversante mais enterree sous trente metres de roche,
 * une plage qui n'existe plus a l'ecran. Aucune mesure chiffree ne les avait
 * vus.
 *
 * POURQUOI UN SOUS-SYSTEME ET NON L'ACTEUR TERRAIN. La tournee a d'abord ete
 * ecrite DANS AWorldseedVoxelTerrain, parce que son minuteur offrait le fil du
 * temps dont elle avait besoin. C'etait une commodite, pas une decision : un
 * acteur qui diffuse des chunks n'a pas a savoir cadrer une photo. Un
 * sous-systeme de monde a sa propre horloge, se cree tout seul, et le terrain
 * ignore jusqu'a son existence.
 */
UCLASS()
class WORLDSEED_API UWorldseedPhotographe : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** Ajoute une vue et demarre la tournee si elle dort. */
	void Photographier(double XMetres, double YMetres, const FString& Nom);

	/** Les arches du monde, vues DANS L'AXE du percement. */
	int32 AjouterLesArches(int32 Combien);

	/** Les plus hautes falaises littorales, vues depuis la mer. */
	int32 AjouterLesFalaises(int32 Combien);

	/**
	 * Les tables : on les cadre DE COTE ET DE LOIN.
	 *
	 * UNE MESA NE SE PHOTOGRAPHIE PAS D EN HAUT NI DE PRES. De pres on ne
	 * voit qu une paroi, et depuis le sommet on ne voit qu une plaine -- dans
	 * les deux cas la forme disparait. Ce qui la fait lire, c est la
	 * SILHOUETTE : un trait horizontal pose sur un socle, et le sommet voisin
	 * a la meme hauteur. Le depot a deja paye la lecon sur la teinte de
	 * l herbe, jugee a la verticale ou un quad opaque est indiscernable du
	 * sol : juger une silhouette CONTRE LE CIEL, de cote, jamais a la
	 * verticale.
	 */
	int32 AjouterLesTables(int32 Combien);

	/**
	 * Les canyons : on descend DEDANS, on ne les survole pas.
	 *
	 * UN CANYON VU D EN HAUT EST UNE RAYURE, et c est tout ce qu on en voit.
	 * Ce qui le fait lire, c est d etre au FOND, entre deux parois qui
	 * montent hors du cadre. La camera se pose donc sur le plancher et
	 * regarde le long de la gorge.
	 */
	int32 AjouterLesCanyons(int32 Combien);

	/** Reecrit l heure a midi avant chaque prise. Sans effet sans UDS. */
	void MidiFige();

	bool EnCours() const { return Tournee.IsValidIndex(Etape); }

private:
	FWorldseedUdsBridge Uds;
	bool bUdsResolu = false;

	/** Le terrain du monde, ou nul s'il n'y en a pas. */
	AWorldseedVoxelTerrain* Terrain() const;

	void Demarrer();
	void Avancer(float DeltaTime);

	TArray<FWorldseedPhotoStop> Tournee;
	int32 Etape = INDEX_NONE;
	int32 Attente = 0;

	/** Memoire du test de stabilisation : voir AWorldseedVoxelTerrain. */
	int32 DernierCompte = -1;
	float StableS = 0.0f;
	bool bQuitterEnsuite = false;

	/**
	 * Arme, mais pas encore construite.
	 *
	 * Le sous-systeme recoit son OnWorldBeginPlay AVANT les acteurs : le
	 * terrain n'existe pas encore, et son monde met une minute a se generer.
	 */
	bool bArme = false;

	/**
	 * Le tick d'un sous-systeme suit la trame, qui varie.
	 *
	 * La tournee, elle, raisonne en DUREES : laisser le monde se batir demande
	 * des secondes, pas des images. On accumule donc le temps plutot que de
	 * compter les tics -- sans quoi la meme tournee tirerait trop tot sur une
	 * machine rapide.
	 */
	float Horloge = 0.0f;
};
