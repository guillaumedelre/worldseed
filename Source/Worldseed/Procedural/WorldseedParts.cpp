// Worldseed - tenir des proportions demandees, par quantiles.
//
// CETTE PAIRE ETAIT ECRITE DEUX FOIS DANS LA LITHOLOGIE : une fois pour les
// domaines de depot, une fois pour le repli sedimentaire. Le fichier portait
// pourtant deja la note qui condamne ce genre de copie -- « ET LE TEST ETAIT
// EN TROIS COPIES dans ce fichier. Ajouter le relief a l'une aurait fait
// diverger les trois, sans qu'aucun compilateur ne le dise. »

#include "Procedural/WorldseedParts.h"

#include "Procedural/WorldseedGrid.h"

void WorldseedParts::Seuils(const TArray<float>& Echantillon,
	const TArray<float>& Parts, int32 NbClasses, TArray<float>& OutSeuils)
{
	OutSeuils.Reset();

	if (Echantillon.Num() == 0 || NbClasses < 2)
	{
		return;
	}

	// LA SOMME PORTE SUR TOUTES LES PARTS DECRITES, pas sur les seules classes
	// qu'on decoupe : c'est ce qui rend les proportions justes quand le fichier
	// de regles decrit plus de parts que la table n'a d'entrees.
	float Somme = 0.0f;
	for (const float P : Parts)
	{
		Somme += P;
	}
	Somme = FMath::Max(Somme, 1e-6f);

	float Cumul = 0.0f;
	for (int32 K = 0; K + 1 < NbClasses; ++K)
	{
		// UNE PART MANQUANTE VAUT ZERO, ELLE N'EST PAS UNE LECTURE HORS BORNES.
		// L'original indexait `Parts[K]` sans garde : un fichier de regles qui
		// decrit moins de parts que d'entrees faisait tomber la generation.
		// Ici la classe recoit une part nulle, donc une tranche vide -- ce qui
		// est exactement ce qu'un fichier muet demande.
		//
		// ET LA DIVISION RESTE DANS L'ACCUMULATION, comme dans l'original :
		// `(p0/S) + (p1/S)` et `(p0 + p1)/S` ne sont pas le meme flottant, et
		// un ULP sur un cumul deplace un quantile, donc bascule des cellules.
		// Un refactor se juge sur un releve IDENTIQUE au chiffre pres ; sortir
		// la division serait plus joli et rendrait ce verdict impossible.
		Cumul += (Parts.IsValidIndex(K) ? Parts[K] : 0.0f) / Somme;
		OutSeuils.Add(WorldseedGrid::Quantile(Echantillon,
			FMath::Clamp(Cumul, 0.0f, 1.0f)));
	}
}

int32 WorldseedParts::Classe(float Valeur, const TArray<float>& Seuils)
{
	int32 K = 0;
	while (K < Seuils.Num() && Valeur > Seuils[K])
	{
		++K;
	}
	return K;
}
