// Worldseed - la colonne stratigraphique : la roche varie aussi en Z.

#include "Procedural/WorldseedStrata.h"

#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedPerlin.h"
#include "Procedural/WorldseedRules.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

FWorldseedStratRules FWorldseedStratRules::FromRules(const UWorldseedRules& Rules,
	const FWorldseedLithologyRules& Litho)
{
	FWorldseedStratRules Out;

	const TCHAR* STR = TEXT("strates");
	auto Num = [&Rules, STR](const TCHAR* Key, double Fallback)
	{
		return static_cast<float>(Rules.Num(STR, Key, Fallback));
	};

	Out.DatumM = Num(TEXT("datumM"), 520.0);
	Out.WarpAmplitudeM = Num(TEXT("gauchissementM"), 140.0);
	Out.WarpFrequency = Num(TEXT("gauchissementFrequence"), 0.00006);
	Out.SocleHardnessMin = Num(TEXT("socleDureteMin"), 0.25);
	Out.SocleHardnessMax = Num(TEXT("socleDureteMax"), 0.70);
	Out.ErosionContrast = Num(TEXT("contrasteErosion"), 3.0);

	// LA SERIE SE LIT PAR CLE DE ROCHE, JAMAIS PAR IDENTIFIANT. Les
	// identifiants sont des positions dans le catalogue ; les ecrire en dur
	// dans une autre section les ferait diverger au premier ajout de roche --
	// et le depot a deja grave que les identifiants sont des CLES qu'on
	// n'intercale jamais.
	const TArray<TSharedPtr<FJsonValue>>* Tableau = Rules.Array(STR, TEXT("serie"));
	if (Tableau)
	{
		for (const TSharedPtr<FJsonValue>& V : *Tableau)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(Obj) || !Obj) { continue; }

			FString Cle;
			double Epaisseur = 0.0;
			(*Obj)->TryGetStringField(TEXT("roche"), Cle);
			(*Obj)->TryGetNumberField(TEXT("epaisseurM"), Epaisseur);
			if (Cle.IsEmpty() || Epaisseur <= 0.0) { continue; }

			int32 Id = INDEX_NONE;
			for (int32 I = 0; I < Litho.Catalogue.Num(); ++I)
			{
				if (Litho.Catalogue[I].Key == Cle) { Id = I; break; }
			}
			if (Id == INDEX_NONE)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[Worldseed] strates : roche « %s » absente du catalogue, banc ignore"),
					*Cle);
				continue;
			}

			FWorldseedStratBanc B;
			B.RockId = static_cast<uint8>(Id);
			B.ThicknessM = static_cast<float>(Epaisseur);
			B.Hardness = Litho.Catalogue[Id].Hardness;
			Out.Serie.Add(B);
			Out.TotalThicknessM += B.ThicknessM;
		}
	}

	return Out;
}

FWorldseedSapementRules FWorldseedSapementRules::FromRules(const UWorldseedRules& Rules)
{
	FWorldseedSapementRules Out;

	const TCHAR* SAP = TEXT("sapement");
	auto Num = [&Rules, SAP](const TCHAR* Key, double Fallback)
	{
		return static_cast<float>(Rules.Num(SAP, Key, Fallback));
	};

	Out.ReachM = Num(TEXT("reculM"), 220.0);
	Out.FaceFraction = Num(TEXT("facePart"), 0.14);
	Out.SoftnessContrast = Num(TEXT("contrasteTendrete"), 1.6);
	Out.PrecipMaxMm = Num(TEXT("pluieMaxMm"), 420.0);
	Out.FloorMinM = Num(TEXT("fondMinM"), 5.0);
	Out.Strength = Num(TEXT("force"), 1.0);
	return Out;
}

int32 FWorldseedErodibilite::BancAProfondeur(float ProfondeurM) const
{
	// AU-DESSUS DU DATUM, C'EST LE BANC SOMMITAL. Un relief plus haut que la
	// couverture est fait de la roche du sommet de la pile ; rendre le socle y
	// ferait affleurer du granite en altitude, l'inverse de la realite.
	if (ProfondeurM < 0.0f) { return BancK.Num() > 0 ? 0 : INDEX_NONE; }

	for (int32 I = 0; I < BasCumulM.Num(); ++I)
	{
		if (ProfondeurM < BasCumulM[I]) { return I; }
	}
	return INDEX_NONE;   // sous la serie : le socle
}

void FWorldseedErodibilite::Echantillonner(const TArray<float>& DemM,
	TArray<float>& OutK) const
{
	const int32 Count = DemM.Num();
	if (!IsActive() || SocleK.Num() != Count || DatumM.Num() != Count)
	{
		return;
	}
	OutK.SetNumUninitialized(Count);

	for (int32 I = 0; I < Count; ++I)
	{
		if (!Couvert.IsValidIndex(I) || Couvert[I] == 0)
		{
			OutK[I] = SocleK[I];
			continue;
		}
		const int32 B = BancAProfondeur(DatumM[I] - DemM[I]);
		OutK[I] = BancK.IsValidIndex(B) ? BancK[B] : SocleK[I];
	}
}

namespace WorldseedStrata
{
	void PreparerErodibilite(const FWorldseedGeometry& Geometry,
		const FWorldseedLithology& Lithology, const FWorldseedLithologyRules& Litho,
		const FWorldseedStratRules& Strat, float Weight, int32 Seed,
		FWorldseedErodibilite& Out)
	{
		Out = FWorldseedErodibilite();

		const int32 Count = Geometry.CellCount();
		if (Weight <= 0.0f || !Strat.IsActive()
			|| Lithology.Id.Num() != Count || Litho.Catalogue.Num() == 0)
		{
			return;
		}

		// LA MOYENNE SE PREND SUR LE MONDE ET NE BOUGERA PLUS, et c'est la
		// condition pour que le VOLUME total d'erosion reste celui d'avant.
		// Meme formule que WorldseedLithology::Erodibility -- on ne recopie pas
		// une formule dans deux fichiers sans le dire : celle-ci en est la
		// version stratifiee, et les deux doivent bouger ensemble.
		double Somme = 0.0;
		int32 N = 0;
		for (const uint8 Id : Lithology.Id)
		{
			if (Litho.Catalogue.IsValidIndex(Id))
			{
				Somme += Litho.Catalogue[Id].Hardness;
				++N;
			}
		}
		if (N == 0) { return; }
		const float Moyenne = static_cast<float>(Somme / N);

		auto KDe = [Moyenne, Weight](float Durete)
		{
			return FMath::Clamp(1.0f + Weight * (Moyenne - Durete), 0.15f, 2.5f);
		};

		// LA MOYENNE DE LA SERIE, PONDEREE PAR L'EPAISSEUR : c'est autour
		// d'elle qu'on ecarte les bancs, donc sans deplacer leur moyenne, donc
		// sans changer le volume d'erosion sur la couverture.
		double SommeSerie = 0.0;
		double SommeEpaisseur = 0.0;
		for (const FWorldseedStratBanc& B : Strat.Serie)
		{
			SommeSerie += static_cast<double>(B.Hardness) * B.ThicknessM;
			SommeEpaisseur += B.ThicknessM;
		}
		const float MoyenneSerie = (SommeEpaisseur > 0.0)
			? static_cast<float>(SommeSerie / SommeEpaisseur) : Moyenne;

		Out.BancK.Reserve(Strat.Serie.Num());
		Out.BasCumulM.Reserve(Strat.Serie.Num());
		float Cumul = 0.0f;
		for (const FWorldseedStratBanc& B : Strat.Serie)
		{
			Cumul += B.ThicknessM;

			// On ECARTE la durete du banc autour de la moyenne de la serie,
			// puis on applique la formule habituelle. Contraste a 1 : les K
			// d'avant, a l'identique.
			const float Ecartee = MoyenneSerie
				+ (B.Hardness - MoyenneSerie) * Strat.ErosionContrast;
			Out.BancK.Add(KDe(Ecartee));
			Out.BasCumulM.Add(Cumul);
		}

		Out.SocleK.SetNumUninitialized(Count);
		Out.Couvert.SetNumUninitialized(Count);
		Out.DatumM.SetNumUninitialized(Count);

		const double LargeurM = Geometry.WidthM();
		const double HauteurM = Geometry.HeightM;
		const int32 NX = Geometry.NX;
		const int32 NY = Geometry.NY;

		int32 Couverts = 0;
		for (int32 J = 0; J < NY; ++J)
		{
			for (int32 I = 0; I < NX; ++I)
			{
				const int32 C = J * NX + I;
				const uint8 Id = Lithology.Id[C];
				const float Durete = Litho.Catalogue.IsValidIndex(Id)
					? Litho.Catalogue[Id].Hardness : Moyenne;
				Out.SocleK[C] = KDe(Durete);

				const bool bSedimentaire = (Durete >= Strat.SocleHardnessMin)
					&& (Durete <= Strat.SocleHardnessMax);
				Out.Couvert[C] = bSedimentaire ? 1 : 0;
				if (bSedimentaire) { ++Couverts; }

				// LE DATUM SE PAIE UNE SEULE FOIS. C'est une surface
				// GEOLOGIQUE : l'erosion ne la deplace pas. Le precalculer
				// ramene chaque reechantillonnage a quelques comparaisons.
				const double X = (static_cast<double>(I) / NX - 0.5) * LargeurM;
				const double Y = (static_cast<double>(J) / NY - 0.5) * HauteurM;
				Out.DatumM[C] = static_cast<float>(DatumAt(X, Y, Strat, Seed));
			}
		}

		float KMin = 1e9f;
		float KMax = -1e9f;
		for (const float K : Out.BancK)
		{
			KMin = FMath::Min(KMin, K);
			KMax = FMath::Max(KMax, K);
		}

		// SANS CE RELEVE ON NE SAIT PAS SI LES BANCS MORDENT. Un K qui ne varie
		// pas d'un banc a l'autre ne produira aucun gradin, et la difference ne
		// se voit pas sur le relief fini.
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] erodabilite stratifiee : %d bancs, K de %.2f a %.2f ")
			TEXT("(contraste %.2f), serie sur %.1f %% des cellules, durete moyenne %.2f"),
			Out.BancK.Num(), KMin, KMax,
			(KMin > 0.0f) ? KMax / KMin : 0.0f,
			100.0 * Couverts / FMath::Max(Count, 1), Moyenne);
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   serie : durete moyenne %.2f, contraste x%.1f"),
			MoyenneSerie, Strat.ErosionContrast);
	}

	double DatumAt(double X, double Y, const FWorldseedStratRules& Rules, int32 Seed)
	{
		if (Rules.WarpAmplitudeM <= 0.0f)
		{
			return Rules.DatumM;
		}

		// UN GAUCHISSEMENT TRES LENT, ET SEULEMENT LUI. Les bancs doivent
		// rester quasi horizontaux : c'est leur platitude qui fait que deux
		// mesas voisines partagent leur sommet. Le gauchissement n'existe que
		// pour eviter que le monde entier porte ses corniches a la meme
		// altitude sur soixante-quatre kilometres.
		const float B = WorldseedPerlin::Perlin(
			static_cast<float>(X) * Rules.WarpFrequency,
			static_cast<float>(Y) * Rules.WarpFrequency, Seed + 6421);
		return Rules.DatumM + Rules.WarpAmplitudeM * B;
	}

	int32 BancAt(double X, double Y, double ZM,
		const FWorldseedStratRules& Rules, int32 Seed)
	{
		if (!Rules.IsActive()) { return INDEX_NONE; }

		const double Toit = DatumAt(X, Y, Rules, Seed);

		// AU-DESSUS DE LA COUVERTURE, C'EST LE BANC SOMMITAL QUI AFFLEURE. Un
		// relief plus haut que le datum est fait de la roche du sommet de la
		// pile ; rendre INDEX_NONE y ferait apparaitre le socle en altitude,
		// ce qui serait l'inverse de la realite.
		if (ZM >= Toit) { return 0; }

		double Profondeur = Toit - ZM;
		for (int32 I = 0; I < Rules.Serie.Num(); ++I)
		{
			Profondeur -= Rules.Serie[I].ThicknessM;
			if (Profondeur < 0.0) { return I; }
		}

		// Sous la serie : le socle, c'est-a-dire la roche de la carte 2D.
		return INDEX_NONE;
	}

	bool ToitDuChapiteau(double X, double Y, double ZM,
		const FWorldseedStratRules& Rules, float HardnessMin, int32 Seed,
		double& OutToitM)
	{
		if (!Rules.IsActive()) { return false; }

		const double Toit = DatumAt(X, Y, Rules, Seed);

		// On descend la pile en gardant le TOIT de chaque banc, et l'on rend le
		// premier banc dur dont le toit est a ZM ou en dessous. Le sommet d'une
		// mesa est exactement cela : la surface s'est abaissee jusqu'au premier
		// banc capable de la porter, puis s'y est arretee.
		double ToitDuBanc = Toit;
		for (int32 I = 0; I < Rules.Serie.Num(); ++I)
		{
			if (Rules.Serie[I].Hardness >= HardnessMin && ToitDuBanc <= ZM)
			{
				OutToitM = ToitDuBanc;
				return true;
			}
			ToitDuBanc -= Rules.Serie[I].ThicknessM;
		}
		return false;
	}

	void Saper(const FWorldseedGeometry& Geometry,
		const FWorldseedStratRules& Strat, const FWorldseedSapementRules& Rules,
		const FWorldseedLithologyRules& Litho, const FWorldseedLithology& Lithology,
		const TArray<float>& PrecipMm, float CapHardnessMin, int32 Seed,
		TArray<float>& ElevationM)
	{
		const double Debut = FPlatformTime::Seconds();

		const int32 NX = Geometry.NX;
		const int32 NY = Geometry.NY;
		const int32 Count = Geometry.CellCount();
		if (!Rules.IsActive() || !Strat.IsActive() || ElevationM.Num() != Count)
		{
			return;
		}

		const float MailleM = FMath::Max(Geometry.MetersPerPixel(), 1e-3f);
		const double LargeurM = Geometry.WidthM();
		const double HauteurM = Geometry.HeightM;
		const bool bPluie = (PrecipMm.Num() == Count);
		const bool bRoche = Lithology.IsValid(Count);

		// Le datum une seule fois : c'est une surface geologique.
		TArray<float> Datum;
		TArray<uint8> Couvert;
		Datum.SetNumUninitialized(Count);
		Couvert.SetNumUninitialized(Count);
		for (int32 J = 0; J < NY; ++J)
		{
			for (int32 I = 0; I < NX; ++I)
			{
				const int32 C = J * NX + I;
				const double X = (static_cast<double>(I) / NX - 0.5) * LargeurM;
				const double Y = (static_cast<double>(J) / NY - 0.5) * HauteurM;
				Datum[C] = static_cast<float>(DatumAt(X, Y, Strat, Seed));

				bool bOk = (ElevationM[C] > Rules.FloorMinM);
				if (bOk && bPluie && PrecipMm[C] > Rules.PrecipMaxMm) { bOk = false; }
				if (bOk && bRoche)
				{
					const uint8 Id = Lithology.Id[C];
					const float D = Litho.Catalogue.IsValidIndex(Id)
						? Litho.Catalogue[Id].Hardness : 1.0f;
					if (D < Strat.SocleHardnessMin || D > Strat.SocleHardnessMax)
					{
						bOk = false;
					}
				}
				Couvert[C] = bOk ? 1 : 0;
			}
		}

		// Toits et bases de chaque banc, en profondeur sous le datum.
		TArray<float> Toit;
		TArray<float> Base;
		float Cumul = 0.0f;
		for (const FWorldseedStratBanc& B : Strat.Serie)
		{
			Toit.Add(Cumul);
			Cumul += B.ThicknessM;
			Base.Add(Cumul);
		}

		TArray<uint8> Front;
		TArray<float> Distance;
		Front.SetNumUninitialized(Count);

		int32 Touchees = 0;
		int32 Corniches = 0;
		double SommeChute = 0.0;
		float PireChute = 0.0f;

		// --- UNE CORNICHE PAR BANC DUR, DU HAUT VERS LE BAS -----------------
		//
		// L'ORDRE EST GEOLOGIQUE : les bancs superieurs sont decapes d'abord, et
		// le recul de chacun decouvre le suivant. Remonter la pile inverserait
		// la chronologie et ferait reculer une corniche que rien n'a encore
		// mise a nu.
		for (int32 B = 0; B < Strat.Serie.Num(); ++B)
		{
			if (Strat.Serie[B].Hardness < CapHardnessMin) { continue; }
			++Corniches;

			// --- LA BANQUETTE EST AU TOIT DU BANC DUR SUIVANT ---------------
			//
			// ERREUR DE GEOLOGIE PAYEE A LA MESURE. Premiere version : la cible
			// etait la BASE du banc dur, c'est-a-dire le sommet du talus tendre
			// qui le porte. Or ce talus se desagrege entierement -- c'est tout
			// le mecanisme -- et la nouvelle marche est portee par la CORNICHE
			// D'EN DESSOUS. Viser le talus revenait a poser la banquette sur ce
			// qui, precisement, ne tient pas : le controle d'altitude est tombe
			// de 0,957 a 0,842, la surface se retrouvant en moyenne sur des
			// bancs PLUS TENDRES qu'avant.
			int32 Suivant = INDEX_NONE;
			for (int32 K = B + 1; K < Strat.Serie.Num(); ++K)
			{
				if (Strat.Serie[K].Hardness >= CapHardnessMin) { Suivant = K; break; }
			}
			const float ProfBanquette = (Suivant != INDEX_NONE)
				? Toit[Suivant] : Base[B];

			// LE FRONT N'EST PAS LA MER, C'EST LA ZONE DEJA DESCENDUE D'UNE
			// MARCHE : la ou la surface a deja atteint la banquette suivante,
			// le talus est a nu et sape la corniche voisine. La transformee
			// rend la distance a la cellule nulle la plus proche.
			for (int32 C = 0; C < Count; ++C)
			{
				const float Profondeur = Datum[C] - ElevationM[C];
				Front[C] = (Profondeur >= ProfBanquette) ? 0 : 1;
			}
			WorldseedGrid::DistanceTransform(Front, NX, NY, Distance);
			if (Distance.Num() != Count) { continue; }

			// C'EST LE TALUS QUI DECIDE DU RECUL, jamais la corniche : une
			// corniche assise sur de la craie est sapee bien plus vite que la
			// meme assise sur de la dolomie.
			const float DureteDessous = Strat.Serie.IsValidIndex(B + 1)
				? Strat.Serie[B + 1].Hardness : Strat.Serie[B].Hardness;
			const float Recul = FMath::Max(1.0f, Rules.ReachM
				* (1.0f + Rules.SoftnessContrast * (0.5f - DureteDessous)));

			for (int32 C = 0; C < Count; ++C)
			{
				if (Couvert[C] == 0) { continue; }

				const float H = ElevationM[C];
				const float Profondeur = Datum[C] - H;

				// La surface doit etre entre le toit de cette corniche et la
				// banquette suivante : c'est la marche que l'on fait reculer.
				if (Profondeur < Toit[B] || Profondeur >= ProfBanquette) { continue; }

				const float T = Distance[C] * MailleM / Recul;
				if (T >= 1.0f) { continue; }

				// --- LE PROFIL : BANQUETTE, PUIS FACE, PUIS RIEN -------------
				//
				// Meme forme que la passe littorale, et meme raison. La cible
				// vaut la BASE du banc -- la banquette degagee -- puis remonte
				// vers le relief existant sur la largeur de la face, puis s'y
				// confond. La cible ne depassant jamais H, il n'y a ni bosse au
				// raccord ni couture a la limite de recul.
				const float Banquette = Datum[C] - ProfBanquette;
				const float Profil = FMath::SmoothStep(
					1.0f - Rules.FaceFraction, 1.0f, T);
				const float Cible = FMath::Max(Rules.FloorMinM,
					FMath::Lerp(Banquette, H, Profil));
				const float Neuf = FMath::Min(H,
					FMath::Lerp(H, Cible, Rules.Strength));

				if (Neuf < H - 0.01f)
				{
					++Touchees;
					SommeChute += H - Neuf;
					PireChute = FMath::Max(PireChute, H - Neuf);
				}
				ElevationM[C] = Neuf;
			}
		}

		int32 Terres = 0;
		for (int32 C = 0; C < Count; ++C) { if (ElevationM[C] > 0.0f) { ++Terres; } }

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] sapement : %d corniches reculees, %d cellules ")
			TEXT("(%.2f %% des terres), abaissement moyen %.1f m, maximum %.0f m  (%.0f ms)"),
			Corniches, Touchees, 100.0 * Touchees / FMath::Max(Terres, 1),
			(Touchees > 0) ? SommeChute / Touchees : 0.0, PireChute,
			(FPlatformTime::Seconds() - Debut) * 1000.0);
	}
}
