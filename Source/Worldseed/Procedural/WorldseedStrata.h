// Worldseed - la colonne stratigraphique : la roche varie aussi en Z.

#pragma once

#include "CoreMinimal.h"

#include "Procedural/WorldseedLithology.h"

struct FWorldseedGeometry;

class UWorldseedRules;

/** Un banc de la serie, avec sa durete recopiee du catalogue. */
struct WORLDSEED_API FWorldseedStratBanc
{
	uint8 RockId = 0;
	float ThicknessM = 60.0f;

	/**
	 * Durete, recopiee du catalogue a la lecture des regles.
	 *
	 * RECOPIEE A DESSEIN, et c'est le meme motif que `DureteParId` dans le
	 * champ de densite : ce test s'evalue des millions de fois par chunk, et
	 * retraverser le catalogue a chaque sommet couterait une indirection pour
	 * rien. La source de verite reste le catalogue ; ceci en est un cache,
	 * rebati a chaque lecture des regles.
	 */
	float Hardness = 0.5f;
};

/**
 * Section "strates" de world_rules.json.
 *
 * POURQUOI UNE FONCTION, ET PAS UNE GRILLE. Une grille 3D de roches est
 * impossible ici, et le projet a deja fait ce calcul pour le champ de densite :
 * a un metre de voxel ce monde ferait SOIXANTE-SEIZE MILLIARDS de voxels, donc
 * 76 Go rien que pour un identifiant par voxel. La solution est la meme que
 * pour le relief -- une FONCTION batie sur la grille 2D existante. La carte 2D
 * dit QUELLE roche fait le socle ; la serie dit ce qu'il y a par-dessus, et se
 * lit a n'importe quel (x, y, z) sans rien stocker.
 *
 * POURQUOI DES BANCS HORIZONTAUX. C'est le cas du plateau du Colorado, et c'est
 * PRECISEMENT ce qui fait la forme demandee : les rayures du Grand Canyon sont
 * horizontales parce que les bancs le sont, et toutes les mesas d'une region
 * partagent leur sommet parce qu'elles s'arretent TOUTES sur le meme banc dur.
 * La platitude des bancs n'est donc pas une simplification, c'est la cause de
 * la forme. Un leger gauchissement evite seulement que le monde entier porte
 * ses corniches a la meme altitude sur 64 km.
 *
 * L'ALTERNANCE DUR / TENDRE EST TOUT LE MECANISME, et le proprietaire l'a
 * decrit : « le vent et l'eau s'attaquent d'abord aux roches tendres, ce qui
 * finit par SAPER la base de la falaise superieure ; privee de soutien, la
 * roche dure s'effondre verticalement par blocs, maintenant ainsi des parois
 * tres abruptes ». Les bancs durs font les corniches, les tendres font les
 * talus, et l'escalier du Grand Canyon en sort tout seul.
 *
 * LA SERIE NE RECOUVRE QUE LE SEDIMENTAIRE. Sur un orogene, le socle affleure
 * -- soulevement et decapage ont emporte la couverture, ce que la regle
 * d'attribution 2D traduit deja. La fenetre de durete garantit donc qu'en
 * dehors des bassins, `RocheAt` rend exactement la roche 2D a toute profondeur,
 * c'est-a-dire le comportement d'avant, a l'identique.
 */
struct WORLDSEED_API FWorldseedStratRules
{
	/** La serie, du HAUT vers le BAS. Vide : aucun banc, comportement d'avant. */
	TArray<FWorldseedStratBanc> Serie;

	/**
	 * Altitude du sommet de la pile avant erosion, en metres.
	 *
	 * C'EST LE TOIT DE LA COUVERTURE, pas le relief. Le relief actuel passe au
	 * travers : la ou il est plus haut que le datum, on est au-dessus de la
	 * serie et c'est le banc sommital qui affleure ; plus bas, on lit dans la
	 * pile.
	 */
	float DatumM = 520.0f;

	/** Amplitude du gauchissement du datum, en metres. */
	float WarpAmplitudeM = 140.0f;

	/** Frequence du gauchissement, en cycles par metre. */
	float WarpFrequency = 0.00006f;

	/**
	 * Fenetre de durete du SOCLE sur laquelle la serie se depose.
	 *
	 * Hors de cette fenetre -- granite, basalte -- il n'y a pas de couverture :
	 * `RocheAt` rend la roche 2D, donc rien ne change.
	 */
	float SocleHardnessMin = 0.25f;
	float SocleHardnessMax = 0.70f;

	/**
	 * Amplification du CONTRASTE d'erodabilite entre bancs.
	 *
	 * SANS ELLE, LES BANCS NE FONT AUCUN GRADIN, et c'est mesure. La formule
	 * d'erodabilite est ADDITIVE et ancree sur la durete moyenne du MONDE --
	 * 0,73, dominee par le granite et le basalte qui couvrent le plus de
	 * surface. Or toute la serie sedimentaire est sous cette moyenne : les K
	 * des six bancs se tassaient entre 1,08 et 1,48, soit un rapport de 1,37.
	 * Une corniche qui resiste 1,37 fois mieux que son talus ne se voit pas.
	 *
	 * L'AMPLIFICATION SE FAIT AUTOUR DE LA MOYENNE DE LA SERIE, jamais autour
	 * de celle du monde, et cette precision porte tout : elle ecarte les bancs
	 * les uns des autres SANS deplacer leur moyenne, donc sans changer la
	 * quantite totale d'erosion sur la couverture. On redistribue, on n'ajoute
	 * pas. Le calage du granite et du basalte n'est pas touche non plus,
	 * puisqu'ils ne sont pas dans la serie.
	 */
	float ErosionContrast = 3.0f;

	/** Epaisseur totale de la serie, calculee a la lecture. */
	float TotalThicknessM = 0.0f;

	bool IsActive() const { return Serie.Num() > 0 && TotalThicknessM > 0.0f; }

	static FWorldseedStratRules FromRules(const UWorldseedRules& Rules,
		const FWorldseedLithologyRules& Litho);
};

/**
 * Erodabilite qui SUIT LA SURFACE a travers les bancs.
 *
 * POURQUOI CE N'EST PAS UN SIMPLE TABLEAU. L'erodabilite etait calculee UNE
 * FOIS, avant la boucle, depuis la roche 2D : chaque cellule gardait donc le
 * meme K du debut a la fin, quelle que soit la profondeur a laquelle la surface
 * etait descendue. C'est exactement ce qui empechait les GRADINS d'exister --
 * une corniche nait de ce que la surface traverse un banc dur APRES avoir
 * traverse un tendre, et un K fige ne peut pas le savoir.
 *
 * LE DATUM EST PRECALCULE, ET C'EST CE QUI REND LA CHOSE ABORDABLE. Le toit de
 * la serie est une surface GEOLOGIQUE : il ne bouge pas pendant l'erosion. On
 * paie donc son bruit une seule fois -- 8 Mo sur cette grille -- et chaque
 * reechantillonnage ne coute plus qu'une descente dans une pile de six
 * elements, soit quelques comparaisons par cellule.
 *
 * L'ANCRAGE RESTE LA MOYENNE DU MONDE, FIGEE, et ce point est portant. Si l'on
 * recalculait la moyenne a chaque passe depuis la surface courante, la quantite
 * TOTALE d'erosion deriverait a mesure que la surface descend dans des bancs
 * plus tendres -- et tout le calage terrestre bougerait pour une raison
 * etrangere a la physique qu'on modelise. On redistribue l'erosion, on n'en
 * change pas le volume.
 */
struct WORLDSEED_API FWorldseedErodibilite
{
	/** K du socle par cellule : exactement le comportement d'avant. */
	TArray<float> SocleK;

	/** Toit de la serie par cellule. FIGE : l'erosion ne le deplace pas. */
	TArray<float> DatumM;

	/** 1 si la serie recouvre cette cellule. */
	TArray<uint8> Couvert;

	/** K de chaque banc, du haut vers le bas. */
	TArray<float> BancK;

	/** Profondeur du BAS de chaque banc sous le datum, cumulee. */
	TArray<float> BasCumulM;

	bool IsActive() const { return BancK.Num() > 0 && DatumM.Num() > 0; }

	/** Indice du banc a cette profondeur sous le datum, ou INDEX_NONE. */
	int32 BancAProfondeur(float ProfondeurM) const;

	/** Remplit OutK a l'altitude courante de chaque cellule. */
	void Echantillonner(const TArray<float>& DemM, TArray<float>& OutK) const;
};

namespace WorldseedStrata
{
	/**
	 * Prepare l'erodabilite stratifiee.
	 *
	 * Poids a zero, ou serie vide : la structure reste inactive et l'erosion
	 * garde son tableau fige -- le comportement d'avant, a l'identique.
	 */
	WORLDSEED_API void PreparerErodibilite(const FWorldseedGeometry& Geometry,
		const FWorldseedLithology& Lithology, const FWorldseedLithologyRules& Litho,
		const FWorldseedStratRules& Strat, float Weight, int32 Seed,
		FWorldseedErodibilite& Out);

	/** Altitude du toit de la serie en ce point, gauchissement compris. */
	WORLDSEED_API double DatumAt(double X, double Y,
		const FWorldseedStratRules& Rules, int32 Seed);

	/**
	 * Indice du banc present a cette altitude, ou INDEX_NONE sous la serie.
	 *
	 * Au-dessus du datum, rend le banc sommital : ce qui affleure sur un relief
	 * plus haut que la couverture est bien la roche du sommet de la pile.
	 */
	WORLDSEED_API int32 BancAt(double X, double Y, double ZM,
		const FWorldseedStratRules& Rules, int32 Seed);

	/**
	 * Toit du premier banc DUR situe a ZM ou en dessous.
	 *
	 * C'EST LE CHAPITEAU, et c'est ce qui fabrique une mesa. Une surface qui
	 * s'abaisse s'arrete sur le premier banc resistant qu'elle rencontre ; deux
	 * mesas voisines s'arretent donc sur LE MEME banc, a la MEME altitude. La
	 * propriete qui definit la forme sort ainsi de la geologie, et non plus
	 * d'un maximum glissant.
	 *
	 * Faux s'il n'y a aucun banc dur sous ce point.
	 */
	WORLDSEED_API bool ToitDuChapiteau(double X, double Y, double ZM,
		const FWorldseedStratRules& Rules, float HardnessMin, int32 Seed,
		double& OutToitM);
}
