// Worldseed - le niveau de qualite du rendu, et l'endroit ou un menu se branchera.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "WorldseedQualiteRendu.generated.h"

/**
 * Applique le niveau de qualite du rendu, une fois, au demarrage.
 *
 * POURQUOI UNE CLASSE A ELLE SEULE. Poser deux lignes dans l'instance de jeu
 * aurait marche et aurait melange une preoccupation de RENDU a un objet qui
 * transporte un MONDE. Ici l'objet ne fait qu'une chose, et c'est la couture
 * exacte ou viendra se brancher un menu d'affichage -- Ultra, Haut, Moyen,
 * Bas -- le jour ou on le posera : il appellera `Appliquer`, et rien d'autre
 * n'aura a changer.
 *
 * POURQUOI PAS UN CVAR DANS UN .INI. Les groupes `sg.*` sont precisement ce
 * que le systeme de qualite d'Unreal REECRIT -- au demarrage, a la detection
 * materielle, et a chaque application des reglages joueur. Un cvar pose dans
 * `[ConsoleVariables]` serait donc ecrase sans prevenir, et l'on chercherait
 * longtemps pourquoi le reglage « ne prend pas ». On passe par l'API de
 * qualite elle-meme (`Scalability::SetQualityLevels`), qui est ce que le
 * systeme lit.
 *
 * --- CE QUE CE REGLAGE VAUT, MESURE LE 22 SEPTEMBRE 2026 ------------------
 *
 * Ventilation de la trame GPU par `ProfileGPU`, monde 4096x2048, vue 1200 m :
 * Lumen (illumination globale + reflexions, avec sa scene et le lancer de
 * rayons materiel) pese 29 % du GPU, la ou le BasePass -- le dessin de la
 * geometrie -- en pese 5.
 *
 * Echelle mesuree au banc, GI et reflexions ensemble :
 *
 *   niveau           trame     images/s   GPU
 *   3 Epique         4,34 ms      230     3,47
 *   2 Haut           3,70         271     2,53
 *   1 Moyen          3,34         299     2,09     <- retenu
 *   0 Bas            2,22         450     1,65     (plus de Lumen du tout)
 *
 * LE CRAN 1 GARDE LUMEN et rend la moitie du gain disponible. Le cran 0 le
 * COUPE : mesure a l'image, le sol perd son occlusion ambiante et se delave de
 * vingt-deux pour cent (+36,7 de luminance sur 255), et l'eau perd ses
 * reflexions. Le rebond de canopee que le registre documente comme un choix
 * assume disparaitrait avec.
 *
 * ET IL N'Y AVAIT PAS DE PROBLEME A RESOUDRE : 4,34 ms pour un budget de
 * 16,67. Ce reglage prend de l'avance sur la vegetation, qui n'est pas encore
 * portee et qui, elle, consommera vraiment le budget.
 */
UCLASS()
class WORLDSEED_API UWorldseedQualiteRendu : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Niveaux d'Unreal, dans son ordre. */
	enum ENiveau : int32
	{
		Bas = 0,
		Moyen = 1,
		Haut = 2,
		Epique = 3,
		Cinematique = 4,
	};

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/**
	 * Pose le niveau de l'illumination globale et des reflexions.
	 *
	 * ON NE TOUCHE QUE CES DEUX GROUPES, et la mesure le justifie : tous les
	 * autres reunis -- ombres, post-traitement, anticrenelage, distance de vue,
	 * textures, effets -- pesent ensemble 0,13 ms. Les baisser abimerait le
	 * rendu pour rien.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Rendu")
	void Appliquer(int32 Niveau);

private:
	/**
	 * INDEX_NONE : ON NE TOUCHE A RIEN, et c'est le defaut voulu.
	 *
	 * ARBITRAGE DU PROPRIETAIRE, 22 septembre 2026 : on ne change pas le rendu
	 * aujourd'hui. La trame vaut 4,34 ms pour un budget de 16,67 -- il n'y a
	 * rien a reparer -- et la scene qui justifie Lumen, une foret ou une
	 * grotte, n'existe pas encore. Trancher maintenant reviendrait a sacrifier
	 * une fonction sur le cas ou elle sert le moins.
	 *
	 * CE SOUS-SYSTEME RESTE PARCE QU'IL PORTE DEUX CHOSES : la mesure
	 * ci-dessus, qui serait autrement a refaire, et la COUTURE ou un menu
	 * d'affichage viendra se brancher -- il appellera `Appliquer`, et rien
	 * d'autre n'aura a changer. C'est la meme discipline que `rugositeMin` ou
	 * `NiveauMax` : un levier arrive ETEINT, une donnee absente reste sans
	 * effet, et l'on peut le mesurer sans recompiler.
	 *
	 * `-WorldseedQualite=<0..4>` l'arme pour un A/B -- parce qu'un A/B ne se
	 * fait pas en editant un fichier, regle posee apres qu'une boucle de mesure
	 * eut vide `world_rules.json` en etant interrompue entre la modification et
	 * la restauration.
	 */
	int32 NiveauParDefaut = INDEX_NONE;
};
