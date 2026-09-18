// Worldseed - decoupe des cours d'eau en troncons qu'une spline peut porter.

#pragma once

#include "CoreMinimal.h"

struct FWorldseedGeometry;
struct FWorldseedRiver;

/**
 * Un troncon de cours d'eau assez doux pour tenir sur une spline.
 *
 * Les bornes sont des INDICES DANS LE TRACE du cours, toutes deux incluses.
 * Rien n'est recopie : le troncon designe, il ne possede pas.
 */
struct WORLDSEED_API FWorldseedReach
{
	int32 RiverIndex = 0;
	int32 First = 0;
	int32 Last = 0;

	int32 Num() const { return Last - First + 1; }
};

/**
 * POURQUOI DECOUPER PLUTOT QUE POSER UNE SPLINE PAR COURS.
 *
 * Une AWaterBodyRiver est une spline, et une spline INTERPOLE. Sur un ressaut
 * de soixante-seize metres a soixante degres — et ce monde en compte jusqu'a
 * cent quarante — elle ne fait pas une chute : elle fait une courbe, qui
 * deborde du lit en haut, s'en ecarte en bas, et dessine une nappe d'eau
 * suspendue dans le vide. C'est precisement ce qui avait mis en echec une
 * premiere tentative de tout confier au plugin.
 *
 * Le cours est donc coupe a ses ressauts. Les biefs calmes, qui sont
 * l'essentiel de sa longueur, passent au plugin et gagnent ses vagues, ses
 * caustiques, son rendu sous-marin et sa flottabilite. Les ressauts restent en
 * maillage procedural, ou un ruban oriente par la bissectrice encaisse
 * n'importe quelle pente.
 *
 * LE CRITERE EST LA PENTE, ET NON LA LISTE DES CASCADES. Une cascade est une
 * classification hydrologique ; ce qui met une spline en echec est la
 * geometrie. Prendre la pente couvre les deux, et ne peut pas desynchroniser.
 */
namespace WorldseedRiverReaches
{
	/**
	 * Decoupe chaque cours en biefs calmes.
	 *
	 * MaxSlopeDeg est la pente au-dela de laquelle un segment devient un
	 * ressaut. MinPoints ecarte les biefs trop courts pour valoir un acteur :
	 * le plugin en exige deux au minimum, et un bief de deux points entre deux
	 * chutes ne serait qu'une facette.
	 */
	WORLDSEED_API void Split(const TArray<FWorldseedRiver>& Rivers,
		const FWorldseedGeometry& Geometry, float MaxSlopeDeg, int32 MinPoints,
		TArray<FWorldseedReach>& OutReaches);

	/**
	 * Marque, cours par cours, les segments confies au plugin.
	 *
	 * Le maillage procedural s'en sert pour ne dessiner QUE le reste : sans
	 * cela les deux representations se superposeraient, et deux nappes d'eau
	 * au meme endroit se battent en z-fighting.
	 *
	 * OutTaken[R] a un booleen par SEGMENT du cours R, soit un de moins que
	 * son nombre de points.
	 */
	WORLDSEED_API void MarkTakenSegments(const TArray<FWorldseedRiver>& Rivers,
		const TArray<FWorldseedReach>& Reaches, TArray<TArray<bool>>& OutTaken);
}
