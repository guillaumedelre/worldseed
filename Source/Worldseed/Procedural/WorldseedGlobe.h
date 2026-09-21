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
	/**
	 * Le point de depart choisi par le joueur, marque sur le globe.
	 *
	 * EN LATITUDE ET LONGITUDE, JAMAIS EN PIXEL : le globe tourne et se zoome,
	 * donc un pixel retenu serait faux des la trame suivante. C'est aussi la
	 * seule forme qui survit au voyage jusqu'a la carte de jeu.
	 */
	struct FRepereGlobe
	{
		bool bActif = false;
		float LatitudeDeg = 0.0f;
		float LongitudeDeg = 0.0f;
	};

	struct WORLDSEED_API FGlobeSettings
	{
		/** Rotation autour de l'axe polaire, en degres. */
		float LongitudeOffsetDeg = 0.0f;

		/**
		 * Le point choisi, s'il y en a un. Il vit ici plutot que dans la
		 * signature du rendu : c'est une facon de dessiner le globe, au meme
		 * titre que les cercles de latitude, et l'ajouter aux parametres
		 * aurait touche quatre sites d'appel pour rien.
		 */
		FRepereGlobe Repere;

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

	// ------------------------------------------------- la projection, et son inverse
	//
	// LE RENDU, LE POINTAGE ET LE REPERE EMPLOIENT LE MEME CODE, et ce n'est
	// pas un gout : ce depot a une regle contre les formules recopiees, et il
	// l'a payee plus d'une fois -- l'amplitude saisonniere recalculee dans un
	// second fichier, le classificateur de biomes reimplemente par le bulletin
	// terrestre, qui validait donc une COPIE du classificateur et non le
	// classificateur. Ici l'enjeu est immediat : si le pointage derive du
	// rendu d'un demi-degre, le repere ne tombe plus sous le curseur.
	//
	// LE CADRE NORMALISE va de -1 a +1 sur les deux axes, X vers la DROITE et
	// Y vers le HAUT -- convention d'ecran inversee en Y par rapport aux
	// lignes de la texture, ce dont s'occupe CadreDepuisUV.

	/**
	 * Rayon du disque dans le cadre normalise.
	 *
	 * Moins de 1 pour que le globe ne touche pas les bords de sa texture : le
	 * bord est fondu sur un pixel, et un disque colle au cadre n'aurait pas la
	 * place de l'etre.
	 */
	constexpr float RayonDisque = 0.92f;

	/**
	 * Ce qui ne depend PAS du pixel, calcule une fois et passe a chaque point.
	 *
	 * Sans cette separation, partager la formule couterait deux appels
	 * trigonometriques par pixel, soit un million sur une texture de 1024 --
	 * la mutualisation ne doit pas se payer en temps de rendu.
	 */
	struct FCadreGlobe
	{
		float CosTilt = 1.0f;
		float SinTilt = 0.0f;
		float LongitudeOffsetDeg = 0.0f;
	};

	FORCEINLINE FCadreGlobe CadreGlobe(const FGlobeSettings& Settings)
	{
		const float Rad = FMath::DegreesToRadians(Settings.TiltDeg);
		FCadreGlobe C;
		C.CosTilt = FMath::Cos(Rad);
		C.SinTilt = FMath::Sin(Rad);
		C.LongitudeOffsetDeg = Settings.LongitudeOffsetDeg;
		return C;
	}

	struct FPointeGlobe
	{
		/** Faux si le point tombe hors du disque, donc dans l'espace. */
		bool bSurLeGlobe = false;

		float LatitudeDeg = 0.0f;

		/** Ramenee dans [0, 360[. */
		float LongitudeDeg = 0.0f;

		/**
		 * Distance au centre rapportee au rayon du disque. Renseignee MEME
		 * hors du globe : c'est elle qui fond le bord sur un pixel.
		 */
		float Rayon01 = 0.0f;

		/**
		 * Le point sur la sphere unite, dans le repere de l'OBSERVATEUR.
		 *
		 * Le rendu s'en sert pour batir le repere tangent de son ombrage. Il
		 * est rendu ici plutot que recalcule pour la meme raison que tout le
		 * reste de cette paire : deux ecritures de la meme formule divergent.
		 */
		FVector Normale = FVector::ZeroVector;
	};

	/** Un point du cadre normalise -> la sphere. */
	FORCEINLINE FPointeGlobe PointerCadre(float CadreX, float CadreY, const FCadreGlobe& C)
	{
		const float NX = CadreX / RayonDisque;
		const float NY = CadreY / RayonDisque;
		const float R2 = NX * NX + NY * NY;

		FPointeGlobe P;
		P.Rayon01 = FMath::Sqrt(R2);
		if (R2 > 1.0f)
		{
			return P;
		}

		// Point de la sphere unite face a l'observateur.
		const float NZ = FMath::Sqrt(FMath::Max(0.0f, 1.0f - R2));

		// On bascule l'axe polaire vers l'observateur : sans cela on ne verrait
		// jamais un pole, donc jamais la calotte.
		const float AxisY = NY * C.CosTilt - NZ * C.SinTilt;
		const float AxisZ = NY * C.SinTilt + NZ * C.CosTilt;

		P.bSurLeGlobe = true;
		P.Normale = FVector(NX, NY, NZ);
		P.LatitudeDeg = FMath::RadiansToDegrees(
			FMath::Asin(FMath::Clamp(AxisY, -1.0f, 1.0f)));

		const float Lon = FMath::RadiansToDegrees(FMath::Atan2(NX, AxisZ))
			+ C.LongitudeOffsetDeg;
		P.LongitudeDeg = FMath::Fmod(FMath::Fmod(Lon, 360.0f) + 360.0f, 360.0f);
		return P;
	}

	/**
	 * L'INVERSE EXACT : la sphere -> le cadre normalise.
	 *
	 * Rend FAUX quand le point est sur la face CACHEE. Sans ce test un repere
	 * pose de l'autre cote de la planete se dessinerait quand meme, par-dessus
	 * le relief qui est cense le masquer -- et l'on croirait a une erreur de
	 * projection alors que la projection serait juste.
	 *
	 * La bascule est une rotation, donc son inverse est sa TRANSPOSEE : d'ou
	 * les memes cosinus et sinus, avec le signe du terme croise change.
	 */
	FORCEINLINE bool CadreDepuisLatLon(float LatitudeDeg, float LongitudeDeg,
		const FCadreGlobe& C, float& OutCadreX, float& OutCadreY)
	{
		const float LatRad = FMath::DegreesToRadians(LatitudeDeg);
		const float Theta = FMath::DegreesToRadians(LongitudeDeg - C.LongitudeOffsetDeg);

		const float AxisY = FMath::Sin(LatRad);
		const float CosLat = FMath::Cos(LatRad);
		const float NX = CosLat * FMath::Sin(Theta);
		const float AxisZ = CosLat * FMath::Cos(Theta);

		const float NY = AxisY * C.CosTilt + AxisZ * C.SinTilt;
		const float NZ = -AxisY * C.SinTilt + AxisZ * C.CosTilt;

		OutCadreX = NX * RayonDisque;
		OutCadreY = NY * RayonDisque;
		return NZ > 0.0f;
	}

	/**
	 * Coordonnees de TEXTURE (0..1, V vers le BAS) -> cadre normalise.
	 *
	 * C'est le seul endroit ou l'inversion de l'axe vertical est ecrite. Une
	 * seconde ecriture ailleurs retournerait le globe sans rien casser
	 * d'autre, donc sans se signaler autrement qu'a l'oeil.
	 */
	FORCEINLINE void CadreDepuisUV(float U01, float V01, float& OutX, float& OutY)
	{
		OutX = U01 * 2.0f - 1.0f;
		OutY = 1.0f - V01 * 2.0f;
	}

	/** Cadre normalise -> pixel de la texture. L'inverse de CadreDepuisUV. */
	FORCEINLINE void PixelDepuisCadre(float CadreX, float CadreY, int32 Res,
		float& OutPX, float& OutPY)
	{
		const float Demi = 0.5f * static_cast<float>(Res - 1);
		OutPX = (CadreX + 1.0f) * Demi;
		OutPY = (1.0f - CadreY) * Demi;
	}

	/**
	 * Construit la texture du globe. Heights est le heightfield en METRES avec
	 * 0 au niveau de la mer, comme le produit la chaine tectonique.
	 */
	WORLDSEED_API UTexture2D* Render(const TArray<float>& Heights,
		const FWorldseedGeometry& Geometry, const FGlobeSettings& Settings,
		int32 PreviewResolution, const TArray<uint8>* BiomeIndex = nullptr,
		const TArray<uint8>* CoverIndex = nullptr);

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
		const TArray<uint8>* BiomeIndex = nullptr,
		const TArray<uint8>* CoverIndex = nullptr);
}
