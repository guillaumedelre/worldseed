// Worldseed - chargement de la police d'icones et catalogue des glyphes.

#include "Procedural/WorldseedIcones.h"

#include "Fonts/CompositeFont.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"

/**
 * UN NAMESPACE NOMME. UBT concatene les .cpp en une seule unite de traduction,
 * ou deux namespaces ANONYMES n'en font qu'un : ce depot a casse quatre fois
 * sur ce piege -- WorldseedMetersToCm, SUB, CompterNonFinis, et les trois
 * derniers etaient dans des fichiers que personne n'avait touches.
 */
namespace WorldseedIconesInterne
{
	/** Le dossier est versionne (.gitignore : Content/* puis !Content/Worldseed/). */
	const TCHAR* const NomFichier = TEXT("Worldseed/Fonts/MaterialSymbolsOutlined.ttf");
	const TCHAR* const NomCatalogue = TEXT("Worldseed/Fonts/MaterialSymbols.codepoints");

	/**
	 * ELLE EST CHARGEE UNE FOIS ET GARDEE, et ce n'est pas qu'une economie :
	 * `FSlateFontInfo` retient la police composite, et le cache de glyphes de
	 * Slate est indexe par cette instance. En reconstruire une par appel
	 * rebatirait l'atlas a chaque trame.
	 */
	TSharedPtr<FStandaloneCompositeFont> PolicePartagee;
	bool bTentee = false;

	void Charger()
	{
		bTentee = true;

		const FString Chemin = FPaths::Combine(FPaths::ProjectContentDir(), NomFichier);
		if (!IFileManager::Get().FileExists(*Chemin))
		{
			// ON LE DIT UNE FOIS, ET ON CONTINUE. Un ecran de carres vides
			// ressemble a un defaut de mise en page ; cette ligne dit que c'est
			// un fichier absent, et lequel.
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] police d'icones INTROUVABLE : %s -- les icones ")
				TEXT("seront des carres vides. Voir Content/Worldseed/Fonts/PROVENANCE.md."),
				*Chemin);
			return;
		}

		PolicePartagee = MakeShared<FStandaloneCompositeFont>();

		// `LazyLoad` : le fichier n'est lu qu'au premier glyphe demande. Il pese
		// dix megaoctets, et un ecran qui n'affiche aucune icone ne les paie pas.
		PolicePartagee->DefaultTypeface.AppendFont(TEXT("Icones"), Chemin,
			EFontHinting::Default, EFontLoadingPolicy::LazyLoad);

		UE_LOG(LogTemp, Log, TEXT("[Worldseed] police d'icones : %s (%.1f Mo)"),
			*FPaths::GetCleanFilename(Chemin),
			IFileManager::Get().FileSize(*Chemin) / (1024.0 * 1024.0));
	}

	/**
	 * LE CATALOGUE, ET IL EST ORDONNE COMME LE ROLE, PAS COMME LE NOM GOOGLE.
	 * C'est la liste que l'oracle confronte au `.codepoints` livre.
	 */
	const WorldseedIcones::FEntree Entrees[] =
	{
		{ TEXT("place"),        WorldseedIcone::Repere   },
		{ TEXT("navigation"),   WorldseedIcone::Joueur   },
		{ TEXT("all_inclusive"), WorldseedIcone::Arche  },
		{ TEXT("trip_origin"),  WorldseedIcone::Gouffre  },
		{ TEXT("landslide"),    WorldseedIcone::Doline   },
		{ TEXT("layers"),       WorldseedIcone::Table    },
		{ TEXT("terrain"),      WorldseedIcone::Canyon   },
		{ TEXT("map"),          WorldseedIcone::Carte    },
		{ TEXT("play_arrow"),   WorldseedIcone::Jouer    },
		{ TEXT("casino"),       WorldseedIcone::Des      },
		{ TEXT("autorenew"),    WorldseedIcone::Generer  },
		{ TEXT("close"),        WorldseedIcone::Annuler  },
		{ TEXT("delete"),       WorldseedIcone::Effacer  },
		{ TEXT("public"),       WorldseedIcone::Globe    },
		{ TEXT("settings"),     WorldseedIcone::Reglages },
		{ TEXT("warning"),      WorldseedIcone::Alerte   },
	};
}


namespace WorldseedIcones
{

FSlateFontInfo Police(float TaillePt)
{
	using namespace WorldseedIconesInterne;

	if (!bTentee)
	{
		Charger();
	}

	if (!PolicePartagee.IsValid())
	{
		// Le repli rend du TEXTE, donc les codepoints sortiront en carres --
		// mais la mise en page tient, et le journal a deja dit pourquoi.
		return FCoreStyle::GetDefaultFontStyle("Regular", TaillePt);
	}

	return FSlateFontInfo(PolicePartagee, TaillePt);
}


bool PoliceDisponible()
{
	using namespace WorldseedIconesInterne;

	if (!bTentee)
	{
		Charger();
	}
	return PolicePartagee.IsValid();
}


FString CheminPolice()
{
	return FPaths::Combine(FPaths::ProjectContentDir(),
		WorldseedIconesInterne::NomFichier);
}


FString CheminCodepoints()
{
	return FPaths::Combine(FPaths::ProjectContentDir(),
		WorldseedIconesInterne::NomCatalogue);
}


FText Glyphe(uint32 Codepoint)
{
	// AU-DELA DU PLAN DE BASE, IL FAUT UNE PAIRE DE SUBSTITUTION. Sur Windows
	// un TCHAR fait seize bits : ecrire 0xFFFA2 tel quel donnerait 0xFFA2, un
	// autre glyphe, sans la moindre erreur.
	if (Codepoint > 0xFFFF)
	{
		const uint32 V = Codepoint - 0x10000;
		const TCHAR Paire[3] = {
			static_cast<TCHAR>(0xD800 + (V >> 10)),
			static_cast<TCHAR>(0xDC00 + (V & 0x3FF)),
			0
		};
		return FText::FromString(FString(Paire));
	}

	const TCHAR Un[2] = { static_cast<TCHAR>(Codepoint), 0 };
	return FText::FromString(FString(Un));
}


TConstArrayView<FEntree> Catalogue()
{
	return TConstArrayView<FEntree>(WorldseedIconesInterne::Entrees,
		UE_ARRAY_COUNT(WorldseedIconesInterne::Entrees));
}

} // namespace WorldseedIcones
