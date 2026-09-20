// Worldseed - terrain voxel : diffusion des chunks autour du joueur.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedCaves.h"
#include "Procedural/WorldseedLithology.h"
#include "Procedural/WorldseedDensity.h"
#include "Procedural/WorldseedPlateau.h"
#include "Procedural/WorldseedStrata.h"
#include "Procedural/WorldseedVoxelChunk.h"

#include <atomic>

#include "WorldseedVoxelTerrain.generated.h"

class UMaterialInterface;
class UProceduralMeshComponent;

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
 * Un maillage de chunk en cours de fabrication sur un fil de travail.
 *
 * MEME DISCIPLINE QUE LA GENERATION DU MONDE DEPUIS LE MENU : le travailleur ne
 * detient AUCUN UObject, il remplit une structure partagee puis pose son drapeau
 * en ecriture liberee ; le fil de jeu lit le drapeau en acquisition avant de
 * toucher au resultat. Pas de verrou, et l'acteur peut mourir en vol -- le
 * travail se termine dans le vide et le pointeur partage se libere tout seul.
 */
struct FWorldseedVoxelJob
{
	FWorldseedChunkKey Key;
	FBox BoundsM = FBox(ForceInit);

	FWorldseedVoxelMesh Mesh;

	/**
	 * Les primitives de grottes qui touchent CE chunk, extraites une fois.
	 *
	 * Elles voyagent avec le travail plutot que d'etre lues depuis l'acteur :
	 * le fil de maillage ne doit rien tenir qui puisse mourir avant lui.
	 */
	FWorldseedCaveLocal Caves;
	FWorldseedVoxelStats Stats;
	bool bHasSurface = false;

	std::atomic<bool> bDone{ false };
	std::atomic<bool> bCancel{ false };
};

using FWorldseedVoxelJobPtr = TSharedPtr<FWorldseedVoxelJob, ESPMode::ThreadSafe>;

/** Ce qu'on garde d'un chunk pose. */
struct FWorldseedVoxelChunkState
{
	TObjectPtr<UProceduralMeshComponent> Mesh;

	/** Travail en cours, s'il y en a un. */
	FWorldseedVoxelJobPtr Job;

	/** Vrai si le chunk ne contient aucune surface : rien a afficher, jamais. */
	bool bEmpty = false;

	bool bHasCollision = false;

	/**
	 * Les faces de transition avec lesquelles ce chunk a ete maille.
	 *
	 * IL FAUT LE RETENIR : le masque depend du niveau des VOISINS, donc de la
	 * position du joueur. Il change sans que le chunk change de niveau, et un
	 * chunk qui garde un masque perime rouvre la fissure qu-il etait cense
	 * fermer. On le compare a chaque passe et l-on remaille quand il differe.
	 */
	uint8 Masque = 0;

	/** Pourquoi le dernier maillage n'a rien rendu. Diagnostic. */
	FWorldseedVoxelStats::ECause Cause = FWorldseedVoxelStats::ECause::Maille;
	int32 Seeds = 0;
	int32 Tris = 0;
};

/**
 * Terrain voxel diffuse autour du joueur.
 *
 * POURQUOI UN ACTEUR SEPARE DE AWorldseedTerrain. Le terrain en carte
 * d'altitude marche ; tant que le voxel n'a pas fait ses preuves, le monde doit
 * rester jouable. Les deux ne seront fondus qu'a la derniere etape, quand le
 * voxel tiendra la comparaison. Poser un acteur a cote coute une classe de plus
 * et ne casse rien.
 *
 * LA GRILLE 2D DECIDE DE LA VERTICALE, et c'est ce qui rend le streaming 3D
 * abordable : elle donne les bornes d'altitude de la surface sur l'empreinte
 * d'une colonne de chunks, donc on sait sans evaluer le champ quels etages
 * peuvent contenir quelque chose. Les etages du socle ne sont meme pas
 * consideres.
 */
UCLASS()
class WORLDSEED_API AWorldseedVoxelTerrain : public AActor
{
	GENERATED_BODY()

public:
	AWorldseedVoxelTerrain();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ------------------------------------------------------------ diffusion

	/** Cote d'un chunk, en metres. 32 m pour des voxels d'un metre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "8.0"))
	float ChunkSideM = 32.0f;

	/**
	 * Marge sous la bande avant de declarer le joueur hors du monde, en metres.
	 *
	 * La bande fait deja cent metres ; on n'y ajoute qu'une marge de securite
	 * pour ne pas rattraper un joueur qui explore legitimement une cavite
	 * profonde. Sous bande + marge, il n'existe aucune geometrie : ce n'est
	 * plus une chute, c'est une sortie du monde.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "0.0"))
	float PlayerRescueMarginM = 20.0f;

	/**
	 * Rayon de construction autour du joueur, en metres.
	 *
	 * PORTE DE 250 A 600 LE 19 SEPTEMBRE, SUR MESURE ET CONTRE MA PROPRE
	 * CONCLUSION. J'avais ecrit qu'elargir la distance de vue etait
	 * « impossible avec un mailleur a resolution uniforme », le debit de
	 * streaming etant le mur. C'ETAIT FAUX, et la cause etait un defaut de mon
	 * banc : il ne forcait que ce rayon-ci, pas `UnloadRadiusM` fige a 350, si
	 * bien que les chunks lointains etaient batis puis DETRUITS aussitot. Le
	 * monde tournait en boucle sur le meme millier de chunks.
	 *
	 * MESURE UNE FOIS LE BANC REPARE, machine au repos, monde 4096x2048 :
	 *
	 *   rayon   chunks   remplissage   trame        pire    memoire
	 *   250 m      809       9 s       6,42 ms      8,93    6,50 Go
	 *   400 m    2 167      18 s       6,38 ms      8,23    6,94 Go
	 *   600 m    5 085      36 s       6,61 ms      9,15    7,63 Go
	 *   800 m    9 242      62 s       6,78 ms      8,31    8,61 Go
	 *
	 * A 800 m : dix millions de triangles, 147 images par seconde, AUCUN
	 * a-coup -- la pire trame reste a 8 ms. Le cout est la MEMOIRE, pas les
	 * images. 600 m est retenu comme compromis : une mesa entiere tient dans
	 * la vue, le remplissage reste sous la minute, et l'on garde de la marge.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "32.0"))
	float LoadRadiusM = 600.0f;

	/**
	 * Rayon de destruction, volontairement plus grand que celui de construction.
	 * Sans cette hysteresis, un pas en avant et un pas en arriere sur la
	 * frontiere feraient construire et detruire le meme chunk en boucle.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "32.0"))
	float UnloadRadiusM = 840.0f;

	// ------------------------------------------------ anneaux de resolution

	/**
	 * Nombre d'anneaux au-dela du plus fin. ZERO = resolution uniforme.
	 *
	 * A ZERO, LA DIFFUSION EST RIGOUREUSEMENT CELLE D'AVANT -- c'est la
	 * propriete de surete de ce chantier, et elle se verifie : meme compte de
	 * chunks, meme geometrie. L'arbitrage du proprietaire etant « mesurer
	 * d'abord, decouper ensuite », les anneaux arrivent ETEINTS et se mesurent
	 * au banc avant qu'on fixe quoi que ce soit.
	 *
	 * A un niveau de plus, les chunks lointains font 64 m pour des voxels de
	 * 2 m : ils couvrent huit fois le volume d'un chunk fin, donc le compte
	 * cesse de croitre en R au carre.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "0", ClampMax = "4"))
	int32 NiveauMax = 0;

	/**
	 * Rayon du premier anneau -- celui qui reste a pleine resolution, en metres.
	 *
	 * Les suivants DOUBLENT : anneau L jusqu'a `RayonAnneau0M * 2^L`. Ce n'est
	 * pas un choix esthetique : la taille d'un chunk double aussi d'un niveau a
	 * l'autre, donc un rayon qui double garde a peu pres CONSTANT le nombre de
	 * chunks par anneau. Tout autre progression fait enfler un anneau au
	 * detriment des autres.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "32.0"))
	float RayonAnneau0M = 250.0f;

	/**
	 * Relief minimal, EN PENTE, pour qu'un noeud merite d etre subdivise.
	 *
	 * Les anneaux repartissent la finesse par la DISTANCE ; celle-ci la
	 * repartit par le TERRAIN. Un marching cubes depense environ deux
	 * triangles par metre carre de surface, que cette surface soit plate ou
	 * non : sur une plaine, doubler la resolution quadruple les triangles pour
	 * decrire la meme nappe.
	 *
	 * LA GRANDEUR EST SANS DIMENSION -- etendue d'altitude divisee par le cote
	 * du noeud -- donc comparable d'un niveau a l'autre. C'est une pente
	 * moyenne a l'echelle du noeud, et elle se lit contre la distribution du
	 * monde : la pente MEDIANE des terres de Worldseed vaut 30,6 degres, soit
	 * une tangente de 0,59. Un seuil de 0,15 garde donc la finesse sur la
	 * grande majorite des terres et ne l'economise que sur ce qui est
	 * franchement plat -- plaines, plateaux, fond marin.
	 *
	 * A ZERO, le critere est inerte et la diffusion est celle des anneaux
	 * seuls, au chunk pres : une donnee absente doit rester sans effet.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "0.0"))
	float RugositeMin = 0.0f;

	/**
	 * Epaisseur de la dalle de transition, en FRACTION d'une cellule du chunk.
	 *
	 * Jamais en metres : elle doit suivre le niveau de detail. Une epaisseur
	 * nulle raccorde geometriquement sans fissure mais « leads to severe shading
	 * problems » (Lengyel, section 4.3) -- les triangles lateraux degenerent et
	 * leurs normales n'ont plus de sens.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float LargeurTransition = 0.5f;

	// IL Y AVAIT ICI UN RAYON DE COLLISION, plus court que le rayon de
	// chargement, au motif que cuire une collision coute plus cher que mailler.
	// Il est RETIRE : aucune API ne permet de donner la collision a une section
	// deja creee, donc la decision prise au televersement etait definitive, et
	// le joueur qui marchait au-dela passait AU TRAVERS DU SOL. Mesure du
	// defaut : sol present de 0 a 110 m, plus rien de 120 a 250 -- la frontiere
	// tombait exactement sur l'ancien rayon. Ne pas le remettre : si la cuisson
	// coute trop cher, la reponse est de la faire de facon asynchrone.

	/** Travaux simultanes sur le pool de fils. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxJobsInFlight = 24;

	/**
	 * Chunks televerses par passe.
	 *
	 * LE MAILLAGE EST HORS DU FIL DE JEU, PAS LE TELEVERSEMENT : creer une
	 * section de ProceduralMesh touche au moteur de rendu et doit donc rester
	 * sur le fil de jeu. C'est la seule part du cout qui se paie en images.
	 *
	 * PORTE DE 6 A 16 LE 19 SEPTEMBRE, SUR MESURE. Six televersements toutes
	 * les 0,2 s plafonnaient le remplissage a TRENTE chunks par seconde, par
	 * construction -- et c'est exactement le 26 par seconde observe a 250 m,
	 * donc le plafond etait atteint. Le debit n'etait pas une limite physique,
	 * c'etait un REGLAGE, pose a une epoque ou le televersement etait suppose
	 * cher.
	 *
	 * IL NE L'EST PAS : instrumente, il coute 0,21 ms par chunk et 1,17 au
	 * pire, soit 0,2 s CUMULEES pour neuf cents chunks. Le maillage, lui, est
	 * deja sur le pool de fils, et la cuisson de collision est asynchrone
	 * depuis longtemps -- la note du depot qui affirme le contraire est
	 * perimee.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "1", ClampMax = "64"))
	int32 UploadsPerPass = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel",
		meta = (ClampMin = "0.05"))
	float UpdatePeriod = 0.1f;

	/** Materiau des chunks. Il lit la couleur de sommet telle quelle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel")
	TObjectPtr<UMaterialInterface> TerrainMaterial;

	// ------------------------------------------------------------ monde

	/**
	 * Monde de secours, quand rien n'attend dans la GameInstance.
	 *
	 * Sert au banc : on peut poser cet acteur dans une carte vide et obtenir un
	 * monde sans passer par le menu.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde")
	int32 FallbackSeed = 20260909;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde")
	float FallbackHeightMeters = 32000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde")
	int32 FallbackResolutionY = 2048;

	/**
	 * Tient le joueur au-dessus du relief tant que son sol n'existe pas.
	 *
	 * SANS CELA IL TOMBE, ET IL EMPORTE TOUT AVEC LUI. Mesure du premier essai :
	 * pion lache a 400 m au-dessus du sol, arrive a -9 857 m -- et comme c'est
	 * LUI qui sert d'origine a la diffusion, les 233 chunks deja mailles sont
	 * sortis du rayon et ont ete detruits derriere sa chute. Attendre qu'un
	 * chunk porte une collision pour le poser etait circulaire : il etait deja
	 * trop loin pour qu'on en construise un.
	 */
	/**
	 * Exageration verticale du relief, reprise de l'acteur qui pose celui-ci.
	 *
	 * ELLE DOIT ETRE LA MEME DES DEUX COTES : le sol de fond et l'ocean sont
	 * batis avec celle du terrain, et un champ de densite qui l'ignorerait
	 * decrirait un relief a une autre echelle verticale -- la jonction entre
	 * l'horizon et le sol proche se verrait comme une marche.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde",
		meta = (ClampMin = "0.01"))
	float HeightExaggeration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde")
	bool bHoldPlayer = true;

	// ------------------------------------------------------------ releves

	/** Etat de la diffusion, pour la sonde et le journal. */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Voxel")
	FString ReportState() const;

private:
	bool LoadWorld();

public:
	/**
	 * Reprend TEL QUEL le monde de l'acteur qui pose celui-ci.
	 *
	 * SANS CELA, LES DEUX EN GENERENT DEUX DIFFERENTS, en silence. Quand aucun
	 * monde n'attend dans l'instance de jeu -- c'est le cas d'un PIE lance
	 * depuis l'editeur, sans passer par le menu -- chaque acteur tombe sur sa
	 * generation de secours, et leurs resolutions ne sont pas les memes : 512 x
	 * 256 pour le terrain, 2048 x 1024 ici. Meme graine, relief different. Le
	 * sol de fond decrivait donc un autre monde que celui qu'on a sous les
	 * pieds, sans le moindre avertissement.
	 *
	 * A appeler AVANT FinishSpawning : BeginPlay charge le monde, et il est
	 * trop tard apres.
	 */
	void AdoptWorld(int32 InSeed, const FWorldseedGeometry& InGeometry,
		const TArray<float>& InHeightsM, const FWorldseedBiomeMap& InBiomes,
		float InHeightExaggeration, const FWorldseedCaveNetwork& InCaves,
		const FWorldseedLithology& InLithology,
		const TArray<float>& InPrecipMm, const TArray<float>& InTempMeanC);

private:

	void UpdateChunks();

	/** Origine de la diffusion : le pion s'il existe, sinon l'acteur. */
	FVector StreamingOriginCm() const;

	/** Centre d'un chunk, en centimetres monde. */
	FVector ChunkCentreCm(const FWorldseedChunkKey& Key) const;

	/** Boite d'un chunk, en metres dans le repere de l'acteur. */
	FBox ChunkBoundsM(const FWorldseedChunkKey& Key) const;

	/** Cote d'un chunk et taille d'un voxel, au niveau donne. */
	double CoteM(int32 Niveau) const
	{
		return static_cast<double>(ChunkSideM) * static_cast<double>(1 << Niveau);
	}
	float VoxelM(int32 Niveau) const
	{
		return DensityRules.VoxelSizeM * static_cast<float>(1 << Niveau);
	}

	/** Rayon exterieur de l'anneau de niveau L. Double a chaque cran. */
	double RayonAnneauM(int32 Niveau) const
	{
		return static_cast<double>(RayonAnneau0M) * static_cast<double>(1 << Niveau);
	}

	/**
	 * Le niveau REELLEMENT emis en un point, obtenu par la MEME descente que la
	 * diffusion.
	 *
	 * IL NE SUFFIT PAS DE LIRE UNE DISTANCE. Deux chunks de niveaux differents
	 * n'ont pas le meme centre, donc un critere pose sur la distance seule peut
	 * faire emettre les deux -- geometrie en double -- ou aucun des deux -- trou.
	 * La seule definition sure est celle que la diffusion applique : on descend
	 * depuis le niveau le plus grossier en subdivisant tant que le noeud qui
	 * contient le point est assez proche. Meme predicat, donc resultat coherent
	 * par construction.
	 */
	int32 NiveauEn(const FVector& PointM, const FVector& OrigineM) const;

	/**
	 * Les faces de ce chunk qui bordent un voisin PLUS FIN.
	 *
	 * Ce sont celles-la qui reclament une cellule de transition : Lengyel les
	 * place dans le bloc GROSSIER, parce que c'est lui qui a trop peu
	 * d'echantillons. Le niveau le plus fin n'en a donc jamais.
	 */
	uint8 MasqueDe(const FWorldseedChunkKey& Key, const FVector& OrigineM) const;

	/** Descend un noeud jusqu'aux feuilles de la partition, et les collecte. */
	void Enumerer(const FWorldseedChunkKey& Key, const FVector& OrigineM,
		TArray<TPair<FWorldseedChunkKey, double>>& Sortie) const;

	/** Lance le maillage d'un chunk sur le pool de fils. */
	/** Bornes d-altitude d-une colonne de chunks, par le cache. */
	void PlageSurface(int32 CX, int32 CY, int32 Niveau,
		float& OutMinM, float& OutMaxM) const;

	/**
	 * LE PREDICAT UNIQUE : ce noeud se subdivise-t-il ?
	 *
	 * IL N'EXISTE QU'UNE FOIS, ET C'EST PORTANT. La diffusion s'en sert pour
	 * decider quels chunks emettre, `NiveauEn` pour savoir quel niveau un
	 * voisin porte, donc quelles faces de transition armer. Les ecrire deux
	 * fois ferait armer des cellules de transition la ou il n'y a pas de
	 * changement de resolution, et en oublierait ailleurs -- c'est-a-dire une
	 * fissure, qu'aucune mesure de couture ne saurait attribuer.
	 */
	bool DoitSubdiviser(const FWorldseedChunkKey& Key, const FVector& OrigineM) const;

	/** Le relief de ce noeud justifie-t-il un voxel plus fin ? */
	bool NoeudAccidente(const FWorldseedChunkKey& Key) const;

	void UpdateChunksInterne();
	void LaunchJob(const FWorldseedChunkKey& Key);

	/** Televerse un maillage termine dans son composant. */
	void UploadChunk(const FWorldseedChunkKey& Key, FWorldseedVoxelChunkState& State);

	/** Couleur et teinte d'un sommet, depuis la carte des biomes. */
	void PaintVertices(FWorldseedVoxelMesh& Mesh) const;

public:
	/**
	 * Etat de la colonne de chunks qui contient ce point. Diagnostic.
	 *
	 * IL N'EXISTE AUCUN AUTRE MOYEN DE SAVOIR POURQUOI UN TROU EST LA. De
	 * l'exterieur, un chunk jamais considere, un chunk declare vide et un chunk
	 * maille a zero triangle se ressemblent tous les trois : on sonde, on ne
	 * touche rien, et on ne peut pas distinguer les trois causes -- qui
	 * appellent pourtant trois corrections differentes.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Voxel")
	FString DiagnostiquerColonne(FVector MondeCm) const;

	/**
	 * Pose le joueur a un endroit choisi, en metres dans le repere du monde.
	 *
	 * IL NE SUFFIT PAS DE DEPLACER LE PION, ET LE PROJET A DEJA PAYE CE PIEGE.
	 * Les chunks se batissent AUTOUR de lui : a l'arrivee il n'y a rien sous
	 * ses pieds, il tombe, et comme c'est lui qui donne l'origine de la
	 * diffusion il emmene la fenetre de chunks dans sa chute -- mesure, -4745 m.
	 * On reutilise donc le filet qui existe deja : le pion est TENU EN VOL
	 * jusqu'a ce que le chunk qui le porte ait une collision cuite, puis rendu
	 * a la gravite. C'est exactement ce que fait la mise en place initiale, et
	 * la refaire ici ferait diverger les deux moities.
	 *
	 * SEULE DIFFERENCE AVEC CETTE MISE EN PLACE : on ne cherche PAS de sol plat
	 * alentour. L'endroit a ete demande, on y va.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Voxel")
	void TeleporterJoueur(double XMetres, double YMetres);

	/** Ou est le joueur, et sur quoi. Pour juger une capture sans deviner. */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Voxel")
	FString OuSuisJe() const;

	/**
	 * Le monde charge, en LECTURE SEULE, pour qui doit l'interroger.
	 *
	 * L'acteur charge et possede ce monde : y donner acces en lecture est
	 * legitime et evite de le recopier. Ce qui ne le serait pas, c'est qu'il
	 * se charge lui-meme des travaux qui s'en servent -- la tournee photo a
	 * vecu ici par commodite avant d'aller dans son propre sous-systeme.
	 */
	const FWorldseedGeometry& MondeGeometrie() const { return Geometry; }
	const TArray<float>& MondeAltitudes() const { return HeightsM; }
	const FWorldseedCaveNetwork& MondeGrottes() const { return CaveNetwork; }

	/** Les sites de tables, rebatis au chargement et jamais serialises. */
	const TArray<FWorldseedPlateauSite>& MondeTables() const { return Tables; }

	/** Les fonds de canyon, rebatis au chargement comme les tables. */
	const TArray<FWorldseedPlateauSite>& MondeCanyons() const { return Canyons; }

	/** Rayon de chargement effectif, en metres. Lu par le banc. */
	float RayonDeChargementM() const { return LoadRadiusM; }

	/** Chunks suivis, et travaux en vol. Le banc s en sert pour savoir
	 *  quand le streaming est STABILISE -- un compte fige et aucun travail. */
	/**
	 * Vrai quand la diffusion ne bouge plus : compte FIGE et aucun travail en vol.
	 *
	 * UNE SEULE DEFINITION POUR DEUX CONSOMMATEURS. Le banc a appris cette
	 * lecon a ses depens -- sa premiere version chauffait un nombre FIXE de
	 * secondes et rendait EXACTEMENT 552 chunks a 250 m comme a 400, donc un
	 * transitoire identique des deux cotes et un A/B qui ne comparait rien. La
	 * tournee photo est restee au delai fixe et prenait des paysages a moitie
	 * batis. La regle du depot interdit de recopier une formule dans deux
	 * fichiers : elle vit donc ici, et les deux l-appellent.
	 *
	 * `DernierCompte` est la memoire de l-appelant, mise a jour au passage.
	 */
	bool DiffusionStable(int32& DernierCompte) const
	{
		const int32 N = Chunks.Num();
		const bool bFige = (N == DernierCompte) && (TravauxEnVol() == 0);
		DernierCompte = N;
		return bFige;
	}

	int32 NombreDeChunks() const { return Chunks.Num(); }
	int32 TravauxEnVol() const;
	const FWorldseedDensity& MondeChamp() const { return Density; }

	/** Vrai quand le pion a ete rendu a la gravite sur un sol solide. */
	bool JoueurPose() const { return bPlayerReleased; }

	/**
	 * Les endroits du monde CHARGE qui meritent d'etre vus.
	 *
	 * Calcules a la demande depuis le relief et la roche, jamais ecrits en dur :
	 * une liste de coordonnees serait juste pour une graine et fausse pour
	 * toutes les autres.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Voxel")
	FString LieuxRemarquables() const;

private:

	void ReleaseChunk(const FWorldseedChunkKey& Key);

	/** Tient le joueur en l'air, puis le rend a la gravite quand le sol existe. */
	void HoldOrReleasePlayer();

	/** Clef du chunk qui porte un point donne, en metres repere acteur. */
	FWorldseedChunkKey KeyForPoint(double X, double Y, double Z) const;

	/**
	 * Cherche un endroit ou POSER le joueur : plat, emerge, et plein dessous.
	 *
	 * Les trois criteres comptent. Emerge, sinon on nait dans la mer. Plat,
	 * sinon on glisse. Et surtout PLEIN DESSOUS : depuis que les galeries
	 * existent, une colonne sur huit porte un vide, et le pion tombe dedans --
	 * mesure, il s'est retrouve a 47 m sous terre au premier essai.
	 *
	 * Faux si rien ne convient dans le rayon fouille.
	 */
	/**
	 * La terre emergee la plus proche, CHERCHEE DANS LA GRILLE 2D.
	 *
	 * `FindFlatGround` choisit ou se poser une fois qu-on est sur la bonne
	 * terre ; celle-ci trouve la terre. Les deux portees n-ont rien a voir --
	 * 384 metres contre des dizaines de kilometres -- et ce monde est de
	 * l-ocean a 70,8 %.
	 */
	bool TrouverTerreEmergee(const FVector2D& AutourM,
		double& OutX, double& OutY) const;

	bool FindFlatGround(const FVector2D& AroundM, double& OutX, double& OutY,
		float& OutSurfaceM, float& OutSlopeDeg) const;

	UPROPERTY()
	TObjectPtr<USceneComponent> RootScene;

	FWorldseedGeometry Geometry;
	TArray<float> HeightsM;
	FWorldseedBiomeMap Biomes;
	int32 WorldSeed = 0;

	/** Le reseau de grottes du monde charge. */
	FWorldseedCaveNetwork CaveNetwork;

	/** Sites de tables, fonction pure du relief et de la graine. */
	TArray<FWorldseedPlateauSite> Tables;

	/** Fonds de canyon : l autre face du meme objet. */
	TArray<FWorldseedPlateauSite> Canyons;

	/** Pluie annuelle, pour appliquer aux sites les gardes de la passe. */
	TArray<float> PrecipMm;

	/** La temperature moyenne annuelle, pour la garde de froid des plateaux. */
	TArray<float> TempMeanC;

	/** La serie stratigraphique, lue une fois au chargement. */
	FWorldseedStratRules StratRules;

	/** Durete par identifiant, pour la garde du socle sedimentaire. */
	TArray<float> DureteParId;
	FWorldseedLithology Lithology;

	/**
	 * Couleur de chaque roche, indexee par identifiant.
	 *
	 * COPIEE, PAS REFERENCEE : cinq couleurs tiennent dans une poignee d'octets,
	 * et cela evite de dependre de la duree de vie des regles au moment ou l'on
	 * peint un chunk.
	 */
	TArray<FLinearColor> CouleurParRoche;

	/** Libelle de chaque roche, meme indexation. Pour le releve, pas le rendu. */
	TArray<FString> NomParRoche;

	/** Destination demandee, tant que le pion n'y est pas pose. */
	bool bTeleportPose = false;
	FVector2D TeleportXYM = FVector2D::ZeroVector;

	FWorldseedDensityRules DensityRules;
	FWorldseedDensity Density;

	/**
	 * Ou en est le balayage des masques perimes.
	 *
	 * IL EST BORNE PAR PASSE, ET LA MESURE L-A EXIGE. Repasser sur TOUTES les
	 * feuilles a chaque mise a jour coute `feuilles x 6 x niveaux` descentes de
	 * NiveauEn, et cela se voit : a 2400 m le p95 montait a 14 ms pour une
	 * moyenne de 6,6 -- un pic periodique, exactement la cadence de la passe.
	 * Le controle qui l-a prouve : deux anneaux a 2400 m (3 934 feuilles, deux
	 * niveaux) donnent le MEME pic que trois anneaux (2 436 feuilles, trois
	 * niveaux), alors que les comptes de chunks n-ont rien de commun. C-est donc
	 * le produit qui compte, pas le nombre de chunks -- et le niveau 3, un temps
	 * soupconne, est innocent.
	 */
	double TotalUpdateMs = 0.0;
	double WorstUpdateMs = 0.0;
	int32 UpdateCount = 0;

	/**
	 * Bornes d-altitude par colonne et par niveau, calculees UNE FOIS.
	 *
	 * MESURE : la passe de diffusion coutait 13,65 ms a 2400 m -- soit le pic de
	 * p95 observe au banc, retrouve en la chronometrant au lieu de le supposer.
	 * La cause est SurfaceRangeM, qui echantillonne au pas de la grille : un
	 * noeud de 256 m demande 289 lectures, et la descente la rappelle pour le
	 * meme noeud que la boucle de tete vient d-interroger.
	 *
	 * Or LE RELIEF 2D NE CHANGE PAS EN COURS DE PARTIE. Ces bornes sont donc une
	 * constante du monde, pas une grandeur a recalculer dix fois par seconde.
	 */
	mutable TMap<FIntVector, FVector2D> CacheSurface;

	/**
	 * Verdict de relief par colonne et par niveau : ce noeud merite-t-il
	 * d etre subdivise ? Mis en cache parce que `NiveauEn` le redemande pour
	 * chaque voisin de chaque feuille a chaque passe de masque.
	 */
	mutable TMap<FIntVector, uint8> CacheRugosite;

	int32 CurseurMasque = 0;

	/** Une seule alerte par partie : l ecart 2:1 est une propriete, pas un compteur. */
	mutable bool bEcart2a1Signale = false;

	TMap<FWorldseedChunkKey, FWorldseedVoxelChunkState> Chunks;

	FTimerHandle UpdateTimer;

	bool bWorldAdopted = false;
	bool bWorldReady = false;
	bool bPlayerHeld = false;
	bool bPlayerReleased = false;

	/** Cumuls pour le releve. */
	int32 BuiltChunks = 0;
	int32 EmptyChunks = 0;

	/** Travaux annules puis repris. Un compteur qui monte sans fin est un signe. */
	int32 AbandonedChunks = 0;

	/** Chunks ou le mailleur n'a rien rendu malgre une traversee. */
	int32 DegenerateChunks = 0;
	int32 TotalTriangles = 0;
	double TotalMeshMs = 0.0;
	double WorstMeshMs = 0.0;

	/**
	 * Temps passe sur le FIL DE JEU a televerser les chunks.
	 *
	 * LE MAILLAGE EST DEJA HORS DU FIL DE JEU, et la cuisson de collision
	 * aussi (bUseAsyncCooking). Ce qui reste sur le fil de jeu est la
	 * creation du COMPOSANT -- un par chunk -- et son enregistrement, plus
	 * la copie du maillage. C est donc le seul candidat restant pour
	 * expliquer le debit, et il faut le MESURER avant de le supposer : le
	 * depot a une note perimee affirmant que la cuisson est synchrone, ce
	 * qui n est plus vrai depuis longtemps.
	 */
	double TotalUploadMs = 0.0;
	double WorstUploadMs = 0.0;
	int32 UploadCount = 0;
	double FirstFillSeconds = 0.0;
	double StartSeconds = 0.0;
};
