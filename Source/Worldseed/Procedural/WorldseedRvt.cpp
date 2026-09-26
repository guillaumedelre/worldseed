// Worldseed - les Runtime Virtual Textures du pack, sur tout le monde.

#include "Procedural/WorldseedRvt.h"

#include "Components/PrimitiveComponent.h"
#include "Components/RuntimeVirtualTextureComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "VT/RuntimeVirtualTexture.h"

#include "Procedural/WorldseedRules.h"   // WorldseedMetersToCm

TArray<FSoftObjectPath> WorldseedRvt::AssetsParDefaut()
{
	// LES DEUX RVT DU PACK, ET IL EN FAUT DEUX. La premiere porte la couleur,
	// la normale et le speculaire ; la seconde la HAUTEUR DU MONDE, dont les
	// materiaux de feuillage se servent pour calculer leur fondu -- c'est elle
	// qui ancre une plante dans le sol au lieu de la repeindre entierement.
	return {
		FSoftObjectPath(TEXT("/Game/Orasot_Bundle/Stylized_Landscape_5_Bioms/"
			"Global/RVT/RVT_Landscape_Material.RVT_Landscape_Material")),
		FSoftObjectPath(TEXT("/Game/Orasot_Bundle/Stylized_Landscape_5_Bioms/"
			"Global/RVT/RVT_Landscape_Height.RVT_Landscape_Height")),
	};
}

int32 WorldseedRvt::Poser(AActor* Proprietaire, USceneComponent* Racine,
	TArrayView<const FSoftObjectPath> Assets, const FWorldseedRvtRegles& Regles,
	double LargeurM, double HauteurM,
	TArray<URuntimeVirtualTextureComponent*>& OutComposants,
	TArray<URuntimeVirtualTexture*>& OutTextures)
{
	OutComposants.Reset();
	OutTextures.Reset();

	if (!Proprietaire || !Racine)
	{
		return 0;
	}

	for (int32 I = 0; I < Assets.Num(); ++I)
	{
		URuntimeVirtualTexture* const Texture =
			Cast<URuntimeVirtualTexture>(Assets[I].TryLoad());
		if (!Texture)
		{
			// `Content/` est exclu du depot : un clone frais n'a pas ces assets.
			// On le dit et l'on continue -- un monde sans RVT reste jouable, il
			// aura seulement de l'herbe bleue la ou le pack l'exige.
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] RVT : asset introuvable, ignore -- %s"),
				*Assets[I].ToString());
			continue;
		}

		const FName Nom(*FString::Printf(TEXT("Rvt_%d"), OutComposants.Num()));
		URuntimeVirtualTextureComponent* const Comp =
			NewObject<URuntimeVirtualTextureComponent>(Proprietaire, Nom);
		Comp->SetupAttachment(Racine);
		Comp->SetVirtualTexture(Texture);
		Comp->RegisterComponent();

		// --- LE VOLUME EST UN COIN ET UNE TAILLE, PAS UN CENTRE -------------
		//
		// Releve sur la carte de demonstration du pack : volume pose a
		// (-101600, -101600, -25600) avec une echelle de 204800, pour un
		// terrain allant de -101600 a +101600. S'y tromper decale toute la
		// fenetre d'une demi-largeur, et la teinte ne tombe plus sur le terrain
		// qu'elle est censee decrire -- un defaut qui se voit mal, parce qu'une
		// teinte decalee reste une teinte plausible.
		const double LargeurCm = LargeurM * WorldseedMetersToCm;
		const double HauteurCm = HauteurM * WorldseedMetersToCm;
		const double BasCm = static_cast<double>(Regles.BasM) * WorldseedMetersToCm;
		const double HautCm = static_cast<double>(Regles.HautM) * WorldseedMetersToCm;

		Comp->SetWorldLocation(FVector(-LargeurCm * 0.5, -HauteurCm * 0.5, BasCm));
		Comp->SetWorldScale3D(FVector(LargeurCm, HauteurCm, HautCm - BasCm));

		OutComposants.Add(Comp);
		OutTextures.Add(Texture);

		// LA RESOLUTION SE DIT, ELLE NE SE SUPPOSE PAS. C'est le seul chiffre
		// qui permette de juger si la teinte sera assez fine, et il depend de
		// l'asset -- un pack pourrait tres bien livrer une RVT bien plus petite.
		const int32 Cote = Texture->GetSize();
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] RVT : %s posee -- %d texels de cote sur %.0f x %.0f km, ")
			TEXT("soit %.1f cm par texel en X"),
			*Texture->GetName(), Cote, LargeurM / 1000.0, HauteurM / 1000.0,
			Cote > 0 ? (LargeurM * 100.0 / Cote) : 0.0);
	}

	return OutComposants.Num();
}

void WorldseedRvt::FaireEcrire(UPrimitiveComponent* Primitive,
	TArrayView<URuntimeVirtualTexture* const> Textures)
{
	if (!Primitive || Textures.Num() == 0)
	{
		return;
	}

	Primitive->RuntimeVirtualTextures.Reset();
	for (URuntimeVirtualTexture* const T : Textures)
	{
		if (T)
		{
			Primitive->RuntimeVirtualTextures.Add(T);
		}
	}

	// `Always` ET NON `Exclusive` -- voir l'en-tete. Le defaut du moteur est
	// `Exclusive`, qui retire la primitive de la passe principale.
	Primitive->VirtualTextureRenderPassType =
		ERuntimeVirtualTextureMainPassType::Always;

	Primitive->MarkRenderStateDirty();
}
