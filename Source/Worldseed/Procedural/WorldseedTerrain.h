// Worldseed - terrain runtime decoupe en chunks, depuis le monde spherique.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedClimatePreset.h"
#include "Procedural/WorldseedApparence.h"
#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedCaves.h"
#include "Procedural/WorldseedLithology.h"
#include "Procedural/WorldseedVoxelTerrain.h"
#include "Procedural/WorldseedTexturePack.h"
#include "Procedural/WorldseedWorldData.h"
#include "WorldseedTerrain.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

/** Ce que les couleurs de sommet transportent jusqu'au materiau. */
UENUM(BlueprintType)
enum class EWorldseedTerrainColouring : uint8
{
	/**
	 * Poids de quatre couches dans RGBA : roche, vegetation, sable, neige.
	 *
	 * Le materiau melange des textures selon ces poids. Riche a l'oeil, mais
	 * il ne distingue que quatre matieres : une savane et une steppe y sont
	 * identiques.
	 */
	LayerWeights	UMETA(DisplayName = "Poids de couches"),

	/**
	 * Couleur du biome, directement dans RGB.
	 *
	 * Les dix-neuf biomes se lisent alors d'un coup d'oeil. Demande un materiau
	 * qui affiche la couleur de sommet telle quelle — voir BiomeMaterial.
	 */
	BiomeColour		UMETA(DisplayName = "Couleur de biome"),

	/**
	 * Poids de quatre MATIERES dans RGBA, teinte du biome dans UV1/UV2.
	 *
	 * Le materiau melange quatre textures de sol par ces poids, puis les teinte.
	 * C'est ce qui permet a une savane et a une prairie de partager la meme
	 * texture d'herbe sans se ressembler — et donc a un pack de quatre textures
	 * d'habiller dix-neuf biomes.
	 */
	TexturePack		UMETA(DisplayName = "Pack de textures"),
};


/**
 * Terrain procedural construit au runtime, decoupe en chunks.
 *
 * POURQUOI PAS WORLD PARTITION. WP streame des acteurs qui EXISTENT DEJA sur
 * disque : on les place dans l'editeur, ils sont sauvegardes en external
 * actors, et WP choisit lesquels charger. Ici le monde nait d'une graine au
 * lancement : il n'y a aucun acteur auteure, donc rien a streamer. WP n'aurait
 * pas de travail. Il redeviendrait le bon outil le jour ou les mondes seraient
 * CUITS hors-ligne — ce qui contredirait l'exigence de generation runtime.
 *
 * La carte est la surface deroulee d'une sphere : 360 degres de longitude sur
 * 180 de latitude, donc deux fois plus large que haute.
 */
UCLASS()
class WORLDSEED_API AWorldseedTerrain : public AActor
{
	GENERATED_BODY()

public:
	AWorldseedTerrain();

	// ------------------------------------------------------------- monde

	/** Graine utilisee si aucun monde n'attend dans le GameInstance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde")
	int32 FallbackSeed = 20260909;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde",
		meta = (ClampMin = "100.0"))
	float FallbackHeightMeters = 32000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde",
		meta = (ClampMin = "64", ClampMax = "2048"))
	int32 FallbackResolutionY = 2048;

	/** Exageration verticale. 1 = altitudes reelles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde",
		meta = (ClampMin = "0.1"))
	float HeightExaggeration = 1.0f;

	/** Sommets des deux nappes, pour rapporter ce que chacune pese. */
	int32 ProxySommets = 0;
	int32 ProxyVueSommets = 0;

	/**
	 * Decimation du sol qu'on VOIT par rapport a celui que l'eau LIT.
	 *
	 * ON NE PEUT PAS DUPLIQUER LA NAPPE A L'IDENTIQUE, et le chiffre tranche :
	 * un FProcMeshVertex porte position, normale, tangente, couleur et quatre
	 * jeux de coordonnees, le tout en double precision -- de l'ordre de cent
	 * cinquante octets. A 8 388 608 sommets, la copie processeur d'UNE nappe
	 * pese plus d'un gigaoctet. La seconde se paie donc decimee.
	 *
	 * Deux, soit un sommet sur quatre : l'horizon passe de 16 a 31 metres de
	 * pas. On ne le voit qu'au-dela du rayon de chargement, ou 31 metres a
	 * 2400 sous-tendent moins d'un degre.
	 */
	UPROPERTY(EditAnywhere, Category = "Worldseed|Sol de fond",
		meta = (ClampMin = "1", ClampMax = "8", EditCondition = "bBuildGroundProxy"))
	int32 GroundProxyHorizonStride = 2;

	/**
	 * De combien le sol qu'on VOIT s'enfonce EN PLUS, en metres.
	 *
	 * C'EST LUI QUI REND UNE ARCHE TRAVERSABLE DU REGARD, et il n'etait pas
	 * reglable tant qu'une seule nappe servait les deux maitres : l'enfoncer
	 * faisait croire a l'eau qu'elle pouvait monter d'autant, et elle noyait
	 * des flancs de colline entiers -- defaut mesure, consigne, et paye.
	 *
	 * Separee, la nappe d'affichage ne nourrit plus rien : elle peut passer
	 * sous les ouvertures d'arches et de grottes au lieu de les boucher.
	 *
	 * LE PRIX EST UNE MARCHE A LA LIMITE DU TERRAIN DETAILLE. A 2400 metres,
	 * quarante metres sous-tendent un degre : c'est le genre de chose qui se
	 * juge a l'image et pas au calcul.
	 */
	UPROPERTY(EditAnywhere, Category = "Worldseed|Sol de fond",
		meta = (ClampMin = "0.0", EditCondition = "bBuildGroundProxy"))
	float GroundProxyHorizonExtraDropM = 40.0f;

	/**
	 * Marge sous la bande creusable, en metres.
	 *
	 * L'enfoncement du decor vu est DEDUIT et non choisi : il doit passer sous
	 * `bandeM`, la seule profondeur ou le voxel creuse quoi que ce soit. Cette
	 * marge couvre ce que la bande ne borne pas -- le deplacement vertical du
	 * champ, et le fait qu'une ouverture s'evase vers le bas.
	 */
	UPROPERTY(EditAnywhere, Category = "Worldseed|Sol de fond",
		meta = (ClampMin = "0.0", EditCondition = "bBuildGroundProxy"))
	float GroundProxyHorizonMarginM = 25.0f;

	/**
	 * Le point de depart choisi dans le menu, en metres sur la carte.
	 *
	 * CE TERRAIN NE S'EN SERT PAS : c'est l'acteur voxel qui place le joueur.
	 * Il ne fait que le CONVOYER, parce qu'il est le seul a lire l'instance de
	 * jeu des que L_Menu a joue.
	 */
	bool bDepartDemande = false;
	FVector2D DepartXYM = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Monde")
	TObjectPtr<UMaterialInterface> TerrainMaterial;

	// ----------------------------------------------------------- couches

	/**
	 * Pente, en degres, a partir de laquelle la roche prend le dessus.
	 * Les deux bornes forment la zone de transition.
	 */
	/** Ce que les couleurs de sommet transportent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches")
	EWorldseedTerrainColouring Colouring = EWorldseedTerrainColouring::BiomeColour;

	/** Materiau employe en mode couleur de biome. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches")
	TObjectPtr<UMaterialInterface> BiomeMaterial;

	/**
	 * Materiau de la mer du DECOR, au-dela de la fenetre du plugin Water.
	 *
	 * Laisse a nul, le code prend celui du plugin
	 * (`/Water/Materials/WaterSurface/Water_FarMesh`) : il est versionne avec
	 * le moteur, alors qu'un asset pose dans `Content/` serait exclu du depot.
	 * Le renseigner ici permet d'en essayer un autre sans recompiler.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches")
	TObjectPtr<UMaterialInterface> HorizonSeaMaterial;

	/**
	 * Un materiau par pack de textures.
	 *
	 * Ce sont des INSTANCES d'un meme materiau maitre, chacune avec ses quatre
	 * textures. Changer de pack ne change donc ni le maillage ni les couleurs
	 * de sommet : seul le materiau pose sur les chunks change.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches")
	TMap<EWorldseedTexturePack, TObjectPtr<UMaterialInterface>> PackMaterials;

	/** Pack choisi dans le menu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches")
	EWorldseedTexturePack TexturePack = EWorldseedTexturePack::BiomeColour;

	/**
	 * Part d'eau melee a la couleur du biome, de 0 a 1.
	 *
	 * A ZERO, LE BIOME SEUL : on lit la savane jusque sur la plage, ce qui est
	 * l'interet d'avoir separe les deux axes. A UN, la mer recouvre tout et on
	 * retrouve l'ancien comportement. Entre les deux, on voit le rivage ET dans
	 * quel biome il s'inscrit.
	 *
	 * L'axe de couverture ne porte plus que l'ocean : les lacs et les rivieres
	 * ont ete retires du generateur le 18 septembre 2026.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CoverTint = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches",
		meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float RockSlopeStartDeg = 26.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches",
		meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float RockSlopeFullDeg = 42.0f;

	/** Altitude, en metres, jusqu ou la plage remonte depuis le rivage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches")
	float BeachTopM = 12.0f;

	/** Temperature moyenne, en degres, sous laquelle la neige tient. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches")
	float SnowTempC = -2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches")
	float SnowTempFullC = -8.0f;

	/** Precipitations, en mm/an, bornant l aridite et la luxuriance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches")
	float AridMm = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Couches")
	float LushMm = 900.0f;

	// ------------------------------------------------------- sol de fond

	/** Construit le sol de fond couvrant tout le monde. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Chunks")
	bool bBuildGroundProxy = true;

	/**
	 * Largeur du sol de fond, en sommets.
	 *
	 * LE COMMENTAIRE D'ORIGINE NE VOYAIT QUE LA MOITIE DU PROBLEME. Il calait
	 * cette valeur sur la texture d'information de l'eau -- 512 pixels pour
	 * toute la zone -- au motif qu'au-dela on paierait un detail qu'elle ne
	 * sait pas lire. C'est juste pour l'EAU, et faux pour l'OEIL : cette nappe
	 * est aussi tout ce que le joueur voit au-dela du rayon de chargement des
	 * chunks, soit 250 m.
	 *
	 * A 64 km, 512 sommets font 125 m par maille, et une maille de 125 m vue a
	 * 500 m couvre quatorze degres du champ de vision : le monde lointain se
	 * lit alors comme un pavage de grands triangles. C'est ce que montraient
	 * les photos, et aucun travail sur le champ de densite ne pouvait y changer
	 * quoi que ce soit -- il ne s'applique qu'aux 250 m proches.
	 *
	 * A 2048 IL COLLE EXACTEMENT A SA SOURCE, et c'est le plafond utile : le
	 * code le borne d'ailleurs a `Geometry.NX`. A 1024 il en jetait la MOITIE
	 * pour rien -- 62 m de maille pour un relief calcule a 31 -- et c'est ce
	 * qui rendait les grandes formes invisibles de loin : un canyon de 150 m
	 * ne faisait plus que 2,4 mailles affichees, une mesa de 500 m en faisait
	 * huit. Quatre tournees photo n'ont montre que des creux doux et des
	 * plateaux lisses pour cette seule raison.
	 *
	 * IL SUIT LA SIMULATION, ET IL DOIT LA SUIVRE. Quand la grille est passee
	 * a 4096 le 19 septembre, un proxy reste a 2048 serait redevenu la MOITIE
	 * de sa source -- exactement le defaut qu'on venait de corriger. La valeur
	 * et le plafond montent donc avec elle ; le code borne de toute facon a
	 * `Geometry.NX`, donc mettre plus haut ne coute rien et ne fait rien.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Chunks",
		meta = (ClampMin = "32", ClampMax = "4096", EditCondition = "bBuildGroundProxy"))
	int32 GroundProxyWidth = 4096;

	/**
	 * Enfoncement du sol de fond sous le terrain detaille, en metres.
	 *
	 * Les deux maillages decrivent le meme relief : sans ce retrait ils se
	 * disputeraient le meme plan et scintilleraient. Sous les chunks, le fond
	 * ne se voit jamais ; au-dela, il est seul et le retrait ne se remarque pas.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Chunks",
		meta = (ClampMin = "0.0", EditCondition = "bBuildGroundProxy"))
	float GroundProxyDropM = 1.0f;

	/**
	 * Retirer le sol de fond du rendu quand l'oeil est SOUS la surface.
	 *
	 * UN DECOR D'HORIZON N'A RIEN A FAIRE DANS UN VOLUME FERME. La nappe
	 * grossiere traverse les salles et les galeries et s'y dessine par-dessus
	 * la roche : mesure, elle occupait 70,2 % du cadre dans la salle sous le
	 * gouffre. Ce qu'on perd en la coupant est mesure aussi : depuis une bouche
	 * de grotte, regard vers le dehors, 4,1 % du cadre seulement -- parce que
	 * le terrain voxel couvre entierement les 250 m du rayon de chargement, et
	 * que ce qu'on voit par une ouverture est presque toujours du vrai terrain.
	 * Dix-sept fois plus de degats a l'interieur que de perte a la sortie.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Chunks",
		meta = (EditCondition = "bBuildGroundProxy"))
	bool bHideGroundProxyUnderground = true;

	/**
	 * Hauteur sondee au-dessus de l'oeil pour chercher un plafond, en metres.
	 *
	 * Le sondage doit depasser le relief le plus epais qu'on puisse avoir
	 * au-dessus de la tete. Cinq cents metres couvrent largement l'amplitude
	 * du monde ; au-dela on paierait un rayon qui ne rencontre jamais rien.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Chunks",
		meta = (ClampMin = "10.0", EditCondition = "bHideGroundProxyUnderground"))
	float GroundProxyRoofProbeM = 500.0f;

	/** Inverse l'ordre des triangles si le terrain apparait retourne. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Chunks")
	bool bFlipWinding = false;

	// ------------------------------------------------------------- voxel

	/**
	 * LE VOXEL EST LE SEUL MAILLEUR, DEPUIS LE 22 SEPTEMBRE 2026.
	 *
	 * Il y avait ici un drapeau `bUseVoxelMesher` et, derriere lui, un mailleur
	 * en carte d'altitude de 456 lignes garde comme REPLI. Il n'etait joignable
	 * par aucune ligne de commande -- seulement en basculant ce drapeau dans
	 * l'editeur -- rien ne l'avait exerce depuis le 18 septembre, et l'on ne
	 * savait donc pas s'il fonctionnait encore. Un filet qu'on n'eprouve pas
	 * n'est pas un filet, c'est une dette. L'historique git le garde intact.
	 *
	 * Ce qu'il bloquait : `ComputeVertexAppearance` avait DEUX consommateurs --
	 * lui et le sol de fond -- donc aucun des deux ne pouvait quitter cet
	 * acteur sans recopier la regle, ce que le depot interdit.
	 */

	/** Classe de l'acteur voxel a poser. Vide : la classe native. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed|Voxel")
	TSubclassOf<AWorldseedVoxelTerrain> VoxelTerrainClass;

	// ------------------------------------------------------------- API

	/** Recharge le monde et repart de zero. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Worldseed")
	void Rebuild();

	UFUNCTION(BlueprintPure, Category = "Worldseed")
	float GetHeightAtWorldXY(float WorldX, float WorldY) const;

	UFUNCTION(BlueprintPure, Category = "Worldseed")
	void GetLonLatAtWorldXY(float WorldX, float WorldY,
		float& OutLongitudeDeg, float& OutLatitudeDeg) const;

	/**
	 * Climat au point donne. Faux si le monde n'en porte pas.
	 *
	 * Rend un echantillon complet plutot que trois nombres separes : c'est
	 * l'objet que le ciel consomme, et l'assembler ici evite que chaque
	 * appelant le reconstitue a sa facon.
	 */
	bool SampleClimateAtWorldXY(float WorldX, float WorldY,
		FWorldseedClimateSample& OutSample) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Recupere le monde du GameInstance, ou le genere en secours. */
	bool AcquireWorld();

	/** Pose l'acteur voxel qui prend le relief proche en charge. */
	void SpawnVoxelTerrain();

	/** Rebatit le reseau de grottes quand le monde vient du menu. */
	void RebuildCaveNetwork();

	/** Echantillonne le climat sous le joueur et le pousse au ciel. */
	void FeedSky(float DeltaSeconds);

	/** Position de reference pour le chargement : le joueur, sinon l'acteur. */
	FVector GetStreamingOrigin() const;

	/**
	 * Apparence d'un sommet : poids de matiere ou couleur, plus la teinte.
	 *
	 * Partagee entre les chunks detailles et le sol de fond : deux versions de
	 * cette regle finiraient par diverger, et la difference se verrait
	 * exactement la ou les deux maillages se rencontrent.
	 */
	/**
	 * Les seuils de surface, rassembles pour etre passes d'un bloc.
	 *
	 * Ils restent editables sur l'acteur -- c'est la ou un auteur les regle --
	 * mais le CALCUL qui les consomme a quitte cette classe. Les recopier dans
	 * une structure a chaque appel coute huit flottants ; les laisser se lire
	 * un par un depuis le calcul aurait rendu celui-ci dependant de l'acteur,
	 * donc intestable.
	 */
	FWorldseedSurfaceRegles ReglesSurface() const
	{
		FWorldseedSurfaceRegles R;
		R.RockSlopeStartDeg = RockSlopeStartDeg;
		R.RockSlopeFullDeg = RockSlopeFullDeg;
		R.BeachTopM = BeachTopM;
		R.SnowTempC = SnowTempC;
		R.SnowTempFullC = SnowTempFullC;
		R.AridMm = AridMm;
		R.LushMm = LushMm;
		R.CoverTint = CoverTint;
		return R;
	}

	/**
	 * Construit le SOL DE FOND : tout le monde, en basse resolution.
	 *
	 * DEUX RAISONS, ET LA SECONDE EST LA PLUS IMPORTANTE.
	 *
	 * Les chunks detailles ne couvrent qu'un rayon autour du joueur — quinze
	 * pour cent d'une carte de seize kilometres. Au-dela, le monde s'arretait
	 * net.
	 *
	 * Et surtout : le systeme d'eau lit le SOL pour savoir ou l'eau rencontre la
	 * terre. Il ne voyait donc du sol que sur ces quinze pour cent, et appliquait
	 * partout ailleurs une profondeur forfaitaire — d'ou ces grandes zones
	 * sombres bordees d'aretes droites, qui sont les frontieres des chunks
	 * charges et non des cotes.
	 */
	void BuildGroundProxy();

	/** Retire ou rend le sol de fond selon que l'oeil est sous terre ou non. */
	void UpdateGroundProxyVisibility();

	FTimerHandle ProxyTimer;
	bool bGroundProxyHidden = false;

	/** Mesures concordantes avant de basculer. Anti-rebond au bord d'un plafond. */
	int32 ProxyVotes = 0;

	/** Le materiau correspondant au mode d'apparence courant. */
	UMaterialInterface* ChooseTerrainMaterial(const FWorldseedAppearance& Mode) const;

	/**
	 * Le materiau de la MER DU DECOR, section 1 de la nappe vue.
	 *
	 * `Repli` est rendu quand aucun materiau de mer n'est disponible ou que la
	 * ligne de commande le coupe : le decor reprend alors l'aspect de terrain
	 * qu'il avait, plutot que de se retrouver sans materiau du tout.
	 */
	UMaterialInterface* ChoisirMateriauMerDecor(UMaterialInterface* Repli) const;

	/**
	 * Coupe les nuages et fige l'horloge, sous `-WorldseedCielClair`.
	 *
	 * Sans drapeau, ne fait RIEN : le ciel du jeu n'est pas touche. C'est un
	 * outil de mesure, pas un reglage de rendu.
	 */
	void CielDInspection();

	UPROPERTY()
	TObjectPtr<USceneComponent> RootScene;

	/**
	 * Le sol de fond, sur son propre acteur.
	 *
	 * IL PORTE AUSSI LE UWaterTerrainComponent, et c'est toute la raison de son
	 * existence separee : le plugin Water redemande sa texture d'information des
	 * qu'un composant de l'acteur porteur salit son etat de rendu. Sur ce
	 * terrain-ci, cela voudrait dire a chaque chunk. Voir WorldseedGroundProxy.h.
	 */
	UPROPERTY()
	TObjectPtr<class AWorldseedGroundProxy> GroundProxy;

	/**
	 * L'horizon, sur son PROPRE acteur.
	 *
	 * Il ne peut etre ni un composant du sol de fond -- le plugin Water prend
	 * la boite de TOUS les composants de l'acteur porteur pour borner sa zone,
	 * et une nappe enfoncee y abaisse le plancher -- ni un composant du terrain,
	 * dont `GetTerrainPrimitives` enumere toutes les primitives. Un acteur a
	 * lui seul est le seul endroit ou il ne ment a personne.
	 */
	UPROPERTY(Transient)
	TObjectPtr<class AWorldseedHorizonProxy> HorizonProxy;

	/**
	 * LE MONDE, TENU PAR REFERENCE ET NON PAR VALEUR.
	 *
	 * IL ETAIT COPIE. Cet acteur en gardait son propre jeu de tableaux, et
	 * `AdoptWorld` en recopiait un second dans l'acteur voxel : sur la grille
	 * 4096x2048, cent quatre-vingt-cinq megaoctets en double, pour une donnee
	 * que personne ne modifie jamais apres sa generation.
	 *
	 * Les accesseurs ci-dessous gardent les noms d'avant, a la parenthese
	 * pres : ils rendent une reference sur le monde partage, donc rien n'est
	 * copie a la lecture, et le code appelant n'a pas change de sens.
	 */
	FWorldseedMondePtr Monde;

	/**
	 * Le vide, rendu quand aucun monde n'est charge.
	 *
	 * POURQUOI IL EXISTE PLUTOT QU'UN DEREFERENCEMENT NU. Les accesseurs sont
	 * appeles depuis des chemins qui tournent avant `AcquireWorld` -- le
	 * releve, les gardes de validite. Rendre une reference sur un tableau vide
	 * leur laisse leur test `Num() == CellCount()` habituel ; dereferencer un
	 * pointeur nul les ferait tomber.
	 */
	static const TArray<float>& FloatsVides();

	/** Heightfield en metres, 0 au niveau de la mer. Indexe J * NX + I. */
	const TArray<float>& HeightsM() const
	{
		return Monde.IsValid() ? Monde->ElevationM : FloatsVides();
	}

	/** Climat, meme indexation. Pilote les couleurs de biome. */
	const TArray<float>& TempC() const
	{
		return Monde.IsValid() ? Monde->TempC : FloatsVides();
	}
	const TArray<float>& PrecipMm() const
	{
		return Monde.IsValid() ? Monde->PrecipMm : FloatsVides();
	}

	/**
	 * Ecart annuel froid/chaud, meme indexation.
	 *
	 * Vient du modele climatique, qui seul connait la continentalite : a
	 * latitude egale, un coeur de continent gele l'hiver la ou une cote reste
	 * douce. C'est cette grille qui regle les saisons d'Ultra Dynamic Sky.
	 */
	const TArray<float>& SeasonalAmpC() const
	{
		return Monde.IsValid() ? Monde->SeasonalAmpC : FloatsVides();
	}

	/**
	 * Continentalite, meme indexation : 0 au bord de mer, 1 loin des cotes.
	 *
	 * Elle ouvre l'ecart jour/nuit du prereglage climatique — un interieur de
	 * continent perd la nuit ce qu'il a gagne le jour, une cote non.
	 */
	const TArray<float>& ContinentalityGrid() const
	{
		return Monde.IsValid() ? Monde->Continentality : FloatsVides();
	}

	/**
	 * La geometrie reste PAR VALEUR, et c'est deliberе.
	 *
	 * Elle pese une quarantaine d'octets et elle est lue cent trente et une
	 * fois dans les deux acteurs : la partager n'economiserait rien et
	 * couterait cent trente et une editions. On ne deplace que ce qui pese.
	 */
	FWorldseedGeometry Geometry;

	/** Graine du monde charge. Ensemence aussi le signal meteo. */
	int32 WorldSeed = 0;

	/** Le pilotage du ciel, qui ne sait rien du terrain. */
	UPROPERTY(VisibleAnywhere, Category = "Worldseed|Ciel")
	TObjectPtr<class UWorldseedSkyDriverComponent> SkyDriver;

	/** La pose de l'ocean. */
	UPROPERTY(VisibleAnywhere, Category = "Worldseed|Eau")
	TObjectPtr<class UWorldseedWaterComponent> Water;

	/** Les 19 biomes du monde charge. Partages, comme le reste. */
	const FWorldseedBiomeMap& Biomes() const
	{
		static const FWorldseedBiomeMap Vide;
		return Monde.IsValid() ? Monde->Biomes : Vide;
	}

	/**
	 * Le reseau de grottes du monde charge.
	 *
	 * Il n'est pas transporte par le menu -- il se rebatit depuis la lithologie
	 * et le climat -- et il est transmis tel quel au mailleur voxel, pour que
	 * les deux acteurs decrivent bien le meme sous-sol.
	 */
	FWorldseedCaveNetwork Caves;

	/**
	 * De quelle roche est fait le sous-sol.
	 *
	 * ELLE NE SERT PAS QU'A SEMER LES CHAMBRES : le champ de densite s'en sert
	 * aussi pour decider ou s'ouvrent les DIACLASES, qui sont la cavite de la
	 * roche insoluble. Le karst et la fracture se partagent ainsi le monde
	 * selon la roche, et jamais selon un reglage.
	 */
	FWorldseedLithology Lithology;

	/** L'acteur voxel pose par cet acteur : c'est lui qui tient le relief. */
	UPROPERTY()
	TObjectPtr<AWorldseedVoxelTerrain> VoxelTerrain;
};

