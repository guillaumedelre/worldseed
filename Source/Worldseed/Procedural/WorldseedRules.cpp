// Worldseed - lecture runtime de Tools/WorldGen/rules/world_rules.json.

#include "Procedural/WorldseedRules.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"

float FWorldseedGeometry::LatitudeDegForRow(int32 J) const
{
	if (NY <= 1)
	{
		return 0.0f;
	}

	// axis_m = linspace(-half, +half, NY) puis y = axis / half  ->  [-1 .. +1].
	const double Y0 = -1.0 + 2.0 * static_cast<double>(J) / static_cast<double>(NY - 1);
	return LatitudeDegForV(static_cast<float>((Y0 + 1.0) * 0.5));
}

float FWorldseedGeometry::LatitudeDegForV(float V) const
{
	const double Y0 = FMath::Clamp(static_cast<double>(V), 0.0, 1.0) * 2.0 - 1.0;

	double Y = Y0;
	const double Blend = (LatitudeMapping == TEXT("equalArea"))
		? FMath::Clamp(static_cast<double>(LatitudeEqualAreaBlend), 0.0, 1.0)
		: 0.0;

	if (Blend > 0.0)
	{
		// Projection equivalente de Lambert : la surface entre -L et +L vaut
		// sin(L), donc on inverse par arcsin pour que chaque ligne represente
		// la meme aire sur la sphere.
		const double EqualArea = FMath::Asin(FMath::Clamp(Y0, -1.0, 1.0)) / (PI * 0.5);
		Y = (1.0 - Blend) * Y0 + Blend * EqualArea;
	}

	return static_cast<float>(Y * (static_cast<double>(LatSpanDeg) * 0.5));
}

float FWorldseedGeometry::VForLatitudeDeg(float LatitudeDeg) const
{
	const double Half = static_cast<double>(LatSpanDeg) * 0.5;
	if (Half <= 0.0)
	{
		return 0.5f;
	}

	// y est l'image de y0 par la correspondance ; on cherche y0.
	const double Y = FMath::Clamp(static_cast<double>(LatitudeDeg) / Half, -1.0, 1.0);

	const double Blend = (LatitudeMapping == TEXT("equalArea"))
		? FMath::Clamp(static_cast<double>(LatitudeEqualAreaBlend), 0.0, 1.0)
		: 0.0;

	double Y0 = Y;
	if (Blend >= 1.0 - KINDA_SMALL_NUMBER)
	{
		// Cas pur : y = asin(y0) / (pi/2)  =>  y0 = sin(y * pi/2).
		Y0 = FMath::Sin(Y * PI * 0.5);
	}
	else if (Blend > 0.0)
	{
		// Melange : pas d'inverse analytique, on tabule et on interpole, comme
		// le fait y_of_latitude() cote Python.
		constexpr int32 TableSize = 256;
		double BestY0 = Y;
		double BestErr = BIG_NUMBER;
		for (int32 I = 0; I < TableSize; ++I)
		{
			const double Candidate = -1.0 + 2.0 * static_cast<double>(I)
				/ static_cast<double>(TableSize - 1);
			const double Image = (1.0 - Blend) * Candidate
				+ Blend * (FMath::Asin(FMath::Clamp(Candidate, -1.0, 1.0)) / (PI * 0.5));
			const double Err = FMath::Abs(Image - Y);
			if (Err < BestErr)
			{
				BestErr = Err;
				BestY0 = Candidate;
			}
		}
		Y0 = BestY0;
	}

	return static_cast<float>(FMath::Clamp((Y0 + 1.0) * 0.5, 0.0, 1.0));
}

void FWorldseedGeometry::UVDepuisMetres(double Xm, double Ym,
	double& OutU, double& OutV) const
{
	double U = Xm / static_cast<double>(WidthM()) + 0.5;
	U -= FMath::FloorToDouble(U);

	OutU = U;
	OutV = FMath::Clamp(Ym / static_cast<double>(HeightM) + 0.5, 0.0, 1.0);
}

int32 FWorldseedGeometry::CelluleDepuisMetres(double Xm, double Ym) const
{
	double U = 0.0;
	double V = 0.0;
	UVDepuisMetres(Xm, Ym, U, V);

	// U vit deja dans [0, 1[ apres le Floor, donc la colonne y tombe seule ;
	// V, lui, est borne a 1 INCLUS, et V = 1 donnerait la ligne NY. Les deux
	// bornes restent quand meme : une grille degeneree ne doit pas indexer
	// hors du tableau.
	const int32 Col = FMath::Clamp(
		FMath::FloorToInt(U * static_cast<double>(NX)), 0, NX - 1);
	const int32 Row = FMath::Clamp(
		FMath::FloorToInt(V * static_cast<double>(NY)), 0, NY - 1);

	return Row * NX + Col;
}

TArray<FString> UWorldseedRules::GetCandidatePaths()
{
	TArray<FString> Paths;

	// Source unique du projet, suivie par git et partagee avec le generateur
	// Python : c'est elle qui fait foi.
	Paths.Add(FPaths::ConvertRelativePathToFull(
		FPaths::ProjectDir() / TEXT("Tools/WorldGen/rules/world_rules.json")));

	// Repli pour un build package, ou Tools/ n'est pas embarque.
	Paths.Add(FPaths::ConvertRelativePathToFull(
		FPaths::ProjectContentDir() / TEXT("Worldseed/Rules/world_rules.json")));

	return Paths;
}

UWorldseedRules* UWorldseedRules::LoadRules(FString& OutError)
{
	OutError.Reset();

	FString Chosen;
	FString Raw;
	for (const FString& Candidate : GetCandidatePaths())
	{
		if (FFileHelper::LoadFileToString(Raw, *Candidate))
		{
			Chosen = Candidate;
			break;
		}
	}

	if (Chosen.IsEmpty())
	{
		OutError = FString::Printf(
			TEXT("world_rules.json introuvable. Chemins explores : %s"),
			*FString::Join(GetCandidatePaths(), TEXT(" | ")));
		UE_LOG(LogTemp, Error, TEXT("[Worldseed] %s"), *OutError);
		return nullptr;
	}

	TSharedPtr<FJsonObject> Parsed;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
	{
		OutError = FString::Printf(TEXT("JSON invalide : %s"), *Chosen);
		UE_LOG(LogTemp, Error, TEXT("[Worldseed] %s"), *OutError);
		return nullptr;
	}

	UWorldseedRules* Rules = NewObject<UWorldseedRules>();
	Rules->Root = Parsed;
	Rules->SourcePath = Chosen;
	Rules->SourceHash = FMD5::HashAnsiString(*Raw);

	double SeedValue = 20260909.0;
	Parsed->TryGetNumberField(TEXT("seed"), SeedValue);
	Rules->Seed = static_cast<int32>(SeedValue);

	FWorldseedGeometry& Geo = Rules->Geometry;

	// CE QUI SUIT EST UN REPLI, PAS LA GEOMETRIE DU MONDE.
	//
	// `Generate` ecrase NY, NX et HeightM avec la taille et la resolution que
	// lui passe l'appelant -- le menu, une sonde ou le banc. Seuls les trois
	// champs de latitude, poses juste apres, survivent a la copie.
	//
	// CONSEQUENCE A CONNAITRE : `world.simResolution` n'est lue QU'ICI et sa
	// valeur ne sert jamais ; `world.resolution` n'est lue NULLE PART depuis
	// que le pipeline PNG a disparu. Les retirer du fichier de regles est un
	// menage legitime, mais il change l'empreinte MD5 du fichier, donc il
	// invalide tous les mondes en cache -- c'est un arbitrage, pas un detail.
	Geo.NY = Rules->Int(TEXT("world"), TEXT("simResolution"), 2049);
	Geo.NX = Geo.NY * 2;

	// `sizeKm` N'EST PAS LA TAILLE DU MONDE : c'est la hauteur de REFERENCE du
	// calage metrique, lue par `WorldseedVerticalScale` (plus bas dans ce
	// fichier), qui en tire le facteur applique a tout ce qui est metrique.
	Geo.HeightM = static_cast<float>(Rules->Num(TEXT("world"), TEXT("sizeKm"), 8.0) * 1000.0);
	Geo.LatSpanDeg = static_cast<float>(Rules->Num(TEXT("world"), TEXT("latitudeSpanDeg"), 180.0));
	Geo.LatitudeMapping = Rules->Str(TEXT("world"), TEXT("latitudeMapping"), TEXT("equalArea"));
	Geo.LatitudeEqualAreaBlend =
		static_cast<float>(Rules->Num(TEXT("world"), TEXT("latitudeEqualAreaBlend"), 1.0));

	// CETTE LIGNE N'ANNONCE PAS LA TAILLE DU MONDE, ET C'EST DELIBERE.
	//
	// Elle l'a longtemps fait, et elle mentait : elle affichait « 16.0 x 8.0 km
	// sim=4098x2049 » pour un monde de 64 x 32 km a 4096x2048. La geometrie
	// batie ci-dessus est un REPLI que `WorldseedPipeline::Generate` ecrase
	// aussitot avec la taille et la resolution de l'appelant
	// (`WorldseedPipeline.cpp:133-135`) : seuls `LatSpanDeg`,
	// `LatitudeMapping` et `LatitudeEqualAreaBlend` lui survivent. Ces trois
	// chiffres ne decrivaient donc AUCUN monde genere -- du bruit qui
	// ressemblait a une information, et le depot a une regle contre cela.
	//
	// `sizeKm` est journalisee sous son vrai nom : c'est la HAUTEUR DE
	// REFERENCE du calage metrique, dont `WorldseedVerticalScale` tire son
	// rapport (32000 / 8000 = 4,0, soit exactement son plafond). Les deux se
	// sont longtemps trouvees egales, ce qui masquait la distinction.
	//
	// La taille reellement generee est dite par la ligne « monde genere : ... »
	// a la fin de la chaine, qui la tient de l'appelant.
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] regles chargees : %s  seed=%d  reference d'echelle %.1f km  ")
		TEXT("latitude=%s (blend %.2f)"),
		*Chosen, Rules->Seed, Geo.HeightM / 1000.0f,
		*Geo.LatitudeMapping, Geo.LatitudeEqualAreaBlend);

	return Rules;
}

const TArray<TSharedPtr<FJsonValue>>* UWorldseedRules::Array(const FString& Section,
	const FString& Key) const
{
	const TSharedPtr<FJsonObject>* Obj = FindSection(Section);
	if (!Obj || !(*Obj).IsValid())
	{
		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* Found = nullptr;
	if ((*Obj)->TryGetArrayField(Key, Found))
	{
		return Found;
	}
	return nullptr;
}

const TSharedPtr<FJsonObject>* UWorldseedRules::FindSection(const FString& Section) const
{
	if (!Root.IsValid())
	{
		return nullptr;
	}

	const TSharedPtr<FJsonObject>* Found = nullptr;
	if (Root->TryGetObjectField(Section, Found))
	{
		return Found;
	}
	return nullptr;
}

bool UWorldseedRules::Has(const FString& Section, const FString& Key) const
{
	const TSharedPtr<FJsonObject>* Obj = FindSection(Section);
	return Obj && (*Obj).IsValid() && (*Obj)->HasField(Key);
}

void UWorldseedRules::NoteMissing(const FString& Section, const FString& Key) const
{
	// LE VERROU N'EST PRIS QUE SUR LE CHEMIN FAUTIF. Num() est appele dans des
	// boucles paralleles -- SeasonalAmplitude le fait par cellule -- donc le
	// chemin nominal, celui ou la cle existe, ne doit rien verrouiller du tout.
	FScopeLock Lock(&MissingKeysLock);
	MissingKeys.Add(Section + TEXT(".") + Key);
}

void UWorldseedRules::ReportMissingKeys() const
{
	FScopeLock Lock(&MissingKeysLock);
	if (MissingKeys.Num() == 0)
	{
		return;
	}

	TArray<FString> Triees = MissingKeys.Array();
	Triees.Sort();

	UE_LOG(LogTemp, Warning,
		TEXT("[Worldseed] regles : %d cle(s) demandee(s) et ABSENTE(S) du fichier ; ")
		TEXT("la valeur codee en dur a servi a la place."), Triees.Num());
	for (const FString& K : Triees)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Worldseed]   manquante : %s"), *K);
	}
}

double UWorldseedRules::Num(const FString& Section, const FString& Key, double Fallback) const
{
	const TSharedPtr<FJsonObject>* Obj = FindSection(Section);
	if (!Obj || !(*Obj).IsValid())
	{
		NoteMissing(Section, Key);
		return Fallback;
	}

	double Value = Fallback;
	if ((*Obj)->TryGetNumberField(Key, Value))
	{
		return Value;
	}

	NoteMissing(Section, Key);
	return Fallback;
}

int32 UWorldseedRules::Int(const FString& Section, const FString& Key, int32 Fallback) const
{
	return static_cast<int32>(FMath::RoundToDouble(
		Num(Section, Key, static_cast<double>(Fallback))));
}

FString UWorldseedRules::Str(const FString& Section, const FString& Key,
	const FString& Fallback) const
{
	const TSharedPtr<FJsonObject>* Obj = FindSection(Section);
	if (!Obj || !(*Obj).IsValid())
	{
		NoteMissing(Section, Key);
		return Fallback;
	}

	FString Value;
	if ((*Obj)->TryGetStringField(Key, Value))
	{
		return Value;
	}

	NoteMissing(Section, Key);
	return Fallback;
}

float WorldseedVerticalScale(const UWorldseedRules& Rules, float MapHeightM)
{
	const float ReferenceHeightM = static_cast<float>(
		Rules.Num(TEXT("world"), TEXT("sizeKm"), 8.0) * 1000.0);
	return (ReferenceHeightM > 1.0f)
		? FMath::Clamp(MapHeightM / ReferenceHeightM, 0.05f, 4.0f) : 1.0f;
}
