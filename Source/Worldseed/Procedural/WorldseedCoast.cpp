// Worldseed - le recul de falaise, qui donne son relief a la roche tendre.

#include "Procedural/WorldseedCoast.h"

#include "Procedural/WorldseedPerlin.h"
#include "Procedural/WorldseedRules.h"

FWorldseedCoastRules FWorldseedCoastRules::FromRules(const UWorldseedRules& Rules)
{
	FWorldseedCoastRules Out;

	const TCHAR* LIT = TEXT("littoral");
	auto Num = [&Rules, LIT](const TCHAR* Key, double Fallback)
	{
		return static_cast<float>(Rules.Num(LIT, Key, Fallback));
	};

	Out.ReachM = Num(TEXT("reculM"), 260.0);
	Out.PlatformFraction = Num(TEXT("plateformePart"), 0.35);
	Out.FaceFraction = Num(TEXT("facePart"), 0.15);
	Out.PlatformM = Num(TEXT("plateformeAltitudeM"), 3.0);
	Out.Strength = Num(TEXT("force"), 0.9);
	Out.RockContrast = Num(TEXT("contrasteRoche"), 0.8);
	Out.NoiseFrequency = Num(TEXT("bruitFrequence"), 0.0012);
	Out.NoiseAmount = Num(TEXT("bruitPart"), 0.45);
	return Out;
}

namespace
{
	/**
	 * Distance a la mer, en metres, par double balayage de chanfrein.
	 *
	 * DEUX PASSES SUFFISENT, ET C'EST LA RAISON DE CE CHOIX. Une transformee
	 * exacte demanderait un parcours par file ; le chanfrein approche la
	 * distance euclidienne a quelques pour cent pres en O(N), ce qui est
	 * largement assez pour decider ou mord une falaise. La longitude
	 * S'ENROULE -- le monde est une sphere deroulee -- donc les balayages
	 * traitent le bord est et le bord ouest comme voisins.
	 */
	void DistanceALaMer(const TArray<float>& ElevationM, int32 NX, int32 NY,
		float MetresParCellule, TArray<float>& Out)
	{
		const float Grand = 1e9f;
		const float Droit = MetresParCellule;
		const float Diagonal = MetresParCellule * 1.41421356f;

		Out.SetNumUninitialized(ElevationM.Num());
		for (int32 I = 0; I < ElevationM.Num(); ++I)
		{
			Out[I] = (ElevationM[I] <= 0.0f) ? 0.0f : Grand;
		}

		auto Enroule = [NX](int32 I) { return ((I % NX) + NX) % NX; };

		for (int32 J = 0; J < NY; ++J)
		{
			for (int32 I = 0; I < NX; ++I)
			{
				const int32 C = J * NX + I;
				if (Out[C] == 0.0f) { continue; }
				float D = Out[C];
				if (J > 0)
				{
					D = FMath::Min(D, Out[(J - 1) * NX + I] + Droit);
					D = FMath::Min(D, Out[(J - 1) * NX + Enroule(I - 1)] + Diagonal);
					D = FMath::Min(D, Out[(J - 1) * NX + Enroule(I + 1)] + Diagonal);
				}
				D = FMath::Min(D, Out[J * NX + Enroule(I - 1)] + Droit);
				Out[C] = D;
			}
		}

		for (int32 J = NY - 1; J >= 0; --J)
		{
			for (int32 I = NX - 1; I >= 0; --I)
			{
				const int32 C = J * NX + I;
				if (Out[C] == 0.0f) { continue; }
				float D = Out[C];
				if (J < NY - 1)
				{
					D = FMath::Min(D, Out[(J + 1) * NX + I] + Droit);
					D = FMath::Min(D, Out[(J + 1) * NX + Enroule(I - 1)] + Diagonal);
					D = FMath::Min(D, Out[(J + 1) * NX + Enroule(I + 1)] + Diagonal);
				}
				D = FMath::Min(D, Out[J * NX + Enroule(I + 1)] + Droit);
				Out[C] = D;
			}
		}
	}
}

void WorldseedCoast::Build(const FWorldseedGeometry& Geometry,
	const FWorldseedLithology& Lithology,
	const FWorldseedLithologyRules& LithoRules,
	const FWorldseedCoastRules& Rules, int32 Seed,
	TArray<float>& ElevationM)
{
	const double Debut = FPlatformTime::Seconds();

	const int32 NX = Geometry.NX;
	const int32 NY = Geometry.NY;
	const int32 Count = Geometry.CellCount();
	if (!Rules.IsActive() || ElevationM.Num() != Count)
	{
		return;
	}

	TArray<float> Distance;
	DistanceALaMer(ElevationM, NX, NY,
		FMath::Max(Geometry.MetersPerPixel(), 1e-3f), Distance);

	const bool bRoche = Lithology.IsValid(Count);
	const double LargeurM = Geometry.WidthM();
	const double HauteurM = Geometry.HeightM;

	int32 Touchees = 0;
	double SommeChute = 0.0;
	float PireChute = 0.0f;

	for (int32 J = 0; J < NY; ++J)
	{
		for (int32 I = 0; I < NX; ++I)
		{
			const int32 C = J * NX + I;
			const float H = ElevationM[C];
			if (H <= 0.0f) { continue; }

			// --- DE COMBIEN LE FRONT A-T-IL RECULE ICI ? ---------------------
			//
			// La roche tendre recule plus, et c'est tout l'objet de la passe :
			// c'est ce qui fait qu'Etretat, dans de la craie, porte des
			// falaises de quatre-vingts metres la ou un granite en porte de
			// vingt.
			float Recul = Rules.ReachM;
			if (bRoche)
			{
				const uint8 R = Lithology.Id[C];
				const float Durete = LithoRules.Catalogue.IsValidIndex(R)
					? LithoRules.Catalogue[R].Hardness : 1.0f;
				Recul *= 1.0f + Rules.RockContrast * (0.5f - Durete);
			}

			// Un trait de cote regulier se voit : le recul varie le long de la
			// cote, ce qui creuse des anses et laisse des caps -- et ce sont
			// justement les caps qui portent les arches.
			const double X = (static_cast<double>(I) / NX - 0.5) * LargeurM;
			const double Y = (static_cast<double>(J) / NY - 0.5) * HauteurM;
			const float Bruit = WorldseedPerlin::Perlin(
				static_cast<float>(X) * Rules.NoiseFrequency,
				static_cast<float>(Y) * Rules.NoiseFrequency, Seed + 9161);
			Recul *= 1.0f + Rules.NoiseAmount * Bruit;
			if (Recul <= 1.0f) { continue; }

			const float T = Distance[C] / Recul;
			if (T >= 1.0f) { continue; }

			// --- LE PROFIL : PLATEFORME, PUIS FACE, PUIS RIEN ----------------
			//
			// ON NE FAIT QUE BAISSER. La cible vaut la plateforme au pied, puis
			// remonte vers le relief EXISTANT sur la largeur de la face, puis
			// se confond avec lui. Comme la cible ne depasse jamais H, il n'y a
			// ni bosse au raccord ni couture a la limite de portee -- et la
			// part emergee ne bouge pas, la plateforme restant au-dessus de
			// zero.
			const float Profil = FMath::SmoothStep(Rules.PlatformFraction,
				Rules.PlatformFraction + Rules.FaceFraction, T);
			const float Cible = FMath::Lerp(Rules.PlatformM, H, Profil);
			const float Neuf = FMath::Min(H, FMath::Lerp(H, Cible, Rules.Strength));

			if (Neuf < H - 0.01f)
			{
				++Touchees;
				SommeChute += H - Neuf;
				PireChute = FMath::Max(PireChute, H - Neuf);
			}
			ElevationM[C] = Neuf;
		}
	}

	// SANS CE RELEVE ON NE SAIT PAS SI LA PASSE A MORDU. Une falaise qui ne se
	// forme pas et une passe qui ne tourne pas se ressemblent parfaitement vues
	// du relief fini.
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] littoral : %d cellules remodelees (%.1f %% des terres), ")
		TEXT("abaissement moyen %.1f m, maximum %.0f m  (%.0f ms)"),
		Touchees, 100.0 * Touchees / FMath::Max(Count, 1),
		(Touchees > 0) ? SommeChute / Touchees : 0.0, PireChute,
		(FPlatformTime::Seconds() - Debut) * 1000.0);
}
