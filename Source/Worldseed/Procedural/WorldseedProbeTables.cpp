// Worldseed - la sonde du plateau disseque : mesas et canyons.
//
// CE QU'ELLE MESURE, ET POURQUOI CE N'EST PAS EVIDENT. Une mesa ne se prouve
// pas en comptant des cellules abaissees : une passe qui tourne et une passe
// qui produit la bonne FORME se ressemblent parfaitement vues du relief fini.
// Ce qui definit une table, c'est une conjonction :
//
//   1. le sommet est PLAT, alors que la pente mediane des terres est elevee ;
//   2. le bord est RAIDE, donc la distribution des pentes devient BIMODALE --
//      beaucoup de plat, beaucoup de vertical, peu d'intermediaire, la ou un
//      relief ordinaire est unimodal ;
//   3. et surtout les sommets voisins PARTAGENT UNE ALTITUDE, parce qu'ils
//      sont les morceaux d'une seule ancienne surface. C'est ce troisieme
//      point qui separe une table d'une colline a sommet plat, et c'est le
//      seul qu'aucun bruit ne sait produire.
//
// ELLE PORTE AUSSI UN ENTONNOIR, et c'est ce qui la rend utile au reglage. Une
// couverture trop faible peut venir de six gardes differentes ; sans le compte
// de chacune, on regle au hasard le mauvais bouton -- ce que le depot a deja
// paye quatre fois de suite sur le routage des galeries, ou aucune correction
// ne bougeait la mesure parce qu'elle MELANGEAIT deux populations.
//
// LE TEMOIN EST SPATIAL, PAS TEMPOREL, et c'est un choix. Comparer deux
// generations demanderait de changer une regle entre les deux -- or le depot a
// paye ce piege : le PIE NE RELIT PAS world_rules.json, seules les sondes le
// font, si bien qu'un temoin ainsi obtenu mesurait exactement la meme chose que
// le cas teste, au centieme pres. Ici, les deux populations vivent dans LE MEME
// monde : les cellules en zone, et celles qui passent toutes les autres gardes
// mais que le masque de region a laissees dehors.

#include "Procedural/WorldseedProbeLibrary.h"

#include "Procedural/WorldseedProbeCommun.h"

#include "Procedural/WorldseedFins.h"
#include "Procedural/WorldseedFlow.h"
#include "Procedural/WorldseedPlateau.h"
#include "Procedural/WorldseedStrata.h"
#include "Procedural/WorldseedRules.h"

namespace WorldseedProbeTablesDetail
{
	static float Mediane(TArray<float>& V)
	{
		if (V.Num() == 0) { return -1.0f; }
		V.Sort();
		return V[V.Num() / 2];
	}

	/** Quantile par tri. Q dans [0..1]. */
	static float Quantile(TArray<float>& V, float Q)
	{
		if (V.Num() == 0) { return -1.0f; }
		V.Sort();
		const int32 I = FMath::Clamp(
			FMath::RoundToInt(Q * (V.Num() - 1)), 0, V.Num() - 1);
		return V[I];
	}

	static float EcartType(const TArray<float>& V)
	{
		if (V.Num() < 2) { return -1.0f; }
		double S = 0.0;
		for (float X : V) { S += X; }
		const double M = S / V.Num();
		double Q = 0.0;
		for (float X : V) { Q += (X - M) * (X - M); }
		return static_cast<float>(FMath::Sqrt(Q / (V.Num() - 1)));
	}

	struct FPopulation
	{
		int32 Cellules = 0;
		int32 Plates = 0;
		int32 Versants = 0;
		int32 Escarpees = 0;
		TArray<float> Pentes;
		TArray<float> EcartsSommets;
	};
}

FString UWorldseedProbeLibrary::ProbeTables(int32 Seed, float HeightMeters,
	int32 ResolutionY)
{
	using namespace WorldseedProbeTablesDetail;

	FWorldseedSonde S;
	if (!S.Preparer(Seed, HeightMeters, ResolutionY))
	{
		return S.Erreur;
	}

	const FWorldseedPlateauRules R = FWorldseedPlateauRules::FromRules(*S.Regles);
	const FWorldseedGeometry& G = S.World.Geometry;
	const TArray<float>& H = S.World.ElevationM;
	const TArray<float>& Pluie = S.World.Climate.PrecipMm;

	const int32 NX = G.NX;
	const int32 NY = G.NY;
	const int32 Count = G.CellCount();
	const float MailleM = FMath::Max(G.MetersPerPixel(), 1e-3f);
	const int32 Rayon = FMath::Clamp(FMath::CeilToInt(R.ReachM / MailleM), 1, 24);

	const bool bPluie = (Pluie.Num() == Count);
	const bool bRoche = S.World.Lithology.IsValid(Count);
	const double LargeurM = G.WidthM();
	const double HauteurM = G.HeightM;

	// LES MEMES FONCTIONS QUE LA PASSE, jamais une reimplementation.
	TArray<float> Plateau;
	TArray<float> ReliefLocal;
	WorldseedPlateau::Surfaces(G, H, R, Plateau, ReliefLocal);
	if (Plateau.Num() != Count) { return TEXT("surfaces indisponibles"); }

	// Le drainage, recalcule comme la passe le fait. Il est pris sur le relief
	// FINAL et non sur celui d'avant la passe ; l'ecart est negligeable tant
	// que la passe ne remodele qu'une part infime des terres, et il est
	// signale plutot que tu.
	TArray<float> Poids;
	Poids.SetNumUninitialized(Count);
	for (int32 I = 0; I < Count; ++I)
	{
		Poids[I] = bPluie ? FMath::Max(Pluie[I], 1.0f) : 1.0f;
	}
	FWorldseedFlow Flux;
	WorldseedFlow::Compute(H, Poids, NX, NY, 0.0f, 1e-4f, Flux);
	const bool bFlux = (Flux.Accumulation.Num() == Count);
	const float InvLn10 = 0.4342944819f;

	auto Index = [NX, NY](int32 I, int32 J)
	{
		const int32 K = ((I % NX) + NX) % NX;
		return FMath::Clamp(J, 0, NY - 1) * NX + K;
	};

	// --- L'ENTONNOIR ---------------------------------------------------------
	//
	// SANS LUI, ON REGLE AU HASARD. Six gardes peuvent expliquer une couverture
	// faible, et leur effet se compose : savoir laquelle mord evite de
	// deplacer la mauvaise valeur, ce que le depot a deja paye quatre fois de
	// suite sur le routage des galeries.
	int32 Terres = 0, PasseAltitude = 0, PasseePluie = 0, PasseRelief = 0;
	int32 PasseRoche = 0, PasseZone = 0;

	FPopulation EnZone, HorsZone;
	TArray<uint8> Sommet; Sommet.Init(0, Count);
	TArray<uint8> Zone;   Zone.Init(0, Count);
	TArray<float> LogAccum;   // sur la population eligible, AVANT le masque

	for (int32 J = 0; J < NY; ++J)
	{
		for (int32 I = 0; I < NX; ++I)
		{
			const int32 C = J * NX + I;
			if (H[C] <= 0.0f) { continue; }
			++Terres;

			if (H[C] < R.MinElevationM) { continue; }
			++PasseAltitude;
			if (bPluie && Pluie[C] > R.PrecipMaxMm) { continue; }
			++PasseePluie;
			if (ReliefLocal[C] > R.LocalReliefMaxM) { continue; }
			++PasseRelief;
			if (bRoche)
			{
				const uint8 Id = S.World.Lithology.Id[C];
				const float D = S.Litho.Catalogue.IsValidIndex(Id)
					? S.Litho.Catalogue[Id].Hardness : 1.0f;
				if (D < R.HardnessMin || D > R.HardnessMax) { continue; }
			}
			++PasseRoche;

			if (bFlux)
			{
				LogAccum.Add(FMath::Loge(1.0f + Flux.Accumulation[C]) * InvLn10);
			}

			const double X = (static_cast<double>(I) / NX - 0.5) * LargeurM;
			const double Y = (static_cast<double>(J) / NY - 0.5) * HauteurM;
			const float Z = WorldseedPlateau::ZoneAt(X, Y, R, Seed);
			const bool bZ = (Z > 0.0f);
			if (bZ) { ++PasseZone; }
			Zone[C] = bZ ? 1 : 2;

			const float Dx = (H[Index(I + 1, J)] - H[Index(I - 1, J)]) / (2.0f * MailleM);
			const float Dy = (H[Index(I, J + 1)] - H[Index(I, J - 1)]) / (2.0f * MailleM);
			const float PenteDeg = FMath::RadiansToDegrees(
				FMath::Atan(FMath::Sqrt(Dx * Dx + Dy * Dy)));

			FPopulation& P = bZ ? EnZone : HorsZone;
			++P.Cellules;
			P.Pentes.Add(PenteDeg);
			if (PenteDeg < 5.0f) { ++P.Plates; }
			else if (PenteDeg < 25.0f) { ++P.Versants; }
			if (PenteDeg > 45.0f) { ++P.Escarpees; }

			// UN SOMMET PLAT N'EST PAS UNE CELLULE PLATE : un fond de vallee
			// est plat lui aussi. La condition est double -- pente faible ET
			// altitude dominant nettement le voisinage.
			if (PenteDeg < 5.0f)
			{
				float Bas = 1e9f;
				const int32 Pas = FMath::Max(1, Rayon / 4);
				for (int32 D = -Rayon; D <= Rayon; D += Pas)
				{
					Bas = FMath::Min(Bas, H[Index(I + D, J)]);
					Bas = FMath::Min(Bas, H[Index(I, J + D)]);
				}
				if (H[C] - Bas > 0.5f * R.ScarpM) { Sommet[C] = 1; }
			}
		}
	}

	// --- LES SOMMETS VOISINS PARTAGENT-ILS UNE ALTITUDE ? -------------------
	const int32 Bloc = FMath::Max(2 * Rayon, 4);
	for (int32 J0 = 0; J0 < NY; J0 += Bloc)
	{
		for (int32 I0 = 0; I0 < NX; I0 += Bloc)
		{
			TArray<float> AZ, AH;
			for (int32 J = J0; J < FMath::Min(J0 + Bloc, NY); ++J)
			{
				for (int32 I = I0; I < FMath::Min(I0 + Bloc, NX); ++I)
				{
					const int32 C = J * NX + I;
					if (!Sommet[C]) { continue; }
					if (Zone[C] == 1) { AZ.Add(H[C]); } else if (Zone[C] == 2) { AH.Add(H[C]); }
				}
			}
			if (AZ.Num() >= 6) { EnZone.EcartsSommets.Add(EcartType(AZ)); }
			if (AH.Num() >= 6) { HorsZone.EcartsSommets.Add(EcartType(AH)); }
		}
	}

	// --- LES BANCS ONT-ILS MORDU ? -----------------------------------------
	//
	// C'EST LA SEULE MESURE QUI REPOND, et elle n'est pas evidente. Une passe
	// d'erosion qui lit les bancs et une passe qui les ignore produisent deux
	// reliefs qui se ressemblent en pente moyenne : le contraste ne se voit
	// pas dans un agregat.
	//
	// LE TEMOIN EST L'HISTOGRAMME DES ALTITUDES. Si les bancs durs freinent
	// l'erosion, la surface s'y ATTARDE : les altitudes des terres couvertes
	// se massent alors au TOIT des bancs durs, au lieu de se repartir
	// uniformement dans la serie. On compare donc la part qui tombe pres d'un
	// toit dur a ce qu'un tirage uniforme donnerait -- un rapport superieur a
	// 1 dit que les corniches existent.
	FString Gradins = TEXT("serie inactive");
	{
		const FWorldseedStratRules SR = FWorldseedStratRules::FromRules(*S.Regles, S.Litho);
		FWorldseedErodibilite Erod;
		WorldseedStrata::PreparerErodibilite(G, S.World.Lithology, S.Litho, SR,
			static_cast<float>(S.Regles->Num(TEXT("erosion"), TEXT("duretePoids"), 0.0)),
			Seed, Erod);

		if (Erod.IsActive() && SR.Serie.Num() == Erod.BasCumulM.Num())
		{
			// --- UN TEST SANS FENETRE, ET LE PREMIER EN AVAIT UNE ----------
			//
			// La premiere version comptait les cellules situees a moins de
			// douze metres du TOIT d'un banc dur. Elle marchait sur des bancs
			// epais et s'est effondree des qu'on les a amincis : a 18-35 m
			// d'epaisseur, les fenetres se recouvrent et « pres d'un toit dur »
			// devient vrai presque partout -- 47,4 % attendus au hasard, donc
			// plus aucun pouvoir discriminant. Meme famille de faute que « un
			// seuil n'est pas une part » : une mesure dont la reference derive
			// avec le reglage qu'on teste ne mesure rien.
			//
			// CELUI-CI N'A PAS DE FENETRE. Si les bancs durs freinent
			// l'erosion, la surface s'y ATTARDE : la durete moyenne du banc
			// qu'elle OCCUPE depasse alors la moyenne de la serie, ponderee par
			// les epaisseurs. Le rapport des deux est sans dimension, sans
			// reglage, et ne sature pas.
			// --- ET LA VRAIE SIGNATURE EST LA PENTE, PAS L'ALTITUDE --------
			//
			// A l'equilibre soulevement / erosion, la loi de puissance de
			// courant donne `S = (U / K.A^m)^(1/n)` : un banc dur ne tient pas
			// une ALTITUDE plus haute, il tient une PENTE plus raide. Tester
			// l'altitude revenait donc a chercher un escalier la ou la physique
			// implementee produit un changement d'inclinaison. Le depot avait
			// deja etabli exactement cela sur les roches 2D -- granite 25
			// degres contre calcaire 9,5.
			double PenteDur = 0.0;
			int32 NDur = 0;
			double PenteTendre = 0.0;
			int32 NTendre = 0;
			const float CapMin = static_cast<float>(
				S.Regles->Num(TEXT("tables"), TEXT("chapiteauDureteMin"), 0.40));

			double SommeSurface = 0.0;
			int32 DansSerie = 0;
			for (int32 C = 0; C < Count; ++C)
			{
				if (H[C] <= 0.0f || !Erod.Couvert.IsValidIndex(C) || Erod.Couvert[C] == 0)
				{
					continue;
				}
				const float D = Erod.DatumM[C] - H[C];
				if (D < 0.0f || D > SR.TotalThicknessM) { continue; }

				const int32 B = Erod.BancAProfondeur(D);
				if (!SR.Serie.IsValidIndex(B)) { continue; }
				SommeSurface += SR.Serie[B].Hardness;
				++DansSerie;

				const int32 I = C % NX;
				const int32 J = C / NX;
				const float Dx = (H[Index(I + 1, J)] - H[Index(I - 1, J)]) / (2.0f * MailleM);
				const float Dy = (H[Index(I, J + 1)] - H[Index(I, J - 1)]) / (2.0f * MailleM);
				const double Pente = FMath::RadiansToDegrees(
					FMath::Atan(FMath::Sqrt(Dx * Dx + Dy * Dy)));

				if (SR.Serie[B].Hardness >= CapMin) { PenteDur += Pente; ++NDur; }
				else { PenteTendre += Pente; ++NTendre; }
			}

			double SommeSerie = 0.0;
			double SommeEpaisseur = 0.0;
			for (const FWorldseedStratBanc& B : SR.Serie)
			{
				SommeSerie += static_cast<double>(B.Hardness) * B.ThicknessM;
				SommeEpaisseur += B.ThicknessM;
			}
			const double MoyenneSerie = (SommeEpaisseur > 0.0)
				? SommeSerie / SommeEpaisseur : 0.0;
			const double MoyenneSurface = (DansSerie > 0)
				? SommeSurface / DansSerie : 0.0;

			const double MoyDur = (NDur > 0) ? PenteDur / NDur : 0.0;
			const double MoyTendre = (NTendre > 0) ? PenteTendre / NTendre : 0.0;

			Gradins = FString::Printf(
				TEXT("%d cellules dans la serie.") LINE_TERMINATOR
				TEXT("[Worldseed]     PENTE par banc -- dur %.2f deg (%d cellules) contre ")
				TEXT("tendre %.2f deg (%d) : ecart %+.2f deg, rapport %.2f") LINE_TERMINATOR
				TEXT("[Worldseed]     ALTITUDE -- durete du banc occupe %.3f contre %.3f ")
				TEXT("en moyenne de serie, rapport %.3f"),
				DansSerie,
				MoyDur, NDur, MoyTendre, NTendre, MoyDur - MoyTendre,
				(MoyTendre > 0.0) ? MoyDur / MoyTendre : 0.0,
				MoyenneSurface, MoyenneSerie,
				(MoyenneSerie > 0.0) ? MoyenneSurface / MoyenneSerie : 0.0);
		}
	}

	const double PartTerres = 100.0 * Terres / FMath::Max(Count, 1);
	auto Pct = [Terres](int32 N) { return 100.0 * N / FMath::Max(Terres, 1); };

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] === PLATEAU DISSEQUE : mesas et canyons ==="));
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   graine %d, %.0f km, grille %dx%d, maille %.0f m, rayon %d cellules"),
		Seed, HeightMeters / 1000.0f, NX, NY, MailleM, Rayon);
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   part emergee %.2f %%  (invariant du projet : 29,2 %%)"), PartTerres);
	UE_LOG(LogTemp, Log, TEXT("[Worldseed]   GRADINS : %s"), *Gradins);
	UE_LOG(LogTemp, Log, TEXT("[Worldseed]"));
	UE_LOG(LogTemp, Log, TEXT("[Worldseed]   ENTONNOIR -- ou les cellules se perdent"));
	UE_LOG(LogTemp, Log, TEXT("[Worldseed]     terres                        %8d  100.00 %%"), Terres);
	UE_LOG(LogTemp, Log, TEXT("[Worldseed]     altitude >= %6.0f m          %8d  %6.2f %%"),
		R.MinElevationM, PasseAltitude, Pct(PasseAltitude));
	UE_LOG(LogTemp, Log, TEXT("[Worldseed]     pluie <= %6.0f mm            %8d  %6.2f %%"),
		R.PrecipMaxMm, PasseePluie, Pct(PasseePluie));
	UE_LOG(LogTemp, Log, TEXT("[Worldseed]     relief local <= %5.0f m       %8d  %6.2f %%"),
		R.LocalReliefMaxM, PasseRelief, Pct(PasseRelief));
	UE_LOG(LogTemp, Log, TEXT("[Worldseed]     durete %.2f a %.2f            %8d  %6.2f %%"),
		R.HardnessMin, R.HardnessMax, PasseRoche, Pct(PasseRoche));
	UE_LOG(LogTemp, Log, TEXT("[Worldseed]     masque de region > %.2f       %8d  %6.2f %%  <- la zone"),
		R.ZoneThreshold, PasseZone, Pct(PasseZone));

	// LE SEUIL DE DRAINAGE SE LIT CONTRE LA DISTRIBUTION DU MONDE, jamais
	// contre l'intuition. Le depot a paye cette lecon sur la pente : un
	// plafond pose a 25 degres "parce que l'herbe pousse jusque-la" ne gardait
	// que 36,7 % des terres d'un monde dont la pente mediane vaut 30,6.
	UE_LOG(LogTemp, Log, TEXT("[Worldseed]"));
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   FLUX ACCUMULE en log10, sur les %d cellules eligibles avant masque :"),
		LogAccum.Num());
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]     p50 %.2f   p75 %.2f   p90 %.2f   p97 %.2f   p99 %.2f   max %.2f"),
		Quantile(LogAccum, 0.50f), Quantile(LogAccum, 0.75f), Quantile(LogAccum, 0.90f),
		Quantile(LogAccum, 0.97f), Quantile(LogAccum, 0.99f), Quantile(LogAccum, 1.00f));
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]     seuil en vigueur %.2f : il coupe donc la queue au-dessus de ce rang."),
		R.DrainStart);

	UE_LOG(LogTemp, Log, TEXT("[Worldseed]"));
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   %-12s %9s %10s %10s %9s %9s %12s"),
		TEXT("population"), TEXT("cellules"), TEXT("pente med"),
		TEXT("plat<5deg"), TEXT("5-25deg"), TEXT(">45deg"), TEXT("ecart somm."));
	for (int32 K = 0; K < 2; ++K)
	{
		FPopulation& P = (K == 0) ? EnZone : HorsZone;
		const int32 N = FMath::Max(P.Cellules, 1);
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   %-12s %9d %10.1f %9.1f %% %8.1f %% %8.1f %% %9.0f m"),
			(K == 0) ? TEXT("EN ZONE") : TEXT("temoin"), P.Cellules,
			Mediane(P.Pentes), 100.0 * P.Plates / N, 100.0 * P.Versants / N,
			100.0 * P.Escarpees / N, Mediane(P.EcartsSommets));
	}
	UE_LOG(LogTemp, Log, TEXT("[Worldseed]"));
	// L'ECART DE SOMMETS NE SE LIT PAS CONTRE LE TEMOIN, ET C'EST UN PIEGE QUE
	// CETTE SONDE A D'ABORD TENDU. Le temoin est une PLAINE : ses rares sommets
	// y partagent trivialement leur altitude, donc son ecart est petit par
	// absence de relief, pas par partage d'une surface. Compare a lui, la zone
	// parait toujours PIRE. La seule lecture juste est RELATIVE A L'ESCARPEMENT :
	// des tables qui se partagent une ancienne surface gardent un ecart de
	// quelques pour cent de la hauteur qui les separe du fond du canyon.
	TArray<float> EZ2 = EnZone.EcartsSommets;
	const float EcartZone = Mediane(EZ2);
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   Lecture : une table donne beaucoup de plat ET beaucoup de raide. ")
		TEXT("L'ecart de sommets se lit RELATIVEMENT A L'ESCARPEMENT, jamais contre le ")
		TEXT("temoin -- une plaine a un ecart minuscule par absence de relief, pas par ")
		TEXT("partage d'une surface. Ici %.0f m pour %.0f m d'escarpement, soit %.1f %% : ")
		TEXT("c'est cette part qui dit si les sommets sont les morceaux d'UNE surface."),
		EcartZone, R.ScarpM,
		(R.ScarpM > 0.0f && EcartZone >= 0.0f) ? 100.0f * EcartZone / R.ScarpM : -1.0f);

	return FString::Printf(
		TEXT("zone %.2f %% des terres ; raide %.1f %% contre %.1f %% ; ecart sommets %.0f m contre %.0f m"),
		Pct(PasseZone),
		100.0 * EnZone.Escarpees / FMath::Max(EnZone.Cellules, 1),
		100.0 * HorsZone.Escarpees / FMath::Max(HorsZone.Cellules, 1),
		Mediane(EnZone.EcartsSommets), Mediane(HorsZone.EcartsSommets));
}
