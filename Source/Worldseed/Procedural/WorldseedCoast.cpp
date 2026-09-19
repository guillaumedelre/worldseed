// Worldseed - le recul de falaise, qui donne son relief a la roche tendre.

#include "Procedural/WorldseedCoast.h"

#include "Procedural/WorldseedGrid.h"
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

	// LA TRANSFORMEE DU PROJET, PAS UN CHANFREIN MAISON. Elle est EXACTE et
	// lineaire (Felzenszwalb et Huttenlocher), elle est deja eprouvee, et la
	// regle du depot interdit de recopier une formule dans deux fichiers : la
	// lithologie a besoin de la meme distance a la mer.
	TArray<uint8> Terre;
	Terre.SetNumUninitialized(Count);
	for (int32 I = 0; I < Count; ++I)
	{
		Terre[I] = (ElevationM[I] > 0.0f) ? 1 : 0;
	}
	TArray<float> Distance;
	WorldseedGrid::DistanceTransform(Terre, NX, NY, Distance);

	// --- LA HAUTEUR DU PLATEAU DERRIERE LE FRONT -----------------------------
	//
	// ET C'EST ELLE LA CIBLE, PAS L'ALTITUDE DE LA CELLULE. Premiere version :
	// le profil remontait vers H, l'altitude du point lui-meme. Comme H suit
	// deja la rampe existante, la passe abaissait tout proportionnellement et
	// PRESERVAIT la forme de rampe -- a l'image, une pyramide a facettes au
	// lieu d'une paroi. Verdict sans appel : la falaise faisait quarante
	// degres la ou la face demandee en valait soixante-dix.
	//
	// Le plateau se prend par un maximum glissant : c'est ce que le front
	// AURAIT entame s'il avait recule jusque-la. Le min() final garantit qu'on
	// ne monte jamais le terrain, donc aucune bosse la ou le plateau depasse.
	const float MailleM = FMath::Max(Geometry.MetersPerPixel(), 1e-3f);
	const int32 Rayon = FMath::Clamp(
		FMath::CeilToInt(Rules.ReachM * 1.6f / MailleM), 1, 32);

	TArray<float> Plateau = ElevationM;
	{
		TArray<float> Tampon;
		Tampon.SetNumUninitialized(Count);

		// Separable : deux passes 1D valent une fenetre carree, et le cout
		// suit le rayon au lieu de son carre.
		for (int32 J = 0; J < NY; ++J)
		{
			for (int32 I = 0; I < NX; ++I)
			{
				float M = -1e9f;
				for (int32 D = -Rayon; D <= Rayon; ++D)
				{
					const int32 K = ((I + D) % NX + NX) % NX;
					M = FMath::Max(M, Plateau[J * NX + K]);
				}
				Tampon[J * NX + I] = M;
			}
		}
		for (int32 J = 0; J < NY; ++J)
		{
			for (int32 I = 0; I < NX; ++I)
			{
				float M = -1e9f;
				for (int32 D = -Rayon; D <= Rayon; ++D)
				{
					const int32 L = FMath::Clamp(J + D, 0, NY - 1);
					M = FMath::Max(M, Tampon[L * NX + I]);
				}
				Plateau[J * NX + I] = M;
			}
		}
	}

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

			const float T = Distance[C] * MailleM / Recul;
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
			const float Cible = FMath::Lerp(Rules.PlatformM, Plateau[C], Profil);
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
