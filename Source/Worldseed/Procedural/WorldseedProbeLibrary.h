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
	 * Confronte le mailleur Transvoxel a celui du moteur, sur les memes chunks.
	 *
	 * LA QUESTION N.EST PAS « COMBIEN CA COUTE » MAIS « EST-CE LA MEME SURFACE ».
	 * A resolution uniforme et sans aucune cellule de transition, les deux
	 * mailleurs decrivent la meme isovaleur du meme champ : ils doivent donc
	 * rendre la meme geometrie. Un mailleur maison qui deplace la surface n.est
	 * pas un mailleur, c.est un defaut -- et il empoisonnerait en silence tout ce
	 * qui viendra se poser dessus.
	 *
	 * QUATRE GRANDEURS, ET AUCUNE NE SUPPOSE QUE L.AUTRE MAILLEUR A RAISON :
	 *
	 *  - l.AIRE de la surface, qui est la grandeur geometrique a comparer. Le
	 *    nombre de triangles, lui, ne prouve rien : deux maillages corrects de la
	 *    meme surface n.ont aucune raison d.avoir le meme decoupage ;
	 *  - la DENSITE AUX SOMMETS. Chaque sommet est cense etre POSE sur
	 *    l.isovaleur zero : |densite| doit y etre quasi nulle. Ce controle-la ne
	 *    compare rien du tout, il juge chaque mailleur dans l.absolu ;
	 *  - les ARETES DE BORD, celles qui n.appartiennent qu.a un seul triangle.
	 *    Sur un chunk elles doivent toutes se trouver sur la paroi de la boite ;
	 *    ailleurs, c.est un trou. C.est le controle qui verra les fissures le
	 *    jour ou les cellules de transition arriveront ;
	 *  - l.ENROULEMENT, confronte aux normales issues du gradient.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeTransvoxel(int32 Seed = 20260909, float HeightMeters = 8000.0f,
		int32 ResolutionY = 256, int32 ChunkSideM = 32, int32 ChunksPerSide = 3);

	/**
	 * Les cellules de transition ferment-elles la fissure ?
	 *
	 * LA SEULE QUESTION QUI COMPTE POUR LE TRANSVOXEL, et elle ne se lit pas sur
	 * un chunk isole : il faut DEUX chunks de resolutions differentes, cote a
	 * cote, et regarder leur couture.
	 *
	 * Le controle est une ARETE OUVERTE sur le plan partage -- une arete qui
	 * n.appartient qu.a UN triangle alors qu.elle est a l.interieur de la
	 * surface. C.est la definition meme d.un trou, et elle ne suppose rien sur la
	 * facon dont le raccord est cense marcher.
	 *
	 * Le TEMOIN est le meme couple de chunks avec le masque de transition a ZERO,
	 * c.est-a-dire l.etat que le depot a aujourd.hui. Il doit montrer la fissure
	 * que les cellules de transition suppriment -- sans quoi on ne mesurerait pas
	 * ce qu.on croit.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeTransition(int32 Seed = 20260909, float HeightMeters = 8000.0f,
		int32 ResolutionY = 256, int32 ChunkSideM = 32,
		float LargeurTransition = 0.5f);

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
	 * LE PROFIL PAR BANDE DE LATITUDE -- fait-il froid LA OU il doit ?
	 *
	 * POURQUOI ELLE MANQUAIT. ProbeBiomes donne des PARTS, ProbeTerre des
	 * criteres sourcés, ProbeWhittaker des cases : aucune ne dit OU se trouve
	 * ce qu'elle compte. Or un monde peut porter exactement la bonne part de
	 * toundra en la posant au mauvais endroit, et les trois sondes le
	 * declareraient conforme.
	 *
	 * ELLE ISOLE SURTOUT LE CRITERE DE LA CALOTTE, qui n'est pas la moyenne
	 * annuelle mais le MOIS LE PLUS CHAUD : une bande a -10 C de moyenne mais
	 * +5 C en ete ne portera jamais de glace permanente. Confondre les deux
	 * fait chercher le froid la ou il ne manque pas.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeZonal(int32 Seed = 20260909, float HeightMeters = 32000.0f,
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

	/**
	 * Doline et aven ont-ils le profil qu-on leur prete ?
	 *
	 * DEUX FORMES QUI NE SE DISTINGUENT QUE PAR LE SENS DE LEUR PROFIL : la
	 * doline s-evase vers le HAUT -- entonnoir d-effondrement, les parois
	 * s-eboulent jusqu-a leur angle de repos -- l-aven vers le BAS -- cloche de
	 * dissolution, l-eau a stagne en bas. Les compter ne prouve donc rien ; il
	 * faut MESURER ce sens, et sur le champ reel, parce que l-union lisse et
	 * tout ce qui passe la modifient le rayon demande.
	 *
	 * Et verifier qu-ils PERCENT : un puits qui n-atteint pas la surface n-est
	 * pas un puits, c-est une poche.
	 */
	/**
	 * De combien le sol de fond flotte-t-il au-dessus du sol reel ?
	 *
	 * DEUX SURFACES QUI NE PEUVENT PAS COINCIDER. La nappe est batie sur le
	 * relief MACRO ; le terrain voxel maille l-isovaleur zero du CHAMP, qui
	 * ajoute a ce relief un deplacement vertical 3D. La question n-est donc pas
	 * de savoir si elles divergent, mais DE COMBIEN -- et ce nombre dit quelle
	 * marge corrigerait le defaut signale : un joueur qui marche sur le voxel et
	 * parait enfonce dans la nappe, qui est dessinee et sans collision.
	 */
	/**
	 * Les trous du terrain : dans le CHAMP ou dans le MAILLEUR ?
	 *
	 * « TROU » RECOUVRE TROIS CHOSES QUI SE RESSEMBLENT VUES DE L-EXTERIEUR --
	 * un chunk jamais considere, un chunk declare vide, de la geometrie dechiree
	 * -- et elles appellent trois corrections opposees. Mais avant de les
	 * departager il faut une bifurcation plus grossiere et bien moins chere : si
	 * le CHAMP n-a pas de surface a cet endroit, aucun mailleur n-en produira de
	 * sol, et c-est la generation qu-il faut regarder.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeTrous(int32 Seed = 20260909, float HeightMeters = 32000.0f,
		int32 ResolutionY = 2048, float CoteM = 512.0f, float PasM = 2.0f);

	/**
	 * DEUX CHUNKS DE MEME NIVEAU SE REJOIGNENT-ILS ?
	 *
	 * Ni ProbeTransvoxel -- qui juge un chunk ISOLE -- ni ProbeTransition -- qui
	 * juge une couture GROSSIER/FIN -- ne regardent la configuration que le jeu
	 * emploie en permanence : des chunks tous de meme taille, masque de
	 * transition NUL, poses cote a cote. C'est le seul cas qu'aucun temoin ne
	 * couvre.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeVoisins(int32 Seed = 20260909, float HeightMeters = 32000.0f,
		int32 ResolutionY = 2048, int32 Cotes = 4, float CoteM = 32.0f,
		float CibleXM = 0.0f, float CibleYM = 0.0f);

	/**
	 * D.OU VIENT LE TERRASSEMENT DU RELIEF ?
	 *
	 * Le monde est couvert de gradins reguliers, visibles sur toute la surface
	 * et pas seulement sur les parois. Cette sonde pose la bifurcation la moins
	 * chere : le relief 2D est-il DEJA en escalier, ou est-ce le champ de
	 * densite qui en fabrique un ? Les deux causes appellent des corrections
	 * opposees, et un seul releve les separe.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeParois(int32 Seed = 20260909, float HeightMeters = 32000.0f,
		int32 ResolutionY = 2048, int32 Transects = 24, float LongueurM = 120.0f);

	/**
	 * LES GRADINS TOMBENT-ILS SUR LES LIMITES DE BANCS ?
	 *
	 * `ProbeParois` a etabli l'etage au-dessus : le terrassement vient de la
	 * chaine 2D et non du champ de densite. Reste a savoir QUI le taille, et
	 * la reponse evidente -- l'erosion stratifiee et le sapement des corniches
	 * -- n'a jamais ete mesuree. Elle a meme ete ECRITE au registre comme si,
	 * sur la foi d'une carte des causes ou les bandes de couleur suivaient les
	 * gradins. Or ces bandes sont horizontales PAR CONSTRUCTION et les gradins
	 * le sont aussi : deux choses independamment horizontales se superposent
	 * toujours.
	 *
	 * Ce qu'on mesure ici est la PHASE : pour chaque point de paroi, sa
	 * position dans son banc -- 0 au toit, 1 a la base -- et la pente moyenne
	 * en fonction de cette phase. Une stratigraphie qui taille des marches
	 * doit effondrer la pente pres du toit (la banquette) et la relever au
	 * milieu (la contremarche).
	 *
	 * LE TEMOIN EST DANS LA SONDE : `DecalageDatumM` deplace les limites
	 * SUPPOSEES sans toucher au terrain. A un demi banc, une structure reelle
	 * doit se brouiller ; si elle survit, c'est la methode qui la fabrique.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeMarches(int32 Seed = 20260909, float HeightMeters = 32000.0f,
		int32 ResolutionY = 2048, float DecalageDatumM = 12.0f);

	/**
	 * QUE TROUVE-T-ON, ET DANS QUEL BIOME ?
	 *
	 * ATTENTION A LA PREMISSE : il n.existe AUCUNE table qui dirait « ce biome
	 * porte ces cavites ». Arbitrage B7 -- les cavites dependent de la
	 * LITHOLOGIE, jamais du biome, et le climat n.entre que par la pluie qui
	 * dissout. Ce que cette sonde rend est une COINCIDENCE mesuree : ce qu.un
	 * joueur rencontre reellement dans chaque biome, parce que roche, pluie et
	 * pente varient ensemble. Elle se rejoue apres chaque reglage -- un tableau
	 * fige serait faux des le suivant.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeInfractuosites(int32 Seed = 20260909,
		float HeightMeters = 32000.0f, int32 ResolutionY = 2048,
		int32 PasCellules = 2);

	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeNappe(int32 Seed = 20260909, float HeightMeters = 32000.0f,
		int32 ResolutionY = 2048, int32 Colonnes = 192);

	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbePuits(int32 Seed = 20260909, float HeightMeters = 32000.0f,
		int32 ResolutionY = 2048);

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

	/**
	 * LE TRAIT DE COTE EST-IL DECOUPE, OU POLYGONAL ?
	 *
	 * « Anguleux », « geometrique », « pas organique » : le defaut se voit
	 * tout de suite et ne se chiffrait pas. Il lui fallait une mesure, sinon
	 * on regle un bruit a l'oeil sur une capture, ce que ce depot s'interdit.
	 *
	 * LA MESURE EST CELLE DE MANDELBROT, et elle est sourcee : la DIMENSION
	 * FRACTALE par comptage de boites (« How Long Is the Coast of Britain ? »,
	 * 1967). Une cote rectiligne vaut 1,00 -- doubler la finesse de la regle
	 * double le nombre de boites. Une cote decoupee a toutes les echelles vaut
	 * davantage, parce que la regle fine trouve des details que la grosse
	 * ignorait. Reperes publies : Grande-Bretagne 1,25, Norvege et ses fjords
	 * 1,52, cote sud-africaine, lisse, 1,05.
	 *
	 * LIMITE A DIRE : la carte est en projection equivalente-aire, qui etire
	 * les hautes latitudes. La dimension y est donc legerement biaisee. Elle
	 * reste bonne pour COMPARER deux reglages du meme monde, ce qui est son
	 * emploi ici.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeCotes(int32 Seed = 20260909, float HeightMeters = 32000.0f,
		int32 ResolutionY = 1024);

	/**
	 * Ecrit la carte du monde a plat dans Saved/Worldseed/Cartes/<Etiquette>.png.
	 *
	 * LE PENDANT VISUEL DE ProbeCotes, et il ne fait pas double emploi : la
	 * dimension fractale mesure la rugosite FINE du trait, tandis que
	 * « anguleux et geometrique » decrit la silhouette GENERALE. Un continent
	 * polygonal a grande echelle peut mesurer 1,01 sans que le chiffre bronche.
	 *
	 * Terres en couleur de biome, mer en degrade de profondeur, trait de cote
	 * souligne -- c'est lui qu'on vient juger.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeCarte(int32 Seed = 20260909, float HeightMeters = 32000.0f,
		int32 ResolutionY = 1024, const FString& Etiquette = TEXT("carte"));

	/**
	 * La PLANCHE-CONTACT de la minimap : six vignettes dans une seule image.
	 *
	 * ELLE JUGE LA MINIMAP SANS SLATE, SANS PIE ET SANS ATTENDRE -- et c'est
	 * sa raison d'etre. La tournee photo ne peut pas photographier un widget :
	 * `FScreenshotRequest::RequestScreenshot` y est appele avec `bShowUI` a
	 * faux, donc le moteur lit la CIBLE DE RENDU du viewport, alors que les
	 * widgets poses par `AddViewportWidgetContent` sont composites apres, dans
	 * le back-buffer. Ce n'est pas un reglage qu'on oublie, c'est un mur.
	 *
	 * Les six vignettes : le meme lieu aux quatre caps cardinaux, une sur le
	 * MERIDIEN DE BORDURE -- ou une fenetre mal enroulee se coupe -- et une au
	 * POLE, ou le monde s'arrete pour de bon.
	 *
	 * COMPOSITEES SUR UN DAMIER, parce qu'un PNG n'a pas de « transparence
	 * visible » : sans damier, ni l'alpha du disque ni celui du cone ne se
	 * jugent.
	 *
	 * Un centre nul cherche une COTE, et non le point (0, 0) : une vignette
	 * prise au hasard en plein ocean ne montrerait rien.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeMinimap(int32 Seed = 20260909, float HeightMeters = 32000.0f,
		int32 ResolutionY = 1024, float CentreXm = 0.0f, float CentreYm = 0.0f,
		float DemiPorteeM = 2000.0f, int32 Res = 256,
		const FString& Etiquette = TEXT("minimap"));

	/**
	 * LA CARTE PLEIN ECRAN, CUITE ET ECRITE EN PNG -- sans Slate, sans jeu.
	 *
	 * ELLE TRANCHE DEUX QUESTIONS QUE LE RAISONNEMENT NE PEUT PAS TRANCHER.
	 * D'abord le TEMPS de cuisson : la carte se cuit une fois a la resolution
	 * de la grille, et c'est ce chiffre -- pas une extrapolation -- qui dira
	 * si les quelque cent cinquante millisecondes attendues se paient sur le
	 * fil de jeu ou s'il faut un fil de travail. Ensuite le BRUIT DE
	 * REDUCTION : une texture de 4096 affichee dans 1600 pixels mange un pixel
	 * de lisere sur deux, et c'est ce qui decidera s'il faut une pyramide de
	 * mips. Les deux se regardent sur l'image qu'elle ecrit.
	 *
	 * DEUX IMAGES COTE A COTE, et c'est ce qui rend la seconde question
	 * lisible : la carte entiere reduite a la largeur demandee, et un
	 * recadrage de la MEME zone a un pixel par cellule. Ce qui se perd entre
	 * les deux est exactement ce qu'une pyramide rendrait.
	 *
	 * `LargeurPx` est la largeur de l'ECRAN simule, pas celle de la cuisson :
	 * celle-la se derive de la grille.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbeCarteEcran(int32 Seed = 20260909,
		float HeightMeters = 32000.0f, int32 ResolutionY = 1024,
		int32 LargeurPx = 1920, const FString& Etiquette = TEXT("carte-ecran"));

	/**
	 * Controle ALLER-RETOUR de la projection du globe.
	 *
	 * Partager une formule entre le rendu, le pointage et le repere garantit
	 * qu'ils sont D'ACCORD, pas qu'ils ont RAISON. Seul l'aller-retour
	 * tranche : une latitude et une longitude connues, projetees vers l'image
	 * puis reinversees, doivent revenir sur elles-memes.
	 *
	 * Ne genere aucun monde -- la projection ne depend que de l'orientation --
	 * donc elle tourne en une fraction de seconde et peut se relancer apres
	 * chaque retouche.
	 */
	UFUNCTION(BlueprintCallable, Category = "Worldseed|Sondes")
	static FString ProbePointage();
};
