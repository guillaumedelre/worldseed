// Worldseed - rendu du monde sous forme de globe, pour l'ecran d'entree.

#pragma once

#include "CoreMinimal.h"
#include "Procedural/WorldseedRules.h"

class UTexture2D;

/**
 * Rend la carte du monde sur une sphere, par lancer de rayon CPU dans la
 * texture d'apercu.
 *
 * Pourquoi pas un mesh plus un SceneCapture : cela imposerait un acteur, une
 * camera, une cible de rendu et un materiau a maintenir dans L_Menu. Ici le
 * widget reste une simple UImage et tout le rendu tient dans cette fonction,
 * entierement deterministe.
 *
 * La correspondance latitude <-> ligne de carte est celle du generateur
 * (projection equivalente de Lambert), donc les bandes climatiques tombent au
 * bon endroit. La LONGITUDE, elle, est une convention d'affichage : la carte
 * est carree alors qu'un globe complet demanderait un rapport 2:1, on etale
 * donc l'axe X sur 360 degres pour donner une planete entiere a regarder.
 */
namespace WorldseedGlobe
{
	struct WORLDSEED_API FGlobeSettings
	{
		/** Rotation autour de l'axe polaire, en degres. */
		float LongitudeOffsetDeg = 0.0f;

		/** Inclinaison de l'axe vers l'observateur, en degres. */
		float TiltDeg = 18.0f;

		/** Trace equateur, tropiques et cercles polaires. */
		bool bShowLatitudeLines = true;

		/** Tropique et cercle polaire, en degres (issus de world_rules.json). */
		float TropicDeg = 23.44f;
		float PolarCircleDeg = 66.56f;

		/** Altitude, en metres, au-dela de laquelle la teinte est neigeuse. */
		float SnowStartM = 250.0f;

		/** Profondeur, en metres, du bleu le plus sombre. */
		float DeepOceanM = -300.0f;

		/**
		 * Accentuation du relief. 1 = pentes reelles. Ce n'est PAS un facteur
		 * de rattrapage : l'ombrage est calcule a partir de la pente vraie, en
		 * metres par metre. Ce reglage n'existe que pour exagerer sciemment.
		 */
		float ReliefStrength = 1.0f;
	};

	/**
	 * Construit la texture du globe. Heights est le heightfield en METRES avec
	 * 0 au niveau de la mer, comme le produit la chaine tectonique.
	 */
	WORLDSEED_API UTexture2D* Render(const TArray<float>& Heights,
		const FWorldseedGeometry& Geometry, const FGlobeSettings& Settings,
		int32 PreviewResolution, const TArray<uint8>* BiomeIndex = nullptr);

	/**
	 * Redessine dans une texture existante. C'est cette voie qu'utilise la
	 * rotation : recreer une UTexture2D a chaque frame saturerait le ramasse-
	 * miettes pour rien, seuls les pixels changent.
	 */
	/**
	 * BiomeIndex est FACULTATIF et doit avoir la taille de Heights.
	 *
	 * Fourni, il donne sa couleur a chaque terre ; absent, le globe retombe
	 * sur la teinte d'ALTITUDE, qui etait son seul mode et qui MENTAIT : une
	 * calotte glaciaire posee a trente metres s'affichait au vert des
	 * plaines, et les sommets blancs n'etaient pas de la neige mais de la
	 * hauteur. L'ombrage du relief est garde dans les deux cas -- c'est lui
	 * qui donne sa lecture au globe, et une carte de biomes a plat ne
	 * montrerait plus aucun relief.
	 */
	WORLDSEED_API bool RenderInto(UTexture2D* Texture, const TArray<float>& Heights,
		const FWorldseedGeometry& Geometry, const FGlobeSettings& Settings,
		const TArray<uint8>* BiomeIndex = nullptr);
}
