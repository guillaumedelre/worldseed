// Worldseed - quelle tempete convient a ce climat, en cette saison.

#include "Procedural/WorldseedOrage.h"

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedClimatePreset.h"

namespace WorldseedOrage
{
	namespace
	{
		/** Vrai pour l'eau libre, ou aucune tempete radiale n'a de sens. */
		bool EstDeLEau(EWorldseedBiome B)
		{
			return B == EWorldseedBiome::Ocean
				|| B == EWorldseedBiome::Lake
				|| B == EWorldseedBiome::River;
		}

		/** Vrai en hiver -- la seule saison qui retourne un choix. */
		bool EstLHiver(EWorldseedSeason S)
		{
			return S == EWorldseedSeason::Winter;
		}
	}

	EWorldseedOrage Choisir(EWorldseedBiome Biome, EWorldseedSeason Saison)
	{
		if (EstDeLEau(Biome))
		{
			return EWorldseedOrage::Aucune;
		}

		const bool bHiver = EstLHiver(Saison);

		switch (Biome)
		{
		// --- LE FROID PERMANENT : la saison n'y change rien ------------------
		//
		// Calotte et toundra ne connaissent pas d'ete qui degele : l'eau y tombe
		// en neige toute l'annee, et le vent de ces latitudes en fait un
		// blizzard. Poser un orage convectif sur la banquise serait la meme
		// faute que le pack commettait a l'envers.
		case EWorldseedBiome::IceCap:
		case EWorldseedBiome::Tundra:
			return EWorldseedOrage::Blizzard;

		// --- LE FROID SAISONNIER --------------------------------------------
		//
		// Taiga et etage alpin ont un ete, court mais reel, et l'orage de
		// montagne est un phenomene banal. En hiver la neige franche, sans le
		// vent du blizzard qu'on reserve aux latitudes polaires : un blizzard
		// partout diluerait ce qui doit rester rare.
		case EWorldseedBiome::Taiga:
		case EWorldseedBiome::Alpine:
			return bHiver ? EWorldseedOrage::Neige : EWorldseedOrage::Orage;

		// --- L'ARIDE : la tempete y est de SABLE, et c'est un etat de VENT ---
		//
		// Mesure du depot, sur les deux prereglages de sable LIVRES : ils posent
		// tous deux `Dust = 10`, et ce qui les separe est `Wind Intensity`, 1
		// contre 10. La tempete de sable est donc un etat de vent, la poussiere
		// en etant l'effet -- et c'est aussi la physique, le sable ne se
		// soulevant qu'au-dela d'une vitesse de friction seuil.
		//
		// LE DESERT FROID GARDE LE SABLE EN HIVER. Un desert froid est aride
		// avant d'etre froid : il n'a pas l'humidite qu'une chute de neige
		// demande, et le vent y souffle autant.
		case EWorldseedBiome::HotDesert:
		case EWorldseedBiome::ColdDesert:
			return EWorldseedOrage::Sable;

		// --- LE CONVECTIF : les chaudes et humides --------------------------
		//
		// Forets tropicales, savane et foret subtropicale sont le domaine de
		// l'orage a cellule -- c'est la que la convection est la plus forte, et
		// leur hiver n'est pas froid. La saison n'y change donc rien.
		case EWorldseedBiome::TropicalRainforest:
		case EWorldseedBiome::TropicalDryForest:
		case EWorldseedBiome::Savanna:
		case EWorldseedBiome::SubtropicalForest:
			return EWorldseedOrage::Orage;

		// --- LES TEMPERES CONTINENTAUX : orage l'ete, neige l'hiver ----------
		//
		// C'est exactement ce que le pack faisait -- et pour ces biomes-la il
		// avait RAISON. On ne change pas ce qui allait : la faute etait de
		// l'appliquer AUSSI a l'equateur et au desert.
		case EWorldseedBiome::TemperateForest:
		case EWorldseedBiome::Grassland:
		case EWorldseedBiome::Steppe:
		case EWorldseedBiome::Marsh:
		case EWorldseedBiome::BareRock:
			return bHiver ? EWorldseedOrage::Neige : EWorldseedOrage::Orage;

		// --- LES OCEANIQUES : la pluie FRONTALE, pas l'orage ----------------
		//
		// Foret temperee humide, mediterraneen et estran recoivent leur eau de
		// systemes frontaux venus de la mer, pas de la convection locale : de la
		// pluie soutenue et du vent, sans appareil electrique. C'est la
		// distinction que `Rain` et `Rain_Thunderstorm` permettent, et s'en
		// priver rendrait toutes les tempetes identiques a l'oreille comme a
		// l'oeil.
		//
		// ⚠ LES DEUX LIGNES DONT JE SUIS LE MOINS SUR sont celles-ci et le
		// `Steppe` ci-dessus : une steppe continentale connait de vrais orages,
		// mais aussi des tempetes de poussiere, et le mediterraneen a des
		// orages d'automne violents. A juger a l'usage.
		// ET LA SAISON N'Y CHANGE RIEN, a dessein : un hiver oceanique est DOUX
		// et arrose -- c'est sa definition -- donc il pleut, il ne neige pas.
		// (Un ternaire dont les deux branches sont identiques traînait ici ; il
		// est retire, parce qu'il faisait croire a une decision saisonniere
		// qu'il ne prenait pas.)
		case EWorldseedBiome::TemperateRainforest:
		case EWorldseedBiome::Mediterranean:
		case EWorldseedBiome::Beach:
			return EWorldseedOrage::Pluie;

		default:
			break;
		}

		// LE DEFAUT EST L'ORAGE, et non `Aucune` : un biome neuf ajoute en fin
		// de liste doit faire QUELQUE CHOSE plutot que rien, sinon son silence
		// se lirait comme un defaut du mecanisme. Le depot a deja paye qu'une
		// absence muette coute plus cher qu'un choix imparfait.
		return EWorldseedOrage::Orage;
	}

	const TCHAR* NomDAsset(EWorldseedOrage Orage)
	{
		switch (Orage)
		{
		case EWorldseedOrage::Orage:    return TEXT("Rain_Thunderstorm");
		case EWorldseedOrage::Pluie:    return TEXT("Rain");
		case EWorldseedOrage::Blizzard: return TEXT("Snow_Blizzard");
		case EWorldseedOrage::Neige:    return TEXT("Snow");
		case EWorldseedOrage::Sable:    return TEXT("Sand_Dust_Storm");
		default:                        return TEXT("");
		}
	}
}
