// Worldseed - les Runtime Virtual Textures du pack, sur tout le monde.

#pragma once

#include "CoreMinimal.h"

class URuntimeVirtualTexture;
class URuntimeVirtualTextureComponent;
class USceneComponent;
class UPrimitiveComponent;
class AActor;

/**
 * DEUX RUNTIME VIRTUAL TEXTURES, SUR TOUT LE MONDE.
 *
 * POURQUOI ELLES EXISTENT. Le pack Orasot accorde sa vegetation a son terrain
 * par une RVT : le sol y ECRIT sa couleur, et le feuillage la RELIT pour
 * prendre son ton. C'est ce qui donne aux rendus du pack leur coherence -- sa
 * fiche Fab le demande d'ailleurs en toutes lettres.
 *
 * ET SANS ELLES, CERTAINS MATERIAUX RENDENT UN BLEU ELECTRIQUE. Ce n'est pas
 * une degradation douce : `M_Grass`, celui des 155 257 brins d'herbe de la
 * carte de demonstration, echantillonne `RVT_Landscape_Material` EN DUR, sans
 * aucun switch pour s'en passer. Aucune instance de materiau ne peut le
 * corriger -- verifie : son seul parametre statique s'appelle `Tweak`. La
 * seule reponse est de lui donner la RVT qu'il attend.
 *
 * ELLE COUVRE LE MONDE ENTIER, ET ELLE N'A PAS BESOIN DE SUIVRE LE JOUEUR.
 *
 * C'etait pourtant le premier plan, par analogie avec la fenetre glissante du
 * plugin Water : une RVT couvre une emprise finie, celle du pack fait 2048 m,
 * et Worldseed en fait 64 000. LE CALCUL DIT LE CONTRAIRE, et c'est le
 * proprietaire qui a pose la question.
 *
 * Une RVT est une texture VIRTUELLE : sa taille en texels est decouplee de sa
 * residence en memoire, et seules les tuiles visibles sont rendues. Le moteur
 * en borne la taille a `VIRTUALTEXTURE_LOG2_MAX_PAGETABLE_SIZE = 12`, soit
 * 4096 tuiles, et `GetClampedTileSize` a 1024 pixels par tuile -- donc jusqu'a
 * 4 194 304 texels de cote. Les deux RVT du pack sont deja a 1024 tuiles de
 * 512 pixels, soit 524 288 texels :
 *
 *     showcase, 2 048 m  ->  4 mm par texel
 *     Worldseed, 64 km   ->  12,2 cm par texel
 *
 * Douze centimetres suffisent largement a teinter du feuillage, dont le plus
 * petit brin fait un metre. Une fenetre glissante aurait coute un recentrage,
 * une invalidation complete a chaque franchissement, et une teinte qui glisse
 * sous les pieds du joueur -- tout cela pour une resolution dont on n'a pas
 * besoin.
 *
 * LE VOLUME EST DONC POSE UNE FOIS, sur les bornes du monde, et ne bouge plus.
 */
struct FWorldseedRvtRegles
{
	/**
	 * Bas et haut du volume, en metres. Doit encadrer TOUT le relief.
	 *
	 * Le monde de reference va de -371 a 1772 m avant le dome de calotte, qui
	 * ajoute jusqu'a 479 m au centre. On prend large des deux cotes : un relief
	 * qui depasse le volume n'ecrit pas dans la RVT, et son feuillage cesse
	 * d'etre teinte sans que rien ne le signale.
	 */
	float BasM = -600.0f;
	float HautM = 2600.0f;
};

namespace WorldseedRvt
{
	/** Les deux RVT du pack, par chemin d'asset. */
	WORLDSEED_API TArray<FSoftObjectPath> AssetsParDefaut();

	/**
	 * Cree les composants de RVT sur un acteur. Rend le nombre pose.
	 *
	 * `OutComposants` recoit les composants crees, `OutTextures` les assets
	 * effectivement charges -- ce sont eux qu'il faut poser sur les primitives
	 * qui doivent ECRIRE dans la RVT.
	 *
	 * `LargeurM` et `HauteurM` sont les dimensions du MONDE : le volume les
	 * couvre exactement, centre sur l'origine comme le terrain.
	 */
	WORLDSEED_API int32 Poser(AActor* Proprietaire, USceneComponent* Racine,
		TArrayView<const FSoftObjectPath> Assets, const FWorldseedRvtRegles& Regles,
		double LargeurM, double HauteurM,
		TArray<URuntimeVirtualTextureComponent*>& OutComposants,
		TArray<URuntimeVirtualTexture*>& OutTextures);

	/**
	 * Fait ECRIRE une primitive dans les RVT.
	 *
	 * LE MODE DE PASSE EST LE PIEGE, ET IL A DEJA FAIT ABANDONNER LA RVT UNE
	 * FOIS. `VirtualTextureRenderPassType` vaut `Exclusive` par defaut : la
	 * primitive ne se dessine alors PLUS dans la passe principale, seulement
	 * dans la RVT. Le terrain proche perd sa geometrie fine et parait APLATI --
	 * on conclut que la RVT est en cause et l'on fait marche arriere. Il faut
	 * `Always` : le terrain se dessine normalement ET alimente la RVT.
	 */
	WORLDSEED_API void FaireEcrire(UPrimitiveComponent* Primitive,
		TArrayView<URuntimeVirtualTexture* const> Textures);
}
