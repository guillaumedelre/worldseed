// Worldseed - sonde des puits : doline et aven ont-ils le profil qu'on leur prete ?

#include "Procedural/WorldseedProbeLibrary.h"

#include "Procedural/WorldseedProbeCommun.h"

#include "Procedural/WorldseedCaves.h"

namespace
{
	/**
	 * Demi-largeur de l'air a une altitude donnee, mesuree sur le CHAMP REEL.
	 *
	 * ON NE LIT PAS LE RAYON DEMANDE, ON MESURE CELUI QU'ON A. Un puits est
	 * creuse par une chaine de capsules dont l'union est LISSE : le rayon
	 * effectif n'est donc pas celui du parametre, et il est encore modifie par
	 * tout ce qui passe la -- galeries, bruit a cretes, diaclases. Relire le
	 * parametre reviendrait a verifier qu'on a bien ecrit ce qu'on a ecrit.
	 *
	 * On marche vers l'exterieur sur quatre rayons et l'on prend la moyenne :
	 * un seul rayon tomberait parfois dans l'ondulation de l'axe, qui deplace
	 * le puits de quelques metres.
	 */
	double DemiLargeur(const FWorldseedDensity& Champ,
		const FWorldseedCaveLocal& Local, const FVector& CentreM, double Z,
		double Portee)
	{
		static const FVector2D Directions[4] =
		{
			FVector2D(1, 0), FVector2D(-1, 0), FVector2D(0, 1), FVector2D(0, -1)
		};

		double Somme = 0.0;
		for (const FVector2D& D : Directions)
		{
			double R = 0.0;
			while (R < Portee)
			{
				const FVector P(CentreM.X + D.X * (R + 0.5),
					CentreM.Y + D.Y * (R + 0.5), Z);
				if (Champ.At(P, &Local) <= 0.0) { break; }
				R += 0.5;
			}
			Somme += R;
		}
		return Somme * 0.25;
	}
}

FString UWorldseedProbeLibrary::ProbePuits(int32 Seed, float HeightMeters,
	int32 ResolutionY)
{
	FWorldseedSonde S;
	if (!S.Preparer(Seed, HeightMeters, ResolutionY))
	{
		UE_LOG(LogTemp, Error, TEXT("[Sonde] %s"), *S.Erreur);
		return S.Erreur;
	}

	const FWorldseedCaveNetwork& Reseau = S.World.Caves;
	if (Reseau.Puits.Num() == 0)
	{
		return TEXT("aucun puits dans ce monde");
	}

	struct FBilan
	{
		int32 Total = 0;
		int32 Ouverts = 0;
		int32 ProfilJuste = 0;
		double SommeHaut = 0.0;
		double SommeBas = 0.0;
	};
	FBilan Dolines;
	FBilan Avens;

	int32 Montres = 0;

	for (const FWorldseedCavePuits& P : Reseau.Puits)
	{
		FBilan& B = P.bDoline ? Dolines : Avens;
		++B.Total;

		// LE RESEAU DOIT ETRE DONNE AU CHAMP, SANS QUOI IL REND LA ROCHE PLEINE
		// et la sonde mesure le monde d'AVANT le percement. Le depot a paye ce
		// piege sur les arches -- "0 traversante" quelles que soient les arches
		// -- et il ne se signale pas : le chiffre reste plausible.
		FWorldseedCaveLocal Local;
		const double Marge = FMath::Max<double>(P.RayonHautM, P.RayonBasM) + 40.0;
		Reseau.Query(FBox(
			FVector(P.CentreM.X - Marge, P.CentreM.Y - Marge, P.BasM - 20.0),
			FVector(P.CentreM.X + Marge, P.CentreM.Y + Marge, P.SolM + 20.0)),
			Local);

		// --- 1. OUVERT AU CIEL ----------------------------------------------
		//
		// Un puits qui ne perce pas la surface n'est pas un puits, c'est une
		// poche. On descend depuis deux metres SOUS le terrain -- au-dessus,
		// c'est de l'air par construction et cela ne prouverait rien -- et l'on
		// exige de l'air jusqu'a une dizaine de metres plus bas.
		bool bOuvert = true;
		for (double Z = P.SolM - 2.0; Z >= P.SolM - 12.0; Z -= 1.0)
		{
			if (S.Densite.At(FVector(P.CentreM.X, P.CentreM.Y, Z), &Local) <= 0.0)
			{
				bOuvert = false;
				break;
			}
		}
		if (bOuvert) { ++B.Ouverts; }

		// --- 2. LE SENS DU PROFIL -------------------------------------------
		//
		// C'est LA distinction entre les deux formes, et la seule. On mesure
		// donc pres du haut et pres du bas, loin des deux extremites ou le
		// raccord lisse arrondit tout.
		const double Portee = FMath::Max<double>(P.RayonHautM, P.RayonBasM) + 30.0;
		const double ZHaut = P.SolM - 4.0;
		const double ZBas = FMath::Min<double>(P.BasM + 4.0, ZHaut - 4.0);

		const double LHaut = DemiLargeur(S.Densite, Local, P.CentreM, ZHaut, Portee);
		const double LBas = DemiLargeur(S.Densite, Local, P.CentreM, ZBas, Portee);

		B.SommeHaut += LHaut;
		B.SommeBas += LBas;

		// UNE DOLINE S'EVASE VERS LE HAUT, UN AVEN VERS LE BAS. On exige un
		// ecart franc : a un metre pres, deux formes egales ne se distinguent
		// pas et le joueur ne verrait qu'un cylindre.
		const bool bJuste = P.bDoline ? (LHaut > LBas + 1.0) : (LBas > LHaut + 1.0);
		if (bJuste) { ++B.ProfilJuste; }

		if (Montres < 12)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Sonde]   %-7s (%7.0f, %7.0f) sol %4.0f m  |  ")
				TEXT("demi-largeur haut %5.1f m, bas %5.1f m  |  %s, %s"),
				P.bDoline ? TEXT("doline") : TEXT("aven"),
				P.CentreM.X, P.CentreM.Y, P.SolM, LHaut, LBas,
				bOuvert ? TEXT("ouvert") : TEXT("FERME"),
				bJuste ? TEXT("profil juste") : TEXT("PROFIL A L'ENVERS"));
			++Montres;
		}
	}

	auto Ligne = [](const TCHAR* Nom, const FBilan& B, const TCHAR* Attendu)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Sonde]   %-8s %5d poses  |  %4d ouverts au ciel (%.0f %%)  |  ")
			TEXT("%4d au profil juste (%.0f %%)  |  demi-largeur moyenne ")
			TEXT("haut %.1f m, bas %.1f m  (%s)"),
			Nom, B.Total,
			B.Ouverts, B.Total > 0 ? 100.0 * B.Ouverts / B.Total : 0.0,
			B.ProfilJuste, B.Total > 0 ? 100.0 * B.ProfilJuste / B.Total : 0.0,
			B.Total > 0 ? B.SommeHaut / B.Total : 0.0,
			B.Total > 0 ? B.SommeBas / B.Total : 0.0,
			Attendu);
	};

	UE_LOG(LogTemp, Log, TEXT("[Sonde] === PUITS : DOLINES ET AVENS ==="));
	Ligne(TEXT("dolines"), Dolines, TEXT("entonnoir : le haut doit etre le plus large"));
	Ligne(TEXT("avens"), Avens, TEXT("cloche : le bas doit etre le plus large"));

	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   Lecture : « ouvert au ciel » se mesure SOUS le terrain -- ")
		TEXT("au-dessus c'est de l'air par construction et cela ne prouverait ")
		TEXT("rien. Et la demi-largeur est celle du CHAMP, pas le rayon demande : ")
		TEXT("l'union lisse et tout ce qui passe la modifient le second."));

	const FString Resume = FString::Printf(
		TEXT("%d dolines (%d ouvertes, %d en entonnoir), %d avens (%d ouverts, ")
		TEXT("%d en cloche)"),
		Dolines.Total, Dolines.Ouverts, Dolines.ProfilJuste,
		Avens.Total, Avens.Ouverts, Avens.ProfilJuste);

	UE_LOG(LogTemp, Log, TEXT("[Sonde] %s"), *Resume);
	return Resume;
}
