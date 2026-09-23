// Worldseed - peindre une FENETRE de la carte du monde, dans un tampon d'image.

#pragma once

#include "CoreMinimal.h"

#include "Procedural/WorldseedBiomes.h"
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
		 * Ecart, en cellules, entre les voisins que l'ombrage compare.
		 *
		 * A 15,6 m de maille, un ombrage pris sur les cellules immediatement
		 * voisines peut etre bruyant -- le relief de simulation y est a pleine
		 * frequence. Aucun chiffre ne le verra : cela se juge a l'image.
		 */
		int32 PasOmbrageCellules = 1;

		/** Le point le plus bas du monde, qui norme le degrade de profondeur. */
		float FondM = -300.0f;

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
	 */
	WORLDSEED_API void PeindreFenetre(const FWorldseedGeometry& Geo,
		const TArray<float>& ElevationM, const FWorldseedBiomeMap& Biomes,
		const FParamsFenetre& P, uint8* PixelsBGRA);

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
