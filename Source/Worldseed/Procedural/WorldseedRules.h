// Worldseed - lecture runtime de Tools/WorldGen/rules/world_rules.json.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "WorldseedRules.generated.h"

class FJsonObject;

/**
 * Le monde raisonne en METRES, la scene d'Unreal en centimetres.
 *
 * DEFINIE UNE SEULE FOIS, ET C'EST LE FOND DE L'AFFAIRE. Quatre fichiers la
 * portaient chacun dans leur namespace anonyme. Deux d'entre eux se sont
 * retrouves dans la meme unite de traduction le jour ou deux fichiers ont ete
 * ajoutes au module : le build unifie d'Unreal concatene les .cpp, et deux
 * namespaces anonymes n'en font alors qu'un. La collision ne dependait donc pas
 * du code ecrit mais du REGROUPEMENT choisi par UBT -- elle dormait depuis des
 * semaines et s'est reveillee sur un fichier que personne n'avait touche.
 */
inline constexpr float WorldseedMetersToCm = 100.0f;

/**
 * Les noms de SECTION du fichier de regles, definis UNE SEULE FOIS.
 *
 * POURQUOI ILS SONT ICI. Chaque module declarait le sien dans un namespace
 * ANONYME, et UBT concatene les .cpp en une seule unite de traduction ou deux
 * namespaces anonymes n'en font qu'un : ajouter la lithologie a suffi a faire
 * tomber la compilation sur un "SUB" deja defini par les biomes. Ce depot avait
 * deja paye ce piege avec WorldseedMetersToCm, pour exactement la meme raison,
 * et la collision ne depend pas du code ecrit mais du REGROUPEMENT choisi par
 * UBT -- donc elle revient sans prevenir.
 */
namespace WorldseedSection
{
	inline constexpr const TCHAR* Biomes = TEXT("biomes");
	inline constexpr const TCHAR* Substrat = TEXT("substrat");
	inline constexpr const TCHAR* Sol = TEXT("ground");
	inline constexpr const TCHAR* Voxel = TEXT("voxel");
}

/**
 * Geometrie du monde : taille, resolution, et surtout correspondance
 * ligne <-> latitude.
 *
 * Ce choix pese plus lourd que n'importe quel reglage de climat, parce qu'il
 * fixe la PART DE SURFACE de chaque zone. Mesures relevees dans config.py :
 *
 *      lineaire     tropiques 26,0 %   temperees 47,9 %   polaires 26,0 %
 *      equal-area   tropiques 39,8 %   temperees 52,0 %   polaires  8,3 %
 *      sphere       tropiques 39,8 %   temperees 52,0 %   polaires  8,3 %
 *
 * La correspondance lineaire donne trois fois trop de surface polaire. Sur une
 * sphere la surface entre -L et +L vaut sin(L) : c'est la projection
 * cylindrique equivalente de Lambert.
 */
USTRUCT(BlueprintType)
struct WORLDSEED_API FWorldseedGeometry
{
	GENERATED_BODY()

	/**
	 * Grille de simulation. NX est l'axe des LONGITUDES (360 degres), NY celui
	 * des LATITUDES (180 degres) : la carte a donc la forme de la surface
	 * deroulee d'une sphere, soit deux fois plus large que haute.
	 *
	 * Une carte carree couvrant 360 x 180 degres etirerait tout d'un facteur
	 * deux horizontalement — chaque pixel y couvrait 0,70 degre de longitude
	 * pour 0,35 de latitude.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Worldseed")
	int32 NX = 4097;

	UPROPERTY(BlueprintReadOnly, Category = "Worldseed")
	int32 NY = 2049;

	/**
	 * Hauteur du monde en metres, d'un pole a l'autre. La largeur vaut le
	 * double : c'est ce qui rend les cellules carrees en metres, condition
	 * necessaire pour que l'erosion, les gradients et les transformees de
	 * distance restent isotropes.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Worldseed")
	float HeightM = 8000.0f;

	float WidthM() const { return HeightM * 2.0f; }

	/** Etendue de latitude couverte par la carte, en degres. */
	UPROPERTY(BlueprintReadOnly, Category = "Worldseed")
	float LatSpanDeg = 180.0f;

	/** "linear" ou "equalArea". */
	UPROPERTY(BlueprintReadOnly, Category = "Worldseed")
	FString LatitudeMapping = TEXT("equalArea");

	/** Dosage entre lineaire (0) et equivalent-aire (1). */
	UPROPERTY(BlueprintReadOnly, Category = "Worldseed")
	float LatitudeEqualAreaBlend = 1.0f;

	/** Identique sur les deux axes, par construction (NX = 2 NY, W = 2 H). */
	float MetersPerPixel() const { return (NY > 1) ? HeightM / static_cast<float>(NY - 1) : HeightM; }
	float HalfSizeM() const { return HeightM * 0.5f; }

	/** Nombre de cellules de la grille. */
	int32 CellCount() const { return NX * NY; }

	/** Longitude en degres [0..360[ de la colonne I. */
	float LongitudeDegForColumn(int32 I) const
	{
		return (NX > 1) ? 360.0f * static_cast<float>(I) / static_cast<float>(NX) : 0.0f;
	}

	/**
	 * Latitude en degres de la ligne J. Convention du generateur Python :
	 * J = 0 au pole SUD, J = NY-1 au pole NORD.
	 */
	float LatitudeDegForRow(int32 J) const;

	/** Meme calcul pour une coordonnee normalisee V dans [0..1] (0 = sud). */
	float LatitudeDegForV(float V) const;

	/**
	 * Inverse : coordonnee V [0..1] d'une latitude donnee. Indispensable pour
	 * reprojeter la carte sur un globe, ou l'on part de la latitude d'un point
	 * de la sphere pour retrouver la ligne correspondante.
	 */
	float VForLatitudeDeg(float LatitudeDeg) const;

	// ------------------------------------------- position du monde -> grille
	//
	// UNE SEULE CONVENTION, ET C'EST CELLE DU SOL. Trois se partageaient le
	// depot le 23 septembre 2026, et deux se contredisaient d'une DEMI-CELLULE :
	//
	//     WorldseedPeinture.cpp    Floor(U * NX)       <- la couleur peinte
	//     WorldseedDensity.cpp     Floor + modulo      <- le champ
	//     WorldseedVoxelTerrain    RoundToInt(U * NX)  <- le releve du joueur
	//
	// Consequence mesurable : le releve du HUD pouvait nommer un biome que le
	// joueur ne foulait pas. On tranche pour `Floor`, parce que c'est ce qui
	// PEINT le sol -- entre ce qu'on voit et ce qu'on lit, c'est ce qu'on voit
	// qui a raison.
	//
	// ELLE VIT DANS LA GEOMETRIE, PAS DANS L'AFFICHAGE, aupres de
	// `LongitudeDegForColumn` et `VForLatitudeDeg` qui sont deja ici : c'est
	// une convention du MONDE, et le prochain consommateur ne doit pas avoir a
	// la redecouvrir.

	/**
	 * Coordonnees normalisees d'une position du monde, en metres.
	 *
	 * U S'ENROULE et V SE BORNE, et ce n'est pas symetrique par gout : la
	 * longitude fait le tour de la sphere, alors qu'un pole n'a pas de voisin
	 * au-dela. Borner U couperait la carte au meridien de bordure ; enrouler V
	 * ferait passer l'Arctique dans l'Antarctique.
	 *
	 * `FloorToDouble` et non `FMath::Fractional` : ce dernier est base sur une
	 * TRONCATURE et rend du negatif pour un X negatif, ce qui indexerait hors
	 * du tableau. Les deux noms se ressemblent, un seul convient.
	 */
	void UVDepuisMetres(double Xm, double Ym, double& OutU, double& OutV) const;

	/** Cellule de la grille sous une position du monde. Convention du sol. */
	int32 CelluleDepuisMetres(double Xm, double Ym) const;
};

/**
 * Regles du monde, lues au runtime depuis le MEME fichier que le generateur
 * Python. La documentation du projet insiste : aucun seuil n'est en dur dans le
 * code, tout vit dans world_rules.json. Dupliquer ces valeurs en constantes C++
 * ferait diverger les deux moities du projet a la premiere retouche.
 */
UCLASS(BlueprintType)
class WORLDSEED_API UWorldseedRules : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Journalise UNE FOIS chaque cle demandee et absente du fichier.
	 *
	 * POURQUOI. Num(), Int() et Str() rendent leur valeur de repli SANS UN MOT
	 * quand la cle manque. Une faute de frappe, une cle renommee d'un seul cote,
	 * une section absente : le jeu tourne sur la valeur codee en dur et le
	 * fichier de regles a l'air respecte. Ce depot a deja paye trois fois cette
	 * famille de defaut -- un materiau de terrain qui pointait le maitre au lieu
	 * de l'instance, une propriete mal orthographiee par Epic, une constante
	 * dupliquee entre deux fichiers.
	 *
	 * Le releve est fait au vol, sans verrou sur le chemin nominal : seule
	 * l'insertion d'une cle MANQUANTE prend le verrou, et elle est rare par
	 * construction.
	 */
	void ReportMissingKeys() const;

	/**
	 * Charge les regles. Cherche d'abord Tools/WorldGen/rules/world_rules.json
	 * (la source unique, suivie par git), puis se rabat sur une copie dans
	 * Content pour les builds packages.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed")
	static UWorldseedRules* LoadRules(FString& OutError);

	/** Chemins explores, dans l'ordre. Expose pour le diagnostic. */
	UFUNCTION(BlueprintPure, Category = "Worldseed")
	static TArray<FString> GetCandidatePaths();

	/** Graine par defaut du fichier. */
	UPROPERTY(BlueprintReadOnly, Category = "Worldseed")
	int32 Seed = 20260909;

	UPROPERTY(BlueprintReadOnly, Category = "Worldseed")
	FWorldseedGeometry Geometry;

	/** Fichier effectivement charge. */
	UPROPERTY(BlueprintReadOnly, Category = "Worldseed")
	FString SourcePath;

	/**
	 * Empreinte du contenu du fichier. Entre dans la cle du cache : sans elle,
	 * retoucher un seuil laisserait charger l ancien monde en croyant tester le
	 * nouveau reglage.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Worldseed")
	FString SourceHash;

	// --- acces types, avec valeur de repli si la cle manque -----------------

	double Num(const FString& Section, const FString& Key, double Fallback) const;
	int32 Int(const FString& Section, const FString& Key, int32 Fallback) const;
	FString Str(const FString& Section, const FString& Key, const FString& Fallback) const;
	bool Has(const FString& Section, const FString& Key) const;

	/**
	 * Tableau brut d'une section.
	 *
	 * Les accesseurs types au-dessus suffisent aux reglages scalaires, mais le
	 * diagramme de Whittaker est une LISTE DE BANDES imbriquees : la reduire a
	 * des cles plates la rendrait illisible dans le fichier de regles, qui est
	 * justement l'endroit ou elle doit se lire. Nullptr si absente.
	 */
	const TArray<TSharedPtr<FJsonValue>>* Array(const FString& Section,
		const FString& Key) const;

private:
	/** Cles demandees et absentes, pour ReportMissingKeys. */
	void NoteMissing(const FString& Section, const FString& Key) const;

	mutable TSet<FString> MissingKeys;
	mutable FCriticalSection MissingKeysLock;

	TSharedPtr<FJsonObject> Root;

	const TSharedPtr<FJsonObject>* FindSection(const FString& Section) const;
};

/**
 * LE FACTEUR D'ECHELLE VERTICALE, ET IL NE SE RECOPIE PAS.
 *
 * `world.sizeKm` N'EST PAS LA TAILLE DU MONDE, c'est la HAUTEUR DE REFERENCE
 * du calage metrique -- les deux se sont longtemps trouvees egales a 8 km, ce
 * qui masquait completement la distinction. Tout ce qui est metrique dans la
 * tectonique suit ce rapport : profondeur oceanique, base continentale,
 * hauteur de montagne. C'est la regle d'echelle du projet, « maquette » :
 * le relatif ne bouge pas, le metrique suit la reduction.
 *
 * ELLE VIT ICI PARCE QUE DEUX PASSES EN DEPENDENT. La tectonique l'applique au
 * relief ; le climat doit l'appliquer au GRADIENT ADIABATIQUE, sans quoi un
 * monde plus grand -- donc plus montagneux -- se refroidit sans compensation.
 * La recopier dans les deux ferait diverger la moitie qui sculpte et celle qui
 * rechauffe, et aucun compilateur ne le dirait.
 */
WORLDSEED_API float WorldseedVerticalScale(const UWorldseedRules& Rules, float MapHeightM);
