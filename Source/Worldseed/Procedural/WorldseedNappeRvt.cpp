// Worldseed - la nappe qui REMPLIT la Runtime Virtual Texture.

#include "Procedural/WorldseedNappeRvt.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/SoftObjectPath.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTextureEnum.h"

#include "Procedural/WorldseedApparence.h"
#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedRules.h"   // FWorldseedGeometry, WorldseedMetersToCm
#include "Procedural/WorldseedRvt.h"     // FWorldseedRvtRegles

namespace
{
/**
 * L'ALLER-RETOUR sRGB EST DELIBERE, ET CE N'EST PAS UNE APPROXIMATION.
 *
 * Les poids et la teinte sont des grandeurs LINEAIRES, mais un echantillonneur
 * `SAMPLERTYPE_COLOR` -- celui qu'une texture 2D porte par defaut -- decode le
 * sRGB. Deux facons de s'en sortir : changer le type d'echantillonneur, au
 * risque qu'il desaccorde la texture PAR DEFAUT du parametre et fasse echouer
 * la compilation du materiau ; ou encoder a la cuisson et laisser le materiau
 * decoder. On choisit la seconde : l'aller-retour est exact a huit bits pres,
 * et il gagne meme de la precision dans les valeurs basses, ou le sRGB est
 * plus dense.
 */
FColor Encoder(const FLinearColor& Valeur)
{
	FColor C = Valeur.ToFColor(/*bSRGB=*/true);
	C.A = 255;
	return C;
}

/**
 * La texture de HAUTEUR, en SEIZE BITS, et ce n'est pas un luxe.
 *
 * Le volume de RVT couvre 3200 m (de -600 a +2600). Sur huit bits cela ferait
 * 12,5 m par pas -- le masque de `MF_RVT` se fond sur quelques metres, il ne
 * verrait qu'un escalier. En seize bits le pas tombe a 4,9 cm.
 *
 * `PF_G16` porte UN canal de seize bits non signes, donc 16,8 Mo pour la
 * grille du jeu, contre 33,5 pour une RGBA huit bits. La hauteur coute donc
 * MOINS que les deux textures deja cuites.
 *
 * SANS sRGB, et c'est portant : une hauteur est une grandeur LINEAIRE, et la
 * courbe sRGB la tordrait. Les deux autres textures, elles, encodent des
 * couleurs et gardent leur sRGB -- c'est le meme aller-retour assume depuis le
 * 27 septembre.
 */
UTexture2D* CreerTextureHauteur(int32 Largeur, int32 Hauteur,
	const TArray<uint16>& Pixels, const TCHAR* Nom)
{
	UTexture2D* const T = UTexture2D::CreateTransient(
		Largeur, Hauteur, PF_G16, FName(Nom));
	if (!T)
	{
		return nullptr;
	}

	T->SRGB = false;
	T->Filter = TextureFilter::TF_Bilinear;
	T->NeverStream = true;
	T->AddressX = TextureAddress::TA_Wrap;
	T->AddressY = TextureAddress::TA_Clamp;
	T->CompressionSettings = TextureCompressionSettings::TC_Grayscale;

	FTexturePlatformData* const Data = T->GetPlatformData();
	if (!Data || Data->Mips.Num() == 0)
	{
		return nullptr;
	}

	void* const Dest = Data->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Dest, Pixels.GetData(),
		static_cast<SIZE_T>(Pixels.Num()) * sizeof(uint16));
	Data->Mips[0].BulkData.Unlock();

	T->UpdateResource();
	return T;
}

UTexture2D* CreerTexture(int32 Largeur, int32 Hauteur,
	const TArray<FColor>& Pixels, const TCHAR* Nom, bool bSRGB = true)
{
	UTexture2D* const T = UTexture2D::CreateTransient(
		Largeur, Hauteur, PF_B8G8R8A8, FName(Nom));
	if (!T)
	{
		return nullptr;
	}

	// LES REGLAGES SE POSENT AVANT `UpdateResource`, qui fige la ressource RHI.
	// Poses apres, ils ne prennent qu'au prochain appel -- et il n'y en a pas.
	T->SRGB = bSRGB;
	T->Filter = TextureFilter::TF_Bilinear;
	T->NeverStream = true;

	// U S'ENROULE ET V SE BORNE, exactement comme `UVDepuisMetres` : la
	// longitude fait le tour de la sphere, un pole n'a pas de voisin au-dela.
	// Borner U couperait la carte au meridien de bordure ; enrouler V ferait
	// passer l'Arctique dans l'Antarctique.
	T->AddressX = TextureAddress::TA_Wrap;
	T->AddressY = TextureAddress::TA_Clamp;

	FTexturePlatformData* const Data = T->GetPlatformData();
	if (!Data || Data->Mips.Num() == 0)
	{
		return nullptr;
	}

	void* const Dest = Data->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Dest, Pixels.GetData(),
		static_cast<SIZE_T>(Pixels.Num()) * sizeof(FColor));
	Data->Mips[0].BulkData.Unlock();

	T->UpdateResource();
	return T;
}
}   // namespace

namespace WorldseedNappeRvt
{
bool Cuire(const FWorldseedBiomeMap& Biomes, const FWorldseedGeometry& Geo,
	const TArray<float>& ElevationM,
	float ExagerationZ, const FWorldseedRvtRegles& RvtRegles,
	UTexture2D*& OutPoids, UTexture2D*& OutTeinte,
	UTexture2D*& OutHauteur, UTexture2D*& OutNormale,
	FWorldseedNappeRvtReleve& Releve)
{
	OutPoids = nullptr;
	OutTeinte = nullptr;
	OutHauteur = nullptr;
	OutNormale = nullptr;
	Releve = FWorldseedNappeRvtReleve();

	const int32 NX = Geo.NX;
	const int32 NY = Geo.NY;
	const int32 Total = NX * NY;

	if (NX <= 1 || NY <= 1 || Biomes.Index.Num() < Total)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] nappe RVT : carte des biomes absente ou trop courte ")
			TEXT("(%d cellules pour %d x %d) -- rien cuit"),
			Biomes.Index.Num(), NX, NY);
		return false;
	}

	const double Debut = FPlatformTime::Seconds();
	const bool bCouverture = Biomes.Cover.Num() >= Total;

	TArray<FColor> Poids;
	TArray<FColor> Teinte;
	TArray<uint16> Hauteur;
	TArray<FColor> Normale;
	TBitArray<> EstTerre;
	Poids.SetNumUninitialized(Total);
	Teinte.SetNumUninitialized(Total);
	Hauteur.SetNumUninitialized(Total);
	Normale.SetNumUninitialized(Total);
	EstTerre.Init(false, Total);

	// UN MINIMUM QUI PART DE ZERO NE VOIT JAMAIS UN MONDE ENTIEREMENT EMERGE,
	// et un maximum qui part de zero ne voit jamais un monde entierement
	// noye : les deux rendraient un intervalle plausible et faux. La boucle
	// tourne au moins une fois, les gardes ci-dessus l'assurant.
	Releve.AltitudeMin = TNumericLimits<float>::Max();
	Releve.AltitudeMax = TNumericLimits<float>::Lowest();

	// UN VOLUME PLAT DIVISERAIT PAR ZERO. Il n'a aucun sens -- une RVT de
	// hauteur sans epaisseur ne peut rien encoder -- mais il arriverait ici
	// sous la forme d'un NaN dans toute la texture, donc d'un ancrage aleatoire
	// plutot que d'une erreur.
	const float PlageZ = FMath::Max(RvtRegles.HautM - RvtRegles.BasM, 1.0f);

	// --- LA NORMALE DU RELIEF, ET POURQUOI ELLE EST PORTANTE ---------------
	//
	// `MF_RVT` ne melange pas que la couleur : il melange des ATTRIBUTS DE
	// MATERIAU, normale comprise. Une sortie RVT dont l'entree `Normal` n'est
	// pas branchee ecrit (0, 0, 1) -- releve dans le moteur,
	// `MaterialExpressions.cpp:3081` -- c'est-a-dire la normale d'une surface
	// PLATE. Des que le masque d'ancrage mord, le dessus d'un pan INCLINE
	// recoit donc cette normale-la, et le voila eclaire comme un plan
	// horizontal tout en projetant l'ombre d'une pente. Mesure a l'image :
	// dessus entierement NOIR sous un dither, qui disparait a
	// `ShowFlag.DynamicShadows 0`.
	//
	// LA DEMO DU PACK N'A PAS CE DEFAUT parce que son Landscape ecrit une VRAIE
	// normale dans la RVT. C'est la meme lecon que la hauteur, un cran plus
	// loin : ce que le pack fournit par sa geometrie, nous devons le CUIRE.
	//
	// ELLE SE DERIVE DU RELIEF, ON NE LA STOCKE PAS. Gradient centre sur la
	// grille, exageration comprise -- la normale VISIBLE est celle du relief
	// exagere, pas celle du relief brut.
	const float PasM = FMath::Max(Geo.MetersPerPixel(), 0.001f);

	auto AltitudeEn = [&ElevationM, NX, NY, ExagerationZ](int32 I, int32 J) -> float
	{
		// LA LONGITUDE S'ENROULE, LA LATITUDE SE BORNE. Un pole n'a pas de
		// voisin au-dela ; le meridien de bordure, si. Borner les deux
		// creerait une ligne de normales fausses sur toute la hauteur du
		// monde, exactement la couture que l'enroulement existe pour eviter.
		const int32 Ic = ((I % NX) + NX) % NX;
		const int32 Jc = FMath::Clamp(J, 0, NY - 1);
		const int32 K = Jc * NX + Ic;
		return (ElevationM.IsValidIndex(K) ? ElevationM[K] : 0.0f) * ExagerationZ;
	};

	for (int32 I = 0; I < Total; ++I)
	{
		// LE SUBSTRAT DECIDE DE LA MATIERE, PAS LE CLIMAT, et c'est
		// `AppearanceBiome` qui rend l'identifiant d'avant la separation des
		// deux axes. Lire l'axe des biomes seul couvrirait d'herbe une paroi
		// en foret tropicale.
		const EWorldseedBiome Biome = bCouverture
			? WorldseedBiomes::AppearanceBiome(Biomes.Index[I], Biomes.Cover[I])
			: static_cast<EWorldseedBiome>(Biomes.Index[I]);

		const FLinearColor P = WorldseedBiomes::SlotWeights(Biome);
		const FLinearColor T =
			WorldseedApparence::TeinteNormalisee(WorldseedBiomes::Colour(Biome));

		Poids[I] = Encoder(P);
		Teinte[I] = Encoder(T / TeinteMax);

		// LA HAUTEUR EST ENCODEE SUR LA PLAGE DU VOLUME, ET SUR RIEN D'AUTRE.
		// Le moteur relit `WorldHeight` avec la transform du volume de RVT :
		// normaliser sur les bornes REELLES du monde donnerait un ancrage faux,
		// d'autant plus faux que le monde est plat. On ECRETE plutot que de
		// replier -- un relief au-dela des bornes doit saturer au bord, pas
		// ressortir en bas.
		//
		// ET L'ALTITUDE EST PORTEE DANS L'ESPACE DU MONDE AVANT D'ETRE
		// NORMALISEE : `ElevationM` est en metres de simulation, le volume en
		// metres du monde, et le terrain multiplie par l'exageration entre les
		// deux. Comparer les deux directement marcherait tant que le facteur
		// vaut un, et deviendrait faux le jour ou il change.
		const float Z = (ElevationM.IsValidIndex(I) ? ElevationM[I] : 0.0f)
			* ExagerationZ;
		const float Normalisee = FMath::Clamp(
			(Z - RvtRegles.BasM) / PlageZ, 0.0f, 1.0f);
		Hauteur[I] = static_cast<uint16>(FMath::RoundToInt(Normalisee * 65535.0f));

		Releve.AltitudeMin = FMath::Min(Releve.AltitudeMin, Z);
		Releve.AltitudeMax = FMath::Max(Releve.AltitudeMax, Z);
		if (Z <= RvtRegles.BasM || Z >= RvtRegles.HautM)
		{
			++Releve.HorsBornes;
		}

		// --- la normale du relief, par gradient centre ---------------------
		{
			const int32 Ix = I % NX;
			const int32 Jy = I / NX;

			const float Dzdx =
				(AltitudeEn(Ix + 1, Jy) - AltitudeEn(Ix - 1, Jy)) / (2.0f * PasM);
			const float Dzdy =
				(AltitudeEn(Ix, Jy + 1) - AltitudeEn(Ix, Jy - 1)) / (2.0f * PasM);

			const FVector N = FVector(-Dzdx, -Dzdy, 1.0).GetSafeNormal();

			// ENCODEE EN LINEAIRE, ET C'EST L'INVERSE DES DEUX AUTRES TEXTURES.
			// Les poids et la teinte sont des COULEURS, relues par un
			// echantillonneur qui decode le sRGB ; une normale est une
			// DIRECTION, et la courbe sRGB la tordrait. Le materiau fait donc
			// le `x * 2 - 1` a la main plutot que de passer par un
			// echantillonneur de carte de normales -- dont le type devrait
			// s'accorder a la texture PAR DEFAUT du parametre, ce qui est
			// precisement le piege qu'on evite.
			Normale[I] = FLinearColor(
				static_cast<float>(N.X) * 0.5f + 0.5f,
				static_cast<float>(N.Y) * 0.5f + 0.5f,
				static_cast<float>(N.Z) * 0.5f + 0.5f,
				1.0f).ToFColor(/*bSRGB=*/false);

			Releve.PenteMaxDeg = FMath::Max(Releve.PenteMaxDeg,
				FMath::RadiansToDegrees(FMath::Acos(
					FMath::Clamp(static_cast<float>(N.Z), 0.0f, 1.0f))));
		}

		Releve.PoidsMax = FMath::Max(Releve.PoidsMax,
			FMath::Max3(P.R, P.G, FMath::Max(P.B, P.A)));
		Releve.TeinteMax = FMath::Max(Releve.TeinteMax, FMath::Max3(T.R, T.G, T.B));

		// --- LA MER SE LIT SUR LA COUVERTURE, JAMAIS SUR LE BIOME ------------
		//
		// PREMIERE VERSION FAUSSE, ET LE COMPTE L'A DITE : elle testait
		// `Biome != Ocean` et annoncait « 8 388 608 terre / 0 mer » sur un
		// monde dont 71 % est oceanique. `AppearanceBiome` ne traduit que les
		// couvertures `Rock` et `Beach` ; pour toutes les autres -- l'ocean
		// compris -- elle rend le biome CLIMATIQUE, qui est defini PARTOUT,
		// meme sous la mer, ou il decrit la bande climatique de l'eau. Elle ne
		// peut donc jamais rendre `Ocean`, et la dilatation ci-dessous ne
		// s'executait pas une seule fois.
		//
		// Un releve qui ressemble a une mesure sans en etre une coute plus
		// cher que pas de releve : c'est lui qui a trouve le defaut, avant
		// qu'aucune image ne puisse le montrer.
		const bool bTerre = !bCouverture
			|| static_cast<EWorldseedCover>(Biomes.Cover[I]) != EWorldseedCover::Ocean;
		EstTerre[I] = bTerre;
		(bTerre ? Releve.Terre : Releve.Mer) += 1;
	}

	// --- DILATER LA TERRE D'UNE CELLULE DANS LA MER -------------------------
	//
	// CE N'EST PAS LA PARADE DU PEINTRE VOXEL, ET L'ARGUMENT DIFFERE. Lui
	// ecarte l'ocean de son melange parce que l'ocean y porte une couleur
	// BLEUE, qui tirerait le sable du rivage vers le bleu. Ici rien n'est
	// jamais bleu : une cellule marine porte les poids de sa bande CLIMATIQUE,
	// c'est-a-dire « la foret qui pousserait la si c'etait de la terre ».
	//
	// Ce qu'on corrige est plus fin : ces poids-la sont ARBITRAIRES au bord de
	// l'eau. Le filtre bilineaire porte sur une cellule de chaque cote, donc
	// une plage melangerait son sable a une foret imaginaire posee sous la
	// mer. En donnant a la cellule marine la valeur de ses voisines
	// TERRESTRES, le rivage se melange a lui-meme.
	//
	// La moyenne se fait sur les valeurs DEJA ENCODEES. C'est une moyenne en
	// sRGB, donc legerement fausse au sens physique ; sur un lisere d'une
	// cellule au bord de l'eau l'ecart ne se voit pas, et la faire en lineaire
	// couterait deux tableaux de flottants de 134 Mo.
	{
		const TArray<FColor> PoidsSource = Poids;
		const TArray<FColor> TeinteSource = Teinte;

		for (int32 Row = 0; Row < NY; ++Row)
		{
			for (int32 Col = 0; Col < NX; ++Col)
			{
				const int32 I = Row * NX + Col;
				if (EstTerre[I])
				{
					continue;
				}

				int32 N = 0;
				int32 PR = 0, PG = 0, PB = 0;
				int32 TR = 0, TG = 0, TB = 0;

				for (int32 DY = -1; DY <= 1; ++DY)
				{
					const int32 RY = Row + DY;
					if (RY < 0 || RY >= NY)
					{
						continue;
					}
					for (int32 DX = -1; DX <= 1; ++DX)
					{
						// La longitude s'enroule : le voisin de la colonne 0
						// est la derniere colonne, pas le vide.
						const int32 RX = (Col + DX + NX) % NX;
						const int32 J = RY * NX + RX;
						if (!EstTerre[J])
						{
							continue;
						}
						++N;
						PR += PoidsSource[J].R;
						PG += PoidsSource[J].G;
						PB += PoidsSource[J].B;
						TR += TeinteSource[J].R;
						TG += TeinteSource[J].G;
						TB += TeinteSource[J].B;
					}
				}

				if (N == 0)
				{
					continue;
				}

				Poids[I] = FColor(static_cast<uint8>(PR / N),
					static_cast<uint8>(PG / N), static_cast<uint8>(PB / N), 255);
				Teinte[I] = FColor(static_cast<uint8>(TR / N),
					static_cast<uint8>(TG / N), static_cast<uint8>(TB / N), 255);
				++Releve.Dilates;
			}
		}
	}

	OutPoids = CreerTexture(NX, NY, Poids, TEXT("WorldseedNappePoids"));
	OutTeinte = CreerTexture(NX, NY, Teinte, TEXT("WorldseedNappeTeinte"));
	OutHauteur = CreerTextureHauteur(NX, NY, Hauteur,
		TEXT("WorldseedNappeHauteur"));

	// LA NORMALE N'EST PAS UNE COULEUR, DONC PAS DE sRGB. Les deux premieres
	// textures encodent des couleurs et assument l'aller-retour sRGB ;
	// celle-ci porte une direction, que la courbe tordrait.
	OutNormale = CreerTexture(NX, NY, Normale, TEXT("WorldseedNappeNormale"),
		/*bSRGB=*/false);

	Releve.Largeur = NX;
	Releve.Hauteur = NY;
	Releve.Mo = (3.0 * static_cast<double>(Total) * sizeof(FColor)
		+ static_cast<double>(Total) * sizeof(uint16)) / (1024.0 * 1024.0);
	Releve.Ms = (FPlatformTime::Seconds() - Debut) * 1000.0;

	return OutPoids != nullptr && OutTeinte != nullptr && OutHauteur != nullptr
		&& OutNormale != nullptr;
}

UStaticMeshComponent* Poser(AActor* Proprietaire, USceneComponent* Racine,
	TArrayView<URuntimeVirtualTexture* const> Textures,
	double LargeurM, double HauteurM,
	UTexture2D* Poids, UTexture2D* Teinte, UTexture2D* Hauteur,
	UTexture2D* Normale, const FWorldseedRvtRegles& RvtRegles)
{
	if (!Proprietaire || !Racine || !Poids || !Teinte || !Hauteur || !Normale)
	{
		return nullptr;
	}

	// --- ON ECRIT DANS LES DEUX, ET LA HAUTEUR N'EST PAS UN SUPPLEMENT ------
	//
	// La note qui tenait ici disait qu'on laissait la RVT de HAUTEUR intacte
	// « a dessein », une nappe plate ne pouvant y ecrire qu'une altitude
	// constante. Le constat etait juste et la conclusion fausse : il ne
	// fallait pas renoncer a la hauteur, mais cesser de la lire sur la
	// GEOMETRIE -- le plan est a Z = 0 -- et la cuire comme le reste.
	//
	// SANS ELLE, LE DESSUS DES PANS NE PEUT PAS S'ACCORDER AU SOL. `MF_RVT`,
	// que `M_Master_Cliff_Mat` appelle sur l'entree B de son HeightLerp,
	// echantillonne DEUX runtime virtual textures : la couleur ET la hauteur.
	// Son masque compare l'altitude du monde a celle du sol ; sans hauteur
	// ecrite il ne mord jamais, et le pan garde sa roche.
	//
	// LE TYPE EST LU SUR L'ASSET, PAS DEDUIT DE SON RANG : un pack pourrait
	// tres bien les livrer dans l'autre ordre, et l'erreur serait silencieuse.
	URuntimeVirtualTexture* Couleur = nullptr;
	URuntimeVirtualTexture* Altitude = nullptr;
	for (URuntimeVirtualTexture* const T : Textures)
	{
		if (!T)
		{
			continue;
		}
		if (T->GetMaterialType() == ERuntimeVirtualTextureMaterialType::WorldHeight)
		{
			if (!Altitude) { Altitude = T; }
		}
		else if (!Couleur)
		{
			Couleur = T;
		}
	}

	if (!Couleur)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] nappe RVT : aucune RVT de COULEUR parmi les %d posees ")
			TEXT("-- rien pose"), Textures.Num());
		return nullptr;
	}

	// UNE ABSENCE SE DIT, ELLE NE SE DEVINE PAS. Sans RVT de hauteur, la
	// couleur fonctionnera et le dessus des pans restera gris : c'est
	// exactement le defaut qu'on vient de fermer, et un silence le rendrait
	// indiscernable d'une regression du materiau.
	if (!Altitude)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] nappe RVT : aucune RVT de HAUTEUR parmi les %d posees ")
			TEXT("-- le dessus des pans restera gris"), Textures.Num());
	}

	UStaticMesh* const Plan = Cast<UStaticMesh>(
		FSoftObjectPath(TEXT("/Engine/BasicShapes/Plane.Plane")).TryLoad());
	UMaterialInterface* const Base = Cast<UMaterialInterface>(
		FSoftObjectPath(TEXT("/Game/Worldseed/Materials/MI_WorldseedNappeRvt")
			TEXT(".MI_WorldseedNappeRvt")).TryLoad());

	if (!Plan || !Base)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] nappe RVT : plan (%s) ou materiau (%s) introuvable ")
			TEXT("-- rien pose. `Content/` est exclu du depot : un clone frais ")
			TEXT("n'a pas ces assets."),
			Plan ? TEXT("ok") : TEXT("ABSENT"), Base ? TEXT("ok") : TEXT("ABSENT"));
		return nullptr;
	}

	UMaterialInstanceDynamic* const Mid =
		UMaterialInstanceDynamic::Create(Base, Proprietaire);
	if (!Mid)
	{
		return nullptr;
	}

	const double LargeurCm = LargeurM * WorldseedMetersToCm;
	const double HauteurCm = HauteurM * WorldseedMetersToCm;

	Mid->SetTextureParameterValue(TEXT("TexPoids"), Poids);
	Mid->SetTextureParameterValue(TEXT("TexTeinte"), Teinte);
	Mid->SetTextureParameterValue(TEXT("TexHauteur"), Hauteur);
	Mid->SetTextureParameterValue(TEXT("TexNormale"), Normale);

	// LES BORNES DU VOLUME, EN CENTIMETRES, POUR QUE LE MATERIAU DECODE.
	//
	// LE MOTEUR ATTEND UN Z DU MONDE EN UNITES UNREAL sur l'entree `WorldHeight`
	// du noeud de sortie RVT : `VirtualTextureMaterial.usf` le prend tel quel et
	// le repacke lui-meme avec la transform du volume (`PackWorldHeight`). Le
	// materiau doit donc rendre `Bas + Lu * Plage`, en CENTIMETRES -- rendre la
	// valeur normalisee [0..1] telle quelle ecrirait une altitude d'un
	// centimetre, ce qui se lirait comme une RVT de hauteur « qui marche » et
	// poserait l'ancrage du feuillage au ras de zero partout.
	//
	// Les bornes viennent des regles du VOLUME, pas d'une copie locale : c'est
	// la meme source que celle qui a servi a cuire la texture.
	Mid->SetVectorParameterValue(TEXT("MondeZCm"),
		FLinearColor(RvtRegles.BasM * WorldseedMetersToCm,
			(RvtRegles.HautM - RvtRegles.BasM) * WorldseedMetersToCm,
			0.0f, 0.0f));
	Mid->SetVectorParameterValue(TEXT("MondeEtendueCm"),
		FLinearColor(static_cast<float>(LargeurCm), static_cast<float>(HauteurCm),
			1.0f, 1.0f));

	UStaticMeshComponent* const SM = NewObject<UStaticMeshComponent>(
		Proprietaire, TEXT("NappeRvt"));
	SM->SetupAttachment(Racine);
	SM->SetStaticMesh(Plan);
	SM->SetMaterial(0, Mid);

	// LA MOBILITE RESTE STATIQUE, ET C'EST TOUTE LA RAISON D'ETRE DE CETTE
	// NAPPE : la passe RVT se batit a partir des lots STATIQUES. On ne peut
	// donc pas s'en tirer en passant le composant en `Movable` comme pour le
	// VOLUME de RVT -- il faut poser la transform AVANT l'enregistrement, ce
	// qui est la seule autre facon, et c'est le defaut que le temoin a paye.
	SM->SetMobility(EComponentMobility::Static);
	SM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SM->SetCastShadow(false);
	SM->bAffectDistanceFieldLighting = false;

	// --- `Never`, ET C'EST L'INVERSE DE LA REGLE DU TERRAIN -----------------
	//
	// UNE primitive qu'on doit AUSSI voir exige `Always` : le defaut du moteur
	// est `Exclusive`, il avait un jour retire le terrain de la passe
	// principale, celui-ci paraissait aplati, et la RVT avait ete abandonnee a
	// tort. Ici l'exigence est renversee -- la nappe ne doit JAMAIS se voir, et
	// c'est bien pourquoi elle ne peut pas passer par un utilitaire qui
	// imposerait `Always`. Le moteur le dit en toutes lettres :
	// « Never render to the main pass. Use this for primitives that only
	// render to Runtime Virtual Texture. »
	// UNE SURCHARGE, PARCE QU'UN A/B NE DOIT ISOLER QU'UNE CHOSE. Couper la
	// nappe entiere par `-WorldseedNappeRvt=0` emporterait la COULEUR avec la
	// hauteur, et l'on ne saurait plus laquelle des deux a change le dessus des
	// pans. Et l'on ne touche pas a `world_rules.json` : son empreinte entre
	// dans la cle du cache, donc les deux moities ne joueraient plus le meme
	// monde.
	int32 AvecHauteur = 1;
	FParse::Value(FCommandLine::Get(), TEXT("WorldseedNappeHauteur="), AvecHauteur);

	SM->RuntimeVirtualTextures.Add(Couleur);
	if (Altitude && AvecHauteur != 0)
	{
		SM->RuntimeVirtualTextures.Add(Altitude);
	}
	else if (Altitude)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] nappe RVT : hauteur COUPEE par ")
			TEXT("-WorldseedNappeHauteur=0 (la couleur reste ecrite)"));
	}
	SM->VirtualTextureRenderPassType = ERuntimeVirtualTextureMainPassType::Never;

	// Le plan du moteur fait cent centimetres de cote : l'echelle vaut donc
	// directement l'etendue en metres.
	//
	// Z = 0, ET L'ALTITUDE DE LA NAPPE N'A AUCUNE IMPORTANCE MEME MAINTENANT
	// QU'ELLE ECRIT UNE HAUTEUR. C'est precisement le point qui avait fait
	// ecarter cette piste : on croyait que la RVT de hauteur prendrait le Z de
	// la GEOMETRIE, donc zero partout. Elle prend ce que le materiau met sur
	// l'entree `WorldHeight` du noeud de sortie -- ici la hauteur CUITE -- et
	// la position du plan ne l'atteint pas.
	SM->SetRelativeLocation(FVector::ZeroVector);
	SM->SetRelativeScale3D(FVector(LargeurM, HauteurM, 1.0));
	SM->RegisterComponent();

	return SM;
}
}   // namespace WorldseedNappeRvt
