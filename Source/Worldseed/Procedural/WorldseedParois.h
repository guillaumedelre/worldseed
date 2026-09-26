// Worldseed - poser des pans de falaise sur les faces raides du terrain.

#pragma once

#include "CoreMinimal.h"

struct FWorldseedVoxelMesh;

/**
 * Un pan de falaise a poser, dans le repere de l'acteur terrain.
 *
 * EN CENTIMETRES, comme le maillage dont il sort : le champ de densite raisonne
 * en metres et la conversion se fait une fois, sur les sommets du chunk. Tenir
 * les deux unites dans la meme structure est le meilleur moyen de se tromper
 * d'un facteur cent sans que rien ne le signale.
 */
struct FWorldseedParoiInstance
{
	/** Index du modele dans le catalogue passe au semeur. */
	int32 Modele = 0;

	/** Transformation complete : position, rotation, echelle. */
	FTransform Transform;
};

/**
 * Ce qu'un modele de falaise mesure, releve une fois sur l'asset.
 *
 * LE PIVOT NE SERT PAS D'ANCRAGE, ET C'EST UNE MESURE QUI L'A DECIDE. Les
 * quinze maillages de falaise du pack Orasot ont des pivots incoherents entre
 * eux -- releve du 25 septembre 2026, `origin.z / extent.z` va de -0,55 a
 * +0,94 d'un modele a l'autre. Ancrer sur le pivot poserait donc chaque modele
 * a une hauteur differente pour la meme regle, et le defaut ne se verrait
 * qu'a l'image, modele par modele. On ancre sur le CENTRE DE LA BOITE, qui est
 * la meme grandeur geometrique pour tous.
 */
struct FWorldseedParoiModele
{
	/** Demi-dimensions de la boite englobante locale, en centimetres. */
	FVector DemiTaille = FVector(1.0, 1.0, 1.0);

	/** Centre de cette boite dans le repere local du maillage, en centimetres. */
	FVector Origine = FVector::ZeroVector;

	/** Plus grande demi-dimension, en centimetres. Sert de rayon d'encombrement. */
	double Rayon() const
	{
		return FMath::Max3(DemiTaille.X, DemiTaille.Y, DemiTaille.Z);
	}
};

/**
 * Ce qu'on lit du relief en un point : est-on sur un rebord, et lequel.
 *
 * SEPARE DU SEMIS A DESSEIN. C'est le seul endroit ou se decide ce qu'est un
 * rebord, et le seul que la sonde aurait a interroger si l'on voulait un jour
 * mesurer leur longueur totale ou leur repartition. Le garder enfoui dans la
 * boucle de semis reviendrait a n'avoir aucune facon de l'eprouver autrement
 * qu'en regardant le monde entier.
 */
struct FWorldseedRebord
{
	/** Direction horizontale du vide, normalisee. */
	FVector2D VersLeVide = FVector2D::ZeroVector;

	/** Altitude du rebord, en metres : le haut de la marche. */
	double AltitudeM = 0.0;

	/** De combien ca tombe, en metres. */
	double ChuteM = 0.0;
};

/**
 * Les reglages du semis.
 *
 * ILS NE VIVENT PAS DANS `world_rules.json`, ET C'EST DELIBERE. L'empreinte de
 * ce fichier est un MD5 de son contenu entier : y poser un reglage de DECOR
 * invaliderait tous les mondes en cache et imposerait deux cents secondes de
 * regeneration pour un chiffre qui ne touche ni le relief, ni le climat, ni les
 * biomes. Meme choix que `LargeurTransition` et que la rugosite des anneaux.
 */
struct FWorldseedParoiRegles
{
	/**
	 * Part de montee toleree autour du point, rapportee a la chute.
	 *
	 * C'EST LE CRITERE QUI FAIT LA LIGNE, et il a remplace un critere de PENTE
	 * qui ne pouvait pas marcher. Chercher les faces raides retient une
	 * SURFACE -- et un canyon est raide partout, si bien que 911 pans
	 * l'ensevelissaient sous un chaos de dalles. Aucune densite, aucun seuil de
	 * denivele n'y changeait rien : c'etait la FORME du critere qui etait
	 * fausse, pas sa valeur.
	 *
	 * Un rebord n'est pas une face raide, c'est le HAUT d'une marche. Au milieu
	 * d'une paroi il y a autant de roche au-dessus qu'en dessous ; sur le
	 * rebord, rien ne domine. Exiger que la montee soit petite devant la chute
	 * ne retient donc que le trait ou le plateau bascule dans le vide -- une
	 * ligne, et c'est le long de cette ligne que le pack pose ses pans.
	 */
	float MonteeMaxFrac = 0.35f;

	/**
	 * Maille du tirage, en metres.
	 *
	 * ELLE FIXE LA DENSITE, ET LA DENSITE SE LIT DANS LE PAS DE GRILLE, jamais
	 * dans le nombre de modeles du catalogue. Releve sur la showcase du pack :
	 * 1425 pans de falaise sur une emprise de 4,08 km2, soit 3,5 a l'hectare.
	 *
	 * ELLE DOIT AUSSI DEPASSER LA LARGEUR D'UN PAN, sans quoi les blocs se
	 * recouvrent. Premier essai a 55 m avec des pans de 123 a 246 m : 5098
	 * instances vivantes, un CHAMP DE DALLES EMPILEES qui ensevelissait le
	 * canyon et dans lequel la camera photo se retrouvait enfermee. Le pan
	 * mesure 77 m dans sa plus grande dimension a l'echelle 1 ; on prend au
	 * moins cela.
	 */
	float PasM = 130.0f;

	/** Part des mailles retenues, dans [0..1]. */
	float Probabilite = 0.5f;

	/**
	 * Rayon sur lequel on mesure la hauteur de la paroi, en metres.
	 *
	 * IL DOIT DEPASSER LARGEMENT LA TAILLE D'UN CHUNK, et c'est tout l'objet de
	 * cette valeur. La premiere version mesurait la paroi dans le maillage du
	 * chunk, donc sur 32 metres ; le relevé de la tournee donne des falaises de
	 * 138 m de chute. La mesure sous-estimait donc SYSTEMATIQUEMENT, l'echelle
	 * tombait sur son plancher, et les pans se lisaient comme des cailloux
	 * poses a plat au lieu d'une paroi.
	 *
	 * On mesure desormais sur le relief MACRO, qui est global et ne connait pas
	 * le decoupage en chunks.
	 */
	float RayonMesureM = 120.0f;

	/**
	 * Denivele minimal pour qu'un endroit merite un pan, en metres.
	 *
	 * SANS LUI, CHAQUE RESSAUT RECOIT SA FALAISE. La pente seule ne suffit pas
	 * a designer une paroi : le terrain voxel porte des gradins partout --
	 * c'est le sapement des corniches, et c'est voulu -- et ils passent tous le
	 * seuil de 58 degres. Mesure de ce defaut : 1738 pans sur la tournee,
	 * formant un CHAOS DE DALLES qui ensevelissait le canyon au lieu d'en
	 * habiller les parois.
	 *
	 * Ce que la showcase habille, ce n'est pas une surface raide, c'est un
	 * BORD : la falaise du releve fait 138 m de chute. On ne garde donc que ce
	 * qui a un vrai denivele, et les ressauts restent du terrain nu.
	 *
	 * C'est le meme raisonnement que l'entonnoir des mesas : un masque de
	 * region dit ou une forme a le DROIT d'exister, il ne dit pas qu'elle
	 * existe -- il faut des gardes geometriques en plus.
	 */
	float DeniveleMinM = 45.0f;

	/**
	 * Bornes de l'echelle, APRES calage sur la hauteur de la paroi.
	 *
	 * L'ECHELLE NE SE TIRE PAS AU SORT, ELLE SE MESURE -- et c'est la
	 * correction la plus importante de ce module. Le premier jet tirait dans
	 * [1,6 .. 3,2], les valeurs relevees sur la showcase du pack. Elles y sont
	 * justes : l'ile flottante fait plusieurs centaines de metres et ses pans
	 * forment une paroi continue. Posees sur un canyon dont la paroi fait
	 * quelques dizaines de metres, elles donnent des blocs plus grands que le
	 * relief qui les porte.
	 *
	 * C'est le piege que ce depot connait bien : « une valeur d'auteur JUSTE
	 * devient fausse quand on change l'echelle du monde » -- le `Sand UV` a
	 * 0,495 d'Orasot, cale sur leur petite carte de demonstration, qui a
	 * transforme le sable en ciel etoile sur nos huit kilometres. Le reflexe
	 * « recopier l'instance du pack » vaut pour une couleur ; il ne vaut
	 * jamais pour une GRANDEUR METRIQUE.
	 */
	float EchelleMin = 0.8f;
	float EchelleMax = 3.2f;

	/**
	 * Basculement maximal hors de la verticale, en degres.
	 *
	 * RELEVE SUR LE PACK, PAS CHOISI. Les 194 instances de `SM_Cliff_2` de la
	 * showcase ont une inclinaison mediane de 14,8 degres et des pitch/roll
	 * dans +/- 24. L'artiste ne retourne PAS ces grands pans -- ce sont les
	 * petits rochers d'accompagnement qui le sont, avec une inclinaison
	 * mediane de 117 degres.
	 */
	float BasculeMaxDeg = 18.0f;

	/**
	 * Quel axe local du modele regarde le vide, en degres depuis +X.
	 *
	 * IL SE REGLE A L'IMAGE, ET IL N'Y A PAS D'AUTRE FACON. Un maillage
	 * quelconque n'a aucune raison d'orienter sa face large selon un axe
	 * plutot qu'un autre ; `SM_Cliff_2` fait 76,8 m en X pour 54,9 en Y, donc
	 * sa face large est le plan X-Z et sa normale suit Y -- d'ou -90 degres,
	 * qui amene Y vers le vide et laisse X courir le long du rebord.
	 *
	 * SE TROMPER DE QUATRE-VINGT-DIX DEGRES NE CASSE RIEN ET NE SE VOIT PAS
	 * AUTREMENT QU'EN REGARDANT : les pans se posent alors de profil, en
	 * rangee de dents au lieu d'une paroi continue. C'est exactement le genre
	 * de convention que ce depot interdit de DEDUIRE -- l'enroulement des
	 * triangles a coute trois passages pour cette raison -- d'ou la surcharge
	 * `-WorldseedParoiYaw=` qui permet l'A/B sans recompiler.
	 */
	float YawOffsetDeg = -90.0f;

	/**
	 * Enfoncement dans la roche, en part de la DEMI-LARGEUR du modele.
	 *
	 * SANS LUI LE PAN FLOTTE, ET AVEC TROP PEU IL SE POSE A PLAT. Un bloc pose
	 * exactement sur la surface ne la touche que par un point ; il faut
	 * l'enfoncer le long de la normale pour que sa face s'appuie contre la
	 * paroi.
	 *
	 * ET C'EST AINSI QUE LA SHOWCASE L'EMPLOIE : ses pans de 120 a 250 m
	 * s'interpenetrent massivement, on n'en voit que la FACE. Un pan dont on
	 * voit le tour entier se lit comme un rocher pose ; un pan dont on ne voit
	 * que la face se lit comme de la paroi. D'ou une valeur au-dela de la
	 * moitie -- la majeure partie du bloc est dans la roche.
	 *
	 * En part et non en metres, sans quoi un pan a l'echelle 3,2 serait enfonce
	 * comme un pan a l'echelle 0,8 et decollerait quatre fois plus.
	 */
	float EnfoncementFrac = 0.62f;

	/** A zero, aucun semis. Permet un temoin sans recompiler. */
	float Densite = 1.0f;
};

/**
 * LE TIRAGE SE FAIT SUR UNE GRILLE DU MONDE, JAMAIS SUR LES TRIANGLES.
 *
 * C'est le seul point d'architecture de ce module, et il merite son paragraphe.
 * Tirer au sort parmi les triangles d'un chunk serait plus simple et donnerait
 * le meme resultat AUJOURD'HUI -- mais le maillage d'un chunk change des qu'il
 * change de niveau d'anneau, et le registre montre que les anneaux existent,
 * sont mesures, et ne sont eteints (`NiveauMax = 0`) que par defaut. Le jour ou
 * on les arme, un semis cale sur les triangles ferait SAUTER les falaises quand
 * le joueur s'approche : elles changeraient de place a chaque remaillage, sans
 * qu'aucun test ne le voie.
 *
 * La grille XY, elle, est une fonction du MONDE : meme maille, meme hachage,
 * meme tirage, quel que soit le decoupage en chunks et quel que soit le niveau.
 * Le maillage ne sert plus qu'a repondre a deux questions locales -- a quelle
 * hauteur est la surface, et quelle est sa normale -- dont la reponse ne bouge
 * que d'un demi-voxel d'un niveau a l'autre. Une falaise de cent metres ne se
 * deplace donc pas visiblement.
 */
namespace WorldseedParois
{
	/**
	 * Seme les pans de falaise d'un chunk.
	 *
	 * `Mesh` est le maillage du chunk, en centimetres dans le repere de
	 * l'acteur. `OrigineChunkCm` et `CoteChunkCm` bornent la maille de grille
	 * a visiter. `Graine` est celle du monde : deux parties sur la meme graine
	 * doivent semer a l'identique, comme tout le reste de ce projet.
	 *
	 * `ReliefM` rend l'altitude MACRO en metres pour une position en metres.
	 * C'est par elle que se mesure la hauteur d'une paroi : le maillage du
	 * chunk ne peut pas la voir, il ne couvre que trente-deux metres.
	 */
	WORLDSEED_API void Semer(const FWorldseedVoxelMesh& Mesh,
		const FVector& OrigineChunkCm, double CoteChunkCm,
		TArrayView<const FWorldseedParoiModele> Catalogue,
		const FWorldseedParoiRegles& Regles, int32 Graine,
		TFunctionRef<double(double, double)> ReliefM,
		TArray<FWorldseedParoiInstance>& Out);

	/**
	 * Le hachage du semis, expose pour qu'un test puisse l'eprouver.
	 *
	 * Rend une valeur dans [0..1) a partir d'une maille et d'un canal. Le canal
	 * separe les tirages -- probabilite, modele, yaw, echelle -- qui doivent
	 * etre INDEPENDANTS : les derouler d'un seul hachage correlerait l'echelle
	 * au yaw, et l'on verrait les gros pans tous tournes du meme cote.
	 */
	WORLDSEED_API float Tirage(int32 MailleX, int32 MailleY, int32 Canal, int32 Graine);

	/**
	 * Y a-t-il un rebord en ce point, et vers ou tombe-t-il ?
	 *
	 * PUBLIQUE PARCE QU'ELLE EST LE CRITERE. Une sonde qui reimplementerait sa
	 * regle validerait une COPIE du mecanisme et non le mecanisme -- c'est la
	 * regle du depot, et le portage de `terre.py` l'a deja rappelee. Rend faux
	 * si le point n'est pas un rebord, auquel cas `Out` n'est pas touche.
	 */
	WORLDSEED_API bool LireRebord(double XM, double YM,
		const FWorldseedParoiRegles& Regles,
		TFunctionRef<double(double, double)> ReliefM,
		FWorldseedRebord& Out);

	/**
	 * Les maillages que l'habillage Orasot pose quand rien n'est regle.
	 *
	 * UN SEUL MODELE, ET C'EST UNE MESURE QUI L'A CHOISI. Sur la carte de
	 * demonstration du pack -- `M_5_Bioms_Showcase` --, les quatorze pans de
	 * falaise visibles dans la vue de reference sont TOUS le meme :
	 * `Biom_Green/SM_Cliff_2`, pose a des echelles de 2,4 a 3,2, soit 120 a
	 * 250 m par bloc. Les autres `SM_Cliff` du pack sont des affleurements de
	 * SURFACE : ils se lisent comme des plaques posees dans l'herbe, pas comme
	 * une paroi.
	 *
	 * ET CELA S'EST VERIFIE A L'IMAGE, APRES UNE ERREUR. Juges d'une vue
	 * ZENITHALE, ces memes pans ressemblaient a des dalles et l'on a conclu
	 * qu'ils n'etaient pas les bons ; vus DE COTE, contre le ciel, ce sont bien
	 * les parois prismatiques cherchees. Le registre portait deja la regle --
	 * « juger une silhouette contre le ciel, de cote, jamais a la verticale » --
	 * et elle a ete repayee le 25 septembre 2026.
	 *
	 * Releve de l'asset : 76,8 x 54,9 x 44,5 m, 3366 triangles, 3 LOD, et
	 * AUCUNE primitive de collision simple.
	 */
	WORLDSEED_API TArray<FSoftObjectPath> MaillagesParDefaut();
}
