#include "Procedural/WorldseedDiffusion.h"

#include "Procedural/WorldseedDensity.h"
#include "Procedural/WorldseedTrace.h"

namespace
{
	/** Les six faces d'un cube, dans l'ordre du masque de transition. */
	const FVector NormalesDeFace[6] =
	{
		FVector(-1, 0, 0), FVector(1, 0, 0),
		FVector(0, -1, 0), FVector(0, 1, 0),
		FVector(0, 0, -1), FVector(0, 0, 1),
	};
}

void FWorldseedDiffusion::Regler(const FWorldseedDiffusionRegles& Nouvelles,
	TSharedPtr<const FWorldseedDensity, ESPMode::ThreadSafe> NouveauChamp)
{
	R = Nouvelles;
	Champ = MoveTemp(NouveauChamp);
	Oublier();
}

void FWorldseedDiffusion::Oublier()
{
	CacheSurface.Reset();
	FeuillesCourantes.Reset();
	EquilibrageAjouts = 0;
	Ecarts2a1 = 0;
}

FBox FWorldseedDiffusion::BoiteM(const FWorldseedChunkKey& Key) const
{
	const double Side = CoteM(Key.Niveau);
	const FVector Min(Key.C.X * Side, Key.C.Y * Side, Key.C.Z * Side);
	return FBox(Min, Min + FVector(Side, Side, Side));
}

int32 FWorldseedDiffusion::NiveauEmis(const FVector& PointM) const
{
	// ON LIT L'ENSEMBLE EMIS, ON NE LE REDEDUIT PAS. Tant que la partition
	// etait une fonction du POINT, une descente ponctuelle pouvait la
	// reproduire ; l'equilibrage regarde les VOISINS, donc le niveau d'une
	// feuille depend de ses voisines et ne se recalcule plus. Une source de
	// verite, pas deux calculs qu'on espere d'accord.
	for (int32 N = FMath::Max(R.NiveauMax, 0); N >= 0; --N)
	{
		const double Cote = CoteM(N);
		const FWorldseedChunkKey Cle{
			FIntVector(
				FMath::FloorToInt(PointM.X / Cote),
				FMath::FloorToInt(PointM.Y / Cote),
				FMath::FloorToInt(PointM.Z / Cote)),
			N };
		if (FeuillesCourantes.Contains(Cle))
		{
			return N;
		}
	}
	return INDEX_NONE;
}

int32 FWorldseedDiffusion::NiveauEstime(const FVector& PointM,
	const FVector& OrigineM) const
{
	const int32 Emis = NiveauEmis(PointM);
	if (Emis != INDEX_NONE)
	{
		return Emis;
	}

	// Hors de l'ensemble emis -- un point pas encore diffuse, ou hors du rayon
	// -- on retombe sur ce que les anneaux SEULS donneraient. C'est une
	// estimation, et elle est nommee ainsi.
	int32 Niveau = FMath::Max(R.NiveauMax, 0);
	while (Niveau > 0 && FVector::Dist(PointM, OrigineM) < RayonAnneauM(Niveau - 1))
	{
		--Niveau;
	}
	return Niveau;
}

uint8 FWorldseedDiffusion::MasqueDe(const FWorldseedChunkKey& Key) const
{
	// Un chunk de niveau 0 n'a aucun voisin plus fin : rien a raccorder.
	if (Key.Niveau <= 0) { return 0; }

	const double Cote = CoteM(Key.Niveau);
	const FVector Centre = BoiteM(Key).GetCenter();

	uint8 Masque = 0;
	for (int32 F = 0; F < 6; ++F)
	{
		const FVector Voisin = Centre + NormalesDeFace[F] * Cote;
		const int32 NiveauVoisin = NiveauEmis(Voisin);

		// LA CELLULE DE TRANSITION VIT DANS LE BLOC GROSSIER, le long de sa
		// frontiere avec le fin : c'est lui qui a trop peu d'echantillons --
		// neuf valeurs fines arrivent sur une face qui n'en porte que quatre --
		// donc c'est a lui de ceder la place.
		if (NiveauVoisin != INDEX_NONE && NiveauVoisin < Key.Niveau)
		{
			Masque |= static_cast<uint8>(1 << F);
		}
	}
	return Masque;
}

void FWorldseedDiffusion::PlageSurface(int32 CX, int32 CY, int32 Niveau,
	float& OutMinM, float& OutMaxM) const
{
	const FIntVector Cle(CX, CY, Niveau);
	if (const FVector2D* Deja = CacheSurface.Find(Cle))
	{
		OutMinM = static_cast<float>(Deja->X);
		OutMaxM = static_cast<float>(Deja->Y);
		return;
	}

	if (!Champ.IsValid())
	{
		OutMinM = 0.0f;
		OutMaxM = 0.0f;
		return;
	}

	const double Cote = CoteM(Niveau);
	Champ->SurfaceRangeM(CX * Cote, CY * Cote,
		(CX + 1) * Cote, (CY + 1) * Cote, OutMinM, OutMaxM);

	// LE CACHE SE VIDE PLUTOT QUE DE GONFLER SANS FIN. Un joueur qui traverse
	// le monde finirait par l'emplir ; le repartir de zero coute une passe de
	// recalcul et rien d'autre, puisque la donnee est deterministe.
	if (CacheSurface.Num() > 200000)
	{
		CacheSurface.Reset();
	}
	CacheSurface.Add(Cle, FVector2D(OutMinM, OutMaxM));
}

bool FWorldseedDiffusion::DoitSubdiviser(const FWorldseedChunkKey& Key,
	const FVector& OrigineM) const
{
	if (Key.Niveau <= 0)
	{
		return false;
	}

	// LA DISTANCE, ET ELLE SEULE : un terrain lointain ne merite pas de
	// finesse. Un second critere a existe ici -- la densite de maillage suivant
	// le relief -- et il a ete RETIRE le 23 septembre 2026 : livre eteint, il
	// CASSAIT la contrainte 2:1, son seuil etant une pente, donc divise par
	// deux a chaque niveau.
	// LA DISTANCE PEUT ETRE ANISOTROPE, et c'est le seul endroit ou elle l'est.
	//
	// Le chargement et le tri des candidats gardent la distance VRAIE : ils
	// decident de ce qui existe, pas de sa finesse. Seul le NIVEAU se laisse
	// ponderer -- voir `FWorldseedDiffusionRegles::PoidsZ` pour pourquoi le
	// critere d'origine est angulairement juste, et pourquoi le vol le met
	// quand meme en defaut.
	const FVector Centre = BoiteM(Key).GetCenter();
	FVector Ecart = Centre - OrigineM;
	Ecart.Z *= R.PoidsZ;
	if (Ecart.Size() >= RayonAnneauM(Key.Niveau - 1))
	{
		return false;
	}

	return true;
}

void FWorldseedDiffusion::Enumerer(const FWorldseedChunkKey& Key,
	const FVector& OrigineM,
	TArray<TPair<FWorldseedChunkKey, double>>& Sortie,
	bool bEmissionForcee) const
{
	const FBox Boite = BoiteM(Key);

	// ON COUPE SUR LA BOITE, PAS SUR LE CENTRE. Un noeud grossier dont le
	// centre est hors du rayon peut avoir un coin dedans : le couper sur son
	// centre laisserait un trou que rien ne signale.
	if (!bEmissionForcee
		&& Boite.ComputeSquaredDistanceToPoint(OrigineM) >
			static_cast<double>(R.LoadRadiusM) * R.LoadRadiusM)
	{
		return;
	}

	// Les deux tiers du volume sont soit du plein, soit de l'air : la plage
	// d'altitude du relief les ecarte avant toute descente.
	float SurfaceMin = 0.0f;
	float SurfaceMax = 0.0f;
	PlageSurface(Key.C.X, Key.C.Y, Key.Niveau, SurfaceMin, SurfaceMax);
	if (Boite.Min.Z > SurfaceMax ||
		Boite.Max.Z < SurfaceMin - R.BandDepthM)
	{
		return;
	}

	const FVector Centre = Boite.GetCenter();
	const double Dist = FVector::Dist(Centre, OrigineM);

	// SOIT ON DESCEND, SOIT ON MAILLE -- jamais les deux. C'est ce qui fait de
	// la sortie une partition.
	if (DoitSubdiviser(Key, OrigineM))
	{
		for (int32 I = 0; I < 8; ++I)
		{
			const FWorldseedChunkKey Enfant{
				FIntVector(
					Key.C.X * 2 + (I & 1),
					Key.C.Y * 2 + ((I >> 1) & 1),
					Key.C.Z * 2 + ((I >> 2) & 1)),
				Key.Niveau - 1 };

			// Les enfants ne heritent PAS du forcage : un noeud force doit
			// donner ses huit enfants, mais ceux-la se coupent normalement.
			Enumerer(Enfant, OrigineM, Sortie, false);
		}
		return;
	}

	if (!bEmissionForcee && Dist > R.LoadRadiusM)
	{
		return;
	}
	Sortie.Emplace(Key, Dist);
}

void FWorldseedDiffusion::Equilibrer(
	TArray<TPair<FWorldseedChunkKey, double>>& Feuilles,
	const FVector& OrigineM)
{
	WORLDSEED_TRACE(Equilibrage);

	FeuillesCourantes.Reset();
	FeuillesCourantes.Reserve(Feuilles.Num());
	for (const TPair<FWorldseedChunkKey, double>& F : Feuilles)
	{
		FeuillesCourantes.Add(F.Key);
	}

	if (R.NiveauMax <= 0)
	{
		// Resolution uniforme : il ne peut y avoir aucun ecart.
		EquilibrageAjouts = 0;
		Ecarts2a1 = 0;
		return;
	}

	// ON SONDE DEPUIS LE COTE FIN, ET C'EST COMPLET PAR CONSTRUCTION. Sonder
	// depuis la feuille GROSSIERE ne l'est pas : la face d'un chunk de niveau 3
	// touche jusqu'a SOIXANTE-QUATRE chunks de niveau 0, et un point par face
	// n'en voit qu'un -- mesure du defaut, 87 violations sur 512 feuilles en
	// regime etabli. Le point juste au-dela d'une face fine tombe forcement
	// DANS la feuille qui couvre cette position, puisqu'un voisin plus
	// grossier est plus GRAND que le point sonde. On marque alors le VOISIN.
	int32 Ajoutees = 0;
	int32 Restants = 0;
	for (int32 Tour = 0; Tour <= FMath::Max(R.NiveauMax, 1); ++Tour)
	{
		TSet<FWorldseedChunkKey> ASubdiviser;
		for (const FWorldseedChunkKey& Cle : FeuillesCourantes)
		{
			const double Cote = CoteM(Cle.Niveau);
			const FVector Centre = BoiteM(Cle).GetCenter();
			for (const FVector& D : NormalesDeFace)
			{
				const FVector P = Centre + D * Cote;
				const int32 NV = NiveauEmis(P);
				if (NV != INDEX_NONE && NV > Cle.Niveau + 1)
				{
					const double CoteV = CoteM(NV);
					ASubdiviser.Add(FWorldseedChunkKey{
						FIntVector(
							FMath::FloorToInt(P.X / CoteV),
							FMath::FloorToInt(P.Y / CoteV),
							FMath::FloorToInt(P.Z / CoteV)),
						NV });
				}
			}
		}

		Restants = ASubdiviser.Num();
		if (Restants == 0)
		{
			break;
		}

		for (const FWorldseedChunkKey& Cle : ASubdiviser)
		{
			FeuillesCourantes.Remove(Cle);

			TArray<TPair<FWorldseedChunkKey, double>> Enfants;
			for (int32 I = 0; I < 8; ++I)
			{
				const FWorldseedChunkKey Enfant{
					FIntVector(
						Cle.C.X * 2 + (I & 1),
						Cle.C.Y * 2 + ((I >> 1) & 1),
						Cle.C.Z * 2 + ((I >> 2) & 1)),
					Cle.Niveau - 1 };
				Enumerer(Enfant, OrigineM, Enfants, true);
			}
			for (const TPair<FWorldseedChunkKey, double>& E : Enfants)
			{
				FeuillesCourantes.Add(E.Key);
				Feuilles.Add(E);
				++Ajoutees;
			}
		}

		// Le tableau doit suivre l'ensemble : un noeud subdivise n'est plus une
		// feuille, et le laisser dedans le ferait mailler par-dessus ses
		// propres enfants.
		Feuilles.RemoveAll([this](const TPair<FWorldseedChunkKey, double>& F)
		{
			return !FeuillesCourantes.Contains(F.Key);
		});
	}

	EquilibrageAjouts = Ajoutees;
	Ecarts2a1 = Restants;
}
