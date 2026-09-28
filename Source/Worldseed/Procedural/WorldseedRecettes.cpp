// Worldseed - les recettes de vegetation : quel biome porte quoi, et a quel pas.

#include "Procedural/WorldseedRecettes.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

TArray<FString> FWorldseedRecettes::CheminsCandidats()
{
	TArray<FString> Chemins;

	// LA VUE ACTIVE D'ABORD, LE CATALOGUE EN REPLI, et l'ordre a ete inverse une
	// fois. `vegetation_recipes.json` est la palette EN VIGUEUR, filtree ;
	// `.complet.json` est le CATALOGUE, toutes familles confondues. C'est la
	// premiere qui decide de l'aspect du monde -- le registre le dit : « les
	// deux sont versionnes, donc un clone frais retrouve le catalogue entier ET
	// la palette en vigueur ».
	//
	// POURQUOI L'ORDRE ETAIT INVERSE AU DEPART : la vue active etait VIDE --
	// ses seize biomes portaient tous `layers: []` -- et lire le catalogue
	// garantissait d'avoir de la matiere. C'etait un pansement : il faisait
	// semer des maillages de neuf packs alors que le proprietaire n'en veut
	// qu'un, et c'est ce qui a mis un palmier egyptien dans un monde Orasot.
	// La vue active est desormais regeneree et non vide ; le repli ne sert plus
	// qu'a un depot ou elle manquerait.
	Chemins.Add(FPaths::ConvertRelativePathToFull(
		FPaths::ProjectDir() / TEXT("Tools/UE/vegetation_recipes.json")));
	Chemins.Add(FPaths::ConvertRelativePathToFull(
		FPaths::ProjectDir() / TEXT("Tools/UE/vegetation_recipes.complet.json")));

	// Repli pour un build package, ou `Tools/` n'est pas embarque.
	Chemins.Add(FPaths::ConvertRelativePathToFull(
		FPaths::ProjectContentDir() / TEXT("Worldseed/Rules/vegetation_recipes.json")));

	return Chemins;
}

bool FWorldseedRecettes::Charger(FString& OutErreur)
{
	ParBiome.Reset();
	Catalogue.Reset();
	EspeceObstacle.Reset();
	RayonEspeceCm.Reset();
	NbCouches = 0;

	FString Brut;
	FString Trouve;
	for (const FString& C : CheminsCandidats())
	{
		if (FFileHelper::LoadFileToString(Brut, *C))
		{
			Trouve = C;
			break;
		}
	}
	if (Trouve.IsEmpty())
	{
		OutErreur = TEXT("aucun fichier de recettes trouve");
		return false;
	}

	TSharedPtr<FJsonObject> Racine;
	TSharedRef<TJsonReader<>> Lecteur = TJsonReaderFactory<>::Create(Brut);
	if (!FJsonSerializer::Deserialize(Lecteur, Racine) || !Racine.IsValid())
	{
		OutErreur = FString::Printf(TEXT("JSON invalide : %s"), *Trouve);
		return false;
	}

	// --- LES RACINES, QUI RESOLVENT LES PREFIXES ----------------------------
	TMap<FString, FString> Racines;
	if (const TSharedPtr<FJsonObject>* R = nullptr;
		Racine->TryGetObjectField(TEXT("roots"), R))
	{
		for (const auto& Paire : (*R)->Values)
		{
			FString V;
			if (Paire.Value->TryGetString(V))
			{
				Racines.Add(FString(StringCast<TCHAR>(*Paire.Key).Get()), V);
			}
		}
	}

	// --- LES PAS DE GRILLE, DESIGNES PAR LEUR NOM ---------------------------
	//
	// LE FICHIER INTERDIT LES NOMBRES EN DUR, et il a raison : la densite est LE
	// reglage qui decide du cout comme du rendu, et la voir ecrite en sept
	// endroits differents serait le meilleur moyen de la faire diverger.
	TMap<FString, float> Pas;
	if (const TSharedPtr<FJsonObject>* S = nullptr;
		Racine->TryGetObjectField(TEXT("spacings"), S))
	{
		for (const auto& Paire : (*S)->Values)
		{
			double V = 0.0;
			if (Paire.Value->TryGetNumber(V))
			{
				Pas.Add(FString(StringCast<TCHAR>(*Paire.Key).Get()), static_cast<float>(V));
			}
		}
	}

	const TSharedPtr<FJsonObject>* Biomes = nullptr;
	if (!Racine->TryGetObjectField(TEXT("biomes"), Biomes))
	{
		OutErreur = TEXT("section 'biomes' absente");
		return false;
	}

	TMap<FString, int32> IndexParChemin;

	// --- LIRE UN TABLEAU `layers`, ET UNE SEULE FOIS -----------------------
	//
	// EXTRAITE PARCE QUE L'ESTRAN A LE MEME FORMAT. Recopier cette lecture
	// pour lui aurait garanti qu'elles divergent a la premiere cle ajoutee --
	// c'est la regle du depot, celle qui a fait sortir l'amplitude saisonniere
	// de l'export des prereglages apres qu'il l'eut recalculee de son cote.
	auto LireCouches = [&](const TArray<TSharedPtr<FJsonValue>>& Couches)
	{
		FWorldseedBiomeRecette Recette;

		for (const TSharedPtr<FJsonValue>& ValCouche : Couches)
		{
			const TSharedPtr<FJsonObject>* C = nullptr;
			if (!ValCouche->TryGetObject(C))
			{
				continue;
			}

			FWorldseedCoucheRecette Couche;
			(*C)->TryGetStringField(TEXT("name"), Couche.Nom);

			FString NomPas;
			if ((*C)->TryGetStringField(TEXT("spacing"), NomPas))
			{
				if (const float* P = Pas.Find(NomPas))
				{
					Couche.PasCm = *P;
				}
			}

			// UN TABLEAU DE DEUX NOMBRES, ET IL PEUT MANQUER. On ne remplace
			// jamais une valeur absente par une invention : on garde le defaut
			// de la structure, qui est neutre.
			auto LirePaire = [&C](const TCHAR* Cle, float& A, float& B)
			{
				const TArray<TSharedPtr<FJsonValue>>* T = nullptr;
				if ((*C)->TryGetArrayField(Cle, T) && T->Num() >= 2)
				{
					A = static_cast<float>((*T)[0]->AsNumber());
					B = static_cast<float>((*T)[1]->AsNumber());
				}
			};
			LirePaire(TEXT("scale"), Couche.EchelleMin, Couche.EchelleMax);
			LirePaire(TEXT("pente"), Couche.PenteMinDeg, Couche.PenteMaxDeg);
			LirePaire(TEXT("cull"), Couche.CullDebutCm, Couche.CullFinCm);

			// ABSENT = FAUX, donc une recette qui ne connait pas cette cle se
			// comporte exactement comme avant. C'est ce qui permet de la poser
			// couche par couche sans casser le reste du fichier.
			(*C)->TryGetBoolField(TEXT("obstacle"), Couche.bObstacle);

			const TSharedPtr<FJsonObject>* Taches = nullptr;
			if ((*C)->TryGetObjectField(TEXT("taches"), Taches))
			{
				double V = 0.0;
				if ((*Taches)->TryGetNumberField(TEXT("taille"), V))
				{
					Couche.TacheTailleCm = static_cast<float>(V);
				}
				if ((*Taches)->TryGetNumberField(TEXT("seuil"), V))
				{
					Couche.TacheSeuil = static_cast<float>(V);
				}
				// LES DEUX SUIVANTES SONT NEUTRES PAR DEFAUT : une couche qui
				// ne les connait pas se comporte exactement comme avant. Les
				// cinquante-trois couches sans taches ne bougent pas non plus,
				// puisque rien de tout ceci ne s'evalue sans `taille`.
				if ((*Taches)->TryGetNumberField(TEXT("octaves"), V))
				{
					Couche.TacheOctaves = FMath::Clamp(
						static_cast<int32>(V), 1, 8);
				}
				if ((*Taches)->TryGetNumberField(TEXT("douceur"), V))
				{
					Couche.TacheDouceur = FMath::Clamp(
						static_cast<float>(V), 0.0f, 1.0f);
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* Especes = nullptr;
			if ((*C)->TryGetArrayField(TEXT("meshes"), Especes))
			{
				for (const TSharedPtr<FJsonValue>& E : *Especes)
				{
					const TArray<TSharedPtr<FJsonValue>>* Couple = nullptr;
					if (!E->TryGetArray(Couple) || Couple->Num() < 1)
					{
						continue;
					}

					const FString Ref = (*Couple)[0]->AsString();
					int32 Sep = INDEX_NONE;
					if (!Ref.FindChar(TEXT(':'), Sep))
					{
						continue;
					}
					const FString Prefixe = Ref.Left(Sep);
					const FString Suffixe = Ref.Mid(Sep + 1);
					const FString* Base = Racines.Find(Prefixe);
					if (!Base)
					{
						continue;
					}

					// LE SUFFIXE PEUT PORTER UN SOUS-DOSSIER -- « RACINE:Dossier/Nom ».
					// Ajoute quand Stylized_Rocks a range ses vingt-six rochers
					// dans vingt-six dossiers ; sans « / », le comportement est
					// celui d'avant, au caractere pres.
					FString Nom = Suffixe;
					int32 Slash = INDEX_NONE;
					if (Suffixe.FindLastChar(TEXT('/'), Slash))
					{
						Nom = Suffixe.Mid(Slash + 1);
					}

					FWorldseedEspece Espece;
					Espece.Chemin = FString::Printf(TEXT("%s/%s.%s"),
						**Base, *Suffixe, *Nom);
					Espece.Poids = (Couple->Num() >= 2)
						? static_cast<float>((*Couple)[1]->AsNumber()) : 1.0f;
					if (Espece.Poids <= 0.0f)
					{
						continue;
					}

					if (const int32* Deja = IndexParChemin.Find(Espece.Chemin))
					{
						Espece.IndexCatalogue = *Deja;
					}
					else
					{
						Espece.IndexCatalogue = Catalogue.Num();
						IndexParChemin.Add(Espece.Chemin, Espece.IndexCatalogue);
						Catalogue.Add(Espece.Chemin);
						EspeceObstacle.Add(false);
					}

					// UNION, ET NON AFFECTATION : le meme rocher est cite par
					// une couche d'eboulis ET par une couche de galets. S'il
					// est de la roche quelque part, il l'est partout -- la
					// derniere couche lue n'a pas a effacer les precedentes.
					if (Couche.bObstacle
						&& EspeceObstacle.IsValidIndex(Espece.IndexCatalogue))
					{
						EspeceObstacle[Espece.IndexCatalogue] = true;
					}

					Couche.Especes.Add(Espece);
					Couche.PoidsTotal += Espece.Poids;
				}
			}

			if (Couche.Especes.Num() > 0)
			{
				Recette.Couches.Add(MoveTemp(Couche));
				++NbCouches;
			}
		}

		return Recette;
	};

	for (const auto& PaireBiome : (*Biomes)->Values)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!PaireBiome.Value->TryGetObject(Obj))
		{
			continue;
		}

		double IdD = -1.0;
		if (!(*Obj)->TryGetNumberField(TEXT("id"), IdD))
		{
			continue;
		}
		const int32 Id = static_cast<int32>(IdD);

		const TArray<TSharedPtr<FJsonValue>>* Couches = nullptr;
		if (!(*Obj)->TryGetArrayField(TEXT("layers"), Couches))
		{
			continue;
		}

		FWorldseedBiomeRecette Recette = LireCouches(*Couches);
		if (Recette.Couches.Num() > 0)
		{
			ParBiome.Add(Id, MoveTemp(Recette));
		}
	}

	// --- L'ESTRAN, QUI N'EST PAS UN BIOME ----------------------------------
	//
	// SA SECTION EST FACULTATIVE : un fichier de recettes qui l'ignore garde
	// exactement le comportement d'avant, plages comprises. Une donnee absente
	// doit rester sans effet.
	const TArray<TSharedPtr<FJsonValue>>* CouchesEstran = nullptr;
	if (Racine->TryGetArrayField(TEXT("estran"), CouchesEstran))
	{
		Estran = LireCouches(*CouchesEstran);
	}

	if (ParBiome.Num() == 0)
	{
		OutErreur = FString::Printf(
			TEXT("aucune couche lue dans %s -- le fichier est peut-etre une vue ")
			TEXT("filtree par la palette, dont toutes les couches sont vides"),
			*Trouve);
		return false;
	}

	OutErreur = Trouve;
	return true;
}
