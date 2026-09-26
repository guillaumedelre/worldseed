// Worldseed - le decoupage du monde en regions geographiques et en pays.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedRules.h"

/**
 * CE QUI DONNE SON CARACTERE A UNE REGION, ET DONC SA LANGUE.
 *
 * Il ne s'agit PAS d'un biome de plus. Un biome est une etiquette par CELLULE,
 * calee sur le diagramme de Whittaker ; un caractere est le trait dominant
 * d'une REGION ENTIERE, lu sur ses moyennes. Une region peut porter cinq
 * biomes et n'avoir qu'un caractere -- c'est precisement ce qu'on cherche,
 * puisque c'est lui qui decide de la sonorite de son nom.
 */
enum class EWorldseedRegionCaractere : uint8
{
	Polaire,      // froid dominant : calotte, toundra, taiga
	Massif,       // haute altitude et forte pente
	Aride,        // desert, steppe seche
	ForetHumide,  // pluie forte et chaleur
	Littoral,     // long trait de cote pour peu de terres
	Plaine,       // le reste : terres basses temperees
	Nombre
};

/** Une region : un morceau de terre coherent, nomme. */
struct WORLDSEED_API FWorldseedRegion
{
	/** Son index dans `FWorldseedRegions::Regions`. */
	int32 Id = INDEX_NONE;

	/** Le pays auquel elle appartient, ou INDEX_NONE. */
	int32 Pays = INDEX_NONE;

	FString Nom;

	/** La cle d'univers de `WorldseedNoms` qui a servi a la nommer. */
	FString Univers;

	EWorldseedRegionCaractere Caractere = EWorldseedRegionCaractere::Plaine;

	/** Centre de gravite, en metres monde. */
	FVector2D CentreM = FVector2D::ZeroVector;

	/**
	 * Ou POSER SON NOM, en metres monde.
	 *
	 * LE CENTRE DE GRAVITE NE CONVIENT PAS, et ce n'est pas un detail : le
	 * barycentre d'une forme CONCAVE lui est EXTERIEUR -- un croissant, une
	 * region qui epouse une baie, un bassin en fer a cheval autour d'un
	 * massif. Mesure sur le monde de reference : **6 regions sur 47**, soit
	 * une etiquette sur huit posee en pleine mer ou chez la voisine.
	 *
	 * L'ancrage vaut le centre de gravite quand il tombe DANS la region, et
	 * sinon la cellule de la region la plus proche de lui.
	 *
	 * ⚠ IL NE TRAVERSE PAS LE CACHE, et c'est deliberé -- comme les noms. Il
	 * se recalcule dans `Nommer`, qui est rejoue dans les DEUX branches ;
	 * l'ecrire n'ajouterait que du poids et une occasion de le figer.
	 */
	FVector2D AncrageM = FVector2D::ZeroVector;

	float AireKm2 = 0.0f;
	float AltitudeMoyenneM = 0.0f;
	float TemperatureMoyenneC = 0.0f;
	float PluieMoyenneMm = 0.0f;

	/** Part de ses cellules qui touchent la mer, dans [0..1]. */
	float PartLittorale = 0.0f;

	uint8 BiomeDominant = 0;
};

/** Un pays : un agregat de regions voisines. */
struct WORLDSEED_API FWorldseedPays
{
	int32 Id = INDEX_NONE;

	FString Nom;
	FString Univers;

	FVector2D CentreM = FVector2D::ZeroVector;

	/**
	 * Ou poser son nom. Meme raison que pour une region, en pire : un pays
	 * agrege plusieurs regions et peut enjamber une mer interieure, donc son
	 * barycentre a plus de chances encore de tomber a l'eau.
	 *
	 * Ne traverse pas le cache : recalcule par `Nommer`.
	 */
	FVector2D AncrageM = FVector2D::ZeroVector;

	float AireKm2 = 0.0f;

	TArray<int32> Regions;
};

/**
 * LE DECOUPAGE COMPLET, ET SA GRILLE PROPRE.
 *
 * ⚠ IL NE TRAVAILLE PAS SUR LA GRILLE DE SIMULATION, et c'est delibere. Une
 * region fait plusieurs kilometres ; la resoudre au metre couterait 8,4
 * millions de cellules pour une frontiere qu'on ne saurait de toute facon pas
 * dessiner plus finement que le pixel de la carte. La grille des regions est
 * un sous-echantillonnage entier de celle du monde (facteur `Facteur`), ce qui
 * garde la correspondance exacte et divise le cache par seize.
 *
 * `Id` vaut -1 en mer et hors monde. Indexation J * NX + I, ligne 0 au SUD,
 * comme partout ailleurs dans ce depot.
 */
struct WORLDSEED_API FWorldseedRegions
{
	/** Cote de la grille des regions. */
	int32 NX = 0;
	int32 NY = 0;

	/** Combien de cellules de simulation par cellule de region. */
	int32 Facteur = 1;

	/** L'etendue du monde en metres, recopiee pour que `RegionEn` se suffise. */
	float LargeurM = 0.0f;
	float HauteurM = 0.0f;

	/** Identifiant de region par cellule, -1 en mer. */
	TArray<int16> Id;

	TArray<FWorldseedRegion> Regions;
	TArray<FWorldseedPays> Pays;

	int32 CellCount() const { return NX * NY; }

	bool EstValide() const
	{
		return NX > 1 && NY > 1 && Id.Num() == CellCount();
	}

	/**
	 * La region sous un point du monde, ou INDEX_NONE en mer.
	 *
	 * Les metres sont ceux du repere monde -- centre a l'origine, X vers l'est,
	 * Y vers le nord -- et la longitude S'ENROULE, comme partout ici.
	 */
	int32 RegionEn(double XM, double YM) const;

	/**
	 * La region sous un point donne en UV, ou INDEX_NONE.
	 *
	 * POUR LE GLOBE, QUI NE CONNAIT PAS LES METRES. Il projette une sphere et
	 * travaille en latitude et longitude ; lui faire convertir ses UV en
	 * metres pour que `RegionEn` les reconvertisse en UV ajouterait deux
	 * divisions et une occasion de se tromper de convention, pour rien.
	 *
	 * ⚠ C'EST LA SEULE IMPLEMENTATION : `RegionEn` appelle celle-ci apres sa
	 * conversion. Deux copies de la meme lecture divergeraient a la premiere
	 * retouche, et l'ecart se verrait exactement la ou le globe et la carte
	 * montrent le meme endroit.
	 */
	int32 RegionEnUV(double U, double V) const;

	/** Le pays sous un point du monde, ou INDEX_NONE. */
	int32 PaysEn(double XM, double YM) const;

	/** Le nom de la region sous ce point, ou une chaine vide. */
	FString NomEn(double XM, double YM) const;
};

/** Les reglages du decoupage, lus dans `world_rules.json`. */
struct WORLDSEED_API FWorldseedRegionRules
{
	/** Sous-echantillonnage de la grille de simulation. */
	int32 Facteur = 4;

	/** En dessous, un bassin est absorbe par son voisin. En km carres. */
	float AireMinKm2 = 6.0f;

	/** Au-dessus, une region est recoupee par l'altitude. En km carres. */
	float AireMaxKm2 = 60.0f;

	/** Aire visee pour un pays. En km carres. */
	float AirePaysKm2 = 150.0f;

	/** Altitude au-dessus de laquelle une region est un massif. */
	float MassifAltitudeM = 600.0f;

	/** Temperature moyenne en dessous de laquelle une region est polaire. */
	float PolaireTempC = 0.0f;

	/** Pluie moyenne en dessous de laquelle une region est aride. */
	float ArideePluieMm = 400.0f;

	/** Pluie au-dessus de laquelle, avec la chaleur, c'est une foret humide. */
	float ForetPluieMm = 1200.0f;

	/** Part de cellules cotieres au-dela de laquelle une region est littorale. */
	float LittoralPart = 0.45f;

	static FWorldseedRegionRules FromRules(const UWorldseedRules& Rules);
};

/**
 * LA PASSE DE DECOUPAGE.
 *
 * ELLE EST GEOGRAPHIQUE, PAS ARBITRAIRE, et c'est tout son objet. Le Voronoi
 * des plaques tectoniques dessine deja les continents ; il ne dit rien de ce
 * qui fait qu'un pays tient ensemble. Ce qui le dit, c'est le BASSIN VERSANT :
 * une vallee dont toutes les eaux se rejoignent est une unite humaine avant
 * d'etre une unite hydrologique -- les routes y suivent les rivieres, les
 * villes s'y posent aux confluences, et les cretes font les frontieres. C'est
 * pour cela que les frontieres reelles suivent si souvent les lignes de
 * partage des eaux.
 *
 * ⚠ UN BASSIN VERSANT NE PEUT PAS ETRE POSITIONNEL. Savoir ou s'ecoule une
 * cellule demande de suivre la pente jusqu'a la mer : c'est un etiquetage
 * GLOBAL, et deux cellules voisines ne peuvent pas le calculer chacune de son
 * cote. Tout le reste de ce depot est positionnel et se recalcule a la demande
 * ; celui-ci ne le peut pas, et c'est ce qui rend sa mise en cache non pas
 * commode mais NECESSAIRE.
 */
namespace WorldseedRegions
{
	/**
	 * Decoupe, mesure, groupe en pays -- et NE NOMME PAS.
	 *
	 * Le nommage est separe parce qu'il lit `noms.json`, un fichier de CONTENU
	 * qui n'entre pas dans l'empreinte du cache : retoucher un suffixe ne doit
	 * pas couter une regeneration du monde. Le decoupage, lui, est geometrique
	 * et se cache. Voir `Nommer`.
	 */
	WORLDSEED_API void Construire(const FWorldseedGeometry& Geometry,
		const TArray<float>& ElevationM, const TArray<float>& TempC,
		const TArray<float>& PrecipMm, const TArray<uint8>& BiomeId,
		const FWorldseedRegionRules& Rules, int32 Seed, FWorldseedRegions& Out);

	/**
	 * Donne a chaque region et a chaque pays son univers et son nom.
	 *
	 * SEPAREE DE `Construire` A DESSEIN : elle se rejoue apres une lecture du
	 * cache, pour quelques millisecondes, et les noms suivent alors le fichier
	 * de tables plutot que le monde fige. Deterministe : la graine d'une region
	 * est derivee de sa POSITION, jamais d'un compteur -- sans quoi inserer une
	 * region renommerait toutes les suivantes.
	 */
	WORLDSEED_API void Nommer(FWorldseedRegions& Regions, int32 Seed);

	/**
	 * Decide ou poser le nom de chaque region et de chaque pays.
	 *
	 * Le centre de gravite quand il tombe dans la forme, la cellule la plus
	 * proche de lui sinon -- une forme concave a son barycentre DEHORS, et
	 * l'etiquette s'y retrouverait en mer. Voir `FWorldseedRegion::AncrageM`.
	 *
	 * Appelee par `Nommer`, donc rejouee dans les deux branches du cache.
	 * Publique parce qu'un test doit pouvoir l'eprouver seule.
	 */
	WORLDSEED_API void AncrerLesEtiquettes(FWorldseedRegions& Regions);

	/** Le libelle d'un caractere, pour les releves. */
	WORLDSEED_API const TCHAR* NomDuCaractere(EWorldseedRegionCaractere C);
}
