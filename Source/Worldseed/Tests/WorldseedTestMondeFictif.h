// Worldseed - un monde minuscule, fabrique a la main, pour les tests.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"

#include "Procedural/WorldseedCaves.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedWorldData.h"

/**
 * UN MONDE FICTIF, ET NON UN MONDE GENERE.
 *
 * POURQUOI ON NE FAIT PAS TOURNER LA CHAINE. Generer coute une minute et
 * dépend de `world_rules.json`, donc d'un fichier qu'on retouche sans arret :
 * un test bati dessus echouerait a chaque reglage, pour de bonnes raisons, et
 * l'on cesserait de le lire. Un test doit echouer quand le CODE casse, pas
 * quand une valeur physique bouge. Ce sont deux questions differentes, et les
 * invariants du monde reel -- terres a 29,2 %, pluie ancree a 715 mm -- ont
 * leur place ailleurs, dans les sondes.
 *
 * Ce monde-ci est donc arbitraire et SANS SIGNIFICATION PHYSIQUE. Il n'a qu'une
 * propriete, mais elle est essentielle : **aucun de ses tableaux n'est
 * constant**, et chacun porte des valeurs qui lui sont propres. Un aller-retour
 * qui confondrait deux tableaux, ou qui en rendrait un plein de zeros, se voit
 * immediatement -- ce qui ne serait pas le cas d'un monde rempli de la meme
 * valeur partout.
 */
namespace WorldseedTest
{
	/** Petite grille, mais >= 4 par axe : la bicubique exige quatre points. */
	inline FWorldseedGeometry Geometrie(int32 NY = 16)
	{
		FWorldseedGeometry G;
		G.NY = NY;
		G.NX = NY * 2;
		G.HeightM = 8000.0f;
		G.LatSpanDeg = 180.0f;
		G.LatitudeMapping = TEXT("equalArea");
		G.LatitudeEqualAreaBlend = 1.0f;
		return G;
	}

	/**
	 * Un relief lisse et NON SYMETRIQUE.
	 *
	 * La dissymetrie n'est pas un detail : une bosse centree passerait un test
	 * qui confondrait les axes X et Y, ou qui inverserait un signe. Ce depot a
	 * deja paye exactement cela -- « la toundra ne valide rien, elle existe aux
	 * DEUX poles » -- et la parade est la meme : choisir un temoin asymetrique.
	 *
	 * ET IL EST LISSE, CE QUI N'EST PAS UN DETAIL. La premiere version portait
	 * un terme `40 * (I % 7)` -- une dent de scie -- ajoute pour rendre les
	 * valeurs uniques. Le champ SOUS-JACENT cessait alors d'etre regulier, si
	 * bien que le test de continuite C1 rendait « cubique 2969,8, bilineaire
	 * 2921,8 » : les deux interpolations sautaient autant l'une que l'autre,
	 * parce qu'elles reproduisaient fidelement une donnee qui saute. **Je
	 * mesurais ma dent de scie, pas l'interpolation.** L'asymetrie vient
	 * desormais d'un troisieme harmonique dephase, qui ne casse pas la
	 * regularite.
	 */
	inline TArray<float> Relief(const FWorldseedGeometry& G)
	{
		TArray<float> H;
		H.SetNumUninitialized(G.CellCount());
		for (int32 J = 0; J < G.NY; ++J)
		{
			for (int32 I = 0; I < G.NX; ++I)
			{
				const float U = static_cast<float>(I) / static_cast<float>(G.NX);
				const float V = static_cast<float>(J) / static_cast<float>(G.NY - 1);
				H[J * G.NX + I] =
					600.0f * FMath::Sin(U * 2.0f * PI)
					+ 300.0f * FMath::Cos(V * PI * 1.5f)
					+ 120.0f * FMath::Sin(U * 6.0f * PI + 0.7f) * FMath::Cos(V * 2.0f * PI)
					- 200.0f;
			}
		}
		return H;
	}

	/** Un tableau de flottants dont chaque valeur est unique, pour l'aller-retour. */
	/**
	 * Combien de valeurs ne sont ni finies ni representables.
	 *
	 * ELLE VIT ICI PARCE QU'UBT CONCATENE LES `.cpp`. Trois fichiers de test
	 * l'avaient chacun dans leur namespace ANONYME ; deux namespaces anonymes
	 * n'en font qu'un dans une unite de traduction unifiee, et la compilation
	 * tombe sur « la fonction a deja un corps » -- dans un fichier que l'on
	 * vient d'ecrire comme dans un fichier auquel on n'a pas touche. Ce depot
	 * a deja paye ce piege deux fois, sur `WorldseedMetersToCm` puis sur `SUB`.
	 *
	 * Un NaN merite son propre compteur partout : il ne plante pas, il se
	 * propage, et une comparaison avec lui rend faux DES DEUX COTES -- donc un
	 * chunk vide, un biome manquant, un trou dans le terrain que rien ne
	 * signale.
	 */
	inline int32 CompterNonFinis(const TArray<float>& A)
	{
		int32 N = 0;
		for (const float V : A)
		{
			if (FMath::IsNaN(V) || !FMath::IsFinite(V)) { ++N; }
		}
		return N;
	}

	inline TArray<float> Serie(int32 N, float Base, float Pas)
	{
		TArray<float> A;
		A.SetNumUninitialized(N);
		for (int32 I = 0; I < N; ++I)
		{
			A[I] = Base + Pas * static_cast<float>(I);
		}
		return A;
	}

	/**
	 * Un reseau de cavites non vide.
	 *
	 * IL EXISTE POUR UNE RAISON PRECISE : le 22 septembre 2026, l'ecriture du
	 * cache etait placee AVANT la passe des grottes. Le reseau partait donc au
	 * fichier alors qu'il n'existait pas encore, et se relisait **sans la
	 * moindre erreur**, vide. Le code retombait proprement sur la
	 * reconstruction et personne n'aurait su pourquoi l'attente persistait.
	 * Un aller-retour qui compare le reseau attrape cela tout seul.
	 */
	inline FWorldseedCaveNetwork Reseau()
	{
		FWorldseedCaveNetwork R;
		for (int32 I = 0; I < 5; ++I)
		{
			FWorldseedCaveChamber C;
			C.CentreM = FVector(100.0 * I, -50.0 * I, -30.0 - I);
			C.RadiusM = 7.0f + static_cast<float>(I);
			R.Chambers.Add(C);
		}
		for (int32 I = 0; I < 4; ++I)
		{
			FWorldseedCaveSegment S;
			S.AM = R.Chambers[I].CentreM;
			S.BM = R.Chambers[I + 1].CentreM;
			S.RadiusAM = 1.5f + 0.25f * I;
			S.RadiusBM = 2.0f + 0.25f * I;
			R.Segments.Add(S);
		}
		// --- LES BORNES DE L'INDEX, ET IL FAUT LES POSER -------------------
		//
		// `ReconstruireIndex` SORT A SA PREMIERE GARDE si `Size` est nul :
		// elle ne CALCULE pas les bornes, elle les suppose posees par la passe
		// qui a bati le reseau (WorldseedCaves.cpp, autour de la ligne 850).
		// Les oublier ici donnait un index vide des les deux cotes de
		// l'aller-retour, et le test signalait le cache pour une faute qui
		// etait dans son propre montage -- un faux positif.
		//
		// Une fixture invalide ne rend pas un test severe, elle le rend MUET
		// sur ce qu'il pretend verifier.
		R.CellM = 64.0f;
		R.Min = FIntPoint(-16, -16);
		R.Size = FIntPoint(32, 32);
		R.ReconstruireIndex();
		return R;
	}

	/** Le monde complet, tous champs garnis et tous differents les uns des autres. */
	inline FWorldseedWorldData Monde(int32 NY = 16)
	{
		FWorldseedWorldData W;
		W.Geometry = Geometrie(NY);
		W.Seed = 4242;
		const int32 N = W.Geometry.CellCount();

		W.ElevationM = Relief(W.Geometry);
		// DES BASES ET DES PAS TOUS DIFFERENTS : si l'ecriture confondait deux
		// tableaux, un test sur des series identiques ne verrait rien.
		W.TempC = Serie(N, -18.0f, 0.11f);
		W.PrecipMm = Serie(N, 55.0f, 1.7f);
		W.SeasonalAmpC = Serie(N, 3.0f, 0.03f);
		W.Continentality = Serie(N, 0.05f, 0.002f);

		W.LithologyId.SetNumUninitialized(N);
		for (int32 I = 0; I < N; ++I)
		{
			W.LithologyId[I] = static_cast<uint8>(I % 8);
		}

		// LES BIOMES SONT GARNIS PARCE QU'UN TABLEAU VIDE REND UNE ASSERTION
		// MUETTE. Le test de partage compare les ADRESSES des tableaux des deux
		// porteurs ; sur un tableau vide, les deux valent nullptr et l'egalite
		// passe qu'il y ait partage ou non. Ce depot a deja paye ce genre de
		// controle qui ne discrimine pas -- le comptage de composants d'herbe
		// qui rendait zero sur le cas TEMOIN comme sur le notre.
		W.Biomes.Index.SetNumUninitialized(N);
		W.Biomes.Cover.SetNumUninitialized(N);
		W.Biomes.SlopeDeg.SetNumUninitialized(N);
		for (int32 I = 0; I < N; ++I)
		{
			W.Biomes.Index[I] = static_cast<uint8>(I % 19);
			W.Biomes.Cover[I] = static_cast<uint8>(I % 4);
			W.Biomes.SlopeDeg[I] = 0.5f * static_cast<float>(I % 90);
		}

		W.Caves = Reseau();

		// LES TABLES ET LES CANYONS, GARNIS POUR LA MEME RAISON QUE LES
		// BIOMES. Un tableau vide se serialise et se relit vide : l'aller-
		// retour passerait donc que la serialisation existe ou non. C'est
		// exactement le defaut du 22 septembre -- un reseau de cavites ecrit
		// avant d'exister se relisait sans erreur, et personne ne voyait
		// pourquoi l'attente persistait.
		//
		// LES VALEURS SONT DISTINCTES CHAMP PAR CHAMP, et c'est voulu : si
		// `AltitudeM` et `EscarpementM` portaient le meme nombre, une
		// serialisation qui les intervertit passerait sans rien dire. Meme
		// chose pour les deux composantes de chaque vecteur.
		for (int32 I = 0; I < 5; ++I)
		{
			FWorldseedPlateauSite T;
			T.CentreM = FVector2D(100.0 * I + 1.0, -200.0 * I - 3.0);
			T.AltitudeM = 410.0f + I;
			T.EscarpementM = 137.0f + 2.0f * I;
			T.VersLeBas = FVector2D(0.6, -0.8);
			W.Tables.Add(T);
		}
		for (int32 I = 0; I < 3; ++I)
		{
			FWorldseedPlateauSite C;
			C.CentreM = FVector2D(-50.0 * I - 7.0, 300.0 * I + 11.0);
			C.AltitudeM = -12.0f - I;
			C.EscarpementM = 88.0f + 3.0f * I;
			C.VersLeBas = FVector2D(-0.28, 0.96);
			W.Canyons.Add(C);
		}

		return W;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
