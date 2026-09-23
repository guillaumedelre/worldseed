#pragma once

#include "CoreMinimal.h"

#include "Procedural/WorldseedApparence.h"
#include "ProceduralMeshComponent.h"

struct FWorldseedGeometry;
struct FWorldseedWorldData;

/**
 * LE SOL DE FOND : LE RELIEF DU MONDE ENTIER, EN UNE NAPPE GROSSIERE.
 *
 * IL Y EN A DEUX, ET LES CONFONDRE COUTE CHER. Une nappe sert l'EAU -- le
 * plugin Water y lit le relief pour sa texture d'information -- et l'autre
 * sert l'IMAGE, c'est-a-dire l'horizon au-dela du rayon voxel. Les separer a
 * ete paye comptant le 21 septembre 2026 : tant qu'une seule servait les deux,
 * la cacher a l'entree d'une grotte coutait 520 ms de recreation de proxy, et
 * la masquer entierement coupait l'ocean SANS le moindre avertissement.
 *
 * LE PIEGE QUI TUE L'OCEAN, ET IL EST SILENCIEUX. La `WaterZone` agrege les
 * bornes en Z de tous les corps d'eau en UN intervalle, dans lequel la texture
 * d'information normalise chaque hauteur. Un ocean d'epaisseur nulle donne
 * `[0 .. 0]` et l'eau cesse de se dessiner. La ligne de journal
 * `eau : ... hauteurs d'eau [%.0f .. %.0f] m` est le controle qui tranche --
 * elle doit valoir [-371 .. 371] et jamais [0 .. 0].
 */
namespace WorldseedNappe
{
	/**
	 * De combien CE sommet de la nappe vue a le droit de descendre.
	 *
	 * LA NAPPE VUE S'ENFONCE POUR NE BOUCHER AUCUNE CAVITE : le voxel ne creuse
	 * que dans une bande sous la surface, et un decor pose sous cette bande ne
	 * peut rien obstruer. Mais elle porte le relief du monde ENTIER, et un
	 * enfoncement uniforme fait passer sous le niveau de la mer tout ce qui
	 * culmine plus bas que lui -- l'ocean, plan a l'altitude zero, le recouvre
	 * alors. Mesure du 22 septembre : **16,3 % des terres** ainsi noyees, une
	 * vallee verte se lisant comme une baie depuis un sommet.
	 *
	 * LE PLAFOND EST EXACTEMENT LA BONNE BORNE, et ce n'est pas un reglage
	 * heureux. Aucune chambre n'existe sous `profondeurMax + rayonMax +
	 * margeMer` d'altitude -- soixante-six metres sur ce monde -- donc tout
	 * plancher de cavite se trouve AU-DESSUS de la marge de mer, partout. Une
	 * nappe qui s'arrete a cette marge reste dessous sans creuser plus loin :
	 * les deux contraintes se rejoignent au lieu de s'opposer.
	 *
	 * ET IL EPINGLE LE RIVAGE, ce qui supprime le seul risque serieux de la
	 * rampe. L'enfoncement suit la camera, donc le relief lointain « respire »
	 * quand on marche ; sur un trait de cote cela se verrait. Or ce plafond
	 * vaut ZERO au niveau de la mer : le rivage ne bouge pas, par construction.
	 * Mesure sur 200 m parcourus : silhouette lointaine deplacee de 10,8 px
	 * avec la rampe contre 13,0 sans, donc moins que la parallaxe de la marche.
	 *
	 * @param AltitudeM     altitude du sommet, en metres, zero au niveau de la mer
	 * @param MargeMerM     la marge sous laquelle on ne descend jamais
	 * @param EnfoncementM  l'enfoncement plein, celui qui passe sous la bande
	 * @return de zero a `EnfoncementM`, JAMAIS negatif -- on n'eleve pas un sommet
	 */
	inline double PlafondDEnfoncement(double AltitudeM, double MargeMerM,
		double EnfoncementM)
	{
		return FMath::Clamp(AltitudeM - MargeMerM, 0.0,
			FMath::Max(EnfoncementM, 0.0));
	}
}

/**
 * Ce qu'il faut savoir pour batir une nappe, et rien de l'acteur.
 *
 * Les trois grandeurs metriques arrivent DEJA RESOLUES : l'appelant a
 * confronte ses reglages au champ de densite -- le retrait doit couvrir le
 * deplacement que le voxel applique a la surface, l'enfoncement doit passer
 * sous tout ce que le voxel peut creuser -- et la nappe n'a pas a relire les
 * regles pour cela.
 */
struct WORLDSEED_API FWorldseedNappeRegles
{
	/** Nombre de sommets en X. Le Y suit le rapport de la carte. */
	int32 Largeur = 512;

	/** De combien la nappe passe SOUS la surface, en metres. */
	double RetraitM = 12.0;

	/** L'exageration verticale du terrain, qu'il faut suivre. */
	float ExagerationZ = 1.0f;

	/** Enfoncement supplementaire de la nappe VUE, en metres. */
	double SurEnfoncementM = 125.0;

	/** Au-dessus de cette altitude, l'enfoncement cesse : le rivage est epingle. */
	double MargeMerM = 5.0;

	/** Decimation de la nappe vue : un sommet sur `Pas`. */
	int32 Pas = 2;

	bool bInverserEnroulement = false;
};

/** Les tableaux qu'un `UProceduralMeshComponent` attend, et rien d'autre. */
struct WORLDSEED_API FWorldseedNappeMaillage
{
	int32 CountX = 0;
	int32 CountY = 0;

	TArray<FVector> Positions;
	TArray<FVector> Normales;
	TArray<FVector2D> UV0;
	TArray<FVector2D> TeinteRG;
	TArray<FVector2D> TeinteB;
	TArray<FProcMeshTangent> Tangentes;
	TArray<FLinearColor> Couleurs;
	TArray<int32> Triangles;

	/** Un sommet par bit : son relief est-il sous le niveau de la mer ? */
	TArray<uint8> SousZero;

	int32 Sommets() const { return CountX * CountY; }
};

/**
 * Ce que la nappe a mesure en se batissant.
 *
 * ON COMPTE CE QU'ON PEINT : une couleur qui ne se voit pas a deux causes
 * opposees -- le terme ne s'evalue jamais, ou il s'evalue et rien ne l'affiche
 * -- et elles n'appellent pas du tout le meme remede.
 */
struct WORLDSEED_API FWorldseedNappeReleve
{
	int32 Sommets = 0;
	int32 SommetsSousZero = 0;
	int32 SommetsEmerges = 0;

	/** Combien de sommets de TERRE l'enfoncement de la vue aplatirait. */
	int32 SommetsNoyesParLaVue = 0;

	/** La courbe du noyage selon l'enfoncement : 25, 50, 75, 100, 125, 150 m. */
	static constexpr int32 NbPaliers = 6;
	int32 ParPalier[NbPaliers] = {};

	/** De combien le plus mauvais sommet depasse le plancher de son voisinage. */
	float PireDepassementM = 0.0f;
};

namespace WorldseedNappe
{
	/**
	 * Bat la nappe PLEINE, celle que l'eau lit.
	 *
	 * ELLE PREND LE PLANCHER DE SON VOISINAGE, PAS SON PROPRE RELIEF. Un
	 * echantillonnage par saut laisserait la nappe passer AU-DESSUS du terrain
	 * detaille entre deux de ses sommets -- elle affleurerait au milieu des
	 * chunks. En prenant le minimum du voisinage couvert, elle passe dessous
	 * partout, et `PireDepassementM` dit de combien la garde a servi.
	 */
	WORLDSEED_API void Batir(const FWorldseedWorldData& Monde,
		const FWorldseedGeometry& Geo, const TArray<float>& Heights,
		const FWorldseedSurfaceRegles& ReglesSurf,
		const FWorldseedAppearance& Mode, const FWorldseedNappeRegles& R,
		FWorldseedNappeMaillage& Out, FWorldseedNappeReleve& Releve);

	/**
	 * Decime la nappe pleine pour en tirer celle qu'on VOIT, et la coupe en
	 * deux sections -- la terre et la mer.
	 *
	 * DEUX SECTIONS PARCE QUE LA MER DU DECOR A SON PROPRE MATERIAU : au-dela
	 * de la fenetre du plugin Water, ce qui se voit n'est plus de l'eau mais
	 * le sol de fond, et le peindre en plage laisse un trait droit en travers
	 * des dunes.
	 *
	 * L'ENFONCEMENT N'EST PAS APPLIQUE ICI : il part dans le canal UV3, et
	 * c'est le materiau qui le fait descendre en fonction de la distance a la
	 * camera. Une nappe enfoncee en dur cacherait l'horizon sur une bande de
	 * plusieurs kilometres -- mesure : 16,3 % des terres noyees a 125 m.
	 */
	WORLDSEED_API void Decimer(const FWorldseedNappeMaillage& Source,
		const FWorldseedNappeRegles& R, FWorldseedNappeMaillage& Out,
		TArray<FVector2D>& OutPlafondCm,
		TArray<int32>& OutTrianglesTerre, TArray<int32>& OutTrianglesMer);
}
