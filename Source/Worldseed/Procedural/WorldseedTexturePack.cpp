// Worldseed - les jeux de textures de sol proposes au joueur.

#include "Procedural/WorldseedTexturePack.h"

#include "Procedural/WorldseedBiomes.h"

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
			// LE COMPTE SE DERIVE DU REGISTRE, il ne se recopie pas. Cette
			// chaine annoncait « les 19 biomes » -- juste avant que
			// l.hydrologie ne parte avec trois couvertures et le marais, et
			// que la roche a nu et l.estran ne quittent l.axe des biomes pour
			// devenir des Cover. Un nombre fige dans un texte pourrit sans
			// que personne ne le voie, et c.est le proprietaire qui l.a vu.
			return FText::Format(
				LOCTEXT("PackNoneDesc",
					"Les {0} biomes climatiques a plat, plus la roche a nu et l'estran. Aucune texture, lisible partout."),
				FText::AsNumber(WorldseedBiomesClimatiques));
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
