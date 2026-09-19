// Worldseed - la colonne stratigraphique : la roche varie aussi en Z.

#include "Procedural/WorldseedStrata.h"

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
}
