// Worldseed - le niveau de qualite du rendu, et l'endroit ou un menu se branchera.

#include "Procedural/WorldseedQualiteRendu.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Scalability.h"

void UWorldseedQualiteRendu::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	int32 Niveau = NiveauParDefaut;

	int32 Impose = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedQualite="), Impose)
		&& Impose >= 0)
	{
		Niveau = FMath::Clamp(Impose, 0, static_cast<int32>(ENiveau::Cinematique));
	}

	// RIEN N'EST APPLIQUE SANS DEMANDE EXPLICITE. Le defaut laisse le rendu
	// exactement tel que le moteur l'a etabli -- c'est l'arbitrage du
	// proprietaire, et il faut qu'il soit VRAI, pas approximativement vrai :
	// un sous-systeme qui « n'applique presque rien » deplacerait quand meme
	// les niveaux au premier demarrage, la ou ils n'ont pas encore ete lus.
	if (Niveau < 0)
	{
		return;
	}

	Appliquer(Niveau);
}

void UWorldseedQualiteRendu::Appliquer(int32 Niveau)
{
	Niveau = FMath::Clamp(Niveau, 0, static_cast<int32>(ENiveau::Cinematique));

	// ON PART DE L'ETAT COURANT ET L'ON NE DEPLACE QUE DEUX GROUPES.
	//
	// `SetFromSingleQualityLevel` aurait ecrase les huit d'un coup, y compris
	// ceux que la mesure declare sans interet -- ombres, post-traitement,
	// anticrenelage, distance de vue, textures, effets, feuillage, ombrage --
	// qui pesent ensemble 0,13 ms. Les baisser serait abimer le rendu pour
	// rien, et c'est exactement ce qu'un reglage global fait sans le dire.
	Scalability::FQualityLevels Niveaux = Scalability::GetQualityLevels();

	const int32 Avant = Niveaux.GlobalIlluminationQuality;
	Niveaux.SetGlobalIlluminationQuality(Niveau);
	Niveaux.SetReflectionQuality(Niveau);

	// `bForce` a vrai : sans lui, l'application est ignoree quand la structure
	// parait inchangee, et l'on passerait a cote au premier demarrage ou les
	// niveaux n'ont pas encore ete lus.
	Scalability::SetQualityLevels(Niveaux, /*bForce=*/true);

	static const TCHAR* const Noms[] = {
		TEXT("Bas"), TEXT("Moyen"), TEXT("Haut"), TEXT("Epique"), TEXT("Cinematique") };

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] rendu : illumination globale et reflexions au niveau %d (%s), ")
		TEXT("depuis %d. Les autres groupes sont inchanges."),
		Niveau, Noms[FMath::Clamp(Niveau, 0, 4)], Avant);
}
