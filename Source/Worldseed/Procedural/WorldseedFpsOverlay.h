// Worldseed - le compteur d'images a l'ecran, pour le debug.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "WorldseedFpsOverlay.generated.h"

class STextBlock;
class SWidget;

/**
 * LE COMPTEUR D'IMAGES, EN HAUT A GAUCHE.
 *
 * POURQUOI UN SOUS-SYSTEME ET PAS UN WIDGET BLUEPRINT. Le chemin normal
 * d'Unreal serait un UUserWidget pose dans un asset, ou un AHUD designe par le
 * mode de jeu. Les deux exigent de toucher a du CONTENU : `Content/*` est exclu
 * du depot par `.gitignore`, et le depot a deja perdu des reglages pour cette
 * raison exacte -- le PlayerStart qui revenait a sa position, les acteurs
 * d'eclairage disparus entre deux sessions. Un sous-systeme de MONDE vit dans
 * le code, donc il est versionne. C'est le meme choix que pour le banc, la
 * tournee photo et le retour au menu.
 *
 * POURQUOI UN WIDGET SLATE ET PAS AddOnScreenDebugMessage. La pile de messages
 * du moteur est deja occupee par le releve meteo -- quatre lignes, et
 * `bShowReadout` vaut true par defaut. Une ligne de plus dans cette pile se
 * DEPLACERAIT verticalement selon ce que les autres messages ecrivent, et sa
 * police serait celle du moteur. Un widget est ancre au pixel.
 *
 * ET IL NE PEUT PAS RECOUVRIR LE RELEVE METEO. Le moteur dessine sa pile a
 * `MessageX = 40` et `MessageStartY = GIsEditor ? 45 : 100`
 * (`UnrealEngine.cpp:13615-13624`, UE 5.8). La bande au-dessus de Y = 45 est
 * donc libre dans les deux cas : le label s'y loge.
 *
 * CE QU'IL AFFICHE, ET POURQUOI TROIS CHIFFRES ET PAS UN :
 *
 *     134 FPS    7.46 ms    pire 12.10
 *
 * La MILLISECONDE est la grandeur que tout le depot mesure -- le banc, les
 * sondes -- et elle se compare directement au budget d'une trame, 16,67 ms.
 * La PIRE TRAME est ce qui fait l'a-coup RESSENTI, et une moyenne le cache :
 * le globe qui saccadait avait une mediane de 16,66 ms pour un maximum de
 * 302,32. Un compteur qui n'aurait montre que sa moyenne ne l'aurait jamais
 * trahi.
 *
 * COMMENT L'ALLUMER ET L'ETEINDRE :
 *
 *     F1                    bascule, en jeu
 *     Worldseed.Fps         bascule depuis la console
 *     Worldseed.Fps 0 / 1   impose l'etat
 *     -WorldseedFps=0       impose l'etat de DEPART, en ligne de commande
 *
 * La derniere sert a la tournee photo, qui juge le RENDU : un compteur pose
 * sur toutes les vues les abimerait.
 */
UCLASS()
class WORLDSEED_API UWorldseedFpsOverlay : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// ON TICKE MEME QUAND LE LABEL EST CACHE : sans cela, la touche de bascule
	// ne serait plus ecoutee et l'on ne pourrait plus le rallumer.
	virtual bool IsTickable() const override { return bArme; }

	/** Montre ou cache le label. Sans effet hors du niveau de jeu. */
	void Montrer(bool bNouvelEtat);
	void Basculer() { Montrer(!bMontre); }
	bool EstMontre() const { return bMontre; }

	/**
	 * Vrai dans le niveau de jeu seulement.
	 *
	 * La commande console le lit pour DIRE pourquoi elle ne fait rien : le
	 * depot a deja note qu'une commande muette ne se diagnostique pas, sur
	 * Worldseed.Ou et Worldseed.Lieux qui se taisaient sans terrain.
	 */
	bool EstArme() const { return bArme; }

private:
	void Construire();
	void Detruire();
	void Publier(float MoyenneMs);

	/** Vrai dans le niveau de jeu seulement. */
	bool bArme = false;
	bool bMontre = true;

	TSharedPtr<SWidget> Racine;
	TSharedPtr<STextBlock> Texte;

	/** Fenetre d'accumulation en cours : duree cumulee et nombre de trames. */
	double FenetreS = 0.0;
	int32 FenetreTrames = 0;

	/**
	 * LA PIRE TRAME SE MAINTIENT, SINON ON NE LA VOIT PAS.
	 *
	 * Un pic dure une trame ; l'affichage se rafraichit quatre fois par
	 * seconde. Publier le pire de la seule fenetre courante le ferait
	 * disparaitre avant qu'on ait tourne les yeux. On le RETIENT donc tant
	 * qu'il n'a pas ete battu, pendant PireMaintienS, puis on le laisse
	 * retomber sur la trame courante.
	 */
	float PireMs = 0.0f;
	double PireDepuisS = 0.0;
};
