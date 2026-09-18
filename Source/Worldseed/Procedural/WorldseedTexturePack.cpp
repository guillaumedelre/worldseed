// Worldseed - les jeux de textures de sol proposes au joueur.

#include "Procedural/WorldseedTexturePack.h"

#define LOCTEXT_NAMESPACE "Worldseed"

namespace WorldseedTexturePack
{
	FText Label(EWorldseedTexturePack Pack)
	{
		switch (Pack)
		{
		case EWorldseedTexturePack::BiomeColour: return LOCTEXT("PackNone", "Couleurs de biome");
		case EWorldseedTexturePack::Dreamscape:  return LOCTEXT("PackDream", "Dreamscape");
		case EWorldseedTexturePack::Village:     return LOCTEXT("PackVillage", "Village");
		case EWorldseedTexturePack::Egypt:       return LOCTEXT("PackEgypt", "Egypte");
		case EWorldseedTexturePack::Mixed:       return LOCTEXT("PackMixed", "Melange");
		default:                                 return FText::GetEmpty();
		}
	}

	FText Description(EWorldseedTexturePack Pack)
	{
		switch (Pack)
		{
		case EWorldseedTexturePack::BiomeColour:
			return LOCTEXT("PackNoneDesc", "Les 19 biomes a plat. Aucune texture, lisible partout.");
		case EWorldseedTexturePack::Dreamscape:
			return LOCTEXT("PackDreamDesc", "Herbe, terre, sable, roche. Pas de mousse.");
		case EWorldseedTexturePack::Village:
			return LOCTEXT("PackVillageDesc", "Herbe, roche, mousse. Pas de sable ni de terre.");
		case EWorldseedTexturePack::Egypt:
			return LOCTEXT("PackEgyptDesc", "Sable, roche, mousse. Pas d'herbe : mondes arides.");
		case EWorldseedTexturePack::Mixed:
			return LOCTEXT("PackMixedDesc", "Les quatre matieres, puisees dans les trois packs.");
		default:
			return FText::GetEmpty();
		}
	}
}

#undef LOCTEXT_NAMESPACE
