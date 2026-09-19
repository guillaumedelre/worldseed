// Worldseed - sondes des cavites : galeries, lames et arches.
//
// LES SONDES SONT ECLATEES PAR DOMAINE, une question par fichier. Elles
// vivaient dans un fourre-tout de 1661 lignes ou l'on ne trouvait rien, et ou
// chacune recopiait le meme preambule de vingt lignes -- ce que le depot a
// deja paye : probe_voxel avait OUBLIE SetLithology, et annoncait "le bruit
// est gratuit" en mesurant le monde d'avant. FWorldseedSonde rend l'oubli
// impossible.

#include "Procedural/WorldseedProbeLibrary.h"

#include "Procedural/WorldseedProbeCommun.h"

#include "Procedural/WorldseedFins.h"
#include "Procedural/WorldseedGlobe.h"
#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedPerlin.h"
#include "Procedural/WorldseedVoxelChunk.h"

FString UWorldseedProbeLibrary::ProbeCaves(int32 Seed, float HeightMeters,
	int32 ResolutionY, float AreaM, float StepM, bool bSteepest)
{
	// Les regles sont relues a chaque appel : c'est ce qui permet d'essayer une
	// valeur, de mesurer, et de recommencer sans redemarrer l'editeur.
	WorldseedPipeline::ReloadRules();

	WorldseedPipeline::FResult World;
	FString Error;
	if (!WorldseedPipeline::Generate(Seed, HeightMeters, ResolutionY, World, Error))
	{
		return FString::Printf(TEXT("generation impossible : %s"), *Error);
	}

	const UWorldseedRules* Rules = WorldseedPipeline::GetRules(Error);
	if (!Rules)
	{
		return FString::Printf(TEXT("regles illisibles : %s"), *Error);
	}

	const FWorldseedDensityRules R = FWorldseedDensityRules::FromRules(*Rules);

	FWorldseedDensity Density;
	Density.Init(World.Geometry, World.ElevationM, 1.0f, Seed, R);

	// --- une zone de TERRE, et une zone ordinaire -----------------------------
	// Le point le plus haut du monde est le plus accidente : il flatterait les
	// surplombs. On prend la mediane des terres, c'est-a-dire un relief banal,
	// celui que le joueur verra le plus souvent.
	TArray<float> Terres;
	Terres.Reserve(World.ElevationM.Num() / 4);
	for (const float H : World.ElevationM)
	{
		if (H > 0.0f) { Terres.Add(H); }
	}
	if (Terres.Num() == 0)
	{
		return TEXT("aucune terre emergee");
	}
	const float Mediane = WorldseedGrid::Quantile(Terres, 0.5f);

	const int32 NX = World.Geometry.NX;
	const int32 NYG = World.Geometry.NY;
	int32 Cellule = INDEX_NONE;

	if (bSteepest)
	{
		// LE DEPLACEMENT HORIZONTAL N'AGIT QUE SUR LE RAIDE, par construction :
		// sur du plat, lire le relief vingt metres plus loin donne la meme
		// altitude. Le mesurer sur un relief median reviendrait donc a conclure
		// qu'il ne fait rien -- ce qui serait vrai, et sans interet.
		float PlusRaide = -1.0f;
		for (int32 J = 1; J < NYG - 1; ++J)
		{
			for (int32 I = 1; I < NX - 1; ++I)
			{
				const int32 C = J * NX + I;
				if (World.ElevationM[C] <= 0.0f) { continue; }
				const float DX = World.ElevationM[C + 1] - World.ElevationM[C - 1];
				const float DY = World.ElevationM[C + NX] - World.ElevationM[C - NX];
				const float G = DX * DX + DY * DY;
				if (G > PlusRaide)
				{
					PlusRaide = G;
					Cellule = C;
				}
			}
		}
	}
	else
	{
		float Meilleur = TNumericLimits<float>::Max();
		for (int32 C = 0; C < World.ElevationM.Num(); ++C)
		{
			const float H = World.ElevationM[C];
			if (H <= 0.0f) { continue; }
			const float Ecart = FMath::Abs(H - Mediane);
			if (Ecart < Meilleur)
			{
				Meilleur = Ecart;
				Cellule = C;
			}
		}
	}
	if (Cellule == INDEX_NONE)
	{
		return TEXT("aucune cellule de terre retenue");
	}

	const double MetresParCellule = World.Geometry.MetersPerPixel();
	const double CentreX = (static_cast<double>(Cellule % NX) - NX * 0.5) * MetresParCellule;
	const double CentreY = (static_cast<double>(Cellule / NX) - World.Geometry.NY * 0.5)
		* MetresParCellule;

	// --- balayage -------------------------------------------------------------
	const double Pas = FMath::Max(StepM, 0.5f);
	const double Demi = FMath::Max(AreaM, Pas * 4.0) * 0.5;
	const double PasZ = FMath::Max(Pas * 0.5, 0.5);

	int64 PointsBande = 0;
	int64 PointsAir = 0;
	int32 Colonnes = 0;
	int32 ColonnesSurplomb = 0;
	int32 ColonnesGalerie = 0;
	int32 ColonnesFranchissables = 0;
	int32 ColonnesDebout = 0;
	int32 ColonnesVisiere = 0;
	int32 TraverseesMax = 0;
	double PlusGrandVideMonde = 0.0;

	/** Hauteur de chaque vide rencontre, pour en donner la distribution. */
	TArray<double> Hauteurs;

	TArray<FVector> Exemples;

	for (double Y = CentreY - Demi; Y <= CentreY + Demi; Y += Pas)
	{
		for (double X = CentreX - Demi; X <= CentreX + Demi; X += Pas)
		{
			const float Surface = Density.SurfaceHeightM(X, Y);
			if (Surface <= 0.0f)
			{
				continue;   // on ne mesure pas sous la mer
			}

			++Colonnes;

			const double Haut = Surface + R.OverhangAmplitudeM * 1.5;
			const double Bas = Surface - R.BandDepthM;

			// Profondeur du PREMIER vide sous la surface, et epaisseur de la
			// roche qui le couvre : de quoi separer visieres et galeries.
			double PremierVideSousSurface = -1.0;
			double PremierToitEpaisseur = -1.0;
			double DebutToit = Haut;
			double FinToit = Haut;

			// ON MESURE LA HAUTEUR DES VIDES, PAS LEUR NOMBRE.
			//
			// Compter les traversees revenait a compter les rides : une
			// ondulation de quelques centimetres pesait autant qu'une arche de
			// dix metres. Le premier releve annoncait ainsi 43,8 % de colonnes
			// « a surplomb » sur un terrain qui, a l'image, paraissait lisse --
			// les deux etaient vrais, c'est l'indicateur qui ne disait rien.
			//
			// Ce qui compte est la HAUTEUR LIBRE sous de la roche : c'est elle
			// qui decide si l'on peut passer dessous, entrer dedans, s'y tenir
			// debout.
			int32 Traversees = 0;
			bool bAirPrecedent = Density.At(FVector(X, Y, Haut)) > 0.0;
			bool bGalerie = false;
			bool bRocheVue = false;
			double HauteurVide = 0.0;
			double PlusGrandVide = 0.0;

			for (double Z = Haut - PasZ; Z >= Bas; Z -= PasZ)
			{
				const bool bAir = Density.At(FVector(X, Y, Z)) > 0.0;
				if (bAir != bAirPrecedent)
				{
					++Traversees;
					bAirPrecedent = bAir;
				}

				if (!bAir)
				{
					// EPAISSEUR DU PREMIER TOIT : la roche traversee entre le
					// ciel et le premier vide. C'est elle qui dit si l'on a
					// affaire a une visiere -- quelques metres -- ou au
					// plafond d'une grotte profonde.
					if (!bRocheVue) { DebutToit = Z; }
					if (PremierToitEpaisseur < 0.0) { FinToit = Z; }
					bRocheVue = true;
					if (HauteurVide > 0.0)
					{
						PlusGrandVide = FMath::Max(PlusGrandVide, HauteurVide);
						Hauteurs.Add(HauteurVide);
						HauteurVide = 0.0;
					}
				}
				else if (bRocheVue)
				{
					// De l'air SOUS de la roche : un vide, et non le ciel.
					if (PremierVideSousSurface < 0.0)
					{
						PremierVideSousSurface = Surface - Z;
						PremierToitEpaisseur = DebutToit - FinToit;
					}
					HauteurVide += PasZ;
				}

				if (Z < Surface)
				{
					++PointsBande;
					if (bAir)
					{
						++PointsAir;
						bGalerie = true;

						// Quelques adresses pour aller voir, prises en
						// profondeur : une poche a un metre sous l'herbe ne
						// prouverait rien.
						if (Exemples.Num() < 6 && (Surface - Z) > 15.0
							&& Exemples.Num() * 97 % 7 == PointsAir % 7)
						{
							Exemples.Add(FVector(X, Y, Z));
						}
					}
				}
			}
			if (HauteurVide > 0.0)
			{
				PlusGrandVide = FMath::Max(PlusGrandVide, HauteurVide);
				Hauteurs.Add(HauteurVide);
			}

			TraverseesMax = FMath::Max(TraverseesMax, Traversees);
			if (Traversees > 1) { ++ColonnesSurplomb; }

			// UNE VISIERE EST UN SURPLOMB PEU PROFOND, et c'est ce qui la
			// distingue d'une galerie. Le chiffre global des surplombs melange
			// les deux : une grotte a quatre-vingts metres sous terre compte
			// autant qu'un abri sous roche ou l'on se tient debout. Or ce sont
			// deux formes differentes, et une seule se voit du dehors.
			if (PremierVideSousSurface >= 0.0
				&& PremierVideSousSurface <= 20.0
				&& PremierToitEpaisseur > 0.0
				&& PremierToitEpaisseur <= 15.0)
			{
				++ColonnesVisiere;
			}
			if (bGalerie) { ++ColonnesGalerie; }
			if (PlusGrandVide >= 2.0) { ++ColonnesFranchissables; }
			if (PlusGrandVide >= 2.5) { ++ColonnesDebout; }
			PlusGrandVideMonde = FMath::Max(PlusGrandVideMonde, PlusGrandVide);
		}
	}

	const double PartSurplomb = Colonnes > 0
		? 100.0 * ColonnesSurplomb / Colonnes : 0.0;
	const double PartGalerieColonnes = Colonnes > 0
		? 100.0 * ColonnesGalerie / Colonnes : 0.0;
	const double PartAir = PointsBande > 0
		? 100.0 * static_cast<double>(PointsAir) / PointsBande : 0.0;

	UE_LOG(LogTemp, Log,
		TEXT("[Sonde] formes : relief median %.1f m, zone %.0f m au pas de %.1f m ")
		TEXT("centree sur (%.0f, %.0f) m [%s], %d colonnes de terre"),
		Mediane, AreaM, Pas, CentreX, CentreY,
		bSteepest ? TEXT("le plus raide") : TEXT("relief median"), Colonnes);
	Hauteurs.Sort();
	const double Mediane2 = Hauteurs.Num() > 0 ? Hauteurs[Hauteurs.Num() / 2] : 0.0;
	const double PartFranchissable = Colonnes > 0
		? 100.0 * ColonnesFranchissables / Colonnes : 0.0;
	const double PartDebout = Colonnes > 0
		? 100.0 * ColonnesDebout / Colonnes : 0.0;

	UE_LOG(LogTemp, Log,
		TEXT("[Sonde] surplombs : %.2f %% des colonnes traversees plus d'une fois ")
		TEXT("(%d au plus)  |  amplitude %.1f m, frequence %.4f"),
		PartSurplomb, TraverseesMax, R.OverhangAmplitudeM, R.OverhangFrequency);
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde] visieres : %.2f %% des colonnes ont un toit de 15 m au plus ")
		TEXT("sur un vide commencant a 20 m au plus sous la surface (arches, abris)"),
		Colonnes > 0 ? 100.0 * ColonnesVisiere / Colonnes : 0.0);
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde] vides : %d mesures, hauteur mediane %.1f m, la plus grande ")
		TEXT("%.1f m  |  %.2f %% des colonnes ont 2 m de libre, %.2f %% en ont 2,5"),
		Hauteurs.Num(), Mediane2, PlusGrandVideMonde, PartFranchissable, PartDebout);
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde] galeries : %.2f %% du volume de la bande est creuse, ")
		TEXT("%.2f %% des colonnes en rencontrent une  |  seuil %.3f, rayon %.1f m, ")
		TEXT("frequence %.4f"),
		PartAir, PartGalerieColonnes, R.CaveThreshold, R.CaveRadiusM, R.CaveFrequency);

	for (const FVector& P : Exemples)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Sonde]   galerie a (%.0f, %.0f, %.0f) m, soit %.0f cm monde"),
			P.X, P.Y, P.Z, P.Z * 100.0);
	}

	const FString Resume = FString::Printf(
		TEXT("vides : %.2f %% des colonnes ont 2 m de libre (%.2f %% en ont 2,5), ")
		TEXT("mediane %.1f m, max %.1f m | air %.2f %% de la bande | ")
		TEXT("ampl %.1f seuil %.3f rayon %.1f"),
		PartFranchissable, PartDebout, Mediane2, PlusGrandVideMonde, PartAir,
		R.OverhangAmplitudeM, R.CaveThreshold, R.CaveRadiusM);

	UE_LOG(LogTemp, Log, TEXT("[Sonde] %s"), *Resume);
	return Resume;
}

FString UWorldseedProbeLibrary::ProbeArches(int32 Seed, float HeightMeters,
	int32 ResolutionY, int32 Sites, float SousLeSommetM, float LargeurMaxM)
{
	WorldseedPipeline::ReloadRules();

	WorldseedPipeline::FResult World;
	FString Error;
	if (!WorldseedPipeline::Generate(Seed, HeightMeters, ResolutionY, World, Error))
	{
		return FString::Printf(TEXT("generation impossible : %s"), *Error);
	}

	const UWorldseedRules* Rules = WorldseedPipeline::GetRules(Error);
	if (!Rules)
	{
		return FString::Printf(TEXT("regles illisibles : %s"), *Error);
	}

	const FWorldseedDensityRules DR = FWorldseedDensityRules::FromRules(*Rules);
	const FWorldseedLithologyRules Litho = FWorldseedLithologyRules::FromRules(*Rules);
	FWorldseedDensity Density;
	Density.Init(World.Geometry, World.ElevationM, 1.0f, Seed, DR);
	Density.SetLithology(World.Lithology, Litho);

	// --- CHOISIR LES SITES : LES PLUS HAUTS, PAS AU HASARD -------------------
	//
	// Une lame ne se trouve pas en plaine. Prendre les sommets donne donc un
	// MAJORANT : si la forme n'existe pas la, elle n'existe nulle part.
	const int32 NX = World.Geometry.NX;
	const int32 NY = World.Geometry.NY;
	TArray<int32> Candidats;
	Candidats.Reserve(World.ElevationM.Num() / 8);
	for (int32 Cell = 0; Cell < World.ElevationM.Num(); ++Cell)
	{
		if (World.ElevationM[Cell] > 30.0f) { Candidats.Add(Cell); }
	}
	if (Candidats.Num() == 0)
	{
		return TEXT("aucune terre au-dessus de 30 m");
	}
	Candidats.Sort([&World](int32 A, int32 B)
	{
		return World.ElevationM[A] > World.ElevationM[B];
	});

	// Les sommets voisins decrivent la MEME montagne : sans ecart minimal on
	// mesurerait quatre cents fois le meme point et le releve ne vaudrait rien.
	const double EcartMinM = FMath::Max(200.0, World.Geometry.MetersPerPixel() * 4.0);
	TArray<FVector2D> Retenus;
	for (int32 Cell : Candidats)
	{
		if (Retenus.Num() >= Sites) { break; }
		const double X = (static_cast<double>(Cell % NX) / NX - 0.5)
			* World.Geometry.WidthM();
		const double Y = (static_cast<double>(Cell / NX) / NY - 0.5)
			* World.Geometry.HeightM;
		bool bTropPres = false;
		for (const FVector2D& P : Retenus)
		{
			if (FVector2D::DistSquared(P, FVector2D(X, Y)) < EcartMinM * EcartMinM)
			{
				bTropPres = true; break;
			}
		}
		if (!bTropPres) { Retenus.Emplace(X, Y); }
	}

	// --- MESURER LA CRETE, SUR LE CHAMP REEL --------------------------------
	//
	// A vingt metres sous le sommet, on marche vers l'exterieur dans huit
	// directions jusqu'a sortir de la roche. La largeur d'une direction est la
	// somme des deux rayons opposes ; la LARGEUR DE LA CRETE est la plus PETITE
	// des quatre, c'est-a-dire l'epaisseur de la lame et non sa longueur.
	const double PasM = 2.0;
	const double PorteeM = 400.0;
	TArray<double> Largeurs;
	Largeurs.Reserve(Retenus.Num());
	int32 Minces = 0;
	double LaPlusMince = PorteeM * 2.0;
	FVector2D OuEllEst = FVector2D::ZeroVector;

	int32 Vides = 0;
	for (const FVector2D& P : Retenus)
	{
		const double Sommet = Density.SurfaceHeightM(P.X, P.Y);
		const double Z = Sommet - SousLeSommetM;

		// UN SITE DONT LE CENTRE EST DEJA DE L'AIR N'EST PAS UNE LAME MINCE.
		// Le sommet vient de la grille MACRO, lue en bilineaire ; le champ
		// reel, lui, deplace la surface et peut y avoir ouvert une diaclase.
		// Compter ces sites comme des cretes de deux metres gonflerait le
		// releve d'exactement ce qu'on cherche a prouver -- deux populations
		// dans un meme chiffre, le piege qui a deja coute quatre corrections
		// inutiles sur le routage des galeries.
		if (Density.At(FVector(P.X, P.Y, Z)) > 0.0)
		{
			++Vides;
			continue;
		}

		double Rayons[8];
		for (int32 D = 0; D < 8; ++D)
		{
			const double Angle = D * (UE_DOUBLE_PI / 4.0);
			const double CX = FMath::Cos(Angle);
			const double CY = FMath::Sin(Angle);
			double R = 0.0;
			while (R < PorteeM)
			{
				const FVector Q(P.X + CX * (R + PasM), P.Y + CY * (R + PasM), Z);
				if (Density.At(Q) > 0.0) { break; }   // > 0 = air
				R += PasM;
			}
			Rayons[D] = R;
		}

		double Largeur = PorteeM * 2.0;
		for (int32 D = 0; D < 4; ++D)
		{
			Largeur = FMath::Min(Largeur, Rayons[D] + Rayons[D + 4] + PasM);
		}
		Largeurs.Add(Largeur);
		if (Largeur <= LargeurMaxM) { ++Minces; }
		if (Largeur < LaPlusMince) { LaPlusMince = Largeur; OuEllEst = P; }
	}

	if (Largeurs.Num() == 0)
	{
		return TEXT("aucun site valide : tous les centres sont dans l'air");
	}
	// --- Y A-T-IL DES FALAISES ? ---------------------------------------------
	//
	// C'EST LA QUESTION QUI DECIDE DES ARCHES, ET ELLE PASSE AVANT LA LAME.
	// Une arche se lit parce qu'on voit le ciel au travers ; il faut donc que
	// le sol TOMBE autour d'elle. Les photos montrent un monde de dunes lisses,
	// et une arche creusee dans une dune est invisible par construction -- le
	// point de vue a son altitude se retrouve DANS la roche.
	//
	// On mesure donc le DENIVELE LOCAL : la plus grande chute d'altitude sur
	// deux cellules, rapportee a la roche. Ce n'est pas la pente moyenne, qui
	// lisse tout : c'est la marche que l'oeil voit.
	{
		const int32 NXr = World.Geometry.NX;
		const int32 NYr = World.Geometry.NY;
		const double MailleM = World.Geometry.MetersPerPixel();

		TArray<int32> Cellules;
		TArray<double> SommeRelief;
		TArray<int32> AuDessusDe40;
		const int32 NR = FMath::Max(1, Litho.Catalogue.Num());
		Cellules.Init(0, NR);
		SommeRelief.Init(0.0, NR);
		AuDessusDe40.Init(0, NR);

		for (int32 J = 2; J < NYr - 2; ++J)
		{
			for (int32 I = 2; I < NXr - 2; ++I)
			{
				const int32 C = J * NXr + I;
				if (World.ElevationM[C] <= 0.0f) { continue; }

				float Chute = 0.0f;
				for (int32 DJ = -2; DJ <= 2; ++DJ)
				{
					for (int32 DI = -2; DI <= 2; ++DI)
					{
						const int32 V = (J + DJ) * NXr + (I + DI);
						Chute = FMath::Max(Chute,
							World.ElevationM[C] - World.ElevationM[V]);
					}
				}

				const uint8 R = World.Lithology.Id.IsValidIndex(C)
					? World.Lithology.Id[C] : 0;
				if (!Cellules.IsValidIndex(R)) { continue; }
				++Cellules[R];
				SommeRelief[R] += Chute;
				if (Chute > 40.0f) { ++AuDessusDe40[R]; }
			}
		}

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] --- denivele local, sur %.0f m ---"), MailleM * 2.0);
		for (int32 R = 0; R < NR; ++R)
		{
			if (Cellules[R] == 0) { continue; }
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]   %-10s chute moyenne %5.1f m, %5.2f %% au-dela de 40 m"),
				WorldseedLithology::Name(Litho, static_cast<uint8>(R)),
				SommeRelief[R] / Cellules[R],
				100.0 * AuDessusDe40[R] / Cellules[R]);
		}
	}

	// --- LE CHAMP DE LAMES : CE QU'IL COUVRE, ET OU -------------------------
	//
	// C'EST LE VRAI RISQUE DE CE TERME, bien plus que le nombre d'arches. Des
	// fentes de cinquante metres tous les soixante-dix, c'est un paysage de
	// canyons : credible sur quelques pour cent des terres, devastateur sur un
	// tiers. La part se MESURE, elle ne se deduit pas d'un seuil -- le projet
	// a deja paye cette confusion sur les diaclases, ou un seuil cense garder
	// 16 % n'en gardait que 1,59.
	{
		const FWorldseedFinRules FR = FWorldseedFinRules::FromRules(*Rules);
		const int32 NXl = World.Geometry.NX;
		const int32 NYl = World.Geometry.NY;

		int32 Terres = 0;
		int32 Gres = 0;
		int32 GresEnZone = 0;
		int32 GresHaut = 0;
		double SommeAltGres = 0.0;

		for (int32 I = 0; I < World.ElevationM.Num(); ++I)
		{
			if (World.ElevationM[I] <= 0.0f) { continue; }
			++Terres;

			const uint8 R = World.Lithology.Id.IsValidIndex(I)
				? World.Lithology.Id[I] : 0;
			const float Durete = Litho.Catalogue.IsValidIndex(R)
				? Litho.Catalogue[R].Hardness : 1.0f;
			if (Durete < FR.HardnessMin || Durete > FR.HardnessMax) { continue; }

			++Gres;
			SommeAltGres += World.ElevationM[I];
			if (World.ElevationM[I] > 63.0f) { ++GresHaut; }

			const double X = (static_cast<double>(I % NXl) / NXl - 0.5)
				* World.Geometry.WidthM();
			const double Y = (static_cast<double>(I / NXl) / NYl - 0.5)
				* World.Geometry.HeightM;
			if (WorldseedFins::Zone(X, Y, FR, Seed) > 0.0f) { ++GresEnZone; }
		}

		UE_LOG(LogTemp, Log, TEXT("[Worldseed] --- champ de lames ---"));
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   roche eligible : %.2f %% des terres, ")
			TEXT("altitude moyenne %.0f m, %.2f %% au-dessus de 63 m"),
			100.0 * Gres / FMath::Max(Terres, 1),
			(Gres > 0) ? SommeAltGres / Gres : 0.0,
			100.0 * GresHaut / FMath::Max(Gres, 1));
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   dont EN ZONE de lames : %.2f %% de cette roche, ")
			TEXT("soit %.2f %% des terres (seuil %.2f)"),
			100.0 * GresEnZone / FMath::Max(Gres, 1),
			100.0 * GresEnZone / FMath::Max(Terres, 1), FR.ZoneThreshold);
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   lames de %.0f m entre des fentes de %.0f m, ")
			TEXT("profondes de %.0f m"),
			FR.SpacingM - FR.SlotM, FR.SlotM, FR.DepthM);
	}

	// --- LE CONTROLE QUI DIT SI CE SONT DES ARCHES ---------------------------
	//
	// UNE ARCHE EST UNE OUVERTURE TRAVERSANTE SOUS UN PONT DE ROCHE CONTINU :
	// les deux moities du critere doivent etre verifiees, et separement. Un
	// trou qui ne traverse pas est un abri sous roche ; un trou sans roche
	// au-dessus est une echancrure. Les compter ensemble laisserait passer les
	// deux defauts.
	{
		int32 Traversantes = 0;
		int32 AvecPont = 0;
		int32 Bonnes = 0;

		for (const FWorldseedCaveArch& A : World.Caves.Arches)
		{
			// LE CHAMP NE CREUSE RIEN SI ON NE LUI DONNE PAS LE RESEAU. At(P)
			// sans primitives rend la roche PLEINE : la sonde mesurait donc le
			// monde d'AVANT le percement et rendait "0 traversante" quelles que
			// soient les arches. Meme piege que probe_voxel sans SetLithology,
			// et il ne se signale pas -- le chiffre est plausible.
			FWorldseedCaveLocal Local;
			World.Caves.Query(FBox(
				FVector(A.CentreM.X - A.EpaisseurM - 40.0,
					A.CentreM.Y - A.EpaisseurM - 40.0, A.CentreM.Z - 60.0),
				FVector(A.CentreM.X + A.EpaisseurM + 40.0,
					A.CentreM.Y + A.EpaisseurM + 40.0, A.CentreM.Z + 60.0)), Local);

			// 1. TRAVERSER : de l'air sur toute la longueur de l'axe, y compris
			//    au milieu de la lame -- c'est la que resterait un bouchon.
			const double Demi = A.EpaisseurM * 0.5 + A.RayonM;
			bool bTraverse = true;
			for (double T = -Demi; T <= Demi; T += 2.0)
			{
				const FVector Q(A.CentreM.X + A.TraversM.X * T,
					A.CentreM.Y + A.TraversM.Y * T, A.CentreM.Z);
				if (Density.At(Q, &Local) <= 0.0) { bTraverse = false; break; }
			}

			// 2. LE PONT, ET ON LE MESURE AU LIEU DE LE COCHER.
			//
			// Un booleen ne dit pas OU ni DE COMBIEN il casse : deux essais de
			// correction n'ont pas bouge le compte, ce qui est le signe qu'on
			// regle le mauvais bouton. On remonte donc depuis le sommet de
			// l'ouverture jusqu'a sortir de la roche, et on garde la plus
			// MINCE des travees -- c'est elle qui decide.
			const double ZHaut = A.CentreM.Z + 1.3 * A.RayonM;
			double PontMesure = TNumericLimits<double>::Max();
			double OuCasse = 0.0;
			// ON SONDE LE COL, PAS AU-DELA. A 0,4 d'epaisseur on se retrouve au
			// BORD du cap, la ou le sol tombe deja : il n'y a evidemment pas de
			// roche au-dessus, et le test rendait zero pont sur seize arches
			// pourtant surmontees de cinquante a quatre-vingts metres de
			// falaise. Le pont d'une arche marine enjambe le COL, pas le cap.
			for (double T = -A.EpaisseurM * 0.18; T <= A.EpaisseurM * 0.18; T += 4.0)
			{
				const double QX = A.CentreM.X + A.TraversM.X * T;
				const double QY = A.CentreM.Y + A.TraversM.Y * T;

				// IL FAUT D'ABORD SORTIR DU TROU. Partir du sommet calcule de
				// l'ouverture et compter la roche vers le haut donnait ZERO
				// partout : ce point est de l'AIR par construction, et l'union
				// lisse l'inflate encore du rayon de raccord. On mesurait donc
				// sa propre erreur, avec un chiffre identique sur les dix
				// arches -- ce qui aurait du alerter tout de suite.
				double Z = ZHaut;
				while (Z < ZHaut + 40.0
					&& Density.At(FVector(QX, QY, Z), &Local) > 0.0)
				{
					Z += 0.5;
				}

				// Puis l'epaisseur de roche franche jusqu'a l'air libre.
				double Epais = 0.0;
				while (Epais < 80.0
					&& Density.At(FVector(QX, QY, Z + Epais), &Local) <= 0.0)
				{
					Epais += 0.5;
				}
				if (Epais < PontMesure) { PontMesure = Epais; OuCasse = T; }
			}

			const bool bPont = (PontMesure >= 2.0);
			if (bTraverse) { ++Traversantes; }
			if (bPont) { ++AvecPont; }
			if (bTraverse && bPont) { ++Bonnes; }

			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]   arche (%.0f, %.0f) : %s, pont demande %.0f m, ")
				TEXT("MESURE %.0f m (au plus mince a %.0f m du centre)"),
				A.CentreM.X, A.CentreM.Y,
				bTraverse ? TEXT("traverse") : TEXT("NE TRAVERSE PAS"),
				A.PontM, PontMesure, OuCasse);
		}

		UE_LOG(LogTemp, Log, TEXT("[Worldseed] --- arches posees ---"));
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   %d posees : %d traversantes, %d avec un pont, ")
			TEXT("%d VRAIES ARCHES"),
			World.Caves.Arches.Num(), Traversantes, AvecPont, Bonnes);
	}

	Largeurs.Sort();
	auto Centile = [&Largeurs](double Q)
	{
		const int32 I = FMath::Clamp(
			FMath::RoundToInt(Q * (Largeurs.Num() - 1)), 0, Largeurs.Num() - 1);
		return Largeurs[I];
	};

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] --- lames de roche, %d sites ---"),
		Largeurs.Num());
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   largeur de crete a %.0f m sous le sommet : ")
		TEXT("min %.0f, p10 %.0f, mediane %.0f, p90 %.0f m"),
		SousLeSommetM, Largeurs[0], Centile(0.10), Centile(0.50), Centile(0.90));
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   sous %.0f m (une lame percable) : %d sites, soit %.2f %%"),
		LargeurMaxM, Minces, 100.0 * Minces / FMath::Max(Largeurs.Num(), 1));
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   la plus mince : %.0f m a (%.0f, %.0f) m"),
		LaPlusMince, OuEllEst.X, OuEllEst.Y);
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   ecartes, centre deja dans l'air : %d sites"), Vides);

	const FString Resume = FString::Printf(
		TEXT("%d sites, crete mediane %.0f m, %d sous %.0f m"),
		Largeurs.Num(), Centile(0.50), Minces, LargeurMaxM);
	return Resume;
}
