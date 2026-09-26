// Worldseed - ou se pose le NOM d'une region, et pourquoi pas sur son centre.
//
// LE DEFAUT QUE CE FICHIER GARDE EST SILENCIEUX. Une etiquette posee sur le
// centre de gravite d'une region CONCAVE tombe hors d'elle -- en pleine mer,
// ou chez la voisine -- et rien ne le signale : le nom s'affiche, il est
// lisible, il designe simplement le mauvais endroit. Mesure sur le monde de
// reference avant correction : **6 regions sur 47**, soit une sur huit.
//
// ON NE TESTE PAS SUR UN MONDE GENERE, et c'est delibere. Les formes concaves
// y existent mais rien ne garantit qu'une graine donnee en porte -- un test
// qui depend de la chance ne garde rien. On fabrique donc la forme qui EXHIBE
// le defaut, et l'on verifie que le remede la traite.

#if WITH_DEV_AUTOMATION_TESTS

#include "Procedural/WorldseedRegions.h"

#include "Misc/AutomationTest.h"

namespace
{
	/**
	 * Un decoupage fabrique, avec une region dont le barycentre est DEHORS.
	 *
	 * La region 0 a la forme d'un U : deux montants et un fond. Son centre de
	 * gravite tombe dans le CREUX, qui n'est pas a elle -- c'est exactement
	 * ce qui arrive a un bassin en fer a cheval autour d'un massif, ou a une
	 * region qui epouse une baie.
	 *
	 * La region 1 occupe le creux. Elle est convexe : son propre centre tombe
	 * chez elle, et elle sert donc de TEMOIN INTERNE -- si le recalage la
	 * deplacait aussi, il ferait plus que ce qu'on lui demande.
	 */
	FWorldseedRegions FabriquerUnU()
	{
		FWorldseedRegions D;
		D.NX = 16;
		D.NY = 16;
		D.LargeurM = 16000.0f;
		D.HauteurM = 16000.0f;
		D.Facteur = 1;
		D.Id.Init(-1, D.NX * D.NY);

		for (int32 J = 0; J < D.NY; ++J)
		{
			for (int32 I = 0; I < D.NX; ++I)
			{
				// Les deux montants, et le fond du U.
				const bool bMontant = (I < 4) || (I >= 12);
				const bool bFond = (J < 4);
				const bool bCreux = !bMontant && !bFond && (J < 12);

				if (bMontant || bFond) { D.Id[J * D.NX + I] = 0; }
				else if (bCreux) { D.Id[J * D.NX + I] = 1; }
			}
		}

		FWorldseedRegion R0;
		R0.Id = 0;
		R0.Pays = 0;
		R0.Nom = TEXT("Le U");
		FWorldseedRegion R1;
		R1.Id = 1;
		R1.Pays = 0;
		R1.Nom = TEXT("Le creux");
		D.Regions = { R0, R1 };

		FWorldseedPays P;
		P.Id = 0;
		P.Nom = TEXT("Pays du U");
		P.Regions = { 0, 1 };
		D.Pays = { P };

		// LES CENTRES DE GRAVITE SE CALCULENT ICI, comme la chaine le fait :
		// `AncrerLesEtiquettes` les LIT, elle ne les produit pas.
		FVector2D Somme[2] = { FVector2D::ZeroVector, FVector2D::ZeroVector };
		int32 Compte[2] = { 0, 0 };
		for (int32 J = 0; J < D.NY; ++J)
		{
			for (int32 I = 0; I < D.NX; ++I)
			{
				const int32 R = D.Id[J * D.NX + I];
				if (R < 0) { continue; }
				Somme[R] += FVector2D(
					((static_cast<double>(I) + 0.5) / D.NX - 0.5) * D.LargeurM,
					((static_cast<double>(J) + 0.5) / D.NY - 0.5) * D.HauteurM);
				++Compte[R];
			}
		}
		for (int32 R = 0; R < 2; ++R)
		{
			D.Regions[R].CentreM = Somme[R] / FMath::Max(1, Compte[R]);
		}
		D.Pays[0].CentreM = (Somme[0] + Somme[1])
			/ FMath::Max(1, Compte[0] + Compte[1]);

		return D;
	}
}

/**
 * L'ANCRAGE TOMBE TOUJOURS DANS SA REGION, LE CENTRE DE GRAVITE NON.
 *
 * LE TEMOIN EST DANS LE TEST, et il porte sur la donnee elle-meme : on
 * verifie d'abord que le centre de gravite du U est bien DEHORS. Sans ce
 * controle, une fixture qui n'exhiberait pas le defaut laisserait le test
 * passer sans rien garder -- ce depot a paye quatre fixtures muettes dans une
 * seule journee, dont une rampe uniforme qui ne contenait pas le cas.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedRegionsAncrageTest,
	"Worldseed.Regions.LEtiquetteTombeDansSaRegion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedRegionsAncrageTest::RunTest(const FString& Parameters)
{
	FWorldseedRegions D = FabriquerUnU();

	TestTrue(TEXT("le decoupage fabrique est valide"), D.EstValide());

	// LE TEMOIN : la fixture doit REELLEMENT porter le defaut.
	const int32 SousLeCentre =
		D.RegionEn(D.Regions[0].CentreM.X, D.Regions[0].CentreM.Y);
	TestNotEqual(
		TEXT("le centre de gravite du U tombe HORS du U -- sinon la fixture "
			"ne contient pas le cas et le test ne garde rien"),
		SousLeCentre, 0);

	// Et la region convexe, elle, doit contenir le sien.
	TestEqual(TEXT("le creux, convexe, contient son propre centre"),
		D.RegionEn(D.Regions[1].CentreM.X, D.Regions[1].CentreM.Y), 1);

	const FVector2D CentreDuCreuxAvant = D.Regions[1].CentreM;

	WorldseedRegions::AncrerLesEtiquettes(D);

	for (const FWorldseedRegion& R : D.Regions)
	{
		TestEqual(FString::Printf(
			TEXT("l'ancrage de « %s » tombe dans « %s »"), *R.Nom, *R.Nom),
			D.RegionEn(R.AncrageM.X, R.AncrageM.Y), R.Id);
	}

	for (const FWorldseedPays& P : D.Pays)
	{
		TestEqual(FString::Printf(
			TEXT("l'ancrage du pays « %s » tombe dans ce pays"), *P.Nom),
			D.PaysEn(P.AncrageM.X, P.AncrageM.Y), P.Id);
	}

	// ON NE DEPLACE QUE CE QU'IL FAUT. Une region dont le centre lui
	// appartient doit garder ce centre : un recalage qui bougerait tout le
	// monde eloignerait inutilement les noms de leur place naturelle.
	TestEqual(TEXT("le creux garde exactement son centre de gravite"),
		D.Regions[1].AncrageM, CentreDuCreuxAvant);

	AddInfo(FString::Printf(
		TEXT("U : centre (%.0f, %.0f) -> ancrage (%.0f, %.0f)"),
		D.Regions[0].CentreM.X, D.Regions[0].CentreM.Y,
		D.Regions[0].AncrageM.X, D.Regions[0].AncrageM.Y));
	return true;
}

/**
 * L'ANCRAGE NE TRAVERSE PAS LE MERIDIEN DE BORDURE.
 *
 * La longitude s'enroule. Sans repli, une region a cheval sur le bord voit
 * ses cellules a une DEMI-CIRCONFERENCE de son propre centre, et la « cellule
 * la plus proche » se trouve alors a l'oppose du monde -- l'etiquette
 * partirait sur l'autre face de la planete.
 *
 * Le temoin est la distance : sans enroulement, l'ancrage retenu serait au
 * milieu de la carte, a des milliers de kilometres.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldseedRegionsAncrageMeridienTest,
	"Worldseed.Regions.LAncrageSEnrouleEnLongitude",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldseedRegionsAncrageMeridienTest::RunTest(const FString& Parameters)
{
	FWorldseedRegions D;
	D.NX = 32;
	D.NY = 16;
	D.LargeurM = 32000.0f;
	D.HauteurM = 16000.0f;
	D.Facteur = 1;
	D.Id.Init(-1, D.NX * D.NY);

	// UN ANNEAU OUVERT A CHEVAL SUR LE BORD : colonnes 30, 31, 0, 1, mais
	// creux au milieu en latitude, pour que le barycentre tombe dehors.
	for (int32 J = 0; J < D.NY; ++J)
	{
		const bool bCreux = (J >= 6 && J < 10);
		for (const int32 I : { 30, 31, 0, 1 })
		{
			if (!bCreux) { D.Id[J * D.NX + I] = 0; }
		}
	}

	FWorldseedRegion R;
	R.Id = 0;
	R.Pays = INDEX_NONE;
	R.Nom = TEXT("Anneau");
	D.Regions = { R };

	// Le centre de gravite, calcule AVEC enroulement -- c'est ce que fait la
	// chaine, par la moyenne des angles.
	double SC = 0.0, SS = 0.0, SY = 0.0;
	int32 N = 0;
	for (int32 J = 0; J < D.NY; ++J)
	{
		for (int32 I = 0; I < D.NX; ++I)
		{
			if (D.Id[J * D.NX + I] != 0) { continue; }
			const double A = (static_cast<double>(I) + 0.5) / D.NX * 2.0 * PI;
			SC += FMath::Cos(A);
			SS += FMath::Sin(A);
			SY += (static_cast<double>(J) + 0.5) / D.NY - 0.5;
			++N;
		}
	}
	const double Angle = FMath::Atan2(SS / N, SC / N);
	const double U = (Angle < 0.0 ? Angle + 2.0 * PI : Angle) / (2.0 * PI);
	D.Regions[0].CentreM = FVector2D((U - 0.5) * D.LargeurM,
		(SY / N) * D.HauteurM);

	TestTrue(TEXT("le decoupage fabrique est valide"), D.EstValide());
	TestNotEqual(TEXT("le centre de gravite de l'anneau tombe dehors"),
		D.RegionEn(D.Regions[0].CentreM.X, D.Regions[0].CentreM.Y), 0);

	WorldseedRegions::AncrerLesEtiquettes(D);

	TestEqual(TEXT("l'ancrage de l'anneau tombe dans l'anneau"),
		D.RegionEn(D.Regions[0].AncrageM.X, D.Regions[0].AncrageM.Y), 0);

	// LE CONTROLE QUI VOIT L'ABSENCE D'ENROULEMENT : l'ancrage doit rester
	// PRES du bord, pas partir au milieu de la carte. Un quart de largeur est
	// large -- l'anneau ne fait que quatre colonnes sur trente-deux -- et ne
	// laisse pourtant passer aucune fuite a l'oppose.
	double DX = FMath::Abs(D.Regions[0].AncrageM.X);
	DX = FMath::Min(DX, D.LargeurM - DX);
	TestTrue(FString::Printf(
		TEXT("l'ancrage reste au bord, la ou est l'anneau (|x| = %.0f m)"),
		D.Regions[0].AncrageM.X),
		D.Regions[0].AncrageM.X < -D.LargeurM * 0.4
		|| D.Regions[0].AncrageM.X > D.LargeurM * 0.4);
	return true;
}

#endif
