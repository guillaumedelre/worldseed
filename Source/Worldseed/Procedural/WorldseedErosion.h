// Worldseed - l'erosion.
//
// L'ORIGINAL PYTHON A ETE SUPPRIME : ce fichier est la SEULE
// implementation. Il ne faut plus chercher de reference ailleurs, ni supposer
// qu'un autre fichier dit la meme chose autrement.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedJob.h"
#include "Procedural/WorldseedRules.h"

/** Bilan de la passe d'erosion, pour verification. */
struct WORLDSEED_API FWorldseedErosionReport
{
	int32 Iterations = 0;
	int32 FlowUpdates = 0;
	float TotalIncisionM = 0.0f;
	float TotalDepositionM = 0.0f;
	float MaxIncisionM = 0.0f;
};

/**
 * Etape 4a - erosion. Trois processus appliques en alternance :
 *
 * 1. Incision par puissance de courant : dh = -K * A^m * S^n. L'aire drainee A
 *    est PONDEREE PAR LA PLUIE, si bien que les bassins humides se creusent en
 *    vallees profondes et les bassins arides restent en plateaux. C'est le lien
 *    direct entre le climat et la forme du terrain.
 * 2. Erosion thermique : au-dela de l'angle de talus, la matiere glisse.
 * 3. Diffusion de versant : adoucit les interfluves.
 *
 * Les exposants travaillent sur des valeurs NORMALISEES — aire et pente
 * ramenees a un centile de reference — pour qu'un meme jeu de reglages donne le
 * meme resultat visuel a n'importe quelle resolution. Sans cela, changer de
 * resolution changerait le monde.
 */
/**
 * Declaree ICI, au niveau GLOBAL, et pas en elaborated-type-specifier dans
 * la signature : ecrit `const struct FWorldseedErodibilite*` A L INTERIEUR du
 * namespace, le compilateur declare un type NEUF
 * `WorldseedErosion::FWorldseedErodibilite`, distinct du vrai, et l on obtient
 * six erreurs de type incomplet sur une structure parfaitement definie
 * ailleurs. Paye comptant.
 */
struct FWorldseedErodibilite;

namespace WorldseedErosion
{
	/** Rend faux si le calcul a ete interrompu. */
	/**
	 * Erodabilite : facteur multiplicatif du taux d'incision, par cellule.
	 *
	 * C'EST LE COEFFICIENT K DE LA LOI DE PUISSANCE DE COURANT, et c'est le
	 * seul endroit juste pour y mettre la roche : `E = K . A^m . S^n`, ou K
	 * porte la resistance du substrat. Le mettre ailleurs -- dans l'exposant,
	 * ou en post-traitement -- reviendrait a bricoler un resultat au lieu de
	 * decrire une cause.
	 *
	 * Tableau vide : comportement d'avant, a l'identique.
	 */
	/**
	 * Soulevement par passe, en metres et par cellule.
	 *
	 * C'EST LUI QUI FAIT DE L'EROSION UN SCULPTEUR AU LIEU D'UN POLISSEUR. Sans
	 * apport, un relief qui s'erode se contente de se relaxer : il converge
	 * vers la meme forme, un peu plus tot ou un peu plus tard, et tout terme
	 * qu'on ajoute a la loi -- la roche comprise -- ne fait que reechelonner le
	 * temps. Mesure a l'appui : la durete branchee sur un modele sans
	 * soulevement ne deplacait que quatre a sept CENTIMETRES.
	 *
	 * Avec apport, le systeme tend vers un equilibre ou `U = K . A^m . S^n`,
	 * donc vers une pente `S = (U / K.A^m)^(1/n)`. A soulevement egal, une
	 * roche dure tient une pente plus RAIDE : escarpements, corniches et
	 * marges de plateau en decoulent sans qu'on les dessine.
	 *
	 * Tableau vide : comportement d'avant, a l'identique.
	 */
	/**
	 * Erodabilite STRATIFIEE, facultative.
	 *
	 * QUAND ELLE EST FOURNIE, K CESSE D ETRE FIGE. Le tableau `Erodibility`
	 * ne decrit que la roche de SURFACE au depart ; des que la surface
	 * descend, elle traverse des bancs, et c est de cette traversee que
	 * naissent les GRADINS -- une corniche est un banc dur atteint apres un
	 * banc tendre. Un K calcule une fois avant la boucle ne peut pas le
	 * savoir, et c est pourquoi l ancienne version ne produisait aucune
	 * marche.
	 *
	 * Nullptr : comportement d avant, a l identique.
	 */
	WORLDSEED_API bool Run(const UWorldseedRules& Rules, const FWorldseedGeometry& Geometry,
		const TArray<float>& PrecipMm, const TArray<float>& Erodibility,
		const TArray<float>& UpliftM,
		const FWorldseedErodibilite* Strates,
		TArray<float>& InOutElevationM,
		FWorldseedErosionReport& OutReport,
		const FWorldseedProgressScope& Progress = FWorldseedProgressScope());
}
