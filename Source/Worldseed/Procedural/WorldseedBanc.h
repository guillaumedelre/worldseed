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
	// LE PROFIL DOIT ROUVRIR LE TICK. Sans `bProfilDemande` ici, le
	// sous-systeme cesse d'etre tickable des que `Conclure` a tourne -- donc
	// l'attente du vidage ne s'ecoulerait jamais et le jeu ne quitterait pas.
	virtual bool IsTickable() const override
	{
		return bArme || bEnCours || bProfilDemande;
	}

private:
	AWorldseedVoxelTerrain* Terrain() const;
	void Conclure();

	/** Arme par la ligne de commande, construit au premier tick utile. */
	bool bArme = false;
	bool bEnCours = false;
	bool bFini = false;
	bool bQuitterEnsuite = false;

	/**
	 * Demander au RHI de VENTILER la trame GPU, et attendre son vidage.
	 *
	 * POURQUOI CE DRAPEAU EXISTE. Le banc dit que le GPU coute 3,4 ms ; il ne
	 * dit pas OU. Or la mesure du 22 septembre a etabli que ce cout ne vient
	 * PAS du terrain -- trente chunks et 62 000 triangles rendent encore
	 * 3,38 ms, contre 3,47 pour deux mille quatre cents chunks et trois
	 * millions de triangles. Il y a donc un plancher, et un total ne se
	 * corrige pas : il se DECOMPOSE. C'est la lecon que ce depot a payee
	 * quatre fois sur le routage des galeries, et une fois de plus le jour
	 * meme sur le retour au menu.
	 *
	 * `ProfileGPU` (RHI, GPUProfiler.cpp:2199) capture une trame et imprime sa
	 * ventilation DANS LE JOURNAL -- donc utilisable sans editeur, ce qui est
	 * la condition pour mesurer ici. Il vit derriere `WITH_PROFILEGPU`, actif
	 * en Development et absent du build final.
	 *
	 * IL FAUT ATTENDRE APRES L'AVOIR DEMANDE : la capture se declenche a la
	 * trame SUIVANTE et le vidage est asynchrone. Quitter aussitot rendrait un
	 * journal sans ventilation -- et un fichier present mais vide ressemble
	 * exactement a un outil qui ne marche pas.
	 */
	bool bProfilGPU = false;
	bool bProfilDemande = false;
	double DebutAttenteProfil = 0.0;

	/** Delai laisse au vidage du profil avant de quitter, en secondes. */
	float AttenteProfilS = 5.0f;

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

	/**
	 * LA MARCHE, ET POURQUOI LE BANC IMMOBILE NE POUVAIT PAS REPONDRE.
	 *
	 * Le banc mesurait un monde POSE : il attend que le streaming se fige,
	 * puis chronometre. C'est exactement ce qu'il faut pour un cout d'image --
	 * et c'est aveugle a tout ce qui n'arrive qu'EN BOUGEANT. Or la diffusion
	 * ne subdivise un chunk que quand on s'en approche, et ne demande une
	 * feuille neuve que quand on avance : sur un banc immobile, il ne se passe
	 * RIEN de ce qu'on veut voir. « Le monde se genere devant moi » n'etait
	 * donc mesurable par aucun releve de ce depot.
	 *
	 * ON MARCHE PAR L'ENTREE DE DEPLACEMENT, PAS PAR TELEPORTATION. Un
	 * `SetActorLocation` a chaque trame donnerait une origine de streaming qui
	 * glisse, mais pas la marche du joueur : ni collision, ni pente, ni
	 * vitesse reelle du personnage. On veut mesurer le cas REEL, pas une
	 * maquette -- et la distance PARCOURUE est journalisee, donc une marche
	 * bloquee contre une paroi se voit au lieu de se confondre avec une marche
	 * reussie.
	 *
	 * A ZERO, LE BANC EST RIGOUREUSEMENT CELUI D'AVANT. C'est la propriete de
	 * surete de cet ajout, et elle se verifie : sans `-WorldseedMarche=`, pas
	 * une ligne de plus au journal et pas un appel de plus par trame.
	 */
	float MarcheS = 0.0f;
	float MarcheCapDeg = 0.0f;
	bool bEnMarche = false;
	double DebutMarche = 0.0;
	FVector DepartMarcheCm = FVector::ZeroVector;

	/** Les deux ecarts, cumules et au pire, sur toute la marche. */
	int64 SommeOrphelins = 0;
	int64 SommeManquants = 0;
	int64 SommeHysteresis = 0;
	int32 PireOrphelins = 0;
	int32 PireManquants = 0;
	int32 PireHysteresis = 0;
	int32 EchantillonsMarche = 0;

	void MesurerLaMarche(class AWorldseedVoxelTerrain* T);
};
