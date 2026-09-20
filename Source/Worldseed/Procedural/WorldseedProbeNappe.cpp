// Worldseed - sonde de la nappe : de combien le sol de fond flotte-t-il ?

#include "Procedural/WorldseedProbeLibrary.h"

#include "Procedural/WorldseedProbeCommun.h"

FString UWorldseedProbeLibrary::ProbeNappe(int32 Seed, float HeightMeters,
	int32 ResolutionY, int32 Colonnes)
{
	FWorldseedSonde S;
	if (!S.Preparer(Seed, HeightMeters, ResolutionY))
	{
		UE_LOG(LogTemp, Error, TEXT("[Sonde] %s"), *S.Erreur);
		return S.Erreur;
	}

	const FWorldseedGeometry& Geo = S.World.Geometry;
	const double LargeurM = Geo.WidthM();
	const double HauteurM = Geo.HeightM;

	// ON COMPARE DEUX SURFACES, PAS UNE SURFACE A UN REGLAGE.
	//
	// Le sol de fond est bati sur le relief MACRO -- la grille 2D que la chaine
	// calcule. Le terrain voxel, lui, maille l'isovaleur zero du CHAMP, qui
	// ajoute a ce relief un deplacement vertical 3D. Les deux ne peuvent donc
	// pas coincider, et la question n'est pas de savoir s'ils divergent mais
	// DE COMBIEN -- c'est ce nombre qui dit quelle marge corrigerait le defaut.
	const int32 N = FMath::Clamp(Colonnes, 16, 512);

	TArray<double> Ecarts;
	Ecarts.Reserve(N * N);

	int32 Terres = 0;
	int32 NappeAuDessus = 0;
	double PireM = 0.0;
	double PireX = 0.0;
	double PireY = 0.0;

	for (int32 J = 0; J < N; ++J)
	{
		for (int32 I = 0; I < N; ++I)
		{
			const double X = ((I + 0.5) / N - 0.5) * LargeurM;
			const double Y = ((J + 0.5) / N - 0.5) * HauteurM;

			const double Macro = S.Densite.SurfaceHeightM(X, Y);
			if (Macro <= 1.0) { continue; }   // la mer n'a pas de nappe a porter
			++Terres;

			// LE SOL REEL EST LE PREMIER CHANGEMENT DE SIGNE EN DESCENDANT, et
			// non le relief macro : c'est la lecon que la sonde des diaclases
			// avait deja payee -- partir du macro fait compter de l'air
			// ordinaire et rend des chiffres qui ne bougent jamais.
			const double Depart = Macro + 20.0;
			double Reel = Macro;
			bool bTrouve = false;
			for (double Z = Depart; Z >= Macro - 40.0; Z -= 0.5)
			{
				if (S.Densite.At(FVector(X, Y, Z)) <= 0.0)
				{
					Reel = Z;
					bTrouve = true;
					break;
				}
			}
			if (!bTrouve) { continue; }

			// Positif = la nappe est AU-DESSUS du sol reel, donc le joueur y
			// est enfonce. C'est le seul signe qui produit le defaut signale.
			const double Ecart = Macro - Reel;
			Ecarts.Add(Ecart);
			if (Ecart > 0.5) { ++NappeAuDessus; }
			if (Ecart > PireM) { PireM = Ecart; PireX = X; PireY = Y; }
		}
	}

	if (Ecarts.Num() == 0)
	{
		return TEXT("aucune colonne de terre exploitable");
	}

	Ecarts.Sort();
	auto Centile = [&Ecarts](double Q)
	{
		const int32 K = FMath::Clamp(
			FMath::RoundToInt(Q * (Ecarts.Num() - 1)), 0, Ecarts.Num() - 1);
		return Ecarts[K];
	};

	double Somme = 0.0;
	for (const double E : Ecarts) { Somme += E; }

	UE_LOG(LogTemp, Log, TEXT("[Sonde] === LA NAPPE CONTRE LE SOL REEL ==="));
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   %d colonnes de terre sondees  |  %d ont la nappe AU-DESSUS ")
		TEXT("du sol (%.1f %%)"),
		Terres, NappeAuDessus, 100.0 * NappeAuDessus / FMath::Max(Ecarts.Num(), 1));
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   ecart nappe - sol : moyenne %.2f m, mediane %.2f, ")
		TEXT("p90 %.2f, p99 %.2f, MAX %.2f m"),
		Somme / Ecarts.Num(), Centile(0.50), Centile(0.90), Centile(0.99), PireM);
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   le pire est a (%.0f, %.0f) m"), PireX, PireY);
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   Lecture : un ecart POSITIF veut dire que la nappe flotte ")
		TEXT("au-dessus du sol sur lequel le joueur marche -- elle est dessinee, ")
		TEXT("elle n'a pas de collision, donc il la traverse et parait enfonce ")
		TEXT("dedans. C'est ce nombre qui dit de combien il faudrait l'abaisser."));

	const FString Resume = FString::Printf(
		TEXT("%.1f %% des colonnes ont la nappe au-dessus du sol ; ecart moyen ")
		TEXT("%.2f m, p99 %.2f, max %.2f"),
		100.0 * NappeAuDessus / FMath::Max(Ecarts.Num(), 1),
		Somme / Ecarts.Num(), Centile(0.99), PireM);

	UE_LOG(LogTemp, Log, TEXT("[Sonde] %s"), *Resume);
	return Resume;
}
