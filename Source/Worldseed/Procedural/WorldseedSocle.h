#pragma once

#include "CoreMinimal.h"

struct FWorldseedGeometry;

/**
 * OU LE SOCLE AFFLEURE, ET OU LA COUVERTURE TIENT ENCORE.
 *
 * LA REGLE D'ATTRIBUTION EST GEOLOGIQUE : « un orogene expose son socle --
 * soulevement et DECAPAGE emportent la couverture sedimentaire ». Le soulevement
 * seul ne suffit pas : ce qui decape est le RELIEF. En ne testant que la
 * convergence, on declarait socle toute la bande convergente -- y compris le
 * BASSIN PLAT qui borde la chaine.
 *
 * OR UN BASSIN D'AVANT-PAYS EST L'INVERSE D'UN SOCLE : il est PLEIN des
 * sediments arraches a la chaine voisine, et c'est litteralement le decor des
 * mesas reelles. Mesure du defaut, avant correction : sur le terrain chaud,
 * aride et peu accidente que les tables demandent, 70,80 % de granite et
 * 29,13 % de basalte pour 0,00 % de gres -- donc aucune table dans tout le
 * monde.
 *
 * POURQUOI CE MODULE EXISTE. Le test du socle etait ecrit EN TROIS COPIES dans
 * la lithologie, et le fichier le disait lui-meme : « ajouter le relief a
 * l'une aurait fait diverger les trois, sans qu'aucun compilateur ne le dise ».
 * Le sortir ici lui donne une seule implementation, un releve, et surtout un
 * ORACLE -- le bassin d'avant-pays est un defaut qu'aucune sonde ne voyait, et
 * qu'il a fallu trouver en cherchant pourquoi le monde n'avait pas une table.
 *
 * IL NE CONNAIT NI LES ROCHES NI LE CATALOGUE, a dessein : il rend un masque.
 * Quelle roche porte un socle est une question de lithologie, pas de relief.
 */
struct WORLDSEED_API FWorldseedSocleRegles
{
	/** Au-dela de cette convergence, la chaine est en formation. */
	float Convergence = 0.35f;

	/**
	 * Altitude au-dela de laquelle un relief ancien est repute decape.
	 *
	 * REPLI SEULEMENT : `PartHaute` la remplace des qu'elle est renseignee. Un
	 * seuil METRIQUE cale sur un monde de 8 km avale tout le relief d'un monde
	 * de 64 -- la faute que ce depot a deja payee sur `tectonics.py`.
	 */
	float ElevationM = 150.0f;

	/**
	 * Part des TERRES rangee au-dessus du seuil d'altitude, entre 0 et 1.
	 *
	 * Hors de ]0..1[, on retombe sur `ElevationM`. Le quantile porte sur les
	 * terres SEULES : y inclure les fonds marins reviendrait a mesurer la part
	 * haute d'une distribution que la mer domine.
	 */
	float PartHaute = 0.15f;

	/**
	 * Part des cellules orogeniques assez ACCIDENTEES pour rester socle.
	 *
	 * C'est le decapage. Hors de ]0..1[, aucun tri : toute cellule orogenique
	 * est socle, c'est-a-dire le comportement d'avant la correction du bassin
	 * d'avant-pays.
	 */
	float PartAccidentee = 0.5f;

	/** Sur quelle portee le relief local se mesure, en metres. */
	float ReliefRayonM = 3000.0f;
};

/**
 * Ce que la decision du socle a compte en passant.
 *
 * LE MODULE MESURE, L'APPELANT RAPPORTE. C'est la regle posee apres
 * l'extraction du sol de fond, ou module et acteur journalisaient tous deux :
 * une passe qui ecrit elle-meme au journal ne peut plus etre appelee depuis un
 * test sans le polluer, ni deux fois sans qu'on croie a un doublon.
 *
 * ET LE COMPTE EST CE QUI TRANCHE, pas le temps : « un chiffre identique apres
 * une correction reelle » est le signe que ce depot a rencontre cinq fois --
 * soit le monde vient du cache, soit la garde ne mord pas.
 */
struct WORLDSEED_API FWorldseedSocleReleve
{
	/** Cellules emergees que la convergence ou l'altitude declarent orogeniques. */
	int32 Orogenes = 0;

	/** Celles d'entre elles que le relief garde au socle. */
	int32 Socles = 0;

	/** Le relief local exige, en metres -- 0 quand le tri est desactive. */
	float SeuilReliefM = 0.0f;

	/** Le seuil d'altitude retenu, en metres, quantile ou repli. */
	float SeuilElevationM = 0.0f;
};

namespace WorldseedSocle
{
	/**
	 * Le seuil d'altitude au-dela duquel un relief est repute decape.
	 *
	 * Par QUANTILE sur les terres emergees, avec repli sur la valeur metrique.
	 * Voir `FWorldseedSocleRegles::PartHaute` : c'est la que se joue la mise a
	 * l'echelle, et un seuil metrique ne la suit pas.
	 */
	WORLDSEED_API float SeuilElevationM(const TArray<float>& ElevationM,
		float PartHaute, float SeuilParDefautM);

	/**
	 * Le relief local, sur une grille grossie d'un facteur.
	 *
	 * LE GROSSISSEMENT EST UN CHOIX DE COUT, pas d'exactitude : la fenetre fait
	 * trois kilometres, soit pres de cent cellules de rayon, et un balayage
	 * naif y coute deux milliards d'operations. Un huitieme de resolution
	 * suffit largement a dire si l'on est dans une chaine ou dans une plaine --
	 * on ne cherche pas un contour, on cherche une CLASSE de terrain.
	 *
	 * Rend le min et le max dilates sur `PX x PY` cellules ; leur difference
	 * est le relief. Publique parce qu'un test doit pouvoir la confronter seule
	 * -- une plaine doit rendre zero, une chaine son amplitude.
	 */
	WORLDSEED_API void ReliefLocal(const FWorldseedGeometry& Geo,
		const TArray<float>& ElevationM, float RayonM, int32 Grossissement,
		TArray<float>& OutBas, TArray<float>& OutHaut, int32& OutPX, int32& OutPY);

	/**
	 * Marque les cellules ou le socle affleure.
	 *
	 * `Convergence` peut etre vide : on retombe alors sur la seule altitude.
	 * `OutEstSocle` est dimensionne a la grille et vaut 1 ou 0.
	 */
	WORLDSEED_API void Marquer(const FWorldseedGeometry& Geo,
		const TArray<float>& ElevationM, const TArray<float>& Convergence,
		const FWorldseedSocleRegles& Regles, TArray<uint8>& OutEstSocle,
		FWorldseedSocleReleve& OutReleve);
}
