// Worldseed - ou le socle affleure. Extrait de WorldseedLithology, ou le test
// etait en TROIS copies et ou le releve n'avait pas d'oracle.

#include "Procedural/WorldseedSocle.h"

#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedTrace.h"

float WorldseedSocle::SeuilElevationM(const TArray<float>& ElevationM,
	float PartHaute, float SeuilParDefautM)
{
	if (PartHaute <= 0.0f || PartHaute >= 1.0f)
	{
		return SeuilParDefautM;
	}

	// LE QUANTILE PORTE SUR LES TERRES SEULES. Y inclure les fonds marins
	// reviendrait a mesurer la part haute d'une distribution que la mer domine
	// -- soit soixante-dix pour cent de l'echantillon sous zero, donc un seuil
	// qui tomberait au niveau de la mer sur un monde ordinaire.
	TArray<float> Terres;
	Terres.Reserve(ElevationM.Num() / 3 + 1);
	for (const float H : ElevationM)
	{
		if (H > 0.0f) { Terres.Add(H); }
	}

	if (Terres.Num() == 0)
	{
		return SeuilParDefautM;
	}

	return WorldseedGrid::Quantile(Terres, 1.0f - PartHaute);
}

void WorldseedSocle::ReliefLocal(const FWorldseedGeometry& Geo,
	const TArray<float>& ElevationM, float RayonM, int32 Grossissement,
	TArray<float>& OutBas, TArray<float>& OutHaut, int32& OutPX, int32& OutPY)
{
	const int32 Grossier = FMath::Max(Grossissement, 1);
	const int32 PX = FMath::Max(Geo.NX / Grossier, 1);
	const int32 PY = FMath::Max(Geo.NY / Grossier, 1);
	OutPX = PX;
	OutPY = PY;

	OutBas.Init(TNumericLimits<float>::Max(), PX * PY);
	OutHaut.Init(TNumericLimits<float>::Lowest(), PX * PY);

	if (ElevationM.Num() < Geo.CellCount())
	{
		return;
	}

	for (int32 J = 0; J < Geo.NY; ++J)
	{
		const int32 PJ = FMath::Min(J / Grossier, PY - 1);
		for (int32 I = 0; I < Geo.NX; ++I)
		{
			const int32 P = PJ * PX + FMath::Min(I / Grossier, PX - 1);
			const float H = ElevationM[J * Geo.NX + I];
			OutBas[P] = FMath::Min(OutBas[P], H);
			OutHaut[P] = FMath::Max(OutHaut[P], H);
		}
	}

	const float MailleM = FMath::Max(Geo.MetersPerPixel(), 1e-3f);
	const int32 Rayon = FMath::Max(
		FMath::RoundToInt(RayonM / (MailleM * Grossier)), 1);

	// SEPARABLE : min et max se propagent par axe, donc deux passes au lieu
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

	Etaler(OutBas, false);
	Etaler(OutHaut, true);
}

void WorldseedSocle::Marquer(const FWorldseedGeometry& Geo,
	const TArray<float>& ElevationM, const TArray<float>& Convergence,
	const FWorldseedSocleRegles& Regles, TArray<uint8>& OutEstSocle,
	FWorldseedSocleReleve& OutReleve)
{
	WORLDSEED_TRACE(Socle);

	OutReleve = FWorldseedSocleReleve();

	const int32 Count = Geo.CellCount();
	OutEstSocle.Init(0, FMath::Max(Count, 0));
	if (Count <= 0 || ElevationM.Num() < Count)
	{
		return;
	}

	const float SeuilElev =
		SeuilElevationM(ElevationM, Regles.PartHaute, Regles.ElevationM);
	OutReleve.SeuilElevationM = SeuilElev;

	constexpr int32 Grossier = 8;
	TArray<float> Bas;
	TArray<float> Haut;
	int32 PX = 0;
	int32 PY = 0;
	ReliefLocal(Geo, ElevationM, Regles.ReliefRayonM, Grossier, Bas, Haut, PX, PY);

	const bool bHasConv = (Convergence.Num() == Count);

	// LE TEST QUI ETAIT EN TROIS COPIES. Il est ecrit UNE fois, et les trois
	// lectures ci-dessous -- l'echantillon, le marquage, le compte -- s'y
	// referent. C'est la seule facon de garantir qu'elles ne divergeront pas.
	auto EstOrogene = [&](int32 C)
	{
		return (bHasConv && Convergence[C] > Regles.Convergence)
			|| (ElevationM[C] > SeuilElev);
	};

	// ON ECHANTILLONNE SUR LE DOMAINE QUI SERA TRIE, pas sur l'ensemble. Lecon
	// deja payee deux fois dans la lithologie : le premier calage du socle
	// demandait 45 / 35 / 20 et rendait 40,8 / 38,1 / 21,1 pour avoir
	// echantillonne trop large.
	TArray<float> ReliefsOrogenes;
	ReliefsOrogenes.Reserve(Count / 4);
	for (int32 J = 0; J < Geo.NY; ++J)
	{
		const int32 PJ = FMath::Min(J / Grossier, PY - 1);
		for (int32 I = 0; I < Geo.NX; ++I)
		{
			const int32 C = J * Geo.NX + I;
			if (ElevationM[C] <= 0.0f || !EstOrogene(C)) { continue; }
			const int32 P = PJ * PX + FMath::Min(I / Grossier, PX - 1);
			ReliefsOrogenes.Add(Haut[P] - Bas[P]);
		}
	}

	const bool bTrier = (Regles.PartAccidentee > 0.0f)
		&& (Regles.PartAccidentee < 1.0f) && (ReliefsOrogenes.Num() > 0);
	const float SeuilRelief = bTrier
		? WorldseedGrid::Quantile(ReliefsOrogenes, 1.0f - Regles.PartAccidentee)
		: 0.0f;
	OutReleve.SeuilReliefM = SeuilRelief;

	for (int32 J = 0; J < Geo.NY; ++J)
	{
		const int32 PJ = FMath::Min(J / Grossier, PY - 1);
		for (int32 I = 0; I < Geo.NX; ++I)
		{
			const int32 C = J * Geo.NX + I;
			const int32 P = PJ * PX + FMath::Min(I / Grossier, PX - 1);
			if (!EstOrogene(C)) { continue; }
			if (bTrier && (Haut[P] - Bas[P]) < SeuilRelief) { continue; }
			OutEstSocle[C] = 1;
		}
	}

	// COMBIEN LE DECAPAGE SAUVE-T-IL ? On compte, l'appelant rapporte.
	for (int32 C = 0; C < Count; ++C)
	{
		if (ElevationM[C] <= 0.0f) { continue; }
		if (EstOrogene(C)) { ++OutReleve.Orogenes; }
		if (OutEstSocle[C] != 0) { ++OutReleve.Socles; }
	}
}
