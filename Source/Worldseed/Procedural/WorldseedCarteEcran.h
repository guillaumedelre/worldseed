// Worldseed - la carte plein ecran : sous-systeme, cuisson, vue, repere.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedCarte.h"
#include "Styling/SlateBrush.h"
#include "Subsystems/WorldSubsystem.h"

#include "WorldseedCarteEcran.generated.h"

class AWorldseedVoxelTerrain;
class SWorldseedCarte;
class SWidget;
class UTexture2D;

/**
 * Ce que la carte montre en plus du relief : un lieu, et ce qu'il est.
 *
 * Les positions sont en METRES dans le repere du terrain, jamais en pixels :
 * la carte se deplace et se zoome, les marqueurs non.
 */
struct WORLDSEED_API FWorldseedMarqueur
{
	enum class EGenre : uint8 { Arche, Gouffre, Doline, Table, Canyon, Repere };

	FVector2D PositionM = FVector2D::ZeroVector;
	EGenre Genre = EGenre::Arche;

	/** En metres par pixel d'ecran : au-dela, le marqueur ne se dessine pas. */
	double EchelleMaxM = 1e9;
};

/**
 * LA CARTE PLEIN ECRAN.
 *
 * ELLE N'EST PAS CAPTUREE, ELLE EST PEINTE -- et une seule fois. Le terrain
 * voxel n'existe que dans les 2400 m charges autour du joueur : une camera
 * orthographique filmerait un disque de terrain pose sur un decor deforme. Le
 * monde, lui, est deja en memoire.
 *
 * CUITE A LA RESOLUTION DE LA GRILLE, DONC UN PIXEL PAR CELLULE. Rien n'est
 * agrege, rien n'est vote, aucune ile ne peut se perdre -- et le zoom comme le
 * deplacement deviennent gratuits : ils ne repeignent rien, ils changent la
 * region UV de la brosse. Mesure : **27 ms pour 4096 x 2048**, une fois par
 * partie, ce qui tient largement sur le fil de jeu. La pyramide de reduction
 * est necessaire et l'image l'a prouve : sans elle, le lisere de cote sort
 * pointille des qu'on regarde le monde entier.
 *
 * LE ZOOM S'ARRETE A UN TEXEL PAR PIXEL, et c'est un plafond physique. La
 * donnee s'arrete a la cellule, 15,6 m ; au-dela il n'y a rien de plus a
 * montrer -- le detail au metre n'existe que dans le champ de densite du
 * voxel, qu'on ne peut pas interroger par pixel de carte.
 *
 * COMMANDES :
 *
 *     Tab                        ouvre et ferme, en jeu
 *     Echap                      ferme la carte -- et seulement elle
 *     molette                    zoom, centre sur le curseur
 *     glisser                    deplace
 *     clic                       pose un repere
 *     Ctrl + clic                y teleporte le joueur
 *     Worldseed.Carte            bascule depuis la console
 *     -WorldseedCarteRes=        plafonne la largeur de cuisson
 */
UCLASS()
class WORLDSEED_API UWorldseedCarteEcran : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bArme; }

	void Ouvrir();
	void Fermer();
	void Basculer() { EstOuverte() ? Fermer() : Ouvrir(); }

	bool EstOuverte() const { return bOuverte; }
	bool EstArme() const { return bArme; }

	/**
	 * La trame ou la carte a consomme Echap.
	 *
	 * Lue par `UWorldseedRetourMenu` : l'ordre de tick entre sous-systemes
	 * n'etant pas garanti, il ne suffit pas de demander si la carte est
	 * ouverte. Voir `WorldseedEchap::DoitRamenerAuMenu`.
	 */
	uint64 TrameEchapConsomme() const { return TrameEchap; }
	void NoterEchapConsomme() { TrameEchap = GFrameCounter; }

	// ------------------------------------------------------------- la vue

	/** Centre de la vue, en metres du monde. */
	FVector2D CentreVueM = FVector2D::ZeroVector;

	/** Echelle, en metres par pixel d'ECRAN. */
	double MetresParPixel = 40.0;

	/**
	 * Borne le centre et l'echelle a ce que le monde peut montrer.
	 *
	 * `EchelleDPI` n'est pas un detail : les bornes de zoom se calculent en
	 * pixels REELS. A facteur 2 sur un ecran dense, un plafond pose en pixels
	 * logiques serait calcule a moitie resolution et la carte resterait deux
	 * fois trop floue sans qu'on comprenne pourquoi.
	 */
	void BornerVue(const FVector2D& TailleEcranPx, float EchelleDPI);

	/** La fenetre de vue, sous la forme que la projection partagee attend. */
	WorldseedCarte::FParamsFenetre ParamsVue(const FVector2D& TailleEcranPx) const;

	// --------------------------------------------------------- le contenu

	const FSlateBrush* BrosseCarte() const { return &Brosse; }

	/** Taille de la texture cuite, en texels. Zero tant qu'elle n'existe pas. */
	FIntPoint TailleCuisson() const { return Cuisson; }

	/** Les lieux a dessiner. Batie une fois a la premiere ouverture. */
	const TArray<FWorldseedMarqueur>& Marqueurs() const { return Lieux; }

	/**
	 * Combien de lieux de ce genre, et a partir de quelle echelle ils se voient.
	 *
	 * LA LEGENDE A BESOIN DES DEUX. Un genre absent de la carte a deux causes
	 * qui ne se ressemblent pas -- le monde n'en porte aucun, ou le zoom les
	 * cache -- et les taire toutes les deux laisse croire a la premiere.
	 */
	int32 NombreDeGenre(FWorldseedMarqueur::EGenre Genre) const;
	double EchelleDeGenre(FWorldseedMarqueur::EGenre Genre) const;

	/** Le repere pose par le joueur, s'il y en a un. */
	bool ARepere() const { return bRepere; }
	FVector2D RepereM() const { return Repere; }
	void PoserRepere(const FVector2D& PositionM);
	void EffacerRepere() { bRepere = false; }

	AWorldseedVoxelTerrain* Terrain() const;

	/** Largeur du monde, pour l'enroulement de la projection. */
	double LargeurMondeM() const;

private:
	/** Peint la carte entiere et la televerse. Idempotent. */
	bool Cuire();

	void RestaurerEntree();

	bool bArme = false;
	bool bOuverte = false;

	/** Zero tant que la carte n'a pas pris Echap. */
	uint64 TrameEchap = 0;

	/**
	 * LA TEXTURE EST TENUE PAR UPROPERTY, ET C'EST TOUT CE QU'IL FAUT.
	 * Pas d'`AddToRoot` : le depot a la regle ecrite, et l'enraciner fuirait
	 * trente-deux megaoctets a chaque retour au menu.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Texture;

	/** La brosse est un MEMBRE : Slate conserve le pointeur qu'on lui donne. */
	FSlateBrush Brosse;

	TSharedPtr<SWorldseedCarte> Widget;
	TSharedPtr<SWidget> Racine;

	FIntPoint Cuisson = FIntPoint::ZeroValue;

	TArray<FWorldseedMarqueur> Lieux;

	bool bRepere = false;
	FVector2D Repere = FVector2D::ZeroVector;

	/**
	 * L'ETAT DE LA MINIMAP AVANT L'OUVERTURE, et non `true` en dur : le joueur
	 * l'a peut-etre coupee, et la carte n'a pas a la rallumer pour lui.
	 */
	bool bMinimapAvant = true;

	/** Plafond de largeur de cuisson, en texels. Surcharge par la ligne de commande. */
	int32 PlafondCuisson = 4096;

	/**
	 * LE HARNAIS : ouvrir la carte sans clavier, pour pouvoir la photographier.
	 *
	 * La tournee photo ne peut PAS voir un widget -- `RequestScreenshot` lit la
	 * cible de rendu du viewport, et Slate est composite apres, dans le
	 * back-buffer. On capture donc la fenetre depuis l'exterieur, ce qui
	 * suppose de pouvoir l'ouvrir depuis la ligne de commande.
	 */
	float DelaiAutoS = 0.0f;
	float HorlogeAuto = 0.0f;

	/** Echelle imposee apres l'ouverture automatique, en metres par pixel. */
	float EchelleAutoM = 0.0f;
};
