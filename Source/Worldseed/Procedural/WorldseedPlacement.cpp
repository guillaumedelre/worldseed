// Worldseed - ou poser le joueur : pente, sol plein, recherche de sol plat.

#include "Procedural/WorldseedPlacement.h"

#include "Procedural/WorldseedDensity.h"
#include "Procedural/WorldseedGrid.h"

namespace WorldseedPlacement
{
	float PenteDeg(const FWorldseedDensity& Champ, double XM, double YM)
	{
		const float HX = Champ.SurfaceHeightM(XM + SondePenteM, YM)
			- Champ.SurfaceHeightM(XM - SondePenteM, YM);
		const float HY = Champ.SurfaceHeightM(XM, YM + SondePenteM)
			- Champ.SurfaceHeightM(XM, YM - SondePenteM);

		const float Pente = FMath::Sqrt(HX * HX + HY * HY)
			/ (2.0f * static_cast<float>(SondePenteM));
		return FMath::RadiansToDegrees(FMath::Atan(Pente));
	}

	bool SolPlein(const FWorldseedDensity& Champ, double XM, double YM,
		double SurfaceM)
	{
		for (double Profondeur = 1.0; Profondeur <= ProfondeurPleinM;
			Profondeur += 2.0)
		{
			if (Champ.At(FVector(XM, YM, SurfaceM - Profondeur)) > 0.0)
			{
				return false;
			}
		}
		return true;
	}

	bool SolPlat(const FWorldseedDensity& Champ, const FVector2D& AutourM,
		float PenteMaxDeg, float EcartAltitudeMaxM, float AltitudeRefM,
		double& OutX, double& OutY, float& OutSurfaceM, float& OutPenteDeg)
	{
		constexpr double PasM = 16.0;
		constexpr int32 Anneaux = 24;

		auto Convient = [&Champ, PenteMaxDeg, EcartAltitudeMaxM, AltitudeRefM]
			(double X, double Y, float& Surface, float& Pente) -> bool
		{
			Surface = Champ.SurfaceHeightM(X, Y);
			if (Surface < 2.0f)
			{
				return false;   // sous la mer, ou tout juste au bord
			}

			// LA BORNE D'ALTITUDE PASSE AVANT LA PENTE, parce qu'elle est
			// beaucoup plus selective et qu'elle coute une soustraction quand
			// l'autre coute quatre echantillonnages du champ.
			if (EcartAltitudeMaxM > 0.0f
				&& FMath::Abs(Surface - AltitudeRefM) > EcartAltitudeMaxM)
			{
				return false;
			}

			Pente = PenteDeg(Champ, X, Y);
			if (Pente > PenteMaxDeg)
			{
				return false;
			}

			return SolPlein(Champ, X, Y, Surface);
		};

		for (int32 Anneau = 0; Anneau <= Anneaux; ++Anneau)
		{
			for (int32 DY = -Anneau; DY <= Anneau; ++DY)
			{
				for (int32 DX = -Anneau; DX <= Anneau; ++DX)
				{
					// Seulement le bord de l'anneau : l'interieur a deja ete vu.
					if (Anneau > 0 && FMath::Abs(DX) != Anneau
						&& FMath::Abs(DY) != Anneau)
					{
						continue;
					}

					const double X = AutourM.X + DX * PasM;
					const double Y = AutourM.Y + DY * PasM;

					float Surface = 0.0f;
					float Pente = 0.0f;
					if (Convient(X, Y, Surface, Pente))
					{
						OutX = X;
						OutY = Y;
						OutSurfaceM = Surface;
						OutPenteDeg = Pente;
						return true;
					}
				}
			}
		}
		return false;
	}
}

bool WorldseedPlacement::TerreEmergeeLaPlusProche(const FWorldseedGeometry& Geo,
	const TArray<float>& Heights, const FVector2D& AutourM,
	float MargeDeplacementM, double& OutX, double& OutY)
{
	const int32 NX = Geo.NX;
	const int32 NY = Geo.NY;
	if (NX < 2 || NY < 2 || Heights.Num() != NX * NY)
	{
		return false;
	}

	// --- POURQUOI LA GRILLE 2D, ET PAS UNE SPIRALE DANS LE CHAMP ----------
	//
	// `WorldseedPlacement::SolPlat` fouille un voisinage en evaluant le champ de densite :
	// vingt-quatre anneaux au pas de seize metres, soit 384 m de portee et
	// deja 2401 evaluations. C'est le bon outil pour choisir OU se poser une
	// fois qu'on est sur la bonne terre -- et c'est le mauvais pour TROUVER
	// cette terre : ce monde est de l'ocean a 70,8 %, le point de depart tombe
	// au large, et il n'y a aucune terre a 384 m. Couvrir trente kilometres au
	// meme pas demanderait des millions d'evaluations.
	//
	// La grille des altitudes, elle, est DEJA EN MEMOIRE et elle est petite :
	// 4096 x 2048 valeurs que l'on balaie une fois, au demarrage, apres une
	// generation qui a dure une minute. C'est la meme doctrine que le reste du
	// diffuseur -- « la grille 2D decide, le champ affine ».
	//
	// ON PREND LA PLUS PROCHE, PAS LA MEILLEURE. Le joueur doit demarrer sur
	// la terre ; rien ne dit qu'il doive demarrer sur LA plus belle plaine du
	// monde, et viser un optimum global le ferait naitre chaque fois au meme
	// endroit quelle que soit la graine.
	//
	// ET ON EXIGE UNE CELLULE INTERIEURE, pas un liseré cotier : une cellule
	// emergee dont les quatre voisines le sont aussi. Sans cela on choisirait
	// volontiers un recif ou une pointe de sable ou `WorldseedPlacement::SolPlat` ne
	// trouverait ensuite ni pente douce ni roche pleine, et l'on serait revenu
	// au point de depart avec une etape de plus.
	//
	// LE PLANCHER SE DEDUIT DU BRUIT, IL N'EST PAS CHOISI. La grille de
	// simulation dit une altitude ; le champ de densite y ajoute ensuite son
	// grain -- jusqu'a `OverhangAmplitudeM` de deplacement vertical et
	// `DetailAmplitudeM` de detail. Une cellule a cinq metres peut donc se
	// retrouver SOUS la ligne d'eau une fois maillee, et c'est exactement ce
	// que le premier essai a donne : « altitude 4,9 m », c'est-a-dire les
	// pieds dans l'eau au premier remous du bruit. On exige donc deux fois
	// l'amplitude que le voxel peut retirer.
	//
	// Le depot a deja la meme regle ailleurs, pour la meme raison : la marge
	// de sommet des arches existe parce que « le champ de densite deplace la
	// surface et la passe des cavites ne le sait pas ». Une constante en dur
	// aurait cesse d'etre juste au premier reglage du bruit.
	const float PlancherM = 2.0f * MargeDeplacementM;

	auto Emergee = [&Heights, NX, NY](int32 I, int32 J, float Seuil) -> bool
	{
		const int32 IW = ((I % NX) + NX) % NX;
		const int32 JC = FMath::Clamp(J, 0, NY - 1);
		return Heights[JC * NX + IW] > Seuil;
	};

	const double LargeurM = Geo.WidthM();
	const double HauteurM = Geo.HeightM;

	// Cellule du point demande, meme convention que `SampleUV` : X enroule,
	// Y est borne.
	const int32 I0 = FMath::Clamp(
		FMath::FloorToInt((AutourM.X / LargeurM + 0.5) * NX), 0, NX - 1);
	const int32 J0 = FMath::Clamp(
		FMath::FloorToInt((AutourM.Y / HauteurM + 0.5) * NY), 0, NY - 1);

	int32 MeilleurI = -1;
	int32 MeilleurJ = -1;
	int64 MeilleureDistance = TNumericLimits<int64>::Max();

	for (int32 J = 0; J < NY; ++J)
	{
		for (int32 I = 0; I < NX; ++I)
		{
			if (Heights[J * NX + I] <= PlancherM)
			{
				continue;
			}
			if (!Emergee(I + 1, J, 0.0f) || !Emergee(I - 1, J, 0.0f)
				|| !Emergee(I, J + 1, 0.0f) || !Emergee(I, J - 1, 0.0f))
			{
				continue;
			}

			// X ENROULE, DONC LA DISTANCE AUSSI. Mesurer l'ecart en colonnes
			// sans tenir compte du bouclage ferait croire qu'une terre situee
			// juste de l'autre cote de la couture est a un monde de distance.
			int64 DI = FMath::Abs(static_cast<int64>(I) - I0);
			DI = FMath::Min(DI, static_cast<int64>(NX) - DI);
			const int64 DJ = static_cast<int64>(J) - J0;

			const int64 D2 = DI * DI + DJ * DJ;
			if (D2 < MeilleureDistance)
			{
				MeilleureDistance = D2;
				MeilleurI = I;
				MeilleurJ = J;
			}
		}
	}

	if (MeilleurI < 0)
	{
		return false;
	}

	// Centre de la cellule : le coin serait sur la frontiere avec une cellule
	// qui peut etre marine.
	OutX = (static_cast<double>(MeilleurI) + 0.5) / NX * LargeurM - LargeurM * 0.5;
	OutY = (static_cast<double>(MeilleurJ) + 0.5) / NY * HauteurM - HauteurM * 0.5;
	return true;
}

FWorldseedDepart WorldseedPlacement::Choisir(const FWorldseedDensity& Champ,
	const FWorldseedGeometry& Geo, const TArray<float>& Heights,
	const FVector2D& DemandeM, const FWorldseedDepartRegles& R)
{
	FWorldseedDepart D;
	D.XM = DemandeM.X;
	D.YM = DemandeM.Y;
	D.SurfaceM = Champ.SurfaceHeightM(D.XM, D.YM);
	D.SurfaceDemandeeM = D.SurfaceM;

	// --- 1. LA TERRE EMERGEE LA PLUS PROCHE ---------------------------------
	//
	// ELLE PRECEDE LA FOUILLE FINE, ET C EST L ORDRE QUI COMPTE. Ce monde est
	// de l ocean a 70,8 % ; `SolPlat` ne porte qu a 384 metres, donc un joueur
	// pose au large n en sortirait jamais. La grille 2D, elle, donne la terre
	// la plus proche en un balayage, a des dizaines de kilometres s il le faut.
	// Chacune fait ce qu elle sait faire.
	double TerreX = D.XM;
	double TerreY = D.YM;
	D.bTerreTrouvee = TerreEmergeeLaPlusProche(Geo, Heights, DemandeM,
		R.MargeDeplacementM, TerreX, TerreY);
	if (D.bTerreTrouvee)
	{
		D.TerreXM = TerreX;
		D.TerreYM = TerreY;
		D.XM = TerreX;
		D.YM = TerreY;
		D.SurfaceM = Champ.SurfaceHeightM(D.XM, D.YM);
	}

	// --- 2. LE REGIME, ET IL Y EN A TROIS -----------------------------------

	// LE DEPART EXACT, POUR INSPECTER UN POINT PRECIS. Aucune recherche, aucune
	// borne de pente : c est le seul moyen d aller voir une paroi, un surplomb
	// ou une bouche de grotte, que toute recherche de sol PLAT ecarterait par
	// construction. Le joueur peut glisser, et c est accepte.
	if (R.bExact)
	{
		D.Issue = EWorldseedDepart::Exact;
		D.PenteDeg = PenteDeg(Champ, D.XM, D.YM);
		D.bColonneCreuse = !SolPlein(Champ, D.XM, D.YM, D.SurfaceM);
		return D;
	}

	double FX = D.XM;
	double FY = D.YM;
	float FSurface = D.SurfaceM;
	float FPente = 0.0f;

	// LE DEPART CHOISI EST BORNE EN ALTITUDE, et cette borne est tout le sujet.
	// `SolPlat` retient le PREMIER point acceptable de sa spirale : sur un
	// versant raide, le premier sol a douze degres est la plaine d en bas.
	// Mesure sur deux parties independantes -- latitudes 73,1 et 15,3 degres,
	// donc sans rapport de terrain -- le joueur est ne 89 et 92 metres SOUS le
	// point qu il avait choisi. Qui visait un sommet naissait a son pied.
	if (R.bChoisi)
	{
		if (SolPlat(Champ, FVector2D(D.XM, D.YM), R.PenteChoisieMaxDeg,
			R.EcartAltitudeMaxM, D.SurfaceM, FX, FY, FSurface, FPente))
		{
			D.Issue = EWorldseedDepart::SolPlatTrouve;
			D.XM = FX;
			D.YM = FY;
			D.SurfaceM = FSurface;
			D.PenteDeg = FPente;
		}
		else
		{
			// ECHEC FRANC : ON TIENT LE POINT VISE.
			//
			// ARBITRAGE DU PROPRIETAIRE, 22 septembre 2026. Il y avait ici un
			// repli vers la recherche LARGE -- douze degres, aucune borne
			// d altitude -- et c etait une FALAISE DE POLITIQUE : quarante
			// metres, puis l infini, en un cran. Deux autres formes ont ete
			// presentees puis ECARTEES : l elargissement par crans, qui gardait
			// un echec graduel, et le MEILLEUR de toute la spirale, qui traitait
			// bien la cause -- « elle retient le premier point acceptable, pas
			// le meilleur » -- mais au prix de la PROXIMITE, en naissant jusqu a
			// 380 metres du point vise pour gagner trois metres.
			//
			// Ce qui est accepte en echange : le pion peut naitre sur une paroi
			// et glisser.
			D.Issue = EWorldseedDepart::PointViseTenu;
			D.PenteDeg = PenteDeg(Champ, D.XM, D.YM);

			// LA COLONNE CREUSE NE FAIT PAS ECHOUER LE CHOIX, mais l appelant
			// doit pouvoir le DIRE : le joueur peut tomber dans une cavite, et
			// personne ne saurait pourquoi.
			D.bColonneCreuse = !SolPlein(Champ, D.XM, D.YM, D.SurfaceM);
		}
		return D;
	}

	// NAISSANCE LIBRE : l endroit n a aucune importance, donc la recherche
	// large reste la bonne reponse.
	if (SolPlat(Champ, FVector2D(D.XM, D.YM), R.PenteLibreMaxDeg,
		0.0f, 0.0f, FX, FY, FSurface, FPente))
	{
		D.Issue = EWorldseedDepart::SolPlatTrouve;
		D.XM = FX;
		D.YM = FY;
		D.SurfaceM = FSurface;
		D.PenteDeg = FPente;
	}
	else
	{
		D.Issue = EWorldseedDepart::Echec;
		D.PenteDeg = PenteDeg(Champ, D.XM, D.YM);
	}
	return D;
}
