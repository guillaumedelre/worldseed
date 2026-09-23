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

		/** Demi-cote de la fenetre, en metres. */
		double DemiPorteeM = 2000.0;

		/** Cote du tampon, en pixels. Il est carre. */
		int32 Res = 256;

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

		double MetresParPixel() const
		{
			return (2.0 * DemiPorteeM) / static_cast<double>(FMath::Max(Res, 1));
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
