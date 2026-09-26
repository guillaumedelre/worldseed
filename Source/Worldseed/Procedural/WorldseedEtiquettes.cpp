// Worldseed - ou poser les noms de pays et de region sur une carte.

#include "Procedural/WorldseedEtiquettes.h"

#include "Procedural/WorldseedRegions.h"

namespace WorldseedEtiquettes
{
	TArray<FEtiquette> Choisir(const FWorldseedRegions& Regions,
		const WorldseedCarte::FParamsFenetre& Vue, double LargeurMondeM,
		const FReglages& R)
	{
		TArray<FEtiquette> Sortie;
		if (!Regions.EstValide() || Vue.ResX <= 0 || Vue.ResY <= 0)
		{
			return Sortie;
		}

		// METRES PAR PIXEL SUR L'AXE DES X. La fenetre est decrite par une
		// demi-portee et une resolution ; les deux axes partagent la meme
		// echelle par construction de `PeindreFenetre`.
		const double MetresParPixel =
			(Vue.DemiPorteeXm * 2.0) / FMath::Max(1, Vue.ResX);
		if (MetresParPixel <= 0.0)
		{
			return Sortie;
		}

		// LE DIAMETRE APPARENT, DERIVE DE L'AIRE. Une region n'est pas un
		// disque, mais la racine de son aire donne l'ordre de grandeur de sa
		// largeur -- et c'est bien un ordre de grandeur qu'on compare a la
		// longueur d'un nom, pas une mesure.
		auto DiametrePx = [MetresParPixel](float AireKm2) -> double
		{
			return FMath::Sqrt(FMath::Max(0.0f, AireKm2)) * 1000.0 / MetresParPixel;
		};

		auto Ajouter = [&](const FVector2D& AncrageM, const FString& Nom,
			bool bPays, double Diametre)
		{
			if (Nom.IsEmpty())
			{
				return;
			}

			double PX = 0.0;
			double PY = 0.0;

			// `PixelDuMetre` rend FAUX hors de la fenetre, et il le fait en
			// ayant quand meme ecrit la position -- un marqueur hors champ a
			// encore une direction. Ici on veut la garde : un nom dont
			// l'ancrage est hors cadre n'a rien a dessiner.
			if (!WorldseedCarte::PixelDuMetre(Vue, LargeurMondeM,
				AncrageM.X, AncrageM.Y, PX, PY))
			{
				return;
			}

			FEtiquette E;
			E.PositionPx = FVector2D(PX, PY);
			E.Texte = Nom;
			E.bPays = bPays;
			E.DiametrePx = Diametre;
			Sortie.Add(MoveTemp(E));
		};

		for (const FWorldseedPays& P : Regions.Pays)
		{
			const double D = DiametrePx(P.AireKm2);
			if (D >= R.DiametreMinPaysPx)
			{
				Ajouter(P.AncrageM, P.Nom, true, D);
			}
		}

		for (const FWorldseedRegion& Reg : Regions.Regions)
		{
			const double D = DiametrePx(Reg.AireKm2);
			if (D >= R.DiametreMinPx)
			{
				Ajouter(Reg.AncrageM, Reg.Nom, false, D);
			}
		}

		// LES PAYS D'ABORD, PUIS LES GRANDES FORMES. L'appelant pose dans cet
		// ordre et rejette ce qui recouvre : ce tri EST la regle de priorite,
		// il n'y en a pas d'autre ailleurs.
		Sortie.Sort([](const FEtiquette& A, const FEtiquette& B)
		{
			if (A.bPays != B.bPays)
			{
				return A.bPays;
			}
			return A.DiametrePx > B.DiametrePx;
		});

		if (Sortie.Num() > R.NombreMax)
		{
			Sortie.SetNum(R.NombreMax);
		}
		return Sortie;
	}

	bool Accepter(TArray<FBox2D>& Occupes, const FBox2D& Boite)
	{
		for (const FBox2D& Pose : Occupes)
		{
			if (Pose.Intersect(Boite))
			{
				return false;
			}
		}
		Occupes.Add(Boite);
		return true;
	}
}
