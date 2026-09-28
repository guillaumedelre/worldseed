// Worldseed - tout materiau qui peut habiller le decor doit porter la rampe.

#if WITH_DEV_AUTOMATION_TESTS

#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/SoftObjectPath.h"

/**
 * POURQUOI CE TEST EXISTE, ET CE QU'IL AURAIT ATTRAPE.
 *
 * LE DECOR D'HORIZON A CESSE D'ETRE ENFONCE PENDANT DES JOURS, EN SILENCE.
 * Il garde ses sommets a l'altitude VRAIE ; c'est un deplacement de sommets,
 * dans le materiau, qui le fait passer sous la bande creusable -- entier pres
 * de la camera, nul au loin. Cette rampe n'existait que dans
 * `M_WorldseedBiome`. Le jour ou l'habillage Orasot est devenu le seul,
 * `AWorldseedTerrain::ChooseTerrainMaterial` s'est mis a rendre
 * `MI_WorldseedGround_Orasot`, qui ne la portait pas.
 *
 * ET RIEN N'A PROTESTE. `SetScalarParameterValue` sur un parametre qu'un
 * materiau ne DECLARE PAS ne rend rien, ne journalise rien, et ne fait rien :
 * les trois lignes de `ReglerRampeDeLaNappe` sont devenues des no-op, et le
 * journal a continue d'annoncer « rampe ARMEE » a chaque partie -- il
 * rapportait ce qu'on avait DEMANDE, pas ce qui avait pris.
 *
 * CE QUE CELA DONNAIT A L'ECRAN. Le decor, reste a l'altitude macro et maille
 * a 31 m, percait le terrain voxel, qui s'en ecarte de `overhangAmplitudeM` --
 * huit metres. Signale en jeu par « il y a 2 heightmap, mon joueur s'enfonce
 * dans le sol », et mesure a **87,3 % du cadre couvert a tort** au point
 * signale, sur un A/B `-WorldseedSansNappe`.
 *
 * CE QUE CE TEST GARDE EST L'INVARIANT, PAS LE CHIFFRE : tout materiau que
 * `ChooseTerrainMaterial` peut rendre doit DECLARER `NappeRampeActive`. Il ne
 * dit rien de la valeur -- c'est la nappe qui l'arme -- seulement que le
 * parametre EXISTE, donc qu'une ecriture dessus ne sera pas muette.
 *
 * ON PASSE PAR UNE INSTANCE DYNAMIQUE, et c'est delibere : c'est exactement
 * ce que fait le jeu. Interroger le maitre ne prouverait rien sur la chaine
 * d'instances que le decor recoit reellement.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedTestRampeDecor,
	"Worldseed.Nappe.LesMateriauxPortentLaRampe",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWorldseedTestRampeDecor::RunTest(const FString& Parameters)
{
	// LES TROIS QUE `ChooseTerrainMaterial` PEUT RENDRE. La liste se tient a
	// jour a la main, et c'est assume : elle est courte, et une branche
	// ajoutee sans son materiau ici se verrait au premier decor non enfonce.
	const TCHAR* const Chemins[] = {
		TEXT("/Game/Worldseed/Materials/MI_WorldseedGround_Orasot"
			 ".MI_WorldseedGround_Orasot"),
		TEXT("/Game/Worldseed/Materials/M_WorldseedBiome.M_WorldseedBiome"),
	};

	int32 Eprouves = 0;

	for (const TCHAR* const Chemin : Chemins)
	{
		UMaterialInterface* const Mat =
			Cast<UMaterialInterface>(FSoftObjectPath(Chemin).TryLoad());

		// `Content/` EST EXCLU DU DEPOT : un clone frais n'a pas ces assets.
		// On le DIT et l'on passe -- un test qui echoue faute de contenu
		// apprendrait seulement qu'il manque du contenu.
		if (!Mat)
		{
			AddInfo(FString::Printf(
				TEXT("materiau absent, non eprouve : %s"), Chemin));
			continue;
		}

		UMaterialInstanceDynamic* const Mid =
			UMaterialInstanceDynamic::Create(Mat, GetTransientPackage());
		if (!TestNotNull(TEXT("instance dynamique creee"), Mid))
		{
			continue;
		}

		float Lu = -1.0f;
		const bool bPortee = Mid->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("NappeRampeActive")), Lu);

		TestTrue(FString::Printf(
			TEXT("%s declare NappeRampeActive -- sans quoi l'enfoncement du ")
			TEXT("decor serait un no-op SILENCIEUX et l'on verrait deux reliefs"),
			*Mat->GetName()), bPortee);

		// ET LA PORTE ARRIVE FERMEE. Les chunks du terrain partagent ce
		// materiau et ne posent aucun UV : si la rampe etait armee par
		// defaut, ils se deplaceraient eux aussi. Seul le decor l'arme, par
		// son instance dynamique.
		if (bPortee)
		{
			TestEqual(FString::Printf(
				TEXT("%s arrive avec la rampe ETEINTE"), *Mat->GetName()),
				Lu, 0.0f);
			++Eprouves;
		}

		float Debut = -1.0f;
		float Fin = -1.0f;
		TestTrue(FString::Printf(TEXT("%s declare NappeRampeDebutCm"),
			*Mat->GetName()),
			Mid->GetScalarParameterValue(
				FMaterialParameterInfo(TEXT("NappeRampeDebutCm")), Debut));
		TestTrue(FString::Printf(TEXT("%s declare NappeRampeFinCm"),
			*Mat->GetName()),
			Mid->GetScalarParameterValue(
				FMaterialParameterInfo(TEXT("NappeRampeFinCm")), Fin));

		// LA RAMPE DOIT AVOIR UNE COURSE. Debut et Fin egaux donneraient une
		// division par zero dans le materiau ; Fin sous Debut inverserait le
		// sens et enfoncerait le decor AU LOIN au lieu de pres du joueur --
		// soit exactement le defaut, dans l'autre sens.
		if (Debut >= 0.0f && Fin >= 0.0f)
		{
			TestTrue(FString::Printf(
				TEXT("%s : la fin de rampe depasse son debut (%.0f > %.0f)"),
				*Mat->GetName(), Fin, Debut), Fin > Debut);
		}
	}

	AddInfo(FString::Printf(
		TEXT("%d materiau(x) d'habillage eprouve(s)"), Eprouves));

	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
