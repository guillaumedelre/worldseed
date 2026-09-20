// Worldseed - sonde des infractuosites : que trouve-t-on, et dans quel biome ?
//
// LA QUESTION EST LEGITIME, SA PREMISSE NE L'EST PAS. Il n'existe AUCUNE table
// qui dirait « ce biome porte ces cavites » -- c'est l'arbitrage B7 du
// proprietaire, et il est structurant : les cavites dependent de la LITHOLOGIE
// derivee de la tectonique, jamais du biome. Un massif calcaire est karstique
// sous une foret tropicale comme sous un maquis. Le climat n'entre QUE par la
// pluie, qui dissout. Faire decider l'etiquette de biome violerait la regle qui
// veut qu'elle ne serve qu'a lier des assets.
//
// CE QUI SE MESURE, ET QUI REPOND VRAIMENT : la coincidence. Roche, pluie et
// pente varient avec la latitude et le relief, donc les biomes aussi -- et le
// croisement dit ce qu'un joueur rencontre REELLEMENT dans chacun. C'est une
// consequence relevee, pas une regle posee, et le tableau change des que la
// lithologie ou le climat bougent. D'ou une sonde plutot qu'un document : un
// tableau fige serait faux au premier reglage suivant.

#include "Procedural/WorldseedProbeLibrary.h"

#include "Procedural/WorldseedProbeCommun.h"

#include "Procedural/WorldseedCaves.h"
#include "Procedural/WorldseedFins.h"
#include "Procedural/WorldseedPlateau.h"

namespace
{
	constexpr int32 NbBiomes = static_cast<int32>(EWorldseedBiome::Count);

	/** Ce qu'on compte pour un biome. */
	struct FReleve
	{
		// --- le terrain ---
		int32 Cellules = 0;
		double SommePluie = 0.0;
		double SommeTemp = 0.0;
		double SommeKarst = 0.0;

		// --- les formes PONCTUELLES, comptees a l'unite ---
		int32 Chambres = 0;
		int32 Avens = 0;
		int32 Dolines = 0;
		int32 Arches = 0;

		// --- les formes ETENDUES, comptees en cellules ---
		int32 Diaclases = 0;
		int32 Lames = 0;
		int32 Tables = 0;
		int32 Canyons = 0;
	};

	/** Le biome de la cellule qui contient ce point du monde, ou -1. */
	int32 BiomeEn(const WorldseedPipeline::FResult& W, double XM, double YM)
	{
		const FWorldseedGeometry& G = W.Geometry;
		double U = XM / G.WidthM() + 0.5;
		U -= FMath::FloorToDouble(U);
		const double V = FMath::Clamp(YM / G.HeightM + 0.5, 0.0, 1.0);

		const int32 I = FMath::Clamp(FMath::FloorToInt(U * G.NX), 0, G.NX - 1);
		const int32 J = FMath::Clamp(FMath::FloorToInt(V * G.NY), 0, G.NY - 1);
		const int32 Idx = J * G.NX + I;

		if (!W.Biomes.Index.IsValidIndex(Idx)) { return INDEX_NONE; }
		const int32 B = W.Biomes.Index[Idx];
		return (B >= 0 && B < NbBiomes) ? B : INDEX_NONE;
	}
}

FString UWorldseedProbeLibrary::ProbeInfractuosites(int32 Seed, float HeightMeters,
	int32 ResolutionY, int32 PasCellules)
{
	FWorldseedSonde S;
	if (!S.Preparer(Seed, HeightMeters, ResolutionY))
	{
		UE_LOG(LogTemp, Error, TEXT("[Sonde] %s"), *S.Erreur);
		return S.Erreur;
	}

	const WorldseedPipeline::FResult& W = S.World;
	const FWorldseedGeometry& G = W.Geometry;

	if (W.Biomes.Index.Num() != G.CellCount())
	{
		return TEXT("la carte des biomes n'a pas ete calculee");
	}

	const UWorldseedRules* Regles = S.Regles;
	if (!Regles) { return TEXT("regles illisibles"); }

	const FWorldseedFinRules FinRules = FWorldseedFinRules::FromRules(*Regles);
	const FWorldseedPlateauRules PlatRules = FWorldseedPlateauRules::FromRules(*Regles);

	TArray<FReleve> Par;
	Par.SetNum(NbBiomes);

	// --- LE TERRAIN, ET LES DEUX FORMES ETENDUES ----------------------------
	//
	// Un pas d'echantillonnage suffit : ces formes couvrent des taches de
	// plusieurs centaines de metres, et l'on ne cherche pas leur contour mais
	// leur PART. Le pas est le meme pour toutes, donc les colonnes se comparent.
	const int32 Pas = FMath::Clamp(PasCellules, 1, 16);
	int32 TerresEchantillonnees = 0;

	for (int32 J = 0; J < G.NY; J += Pas)
	{
		for (int32 I = 0; I < G.NX; I += Pas)
		{
			const int32 Idx = J * G.NX + I;
			if (W.ElevationM[Idx] <= 0.0f) { continue; }

			const int32 B = W.Biomes.Index[Idx];
			if (B < 0 || B >= NbBiomes) { continue; }

			FReleve& R = Par[B];
			++R.Cellules;
			++TerresEchantillonnees;

			if (W.Climate.PrecipMm.IsValidIndex(Idx))
			{
				R.SommePluie += W.Climate.PrecipMm[Idx];
			}
			if (W.Climate.TempMeanC.IsValidIndex(Idx))
			{
				R.SommeTemp += W.Climate.TempMeanC[Idx];
			}

			const double X = (static_cast<double>(I) / G.NX - 0.5) * G.WidthM();
			const double Y = (static_cast<double>(J) / G.NY - 0.5) * G.HeightM;

			// La roche se lit au catalogue, au PLUS PROCHE VOISIN : un identifiant
			// est une categorie, et interpoler entre du granite et du calcaire
			// donnerait du gres.
			if (W.Lithology.Id.IsValidIndex(Idx))
			{
				const uint8 Roche = W.Lithology.Id[Idx];
				if (S.Litho.Catalogue.IsValidIndex(Roche))
				{
					R.SommeKarst += S.Litho.Catalogue[Roche].Karstifiable;
				}
			}

			// LE CRITERE DU CHAMP, PAS UNE COPIE -- la sonde des diaclases
			// reimplementait le sien et validait donc une copie du mecanisme.
			if (S.Densite.DiaclaseZoneAt(X, Y) > 0.0f) { ++R.Diaclases; }

			// Une lame se compte a la FENTE qui la borde : SlotAt est positif
			// dans l'air de la fente. On sonde quelques metres sous la surface,
			// la ou la fente est franche.
			const double Surface = S.Densite.SurfaceHeightM(X, Y);
			if (WorldseedFins::SlotAt(X, Y, 8.0, FinRules, Seed) > 0.0) { ++R.Lames; }
			(void)Surface;
		}
	}

	// --- LES FORMES PONCTUELLES, A L'UNITE ET SANS ECHANTILLONNAGE ----------
	//
	// Une chambre ou une arche est un OBJET : on les compte toutes, il n'y a
	// aucune raison d'en estimer la densite.
	for (const FWorldseedCaveChamber& C : W.Caves.Chambers)
	{
		const int32 B = BiomeEn(W, C.CentreM.X, C.CentreM.Y);
		if (B != INDEX_NONE) { ++Par[B].Chambres; }
	}
	for (const FWorldseedCavePuits& P : W.Caves.Puits)
	{
		const int32 B = BiomeEn(W, P.CentreM.X, P.CentreM.Y);
		if (B == INDEX_NONE) { continue; }
		if (P.bDoline) { ++Par[B].Dolines; } else { ++Par[B].Avens; }
	}
	for (const FWorldseedCaveArch& A : W.Caves.Arches)
	{
		const int32 B = BiomeEn(W, A.CentreM.X, A.CentreM.Y);
		if (B != INDEX_NONE) { ++Par[B].Arches; }
	}

	// TABLES ET CANYONS SE COMPTENT EN SITES, PAS EN SURFACE. Ma premiere
	// version employait WorldseedPlateau::ZoneAt, qui ne rend que le MASQUE DE
	// REGION -- « ou la forme a le DROIT d.exister », dit son propre
	// commentaire. Elle annoncait 12 a 43 pour cent des terres quand les tables
	// n.en couvrent que 1,47 : un masque de region n.est pas une couverture, et
	// le depot avait deja paye cette confusion exacte en posant des sites de
	// table sur des MONTAGNES que la passe n.avait jamais touchees.
	//
	// On appelle donc la MEME fonction que la passe et que le terrain, qui
	// applique toutes les gardes physiques.
	TArray<FWorldseedPlateauSite> Tables;
	TArray<FWorldseedPlateauSite> Canyons;
	WorldseedPlateau::Sites(G, W.ElevationM, PlatRules, W.Lithology, S.Litho,
		W.Climate.PrecipMm, W.Climate.TempMeanC, Seed, Tables, &Canyons);

	for (const FWorldseedPlateauSite& T : Tables)
	{
		const int32 B = BiomeEn(W, T.CentreM.X, T.CentreM.Y);
		if (B != INDEX_NONE) { ++Par[B].Tables; }
	}
	for (const FWorldseedPlateauSite& C : Canyons)
	{
		const int32 B = BiomeEn(W, C.CentreM.X, C.CentreM.Y);
		if (B != INDEX_NONE) { ++Par[B].Canyons; }
	}

	// --- le tableau ----------------------------------------------------------
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde] === LES INFRACTUOSITES PAR BIOME ==="));
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   graine %d, %d x %d, %d cellules de terre echantillonnees ")
		TEXT("(un point sur %d par axe)"),
		Seed, G.NX, G.NY, TerresEchantillonnees, Pas);
	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   %-26s %8s %6s %6s %6s | %6s %5s %6s %5s | %7s %6s %6s %7s"),
		TEXT("biome"), TEXT("% terres"), TEXT("temp"), TEXT("pluie"), TEXT("karst"),
		TEXT("salles"), TEXT("avens"), TEXT("dolin."), TEXT("arch."),
		TEXT("%diacl."), TEXT("%lames"), TEXT("tables"), TEXT("canyons"));

	for (int32 B = 0; B < NbBiomes; ++B)
	{
		const FReleve& R = Par[B];
		if (R.Cellules == 0) { continue; }

		const double N = R.Cellules;
		UE_LOG(LogTemp, Log,
			TEXT("[Sonde]   %-26s %6.2f %% %5.1f %6.0f %6.2f | %6d %5d %6d %5d | %6.2f %% %5.2f %% %6d %7d"),
			WorldseedBiomes::Name(static_cast<EWorldseedBiome>(B)),
			100.0 * N / FMath::Max(TerresEchantillonnees, 1),
			R.SommeTemp / N,
			R.SommePluie / N,
			R.SommeKarst / N,
			R.Chambres, R.Avens, R.Dolines, R.Arches,
			100.0 * R.Diaclases / N,
			100.0 * R.Lames / N,
			R.Tables, R.Canyons);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Sonde]   Lecture : AUCUNE de ces colonnes n'est une regle. Le biome ")
		TEXT("ne decide de rien -- c'est la colonne KARST, qui vient de la ")
		TEXT("lithologie, qui explique les salles, les avens et les dolines ; et ")
		TEXT("la PLUIE, seule variable de climat qui entre, module la dissolution. ")
		TEXT("Les diaclases sont l'exact complement du karst : elles ne s'ouvrent ")
		TEXT("que dans la roche qui NE se dissout pas. Lames et plateaux tiennent ")
		TEXT("au gres et a l'aridite. Ce tableau est une COINCIDENCE mesuree, et ")
		TEXT("il change des que la lithologie ou le climat bougent."));

	int32 Dolines = 0;
	for (const FWorldseedCavePuits& P : W.Caves.Puits) { if (P.bDoline) { ++Dolines; } }
	int32 BiomesPeuples = 0;
	for (const FReleve& R : Par) { if (R.Cellules > 0) { ++BiomesPeuples; } }

	const FString Resume = FString::Printf(
		TEXT("%d chambres, %d avens, %d dolines, %d arches reparties sur %d biomes"),
		W.Caves.Chambers.Num(), W.Caves.Puits.Num() - Dolines, Dolines,
		W.Caves.Arches.Num(), BiomesPeuples);

	UE_LOG(LogTemp, Log, TEXT("[Sonde] %s"), *Resume);
	return Resume;
}
