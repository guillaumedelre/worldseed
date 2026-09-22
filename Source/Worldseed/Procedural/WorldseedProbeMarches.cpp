// Worldseed - sonde des marches : les gradins tombent-ils sur les BANCS ?
//
// LA QUESTION, ET POURQUOI ELLE N'ETAIT PAS ENCORE POSEE. Le 22 septembre, la
// carte des causes a montre des parois de canyon en gradins dont les bandes de
// couleur suivent les bancs. J'en ai conclu, et ECRIT au registre, que
// l'erosion stratifiee et le sapement des corniches avaient taille ces
// marches. C'etait une INFERENCE tiree d'une image.
//
// ELLE PEUT ETRE FAUSSE POUR UNE RAISON BETE : les bandes de couleur sont
// horizontales PAR CONSTRUCTION -- `BancAt` ne depend que de Z -- et les
// gradins d'un versant le sont aussi. Deux choses independamment horizontales
// se superposent toujours. Ce depot a deja paye la version voisine de cette
// faute en prenant l'ombrage d'un versant pour des bandes de roche.
//
// CE QUE CETTE SONDE MESURE : la PHASE. Pour chaque point de paroi, on calcule
// ou il se trouve DANS son banc -- 0 au toit, 1 a la base -- et l'on regarde
// si la PENTE depend de cette phase. Si la stratigraphie taille les marches,
// la pente doit s'effondrer pres du toit d'un banc dur (la banquette) et
// remonter au milieu (la contremarche). Si elle n'y est pour rien, la pente ne
// depend pas de la phase et la courbe est plate.
//
// LE TEMOIN EST DANS LA SONDE, ET IL EST INDISPENSABLE. `DecalageDatumM`
// deplace les limites de bancs SUPPOSEES sans toucher au terrain. A un demi
// banc de decalage, une structure reelle doit se BROUILLER ; si elle survit,
// c'est la methode qui la fabrique. Sans ce temoin on ne saurait pas
// distinguer « les marches suivent les bancs » de « ma statistique rend
// toujours une bosse ».

#include "Procedural/WorldseedProbeLibrary.h"

#include "Procedural/WorldseedProbeCommun.h"
#include "Procedural/WorldseedStrata.h"

namespace
{
	/** Un casier de phase : combien de points, et combien de PLATS. */
	struct FCasier
	{
		int32 N = 0;
		int32 Plats = 0;
		double SommePente = 0.0;

		double Moyenne() const { return N > 0 ? SommePente / N : 0.0; }
		double PartPlats() const { return N > 0 ? 100.0 * Plats / N : 0.0; }
	};

	constexpr int32 NbCasiers = 10;

	/**
	 * L'AMPLITUDE D'UNE COURBE, RAPPORTEE A SA MOYENNE.
	 *
	 * Sans dimension a dessein : elle se compare d'un temoin a l'autre sans
	 * dependre du relief du monde.
	 */
	double Amplitude(const FCasier* C, double Moyenne)
	{
		double Min = 1e30, Max = -1e30;
		for (int32 K = 0; K < NbCasiers; ++K)
		{
			Min = FMath::Min(Min, C[K].Moyenne());
			Max = FMath::Max(Max, C[K].Moyenne());
		}
		return (Moyenne > 1e-6) ? (Max - Min) / Moyenne : 0.0;
	}

	/** Le casier le plus charge : DIT OU est le pic, ce que l'amplitude tait. */
	int32 PicDesComptes(const FCasier* C)
	{
		int32 Pic = 0;
		for (int32 K = 1; K < NbCasiers; ++K)
		{
			if (C[K].N > C[Pic].N) { Pic = K; }
		}
		return Pic;
	}

	/**
	 * La pente locale du relief 2D, en degres.
	 *
	 * ELLE SE LIT SUR LA GRILLE ET NON SUR LA BICUBIQUE, a dessein : une
	 * marche du relief 2D ne peut pas etre plus fine que la maille de
	 * simulation -- 15,6 m ici -- et interroger l'interpolant a un pas plus
	 * fin ne ferait qu'inventer des pentes intermediaires que la donnee ne
	 * porte pas.
	 */
	double PenteDeg(const FWorldseedGeometry& G, const TArray<float>& H,
		int32 Col, int32 Row)
	{
		const int32 C0 = FMath::Clamp(Col - 1, 0, G.NX - 1);
		const int32 C1 = FMath::Clamp(Col + 1, 0, G.NX - 1);
		const int32 R0 = FMath::Clamp(Row - 1, 0, G.NY - 1);
		const int32 R1 = FMath::Clamp(Row + 1, 0, G.NY - 1);

		const double MailleX = G.WidthM() / FMath::Max(1, G.NX);
		const double MailleY = G.HeightM / FMath::Max(1, G.NY);

		const double DX = (H[Row * G.NX + C1] - H[Row * G.NX + C0])
			/ FMath::Max(1.0, static_cast<double>(C1 - C0) * MailleX);
		const double DY = (H[R1 * G.NX + Col] - H[R0 * G.NX + Col])
			/ FMath::Max(1.0, static_cast<double>(R1 - R0) * MailleY);

		return FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(DX * DX + DY * DY)));
	}
}

FString UWorldseedProbeLibrary::ProbeMarches(int32 Seed, float HeightMeters,
	int32 ResolutionY, float DecalageDatumM)
{
	FWorldseedSonde S;
	if (!S.Preparer(Seed, HeightMeters, ResolutionY))
	{
		return FString::Printf(TEXT("[marches] %s"), *S.Erreur);
	}

	const FWorldseedStratRules SR = FWorldseedStratRules::FromRules(*S.Regles, S.Litho);
	if (!SR.IsActive())
	{
		return TEXT("[marches] la serie stratigraphique est vide : rien a mesurer");
	}

	const FWorldseedGeometry& G = S.World.Geometry;
	const TArray<float>& H = S.World.ElevationM;

	// --- OU MESURER : LES PAROIS DE CANYON, ET RIEN D'AUTRE ------------------
	//
	// « MESURER UNE FORME RARE AU MAUVAIS ENDROIT » est le piege que ce depot a
	// paye sur les diaclases, puis sur l'entonnoir de la teinte de roche --
	// releve au point d'apparition du banc, une plaine cotiere a 4,9 m ou la
	// serie (267 a 520 m) ne monte jamais. On part donc des sites de canyon
	// que la chaine a elle-meme designes.
	const TArray<FWorldseedPlateauSite>& Canyons = S.World.Canyons;
	if (Canyons.Num() == 0)
	{
		return TEXT("[marches] aucun canyon dans ce monde : rien a mesurer");
	}

	// Les bornes de la pile, telles que la GEOMETRIE les a vues. L'erosion
	// stratifiee et le sapement lisent la pile BORNEE -- l'enroulement n'a
	// touche que `BancAt`, donc la couleur. Au-dessus du datum, la geometrie
	// n'a aucune structure de bancs : la mesurer la-haut compterait du bruit.
	const double Total = static_cast<double>(SR.TotalThicknessM);

	FCasier Casiers[NbCasiers];
	FCasier CasiersDecale[NbCasiers];
	FCasier CasiersHasard[NbCasiers];
	int32 Retenus = 0;
	int32 HorsPile = 0;
	int32 AuDessus = 0;
	int32 SousLaPile = 0;
	int32 HorsRelief = 0;

	const double MailleXM = G.WidthM() / FMath::Max(1, G.NX);
	const double MailleYM = G.HeightM / FMath::Max(1, G.NY);

	for (const FWorldseedPlateauSite& Site : Canyons)
	{
		// Une fenetre autour du site, assez large pour porter les deux parois
		// et assez serree pour ne pas sortir du canyon.
		constexpr double RayonM = 600.0;
		const int32 DemiC = FMath::Max(1, FMath::RoundToInt(RayonM / MailleXM));
		const int32 DemiR = FMath::Max(1, FMath::RoundToInt(RayonM / MailleYM));

		const double U = Site.CentreM.X / G.WidthM() + 0.5;
		const double V = FMath::Clamp(Site.CentreM.Y / G.HeightM + 0.5, 0.0, 1.0);
		const int32 CC = FMath::Clamp(FMath::FloorToInt(U * G.NX), 0, G.NX - 1);
		const int32 CR = FMath::Clamp(FMath::FloorToInt(V * G.NY), 0, G.NY - 1);

		for (int32 R = CR - DemiR; R <= CR + DemiR; ++R)
		{
			if (R < 1 || R >= G.NY - 1) { continue; }
			for (int32 C = CC - DemiC; C <= CC + DemiC; ++C)
			{
				if (C < 1 || C >= G.NX - 1) { continue; }

				const double Z = static_cast<double>(H[R * G.NX + C]);

				// Position reelle de la cellule, pour que le gauchissement du
				// datum soit lu au bon endroit.
				const double XM = (static_cast<double>(C) / G.NX - 0.5) * G.WidthM();
				const double YM = (static_cast<double>(R) / G.NY - 0.5) * G.HeightM;

				const double Datum = WorldseedStrata::DatumAt(XM, YM, SR, Seed);

				// SEULES LES CELLULES DANS LA PILE COMPTENT. Au-dessus du
				// datum la geometrie n'a pas de bancs ; en dessous non plus.
				// LA GEOMETRIE N'A DE BANCS QUE DANS LA PILE BORNEE, et cette
				// ventilation dit combien de terrain n'en a pas : au-dessus du
				// datum, la couleur enroulee peint des bandes sur un relief
				// qui n'en porte aucune.
				const double Prof = Datum - Z;
				if (Prof < 0.0) { ++AuDessus; ++HorsPile; continue; }
				if (Prof > Total) { ++SousLaPile; ++HorsPile; continue; }

				const double Pente = PenteDeg(G, H, C, R);

				// --- ON NE FILTRE PLUS PAR LA PENTE, ET C'EST UNE CORRECTION
				//     DE METHODE -------------------------------------------
				//
				// La premiere version ecartait les points sous huit degres
				// « pour ne garder que les parois ». Elle ecartait donc
				// exactement les BANQUETTES -- la partie plate d'une marche --
				// c'est-a-dire la moitie de ce dont on teste l'existence. On
				// ne peut pas chercher un escalier en jetant ses marches.
				//
				// Le tri est desormais SPATIAL : on garde les cellules d'un
				// terrain DISSEQUE, mesure par le relief local, et l'on y
				// compte les plats comme les raides.
				double HMin = 1e30, HMax = -1e30;
				for (int32 DR = -3; DR <= 3; ++DR)
				{
					for (int32 DC = -3; DC <= 3; ++DC)
					{
						const int32 RR = FMath::Clamp(R + DR, 0, G.NY - 1);
						const int32 CCx = FMath::Clamp(C + DC, 0, G.NX - 1);
						const double HV = static_cast<double>(H[RR * G.NX + CCx]);
						HMin = FMath::Min(HMin, HV);
						HMax = FMath::Max(HMax, HV);
					}
				}
				if (HMax - HMin < 30.0) { ++HorsRelief; continue; }

				++Retenus;

				// LA PHASE : ou sommes-nous DANS le banc, de 0 au toit a 1 a
				// la base. C'est la seule grandeur qui puisse lier une pente a
				// la stratigraphie sans rien supposer de l'echelle.
				auto Deposer = [&](int32 K, FCasier* Ou)
				{
					Ou[K].N += 1;
					Ou[K].SommePente += Pente;
					if (Pente < 15.0) { Ou[K].Plats += 1; }
				};

				auto Ranger = [&](double DatumSuppose, FCasier* Ou)
				{
					double P = DatumSuppose - Z;
					P = FMath::Fmod(P, Total);
					if (P < 0.0) { P += Total; }

					double Reste = P;
					for (int32 B = 0; B < SR.Serie.Num(); ++B)
					{
						const double E = static_cast<double>(SR.Serie[B].ThicknessM);
						if (Reste < E)
						{
							Deposer(FMath::Clamp(
								FMath::FloorToInt((Reste / FMath::Max(1e-6, E)) * NbCasiers),
								0, NbCasiers - 1), Ou);
							return;
						}
						Reste -= E;
					}
				};

				Ranger(Datum, Casiers);

				// --- DEUX TEMOINS, PARCE QU'ILS NE DISENT PAS LA MEME CHOSE --
				//
				// LE DECALAGE ne DETRUIT pas une structure reelle : il la
				// DEPLACE. Mon premier jet n'avait que lui, et comparait des
				// AMPLITUDES -- or l'amplitude d'une courbe est invariante par
				// translation, donc le temoin rendait le meme chiffre quoi
				// qu'il arrive et ne pouvait rien refuter. Il reste utile pour
				// une autre question : le pic doit BOUGER.
				Ranger(Datum + DecalageDatumM, CasiersDecale);

				// LE HASARD, LUI, DETRUIT. Une phase tiree par le hachage de la
				// cellule garde les memes pentes et les memes effectifs, et
				// n'a aucun lien avec les bancs : ce qui subsiste dans cette
				// courbe est le bruit d'echantillonnage, et rien d'autre.
				// C'est le vrai zero de la mesure.
				const uint32 Hache = HashCombine(
					GetTypeHash(C * 73856093), GetTypeHash(R * 19349663));
				Deposer(static_cast<int32>(Hache % NbCasiers), CasiersHasard);
			}
		}
	}

	if (Retenus == 0)
	{
		return FString::Printf(
			TEXT("[marches] aucun point dans la pile (%d hors pile, ")
			TEXT("%d hors relief) -- la mesure ne dit RIEN"),
			HorsPile, HorsRelief);
	}

	double Somme = 0.0;
	for (int32 K = 0; K < NbCasiers; ++K) { Somme += Casiers[K].Moyenne(); }
	const double Moy = Somme / NbCasiers;

	const double Attendu = static_cast<double>(Retenus) / NbCasiers;

	FString R;
	R += FString::Printf(
		TEXT("[marches] %d canyons, %d points retenus (%d hors pile, ")
		TEXT("%d hors relief) | pile %.0f m, datum %.0f m, casier %.1f m\n"),
		Canyons.Num(), Retenus, HorsPile, HorsRelief, Total, SR.DatumM,
		Total / NbCasiers);

	// --- COMBIEN DE TERRAIN LA PILE COUVRE-T-ELLE VRAIMENT ------------------
	//
	// LE CHIFFRE QUI CHANGE LA LECTURE DU ZEBRE. La geometrie n'a de bancs que
	// dans la pile BORNEE, 267 a 520 m ; la couleur, elle, est ENROULEE depuis
	// le 22 septembre et peint des bandes a toutes les altitudes. Partout ou
	// le terrain est au-dessus du datum, on peint donc des rayures sur un
	// relief qui n'en porte aucune -- de la strate sans marche.
	{
		const double TotalCellules = static_cast<double>(Retenus + HorsPile);
		R += FString::Printf(
			TEXT("  couverture de la pile : %.1f %% des cellules dedans, ")
			TEXT("%.1f %% AU-DESSUS du datum, %.1f %% sous la pile\n"),
			100.0 * Retenus / FMath::Max(1.0, TotalCellules),
			100.0 * AuDessus / FMath::Max(1.0, TotalCellules),
			100.0 * SousLaPile / FMath::Max(1.0, TotalCellules));
	}

	R += TEXT("  phase dans le banc, 0 = TOIT et 1 = base\n");
	R += TEXT("  casier |   points  (x attendu) | pente moy. | part de plats\n");
	for (int32 K = 0; K < NbCasiers; ++K)
	{
		R += FString::Printf(
			TEXT("  %.1f-%.1f | %8d  (x%.2f)  | %6.2f deg  | %5.1f %%\n"),
			static_cast<double>(K) / NbCasiers,
			static_cast<double>(K + 1) / NbCasiers,
			Casiers[K].N, Casiers[K].N / FMath::Max(1.0, Attendu),
			Casiers[K].Moyenne(), Casiers[K].PartPlats());
	}

	// --- LES DEUX TEMOINS, ET CE QUE CHACUN REFUTE --------------------------
	const int32 Pic = PicDesComptes(Casiers);
	const int32 PicDecale = PicDesComptes(CasiersDecale);
	const int32 PicHasard = PicDesComptes(CasiersHasard);

	const double A = Amplitude(Casiers, Moy);
	const double ADecale = Amplitude(CasiersDecale, Moy);
	const double AHasard = Amplitude(CasiersHasard, Moy);

	// LA CONCENTRATION EST LE CHIFFRE QUI COMPTE, PAS L'AMPLITUDE DE LA PENTE.
	// Un escalier se signale d'abord par le fait que la surface SEJOURNE a
	// certaines profondeurs : le casier le plus charge porte alors bien plus
	// que son dixieme.
	const double Concentration = Casiers[Pic].N / FMath::Max(1.0, Attendu);
	const double ConcHasard = CasiersHasard[PicHasard].N / FMath::Max(1.0, Attendu);

	R += FString::Printf(
		TEXT("  CONCENTRATION du casier le plus charge : x%.2f (casier %d)\n"),
		Concentration, Pic);
	R += FString::Printf(
		TEXT("  TEMOIN HASARD -- phase tiree au hachage : x%.2f, ")
		TEXT("amplitude de pente %.1f %% contre %.1f %%\n"),
		ConcHasard, AHasard * 100.0, A * 100.0);
	R += FString::Printf(
		TEXT("  TEMOIN DECALAGE de %.0f m -- le pic doit BOUGER : ")
		TEXT("casier %d contre %d, amplitude %.1f %%\n"),
		DecalageDatumM, PicDecale, Pic, ADecale * 100.0);

	// LE VERDICT EXIGE LES DEUX TEMOINS, et il le dit. Le hasard doit rendre
	// une concentration plate ; le decalage doit DEPLACER le pic. Un seul des
	// deux ne suffit pas : l'amplitude d'une courbe survit a une translation,
	// donc le decalage seul ne peut rien refuter.
	const bool bPlusQueLeHasard = Concentration > 1.4 * ConcHasard;
	const bool bPicBouge = (PicDecale != Pic);

	R += (bPlusQueLeHasard && bPicBouge)
		? TEXT("  -> LA SURFACE EST CALEE SUR LES BANCS : la stratigraphie ")
		  TEXT("taille bien les marches")
		: TEXT("  -> AUCUN CALAGE SUR LES BANCS : les marches viennent ")
		  TEXT("d'ailleurs, et la piste des strates est fermee");

	return R;
}
