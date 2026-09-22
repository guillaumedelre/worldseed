// Worldseed - le plateau disseque : les mesas sont ce qu'il en reste.

#pragma once

#include "CoreMinimal.h"

#include "Procedural/WorldseedLithology.h"
#include "Procedural/WorldseedStrata.h"

class UWorldseedRules;
struct FWorldseedGeometry;
struct FWorldseedFinRules;

/**
 * Section "tables" de world_rules.json.
 *
 * CE QUE C'EST, ET POURQUOI CE N'EST PAS UN BRUIT. Une mesa n'est pas une
 * colline a sommet plat : c'est le RESTE d'une ancienne plaine que l'erosion a
 * decoupee. Toute la lisibilite de la forme tient dans un seul fait -- sur une
 * photo de Monument Valley, TOUS les sommets sont a la MEME altitude, parce
 * qu'ils sont les morceaux d'UNE SEULE surface. Aucun bruit fractal ne peut
 * produire cela : un fBm donne des sommets a des hauteurs toutes differentes,
 * et l'on obtient des collines, jamais des tables.
 *
 * LE COMMENTAIRE DE LA SECTION "lames" PROMETTAIT TROP. Il affirmait que le
 * champ de lames donne « les canyons et les mesas » a d'autres reglages. C'est
 * vrai pour les canyons EN FENTE, faux pour les mesas : des fentes paralleles
 * laissent des CRETES, dont les sommets suivent le relief existant. Il y
 * manquait la surface de reference, qui est tout le mecanisme.
 *
 * LA PASSE N'ABAISSE JAMAIS, exactement comme la passe littorale dont elle
 * copie la structure. C'est ce qui garantit que la part emergee -- calibree a
 * 29,2 pour cent et traitee comme un invariant du projet -- ne bouge pas, et
 * qu'aucune bosse n'apparait au raccord avec le relief voisin.
 *
 * ELLE VIT DANS LE RELIEF 2D, PAS DANS LE CHAMP DE DENSITE, et ce n'est pas un
 * detail : le voxel ne creuse que `bandeM` sous la surface, cent metres. Une
 * table de deux cents metres n'y tiendrait tout simplement pas. Le voxel
 * ajoute ensuite son grain d'un metre sur la paroi, ce qui est son role.
 */
struct WORLDSEED_API FWorldseedPlateauRules
{
	/**
	 * Hauteur de l'escarpement, en metres : du sommet de la table au fond du
	 * canyon.
	 *
	 * C'EST LA SEULE VALEUR QUI DECIDE DE L'ALLURE. Sous cinquante metres on
	 * lit des banquettes, au-dela de deux cents on lit des gorges.
	 */
	float ScarpM = 170.0f;

	/**
	 * Rayon de la fenetre qui construit la surface d'aplanissement, en metres.
	 *
	 * IL FIXE LA TAILLE D'UNE TABLE, et c'est sa vraie fonction. La surface est
	 * un maximum glissant de ce rayon, puis lisse : le maximum retient les
	 * sommets, le lissage les relie en UNE surface, et c'est la ou le lissage
	 * passe SOUS un sommet isole que ce sommet se trouve rabote a plat.
	 */
	float ReachM = 900.0f;

	/**
	 * Denivele local au-dela duquel on ne fait rien, en metres.
	 *
	 * UNE MESA EST LE RESTE D'UNE PLAINE : s'il n'y a pas de plaine, il n'y a
	 * rien a disséquer. Sur un versant de montagne, raboter les sommets et
	 * creuser les vallees ne donnerait pas des tables mais un relief mutile.
	 */
	float LocalReliefMaxM = 320.0f;

	/**
	 * Pluie au-dessus de laquelle on ne fait rien, en mm/an.
	 *
	 * UN ESCARPEMENT VERTICAL EST UNE FORME ARIDE, et c'est un fait de terrain,
	 * pas une preference : sous la pluie, le sol se forme, la vegetation
	 * s'installe, et la paroi s'adoucit en versant. C'est le CHAMP de pluie qui
	 * est lu, jamais l'etiquette de biome -- meme regle que le karst, qui
	 * demande une roche soluble et une pluie suffisante.
	 */
	float PrecipMaxMm = 520.0f;

	/**
	 * Temperature moyenne annuelle MINIMALE, en degres.
	 *
	 * SOURCE, ET C.EST UN DEFAUT TROUVE PAR LA MESURE. Le croisement des formes
	 * avec les biomes a montre que les 14 tables et 9 des 12 canyons du monde
	 * etaient sous la CALOTTE GLACIAIRE. La garde d.aridite ne regarde que le
	 * cumul de pluie -- 203 mm sous la calotte -- et un desert POLAIRE la passe
	 * aussi bien qu.un desert chaud.
	 *
	 * OR LE MECANISME D.UNE TABLE EXIGE DE L.EAU LIQUIDE. Une corniche ne tient
	 * que parce que le talus tendre sous elle est EMPORTE : c.est le ruissellement
	 * qui decape, et c.est lui qui fait reculer l.escarpement en restant vertical.
	 * Sous zero en moyenne annuelle, l.eau est prise ; les processus qui dominent
	 * sont glaciaires et periglaciaires -- gelifraction, solifluxion -- et ils
	 * arrondissent au lieu de trancher. Le seuil est donc a ZERO, la limite
	 * classique du pergelisol continu, et non une valeur de gout.
	 *
	 * Elle ne ferme pas le desert FROID, qui reste au-dessus de zero : c.est bien
	 * la le paysage de badlands qu.on veut garder.
	 */
	float TempMinC = 0.0f;

	/**
	 * Fenetre de durete de la roche, dans [0..1].
	 *
	 * LA ROCHE SEDIMENTAIRE TENDRE, ET C'EST MECANIQUE. Une table-montagne est
	 * un empilement sedimentaire ; le granite ne se debite pas en bancs
	 * horizontaux. La fenetre par defaut retient le gres (0,55), le calcaire
	 * (0,45) et la craie du catalogue, sans toucher au granite ni au basalte.
	 */
	float HardnessMin = 0.25f;
	float HardnessMax = 0.70f;

	/** Altitude minimale, en metres : on ne fait pas de table sur la plage. */
	float MinElevationM = 60.0f;

	/**
	 * Altitude plancher du fond de canyon, en metres.
	 *
	 * AU-DESSUS DE ZERO A DESSEIN, et c'est la meme raison que la plateforme
	 * de la passe littorale : la part emergee est calibree a 29,2 pour cent et
	 * traitee comme un invariant du projet. Sans ce plancher, un escarpement
	 * de cent soixante-dix metres creuse depuis une table qui n'en fait que
	 * quatre-vingts ferait passer le fond SOUS le niveau de la mer -- donc
	 * convertirait de la terre en mer a chaque generation, et le bulletin
	 * terrestre bougerait sans qu'on sache pourquoi.
	 */
	float FloorMinM = 5.0f;

	/** Frequence du masque de region, en cycles par metre. */
	float ZoneFrequency = 0.00012f;

	/**
	 * Seuil du masque de region.
	 *
	 * UN SEUIL N'EST PAS UNE PART -- le depot a paye cette confusion deux fois,
	 * sur les diaclases puis sur la pluie prise pour une mediane. Le bruit de
	 * Perlin se masse autour de zero, donc la part reellement couverte se
	 * MESURE. Le releve est dans le commentaire de la regle.
	 */
	float ZoneThreshold = 0.30f;

	/**
	 * Flux accumule, en log10, a partir duquel la decoupe commence, et largeur
	 * de la transition.
	 *
	 * LE CANYON SUIT LE DRAINAGE, donc un reseau DENDRITIQUE : une gorge
	 * principale et ses affluents, ce qu'aucun bruit ne sait dessiner. Le flux
	 * est pondere par la PLUIE, ce qui reproduit le fleuve ALLOGENE -- celui
	 * qui ramasse son eau ailleurs et traverse un desert. C'est exactement
	 * l'histoire du Colorado, et c'est pourquoi le plus grand canyon du monde
	 * se trouve dans une region qui ne recoit rien.
	 *
	 * LA LARGEUR FAIT LA VERTICALITE DE LA PAROI : plus elle est etroite, plus
	 * la decoupe passe de zero a un sur peu de cellules.
	 */
	float DrainStart = 4.3f;

	/**
	 * Part de l'escarpement que creusent les fentes de diaclases.
	 *
	 * LA SECONDE ECHELLE. Le drainage taille la gorge ; les joints paralleles
	 * du champ de lames ajoutent les fentes etroites dans les parois et sur le
	 * plateau -- les slots d'Antelope. A zero, il ne reste que la gorge.
	 */
	/**
	 * Demi-largeur du PLANCHER de la gorge, en metres.
	 *
	 * UN CANYON A UN FOND PLAT, et c'est ce qui le separe d'une vallee en V :
	 * le lit du fleuve etale ses galets, la paroi monte d'un coup au-dessus.
	 * La profondeur vaut donc pleine valeur sur toute cette largeur.
	 */
	float FloorWidthM = 45.0f;

	/**
	 * Largeur de la PAROI, en mailles de simulation.
	 *
	 * LA FACE DOIT TENIR DANS UNE MAILLE, sinon la falaise n'est qu'une rampe.
	 * C'est la lecon deja ecrite dans la passe littorale, et la premiere
	 * version du canyon l'a ignoree : un smoothstep sur le flux accumule
	 * etalait la descente sur plusieurs mailles, d'ou un abaissement MOYEN de
	 * 58 m pour un maximum de 166 -- une rampe, pas une gorge. A une maille, le
	 * voxel rend ensuite la paroi franche au metre.
	 */
	float WallCells = 1.0f;

	float SlotShare = 0.45f;

	/** Dosage global. A zero, la passe ne fait rien, a l'identique. */
	/**
	 * Durete minimale d un banc pour faire CHAPITEAU.
	 *
	 * LE SOMMET D UNE MESA N EST PLUS RABOTE GEOMETRIQUEMENT, il se cale sur
	 * le toit du premier banc capable de porter la surface. Deux mesas
	 * voisines s arretent donc sur LE MEME banc, a la MEME altitude -- la
	 * propriete qui definit la forme sort ainsi de la geologie et non plus
	 * d un maximum glissant. Avec le catalogue actuel, 0,40 retient gres,
	 * dolomie et calcaire, et laisse schiste et craie faire les talus.
	 */
	float CapHardnessMin = 0.40f;

	float Strength = 1.0f;

	bool IsActive() const { return Strength > 0.0f && ScarpM > 0.0f && ReachM > 0.0f; }

	static FWorldseedPlateauRules FromRules(const UWorldseedRules& Rules);
};

/**
 * Un site de table, retenu pour qu'on puisse aller le voir.
 *
 * UNE FORME QU'ON NE SAIT PAS TROUVER N'EXISTE PAS. Le depot a deja tire cette
 * lecon sur les bouches de grotte, dont les positions sont journalisees pour la
 * meme raison, et sur les arches, que le reseau conserve. Sans ces sites, une
 * mesa parfaitement formee reste invisible : 1,5 pour cent des terres sur
 * 64 x 32 km, c'est introuvable au hasard.
 *
 * RIEN N'EST SERIALISE : la passe tourne a chaque generation et ses sites se
 * rebatissent avec elle, exactement comme le reseau de grottes. Aucun effet sur
 * le cache.
 */
struct WORLDSEED_API FWorldseedPlateauSite
{
	/** Centre du sommet, en metres monde. */
	FVector2D CentreM = FVector2D::ZeroVector;

	/** Altitude du point : sommet pour une table, FOND pour un canyon. */
	float AltitudeM = 0.0f;

	/** Denivele local autour, en metres : la hauteur de la paroi. */
	float EscarpementM = 0.0f;

	/**
	 * Direction de la plus forte CHUTE, normalisee. Zero si inconnue.
	 *
	 * ON SE PLACE DU COTE BAS, sinon on photographie le plateau et la paroi
	 * est DERRIERE la camera. C est exactement ce que fait deja le selecteur
	 * de falaises marines, dont les vues sont les seules de la tournee a
	 * montrer une vraie paroi.
	 */
	FVector2D VersLeBas = FVector2D::ZeroVector;
};

namespace WorldseedPlateau
{
	/**
	 * La surface d'aplanissement et le denivele local.
	 *
	 * PUBLIQUE A DESSEIN, pour etre lue par DEUX consommateurs qui ne se
	 * connaissent pas : la passe, qui rabote et creuse, et la sonde, qui
	 * mesure. Le depot a une regle contre la recopie d'une formule dans deux
	 * fichiers, et elle a deja ete payee -- une sonde qui reimplemente son
	 * critere valide une COPIE du mecanisme, pas le mecanisme, et les deux
	 * divergent a la premiere retouche.
	 *
	 * DEUX OPERATIONS, ET CHACUNE A SON ROLE. Le maximum glissant retient les
	 * SOMMETS : c'est le toit du relief, donc ce que la plaine ancienne devait
	 * etre. Le lissage les relie en UNE surface continue -- et c'est la que
	 * tout se joue, parce que la ou le lissage passe SOUS un sommet isole, ce
	 * sommet se trouve rabote a plat. Le maximum SEUL ne raboterait rien : il
	 * vaut H sur chaque sommet par construction.
	 */
	WORLDSEED_API void Surfaces(const FWorldseedGeometry& Geometry,
		const TArray<float>& ElevationM, const FWorldseedPlateauRules& Rules,
		TArray<float>& OutPlateau, TArray<float>& OutReliefLocalM,
		TArray<float>* OutSolM = nullptr);

	/**
	 * Force du masque de region dans [0..1]. Zero : pas de tables ici.
	 *
	 * Publique pour la meme raison que Surfaces : la sonde doit poser LE meme
	 * test, pas un test qui lui ressemble.
	 */
	WORLDSEED_API float ZoneAt(double X, double Y,
		const FWorldseedPlateauRules& Rules, int32 Seed);

	/**
	 * LA GARDE PHYSIQUE D.UN SITE : roche, pluie, froid.
	 *
	 * UNE SEULE IMPLEMENTATION, TROIS CONSOMMATEURS. Ce test existait en DEUX
	 * copies dans le meme fichier -- une dans Sites, qui LISTE, et une dans
	 * Build, qui CREUSE. Ajouter un critere a l.une aurait fait diverger les
	 * deux : on aurait cesse de lister des tables qu.on continuait a tailler.
	 * C.est la regle du depot, « ne jamais recopier une formule », et le
	 * compilateur ne l.aurait pas signalee.
	 *
	 * ELLE NE LIT QUE DES CHAMPS CONTINUS, jamais l.etiquette de biome : un
	 * biome ne sert qu.a lier des assets, il ne decide d.aucune geometrie.
	 */
	WORLDSEED_API bool Eligible(int32 Cell, const FWorldseedPlateauRules& Rules,
		const FWorldseedLithology& Lithology,
		const FWorldseedLithologyRules& LithoRules,
		const TArray<float>& PrecipMm, const TArray<float>& TempMeanC,
		int32 Count);

	/**
	 * Les sites de tables d'un relief FINI.
	 *
	 * FONCTION PURE, ET C'EST CE QUI COMPTE : elle se rejoue a l'identique
	 * depuis la graine, les regles et le relief, donc rien n'a besoin d'etre
	 * transporte ni serialise. C'est exactement le raisonnement que le depot
	 * applique deja au reseau de grottes, que le menu ne transporte pas et que
	 * le terrain rebatit -- transporter une donnee deterministe la doublerait.
	 */
	/**
	 * LES MEMES GARDES QUE LA PASSE, roche et pluie comprises.
	 *
	 * DEFAUT TROUVE A L IMAGE, ET DEUX FOIS. Premiere version : seul le
	 * masque de region etait teste, et les sites designaient des MONTAGNES a
	 * 700 et 1064 m. On y a ajoute des gardes GEOMETRIQUES -- sommet plat,
	 * denivele d un escarpement -- et le defaut a change de visage sans
	 * disparaitre : les sites tombaient alors sur des epaules enneigees et
	 * des versants cotiers VERTS, c est-a-dire hors de la roche sedimentaire
	 * et hors du climat aride. Un masque de region dit ou une forme a le
	 * DROIT d exister ; seules les gardes PHYSIQUES disent ou elle existe.
	 */
	WORLDSEED_API void Sites(const FWorldseedGeometry& Geometry,
		const TArray<float>& ElevationM, const FWorldseedPlateauRules& Rules,
		const FWorldseedLithology& Lithology, const FWorldseedLithologyRules& Litho,
		const TArray<float>& PrecipMm, const TArray<float>& TempMeanC,
		int32 Seed, TArray<FWorldseedPlateauSite>& OutTables,
		TArray<FWorldseedPlateauSite>* OutCanyons = nullptr);

	/**
	 * Rabote les sommets sur une surface d'aplanissement, puis la decoupe.
	 *
	 * Modifie ElevationM EN PLACE, et seulement vers le bas. A poser apres
	 * l'erosion et AVANT la seconde passe de climat, pour que la temperature
	 * puis les biomes voient le relief final -- c'est la place de la passe
	 * littorale, et pour la meme raison.
	 */
	WORLDSEED_API void Build(const FWorldseedGeometry& Geometry,
		const FWorldseedLithology& Lithology,
		const FWorldseedLithologyRules& LithoRules,
		const TArray<float>& PrecipMm, const TArray<float>& TempMeanC,
		const FWorldseedPlateauRules& Rules,
		const FWorldseedFinRules& FinRules,
		const FWorldseedStratRules& StratRules, int32 Seed,
		TArray<float>& ElevationM,
		TArray<FWorldseedPlateauSite>* OutSites = nullptr,
		TArray<FWorldseedPlateauSite>* OutCanyons = nullptr);
}
