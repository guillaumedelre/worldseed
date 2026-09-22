// Worldseed - la lithologie : de quelle ROCHE est fait le sous-sol.

#include "Procedural/WorldseedLithology.h"

#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedPerlin.h"
#include "Procedural/WorldseedTrace.h"

#include "Async/ParallelFor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	int32 IdDeCle(const TArray<FWorldseedLithologyEntry>& Catalogue, const FString& Cle)
	{
		for (int32 I = 0; I < Catalogue.Num(); ++I)
		{
			if (Catalogue[I].Key == Cle) { return I; }
		}
		return INDEX_NONE;
	}
}

FWorldseedLithologyRules FWorldseedLithologyRules::FromRules(const UWorldseedRules& Rules)
{
	FWorldseedLithologyRules Out;

	// --- le catalogue, sur le meme patron que le registre des biomes ---------
	if (const TArray<TSharedPtr<FJsonValue>>* Entrees =
		Rules.Array(WorldseedSection::Substrat, TEXT("lithologies")))
	{
		int32 MaxId = -1;
		for (const TSharedPtr<FJsonValue>& V : *Entrees)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			int32 Id = -1;
			if (V.IsValid() && V->TryGetObject(Obj) && Obj
				&& (*Obj)->TryGetNumberField(TEXT("id"), Id))
			{
				MaxId = FMath::Max(MaxId, Id);
			}
		}
		Out.Catalogue.SetNum(FMath::Max(MaxId + 1, 0));

		for (const TSharedPtr<FJsonValue>& V : *Entrees)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			int32 Id = -1;
			if (!V.IsValid() || !V->TryGetObject(Obj) || !Obj
				|| !(*Obj)->TryGetNumberField(TEXT("id"), Id)
				|| !Out.Catalogue.IsValidIndex(Id))
			{
				continue;
			}

			FWorldseedLithologyEntry& E = Out.Catalogue[Id];
			(*Obj)->TryGetStringField(TEXT("cle"), E.Key);
			(*Obj)->TryGetStringField(TEXT("libelle"), E.Label);

			double Nombre = 0.0;
			if ((*Obj)->TryGetNumberField(TEXT("karstifiable"), Nombre))
			{
				E.Karstifiable = static_cast<float>(Nombre);
			}
			if ((*Obj)->TryGetNumberField(TEXT("durete"), Nombre))
			{
				E.Hardness = static_cast<float>(Nombre);
			}

			const TArray<TSharedPtr<FJsonValue>>* Couleur = nullptr;
			if ((*Obj)->TryGetArrayField(TEXT("couleur"), Couleur) && Couleur->Num() >= 3)
			{
				E.Colour = FLinearColor(FColor(
					static_cast<uint8>((*Couleur)[0]->AsNumber()),
					static_cast<uint8>((*Couleur)[1]->AsNumber()),
					static_cast<uint8>((*Couleur)[2]->AsNumber()), 255));
			}
		}
	}

	// --- l'attribution --------------------------------------------------------
	Out.SocleConvergence = static_cast<float>(
		Rules.Num(WorldseedSection::Substrat, TEXT("lithologieSocleConvergence"), 0.35));
	Out.SocleElevationM = static_cast<float>(
		Rules.Num(WorldseedSection::Substrat, TEXT("lithologieSocleElevationM"), 150.0));
	Out.MotifFrequency = static_cast<float>(
		Rules.Num(WorldseedSection::Substrat, TEXT("lithologieMotifFrequency"), 6.0));
	Out.MotifOctaves = Rules.Int(WorldseedSection::Substrat, TEXT("lithologieMotifOctaves"), 3);

	Out.IdOceanique = IdDeCle(Out.Catalogue,
		Rules.Str(WorldseedSection::Substrat, TEXT("lithologieOceanique"), TEXT("basalte")));
	Out.IdSocle = IdDeCle(Out.Catalogue,
		Rules.Str(WorldseedSection::Substrat, TEXT("lithologieSocle"), TEXT("granite")));

	// Les roches de bassin et leurs proportions, dans l'ordre du fichier.
	if (const TArray<TSharedPtr<FJsonValue>>* Bassin =
		Rules.Array(WorldseedSection::Substrat, TEXT("lithologieBassin")))
	{
		for (const TSharedPtr<FJsonValue>& V : *Bassin)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(Obj) || !Obj) { continue; }

			FString Cle;
			double Part = 0.0;
			if (!(*Obj)->TryGetStringField(TEXT("cle"), Cle)) { continue; }
			(*Obj)->TryGetNumberField(TEXT("part"), Part);

			const int32 Id = IdDeCle(Out.Catalogue, Cle);
			if (Id != INDEX_NONE && Part > 0.0)
			{
				Out.IdsSedimentaires.Add(Id);
				Out.PartsSedimentaires.Add(static_cast<float>(Part));
			}
		}
	}

	Out.SoclePartHaute = static_cast<float>(Rules.Num(
		WorldseedSection::Substrat, TEXT("lithologieSoclePartHaute"), 0.15));
	Out.SoclePartAccidentee = static_cast<float>(Rules.Num(
		WorldseedSection::Substrat, TEXT("lithologieSoclePartAccidentee"), 0.5));
	Out.SocleReliefRayonM = static_cast<float>(Rules.Num(
		WorldseedSection::Substrat, TEXT("lithologieSocleReliefRayonM"), 3000.0));

	// --- LES DOMAINES DE DEPOT ------------------------------------------------
	Out.PlateformeAltitudeMaxM = static_cast<float>(Rules.Num(
		WorldseedSection::Substrat, TEXT("lithologiePlateformeAltitudeMaxM"), 80.0));
	Out.PlateformePorteeM = static_cast<float>(Rules.Num(
		WorldseedSection::Substrat, TEXT("lithologiePlateformePorteeM"), 4000.0));
	Out.VolcanPorteeM = static_cast<float>(Rules.Num(
		WorldseedSection::Substrat, TEXT("lithologieVolcanPorteeM"), 3000.0));
	Out.VolcanConvergenceMin = static_cast<float>(Rules.Num(
		WorldseedSection::Substrat, TEXT("lithologieVolcanConvergenceMin"), 0.12));
	Out.PlisseConvergenceMin = static_cast<float>(Rules.Num(
		WorldseedSection::Substrat, TEXT("lithologiePlisseConvergenceMin"), 0.08));

	if (const TArray<TSharedPtr<FJsonValue>>* Domaines =
		Rules.Array(WorldseedSection::Substrat, TEXT("lithologieDomaines")))
	{
		for (const TSharedPtr<FJsonValue>& V : *Domaines)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(Obj) || !Obj) { continue; }

			FWorldseedLithoDomaine D;
			if (!(*Obj)->TryGetStringField(TEXT("cle"), D.Cle)) { continue; }

			const TArray<TSharedPtr<FJsonValue>>* Roches = nullptr;
			if (!(*Obj)->TryGetArrayField(TEXT("roches"), Roches) || !Roches) { continue; }

			for (const TSharedPtr<FJsonValue>& RV : *Roches)
			{
				const TSharedPtr<FJsonObject>* RObj = nullptr;
				if (!RV.IsValid() || !RV->TryGetObject(RObj) || !RObj) { continue; }
				FString Cle;
				double Part = 0.0;
				if (!(*RObj)->TryGetStringField(TEXT("cle"), Cle)) { continue; }
				(*RObj)->TryGetNumberField(TEXT("part"), Part);
				const int32 Id = IdDeCle(Out.Catalogue, Cle);
				if (Id != INDEX_NONE && Part > 0.0)
				{
					D.Ids.Add(Id);
					D.Parts.Add(static_cast<float>(Part));
				}
			}

			if (D.Ids.Num() > 0) { Out.Domaines.Add(MoveTemp(D)); }
		}
	}

	return Out;
}

void WorldseedLithology::Erodibility(const FWorldseedLithology& Lithology,
	const FWorldseedLithologyRules& Rules, float Weight, TArray<float>& Out)
{
	Out.Reset();
	if (Weight <= 0.0f || Lithology.Id.Num() == 0 || Rules.Catalogue.Num() == 0)
	{
		return;
	}

	// LA MOYENNE SE PREND SUR LE MONDE, PAS SUR LE CATALOGUE : deux roches qui
	// couvrent 1 % et 40 % des terres ne pesent pas pareil dans le bilan, et
	// c'est le bilan qu'on veut laisser inchange.
	double Somme = 0.0;
	int32 N = 0;
	for (const uint8 Id : Lithology.Id)
	{
		if (Rules.Catalogue.IsValidIndex(Id))
		{
			Somme += Rules.Catalogue[Id].Hardness;
			++N;
		}
	}
	if (N == 0)
	{
		return;
	}
	const float Moyenne = static_cast<float>(Somme / N);

	Out.SetNumUninitialized(Lithology.Id.Num());
	for (int32 I = 0; I < Lithology.Id.Num(); ++I)
	{
		const uint8 Id = Lithology.Id[I];
		const float Durete = Rules.Catalogue.IsValidIndex(Id)
			? Rules.Catalogue[Id].Hardness : Moyenne;

		// Plus dur que la moyenne : on s'use moins. Borne des deux cotes pour
		// qu'aucune cellule ne devienne indestructible ni ne fonde.
		Out[I] = FMath::Clamp(1.0f + Weight * (Moyenne - Durete), 0.15f, 2.5f);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] erodabilite : durete moyenne %.2f, poids %.2f, ")
		TEXT("K de %.2f a %.2f"),
		Moyenne, Weight,
		FMath::Clamp(1.0f + Weight * (Moyenne - 1.0f), 0.15f, 2.5f),
		FMath::Clamp(1.0f + Weight * Moyenne, 0.15f, 2.5f));
}

const TCHAR* WorldseedLithology::Name(const FWorldseedLithologyRules& Rules, uint8 Id)
{
	return Rules.Catalogue.IsValidIndex(Id) ? *Rules.Catalogue[Id].Label : TEXT("inconnue");
}

void WorldseedLithology::Compute(const FWorldseedGeometry& Geometry,
	const TArray<float>& ElevationM, const TArray<uint8>& IsContinental,
	const TArray<float>& Convergence, const FWorldseedLithologyRules& Rules,
	int32 Seed, FWorldseedLithology& Out)
{
	WORLDSEED_TRACE(Lithologie);

	const double StartTime = FPlatformTime::Seconds();

	const int32 Count = Geometry.CellCount();
	Out.Id.Reset();
	if (Count <= 0 || ElevationM.Num() != Count || Rules.Catalogue.Num() == 0)
	{
		return;
	}

	const bool bHasCont = (IsContinental.Num() == Count);
	const bool bHasConv = (Convergence.Num() == Count);

	const uint8 IdOcean = static_cast<uint8>(FMath::Max(Rules.IdOceanique, 0));
	const uint8 IdSocle = static_cast<uint8>(FMath::Max(Rules.IdSocle, 0));

	// --- LE SEUIL DU SOCLE, PAR QUANTILE ET NON EN METRES ---------------------
	//
	// Voir le commentaire de SoclePartHaute : un seuil metrique cale sur un
	// monde de 8 km avale tout le relief d'un monde de 64. Le quantile porte
	// sur les TERRES seules -- y inclure les fonds marins reviendrait a mesurer
	// la part haute d'une distribution que la mer domine.
	float SeuilSocleM = Rules.SocleElevationM;
	if (Rules.SoclePartHaute > 0.0f && Rules.SoclePartHaute < 1.0f)
	{
		TArray<float> Terres;
		Terres.Reserve(Count / 3 + 1);
		for (int32 I = 0; I < Count; ++I)
		{
			if (ElevationM[I] > 0.0f) { Terres.Add(ElevationM[I]); }
		}
		if (Terres.Num() > 0)
		{
			SeuilSocleM = WorldseedGrid::Quantile(Terres, 1.0f - Rules.SoclePartHaute);
		}
	}

	// --- LE SOCLE SE DECIDE UNE FOIS, ET IL DEMANDE UN DECAPAGE --------------
	//
	// LA REGLE D'ATTRIBUTION LE DEMANDAIT DEJA, et le code ne l'ecoutait qu'a
	// moitie : « un OROGENE expose son socle -- soulevement et DECAPAGE
	// emportent la couverture sedimentaire ». Le decapage est une EROSION, et
	// ce qui decape est le RELIEF ; la convergence ne fait que soulever. En ne
	// testant que la convergence, on declarait socle toute la bande
	// convergente -- y compris le BASSIN PLAT qui borde la chaine.
	//
	// OR UN BASSIN D'AVANT-PAYS EST L'INVERSE D'UN SOCLE : il est PLEIN des
	// sediments arraches a la chaine voisine. C'est litteralement le decor des
	// mesas reelles. Mesure du defaut : sur le terrain chaud, aride et peu
	// accidente que les tables demandent, 70,80 % de granite et 29,13 % de
	// basalte pour 0,00 % de gres -- donc aucune table dans tout le monde.
	//
	// ET LE TEST ETAIT EN TROIS COPIES dans ce fichier. Ajouter le relief a
	// l'une aurait fait diverger les trois, sans qu'aucun compilateur ne le
	// dise. On le calcule donc UNE fois, ici, et les trois le lisent.
	TArray<uint8> EstSocle;
	EstSocle.Init(0, Count);
	{
		// LE RELIEF LOCAL SE MESURE SUR UNE GRILLE GROSSIE, et c'est un choix
		// de cout : la fenetre fait trois kilometres, soit pres de cent
		// cellules de rayon, et un balayage naif y coute deux milliards
		// d'operations. Un huitieme de resolution suffit largement a dire si
		// l'on est dans une chaine ou dans une plaine -- on ne cherche pas un
		// contour, on cherche une CLASSE de terrain.
		constexpr int32 Grossier = 8;
		const int32 PX = FMath::Max(Geometry.NX / Grossier, 1);
		const int32 PY = FMath::Max(Geometry.NY / Grossier, 1);

		TArray<float> Bas;  Bas.Init(TNumericLimits<float>::Max(), PX * PY);
		TArray<float> Haut; Haut.Init(TNumericLimits<float>::Lowest(), PX * PY);

		for (int32 J = 0; J < Geometry.NY; ++J)
		{
			const int32 PJ = FMath::Min(J / Grossier, PY - 1);
			for (int32 I = 0; I < Geometry.NX; ++I)
			{
				const int32 P = PJ * PX + FMath::Min(I / Grossier, PX - 1);
				const float H = ElevationM[J * Geometry.NX + I];
				Bas[P] = FMath::Min(Bas[P], H);
				Haut[P] = FMath::Max(Haut[P], H);
			}
		}

		const float MailleM = FMath::Max(Geometry.MetersPerPixel(), 1e-3f);
		const int32 Rayon = FMath::Max(
			FMath::RoundToInt(Rules.SocleReliefRayonM / (MailleM * Grossier)), 1);

		// Separable : min et max se propagent par axe, donc deux passes au lieu
		// d'une fenetre carree.
		auto Etaler = [PX, PY, Rayon](TArray<float>& V, bool bMax)
		{
			TArray<float> Tmp = V;
			for (int32 J = 0; J < PY; ++J)
			{
				for (int32 I = 0; I < PX; ++I)
				{
					float A = V[J * PX + I];
					for (int32 D = -Rayon; D <= Rayon; ++D)
					{
						const int32 K = FMath::Clamp(I + D, 0, PX - 1);
						A = bMax ? FMath::Max(A, V[J * PX + K])
								 : FMath::Min(A, V[J * PX + K]);
					}
					Tmp[J * PX + I] = A;
				}
			}
			V = Tmp;
			for (int32 J = 0; J < PY; ++J)
			{
				for (int32 I = 0; I < PX; ++I)
				{
					float A = V[J * PX + I];
					for (int32 D = -Rayon; D <= Rayon; ++D)
					{
						const int32 K = FMath::Clamp(J + D, 0, PY - 1);
						A = bMax ? FMath::Max(A, V[K * PX + I])
								 : FMath::Min(A, V[K * PX + I]);
					}
					Tmp[J * PX + I] = A;
				}
			}
			V = Tmp;
		};

		Etaler(Bas, false);
		Etaler(Haut, true);

		auto EstOrogene = [&](int32 C)
		{
			return (bHasConv && Convergence[C] > Rules.SocleConvergence)
				|| (ElevationM[C] > SeuilSocleM);
		};

		// LE SEUIL SE LIT CONTRE LA DISTRIBUTION DU MONDE, jamais contre
		// l.intuition -- et l.on echantillonne SUR LE DOMAINE QUI SERA TRIE,
		// pas sur l.ensemble. Lecon deja payee deux fois dans ce fichier : le
		// premier calage du socle demandait 45 / 35 / 20 et rendait
		// 40,8 / 38,1 / 21,1 pour avoir echantillonne trop large.
		TArray<float> ReliefsOrogenes;
		ReliefsOrogenes.Reserve(Count / 4);
		for (int32 J = 0; J < Geometry.NY; ++J)
		{
			const int32 PJ = FMath::Min(J / Grossier, PY - 1);
			for (int32 I = 0; I < Geometry.NX; ++I)
			{
				const int32 C = J * Geometry.NX + I;
				if (ElevationM[C] <= 0.0f || !EstOrogene(C)) { continue; }
				ReliefsOrogenes.Add(
					Haut[PJ * PX + FMath::Min(I / Grossier, PX - 1)]
					- Bas[PJ * PX + FMath::Min(I / Grossier, PX - 1)]);
			}
		}

		const bool bTrier = (Rules.SoclePartAccidentee > 0.0f)
			&& (Rules.SoclePartAccidentee < 1.0f) && (ReliefsOrogenes.Num() > 0);
		const float SeuilRelief = bTrier
			? WorldseedGrid::Quantile(ReliefsOrogenes, 1.0f - Rules.SoclePartAccidentee)
			: 0.0f;

		for (int32 J = 0; J < Geometry.NY; ++J)
		{
			const int32 PJ = FMath::Min(J / Grossier, PY - 1);
			for (int32 I = 0; I < Geometry.NX; ++I)
			{
				const int32 C = J * Geometry.NX + I;
				const int32 P = PJ * PX + FMath::Min(I / Grossier, PX - 1);
				if (!EstOrogene(C)) { continue; }
				if (bTrier && (Haut[P] - Bas[P]) < SeuilRelief) { continue; }
				EstSocle[C] = 1;
			}
		}

		// COMBIEN LE DECAPAGE SAUVE-T-IL ? Un chiffre identique apres une
		// correction reelle est le signe que ce depot a rencontre cinq fois :
		// soit le monde vient du cache, soit la garde ne mord pas. On le dit.
		int32 Orogenes = 0, Socles = 0;
		for (int32 C = 0; C < Count; ++C)
		{
			if (ElevationM[C] <= 0.0f) { continue; }
			if (EstOrogene(C)) { ++Orogenes; }
			if (EstSocle[C] != 0) { ++Socles; }
		}
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] substrat : %d cellules orogeniques, %d gardees SOCLE ")
			TEXT("(%.1f %%) -- le decapage en rend %d au bassin, relief exige %.0f m ")
			TEXT("sur %.0f m"),
			Orogenes, Socles,
			(Orogenes > 0) ? 100.0 * Socles / Orogenes : 0.0,
			Orogenes - Socles, SeuilRelief, Rules.SocleReliefRayonM);
	}

	// --- le motif sedimentaire -----------------------------------------------
	//
	// UN BRUIT COHERENT, PAS UN TIRAGE PAR CELLULE. Les bassins sont des
	// ensembles etendus : un calcaire poivre au hasard dans du gres ne
	// ressemble a rien, et surtout ne donnerait jamais le massif d'un seul
	// tenant dont un reseau karstique a besoin.
	TArray<float> Motif;
	WorldseedPerlin::FBMSphere(Motif, Geometry, Rules.MotifFrequency,
		FMath::Max(Rules.MotifOctaves, 1), Seed + 7717);

	// --- LES DOMAINES DE DEPOT -----------------------------------------------
	//
	// LE LIEU DECIDE, PUIS LE BRUIT. Le modele d'avant tirait toutes les roches
	// de bassin dans une seule loterie : de la craie pouvait apparaitre au
	// coeur d'un continent, et comme c'est la ROCHE qui decide des formes, la
	// forme se retrouvait au mauvais endroit -- une falaise de craie en haute
	// montagne, un karst sans paroi ou s'ouvrir. On partitionne donc d'abord
	// par le milieu de depot ; le bruit ne decide qu'a l'interieur, ce qui
	// garde les massifs d'un seul tenant dont un reseau karstique a besoin.
	// COPIE LOCALE : les seuils se calculent ici, et les regles sont const --
	// les forcer serait un mensonge au compilateur autant qu'un piege pour le
	// jour ou deux mondes se generont en parallele.
	TArray<FWorldseedLithoDomaine> Domaines = Rules.Domaines;
	TArray<int32> DomaineDe;
	if (Domaines.Num() > 0)
	{
		// Distance a la MER et distance a la CROUTE OCEANIQUE sont deux choses
		// differentes : un plateau continental est immerge mais continental.
		// La premiere place les plates-formes de craie, la seconde les arcs.
		TArray<uint8> Terre;
		TArray<uint8> Continental;
		Terre.SetNumUninitialized(Count);
		Continental.SetNumUninitialized(Count);
		for (int32 I = 0; I < Count; ++I)
		{
			Terre[I] = (ElevationM[I] > 0.0f) ? 1 : 0;
			Continental[I] = (bHasCont && IsContinental[I] == 0) ? 0 : 1;
		}

		TArray<float> DistMer;
		TArray<float> DistCroute;
		WorldseedGrid::DistanceTransform(Terre, Geometry.NX, Geometry.NY, DistMer);
		WorldseedGrid::DistanceTransform(Continental, Geometry.NX, Geometry.NY,
			DistCroute);

		const float MailleM = FMath::Max(Geometry.MetersPerPixel(), 1e-3f);

		int32 IMarin = INDEX_NONE;
		int32 IVolcan = INDEX_NONE;
		int32 IPlisse = INDEX_NONE;
		int32 IContinental = INDEX_NONE;
		for (int32 D = 0; D < Domaines.Num(); ++D)
		{
			const FString& C = Domaines[D].Cle;
			if (C == TEXT("marin")) { IMarin = D; }
			else if (C == TEXT("volcanique")) { IVolcan = D; }
			else if (C == TEXT("plisse")) { IPlisse = D; }
			else if (C == TEXT("continental")) { IContinental = D; }
		}
		if (IContinental == INDEX_NONE) { IContinental = Domaines.Num() - 1; }

		DomaineDe.SetNumUninitialized(Count);
		for (int32 I = 0; I < Count; ++I)
		{
			DomaineDe[I] = INDEX_NONE;

			const bool bOceanique = bHasCont && IsContinental[I] == 0;
			const bool bSocle = (EstSocle[I] != 0);
			if (bOceanique || bSocle || ElevationM[I] <= 0.0f) { continue; }

			const float Conv = bHasConv ? Convergence[I] : 0.0f;

			// L'ORDRE COMPTE : un cap de craie au pied d'un arc volcanique est
			// de la craie. Le milieu de DEPOT prime sur le contexte tectonique.
			if (IMarin != INDEX_NONE
				&& ElevationM[I] < Rules.PlateformeAltitudeMaxM
				&& DistMer[I] * MailleM < Rules.PlateformePorteeM)
			{
				DomaineDe[I] = IMarin;
			}
			else if (IVolcan != INDEX_NONE
				&& Conv > Rules.VolcanConvergenceMin
				&& DistCroute[I] * MailleM < Rules.VolcanPorteeM)
			{
				DomaineDe[I] = IVolcan;
			}
			else if (IPlisse != INDEX_NONE && Conv > Rules.PlisseConvergenceMin)
			{
				DomaineDe[I] = IPlisse;
			}
			else
			{
				DomaineDe[I] = IContinental;
			}
		}

		// --- LES SEUILS, PAR DOMAINE ----------------------------------------
		//
		// ET SUR LE DOMAINE LUI-MEME, JAMAIS SUR L'ENSEMBLE. C'est la lecon
		// deja payee sur le socle : echantillonner un domaine plus large que
		// celui auquel les quantiles s'appliquent ne tient pas les proportions
		// demandees -- 40,8 / 38,1 / 21,1 % pour 45 / 35 / 20.
		for (int32 D = 0; D < Domaines.Num(); ++D)
		{
			FWorldseedLithoDomaine& Dom = Domaines[D];
			Dom.Seuils.Reset();
			if (Dom.Ids.Num() < 2) { continue; }

			TArray<float> Echantillon;
			Echantillon.Reserve(Count / 8 + 1);
			for (int32 I = 0; I < Count; ++I)
			{
				if (DomaineDe[I] == D) { Echantillon.Add(Motif[I]); }
			}
			if (Echantillon.Num() == 0) { continue; }

			float Somme = 0.0f;
			for (const float P : Dom.Parts) { Somme += P; }
			Somme = FMath::Max(Somme, 1e-6f);

			float Cumul = 0.0f;
			for (int32 K = 0; K + 1 < Dom.Ids.Num(); ++K)
			{
				Cumul += Dom.Parts[K] / Somme;
				Dom.Seuils.Add(WorldseedGrid::Quantile(Echantillon,
					FMath::Clamp(Cumul, 0.0f, 1.0f)));
			}
		}
	}

	// --- les seuils, PAR QUANTILE --------------------------------------------
	//
	// Les proportions demandees doivent etre tenues quelle que soit la
	// distribution du bruit. Poser des seuils en dur sur sa valeur donnerait des
	// parts qui bougent a chaque changement d'octave ou de frequence.
	TArray<float> Seuils;
	if (Rules.IdsSedimentaires.Num() > 1)
	{
		TArray<float> Echantillon;
		Echantillon.Reserve(Count / 4 + 1);
		// ON N'ECHANTILLONNE QUE LE DOMAINE QUI RESTERA AU BASSIN. Prendre toute
		// la terre continentale incluait les cellules que la regle du socle
		// allait emporter juste apres : les quantiles portaient alors sur un
		// domaine plus large que celui auquel ils s'appliquent, et les
		// proportions demandees n'etaient pas tenues. Mesure de l'ecart avant
		// correction : 40,8 / 38,1 / 21,1 % pour 45 / 35 / 20 demandes.
		for (int32 I = 0; I < Count; ++I)
		{
			const bool bOceanique = bHasCont && IsContinental[I] == 0;
			const bool bSocle = (EstSocle[I] != 0);

			if (!bOceanique && !bSocle && ElevationM[I] > 0.0f)
			{
				Echantillon.Add(Motif[I]);
			}
		}

		if (Echantillon.Num() > 0)
		{
			float Somme = 0.0f;
			for (const float P : Rules.PartsSedimentaires) { Somme += P; }
			Somme = FMath::Max(Somme, 1e-6f);

			float Cumul = 0.0f;
			for (int32 K = 0; K + 1 < Rules.IdsSedimentaires.Num(); ++K)
			{
				Cumul += Rules.PartsSedimentaires[K] / Somme;
				Seuils.Add(WorldseedGrid::Quantile(Echantillon, FMath::Clamp(Cumul, 0.0f, 1.0f)));
			}
		}
	}

	Out.Id.SetNumUninitialized(Count);

	ParallelFor(Count, [&](int32 I)
	{
		// 1. La croute oceanique est basaltique. C'est vrai partout sur Terre,
		//    et ca ne demande aucun reglage.
		if (bHasCont && IsContinental[I] == 0)
		{
			Out.Id[I] = IdOcean;
			return;
		}

		// 2. Un orogene expose son SOCLE : soulevement et decapage emportent la
		//    couverture sedimentaire. La convergence dit la chaine en formation,
		//    l'altitude les reliefs anciens deja decapes.
		const bool bSocle = (EstSocle[I] != 0);
		if (bSocle)
		{
			Out.Id[I] = IdSocle;
			return;
		}

		// 3. Le reste est un bassin : son MILIEU DE DEPOT donne la palette, le
		//    motif choisit dedans.
		if (DomaineDe.Num() == Count && Domaines.IsValidIndex(DomaineDe[I]))
		{
			const FWorldseedLithoDomaine& Dom = Domaines[DomaineDe[I]];
			int32 K = 0;
			while (K < Dom.Seuils.Num() && Motif[I] > Dom.Seuils[K]) { ++K; }
			Out.Id[I] = static_cast<uint8>(Dom.Ids[FMath::Min(K, Dom.Ids.Num() - 1)]);
			return;
		}

		// Repli : l'ancienne loterie unique, si le fichier de regles ne decrit
		// aucun domaine. Un fichier plus ancien garde donc son comportement.
		if (Rules.IdsSedimentaires.Num() == 0)
		{
			Out.Id[I] = IdSocle;
			return;
		}

		int32 K = 0;
		while (K < Seuils.Num() && Motif[I] > Seuils[K]) { ++K; }
		Out.Id[I] = static_cast<uint8>(
			Rules.IdsSedimentaires[FMath::Min(K, Rules.IdsSedimentaires.Num() - 1)]);
	});

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] lithologie : %d roches  (%.0f ms)"),
		Rules.Catalogue.Num(), (FPlatformTime::Seconds() - StartTime) * 1000.0);
}
