// Worldseed - ce qu'un monde calcule transporte d'une etape a l'autre.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedPlateau.h"
#include "Procedural/WorldseedCaves.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedTexturePack.h"

/**
 * Le monde, tel qu'il voyage entre la generation, le cache disque, le menu et
 * le terrain.
 *
 * POURQUOI UNE STRUCTURE PLUTOT QUE DES PARAMETRES. Chaque grandeur ajoutee au
 * monde devait sinon l'etre a quatre signatures et a tous leurs appelants. Ici
 * une grandeur s'ajoute a un seul endroit, et les etapes qui ne la connaissent
 * pas la transportent sans y toucher.
 */
struct WORLDSEED_API FWorldseedWorldData
{
	FWorldseedGeometry Geometry;
	int32 Seed = 0;

	/** Altitude en metres, 0 = niveau de la mer. Indexe J * NX + I. */
	TArray<float> ElevationM;

	/** Moyenne annuelle au sol, en degres. */
	TArray<float> TempC;

	/** Cumul annuel de precipitations, en millimetres. */
	TArray<float> PrecipMm;

	/**
	 * Ecart entre le mois le plus froid et le plus chaud, en degres.
	 *
	 * TRANSPORTE PLUTOT QUE RECALCULE. Sa formule depend de la CONTINENTALITE,
	 * que seul le modele climatique connait : la deviner a l'arrivee donnait
	 * des saisons uniformes alors qu'un coeur de continent doit geler l'hiver
	 * la ou une cote a la meme latitude reste douce.
	 */
	TArray<float> SeasonalAmpC;

	/**
	 * Continentalite : 0 au bord de mer, 1 loin des cotes.
	 *
	 * Comme l'amplitude, elle est TRANSPORTEE plutot que recalculee : elle
	 * demande une transformee de distance sur toute la grille, trop chere a
	 * l'arrivee, et c'est elle qui ouvre l'ecart jour/nuit des climats
	 * continentaux.
	 */
	TArray<float> Continentality;

	/**
	 * De quelle roche est fait le sous-sol, par cellule.
	 *
	 * TRANSPORTEE PLUTOT QUE RECALCULEE, comme l'amplitude saisonniere et la
	 * continentalite, et pour la meme raison : elle depend de grandeurs que
	 * SEULE la tectonique connait -- croute continentale ou oceanique, et
	 * convergence des plaques -- et le cache ne les porte pas. La recalculer a
	 * l'arrivee demanderait de rejouer la tectonique entiere.
	 */
	TArray<uint8> LithologyId;

	/** Les biomes. Recalcules a chaque chargement plutot que serialises. */
	FWorldseedBiomeMap Biomes;

	/**
	 * Le reseau de cavites, SERIALISE.
	 *
	 * SIGNALE EN JEU : « malgre que la carte soit dans le cache, elle met
	 * beaucoup de temps a s'afficher sur le globe ». Chronometre par passe, le
	 * retour au menu coutait 14,9 s dont **11,2 rien que pour rebatir ce
	 * reseau** -- soit soixante-quinze pour cent d'une attente que le mot
	 * « cache » laissait croire nulle. La lecture du fichier, elle, prend
	 * 456 ms.
	 *
	 * POURQUOI IL NE L'ETAIT PAS, ET POURQUOI L'ARGUMENT NE TIENT PLUS. Le
	 * registre justifiait de le rebatir par « il est reconstruit a chaque
	 * chargement, donc aucun impact sur le cache ». C'etait vrai quand on ne
	 * payait ce prix qu'une fois au lancement ; cela ne l'est plus des qu'on
	 * fait des allers-retours entre le menu et la partie.
	 *
	 * ET IL EST MINUSCULE : 155 chambres, 177 liaisons, 8 413 troncons,
	 * 24 arches sur le monde de reference -- quelques centaines de kilo-octets
	 * a cote des dix-huit megaoctets du cache. L'index spatial, lui, n'est PAS
	 * ecrit : il est derive, il pese plus que ce qu'il indexe, et il se refait
	 * en quelques millisecondes par `ReconstruireIndex`.
	 */
	FWorldseedCaveNetwork Caves;

	/**
	 * Pack de textures choisi dans le menu.
	 *
	 * Ce n'est pas une donnee du monde mais un CHOIX D'AFFICHAGE : deux parties
	 * sur la meme graine produisent le meme relief et le meme climat, et seul
	 * l'habillage differe. Il voyage ici parce que c'est le menu qui le connait
	 * et le terrain qui l'applique.
	 */
	EWorldseedTexturePack TexturePack = EWorldseedTexturePack::BiomeColour;

	/**
	 * Ou le joueur a demande a naitre, en METRES sur la carte.
	 *
	 * Comme le pack de textures, ce n'est pas une donnee du monde mais un
	 * CHOIX : deux parties sur la meme graine produisent le meme relief, et
	 * seul le point d'arrivee differe. Il voyage ici parce que c'est le menu
	 * qui le connait et le terrain qui l'applique.
	 *
	 * EN METRES ET NON EN CELLULE : le menu genere a la resolution qu'il veut,
	 * et une cellule ne veut rien dire sans la grille qui va avec. Les metres
	 * survivent a tout changement de resolution.
	 */
	FVector2D SpawnXYM = FVector2D::ZeroVector;

	/**
	 * Faux si le joueur n'a rien choisi, et c'est le cas par DEFAUT.
	 *
	 * Le terrain retombe alors sur ce qu'il a toujours fait -- terre emergee
	 * la plus proche, puis sol plat. Un drapeau plutot qu'une position
	 * sentinelle : (0, 0) est un point parfaitement valide de cette carte.
	 */
	bool bHasSpawn = false;

	/**
	 * Les tables et les canyons, PORTES et non recalcules.
	 *
	 * ILS NE SE SERIALISENT PAS -- le cache n'en ecrit pas un octet -- mais ils
	 * voyagent avec le monde en memoire, et c'est tout l'objet. `Sites` est une
	 * fonction pure du relief fini : la rejouer donne exactement le meme
	 * resultat, et elle coute **2,8 secondes** sur la grille du jeu.
	 *
	 * ELLE ETAIT JOUEE DEUX FOIS, et c'est ce qui a fait ecrire ces deux
	 * champs : une fois par la chaine -- qui en a besoin pour la liste des
	 * lieux du menu -- et une fois par l'acteur voxel, qui les recalculait pour
	 * son propre compte. Le journal montrait les deux blocs identiques a la
	 * suite. Les porter coute quelques centaines d'octets en memoire et rend
	 * ces 2,8 secondes au chargement.
	 */
	TArray<FWorldseedPlateauSite> Tables;
	TArray<FWorldseedPlateauSite> Canyons;

	int32 CellCount() const { return Geometry.CellCount(); }

	bool HasClimate() const
	{
		const int32 N = CellCount();
		return N > 0 && TempC.Num() == N && PrecipMm.Num() == N;
	}

	bool IsValid() const
	{
		return Geometry.NX >= 2 && ElevationM.Num() == CellCount();
	}

	/**
	 * Ce que pesent les grands tableaux, en octets.
	 *
	 * IL EXISTE POUR QU'UNE DUPLICATION SE VOIE. Le monde etait recopie deux
	 * fois -- l'instance de jeu, le terrain, puis l'acteur voxel -- et rien ne
	 * le disait : la memoire du processus derive de plusieurs centaines de
	 * megaoctets d'un lancement a l'autre, donc deux cent vingt de trop s'y
	 * noient. Ce chiffre-la, lui, est DETERMINISTE, et le releve le porte a
	 * cote du nombre de porteurs.
	 *
	 * « Approximatif » parce qu'il ignore les petits champs et le bourrage :
	 * ce qu'on veut savoir est l'ordre de grandeur des tableaux, pas le poids
	 * exact de la structure.
	 */
	int64 OctetsApprox() const
	{
		auto Poids = [](const auto& A) -> int64
		{
			return static_cast<int64>(A.Num()) * sizeof(typename
				TRemoveReference<decltype(A)>::Type::ElementType);
		};
		return Poids(ElevationM) + Poids(TempC) + Poids(PrecipMm)
			+ Poids(SeasonalAmpC) + Poids(Continentality) + Poids(LithologyId)
			+ Poids(Biomes.Index) + Poids(Biomes.Cover) + Poids(Biomes.SlopeDeg);
	}
};

/**
 * LE MONDE, PARTAGE ET IMMUABLE.
 *
 * POURQUOI. Le monde etait COPIE a chaque etape -- le menu le rangeait dans
 * l'instance de jeu, le terrain l'en sortait, puis `AdoptWorld` le recopiait
 * dans l'acteur voxel. Sur une grille 4096x2048, les sept grands tableaux
 * pesent cent quatre-vingt-treize megaoctets, et deux acteurs en tenaient
 * chacun un jeu complet.
 *
 * ET SURTOUT, IL N'AVAIT PAS DE PROPRIETAIRE CLAIR. Le champ de densite est un
 * membre PAR VALEUR de l'acteur voxel, et chaque travail de maillage en
 * capture l'ADRESSE. `EndPlay` annule les travaux sans les attendre -- ce qui
 * est le bon choix, attendre bloquerait la fermeture -- donc un fil peut lire
 * cette memoire pendant que le ramasse-miettes s'apprete a la liberer. La
 * fenetre est courte, mais c'est la forme exacte d'un plantage rare au
 * changement de niveau : celui qu'on ne reproduit jamais.
 *
 * UNE REFERENCE PARTAGEE REPOND AUX DEUX A LA FOIS. Le travail en tient une,
 * donc la donnee lui survit par construction, et il n'y a plus qu'un seul
 * exemplaire en memoire. C'est le meme raisonnement que pour les primitives de
 * grottes, deja resolu ainsi : « le fil de maillage ne doit rien tenir qui
 * puisse mourir avant lui ».
 *
 * IMMUABLE PAR LE TYPE, ET CE N'EST PAS DECORATIF. `const` interdit qu'un
 * porteur modifie ce que les autres lisent -- ce qui, entre fils, serait une
 * course. Un monde se CONSTRUIT une fois, puis ne fait plus que se lire ; le
 * type le dit, et le compilateur le verifie.
 */
using FWorldseedMondeRef = TSharedRef<const FWorldseedWorldData, ESPMode::ThreadSafe>;
using FWorldseedMondePtr = TSharedPtr<const FWorldseedWorldData, ESPMode::ThreadSafe>;
