// Worldseed - ou poser les noms de pays et de region sur une carte.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedCarte.h"

struct FWorldseedRegions;

/**
 * LE CHOIX DES ETIQUETTES, ECRIT UNE FOIS POUR LES DEUX CARTES.
 *
 * La minimap et la carte plein ecran montrent le MEME monde a deux echelles.
 * Deux implementations du meme choix divergeraient a la premiere retouche, et
 * l'ecart se verrait precisement la ou l'on compare les deux -- c'est la
 * regle de ce depot, et elle a deja ete payee : l'amplitude saisonniere
 * recalculee dans un second fichier, le classificateur de biomes reimplemente
 * par le bulletin terrestre.
 *
 * CE QUE CE MODULE NE FAIT PAS : dessiner. Il ne connait ni police, ni
 * couleur, ni Slate. Il rend des positions en pixels et des textes ; la vue
 * mesure et peint.
 */
namespace WorldseedEtiquettes
{
	/** Un nom a poser, deja projete dans la fenetre. */
	struct WORLDSEED_API FEtiquette
	{
		/** En pixels de la fenetre demandee. */
		FVector2D PositionPx = FVector2D::ZeroVector;

		FString Texte;

		/** Vrai pour un pays : il se dessine plus gros et passe en premier. */
		bool bPays = false;

		/** Diametre apparent de la forme nommee, en pixels. Sert au tri. */
		double DiametrePx = 0.0;
	};

	/** Ce qui decide de montrer un nom ou de le taire. */
	struct WORLDSEED_API FReglages
	{
		/**
		 * Diametre apparent minimal, en pixels, pour qu'une forme soit nommee.
		 *
		 * LE SEUIL EST RELATIF A LA FORME, PAS A L'ECHELLE DE LA CARTE, et
		 * c'est ce qui le rend juste aux deux bouts du zoom : une grande
		 * region apparait tot, une petite attend qu'on s'approche. Un seuil
		 * pose sur les metres par pixel aurait fait apparaitre les
		 * quarante-sept noms d'un coup, ce qui ne fait pas une carte mais un
		 * voile -- le depot a deja ce constat pour ses cinq cents gouffres.
		 *
		 * Cale sur la place qu'un nom occupe : une etiquette fait de l'ordre
		 * de cent pixels de large, et poser un nom sur une forme plus petite
		 * que son propre nom ne designe plus rien.
		 */
		double DiametreMinPx = 130.0;

		/** Idem pour un pays. Plus bas : ils sont peu nombreux et structurants. */
		double DiametreMinPaysPx = 90.0;

		/** Au-dela, on s'arrete. Une carte lisible ne porte pas trente noms. */
		int32 NombreMax = 14;
	};

	/**
	 * Les noms a poser, tries par PRIORITE DECROISSANTE.
	 *
	 * L'ordre compte : la vue pose les premiers et rejette ceux qui
	 * recouvriraient un nom deja pose. Les pays passent avant les regions --
	 * ils structurent la lecture -- puis les grandes formes avant les petites,
	 * parce qu'une grande region a plus de place pour porter son nom et qu'un
	 * nom perdu se remarque moins sur une petite.
	 *
	 * Les positions sont celles de l'ANCRAGE, jamais du centre de gravite :
	 * le barycentre d'une forme concave lui est exterieur, et une etiquette
	 * sur huit tomberait en mer. Voir `FWorldseedRegion::AncrageM`.
	 */
	WORLDSEED_API TArray<FEtiquette> Choisir(const FWorldseedRegions& Regions,
		const WorldseedCarte::FParamsFenetre& Vue, double LargeurMondeM,
		const FReglages& R = FReglages());

	/**
	 * Une boite peut-elle etre posee sans en recouvrir une deja posee ?
	 *
	 * Vrai si elle passe, et la boite est alors ENREGISTREE. Faux sinon, et
	 * rien n'est modifie -- l'appelant n'a qu'a passer au candidat suivant.
	 *
	 * Comparaison lineaire : on pose au plus une quinzaine de noms, et un
	 * index spatial couterait plus cher que le balayage qu'il evite.
	 */
	WORLDSEED_API bool Accepter(TArray<FBox2D>& Occupes, const FBox2D& Boite);
}
