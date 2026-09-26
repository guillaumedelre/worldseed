// Worldseed - les jeux de textures de sol proposes au joueur.

#pragma once

#include "CoreMinimal.h"
#include "WorldseedTexturePack.generated.h"

/**
 * Les packs de textures disponibles.
 *
 * TROIS PACKS PURS ET UN MELANGE, a dessein. Un pack pur est coherent de style
 * mais laisse des emplacements vides — aucun des trois ne couvre les quatre
 * matieres. Le melange les remplit tous, au risque d'accoler une falaise d'un
 * pack a une herbe d'un autre. Les avoir cote a cote dans le menu est le seul
 * moyen de trancher, parce que c'est une question d'oeil et non de mesure.
 *
 * La neige ne figure dans aucun : Ultra Dynamic Sky la depose lui-meme sur le
 * materiau selon la temperature et les chutes que le climat calcule.
 */
UENUM(BlueprintType)
enum class EWorldseedTexturePack : uint8
{
	/** Aucune texture : les dix-neuf couleurs de biome, a plat. */
	BiomeColour		UMETA(DisplayName = "Couleurs de biome"),

	/** Dreamscape seul. Manque la mousse. */
	Dreamscape		UMETA(DisplayName = "Dreamscape"),

	/** Stylized_Village seul. Manque l'aride. */
	Village			UMETA(DisplayName = "Village"),

	/** Stylized_Egypt seul. Manque l'herbe — monde aride uniquement. */
	Egypt			UMETA(DisplayName = "Egypte"),

	/** Le meilleur de chaque pack. Complet, mais de styles melanges. */
	Mixed			UMETA(DisplayName = "Melange"),

	/**
	 * Orasot : le sol du pack, ET des parois de falaise en GEOMETRIE.
	 *
	 * SEUL HABILLAGE QUI POSE DES MAILLAGES, et c'est une decision du
	 * proprietaire prise le 25 septembre 2026. Les autres packs ne changent
	 * que le materiau du terrain ; celui-ci seme en plus des pans de falaise
	 * sur les faces raides, pour retrouver le rendu de l'ile flottante de
	 * `M_5_Bioms_Showcase`.
	 *
	 * CE QUE CELA NE CHANGE PAS : LE CHAMP DE DENSITE. Le relief, la carte et
	 * les sondes restent identiques d'un habillage a l'autre -- les falaises
	 * sont posees PAR-DESSUS, elles ne creusent rien. L'invariant « meme
	 * graine, meme relief » tient donc, et c'est ce qui rend ce choix sur.
	 * Le seul point qu'il deplace est la COLLISION, si on la leur donne : le
	 * joueur ne poserait plus le pied au meme endroit selon l'habillage.
	 */
	Orasot			UMETA(DisplayName = "Orasot"),

	Count			UMETA(Hidden)
};

namespace WorldseedTexturePack
{
	/** Libelle affiche dans le menu. */
	WORLDSEED_API FText Label(EWorldseedTexturePack Pack);

	/** Ce que le pack couvre et ce qui lui manque, pour l'aide du menu. */
	WORLDSEED_API FText Description(EWorldseedTexturePack Pack);
}
