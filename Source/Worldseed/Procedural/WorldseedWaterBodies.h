// Worldseed - l'ocean confie au plugin Water d'Unreal.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedRules.h"

class AWaterBodyOcean;
class AWaterZone;

/** Ce que la pose a produit, pour pouvoir la defaire. */
struct WORLDSEED_API FWorldseedWaterBodies
{
	TWeakObjectPtr<AWaterZone> Zone;
	TWeakObjectPtr<AWaterBodyOcean> Ocean;

	bool IsEmpty() const { return !Zone.IsValid() && !Ocean.IsValid(); }
};

/**
 * Le seul endroit du projet qui connaisse le plugin Water.
 *
 * POURQUOI LUI CONFIER L'OCEAN. Une nappe plate d'etendue connue est
 * exactement ce qu'un AWaterBodyOcean attend, et on y gagne les vagues, les
 * caustiques, le rendu sous-marin, la flottabilite et la nage, qu'un maillage
 * nu n'aura jamais.
 *
 * IL N'Y A PLUS D'AUTRE CORPS D'EAU. Les lacs, les rivieres et les cascades
 * ont ete retires du generateur le 18 septembre 2026 ; CLAUDE.md porte les
 * mesures qui ont motive ce retrait. L'ocean est donc SEUL dans sa zone, et
 * ce n'est pas anodin : lire le commentaire de ChannelDepth dans Build.
 *
 * Le plugin est EXPERIMENTAL : toute la surface de contact tient dans ce
 * fichier, et si l'un de ses appels disparait, il n'y a qu'ici a reprendre.
 */
namespace WorldseedWaterBodies
{
	/**
	 * Pose la zone d'eau et l'ocean.
	 *
	 * Faux si le plugin ne repond pas : l'appelant garde alors son maillage.
	 *
	 * SeabedM est l'altitude du point le plus bas du monde, en metres. Elle
	 * donne son EPAISSEUR a l'ocean, et cette epaisseur n'est pas cosmetique :
	 * la zone d'eau agrege les bornes en Z de tous les corps en un intervalle
	 * unique, et la texture d'information y normalise toutes ses hauteurs. Un
	 * ocean d'epaisseur nulle donne un intervalle NUL des qu'il est le seul
	 * corps d'eau — ce qui est desormais TOUJOURS le cas. Le shader s'en
	 * protege de la division, mais tous ses seuils tombent alors exactement sur
	 * leur borne, et l'eau cesse de se dessiner.
	 */
	WORLDSEED_API bool Build(UWorld* World, const FWorldseedGeometry& Geometry,
		float HeightExaggeration, float SeabedM, FWorldseedWaterBodies& Out);

	/**
	 * Signale que le SOL a change, et redemande la texture d'information.
	 *
	 * ELLE EST RENDUE A UN INSTANT DONNE, avec le terrain qui existe alors.
	 * Or le terrain arrive par chunks, quelques-uns par passe : la premiere
	 * version de cette texture ne connaissait que les quatre premiers, et le
	 * rivage s'arretait net a leur frontiere — une arete droite au milieu de
	 * l'eau, la ou le relief aurait du dessiner une cote.
	 *
	 * On ne redemande que la TEXTURE, pas le maillage : le quadtree de l'eau ne
	 * depend pas du sol, et le refaire a chaque chunk couterait pour rien.
	 */
	WORLDSEED_API void NotifyGroundChanged(FWorldseedWaterBodies& Bodies);

	/**
	 * Releve l'etat de la chaine de rendu de l'eau, une fois pose.
	 *
	 * ELLE ECHOUE EN SILENCE A SEPT ENDROITS. La zone existe mais ne voit pas
	 * le sol ; le sol existe mais n'intersecte pas la zone ; la texture
	 * d'information attend des shaders qui ne viendront pas. Aucun de ces cas
	 * ne produit d'avertissement : l'eau ne se dessine simplement pas.
	 *
	 * Ce releve nomme chaque maillon. A appeler QUELQUES SECONDES apres la
	 * pose : avant, la zone n'a pas encore rendu sa texture et ses bornes ne
	 * veulent rien dire.
	 */
	WORLDSEED_API void LogHealth(UWorld* World, const FWorldseedWaterBodies& Bodies);

	/** Detruit ce que Build a pose. */
	WORLDSEED_API void Clear(FWorldseedWaterBodies& Bodies);
}
