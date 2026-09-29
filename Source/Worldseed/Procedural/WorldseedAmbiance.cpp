// Worldseed - quel SON D'AMBIANCE convient a l'endroit ou l'on se trouve.
//
// SEULE IMPLEMENTATION : il n'existe pas d'original ailleurs.

#include "Procedural/WorldseedAmbiance.h"

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedRules.h"

namespace
{
	const FString Vide;

	/** La ou le pack range sa seule ambiance ; nos trois dosages sont a nous. */
	const TCHAR* const DefautLisiere = TEXT("/Game/Worldseed/Sound/EA_Worldseed_Lisiere");
	const TCHAR* const DefautClaire = TEXT("/Game/Worldseed/Sound/EA_Worldseed_ForetClaire");
	const TCHAR* const DefautDense = TEXT("/Game/Worldseed/Sound/EA_Worldseed_ForetDense");
}

FWorldseedAmbianceRegles FWorldseedAmbianceRegles::FromRules(const UWorldseedRules& Rules)
{
	FWorldseedAmbianceRegles R;
	R.FonduS = static_cast<float>(Rules.Num(TEXT("uds"), TEXT("ambianceFonduS"), R.FonduS));
	R.CheminLisiere = Rules.Str(TEXT("uds"), TEXT("ambianceLisiere"), DefautLisiere);
	R.CheminForetClaire = Rules.Str(TEXT("uds"), TEXT("ambianceForetClaire"), DefautClaire);
	R.CheminForetDense = Rules.Str(TEXT("uds"), TEXT("ambianceForetDense"), DefautDense);
	return R;
}

const FString& FWorldseedAmbianceRegles::Chemin(EWorldseedAmbiance Famille) const
{
	switch (Famille)
	{
	case EWorldseedAmbiance::Lisiere:      return CheminLisiere;
	case EWorldseedAmbiance::ForetClaire:  return CheminForetClaire;
	case EWorldseedAmbiance::ForetDense:   return CheminForetDense;
	default:                               return Vide;
	}
}

EWorldseedAmbiance WorldseedAmbiance::Choisir(EWorldseedBiome Biome,
	EWorldseedCover Couverture)
{
	// --- LA COUVERTURE D'ABORD, ET C'EST ELLE QUI PEUT TOUT ANNULER ---------
	//
	// Le biome climatique est defini PARTOUT, mer comprise : au large d'une
	// foret tropicale il vaut « foret tropicale ». Sans cette garde on
	// entendrait les merles en nageant, et ce depot connait ce piege par coeur
	// -- il a deja fait annoncer a la nappe RVT « 8 388 608 terre / 0 mer » sur
	// un monde couvert d'ocean a 71 %.
	switch (Couverture)
	{
	case EWorldseedCover::Ocean:
	case EWorldseedCover::Lake:
	case EWorldseedCover::River:
	case EWorldseedCover::SeaIce:
		return EWorldseedAmbiance::Aucune;

	case EWorldseedCover::Rock:
		// UNE PAROI N'EST PAS UNE CLAIRIERE, mais elle n'est pas non plus hors
		// du monde : un versant raide au milieu d'une foret tropicale reste
		// climatiquement de la foret, et les oiseaux du versant d'en face
		// s'entendent. On garde donc le biome, d'un cran plus bas -- c'est
		// exactement le raisonnement qui a fait de `Rock` une COUVERTURE et non
		// un biome.
		break;

	case EWorldseedCover::Beach:
		// L'ESTRAN SE TAIT, faute de la bonne source. Le pack ne livre aucun
		// son de ressac ni d'oiseau de mer : jouer la foret sur le sable ferait
		// chanter des passereaux au bord de l'eau. Le vent et la pluie, eux,
		// continuent -- ils ne dependent pas de l'ambiance.
		return EWorldseedAmbiance::Aucune;

	default:
		break;
	}

	const EWorldseedAmbiance Famille = [Biome]()
	{
		switch (Biome)
		{
		// --- LA FORET DENSE, la ou la source du pack est chez elle ----------
		case EWorldseedBiome::TropicalRainforest:
		case EWorldseedBiome::TemperateRainforest:
		case EWorldseedBiome::TemperateForest:
		case EWorldseedBiome::SubtropicalForest:
			return EWorldseedAmbiance::ForetDense;

		// --- LA FORET CLAIRE ------------------------------------------------
		//
		// LA TAIGA EN FAIT PARTIE, ET C'EST UN CHOIX QU'IL FAUT DIRE : la
		// source du pack est une foret de FEUILLUS, et une pessiere boreale ne
		// sonne pas ainsi -- moins d'especes, un vent plus sec dans les
		// aiguilles. Jouee a volume reduit elle reste defendable, et c'est tout
		// ce que le pack permet. Le jour ou une source boreale existera, c'est
		// ici qu'elle se branchera.
		case EWorldseedBiome::Taiga:
		case EWorldseedBiome::TropicalDryForest:
		case EWorldseedBiome::Marsh:
		case EWorldseedBiome::Mediterranean:
			return EWorldseedAmbiance::ForetClaire;

		// --- LA LISIERE : de l'herbe, quelques bosquets, des oiseaux au loin
		case EWorldseedBiome::Savanna:
		case EWorldseedBiome::Grassland:
		case EWorldseedBiome::Steppe:
			return EWorldseedAmbiance::Lisiere;

		// --- ET LE SILENCE, qui est la reponse juste et non un trou ---------
		//
		// Calotte, toundra, alpin, deserts chaud et froid, roche nue, et l'eau.
		// Aucun de ces endroits n'a d'arbres : y jouer du vent dans les
		// branches serait un mensonge qu'on entendrait tout de suite. La
		// TOUNDRA est le cas qui se discute -- elle a de vrais oiseaux l'ete --
		// mais elle se definit par l'ABSENCE d'arbres, ce qui est precisement
		// ce que la source du pack fait entendre.
		default:
			return EWorldseedAmbiance::Aucune;
		}
	}();

	// UNE PAROI DESCEND D'UN CRAN plutot que de se taire : moins de sous-bois,
	// donc moins d'oiseaux, mais le versant d'en face s'entend encore.
	if (Couverture == EWorldseedCover::Rock)
	{
		switch (Famille)
		{
		case EWorldseedAmbiance::ForetDense:  return EWorldseedAmbiance::ForetClaire;
		case EWorldseedAmbiance::ForetClaire: return EWorldseedAmbiance::Lisiere;
		default:                              return EWorldseedAmbiance::Aucune;
		}
	}

	return Famille;
}

const TCHAR* WorldseedAmbiance::Nom(EWorldseedAmbiance Famille)
{
	switch (Famille)
	{
	case EWorldseedAmbiance::Lisiere:     return TEXT("lisiere");
	case EWorldseedAmbiance::ForetClaire: return TEXT("foret claire");
	case EWorldseedAmbiance::ForetDense:  return TEXT("foret dense");
	case EWorldseedAmbiance::Aucune:      return TEXT("aucune");

	// ⚠ `Count` N'EST PAS `Aucune`, ET LE JOURNAL DOIT LES SEPARER. Tant que
	// les deux rendaient « aucune », le tout premier passage dans un desert
	// ecrivait « ambiance « aucune » -> « aucune » » : cela se lit comme un
	// appel inutile alors que c'est la POSE INITIALE, celle qui arrete une
	// ambiance que le pack aurait pu avoir assignee dans la carte. Du bruit
	// qu'on lit comme une information, une fois de plus.
	default:                              return TEXT("jamais posee");
	}
}
