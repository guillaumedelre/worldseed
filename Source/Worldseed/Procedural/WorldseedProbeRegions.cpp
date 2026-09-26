// Worldseed - la sonde des regions : le decoupage, et ce qu'il vaut.
//
// A QUELLE QUESTION ELLE REPOND. Le decoupage produit des nombres qu'aucun
// oracle ne peut juger dans l'absolu : combien de regions, de quelle taille,
// et se tiennent-elles ? Un test dira qu'une partition est une partition ; il
// ne dira pas qu'un monde de 598 km2 decoupe en trois cents regions serait
// illisible, ni qu'en deux il serait inutile. C'est une question de CALAGE, et
// le calage se REGARDE.

#include "Procedural/WorldseedProbeCommun.h"
#include "Procedural/WorldseedProbeLibrary.h"
#include "Procedural/WorldseedRegions.h"

FString UWorldseedProbeLibrary::ProbeRegions(int32 Seed, float HeightMeters,
	int32 ResolutionY)
{
	FWorldseedSonde S;
	if (!S.Preparer(Seed, HeightMeters, ResolutionY))
	{
		return FString::Printf(TEXT("[regions] echec : %s"), *S.Erreur);
	}

	const FWorldseedRegions& R = S.World.Regions;
	if (!R.EstValide())
	{
		return TEXT("[regions] aucun decoupage : le monde n'en porte pas.");
	}

	FString Out;
	Out += FString::Printf(
		TEXT("[regions] graine %d  monde %.0f x %.0f m  grille %d x %d ")
		TEXT("(facteur %d, maille %.0f m)\n"),
		Seed, R.LargeurM, R.HauteurM, R.NX, R.NY, R.Facteur,
		R.LargeurM / FMath::Max(R.NX, 1));

	// --- 1. LA PARTITION EST-ELLE COMPLETE ? --------------------------------
	//
	// LE CONTROLE QUI COMPTE, et il se fait par COMPTAGE et non par confiance :
	// toute cellule de terre doit porter exactement une region, et aucune
	// cellule de mer ne doit en porter. Un trou ne leve rien -- la carte
	// montrerait simplement une tache sans nom.
	int32 Terre = 0, Mer = 0, TerreSansRegion = 0, MerAvecRegion = 0;
	int32 HorsBornes = 0;
	{
		const int32 F = FMath::Max(1, R.Facteur);
		const FWorldseedGeometry& G = S.World.Geometry;
		for (int32 J = 0; J < R.NY; ++J)
		{
			for (int32 I = 0; I < R.NX; ++I)
			{
				// Le relief moyen du bloc, comme le decoupage l'a lu.
				double Somme = 0.0;
				int32 N = 0;
				for (int32 DJ = 0; DJ < F; ++DJ)
				{
					const int32 JS = J * F + DJ;
					if (JS >= G.NY) { break; }
					for (int32 DI = 0; DI < F; ++DI)
					{
						const int32 IS = I * F + DI;
						if (IS >= G.NX) { break; }
						Somme += S.World.ElevationM[JS * G.NX + IS];
						++N;
					}
				}
				const bool bTerre = (N > 0) && (Somme / N > 0.0);
				const int16 Id = R.Id[J * R.NX + I];

				if (Id >= 0 && !R.Regions.IsValidIndex(Id)) { ++HorsBornes; }
				if (bTerre) { ++Terre; if (Id < 0) { ++TerreSansRegion; } }
				else { ++Mer; if (Id >= 0) { ++MerAvecRegion; } }
			}
		}
	}

	const double AireCelluleKm2 =
		(static_cast<double>(R.LargeurM) / R.NX / 1000.0)
		* (static_cast<double>(R.HauteurM) / R.NY / 1000.0);

	Out += FString::Printf(
		TEXT("  partition : %d cellules de terre (%.1f km2), %d de mer ; ")
		TEXT("%d terres sans region, %d mers avec, %d identifiants hors bornes\n"),
		Terre, Terre * AireCelluleKm2, Mer,
		TerreSansRegion, MerAvecRegion, HorsBornes);

	// --- 2. LA DISTRIBUTION DES AIRES ---------------------------------------
	//
	// ELLE DECIDE DE LA LISIBILITE DE LA CARTE, et une moyenne seule ne la
	// dit pas : dix regions de 10 km2 et une de 500 ont la meme moyenne qu'un
	// decoupage regulier. On rend donc les quantiles.
	TArray<float> Aires;
	Aires.Reserve(R.Regions.Num());
	for (const FWorldseedRegion& Reg : R.Regions) { Aires.Add(Reg.AireKm2); }
	Aires.Sort();

	auto Quantile = [&Aires](float Q) -> float
	{
		if (Aires.Num() == 0) { return 0.0f; }
		const int32 I = FMath::Clamp(
			FMath::RoundToInt(Q * (Aires.Num() - 1)), 0, Aires.Num() - 1);
		return Aires[I];
	};

	float Total = 0.0f;
	for (const float A : Aires) { Total += A; }

	Out += FString::Printf(
		TEXT("  %d regions, %d pays  |  aire km2 : min %.1f  p25 %.1f  ")
		TEXT("mediane %.1f  p75 %.1f  max %.1f  moyenne %.1f\n"),
		R.Regions.Num(), R.Pays.Num(),
		Quantile(0.0f), Quantile(0.25f), Quantile(0.5f), Quantile(0.75f),
		Quantile(1.0f), Total / FMath::Max(R.Regions.Num(), 1));

	// --- 2 bis. LA PART LITTORALE, ET POURQUOI ELLE EST ICI -----------------
	//
	// UN SEUIL SE LIT CONTRE LA DISTRIBUTION DU MONDE, JAMAIS CONTRE
	// L'INTUITION. `littoralPart` a d'abord ete pose a 0,45 -- « il faut
	// qu'une cellule sur deux borde l'eau » -- et n'a JAMAIS mordu : la valeur
	// mesuree ne depasse pas 0,11, parce qu'une region de 12 km2 sur une
	// maille de 62 m compte des milliers de cellules pour quelques centaines
	// de cellules de rivage. Zero region littorale, et rien ne le disait.
	//
	// Ce depot a la meme note pour la pente -- un plafond a 25 degres qui ne
	// gardait que 36 % des terres sur un monde dont la pente mediane vaut
	// 30,6 -- et pour le seuil des diaclases, pose comme une PART alors que
	// c'etait un SEUIL. La distribution part donc au releve, pour que le
	// prochain calage ne se fasse pas a l'aveugle.
	{
		TArray<float> Litt;
		Litt.Reserve(R.Regions.Num());
		for (const FWorldseedRegion& Reg : R.Regions) { Litt.Add(Reg.PartLittorale); }
		Litt.Sort();
		auto Q = [&Litt](float F) -> float
		{
			if (Litt.Num() == 0) { return 0.0f; }
			return Litt[FMath::Clamp(
				FMath::RoundToInt(F * (Litt.Num() - 1)), 0, Litt.Num() - 1)];
		};
		Out += FString::Printf(
			TEXT("  part littorale : min %.3f  mediane %.3f  p75 %.3f  ")
			TEXT("p90 %.3f  max %.3f\n"),
			Q(0.0f), Q(0.5f), Q(0.75f), Q(0.9f), Q(1.0f));
	}

	// --- 3. LES CARACTERES --------------------------------------------------
	//
	// C'EST LE CARACTERE QUI DECIDE DE LA LANGUE, donc sa repartition decide
	// de la couleur du monde. Un monde dont toutes les regions seraient
	// « plaine » parlerait une seule langue, et les six autres bases ne
	// serviraient jamais -- defaut invisible autrement que par ce compte.
	{
		int32 Compte[static_cast<int32>(EWorldseedRegionCaractere::Nombre)] = {};
		for (const FWorldseedRegion& Reg : R.Regions)
		{
			const int32 C = static_cast<int32>(Reg.Caractere);
			if (C >= 0 && C < UE_ARRAY_COUNT(Compte)) { ++Compte[C]; }
		}
		Out += TEXT("  caracteres :");
		for (int32 C = 0; C < UE_ARRAY_COUNT(Compte); ++C)
		{
			Out += FString::Printf(TEXT("  %s %d"),
				WorldseedRegions::NomDuCaractere(
					static_cast<EWorldseedRegionCaractere>(C)), Compte[C]);
		}
		Out += TEXT("\n");
	}

	// --- 4. LES NOMS SONT-ILS UNIQUES ? -------------------------------------
	//
	// DEUX REGIONS DU MEME NOM se remarquent immediatement sur une carte, et
	// aucun oracle de determinisme ne les verrait -- le monde serait
	// parfaitement reproductible, et faux.
	{
		TSet<FString> Vus;
		int32 Doublons = 0, Vides = 0;
		for (const FWorldseedRegion& Reg : R.Regions)
		{
			if (Reg.Nom.IsEmpty()) { ++Vides; continue; }
			if (Vus.Contains(Reg.Nom)) { ++Doublons; }
			Vus.Add(Reg.Nom);
		}
		for (const FWorldseedPays& P : R.Pays)
		{
			if (P.Nom.IsEmpty()) { ++Vides; continue; }
			if (Vus.Contains(P.Nom)) { ++Doublons; }
			Vus.Add(P.Nom);
		}
		Out += FString::Printf(TEXT("  noms : %d distincts, %d doublons, %d vides\n"),
			Vus.Num(), Doublons, Vides);
	}

	// --- 5. ET ON REGARDE ---------------------------------------------------
	//
	// Les pays d'abord, avec leurs regions : c'est la seule facon de juger si
	// un decoupage « se tient » -- un pays de onze regions eparpillees et un
	// pays d'une seule ne se lisent pas de la meme facon, et aucun quantile ne
	// le dit.
	Out += TEXT("\n  pays                      base          km2   reg  regions\n");
	for (const FWorldseedPays& P : R.Pays)
	{
		FString Membres;
		for (int32 K = 0; K < P.Regions.Num() && K < 6; ++K)
		{
			if (R.Regions.IsValidIndex(P.Regions[K]))
			{
				Membres += (K ? TEXT(", ") : TEXT("")) + R.Regions[P.Regions[K]].Nom;
			}
		}
		if (P.Regions.Num() > 6) { Membres += TEXT("..."); }

		Out += FString::Printf(TEXT("  %-24s  %-12s %6.0f  %3d  %s\n"),
			*P.Nom, *P.Univers, P.AireKm2, P.Regions.Num(), *Membres);
	}

	// Puis le detail des regions, plafonne : sur un monde qui en porte
	// beaucoup, la liste entiere noierait le releve.
	Out += TEXT("\n  region                 caractere      km2   alt     C    mm  litt\n");
	const int32 Montre = FMath::Min(R.Regions.Num(), 40);
	for (int32 K = 0; K < Montre; ++K)
	{
		const FWorldseedRegion& Reg = R.Regions[K];
		Out += FString::Printf(
			TEXT("  %-22s %-12s %6.1f %5.0f %5.1f %5.0f %5.2f\n"),
			*Reg.Nom, WorldseedRegions::NomDuCaractere(Reg.Caractere),
			Reg.AireKm2, Reg.AltitudeMoyenneM, Reg.TemperatureMoyenneC,
			Reg.PluieMoyenneMm, Reg.PartLittorale);
	}
	if (R.Regions.Num() > Montre)
	{
		Out += FString::Printf(TEXT("  ... et %d autres\n"),
			R.Regions.Num() - Montre);
	}

	UE_LOG(LogTemp, Log, TEXT("%s"), *Out);
	return Out;
}
