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

namespace WorldseedStrata
{
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
