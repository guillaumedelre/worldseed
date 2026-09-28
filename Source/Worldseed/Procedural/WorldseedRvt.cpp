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

		// --- LA MOBILITE SE POSE AVANT L'ENREGISTREMENT -----------------------
		//
		// ET C'EST CE QUI EMPECHAIT LA RVT D'EXISTER. Un composant de scene
		// arrive en mobilite STATIQUE ; le deplacer APRES `RegisterComponent`
		// est refuse, et le moteur le disait -- « Mobility of ... Rvt_0 has to
		// be 'Movable' if you'd like to move ». La transform posee juste apres
		// ne prenait donc pas : le volume gardait l'origine et l'echelle un,
		// soit un cube d'un centimetre, et la RVT ne couvrait RIEN.
		//
		// LE SYMPTOME NE RESSEMBLAIT PAS A LA CAUSE. Ce qui se voyait etait le
		// DESSUS DES PANS DE FALAISE EN NOIR : `M_Master_Cliff_Mat` y melange
		// la couleur du sol lue dans la RVT, et une RVT vide rend du noir. On a
		// donc d'abord soupconne le materiau des pans, puis le cablage de la
		// sortie RVT du sol -- deux pistes fermees par la mesure.
		//
		// LE TEMOIN QUI A TRANCHE : un MAGENTA FRANC branche sur la BaseColor
		// de la sortie RVT du terrain. Le dessus est reste NOIR. Une couleur
		// franche ne se compare a rien : si rien ne devient magenta, rien n'est
		// ecrit -- ce n'etait donc pas le contenu, c'etait le montage.
		Comp->SetMobility(EComponentMobility::Movable);
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
		// --- ON RELIT LA TRANSFORM, ON NE LA SUPPOSE PAS ---------------------
		//
		// La ligne d'avant journalisait ce qu'on AVAIT DEMANDE : « 524288
		// texels sur 64 x 32 km », un chiffre calcule depuis nos propres
		// arguments, qui restait juste alors meme que le composant n'avait pas
		// bouge. C'est la forme exacte du piege deja consigne -- « un repli
		// journalise ressemble a une mesure ». On relit donc l'echelle REELLE,
		// et l'on crie si elle ne fait pas la taille du monde.
		const FVector EchelleReelle = Comp->GetComponentScale();
		const FVector CoinReel = Comp->GetComponentLocation();

		const int32 Cote = Texture->GetSize();
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] RVT : %s posee -- %d texels de cote, coin RELU ")
			TEXT("(%.0f, %.0f, %.0f) m, etendue RELUE %.2f x %.2f km, ")
			TEXT("soit %.1f cm par texel en X"),
			*Texture->GetName(), Cote,
			CoinReel.X / WorldseedMetersToCm, CoinReel.Y / WorldseedMetersToCm,
			CoinReel.Z / WorldseedMetersToCm,
			EchelleReelle.X / WorldseedMetersToCm / 1000.0,
			EchelleReelle.Y / WorldseedMetersToCm / 1000.0,
			Cote > 0 ? (EchelleReelle.X / WorldseedMetersToCm * 100.0 / Cote) : 0.0);

		if (EchelleReelle.X < LargeurCm * 0.5 || EchelleReelle.Y < HauteurCm * 0.5)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[Worldseed] RVT : %s NE COUVRE RIEN -- etendue relue %.0f x %.0f cm ")
				TEXT("pour %.0f x %.0f demandes. Tout ce qui la LIT rendra du NOIR, ")
				TEXT("a commencer par le dessus des pans de falaise."),
				*Texture->GetName(), EchelleReelle.X, EchelleReelle.Y,
				LargeurCm, HauteurCm);
		}
	}

	return OutComposants.Num();
}

// `FaireEcrire` VIVAIT ICI -- retiree le 28 septembre 2026, son seul appelant
// etant inerte. Les deux lecons qu'elle portait sont dans l'en-tete, avec la
// mesure relevee dans la source du moteur.
