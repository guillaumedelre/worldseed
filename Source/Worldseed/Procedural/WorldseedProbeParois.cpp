// Worldseed - sonde des parois : d'ou vient le TERRASSEMENT du relief ?
//
// LIMITE CONNUE DE CETTE SONDE, DITE FRANCHEMENT. Ses deux parts -- pas plats,
// pas francs -- sont MELANGEES sur deux populations : le transect part du
// versant le plus raide mais le quitte, et sa fin court sur du terrain plat ou
// tous les pas sont petits. La part de « presque plats » y monte donc sans
// qu'aucun gradin existe, et ces deux chiffres ne doivent PAS servir a regler
// quoi que ce soit -- c'est exactement l'agregat sur des natures differentes
// que ce depot s'interdit depuis le routage des galeries.
//
// CE QU'ELLE ETABLIT VRAIMENT, ET QUI TIENT : la COMPARAISON entre le relief
// macro et le sol reel, tous deux mesures de la meme facon sur les memes
// transects. Le biais est le meme des deux cotes, donc il s'annule. Releve du
// 20 septembre : macro 26,2 / 11,3, reel 23,3 / 10,5 -- le champ de densite
// n'ajoute rien, ce qu'on voit vient de la chaine 2D. C'est la bifurcation la
// moins chere, et c'est tout ce qu'on lui demande.
//
// Et le PROFIL imprime vaut mieux que les deux parts : au premier transect il
// rend une courbe en S lisse, sans une marche. C'est lui qui a montre que le
// cotele n'etait pas de la geometrie.

#include "Procedural/WorldseedProbeLibrary.h"

#include "Procedural/WorldseedProbeCommun.h"

namespace
{
	/**
	 * La signature d'un ESCALIER, et pourquoi ce n'est pas une pente moyenne.
	 *
	 * Une pente reguliere et un escalier de meme denivele ont la MEME pente
	 * moyenne : la moyenne ne peut donc pas les distinguer, et c'est exactement
	 * le genre d'agregat que ce depot s'interdit. Ce qui les separe est la
	 * DISTRIBUTION des denivelees d'un pas au suivant -- lisse, elles se
	 * ressemblent toutes ; en escalier, elles sont BIMODALES : des marches
	 * presque plates et des contremarches franches.
	 *
	 * On compte donc deux parts, et c'est leur COEXISTENCE qui accuse :
	 *  - les pas presque plats, sous un dixieme de la denivelee moyenne ;
	 *  - les pas francs, au-dela du triple.
	 * Un versant lisse rend peu des deux. Un escalier rend beaucoup des deux.
	 */
	struct FMarches
	{
		int32 Pas = 0;
		int32 Plats = 0;
		int32 Francs = 0;
		double MoyenneM = 0.0;
		double PireM = 0.0;
	};

	FMarches Analyser(const TArray<double>& Profil)
	{
		FMarches R;
		if (Profil.Num() < 3) { return R; }

		TArray<double> Ecarts;
		Ecarts.Reserve(Profil.Num() - 1);
		double Somme = 0.0;
		for (int32 I = 1; I < Profil.Num(); ++I)
		{
			const double D = FMath::Abs(Profil[I] - Profil[I - 1]);
			Ecarts.Add(D);
			Somme += D;
			R.PireM = FMath::Max(R.PireM, D);
		}

		R.Pas = Ecarts.Num();
		R.MoyenneM = Somme / FMath::Max(R.Pas, 1);

		for (const double D : Ecarts)
		{
			if (D < 0.10 * R.MoyenneM) { ++R.Plats; }
			if (D > 3.00 * R.MoyenneM) { ++R.Francs; }
		}
		return R;
	}
}

FString UWorldseedProbeLibrary::ProbeParois(int32 Seed, float HeightMeters,
	int32 ResolutionY, int32 Transects, float LongueurM)
{
	FWorldseedSonde S;
	if (!S.Preparer(Seed, HeightMeters, ResolutionY))
	{
		UE_LOG(LogTemp, Error, TEXT("[Sonde] %s"), *S.Erreur);
		return S.Erreur;
	}

	const FWorldseedGeometry& Geo = S.World.Geometry;
	const double MpP = Geo.MetersPerPixel();

	// --- ON MESURE SUR DES VERSANTS, PAS N'IMPORTE OU ------------------------
	//
	// Un terrassement ne se lit que sur une PENTE : sur un plat, tous les pas
	// sont petits et la part de « presque plats » sature a cent pour cent sans
	// rien vouloir dire. Meme famille de faute que la fenetre qui saturait sur
	// les bancs minces -- une mesure dont la reference derive avec ce qu'on
	// teste ne mesure rien.
	struct FSite { double X; double Y; double Pente; };
	TArray<FSite> Sites;

	for (int32 J = 8; J < Geo.NY - 8; J += 16)
	{
		for (int32 I = 8; I < Geo.NX - 8; I += 16)
		{
			const int32 Idx = J * Geo.NX + I;
			if (S.World.ElevationM[Idx] <= 40.0f) { continue; }

			const double DX = S.World.ElevationM[Idx + 1] - S.World.ElevationM[Idx - 1];
			const double DY = S.World.ElevationM[Idx + Geo.NX] - S.World.ElevationM[Idx - Geo.NX];
			const double Pente = FMath::Sqrt(DX * DX + DY * DY) / (2.0 * MpP);
			if (Pente < 0.45) { continue; }   // environ 24 degres

			Sites.Add({ (static_cast<double>(I) / Geo.NX - 0.5) * Geo.WidthM(),
				(static_cast<double>(J) / Geo.NY - 0.5) * Geo.HeightM, Pente });
		}
	}

	if (Sites.Num() == 0)
	{
		return TEXT("aucun versant assez raide pour mesurer un terrassement");
	}

	Sites.Sort([](const FSite& A, const FSite& B) { return A.Pente > B.Pente; });

	const int32 N = FMath::Clamp(Transects, 1, Sites.Num());
	const double Longueur = FMath::Max(LongueurM, 20.0f);
	const double Pas = 0.5;

	FMarches Macro;
	FMarches Reel;
	int32 Retenus = 0;

	FString Extrait;

	for (int32 K = 0; K < N; ++K)
	{
		const FSite& Si = Sites[K];

		// On descend le versant : la direction est celle du gradient, prise sur
		// le relief macro, et elle suffit -- on ne cherche pas un itineraire,
		// seulement a traverser des courbes de niveau.
		const double H0 = S.Densite.SurfaceHeightM(Si.X, Si.Y);
		const double DX = S.Densite.SurfaceHeightM(Si.X + MpP, Si.Y)
			- S.Densite.SurfaceHeightM(Si.X - MpP, Si.Y);
		const double DY = S.Densite.SurfaceHeightM(Si.X, Si.Y + MpP)
			- S.Densite.SurfaceHeightM(Si.X, Si.Y - MpP);
		const double Norme = FMath::Sqrt(DX * DX + DY * DY);
		if (Norme < 1e-6) { continue; }
		const double UX = -DX / Norme;
		const double UY = -DY / Norme;

		TArray<double> ProfilMacro;
		TArray<double> ProfilReel;

		for (double T = 0.0; T <= Longueur; T += Pas)
		{
			const double X = Si.X + UX * T;
			const double Y = Si.Y + UY * T;

			const double M = S.Densite.SurfaceHeightM(X, Y);
			ProfilMacro.Add(M);

			// LE SOL REEL EST LE PREMIER CHANGEMENT DE SIGNE EN DESCENDANT, et
			// non le relief macro : le champ deplace la surface de plusieurs
			// metres, et partir du macro compte de l'air ordinaire. Le depot a
			// paye cette lecon sur la sonde des diaclases.
			double Reelle = M;
			for (double Z = M + 25.0; Z >= M - 25.0; Z -= 0.25)
			{
				if (S.Densite.At(FVector(X, Y, Z)) <= 0.0) { Reelle = Z; break; }
			}
			ProfilReel.Add(Reelle);
		}

		const FMarches A = Analyser(ProfilMacro);
		const FMarches B = Analyser(ProfilReel);

		Macro.Pas += A.Pas; Macro.Plats += A.Plats; Macro.Francs += A.Francs;
		Macro.MoyenneM += A.MoyenneM; Macro.PireM = FMath::Max(Macro.PireM, A.PireM);
		Reel.Pas += B.Pas; Reel.Plats += B.Plats; Reel.Francs += B.Francs;
		Reel.MoyenneM += B.MoyenneM; Reel.PireM = FMath::Max(Reel.PireM, B.PireM);
		++Retenus;

		if (K == 0)
		{
			// Quarante metres du profil REEL, tous les deux metres : un escalier
			// se voit a l'oeil sur une suite de nombres, et aucun agrege ne
			// remplace la lecture directe du premier cas.
			for (int32 I = 0; I < ProfilReel.Num() && I < 80; I += 4)
			{
				Extrait += FString::Printf(TEXT(" %.1f"), ProfilReel[I]);
			}
		}
	}

	if (Retenus == 0) { return TEXT("aucun transect exploitable"); }

	Macro.MoyenneM /= Retenus;
	Reel.MoyenneM /= Retenus;

	auto Part = [](int32 A, int32 B) { return (B > 0) ? 100.0 * A / B : 0.0; };

	UE_LOG(LogTemp, Log, TEXT("[Sonde] === LE TERRASSEMENT : 2D OU CHAMP ? ==="));
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   %d transects de %.0f m au pas de %.1f m, sur les versants ")
		TEXT("les plus raides (%d candidats)"),
		Retenus, Longueur, Pas, Sites.Num());
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   relief MACRO (grille 2D) : %.1f %% de pas presque plats, ")
		TEXT("%.1f %% de pas francs  |  denivelee moyenne %.3f m, pire %.2f m"),
		Part(Macro.Plats, Macro.Pas), Part(Macro.Francs, Macro.Pas),
		Macro.MoyenneM, Macro.PireM);
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   sol REEL (champ de densite) : %.1f %% de pas presque plats, ")
		TEXT("%.1f %% de pas francs  |  denivelee moyenne %.3f m, pire %.2f m"),
		Part(Reel.Plats, Reel.Pas), Part(Reel.Francs, Reel.Pas),
		Reel.MoyenneM, Reel.PireM);
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   profil reel du premier transect, tous les 2 m :%s"), *Extrait);
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   Lecture : un versant LISSE rend peu de pas plats ET peu de ")
		TEXT("pas francs -- ses denivelees se ressemblent toutes. Un ESCALIER rend ")
		TEXT("beaucoup des DEUX. Si le macro est lisse et le reel en escalier, la ")
		TEXT("cause est dans le champ de densite ; si les deux sont en escalier, ")
		TEXT("elle est dans la chaine 2D, et le voxel ne fait que la rendre."));

	const FString Resume = FString::Printf(
		TEXT("macro %.1f %% plats / %.1f %% francs ; reel %.1f %% plats / %.1f %% francs"),
		Part(Macro.Plats, Macro.Pas), Part(Macro.Francs, Macro.Pas),
		Part(Reel.Plats, Reel.Pas), Part(Reel.Francs, Reel.Pas));

	UE_LOG(LogTemp, Log, TEXT("[Sonde] %s"), *Resume);
	return Resume;
}
