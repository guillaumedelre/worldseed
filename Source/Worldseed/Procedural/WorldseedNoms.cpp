// Worldseed - le generateur de noms : chaine de Markov sur 43 corpus.
//
// PORTE D'AZGAAR'S FANTASY MAP GENERATOR, `src/generators/names-generator.ts`,
// sous licence MIT -- Copyright (c) 2017 Azgaar. L'attribution complete est
// dans `Content/Worldseed/Data/PROVENANCE.md`.
//
// LE PORTAGE EST FIDELE, Y COMPRIS DANS SES BIZARRERIES, et c'est delibere :
// ce sont elles qui font sonner les noms comme ceux de l'original. Chacune est
// signalee la ou elle se trouve, pour qu'on ne la « corrige » pas un jour sans
// savoir ce qu'on change.

#include "Procedural/WorldseedNoms.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldseedNoms, Log, All);

namespace
{
	// ⚠ AUCUN CONTENEUR GLOBAL A INITIALISATION DYNAMIQUE DANS CE MODULE.
	//
	// Une `TArray` ou une `TMap` posee a portee de fichier se construit au
	// chargement de la DLL -- donc AVANT que le moteur soit pret -- et se
	// detruit APRES le depart de son allocateur. Paye comptant le 26 septembre
	// 2026 : `EXCEPTION_ACCESS_VIOLATION` dans CoreUObject au demarrage, bien
	// avant qu'une ligne de Worldseed ne tourne, donc sans le moindre indice
	// pointant vers ce fichier. Une statique LOCALE se construit a son premier
	// appel, et la construction est thread-safe depuis C++11.

	TArray<FWorldseedBaseDeNoms>& Bases()
	{
		static TArray<FWorldseedBaseDeNoms> Tables;
		return Tables;
	}

	TArray<FString>& OrdreCharge()
	{
		static TArray<FString> Ordre;
		return Ordre;
	}

	bool bCharge = false;

	FString Chemin()
	{
		// LA DONNEE VIT DANS `Content/Worldseed/Data/`, avec la police
		// d'icones et pour la meme raison : c'est du CONTENU, que le jeu lit
		// -- pas une regle de generation du monde.
		//
		// ⚠ ELLE N'ENTRE PAS DANS L'EMPREINTE DU CACHE, et c'est voulu :
		// `world_rules.json` est hache EN ENTIER, si bien qu'y toucher -- meme
		// un commentaire -- regenere tous les mondes. Retoucher un corpus de
		// noms ne doit pas couter quatre minutes de tectonique.
		return FPaths::Combine(FPaths::ProjectContentDir(),
			TEXT("Worldseed/Data/noms.json"));
	}

	bool EstVoyelle(TCHAR C)
	{
		// LA LISTE EST CELLE D'AZGAAR (`utils/stringUtils.ts`), accents
		// compris : ses corpus portent du « ü », du « ø » et du « å », et les
		// ignorer changerait le decoupage en syllabes de six langues.
		static const FString Voyelles = TEXT("aeiouyAEIOUY")
			TEXT("aeiouyàèìòùáéíóúýâêîôûãñõäëïöüÿåæø")
			TEXT("ÀÈÌÒÙÁÉÍÓÚÝÂÊÎÔÛÃÑÕÄËÏÖÜŸÅÆØ");
		return Voyelles.Contains(FString::Chr(C), ESearchCase::CaseSensitive);
	}

	/** Un element au hasard. Le tirage vient de l'appelant, jamais d'ailleurs. */
	const FString& AuHasard(const TArray<FString>& Liste, FRandomStream& Rng)
	{
		static const FString Vide;
		if (Liste.Num() == 0)
		{
			return Vide;
		}
		return Liste[Rng.RandHelper(Liste.Num())];
	}

	/** L'equivalent du `P(x)` d'Azgaar : vrai avec la probabilite donnee. */
	bool Chance(float P, FRandomStream& Rng)
	{
		return Rng.GetFraction() < P;
	}

	/**
	 * DECOUPE LES MOTS D'UN CORPUS EN PSEUDO-SYLLABES et batit la chaine.
	 *
	 * La cle est la lettre qui PRECEDE la syllabe -- chaine vide en debut de
	 * mot. C'est donc un Markov d'ordre 1 sur les lettres, mais qui emet des
	 * SYLLABES : c'est ce compromis qui lui donne son air de langue sans
	 * demander un corpus enorme.
	 *
	 * UNE SYLLABE VIDE EST LE MARQUEUR DE FIN DE MOT. Le dernier tour de la
	 * boucle exterieure n'a plus rien a decouper et pousse « » : la generation
	 * s'arrete en la tirant. Sans elle, aucun mot ne finirait.
	 */
	void BatirLaChaine(FWorldseedBaseDeNoms& Base)
	{
		Base.Chaine.Reset();

		for (const FString& Brut : Base.Mots)
		{
			const FString Nom = Brut.ToLower();
			const int32 Len = Nom.Len();

			// `basic` chez Azgaar : ASCII imprimable, donc les regles
			// anglaises de diphtongues s'appliquent. Un corpus accentue --
			// francais, nordique, hongrois -- ne les recoit pas.
			bool bBasic = true;
			for (int32 K = 0; K < Len; ++K)
			{
				if (Nom[K] < 0x20 || Nom[K] > 0x7e) { bBasic = false; break; }
			}

			for (int32 I = -1; I < Len; )
			{
				const FString Prev = (I >= 0) ? FString::Chr(Nom[I]) : FString();

				FString Syllabe;
				bool bAVoyelle = false;

				for (int32 C = I + 1; C < Len && Syllabe.Len() < 5; ++C)
				{
					const TCHAR Ici = Nom[C];
					const TCHAR Suiv = (C + 1 < Len) ? Nom[C + 1] : TEXT('\0');

					Syllabe.AppendChar(Ici);
					if (Syllabe == TEXT(" ") || Syllabe == TEXT("-")) { break; }
					if (Suiv == TEXT('\0') || Suiv == TEXT(' ')
						|| Suiv == TEXT('-'))
					{
						break;
					}

					if (EstVoyelle(Ici)) { bAVoyelle = true; }

					// Diphtongues qu'on ne coupe pas.
					if (Ici == TEXT('y') && Suiv == TEXT('e')) { continue; }
					if (bBasic)
					{
						if (Ici == TEXT('o') && Suiv == TEXT('o')) { continue; }
						if (Ici == TEXT('e') && Suiv == TEXT('e')) { continue; }
						if (Ici == TEXT('a') && Suiv == TEXT('e')) { continue; }
						if (Ici == TEXT('c') && Suiv == TEXT('h')) { continue; }
					}

					// ⚠ UNE LIGNE DE L'ORIGINAL EST ABSENTE ICI, ET C'EST
					// VOULU. Elle s'ecrit `if (isVowel(that) === next) break;`
					// -- un BOOLEEN compare a une CHAINE. En JavaScript strict
					// cette egalite est TOUJOURS FAUSSE, donc la condition ne
					// se declenche jamais : le portage TypeScript a du la
					// caster pour compiler et son commentaire la nomme
					// « original quirky behavior ». La transcrire fidelement en
					// C++ demanderait d'ecrire `if (false)`. La rendre
					// « juste » -- couper sur deux voyelles de suite --
					// changerait le decoupage de TOUS les corpus, donc tous les
					// noms du jeu.
					if (bAVoyelle && C + 2 < Len && EstVoyelle(Nom[C + 2]))
					{
						break;
					}
				}

				Base.Chaine.FindOrAdd(Prev).Add(Syllabe);
				I += (Syllabe.Len() > 0) ? Syllabe.Len() : 1;
			}
		}

		Base.bChainePrete = true;
	}

	/** Colle un suffixe en rabotant ce qui l'empeche de sonner. */
	FString ValiderSuffixe(FString Nom, const FString& Suffixe)
	{
		if (Suffixe.IsEmpty() || Nom.IsEmpty())
		{
			return Nom + Suffixe;
		}
		if (Nom.EndsWith(Suffixe, ESearchCase::CaseSensitive))
		{
			return Nom;
		}

		const TCHAR S1 = Suffixe[0];
		auto Dernier = [&Nom]() { return Nom.IsEmpty() ? TEXT('\0') : Nom[Nom.Len() - 1]; };
		auto Avant = [&Nom]() { return Nom.Len() >= 2 ? Nom[Nom.Len() - 2] : TEXT('\0'); };

		if (Dernier() == S1) { Nom.LeftChopInline(1); }
		if (!Nom.IsEmpty()
			&& EstVoyelle(S1) == EstVoyelle(Dernier())
			&& EstVoyelle(S1) == EstVoyelle(Avant()))
		{
			Nom.LeftChopInline(1);
		}
		if (!Nom.IsEmpty() && Dernier() == S1) { Nom.LeftChopInline(1); }
		return Nom + Suffixe;
	}

	FWorldseedBaseDeNoms* TrouverMutable(const FString& Nom)
	{
		for (FWorldseedBaseDeNoms& B : Bases())
		{
			if (B.Nom == Nom) { return &B; }
		}
		return nullptr;
	}
}

bool WorldseedNoms::Charger(FString& OutErreur)
{
	if (bCharge)
	{
		return true;
	}

	FString Texte;
	if (!FFileHelper::LoadFileToString(Texte, *Chemin()))
	{
		OutErreur = FString::Printf(
			TEXT("corpus de noms introuvable (%s)"), *Chemin());
		return false;
	}

	TSharedPtr<FJsonObject> Racine;
	const TSharedRef<TJsonReader<>> Lecteur = TJsonReaderFactory<>::Create(Texte);
	if (!FJsonSerializer::Deserialize(Lecteur, Racine) || !Racine.IsValid())
	{
		OutErreur = FString::Printf(
			TEXT("corpus de noms illisible (%s)"), *Chemin());
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Tableau = nullptr;
	if (!Racine->TryGetArrayField(TEXT("bases"), Tableau) || !Tableau)
	{
		OutErreur = TEXT("corpus de noms : la cle « bases » manque");
		return false;
	}

	Bases().Reset();
	for (const TSharedPtr<FJsonValue>& V : *Tableau)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!V.IsValid() || !V->TryGetObject(Obj) || !Obj)
		{
			continue;
		}

		FWorldseedBaseDeNoms B;
		(*Obj)->TryGetStringField(TEXT("nom"), B.Nom);
		(*Obj)->TryGetNumberField(TEXT("i"), B.Index);
		(*Obj)->TryGetNumberField(TEXT("min"), B.Min);
		(*Obj)->TryGetNumberField(TEXT("max"), B.Max);
		(*Obj)->TryGetStringField(TEXT("dupl"), B.Dupl);

		const TArray<TSharedPtr<FJsonValue>>* Mots = nullptr;
		if ((*Obj)->TryGetArrayField(TEXT("mots"), Mots) && Mots)
		{
			B.Mots.Reserve(Mots->Num());
			for (const TSharedPtr<FJsonValue>& M : *Mots)
			{
				FString S;
				if (M.IsValid() && M->TryGetString(S) && !S.IsEmpty())
				{
					B.Mots.Add(MoveTemp(S));
				}
			}
		}

		if (!B.Nom.IsEmpty() && B.Mots.Num() > 0)
		{
			Bases().Add(MoveTemp(B));
		}
	}

	// ORDONNEES PAR INDEX D'ORIGINE, jamais par ordre de lecture : l'index est
	// portant -- les suffixes d'Etat le testent par numero -- et l'ordre d'un
	// tableau JSON, lui, n'est garanti par rien apres un remaniement.
	Bases().Sort([](const FWorldseedBaseDeNoms& A, const FWorldseedBaseDeNoms& B)
	{
		return A.Index < B.Index;
	});

	OrdreCharge().Reset();
	int32 TotalMots = 0;
	for (const FWorldseedBaseDeNoms& B : Bases())
	{
		OrdreCharge().Add(B.Nom);
		TotalMots += B.Mots.Num();
	}

	bCharge = true;
	UE_LOG(LogWorldseedNoms, Log,
		TEXT("[Worldseed] noms : %d bases, %d mots (%s)"),
		Bases().Num(), TotalMots, *FPaths::GetCleanFilename(Chemin()));
	return true;
}

const TArray<FString>& WorldseedNoms::Ordre()
{
	FString Erreur;
	if (!Charger(Erreur))
	{
		UE_LOG(LogWorldseedNoms, Warning, TEXT("[Worldseed] %s"), *Erreur);
	}
	return OrdreCharge();
}

const FWorldseedBaseDeNoms* WorldseedNoms::Trouver(const FString& Nom)
{
	FString Erreur;
	if (!Charger(Erreur))
	{
		UE_LOG(LogWorldseedNoms, Warning, TEXT("[Worldseed] %s"), *Erreur);
	}
	return TrouverMutable(Nom);
}

FString WorldseedNoms::Mot(const FString& Base, FRandomStream& Rng,
	int32 Min, int32 Max)
{
	FString Erreur;
	if (!Charger(Erreur))
	{
		return FString();
	}

	FWorldseedBaseDeNoms* const B = TrouverMutable(Base);
	if (!B)
	{
		return FString();
	}
	if (!B->bChainePrete)
	{
		BatirLaChaine(*B);
	}

	const TArray<FString>* const Depart = B->Chaine.Find(FString());
	if (!Depart || Depart->Num() == 0)
	{
		return FString();
	}

	if (Min <= 0) { Min = B->Min; }
	if (Max <= 0) { Max = B->Max; }

	// --- la marche aleatoire dans la chaine ---------------------------------
	//
	// VINGT TOURS AU PLUS, comme l'original. Ce n'est pas une precaution
	// theorique : quand la longueur minimale ne peut pas etre atteinte, la
	// boucle REPART de zero, et sans plafond elle tournerait sans fin.
	const TArray<FString>* Courant = Depart;
	FString Syllabe = AuHasard(*Courant, Rng);
	FString Mot;

	for (int32 Tour = 0; Tour < 20; ++Tour)
	{
		if (Syllabe.IsEmpty())
		{
			// Fin de mot tiree. Trop court : on recommence.
			if (Mot.Len() < Min)
			{
				Mot.Reset();
				Courant = Depart;
			}
			else
			{
				break;
			}
		}
		else
		{
			if (Mot.Len() + Syllabe.Len() > Max)
			{
				// Trop long : on ne garde la syllabe que si l'on est encore
				// sous le minimum -- mieux vaut depasser que rendre un moignon.
				if (Mot.Len() < Min) { Mot += Syllabe; }
				break;
			}

			const FString Derniere = FString::Chr(Syllabe[Syllabe.Len() - 1]);
			const TArray<FString>* const Suite = B->Chaine.Find(Derniere);
			Courant = (Suite && Suite->Num() > 0) ? Suite : Depart;
		}

		Mot += Syllabe;
		Syllabe = AuHasard(*Courant, Rng);
	}

	// --- le post-traitement --------------------------------------------------
	if (!Mot.IsEmpty())
	{
		const TCHAR Fin = Mot[Mot.Len() - 1];
		if (Fin == TEXT('\'') || Fin == TEXT(' ') || Fin == TEXT('-'))
		{
			Mot.LeftChopInline(1);
		}
	}

	FString Sortie;
	Sortie.Reserve(Mot.Len());
	for (int32 I = 0; I < Mot.Len(); ++I)
	{
		const TCHAR C = Mot[I];
		const TCHAR Suiv = (I + 1 < Mot.Len()) ? Mot[I + 1] : TEXT('\0');

		// Doublement interdit, SAUF pour les lettres que cette langue autorise.
		if (C == Suiv && !B->Dupl.Contains(FString::Chr(C), ESearchCase::CaseSensitive))
		{
			continue;
		}
		if (Sortie.IsEmpty())
		{
			Sortie.AppendChar(FChar::ToUpper(C));
			continue;
		}

		const TCHAR Precedent = Sortie[Sortie.Len() - 1];
		if (Precedent == TEXT('-') && C == TEXT(' ')) { continue; }
		if (Precedent == TEXT(' ') || Precedent == TEXT('-'))
		{
			Sortie.AppendChar(FChar::ToUpper(C));
			continue;
		}
		// « ae » -> « e ».
		if (C == TEXT('a') && Suiv == TEXT('e')) { continue; }
		// Trois identiques de suite, y compris parmi les lettres doublables.
		if (I + 2 < Mot.Len() && C == Suiv && C == Mot[I + 2]) { continue; }

		Sortie.AppendChar(C);
	}

	// UN MORCEAU D'UNE SEULE LETTRE recolle le mot : « A Bendor » est un
	// accident du decoupage, pas un nom.
	TArray<FString> Morceaux;
	Sortie.ParseIntoArray(Morceaux, TEXT(" "), false);
	bool bTropCourt = false;
	for (const FString& M : Morceaux)
	{
		if (M.Len() < 2) { bTropCourt = true; break; }
	}
	if (bTropCourt && Morceaux.Num() > 1)
	{
		FString Colle = Morceaux[0];
		for (int32 I = 1; I < Morceaux.Num(); ++I)
		{
			Colle += Morceaux[I].ToLower();
		}
		Sortie = Colle;
	}

	// DERNIER RECOURS : un mot du corpus. Il vaut mieux un nom reel repete
	// qu'une syllabe isolee.
	if (Sortie.Len() < 2)
	{
		Sortie = AuHasard(B->Mots, Rng);
	}
	return Sortie;
}

FString WorldseedNoms::MotCourt(const FString& Base, FRandomStream& Rng)
{
	const FWorldseedBaseDeNoms* const B = Trouver(Base);
	if (!B)
	{
		return FString();
	}
	const int32 Min = FMath::Max(1, B->Min - 1);
	const int32 Max = FMath::Max(B->Max - 2, Min);
	return Mot(Base, Rng, Min, Max);
}

FString WorldseedNoms::Etat(const FString& Base, const FString& Racine,
	FRandomStream& Rng)
{
	const FWorldseedBaseDeNoms* const B = Trouver(Base);
	if (!B)
	{
		return FString();
	}

	FString Nom = Racine.IsEmpty() ? Mot(Base, Rng) : Racine;
	if (Nom.IsEmpty())
	{
		return Nom;
	}

	// ⚠ LES TESTS PORTENT SUR L'INDEX D'ORIGINE D'AZGAAR, pas sur le libelle.
	// C'est ce qui rend cet index portant, et c'est dit dans l'en-tete.
	const int32 Id = B->Index;

	// Pas de nom d'Etat en plusieurs mots.
	if (Nom.Contains(TEXT(" ")))
	{
		Nom = Nom.Replace(TEXT(" "), TEXT("")).ToLower();
		if (!Nom.IsEmpty()) { Nom[0] = FChar::ToUpper(Nom[0]); }
	}
	// Terminaisons qui ne conviennent pas a un Etat.
	if (Nom.Len() > 6 && Nom.EndsWith(TEXT("berg"))) { Nom.LeftChopInline(4); }
	if (Nom.Len() > 5 && Nom.EndsWith(TEXT("ton"))) { Nom.LeftChopInline(3); }

	auto Fin2 = [&Nom]() { return Nom.Len() >= 2 ? Nom.Right(2) : FString(); };
	auto Dernier = [&Nom]() { return Nom.IsEmpty() ? TEXT('\0') : Nom[Nom.Len() - 1]; };
	auto Avant = [&Nom]() { return Nom.Len() >= 2 ? Nom[Nom.Len() - 2] : TEXT('\0'); };

	if (Id == 5)
	{
		// Ruthene : on retire -sk / -ev / -ov.
		const FString F = Fin2();
		if (F == TEXT("sk") || F == TEXT("ev") || F == TEXT("ov"))
		{
			Nom.LeftChopInline(2);
		}
	}
	else if (Id == 12)
	{
		// Japonais : finit sur une voyelle, ou sur -u.
		return EstVoyelle(Dernier()) ? Nom : Nom + TEXT("u");
	}
	else if (Id == 18 && Chance(0.4f, Rng))
	{
		// Arabe : prefixe Al-.
		Nom = EstVoyelle(Nom[0])
			? FString(TEXT("Al")) + Nom.ToLower()
			: FString(TEXT("Al ")) + Nom;
	}

	// LES ONZE BASES DE FANTASY NE PRENNENT AUCUN SUFFIXE : elles portent deja
	// leur couleur dans le mot, et « Draconia » sonnerait romain.
	if (Id > 32 && Id < 42)
	{
		return Nom;
	}

	// Faut-il seulement un suffixe ?
	if (Nom.Len() > 3 && EstVoyelle(Dernier()))
	{
		if (EstVoyelle(Avant()) && Chance(0.85f, Rng))
		{
			Nom.LeftChopInline(2);
		}
		else if (Chance(0.7f, Rng))
		{
			Nom.LeftChopInline(1);
		}
		else
		{
			return Nom;
		}
	}
	else if (Chance(0.4f, Rng))
	{
		return Nom;
	}

	FString Suffixe = TEXT("ia");
	const float Tirage = Rng.GetFraction();
	const int32 L = Nom.Len();

	if (Id == 3 && Tirage < 0.03f && L < 7) { Suffixe = TEXT("terra"); }        // italien
	else if (Id == 4 && Tirage < 0.03f && L < 7) { Suffixe = TEXT("terra"); }   // castillan
	else if (Id == 13 && Tirage < 0.03f && L < 7) { Suffixe = TEXT("terra"); }  // portugais
	else if (Id == 2 && Tirage < 0.03f && L < 7) { Suffixe = TEXT("terre"); }   // francais
	else if (Id == 0 && Tirage < 0.5f && L < 7) { Suffixe = TEXT("land"); }     // allemand
	else if (Id == 1 && Tirage < 0.4f && L < 7) { Suffixe = TEXT("land"); }     // anglais
	else if (Id == 6 && Tirage < 0.3f && L < 7) { Suffixe = TEXT("land"); }     // nordique
	else if (Id == 32 && Tirage < 0.1f && L < 7) { Suffixe = TEXT("land"); }    // humain
	else if (Id == 7 && Tirage < 0.1f) { Suffixe = TEXT("eia"); }               // grec
	else if (Id == 9 && Tirage < 0.35f) { Suffixe = TEXT("maa"); }              // finnois
	else if (Id == 15 && Tirage < 0.4f && L < 6) { Suffixe = TEXT("orszag"); }  // hongrois
	else if (Id == 16) { Suffixe = (Tirage < 0.6f) ? TEXT("yurt") : TEXT("eli"); } // turc
	else if (Id == 10) { Suffixe = TEXT("guk"); }                               // coreen
	else if (Id == 11) { Suffixe = TEXT(" Guo"); }                              // chinois
	else if (Id == 14) { Suffixe = (Tirage < 0.5f && L < 6) ? TEXT("tlan") : TEXT("co"); } // nahuatl
	else if (Id == 17 && Tirage < 0.8f) { Suffixe = TEXT("a"); }                // berbere
	else if (Id == 18 && Tirage < 0.8f) { Suffixe = TEXT("a"); }                // arabe

	return ValiderSuffixe(Nom, Suffixe);
}

FString WorldseedNoms::Personne(const FString& Base, bool bComplet,
	FRandomStream& Rng)
{
	FString Nom = Mot(Base, Rng);
	if (bComplet && !Nom.IsEmpty())
	{
		const FString Famille = Mot(Base, Rng);
		if (!Famille.IsEmpty())
		{
			Nom += TEXT(" ") + Famille;
		}
	}
	return Nom;
}

void WorldseedNoms::ViderLeCache()
{
	Bases().Reset();
	OrdreCharge().Reset();
	bCharge = false;
}
