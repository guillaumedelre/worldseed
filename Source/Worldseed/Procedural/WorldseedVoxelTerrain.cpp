// Worldseed - terrain voxel : diffusion des chunks autour du joueur.

#include "Procedural/WorldseedVoxelTerrain.h"
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
	bool bRugositeForcee = false;

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
		float Rugosite = -1.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedRugosite="), Rugosite)
			&& Rugosite >= 0.0f)
		{
			RugositeMin = Rugosite;
			bRugositeForcee = true;
		}
	}

	// --- LE DEBIT DE POSE SE PILOTE, PARCE QUE C'EST LUI QUI FAIT LA LATENCE
	//
	// MESURE DU 22 SEPTEMBRE, ET ELLE DESIGNE LE COUPABLE SANS AMBIGUITE :
	//
	//   maillage      8,24 ms/chunk, 24 travaux en vol  ->  ~2900 chunks/s
	//   televersement 16 par passe toutes les 0,1 s     ->    160 chunks/s
	//
	// Le maillage a DIX-HUIT FOIS la capacite necessaire ; ce qui borne le
	// remplissage est une constante, pas un calcul. A 2346 chunks, ce plafond
	// fait a lui seul une quinzaine de secondes de remplissage -- et c'est
	// exactement ce que le joueur voit se construire devant lui.
	//
	// La question « peut-on multithreader pour ne plus voir la generation »
	// trouve donc ici sa reponse : le maillage est DEJA hors du fil de jeu et
	// tourne au dix-huitieme de ses moyens. Il n'y a rien a paralleliser de
	// plus, il y a un robinet a ouvrir -- et a mesurer, parce que ce qu'il
	// laisse passer se paie sur le fil de jeu, a 0,18 ms par chunk pose.
	{
		int32 Lot = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedTeleversements="), Lot)
			&& Lot > 0)
		{
			UploadsPerPass = FMath::Clamp(Lot, 1, 256);
		}
		float Periode = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedPeriode="), Periode)
			&& Periode >= 0.01f)
		{
			UpdatePeriod = Periode;
		}

		// ET LE NOMBRE DE TRAVAUX EN VOL, QUI EST LE PLAFOND SUIVANT.
		//
		// MESURE, ET ELLE VALIDE LE MODELE PAR UNE PREDICTION VERIFIEE :
		//
		//   16 poses/passe -> 160/s demandes                    20 s
		//   32 poses/passe -> 320/s demandes, 24 travaux = 240  15 s
		//   64 poses/passe -> 640/s demandes, 24 travaux = 240  15 s
		//
		// Doubler les poses a gagne cinq secondes ; les quadrupler n'a RIEN
		// gagne de plus, et c'etait annonce avant de lire le chiffre. Un
		// travail dure 8,4 ms et la passe 100 : au plus `MaxJobsInFlight`
		// d'entre eux peuvent etre lances et recoltes par passe, ce qui borne
		// le debit a `MaxJobsInFlight / UpdatePeriod` -- 240 par seconde, quel
		// que soit le budget de pose. C'est le robinet suivant.
		int32 Travaux = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedTravaux="), Travaux)
			&& Travaux > 0)
		{
			MaxJobsInFlight = FMath::Clamp(Travaux, 1, 512);
		}
	}

	// --- L'OMBRE DES CHUNKS SE COUPE POUR LA MESURER -----------------------
	//
	// POURQUOI CETTE SURCHARGE EXISTE. Le moteur affiche a l'ecran :
	//
	//   [VSM] Non-Nanite Marking Job Queue overflow. Performance may be
	//   affected. This occurs when many non-nanite meshes cover a large area
	//   of the shadow map.
	//
	// Le mecanisme, lu dans le shader (VirtualShadowMapBuildPerPageDrawCommands.usf) :
	// une instance dont le rectangle depasse huit pages (MAX_SINGLE_THREAD_MARKING_AREA)
	// devient un « gros travail » et prend une place dans une file de 128
	// (MARKING_JOB_QUEUE_SIZE = NUM_THREADS_PER_GROUP * 2). Quand la file
	// deborde, le shader retombe sur un marquage MONO-THREAD : le resultat
	// reste JUSTE -- ce n'est pas un artefact, contrairement aux debordements
	// de PagePool et de VisibleInstances que le meme switch signale comme
	// « will produce visual artifacts » -- mais il est plus lent.
	//
	// Cette file est une constante de COMPILATION du shader : aucune variable
	// de console ne la leve. Les seuls leviers sont donc de reduire le nombre
	// d'instances non-Nanite qui couvrent beaucoup de pages, ou la surface de
	// pages elle-meme. Nos chunks sont exactement ce cas : des milliers de
	// ProceduralMeshComponent de trente-deux metres, qui ne peuvent pas etre
	// Nanite, tous en `SetCastShadow(true)`. Le sol de fond, lui, est hors de
	// cause : il est deja en `SetCastShadow(false)`.
	//
	// AVANT DE SACRIFIER QUOI QUE CE SOIT, ON CHIFFRE. A 198 images par
	// seconde ce n'est pas le goulot, et couper des ombres a l'aveugle pour
	// faire taire un avertissement serait exactement ce que ce depot
	// s'interdit. Cette bascule sert a UNE chose : mesurer l'ecart de temps
	// GPU avec et sans, sur la MEME binaire et le MEME monde.
	{
		int32 Ombres = 1;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedOmbres="), Ombres))
		{
			bOmbresChunks = (Ombres != 0);
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : ombre portee des chunks %s (mesure du VSM)"),
				bOmbresChunks ? TEXT("ACTIVE") : TEXT("COUPEE"));
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
	if (!bRugositeForcee)
	{
		RugositeMin = DensityRules.RugositeMin;
	}
	LargeurTransition = DensityRules.LargeurTransition;

	// ET LE MAILLEUR AUSSI SE DEBRANCHE EN LIGNE DE COMMANDE. Un A/B qui
	// demande de rouvrir le fichier de regles change son empreinte, donc
	// regenere le monde entre les deux moities : ce ne serait plus le meme
	// monde, et le depot a une regle contre les A/B mal montes.
	{
		int32 Tv = -1;
		if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedTransvoxel="), Tv)
			&& Tv >= 0)
		{
			DensityRules.bTransvoxel = (Tv > 0);
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : mailleur force -- %s"),
				DensityRules.bTransvoxel ? TEXT("Transvoxel") : TEXT("FMarchingCubes"));
		}
	}

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

	if (NiveauMax > 0)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] voxel : %d anneaux -- %.0f / %.0f / %.0f m, vue %.0f m, ")
			TEXT("dalle de transition %.2f cellule"),
			NiveauMax + 1, RayonAnneauM(0), RayonAnneauM(1), RayonAnneauM(2),
			LoadRadiusM, LargeurTransition);
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
				TEXT("[Worldseed] voxel : strates -- %d bancs, %.0f m de serie, datum %.0f m"),
				StratRules.Serie.Num(), StratRules.TotalThicknessM, StratRules.DatumM);

			CouleurParRoche.Reset();
			NomParRoche.Reset();
			for (const FWorldseedLithologyEntry& E : LR.Catalogue)
			{
				CouleurParRoche.Add(E.Colour);
				NomParRoche.Add(E.Label.IsEmpty() ? E.Key : E.Label);
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

	// LES SITES DE TABLES SE REBATISSENT, ILS NE SE TRANSPORTENT PAS. Meme
	// raisonnement que pour le reseau de grottes, que le menu ne transporte pas
	// non plus : la recherche est une fonction PURE du relief, des regles et de
	// la graine, donc la transporter doublerait une donnee deterministe. Et il
	// FAUT la faire ici, sans quoi une partie lancee depuis le menu n'aurait
	// aucun lieu remarquable la ou une partie lancee en PIE en a.
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

FBox AWorldseedVoxelTerrain::ChunkBoundsM(const FWorldseedChunkKey& Key) const
{
	const double Side = CoteM(Key.Niveau);
	const FVector Min(Key.C.X * Side, Key.C.Y * Side, Key.C.Z * Side);
	return FBox(Min, Min + FVector(Side, Side, Side));
}

int32 AWorldseedVoxelTerrain::NiveauEmis(const FVector& PointM) const
{
	// ON LIT L'ENSEMBLE EMIS, ON NE LE REDERIVE PLUS, ET C'EST LE COEUR DE
	// TOUT CE FICHIER.
	//
	// IL Y AVAIT ICI UNE DESCENTE PONCTUELLE qui rejouait le meme predicat que
	// la diffusion, au motif -- ecrit en toutes lettres -- qu'un predicat
	// unique suffirait a garder les deux d'accord. C'est vrai tant que la
	// partition est une fonction du POINT. Elle a cesse de l'etre le jour ou le
	// 2:1 a du etre equilibre : l'equilibrage regarde les VOISINS, donc le
	// niveau d'une feuille depend de ses voisines et non plus d'elle seule.
	// Aucune descente ponctuelle ne peut reproduire cela, si fidele soit-elle
	// au predicat.
	//
	// La seule reponse juste est donc de demander a l'ensemble reellement emis
	// -- une source de verite au lieu de deux calculs qu'on espere d'accord.
	//
	// INDEX_NONE PLUTOT QU'UN NIVEAU PAR DEFAUT : hors de l'ensemble, il n'y a
	// pas de feuille, et pretendre un niveau ferait armer une face de
	// transition vers un voisin qui n'existe pas. C'est a l'appelant de dire
	// ce que l'absence signifie pour lui.
	for (int32 N = FMath::Max(NiveauMax, 0); N >= 0; --N)
	{
		const double Cote = CoteM(N);
		const FWorldseedChunkKey Cle{
			FIntVector(
				FMath::FloorToInt(PointM.X / Cote),
				FMath::FloorToInt(PointM.Y / Cote),
				FMath::FloorToInt(PointM.Z / Cote)),
			N };
		if (FeuillesCourantes.Contains(Cle))
		{
			return N;
		}
	}
	return INDEX_NONE;
}

int32 AWorldseedVoxelTerrain::NiveauEstime(const FVector& PointM,
	const FVector& OrigineM) const
{
	const int32 Emis = NiveauEmis(PointM);
	if (Emis != INDEX_NONE)
	{
		return Emis;
	}

	// HORS DE L'ENSEMBLE, ON N'A QUE LA DISTANCE -- et elle suffit, parce
	// qu'elle est le critere PRINCIPAL et qu'elle ne depend d'aucun voisin.
	// Ce chemin ne sert qu'a decrire un point qui n'est pas maille : le filet
	// du joueur pendant la mise en place, et le releve d'ecran. Jamais a
	// decider d'une face de transition.
	int32 Niveau = FMath::Max(NiveauMax, 0);
	while (Niveau > 0 && FVector::Dist(PointM, OrigineM) < RayonAnneauM(Niveau - 1))
	{
		--Niveau;
	}
	return Niveau;
}

uint8 AWorldseedVoxelTerrain::MasqueDe(const FWorldseedChunkKey& Key) const
{
	// Le niveau le plus fin ne peut pas avoir de voisin plus fin.
	if (Key.Niveau <= 0) { return 0; }

	const double Cote = CoteM(Key.Niveau);
	const FVector Centre = ChunkBoundsM(Key).GetCenter();

	// Meme ordre que WorldseedTransvoxel::EFace : -X, +X, -Y, +Y, -Z, +Z.
	static const FVector Normales[6] =
	{
		FVector(-1, 0, 0), FVector(1, 0, 0),
		FVector(0, -1, 0), FVector(0, 1, 0),
		FVector(0, 0, -1), FVector(0, 0, 1),
	};

	uint8 Masque = 0;
	for (int32 F = 0; F < 6; ++F)
	{
		const FVector Voisin = Centre + Normales[F] * Cote;
		const int32 NiveauVoisin = NiveauEmis(Voisin);

		// PAS DE VOISIN, PAS DE TRANSITION. Au bord du rayon de chargement il
		// n'y a rien a coudre : armer une face vers le vide retrancherait un
		// demi-voxel de geometrie sans rien y gagner, et la jointure se verrait
		// comme une marche au lieu d'un bord franc.
		if (NiveauVoisin != INDEX_NONE && NiveauVoisin < Key.Niveau)
		{
			Masque |= static_cast<uint8>(1 << F);
		}
	}
	return Masque;
}

void AWorldseedVoxelTerrain::PlageSurface(int32 CX, int32 CY, int32 Niveau,
	float& OutMinM, float& OutMaxM) const
{
	const FIntVector Cle(CX, CY, Niveau);
	if (const FVector2D* Deja = CacheSurface.Find(Cle))
	{
		OutMinM = static_cast<float>(Deja->X);
		OutMaxM = static_cast<float>(Deja->Y);
		return;
	}

	const double Cote = CoteM(Niveau);
	Density->SurfaceRangeM(CX * Cote, CY * Cote,
		(CX + 1) * Cote, (CY + 1) * Cote, OutMinM, OutMaxM);

	// LE CACHE SE VIDE PLUTOT QUE DE GONFLER SANS FIN. Un joueur qui traverse
	// le monde finirait par l'emplir ; le repartir de zero coute une passe de
	// recalcul et rien d'autre, puisque la donnee est deterministe.
	if (CacheSurface.Num() > 200000)
	{
		CacheSurface.Reset();
	}
	CacheSurface.Add(Cle, FVector2D(OutMinM, OutMaxM));
}

bool AWorldseedVoxelTerrain::DoitSubdiviser(const FWorldseedChunkKey& Key,
	const FVector& OrigineM) const
{
	if (Key.Niveau <= 0)
	{
		return false;
	}

	// --- 1. LA DISTANCE ----------------------------------------------------
	// C'est le critere d'origine, et il reste PREMIER : un terrain lointain ne
	// merite pas de finesse, si accidente soit-il.
	const FVector Centre = ChunkBoundsM(Key).GetCenter();
	if (FVector::Dist(Centre, OrigineM) >= RayonAnneauM(Key.Niveau - 1))
	{
		return false;
	}

	// --- 2. LE RELIEF ------------------------------------------------------
	//
	// « Dans une plaine nous n'avons pas besoin d'un maillage aussi dense que
	// sur une montagne rocheuse. » C'est juste, et la raison est geometrique :
	// le marching cubes depense environ deux triangles par metre carre de
	// SURFACE, que cette surface soit plate ou non. Sur un terrain quasi
	// horizontal, doubler la resolution quadruple les triangles pour decrire
	// la MEME nappe -- on paie quatre fois la meme forme.
	//
	// LA GRANDEUR EST UNE PENTE, PAS UNE AMPLITUDE. Rapporter l'etendue
	// d'altitude au cote du noeud donne un nombre sans dimension, donc
	// comparable d'un niveau a l'autre : un noeud de 256 m qui varie de 25 m
	// et un noeud de 32 m qui varie de 3,2 m decrivent le meme terrain et
	// doivent recevoir la meme reponse. Une amplitude en metres aurait
	// repondu differemment aux deux.
	if (RugositeMin > 0.0f && !NoeudAccidente(Key))
	{
		return false;
	}

	return true;
}

bool AWorldseedVoxelTerrain::NoeudAccidente(const FWorldseedChunkKey& Key) const
{
	const FIntVector Cle(Key.C.X, Key.C.Y, Key.Niveau);
	if (const uint8* Deja = CacheRugosite.Find(Cle))
	{
		return *Deja != 0;
	}

	const double Cote = CoteM(Key.Niveau);

	// --- L'EMPRISE EST ELARGIE D'UNE CELLULE, ET C'EST CE QUI GARANTIT LE 2:1
	//
	// Transvoxel ne sait coudre QU'UN niveau d'ecart. Deux feuilles voisines a
	// deux niveaux d'ecart rouvrent une fissure que rien ne fermerait, et rien
	// ne le signalerait. Or un critere purement LOCAL produit exactement cela :
	// une plaine collee a une falaise: la plaine reste grossiere, la falaise
	// descend deux fois.
	//
	// Prendre le maximum du relief sur l'emprise ELARGIE le resout par
	// construction, et la preuve tient en une recurrence. Si un noeud A se
	// subdivise, c'est que son emprise elargie est accidentee ; or cette
	// emprise contient ses voisins immediats, donc chaque voisin B voit lui
	// aussi ce relief dans SA propre emprise elargie, donc B se subdivise
	// aussi. A chaque niveau, « A descend » implique « les voisins de A
	// descendent » : deux feuilles voisines ne peuvent donc pas differer de
	// plus d'un cran.
	//
	// Cela coute huit lectures de plus, toutes servies par le cache de
	// `PlageSurface` -- qui est lui-meme deja peuple par la diffusion.
	float Min = TNumericLimits<float>::Max();
	float Max = TNumericLimits<float>::Lowest();
	for (int32 DY = -1; DY <= 1; ++DY)
	{
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			float M = 0.0f;
			float X = 0.0f;
			PlageSurface(Key.C.X + DX, Key.C.Y + DY, Key.Niveau, M, X);
			Min = FMath::Min(Min, M);
			Max = FMath::Max(Max, X);
		}
	}

	// ON RAPPORTE L'ETENDUE A L'EMPRISE REELLEMENT OBSERVEE, ET CE N'EST PAS
	// UN DETAIL. La premiere version divisait par le cote du NOEUD alors que
	// l'etendue etait relevee sur les NEUF -- soit une pente sous-estimee d'un
	// facteur trois, et un critere qui laissait tout passer. Le signe etait
	// sans appel : a 0,15 comme a 0, le banc rendait 2165 chunks, 1162/615/388
	// par niveau et 2 809 559 triangles, AU CHIFFRE PRES. Ce depot a maintenant
	// rencontre six fois ce meme signe -- deux mesures identiques pour deux
	// reglages differents ne sont jamais un hasard.
	const float Emprise = 3.0f * static_cast<float>(Cote);
	bool bAccidente = (Max - Min) >= RugositeMin * Emprise;

	// --- ET LA SURFACE NE DIT PAS TOUT ------------------------------------
	//
	// UN AVEN S'OUVRE SUR UN PLATEAU : c'est sa definition meme, et le depot
	// l'a ecrit en toutes lettres en distinguant les trois formes d'ouverture
	// -- « la bouche s'ouvre a l'horizontale dans un versant recoupe par une
	// vallee, l'aven s'ouvre a la verticale la ou l'eau s'infiltre a travers
	// un plateau ». Juger la finesse sur le relief de SURFACE degraderait donc
	// precisement les endroits ou le sous-sol est le plus interessant : avens,
	// dolines, salles, et les diaclases qui se referment en profondeur.
	//
	// On interroge donc l'index spatial du reseau. Il est deja bati, la
	// requete est une intersection de boites, et elle n'a lieu que pour les
	// noeuds que le relief venait de declarer plats.
	if (!bAccidente && CaveNetwork.IsValid())
	{
		FWorldseedCaveLocal Local;
		CaveNetwork.Query(ChunkBoundsM(Key).ExpandBy(DensityRules.CaveBlendM + 4.0f), Local);
		bAccidente = !Local.IsEmpty();
	}

	// Meme politique que le cache de surface : on repart de zero plutot que de
	// gonfler sans fin, la donnee etant deterministe.
	if (CacheRugosite.Num() > 200000)
	{
		CacheRugosite.Reset();
	}
	CacheRugosite.Add(Cle, bAccidente ? 1 : 0);
	return bAccidente;
}
void AWorldseedVoxelTerrain::Enumerer(const FWorldseedChunkKey& Key,
	const FVector& OrigineM,
	TArray<TPair<FWorldseedChunkKey, double>>& Sortie,
	bool bEmissionForcee) const
{
	const FBox Boite = ChunkBoundsM(Key);

	// ON ECARTE PAR LA DISTANCE A LA BOITE, PAS AU CENTRE, et seulement ici.
	// Un noeud grossier dont le CENTRE est hors du rayon peut tres bien avoir
	// des enfants dedans : l'ecarter sur son centre creuserait un trou. La
	// distance a la boite, elle, ne peut que diminuer en descendant.
	//
	// L'EMISSION FORCEE LEVE CE FILTRE, ET IL LE FAUT. Quand l'equilibrage
	// subdivise un noeud pour tenir le 2:1, certains de ses huit enfants
	// tombent hors du rayon alors que le PARENT, lui, y etait -- son centre
	// etait plus proche. Les filtrer laisserait exactement le trou que la
	// subdivision devait eviter. Ce n'est pas une entorse au rayon : la
	// matiere etait deja chargee sous forme du parent, on ne fait que la
	// decouper.
	if (!bEmissionForcee
		&& Boite.ComputeSquaredDistanceToPoint(OrigineM) >
			static_cast<double>(LoadRadiusM) * LoadRadiusM)
	{
		return;
	}

	// LA GRILLE 2D DECIDE DE LA VERTICALE : on ne descend pas dans un noeud que
	// la surface ne traverse pas, ni dans la bande creusable sous elle.
	float SurfaceMin = 0.0f;
	float SurfaceMax = 0.0f;
	PlageSurface(Key.C.X, Key.C.Y, Key.Niveau, SurfaceMin, SurfaceMax);
	if (Boite.Min.Z > SurfaceMax ||
		Boite.Max.Z < SurfaceMin - DensityRules.BandDepthM)
	{
		return;
	}

	const FVector Centre = Boite.GetCenter();
	const double Dist = FVector::Dist(Centre, OrigineM);

	// SUBDIVISER OU EMETTRE, JAMAIS LES DEUX. C'est ce qui fait de la diffusion
	// une partition : ni recouvrement, ni trou, quelle que soit la facon dont
	// les rayons sont choisis.
	//
	// LE PREDICAT VIT AILLEURS, EN UN SEUL EXEMPLAIRE : `NiveauEn` doit poser
	// exactement la meme question pour que les masques de transition tombent
	// juste.
	if (DoitSubdiviser(Key, OrigineM))
	{
		for (int32 I = 0; I < 8; ++I)
		{
			const FWorldseedChunkKey Enfant{
				FIntVector(
					Key.C.X * 2 + (I & 1),
					Key.C.Y * 2 + ((I >> 1) & 1),
					Key.C.Z * 2 + ((I >> 2) & 1)),
				Key.Niveau - 1 };
			// LE FORCAGE NE SE PROPAGE PAS. Il ne vaut que pour le noeud qu'on
			// vient d'ouvrir : ses enfants retrouvent le regime normal, et
			// l'equilibrage les reprendra au tour suivant s'ils doivent
			// descendre encore. Le propager creuserait tout le sous-arbre a
			// pleine finesse pour une seule contrainte de voisinage.
			Enumerer(Enfant, OrigineM, Sortie, false);
		}
		return;
	}

	if (!bEmissionForcee && Dist > LoadRadiusM)
	{
		return;
	}
	Sortie.Emplace(Key, Dist);
}

void AWorldseedVoxelTerrain::Equilibrer(
	TArray<TPair<FWorldseedChunkKey, double>>& Feuilles,
	const FVector& OrigineM)
{
	// --- POURQUOI CETTE PASSE EXISTE, ET POURQUOI AUCUN CRITERE LOCAL NE LA
	//     REMPLACE -----------------------------------------------------------
	//
	// `NoeudAccidente` portait une preuve de l'equilibrage 2:1 : « si un noeud
	// A se subdivise, son emprise elargie est accidentee ; cette emprise
	// contient ses voisins immediats, donc chaque voisin B voit le meme relief
	// et se subdivise aussi ». Elle est JUSTE A UN NIVEAU DONNE, et FAUSSE d'un
	// niveau a l'autre, parce que le seuil est une PENTE -- donc il est divise
	// par deux a chaque descente.
	//
	// Soit A au niveau L, B son voisin, C un enfant de B au niveau L-1.
	// L'emprise elargie de A contient B, donc etendue(C) <= etendue(A). Mais :
	//
	//     C descend si  etendue(C) >= R x 3 x Cote(L) / 2
	//     A descend si  etendue(A) >= R x 3 x Cote(L)
	//
	// Entre les deux seuils, C descend et A reste : DEUX CRANS D'ECART. La
	// recurrence ne saute pas par accident, elle ne PEUT pas fermer -- une
	// grandeur sans dimension ne peut pas etre monotone contre un seuil qui
	// change d'echelle.
	//
	// Il n'existe donc aucune retouche locale qui garde l'invariance d'echelle.
	// Un seuil en metres absolus la fermerait, mais sur ce monde -- pente
	// mediane 30,6 degres -- tout noeud grossier le depasserait et le gain
	// disparaitrait. On equilibre donc l'ENSEMBLE EMIS.
	//
	// ET C'EST CE QUI A TUE `NiveauEn`. Tant que la partition etait une
	// fonction du POINT, une descente ponctuelle pouvait la reproduire. Elle
	// ne l'est plus : le niveau d'une feuille depend de ses VOISINES. Le masque
	// de transition lit donc desormais l'ensemble (`NiveauEmis`) au lieu de le
	// rederiver -- une source de verite au lieu de deux calculs qu'on espere
	// d'accord.
	FeuillesCourantes.Reset();
	FeuillesCourantes.Reserve(Feuilles.Num());
	for (const TPair<FWorldseedChunkKey, double>& F : Feuilles)
	{
		FeuillesCourantes.Add(F.Key);
	}

	if (NiveauMax <= 0)
	{
		// Un seul niveau : il n'y a rien a equilibrer, et la diffusion est
		// RIGOUREUSEMENT celle d'avant les anneaux. C'est la propriete de
		// surete de ce chantier, et elle doit rester gratuite.
		return;
	}

	// Meme ordre que le masque : ce sont les six faces qui se cousent.
	static const FVector Normales[6] =
	{
		FVector(-1, 0, 0), FVector(1, 0, 0),
		FVector(0, -1, 0), FVector(0, 1, 0),
		FVector(0, 0, -1), FVector(0, 0, 1),
	};

	// --- ON SONDE DEPUIS LA FEUILLE FINE, ET C'EST UNE CORRECTION -----------
	//
	// PREMIERE VERSION FAUSSE : chaque feuille GROSSIERE sondait ses six faces
	// en leur centre, et marquait le noeud a subdiviser si elle y voyait plus
	// fin. Le raisonnement etait bon, l'echantillonnage non -- la face d'un
	// chunk de niveau 3 touche jusqu'a SOIXANTE-QUATRE chunks de niveau 0, et
	// un point par face n'en voit qu'un seul. La mesure l'a dit : « ECART 2:1
	// -- 87 sur 512 feuilles examinees », en regime etabli, apres equilibrage.
	//
	// SONDER DEPUIS LE COTE FIN EST COMPLET PAR CONSTRUCTION. Le point juste
	// au-dela de la face d'une feuille tombe forcement DANS la feuille qui
	// couvre cette position, quel que soit le niveau de celle-ci -- puisque le
	// voisin, s'il est plus grossier, est plus GRAND que le point sonde. Toute
	// paire adjacente a plus d'un cran d'ecart est donc vue, exactement une
	// fois, depuis sa moitie fine. On marque alors LE VOISIN, pas soi-meme.
	//
	// C'est la meme lecon que le routage des galeries : quand une correction ne
	// deplace pas la mesure, le defaut n'est pas dans le reglage mais dans ce
	// qu'on regarde.
	//
	// LE NOMBRE DE TOURS EST BORNE PAR LA PROFONDEUR, et ce n'est pas une
	// precaution : chaque tour ne remonte un noeud que d'UN cran, donc au pire
	// on epuise les niveaux. Une boucle non bornee sur une structure qu'on
	// modifie en la parcourant est exactement ce qu'on ne veut pas sur le fil
	// de jeu.
	int32 Ajoutees = 0;
	int32 Restants = 0;
	for (int32 Tour = 0; Tour <= FMath::Max(NiveauMax, 1); ++Tour)
	{
		TSet<FWorldseedChunkKey> ASubdiviser;
		for (const FWorldseedChunkKey& Cle : FeuillesCourantes)
		{
			const double Cote = CoteM(Cle.Niveau);
			const FVector Centre = ChunkBoundsM(Cle).GetCenter();
			for (const FVector& D : Normales)
			{
				const FVector P = Centre + D * Cote;
				const int32 NV = NiveauEmis(P);
				if (NV != INDEX_NONE && NV > Cle.Niveau + 1)
				{
					// LA CLE DU VOISIN, au niveau ou il a ete emis : c'est LUI
					// qui doit descendre, pas la feuille qui le signale.
					const double CoteV = CoteM(NV);
					ASubdiviser.Add(FWorldseedChunkKey{
						FIntVector(
							FMath::FloorToInt(P.X / CoteV),
							FMath::FloorToInt(P.Y / CoteV),
							FMath::FloorToInt(P.Z / CoteV)),
						NV });
				}
			}
		}

		Restants = ASubdiviser.Num();
		if (Restants == 0)
		{
			break;
		}

		for (const FWorldseedChunkKey& Cle : ASubdiviser)
		{
			FeuillesCourantes.Remove(Cle);

			TArray<TPair<FWorldseedChunkKey, double>> Enfants;
			for (int32 I = 0; I < 8; ++I)
			{
				const FWorldseedChunkKey Enfant{
					FIntVector(
						Cle.C.X * 2 + (I & 1),
						Cle.C.Y * 2 + ((I >> 1) & 1),
						Cle.C.Z * 2 + ((I >> 2) & 1)),
					Cle.Niveau - 1 };
				Enumerer(Enfant, OrigineM, Enfants, true);
			}
			for (const TPair<FWorldseedChunkKey, double>& E : Enfants)
			{
				FeuillesCourantes.Add(E.Key);
				Feuilles.Add(E);
				++Ajoutees;
			}
		}

		// LES FEUILLES REMPLACEES SORTENT DU TABLEAU, pas seulement de
		// l'ensemble. Sans cela la diffusion lancerait le maillage d'un chunk
		// grossier ET de ses enfants : de la geometrie dessinee en double,
		// exactement ce que la partition existe pour interdire.
		Feuilles.RemoveAll([this](const TPair<FWorldseedChunkKey, double>& F)
		{
			return !FeuillesCourantes.Contains(F.Key);
		});
	}

	EquilibrageAjouts = Ajoutees;

	// --- LE CONTROLE VIT ICI, ET NON DANS LE BALAYAGE DES MASQUES ----------
	//
	// IL Y ETAIT, ET IL OSCILLAIT. Le balayage n'examine que 512 feuilles par
	// passe, avec un curseur qui TOURNE : le compte sautait de 87 a 0 et
	// revenait, et la ligne « resorbe » ne disait jamais que « aucune dans
	// cette fenetre-ci ». Un controle dont la valeur depend de la fenetre
	// qu'on regarde ne mesure pas la propriete, il mesure la fenetre.
	//
	// Ici il est EXACT et GRATUIT. La boucle ci-dessus parcourt toutes les
	// feuilles ; `Restants` vaut zero si et seulement si elle s'est arretee
	// faute de violation a corriger, et sinon le nombre de noeuds encore
	// fautifs apres avoir epuise les tours. C'est la reponse a « est-ce vrai
	// maintenant », sur l'ensemble entier.
	if (Restants != DernierEcart2a1Dit)
	{
		if (Restants > 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] voxel : ECART 2:1 NON RESORBE -- %d noeuds restent ")
				TEXT("a plus d'un cran de leur voisin apres %d tours ")
				TEXT("(rugositeMin %.3f, %d feuilles). Fissures possibles."),
				Restants, FMath::Max(NiveauMax, 1) + 1, RugositeMin,
				FeuillesCourantes.Num());
		}
		else if (DernierEcart2a1Dit > 0)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : ECART 2:1 resorbe sur l'ensemble des ")
				TEXT("%d feuilles"), FeuillesCourantes.Num());
		}
		DernierEcart2a1Dit = Restants;
	}
}

FVector AWorldseedVoxelTerrain::ChunkCentreCm(const FWorldseedChunkKey& Key) const
{
	const FBox B = ChunkBoundsM(Key);
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
		const FVector CentreM = ChunkBoundsM(Pair.Key).GetCenter();
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
	const double CoteHaute = CoteM(NiveauHaut);
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
			PlageSurface(CX, CY, NiveauHaut, SurfaceMin, SurfaceMax);

			const int32 ZBas = FMath::FloorToInt(
				(SurfaceMin - DensityRules.BandDepthM) / CoteHaute);
			const int32 ZHaut = FMath::FloorToInt(SurfaceMax / CoteHaute);

			for (int32 CZ = ZBas; CZ <= ZHaut; ++CZ)
			{
				Enumerer(FWorldseedChunkKey{ FIntVector(CX, CY, CZ), NiveauHaut },
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
	Equilibrer(Feuilles, OriginM);

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
			if (S->Masque != MasqueDe(Cle))
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

	HoldOrReleasePlayer();
}

void AWorldseedVoxelTerrain::LaunchJob(const FWorldseedChunkKey& Key)
{
	WORLDSEED_TRACE(LancerTravail);

	FWorldseedVoxelChunkState& State = Chunks.FindOrAdd(Key);

	FWorldseedVoxelJobPtr Job = MakeShared<FWorldseedVoxelJob, ESPMode::ThreadSafe>();
	Job->Key = Key;
	Job->BoundsM = ChunkBoundsM(Key);
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
	const float VoxelSizeM = VoxelM(Key.Niveau);
	const float Largeur = LargeurTransition;

	// LE MASQUE EST CALCULE ICI, SUR LE FIL DE JEU, ET RETENU DANS L'ETAT. Il
	// depend de l'origine de diffusion, qui bouge : le retenir est ce qui permet
	// de savoir, a la passe suivante, qu'il a change et qu'il faut remailler.
	const FVector OrigineM =
		(StreamingOriginCm() - GetActorLocation()) / WorldseedMetersToCm;
	const uint8 Masque = MasqueDe(Key);
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
		const FVector CentreM = ChunkBoundsM(Key).GetCenter();
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
			const FBox B = ChunkBoundsM(Key);
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
	WORLDSEED_TRACE(PeindreSommets);

	const int32 Count = Mesh.Positions.Num();
	Mesh.Colours.SetNumUninitialized(Count);

	// LA CARTE EST PRISE UNE FOIS, PAS PAR SOMMET. L'accesseur passe par le
	// monde partage : un test de validite et un dereferencement, donc presque
	// rien -- mais cette boucle tourne sur 1,67 million de sommets, et la lier
	// une fois dit aussi ce qui est vrai : la carte ne change pas pendant la
	// peinture.
	const FWorldseedBiomeMap& Carte = Biomes();

	const bool bHasBiomes = (Carte.Index.Num() == Geometry.CellCount());
	const bool bHasCover = (Carte.Cover.Num() == Geometry.CellCount());
	const bool bAvecRoche = Lithology.IsValid(Geometry.CellCount())
		&& CouleurParRoche.Num() > 0;
	const FWorldseedDensityRules& Rules = DensityRules;

	const double WidthM = Geometry.WidthM();
	const double HeightM = Geometry.HeightM;

	for (int32 I = 0; I < Count; ++I)
	{
		if (!bHasBiomes)
		{
			Mesh.Colours[I] = FLinearColor::Gray;
			continue;
		}

		// Le sommet est en centimetres dans le repere de l'acteur ; la carte
		// des biomes est une grille 2D en longitude/latitude.
		const double X = Mesh.Positions[I].X / WorldseedMetersToCm;
		const double Y = Mesh.Positions[I].Y / WorldseedMetersToCm;

		double U = X / WidthM + 0.5;
		U -= FMath::FloorToDouble(U);
		const double V = FMath::Clamp(Y / HeightM + 0.5, 0.0, 1.0);

		const int32 Col = FMath::Clamp(
			FMath::FloorToInt(U * Geometry.NX), 0, Geometry.NX - 1);
		const int32 Row = FMath::Clamp(
			FMath::FloorToInt(V * Geometry.NY), 0, Geometry.NY - 1);

		// LA COULEUR SUIT LE SUBSTRAT QUAND IL Y EN A UN. Depuis que la roche
		// a nu et l'estran ont quitte l'axe des biomes, l'index porte le climat
		// meme sur une paroi : le lire seul peindrait la falaise en vert.
		const int32 Cell = Row * Geometry.NX + Col;
		const EWorldseedBiome Biome = bHasCover
			? WorldseedBiomes::AppearanceBiome(Carte.Index[Cell], Carte.Cover[Cell])
			: static_cast<EWorldseedBiome>(Carte.Index[Cell]);
		FLinearColor Teinte = WorldseedBiomes::Colour(Biome);

		// --- SOUS TERRE, C'EST LA ROCHE QUI HABILLE -------------------------
		//
		// Ce calcul etait purement 2D : on lisait le biome de la colonne et on
		// peignait, sans aucune notion de profondeur. Une paroi de grotte a
		// quarante metres sous une prairie rendait donc VERTE -- constate a
		// l'image dans la salle sous le gouffre, et c'est ce qui rendait les
		// cavites illisibles meme une fois eclairees.
		//
		// La couleur d'une paroi est celle de sa ROCHE, et la lithologie la
		// porte deja : calcaire creme, granite gris rose, basalte sombre. Meme
		// doctrine que partout ailleurs dans cette passe -- la roche decide.
		if (bAvecRoche && Rules.RockColourFadeM > 0.0f)
		{
			const double Z = Mesh.Positions[I].Z / WorldseedMetersToCm;
			const double Profondeur = Density->SurfaceHeightM(X, Y) - Z;
			if (Profondeur > 0.0)
			{
				// --- LA ROCHE SE LIT EN TROIS DIMENSIONS --------------------
				//
				// C'ETAIT UNE ROCHE PAR COLONNE, donc une paroi d'une seule
				// teinte du sommet au pied. Or un vrai sous-sol est FEUILLETE,
				// et c'est exactement ce qui donne au Grand Canyon ses rayures :
				// les bancs durs font les corniches, les tendres les talus, et
				// chacun a sa couleur.
				//
				// La serie ne recouvre que le SEDIMENTAIRE : sur du granite ou
				// du basalte, la garde de durete ne passe pas et l'on retombe
				// sur la roche 2D, c'est-a-dire le comportement d'avant a
				// l'identique. Une donnee absente doit rester sans effet.
				uint8 Id = Lithology.Id[Cell];
				if (StratRules.IsActive())
				{
					const float DureteSocle = DureteParId.IsValidIndex(Id)
						? DureteParId[Id] : 1.0f;
					if (DureteSocle >= StratRules.SocleHardnessMin
						&& DureteSocle <= StratRules.SocleHardnessMax)
					{
						const int32 Banc = WorldseedStrata::BancAt(
							X, Y, Z, StratRules, WorldSeed);
						if (StratRules.Serie.IsValidIndex(Banc))
						{
							Id = StratRules.Serie[Banc].RockId;
						}
					}
				}
				if (CouleurParRoche.IsValidIndex(Id))
				{
					// --- LA PROFONDEUR SE COMPTE SOUS LE BRUIT, PAS SOUS LE
					//     RELIEF MACRO ---------------------------------------
					//
					// LE COTELE DU MONDE VENAIT D'ICI, et il a fallu trois
					// mesures pour y arriver : la geometrie est lisse -- le
					// profil d'un versant est une courbe en S sans une marche --
					// et le cotele SURVIT a `ShowFlag.Lighting 0`, donc ce
					// n'est ni la forme ni les normales, c'est la COULEUR. Le
					// temoin qui l'a nomme est cette teinte coupee : le monde
					// redevient d'un coup en aplats de biome.
					//
					// LE MECANISME. `Profondeur` se mesure contre la surface
					// MACRO, alors que le champ deplace la vraie surface de
					// plus ou moins dix metres -- surplombs et detail. Sur
					// chaque BOSSE la profondeur est donc negative et l'on
					// peint le biome ; dans chaque CREUX elle est positive et
					// l'on peint la roche. Le fondu valait douze metres et le
					// detail a la meme echelle : la couleur se mettait a suivre
					// le micro-relief, d'ou des rubans qui epousent les courbes
					// de niveau sur tout le monde.
					//
					// LA MARGE N'EST PAS UN REGLAGE, ELLE EST L'AMPLITUDE DU
					// DEPLACEMENT. Sous elle, on ne peut pas savoir si l'on est
					// dessus ou dessous ; au-dela, on est vraiment sous terre --
					// ce que cette teinte a toujours voulu dire : « une paroi
					// de grotte a quarante metres sous une prairie ne doit pas
					// rendre VERTE ».
					const double Marge = static_cast<double>(Rules.OverhangAmplitudeM)
						+ static_cast<double>(Rules.DetailAmplitudeM);

					const float T = FMath::Clamp(
						static_cast<float>(Profondeur - Marge) / Rules.RockColourFadeM,
						0.0f, 1.0f);
					Teinte = FMath::Lerp(Teinte, CouleurParRoche[Id], T);
				}
			}
		}

		Mesh.Colours[I] = Teinte;
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
	const int32 Niveau = NiveauEstime(PointM, OrigineM);
	const double Cote = CoteM(Niveau);

	return FWorldseedChunkKey{
		FIntVector(
			FMath::FloorToInt(X / Cote),
			FMath::FloorToInt(Y / Cote),
			FMath::FloorToInt(Z / Cote)),
		Niveau };
}

bool AWorldseedVoxelTerrain::TrouverTerreEmergee(const FVector2D& AutourM,
	double& OutX, double& OutY) const
{
	const int32 NX = Geometry.NX;
	const int32 NY = Geometry.NY;
	if (NX < 2 || NY < 2 || HeightsM().Num() != NX * NY)
	{
		return false;
	}

	// --- POURQUOI LA GRILLE 2D, ET PAS UNE SPIRALE DANS LE CHAMP ----------
	//
	// `FindFlatGround` fouille un voisinage en evaluant le champ de densite :
	// vingt-quatre anneaux au pas de seize metres, soit 384 m de portee et
	// deja 2401 evaluations. C'est le bon outil pour choisir OU se poser une
	// fois qu'on est sur la bonne terre -- et c'est le mauvais pour TROUVER
	// cette terre : ce monde est de l'ocean a 70,8 %, le point de depart tombe
	// au large, et il n'y a aucune terre a 384 m. Couvrir trente kilometres au
	// meme pas demanderait des millions d'evaluations.
	//
	// La grille des altitudes, elle, est DEJA EN MEMOIRE et elle est petite :
	// 4096 x 2048 valeurs que l'on balaie une fois, au demarrage, apres une
	// generation qui a dure une minute. C'est la meme doctrine que le reste du
	// diffuseur -- « la grille 2D decide, le champ affine ».
	//
	// ON PREND LA PLUS PROCHE, PAS LA MEILLEURE. Le joueur doit demarrer sur
	// la terre ; rien ne dit qu'il doive demarrer sur LA plus belle plaine du
	// monde, et viser un optimum global le ferait naitre chaque fois au meme
	// endroit quelle que soit la graine.
	//
	// ET ON EXIGE UNE CELLULE INTERIEURE, pas un liseré cotier : une cellule
	// emergee dont les quatre voisines le sont aussi. Sans cela on choisirait
	// volontiers un recif ou une pointe de sable ou `FindFlatGround` ne
	// trouverait ensuite ni pente douce ni roche pleine, et l'on serait revenu
	// au point de depart avec une etape de plus.
	//
	// LE PLANCHER SE DEDUIT DU BRUIT, IL N'EST PAS CHOISI. La grille de
	// simulation dit une altitude ; le champ de densite y ajoute ensuite son
	// grain -- jusqu'a `OverhangAmplitudeM` de deplacement vertical et
	// `DetailAmplitudeM` de detail. Une cellule a cinq metres peut donc se
	// retrouver SOUS la ligne d'eau une fois maillee, et c'est exactement ce
	// que le premier essai a donne : « altitude 4,9 m », c'est-a-dire les
	// pieds dans l'eau au premier remous du bruit. On exige donc deux fois
	// l'amplitude que le voxel peut retirer.
	//
	// Le depot a deja la meme regle ailleurs, pour la meme raison : la marge
	// de sommet des arches existe parce que « le champ de densite deplace la
	// surface et la passe des cavites ne le sait pas ». Une constante en dur
	// aurait cesse d'etre juste au premier reglage du bruit.
	const float PlancherM = 2.0f
		* (DensityRules.OverhangAmplitudeM + DensityRules.DetailAmplitudeM);

	auto Emergee = [this, NX, NY](int32 I, int32 J, float Seuil) -> bool
	{
		const int32 IW = ((I % NX) + NX) % NX;
		const int32 JC = FMath::Clamp(J, 0, NY - 1);
		return HeightsM()[JC * NX + IW] > Seuil;
	};

	const double LargeurM = Geometry.WidthM();
	const double HauteurM = Geometry.HeightM;

	// Cellule du point demande, meme convention que `SampleUV` : X enroule,
	// Y est borne.
	const int32 I0 = FMath::Clamp(
		FMath::FloorToInt((AutourM.X / LargeurM + 0.5) * NX), 0, NX - 1);
	const int32 J0 = FMath::Clamp(
		FMath::FloorToInt((AutourM.Y / HauteurM + 0.5) * NY), 0, NY - 1);

	int32 MeilleurI = -1;
	int32 MeilleurJ = -1;
	int64 MeilleureDistance = TNumericLimits<int64>::Max();

	for (int32 J = 0; J < NY; ++J)
	{
		for (int32 I = 0; I < NX; ++I)
		{
			if (HeightsM()[J * NX + I] <= PlancherM)
			{
				continue;
			}
			if (!Emergee(I + 1, J, 0.0f) || !Emergee(I - 1, J, 0.0f)
				|| !Emergee(I, J + 1, 0.0f) || !Emergee(I, J - 1, 0.0f))
			{
				continue;
			}

			// X ENROULE, DONC LA DISTANCE AUSSI. Mesurer l'ecart en colonnes
			// sans tenir compte du bouclage ferait croire qu'une terre situee
			// juste de l'autre cote de la couture est a un monde de distance.
			int64 DI = FMath::Abs(static_cast<int64>(I) - I0);
			DI = FMath::Min(DI, static_cast<int64>(NX) - DI);
			const int64 DJ = static_cast<int64>(J) - J0;

			const int64 D2 = DI * DI + DJ * DJ;
			if (D2 < MeilleureDistance)
			{
				MeilleureDistance = D2;
				MeilleurI = I;
				MeilleurJ = J;
			}
		}
	}

	if (MeilleurI < 0)
	{
		return false;
	}

	// Centre de la cellule : le coin serait sur la frontiere avec une cellule
	// qui peut etre marine.
	OutX = (static_cast<double>(MeilleurI) + 0.5) / NX * LargeurM - LargeurM * 0.5;
	OutY = (static_cast<double>(MeilleurJ) + 0.5) / NY * HauteurM - HauteurM * 0.5;
	return true;
}
bool AWorldseedVoxelTerrain::FindFlatGround(const FVector2D& AroundM,
	double& OutX, double& OutY, float& OutSurfaceM, float& OutSlopeDeg,
	float PenteMaxDeg, float EcartAltitudeMaxM, float AltitudeRefM) const
{
	// Spirale carree autour du point demande : on prend le PREMIER endroit
	// acceptable, donc le plus proche, et non le meilleur du monde.
	constexpr double PasM = 16.0;
	constexpr int32 Anneaux = 24;
	constexpr double SondeM = 6.0;        // ecart pour estimer la pente

	auto Convient = [this, PenteMaxDeg, EcartAltitudeMaxM, AltitudeRefM]
		(double X, double Y, float& Surface, float& PenteDeg) -> bool
	{
		Surface = Density->SurfaceHeightM(X, Y);
		if (Surface < 2.0f)
		{
			return false;   // sous la mer, ou tout juste au bord
		}

		// LA BORNE D'ALTITUDE PASSE AVANT LA PENTE, parce qu'elle est
		// beaucoup plus selective et qu'elle coute une soustraction quand
		// l'autre coute quatre echantillonnages du champ.
		if (EcartAltitudeMaxM > 0.0f
			&& FMath::Abs(Surface - AltitudeRefM) > EcartAltitudeMaxM)
		{
			return false;
		}

		const float HX = Density->SurfaceHeightM(X + SondeM, Y)
			- Density->SurfaceHeightM(X - SondeM, Y);
		const float HY = Density->SurfaceHeightM(X, Y + SondeM)
			- Density->SurfaceHeightM(X, Y - SondeM);
		const float Pente = FMath::Sqrt(HX * HX + HY * HY) / (2.0f * SondeM);
		PenteDeg = FMath::RadiansToDegrees(FMath::Atan(Pente));
		if (PenteDeg > PenteMaxDeg)
		{
			return false;
		}

		// PLEIN SOUS LES PIEDS, sur toute la hauteur d'une galerie typique :
		// un plancher de deux metres au-dessus d'un vide ne tient pas.
		for (double Profondeur = 1.0; Profondeur <= 12.0; Profondeur += 2.0)
		{
			if (Density->At(FVector(X, Y, Surface - Profondeur)) > 0.0)
			{
				return false;
			}
		}
		return true;
	};

	for (int32 Anneau = 0; Anneau <= Anneaux; ++Anneau)
	{
		for (int32 DY = -Anneau; DY <= Anneau; ++DY)
		{
			for (int32 DX = -Anneau; DX <= Anneau; ++DX)
			{
				// Seulement le bord de l'anneau : l'interieur a deja ete vu.
				if (Anneau > 0 && FMath::Abs(DX) != Anneau && FMath::Abs(DY) != Anneau)
				{
					continue;
				}

				const double X = AroundM.X + DX * PasM;
				const double Y = AroundM.Y + DY * PasM;

				float Surface = 0.0f;
				float PenteDeg = 0.0f;
				if (Convient(X, Y, Surface, PenteDeg))
				{
					OutX = X;
					OutY = Y;
					OutSurfaceM = Surface;
					OutSlopeDeg = PenteDeg;
					return true;
				}
			}
		}
	}
	return false;
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
		// UNE DESTINATION DEMANDEE NE SE CORRIGE PAS. FindFlatGround fouille un
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
		// RECHERCHE, et rien d'autre. Tout ce qui suit -- terre emergee la
		// plus proche, puis sol plat -- s'applique ensuite a l'identique.
		// C'est le minimum : ecrire ici un second chemin de mise en place
		// ferait diverger deux moities qui doivent rester la meme.
		//
		// IL NE JOUE QU'UNE FOIS. Le filet de rattrapage rearme cette mise en
		// place quand le joueur passe sous la bande de terrain ; le rejouer le
		// ramenerait a son point de naissance a chaque chute, ce qui n'est pas
		// un filet mais une laisse.
		// LE DRAPEAU EST CONSOMME TOUT DE SUITE, MAIS LE FAIT SURVIT. La suite
		// a besoin de savoir qu'un point a ete CHOISI -- pour borner le
		// denivele -- et a quelle altitude il etait, pour la mesurer.
		const bool bChoisi = bDepartDemande;
		float SurfaceDemandeeM = 0.0f;

		if (bDepartDemande)
		{
			bDepartDemande = false;
			X = DepartXYM.X;
			Y = DepartXYM.Y;
			SurfaceM = Density->SurfaceHeightM(X, Y);
			SurfaceDemandeeM = SurfaceM;

			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : depart demande a (%.0f, %.0f) m, ")
				TEXT("surface %.1f m"), X, Y, SurfaceM);
		}

		// ON CHOISIT L'ENDROIT, ON NE SE CONTENTE PAS DE CELUI DU PlayerStart.
		// Il faut du plat, de l'emerge, et du plein dessous : une colonne sur
		// huit porte une galerie, et naitre au-dessus revient a tomber dedans.
		//
		// LA TERRE D'ABORD, LE SOL PLAT ENSUITE, et l'ordre est tout le
		// correctif. `FindFlatGround` refusait deja ce qui est sous la mer,
		// mais il ne cherche que dans 384 metres : sur un monde couvert
		// d'ocean a 70,8 %, le point de depart tombe au large et il n'y a
		// simplement aucune terre a cette distance. Mesure avant correction,
		// le journal etait sans appel -- « aucun sol plat autour du depart,
		// pose sur place », puis « joueur tenu a (0, 0) m, surface -153,8 m ».
		// Cent cinquante metres sous le niveau de la mer.
		//
		// La grille 2D donne la terre la plus proche en un balayage, et la
		// fouille fine repart de la. Chacune fait ce qu'elle sait faire.
		double TerreX = X;
		double TerreY = Y;
		if (TrouverTerreEmergee(FVector2D(X, Y), TerreX, TerreY))
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : terre emergee la plus proche a ")
				TEXT("(%.0f, %.0f) m, soit %.1f km du point demande"),
				TerreX, TerreY,
				FVector2D::Distance(FVector2D(X, Y), FVector2D(TerreX, TerreY)) / 1000.0);
			X = TerreX;
			Y = TerreY;
			SurfaceM = Density->SurfaceHeightM(X, Y);
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] voxel : AUCUNE terre emergee dans la grille -- ")
				TEXT("le monde est-il entierement sous l'eau ?"));
		}

		float PenteDeg = 0.0f;
		double FX = X;
		double FY = Y;
		float FSurface = SurfaceM;

		// --- QUAND LE JOUEUR A CHOISI, ON RESTE SUR SON RELIEF ---------------
		//
		// La spirale retient le PREMIER point acceptable, pas le meilleur : sur
		// un versant raide, le premier sol a moins de douze degres est la
		// plaine d'en bas. Mesure sur deux parties independantes -- latitudes
		// 73,1 et 15,3 degres, donc sans rapport de terrain -- le joueur
		// naissait 89 et 92 metres SOUS le point qu'il avait choisi. Qui visait
		// un sommet naissait a son pied.
		//
		// La passe bornee accepte une pente PLUS FORTE en echange d'un ecart
		// d'altitude PLUS FAIBLE : qui a vise un versant accepte d'etre sur un
		// versant, c'est la descente qu'il n'a pas demandee.
		// --- LE DEPART EXACT, POUR INSPECTER UN POINT PRECIS ----------------
		//
		// SIGNALE : « tu n'es pas a l'endroit de la capture que je t'ai
		// faite ». C'etait exact, et de loin : altitude demandee 925 m,
		// obtenue 10,8 -- NEUF CENT QUATORZE METRES plus bas, au niveau de la
		// mer. La passe bornee avait echoue (aucun sol a moins de 40 m
		// d'altitude sur ce versant a 18 degres) et le repli large avait pris
		// la plaine.
		//
		// C'est le bon comportement pour un JOUEUR -- on ne le fait pas naitre
		// sur une pente ou il glisse -- et le mauvais pour une INSPECTION : un
		// point de vue ne se juge que depuis le point de vue. Sans ce drapeau,
		// une couture qui ne se voit qu'a 930 m est inatteignable par
		// l'outillage, et c'est exactement le defaut qu'on cherche a regarder.
		bool bExact = false;
		{
			int32 Exact = 0;
			if (FParse::Value(FCommandLine::Get(), TEXT("WorldseedDepartExact="), Exact)
				&& Exact != 0)
			{
				bExact = true;
				UE_LOG(LogTemp, Warning,
					TEXT("[Worldseed] voxel : depart EXACT demande -- ")
					TEXT("aucune recherche de sol plat, pente non bornee"));
			}
		}

		bool bPose = false;
		bool bTenuSurPlace = false;

		if (bExact)
		{
			// On garde X, Y et SurfaceM tels quels : c'est tout l'objet.
			bPose = true;
			bTenuSurPlace = true;

			// LA PENTE SE MESURE QUAND MEME. Ce chemin rapportait « pente
			// 0,0 deg » sur n'importe quelle paroi, faute d'appeler la
			// recherche -- or c'est le drapeau qu'on emploie justement pour
			// aller inspecter des endroits impraticables, et savoir sur quoi
			// l'on vient de se poser fait partie de l'inspection.
			PenteDeg = WorldseedPlacement::PenteDeg(*Density, X, Y);
		}
		else if (bChoisi)
		{
			// LA BORNE SE SURCHARGE EN LIGNE DE COMMANDE, et c'est la regle du
			// depot : « quand un A/B demande un reglage qui n'a pas de
			// surcharge, on AJOUTE la surcharge ; on ne touche pas au
			// fichier ». Ici elle sert surtout a EPROUVER le chemin d'echec --
			// sur ce monde la spirale trouve presque toujours quelque chose
			// dans ses 384 metres, si bien qu'une valeur basse est le seul
			// moyen sur de faire jouer l'echec franc.
			float Ecart = EcartAltitudeDepartM;
			if (FParse::Value(FCommandLine::Get(),
				TEXT("WorldseedEcartDepart="), Ecart))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[Worldseed] voxel : ecart d'altitude tolere force a ")
					TEXT("%.1f m (defaut %.0f)"), Ecart, EcartAltitudeDepartM);
			}

			bPose = FindFlatGround(FVector2D(X, Y), FX, FY, FSurface, PenteDeg,
				PenteDepartMaxDeg, Ecart, SurfaceM);

			if (!bPose)
			{
				// --- ECHEC FRANC : ON TIENT LE POINT VISE --------------------
				//
				// ARBITRAGE DU PROPRIETAIRE, 22 septembre 2026. Il y avait ici
				// un repli vers la recherche LARGE -- douze degres, aucune
				// borne d'altitude -- et c'etait une FALAISE DE POLITIQUE :
				// quarante metres puis l'infini, en un cran. Mesure de
				// l'epoque, 357 metres plus bas que le point vise ; une autre
				// partie, 914 metres, au niveau de la mer. Qui visait un
				// sommet naissait dans la plaine.
				//
				// Les deux autres voies ont ete presentees et ecartees :
				// elargir par crans (40, 120, 360, sans borne) aurait garde un
				// echec possible mais graduel ; retenir le MEILLEUR de toute la
				// spirale aurait traite la cause nommee par le commentaire
				// d'origine -- « elle retient le PREMIER point acceptable, pas
				// le meilleur » -- mais au prix de la PROXIMITE, en naissant
				// jusqu'a 380 m du point vise pour gagner trois metres.
				//
				// CE QUI EST ACCEPTE EN ECHANGE, ET IL FAUT LE DIRE : le pion
				// peut naitre sur une paroi et glisser. Le sol n'est marchable
				// que jusqu'a 45 degres, et ce depot a deja mesure 2,7 km de
				// glissade depuis un depart a 22. Le filet de rattrapage reste
				// en place -- il rearme la mise en place quand le joueur passe
				// sous la bande -- mais il ne le ramenera pas ici : un depart
				// n'est consomme qu'UNE fois, sans quoi ce n'est plus un filet
				// mais une laisse.
				bTenuSurPlace = true;
				bPose = true;

				// LA PENTE SE MESURE QUAND MEME, pour que le releve dise sur
				// quoi on vient de poser le joueur. Sans elle, ce chemin
				// rapportait « pente 0,0 deg » sur une paroi a soixante.
				PenteDeg = WorldseedPlacement::PenteDeg(*Density, X, Y);

				UE_LOG(LogTemp, Warning,
					TEXT("[Worldseed] voxel : aucun sol tenable a moins de ")
					TEXT("%.0f m d'altitude du point choisi -- ON TIENT LE ")
					TEXT("POINT VISE (pente %.1f deg)"),
					Ecart, PenteDeg);

				// LE VIDE SOUS LES PIEDS NE DEPLACE PLUS PERSONNE, MAIS IL SE
				// DIT. Une colonne sur huit porte une galerie : tenir le point
				// peut donc poser le joueur au-dessus d'un plafond mince. On
				// ne corrige pas -- ce serait deplacer le point qu'on vient de
				// decider de tenir -- on previent.
				if (!WorldseedPlacement::SolPlein(*Density, X, Y, SurfaceM))
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[Worldseed] voxel : ET LA COLONNE EST CREUSE ")
						TEXT("sous ce point -- le joueur peut tomber dans une ")
						TEXT("cavite"));
				}
			}
		}
		else
		{
			// NAISSANCE LIBRE : l'endroit n'a aucune importance, donc la
			// recherche large reste la bonne reponse. Elle n'a pas change.
			bPose = FindFlatGround(FVector2D(X, Y), FX, FY, FSurface, PenteDeg);
		}

		// TROIS ISSUES, ET ELLES NE SE RESSEMBLENT PAS. On a DEPLACE le joueur
		// vers du plat, on a TENU son point, ou l'on n'a rien trouve du tout.
		// Les deux dernieres se lisaient pareil avant -- « pose sur place » --
		// alors que l'une est un choix et l'autre un echec.
		if (bPose && !bTenuSurPlace)
		{
			X = FX;
			Y = FY;
			SurfaceM = FSurface;
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : sol plat trouve a (%.0f, %.0f) m, ")
				TEXT("altitude %.1f m, pente %.1f deg%s"),
				X, Y, SurfaceM, PenteDeg,
				bChoisi
					? *FString::Printf(TEXT(" (%+.0f m du point choisi)"),
						SurfaceM - SurfaceDemandeeM)
					: TEXT(""));
		}
		else if (bPose)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] voxel : %s a (%.0f, %.0f) m, ")
				TEXT("altitude %.1f m, pente %.1f deg (ecart nul, par ")
				TEXT("construction)"),
				bExact ? TEXT("depart EXACT tenu") : TEXT("point vise TENU"),
				X, Y, SurfaceM, PenteDeg);
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] voxel : aucun sol plat autour du depart, ")
				TEXT("pose sur place"));
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
			AvecTransition, EquilibrageAjouts);
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
	const int32 Niveau = NiveauEstime(FVector(X, Y, Z), OrigineM);
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
