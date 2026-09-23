// Worldseed - la minimap : une fenetre de carte, une rose des vents, un cone.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Subsystems/WorldSubsystem.h"

#include "WorldseedMinimap.generated.h"

class AWorldseedVoxelTerrain;
class SImage;
class STextBlock;
class SWidget;
class UTexture2D;

/**
 * LA MINIMAP, EN HAUT A DROITE.
 *
 * POURQUOI UN SOUS-SYSTEME ET DU SLATE, ET NON UN WIDGET BLUEPRINT. C'est le
 * choix de toute l'interface de ce projet -- le menu, le compteur d'images, le
 * retour au menu -- et il tient a peu de chose : un `.uasset` ne se relit pas
 * dans un diff et ne se teste pas en automation. `Content/Worldseed/` est bien
 * versionne, donc un Blueprint serait POSSIBLE ; il serait simplement opaque.
 *
 * CE QU'ELLE MONTRE. Le relief et les biomes peints depuis les donnees du
 * monde -- jamais une capture de scene, qui ne verrait que les 2400 m de
 * terrain charge -- plus les quatre points cardinaux et le cone de visee.
 *
 * LE NORD EST EN HAUT ET LA CARTE NE TOURNE PAS. C'est le cone qui pivote.
 * Les quatre lettres sont donc FIXES, ce qui evite du texte penche, et l'on
 * garde le sens de l'orientation dans le monde -- utile des que la carte porte
 * des reperes fixes.
 *
 * DEUX TEXTURES ET DEUX CADENCES, et ce n'est pas le cout qui l'impose -- une
 * fenetre de 256 pixels se peint en une milliseconde et demie. C'est la
 * LATENCE : a 15,6 m de maille et 6 km/h, le joueur met neuf secondes a
 * traverser une cellule, alors que sa tete tourne en une fraction de seconde.
 * Un cone en retard de 250 ms *se sent* casse. Et un cone fondu dans le fond
 * cesserait d'etre une fonction pure, donc testable sans monde ni partie.
 *
 * COMMENT L'ALLUMER, L'ETEINDRE ET LA REGLER :
 *
 *     M                          bascule, en jeu
 *     Ctrl + M                   change de cran de portee
 *     Worldseed.Minimap          bascule depuis la console
 *     Worldseed.Minimap 0 / 1    impose l'etat
 *     Worldseed.Minimap.Portee   impose la portee, en metres
 *     -WorldseedMinimap=0        impose l'etat de DEPART
 *     -WorldseedMinimapPortee=   impose la portee de depart
 */
UCLASS()
class WORLDSEED_API UWorldseedMinimap : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// ON TICKE MEME QUAND ELLE EST CACHEE : sans cela la touche de bascule ne
	// serait plus ecoutee, et l'on ne pourrait plus la rallumer.
	virtual bool IsTickable() const override { return bArme; }

	/** Montre ou cache la minimap. Sans effet hors du niveau de jeu. */
	void Montrer(bool bNouvelEtat);
	void Basculer() { Montrer(!bMontre); }
	bool EstMontre() const { return bMontre; }

	/** Vrai dans le niveau de jeu seulement. */
	bool EstArme() const { return bArme; }

	/** Demi-portee courante, en metres. */
	float PorteeM() const { return DemiPorteeM; }

	/** Impose une demi-portee. Repeint au prochain tick. */
	void PoserPortee(float NouvelleM);

	/** Passe au cran suivant : 500 m, 2 km, 6 km, puis on revient. */
	void CranSuivant();

private:
	void Construire();
	void Detruire();

	/** Repeint ce qui doit l.etre, selon ce qui a bouge. */
	void Rafraichir();

	/** Pose l.epingle de la carte, ou la range si le joueur n.en a pas. */
	void PlacerRepere(const struct FWorldseedReperePlayer& Ou);

	AWorldseedVoxelTerrain* Terrain() const;

	/** Cree une texture transitoire carree, ou rend nullptr. */
	UTexture2D* CreerTexture(int32 Cote) const;

	/**
	 * Televerse un tampon BGRA et en PREND LA PROPRIETE.
	 *
	 * Le tampon doit venir d'un `new uint8[]` ; il est libere par le rappel du
	 * moteur, une fois que le fil de rendu en a fini. Ne jamais lui passer la
	 * memoire d'un membre ou d'un `TArray` local.
	 */
	static void Televerser(UTexture2D* Texture, uint8* PixelsPossedes, int32 Cote);

	/** Vrai dans le niveau de jeu seulement. */
	bool bArme = false;
	bool bMontre = true;

	/** Vrai une fois la premiere peinture faite et journalisee. */
	bool bPremierePeinture = false;

	/**
	 * LES TEXTURES SONT TENUES PAR UPROPERTY, ET C'EST TOUT CE QU'IL FAUT.
	 *
	 * Pas d'`AddToRoot` : le depot a la regle ecrite -- « l'appelant conserve
	 * la texture dans une UPROPERTY, ce qui suffit au GC ; l'enraciner fuirait
	 * un apercu a chaque changement de graine ».
	 */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TextureFond;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TextureCone;

	/**
	 * LES BROSSES SONT DES MEMBRES, et ce n'est pas une commodite : `SImage`
	 * prend un `const FSlateBrush*` qu'elle conserve. Une brosse temporaire
	 * laisserait un pointeur pendant.
	 */
	FSlateBrush BrosseFond;
	FSlateBrush BrosseCone;

	TSharedPtr<SWidget> Racine;
	TSharedPtr<SImage> ImageFond;
	TSharedPtr<SImage> ImageCone;

	/**
	 * LE REPERE POSE SUR LA CARTE, RAPPELE ICI -- et c'est le MEME glyphe.
	 *
	 * Il n'est pas peint dans la texture du cone, ou il serait un losange
	 * dessine a la main : le joueur doit reconnaitre SON repere, celui qu'il
	 * vient de poser sur la carte plein ecran. Un widget de texte porte donc
	 * l'epingle, et son ancrage suit la position relative.
	 */
	TSharedPtr<STextBlock> TexteRepere;

	/**
	 * SA POSITION PASSE PAR UN PADDING LIE, et non par un slot conserve.
	 *
	 * Garder un `FSlot*` obligerait a declarer un type IMBRIQUE en avant, ce
	 * que C++ ne permet pas -- et l'en-tete du sous-systeme n'a pas a tirer
	 * tout Slate. Un attribut lie laisse Slate redemander la position quand il
	 * en a besoin, et le calcul reste dans le .cpp.
	 */
	FVector2D MargeRepereEcran = FVector2D::ZeroVector;
	bool bRepereVisible = false;

	/** Le padding que Slate redemande : la position de l'epingle. */
	FMargin MargeRepere() const;

	/** Demi-portee courante, en metres. */
	float DemiPorteeM = 2000.0f;

	/** Horloge de sondage du fond. */
	double Horloge = 0.0;

	/**
	 * LA CELLULE DU DERNIER FOND PEINT, et non la position exacte.
	 *
	 * On ne repeint que lorsqu'elle change. Effet de bord VOULU : la carte
	 * avance par sauts d'un pixel, ce qui supprime le scintillement
	 * sous-pixel ; le joueur est alors jusqu'a un demi-pixel hors du centre
	 * exact, soit moins de huit metres sur la portee par defaut.
	 */
	int32 DerniereCellule = INDEX_NONE;

	/** Cap du dernier cone peint, en degres. */
	float DernierCap = -1000.0f;

	/** Portee du dernier fond peint : la changer force un repeint. */
	float DernierePorteeM = -1.0f;

	/**
	 * LE POINT LE PLUS BAS DU MONDE, RETENU -- il etait rebalaye a chaque
	 * repeinture.
	 *
	 * `FondDuMonde` parcourt tout le relief, soit huit millions et demi de
	 * flottants sur la grille du jeu, pour une valeur qui ne peut pas changer
	 * tant que le monde est le meme. La minimap se repeint des que le joueur
	 * change de cellule -- tous les quinze metres parcourus -- et payait ce
	 * balayage a chaque fois.
	 *
	 * Zero signifie « pas encore calcule » : la vraie valeur est toujours
	 * strictement negative, `FondDuMonde` la bornant a -1.
	 */
	float FondDuMondeM = 0.0f;

	/** Taille du relief au moment du calcul : s'il change, le fond aussi. */
	int32 CellulesDuFond = 0;

	// PAS DE TAMPON MEMBRE, ET C'EST DELIBERE. Le reflexe est d'en garder un
	// pour ne pas reallouer a chaque peinture. Ce serait un bug : le fil de
	// RENDU lit le tampon passe a `UpdateTextureRegions` APRES le retour de la
	// fonction, donc un membre reutilise serait reecrit sous ses yeux. Cela
	// marcherait quatre-vingt-dix-neuf fois sur cent, ce qui est la pire facon
	// d'echouer. Chaque peinture alloue donc le sien, libere dans le rappel --
	// c'est ce que fait deja le globe, soixante fois par seconde et sur quatre
	// fois plus de pixels.
};
