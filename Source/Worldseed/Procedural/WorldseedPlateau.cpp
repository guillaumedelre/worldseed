// Worldseed - le plateau disseque : les mesas sont ce qu'il en reste.

#include "Procedural/WorldseedPlateau.h"

#include "Procedural/WorldseedFins.h"
#include "Procedural/WorldseedFlow.h"
#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedLithology.h"
#include "Procedural/WorldseedPerlin.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedStrata.h"
#include "Procedural/WorldseedTrace.h"

FWorldseedPlateauRules FWorldseedPlateauRules::FromRules(const UWorldseedRules& Rules)
{
	FWorldseedPlateauRules Out;

	const TCHAR* TAB = TEXT("tables");
	auto Num = [&Rules, TAB](const TCHAR* Key, double Fallback)
	{
		return static_cast<float>(Rules.Num(TAB, Key, Fallback));
	};

	Out.ScarpM = Num(TEXT("escarpementM"), 170.0);
	Out.ReachM = Num(TEXT("porteeM"), 900.0);
	Out.LocalReliefMaxM = Num(TEXT("reliefLocalMaxM"), 320.0);
	Out.PrecipMaxMm = Num(TEXT("pluieMaxMm"), 520.0);
	Out.TempMinC = Num(TEXT("temperatureMinC"), 0.0);
	Out.HardnessMin = Num(TEXT("dureteMin"), 0.25);
	Out.HardnessMax = Num(TEXT("dureteMax"), 0.70);
	Out.MinElevationM = Num(TEXT("altitudeMinM"), 60.0);
	Out.FloorMinM = Num(TEXT("fondMinM"), 5.0);
	Out.ZoneFrequency = Num(TEXT("zoneFrequence"), 0.00012);
	Out.ZoneThreshold = Num(TEXT("zoneSeuil"), 0.30);
	Out.DrainStart = Num(TEXT("drainageDebut"), 3.4);
	Out.FloorWidthM = Num(TEXT("largeurFondM"), 45.0);
	Out.WallCells = Num(TEXT("paroiMailles"), 1.0);
	Out.SlotShare = Num(TEXT("fentePart"), 0.45);
	Out.CapHardnessMin = Num(TEXT("chapiteauDureteMin"), 0.40);
	Out.Strength = Num(TEXT("force"), 1.0);
	return Out;
}

// NOM DE NAMESPACE EXPLICITE, PAS ANONYME. Le depot a paye DEUX FOIS le piege
// du build unifie : UBT concatene les .cpp en une seule unite de traduction, ou
// deux namespaces ANONYMES n'en font qu'un. La collision avait alors casse la
// compilation de fichiers que personne n'avait touches.
namespace WorldseedPlateauDetail
{
	enum class EFenetre : uint8 { Max, Min, Moyenne };

	/**
	 * Fenetre glissante SEPARABLE : deux passes 1D valent une fenetre carree,
	 * et le cout suit le rayon au lieu de son carre. La longitude s'enroule,
	 * les lignes se bornent -- un pole n'a pas de voisin au-dela.
	 */
	static void Fenetre(const TArray<float>& Source, int32 NX, int32 NY,
		int32 Rayon, EFenetre Mode, TArray<float>& Out)
	{
		const int32 Count = NX * NY;
		TArray<float> Tampon;
		Tampon.SetNumUninitialized(Count);
		Out.SetNumUninitialized(Count);

		const float Large = (Mode == EFenetre::Max) ? -1e9f : 1e9f;
		const float Diviseur = static_cast<float>(2 * Rayon + 1);

		for (int32 J = 0; J < NY; ++J)
		{
			for (int32 I = 0; I < NX; ++I)
			{
				float A = (Mode == EFenetre::Moyenne) ? 0.0f : Large;
				for (int32 D = -Rayon; D <= Rayon; ++D)
				{
					const int32 K = ((I + D) % NX + NX) % NX;
					const float V = Source[J * NX + K];
					A = (Mode == EFenetre::Max) ? FMath::Max(A, V)
						: (Mode == EFenetre::Min) ? FMath::Min(A, V) : A + V;
				}
				Tampon[J * NX + I] = (Mode == EFenetre::Moyenne) ? A / Diviseur : A;
			}
		}
		for (int32 J = 0; J < NY; ++J)
		{
			for (int32 I = 0; I < NX; ++I)
			{
				float A = (Mode == EFenetre::Moyenne) ? 0.0f : Large;
				for (int32 D = -Rayon; D <= Rayon; ++D)
				{
					const int32 L = FMath::Clamp(J + D, 0, NY - 1);
					const float V = Tampon[L * NX + I];
					A = (Mode == EFenetre::Max) ? FMath::Max(A, V)
						: (Mode == EFenetre::Min) ? FMath::Min(A, V) : A + V;
				}
				Out[J * NX + I] = (Mode == EFenetre::Moyenne) ? A / Diviseur : A;
			}
		}
	}
}

void WorldseedPlateau::Surfaces(const FWorldseedGeometry& Geometry,
	const TArray<float>& ElevationM, const FWorldseedPlateauRules& Rules,
	TArray<float>& OutPlateau, TArray<float>& OutReliefLocalM,
	TArray<float>* OutSolM)
{
	using namespace WorldseedPlateauDetail;

	const int32 NX = Geometry.NX;
	const int32 NY = Geometry.NY;
	const int32 Count = Geometry.CellCount();
	if (ElevationM.Num() != Count) { return; }

	const float MailleM = FMath::Max(Geometry.MetersPerPixel(), 1e-3f);
	const int32 Rayon = FMath::Clamp(
		FMath::CeilToInt(Rules.ReachM / MailleM), 1, 24);

	TArray<float> Toit;
	Fenetre(ElevationM, NX, NY, Rayon, EFenetre::Max, Toit);
	Fenetre(Toit, NX, NY, Rayon, EFenetre::Moyenne, OutPlateau);

	// Le denivele local dit si une plaine existe ici. Sur un versant de
	// montagne il n'y a rien a dissequer, et raboter ses sommets donnerait un
	// relief mutile plutot que des tables.
	TArray<float> Sol;
	Fenetre(ElevationM, NX, NY, Rayon, EFenetre::Min, Sol);

	OutReliefLocalM.SetNumUninitialized(Count);
	for (int32 I = 0; I < Count; ++I)
	{
		OutReliefLocalM[I] = Toit[I] - Sol[I];
	}
	if (OutSolM) { *OutSolM = MoveTemp(Sol); }
}

float WorldseedPlateau::ZoneAt(double X, double Y,
	const FWorldseedPlateauRules& Rules, int32 Seed)
{
	const float B = WorldseedPerlin::Perlin(
		static_cast<float>(X) * Rules.ZoneFrequency,
		static_cast<float>(Y) * Rules.ZoneFrequency, Seed + 7717);
	if (B <= Rules.ZoneThreshold) { return 0.0f; }
	return FMath::Min(1.0f, (B - Rules.ZoneThreshold) / 0.2f);
}

bool WorldseedPlateau::Eligible(int32 Cell, const FWorldseedPlateauRules& Rules,
	const FWorldseedLithology& Lithology,
	const FWorldseedLithologyRules& LithoRules,
	const TArray<float>& PrecipMm, const TArray<float>& TempMeanC,
	int32 Count)
{
	// UNE DONNEE ABSENTE RESTE SANS EFFET, jamais un effet arbitraire : sans
	// carte de pluie, de roche ou de temperature, la garde correspondante ne
	// mord pas et le comportement est celui d'avant, a l'identique.
	if (PrecipMm.Num() == Count && PrecipMm[Cell] > Rules.PrecipMaxMm)
	{
		return false;
	}

	// LE FROID FERME LA FORME, ET C'EST LE MECANISME QUI LE DIT. Une corniche
	// ne tient que parce que le talus tendre sous elle est EMPORTE par le
	// ruissellement ; sous zero en moyenne annuelle l'eau est prise, et les
	// processus qui dominent -- gelifraction, solifluxion -- arrondissent au
	// lieu de trancher. Sans cette garde, l'aridite polaire passait pour de
	// l'aridite desertique : 14 tables sur 14 et 9 canyons sur 12 se
	// retrouvaient sous la CALOTTE GLACIAIRE, invisibles et injouables.
	if (TempMeanC.Num() == Count && TempMeanC[Cell] < Rules.TempMinC)
	{
		return false;
	}

	if (Lithology.IsValid(Count))
	{
		const uint8 Id = Lithology.Id[Cell];
		const float Durete = LithoRules.Catalogue.IsValidIndex(Id)
			? LithoRules.Catalogue[Id].Hardness : 1.0f;
		if (Durete < Rules.HardnessMin || Durete > Rules.HardnessMax)
		{
			return false;
		}
	}

	return true;
}

void WorldseedPlateau::Sites(const FWorldseedGeometry& Geometry,
	const TArray<float>& ElevationM, const FWorldseedPlateauRules& Rules,
	const FWorldseedLithology& Lithology, const FWorldseedLithologyRules& Litho,
	const TArray<float>& PrecipMm, const TArray<float>& TempMeanC,
	int32 Seed, TArray<FWorldseedPlateauSite>& OutSites,
	TArray<FWorldseedPlateauSite>* OutCanyons)
{
	OutSites.Reset();
	if (OutCanyons) { OutCanyons->Reset(); }

	const int32 NX = Geometry.NX;
	const int32 NY = Geometry.NY;
	const int32 Count = Geometry.CellCount();
	if (!Rules.IsActive() || ElevationM.Num() != Count) { return; }

	// SUR LE RELIEF FINI, ET C'EST TOUT LE POINT. Une table, c'est une cellule
	// restee AU NIVEAU du plateau alors que son voisinage est tombe. La
	// chercher sur le relief d'AVANT la passe designerait au contraire les
	// sommets que la passe s'apprete a raboter.
	TArray<float> Plateau;
	TArray<float> Relief;
	TArray<float> Sol;
	Surfaces(Geometry, ElevationM, Rules, Plateau, Relief, &Sol);
	if (Plateau.Num() != Count || Sol.Num() != Count) { return; }

	const double LargeurM = Geometry.WidthM();
	const double HauteurM = Geometry.HeightM;

	// LES MEMES GARDES QUE LA PASSE. Un site designe hors de la roche
	// sedimentaire ou hors du climat aride pointe un endroit que la passe
	// n a jamais touche -- et l image l a dit deux fois : montagnes
	// enneigees, versants cotiers verts.
	// LA MEME GARDE QUE CELLE QUI CREUSE, appelee et non recopiee.
	auto EstEligible = [&](int32 C)
	{
		return Eligible(C, Rules, Lithology, Litho, PrecipMm, TempMeanC, Count);
	};

	struct FCandidat { int32 C; float Relief; };
	TArray<FCandidat> Candidats;

	for (int32 J = 0; J < NY; ++J)
	{
		for (int32 I = 0; I < NX; ++I)
		{
			const int32 C = J * NX + I;
			if (ElevationM[C] <= Rules.MinElevationM) { continue; }
			if (!EstEligible(C)) { continue; }
			if (ElevationM[C] < Plateau[C] - 12.0f) { continue; }

			// --- TROIS GARDES, ET LES DEUX DERNIERES ONT ETE PAYEES A L'IMAGE
			//
			// La premiere version ne testait que le masque de region, et le
			// resultat a ete sans appel : les sites designes avaient leur
			// sommet a 700 et 1064 metres, c'est-a-dire des MONTAGNES que la
			// passe n'avait jamais touchees. Un masque de region dit ou la
			// forme a le droit d'exister, il ne dit pas qu'elle existe.
			//
			// Une table se reconnait a deux choses que la geometrie suffit a
			// mesurer, sans relire ni la roche ni la pluie : son sommet est
			// PLAT, et son denivele local vaut a peu pres un escarpement. Un
			// pic a le meme denivele mais une pente forte ; une montagne a la
			// meme platitude nulle part mais un denivele bien plus grand.
			if (Relief[C] < 0.5f * Rules.ScarpM) { continue; }
			if (Relief[C] > 1.6f * Rules.ScarpM) { continue; }

			// LE PIED D UNE TABLE EST SUR LA TERRE, JAMAIS SOUS LA MER, et
			// cette garde a ete payee a l image : une vue entierement bleue,
			// camera NOYEE. Le site avait 139 m de sommet pour 224 m de relief
			// local -- sa fenetre de mesure mordait sur la mer, donc son pied
			// calcule tombait a -45 m. Un denivele local qui depasse l altitude
			// ne decrit pas une paroi de table, il decrit une cote.
			if (Sol[C] < 20.0f) { continue; }

			const float Dx = (ElevationM[J * NX + ((I + 1) % NX)]
				- ElevationM[J * NX + ((I - 1 + NX) % NX)]) * 0.5f;
			const float Dy = (ElevationM[FMath::Min(J + 1, NY - 1) * NX + I]
				- ElevationM[FMath::Max(J - 1, 0) * NX + I]) * 0.5f;
			const float MailleM = FMath::Max(Geometry.MetersPerPixel(), 1e-3f);
			const float PenteDeg = FMath::RadiansToDegrees(
				FMath::Atan(FMath::Sqrt(Dx * Dx + Dy * Dy) / MailleM));
			if (PenteDeg > 8.0f) { continue; }

			const double X = (static_cast<double>(I) / NX - 0.5) * LargeurM;
			const double Y = (static_cast<double>(J) / NY - 0.5) * HauteurM;
			if (ZoneAt(X, Y, Rules, Seed) <= 0.0f) { continue; }

			Candidats.Add({ C, Relief[C] });
		}
	}

	// LES PLUS HAUTES D'ABORD, ET BIEN SEPAREES. Sans l'ecart minimal, les
	// premiers sites seraient vingt cellules voisines d'une MEME table -- c'est
	// exactement le defaut des bouches de grotte en double, ou deux chambres
	// elisaient la meme paroi. On ecarte donc PENDANT le choix, et non apres :
	// refuser a la fin ferait perdre le site, ecarter en cours de route laisse
	// la recherche trouver la table suivante.
	Candidats.Sort([](const FCandidat& A, const FCandidat& B)
		{ return A.Relief > B.Relief; });

	const double EcartMin = FMath::Max(2.0f * Rules.ReachM, 500.0f);
	for (const FCandidat& K : Candidats)
	{
		if (OutSites.Num() >= 24) { break; }

		const int32 I = K.C % NX;
		const int32 J = K.C / NX;
		const double X = (static_cast<double>(I) / NX - 0.5) * LargeurM;
		const double Y = (static_cast<double>(J) / NY - 0.5) * HauteurM;

		bool bTropPres = false;
		for (const FWorldseedPlateauSite& S : OutSites)
		{
			if (FVector2D::Distance(S.CentreM, FVector2D(X, Y)) < EcartMin)
			{
				bTropPres = true;
				break;
			}
		}
		if (bTropPres) { continue; }

		FWorldseedPlateauSite S;
		S.CentreM = FVector2D(X, Y);
		S.AltitudeM = ElevationM[K.C];
		S.EscarpementM = K.Relief;
		OutSites.Add(S);
	}

	// --- LES CANYONS, L'AUTRE FACE DU MEME OBJET ----------------------------
	//
	// MESA ET CANYON SONT CE QUI RESTE ET CE QUI A ETE ENLEVE, donc ils se
	// trouvent avec les MEMES surfaces -- d'ou leur recherche ici et non dans
	// une seconde fonction, qui aurait refait le maximum glissant pour rien.
	//
	// Un fond de canyon, c'est une cellule TOMBEE loin sous le plateau, avec
	// de la paroi tout autour. Le critere est donc l'inverse exact de celui
	// d'une table : la table est restee AU NIVEAU du plateau, le canyon en est
	// descendu.
	if (OutCanyons)
	{
		// --- ON CLASSE PAR CHUTE, PAS PAR PROFONDEUR ------------------------
		//
		// DEFAUT TROUVE A L'IMAGE, APRES QUATRE TOURNEES. Les sites etaient
		// classes par CREUX -- l'ecart entre la surface et le plateau -- ce qui
		// designe les points les plus BAS. Or profond n'est pas escarpe : les
		// vues tombaient systematiquement sur des pentes douces, alors que la
		// mesure disait qu'il y avait bien 660 cellules a plus de 45 degres
		// dans la zone. Les parois EXISTAIENT, je photographiais ailleurs.
		//
		// Le bon critere etait sous les yeux : le selecteur de falaises
		// marines classe par CHUTE LOCALE, et ses vues sont les seules de la
		// tournee a montrer une vraie paroi. On reprend le meme, gardes de la
		// passe en plus.
		struct FFond { int32 C; float Chute; FVector2D VersLeBas; };
		TArray<FFond> Fonds;

		for (int32 J = 2; J < NY - 2; ++J)
		{
			for (int32 I = 2; I < NX - 2; ++I)
			{
				const int32 C = J * NX + I;
				if (ElevationM[C] <= Rules.MinElevationM) { continue; }
				if (!EstEligible(C)) { continue; }

				const double X = (static_cast<double>(I) / NX - 0.5) * LargeurM;
				const double Y = (static_cast<double>(J) / NY - 0.5) * HauteurM;
				if (ZoneAt(X, Y, Rules, Seed) <= 0.0f) { continue; }

				float Chute = 0.0f;
				FIntPoint Vers = FIntPoint::ZeroValue;
				for (int32 DJ = -2; DJ <= 2; ++DJ)
				{
					for (int32 DI = -2; DI <= 2; ++DI)
					{
						const int32 V = (J + DJ) * NX + (((I + DI) % NX + NX) % NX);
						const float D = ElevationM[C] - ElevationM[V];
						if (D > Chute) { Chute = D; Vers = FIntPoint(DI, DJ); }
					}
				}
				if (Chute < 0.35f * Rules.ScarpM) { continue; }

				Fonds.Add({ C, Chute,
					FVector2D(Vers.X, Vers.Y).GetSafeNormal() });
			}
		}

		Fonds.Sort([](const FFond& A, const FFond& B) { return A.Chute > B.Chute; });

		// Deux fois l'ecart des tables : une paroi est LONGUE, donc ses
		// cellules escarpees se suivent, et un ecart calibre sur la largeur
		// d'une table ne les separe pas.
		for (const FFond& K : Fonds)
		{
			if (OutCanyons->Num() >= 16) { break; }

			const int32 I = K.C % NX;
			const int32 J = K.C / NX;
			const double X = (static_cast<double>(I) / NX - 0.5) * LargeurM;
			const double Y = (static_cast<double>(J) / NY - 0.5) * HauteurM;

			bool bTropPres = false;
			for (const FWorldseedPlateauSite& S : *OutCanyons)
			{
				if (FVector2D::Distance(S.CentreM, FVector2D(X, Y)) < 2.0 * EcartMin)
				{
					bTropPres = true;
					break;
				}
			}
			if (bTropPres) { continue; }

			FWorldseedPlateauSite S;
			S.CentreM = FVector2D(X, Y);
			S.AltitudeM = ElevationM[K.C];
			S.EscarpementM = K.Chute;
			S.VersLeBas = K.VersLeBas;
			OutCanyons->Add(S);
		}

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] canyons : %d parois retenues sur %d candidates"),
			OutCanyons->Num(), Fonds.Num());
		for (int32 I = 0; I < OutCanyons->Num() && I < 6; ++I)
		{
			const FWorldseedPlateauSite& S = (*OutCanyons)[I];
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]   paroi %2d : (%.0f, %.0f) m, sommet %.0f m, ")
				TEXT("chute %.0f m sur 63 m"),
				I + 1, S.CentreM.X, S.CentreM.Y, S.AltitudeM, S.EscarpementM);
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] tables : %d sites retenus sur %d candidats"),
		OutSites.Num(), Candidats.Num());
	for (int32 I = 0; I < OutSites.Num() && I < 8; ++I)
	{
		const FWorldseedPlateauSite& S = OutSites[I];
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed]   table %2d : (%.0f, %.0f) m, sommet %.0f m, paroi %.0f m"),
			I + 1, S.CentreM.X, S.CentreM.Y, S.AltitudeM, S.EscarpementM);
	}
}

void WorldseedPlateau::Build(const FWorldseedGeometry& Geometry,
	const FWorldseedLithology& Lithology,
	const FWorldseedLithologyRules& LithoRules,
	const TArray<float>& PrecipMm, const TArray<float>& TempMeanC,
	const FWorldseedPlateauRules& Rules,
	const FWorldseedFinRules& FinRules,
	const FWorldseedStratRules& StratRules, int32 Seed,
	TArray<float>& ElevationM,
	TArray<FWorldseedPlateauSite>* OutSites)
{
	WORLDSEED_TRACE(Plateaux);

	using namespace WorldseedPlateauDetail;

	const double Debut = FPlatformTime::Seconds();

	const int32 NX = Geometry.NX;
	const int32 NY = Geometry.NY;
	const int32 Count = Geometry.CellCount();
	if (!Rules.IsActive() || ElevationM.Num() != Count)
	{
		return;
	}

	const float MailleM = FMath::Max(Geometry.MetersPerPixel(), 1e-3f);

	// UNE SEULE IMPLEMENTATION, DEUX CONSOMMATEURS. La sonde appelle exactement
	// ces fonctions : un critere recopie valide une COPIE du mecanisme, pas le
	// mecanisme, et le depot a une regle contre cela.
	TArray<float> Plateau;
	TArray<float> ReliefLocal;
	Surfaces(Geometry, ElevationM, Rules, Plateau, ReliefLocal);
	if (Plateau.Num() != Count || ReliefLocal.Num() != Count) { return; }

	// --- LE DRAINAGE, QUI TAILLE LA GORGE ------------------------------------
	//
	// PONDERE PAR LA PLUIE, ET CE N'EST PAS UN RAFFINEMENT. Un flux a poids
	// uniforme mesure une AIRE ; pondere par la pluie, il mesure un DEBIT, et
	// c'est toute la difference entre un oued et un fleuve. Cela fait exister
	// le fleuve ALLOGENE -- celui qui ramasse son eau dans des montagnes
	// humides et traverse ensuite un desert. Le Colorado est exactement cela,
	// et c'est pourquoi le plus grand canyon du monde se trouve dans une
	// region qui ne recoit presque rien.
	const bool bPluie = (PrecipMm.Num() == Count);
	TArray<float> Poids;
	Poids.SetNumUninitialized(Count);
	for (int32 I = 0; I < Count; ++I)
	{
		Poids[I] = bPluie ? FMath::Max(PrecipMm[I], 1.0f) : 1.0f;
	}
	FWorldseedFlow Flux;
	WorldseedFlow::Compute(ElevationM, Poids, NX, NY, 0.0f, 1e-4f, Flux);
	const bool bFlux = (Flux.Accumulation.Num() == Count);

	// --- LE CHENAL, PUIS LA DISTANCE AU CHENAL ------------------------------
	//
	// POURQUOI UNE TRANSFORMEE DE DISTANCE, ET PAS UN SEUIL SUR LE FLUX. Un
	// smoothstep sur le flux accumule ne decrit pas une GORGE, il decrit une
	// rampe : le flux decroit continument en s'eloignant du lit, donc la
	// profondeur aussi, et l'on obtient un V adouci sur plusieurs mailles.
	// Mesure de cette premiere version : abaissement MOYEN 58 m pour un maximum
	// de 166, quand une vraie gorge donnerait une distribution a deux bosses --
	// zero ou pleine profondeur. Et 39,8 % des cellules entre 5 et 25 degres
	// contre 16,4 au-dela de 45.
	//
	// La distance au chenal, elle, permet de DESSINER le profil en travers :
	// plancher plat sur `largeurFondM`, puis paroi qui tombe en UNE maille de
	// simulation. C'est exactement ce que fait la passe littorale avec son
	// `facePart`, dont le commentaire porte la lecon : « LA FACE DOIT TENIR
	// DANS UNE MAILLE DE SIMULATION, sinon la falaise n'est qu'une rampe ».
	//
	// La transformee employee est celle du projet -- exacte et lineaire
	// (Felzenszwalb et Huttenlocher) -- et non un chanfrein maison : la regle
	// du depot interdit de recopier une formule dans deux fichiers.
	TArray<float> DistanceChenal;
	if (bFlux)
	{
		TArray<uint8> HorsChenal;
		HorsChenal.SetNumUninitialized(Count);
		int32 Chenaux = 0;
		for (int32 I = 0; I < Count; ++I)
		{
			const float LogA = FMath::Loge(1.0f + Flux.Accumulation[I]) * 0.4342944819f;
			const bool bLit = (LogA >= Rules.DrainStart) && (ElevationM[I] > 0.0f);
			HorsChenal[I] = bLit ? 0 : 1;
			if (bLit) { ++Chenaux; }
		}
		if (Chenaux > 0)
		{
			WorldseedGrid::DistanceTransform(HorsChenal, NX, NY, DistanceChenal);
		}
	}
	const bool bChenal = (DistanceChenal.Num() == Count);

	const double LargeurM = Geometry.WidthM();
	const double HauteurM = Geometry.HeightM;
	const float InvLn10 = 0.4342944819f;

	int32 Terres = 0;
	int32 EnZone = 0;
	int32 Touchees = 0;
	double SommeChute = 0.0;
	float PireChute = 0.0f;

	for (int32 J = 0; J < NY; ++J)
	{
		for (int32 I = 0; I < NX; ++I)
		{
			const int32 C = J * NX + I;
			const float H = ElevationM[C];
			if (H <= 0.0f) { continue; }
			++Terres;

			// --- OU LA FORME A-T-ELLE LE DROIT D'EXISTER ? ------------------
			//
			// TROIS CRITERES PHYSIQUES, LUS SUR LES CHAMPS CONTINUS ET JAMAIS
			// SUR L'ETIQUETTE DE BIOME. C'est la regle du depot : un biome ne
			// sert qu'a lier des assets, il ne decide d'aucune geometrie. Le
			// karst demande deja une roche soluble et une pluie suffisante ;
			// une table demande une roche sedimentaire, une pluie faible et
			// une plaine.
			if (H < Rules.MinElevationM) { continue; }
			if (ReliefLocal[C] > Rules.LocalReliefMaxM) { continue; }

			// LA MEME GARDE QUE CELLE QUI LISTE. Elle etait recopiee ici, et
			// la copie aurait laisse creuser des tables que Sites ne listait
			// plus -- un ecart qu'aucun compilateur ne signale.
			if (!Eligible(C, Rules, Lithology, LithoRules, PrecipMm, TempMeanC, Count))
			{
				continue;
			}

			const double X = (static_cast<double>(I) / NX - 0.5) * LargeurM;
			const double Y = (static_cast<double>(J) / NY - 0.5) * HauteurM;

			// Le masque de region : les tables se groupent en PAYS, elles ne se
			// poivrent pas au hasard sur toute la roche tendre aride.
			const float Zone = ZoneAt(X, Y, Rules, Seed);
			if (Zone <= 0.0f) { continue; }
			++EnZone;

			// --- LA DECOUPE, A DEUX ECHELLES --------------------------------
			//
			// La gorge suit le DRAINAGE, donc un reseau dendritique qu'aucun
			// bruit ne sait dessiner ; les fentes de diaclases ajoutent les
			// slots, etroits et rectilignes. On prend le MAXIMUM des deux : la
			// ou la gorge passe, elle l'emporte ; ailleurs les fentes rayent le
			// plateau.
			// LE PROFIL EN TRAVERS D'UNE GORGE : plancher, puis paroi.
			//
			// Un canyon n'est pas une vallee en V : c'est un PLANCHER plat --
			// le lit du fleuve -- borde de parois qui montent d'un coup. La
			// profondeur vaut donc 1 sur toute la largeur du plancher, puis
			// tombe a 0 sur UNE maille de simulation. Le voxel, qui travaille
			// au metre, rend ensuite la paroi franche.
			float Coupe = 0.0f;
			if (bChenal)
			{
				const float DistanceM = DistanceChenal[C] * MailleM;
				const float Paroi = FMath::Max(Rules.WallCells * MailleM, 1.0f);
				if (DistanceM <= Rules.FloorWidthM)
				{
					Coupe = 1.0f;
				}
				else if (DistanceM < Rules.FloorWidthM + Paroi)
				{
					Coupe = 1.0f - (DistanceM - Rules.FloorWidthM) / Paroi;
				}
			}
			if (Rules.SlotShare > 0.0f)
			{
				const double Fente = WorldseedFins::SlotAt(X, Y, 0.0, FinRules, Seed);
				if (Fente > 0.0)
				{
					const float Demi = FMath::Max(0.5f * FinRules.SlotM, 1e-3f);
					Coupe = FMath::Max(Coupe, Rules.SlotShare
						* FMath::Min(1.0f, static_cast<float>(Fente) / Demi));
				}
			}

			// --- RABOTER, PUIS CREUSER --------------------------------------
			//
			// ET LE min() FINAL EST LA GARANTIE DE TOUT LE RESTE. La passe ne
			// fait que baisser : la part emergee -- calibree a 29,2 pour cent
			// et traitee comme un invariant du projet -- ne bouge pas, aucune
			// bosse n'apparait au raccord, et le bord de la zone ne fait pas de
			// couture puisque Zone y vaut zero, donc Cible y vaut H.
			//
			// ET LE PLANCHER N'EST PAS DEFENSIF, IL EST PORTANT. Un
			// escarpement de cent soixante-dix metres creuse depuis une table
			// qui n'en fait que quatre-vingts enverrait le fond SOUS le niveau
			// de la mer, donc convertirait de la terre en mer -- et la part
			// emergee, calibree a 29,2 pour cent, est un invariant du projet.
			// La passe littorale garde sa plateforme au-dessus de zero pour
			// exactement la meme raison.
			// --- LE CHAPITEAU, ET NON PLUS UN RABOTAGE GEOMETRIQUE -----
			//
			// LA SURFACE S ARRETE SUR LE PREMIER BANC QUI LA PORTE. C est le
			// mecanisme reel d une mesa : le banc dur protege les tendres du
			// dessous, l erosion sape la base, et la roche dure s effondre par
			// blocs en laissant une paroi abrupte. Deux mesas voisines
			// s arretent donc sur LE MEME banc, a la MEME altitude -- la
			// propriete qui definit la forme vient de la geologie, plus d un
			// maximum glissant.
			//
			// LE MAXIMUM GLISSANT RESTE, mais pour ce qu il sait faire : dire
			// ou est le toit du relief, donc servir de repli la ou aucun banc
			// dur n affleure. Sans serie, on retrouve exactement l ancien
			// comportement.
			float Niveau = Plateau[C];
			double ToitChapiteau = 0.0;
			if (StratRules.IsActive()
				&& WorldseedStrata::ToitDuChapiteau(X, Y, H, StratRules,
					Rules.CapHardnessMin, Seed, ToitChapiteau))
			{
				// Borne a un escarpement : un banc dur tres bas ne doit pas
				// faire disparaitre le relief d un coup.
				Niveau = FMath::Max(static_cast<float>(ToitChapiteau),
					H - Rules.ScarpM);
			}
			const float Rabot = FMath::Lerp(H, Niveau, Zone);
			const float Cible = FMath::Max(Rules.FloorMinM,
				Rabot - Rules.ScarpM * Coupe * Zone);
			const float Neuf = FMath::Min(H, FMath::Lerp(H, Cible, Rules.Strength));

			if (Neuf < H - 0.01f)
			{
				++Touchees;
				SommeChute += H - Neuf;
				PireChute = FMath::Max(PireChute, H - Neuf);
			}
			ElevationM[C] = Neuf;
		}
	}

	if (OutSites)
	{
		Sites(Geometry, ElevationM, Rules, Lithology, LithoRules, PrecipMm,
			TempMeanC, Seed, *OutSites);
	}

	// LE RELEVE PORTE LA PART EN ZONE, ET PAS SEULEMENT LES CELLULES TOUCHEES.
	// C'est la part de zone qui se calibre -- environ un pour cent des terres,
	// arbitrage du proprietaire -- et un seuil de Perlin NE SE DEDUIT PAS, il
	// se mesure : le depot a paye cette confusion sur les diaclases, ou un
	// seuil cense garder 16 % n'en gardait que 1,59.
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] tables : zone %.2f %% des terres, %d cellules remodelees ")
		TEXT("(%.2f %%), abaissement moyen %.1f m, maximum %.0f m  (%.0f ms)"),
		100.0 * EnZone / FMath::Max(Terres, 1), Touchees,
		100.0 * Touchees / FMath::Max(Terres, 1),
		(Touchees > 0) ? SommeChute / Touchees : 0.0, PireChute,
		(FPlatformTime::Seconds() - Debut) * 1000.0);
}
