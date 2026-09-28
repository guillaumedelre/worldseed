// Worldseed - classer une cellule dans un climat de Koppen.

#include "Procedural/WorldseedKoppen.h"

namespace
{
	/** Hiver, printemps, ete, automne -- l'ordre de EWorldseedSeason. */
	constexpr int32 Hiver = 0;
	constexpr int32 Printemps = 1;
	constexpr int32 Ete = 2;
	constexpr int32 Automne = 3;

	/**
	 * LE SEUIL D'ARIDITE DE KOPPEN, ET SA PRIME DE SAISON.
	 *
	 * Un climat est aride quand sa pluie ne couvre pas ce que l'evaporation
	 * demande, et cette demande croit avec la temperature : d'ou le terme en
	 * 20 x T. La prime, elle, dit QUAND la pluie tombe -- une pluie d'ete
	 * s'evapore avant de servir, une pluie d'hiver reste. Koppen ajoute donc
	 * 28 quand au moins 70 pour cent tombe au semestre chaud, 0 quand au moins
	 * 70 tombe au froid, et 14 entre les deux.
	 */
	float SeuilAridite(float TMoyC, float PartEte)
	{
		// LES PRIMES SONT EN MILLIMETRES : 280, 140, 0 -- et non 28, 14, 0, qui
		// est la meme formule exprimee en centimetres. Un premier jet a melange
		// les deux, et le seuil valait alors le dixieme du vrai : des savanes a
		// huit cents millimetres passaient pour des steppes.
		float Prime = 140.0f;
		if (PartEte >= 0.7f) { Prime = 280.0f; }
		else if (PartEte <= 0.3f) { Prime = 0.0f; }
		return 20.0f * TMoyC + Prime;
	}
}

namespace WorldseedKoppen
{

EWorldseedKoppen Classer(const FWorldseedKoppenEntree& E, float PointeMensuelleC)
{
	// LES EXTREMES MENSUELS SONT PLUS FRANCS QUE LES SAISONNIERS, et Koppen se
	// definit sur les mois. Une saison etant la moyenne de trois mois, son
	// minimum est plus doux que le vrai : sans cette correction, des
	// continentaux franchissent la frontiere du groupe tempere. Mesure : c'est
	// exactement ce qui faisait classer `Hot_Summer_Continental` en Cf.
	const float TFroid = E.TFroidC - PointeMensuelleC;
	const float TChaud = E.TChaudC + PointeMensuelleC;

	// --- E, LES POLAIRES : le mois le plus chaud decide, et lui seul ---------
	if (TChaud < 0.0f) { return EWorldseedKoppen::EF; }
	if (TChaud < 10.0f) { return EWorldseedKoppen::ET; }

	const float TMoy = 0.5f * (E.TFroidC + E.TChaudC);

	float Total = 0.0f;
	for (int32 S = 0; S < 4; ++S) { Total += E.PluieSaisonMm[S]; }
	const float PartEte = (Total > 1e-6f)
		? (E.PluieSaisonMm[Ete] + 0.5f * (E.PluieSaisonMm[Printemps]
			+ E.PluieSaisonMm[Automne])) / Total
		: 0.5f;

	// --- B, LES ARIDES : ils passent AVANT les groupes de temperature -------
	//
	// ET C'EST L'ORDRE DE KOPPEN, pas une commodite : un desert chaud est un
	// desert avant d'etre un tropical, et le classer d'abord par sa temperature
	// le rangerait avec la savane. Le meme point vaut pour les steppes froides.
	// LE DESERT EST SOUS LA MOITIE DU SEUIL, LA STEPPE SOUS LE SEUIL -- et non
	// l'inverse. Un premier jet prenait le seuil pour la frontiere du desert et
	// son DOUBLE pour celle de la steppe, ce qui rangeait Londres et ses sept
	// cents millimetres parmi les climats arides.
	const float Seuil = SeuilAridite(TMoy, PartEte);
	if (E.PrecipAnnuelMm < 0.5f * Seuil)
	{
		return (TMoy >= 18.0f) ? EWorldseedKoppen::BWh : EWorldseedKoppen::BWk;
	}
	if (E.PrecipAnnuelMm < Seuil)
	{
		return (TMoy >= 18.0f) ? EWorldseedKoppen::BSh : EWorldseedKoppen::BSk;
	}

	float PlusSec = E.PluieSaisonMm[0];
	float PlusHumide = E.PluieSaisonMm[0];
	for (int32 S = 1; S < 4; ++S)
	{
		PlusSec = FMath::Min(PlusSec, E.PluieSaisonMm[S]);
		PlusHumide = FMath::Max(PlusHumide, E.PluieSaisonMm[S]);
	}

	// --- A, LES TROPICAUX : aucun mois sous dix-huit degres -----------------
	if (TFroid >= 18.0f)
	{
		if (PlusSec >= 60.0f) { return EWorldseedKoppen::Af; }
		// LA LIMITE DE LA MOUSSON EST UNE DROITE, pas un seuil : plus l'annee
		// est arrosee, plus un mois sec lui est pardonne.
		if (PlusSec >= 100.0f - E.PrecipAnnuelMm / 25.0f)
		{
			return EWorldseedKoppen::Am;
		}
		return (E.PluieSaisonMm[Ete] < E.PluieSaisonMm[Hiver])
			? EWorldseedKoppen::As : EWorldseedKoppen::Aw;
	}

	const bool bContinental = (TFroid < -3.0f);

	// --- LA SAISON SECHE, et ses deux criteres asymetriques -----------------
	//
	// L'ETE SEC demande un rapport de trois ET un plafond absolu ; l'HIVER SEC
	// un rapport de dix, sans plafond. L'asymetrie est celle de Koppen, et elle
	// a une raison : un ete sec se juge a ce qui manque quand l'evaporation est
	// forte, un hiver sec a ce qui manque quand elle est faible.
	const float Ec = E.PluieSaisonMm[Ete];
	const float Hi = E.PluieSaisonMm[Hiver];
	const bool bEteSec = (Ec < Hi / 3.0f) && (Ec < 40.0f);
	const bool bHiverSec = (Hi < Ec / 10.0f);

	if (bContinental)
	{
		// LES CONTINENTAUX A ETE SEC OU HIVER SEC EXISTENT (Ds, Dw), mais
		// aucun de nos vingt-trois releves n'en porte : les ranger en Df est
		// le repli le plus proche, et c'est dit plutot que tu.
		if (TChaud >= 22.0f) { return EWorldseedKoppen::Dfa; }
		if (TChaud >= 10.0f && E.TChaudC >= 10.0f)
		{
			// Dfc et Dfd se separent par la RIGUEUR DE L'HIVER, pas par l'ete.
			if (TFroid < -38.0f) { return EWorldseedKoppen::Dfd; }
			return (TChaud >= 18.0f) ? EWorldseedKoppen::Dfb : EWorldseedKoppen::Dfc;
		}
		return EWorldseedKoppen::Dfc;
	}

	if (bEteSec)
	{
		if (TChaud >= 22.0f) { return EWorldseedKoppen::Csa; }
		return (TChaud >= 16.0f) ? EWorldseedKoppen::Csb : EWorldseedKoppen::Csc;
	}
	if (bHiverSec)
	{
		return (TChaud >= 22.0f) ? EWorldseedKoppen::Cwa : EWorldseedKoppen::Cwb;
	}
	if (TChaud >= 22.0f) { return EWorldseedKoppen::Cfa; }
	return (TChaud >= 16.0f) ? EWorldseedKoppen::Cfb : EWorldseedKoppen::Cfc;
}

const TCHAR* Nom(EWorldseedKoppen K)
{
	switch (K)
	{
	case EWorldseedKoppen::Af:  return TEXT("Af");
	case EWorldseedKoppen::Am:  return TEXT("Am");
	case EWorldseedKoppen::Aw:  return TEXT("Aw");
	case EWorldseedKoppen::As:  return TEXT("As");
	case EWorldseedKoppen::BWh: return TEXT("BWh");
	case EWorldseedKoppen::BWk: return TEXT("BWk");
	case EWorldseedKoppen::BSh: return TEXT("BSh");
	case EWorldseedKoppen::BSk: return TEXT("BSk");
	case EWorldseedKoppen::Csa: return TEXT("Csa");
	case EWorldseedKoppen::Csb: return TEXT("Csb");
	case EWorldseedKoppen::Csc: return TEXT("Csc");
	case EWorldseedKoppen::Cwa: return TEXT("Cwa");
	case EWorldseedKoppen::Cwb: return TEXT("Cwb");
	case EWorldseedKoppen::Cfa: return TEXT("Cfa");
	case EWorldseedKoppen::Cfb: return TEXT("Cfb");
	case EWorldseedKoppen::Cfc: return TEXT("Cfc");
	case EWorldseedKoppen::Dfa: return TEXT("Dfa");
	case EWorldseedKoppen::Dfb: return TEXT("Dfb");
	case EWorldseedKoppen::Dfc: return TEXT("Dfc");
	case EWorldseedKoppen::Dfd: return TEXT("Dfd");
	case EWorldseedKoppen::ET:  return TEXT("ET");
	case EWorldseedKoppen::EF:  return TEXT("EF");
	default:                    return TEXT("--");
	}
}

FWorldseedKoppenEntree DepuisChamps(float TempMeanC, float PrecipAnnuelMm,
	float SeasonalAmpC, float SummerRainFrac, float ContrasteExposant)
{
	FWorldseedKoppenEntree E;

	// LES MEMES CONVENTIONS QUE LE PREREGLAGE CLIMATIQUE, et il le faut : deux
	// facons de tirer des saisons d'une moyenne et d'une amplitude finiraient
	// par diverger, et l'ecart se verrait la ou le classement et la meteo se
	// contredisent.
	const float Demi = 0.5f * SeasonalAmpC;
	E.TFroidC = TempMeanC - Demi;
	E.TChaudC = TempMeanC + Demi;
	E.PrecipAnnuelMm = PrecipAnnuelMm;

	const float Fe = FMath::Clamp(SummerRainFrac, 0.0f, 1.0f);
	const float K = FMath::Max(ContrasteExposant, 0.01f);

	float Base[4];
	Base[Hiver] = 1.0f - Fe;
	Base[Printemps] = 0.5f;
	Base[Ete] = Fe;
	Base[Automne] = 0.5f;

	float Somme = 0.0f;
	for (int32 S = 0; S < 4; ++S)
	{
		Base[S] = FMath::Pow(FMath::Max(Base[S], 1e-3f), K);
		Somme += Base[S];
	}

	// La pluie des releves est un cumul MENSUEL, pas saisonnier : on divise
	// donc par douze et non par quatre.
	const float Mensuel = PrecipAnnuelMm / 12.0f;
	const float Moyenne = Somme / 4.0f;
	for (int32 S = 0; S < 4; ++S)
	{
		E.PluieSaisonMm[S] = (Moyenne > 1e-6f) ? Mensuel * Base[S] / Moyenne : Mensuel;
	}

	return E;
}

}
