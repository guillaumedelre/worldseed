// Worldseed - la nappe qui REMPLIT la Runtime Virtual Texture.

#include "Procedural/WorldseedNappeRvt.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/SoftObjectPath.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTextureEnum.h"

#include "Procedural/WorldseedApparence.h"
#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedRules.h"   // FWorldseedGeometry, WorldseedMetersToCm

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

UTexture2D* CreerTexture(int32 Largeur, int32 Hauteur,
	const TArray<FColor>& Pixels, const TCHAR* Nom)
{
	UTexture2D* const T = UTexture2D::CreateTransient(
		Largeur, Hauteur, PF_B8G8R8A8, FName(Nom));
	if (!T)
	{
		return nullptr;
	}

	// LES REGLAGES SE POSENT AVANT `UpdateResource`, qui fige la ressource RHI.
	// Poses apres, ils ne prennent qu'au prochain appel -- et il n'y en a pas.
	T->SRGB = true;
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
	UTexture2D*& OutPoids, UTexture2D*& OutTeinte,
	FWorldseedNappeRvtReleve& Releve)
{
	OutPoids = nullptr;
	OutTeinte = nullptr;
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
	TBitArray<> EstTerre;
	Poids.SetNumUninitialized(Total);
	Teinte.SetNumUninitialized(Total);
	EstTerre.Init(false, Total);

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

	Releve.Largeur = NX;
	Releve.Hauteur = NY;
	Releve.Mo = 2.0 * static_cast<double>(Total) * sizeof(FColor) / (1024.0 * 1024.0);
	Releve.Ms = (FPlatformTime::Seconds() - Debut) * 1000.0;

	return OutPoids != nullptr && OutTeinte != nullptr;
}

UStaticMeshComponent* Poser(AActor* Proprietaire, USceneComponent* Racine,
	TArrayView<URuntimeVirtualTexture* const> Textures,
	double LargeurM, double HauteurM,
	UTexture2D* Poids, UTexture2D* Teinte)
{
	if (!Proprietaire || !Racine || !Poids || !Teinte)
	{
		return nullptr;
	}

	// --- ON N'ECRIT QUE DANS LA RVT DE COULEUR ------------------------------
	//
	// La seconde RVT du pack porte la HAUTEUR DU MONDE, dont les materiaux de
	// feuillage tirent leur fondu d'ancrage. Une nappe plate y ecrirait une
	// altitude constante, ce qui deplacerait cet ancrage partout sans qu'on
	// l'ait mesure. On la laisse donc exactement dans l'etat ou elle est.
	//
	// LE TYPE EST LU SUR L'ASSET, PAS DEDUIT DE SON RANG : un pack pourrait
	// tres bien les livrer dans l'autre ordre, et l'erreur serait silencieuse.
	URuntimeVirtualTexture* Couleur = nullptr;
	for (URuntimeVirtualTexture* const T : Textures)
	{
		if (T && T->GetMaterialType() != ERuntimeVirtualTextureMaterialType::WorldHeight)
		{
			Couleur = T;
			break;
		}
	}

	if (!Couleur)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] nappe RVT : aucune RVT de COULEUR parmi les %d posees ")
			TEXT("-- rien pose"), Textures.Num());
		return nullptr;
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
	SM->RuntimeVirtualTextures.Add(Couleur);
	SM->VirtualTextureRenderPassType = ERuntimeVirtualTextureMainPassType::Never;

	// Le plan du moteur fait cent centimetres de cote : l'echelle vaut donc
	// directement l'etendue en metres. Z = 0 a dessein -- la nappe n'ecrit
	// aucune hauteur, donc son altitude n'a pas d'effet, et le niveau de la
	// mer est le choix qui se defend le jour ou elle en ecrira une.
	SM->SetRelativeLocation(FVector::ZeroVector);
	SM->SetRelativeScale3D(FVector(LargeurM, HauteurM, 1.0));
	SM->RegisterComponent();

	return SM;
}
}   // namespace WorldseedNappeRvt
