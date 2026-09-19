// Worldseed - le socle commun des sondes.

#include "Procedural/WorldseedProbeCommun.h"

#include "Procedural/WorldseedRules.h"

bool FWorldseedSonde::Preparer(int32 Seed, float HeightMeters, int32 ResolutionY)
{
	bValide = false;

	// LES SONDES RELISENT LES REGLES, LE PIE NON. C'est ce qui permet de
	// changer une valeur et de mesurer sans relancer l'editeur -- et c'est
	// aussi le piege : modifier une regle puis relancer un PIE ne change RIEN
	// tant qu'aucune sonde n'est passee entre les deux.
	WorldseedPipeline::ReloadRules();

	if (!WorldseedPipeline::Generate(Seed, HeightMeters, ResolutionY, World, Erreur))
	{
		Erreur = FString::Printf(TEXT("generation impossible : %s"), *Erreur);
		return false;
	}

	Regles = WorldseedPipeline::GetRules(Erreur);
	if (!Regles)
	{
		Erreur = FString::Printf(TEXT("regles illisibles : %s"), *Erreur);
		return false;
	}

	Litho = FWorldseedLithologyRules::FromRules(*Regles);
	DensiteRegles = FWorldseedDensityRules::FromRules(*Regles);

	Densite.Init(World.Geometry, World.ElevationM, 1.0f, Seed, DensiteRegles);

	// LA LITHOLOGIE EST INDISPENSABLE, ET L'OUBLIER REND LA SONDE
	// SILENCIEUSEMENT FAUSSE. Sans elle, KarstifiableAt rend 1 partout, le
	// terme de diaclase sort a la premiere garde, et l'on mesure le monde
	// d'avant en croyant mesurer le nouveau.
	if (World.Lithology.IsValid(World.Geometry.CellCount()))
	{
		Densite.SetLithology(World.Lithology, Litho);
	}

	bValide = true;
	return true;
}
