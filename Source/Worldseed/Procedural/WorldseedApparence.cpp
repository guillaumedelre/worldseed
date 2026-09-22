// Worldseed - l'apparence d'un sommet de terrain : poids, couleur, teinte.

#include "Procedural/WorldseedApparence.h"


namespace WorldseedApparence
{
void Sommet(const FWorldseedWorldData& Monde, const FWorldseedSurfaceRegles& Regles,
	int32 Cell, float HeightM, const FVector& Normal,
	const FWorldseedAppearance& Mode,
	FLinearColor& OutColour, FVector2D& OutTintRG, FVector2D& OutTintB)
{
	// La pente se lit directement sur la composante verticale de la normale :
	// cos(pente), sans arc cosinus.
	const float CosRockStart = FMath::Cos(FMath::DegreesToRadians(Regles.RockSlopeStartDeg));
	const float CosRockFull = FMath::Cos(FMath::DegreesToRadians(Regles.RockSlopeFullDeg));

	const bool bTexturePack = Mode.bTexturePack;
	const bool bColourByBiome = Mode.bColourByBiome;
	const bool bHasClimate = Mode.bHasClimate;
	const bool bHasCover = Mode.bHasCover;

	// --- poids des couches ---------------------------------------
	const float HereM = HeightM;

	// La pente se lit directement sur la composante verticale de la
	// normale : cos(pente). Pas besoin d arc cosinus.
	const float Rock = 1.0f - FMath::GetMappedRangeValueClamped(
		FVector2D(CosRockFull, CosRockStart), FVector2D(0.0f, 1.0f),
		static_cast<float>(Normal.Z));

	float Snow = 0.0f;
	float Vegetation = 0.0f;
	if (bHasClimate)
	{
		const int32 CI = Cell;

		// La neige suit la TEMPERATURE, pas l altitude : un sommet
		// equatorial et une plaine polaire peuvent etre a la meme
		// altitude sans avoir le meme climat.
		Snow = FMath::GetMappedRangeValueClamped(
			FVector2D(Regles.SnowTempC, Regles.SnowTempFullC), FVector2D(0.0f, 1.0f), Monde.TempC[Cell]);

		Vegetation = FMath::GetMappedRangeValueClamped(
			FVector2D(Regles.AridMm, Regles.LushMm), FVector2D(0.0f, 1.0f), Monde.PrecipMm[Cell]);
	}
	else
	{
		// Sans climat on retombe sur l altitude seule, faute de mieux.
		Snow = FMath::GetMappedRangeValueClamped(
			FVector2D(150.0f, 320.0f), FVector2D(0.0f, 1.0f), HereM);
		Vegetation = 1.0f - Snow;
	}

	// Le sable ne tient qu au bord de l eau, et pas sur une falaise.
	const float Beach = FMath::GetMappedRangeValueClamped(
		FVector2D(Regles.BeachTopM, 0.0f), FVector2D(0.0f, 1.0f), HereM)
		* (1.0f - Rock) * (1.0f - Snow);

	// La roche gagne partout ou ca penche : rien ne pousse ni ne tient
	// sur une paroi.
	Vegetation *= (1.0f - Rock) * (1.0f - Snow) * (1.0f - Beach);

	// Par defaut la teinte ne sert pas : le mode poids de couches n'en
	// a que faire, et le mode couleur de biome la met dans RGBA.
	OutTintRG = FVector2D::ZeroVector;
	OutTintB = FVector2D::ZeroVector;

	// --- LE DECOR LOINTAIN DOIT LIRE COMME UNE MER SOUS ZERO --------------
	//
	// SIGNALE EN JEU : « je vois nettement un carre d'ocean autour de moi ».
	// Le bord du carre est la fenetre glissante du plugin Water ; au-dela, il
	// n'y a AUCUNE eau rendue. Le plateau cotier s'y dessinait donc a sec, et
	// son biome le peint en PLAGE -- d'ou une bande de sable pale, coupee par
	// un trait DROIT qui traverse les dunes sans les suivre. Verifie a
	// l'echantillon sur `balayage/4_centre.png` : eau R78 G125 B153, puis
	// plateau R196 G188 B174 de l'autre cote du trait.
	//
	// AGRANDIR LA FENETRE NE SUPPRIME PAS CETTE COUTURE, IL LA DEPLACE. Elle
	// reapparait des qu'un haut-fond clair s'etend au-dela. Peindre en mer ce
	// qui est sous zero la rend invisible PAR CONSTRUCTION, a n'importe quelle
	// distance et depuis n'importe quelle altitude, pour zero appel de dessin.
	//
	// CE QUE CE N'EST PAS : de l'eau. C'est du DECOR qui a la couleur de
	// l'eau. Il n'a ni vague, ni reflet, ni profondeur -- exactement comme le
	// `Water_FarMesh` du plugin, qui remplit le meme role et que nous ne
	// pouvons pas employer ici (sa jupe s'accroche aux bornes de la ZONE,
	// 64 x 32 km, donc hors d'atteinte depuis l'interieur du monde).
	//
	// LA GEOMETRIE N'EST PAS TOUCHEE, seulement la couleur. Le sol de fond est
	// deja ENFONCE sous la bande creusable pour ne rien boucher ; le remonter
	// a zero le ferait ressortir au travers du relief.
	if (Mode.bMerOpaque && bColourByBiome && HereM < 0.0f)
	{
		OutColour = Mode.bMerTemoin
			? FLinearColor(1.0f, 0.0f, 1.0f, 1.0f)
			: WorldseedBiomes::Colour(EWorldseedBiome::Ocean);
		return;
	}

	if (bTexturePack)
	{
		const int32 CI = Cell;

		// LE SUBSTRAT DECIDE DE LA MATIERE, PAS LE CLIMAT. Une paroi en
		// foret tropicale porte desormais "foret tropicale" dans l'axe
		// des biomes ; si on lisait cet axe pour choisir les textures,
		// la falaise se couvrirait d'herbe. AppearanceBiome rend
		// l'identifiant d'avant la separation des deux axes, donc cette
		// ligne peint rigoureusement la meme chose qu'avant.
		const EWorldseedBiome Biome = bHasCover
			? WorldseedBiomes::AppearanceBiome(Monde.Biomes.Index[Cell], Monde.Biomes.Cover[Cell])
			: static_cast<EWorldseedBiome>(Monde.Biomes.Index[Cell]);

		// RGBA porte les POIDS DE MATIERE, jamais une couleur : c'est
		// le materiau qui melange les quatre textures avec.
		OutColour = WorldseedBiomes::SlotWeights(Biome);

		FLinearColor Tint = WorldseedBiomes::Colour(Biome);
		if (bHasCover)
		{
			const EWorldseedCover C = static_cast<EWorldseedCover>(Monde.Biomes.Cover[Cell]);
			if (C != EWorldseedCover::None)
			{
				Tint = FMath::Lerp(Tint,
					WorldseedBiomes::CoverColour(C), Regles.CoverTint);
			}
		}

		// LA TEINTE PART DEJA NORMALISEE EN LUMINANCE.
		//
		// Le materiau se contente alors d'un multiplie : c'est ici, en
		// C++, que le calcul delicat se lit et se verifie, pas dans un
		// graphe de shader. Sans cette normalisation, multiplier par la
		// couleur de reference d'un biome — qui vaut autour de 0,4 —
		// assombrirait le sol : une savane aurait une herbe plus sombre
		// qu'une prairie, alors qu'elle doit seulement etre plus jaune.
		const float Luminance = FMath::Max(
			0.299f * Tint.R + 0.587f * Tint.G + 0.114f * Tint.B, 0.01f);

		// Borne haute : un biome tres sombre et tres sature donnerait
		// sinon un facteur enorme sur un seul canal, et un sol fluo.
		const FLinearColor Normalised(
			FMath::Min(Tint.R / Luminance, 2.5f),
			FMath::Min(Tint.G / Luminance, 2.5f),
			FMath::Min(Tint.B / Luminance, 2.5f));

		OutTintRG = FVector2D(Normalised.R, Normalised.G);
		OutTintB = FVector2D(Normalised.B, 0.0f);
	}
	else if (bColourByBiome)
	{
		// LA COULEUR DU BIOME REMPLACE LES POIDS, elle ne s'y ajoute
		// pas : les deux occupent les memes quatre canaux, et un
		// materiau ne peut pas deviner lequel il recoit.
		const int32 CI = Cell;
		FLinearColor Tint = WorldseedBiomes::Colour(bHasCover
			? WorldseedBiomes::AppearanceBiome(Monde.Biomes.Index[Cell], Monde.Biomes.Cover[Cell])
			: static_cast<EWorldseedBiome>(Monde.Biomes.Index[Cell]));

		// L'eau se MELE au biome au lieu de l'effacer : c'est tout le
		// benefice de la separation des deux axes.
		if (bHasCover && Regles.CoverTint > 0.0f)
		{
			const EWorldseedCover Cover =
				static_cast<EWorldseedCover>(Monde.Biomes.Cover[Cell]);
			if (Cover != EWorldseedCover::None)
			{
				Tint = FMath::Lerp(Tint,
					WorldseedBiomes::CoverColour(Cover), Regles.CoverTint);
			}
		}

		OutColour = Tint;
	}
	else
	{
		OutColour = FLinearColor(Rock, Vegetation, Beach, Snow);
	}
}

}
