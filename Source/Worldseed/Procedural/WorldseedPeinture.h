#pragma once

#include "CoreMinimal.h"

#include "Procedural/WorldseedLithology.h"
#include "Procedural/WorldseedStrata.h"

struct FWorldseedBiomeMap;
struct FWorldseedGeometry;
struct FWorldseedVoxelMesh;
class FWorldseedDensity;

/**
 * QUI A DECIDE LA COULEUR DE CE SOMMET.
 *
 * ELLE N'EST PAS DECORATIVE : c'est elle qui a nomme la cause du noir des
 * parois, le 22 septembre 2026. Devant des bandes sombres qu'on prenait pour
 * de la matiere, la question « la peinture ECRIT-elle seulement du noir ? »
 * n'avait jamais ete posee -- on cherchait la cause d'une couleur sans avoir
 * verifie qu'elle venait de nous.
 *
 * LA REPONSE EST NON, et la carte le montre d'un coup d'oeil : chaque sommet
 * peint par sa BRANCHE, en aplats francs et TOUS CLAIRS, donc tout pixel noir
 * y est par construction quelque chose qu'on ne peint pas. Mesure : luminance
 * ECRITE de 66 a 234 sur 255, 0,05 % sous le seuil -- et 18,5 % de noir a
 * l'ecran une fois eclaire. Le noir est l'ombre des gradins, pas une matiere.
 */
namespace WorldseedPeinture
{
	enum class ECause : uint8
	{
		Repli   = 0,   // pas de carte de biomes : le gris de secours
		Biome   = 1,   // au-dessus du sol, ou la roche ne mord pas encore
		Roche2D = 2,   // sous terre, hors de la fenetre de la serie
		Banc    = 3,   // sous terre, dans la serie stratigraphique
	};

	inline constexpr int32 NbCauses = 4;

	WORLDSEED_API const TCHAR* NomDeCause(ECause C);

	/**
	 * L'aplat qui designe une branche, sur la carte des causes.
	 *
	 * TOUS CLAIRS A DESSEIN : c'est ce qui rend la carte concluante. Un aplat
	 * sombre y laisserait un doute, alors qu'avec cette palette tout pixel
	 * noir est forcement autre chose que de la peinture.
	 */
	WORLDSEED_API FLinearColor Aplat(ECause C, int32 Banc, int32 NbBancs);
}

/**
 * Ce que la peinture d'un maillage voxel demande, et rien de l'acteur.
 *
 * LES REFERENCES SONT DELIBEREES : ce contexte se bati sur la PILE a chaque
 * appel, donc rien ici ne peut survivre a son porteur. La peinture tourne sur
 * le fil de jeu, au televersement ; la regle du depot -- « le fil de maillage
 * ne doit rien tenir qui puisse mourir avant lui » -- ne s'y applique pas,
 * mais la forme la rappelle.
 */
struct WORLDSEED_API FWorldseedPeintureContexte
{
	const FWorldseedBiomeMap& Biomes;
	const FWorldseedGeometry& Geo;
	const FWorldseedDensity& Champ;
	const FWorldseedLithology& Litho;
	const TArray<FLinearColor>& CouleurParRoche;
	const TArray<float>& DureteParId;
	const FWorldseedStratRules& Strates;

	/** Sur quelle profondeur la roche remplace le biome, en metres. */
	float FonduRocheM = 12.0f;

	/**
	 * De combien le champ deplace la surface, en metres.
	 *
	 * LE COTELE DU MONDE VENAIT D'ICI. La profondeur se mesure contre la
	 * surface MACRO, alors que le champ deplace la vraie surface de plus ou
	 * moins dix metres. Sur chaque BOSSE la profondeur est negative et l'on
	 * peint le biome, dans chaque CREUX elle est positive et l'on peint la
	 * roche : sans cette marge, la couleur suit le micro-relief et dessine des
	 * rubans qui epousent les courbes de niveau sur tout le monde.
	 */
	float MargeDeplacementM = 12.0f;

	int32 Seed = 0;

	/** Peindre par BRANCHE au lieu de par matiere. A regarder sans eclairage. */
	bool bCarteDesCauses = false;
};

/**
 * Ce que la peinture a compte en passant.
 *
 * ON COMPTE CE QU'ON PEINT, et ce n'est pas du confort : une couleur qui ne se
 * voit pas a deux causes OPPOSEES -- le terme ne s'evalue jamais, ou il
 * s'evalue et rien ne l'affiche -- et elles n'appellent pas du tout le meme
 * remede. Le compte les separe avant meme qu'on regarde l'image.
 */
struct WORLDSEED_API FWorldseedPeintureReleve
{
	int64 Sommets = 0;
	int64 SousLaSurface = 0;
	int64 SerieActive = 0;
	int64 Teintee = 0;

	double ProfondeurSomme = 0.0;
	double ProfondeurMax = -1e30;
	double ProfondeurMin = 1e30;

	/** Combien de sommets chaque banc de la serie a peints. */
	TArray<int32> ParBanc;

	/** La luminance ECRITE, celle qui a servi a innocenter la palette. */
	double LumMin = 1e30;
	double LumMax = -1e30;
	int64 SombresEcrits = 0;

	int64 ParCause[WorldseedPeinture::NbCauses] = {};
	int64 SombresParCause[WorldseedPeinture::NbCauses] = {};
};

namespace WorldseedPeinture
{
	/**
	 * Peint les sommets d'un maillage voxel, et rend ce qu'elle a compte.
	 *
	 * LA BRANCHE COMPTEE EST CELLE QUI A ECRIT, pas celle qu'on a traversee.
	 * Le fondu peut ramener au biome un sommet dont on venait de lire le banc :
	 * compter la branche PARCOURUE donnerait un entonnoir juste et une carte
	 * des causes fausse.
	 */
	WORLDSEED_API void Sommets(FWorldseedVoxelMesh& Mesh,
		const FWorldseedPeintureContexte& C, FWorldseedPeintureReleve& Releve);
}
