// Worldseed - terrain voxel : diffusion des chunks autour du joueur.

#include "Procedural/WorldseedVoxelTerrain.h"
#include "Procedural/WorldseedPeinture.h"
#include "Procedural/WorldseedPlacement.h"
#include "Procedural/WorldseedPlateau.h"
#include "Procedural/WorldseedStrata.h"

#include "Procedural/WorldseedGameInstance.h"
#include "Procedural/WorldseedPipeline.h"
#include "Procedural/WorldseedTrace.h"

#include "Async/Async.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "TimerManager.h"

AWorldseedVoxelTerrain::AWorldseedVoxelTerrain()
{
	PrimaryActorTick.bCanEverTick = false;

	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(RootScene);
}

// ---------------------------------------------------------------- chargement

void AWorldseedVoxelTerrain::DemanderDepart(const FVector2D& XYMetres)
{
	bDepartDemande = true;
	DepartXYM = XYMetres;
}

void AWorldseedVoxelTerrain::AdoptWorld(FWorldseedMondeRef InMonde,
	float InHeightExaggeration, const FWorldseedCaveNetwork& InCaves,
	const FWorldseedLithology& InLithology)
{
	// NEUF PARAMETRES SONT DEVENUS QUATRE, et ce n'est pas qu'une affaire de
	// signature : les cinq qui ont disparu etaient les TABLEAUX DU MONDE, que
	// cette fonction recopiait un a un. Ils arrivent maintenant ensemble, par
	// reference, et le monde n'existe plus qu'en un exemplaire.
	//
	// Ce qui reste passe a part parce que ce n'est PAS le monde : le reseau de
	// grottes se rebatit a chaque chargement, la lithologie porte un catalogue
	// issu des regles, et l'exageration verticale est un reglage du terrain --
	// elle doit etre la MEME des deux cotes, sans quoi le sol de fond et le
	// relief proche decriraient deux echelles differentes.
	Monde = InMonde;
	WorldSeed = InMonde->Seed;
	Geometry = InMonde->Geometry;

	CaveNetwork = InCaves;
	Lithology = InLithology;
	HeightExaggeration = InHeightExaggeration;
	bWorldAdopted = true;
}

const TArray<float>& AWorldseedVoxelTerrain::FloatsVides()
{
	// Voir la note du meme nom dans AWorldseedTerrain : un tableau vide se
	// teste comme avant, un pointeur nul fait tomber.
	static const TArray<float> Vide;
	return Vide;
}

const FWorldseedDensity& AWorldseedVoxelTerrain::ChampVide()
{
	// MEME RAISON, ET LE MEME PIEGE EVITE. Un champ par defaut n'a ni relief ni
	// regles : il rend zero partout, ce qui se lit comme « pas de surface » et
	// non comme du terrain. Ce qu'il ne fait PAS, c'est tomber.
	static const FWorldseedDensity Vide;
	return Vide;
}

bool AWorldseedVoxelTerrain::LoadWorld()
{
	// UN MONDE ADOPTE NE SE RECHARGE PAS. C'est celui de l'acteur qui a pose
	// celui-ci, donc celui que le sol de fond et l'ocean decrivent deja.
	if (bWorldAdopted && Geometry.NX >= 2 && HeightsM().Num() == Geometry.CellCount())
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] voxel : monde repris du terrain, seed=%d  %dx%d"),
			WorldSeed, Geometry.NX, Geometry.NY);
		return true;
	}

	if (const UWorldseedGameInstance* GI =
		UWorldseedGameInstance::GetWorldseedGameInstance(this))
	{
		if (FWorldseedMondePtr Partage = GI->MondePartage())
		{
			Monde = Partage;
			WorldSeed = Monde->Seed;
			Geometry = Monde->Geometry;
			// Le reseau n'est pas transporte par le menu : il se rebatit ici.
			CaveNetwork.Reset();

			// LE DEPART CHOISI DANS LE MENU. Il ne remplace pas la mise en
			// place, il en deplace seulement le POINT DE DEPART : la recherche
			// de terre emergee puis de sol plat s'applique ensuite comme
			// toujours. Un clic sur un globe vise a une quinzaine de metres
			// pres, et sans cette recherche on pourrait naitre sur une paroi a
			// soixante degres ou au-dessus d'une galerie.
			bDepartDemande = Monde->bHasSpawn;
			DepartXYM = Monde->SpawnXYM;

			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : monde repris du menu, seed=%d  %dx%d%s"),
				WorldSeed, Geometry.NX, Geometry.NY,
				bDepartDemande
					? *FString::Printf(TEXT(", depart demande a (%.0f, %.0f) m"),
						DepartXYM.X, DepartXYM.Y)
					: TEXT(", depart libre"));
			return true;
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] voxel : aucun monde en attente, generation de secours"));

	WorldseedPipeline::FResult World;
	FString Error;
	if (!WorldseedPipeline::Generate(FallbackSeed, FallbackHeightMeters,
		FallbackResolutionY, World, Error))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Worldseed] voxel : generation de secours impossible : %s"), *Error);
		return false;
	}

	// MEME CHEMIN QUE LE MENU : un monde partage, et non des membres remplis.
	{
		FWorldseedWorldData Bati;
		Bati.Seed = FallbackSeed;
		Bati.Geometry = World.Geometry;
		Bati.ElevationM = MoveTemp(World.ElevationM);
		Bati.TempC = MoveTemp(World.Climate.TempMeanC);
		Bati.PrecipMm = MoveTemp(World.Climate.PrecipMm);
		Bati.SeasonalAmpC = MoveTemp(World.Climate.SeasonalAmpC);
		Bati.Continentality = MoveTemp(World.Climate.Continentality);
		Bati.Biomes = MoveTemp(World.Biomes);
		Monde = MakeShared<const FWorldseedWorldData, ESPMode::ThreadSafe>(MoveTemp(Bati));
	}

	WorldSeed = FallbackSeed;
	Geometry = World.Geometry;
	return true;
}

void AWorldseedVoxelTerrain::BeginPlay()
{
	Super::BeginPlay();

	StartSeconds = FPlatformTime::Seconds();

	// --- OU NAITRE, DEPUIS LA LIGNE DE COMMANDE ----------------------------
	//
	// POURQUOI CETTE SURCHARGE EXISTE, et elle a ete payee. Pour aller voir un
	// endroit precis il y avait deux chemins, et aucun ne tenait : le menu, qui
	// ne sait pas viser une coordonnee ; et `Worldseed.Aller` dans la console,
	// qu'il faut ouvrir au clavier. Or la touche console d'Unreal s'appelle
	// `Tilde` et vaut VK_OEM_3, ce qui est la touche a gauche du 1 sur QWERTY
	// mais la touche `u accent grave` sur AZERTY : elle ouvre la console ET
	// laisse son caractere dans la ligne, si bien que la commande devient
	// « uWorldseed.Aller ... » et ne s'execute jamais. Mesure : lu tel quel a
	// l'ecran par le proprietaire.
	//
	// Une surcharge de lancement ne depend ni du clavier ni du focus, et elle
	// rend le BALAYAGE possible : une serie de rivages, un lancement chacun,
	// une capture chacun.
	// DEUX PARAMETRES PLUTOT QU'UN COUPLE, ET CE N'EST PAS UN GOUT.
	// `FParse::Value` s'arrete a la VIRGULE, qu'il traite en delimiteur : un
	// « -WorldseedDepart=14875,9369 » ne rend que « 14875 », et le reste part
	// en silence. Mesure : les sept lancements d'un balayage sont tous partis
	// au point par defaut, (24, 24) m, sans que rien ne le signale -- sauf la
	// branche de refus ci-dessous, qui a fini par le dire. Deux cles nommees
	// n'ont aucun separateur a negocier.
	{
		float DepartX = 0.0f;
		float DepartY = 0.0f;
		const bool bX = FParse::Value(FCommandLine::Get(), TEXT("WorldseedDepartX="), DepartX);
		const bool bY = FParse::Value(FCommandLine::Get(), TEXT("WorldseedDepartY="), DepartY);

		if (bX && bY)
		{
			DepartXYM = FVector2D(DepartX, DepartY);
			bDepartDemande = true;

			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : depart impose par la ligne de ")
				TEXT("commande a (%.0f, %.0f) m"), DepartXYM.X, DepartXYM.Y);
		}
		else if (bX || bY)
		{
			// UN SEUL DES DEUX EST UNE ERREUR QUI DOIT SE VOIR. Le prendre pour
			// un depart a moitie demande poserait le joueur sur un axe, et l'on
			// chercherait pourquoi il n'est jamais ou on l'attend.
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] depart ignore : il faut -WorldseedDepartX= ET ")
				TEXT("-WorldseedDepartY=, en metres"));
		}
	}

	// Ce que la ligne de commande a impose : les regles ne le recouvriront pas.
	bool bRayonForce = false;
	bool bNiveauxForce = false;
	bool bAnneau0Force = false;

	// LE RAYON SE PILOTE DEPUIS LA LIGNE DE COMMANDE, pour le banc.
	// Sans ce levier, comparer deux rayons demanderait de recompiler entre
	// les deux mesures -- et le depot a une regle contre les A/B dont les
	// deux moities ne sont pas montees a l'identique.
	{
		float Rayon = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedRayon="), Rayon)
			&& Rayon > 32.0f)
		{
			// LE RAYON DE DECHARGEMENT SUIT, ET C'EST OBLIGATOIRE.
			//
			// DEFAUT DE BANC PAYE COMPTANT. La premiere version ne forcait que
			// LoadRadiusM : au-dela de `UnloadRadiusM`, fige a 350 m, les
			// chunks etaient batis puis DETRUITS aussitot. A 400 comme a 600 m
			// le monde tournait donc en boucle sur le meme millier de chunks,
			// et le releve rendait EXACTEMENT 1679 des deux cotes -- la
			// cinquieme fois dans ce depot qu'un chiffre identique au chiffre
			// pres trahit un plafond cache. Toutes les conclusions tirees de
			// ces mesures, dont « le debit est le mur », portaient sur une
			// configuration cassee.
			//
			// L'hysteresis d'origine -- 350 pour 250, soit 1,4 fois -- est
			// conservee : sans elle, un pas en avant et un pas en arriere sur
			// la frontiere feraient construire et detruire le meme chunk en
			// boucle.
			const float Hysteresis = (LoadRadiusM > 0.0f)
				? (UnloadRadiusM / LoadRadiusM) : 1.4f;
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : rayons forces a %.0f m de chargement ")
				TEXT("et %.0f de dechargement (defauts %.0f / %.0f)"),
				Rayon, Rayon * Hysteresis, LoadRadiusM, UnloadRadiusM);
			LoadRadiusM = Rayon;
			UnloadRadiusM = Rayon * Hysteresis;
			bRayonForce = true;
		}
	}

	// LES ANNEAUX AUSSI SE PILOTENT DEPUIS LA LIGNE DE COMMANDE, et pour la
	// meme raison : un A/B dont les deux moities demandent une recompilation
	// n'est pas un A/B. C'est ce qui permet de verifier, sur la MEME binaire,
	// que `NiveauMax = 0` rend exactement la diffusion d'avant les anneaux.
	{
		int32 Niveaux = -1;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedNiveaux="), Niveaux)
			&& Niveaux >= 0)
		{
			NiveauMax = FMath::Clamp(Niveaux, 0, 4);
			bNiveauxForce = true;
		}
		float Anneau0 = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedAnneau0="), Anneau0)
			&& Anneau0 > 32.0f)
		{
			RayonAnneau0M = Anneau0;
			bAnneau0Force = true;
		}
	}



	if (!LoadWorld())
	{
		return;
	}

	FString Error;
	if (const UWorldseedRules* Rules = WorldseedPipeline::GetRules(Error))
	{
		DensityRules = FWorldseedDensityRules::FromRules(*Rules);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] voxel : regles illisibles (%s), valeurs par defaut"), *Error);
	}

	// --- LES REGLES FIXENT LES ANNEAUX, LA LIGNE DE COMMANDE GARDE LA MAIN ---
	//
	// Les seuils vivent dans world_rules.json, c'est la regle du depot. Mais le
	// banc doit pouvoir comparer deux configurations sur le MEME binaire, sans
	// quoi les deux moities d'un A/B ne sont pas montees a l'identique -- et ce
	// depot a deja bati une journee de conclusions sur un harnais fausse. Les
	// surcharges de ligne de commande sont donc appliquees APRES les regles et
	// l'emportent ; c'est pour cela qu'elles sont relues ici plutot que plus
	// haut, avant que les regles n'existent.
	if (!bRayonForce && DensityRules.LoadRadiusM > 32.0f)
	{
		const float Hysteresis = (LoadRadiusM > 0.0f)
			? (UnloadRadiusM / LoadRadiusM) : 1.4f;
		LoadRadiusM = DensityRules.LoadRadiusM;
		UnloadRadiusM = LoadRadiusM * Hysteresis;
	}
	if (!bNiveauxForce)
	{
		NiveauMax = FMath::Clamp(DensityRules.NiveauMax, 0, 4);
	}
	if (!bAnneau0Force)
	{
		RayonAnneau0M = DensityRules.RayonAnneau0M;
	}
	LargeurTransition = DensityRules.LargeurTransition;

	// ET LA TAILLE DU VOXEL, QUI EST LA DENSITE DE MAILLAGE ELLE-MEME.
	//
	// C.EST LE SEUL LEVIER QUI CHANGE LE NOMBRE DE TRIANGLES SANS TOUCHER A
	// RIEN D.AUTRE : le decoupage en chunks suit ChunkSideM, donc doubler le
	// voxel divise par quatre les triangles a nombre de chunks, de composants
	// et d.emprise RIGOUREUSEMENT identiques. C.est ce qui permet de repondre
	// a « le cout du voxel, est-ce les triangles ? » par une mesure et non par
	// une intuition -- le depot a deja cru que le cout etait la ou il n.etait
	// pas, sur le debit de streaming comme sur le bridage de l.editeur.
	{
		float Vx = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedVoxel="), Vx) && Vx > 0.0f)
		{
			DensityRules.VoxelSizeM = Vx;
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : taille forcee -- %.2f m, soit %d cellules par chunk"),
				Vx, FMath::RoundToInt(ChunkSideM / Vx));
		}
	}

	// ET LA TEINTE DE ROCHE, pour separer ce qui colore de ce qui eclaire.
	//
	// UN A/B NE SE FAIT JAMAIS EN EDITANT LE FICHIER DE REGLES : une
	// restauration posee a la fin d.une commande longue n.est pas une
	// restauration, et ce depot a vide world_rules.json de ses 1322 lignes le
	// jour meme en s.y risquant. Quand un A/B demande un reglage sans
	// surcharge, on AJOUTE la surcharge.
	{
		float Cr = -1.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedCouleurRoche="), Cr) && Cr >= 0.0f)
		{
			DensityRules.RockColourFadeM = Cr;
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : teinte de roche forcee -- fondu %.1f m%s"),
				Cr, (Cr <= 0.0f) ? TEXT(" (COUPEE)") : TEXT(""));
		}
	}

	// --- LE DETAIL, ET SON DERNIER OCTAVE QUI TOMBE SOUS NYQUIST ------------
	//
	// Le commentaire de la regle l'annonce comme une qualite : « quatre octaves
	// depuis douze metres descendent a un metre et demi, SOIT LA TAILLE DU
	// VOXEL ». C'est exactement ce qu'une grille ne peut pas representer -- il
	// faut deux echantillons par periode, donc deux metres de longueur d'onde
	// pour un voxel d'un metre. Le dernier octave n'est pas rendu, il est
	// ALIASE, et l'aliasing d'un bruit se lit comme une moire a basse
	// frequence : les terrasses des parois.
	//
	// ET IL NE SE VOIT QUE SUR LES PAROIS, ce qui acheve de le designer : le
	// detail est MODULE PAR LA PENTE (`detailPenteMin` 0,15 a plat, plein sur
	// une falaise). Le sol plat n'en porte presque pas, donc il n'aliase pas.
	{
		int32 Perp = -1;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedPerp="), Perp)
			&& Perp >= 0)
		{
			DensityRules.bPorteePerpendiculaire = (Perp > 0);
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : portes du champ %s"),
				DensityRules.bPorteePerpendiculaire
					? TEXT("PERPENDICULAIRES a la surface")
					: TEXT("VERTICALES (etat d'avant, parois en terrasses)"));
		}
	}

	{
		int32 Oct = -1;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedDetailOctaves="), Oct)
			&& Oct >= 0)
		{
			DensityRules.DetailOctaves = Oct;
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : detail force a %d octaves ")
				TEXT("(longueur d'onde la plus fine %.2f m)"),
				Oct, Oct > 0
					? 1.0f / (DensityRules.DetailFrequency * (1 << (Oct - 1)))
					: 0.0f);
		}
	}

	// ON LE BATIT MUTABLE, PUIS ON LE FIGE. Un champ se CONSTRUIT -- Init, puis
	// eventuellement la lithologie -- et ne fait plus ensuite que se lire,
	// depuis des dizaines de fils a la fois. La reference qui le publie est
	// donc `const` : le type dit que la construction est finie, et le
	// compilateur interdit qu'un fil modifie ce que les autres lisent.
	TSharedRef<FWorldseedDensity, ESPMode::ThreadSafe> Champ =
		MakeShared<FWorldseedDensity, ESPMode::ThreadSafe>();

	// IL POINTE DANS LE MONDE PARTAGE, et c'est ce qui rend l'ensemble sur :
	// `Init` ne copie ni la geometrie ni le relief, il les reference. Tant que
	// le champ tient le monde en vie -- ce que fait le travail en tenant les
	// deux -- ces references restent valides.
	Champ->Init(Geometry, HeightsM(), HeightExaggeration, WorldSeed, DensityRules);

	// LA LITHOLOGIE EST BRANCHEE APRES Init, ET SEULEMENT SI ELLE EXISTE. Sans
	// elle le champ reste evaluable et ne creuse aucune diaclase : une donnee
	// absente doit rester sans effet, jamais produire un effet arbitraire.
	if (!Lithology.IsValid(Geometry.CellCount()))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] voxel : PAS de lithologie (%d identifiants pour %d cellules) ")
			TEXT("-- les parois garderont la couleur du biome de surface"),
			Lithology.Id.Num(), Geometry.CellCount());
	}
	if (Lithology.IsValid(Geometry.CellCount()))
	{
		FString LithoError;
		if (const UWorldseedRules* LithoRules = WorldseedPipeline::GetRules(LithoError))
		{
			const FWorldseedLithologyRules LR = FWorldseedLithologyRules::FromRules(*LithoRules);
			Champ->SetLithology(Lithology, LR);

			StratRules = FWorldseedStratRules::FromRules(*LithoRules, LR);
			DureteParId.Reset();
			for (const FWorldseedLithologyEntry& E : LR.Catalogue)
			{
				DureteParId.Add(E.Hardness);
			}
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : strates -- %d bancs, %.0f m de serie, ")
				TEXT("datum %.0f m, pile %s (%.0f a %.0f m si bornee)"),
				StratRules.Serie.Num(), StratRules.TotalThicknessM, StratRules.DatumM,
				StratRules.bPileCyclique ? TEXT("CYCLIQUE") : TEXT("bornee"),
				StratRules.DatumM - StratRules.TotalThicknessM, StratRules.DatumM);

			CouleurParRoche.Reset();
			NomParRoche.Reset();
			for (const FWorldseedLithologyEntry& E : LR.Catalogue)
			{
				CouleurParRoche.Add(E.Colour);
				NomParRoche.Add(E.Label.IsEmpty() ? E.Key : E.Label);
			}

			// LA CARTE DES CAUSES S'ARME ICI, une fois par monde et non
			// par chunk : la peinture tourne sur 1,67 million de sommets,
			// et relire la ligne de commande dedans serait une depense
			// pure.
			int32 Carte = 0;
			if (FParse::Value(FCommandLine::Get(),
					TEXT("WorldseedCarteCauses="), Carte) && Carte > 0)
			{
				bCarteDesCauses = true;
				UE_LOG(LogTemp, Warning,
					TEXT("[Worldseed] voxel : CARTE DES CAUSES armee -- les ")
					TEXT("sommets sont peints par BRANCHE, pas par matiere. ")
					TEXT("Magenta = repli, vert = biome, bleu = roche 2D, ")
					TEXT("teinte vive = banc. A regarder avec ")
					TEXT("ShowFlag.Lighting 0."));
			}

			// SANS CETTE LIGNE ON NE SAIT PAS SI LA ROCHE EST BRANCHEE, et la
			// difference ne se voit pas : une paroi peut etre creme parce
			// qu'elle est du calcaire, ou parce que le biome au-dessus est du
			// desert. Deux causes, une seule image.
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : lithologie branchee, %d couleurs de roche, ")
				TEXT("fondu %.0f m"),
				CouleurParRoche.Num(), DensityRules.RockColourFadeM);
		}
	}

	// LA CONSTRUCTION EST FINIE : on publie, et le champ devient `const`. Tout
	// ce qui suit ne fera plus que le lire -- y compris depuis les fils de
	// maillage, qui en tiendront chacun une reference.
	Density = Champ;

	// ET LA DIFFUSION RECOIT CE QU'ELLE LIT, UNE FOIS, ICI. Six nombres et le
	// champ : c'est tout ce que decider « qui est emis, a quel niveau » demande
	// reellement. Le reste de cet acteur -- composants, travaux en vol, pion,
	// materiau -- ne la concerne pas, et c'est ce qui la rend eprouvable.
	{
		FWorldseedDiffusionRegles DR;
		DR.ChunkSideM = ChunkSideM;
		DR.VoxelSizeM = DensityRules.VoxelSizeM;
		DR.RayonAnneau0M = RayonAnneau0M;
		DR.LoadRadiusM = LoadRadiusM;
		DR.BandDepthM = DensityRules.BandDepthM;
		DR.NiveauMax = NiveauMax;
		Diffusion.Regler(DR, Density);
	}

	// ET L'ON DIT LES ANNEAUX **APRES** LES AVOIR POSES.
	//
	// CETTE LIGNE MENTAIT. Elle vivait deux cents lignes plus haut et lisait
	// `Diffusion.RayonAnneauM(...)` avant que `Regler` ne configure quoi que ce
	// soit : elle affichait donc le defaut d'usine de la diffusion -- 250 --
	// pour un jeu qui tournait a 300, la valeur de `world_rules.json`. Le
	// releve etait faux de vingt pour cent, et il l'etait depuis toujours.
	//
	// UN LIBELLE QUI MENT EST PIRE QUE PAS DE LIBELLE : celui-ci a servi a
	// raisonner sur la position des paliers, et c'est en cherchant d'ou venait
	// la discordance avec la regle qu'on l'a trouve. Un releve qui ne porte pas
	// sa vraie configuration ne se compare a rien six mois plus tard.
	if (NiveauMax > 0)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] voxel : %d anneaux -- %.0f / %.0f / %.0f m, vue %.0f m, ")
			TEXT("dalle de transition %.2f cellule"),
			NiveauMax + 1, Diffusion.RayonAnneauM(0), Diffusion.RayonAnneauM(1),
			Diffusion.RayonAnneauM(2), LoadRadiusM, LargeurTransition);
	}

	bWorldReady = Density->IsValid();

	if (!bWorldReady)
	{
		UE_LOG(LogTemp, Error, TEXT("[Worldseed] voxel : champ de densite invalide"));
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] voxel : chunks de %.0f m, voxel %.2f m, rayon %.0f m, ")
		TEXT("collision partout, %d travaux simultanes"),
		ChunkSideM, DensityRules.VoxelSizeM, LoadRadiusM, MaxJobsInFlight);

	// --- LES SITES ARRIVENT AVEC LE MONDE, ILS NE SE REBATISSENT PLUS -------
	//
	// IL ETAIT ECRIT ICI QU'ILS « SE REBATISSENT, ILS NE SE TRANSPORTENT PAS »,
	// au motif qu'une fonction PURE du relief ne doit pas etre doublee. Le
	// raisonnement etait bon tant que personne d'autre ne la jouait. Depuis que
	// le menu offre les tables et les canyons dans sa liste de lieux, la chaine
	// la joue AUSSI -- et le journal montrait alors les deux blocs identiques a
	// la suite, pour **2,8 secondes** de calcul en double sur la grille du jeu.
	//
	// Le monde les porte donc, en memoire seulement : le cache n'en ecrit pas
	// un octet, et une partie lancee en PIE sans menu les recalcule ci-dessous.
	if (Monde.IsValid() && Monde->Tables.Num() + Monde->Canyons.Num() > 0)
	{
		Tables = Monde->Tables;
		Canyons = Monde->Canyons;
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] voxel : %d tables et %d canyons repris du monde"),
			Tables.Num(), Canyons.Num());
	}
	else
	{
		FString Err;
		if (const UWorldseedRules* const R = WorldseedPipeline::GetRules(Err))
		{
			WorldseedPlateau::Sites(Geometry, HeightsM(),
				FWorldseedPlateauRules::FromRules(*R), Lithology,
				FWorldseedLithologyRules::FromRules(*R), PrecipMm(), TempMeanC(),
				WorldSeed, Tables, &Canyons);
		}
	}

	if (UWorld* const W = GetWorld())
	{
		W->GetTimerManager().SetTimer(UpdateTimer, this,
			&AWorldseedVoxelTerrain::UpdateChunks,
			FMath::Max(UpdatePeriod, 0.05f), true);
	}

	// Une passe tout de suite : sans elle le monde reste vide le temps du
	// premier reveil du minuteur, ce qui se voit au demarrage.
	UpdateChunks();
}

void AWorldseedVoxelTerrain::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* const W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(UpdateTimer);
	}

	// ON ANNULE, ON N'ATTEND PAS. Les travaux en vol detiennent un pointeur
	// partage sur leur propre structure, jamais sur l'acteur : ils peuvent donc
	// finir dans le vide sans rien toucher de mort. Attendre les bloquerait le
	// fil de jeu pendant la fermeture du PIE, pour rien.
	for (TPair<FWorldseedChunkKey, FWorldseedVoxelChunkState>& Pair : Chunks)
	{
		if (Pair.Value.Job.IsValid())
		{
			Pair.Value.Job->bCancel.store(true, std::memory_order_release);
		}
	}
	Chunks.Reset();

	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------- geometrie

FVector AWorldseedVoxelTerrain::StreamingOriginCm() const
{
	if (const UWorld* const W = GetWorld())
	{
		if (const APawn* const Pawn = UGameplayStatics::GetPlayerPawn(W, 0))
		{
			return Pawn->GetActorLocation();
		}
	}
	return GetActorLocation();
}

FVector AWorldseedVoxelTerrain::ChunkCentreCm(const FWorldseedChunkKey& Key) const
{
	const FBox B = Diffusion.BoiteM(Key);
	const FVector CentreM = B.GetCenter();
	return GetActorLocation() + CentreM * WorldseedMetersToCm;
}

// ---------------------------------------------------------------- diffusion

void AWorldseedVoxelTerrain::UpdateChunks()
{
	// ON MESURE AU LIEU DE SUPPOSER. Le banc a montre un pic periodique a 2400 m
	// -- p95 a 14 ms pour une moyenne de 6,6 -- et ma premiere explication,
	// « c-est le balayage des masques », a ete DEMENTIE : le borner n-a pas
	// deplace le chiffre d-un dixieme. Plutot que d-essayer une deuxieme
	// hypothese a l-aveugle, on chronometre la passe elle-meme : si le pic n-est
	// pas ici, il est ailleurs, et ce sera dit.
	const double DebutPasse = FPlatformTime::Seconds();
	UpdateChunksInterne();
	const double Ms = (FPlatformTime::Seconds() - DebutPasse) * 1000.0;
	TotalUpdateMs += Ms;
	++UpdateCount;
	WorstUpdateMs = FMath::Max(WorstUpdateMs, Ms);
}

void AWorldseedVoxelTerrain::UpdateChunksInterne()
{
	WORLDSEED_TRACE(Diffusion);

	if (!bWorldReady)
	{
		return;
	}

	const FVector OriginCm = StreamingOriginCm() - GetActorLocation();
	const FVector OriginM = OriginCm / WorldseedMetersToCm;
	const double Side = ChunkSideM;

	// --- 1. relacher ce qui est trop loin -----------------------------------
	TArray<FWorldseedChunkKey> ARelacher;
	for (const TPair<FWorldseedChunkKey, FWorldseedVoxelChunkState>& Pair : Chunks)
	{
		const FVector CentreM = Diffusion.BoiteM(Pair.Key).GetCenter();
		if (FVector::Dist(CentreM, OriginM) > UnloadRadiusM)
		{
			ARelacher.Add(Pair.Key);
		}
	}
	for (const FWorldseedChunkKey& Key : ARelacher)
	{
		ReleaseChunk(Key);
	}

	// --- 2. recolter les travaux termines -----------------------------------
	int32 Televerses = 0;
	for (TPair<FWorldseedChunkKey, FWorldseedVoxelChunkState>& Pair : Chunks)
	{
		FWorldseedVoxelChunkState& State = Pair.Value;
		if (!State.Job.IsValid())
		{
			continue;
		}
		if (!State.Job->bDone.load(std::memory_order_acquire))
		{
			continue;
		}
		if (Televerses >= UploadsPerPass)
		{
			break;
		}

		UploadChunk(Pair.Key, State);
		++Televerses;
	}

	// --- 3. lancer ce qui manque --------------------------------------------
	int32 EnVol = 0;
	for (const TPair<FWorldseedChunkKey, FWorldseedVoxelChunkState>& Pair : Chunks)
	{
		if (Pair.Value.Job.IsValid())
		{
			++EnVol;
		}
	}
	if (EnVol >= MaxJobsInFlight)
	{
		HoldOrReleasePlayer();
		return;
	}

	// --- 3 bis. REPRENDRE CE QUI A ETE ABANDONNE ----------------------------
	//
	// Un chunk dont le travail a ete annule reste dans la table SANS maillage,
	// SANS travail et SANS marque de vide. La boucle des candidats ci-dessous
	// saute toute cle deja presente, donc il ne reviendrait jamais tout seul :
	// il faut le relancer explicitement. C'est le pendant indispensable de la
	// distinction faite a la reception -- sans lui, on a seulement remplace un
	// trou marque "vide" par un trou sans marque.
	{
		TArray<FWorldseedChunkKey> ARelancer;
		for (const TPair<FWorldseedChunkKey, FWorldseedVoxelChunkState>& Pair : Chunks)
		{
			const FWorldseedVoxelChunkState& S = Pair.Value;
			if (!S.bEmpty && !S.Mesh && !S.Job.IsValid())
			{
				ARelancer.Add(Pair.Key);
			}
		}
		for (const FWorldseedChunkKey& Key : ARelancer)
		{
			if (EnVol >= MaxJobsInFlight) { break; }
			LaunchJob(Key);
			++EnVol;
		}
	}

	// LES COLONNES D'ABORD, LA VERTICALE ENSUITE, et c'est la grille 2D qui la
	// donne : on ne considere que les etages ou la surface peut se trouver,
	// plus la bande creusable dessous. Les etages de socle n'existent meme pas
	// dans cette enumeration.
	//
	// ON PART DU NIVEAU LE PLUS GROSSIER ET L'ON DESCEND. Avec `NiveauMax` a
	// zero, il n'y a qu'un niveau et la descente ne fait rien : l'enumeration
	// est alors RIGOUREUSEMENT celle d'avant les anneaux, ce qui est la
	// propriete de surete de ce chantier.
	const int32 NiveauHaut = FMath::Max(NiveauMax, 0);
	const double CoteHaute = Diffusion.CoteM(NiveauHaut);
	const int32 Portee = FMath::CeilToInt(LoadRadiusM / CoteHaute) + 1;
	const int32 CX0 = FMath::FloorToInt(OriginM.X / CoteHaute);
	const int32 CY0 = FMath::FloorToInt(OriginM.Y / CoteHaute);

	TArray<TPair<FWorldseedChunkKey, double>> Feuilles;

	for (int32 DY = -Portee; DY <= Portee; ++DY)
	{
		for (int32 DX = -Portee; DX <= Portee; ++DX)
		{
			const int32 CX = CX0 + DX;
			const int32 CY = CY0 + DY;

			float SurfaceMin = 0.0f;
			float SurfaceMax = 0.0f;
			Diffusion.PlageSurface(CX, CY, NiveauHaut, SurfaceMin, SurfaceMax);

			const int32 ZBas = FMath::FloorToInt(
				(SurfaceMin - DensityRules.BandDepthM) / CoteHaute);
			const int32 ZHaut = FMath::FloorToInt(SurfaceMax / CoteHaute);

			for (int32 CZ = ZBas; CZ <= ZHaut; ++CZ)
			{
				Diffusion.Enumerer(FWorldseedChunkKey{ FIntVector(CX, CY, CZ), NiveauHaut },
					OriginM, Feuilles);
			}
		}
	}

	// --- 2 BIS. L'EQUILIBRAGE 2:1, AVANT TOUT LE RESTE -----------------------
	//
	// IL DOIT VENIR ICI, ET L'ORDRE EST PORTANT. Tout ce qui suit -- le
	// balayage des masques perimes, le tri, le lancement des travaux -- lit
	// `FeuillesCourantes` ou en depend. Equilibrer apres coup laisserait une
	// passe entiere travailler sur une partition non equilibree, donc armer des
	// masques faux pendant un dixieme de seconde a chaque deplacement. Ce n'est
	// pas long, et c'est exactement le genre de fissure intermittente qu'on ne
	// sait plus attribuer ensuite.
	Diffusion.Equilibrer(Feuilles, OriginM);

	// --- 3 ter. LES MASQUES PERIMES ------------------------------------------
	//
	// Le masque d'un chunk depend du niveau de ses VOISINS, donc de la position
	// du joueur : il change sans que le chunk change de niveau. Un chunk qui
	// garde un masque perime rouvre exactement la fissure que la cellule de
	// transition etait censee fermer -- et rien ne le signalerait. On remaille
	// donc, en gardant l'ancien maillage visible jusqu'au televersement du neuf.
	//
	// LE BALAYAGE EST BORNE PAR PASSE, ET LA MESURE L'A EXIGE. La premiere
	// version repassait sur TOUTES les feuilles a chaque mise a jour, ce qui
	// coute `feuilles x 6 x niveaux` descentes de NiveauEn -- et cela se voyait :
	// a 2400 m, p95 a 14 ms pour une moyenne de 6,6, soit un pic periodique a la
	// cadence exacte de la passe. Le controle qui a designe le coupable : deux
	// anneaux a 2400 m (3 934 feuilles, deux niveaux) donnent le MEME pic que
	// trois anneaux (2 436 feuilles, trois niveaux). C'est donc le PRODUIT qui
	// compte et non le nombre de chunks -- le niveau 3, un temps soupconne, est
	// innocent.
	//
	// Un balayage tournant etale le cout : la fissure eventuelle ne dure que le
	// temps d'un tour, soit quelques dixiemes de seconde, et le pic disparait.
	if (NiveauMax > 0 && Feuilles.Num() > 0)
	{
		constexpr int32 BudgetParPasse = 512;

		TArray<FWorldseedChunkKey> ARemailler;
		const int32 Nombre = Feuilles.Num();
		const int32 Examen = FMath::Min(BudgetParPasse, Nombre);

		for (int32 I = 0; I < Examen; ++I)
		{
			if (CurseurMasque >= Nombre) { CurseurMasque = 0; }
			const FWorldseedChunkKey& Cle = Feuilles[CurseurMasque].Key;
			++CurseurMasque;

			// --- LE CONTROLE DE SURETE 2:1, ET IL NE PARLE QUE S'IL ECHOUE ---
			//
			// Transvoxel ne sait coudre QU'UN niveau d'ecart. Deux feuilles
			// voisines a deux niveaux d'ecart rouvrent une fissure que rien ne
			// fermerait, et -- c'est le point -- RIEN NE LE SIGNALERAIT : le
			// masque de transition s'arme quand meme, la geometrie reste
			// combinatoirement close, et le trou ne se voit qu'a l'oeil, de
			// loin, sur une jointure precise.
			//
			// L'EQUILIBRAGE EST DESORMAIS FAIT PAR UNE PASSE DEDIEE, et ce
			// controle est ce qui l'eprouve. Il a d'ailleurs servi : la preuve
			// que portait `NoeudAccidente` -- l'emprise elargie garantirait le
			// 2:1 -- est fausse d'un niveau a l'autre, et c'est LUI qui l'a
			// dit en criant a 0,20 de rugosite. Une propriete de surete SE
			// VERIFIE, elle ne se suppose pas ; celle-ci se supposait, et elle
			// etait fausse.
			//
			// Le controle est GRATUIT quand tout va bien : il ne journalise
			// rien. Il tourne dans le balayage deja borne, donc il ne coute
			// aucune passe de plus.
			const FWorldseedVoxelChunkState* const S = Chunks.Find(Cle);
			if (!S || S->Job.IsValid() || S->bEmpty) { continue; }
			if (S->Masque != Diffusion.MasqueDe(Cle))
			{
				ARemailler.Add(Cle);
			}
		}
		for (const FWorldseedChunkKey& Key : ARemailler)
		{
			if (EnVol >= MaxJobsInFlight) { break; }
			LaunchJob(Key);
			++EnVol;
		}
	}

	// Les plus proches d'abord : c'est ce que le joueur voit en premier.
	Feuilles.Sort([](const TPair<FWorldseedChunkKey, double>& A,
		const TPair<FWorldseedChunkKey, double>& B)
	{
		return A.Value < B.Value;
	});

	for (const TPair<FWorldseedChunkKey, double>& F : Feuilles)
	{
		if (EnVol >= MaxJobsInFlight)
		{
			break;
		}
		if (Chunks.Contains(F.Key))
		{
			continue;
		}
		LaunchJob(F.Key);
		++EnVol;
	}

	// --- 5. OUBLIER CE QUI N'EST PLUS UNE FEUILLE ---------------------------
	//
	// LA DISTANCE NE SUFFIT PAS, ET C'ETAIT LE SEUL CRITERE. Un chunk ne cesse
	// pas d'exister seulement parce qu'il est loin : il cesse d'exister quand
	// il SE SUBDIVISE. A 300 m, un chunk de niveau 1 cede la place a ses huit
	// enfants de niveau 0 -- les enfants naissaient, le parent restait, et sa
	// surface se superposait a la leur au voxel pres.
	//
	// MESURE DU DEFAUT, marche de 197 m : 2422 feuilles demandees pour 3290
	// chunks suivis, soit 806 orphelins et 4 974 896 triangles contre
	// 3 140 011 a l'arret -- cinquante-huit pour cent de geometrie dessinee
	// deux fois. Le compte montait de 4,1 par metre parcouru et rien ne le
	// faisait redescendre : le test de distance ne les rattrapait qu'au-dela
	// de 1680 m.
	//
	// ON NE TOUCHE PAS A L'HYSTERESIS. Au-dela du rayon de CHARGEMENT, un
	// chunk n'est plus une feuille pour la seule raison que l'enumeration
	// s'arrete la -- et on le garde a dessein, pour qu'un demi-pas en arriere
	// ne fasse pas tout remailler. Le releve les compte a part pour cette
	// raison, et le temoin dit qu'ils sont peu nombreux : 62.
	//
	// ET L'ON ATTEND QUE LES ENFANTS SOIENT PRETS, sans quoi on echangerait un
	// DOUBLON contre un TROU -- les deux defauts que le proprietaire signale,
	// et « corriger un defaut en revele parfois un autre qu'il masquait » est
	// deja une note de ce depot. Un enfant qui n'est pas demande n'est pas
	// attendu : l'enumeration n'emet que les etages ou la surface peut se
	// trouver, donc les huit ne sont pas tous des feuilles.
	{
		const TSet<FWorldseedChunkKey>& Demande = Diffusion.Feuilles();

		auto EnfantsPrets = [this, &Demande](const FWorldseedChunkKey& K)
		{
			if (K.Niveau <= 0) { return true; }
			for (int32 I = 0; I < 8; ++I)
			{
				const FWorldseedChunkKey E{
					FIntVector(K.C.X * 2 + (I & 1),
						K.C.Y * 2 + ((I >> 1) & 1),
						K.C.Z * 2 + ((I >> 2) & 1)),
					K.Niveau - 1 };

				if (!Demande.Contains(E)) { continue; }

				const FWorldseedVoxelChunkState* const S = Chunks.Find(E);
				if (!S || (!S->Mesh && !S->bEmpty)) { return false; }
			}
			return true;
		};

		TArray<FWorldseedChunkKey> AOublier;
		for (const TPair<FWorldseedChunkKey, FWorldseedVoxelChunkState>& Pair : Chunks)
		{
			if (Demande.Contains(Pair.Key)) { continue; }
			if (Pair.Value.Job.IsValid()) { continue; }

			const FVector Centre = Diffusion.BoiteM(Pair.Key).GetCenter();
			if (FVector::Dist(Centre, OriginM) > LoadRadiusM) { continue; }

			if (EnfantsPrets(Pair.Key)) { AOublier.Add(Pair.Key); }
		}
		for (const FWorldseedChunkKey& Key : AOublier)
		{
			ReleaseChunk(Key);
		}
	}

	HoldOrReleasePlayer();
}

void AWorldseedVoxelTerrain::LaunchJob(const FWorldseedChunkKey& Key)
{
	WORLDSEED_TRACE(LancerTravail);

	FWorldseedVoxelChunkState& State = Chunks.FindOrAdd(Key);

	FWorldseedVoxelJobPtr Job = MakeShared<FWorldseedVoxelJob, ESPMode::ThreadSafe>();
	Job->Key = Key;
	Job->BoundsM = Diffusion.BoiteM(Key);
	State.Job = Job;

	// LE CHAMP EST CAPTURE PAR REFERENCE PARTAGEE, ET C'EST UNE CORRECTION.
	//
	// IL ETAIT CAPTURE PAR ADRESSE -- `&Density` -- sous un commentaire qui
	// affirmait que c'etait sur parce qu'on ne capturait pas l'acteur. On
	// capturait un pointeur DANS l'acteur, ce qui revient au meme des qu'il
	// meurt : `EndPlay` annule les travaux SANS LES ATTENDRE -- le bon choix,
	// attendre bloquerait la fermeture -- donc un travail deja entre dans
	// `Build` lisait encore une memoire que le ramasse-miettes allait
	// reprendre. Fenetre courte, plantage rare au changement de niveau : celui
	// qu'on ne reproduit jamais.
	//
	// Tenir une reference ferme la question par CONSTRUCTION. Le champ, et a
	// travers lui le monde qu'il reference, survivent au travail par
	// definition -- exactement ce que ce fichier fait deja pour les primitives
	// de grottes, « le fil de maillage ne doit rien tenir qui puisse mourir
	// avant lui », et qu'il ne faisait pas pour le champ.
	//
	// Ce qui restait vrai dans l'ancien commentaire le reste : le champ n'a
	// aucun etat mutable, donc plusieurs fils l'interrogent sans verrou. Le
	// type le dit maintenant -- il est `const`.
	TSharedPtr<const FWorldseedDensity, ESPMode::ThreadSafe> Champ = Density;
	const float VoxelSizeM = Diffusion.VoxelM(Key.Niveau);
	const float Largeur = LargeurTransition;

	// LE MASQUE EST CALCULE ICI, SUR LE FIL DE JEU, ET RETENU DANS L'ETAT. Il
	// depend de l'origine de diffusion, qui bouge : le retenir est ce qui permet
	// de savoir, a la passe suivante, qu'il a change et qu'il faut remailler.
	const FVector OrigineM =
		(StreamingOriginCm() - GetActorLocation()) / WorldseedMetersToCm;
	const uint8 Masque = Diffusion.MasqueDe(Key);
	State.Masque = Masque;

	// LES CELLULES DE TRANSITION IMPLIQUENT LE MAILLEUR MAISON. Le marching
	// cubes du moteur ne sait pas les produire : demander un raccord tout en
	// maillant avec lui donnerait une fissure silencieuse. Des qu'un masque est
	// arme, on passe donc par Transvoxel, quel que soit le reglage.
	const bool bTransvoxel = DensityRules.bTransvoxel || (Masque != 0);

	// L'EXTRACTION SE FAIT ICI, SUR LE FIL DE JEU, ET UNE SEULE FOIS. Le chunk
	// est elargi du rayon de raccordement : une capsule qui ne touche pas la
	// boite peut quand meme arrondir une arete a l'interieur.
	if (CaveNetwork.IsValid())
	{
		CaveNetwork.Query(Job->BoundsM.ExpandBy(DensityRules.CaveBlendM + 4.0f), Job->Caves);
	}

	Async(EAsyncExecution::ThreadPool,
		[Job, Champ, VoxelSizeM, bTransvoxel, Masque, Largeur]()
	{
		if (!Job->bCancel.load(std::memory_order_acquire))
		{
			Job->bHasSurface = WorldseedVoxelChunk::Build(
				*Champ, &Job->Caves, Job->BoundsM, VoxelSizeM, Job->Mesh, Job->Stats,
				[Job]() { return Job->bCancel.load(std::memory_order_acquire); },
				bTransvoxel, Masque, Largeur);
		}

		// EN DERNIER, ET EN LIBERATION : tout ce qui precede doit etre visible
		// du fil de jeu avant qu'il ne voie ce drapeau.
		Job->bDone.store(true, std::memory_order_release);
	});
}

FString AWorldseedVoxelTerrain::DiagnostiquerColonne(FVector MondeCm) const
{
	if (!bWorldReady)
	{
		return TEXT("monde pas pret");
	}

	const FVector LocalM = (MondeCm - GetActorLocation()) / WorldseedMetersToCm;
	const double Side = ChunkSideM;
	const int32 CX = FMath::FloorToInt(LocalM.X / Side);
	const int32 CY = FMath::FloorToInt(LocalM.Y / Side);

	float SurfMin = 0.0f, SurfMax = 0.0f;
	Density->SurfaceRangeM(CX * Side, CY * Side, CX * Side + Side, CY * Side + Side,
		SurfMin, SurfMax);
	const int32 ZBas = FMath::FloorToInt((SurfMin - DensityRules.BandDepthM) / Side);
	const int32 ZHaut = FMath::FloorToInt(SurfMax / Side);

	const FVector OriginM = (StreamingOriginCm() - GetActorLocation()) / WorldseedMetersToCm;

	FString R = FString::Printf(
		TEXT("colonne (%d, %d) : surface macro %.1f a %.1f m, etages %d a %d"),
		CX, CY, SurfMin, SurfMax, ZBas, ZHaut);

	// La surface REELLE, echantillonnee au centre de la colonne : c'est elle
	// que le mailleur voit, et elle differe de la macro par le bruit.
	const double MX = CX * Side + Side * 0.5;
	const double MY = CY * Side + Side * 0.5;
	R += FString::Printf(TEXT("\n  champ au centre, tous les 4 m :"));
	for (double Z = ZBas * Side; Z <= (ZHaut + 1) * Side; Z += 4.0)
	{
		R += FString::Printf(TEXT(" %.0f:%+.1f"), Z, Density->At(FVector(MX, MY, Z)));
	}

	for (int32 CZ = ZBas; CZ <= ZHaut; ++CZ)
	{
		const FWorldseedChunkKey Key{ FIntVector(CX, CY, CZ), 0 };
		const FVector CentreM = Diffusion.BoiteM(Key).GetCenter();
		const double DistM = FVector::Dist(CentreM, OriginM);
		const FWorldseedVoxelChunkState* S = Chunks.Find(Key);

		R += FString::Printf(TEXT("\n  etage %+d  d=%.0f m  "), CZ, DistM);
		if (!S)
		{
			R += (DistM > LoadRadiusM)
				? TEXT("ABSENT (hors rayon)")
				: TEXT("ABSENT ALORS QU'IL EST DANS LE RAYON");
			continue;
		}
		R += FString::Printf(TEXT("present : vide=%s travail=%s maillage=%s"),
			S->bEmpty ? TEXT("OUI") : TEXT("non"),
			S->Job.IsValid() ? TEXT("en cours") : TEXT("aucun"),
			S->Mesh ? TEXT("OUI") : TEXT("AUCUN"));

		// ON REJOUE LE BALAYAGE PAR GERMES, a l'identique, pour savoir s'il
		// voit la traversee. C'est le seul moyen de distinguer un chunk que le
		// balayage a manque d'un chunk que le mailleur n'a pas su remplir :
		// vus du dehors, les deux sont un trou.
		{
			const FBox B = Diffusion.BoiteM(Key);
			const double PasM = DensityRules.VoxelSizeM * 4.0;
			const int32 N = FMath::Max(FMath::CeilToInt(ChunkSideM / PasM), 1);
			// AVEC LES GROTTES, comme le mailleur. Mon premier balayage de
			// diagnostic appelait le champ SANS elles et voyait donc une
			// traversee nette la ou le mailleur n'en voyait aucune : la sonde
			// et le code mesuraient deux champs differents.
			FWorldseedCaveLocal Local;
			CaveNetwork.Query(B.ExpandBy(DensityRules.CaveBlendM + 4.0f), Local);

			int32 Dedans = 0, Dehors = 0;
			for (int32 K = 0; K <= N; ++K)
			for (int32 J = 0; J <= N; ++J)
			for (int32 I = 0; I <= N; ++I)
			{
				const FVector P(
					FMath::Min(B.Min.X + I * PasM, B.Max.X),
					FMath::Min(B.Min.Y + J * PasM, B.Max.Y),
					FMath::Min(B.Min.Z + K * PasM, B.Max.Z));
				(Density->At(P, &Local) < 0.0 ? Dedans : Dehors)++;
			}
			static const TCHAR* NomCause[] = {
				TEXT("maille"), TEXT("sans traversee"), TEXT("annule"), TEXT("maillage vide") };
			R += FString::Printf(
				TEXT("  [balayage %dx%d : %d roche, %d air | cause : %s | %d capsules]"),
				N + 1, N + 1, Dedans, Dehors,
				NomCause[static_cast<int32>(S->Cause)],
				Local.Chambers.Num() + Local.Segments.Num());
			R += FString::Printf(TEXT(" [germes %d, triangles %d]"), S->Seeds, S->Tris);
		}
	}
	return R;
}

void AWorldseedVoxelTerrain::PaintVertices(FWorldseedVoxelMesh& Mesh) const
{
	// TOUT LE CALCUL EST AILLEURS, ET IL N'A BESOIN DE RIEN DE CET ACTEUR : une
	// carte de biomes, une geometrie, un champ, une lithologie, une serie et
	// quatre nombres. Ce qui reste ici est de DETENIR ces donnees et de cumuler
	// le releve entre les chunks -- le travail d'un acteur.
	const FWorldseedPeintureContexte Contexte{
		Biomes(),
		Geometry,
		*Density,
		Lithology,
		CouleurParRoche,
		DureteParId,
		StratRules,
		DensityRules.RockColourFadeM,
		DensityRules.OverhangAmplitudeM + DensityRules.DetailAmplitudeM,
		WorldSeed,
		bCarteDesCauses
	};

	FWorldseedPeintureReleve Releve;
	WorldseedPeinture::Sommets(Mesh, Contexte, Releve);

	// LE CUMUL EST LA RESPONSABILITE DE L'APPELANT, et c'est ce qui permet a la
	// peinture d'etre une fonction : elle mesure UN maillage, l'acteur ajoute
	// les chunks les uns aux autres. Une passe qui cumulerait elle-meme ne
	// pourrait plus etre appelee deux fois sans etre remise a zero, et le
	// depot a deja paye ce genre d'etat cache.
	PeintureSommets += Releve.Sommets;
	PeintureSousLaSurface += Releve.SousLaSurface;
	PeintureSerieActive += Releve.SerieActive;
	PeintureTeintee += Releve.Teintee;
	PeintureProfondeurSomme += Releve.ProfondeurSomme;
	PeintureProfondeurMax = FMath::Max(PeintureProfondeurMax, Releve.ProfondeurMax);
	PeintureProfondeurMin = FMath::Min(PeintureProfondeurMin, Releve.ProfondeurMin);
	PeintureSombresEcrits += Releve.SombresEcrits;
	PeintureLumMin = FMath::Min(PeintureLumMin, Releve.LumMin);
	PeintureLumMax = FMath::Max(PeintureLumMax, Releve.LumMax);

	for (int32 K = 0; K < WorldseedPeinture::NbCauses; ++K)
	{
		PeintureParCause[K] += Releve.ParCause[K];
		PeintureSombresParCause[K] += Releve.SombresParCause[K];
	}

	if (PeintureParBanc.Num() < Releve.ParBanc.Num())
	{
		PeintureParBanc.SetNumZeroed(Releve.ParBanc.Num());
	}
	for (int32 K = 0; K < Releve.ParBanc.Num(); ++K)
	{
		PeintureParBanc[K] += Releve.ParBanc[K];
	}
}

void AWorldseedVoxelTerrain::UploadChunk(const FWorldseedChunkKey& Key,
	FWorldseedVoxelChunkState& State)
{
	WORLDSEED_TRACE(TeleverserChunk);

	FWorldseedVoxelJobPtr Job = State.Job;
	State.Job.Reset();

	if (!Job.IsValid())
	{
		return;
	}

	TotalMeshMs += Job->Stats.MeshMs + Job->Stats.NormalMs;
	WorstMeshMs = FMath::Max(WorstMeshMs, Job->Stats.MeshMs + Job->Stats.NormalMs);

	State.Cause = Job->Stats.Cause;
	State.Seeds = Job->Stats.Seeds;
	State.Tris = Job->Stats.Triangles;

	if (!Job->bHasSurface || Job->Mesh.IsEmpty())
	{
		// UN TRAVAIL ANNULE N'EST PAS UN CHUNK VIDE, et les confondre laissait
		// un TROU DEFINITIF dans le sol.
		//
		// Ce test marquait "vide" les trois causes d'echec du mailleur : pas de
		// traversee, travail annule, maillage sorti vide. Seule la premiere est
		// une propriete du monde ; les deux autres sont des accidents. Et comme
		// un chunk marque vide n'est JAMAIS repropose -- la boucle des
		// candidats saute toute cle deja presente -- l'accident devenait
		// permanent. Mesure chez le proprietaire : 2 colonnes sans aucun sol
		// sur 135 chargees, soit 1,5 %, et le sol de fond les masquait en se
		// dessinant un metre plus bas, ce qui donnait les "zones bizarres".
		// On tombait au travers.
		if (Job->Stats.Cause == FWorldseedVoxelStats::ECause::Annule)
		{
			// Ni maillage ni marque : la passe suivante le reprendra.
			++AbandonedChunks;
			return;
		}

		// AUCUNE SURFACE : on s'en souvient au lieu de l'oublier. Sans cette
		// marque, la meme boite serait relancee a chaque passe du minuteur.
		State.bEmpty = true;
		++EmptyChunks;
		if (Job->Stats.Cause == FWorldseedVoxelStats::ECause::MaillageVide)
		{
			++DegenerateChunks;
		}
		return;
	}

	// --- LA PEINTURE SE CHRONOMETRE A PART, ET C'EST UNE CORRECTION ---------
	//
	// LE CHRONO DU TELEVERSEMENT DEMARRAIT APRES CET APPEL, donc il excluait
	// cette passe-ci. C'est sur ce chiffre ampute -- « 0,21 ms par chunk,
	// 1,17 au pire » -- qu'on a conclu que le televersement « ne coute rien »
	// et porte `UploadsPerPass` de 6 a 16. La conclusion est peut-etre juste ;
	// la mesure qui la soutenait, non.
	//
	// ET CETTE PASSE N'EST PAS GRATUITE : elle boucle sur CHAQUE sommet et
	// appelle `Density->SurfaceHeightM`, c'est-a-dire un echantillonnage
	// BICUBIQUE -- seize lectures dispersees dans un tableau de plusieurs
	// dizaines de megaoctets, donc hostiles au cache -- plus une descente dans
	// la pile stratigraphique. Multiplie par les sommets de seize chunks par
	// passe, sur le fil de jeu.
	//
	// C'est la signature que ce depot denonce ailleurs : « une mesure
	// identique au chiffre pres sur dix cas mesure le mesureur ». Ici, un
	// chrono place APRES le traitement mesure tout sauf le traitement. On ne
	// DEPLACE rien tant qu'on n'a pas le chiffre : la peinture ne touche aucun
	// UObject et aurait sa place dans le travail, mais cette decision se prend
	// sur une mesure, pas sur une intuition.
	const double DebutPeinture = FPlatformTime::Seconds();
	PaintVertices(Job->Mesh);
	const double PeintureMs = (FPlatformTime::Seconds() - DebutPeinture) * 1000.0;
	TotalPaintMs += PeintureMs;
	WorstPaintMs = FMath::Max(WorstPaintMs, PeintureMs);
	TotalPaintVerts += Job->Mesh.Positions.Num();

	const double DebutUpload = FPlatformTime::Seconds();

	if (!State.Mesh)
	{
		// LE NIVEAU ENTRE DANS LE NOM : sans lui, deux chunks de niveaux
		// differents mais de memes indices porteraient le meme nom de composant.
		const FName Nom(*FString::Printf(TEXT("Voxel_L%d_%d_%d_%d"),
			Key.Niveau, Key.C.X, Key.C.Y, Key.C.Z));
		State.Mesh = NewObject<UProceduralMeshComponent>(this, Nom);
		State.Mesh->SetupAttachment(RootScene);
		State.Mesh->bUseAsyncCooking = true;
		// VRAI PAR DEFAUT, ET SEULE LA LIGNE DE COMMANDE LE COUPE : le relief
		// doit porter son ombre. La bascule n'existe que pour chiffrer le
		// repli du VSM (voir `-WorldseedOmbres=` dans BeginPlay).
		State.Mesh->SetCastShadow(bOmbresChunks);
		State.Mesh->bAffectDistanceFieldLighting = false;
		State.Mesh->RegisterComponent();

		if (TerrainMaterial)
		{
			State.Mesh->SetMaterial(0, TerrainMaterial);
		}
	}

	// TOUT CHUNK MAILLE EST SOLIDE, SANS EXCEPTION.
	//
	// Il y avait ici un rayon de collision, plus court que le rayon de
	// chargement : les chunks lointains etaient poses SANS collision, pour
	// economiser la cuisson. Le defaut est qu'il n'existe aucune facon de
	// donner la collision a une section deja creee -- ProceduralMeshComponent
	// n'expose rien de tel -- donc la decision, prise UNE FOIS au televersement,
	// etait definitive. Un chunk pose a plus de 120 m n'en recevait jamais, et
	// le joueur qui marchait jusqu'a lui passait AU TRAVERS DU SOL.
	//
	// Mesure du defaut, sondes verticales tous les dix metres depuis le pion :
	// sol present de 0 a 110 m, PLUS RIEN de 120 a 250 m. La frontiere tombait
	// exactement sur l'ancien rayon.
	//
	// Un chunk qu'on voit est un chunk qu'on peut atteindre, et le rayon de
	// CHARGEMENT borne deja le travail. Si la cuisson coute trop cher, la
	// reponse est de la faire de facon asynchrone, pas de laisser un trou.
	State.Mesh->CreateMeshSection_LinearColor(0, Job->Mesh.Positions,
		Job->Mesh.Triangles, Job->Mesh.Normals, TArray<FVector2D>(),
		Job->Mesh.Colours, TArray<FProcMeshTangent>(), true);

	State.bHasCollision = true;

	const double UploadMs = (FPlatformTime::Seconds() - DebutUpload) * 1000.0;
	TotalUploadMs += UploadMs;
	WorstUploadMs = FMath::Max(WorstUploadMs, UploadMs);
	++UploadCount;

	++BuiltChunks;
	TotalTriangles += Job->Mesh.TriangleCount();

	if (FirstFillSeconds <= 0.0 && BuiltChunks >= 8)
	{
		FirstFillSeconds = FPlatformTime::Seconds() - StartSeconds;
	}
}

void AWorldseedVoxelTerrain::ReleaseChunk(const FWorldseedChunkKey& Key)
{
	FWorldseedVoxelChunkState* const State = Chunks.Find(Key);
	if (!State)
	{
		return;
	}

	if (State->Job.IsValid())
	{
		State->Job->bCancel.store(true, std::memory_order_release);
	}
	if (State->Mesh)
	{
		State->Mesh->DestroyComponent();
	}
	Chunks.Remove(Key);
}

FWorldseedChunkKey AWorldseedVoxelTerrain::KeyForPoint(double X, double Y, double Z) const
{
	// LE NIVEAU SE DEDUIT, IL NE SE SUPPOSE PAS. Le chunk qui couvre un point
	// n-est au niveau le plus fin que si le point est dans le premier anneau ;
	// chercher ailleurs une cle de niveau zero ne trouverait rien, et le filet
	// du joueur conclurait que le sol n-existe pas.
	const FVector PointM(X, Y, Z);
	const FVector OrigineM =
		(StreamingOriginCm() - GetActorLocation()) / WorldseedMetersToCm;
	const int32 Niveau = Diffusion.NiveauEstime(PointM, OrigineM);
	const double Cote = Diffusion.CoteM(Niveau);

	return FWorldseedChunkKey{
		FIntVector(
			FMath::FloorToInt(X / Cote),
			FMath::FloorToInt(Y / Cote),
			FMath::FloorToInt(Z / Cote)),
		Niveau };
}

void AWorldseedVoxelTerrain::HoldOrReleasePlayer()
{
	if (!bHoldPlayer || !bWorldReady)
	{
		return;
	}

	UWorld* const W = GetWorld();
	APawn* const Pawn = W ? UGameplayStatics::GetPlayerPawn(W, 0) : nullptr;
	if (!Pawn)
	{
		// Le pion n'existe pas encore : le mode de jeu le posera, et la passe
		// suivante s'en occupera.
		return;
	}

	UCharacterMovementComponent* const Move =
		Pawn->FindComponentByClass<UCharacterMovementComponent>();

	const FVector PosCm = Pawn->GetActorLocation() - GetActorLocation();
	double X = PosCm.X / WorldseedMetersToCm;
	double Y = PosCm.Y / WorldseedMetersToCm;
	float SurfaceM = Density->SurfaceHeightM(X, Y);

	// --- LE FILET EST PERMANENT, IL NE JOUE PAS QU'UNE FOIS ------------------
	//
	// Cette fonction sortait immediatement des que bPlayerReleased etait pose,
	// donc apres la mise en place initiale il n'y avait PLUS AUCUN filet. Un
	// pion qui passe sous la bande de terrain tombe alors indefiniment :
	// mesure, -4745 m a quarante metres par seconde, et rien ne le ramene.
	//
	// LE CRITERE N'EST PAS ARBITRAIRE, IL VIENT DE LA BANDE ELLE-MEME. Le
	// terrain n'est maille que de la surface a bandeM en dessous ; plus bas,
	// aucun chunk n'existe et il ne peut RIEN y avoir. Un pion qu'on y trouve
	// n'est pas en train de tomber dans un trou, il est HORS DU MONDE.
	if (bPlayerReleased)
	{
		const double SousM = SurfaceM - PosCm.Z / WorldseedMetersToCm;
		if (SousM <= DensityRules.BandDepthM + PlayerRescueMarginM)
		{
			return;
		}

		// ON DIT OU, ET CE QU'ON Y TROUVE. Le filet disait seulement qu'il
		// avait joue, jamais ce qu'il rattrapait : impossible de savoir si le
		// joueur etait tombe dans une galerie, passe par un trou de chunk, ou
		// simplement sorti par le bas d'une falaise. Ce releve ne coute rien
		// -- il ne s'ecrit qu'a la chute -- et il est la seule trace qui reste
		// une fois le pion remonte.
		const FVector VitesseCms = Pawn->GetVelocity();

		// LE CHAMP SE REJOUE AVEC LES CAVITES, jamais sans. Ce depot a deja
		// paye la difference : un balayage de diagnostic qui appelait At(P)
		// quand le mailleur appelle At(P, &Caves) mesurait un autre monde et
		// le disait avec aplomb.
		const FVector PosM(X, Y, PosCm.Z / WorldseedMetersToCm);
		FWorldseedCaveLocal Local;
		CaveNetwork.Query(FBox(PosM, PosM).ExpandBy(DensityRules.CaveBlendM + 4.0f), Local);
		const bool bDansLaRoche = Density->At(PosM, &Local) <= 0.0;

		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] voxel : joueur a %.0f m SOUS la bande de terrain ")
			TEXT("-- hors du monde, on le remonte"),
			SousM - DensityRules.BandDepthM);

		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed]   chute : (%.0f, %.0f) m, z %.1f m, surface %.1f m, ")
			TEXT("descente %.1f m/s, champ %s, mode %d"),
			X, Y, PosCm.Z / WorldseedMetersToCm, SurfaceM,
			-VitesseCms.Z / 100.0, bDansLaRoche ? TEXT("PLEIN") : TEXT("vide"),
			Move ? static_cast<int32>(Move->MovementMode.GetValue()) : -1);

		// L'ETAT DES CHUNKS DE LA COLONNE : c'est lui qui distingue un TROU
		// -- aucune surface la ou il devrait y en avoir -- d'une chute par une
		// ouverture parfaitement normale.
		UE_LOG(LogTemp, Warning, TEXT("[Worldseed]   %s"),
			*DiagnostiquerColonne(Pawn->GetActorLocation()));

		// On rearme la mise en place initiale, qui sait deja poser le pion et
		// ne le relacher qu'une fois le chunk SOLIDE. Refaire ce travail ici
		// serait le dupliquer, et les deux moities divergeraient.
		bPlayerReleased = false;
		bPlayerHeld = false;
	}

	if (!bPlayerHeld && bTeleportPose)
	{
		// UNE DESTINATION DEMANDEE NE SE CORRIGE PAS. `SolPlat` fouille un
		// voisinage pour trouver du plat : tres bien au depart, ou l'endroit
		// n'a aucune importance, mais il deplacerait de plusieurs centaines de
		// metres un joueur venu voir UN sommet precis.
		X = TeleportXYM.X;
		Y = TeleportXYM.Y;
		SurfaceM = Density->SurfaceHeightM(X, Y);
	}
	else if (!bPlayerHeld)
	{
		// LE DEPART CHOISI DANS LE MENU DEPLACE LE POINT DE DEPART DE LA
		// RECHERCHE, et rien d'autre. Tout ce qui suit -- terre emergee la plus
		// proche, puis sol plat -- s'applique ensuite a l'identique. C'est le
		// minimum : ecrire ici un second chemin de mise en place ferait diverger
		// les deux.
		const bool bChoisi = bDepartDemande;
		if (bDepartDemande)
		{
			bDepartDemande = false;
			X = DepartXYM.X;
			Y = DepartXYM.Y;

			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : depart demande a (%.0f, %.0f) m, ")
				TEXT("surface %.1f m"), X, Y, Density->SurfaceHeightM(X, Y));
		}

		// --- LES SURCHARGES, QUI NE VIVENT QUE DANS L'ACTEUR ----------------
		//
		// ELLES SONT LUES ICI ET NON DANS LA REGLE, et c'est deliberement : une
		// passe qui relit la ligne de commande pour son compte ne s'eprouve
		// plus -- il faudrait relancer le jeu pour la mettre dans un etat. La
		// regle recoit des NOMBRES ; d'ou ils viennent ne la concerne pas.
		FWorldseedDepartRegles DR;
		DR.bChoisi = bChoisi;
		DR.PenteChoisieMaxDeg = PenteDepartMaxDeg;
		DR.EcartAltitudeMaxM = EcartAltitudeDepartM;
		DR.MargeDeplacementM =
			DensityRules.OverhangAmplitudeM + DensityRules.DetailAmplitudeM;

		{
			int32 Exact = 0;
			if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedDepartExact="), Exact)
				&& Exact != 0)
			{
				DR.bExact = true;
				UE_LOG(LogTemp, Warning,
					TEXT("[Worldseed] voxel : depart EXACT demande -- ")
					TEXT("aucune recherche de sol plat, pente non bornee"));
			}
		}

		if (FParse::Value(FCommandLine::Get(),
			TEXT("WorldseedEcartDepart="), DR.EcartAltitudeMaxM))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] voxel : ecart d'altitude tolere force a ")
				TEXT("%.1f m (defaut %.0f)"),
				DR.EcartAltitudeMaxM, EcartAltitudeDepartM);
		}

		// --- LA REGLE, QUI NE CONNAIT NI PION NI COMPOSANT ------------------
		const FVector2D DemandeM(X, Y);
		const FWorldseedDepart Depart = WorldseedPlacement::Choisir(
			*Density, Geometry, HeightsM(), DemandeM, DR);

		X = Depart.XM;
		Y = Depart.YM;
		SurfaceM = Depart.SurfaceM;

		if (Depart.bTerreTrouvee)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : terre emergee la plus proche a ")
				TEXT("(%.0f, %.0f) m, soit %.1f km du point demande"),
				Depart.TerreXM, Depart.TerreYM,
				FVector2D::Distance(DemandeM,
					FVector2D(Depart.TerreXM, Depart.TerreYM)) / 1000.0);
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] voxel : AUCUNE terre emergee dans la grille -- ")
				TEXT("le monde est-il entierement sous l'eau ?"));
		}

		// TROIS ISSUES, ET ELLES NE SE RESSEMBLENT PAS. On a DEPLACE le joueur
		// vers du plat, on a TENU son point, ou l'on n'a rien trouve du tout.
		// Les deux dernieres se lisaient pareil avant -- « pose sur place » --
		// alors que l'une est un choix et l'autre un echec.
		switch (Depart.Issue)
		{
		case EWorldseedDepart::SolPlatTrouve:
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : sol plat trouve a (%.0f, %.0f) m, ")
				TEXT("altitude %.1f m, pente %.1f deg%s"),
				X, Y, SurfaceM, Depart.PenteDeg,
				bChoisi
					? *FString::Printf(TEXT(" (%+.0f m du point choisi)"),
						SurfaceM - Depart.SurfaceDemandeeM)
					: TEXT(""));
			break;

		case EWorldseedDepart::Exact:
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : depart EXACT tenu a (%.0f, %.0f) m, ")
				TEXT("altitude %.1f m, pente %.1f deg (ecart nul, par ")
				TEXT("construction)"),
				X, Y, SurfaceM, Depart.PenteDeg);
			break;

		case EWorldseedDepart::PointViseTenu:
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] voxel : aucun sol tenable a moins de ")
				TEXT("%.0f m d'altitude du point choisi -- ON TIENT LE ")
				TEXT("POINT VISE (pente %.1f deg)"),
				DR.EcartAltitudeMaxM, Depart.PenteDeg);
			break;

		case EWorldseedDepart::Echec:
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] voxel : aucun sol plat autour du depart, ")
				TEXT("pose sur place"));
			break;
		}

		// LA COLONNE CREUSE SE DIT, ELLE N'ARRETE RIEN : l'echec franc tient le
		// point quoi qu'il arrive, mais sans cette ligne le joueur tomberait
		// dans une cavite et personne ne saurait pourquoi.
		if (Depart.bColonneCreuse)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] voxel : ET LA COLONNE EST CREUSE sous ce ")
				TEXT("point -- le joueur peut tomber dans une cavite"));
		}
	}

	// Marge au-dessus du relief : le deplacement 3D peut avoir remonte la
	// surface, et on ne veut pas naitre a l'interieur de la roche.
	const double PoseZCm = GetActorLocation().Z
		+ (SurfaceM + DensityRules.OverhangAmplitudeM + 3.0) * WorldseedMetersToCm;

	if (!bPlayerHeld)
	{
		Pawn->SetActorLocation(
			FVector(GetActorLocation().X + X * WorldseedMetersToCm,
				GetActorLocation().Y + Y * WorldseedMetersToCm, PoseZCm),
			false, nullptr, ETeleportType::TeleportPhysics);

		if (Move)
		{
			// EN VOL, PAS EN CHUTE : c'est le seul mode qui ne consomme pas la
			// gravite. Sans lui le pion descend pendant qu'on maille, et comme
			// c'est lui qui donne l'origine de la diffusion, il emmene la
			// fenetre de chunks avec lui.
			Move->SetMovementMode(MOVE_Flying);
			Move->Velocity = FVector::ZeroVector;
		}

		bPlayerHeld = true;
		bTeleportPose = false;
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] voxel : joueur tenu a (%.0f, %.0f) m, z %.0f cm, ")
			TEXT("surface %.1f m"),
			X, Y, PoseZCm, SurfaceM);
		return;
	}

	// --- relacher, quand le sol existe VRAIMENT -----------------------------
	const FWorldseedChunkKey Key = KeyForPoint(X, Y, SurfaceM);
	const FWorldseedVoxelChunkState* const State = Chunks.Find(Key);
	if (!State || !State->bHasCollision || State->Job.IsValid())
	{
		// On maintient la position : un pion en vol derive s'il a de l'inertie.
		if (Move)
		{
			Move->Velocity = FVector::ZeroVector;
		}
		return;
	}

	if (Move)
	{
		Move->SetMovementMode(MOVE_Falling);
	}
	bPlayerReleased = true;

	// --- LE CAP DE DEPART, TROISIEME PIECE DE LA FAMILLE -----------------
	//
	// `-WorldseedDepartX/Y` posent le joueur ; elles ne disent rien de ce
	// qu'il REGARDE. Un A/B monte sur elles seules peut etre rigoureux --
	// meme position, meme monde -- et parfaitement vide : celui de la mer
	// opaque a rendu deux captures identiques au pixel pres, tournees vers
	// les collines, sans une goutte d'eau dans le cadre. Une capture ne vaut
	// que par son SUJET.
	//
	// LA CONVENTION EST CELLE DE LA BOUSSOLE, et elle se deduit de la carte :
	// `azimut = 90 - lacet`, deja etablie et verifiee sur les quatre quarts.
	// On l'inverse ici. Poser le lacet directement ferait diverger l'affichage
	// du reglage a la premiere relecture.
	//
	// C'EST LE CONTROLEUR QUI PORTE LA VISEE, pas le pion : en vue a la
	// troisieme personne le pion suit, et ecrire sa rotation serait ecrase au
	// tick suivant.
	float CapDeg = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedCap="), CapDeg))
	{
		if (APlayerController* const PC =
			UGameplayStatics::GetPlayerController(this, 0))
		{
			const float Lacet = FRotator::ClampAxis(90.0f - CapDeg);
			PC->SetControlRotation(FRotator(0.0f, Lacet, 0.0f));

			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : cap de depart impose a %.0f deg (lacet %.0f)"),
				CapDeg, Lacet);
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] voxel : joueur rendu a la gravite, chunk %d,%d,%d ")
		TEXT("de niveau %d solide"),
		Key.C.X, Key.C.Y, Key.C.Z, Key.Niveau);
}

int32 AWorldseedVoxelTerrain::TravauxEnVol() const
{
	int32 N = 0;
	for (const TPair<FWorldseedChunkKey, FWorldseedVoxelChunkState>& Pair : Chunks)
	{
		if (Pair.Value.Job.IsValid()) { ++N; }
	}
	return N;
}

FWorldseedStreamingReleve AWorldseedVoxelTerrain::ReleveStreaming() const
{
	FWorldseedStreamingReleve R;

	const TSet<FWorldseedChunkKey>& Demande = Diffusion.Feuilles();
	R.Feuilles = Demande.Num();
	R.Suivis = Chunks.Num();

	// UN SUIVI QUI N'EST PLUS UNE FEUILLE EST DESSINE EN TROP -- MAIS SEULEMENT
	// S'IL EST ENCORE DANS LE RAYON DE CHARGEMENT.
	//
	// AU-DELA, C'EST L'HYSTERESIS, ET ELLE EST VOULUE : on garde ce qui vient
	// de sortir jusqu'a `UnloadRadiusM`, pour qu'un demi-pas en arriere ne
	// fasse pas tout remailler. Ces chunks-la n'ont PAS d'enfants emis, donc
	// rien ne les recouvre.
	//
	// LES CONFONDRE RUINE LA MESURE, et le premier releve les confondait : 872
	// « orphelins » apres 197 m de marche, dont la bande d'hysteresis --
	// 1200 a 1680 m, soit 4,3 km2 -- pouvait a elle seule porter la majorite.
	// C'est exactement la faute que ce depot a payee sur le routage des
	// galeries : un agregat sur deux populations ne se corrige pas, il se
	// decompose.
	const FVector OrigineM =
		(StreamingOriginCm() - GetActorLocation()) / WorldseedMetersToCm;

	for (const TPair<FWorldseedChunkKey, FWorldseedVoxelChunkState>& Pair : Chunks)
	{
		if (Demande.Contains(Pair.Key)) { continue; }

		const FVector Centre = Diffusion.BoiteM(Pair.Key).GetCenter();
		if (FVector::Dist(Centre, OrigineM) > LoadRadiusM)
		{
			++R.GardesParHysteresis;
		}
		else
		{
			++R.Orphelins;
		}
	}

	// ET UNE FEUILLE SANS MAILLAGE EST UN TROU -- sauf si elle est MARQUEE
	// VIDE, auquel cas il n'y a reellement rien a dessiner et c'est une
	// propriete du monde, pas un retard. Confondre les deux ferait compter en
	// trou les deux cinquiemes du volume qui n'ont aucune surface.
	for (const FWorldseedChunkKey& Cle : Demande)
	{
		const FWorldseedVoxelChunkState* const S = Chunks.Find(Cle);
		if (!S || (!S->Mesh && !S->bEmpty)) { ++R.Manquants; }
	}

	return R;
}

FString AWorldseedVoxelTerrain::ReportState() const
{
	int32 EnVol = 0;
	int32 AvecCollision = 0;
	int32 ParNiveau[5] = { 0, 0, 0, 0, 0 };
	int32 AvecTransition = 0;
	for (const TPair<FWorldseedChunkKey, FWorldseedVoxelChunkState>& Pair : Chunks)
	{
		if (Pair.Value.Job.IsValid()) { ++EnVol; }
		if (Pair.Value.bHasCollision) { ++AvecCollision; }
		if (Pair.Value.Masque != 0) { ++AvecTransition; }
		++ParNiveau[FMath::Clamp(Pair.Key.Niveau, 0, 4)];
	}

	// LA REPARTITION PAR NIVEAU EST CE QUI DIT SI LES ANNEAUX MORDENT. Le compte
	// total seul ne le dit pas : il baisse aussi quand le rayon baisse, et l'on
	// croirait a un gain des anneaux la ou l'on n'a fait que voir moins loin.
	if (NiveauMax > 0)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] voxel : chunks par niveau  %d / %d / %d / %d / %d")
			TEXT("  |  %d portent une face de transition")
			TEXT("  |  %d feuilles ajoutees par l'equilibrage 2:1"),
			ParNiveau[0], ParNiveau[1], ParNiveau[2], ParNiveau[3], ParNiveau[4],
			AvecTransition, Diffusion.AjoutsDeLEquilibrage());
	}

	const double Moyenne = (BuiltChunks + EmptyChunks) > 0
		? TotalMeshMs / (BuiltChunks + EmptyChunks) : 0.0;

	const FString Resume = FString::Printf(
		TEXT("%d chunks suivis (%d mailles, %d vides, %d en vol, %d avec collision)  |  ")
		TEXT("%d triangles  |  maillage %.2f ms/chunk, %.2f au pire  |  ")
		TEXT("PASSE DE DIFFUSION %.2f ms en moyenne, %.2f au pire  |  ")
		TEXT("TELEVERSEMENT sur le fil de jeu %.2f ms/chunk, %.2f au pire, ")
		TEXT("%.1f s cumulees  |  premier remplissage %.1f s"),
		Chunks.Num(), BuiltChunks, EmptyChunks, EnVol, AvecCollision,
		TotalTriangles, Moyenne, WorstMeshMs,
		(UpdateCount > 0) ? TotalUpdateMs / UpdateCount : 0.0, WorstUpdateMs,
		(UploadCount > 0) ? TotalUploadMs / UploadCount : 0.0, WorstUploadMs,
		TotalUploadMs / 1000.0, FirstFillSeconds);

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] voxel : %s"), *Resume);

	// LA MOITIE DE L'A/B SE DIT DANS LE RELEVE, ELLE NE SE DEDUIT PAS DE LA
	// LIGNE DE COMMANDE. Ce depot a deja compare deux releves pris dans deux
	// etats differents du code en croyant qu'ils etaient comparables -- « les
	// regles n'avaient pourtant pas bouge entre les deux ». Un releve qui ne
	// porte pas sa configuration ne se compare a rien six mois plus tard.
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] voxel : ombre portee des chunks %s"),
		bOmbresChunks ? TEXT("ACTIVE") : TEXT("COUPEE"));

	// --- L'ENTONNOIR DE LA TEINTE DE ROCHE ----------------------------------
	//
	// Il dit LAQUELLE des gardes mord, au lieu de laisser deviner. Sans lui, la
	// question « pourquoi les parois ne sont pas rayees » a cinq reponses
	// possibles et aucun moyen de les departager.
	if (PeintureSommets > 0)
	{
		const double Inv = 100.0 / static_cast<double>(PeintureSommets);
		FString Bancs;
		for (int32 B = 0; B < PeintureParBanc.Num(); ++B)
		{
			if (PeintureParBanc[B] > 0)
			{
				Bancs += FString::Printf(TEXT(" %d:%d"), B, PeintureParBanc[B]);
			}
		}

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] voxel : teinte de roche -- %lld sommets, ")
			TEXT("sous la surface %.1f %%, serie active %.1f %%, TEINTES %.1f %% ")
			TEXT("| profondeur moyenne %.1f m, de %.1f a %.1f | bancs touches%s"),
			PeintureSommets,
			PeintureSousLaSurface * Inv,
			PeintureSerieActive * Inv,
			PeintureTeintee * Inv,
			PeintureProfondeurSomme / static_cast<double>(PeintureSommets),
			PeintureProfondeurMin, PeintureProfondeurMax,
			Bancs.IsEmpty() ? TEXT(" AUCUN") : *Bancs);

		// --- ET CE QUE LA PEINTURE A REELLEMENT ECRIT --------------------
		//
		// LA LIGNE QUI TRANCHE LE DIAGNOSTIC DU NOIR. Huit etats ont
		// innocente la palette sans jamais poser la question prealable :
		// ecrivons-nous seulement du noir ? Si `sombres ecrits` vaut zero
		// alors que les captures en comptent douze pour cent, le noir n'est
		// pas une couleur de sommet -- et toute recherche du cote du
		// catalogue, des strates ou de l'erosion est perdue d'avance.
		//
		// Les luminances sont rendues en sRGB, l'echelle des captures, pour
		// que les deux chiffres se comparent sans conversion mentale.
		const auto EnSRGB = [](double Lineaire) -> int32
		{
			return FLinearColor(static_cast<float>(Lineaire),
				static_cast<float>(Lineaire),
				static_cast<float>(Lineaire)).ToFColor(true).R;
		};

		FString ParCause;
		for (int32 C = 0; C < 4; ++C)
		{
			if (PeintureParCause[C] > 0)
			{
				ParCause += FString::Printf(TEXT(" %s %.1f %% (dont %.2f %% sombres)"),
					WorldseedPeinture::NomDeCause(
						static_cast<WorldseedPeinture::ECause>(C)),
					PeintureParCause[C] * Inv,
					PeintureParCause[C] > 0
						? 100.0 * PeintureSombresParCause[C]
							/ static_cast<double>(PeintureParCause[C])
						: 0.0);
			}
		}

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] voxel : couleurs ECRITES -- luminance %d a %d sur 255, ")
			TEXT("SOMBRES %.2f %% (seuil 70) | par branche :%s"),
			EnSRGB(PeintureLumMin), EnSRGB(PeintureLumMax),
			PeintureSombresEcrits * Inv,
			ParCause.IsEmpty() ? TEXT(" aucune") : *ParCause);
	}

	// --- CE QUE LE TELEVERSEMENT NE DISAIT PAS ------------------------------
	//
	// La peinture des sommets est sur le MEME fil de jeu et dans le MEME appel
	// que le televersement ; elle etait simplement hors du chrono. Elle se lit
	// donc a cote de lui, et non dans une ligne separee : les deux se
	// comparent, et c'est leur SOMME qui est le vrai cout d'un chunk pose.
	//
	// Le cout par SOMMET est la colonne qui tranche : s'il est eleve, c'est le
	// traitement qu'il faut deplacer sur le fil de travail ; s'il est faible,
	// c'est qu'il y a simplement beaucoup de geometrie, et le remede est
	// ailleurs -- la densite de maillage, pas le fil.
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] voxel : PEINTURE sur le fil de jeu %.2f ms/chunk, ")
		TEXT("%.2f au pire, %.1f s cumulees  |  %lld sommets, %.3f us/sommet  |  ")
		TEXT("fil de jeu par chunk pose : peinture + televersement = %.2f ms"),
		(UploadCount > 0) ? TotalPaintMs / UploadCount : 0.0, WorstPaintMs,
		TotalPaintMs / 1000.0, TotalPaintVerts,
		(TotalPaintVerts > 0) ? (TotalPaintMs * 1000.0) / TotalPaintVerts : 0.0,
		(UploadCount > 0) ? (TotalPaintMs + TotalUploadMs) / UploadCount : 0.0);

	return Resume;
}


// ------------------------------------------------------- aller, et savoir ou

void AWorldseedVoxelTerrain::TeleporterJoueur(double XMetres, double YMetres)
{
	if (!bWorldReady)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Worldseed] aller : le monde n'est pas encore charge"));
		return;
	}

	TeleportXYM = FVector2D(XMetres, YMetres);
	bTeleportPose = true;

	// ON REARME LA MISE EN PLACE, ON NE DEPLACE PAS LE PION A LA MAIN. Elle
	// sait tenir en vol, attendre la collision et relacher ; la dupliquer ici
	// ferait deux codes a maintenir pour une seule regle.
	bPlayerHeld = false;
	bPlayerReleased = false;

	const float SurfaceM = Density->SurfaceHeightM(XMetres, YMetres);
	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] aller : (%.0f, %.0f) m, surface %.0f m -- ")
		TEXT("le joueur est tenu en vol jusqu'a ce que le sol soit solide"),
		XMetres, YMetres, SurfaceM);

	HoldOrReleasePlayer();
}

FString AWorldseedVoxelTerrain::OuSuisJe() const
{
	UWorld* const W = GetWorld();
	APawn* const Pawn = W ? UGameplayStatics::GetPlayerPawn(W, 0) : nullptr;
	if (!Pawn || !bWorldReady)
	{
		return TEXT("pas de joueur, ou monde pas encore charge");
	}

	const FVector PosCm = Pawn->GetActorLocation() - GetActorLocation();
	const double X = PosCm.X / WorldseedMetersToCm;
	const double Y = PosCm.Y / WorldseedMetersToCm;
	const double Z = PosCm.Z / WorldseedMetersToCm;
	const float SurfaceM = Density->SurfaceHeightM(X, Y);

	// La roche se lit au PLUS PROCHE VOISIN : un identifiant est une categorie,
	// et interpoler entre du granite et du calcaire donnerait du gres.
	FString Roche = TEXT("inconnue");
	if (Lithology.IsValid(Geometry.CellCount()))
	{
		const double U = FMath::Frac((X / Geometry.WidthM()) + 0.5);
		const double V = FMath::Clamp((Y / Geometry.HeightM) + 0.5, 0.0, 1.0);
		const int32 I = FMath::Clamp(FMath::RoundToInt(U * Geometry.NX),
			0, Geometry.NX - 1);
		const int32 J = FMath::Clamp(FMath::RoundToInt(V * Geometry.NY),
			0, Geometry.NY - 1);
		const uint8 Id = Lithology.Id[J * Geometry.NX + I];
		Roche = NomParRoche.IsValidIndex(Id) ? NomParRoche[Id]
			: FString::Printf(TEXT("roche %d"), Id);
	}

	// La pente se mesure sur le champ lui-meme, pas sur la grille : c'est le
	// relief qu'on a REELLEMENT sous les pieds, deplacement 3D compris.
	const double Pas = 4.0;
	const double DX = Density->SurfaceHeightM(X + Pas, Y)
		- Density->SurfaceHeightM(X - Pas, Y);
	const double DY = Density->SurfaceHeightM(X, Y + Pas)
		- Density->SurfaceHeightM(X, Y - Pas);
	const double PenteDeg = FMath::RadiansToDegrees(
		FMath::Atan(FMath::Sqrt(DX * DX + DY * DY) / (2.0 * Pas)));

	const FString Resume = FString::Printf(
		TEXT("(%.0f, %.0f) m -- altitude %.0f m, surface %.0f m (%+.0f m), ")
		TEXT("pente %.0f deg, roche %s"),
		X, Y, Z, SurfaceM, Z - SurfaceM, PenteDeg, *Roche);

	UE_LOG(LogTemp, Log, TEXT("[Worldseed] ou : %s"), *Resume);
	return Resume;
}

FString AWorldseedVoxelTerrain::LieuxRemarquables() const
{
	if (!bWorldReady || HeightsM().Num() != Geometry.CellCount())
	{
		return TEXT("monde pas encore charge");
	}

	const bool bRoche = Lithology.IsValid(Geometry.CellCount());

	int32 Sommet = INDEX_NONE;
	float ZMax = -1e9f;
	int32 Dure = INDEX_NONE;
	float ZDure = -1e9f;
	int32 Tendre = INDEX_NONE;
	float ZTendre = 1e9f;
	int32 Cote = INDEX_NONE;

	for (int32 I = 0; I < HeightsM().Num(); ++I)
	{
		const float Z = HeightsM()[I];
		if (Z > ZMax) { ZMax = Z; Sommet = I; }
		if (Z <= 0.0f) { continue; }
		if (Cote == INDEX_NONE && Z > 2.0f && Z < 12.0f) { Cote = I; }

		if (!bRoche) { continue; }
		const uint8 R = Lithology.Id[I];

		// Le contraste se lit entre la roche qui PORTE les hauteurs et celle
		// qui reste en plaine -- c'est tout l'objet de l'erosion differentielle.
		if (NomParRoche.IsValidIndex(R))
		{
			if (Z > ZDure && R == Lithology.Id[Sommet]) { ZDure = Z; Dure = I; }
			if (Z < ZTendre && Z > 20.0f && R != Lithology.Id[Sommet])
			{
				ZTendre = Z; Tendre = I;
			}
		}
	}

	FString Sortie;
	auto Ligne = [&](const TCHAR* Nom, int32 I, const TCHAR* Pourquoi)
	{
		if (I == INDEX_NONE) { return; }
		const double X = (static_cast<double>(I % Geometry.NX) / Geometry.NX - 0.5)
			* Geometry.WidthM();
		const double Y = (static_cast<double>(I / Geometry.NX) / Geometry.NY - 0.5)
			* Geometry.HeightM;
		const uint8 R = bRoche ? Lithology.Id[I] : 0;
		const FString Roche = (bRoche && NomParRoche.IsValidIndex(R))
			? NomParRoche[R] : FString(TEXT("?"));
		const FString L = FString::Printf(
			TEXT("Worldseed.Aller %.0f %.0f   %-18s %5.0f m, %-10s -- %s"),
			X, Y, Nom, HeightsM()[I], *Roche, Pourquoi);
		UE_LOG(LogTemp, Log, TEXT("[Worldseed] lieu : %s"), *L);
		Sortie += L + LINE_TERMINATOR;
	};

	Ligne(TEXT("le sommet"), Sommet, TEXT("l'amplitude du relief"));
	Ligne(TEXT("roche dure"), Dure, TEXT("le versant que la roche tient"));
	Ligne(TEXT("roche tendre"), Tendre, TEXT("la plaine decapee, a comparer"));
	Ligne(TEXT("la cote"), Cote, TEXT("le fond marin et l'eau"));

	// LES ARCHES, ELLES, SONT CONSERVEES PAR LE RESEAU. C'est l'arbitrage B8 --
	// la sortie de la passe macro est interrogeable au runtime -- et c'est ce
	// qui permet d'y aller sans relire un journal. Les bouches, gouffres et
	// dolines ne le sont pas encore : elles restent au journal de generation.
	for (int32 I = 0; I < CaveNetwork.Arches.Num(); ++I)
	{
		const FWorldseedCaveArch& A = CaveNetwork.Arches[I];
		const FString L = FString::Printf(
			TEXT("Worldseed.Aller %.0f %.0f   arche %-12d %5.0f m, ")
			TEXT("ouverture %.0f m sous un pont, lame de %.0f m"),
			A.CentreM.X, A.CentreM.Y, I + 1, A.CentreM.Z,
			2.6f * A.RayonM, A.EpaisseurM);
		UE_LOG(LogTemp, Log, TEXT("[Worldseed] lieu : %s"), *L);
		Sortie += L + LINE_TERMINATOR;
	}

	if (CaveNetwork.IsValid())
	{
		Sortie += FString::Printf(
			TEXT("%d chambres dans le monde ; les bouches, gouffres et dolines ")
			TEXT("sont au journal de generation.") LINE_TERMINATOR,
			CaveNetwork.Chambers.Num());
	}

	// LES TABLES SONT INTROUVABLES AU HASARD, et c'est la raison d'etre de ces
	// lignes : elles couvrent environ un pour cent des terres sur 64 x 32 km.
	// Meme motif que pour les arches -- une forme qu'on ne sait pas trouver
	// n'existe pas pour le joueur.
	for (int32 I = 0; I < Tables.Num() && I < 10; ++I)
	{
		const FWorldseedPlateauSite& S = Tables[I];
		const FString L = FString::Printf(
			TEXT("Worldseed.Aller %.0f %.0f   table %-12d sommet %5.0f m, ")
			TEXT("paroi de %.0f m"),
			S.CentreM.X, S.CentreM.Y, I + 1, S.AltitudeM, S.EscarpementM);
		UE_LOG(LogTemp, Log, TEXT("[Worldseed] lieu : %s"), *L);
		Sortie += L + LINE_TERMINATOR;
	}

	return Sortie;
}



// ------------------------------------------- le releve local, pour l'ecran

TArray<FString> AWorldseedVoxelTerrain::ReleveJoueur() const
{
	TArray<FString> Lignes;

	UWorld* const W = GetWorld();
	APawn* const Pawn = W ? UGameplayStatics::GetPlayerPawn(W, 0) : nullptr;
	if (!Pawn || !bWorldReady || Geometry.NX < 2)
	{
		return Lignes;
	}

	const FVector PosCm = Pawn->GetActorLocation() - GetActorLocation();
	const double X = PosCm.X / WorldseedMetersToCm;
	const double Y = PosCm.Y / WorldseedMetersToCm;
	const double Z = PosCm.Z / WorldseedMetersToCm;

	// LA CELLULE SE PREND AU PLUS PROCHE VOISIN, comme partout ailleurs : biome
	// et roche sont des IDENTIFIANTS, et interpoler entre deux categories
	// fabrique une valeur qui n'existe pas. Le depot a paye ce piege sur la
	// carte des biomes lue par PCG -- 1,71 % des points affectes a un biome
	// absent de l'endroit.
	const double U = FMath::Frac((X / Geometry.WidthM()) + 0.5);
	const double V = FMath::Clamp((Y / Geometry.HeightM) + 0.5, 0.0, 1.0);
	const int32 I = FMath::Clamp(FMath::RoundToInt(U * Geometry.NX), 0, Geometry.NX - 1);
	const int32 J = FMath::Clamp(FMath::RoundToInt(V * Geometry.NY), 0, Geometry.NY - 1);
	const int32 Cellule = J * Geometry.NX + I;

	// --- 1. OU SUIS-JE ------------------------------------------------------
	//
	// LA LATITUDE NE SE DEDUIT PAS DE Y PAR UNE REGLE DE TROIS. La carte est en
	// projection equivalente-aire : au point d'apparition, le produit lineaire
	// donnait +65,77 degres la ou la vraie valeur est +46,95. Dix-neuf degres
	// d'erreur, et c'est pourquoi le manifeste avait cesse d'ecrire un
	// « degres par metre ».
	//
	// LA LONGITUDE, ELLE, EST UNE CONVENTION D'AFFICHAGE : le monde s'enroule
	// en X, et l'on etale cet axe sur 360 degres pour donner une planete. On
	// la centre sur zero, comme un meridien d'origine.
	const float LatitudeDeg = Geometry.LatitudeDegForV(static_cast<float>(V));
	const float LongitudeDeg = static_cast<float>(U * 360.0) - 180.0f;

	// --- LA BOUSSOLE --------------------------------------------------------
	//
	// SANS ELLE ON NE PEUT PAS EXPLORER, et le proprietaire l'a dit en ces
	// termes : « je ne suis pas en capacite de me reperer pour marcher dans une
	// direction precise ». Des coordonnees disent OU l'on est, pas vers ou l'on
	// va -- et un monde qu'on ne sait pas parcourir reste un monde qu'on ne
	// peut pas eprouver.
	//
	// LES DEUX CONVENTIONS SE DEDUISENT DE LA CARTE, elles ne se choisissent
	// pas. V croit vers le NORD -- `LatitudeDegForV` rend +90 en V = 1 -- et
	// V croit avec Y, donc +Y est le nord. De meme U croit avec X et la
	// longitude croit avec U, donc +X est l'est.
	//
	// Unreal compte son lacet depuis +X et vers +Y ; l'azimut se compte depuis
	// le NORD et vers l'EST. D'ou azimut = 90 - lacet, verifie sur les quatre
	// quarts : lacet 0 (+X) donne 90, l'est ; lacet 90 (+Y) donne 0, le nord.
	//
	// C'EST LA CAMERA QUI DONNE LE CAP, PAS LE PION. On s'oriente sur ce qu'on
	// REGARDE, et le depot a deja fait ce choix pour le sol de fond -- en vue a
	// la troisieme personne, le bras place l'oeil jusqu'a quatre metres
	// derriere le personnage.
	float CapDeg = 0.0f;
	if (const APlayerCameraManager* const Cam = UGameplayStatics::GetPlayerCameraManager(W, 0))
	{
		CapDeg = FMath::Fmod(90.0f - static_cast<float>(Cam->GetCameraRotation().Yaw) + 720.0f, 360.0f);
	}

	static const TCHAR* const Roses[] = {
		TEXT("N"), TEXT("NE"), TEXT("E"), TEXT("SE"),
		TEXT("S"), TEXT("SO"), TEXT("O"), TEXT("NO") };
	const int32 Quart = FMath::RoundToInt(CapDeg / 45.0f) % 8;

	Lignes.Add(FString::Printf(
		TEXT("lon %6.1f %s   lat %5.1f %s   alt %6.0f m   cap %3.0f %-2s   (%.0f, %.0f) m"),
		FMath::Abs(LongitudeDeg), LongitudeDeg >= 0.0f ? TEXT("E") : TEXT("O"),
		FMath::Abs(LatitudeDeg), LatitudeDeg >= 0.0f ? TEXT("N") : TEXT("S"),
		Z, CapDeg, Roses[Quart], X, Y));

	// --- 2. CLIMAT, BIOME, ROCHE --------------------------------------------
	FString Biome = TEXT("--");
	if (Biomes().Index.IsValidIndex(Cellule) && Biomes().Cover.IsValidIndex(Cellule))
	{
		Biome = WorldseedBiomes::Name(WorldseedBiomes::AppearanceBiome(
			Biomes().Index[Cellule], Biomes().Cover[Cellule]));
	}

	FString Roche = TEXT("--");
	if (Lithology.IsValid(Geometry.CellCount()))
	{
		const uint8 Id = Lithology.Id[Cellule];
		Roche = NomParRoche.IsValidIndex(Id) ? NomParRoche[Id]
			: FString::Printf(TEXT("roche %d"), Id);
	}

	// Temperature MOYENNE ANNUELLE et pluie : les deux axes du diagramme de
	// Whittaker, donc les deux grandeurs qui decident du biome affiche a cote.
	const float TempC = TempMeanC().IsValidIndex(Cellule) ? TempMeanC()[Cellule] : 0.0f;
	const float PluieMm = PrecipMm().IsValidIndex(Cellule) ? PrecipMm()[Cellule] : 0.0f;

	Lignes.Add(FString::Printf(
		TEXT("%-24s %5.1f C  %5.0f mm/an   %s"), *Biome, TempC, PluieMm, *Roche));

	// --- 3. CE QU'ON EST EN TRAIN DE REGARDER -------------------------------
	//
	// L'ANNEAU ET SA MAILLE SONT LA POUR UNE RAISON PRECISE : une falaise
	// dechiree vue de loin et lisse de pres est un defaut d'ECHANTILLONNAGE,
	// pas de geometrie -- les diaclases ont 2,4 m d'ouverture et l'anneau 1
	// maille a 2 m. Sans ce chiffre a l'ecran, il faut marcher jusqu'a la
	// paroi pour s'en apercevoir.
	const FVector OrigineM =
		(StreamingOriginCm() - GetActorLocation()) / WorldseedMetersToCm;
	const int32 Niveau = Diffusion.NiveauEstime(FVector(X, Y, Z), OrigineM);
	// Un chunk de niveau N porte TOUJOURS le meme nombre de cellules ; c'est le
	// voxel qui double a chaque cran.
	const double MailleM = DensityRules.VoxelSizeM * FMath::Pow(2.0, Niveau);

	const float SurfaceM = Density->SurfaceHeightM(X, Y);

	// La pente se mesure sur le CHAMP, pas sur la grille : c'est le relief
	// qu'on a reellement sous les pieds, deplacement 3D compris.
	const double Pas = 4.0;
	const double DX = Density->SurfaceHeightM(X + Pas, Y) - Density->SurfaceHeightM(X - Pas, Y);
	const double DY = Density->SurfaceHeightM(X, Y + Pas) - Density->SurfaceHeightM(X, Y - Pas);
	const double PenteDeg = FMath::RadiansToDegrees(
		FMath::Atan(FMath::Sqrt(DX * DX + DY * DY) / (2.0 * Pas)));

	Lignes.Add(FString::Printf(
		TEXT("anneau %d (maille %.0f m)   pente %3.0f deg   sol %+.0f m"),
		Niveau, MailleM, PenteDeg, Z - SurfaceM));

	// --- 4. QUELLES CAVITES, ET POURQUOI CETTE FALAISE ----------------------
	//
	// LA KARSTIFIABILITE DIT QUELLE FORME ATTENDRE ICI, et les deux s'excluent
	// par construction : la ou la roche se dissout, chambres et galeries ; la
	// ou elle ne se dissout pas, des fractures. Aucun reglage ne choisit entre
	// les deux, c'est la ROCHE qui decide.
	const float Karst = Density->KarstifiableAt(X, Y);

	FString Banc = TEXT("pas de serie ici");
	if (StratRules.IsActive())
	{
		const int32 Rang = WorldseedStrata::BancAt(X, Y, SurfaceM, StratRules, WorldSeed);
		if (StratRules.Serie.IsValidIndex(Rang))
		{
			Banc = FString::Printf(TEXT("banc %d durete %.2f"),
				Rang, StratRules.Serie[Rang].Hardness);
		}
	}

	Lignes.Add(FString::Printf(
		TEXT("karst %.2f (%s)   %s"), Karst,
		Karst > 0.5f ? TEXT("grottes") : TEXT("diaclases"), *Banc));

	// --- 5. POURQUOI CE BIOME -----------------------------------------------
	//
	// LES DEUX CHAMPS CONTINUS QUI L'EXPLIQUENT. L'etiquette de biome n'est
	// qu'un cache pose a cote pour lier des assets ; ce sont ces grandeurs-la
	// qui la decident, et les lire evite de croire qu'un biome est « faux »
	// alors qu'il est la consequence exacte de son climat.
	const float Cont = Continentalite().IsValidIndex(Cellule) ? Continentalite()[Cellule] : -1.0f;
	const float Saison = SaisonAmpC().IsValidIndex(Cellule) ? SaisonAmpC()[Cellule] : -1.0f;

	Lignes.Add(FString::Printf(
		TEXT("continentalite %.2f   amplitude saisonniere %.0f C"), Cont, Saison));

	// --- 6. EST-CE QUE CA A FINI DE CHARGER ? -------------------------------
	//
	// LA LIGNE QUI AURAIT TRANCHE TOUT DE SUITE le jour ou « les faces de la
	// montagne ne sont pas finies d'afficher ». Un compte fige et zero travail
	// en vol disent que ce qu'on voit est ce qu'il y aura.
	Lignes.Add(FString::Printf(
		TEXT("chunks %d poses, %d en vol   rayon %.0f m"),
		Chunks.Num(), TravauxEnVol(), LoadRadiusM));

	return Lignes;
}
