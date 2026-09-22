// Worldseed - le banc : mesurer le cout du terrain sans outillage externe.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "WorldseedBanc.generated.h"

class AWorldseedVoxelTerrain;

/**
 * Un banc de mesure qui tourne DANS le jeu.
 *
 * POURQUOI IL EXISTE. Le lien d'outillage externe tombe des que l'editeur est
 * tue et relance plusieurs fois -- donc a chaque compilation -- et l'on redevient
 * alors aveugle sur la performance. Le depot a deja une note « MESURER SANS
 * MCP » pour les sondes de generation ; il manquait l'equivalent pour le COUT
 * D'AFFICHAGE, qui ne se lit pas depuis un commandlet puisqu'il n'y a pas de
 * rendu.
 *
 * ET IL EVITE LE PIEGE LE PLUS COUTEUX DU PROJET. L'editeur non focalise bride
 * son rendu, et l'outillage externe rapporte alors un temps de trame plafonne
 * par le bridage en l'attribuant au GPU -- « une journee entiere de conclusions
 * de performance a ete batie dessus ». Ici la mesure est prise DANS le jeu, par
 * le jeu, sur des DeltaTime reels : il n'y a pas d'editeur a brider.
 *
 * Usage :
 *
 *     UnrealEditor.exe Worldseed.uproject /Game/Worldseed/Maps/L_Worldseed_Proc
 *       -game -WorldseedBanc -WorldseedRayon=500 -WorldseedQuitter
 *       -windowed -resx=1600 -resy=900
 */
UCLASS()
class WORLDSEED_API UWorldseedBanc : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(UWorldseedBanc, STATGROUP_Tickables);
	}
	virtual bool IsTickable() const override { return bArme || bEnCours; }

private:
	AWorldseedVoxelTerrain* Terrain() const;
	void Conclure();

	/** Arme par la ligne de commande, construit au premier tick utile. */
	bool bArme = false;
	bool bEnCours = false;
	bool bFini = false;
	bool bQuitterEnsuite = false;

	/**
	 * ON LAISSE LE MONDE SE POSER AVANT DE MESURER.
	 *
	 * Le depot a deja paye cette lecon : une lecture prise juste apres avoir
	 * deplace le pion a donne 127 images par seconde la ou la meme scene
	 * stabilisee en donnait 21 a 33 -- les chunks etaient en cours de
	 * relachement et la scene presque vide. On attend donc que le premier
	 * remplissage soit fini, PUIS on laisse passer le temps de chauffe.
	 */
	/** Duree pendant laquelle le streaming doit rester FIGE avant de mesurer. */
	float ChauffeS = 4.0f;

	/** Au-dela, on mesure quand meme et l'on DIT que c'est un transitoire. */
	float PlafondAttenteS = 180.0f;

	int32 DernierCompte = -1;
	float StableS = 0.0f;
	float MesureS = 12.0f;

	double Horloge = 0.0;
	double DebutMesure = 0.0;

	/** Trames mesurees, en millisecondes. */
	TArray<float> Trames;

	/**
	 * Les QUATRE fils, en millisecondes, accumules sur la duree de mesure.
	 *
	 * POURQUOI LE TOTAL NE SUFFISAIT PAS. Le banc ne rapportait que la trame
	 * entiere. Or une trame de 6,4 ms ne dit pas ce qui la remplit, et la
	 * premiere question de toute mesure de performance est justement celle-la :
	 * limite par le PROCESSEUR ou par le GPU ? Optimiser le GPU sur une trame
	 * limitee par le fil de jeu ne fait rien, et l-inverse non plus.
	 *
	 * ET C-EST CE QUI MANQUAIT POUR TRANCHER LA QUESTION DU VSM. Le moteur
	 * signale un debordement de sa file de marquage d-ombres et previent que
	 * « performance may be affected » -- sans dire de combien. La reponse est
	 * un ecart de temps GPU entre deux passes du banc, ombres actives puis
	 * coupees (`-WorldseedOmbres=0`). Sans cette colonne, on ne pouvait que
	 * supposer, ou faire taire l-avertissement sans savoir.
	 *
	 * ON LIT LES GLOBALES DU MOTEUR, PAS `stat unit`. FStatUnitData n-est
	 * rempli que par son propre affichage (UnrealClient.cpp:361) : s-appuyer
	 * dessus rendrait la mesure dependante d-un affichage a l-ecran, et
	 * rendrait zero quand il est eteint. Les globales, elles, sont mises a
	 * jour par le moteur a chaque trame, qu-on les regarde ou non -- c-est
	 * d-ailleurs ce que `stat unit` lit lui-meme.
	 */
	double SommeJeuMs = 0.0;
	double SommeRenduMs = 0.0;
	double SommeRhiMs = 0.0;
	double SommeGpuMs = 0.0;
	int32 Echantillons = 0;
};
