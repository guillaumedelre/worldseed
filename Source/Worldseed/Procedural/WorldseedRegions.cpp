// Worldseed - le decoupage du monde en regions geographiques et en pays.

#include "Procedural/WorldseedRegions.h"

#include "Procedural/WorldseedFlow.h"
#include "Procedural/WorldseedNoms.h"
#include "Procedural/WorldseedTrace.h"

#include "Math/RandomStream.h"

namespace
{
	/**
	 * ⚠ LE NOM DE SECTION VIT ICI, UNE SEULE FOIS.
	 *
	 * Ce depot a paye TROIS fois le piege du build unifie : UBT concatene les
	 * `.cpp` en une seule unite de traduction, et deux namespaces ANONYMES n'en
	 * font alors qu'un. `WorldseedMetersToCm` puis `SUB` ont ainsi casse la
	 * compilation de fichiers auxquels personne n'avait touche.
	 */
	const TCHAR* REG = TEXT("regions");

	/** Cle canonique d'une paire, pour que (A,B) et (B,A) se confondent. */
	uint64 ClePaire(int32 A, int32 B)
	{
		const uint32 Min = static_cast<uint32>(FMath::Min(A, B));
		const uint32 Max = static_cast<uint32>(FMath::Max(A, B));
		return (static_cast<uint64>(Min) << 32) | Max;
	}

	/**
	 * UNION-FIND avec compression de chemin.
	 *
	 * L'agglomeration fusionne des milliers de bassins ; refaire l'etiquetage
	 * a chaque fusion serait quadratique. On fusionne des CLASSES, et on ne
	 * reecrit la grille qu'une fois, a la fin.
	 */
	struct FUnion
	{
		TArray<int32> Parent;

		void Init(int32 N)
		{
			Parent.SetNumUninitialized(N);
			for (int32 I = 0; I < N; ++I) { Parent[I] = I; }
		}

		int32 Racine(int32 X)
		{
			while (Parent[X] != X)
			{
				Parent[X] = Parent[Parent[X]];
				X = Parent[X];
			}
			return X;
		}

		void Unir(int32 Absorbe, int32 Vers)
		{
			Parent[Racine(Absorbe)] = Racine(Vers);
		}
	};

	/**
	 * Sous-echantillonne un champ de la grille de simulation vers celle des
	 * regions, par MOYENNE de bloc.
	 *
	 * ⚠ JAMAIS POUR UN IDENTIFIANT. Moyenner un identifiant de biome donnerait
	 * un biome qui n'existe pas la -- ce depot a deja paye 307 points faux sur
	 * 17 956 pour cette faute exacte, par un filtre bilineaire de PCG. Les
	 * identifiants passent par `VoteMajoritaire`.
	 */
	void MoyenneDeBloc(const TArray<float>& Source, int32 SrcNX, int32 SrcNY,
		int32 Facteur, int32 DstNX, int32 DstNY, TArray<float>& Out)
	{
		Out.Init(0.0f, DstNX * DstNY);
		if (Source.Num() != SrcNX * SrcNY)
		{
			return;
		}

		for (int32 JD = 0; JD < DstNY; ++JD)
		{
			for (int32 ID = 0; ID < DstNX; ++ID)
			{
				double Somme = 0.0;
				int32 Compte = 0;
				for (int32 DJ = 0; DJ < Facteur; ++DJ)
				{
					const int32 JS = JD * Facteur + DJ;
					if (JS >= SrcNY) { break; }
					for (int32 DI = 0; DI < Facteur; ++DI)
					{
						const int32 IS = ID * Facteur + DI;
						if (IS >= SrcNX) { break; }
						Somme += Source[JS * SrcNX + IS];
						++Compte;
					}
				}
				Out[JD * DstNX + ID] = (Compte > 0)
					? static_cast<float>(Somme / Compte) : 0.0f;
			}
		}
	}

	/** Le plus frequent d'un bloc. Pour les identifiants, qu'on ne moyenne pas. */
	void VoteMajoritaire(const TArray<uint8>& Source, int32 SrcNX, int32 SrcNY,
		int32 Facteur, int32 DstNX, int32 DstNY, TArray<uint8>& Out)
	{
		Out.Init(0, DstNX * DstNY);
		if (Source.Num() != SrcNX * SrcNY)
		{
			return;
		}

		for (int32 JD = 0; JD < DstNY; ++JD)
		{
			for (int32 ID = 0; ID < DstNX; ++ID)
			{
				int32 Compte[256] = {};
				for (int32 DJ = 0; DJ < Facteur; ++DJ)
				{
					const int32 JS = JD * Facteur + DJ;
					if (JS >= SrcNY) { break; }
					for (int32 DI = 0; DI < Facteur; ++DI)
					{
						const int32 IS = ID * Facteur + DI;
						if (IS >= SrcNX) { break; }
						++Compte[Source[JS * SrcNX + IS]];
					}
				}
				int32 Meilleur = 0;
				for (int32 V = 1; V < 256; ++V)
				{
					if (Compte[V] > Compte[Meilleur]) { Meilleur = V; }
				}
				Out[JD * DstNX + ID] = static_cast<uint8>(Meilleur);
			}
		}
	}

	/**
	 * REETIQUETTE PAR COMPOSANTE CONNEXE. Rend le nombre de composantes.
	 *
	 * ⚠ CE N'EST PAS UN RAFFINEMENT, C'EST UNE CORRECTION NECESSAIRE. La coupe
	 * d'une region en hautes et basses terres se fait par un SEUIL D'ALTITUDE,
	 * et rien n'assure que les cellules au-dessus du seuil se touchent : deux
	 * sommets separes par un col en dessous de la mediane donnent une « region »
	 * en deux morceaux disjoints, qui porterait UN nom et UNE etiquette pour
	 * deux endroits sans rapport. Le defaut ne leve rien -- la partition reste
	 * valide, les aires restent justes -- et il se verrait seulement a l'image,
	 * sous la forme d'une frontiere qui saute par-dessus une vallee.
	 *
	 * LA LONGITUDE S'ENROULE, comme partout : une region a cheval sur le
	 * meridien de bordure doit rester d'un seul tenant.
	 */
	int32 ComposantesConnexes(TArray<int32>& Etiquettes, int32 NX, int32 NY)
	{
		const int32 Count = NX * NY;
		TArray<int32> Sortie;
		Sortie.Init(INDEX_NONE, Count);

		TArray<int32> Pile;
		int32 Nb = 0;

		for (int32 Depart = 0; Depart < Count; ++Depart)
		{
			if (Etiquettes[Depart] < 0 || Sortie[Depart] >= 0)
			{
				continue;
			}

			const int32 Source = Etiquettes[Depart];
			const int32 Id = Nb++;

			Pile.Reset();
			Pile.Add(Depart);
			Sortie[Depart] = Id;

			while (Pile.Num() > 0)
			{
				const int32 C = Pile.Pop(EAllowShrinking::No);
				const int32 I = C % NX;
				const int32 J = C / NX;

				const int32 Voisins4[4] = {
					J * NX + ((I + 1) % NX),
					J * NX + ((I + NX - 1) % NX),
					(J + 1 < NY) ? (J + 1) * NX + I : INDEX_NONE,
					(J > 0) ? (J - 1) * NX + I : INDEX_NONE
				};

				for (const int32 V : Voisins4)
				{
					if (V >= 0 && Sortie[V] < 0 && Etiquettes[V] == Source)
					{
						Sortie[V] = Id;
						Pile.Add(V);
					}
				}
			}
		}

		Etiquettes = MoveTemp(Sortie);
		return Nb;
	}

	/**
	 * ABSORBE LES MORCEAUX SOUS `AireMin` dans leur voisin de plus LONGUE
	 * FRONTIERE, puis renumerote de facon compacte. Rend le nombre restant.
	 *
	 * LE CRITERE DE FRONTIERE COMPTE PLUS QU'IL N'Y PARAIT : fusionner vers le
	 * voisin le plus GRAND donnerait des regions en etoile qui enjambent des
	 * cretes, alors que la plus longue frontiere commune designe precisement
	 * celui avec lequel le morceau forme deja un ensemble.
	 *
	 * ON FUSIONNE DES CLASSES, PAS DES CELLULES : reetiqueter la grille a
	 * chaque fusion serait quadratique. Elle n'est reecrite qu'une fois.
	 *
	 * ⚠ TOUT ORDRE EST TOTAL, et ce n'est pas du zele : l'ordre de parcours
	 * d'un `TSet` ou d'un `TMap` n'est pas garanti stable, si bien qu'une
	 * egalite tranchee au hasard ferait deux mondes differents pour une meme
	 * graine -- l'invariant que tout ce depot defend.
	 */
	int32 Agglomerer(TArray<int32>& Etiquettes, int32 Nb, int32 NX, int32 NY,
		int32 AireMin)
	{
		if (Nb <= 0)
		{
			return 0;
		}
		const int32 Count = NX * NY;

		TArray<int32> Aire;
		Aire.Init(0, Nb);
		for (int32 C = 0; C < Count; ++C)
		{
			if (Etiquettes[C] >= 0) { ++Aire[Etiquettes[C]]; }
		}

		TMap<uint64, int32> Frontieres;
		TArray<TSet<int32>> Voisins;
		Voisins.SetNum(Nb);

		auto Relier = [&](int32 A, int32 B)
		{
			if (A < 0 || B < 0 || A == B) { return; }
			Frontieres.FindOrAdd(ClePaire(A, B)) += 1;
			Voisins[A].Add(B);
			Voisins[B].Add(A);
		};

		for (int32 J = 0; J < NY; ++J)
		{
			for (int32 I = 0; I < NX; ++I)
			{
				const int32 C = J * NX + I;
				if (Etiquettes[C] < 0) { continue; }
				Relier(Etiquettes[C], Etiquettes[J * NX + ((I + 1) % NX)]);
				if (J + 1 < NY)
				{
					Relier(Etiquettes[C], Etiquettes[(J + 1) * NX + I]);
				}
			}
		}

		FUnion Classes;
		Classes.Init(Nb);
		TArray<int32> AireClasse = Aire;
		TArray<uint8> Vivant;
		Vivant.Init(1, Nb);

		// Aire croissante, puis identifiant : un ordre TOTAL.
		TArray<int32> ParAire;
		ParAire.Reserve(Nb);
		for (int32 I = 0; I < Nb; ++I) { ParAire.Add(I); }
		ParAire.Sort([&Aire](int32 A, int32 B)
		{
			return (Aire[A] != Aire[B]) ? (Aire[A] < Aire[B]) : (A < B);
		});

		for (const int32 Candidat : ParAire)
		{
			const int32 R = Classes.Racine(Candidat);
			if (!Vivant[R] || AireClasse[R] >= AireMin)
			{
				continue;
			}

			int32 Meilleur = INDEX_NONE;
			int32 MeilleureLongueur = -1;
			for (const int32 V : Voisins[R])
			{
				const int32 RV = Classes.Racine(V);
				if (RV == R || !Vivant[RV]) { continue; }
				const int32* L = Frontieres.Find(ClePaire(R, RV));
				const int32 Longueur = L ? *L : 0;
				if (Longueur > MeilleureLongueur
					|| (Longueur == MeilleureLongueur && RV < Meilleur))
				{
					MeilleureLongueur = Longueur;
					Meilleur = RV;
				}
			}

			// UNE ILE SOUS LE SEUIL N'A AUCUN VOISIN TERRESTRE, donc aucune
			// frontiere par ou l'absorber. On la laisse ici et on la traite
			// APRES, par proximite -- voir la passe des orphelines.
			if (Meilleur == INDEX_NONE)
			{
				continue;
			}

			for (const int32 V : Voisins[R])
			{
				const int32 RV = Classes.Racine(V);
				if (RV == R || RV == Meilleur || !Vivant[RV]) { continue; }
				if (const int32* L = Frontieres.Find(ClePaire(R, RV)))
				{
					Frontieres.FindOrAdd(ClePaire(Meilleur, RV)) += *L;
				}
				Voisins[Meilleur].Add(RV);
				Voisins[RV].Add(Meilleur);
			}

			AireClasse[Meilleur] += AireClasse[R];
			Vivant[R] = 0;
			Classes.Unir(R, Meilleur);
		}

		// --- LES ORPHELINES : LES ILES, RATTACHEES PAR PROXIMITE -------------
		//
		// ⚠ SANS CETTE PASSE, LA MOITIE DES REGIONS SONT DES MIETTES. Mesure
		// du 26 septembre 2026, avant correction : 16 regions dont la MEDIANE
		// d'aire valait 0,0 km2 -- c'est-a-dire une cellule -- et 14 pays pour
		// ces 16 regions, chaque ilot formant le sien puisqu'il n'a aucun
		// voisin avec qui s'agreger. La partition etait pourtant parfaite : 0
		// trou, 0 debordement. C'est exactement le genre de defaut qu'un
		// oracle de partition ne peut pas voir, et qu'un quantile montre d'un
		// coup.
		//
		// UN ILOT D'UNE CELLULE N'EST PAS UNE REGION. Il n'a pas de frontiere
		// terrestre, donc rien a quoi l'agglomeration puisse l'accrocher --
		// mais il a une POSITION, et une petite ile au large appartient a la
		// cote qu'elle borde. C'est ainsi que la geographie reelle range les
		// archipels.
		{
			// Centroides, en cellules. La longitude s'enroule, donc on moyenne
			// l'ANGLE : la moyenne de la colonne 2 et de la colonne NX-2 vaut
			// le milieu du monde, ce qui serait l'oppose de la verite.
			TArray<double> SCos, SSin, SY;
			TArray<int32> N;
			SCos.Init(0.0, Nb); SSin.Init(0.0, Nb); SY.Init(0.0, Nb);
			N.Init(0, Nb);

			for (int32 J = 0; J < NY; ++J)
			{
				for (int32 I = 0; I < NX; ++I)
				{
					const int32 E = Etiquettes[J * NX + I];
					if (E < 0) { continue; }
					const int32 R = Classes.Racine(E);
					const double A = (I + 0.5) / NX * 2.0 * PI;
					SCos[R] += FMath::Cos(A);
					SSin[R] += FMath::Sin(A);
					SY[R] += J + 0.5;
					++N[R];
				}
			}

			// Les hotes possibles : les classes vivantes qui tiennent le seuil.
			TArray<int32> Hotes;
			for (int32 R = 0; R < Nb; ++R)
			{
				if (Vivant[R] && Classes.Racine(R) == R && AireClasse[R] >= AireMin)
				{
					Hotes.Add(R);
				}
			}

			// S'IL N'Y EN A AUCUN, ON NE TOUCHE A RIEN. Un monde d'archipels
			// dont aucune ile n'atteint le seuil garde ses iles : mieux vaut
			// des petites regions qu'une seule region eparpillee sur tout
			// l'ocean.
			if (Hotes.Num() > 0)
			{
				for (int32 R = 0; R < Nb; ++R)
				{
					if (!Vivant[R] || Classes.Racine(R) != R
						|| AireClasse[R] >= AireMin || N[R] == 0)
					{
						continue;
					}

					const double AR = FMath::Atan2(SSin[R] / N[R], SCos[R] / N[R]);
					const double YR = SY[R] / N[R];

					int32 Proche = INDEX_NONE;
					double Meilleure = TNumericLimits<double>::Max();
					for (const int32 H : Hotes)
					{
						if (N[H] == 0) { continue; }
						const double AH = FMath::Atan2(SSin[H] / N[H], SCos[H] / N[H]);

						// L'ECART D'ANGLE SE REPLIE SUR [-pi, pi], sans quoi
						// deux points de part et d'autre du meridien de bordure
						// seraient declares aux antipodes.
						double DA = AH - AR;
						while (DA > PI) { DA -= 2.0 * PI; }
						while (DA < -PI) { DA += 2.0 * PI; }

						const double DX = DA / (2.0 * PI) * NX;
						const double DY = (SY[H] / N[H]) - YR;
						const double D2 = DX * DX + DY * DY;

						// A EGALITE, LE PLUS PETIT IDENTIFIANT : ordre total.
						if (D2 < Meilleure || (D2 == Meilleure && H < Proche))
						{
							Meilleure = D2;
							Proche = H;
						}
					}

					if (Proche != INDEX_NONE)
					{
						AireClasse[Proche] += AireClasse[R];
						Vivant[R] = 0;
						Classes.Unir(R, Proche);
					}
				}
			}
		}

		// Renumerotation compacte, dans l'ordre des classes survivantes.
		TArray<int32> Nouveau;
		Nouveau.Init(INDEX_NONE, Nb);
		int32 Restant = 0;
		for (int32 I = 0; I < Nb; ++I)
		{
			const int32 R = Classes.Racine(I);
			if (Nouveau[R] == INDEX_NONE)
			{
				Nouveau[R] = Restant++;
			}
			Nouveau[I] = Nouveau[R];
		}
		for (int32 C = 0; C < Count; ++C)
		{
			if (Etiquettes[C] >= 0) { Etiquettes[C] = Nouveau[Etiquettes[C]]; }
		}
		return Restant;
	}
}

int32 FWorldseedRegions::RegionEnUV(double U, double V) const
{
	if (!EstValide())
	{
		return INDEX_NONE;
	}

	// LA LONGITUDE S'ENROULE, LA LATITUDE SE BORNE -- la meme convention que
	// le drainage et la peinture. Un pole n'a pas de voisin au-dela.
	U -= FMath::FloorToDouble(U);
	V = FMath::Clamp(V, 0.0, 1.0);

	// TRONCATURE, JAMAIS ARRONDI : la cellule k couvre [k, k+1[, donc son
	// centre est en k + 0,5 et c'est `Floor` qui designe le bon centre. Ce
	// depot a deja paye une demi-cellule d'ecart -- huit metres -- pour avoir
	// employe `RoundToInt` a cet endroit, et le biome NOMME n'etait alors pas
	// le biome PEINT sous les pieds.
	const int32 I = FMath::Clamp(static_cast<int32>(U * NX), 0, NX - 1);
	const int32 J = FMath::Clamp(static_cast<int32>(V * NY), 0, NY - 1);

	const int16 R = Id[J * NX + I];
	return (R >= 0 && Regions.IsValidIndex(R)) ? R : INDEX_NONE;
}

int32 FWorldseedRegions::RegionEn(double XM, double YM) const
{
	if (LargeurM <= 0.0f || HauteurM <= 0.0f)
	{
		return INDEX_NONE;
	}
	return RegionEnUV(XM / LargeurM + 0.5, YM / HauteurM + 0.5);
}

int32 FWorldseedRegions::PaysEn(double XM, double YM) const
{
	const int32 R = RegionEn(XM, YM);
	return Regions.IsValidIndex(R) ? Regions[R].Pays : INDEX_NONE;
}

FString FWorldseedRegions::NomEn(double XM, double YM) const
{
	const int32 R = RegionEn(XM, YM);
	return Regions.IsValidIndex(R) ? Regions[R].Nom : FString();
}

FWorldseedRegionRules FWorldseedRegionRules::FromRules(const UWorldseedRules& Rules)
{
	FWorldseedRegionRules R;
	R.Facteur = FMath::Clamp(
		static_cast<int32>(Rules.Num(REG, TEXT("facteur"), R.Facteur)), 1, 64);
	R.AireMinKm2 = static_cast<float>(
		Rules.Num(REG, TEXT("aireMinKm2"), R.AireMinKm2));
	R.AireMaxKm2 = static_cast<float>(
		Rules.Num(REG, TEXT("aireMaxKm2"), R.AireMaxKm2));
	R.AirePaysKm2 = static_cast<float>(
		Rules.Num(REG, TEXT("airePaysKm2"), R.AirePaysKm2));
	R.MassifAltitudeM = static_cast<float>(
		Rules.Num(REG, TEXT("massifAltitudeM"), R.MassifAltitudeM));
	R.PolaireTempC = static_cast<float>(
		Rules.Num(REG, TEXT("polaireTempC"), R.PolaireTempC));
	R.ArideePluieMm = static_cast<float>(
		Rules.Num(REG, TEXT("aridePluieMm"), R.ArideePluieMm));
	R.ForetPluieMm = static_cast<float>(
		Rules.Num(REG, TEXT("foretPluieMm"), R.ForetPluieMm));
	R.LittoralPart = static_cast<float>(
		Rules.Num(REG, TEXT("littoralPart"), R.LittoralPart));
	return R;
}

const TCHAR* WorldseedRegions::NomDuCaractere(EWorldseedRegionCaractere C)
{
	switch (C)
	{
	case EWorldseedRegionCaractere::Polaire:     return TEXT("polaire");
	case EWorldseedRegionCaractere::Massif:      return TEXT("massif");
	case EWorldseedRegionCaractere::Aride:       return TEXT("aride");
	case EWorldseedRegionCaractere::ForetHumide: return TEXT("foret humide");
	case EWorldseedRegionCaractere::Littoral:    return TEXT("littoral");
	default:                                     return TEXT("plaine");
	}
}

void WorldseedRegions::Construire(const FWorldseedGeometry& Geometry,
	const TArray<float>& ElevationM, const TArray<float>& TempC,
	const TArray<float>& PrecipMm, const TArray<uint8>& BiomeId,
	const FWorldseedRegionRules& Rules, int32 Seed, FWorldseedRegions& Out)
{
	WORLDSEED_TRACE(Regions);

	Out = FWorldseedRegions();

	const int32 SrcNX = Geometry.NX;
	const int32 SrcNY = Geometry.NY;
	if (SrcNX < 2 || SrcNY < 2 || ElevationM.Num() != SrcNX * SrcNY)
	{
		return;
	}

	const int32 F = FMath::Max(1, Rules.Facteur);
	const int32 NX = FMath::Max(2, SrcNX / F);
	const int32 NY = FMath::Max(2, SrcNY / F);
	const int32 Count = NX * NY;

	Out.NX = NX;
	Out.NY = NY;
	Out.Facteur = F;
	Out.LargeurM = Geometry.WidthM();
	Out.HauteurM = Geometry.HeightM;

	// Aire d'une cellule de region, en kilometres carres. La projection est
	// equivalente-aire : toutes les cellules valent la meme surface.
	const double AireCelluleKm2 =
		(static_cast<double>(Out.LargeurM) / NX / 1000.0)
		* (static_cast<double>(Out.HauteurM) / NY / 1000.0);

	// --- 1. le relief et les champs, ramenes a la grille des regions --------
	TArray<float> Relief, Temp, Pluie;
	TArray<uint8> Biome;
	MoyenneDeBloc(ElevationM, SrcNX, SrcNY, F, NX, NY, Relief);
	MoyenneDeBloc(TempC, SrcNX, SrcNY, F, NX, NY, Temp);
	MoyenneDeBloc(PrecipMm, SrcNX, SrcNY, F, NX, NY, Pluie);
	VoteMajoritaire(BiomeId, SrcNX, SrcNY, F, NX, NY, Biome);

	TArray<uint8> EstTerre;
	EstTerre.SetNumUninitialized(Count);
	int32 NbTerre = 0;
	for (int32 I = 0; I < Count; ++I)
	{
		EstTerre[I] = (Relief[I] > 0.0f) ? 1 : 0;
		NbTerre += EstTerre[I];
	}
	if (NbTerre == 0)
	{
		Out.Id.Init(-1, Count);
		return;
	}

	// --- 2. le drainage, sur la grille des regions --------------------------
	//
	// UN SECOND PRIORITY-FLOOD, ET IL NE COUTE RIEN. Celui des champs du sol
	// tourne sur 8,4 millions de cellules pour 1,4 s ; celui-ci sur 524 288,
	// soit seize fois moins. Reutiliser le premier obligerait a remonter un
	// etiquetage de 8,4 M de cellules pour le redescendre ensuite -- plus cher,
	// et pour une precision dont une frontiere n'a que faire.
	FWorldseedFlow Flux;
	WorldseedFlow::Compute(Relief, TArray<float>(), NX, NY, 0.0f, 1e-4f, Flux);
	if (Flux.Receivers.Num() != Count)
	{
		Out.Id.Init(-1, Count);
		return;
	}

	// --- 3. etiqueter par exutoire ------------------------------------------
	//
	// `Order` range l'amont AVANT l'aval. On le parcourt donc A L'ENVERS : le
	// receveur d'une cellule est en aval, donc deja etiquete quand on arrive.
	// Une seule passe, sans recursion ni pile.
	TArray<int32> Brut;
	Brut.Init(INDEX_NONE, Count);

	int32 NbBruts = 0;
	for (int32 K = Flux.Order.Num() - 1; K >= 0; --K)
	{
		const int32 C = Flux.Order[K];
		if (!EstTerre[C])
		{
			continue;
		}
		const int32 R = Flux.Receivers[C];

		// Exutoire : soit la cellule se recoit elle-meme, soit elle se deverse
		// en mer. Les deux ouvrent un bassin.
		if (R == C || !EstTerre[R])
		{
			Brut[C] = NbBruts++;
		}
		else
		{
			Brut[C] = Brut[R];
		}
	}

	// UNE CELLULE DE TERRE SANS ETIQUETTE serait un trou dans la partition.
	// Elle ne peut venir que d'une cellule absente de `Order` ; on lui ouvre
	// son propre bassin plutot que de la laisser a -1.
	for (int32 C = 0; C < Count; ++C)
	{
		if (EstTerre[C] && Brut[C] == INDEX_NONE)
		{
			Brut[C] = NbBruts++;
		}
	}

	if (NbBruts == 0)
	{
		Out.Id.Init(-1, Count);
		return;
	}

	// --- 4. aires et adjacence ----------------------------------------------
	TArray<int32> Aire;
	Aire.Init(0, NbBruts);
	for (int32 C = 0; C < Count; ++C)
	{
		if (Brut[C] >= 0) { ++Aire[Brut[C]]; }
	}

	// LA LONGITUDE S'ENROULE : la colonne 0 touche la derniere. Sans cela, une
	// region a cheval sur le meridien de bordure se couperait en deux, et la
	// frontiere artificielle se verrait sur la carte.
	TMap<uint64, int32> Frontieres;
	TArray<TSet<int32>> Voisins;
	Voisins.SetNum(NbBruts);

	auto Relier = [&](int32 A, int32 B)
	{
		if (A < 0 || B < 0 || A == B) { return; }
		Frontieres.FindOrAdd(ClePaire(A, B)) += 1;
		Voisins[A].Add(B);
		Voisins[B].Add(A);
	};

	for (int32 J = 0; J < NY; ++J)
	{
		for (int32 I = 0; I < NX; ++I)
		{
			const int32 C = J * NX + I;
			if (Brut[C] < 0) { continue; }
			Relier(Brut[C], Brut[J * NX + ((I + 1) % NX)]);
			if (J + 1 < NY) { Relier(Brut[C], Brut[(J + 1) * NX + I]); }
		}
	}

	// --- 5. absorber les miettes --------------------------------------------
	//
	// Des milliers de micro-bassins naissent de l'etiquetage : chaque cellule
	// cotiere qui se deverse directement en mer ouvre le sien. Ce ne sont pas
	// des regions, c'est la poussiere du relief.
	const int32 AireMin = FMath::Max(1,
		FMath::RoundToInt(Rules.AireMinKm2 / FMath::Max(AireCelluleKm2, 1e-9)));

	int32 NbRegions = Agglomerer(Brut, NbBruts, NX, NY, AireMin);

	// --- 6. recouper les regions trop grandes, par l'altitude ---------------
	//
	// UN GRAND BASSIN N'EST PAS UNE REGION : le Rhone n'est pas un pays. On le
	// coupe en HAUTES et BASSES terres, ce qui est le partage le plus lisible
	// sur une carte en relief -- et le plus vrai, une vallee alpine et son
	// delta n'ont ni le meme climat, ni la meme vegetation, ni les memes
	// hommes.
	//
	// LA COUPE SE FAIT A LA MEDIANE de la region, jamais a un seuil absolu :
	// un seuil fixe couperait tout un continent bas au meme endroit, et ne
	// couperait rien du tout dans un massif.
	const int32 AireMax = FMath::Max(AireMin * 2,
		FMath::RoundToInt(Rules.AireMaxKm2 / FMath::Max(AireCelluleKm2, 1e-9)));

	{
		TArray<TArray<int32>> ParRegion;
		ParRegion.SetNum(NbRegions);
		for (int32 C = 0; C < Count; ++C)
		{
			if (Brut[C] >= 0) { ParRegion[Brut[C]].Add(C); }
		}

		int32 Prochain = NbRegions;
		for (int32 R = 0; R < NbRegions; ++R)
		{
			const TArray<int32>& Cellules = ParRegion[R];
			if (Cellules.Num() <= AireMax) { continue; }

			TArray<float> H;
			H.Reserve(Cellules.Num());
			for (const int32 C : Cellules) { H.Add(Relief[C]); }
			H.Sort();
			const float Seuil = H[H.Num() / 2];

			const int32 IdHaut = Prochain++;
			for (const int32 C : Cellules)
			{
				if (Relief[C] > Seuil) { Brut[C] = IdHaut; }
			}
		}
		NbRegions = Prochain;
	}

	// LA COUPE PEUT AVOIR ECLATE UNE REGION, et il faut le reparer ici.
	//
	// Le seuil d'altitude ne connait pas la geographie : deux sommets separes
	// par un col sous la mediane tombent tous deux dans les « hautes terres »
	// sans se toucher. On obtiendrait une region en deux morceaux disjoints,
	// portant UN nom pour deux endroits sans rapport -- et rien ne le
	// signalerait, la partition restant parfaitement valide. On reetiquette
	// donc par composante connexe, puis on repasse l'agglomeration pour que
	// les eclats trop petits rejoignent un voisin.
	NbRegions = ComposantesConnexes(Brut, NX, NY);
	NbRegions = Agglomerer(Brut, NbRegions, NX, NY, AireMin);

	const TArray<int32>& Final = Brut;
	const int32 NbFinal = NbRegions;

	// --- 7. mesurer chaque region -------------------------------------------
	Out.Id.Init(-1, Count);
	Out.Regions.SetNum(NbFinal);
	for (int32 R = 0; R < NbFinal; ++R)
	{
		Out.Regions[R].Id = R;
	}

	TArray<int32> Cellules;
	Cellules.Init(0, NbFinal);
	TArray<int32> Cotieres;
	Cotieres.Init(0, NbFinal);
	TArray<double> SommeH, SommeT, SommeP;
	SommeH.Init(0.0, NbFinal);
	SommeT.Init(0.0, NbFinal);
	SommeP.Init(0.0, NbFinal);

	// LE CENTRE SE MOYENNE EN ANGLE, PAS EN COORDONNEE. Une region a cheval sur
	// le meridien de bordure aurait sinon son centre a l'oppose du monde -- la
	// moyenne de -31 km et +31 km vaut zero. On moyenne le vecteur unitaire de
	// la longitude et l'on revient par `Atan2`, ce qui est la seule facon juste
	// de moyenner un angle.
	TArray<double> SommeCos, SommeSin, SommeY;
	SommeCos.Init(0.0, NbFinal);
	SommeSin.Init(0.0, NbFinal);
	SommeY.Init(0.0, NbFinal);

	TArray<TArray<int32>> Biomes;
	Biomes.SetNum(NbFinal);
	for (TArray<int32>& B : Biomes) { B.Init(0, 256); }

	for (int32 J = 0; J < NY; ++J)
	{
		for (int32 I = 0; I < NX; ++I)
		{
			const int32 C = J * NX + I;
			const int32 R = Final[C];
			if (R < 0) { continue; }

			Out.Id[C] = static_cast<int16>(R);
			++Cellules[R];
			SommeH[R] += Relief[C];
			SommeT[R] += Temp.IsValidIndex(C) ? Temp[C] : 0.0f;
			SommeP[R] += Pluie.IsValidIndex(C) ? Pluie[C] : 0.0f;
			++Biomes[R][Biome.IsValidIndex(C) ? Biome[C] : 0];

			const double Angle = (static_cast<double>(I) + 0.5) / NX * 2.0 * PI;
			SommeCos[R] += FMath::Cos(Angle);
			SommeSin[R] += FMath::Sin(Angle);
			SommeY[R] += (static_cast<double>(J) + 0.5) / NY - 0.5;

			// Cotiere : au moins un des quatre voisins est en mer.
			const bool bCote =
				!EstTerre[J * NX + ((I + 1) % NX)]
				|| !EstTerre[J * NX + ((I + NX - 1) % NX)]
				|| (J + 1 < NY && !EstTerre[(J + 1) * NX + I])
				|| (J > 0 && !EstTerre[(J - 1) * NX + I]);
			Cotieres[R] += bCote ? 1 : 0;
		}
	}

	for (int32 R = 0; R < NbFinal; ++R)
	{
		FWorldseedRegion& Reg = Out.Regions[R];
		const int32 N = FMath::Max(1, Cellules[R]);

		Reg.AireKm2 = static_cast<float>(Cellules[R] * AireCelluleKm2);
		Reg.AltitudeMoyenneM = static_cast<float>(SommeH[R] / N);
		Reg.TemperatureMoyenneC = static_cast<float>(SommeT[R] / N);
		Reg.PluieMoyenneMm = static_cast<float>(SommeP[R] / N);
		Reg.PartLittorale = static_cast<float>(Cotieres[R]) / N;

		const double Angle = FMath::Atan2(SommeSin[R] / N, SommeCos[R] / N);
		const double U = (Angle < 0.0 ? Angle + 2.0 * PI : Angle) / (2.0 * PI);
		Reg.CentreM = FVector2D(
			(U - 0.5) * Out.LargeurM,
			(SommeY[R] / N) * Out.HauteurM);

		int32 Meilleur = 0;
		for (int32 B = 1; B < 256; ++B)
		{
			if (Biomes[R][B] > Biomes[R][Meilleur]) { Meilleur = B; }
		}
		Reg.BiomeDominant = static_cast<uint8>(Meilleur);

		// --- le caractere, lu sur les moyennes ------------------------------
		//
		// L'ORDRE DES TESTS EST LE CRITERE, et il va du plus contraignant au
		// plus general. Une region polaire de montagne est POLAIRE : c'est le
		// froid qui decide de tout ce qui y pousse et de qui y vit, pas
		// l'altitude. Un desert d'altitude est ARIDE pour la meme raison.
		if (Reg.TemperatureMoyenneC < Rules.PolaireTempC)
		{
			Reg.Caractere = EWorldseedRegionCaractere::Polaire;
		}
		else if (Reg.PluieMoyenneMm < Rules.ArideePluieMm)
		{
			Reg.Caractere = EWorldseedRegionCaractere::Aride;
		}
		else if (Reg.AltitudeMoyenneM > Rules.MassifAltitudeM)
		{
			Reg.Caractere = EWorldseedRegionCaractere::Massif;
		}
		else if (Reg.PluieMoyenneMm > Rules.ForetPluieMm
			&& Reg.TemperatureMoyenneC > 12.0f)
		{
			Reg.Caractere = EWorldseedRegionCaractere::ForetHumide;
		}
		else if (Reg.PartLittorale > Rules.LittoralPart)
		{
			Reg.Caractere = EWorldseedRegionCaractere::Littoral;
		}
		else
		{
			Reg.Caractere = EWorldseedRegionCaractere::Plaine;
		}
	}

	// --- 8. grouper en pays --------------------------------------------------
	//
	// GLOUTON SUR L'ADJACENCE, EN PARTANT DES PLUS GRANDES. Un pays s'agrege
	// autour d'un coeur ; partir des miettes donnerait des chapelets.
	{
		// Adjacence des regions FINALES -- celle des bassins bruts ne vaut plus
		// depuis la coupe en hautes et basses terres.
		TMap<uint64, int32> FrontRegions;
		TArray<TSet<int32>> VoisinsR;
		VoisinsR.SetNum(NbFinal);
		for (int32 J = 0; J < NY; ++J)
		{
			for (int32 I = 0; I < NX; ++I)
			{
				const int32 A = Final[J * NX + I];
				if (A < 0) { continue; }
				const int32 D = Final[J * NX + ((I + 1) % NX)];
				if (D >= 0 && D != A)
				{
					FrontRegions.FindOrAdd(ClePaire(A, D)) += 1;
					VoisinsR[A].Add(D);
					VoisinsR[D].Add(A);
				}
				if (J + 1 < NY)
				{
					const int32 B = Final[(J + 1) * NX + I];
					if (B >= 0 && B != A)
					{
						FrontRegions.FindOrAdd(ClePaire(A, B)) += 1;
						VoisinsR[A].Add(B);
						VoisinsR[B].Add(A);
					}
				}
			}
		}

		TArray<int32> ParTaille;
		ParTaille.Reserve(NbFinal);
		for (int32 R = 0; R < NbFinal; ++R) { ParTaille.Add(R); }
		ParTaille.Sort([&Out](int32 A, int32 B)
		{
			return (Out.Regions[A].AireKm2 != Out.Regions[B].AireKm2)
				? (Out.Regions[A].AireKm2 > Out.Regions[B].AireKm2)
				: (A < B);
		});

		for (const int32 Graine : ParTaille)
		{
			if (Out.Regions[Graine].Pays != INDEX_NONE) { continue; }

			const int32 PaysId = Out.Pays.Num();
			FWorldseedPays Nouveau;
			Nouveau.Id = PaysId;
			Out.Pays.Add(Nouveau);

			Out.Regions[Graine].Pays = PaysId;
			Out.Pays[PaysId].Regions.Add(Graine);
			float AireCumul = Out.Regions[Graine].AireKm2;

			// On etend tant qu'on n'a pas l'aire visee, en prenant a chaque
			// tour la region libre dont la frontiere avec le pays est la plus
			// longue. A egalite, le plus petit identifiant -- ordre total.
			while (AireCumul < Rules.AirePaysKm2)
			{
				int32 Meilleur = INDEX_NONE;
				int32 MeilleureLongueur = 0;
				for (const int32 Membre : Out.Pays[PaysId].Regions)
				{
					for (const int32 V : VoisinsR[Membre])
					{
						if (Out.Regions[V].Pays != INDEX_NONE) { continue; }
						const int32* L = FrontRegions.Find(ClePaire(Membre, V));
						const int32 Longueur = L ? *L : 0;
						if (Longueur > MeilleureLongueur
							|| (Longueur == MeilleureLongueur && V < Meilleur))
						{
							MeilleureLongueur = Longueur;
							Meilleur = V;
						}
					}
				}
				if (Meilleur == INDEX_NONE) { break; }

				Out.Regions[Meilleur].Pays = PaysId;
				Out.Pays[PaysId].Regions.Add(Meilleur);
				AireCumul += Out.Regions[Meilleur].AireKm2;
			}
		}

		// Mesurer les pays.
		for (FWorldseedPays& P : Out.Pays)
		{
			double SCos = 0.0, SSin = 0.0, SY = 0.0;
			P.AireKm2 = 0.0f;
			for (const int32 R : P.Regions)
			{
				const FWorldseedRegion& Reg = Out.Regions[R];
				P.AireKm2 += Reg.AireKm2;
				const double A = (Reg.CentreM.X / Out.LargeurM + 0.5) * 2.0 * PI;
				SCos += FMath::Cos(A) * Reg.AireKm2;
				SSin += FMath::Sin(A) * Reg.AireKm2;
				SY += Reg.CentreM.Y * Reg.AireKm2;
			}
			const double Poids = FMath::Max<double>(P.AireKm2, 1e-6);
			const double Angle = FMath::Atan2(SSin / Poids, SCos / Poids);
			const double U = (Angle < 0.0 ? Angle + 2.0 * PI : Angle) / (2.0 * PI);
			P.CentreM = FVector2D((U - 0.5) * Out.LargeurM, SY / Poids);
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] regions : %d bassins -> %d regions, %d pays ")
		TEXT("(grille %dx%d, maille %.0f m, %.1f km2 de terres)"),
		NbBruts, Out.Regions.Num(), Out.Pays.Num(), NX, NY,
		Out.LargeurM / NX, NbTerre * AireCelluleKm2);
}

void WorldseedRegions::Nommer(FWorldseedRegions& Regions, int32 Seed)
{
	WORLDSEED_TRACE(RegionsNommer);

	FString Erreur;
	if (!WorldseedNoms::Charger(Erreur))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] regions non nommees : %s"), *Erreur);
		return;
	}

	/**
	 * LE CARACTERE DU TERRAIN DECIDE DE LA LANGUE, et cette table est le seul
	 * endroit ou les deux se rencontrent.
	 *
	 * ELLE EST EN DUR, PAS DANS `world_rules.json`, et c'est un choix : ce
	 * fichier est hache EN ENTIER pour l'empreinte du cache, si bien qu'y
	 * changer une sonorite regenererait tous les mondes -- quatre minutes de
	 * tectonique pour un nom. C'est le meme argument qui met les corpus dans
	 * `Content/` plutot que dans les regles.
	 *
	 * TROIS BASES PAR CARACTERE, ET NON UNE. Le corpus d'Azgaar en porte 43 ;
	 * n'en employer que six laisserait dormir les sept huitiemes de ce qu'on
	 * vient de porter. Le choix entre les trois se fait sur la POSITION du
	 * pays, donc il est stable.
	 *
	 * LES CHOIX NE SONT PAS ARBITRAIRES : le basque est la langue des
	 * Pyrenees, donc une langue de massif ; le portugais, le grec et l'italien
	 * sont trois langues de facade maritime ; l'inuit et le finnois sont des
	 * langues de froid. La geographie reelle a deja fait ce travail.
	 */
	auto BasesPour = [](EWorldseedRegionCaractere C) -> TArray<FString>
	{
		switch (C)
		{
		case EWorldseedRegionCaractere::Polaire:
			return { TEXT("Nordic"), TEXT("Finnic"), TEXT("Inuit") };
		case EWorldseedRegionCaractere::Massif:
			return { TEXT("Dwarven"), TEXT("Giant"), TEXT("Basque") };
		case EWorldseedRegionCaractere::Aride:
			return { TEXT("Arabic"), TEXT("Berber"), TEXT("Mesopotamian") };
		case EWorldseedRegionCaractere::ForetHumide:
			return { TEXT("Elven"), TEXT("Celtic"), TEXT("Nahuatl") };
		case EWorldseedRegionCaractere::Littoral:
			return { TEXT("Portuguese"), TEXT("Greek"), TEXT("Italian") };
		default:
			return { TEXT("French"), TEXT("German"), TEXT("Hungarian") };
		}
	};

	// LA GRAINE VIENT DE LA POSITION, JAMAIS D'UN COMPTEUR. Un compteur ferait
	// dependre le nom de la Ne region de toutes les precedentes : inserer une
	// region, ou changer l'ordre de l'agglomeration, renommerait tout le monde.
	// La position, elle, ne bouge pas.
	auto GraineDe = [Seed](const FVector2D& P, uint32 Sel) -> int32
	{
		const int32 X = FMath::RoundToInt(P.X);
		const int32 Y = FMath::RoundToInt(P.Y);
		uint32 H = static_cast<uint32>(Seed) * 2654435761u;
		H ^= static_cast<uint32>(X) * 2246822519u;
		H = (H << 13) | (H >> 19);
		H ^= static_cast<uint32>(Y) * 3266489917u;
		H ^= Sel * 374761393u;
		H ^= H >> 15;
		return static_cast<int32>(H & 0x7FFFFFFF);
	};

	/**
	 * UNE LANGUE PAR PAYS, ET C'EST L'INVARIANT QUI COMPTE.
	 *
	 * Le premier jet donnait sa base a CHAQUE region d'apres son propre
	 * caractere. Le resultat aurait ete une bouillie : deux vallees voisines
	 * d'un meme royaume, l'une un peu plus haute que l'autre, auraient porte
	 * l'une un nom nain et l'autre un nom francais. Une frontiere politique
	 * separe des langues ; un col n'en separe pas.
	 *
	 * C'est donc la region la PLUS ETENDUE du pays qui donne le ton -- son
	 * caractere est celui qui domine le territoire -- et toutes les autres la
	 * suivent. Le caractere de chaque region reste calcule et sert ailleurs :
	 * il est la pour la vegetation, l'ambiance et le releve.
	 */
	for (FWorldseedPays& P : Regions.Pays)
	{
		int32 Coeur = INDEX_NONE;
		float Plus = -1.0f;
		for (const int32 R : P.Regions)
		{
			if (Regions.Regions.IsValidIndex(R) && Regions.Regions[R].AireKm2 > Plus)
			{
				Plus = Regions.Regions[R].AireKm2;
				Coeur = R;
			}
		}

		const EWorldseedRegionCaractere Ton = Regions.Regions.IsValidIndex(Coeur)
			? Regions.Regions[Coeur].Caractere
			: EWorldseedRegionCaractere::Plaine;

		const TArray<FString> Choix = BasesPour(Ton);
		const int32 Tire = GraineDe(P.CentreM, 0x9E37u) % FMath::Max(1, Choix.Num());
		P.Univers = Choix[Tire];

		// LE PAYS PREND UN NOM D'ETAT -- c'est-a-dire une racine plus le
		// suffixe que sa langue appelle : « -ia », « -land », « -terre ». Les
		// bases de fantasy n'en recoivent aucun, et c'est voulu : « Draconia »
		// sonnerait romain.
		FRandomStream RngPays(GraineDe(P.CentreM, 0x5BF0u));
		P.Nom = WorldseedNoms::Etat(P.Univers, FString(), RngPays);

		// LES REGIONS PARLENT LA LANGUE DE LEUR PAYS, et portent un nom de
		// LIEU -- un mot, sans suffixe d'Etat : une province n'est pas un
		// royaume.
		for (const int32 R : P.Regions)
		{
			if (!Regions.Regions.IsValidIndex(R)) { continue; }
			FWorldseedRegion& Reg = Regions.Regions[R];
			Reg.Univers = P.Univers;
			FRandomStream Rng(GraineDe(Reg.CentreM, 0x2545u));
			Reg.Nom = WorldseedNoms::Mot(Reg.Univers, Rng);
		}
	}

	// UNE REGION SANS PAYS NE DOIT PAS RESTER SANS NOM. Le cas n'arrive que si
	// l'agregation a laisse quelque chose de cote -- une ile isolee, par
	// exemple -- et un trou se verrait sur la carte.
	for (FWorldseedRegion& R : Regions.Regions)
	{
		if (!R.Nom.IsEmpty()) { continue; }
		const TArray<FString> Choix = BasesPour(R.Caractere);
		const int32 Tire = GraineDe(R.CentreM, 0x9E37u) % FMath::Max(1, Choix.Num());
		R.Univers = Choix[Tire];
		FRandomStream Rng(GraineDe(R.CentreM, 0x2545u));
		R.Nom = WorldseedNoms::Mot(R.Univers, Rng);
	}

	// ⚠ L'UNICITE SE FORCE, ELLE NE S'ESPERE PAS. Un Markov peut rendre deux
	// fois le meme mot -- c'est meme certain sur un corpus de deux cents mots
	// et quarante tirages (paradoxe des anniversaires). Deux regions du meme
	// nom sur une carte sont un defaut que le joueur remarque immediatement,
	// et qu'aucun oracle de determinisme ne verrait.
	//
	// ON RETIRE JUSQU'A HUIT FOIS, puis on renonce : un corpus trop maigre ne
	// doit pas faire tourner la boucle sans fin, et un doublon vaut mieux
	// qu'un gel.
	{
		TSet<FString> Pris;
		for (FWorldseedPays& P : Regions.Pays)
		{
			int32 Essai = 0;
			while (!P.Nom.IsEmpty() && Pris.Contains(P.Nom) && Essai < 8)
			{
				FRandomStream Rng(GraineDe(P.CentreM, 0x5BF0u + (++Essai)));
				P.Nom = WorldseedNoms::Etat(P.Univers, FString(), Rng);
			}
			Pris.Add(P.Nom);
		}
		for (FWorldseedRegion& R : Regions.Regions)
		{
			int32 Essai = 0;
			while (!R.Nom.IsEmpty() && Pris.Contains(R.Nom) && Essai < 8)
			{
				FRandomStream Rng(GraineDe(R.CentreM, 0x2545u + (++Essai)));
				R.Nom = WorldseedNoms::Mot(R.Univers, Rng);
			}
			Pris.Add(R.Nom);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] regions nommees : %d regions, %d pays"),
		Regions.Regions.Num(), Regions.Pays.Num());
}
