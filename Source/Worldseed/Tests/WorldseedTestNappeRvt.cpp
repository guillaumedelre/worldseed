// Worldseed - la nappe doit DECLARER les trois canaux qu'elle ecrit.

#if WITH_DEV_AUTOMATION_TESTS

#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameters.h"
#include "Misc/AutomationTest.h"
#include "UObject/SoftObjectPath.h"

/**
 * POURQUOI CE TEST EXISTE, ET CE QU'IL AURAIT ATTRAPE.
 *
 * LA NAPPE ECRIT TROIS CHOSES DANS LA RVT -- couleur, hauteur, normale -- et
 * les trois passent par des PARAMETRES du materiau, poses a l'execution.
 * `SetTextureParameterValue` et `SetVectorParameterValue` sont MUETS sur un
 * nom que le materiau ne declare pas : ils ne rendent rien, ne journalisent
 * rien, et ne font rien. Ce depot a paye exactement cela sur la rampe du
 * decor -- trois lignes devenues des no-op sous un journal qui annoncait
 * « rampe ARMEE ».
 *
 * ET LE DEFAUT NE SE VERRAIT PAS COMME UN DEFAUT DE NAPPE :
 *
 * - `TexHauteur` manquante -> la RVT de hauteur garde son defaut, le masque
 *   d'ancrage de `MF_RVT` ne mord jamais, et le dessus des pans de falaise
 *   reste gris-bleu. C'est le defaut qu'on vient de fermer, a l'identique ;
 * - `TexNormale` manquante -> la sortie RVT ecrit (0, 0, 1), la normale d'un
 *   PLAN. Mesure a l'image : le dessus des pans rend entierement NOIR sous un
 *   dither, et le noir disparait a `ShowFlag.DynamicShadows 0` ;
 * - `MondeZCm` manquant -> le materiau decode la hauteur sur une plage nulle
 *   et ecrit une altitude d'UN CENTIMETRE partout. Ce cas-la est le pire des
 *   trois : la RVT de hauteur EXISTE, elle est remplie, et elle est fausse.
 *
 * ON PASSE PAR UNE INSTANCE DYNAMIQUE DE `MI_WorldseedNappeRvt`, parce que
 * c'est exactement ce que `WorldseedNappeRvt::Poser` fabrique. Interroger le
 * maitre ne prouverait rien sur la chaine d'instances que la nappe recoit.
 *
 * CE QUE CE TEST NE GARDE PAS, et il faut le dire : il ne verifie ni que la
 * nappe est posee, ni que les textures sont cuites, ni que ce qu'elles
 * portent est juste. Il garde la COUTURE -- que les ecritures ne soient pas
 * muettes. Le reste se lit au journal, qui rend la pente maximale de la
 * normale et le compte de texels hors bornes.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestNappeRvt,
	"Worldseed.Nappe.LaNappeDeclareSesTroisCanaux",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestNappeRvt::RunTest(const FString& Parameters)
{
	const TCHAR* const Chemin =
		TEXT("/Game/Worldseed/Materials/MI_WorldseedNappeRvt.MI_WorldseedNappeRvt");

	UMaterialInterface* const Mat =
		Cast<UMaterialInterface>(FSoftObjectPath(Chemin).TryLoad());

	// `Content/` EST EXCLU DU DEPOT : un clone frais n'a pas cet asset. On le
	// DIT et l'on passe -- un test qui echoue faute de contenu apprendrait
	// seulement qu'il manque du contenu.
	if (!Mat)
	{
		AddInfo(FString::Printf(
			TEXT("materiau absent, non eprouve : %s"), Chemin));
		return true;
	}

	UMaterialInstanceDynamic* const Mid =
		UMaterialInstanceDynamic::Create(Mat, GetTransientPackage());
	if (!TestNotNull(TEXT("instance dynamique creee"), Mid))
	{
		return false;
	}

	const TCHAR* const Textures[] = {
		TEXT("TexPoids"), TEXT("TexTeinte"), TEXT("TexHauteur"),
		TEXT("TexNormale"),
	};

	for (const TCHAR* const Nom : Textures)
	{
		UTexture* Lue = nullptr;
		TestTrue(FString::Printf(
			TEXT("la nappe declare le parametre de texture %s -- sans lui, ")
			TEXT("l'ecriture est un no-op SILENCIEUX"), Nom),
			Mid->GetTextureParameterValue(FMaterialParameterInfo(Nom), Lue));
	}

	// LES BORNES DU VOLUME, SANS LESQUELLES LA HAUTEUR SE DECODE SUR ZERO.
	FLinearColor Bornes = FLinearColor::Black;
	const bool bBornes = Mid->GetVectorParameterValue(
		FMaterialParameterInfo(TEXT("MondeZCm")), Bornes);

	TestTrue(TEXT("la nappe declare MondeZCm, les bornes du volume en cm"),
		bBornes);

	// LE COMMUTATEUR DOIT ETRE ARME SUR LA NAPPE, et c'est ce qui la distingue
	// du sol : les trois branches -- poids, hauteur, normale -- passent par le
	// MEME `PoidsDepuisTexture`. A faux, la nappe lirait la couleur de sommet
	// d'un plan uniforme et la hauteur de sa propre geometrie, c'est-a-dire
	// zero partout : une RVT remplie, plausible, et entierement fausse.
	//
	// UN `get` SEUL NE PROUVE RIEN SUR L'HERITAGE -- ce depot l'a consigne
	// pour les textures comme pour les switchs. Ici la valeur EFFECTIVE suffit,
	// puisque c'est elle que le rendu emploie.
	bool bDepuisTexture = false;
	FGuid Guid;
	if (Mid->GetStaticSwitchParameterValue(
		FHashedMaterialParameterInfo(TEXT("PoidsDepuisTexture")),
		bDepuisTexture, Guid))
	{
		TestTrue(TEXT("la nappe lit ses trois canaux dans ses TEXTURES"),
			bDepuisTexture);
	}
	else
	{
		AddInfo(TEXT("PoidsDepuisTexture non lisible sur l'instance dynamique"));
	}

	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
