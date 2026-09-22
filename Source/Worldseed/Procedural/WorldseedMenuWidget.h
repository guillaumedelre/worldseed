// Worldseed - ecran d'entree : choix du seed et de la taille, apercu.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Procedural/WorldseedNoise.h"
#include "Procedural/WorldseedJob.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedGlobe.h"
#include "Procedural/WorldseedGlobeBake.h"
#include "Procedural/WorldseedTexturePack.h"
#include "Procedural/WorldseedRules.h"
#include "WorldseedMenuWidget.generated.h"

class UButton;
class UComboBoxString;
class UProgressBar;
class UVerticalBox;
class UEditableTextBox;
class UImage;
class UTextBlock;

/**
 * Menu construit integralement en C++ dans RebuildWidget(). Aucun Widget
 * Blueprint n'est necessaire : toute la hierarchie et toute la logique vivent
 * ici, ce qui evite d'avoir a cabler un graphe Blueprint par script.
 */
UCLASS()
class WORLDSEED_API UWorldseedMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Niveau ouvert quand le joueur valide. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldseed")
	FName GameLevelName = TEXT("L_Worldseed_Proc");

	/**
	 * Le menu joue tout seul, pour que le parcours COMPLET soit testable.
	 *
	 * UN PARCOURS QU-ON NE PEUT PAS LANCER N-EST PAS TESTABLE, et celui-ci ne
	 * l-etait pas : tous les bancs et toutes les tournees photo ouvrent
	 * L_Worldseed_Proc DIRECTEMENT, ce qui prend le chemin de SECOURS. Le
	 * chemin du menu -- generer, deposer le monde dans la GameInstance,
	 * changer de niveau, le reprendre -- n-avait jamais ete parcouru en
	 * entier, alors que c-est le seul que le joueur emprunte.
	 *
	 * Arme par `-WorldseedMenuAuto`, avec une graine facultative par
	 * `-WorldseedGraine=N`. Inerte sans cela.
	 */
	/**
	 * La lithologie du monde genere, pour qu-elle voyage avec lui.
	 *
	 * ELLE ETAIT PERDUE AU CHANGEMENT DE NIVEAU, et le champ existait pourtant
	 * deja dans FWorldseedWorldData -- le CACHE l-ecrit et le relit. Seul le
	 * passage menu vers niveau le laissait tomber, des deux cotes.
	 */
	TArray<uint8> CachedLithologyId;

	/** Tables et canyons du monde genere, portes jusqu-au jeu. */
	TArray<FWorldseedPlateauSite> CachedTables;
	TArray<FWorldseedPlateauSite> CachedCanyons;

	bool bAutoJouer = false;

	/** Etat courant du formulaire. */
	UPROPERTY(BlueprintReadWrite, Category = "Worldseed")
	FWorldseedTerrainParams Params;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/**
	 * Cadence de la rotation. On passe par un timer et non par NativeTick :
	 * UUserWidget est en TickFrequency::Auto, qui coupe le tick pour un widget
	 * sans tick Blueprint ni animation — un NativeTick en C++ ne suffit pas a
	 * le reactiver.
	 */
	UFUNCTION()
	void HandleGlobeTimer();

	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent) override;

	void ApplyGlobeZoom();

	/**
	 * Lance une generation EN TACHE DE FOND. Toute generation deja en cours est
	 * annulee : c'est ce qui permet de rechanger les parametres sans attendre.
	 */
	void StartGeneration();

	/** Demande l'arret de la generation en cours, s'il y en a une. */
	void CancelGeneration();

	/** Lit l'avancement et recupere le resultat quand il est pret. */
	void PollGeneration();

	/** Met a jour le tableau de mesures de la colonne de droite. */
	void UpdateInfoText();

	/**
	 * Remplit les barres de biomes depuis `CachedBiomes.LandSharePct`.
	 *
	 * Rien n'est recalcule ici : la part de chaque biome sur les terres est
	 * deja produite par `WorldseedBiomes::Classify`, qui la compte sur la
	 * grille pleine. La relire autrement -- par exemple en rebalayant l'image
	 * du globe -- donnerait un SECOND chiffre pour la meme grandeur, et le
	 * depot a deja paye ce qu'une formule recopiee coute.
	 */
	void UpdateBiomeStats();

	/** Affiche ou retire le bloc progression. */
	void SetProgressVisible(bool bVisible);

	/**
	 * Redessine le globe SANS regenerer le monde. C'est ce que fait la
	 * rotation : seul le point de vue change, le heightfield est inchange.
	 */
	void RedrawGlobe();

	/**
	 * La geometrie de la carte PLEINE, corrigee des latitudes de reference.
	 *
	 * RedrawGlobe patche une copie locale avec LatSpanDeg et le mode de
	 * projection lus dans les regles -- sans quoi tropiques et cercles
	 * polaires tombent a cote. Le pointage doit subir EXACTEMENT le meme
	 * patch, sinon le point choisi et le point dessine seraient a des
	 * latitudes differentes. D'ou cette fonction, appelee par les deux.
	 */
	FWorldseedGeometry GeometrieCarte() const;

	/** Les trois champs de latitude que les regles imposent a une geometrie. */
	void AppliquerLatitudes(FWorldseedGeometry& Geometry) const;

	/**
	 * Convertit une position ecran en point de la carte. Faux hors du globe.
	 *
	 * Elle DEFAIT l'echelle de rendu du zoom, appliquee autour du centre de
	 * l'image : sans cela, le repere tomberait juste a zoom 1 et derivait de
	 * plus en plus a mesure qu'on grossit.
	 */
	bool PointerSurLeGlobe(const FVector2D& PositionEcran,
		float& OutLatitudeDeg, float& OutLongitudeDeg, int32& OutCellule) const;

	/** La cellule de la carte sous une latitude et une longitude. */
	int32 CelluleDe(float LatitudeDeg, float LongitudeDeg) const;

	/** Retient le point, ou l'efface si l'index de cellule est negatif. */
	void PoserDepart(float LatitudeDeg, float LongitudeDeg, int32 Cellule);

	/** Remplit la liste deroulante depuis les lieux que la chaine a nommes. */
	void RemplirLieux(const WorldseedPipeline::FResult& Resultat);

	/** Lit le champ de saisie et la combo vers Params. */
	void PullFormIntoParams();

	UFUNCTION()
	void HandlePlayClicked();

	UFUNCTION()
	void HandleRandomSeedClicked();

	/**
	 * Relance la generation avec la graine SAISIE.
	 *
	 * POURQUOI CE BOUTON EXISTE. L'action existait deja -- taper Entree dans le
	 * champ appelle HandleSeedCommitted, qui appelle StartGeneration -- mais
	 * rien ne le disait. Le seul declencheur VISIBLE etait le de, qui impose
	 * une graine tiree au sort : qui voulait la sienne n'avait aucun bouton a
	 * viser. Une action qu'il faut deviner n'existe pas pour celui qui ne la
	 * devine pas.
	 */
	UFUNCTION()
	void HandleGenerateClicked();

	UFUNCTION()
	void HandleSeedCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandlePackChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleLieuChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleCancelClicked();

	UFUNCTION()
	void HandleClearObsoleteClicked();

	UFUNCTION()
	void HandleClearAllClicked();

	/** Relit l inventaire du cache et met a jour la ligne correspondante. */
	void UpdateCacheInfo();

private:
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> SeedBox;

	/** Choix du pack de textures. */
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> PackCombo;

	/** Ce que le pack couvre, affiche sous la liste. */
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PackHint;

	/**
	 * Pack choisi.
	 *
	 * CHANGER DE PACK NE RELANCE PAS LA GENERATION, contrairement a la taille :
	 * le relief, le climat et les biomes sont identiques, seul l'habillage
	 * change. Relancer ferait payer quinze secondes pour un choix de texture.
	 */
	EWorldseedTexturePack SelectedPack = EWorldseedTexturePack::BiomeColour;
	UPROPERTY(Transient) TObjectPtr<UImage> PreviewImage;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> InfoText;
	UPROPERTY(Transient) TObjectPtr<UButton> PlayButton;
	UPROPERTY(Transient) TObjectPtr<UButton> RandomButton;
	UPROPERTY(Transient) TObjectPtr<UButton> GenerateButton;
	UPROPERTY(Transient) TObjectPtr<UButton> CancelButton;
	UPROPERTY(Transient) TObjectPtr<UButton> ClearObsoleteButton;
	UPROPERTY(Transient) TObjectPtr<UButton> ClearAllButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CacheText;

	/** Bloc « depart du joueur » : la note, le tableau, et la liste des lieux. */
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DepartHint;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DepartLatValue;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DepartBiomeValue;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DepartAltValue;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> LieuxCombo;

	/** Valeurs du tableau de mesures, remplies apres chaque generation. */
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatGridValue;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatScaleValue;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatLandValue;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatElevationValue;

	/**
	 * STATISTIQUES DU MONDE : une ligne par biome, part des terres emergees.
	 *
	 * L'INDICE EST LE RANG A L'ECRAN, PAS L'IDENTIFIANT DU BIOME. La ligne 0
	 * porte le biome le plus etendu de CE monde, la ligne 1 le suivant. C'est
	 * ce qui permet de trier sans toucher a l'arbre de widgets : on ecrit un
	 * nom, une couleur et une valeur dans des lignes deja construites, au lieu
	 * de deplacer des enfants dans leur boite. Les lignes en trop sont repliees
	 * au remplissage.
	 *
	 * Quatre tableaux PARALLELES plutot qu'un tableau de structures : ce sont
	 * des `UPROPERTY`, elles retiennent les widgets pour le ramasse-miettes, et
	 * une structure imbriquee demanderait un `USTRUCT` juste pour cela.
	 */
	UPROPERTY(Transient) TArray<TObjectPtr<UVerticalBox>> BiomeRows;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> BiomeNames;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> BiomeValues;
	UPROPERTY(Transient) TArray<TObjectPtr<UProgressBar>> BiomeBars;

	/** Tient la place sous l intitule tant qu aucun monde n existe. */
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BiomesHint;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> ProgressBar;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;

	/**
	 * Bloc progression. Bascule en Collapsed et non Hidden : Hidden garde la
	 * place reservee et laisse un trou, Collapsed la retire de la mise en page.
	 * C est ce qui evite que la barre pousse le reste de l ecran.
	 */
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> ProgressGroup;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> PreviewTexture;

	/**
	 * Le monde cuit en textures, et le materiau qui les projette sur la sphere.
	 *
	 * VOIE PAR DEFAUT. Le monde ne change pas pendant qu'on le regarde : le
	 * relire a chaque image pour refaire le meme calcul etait du gaspillage
	 * pur. Cuit une fois, tourner le globe ne coute plus qu'un parametre
	 * scalaire, et la carte graphique fait le reste.
	 */
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GlobeMaterial;

	FWorldseedGlobeTextures GlobeTextures;

	/**
	 * Prepare la voie graphique. Faux si le materiau est absent : on retombe
	 * alors sur le lance-de-rayon processeur, qui reste correct.
	 */
	bool BakeGlobe();

	/** Heightfield erode, calcule une fois puis transmis au terrain. */
	TArray<float> CachedHeights;

	/**
	 * Heightfield REDUIT, dedie au globe d'apercu.
	 *
	 * POURQUOI NE PAS LIRE DIRECTEMENT CachedHeights. Le globe fait cinq
	 * echantillonnages bilineaires par pixel — vingt lectures dispersees — soit
	 * cinq millions par image, quelle que soit la taille du monde. Tant que le
	 * heightfield tient en cache, elles ne coutent rien ; a 32 Mo elles vont
	 * chercher en memoire centrale et l'image passe de 1,7 a 8,0 ms. La
	 * rotation saccadait pour cette seule raison.
	 *
	 * Ce n'est pas une perte de detail : le globe fait 512 pixels de diametre
	 * et n'en montre qu'un hemisphere, donc il ne peut resoudre qu'environ mille
	 * colonnes. Reduire par MOYENNE ameliore meme le rendu, le point-sampling
	 * d'avant faisant scintiller le relief pendant la rotation.
	 */
	TArray<float> PreviewHeights;

	/**
	 * Les biomes a la resolution de PreviewHeights, pour que le globe les
	 * colore. Reduits au PLUS PROCHE VOISIN : on ne moyenne pas un identifiant.
	 */
	TArray<uint8> PreviewBiomes;

	/** Les couvertures a la meme resolution : c'est elles qui portent la banquise. */
	TArray<uint8> PreviewCover;

	/** Geometrie correspondant a PreviewHeights. */
	FWorldseedGeometry PreviewGeometry;

	/**
	 * Largeur maximale du heightfield d'apercu.
	 *
	 * Deux fois la largeur de la texture : de quoi rester sur-echantillonne
	 * jusqu'au limbe, ou la sphere comprime les meridiens.
	 */
	static constexpr int32 GlobePreviewMaxWidth = 1024;

	/** Construit PreviewHeights depuis CachedHeights. */
	void BuildPreviewField();

	/** Climat du monde en cache : il pilote les couleurs du terrain. */
	TArray<float> CachedTempC;
	TArray<float> CachedPrecipMm;

	/** Amplitude saisonniere, transportee jusqu'au ciel du niveau de jeu. */
	TArray<float> CachedSeasonalAmpC;

	/** Continentalite, pour l'ecart jour/nuit du ciel en jeu. */
	TArray<float> CachedContinentality;

	/** Carte des biomes du monde affiche. */
	FWorldseedBiomeMap CachedBiomes;

	/** Geometrie effective du monde en cache : NX = 2 NY. */
	FWorldseedGeometry WorldGeometry;

	/**
	 * Etat partage avec le thread de calcul. Les deux pointeurs sont captures
	 * par la tache : si le joueur quitte le menu pendant un calcul, le widget
	 * disparait sans que le worker n'ait rien a savoir.
	 */
	FWorldseedJobPtr CurrentJob;
	TSharedPtr<WorldseedPipeline::FResult, ESPMode::ThreadSafe> PendingResult;

	/** Regles du monde, lues au runtime : geometrie et latitudes de reference. */
	UPROPERTY(Transient) TObjectPtr<class UWorldseedRules> Rules;

	/** Rotation courante du globe, en degres. */
	float GlobeLongitudeDeg = 0.0f;

	/** Inclinaison de l'axe vers l'observateur, ajustable a la souris. */
	float GlobeTiltDeg = 18.0f;

	/** Vitesse de la rotation automatique, en degres par seconde. */
	float AutoSpinDegPerSecond = 5.0f;

	/** Vrai pendant un glisser : la rotation automatique est suspendue. */
	bool bDraggingGlobe = false;

	/**
	 * Vrai des que le joueur a touche le globe : la rotation ne revient plus.
	 *
	 * Tant que le globe n'etait qu'a regarder, tourner tout seul etait une
	 * invitation. Depuis qu'on y CHOISIT ou naitre, c'est une cible mobile.
	 */
	bool bGlobePrisEnMain = false;

	/**
	 * Grossissement du globe a la molette.
	 *
	 * IL EST POSE SUR LE WIDGET, PAS DANS LE MATERIAU, et c.est une contrainte
	 * assumee : le globe est pilote par un materiau dont les parametres vivent
	 * dans un .uasset, et en ajouter un exigerait de la chirurgie d.asset par
	 * MCP -- un lien que ce depot sait tomber des que l.editeur est relance.
	 * Une echelle de rendu marche tout de suite, et elle marche AUSSI sur le
	 * chemin de repli CPU, qui n.utilise pas le materiau du tout.
	 */
	float GlobeZoom = 1.0f;

	/** Derniere position souris connue, en pixels ecran. */
	FVector2D LastDragPosition = FVector2D::ZeroVector;

	// ------------------------------------------- choisir ou naitre

	/**
	 * Deplacement CUMULE depuis l'enfoncement, en pixels.
	 *
	 * POURQUOI CUMULE ET NON « depart a arrivee ». Un aller-retour revient au
	 * point de depart : mesurer l'ecart entre les deux extremites declarerait
	 * CLIC un glisser qui a fait tout le tour du globe et l'a ramene, et le
	 * repere sauterait sous le curseur sans qu'on l'ait demande.
	 */
	float CumulGlisse = 0.0f;

	/**
	 * Au-dela, c'est un glisser et non un clic.
	 *
	 * Trois pixels : en dessous, le tremblement de la main sur un bouton de
	 * souris suffirait a annuler un clic volontaire ; au-dela, on commencerait
	 * a poser un repere en voulant tourner le globe.
	 */
	static constexpr float SeuilClicPx = 3.0f;

	/**
	 * Le point choisi, en latitude et longitude, tel que le globe le dessine.
	 *
	 * Il vit EN DEGRES et non en pixels : le globe tourne et se zoome, donc un
	 * pixel retenu serait faux des la trame suivante.
	 */
	WorldseedGlobe::FRepereGlobe Repere;

	/** Le meme point en METRES sur la carte : ce qui voyage jusqu'au jeu. */
	FVector2D DepartXYM = FVector2D::ZeroVector;

	/** Faux tant que le joueur n'a rien choisi : le terrain decide alors seul. */
	bool bDepartChoisi = false;

	/**
	 * Les lieux que la chaine a nommes, dans l'ordre de la liste deroulante.
	 *
	 * ILS SONT RETENUS ICI PARCE QUE LE MENU LES JETTE. `PendingResult` est
	 * relache des la fin de la generation et n'en conserve que relief, climat,
	 * biomes et roches ; arches, gouffres, dolines, bouches et tables partent
	 * avec lui. Le terrain les REBATIT a l'identique cote jeu -- ils sont
	 * deterministes -- mais le menu, lui, ne les aurait plus pour les offrir.
	 *
	 * La premiere entree est le choix VIDE, donc ce tableau commence par une
	 * position sans objet : les index de la liste et ceux-ci se correspondent
	 * un pour un, ce qui evite un decalage a chaque lecture.
	 */
	TArray<FVector2D> LieuxXYM;

	/** Periode de redessin du globe, en secondes. */
	// SOIXANTE HERTZ depuis que le globe est dessine par la carte graphique :
	// une image ne coute plus qu'ecrire deux scalaires dans un materiau. Les
	// trente hertz d'avant etaient un compromis impose par le lance-de-rayon
	// processeur, qui prenait plusieurs millisecondes par image.
	//
	// La vitesse de rotation ne bouge pas : elle se calcule EN DEGRES PAR
	// SECONDE, multipliee par cette periode.
	static constexpr float GlobeRedrawPeriod = 1.0f / 60.0f;

	FTimerHandle GlobeTimerHandle;

	/** Horloge du dernier declenchement, pour un pas de rotation en temps reel. */
	double DernierTicGlobe = 0.0;

	/**
	 * Releve des intervalles REELS du timer, en millisecondes, une fois par
	 * ouverture de l ecran. Trois secondes suffisent a voir si le timer bat
	 * regulierement ; au-dela on mesurerait la meme chose plus longtemps.
	 */
	static constexpr int32 NbReleveGlobe = 180;
	TArray<double> IntervallesGlobe;

	/** Cout cumule du redessin du globe, pour departager les deux voies. */
	double CumulRedrawMs = 0.0;
	int32 NbRedraw = 0;

	/** Mesures du dernier monde genere, affichees sous le globe. */
	float LastLandRatio = 0.0f;
	float LastMinElevationM = 0.0f;
	float LastMaxElevationM = 0.0f;
};
