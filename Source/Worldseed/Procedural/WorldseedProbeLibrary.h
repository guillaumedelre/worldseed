// Worldseed - points d'entree de verification, appelables sans lancer le jeu.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "WorldseedProbeLibrary.generated.h"

/**
 * Sondes de verification.
 *
 * POURQUOI CE FICHIER. La chaine de generation ne tourne qu'en jeu, derriere le
 * menu : verifier une etape demandait de lancer l'editeur, cliquer, attendre,
 * puis lire les journaux. Ces fonctions donnent le meme resultat depuis un
 * script, en quelques secondes — ce qui change la boucle de travail du tout au
 * tout quand on porte un algorithme et qu'on veut savoir s'il produit
 * les bons nombres.
 *
 * Elles ne servent qu'au diagnostic : rien du jeu n'en depend.
 */
UCLASS()
class WORLDSEED_API UWorldseedProbeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Chronometre le rendu du globe d'apercu.
	 *
	 * La taille de la texture ne change pas avec celle du monde : si le temps
	 * par image grimpe quand meme, c'est que le cout ne vient pas du nombre de
	 * pixels mais des ACCES au heightfield.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeGlobe(int32 Seed = 20260909, float HeightMeters = 8000.0f,
		int32 ResolutionY = 256, int32 Frames = 20);

	/**
	 * Maille des chunks de voxels et rend ce qu'ils ont coute.
	 *
	 * C'EST LA MESURE QUI DECIDE DE LA TAILLE DU VOXEL ET DU CHUNK, et elle
	 * vient avant tout le reste : streaming, collision et creusement se
	 * dimensionnent sur elle. Sans moteur de rendu, sans acteur, sans PIE --
	 * donc reproductible et comparable d'une session a l'autre.
	 *
	 * Les chunks sont pris en tuile autour d'un point de TERRE, sans quoi on
	 * mesurerait le cout du vide : au-dessus de l'ocean il n'y a pas de
	 * surface a mailler, et le releve serait flatteur autant qu'inutile.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeVoxel(int32 Seed = 20260909, float HeightMeters = 8000.0f,
		int32 ResolutionY = 256, int32 ChunkSideM = 32, int32 ChunksPerSide = 4);

	/**
	 * Mesure ce que le bruit 3D produit reellement : galeries et surplombs.
	 *
	 * DEUX GRANDEURS, ET ELLES SE SUFFISENT.
	 *
	 * Un SURPLOMB, c'est par definition une colonne que la surface traverse
	 * plus d'une fois : on compte donc les changements de signe du champ le
	 * long de chaque verticale. Une GALERIE, c'est de l'air sous la surface :
	 * on compte la part des points de la bande ou le champ est positif.
	 *
	 * Rien de tout cela ne se voit a l'oeil -- une grotte est sous terre et il
	 * y fait noir -- d'ou cette sonde. Elle relit les regles du disque a chaque
	 * appel, pour qu'un essai coute une seconde et non un redemarrage.
	 */
	/**
	 * Rend la part des terres que porte CHAQUE CASE du diagramme de Whittaker.
	 *
	 * POURQUOI CETTE SONDE. Le tableau des biomes donne la part de chaque NOM,
	 * or plusieurs noms couvrent plusieurs cases : "desert froid" s'etale sur
	 * trois bandes de temperature, "foret temperee" sur deux. Une case qui ne
	 * porte rien ne merite pas un nom ; une case qui porte cinq pour cent des
	 * terres sous le nom d'une autre est une erreur de vocabulaire. La part par
	 * NOM ne permet de trancher ni l'un ni l'autre.
	 *
	 * Elle relit les regles du disque, comme les autres : retoucher un seuil du
	 * diagramme et remesurer doit couter une seconde.
	 */
	/**
	 * Rend la part des terres de CHAQUE biome, et de chaque substrat.
	 *
	 * Le journal de la chaine n'en donne que les trois premiers -- de quoi voir
	 * si la carte est plausible, pas de quoi caler un seuil. Celle-ci donne le
	 * tableau entier, trie, avec la reference terrestre en regard quand elle
	 * existe. C'est le juge des reglages de biomes : on change un seuil dans
	 * les regles, on relance, on compare.
	 */
	/**
	 * Verifie les deux champs continus du sol.
	 *
	 * LE CONTROLE QUI COMPTE EST CELUI DE L'HEMISPHERE. Un champ d'ensoleillement
	 * se trompe silencieusement d'un signe : les chiffres restent plausibles,
	 * l'adret se retrouve simplement du mauvais cote, et rien ne le signale --
	 * sauf a comparer les DEUX hemispheres, ou l'asymetrie doit s'inverser. Elle
	 * ne s'inverse pas si le signe est faux, et c'est immediat a lire.
	 */
	/**
	 * Verifie la lithologie : les parts, et surtout leur PLACE.
	 *
	 * Les parts seules ne prouvent rien -- un tirage au hasard donnerait les
	 * memes. Ce qui tranche est le croisement avec l'altitude et la mer : le
	 * basalte doit etre sous l'eau, le granite en hauteur, le calcaire et le
	 * gres dans les bas pays. Une erreur d'attribution se lit immediatement
	 * dans ce tableau, et nulle part ailleurs.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeLithology(int32 Seed = 20260909, float HeightMeters = 8000.0f,
		int32 ResolutionY = 1024);

	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeGroundFields(int32 Seed = 20260909, float HeightMeters = 8000.0f,
		int32 ResolutionY = 1024);

	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeBiomes(int32 Seed = 20260909, float HeightMeters = 8000.0f,
		int32 ResolutionY = 1024);

	/**
	 * Le BULLETIN DE CONFORMITE TERRESTRE, porte du Python vers le C++.
	 *
	 * Un monde procedural peut etre coherent avec lui-meme et faux par rapport
	 * a la Terre : rien, dans la chaine, ne l'empeche de produire des deserts a
	 * l'equateur. C'est le seul controle qui confronte le monde a des valeurs
	 * EXTERIEURES au projet.
	 *
	 * Deux controles distincts. Vingt-trois climats de VILLES REELLES passes
	 * dans notre diagramme -- celui-la juge le CLASSIFICATEUR, pas le monde.
	 * Puis le bulletin sur des criteres SOURCES, jamais un pourcentage de biome
	 * sorti de memoire.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeTerre(int32 Seed = 20260909, float HeightMeters = 32000.0f,
		int32 ResolutionY = 1024);

	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeWhittaker(int32 Seed = 20260909, float HeightMeters = 8000.0f,
		int32 ResolutionY = 1024);

	/**
	 * Y a-t-il des LAMES de roche assez minces pour porter une arche ?
	 *
	 * C'EST LA SEULE QUESTION QUI DECIDE DES ARCHES, et elle avait ete tranchee
	 * par la negative sur le monde de 16 x 8 km : zero site sur 402 points
	 * emerges. Une arche est une ouverture TRAVERSANTE sous un pont de roche ;
	 * percer une colline de deux cents metres ne donne pas une arche, ca donne
	 * un tunnel. Il faut donc d'abord une crete mince.
	 *
	 * La mesure interroge le CHAMP REEL et non la grille macro : le relief de
	 * simulation a 31 m de maille sur une grande carte, et une lame de soixante
	 * metres y tient dans deux cellules. Si une lame existe, elle vient de la
	 * couche voxel, qui travaille au metre.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeArches(int32 Seed = 20260909, float HeightMeters = 32000.0f,
		int32 ResolutionY = 1024, int32 Sites = 400, float SousLeSommetM = 20.0f,
		float LargeurMaxM = 80.0f);

	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeCaves(int32 Seed = 20260909, float HeightMeters = 8000.0f,
		int32 ResolutionY = 1024, float AreaM = 512.0f, float StepM = 4.0f,
		bool bSteepest = false);

	/**
	 * Le plateau disseque : mesas et canyons.
	 *
	 * TEMOIN SPATIAL, dans le MEME monde : les cellules en zone contre celles
	 * qui remplissent tous les autres criteres -- roche, aridite, relief,
	 * altitude -- et que seul le masque de region a laissees dehors. Comparer
	 * deux generations demanderait de changer une regle entre les deux, et le
	 * depot a paye ce piege : le PIE ne relit pas world_rules.json, si bien
	 * qu'un temoin ainsi obtenu mesure exactement le cas teste.
	 *
	 * LA MESURE QUI TRANCHE est l'ecart-type des altitudes de sommets par
	 * bloc. Une colline a sommet plat existe partout ; ce qui fait une TABLE,
	 * c'est que les sommets voisins partagent une altitude, parce qu'ils sont
	 * les morceaux d'une seule ancienne surface.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeTables(int32 Seed = 20260909, float HeightMeters = 32000.0f,
		int32 ResolutionY = 1024);
};
