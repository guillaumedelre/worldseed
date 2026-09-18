// Worldseed - ocean et lacs confies au plugin Water d'Unreal.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedHydrology.h"
#include "Procedural/WorldseedRules.h"

class AWaterBodyLake;
class AWaterBodyOcean;
class AWaterBodyRiver;
class AWaterZone;

/** Ce que la pose a produit, pour pouvoir la defaire. */
struct WORLDSEED_API FWorldseedWaterBodies
{
	TWeakObjectPtr<AWaterZone> Zone;
	TWeakObjectPtr<AWaterBodyOcean> Ocean;
	TArray<TWeakObjectPtr<AWaterBodyLake>> Lakes;

	TArray<TWeakObjectPtr<AWaterBodyRiver>> Rivers;

	/**
	 * Segments de cours confies au plugin, par cours.
	 *
	 * LE MAILLAGE PROCEDURAL DOIT EVITER CEUX-LA. Deux nappes d'eau au meme
	 * endroit ne se departagent pas : elles clignotent l'une dans l'autre.
	 */
	TArray<TArray<bool>> TakenRiverSegments;

	bool IsEmpty() const { return !Zone.IsValid() && Lakes.Num() == 0; }
};

/**
 * Le seul endroit du projet qui connaisse le plugin Water.
 *
 * POURQUOI LUI CONFIER L'OCEAN ET LES LACS, ET PAS LES RIVIERES. Chaque systeme
 * fait ce qu'il sait faire :
 *
 *   - Une nappe plate delimitee par un contour est exactement ce qu'un
 *     AWaterBodyLake attend, et l'hydrologie en produit deja un — rivage ferme,
 *     ordonne, sans croisement. On y gagne les vagues, les caustiques, le rendu
 *     sous-marin, la flottabilite et la nage, qu'un maillage nu n'aura jamais.
 *
 *   - Une riviere du plugin est une SPLINE, et une spline se casse sur une
 *     chute. Ce monde compte jusqu'a cent quarante cascades, dont certaines a
 *     soixante degres et soixante-seize metres de denivele : c'est precisement
 *     la geometrie qui avait mis en echec une premiere tentative. Les rivieres
 *     et les cascades restent donc en maillage procedural, ou un ruban oriente
 *     par la bissectrice encaisse n'importe quelle pente.
 *
 * Le plugin est EXPERIMENTAL : toute la surface de contact tient dans ce
 * fichier, et si l'un de ses appels disparait, il n'y a qu'ici a reprendre.
 */
namespace WorldseedWaterBodies
{
	/**
	 * Pose la zone d'eau, l'ocean et un lac par nappe.
	 *
	 * Faux si le plugin ne repond pas : l'appelant garde alors ses maillages.
	 *
	 * SeabedM est l'altitude du point le plus bas du monde, en metres. Elle
	 * donne son EPAISSEUR a l'ocean, et cette epaisseur n'est pas cosmetique :
	 * la zone d'eau agrege les bornes en Z de tous les corps en un intervalle
	 * unique, et la texture d'information y normalise toutes ses hauteurs. Un
	 * ocean d'epaisseur nulle donne un intervalle NUL des qu'il est le seul
	 * corps d'eau — ce qui arrive sur toute carte sans lac. Le shader s'en
	 * protege de la division, mais tous ses seuils tombent alors exactement sur
	 * leur borne, et l'eau cesse de se dessiner.
	 */
	WORLDSEED_API bool Build(UWorld* World, const FWorldseedGeometry& Geometry,
		const FWorldseedHydrology& Hydrology, float HeightExaggeration,
		float SeabedM, FWorldseedWaterBodies& Out);

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
