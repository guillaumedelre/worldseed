// Worldseed - peindre une FENETRE de la carte du monde, dans un tampon d'image.

#pragma once

#include "CoreMinimal.h"

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedRegions.h"
#include "Procedural/WorldseedRules.h"

/**
 * LA CARTE N'EST PAS CAPTUREE, ELLE EST PEINTE.
 *
 * Le reflexe, pour une minimap, est une camera orthographique au-dessus du
 * joueur. Il ne vaut rien ici : le terrain voxel n'existe que dans le rayon de
 * chargement -- 2400 m -- et au-dela il n'y a que la nappe d'horizon, enfoncee
 * sous la bande creusable et sortie du rendu principal. Une capture filmerait
 * un disque de terrain pose sur un decor deforme, et couterait une passe de
 * rendu par image sur un jeu deja borne par son fil de rendu.
 *
 * Or le monde est DEJA EN MEMOIRE : altitude, biomes et couverture sont des
 * tableaux que le terrain tient par reference. On les lit, on les peint, et
 * cela ne coute rien au GPU.
 *
 * UNE SEULE FONCTION POUR DEUX USAGES. `PeindreFenetre` prend un centre et une
 * portee : la minimap l'appelle avec le joueur au centre, une carte plein ecran
 * l'appellera avec le monde entier. Ecrire deux peintres ferait diverger les
 * deux a la premiere retouche -- ce depot a paye cette lecon sur l'amplitude
 * saisonniere recalculee dans un second fichier, et sur le classificateur de
 * biomes reimplemente par le bulletin terrestre, qui validait donc une COPIE du
 * classificateur.
 */
namespace WorldseedCarte
{
	/**
	 * Ce qu'on regarde, et comment.
	 *
	 * LA PORTEE EST EN METRES, ET LA RESOLUTION N'EN DEPEND PAS. Sur la grille
	 * du jeu -- 4096 x 2048 pour 64 x 32 km, soit 15,6 m de maille -- une
	 * demi-portee de 2000 m dans 256 pixels tombe a un pixel par cellule. C'est
	 * une COINCIDENCE DE CETTE GRILLE, pas une loi : le menu peut en choisir
	 * une autre. Figer ce rapport en constante serait la faute que ce depot a
	 * deja payee deux fois -- la prime d'altitude en dur de `tectonics.py`, et
	 * le `Sand UV` d'Orasot, tous deux justes a une echelle et faux a l'autre.
	 */
	struct WORLDSEED_API FParamsFenetre
	{
		/** Centre de la fenetre, dans le repere de l'acteur terrain. */
		double CentreXm = 0.0;
		double CentreYm = 0.0;

		/**
		 * Demi-largeur et demi-hauteur de la fenetre, en metres.
		 *
		 * ELLES NE SE REMPLISSENT PAS A LA MAIN : voir `Carree` et `Rectangle`.
		 * Deux demi-portees libres n'imposent PAS des pixels carres -- le monde
		 * est en 2:1 et un ecran en 16:9 -- et une fenetre etiree fausserait
		 * l'ombrage, qui suppose des metres isotropes, comme le disque, qui
		 * suppose un cercle.
		 */
		double DemiPorteeXm = 2000.0;
		double DemiPorteeYm = 2000.0;

		/** Cote du tampon, en pixels. */
		int32 ResX = 256;
		int32 ResY = 256;

		/** Alpha nul hors du disque inscrit, avec un fondu d'un pixel. */
		bool bDisque = true;

		/** Ombrage de relief par lumiere rasante. */
		bool bOmbrage = true;

		/** Souligne le trait de cote. */
		bool bLisereCote = true;

		/**
		 * Trace les frontieres de REGION, en trait fin.
		 *
		 * MEME MECANIQUE QUE LE LISERE DE COTE, et pour la meme raison : une
		 * seconde passe qui lit un tableau retenu par pixel et n'ecrit que son
		 * propre pixel. Souligner en place propagerait le trait de proche en
		 * proche, chaque pixel marque devenant a son tour une frontiere.
		 */
		bool bFrontieresRegions = false;

		/**
		 * Trace les frontieres de PAYS, en trait appuye.
		 *
		 * ELLES L'EMPORTENT SUR CELLES DES REGIONS quand les deux tombent au
		 * meme endroit -- ce qui est le cas de TOUTE frontiere de pays, un
		 * pays etant un agregat de regions entieres. Sans cette priorite, le
		 * trait fin recouvrirait le trait appuye une fois sur deux selon
		 * l'ordre de parcours, et la hierarchie des deux ne se lirait plus.
		 */
		bool bFrontieresPays = false;

		/**
		 * Ecart, en cellules, entre les voisins que l'ombrage compare.
		 *
		 * A 15,6 m de maille, un ombrage pris sur les cellules immediatement
		 * voisines peut etre bruyant -- le relief de simulation y est a pleine
		 * frequence. Aucun chiffre ne le verra : cela se juge a l'image.
		 */
		int32 PasOmbrageCellules = 1;

		/** Le point le plus bas du monde, qui norme le degrade de profondeur. */
		float FondM = -300.0f;

		/**
		 * Faut-il agreger les cellules qu'un pixel recouvre ?
		 *
		 * `Auto` decide d'apres l'echelle, et c'est ce qu'on veut partout. Les
		 * deux autres existent pour les oracles : une propriete ne se prouve
		 * qu'en comparant les deux branches sur la MEME entree.
		 */
		enum class EAgregation : uint8 { Auto, Jamais, Toujours };
		EAgregation Agregation = EAgregation::Auto;

		/** La fenetre carree : celle de la minimap, et celle des cinq oracles. */
		static FParamsFenetre Carree(double DemiM, int32 Res)
		{
			FParamsFenetre P;
			P.DemiPorteeXm = DemiM;
			P.DemiPorteeYm = DemiM;
			P.ResX = Res;
			P.ResY = Res;
			return P;
		}

		/**
		 * Le rectangle : UNE demi-portee, et l'autre SE DEDUIT du nombre de
		 * pixels. Les pixels sont donc carres par CONSTRUCTION, et non par
		 * discipline -- il n'existe aucune facon d'exprimer une carte etiree.
		 */
		static FParamsFenetre Rectangle(double DemiXm, int32 ResX, int32 ResY)
		{
			FParamsFenetre P;
			P.ResX = FMath::Max(ResX, 1);
			P.ResY = FMath::Max(ResY, 1);
			P.DemiPorteeXm = DemiXm;
			P.DemiPorteeYm = DemiXm * static_cast<double>(P.ResY)
				/ static_cast<double>(P.ResX);
			return P;
		}

		double MetresParPixelX() const
		{
			return (2.0 * DemiPorteeXm) / static_cast<double>(FMath::Max(ResX, 1));
		}

		double MetresParPixelY() const
		{
			return (2.0 * DemiPorteeYm) / static_cast<double>(FMath::Max(ResY, 1));
		}

		/** Ce que les deux fabriques garantissent, et qu'un remplissage a la main peut casser. */
		bool PixelsCarres() const
		{
			return FMath::IsNearlyEqual(MetresParPixelX(), MetresParPixelY(),
				FMath::Max(MetresParPixelX() * 1e-6, UE_DOUBLE_KINDA_SMALL_NUMBER));
		}
	};

	/**
	 * Ou tombe le centre d'un pixel, en metres du monde.
	 *
	 * ELLE EST ISOLEE PARCE QUE C'EST ELLE QUE LE TEST VISE. Trois signes
	 * s'enchainent entre un lacet de camera et un pixel d'ecran, et se tromper
	 * sur l'un d'eux donne une carte qui a l'air juste jusqu'a ce qu'on marche.
	 *
	 * ET L'INVERSION DU NORD VIT ICI, UNE SEULE FOIS. La carte du monde a sa
	 * LIGNE 0 AU SUD ; une texture, elle, montre sa ligne 0 EN HAUT. Peinte
	 * telle quelle, la minimap aurait donc le nord en bas. Le sens vertical de
	 * ce depot se MESURE -- il ne se deduit pas -- et pas sur une calotte
	 * polaire, qui existe aux deux poles.
	 */
	WORLDSEED_API void MetresDuPixel(const FParamsFenetre& P, int32 PX, int32 PY,
		double& OutXm, double& OutYm);

	/**
	 * L'INVERSE EXACT de `MetresDuPixel`, et la fonction la plus porteuse d'ici.
	 *
	 * Elle sert QUATRE choses qui, sans elle, reformuleraient chacune la meme
	 * projection : la region UV de la brosse a l'ecran, la position d'un
	 * marqueur, le clic qui redevient une coordonnee du monde, et le zoom
	 * centre sur le curseur. L'ecrire une fois est ce qui empeche l'inversion
	 * nord/sud d'etre reecrite ailleurs -- l'en-tete de ce fichier l'interdit
	 * en toutes lettres, et le depot a paye ce qu'une formule recopiee coute.
	 *
	 * `LargeurMondeM` sert a L'ENROULEMENT, et elle n'est pas facultative en
	 * pratique : le monde reboucle en longitude, donc un point peut etre a la
	 * fois « tres a l'est » et « juste a l'ouest ». On retient le representant
	 * le plus proche du centre de la fenetre, sans quoi le marqueur du joueur
	 * saute hors de l'ecran des que la vue approche le meridien de bordure.
	 * Zero desarme l'enroulement.
	 *
	 * @return vrai si le point tombe DANS la fenetre. La sortie est ecrite dans
	 *         tous les cas : un marqueur hors champ a encore une direction.
	 */
	WORLDSEED_API bool PixelDuMetre(const FParamsFenetre& P, double LargeurMondeM,
		double Xm, double Ym, double& OutPX, double& OutPY);

	/**
	 * L'ecart, en metres, entre les deux points que l'ombrage compare.
	 *
	 * IL EST BORNE PAR LE PIXEL, ET C'EST NYQUIST, PAS UNE QUESTION D'UNITES.
	 * A trois cellules par pixel, comparer deux points distants d'UNE cellule
	 * echantillonne une frequence que l'image ne porte plus : le relief sort en
	 * poivre et sel. Le commentaire d'origine -- « une VRAIE pente : l'ombrage
	 * ne depend pas de la resolution » -- a raison sur les unites et tort sur
	 * l'echantillonnage.
	 *
	 * Fonction pure, et exposee pour cette seule raison : c'est elle que
	 * l'oracle vise.
	 */
	WORLDSEED_API double PasOmbrageMetres(const FParamsFenetre& P,
		const FWorldseedGeometry& Geo);

	/** Le point le plus bas du monde. Norme le degrade de bathymetrie. */
	WORLDSEED_API float FondDuMonde(const TArray<float>& ElevationM);

	// ------------------------------------------------- le trait de frontiere
	//
	// LA CARTE ET LE GLOBE PEIGNENT LEURS FRONTIERES SEPAREMENT -- l'une sur
	// une projection a plat, l'autre par lancer de rayon -- mais la REGLE du
	// trait doit etre unique, sans quoi la meme frontiere se lit autrement
	// selon l'ecran qui la montre. C'est la regle de ce depot, et elle a deja
	// ete payee plus d'une fois.
	//
	// ⚠ UN TRAIT DE COULEUR FIXE NE PEUT PAS ETRE LISIBLE PARTOUT. Le premier
	// jet etait un presque-noir melange a opacite fixe : parfait sur du sable,
	// invisible sur une foret sombre, et deux fois pire sur le globe, dont le
	// terrain est OMBRE. Un trait ne se definit donc pas par sa couleur mais
	// par son ECART a ce qu'il traverse.

	/**
	 * Luminance percue, canaux et resultat dans [0..1].
	 *
	 * Ponderation Rec. 601 : l'oeil voit le vert cinq fois plus que le bleu,
	 * et une moyenne arithmetique rendrait un trait « lisible » sur le papier
	 * et invisible a l'ecran.
	 */
	FORCEINLINE float Luminance01(float R, float G, float B)
	{
		return 0.299f * R + 0.587f * G + 0.114f * B;
	}

	/**
	 * La luminance que doit prendre un trait pose sur ce fond.
	 *
	 * IL CHOISIT SON SENS : il assombrit un fond clair, il eclaircit un fond
	 * sombre. C'est ce qui le rend lisible des deux cotes de la mediane --
	 * une frontiere qui longe une cote traverse du sable a 0,8 et de la foret
	 * a 0,2 dans la meme minute.
	 *
	 * Le fond ne laisse pas toujours la place : sur un blanc de calotte, il
	 * n'y a rien au-dessus, donc on descend meme si l'ecart demande est grand.
	 * La bascule se fait au MILIEU de la plage restante, jamais a 0,5 fixe,
	 * pour que le trait garde son ecart au lieu d'etre ecrete contre 0 ou 1.
	 */
	FORCEINLINE float ViserLEcart01(float LumFond, float Ecart)
	{
		const bool bPlaceEnDessous = (LumFond >= Ecart);
		const bool bPlaceAuDessus = (LumFond + Ecart <= 1.0f);

		if (bPlaceEnDessous && bPlaceAuDessus)
		{
			// Les deux sens tiennent : on assombrit, parce qu'un trait sombre
			// se lit comme un trait et un trait clair comme une route.
			return LumFond - Ecart;
		}
		return bPlaceEnDessous ? (LumFond - Ecart) : (LumFond + Ecart);
	}

	/**
	 * L'ecart de luminance d'un trait de PAYS, puis de REGION.
	 *
	 * C'EST ICI QUE VIT LA HIERARCHIE, et non plus dans l'opacite : une
	 * limite d'Etat est grasse, une limite de province legere -- exactement ce
	 * que fait une carte reelle. L'exprimer en ecart plutot qu'en opacite la
	 * rend vraie sur TOUS les fonds ; en opacite, elle ne valait que sur les
	 * fonds clairs.
	 *
	 * LES VALEURS SONT CALEES SUR CE QUE L'ANCIEN TRAIT ATTEIGNAIT DE MIEUX,
	 * et non choisies a vue. Le presque-noir a opacite fixe donnait, en ecart
	 * de luminance mesure :
	 *
	 *     fond             pays      region
	 *     sable (0,745)    0,617     0,235      <- son meilleur cas
	 *     foret (0,224)    0,147     0,037
	 *     globe ombre      0,036     ~0,01      <- invisible, d'ou ce chantier
	 *
	 * Un premier jet a 0,38 et 0,20 rendait donc le trait PLUS PALE qu'avant
	 * sur le sable -- vu a l'image, planche A/B agrandie : le pire cas etait
	 * corrige et le meilleur degrade. Une correction qui ameliore un bout et
	 * abime l'autre n'en est pas une.
	 *
	 * ⚠ L'OPACITE RABOTE L'ECART, et il faut en tenir compte : un trait pose
	 * a 85 % ne deplace que 85 % du chemin. Les valeurs ci-dessous sont donc
	 * les ecarts DEMANDES ; les ecarts OBTENUS valent 0,58 x 0,95 = 0,55 pour
	 * un pays et 0,36 x 0,85 = 0,31 pour une region -- c'est-a-dire l'ancien
	 * meilleur cas, desormais garanti partout au lieu de n'etre vrai que sur
	 * les fonds clairs.
	 */
	constexpr float EcartFrontierePays01 = 0.58f;
	constexpr float EcartFrontiereRegion01 = 0.36f;

	/** Ce que le trait laisse voir du terrain qu'il traverse. */
	constexpr float OpaciteFrontierePays = 0.95f;
	constexpr float OpaciteFrontiereRegion = 0.85f;

	/** Ce qu'un pixel trouve quand il recouvre plusieurs cellules. */
	struct WORLDSEED_API FBlocCellule
	{
		float AltitudeMoyenneM = 0.0f;
		uint8 BiomeMajoritaire = 0;
		uint8 CoverMajoritaire = 0;

		/** Zero quand le bloc tombe hors du monde : rien n'a ete lu. */
		int32 NbCellules = 0;
	};

	/**
	 * Ce que porte le rectangle de monde qu'un pixel recouvre.
	 *
	 * POURQUOI ELLE EXISTE, ET CE N'EST PAS POUR LA CARTE PLEIN ECRAN. Le cran
	 * de six kilometres de la minimap prend deja 46,9 m par pixel pour une
	 * maille de 15,6 : TROIS cellules par pixel, lues par un PRELEVEMENT
	 * PONCTUEL. La grille d'echantillonnage glisse d'un tiers de pixel a chaque
	 * pas du joueur -- ca fourmille en marchant, et les iles d'une cellule
	 * apparaissent et disparaissent.
	 *
	 * L'ALTITUDE SE MOYENNE, L'IDENTIFIANT SE VOTE. C'est la regle du depot, et
	 * elle a ete payee : le filtre bilineaire de PCG sur une carte de biomes
	 * rendait 307 points faux sur 17956, du type « plage » lu comme « alpin ».
	 * La moyenne de « desert » et de « toundra » n'est pas un biome
	 * intermediaire, c'est un biome qui n'existe nulle part.
	 *
	 * ET LE VOTE EST CONJOINT SUR LE COUPLE (biome, couverture). Voter
	 * separement fabriquerait une paire qui n'existe dans AUCUNE cellule du
	 * bloc -- « foret » majoritaire plus « neige » majoritaire, quand toute la
	 * neige etait sur les cellules de toundra. C'est la meme faute que la
	 * moyenne d'identifiants, sous un autre costume.
	 *
	 * `LargeurM` et `HauteurM` sont l'emprise du pixel, pas un rayon.
	 */
	WORLDSEED_API FBlocCellule AgregerBloc(const FWorldseedGeometry& Geo,
		const TArray<float>& ElevationM, const FWorldseedBiomeMap& Biomes,
		double CentreXm, double CentreYm, double LargeurM, double HauteurM);

	/** Cellules par pixel. Sous deux, la bilineaire reste le bon outil. */
	WORLDSEED_API double CellulesParPixel(const FParamsFenetre& P,
		const FWorldseedGeometry& Geo);

	/**
	 * La couleur d'une cellule : son biome si elle emerge, sa profondeur sinon.
	 *
	 * PARTAGEE AVEC `ProbeCarte`, qui peignait cette rampe pour son compte.
	 * Deux autres rampes existent dans le depot -- celles du globe -- et elles
	 * ne sont d'accord ni entre elles ni avec celle-ci. Les unifier changerait
	 * l'apparence du globe du menu : c'est un vrai changement visuel, qui
	 * merite son propre avant/apres, et non un effet de bord.
	 */
	WORLDSEED_API FLinearColor CouleurCellule(float AltitudeM, uint8 BiomeIndex,
		uint8 Cover, float FondM);

	/**
	 * Peint la fenetre dans un tampon BGRA de `Res * Res * 4` octets.
	 *
	 * BGRA parce que c'est l'ordre de `PF_B8G8R8A8`, le format des textures de
	 * ce depot. Intervertir R et B donne un monde bleu, ce qui se voit du
	 * premier coup d'oeil ; intervertir G et B donne un monde PLAUSIBLE, ce qui
	 * ne se voit pas.
	 *
	 * LES TROIS SOURCES SONT DES REFERENCES D'ARGUMENT, pas des pointeurs
	 * ranges dans `FParamsFenetre` -- et c'est deliberе. Une structure voyage,
	 * survit a son appel et finit un jour capturee par un fil ; ce depot a paye
	 * ce piege sur les primitives de grottes capturees par le mailleur, et la
	 * regle qui en est sortie tient en une ligne : rien ne doit tenir ce qui
	 * peut mourir avant lui. Ici la peinture est synchrone et ne retient rien.
	 * C'est aussi le decoupage de `WorldseedGlobe::Render`.
	 *
	 * ⚠ `Regions` EST LE SEUL POINTEUR, et il est nullable a dessein : un monde
	 * peut n'en porter aucune -- cache d'une version anterieure, monde sans
	 * terre, fixture de test -- et c'est un ETAT VALIDE, pas une erreur. Les
	 * deux drapeaux de frontiere sont alors sans effet plutot que de faire
	 * crier la peinture. Il ne survit pas a l'appel, comme les trois autres.
	 */
	WORLDSEED_API void PeindreFenetre(const FWorldseedGeometry& Geo,
		const TArray<float>& ElevationM, const FWorldseedBiomeMap& Biomes,
		const FParamsFenetre& P, uint8* PixelsBGRA,
		const FWorldseedRegions* Regions = nullptr);

	/**
	 * La pyramide de reduction d'une image BGRA : les niveaux 1 a N.
	 *
	 * POURQUOI ELLE EST NECESSAIRE, ET C'EST L'IMAGE QUI L'A DIT. La carte est
	 * cuite a la resolution de la grille -- 4096 par 2048 -- et affichee dans
	 * environ 1900 pixels : une reduction de 2,13. Sans niveaux intermediaires,
	 * le GPU preleve un texel sur deux et **le lisere de cote sort pointille**,
	 * les ilots d'une ou deux cellules se brisent en traits. Avec, le trait
	 * reste continu. Verifie a l'image sur la planche de `ProbeCarteEcran`,
	 * vignettes 1 et 2 : c'est exactement cet A/B.
	 *
	 * MOYENNER DES COULEURS DEJA PEINTES N'EST PAS MOYENNER UN IDENTIFIANT.
	 * La regle du depot porte sur l'IDENTIFIANT -- la moyenne de « desert » et
	 * de « toundra » est un biome qui n'existe nulle part -- et c'est pour cela
	 * que `AgregerBloc` vote. Ici on reduit une IMAGE deja rendue, ce qu'un
	 * mipmap fait par construction ; aucun biome n'est relu depuis ces pixels.
	 *
	 * ET LA MOYENNE SE FAIT SUR LES OCTETS, DONC EN sRGB. Moyenner en lumiere
	 * -- linearise puis reencode -- serait plus juste pour de l'eclairage, et
	 * ferait ici DISPARAITRE le lisere : un trait tres sombre ne pese presque
	 * rien en lineaire. Sur une carte, ce qu'on veut garder est precisement ce
	 * trait. Le choix est donc delibere, pas une simplification.
	 *
	 * Chaque niveau fait `max(1, Cote >> k)`, la regle des mipmaps, y compris
	 * pour une taille qui n'est pas une puissance de deux.
	 */
	WORLDSEED_API void CuireReductions(const uint8* BaseBGRA, int32 BaseX,
		int32 BaseY, TArray<TArray<uint8>>& OutNiveaux);

	/**
	 * Peint le cone de visee dans SON PROPRE tampon, transparent ailleurs.
	 *
	 * POURQUOI PAS DANS LE MEME TAMPON QUE LE FOND. On ne peut pas DES-estamper :
	 * deplacer le cone d'un degre imposerait de repeindre tout le fond dessous,
	 * donc de payer le fond a la cadence du cone. Et un cone fondu dans le fond
	 * cesse d'etre une fonction pure : il ne serait plus testable sans monde,
	 * sans Slate et sans partie en cours -- or l'orientation d'un cone est
	 * exactement ce qu'on casse le plus facilement.
	 *
	 * `AzimutDeg` suit la convention de la boussole du depot : zero au NORD,
	 * croissant vers l'EST.
	 */
	WORLDSEED_API void PeindreCone(uint8* PixelsBGRA, int32 Res, float AzimutDeg,
		float DemiAngleDeg, float RayonFraction);
}
