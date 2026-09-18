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
	TSharedPtr<FJsonObject> Root;

	const TSharedPtr<FJsonObject>* FindSection(const FString& Section) const;
};
