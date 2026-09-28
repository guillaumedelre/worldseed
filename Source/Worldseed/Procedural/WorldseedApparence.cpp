// Worldseed - l'apparence d'un sommet de terrain : poids, couleur, teinte.

#include "Procedural/WorldseedApparence.h"


namespace WorldseedApparence
{
FLinearColor TeinteNormalisee(const FLinearColor& Teinte)
{
	const float Luminance = FMath::Max(
		0.299f * Teinte.R + 0.587f * Teinte.G + 0.114f * Teinte.B, 0.01f);

	return FLinearColor(
		FMath::Min(Teinte.R / Luminance, 2.5f),
		FMath::Min(Teinte.G / Luminance, 2.5f),
		FMath::Min(Teinte.B / Luminance, 2.5f),
		1.0f);
}

float PartEstran(EWorldseedBiome BiomeApparent)
{
	// UN SEUIL FRANC, ET IL LE FAUT : l'estran est un SUBSTRAT, donc un fait
	// binaire -- une cellule est de l'estran ou elle ne l'est pas. Ce qui
	// adoucit la frontiere n'est pas ce test mais ce qu'on en fait : la
	// peinture du terrain voxel moyenne cette part sur les quatre cellules
	// voisines, exactement comme elle moyenne deja la teinte et les poids, et
	// l'interpolation des UV l'etale ensuite sur le triangle.
	//
	// ON NE MOYENNE JAMAIS UN IDENTIFIANT, mais ON PEUT moyenner ce qui en
	// DERIVE. C'est la regle que ce depot a payee sur la carte des biomes --
	// 307 points faux sur 17956 -- et la distinction est exactement celle-ci.
	return BiomeApparent == EWorldseedBiome::Beach ? 1.0f : 0.0f;
}

float PartNeige(EWorldseedBiome BiomeApparent)
{
	// UN SEUIL FRANC, POUR LA MEME RAISON QUE L'ESTRAN : une cellule est de la
	// calotte ou elle ne l'est pas, et ce qui adoucit la frontiere n'est pas ce
	// test mais ce qu'on en fait -- la peinture moyenne cette part sur les
	// quatre cellules voisines, puis l'interpolation des UV l'etale sur le
	// triangle.
	//
	// LA CALOTTE SEULE, ET C'EST DELIBERE POUR CE PREMIER JET. L'alpin et la
	// toundra portent eux aussi de la neige une partie de l'annee, mais elle
	// est SAISONNIERE : c'est le travail de la meteo, qui pilote deja DLWE par
	// la collection d'UDW. Leur donner une part permanente les figerait sous la
	// neige en plein ete. La calotte, elle, se definit par le fait que son mois
	// le plus chaud reste sous zero -- ce qui fond en ete n'y tient pas
	// l'annee -- donc elle est blanche par nature et non par saison.
	return BiomeApparent == EWorldseedBiome::IceCap ? 1.0f : 0.0f;
}

void Sommet(const FWorldseedWorldData& Monde, const FWorldseedSurfaceRegles& Regles,
	int32 Cell, float HeightM, const FVector& Normal,
	const FWorldseedAppearance& Mode,
	FLinearColor& OutColour, FVector2D& OutTintRG, FVector2D& OutTintB,
	float& OutNeige)
{
	// LA VALEUR DE REPLI EST POSEE D'ABORD, ET TOUS LES CHEMINS LA GARDENT
	// SAUF UN. Cette fonction sort par plusieurs `return` -- mer opaque,
	// couleur de biome, pack de textures -- et une sortie oubliee laisserait
	// une part de neige INDETERMINEE dans un tableau non initialise, donc de
	// la neige au hasard sur le decor. Zero est le seul repli sur.
	OutNeige = 0.0f;
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

		// LA TEINTE PART DEJA NORMALISEE EN LUMINANCE, et le calcul vit
		// desormais dans `TeinteNormalisee` : il a DEUX appelants -- cette
		// nappe et la peinture du terrain voxel -- et le depot interdit de
		// recopier une formule dans deux fichiers.
		const FLinearColor Normalised = WorldseedApparence::TeinteNormalisee(Tint);

		// UV2.Y PORTE LA PART D'ESTRAN, et c'etait le dernier canal libre du
		// sommet. Le materiau y fond le sable de plage sur celui du desert :
		// les deux partagent la meme MATIERE aride -- il n'y en a que quatre --
		// et ne pouvaient donc pas avoir deux textures sans ce transport.
		OutTintRG = FVector2D(Normalised.R, Normalised.G);
		OutTintB = FVector2D(Normalised.B,
			WorldseedApparence::PartEstran(Biome));

		// LA NEIGE SUIT L'ESTRAN, canal voisin et meme raisonnement : c'est
		// une part derivee du biome d'APPARENCE, pas un poids de matiere.
		OutNeige = WorldseedApparence::PartNeige(Biome);
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
