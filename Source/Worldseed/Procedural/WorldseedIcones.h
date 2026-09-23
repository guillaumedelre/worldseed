// Worldseed - la police d'icones du projet, et les glyphes qu'on en emploie.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

/**
 * UNE POLICE D'ICONES, PAS DES IMAGES.
 *
 * Un glyphe est vectoriel : net a toutes les tailles et a tous les facteurs
 * DPI, teintable comme du texte, et il ne coute ni asset, ni atlas, ni import.
 * Une icone en PNG demanderait une texture par taille, et se retrouverait molle
 * des le premier ecran a DPI 2.
 *
 * C'EST CE QUE FAIT EPIC. L'editeur d'Unreal rend ses propres icones avec
 * `FontAwesome.ttf` et une table de glyphes nommes -- `EditorFontGlyphs.h`,
 * dont chaque entree est un `FText::FromString(FString(TEXT("\xf100")))`.
 * Ce fichier suit ce modele, avec Material Symbols a la place.
 *
 * PAS D'ASSET DE POLICE, ET C'EST DELIBERE. Un `UFont` serait un `.uasset`
 * opaque au diff ; le `.ttf` brut se lit, se remplace et porte sa licence a
 * cote. Il se charge par `FStandaloneCompositeFont`, la voie que le moteur
 * emploie lui-meme pour ses ecrans de chargement
 * (`PreLoadSettingsContainer.cpp:240`) -- les constructeurs de `FSlateFontInfo`
 * qui prennent un chemin, eux, sont deprecies depuis 5.6.
 *
 * Provenance, licence, empreintes et piege de packaging :
 * `Content/Worldseed/Fonts/PROVENANCE.md`.
 */
namespace WorldseedIcones
{
	/**
	 * La police, a la taille demandee, PRETE A ETRE POSEE.
	 *
	 * Si le fichier manque, elle rend la police par defaut du moteur apres
	 * l'avoir dit UNE fois au journal : sans cela l'ecran se remplirait de
	 * carres vides, et l'on chercherait un defaut de mise en page pour ce qui
	 * n'est qu'un fichier absent.
	 */
	WORLDSEED_API FSlateFontInfo Police(float TaillePt);

	/** Vrai si le `.ttf` a ete trouve et charge. Faux avant le premier appel. */
	WORLDSEED_API bool PoliceDisponible();

	/** Chemin du `.ttf`. Expose parce que c'est l'oracle qui le verifie. */
	WORLDSEED_API FString CheminPolice();

	/** Chemin du catalogue `nom -> codepoint` livre avec la police. */
	WORLDSEED_API FString CheminCodepoints();

	/**
	 * Le glyphe, pret pour un `STextBlock` ou un `MakeText`.
	 *
	 * IL PREND UN `uint32` ET NON UN `TCHAR`, et ce n'est pas de la prudence
	 * gratuite : le catalogue porte des codepoints au-dela du plan de base --
	 * `bus_map_pin` vaut 0xFFFA2 -- qu'un `TCHAR` de seize bits tronquerait en
	 * un autre glyphe, sans erreur ni avertissement. Au-dela de 0xFFFF on ecrit
	 * la paire de substitution.
	 */
	WORLDSEED_API FText Glyphe(uint32 Codepoint);

	/** Une icone employee : son nom au catalogue Google, et son codepoint. */
	struct WORLDSEED_API FEntree
	{
		const TCHAR* Nom = nullptr;
		uint32 Codepoint = 0;
	};

	/**
	 * Tout ce que le projet emploie, et rien d'autre.
	 *
	 * ON N'EMBARQUE PAS LES 4284 CODEPOINTS DU CATALOGUE. Cette table est ce
	 * que l'oracle confronte au `.codepoints` livre : un codepoint recopie de
	 * travers, ou une icone que Google renomme, s'y voit tout de suite.
	 */
	WORLDSEED_API TConstArrayView<FEntree> Catalogue();
}

/**
 * LES CODEPOINTS, NOMMES PAR CE QU'ILS DESIGNENT CHEZ NOUS -- et non par leur
 * nom Google, qui ne dit rien du role qu'on leur fait jouer. Le nom du
 * catalogue est en commentaire, parce que c'est lui qu'il faut pour en chercher
 * une autre.
 *
 * PLUSIEURS NOMS PARTAGENT UN GLYPHE : `place` et `location_on` valent tous
 * deux 0xF1DB, `landscape` et `terrain` tous deux 0xE564. Trouver un codepoint
 * deja employe sous un autre nom n'est donc pas une erreur.
 */
namespace WorldseedIcone
{
	// --- la carte -----------------------------------------------------------
	constexpr uint32 Repere   = 0xF1DB;   // place        -- l'epingle du joueur
	constexpr uint32 Joueur   = 0xE55D;   // navigation   -- la fleche de cap
	constexpr uint32 Arche    = 0xE77C;   // door_open    -- on passe au travers
	constexpr uint32 Gouffre  = 0xE57B;   // trip_origin  -- un puits, vu du ciel
	constexpr uint32 Doline   = 0xEBD7;   // landslide    -- un effondrement
	constexpr uint32 Table    = 0xE53B;   // layers       -- des bancs empiles
	constexpr uint32 Canyon   = 0xE564;   // terrain      -- une entaille
	constexpr uint32 Carte    = 0xE55B;   // map

	// --- le menu et l'interface --------------------------------------------
	constexpr uint32 Jouer    = 0xE037;   // play_arrow
	constexpr uint32 Des      = 0xEB40;   // casino       -- une graine au hasard
	constexpr uint32 Generer  = 0xE863;   // autorenew
	constexpr uint32 Annuler  = 0xE5CD;   // close
	constexpr uint32 Effacer  = 0xE92E;   // delete
	constexpr uint32 Globe    = 0xE80B;   // public
	constexpr uint32 Reglages = 0xE8B8;   // settings
	constexpr uint32 Alerte   = 0xF083;   // warning
}
