// Worldseed - sondes de la roche : lithologie et diaclases.
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

FString UWorldseedProbeLibrary::ProbeLithology(int32 Seed, float HeightMeters,
	int32 ResolutionY)
{
	WorldseedPipeline::ReloadRules();

	WorldseedPipeline::FResult World;
	FString Error;
	if (!WorldseedPipeline::Generate(Seed, HeightMeters, ResolutionY, World, Error))
	{
		return FString::Printf(TEXT("generation impossible : %s"), *Error);
	}

	const int32 Count = World.Geometry.CellCount();
	if (!World.Lithology.IsValid(Count))
	{
		return TEXT("la lithologie n'a pas ete calculee");
	}

	const UWorldseedRules* Rules = WorldseedPipeline::GetRules(Error);
	if (!Rules)
	{
		return FString::Printf(TEXT("regles illisibles : %s"), *Error);
	}
	const FWorldseedLithologyRules Litho = FWorldseedLithologyRules::FromRules(*Rules);
	const int32 Roches = Litho.Catalogue.Num();

	TArray<int32> TotalMer;   TotalMer.Init(0, Roches);
	TArray<int32> TotalTerre; TotalTerre.Init(0, Roches);
	TArray<double> SommeAlt;  SommeAlt.Init(0.0, Roches);
	int32 Mer = 0;
	int32 Terre = 0;

	for (int32 I = 0; I < Count; ++I)
	{
		const int32 R = World.Lithology.Id[I];
		if (!TotalMer.IsValidIndex(R)) { continue; }
		if (World.ElevationM[I] > 0.0f)
		{
			++TotalTerre[R]; ++Terre;
			SommeAlt[R] += World.ElevationM[I];
		}
		else
		{
			++TotalMer[R]; ++Mer;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] --- lithologie ---"));
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   %-10s %10s %10s %14s %10s"),
		TEXT("roche"), TEXT("des terres"), TEXT("des mers"),
		TEXT("altitude moy."), TEXT("karst"));

	for (int32 R = 0; R < Roches; ++R)
	{
		if (TotalTerre[R] == 0 && TotalMer[R] == 0) { continue; }
		const double Alt = (TotalTerre[R] > 0) ? SommeAlt[R] / TotalTerre[R] : 0.0;
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   %-10s %9.2f %% %9.2f %% %11.0f m %10.2f"),
			WorldseedLithology::Name(Litho, static_cast<uint8>(R)),
			100.0 * TotalTerre[R] / FMath::Max(Terre, 1),
			100.0 * TotalMer[R] / FMath::Max(Mer, 1),
			Alt, Litho.Catalogue[R].Karstifiable);
	}

	// LA PART KARSTIFIABLE DES TERRES : c'est elle qui dira, quand les grottes
	// arriveront, quelle fraction du monde peut porter un reseau de dissolution.
	double Karst = 0.0;
	for (int32 R = 0; R < Roches; ++R)
	{
		Karst += Litho.Catalogue[R].Karstifiable * TotalTerre[R];
	}
	const double KarstPct = 100.0 * Karst / FMath::Max(Terre, 1);
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed]   terres karstifiables : %.2f %%"), KarstPct);

	// --- OU FAUT-IL ALLER VOIR ? ---------------------------------------------
	//
	// UNE MESURE NE REMPLACE PAS UN REGARD, et un regard demande une adresse.
	// Cette passe designe les endroits ou chaque chose se juge : le sommet pour
	// l'amplitude, le contraste granite / calcaire pour l'erosion
	// differentielle, la cote pour la mer et l'eau.
	{
		int32 Sommet = INDEX_NONE;
		float ZMax = -1e9f;
		int32 GraniteRaide = INDEX_NONE;
		float PenteGranite = -1.0f;
		int32 CalcairePlat = INDEX_NONE;
		float PenteCalcaire = 1e9f;
		int32 Cote = INDEX_NONE;

		TArray<float> GY, GX;
		WorldseedGrid::Gradient(World.ElevationM, World.Geometry.NX, World.Geometry.NY,
			World.Geometry.MetersPerPixel(), GY, GX);

		for (int32 I = 0; I < World.ElevationM.Num(); ++I)
		{
			const float Z = World.ElevationM[I];
			if (Z > ZMax) { ZMax = Z; Sommet = I; }
			if (Z <= 0.0f) { continue; }

			const float P = FMath::RadiansToDegrees(
				FMath::Atan(FMath::Sqrt(GX[I] * GX[I] + GY[I] * GY[I])));

			const uint8 R = World.Lithology.Id.IsValidIndex(I) ? World.Lithology.Id[I] : 255;
			if (!Litho.Catalogue.IsValidIndex(R)) { continue; }
			const float D = Litho.Catalogue[R].Hardness;

			if (D > 0.9f && Z > 300.0f && P > PenteGranite) { PenteGranite = P; GraniteRaide = I; }
			if (D < 0.5f && Z > 20.0f && Z < 200.0f && P < PenteCalcaire)
			{
				PenteCalcaire = P; CalcairePlat = I;
			}
			if (Cote == INDEX_NONE && Z > 2.0f && Z < 12.0f) { Cote = I; }
		}

		auto Dire = [&](const TCHAR* Nom, int32 I, const TCHAR* Pourquoi)
		{
			if (I == INDEX_NONE) { return; }
			const double X = (static_cast<double>(I % World.Geometry.NX) / World.Geometry.NX - 0.5)
				* World.Geometry.WidthM();
			const double Y = (static_cast<double>(I / World.Geometry.NX) / World.Geometry.NY - 0.5)
				* World.Geometry.HeightM;
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] A VOIR : %-22s (%.0f, %.0f) m, altitude %.0f m -- %s"),
				Nom, X, Y, World.ElevationM[I], Pourquoi);
		};

		Dire(TEXT("le sommet"), Sommet, TEXT("l'amplitude du nouveau relief"));
		Dire(TEXT("granite le plus raide"), GraniteRaide,
			TEXT("la roche dure qui tient la pente"));
		Dire(TEXT("calcaire le plus plat"), CalcairePlat,
			TEXT("la roche tendre decapee, a comparer au granite"));
		Dire(TEXT("la cote"), Cote, TEXT("le fond marin releve et la texture d'eau"));
	}

	// --- LES DIACLASES, la cavite de la roche qui NE se dissout PAS ----------
	//
	// LA MESURE QUI COMPTE N'EST PAS LE VOLUME CREUSE. Une fente de deux metres
	// dans une bande de quarante-cinq ne peut peser qu'une fraction de pour
	// cent du volume, et ce chiffre ne dirait rien de ce qu'on veut savoir :
	// est-ce qu'on peut y ENTRER, et est-ce qu'elle MENE quelque part. On
	// echantillonne donc des COLONNES verticales et on compte celles qui sont
	// ouvertes sur une hauteur d'homme, puis la longueur horizontale continue
	// du vide a mi-bande.
	{
		const FWorldseedDensityRules DR = FWorldseedDensityRules::FromRules(*Rules);

		FWorldseedDensity Champ;
		Champ.Init(World.Geometry, World.ElevationM, 1.0f, Seed, DR);
		Champ.SetLithology(World.Lithology, Litho);

		// ON VISE LE COEUR D'UNE ZONE OUVERTE, ET LE CRITERE EST LE MASQUE,
		// PAS L'ALTITUDE. Premiere version : je prenais la cellule insoluble la
		// plus HAUTE. Comme le masque n'ouvre que 5 % de cette roche, ce point
		// avait 95 % de chances d'etre dans une zone FERMEE -- et il l'etait.
		// Le temoin l'a dit sans appel : avec et sans diaclases, 2902 colonnes
		// dans les deux cas, au chiffre pres. La sonde mesurait le bruit des
		// galeries et rien d'autre.
		FVector2D Centre(0.0, 0.0);
		double MeilleureAlt = -1e9;
		float MeilleurMasque = -1e9f;
		for (int32 J = 0; J < World.Geometry.NY; J += 4)
		{
			for (int32 I = 0; I < World.Geometry.NX; I += 4)
			{
				const int32 Idx = J * World.Geometry.NX + I;
				if (World.ElevationM[Idx] <= 20.0f) { continue; }
				const uint8 Id = World.Lithology.Id[Idx];
				if (!Litho.Catalogue.IsValidIndex(Id)) { continue; }
				if (Litho.Catalogue[Id].Karstifiable > 0.05f) { continue; }

				const double X = (static_cast<double>(I) / World.Geometry.NX - 0.5)
					* World.Geometry.WidthM();
				const double Y = (static_cast<double>(J) / World.Geometry.NY - 0.5)
					* World.Geometry.HeightM;

				// LE CRITERE DU CHAMP, PAS UNE COPIE : on cherche l.endroit ou
				// les fentes s.ouvrent LE PLUS, donc la garde complete -- tirage,
				// masque et pente -- et non le seul bruit. Viser le maximum du
				// bruit enverrait regarder une region que le tirage a ecartee.
				const float Masque = Champ.DiaclaseZoneAt(X, Y);

				if (Masque > MeilleurMasque)
				{
					MeilleurMasque = Masque;
					MeilleureAlt = World.ElevationM[Idx];
					Centre = FVector2D(X, Y);
				}
			}
		}

		// COMBIEN DE MONDE CETTE FORME TOUCHE-T-ELLE, ET QUELLE GARDE RETIENT ?
		//
		// UN ENTONNOIR, ET NON UNE COURBE DE SEUIL. Tant qu'il n'y avait qu'un
		// masque de bruit, relever sa courbe suffisait. Depuis qu'un tirage de
		// region et un seuil de pente s'y composent, une couverture trop faible
		// peut venir de TROIS gardes, et sans le compte de chacune on regle au
		// hasard la mauvaise -- ce que ce depot a paye quatre fois de suite sur
		// le routage des galeries, puis evite sur les plateaux grace a
		// exactement ce releve-ci.
		//
		// Chaque ligne est CUMULATIVE : elle compte ce qui a passe toutes les
		// gardes precedentes ET la sienne. La derniere est la couverture reelle.
		{
			int32 TerresInsolubles = 0;
			int32 ApresTirage = 0;
			int32 ApresMasque = 0;
			int32 Ouvertes = 0;

			for (int32 I = 0; I < World.Geometry.CellCount(); ++I)
			{
				if (World.ElevationM[I] <= 0.0f) { continue; }
				const uint8 Id = World.Lithology.Id[I];
				if (!Litho.Catalogue.IsValidIndex(Id)) { continue; }
				if (Litho.Catalogue[Id].Karstifiable > 0.05f) { continue; }
				++TerresInsolubles;

				const int32 Ix = I % World.Geometry.NX;
				const int32 Jy = I / World.Geometry.NX;
				const double X = (static_cast<double>(Ix) / World.Geometry.NX - 0.5)
					* World.Geometry.WidthM();
				const double Y = (static_cast<double>(Jy) / World.Geometry.NY - 0.5)
					* World.Geometry.HeightM;

				// LES GARDES DU CHAMP, UNE A UNE, ET AUCUNE REECRITE ICI. La
				// version precedente recopiait le Perlin « a la lettre » : elle
				// validait donc une COPIE du mecanisme, et elle aurait rendu
				// l'ancien chiffre sans broncher des la premiere garde ajoutee
				// au champ. C'est le « temoin non branche » du depot, et il ne
				// se signale jamais.
				if (Champ.DiaclaseTirageAt(X, Y) <= 0.0f) { continue; }
				++ApresTirage;

				if (Champ.DiaclaseMasqueAt(X, Y) <= 0.0f) { continue; }
				++ApresMasque;

				if (Champ.DiaclasePenteAt(X, Y) <= 0.0f) { continue; }
				++Ouvertes;
			}

			const double Base = FMath::Max(TerresInsolubles, 1);
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]   diaclases, entonnoir sur la roche insoluble emergee :"));
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]     roche insoluble emergee   %8d   100,00 %%"),
				TerresInsolubles);
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]     + tirage de region        %8d   %6.2f %%   ")
				TEXT("(maille %.0f m, part demandee %.2f)"),
				ApresTirage, 100.0 * ApresTirage / Base,
				DR.JointRegionM, DR.JointRegionPart);
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]     + masque de bruit         %8d   %6.2f %%   (seuil %.2f)"),
				ApresMasque, 100.0 * ApresMasque / Base, DR.JointZoneThreshold);
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]     + pente                   %8d   %6.2f %%   ")
				TEXT("(au-dela de %.0f deg)"),
				Ouvertes, 100.0 * Ouvertes / Base, DR.JointPenteMinDeg);

			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]   diaclases : %.2f %% de la roche insoluble emergee ")
				TEXT("porte un reseau ouvert, soit %.2f %% DES TERRES"),
				100.0 * Ouvertes / Base,
				100.0 * Ouvertes / FMath::Max(Terre, 1));
		}

		if (MeilleureAlt < -1e8)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]   diaclases : aucune roche insoluble emergee"));
		}
		else
		{
			// UN CARRE DE 400 m AU PAS DE 2 m, MESURE DEUX FOIS : une fois avec
			// les diaclases, une fois sans. C'EST LE TEMOIN QUI MANQUAIT, et son
			// absence a produit un chiffre entierement faux -- voir plus bas.
			const double Pas = 2.0;
			const int32 N = 200;

			struct FReleve
			{
				int32 Praticables = 0;
				double HauteurMax = 0.0;
				int32 LongueurMax = 0;
			};

			// PREMIERE VERSION DE CETTE MESURE, ET ELLE ETAIT FAUSSE. Elle
			// descendait depuis SurfaceHeightM -- le relief MACRO -- et comptait
			// comme vide tout ce qu'elle trouvait dessous. Or le champ deplace
			// la surface de plusieurs metres : huit d'amplitude plus vingt-cinq
			// de deplacement horizontal. Partout ou le sol reel passe sous le
			// relief macro, elle comptait de l'AIR ORDINAIRE comme une fissure.
			// Elle annoncait 15,85 % de colonnes ouvertes alors que la maille,
			// en jeu, n'en montrait aucune -- et ses chiffres n'avaient pas
			// bouge d'un iota quand le masque de zone avait change, ce qui
			// aurait du suffire a la soupconner.
			auto Balayer = [&](const FWorldseedDensityRules& Regles) -> FReleve
			{
				FWorldseedDensity C;
				C.Init(World.Geometry, World.ElevationM, 1.0f, Seed, Regles);
				C.SetLithology(World.Lithology, Litho);

				FReleve R;
				for (int32 Jy = 0; Jy < N; ++Jy)
				{
					int32 LongueurCourante = 0;
					for (int32 Ix = 0; Ix < N; ++Ix)
					{
						const double X = Centre.X + (Ix - N / 2) * Pas;
						const double Y = Centre.Y + (Jy - N / 2) * Pas;
						const double Haut = C.SurfaceHeightM(X, Y) + 40.0;
						const double Bas = Haut - 40.0 - Regles.JointDepthM;

						const double PasZ = 0.5;
						double Meilleur = 0.0;
						double Vide = 0.0;
						bool bSolTrouve = false;

						for (double Z = Haut; Z > Bas; Z -= PasZ)
						{
							const double V = C.At(FVector(X, Y, Z));
							if (!bSolTrouve)
							{
								// On ne compte qu'APRES avoir traverse la roche :
								// tout ce qui precede est le ciel.
								if (V <= 0.0) { bSolTrouve = true; }
								continue;
							}
							if (V > 0.0) { Vide += PasZ; Meilleur = FMath::Max(Meilleur, Vide); }
							else { Vide = 0.0; }
						}

						R.HauteurMax = FMath::Max(R.HauteurMax, Meilleur);
						if (Meilleur >= 2.0)
						{
							++R.Praticables;
							++LongueurCourante;
							R.LongueurMax = FMath::Max(R.LongueurMax, LongueurCourante);
						}
						else
						{
							LongueurCourante = 0;
						}
					}
				}
				return R;
			};

			const double t0 = FPlatformTime::Seconds();
			const FReleve Avec = Balayer(DR);

			FWorldseedDensityRules Sans = DR;
			Sans.JointApertureM = 0.0f;
			const FReleve SansD = Balayer(Sans);

			const int32 Colonnes = N * N;
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]   diaclases a (%.0f, %.0f) m, altitude %.0f m, ")
				TEXT("masque %.2f pour un seuil de %.2f, carre de %d m :"),
				Centre.X, Centre.Y, MeilleureAlt, MeilleurMasque, DR.JointZoneThreshold,
				static_cast<int32>(N * Pas));
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]     AVEC : %d colonnes sur %d ouvertes sur 2 m au moins ")
				TEXT("(%.2f %%), vide le plus haut %.1f m, fente la plus longue %.0f m"),
				Avec.Praticables, Colonnes, 100.0 * Avec.Praticables / Colonnes,
				Avec.HauteurMax, Avec.LongueurMax * Pas);
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]     SANS (temoin) : %d colonnes (%.2f %%), ")
				TEXT("vide le plus haut %.1f m, fente la plus longue %.0f m"),
				SansD.Praticables, 100.0 * SansD.Praticables / Colonnes,
				SansD.HauteurMax, SansD.LongueurMax * Pas);
			UE_LOG(LogTemp, Log, TEXT("[Worldseed]     les deux balayages en %.0f ms"),
				1000.0 * (FPlatformTime::Seconds() - t0));
		}
	}

	return FString::Printf(TEXT("%d roches ; %.2f %% des terres karstifiables ; detail au journal"),
		Roches, KarstPct);
}
