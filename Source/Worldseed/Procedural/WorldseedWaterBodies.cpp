// Worldseed - l'ocean confie au plugin Water d'Unreal.

#include "Procedural/WorldseedWaterBodies.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"



#include "Components/SceneComponent.h"
#include "UObject/UnrealType.h"
#include "Components/SplineComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "WaterBodyComponent.h"
#include "WaterBodyOceanActor.h"
#include "WaterBodyOceanComponent.h"
#include "WaterSplineComponent.h"
#include "WaterZoneActor.h"
#include "WaterMeshComponent.h"
#include "WaterSubsystem.h"
#include "WaterTerrainComponent.h"

namespace WorldseedWaterBodies
{
	namespace
	{

		/**
		 * Bornes de l'epaisseur de l'ocean, en centimetres.
		 *
		 * Elle sert un seul but : que l'intervalle de hauteurs d'eau partage
		 * par toute la zone ne soit JAMAIS nul. Le plancher garantit qu'une
		 * carte plate en produise un ; le plafond evite qu'un abysse ne l'etale
		 * sur des kilometres et n'y noie la precision, comme l'a fait une
		 * premiere version qui y avait mis la largeur de la carte.
		 */
		constexpr float MinOceanDepthCm = 5000.0f;    // cinquante metres
		constexpr float MaxOceanDepthCm = 50000.0f;   // cinq cents metres

		/**
		 * Cote de l'ile centrale de l'ocean, en centimetres.
		 *
		 * L'OCEAN DU PLUGIN SE DEFINIT PAR UN TROU. Sa spline decrit une ile
		 * centrale, et l'eau remplit tout le reste de son etendue autour. Sans
		 * spline, GenerateWaterBodyMesh sort sur un `return false` muet des que
		 * le contour compte moins de trois segments : pas d'erreur, pas
		 * d'avertissement, pas d'ocean.
		 *
		 * Ce monde n'a pas d'ile a exclure — le relief emerge simplement au
		 * travers de la nappe. On donne donc le plus petit contour valable, deux
		 * metres de cote : invisible a l'echelle d'une carte de plusieurs
		 * kilometres, et suffisant pour que le maillage se genere.
		 */
		constexpr float OceanIslandSideCm = 200.0f;

		/**
		 * Active la fenetre glissante de la zone d'eau, par reflexion.
		 *
		 * Ces deux reglages sont PRIVES et sans accesseur : le plugin les
		 * destine a l'editeur. La reflexion degrade proprement — si le plugin
		 * les renomme, on renvoie faux et la zone reste en mode global, ce qui
		 * marche, simplement moins finement.
		 */
		bool EnableLocalWindow(AWaterZone* Zone, const FVector& ExtentCm)
		{
			if (!Zone)
			{
				return false;
			}

			UClass* Class = Zone->GetClass();

			FBoolProperty* Enable = CastField<FBoolProperty>(
				Class->FindPropertyByName(TEXT("bEnableLocalOnlyTessellation")));
			FStructProperty* Extent = CastField<FStructProperty>(
				Class->FindPropertyByName(TEXT("LocalTessellationExtent")));

			if (!Enable || !Extent || Extent->Struct != TBaseStructure<FVector>::Get())
			{
				return false;
			}

			// L'ETENDUE AVANT L'INTERRUPTEUR : le plugin lit la premiere quand on
			// bascule le second, et une fenetre de taille nulle ne dessinerait
			// rien du tout.
			*Extent->ContainerPtrToValuePtr<FVector>(Zone) = ExtentCm;
			Enable->SetPropertyValue_InContainer(Zone, true);
			return true;
		}

		/**
		 * Rend tous les composants d'un acteur deplacables.
		 *
		 * LES COMPOSANTS D'UN CORPS D'EAU NAISSENT EN MOBILITE STATIQUE. C'est
		 * juste pour de l'eau posee a la main dans l'editeur, qui ne bouge plus
		 * ensuite. Mais poser une spline DEPLACE le maillage et la collision :
		 * Unreal refuse alors le mouvement — "LakeMeshComponent doit etre
		 * Movable si vous souhaitez move" — et la nappe reste la ou elle n'est
		 * pas. L'acteur existe, la forme est juste, et l'ecran reste vide.
		 *
		 * A POSER AVANT L'ENREGISTREMENT, par un spawn differe. Changer la
		 * mobilite d'un composant deja enregistre n'a pas toujours l'effet
		 * voulu, et surtout : AreDynamicDataChangesAllowed() teste
		 * `IsRegistered() && Mobility == Static`. Or c'est CE test qui decide
		 * si le plugin construit le static mesh du corps d'eau une fois la
		 * partie commencee — et sans ce maillage, RebuildWaterMesh saute
		 * purement et simplement le corps.
		 *
		 * A rappeler aussi APRES la mise a jour de forme : le plugin cree
		 * certains de ses composants en chemin, et ceux-la naissent statiques.
		 */
		void MakeMovable(AActor* Actor)
		{
			if (!Actor)
			{
				return;
			}

			TInlineComponentArray<USceneComponent*> Components;
			Actor->GetComponents(Components);
			for (USceneComponent* Component : Components)
			{
				if (Component && Component->Mobility != EComponentMobility::Movable)
				{
					Component->SetMobility(EComponentMobility::Movable);
				}
			}
		}

		/**
		 * Materiaux du plugin, poses explicitement.
		 *
		 * UN CORPS D'EAU NE DE C++ N'EN A AUCUN. Les valeurs par defaut vivent
		 * dans les Blueprints du plugin, que SpawnActor sur la classe native ne
		 * traverse pas : l'acteur apparait dans l'organiseur, occupe sa place,
		 * et ne dessine rien. C'est exactement ce qu'on a observe.
		 */
		UMaterialInterface* Load(const TCHAR* Path)
		{
			return LoadObject<UMaterialInterface>(nullptr, Path);
		}

		void ApplyMaterials(UWaterBodyComponent* Component, const TCHAR* Surface,
			const TCHAR* StaticMesh)
		{
			if (!Component)
			{
				return;
			}

			if (UMaterialInterface* M = Load(Surface))
			{
				Component->SetWaterMaterial(M);
			}
			if (UMaterialInterface* M = Load(StaticMesh))
			{
				Component->SetWaterStaticMeshMaterial(M);
			}
			if (UMaterialInterface* M = Load(
				TEXT("/Water/Materials/WaterInfo/DrawWaterInfo.DrawWaterInfo")))
			{
				Component->SetWaterInfoMaterial(M);
			}

			// LE VOILE SOUS-MARIN N'ARRIVE PAS TOUT SEUL NON PLUS.
			//
			// Le plugin sait pourtant tres bien qu'on est dessous : il balaie
			// une sphere vers le haut sur le canal de trace de l'eau, trouve le
			// volume de collision du corps, et fabrique a la volee un volume de
			// post-traitement. Mais ce volume ne melange QUE le materiau porte
			// par UnderwaterPostProcessMaterial — et ce champ vaut nul.
			//
			// Le defaut existe, FWaterBodyDefaults le declare ; il vit dans le
			// module EDITEUR du plugin, applique par les outils de pose. Un
			// acteur ne de SpawnActor ne le traverse jamais. Meme oubli que la
			// surface et la texture d'information, en plus silencieux encore :
			// ici rien ne manque a l'ecran, il ne se passe simplement rien.
			if (UMaterialInterface* M = Load(TEXT("/Water/Materials/PostProcessing/")
				TEXT("M_UnderWater_PostProcess_Volume.M_UnderWater_PostProcess_Volume")))
			{
				Component->SetUnderwaterPostProcessMaterial(M);
			}
		}

		/**
		 * Applique une forme a un corps d'eau.
		 *
		 * UpdateAll avec bShapeOrPositionChanged est ce qui declenche le
		 * recalcul du maillage et de la texture d'information. Sans lui, la
		 * spline change et rien ne bouge a l'ecran.
		 */
		void PushShape(UWaterBodyComponent* Component)
		{
			if (!Component)
			{
				return;
			}

			FOnWaterBodyChangedParams Params;
			Params.bShapeOrPositionChanged = true;

			// bUserTriggered N'EST PAS DECORATIF : c'est lui, et lui seul, qui
			// declenche UpdateWaterZones() — la resolution de la zone
			// proprietaire et l'appel a AddWaterBodyComponent. Sans lui, le
			// plugin se fie au pointeur de zone SERIALISE, que possede une eau
			// posee dans l'editeur mais qu'un acteur ne au runtime n'a pas. Le
			// corps ne se rattachait donc a aucune zone, et le rebuild qui suit
			// n'avait personne a prevenir : tout existait, rien ne s'affichait.
			Params.bUserTriggered = true;

			Component->UpdateAll(Params);

#if WITH_EDITOR
			// LE MAILLAGE SE CONSTRUIT ICI, ET PAS AVANT.
			//
			// SetOceanExtent le demande deja — mais je l'appelle avant d'avoir
			// pose la spline, donc sur un contour vide : GenerateWaterBodyMesh
			// sort sur son `return false`, et plus rien ne le redemande. Le
			// corps se retrouve avec ses composants de maillage en place et
			// aucune geometrie dedans, ce que RebuildWaterMesh traite en le
			// sautant sans un mot.
			//
			// L'appel est reserve a l'editeur cote plugin : un monde empaquete
			// devra s'appuyer sur IsBodyDynamic(), note ailleurs.
			Component->UpdateWaterBodyRenderData();
#endif
		}
	}

	float ProfondeurDOceanCm(float SeabedM, float ExagerationZ)
	{
		return FMath::Clamp(-SeabedM * WorldseedMetersToCm * ExagerationZ,
			MinOceanDepthCm, MaxOceanDepthCm);
	}

	float FenetreMaximaleKm(int32 PlafondDeTuiles)
	{
		// `RoundUpToPowerOfTwo(demi-etendue / 24 m)` plafonne a ce nombre de
		// tuiles : la demi-etendue vaut donc au plus `plafond x 24 m`, et le
		// COTE le double. 256 tuiles rendent 12,288 km, 512 en rendent 24,576.
		return 2.0f * static_cast<float>(FMath::Max(PlafondDeTuiles, 1))
			* 24.0f / 1000.0f;
	}

	bool Build(UWorld* World, const FWorldseedGeometry& Geometry,
		float HeightExaggeration, float SeabedM, FWorldseedWaterBodies& Out)
	{
		Clear(Out);

		if (!World || Geometry.NX < 2)
		{
			return false;
		}

		const double StartTime = FPlatformTime::Seconds();

		const float WidthCm = Geometry.WidthM() * WorldseedMetersToCm;
		const float HeightCm = Geometry.HeightM * WorldseedMetersToCm;

		// --- la zone d'eau --------------------------------------------------
		// Elle porte le quadtree qui dessine toutes les nappes. Son etendue
		// couvre le monde : c'est son propre niveau de detail qui rend cela
		// abordable, pas une limitation de taille.
		Out.Zone = World->SpawnActorDeferred<AWaterZone>(
			AWaterZone::StaticClass(), FTransform::Identity, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Out.Zone.IsValid())
		{
			MakeMovable(Out.Zone.Get());
			Out.Zone->FinishSpawning(FTransform::Identity);
		}
		if (!Out.Zone.IsValid())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Worldseed] AWaterZone non cree : l'eau reste en maillage"));
			return false;
		}
		Out.Zone->SetZoneExtent(FVector2D(WidthCm, HeightCm));

		// --- LA FINESSE DU RIVAGE, ET ELLE NE SE REGLE QU'ICI ----------------
		//
		// La `water info texture` encode la hauteur du sol et celle de la
		// surface pour TOUTE l'eau du niveau. C'est elle qui dessine le trait
		// de cote : a texel grossier, le rivage devient un escalier et l'eau
		// monte en plaques sur les falaises (defaut deja documente au
		// registre).
		//
		// SON DEFAUT EST 512 x 512 (WaterZoneActor.cpp:96) ET NOTRE CODE NE LA
		// REGLAIT PAS. Le registre annoncait 4096 : cette valeur appartenait a
		// la zone posee dans l'editeur, qui n'existe plus -- meme histoire que
		// la nappe lointaine. Une `WaterZone` ne se spawne pas, le plugin la
		// recree a chaque partie, donc TOUT reglage pose sur elle doit l'etre
		// PAR CODE.
		//
		// Le moteur ne la plafonne pas : `r.Water.WaterInfo.RenderTargetResolutionMax`
		// vaut 0, c'est-a-dire illimite (WaterZoneActor.cpp:47). Le format est
		// RGBA16f, huit octets par texel, une tranche en solo :
		//
		//      512^2    2 Mo    24,0 m par texel sur la fenetre de 12,288 km
		//     2048^2   34 Mo     6,0 m
		//     4096^2  134 Mo     3,0 m   <- retenu
		//     8192^2  537 Mo     1,5 m
		//
		// Le vrai cout n'est pas la memoire mais le REDESSIN : la texture se
		// re-rend entierement a chaque deplacement de la fenetre glissante.
		const int32 Texels = 4096;
		Out.Zone->SetRenderTargetResolution(FIntPoint(Texels, Texels));

		// --- LA NAPPE LOINTAINE, SANS QUOI L'EAU S'ARRETE NET ----------------
		//
		// SIGNALE EN JEU : « l'eau se coupait ». La surface s'arrete sur un
		// trait DROIT en travers d'une baie -- au CENTRE du monde, longitude
		// 12,7 E, ce qui ecarte la couture de la carte. Balayage de sept
		// rivages etales de -178,9 a +178,8 degres : la coupure n'est visible
		// qu'au seul point ou le joueur est EN HAUTEUR (324 m contre 5 a 76 m
		// pour les six autres). LA VARIABLE N'EST DONC PAS LA LONGITUDE, C'EST
		// L'ALTITUDE DU POINT DE VUE -- de profil, le bord tombe sous
		// l'horizon ; d'en haut, on le regarde.
		//
		// LA CAUSE, LUE ET NON SUPPOSEE : « relais lointain 0.0 km, materiau
		// ABSENT ». Le maillage glissant ne couvre que 6,1 km -- 256 tuiles de
		// 24 m, plafond du moteur -- et AU-DELA IL N'Y A RIEN. Pas une nappe
		// degradee : le neant.
		//
		// POURQUOI CE REGLAGE AVAIT DISPARU. Le registre du projet affirmait
		// que le composant portait « 40 km avec le materiau Water_FarMesh ».
		// Il portait zero. La `WaterZone` ne se spawne pas : le plugin la cree
		// lui-meme a chaque partie et on l'ADOPTE -- donc tout reglage pose sur
		// elle dans l'editeur meurt avec elle. C'est la regle que ce depot a
		// deja ecrite pour le PlayerStart et les acteurs d'eclairage. On le
		// pose donc PAR CODE, a cote de l'etendue et de la fenetre glissante,
		// qui sont la pour exactement la meme raison.
		if (UWaterMeshComponent* const Maillage = Out.Zone->GetWaterMeshComponent())
		{
			// Le materiau est celui du plugin, et il DOIT porter son drapeau
			// « utilise avec l'eau » : `IsMaterialUsedWithWater` le verifie, et
			// un materiau qui echoue est silencieusement remis a nul
			// (WaterMeshComponent.cpp:283). On journalise donc ce qui est pose.
			UMaterialInterface* const Lointain = LoadObject<UMaterialInterface>(
				nullptr, TEXT("/Water/Materials/WaterSurface/Water_FarMesh.Water_FarMesh"));

			// Quarante kilometres : de quoi couvrir la diagonale du monde
			// depuis n'importe quel point, donc un horizon sans trou.
			constexpr float PorteeCm = 4000000.0f;

			Maillage->FarDistanceMaterial = Lointain;
			Maillage->FarDistanceMeshExtent = PorteeCm;

			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] eau : nappe lointaine posee -- %.0f km, materiau %s"),
				PorteeCm / 100000.0f,
				Lointain ? *Lointain->GetName() : TEXT("INTROUVABLE"));
		}

		// --- fenetre glissante ------------------------------------------------
		// UNE SEULE TEXTURE ETIREE SUR SEIZE KILOMETRES NE PEUT PAS DECRIRE UN
		// RIVAGE. A mille pixels de cote, chaque texel couvre seize metres : la
		// cote y devient un escalier, et aucune finesse de terrain n'y change
		// rien.
		//
		// En mode local, cette texture ne decrit qu'une fenetre autour de la
		// camera — meme nombre de pixels, sur quatre kilometres au lieu de
		// seize, donc quatre fois plus fin — et c'est LE MOTEUR qui la
		// regenere quand la fenetre glisse. On n'a donc plus a la redemander
		// nous-memes, ce qui faisait disparaitre l'eau le temps du redessin.
		// LA TAILLE N'EST PAS RONDE, ELLE EST DEDUITE DU PLAFOND DE TUILES.
		// `UWaterMeshComponent::GetExtentInTiles` (WaterMeshComponent.cpp:233)
		// fait `RoundUpToPowerOfTwo(demi-etendue / 24 m)`, et
		// `r.Water.WaterMesh.MaxDimensionInTiles` plafonne le resultat a 256
		// (WaterMeshComponent.cpp:38). Au-dela, le moteur DIVISE la taille de
		// tuile -- tuiles de 48 m, rivage deux fois plus grossier, ce qui
		// annule le benefice d'agrandir.
		//
		//     4,000 km -> 83,3  -> 128 tuiles       (l'ancienne valeur)
		//     8,000 km -> 166,7 -> 256 tuiles
		//    12,288 km -> 256,0 -> 256 tuiles       <- le maximum
		//    16,000 km -> 333,3 -> 512, donc biaise
		//
		// A 12,288 km = 2 x 256 x 24 m, la boite du quadtree (+/- 6144 m)
		// coincide PILE avec la fenetre d'information : aucun arrondi perdu.
		//
		// ATTENTION AU NOM DE LA VARIABLE DE CONSOLE : le message
		// d'avertissement du moteur (WaterMeshComponent.cpp:605) cite
		// `r.Water.WaterMesh.MaxWidthInTiles`, QUI N'EXISTE PAS. La vraie est
		// `MaxDimensionInTiles`. Ce depot avait recopie le mauvais nom.
		//
		// LA SURCHARGE EXISTE PARCE QU'UN A/B NE S'EDITE PAS. Comparer deux
		// tailles en rouvrant un fichier -- regles ou source -- impose soit de
		// regenerer le monde, soit de recompiler entre les deux moities : ce
		// n'est alors plus le meme essai. Ce depot a paye cette regle en vidant
		// `world_rules.json` un soir. `-WorldseedEauFenetre=<km>` compare sur
		// le MEME binaire et le MEME monde, et sert ensuite a regler sans
		// recompiler.
		// --- LE PLAFOND DE TUILES SE LEVE ICI, PAS DANS UN INI ---------------
		//
		// `r.Water.WaterMesh.MaxDimensionInTiles` vaut 256 par defaut, et c'est
		// LUI qui bornait la fenetre a 12,288 km. Le porter a 512 double la
		// portee de l'eau detaillee -- 24,576 km, bord a 12,3 km au lieu de
		// 6,1.
		//
		// LES DEUX VALEURS NE PEUVENT PAS VIVRE SEPAREMENT. Une fenetre plus
		// large qu'un plafond inchange ne rend pas une erreur : le moteur
		// DIVISE la taille de tuile jusqu'a rentrer (WaterMeshComponent.cpp:601)
		// et le rivage devient deux fois plus grossier, en silence. Les poser
		// cote a cote, dans le meme fichier et a la meme ligne de raisonnement,
		// est la seule facon d'empecher qu'elles derivent l'une de l'autre --
		// et c'est exactement ce que ce depot a paye en posant jadis
		// `MaxWidthInTiles` dans un ini, nom qui n'existe pas et ligne qui
		// n'etait pas la.
		//
		// CE QUE CELA COUTE, ET IL FAUT LE DIRE : quatre fois les tuiles du
		// quadtree. La documentation d'Epic previent -- « Having too many tiles
		// can create very large GPU allocations ». Et la texture d'info, a
		// nombre de texels constant, s'etale sur deux fois la distance : 3,0 ->
		// 6,0 m par texel. C'est le rivage qu'on echange contre l'horizon.
		if (IConsoleVariable* const Plafond = IConsoleManager::Get()
				.FindConsoleVariable(TEXT("r.Water.WaterMesh.MaxDimensionInTiles")))
		{
			if (Plafond->GetInt() < 512)
			{
				Plafond->Set(512, ECVF_SetByCode);
				UE_LOG(LogTemp, Log,
					TEXT("[Worldseed] eau : plafond de tuiles porte a 512 ")
					TEXT("(defaut 256) -- sans quoi la fenetre serait ramenee a 12,288 km"));
			}
		}

		const float FenetreKm = FenetreMaximaleKm(512);
		const float LocalWindowCm = FenetreKm * 100000.0f;

		const bool bLocalWindow = (WidthCm > LocalWindowCm)
			&& EnableLocalWindow(Out.Zone.Get(),
				FVector(LocalWindowCm, LocalWindowCm, HeightCm));

		// --- l'ocean --------------------------------------------------------
		// L'ALTITUDE ZERO EST LE NIVEAU DE LA MER par construction : toute la
		// chaine de generation cale son quantile dessus.
		Out.Ocean = World->SpawnActorDeferred<AWaterBodyOcean>(
			AWaterBodyOcean::StaticClass(), FTransform::Identity, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

		if (Out.Ocean.IsValid())
		{
			// AVANT l'enregistrement : c'est la condition de
			// AreDynamicDataChangesAllowed, donc de la construction du maillage.
			MakeMovable(Out.Ocean.Get());
			Out.Ocean->FinishSpawning(FTransform::Identity);
			MakeMovable(Out.Ocean.Get());

			if (UWaterBodyOceanComponent* Component =
				Cast<UWaterBodyOceanComponent>(Out.Ocean->GetWaterBodyComponent()))
			{
				// UNE SEULE PROFONDEUR, CELLE DU FOND MARIN.
				//
				// Le Z de l'etendue de collision prenait HeightCm — la
				// dimension HORIZONTALE de la carte, soit quatre kilometres de
				// boite sous la mer. Sans effet visible : la boite n'est qu'un
				// filtre grossier, et c'est une requete precise sur la hauteur
				// du plan d'eau qui decide ensuite. Mais une grandeur
				// horizontale employee comme profondeur est exactement ce qui,
				// mis dans ChannelDepth, a fait disparaitre toute l'eau — on ne
				// laisse pas traîner le meme piege deux fois.
				const float OceanDepthCm =
					ProfondeurDOceanCm(SeabedM, HeightExaggeration);

				Component->SetOceanExtent(FVector2D(WidthCm, HeightCm));

				// L'etendue est une DEMI-hauteur : la boite descend du double
				// sous la surface, ce qui laisse de la marge sous le fond.
				Component->SetCollisionExtents(
					FVector(WidthCm * 0.5f, HeightCm * 0.5f, OceanDepthCm));

				// L'EPAISSEUR DE L'OCEAN, ET POURQUOI IL LUI EN FAUT UNE.
				//
				// Sa collision ne vient pas de la : elle sort de
				// CollisionExtents, pose juste au-dessus. ChannelDepth ne
				// pilote que CalcBounds — mais la zone d'eau agrege les bornes
				// de TOUS les corps en un seul intervalle, dans lequel la
				// texture d'information normalise chaque hauteur :
				//
				//     WaterZ = NormalizedWaterZ * (ZMax - ZMin) + ZMin
				//
				// Deux ecueils s'y font face. Trop large — une premiere version
				// y avait mis la largeur de la carte — et le pas de
				// quantification s'etale sur des kilometres : la hauteur relue
				// ne designe plus aucune surface, et l'eau disparait. NUL, et
				// c'est pire encore : sur une carte sans lac l'ocean est le seul
				// corps d'eau, l'intervalle se reduit a [0, 0], et tous les
				// seuils du shader tombent exactement sur leur borne. Le plugin
				// se protege de la division par zero, pas de cela.
				//
				// Le fond marin donne la bonne mesure : assez epais pour que
				// l'intervalle vive, du meme ordre que le relief.
				Component->CurveSettings.ChannelDepth = OceanDepthCm;

				// L'ile centrale, sans laquelle rien ne se genere.
				if (UWaterSplineComponent* Spline = Component->GetWaterSpline())
				{
					const float H = OceanIslandSideCm * 0.5f;
					const TArray<FVector> Island = {
						FVector(-H, -H, 0.0f), FVector(H, -H, 0.0f),
						FVector(H,  H, 0.0f),  FVector(-H, H, 0.0f),
					};

					Spline->SetSplinePoints(Island, ESplineCoordinateSpace::Local, true);
					Spline->SetClosedLoop(true, true);
					for (int32 I = 0; I < Spline->GetNumberOfSplinePoints(); ++I)
					{
						Spline->SetSplinePointType(I, ESplinePointType::Linear, false);
					}
					Spline->UpdateSpline();
				}

				ApplyMaterials(Component,
					TEXT("/Water/Materials/WaterSurface/Water_Material_Ocean.Water_Material_Ocean"),
					TEXT("/Water/Materials/WaterSurface/LODs/Water_Material_Ocean_LOD.Water_Material_Ocean_LOD"));

				PushShape(Component);

				// Le plugin a pu creer maillage et collision pendant la mise a
				// jour : ils naissent statiques a leur tour.
				MakeMovable(Out.Ocean.Get());
			}
		}
		// LA ZONE NE SE RECONSTRUIT PAS TOUTE SEULE.
		//
		// Elle porte le quadtree qui dessine l'eau et la texture d'information
		// qui dit ou elle se trouve ; ni l'un ni l'autre ne connait les corps
		// nes apres elle. Sans cet appel les acteurs existent, la zone existe,
		// et l'ecran reste vide — ce qui ne ressemble a aucune erreur.
		Out.Zone->MarkForRebuild(EWaterZoneRebuildFlags::All, Out.Zone.Get());
		Out.Zone->Update();

		// Chaque corps a-t-il trouve sa zone, et sa forme tient-elle ? Sans ces
		// deux-la, l'acteur existe et l'ecran reste vide.
		auto Report = [](const TCHAR* Label, AActor* Actor, UWaterBodyComponent* Component)
		{
			if (!Component)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Worldseed]   %s : pas de composant"), Label);
				return;
			}

			const UWaterSplineComponent* Spline = Component->GetWaterSpline();

			// ON NOMME LES MAILLAGES AU LIEU DE LES COMPTER.
			//
			// "1/3" ne dit pas LEQUEL manque, et ils ne jouent pas le meme
			// role : l'un porte la surface visible, l'autre inscrit le corps
			// dans la texture d'information, le troisieme l'y inscrit DILATE —
			// c'est ce dernier qui permet a l'eau de deborder du lit jusqu'a
			// la berge. Un compte les confond ; un nom les separe.
			FString Maillages;
			if (Actor)
			{
				TInlineComponentArray<UStaticMeshComponent*> Meshes;
				Actor->GetComponents(Meshes);
				int32 Anonymes = 0;
				for (const UStaticMeshComponent* Mesh : Meshes)
				{
					if (!Mesh)
					{
						continue;
					}
					const FString Nom = Mesh->GetName();
					if (!Nom.Contains(TEXT("Info")))
					{
						Anonymes += Mesh->GetStaticMesh() ? 1 : 0;
						continue;
					}
					Maillages += FString::Printf(TEXT("%s=%s "), *Nom,
						Mesh->GetStaticMesh() ? TEXT("garni") : TEXT("VIDE"));
				}
				Maillages += FString::Printf(TEXT("surface=%d"), Anonymes);
			}

			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed]   %s : zone=%s  segments=%d  mobilite=%s  dilatation=%.0f m  |  %s"),
				Label,
				Component->GetWaterZone() ? TEXT("oui") : TEXT("AUCUNE"),
				Spline ? Spline->GetNumberOfSplineSegments() : -1,
				Component->Mobility == EComponentMobility::Movable
					? TEXT("Movable") : TEXT("STATIC"),
				Component->ShapeDilation / WorldseedMetersToCm, *Maillages);
		};

		if (Out.Ocean.IsValid())
		{
			Report(TEXT("ocean"), Out.Ocean.Get(), Out.Ocean->GetWaterBodyComponent());
		}

		// LA TAILLE SE LIT DANS LA CONSTANTE, ELLE NE SE RECOPIE PAS. Cette
		// ligne annoncait « glissante 4 km » en dur : elle aurait menti des le
		// premier agrandissement, et ce depot a deja paye deux fois un journal
		// qui affirme une valeur que le code ne pose plus.
		const FString Fenetre = bLocalWindow
			? FString::Printf(TEXT("glissante %.3f km"), LocalWindowCm / 100000.0f)
			: FString(TEXT("globale"));

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] plugin Water : zone %.1f x %.1f km, fenetre=%s, ocean=%d  (%.0f ms)"),
			Geometry.WidthM() / 1000.0f, Geometry.HeightM / 1000.0f,
			*Fenetre,
			Out.Ocean.IsValid() ? 1 : 0,
			(FPlatformTime::Seconds() - StartTime) * 1000.0);

		return true;
	}

	void NotifyGroundChanged(FWorldseedWaterBodies& Bodies)
	{
		if (AWaterZone* Zone = Bodies.Zone.Get())
		{
			Zone->MarkForRebuild(EWaterZoneRebuildFlags::UpdateWaterInfoTexture, Zone);
		}
	}

	void LogHealth(UWorld* World, const FWorldseedWaterBodies& Bodies)
	{
		AWaterZone* const Zone = Bodies.Zone.Get();
		if (!World || !Zone)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Worldseed] eau : aucune zone a relever"));
			return;
		}

		const FBox2D ZoneBounds = Zone->GetZoneBounds2D();
		const FVector2f Heights = Zone->GetWaterHeightExtents();
		const FVector InfoExtent = Zone->GetDynamicWaterInfoExtent();

		// LE SOL TEL QUE LA ZONE LE VOIT, et non tel qu'on croit l'avoir pose.
		int32 Sols = 0;
		int32 Primitives = 0;
		FBox2D SolBounds(ForceInit);
		bool bIntersecte = false;
		if (UWaterSubsystem* Sub = UWaterSubsystem::GetWaterSubsystem(World))
		{
			TArray<UWaterTerrainComponent*> Terrains;
			Sub->GetWaterTerrainComponents(Terrains);
			for (UWaterTerrainComponent* Terrain : Terrains)
			{
				if (!Terrain)
				{
					continue;
				}
				++Sols;
				Primitives += Terrain->GetTerrainPrimitives().Num();
				SolBounds += Terrain->GetTerrainBounds();
				bIntersecte |= Terrain->AffectsWaterZone(Zone);
			}
		}

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] eau : zone %.1f x %.1f km, fenetre %.1f x %.1f km%s  |  ")
			TEXT("hauteurs d'eau [%.0f .. %.0f] m, sol a partir de %.0f m"),
			ZoneBounds.GetSize().X / 100000.0f, ZoneBounds.GetSize().Y / 100000.0f,
			InfoExtent.X / 100000.0f, InfoExtent.Y / 100000.0f,
			Zone->IsLocalOnlyTessellationEnabled() ? TEXT(" (glissante)") : TEXT(""),
			Heights.X / 100.0f, Heights.Y / 100.0f, Zone->GetGroundZMin() / 100.0f);

		// LA FINESSE DU RIVAGE EST UN QUOTIENT, PAS UNE RESOLUTION. Deux mondes
		// a 4096 texels n'ont pas le meme rivage si leur fenetre differe. On
		// journalise donc les metres par texel, qui est la grandeur qui compte,
		// a cote des deux nombres dont elle sort -- sans quoi le registre
		// recommence a affirmer une valeur que personne ne relit.
		//
		// ET LE GETTER DU PLUGIN N'EST PAS EXPORTE. `SetRenderTargetResolution`
		// porte `UE_API`, `GetRenderTargetResolution` NON -- deux lignes de
		// suite dans `WaterZoneActor.h`, 66 et 67. On peut donc ECRIRE la
		// resolution depuis notre module et pas la RELIRE : l'edition de liens
		// tombe sur un symbole non resolu, et l'erreur n'arrive qu'au LIEN,
		// bien apres une compilation reussie. On relit par reflexion, comme la
		// fenetre glissante -- ce qui a de toute facon la bonne vertu : c'est
		// la valeur REELLEMENT portee par l'acteur, pas celle qu'on a demandee.
		{
			FIntPoint Res(0, 0);
			if (const FStructProperty* const Prop = CastField<FStructProperty>(
					Zone->GetClass()->FindPropertyByName(TEXT("RenderTargetResolution"))))
			{
				if (Prop->Struct && Prop->Struct->GetFName() == TEXT("IntPoint"))
				{
					Res = *Prop->ContainerPtrToValuePtr<FIntPoint>(Zone);
				}
			}
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] eau : texture d'info %dx%d, soit %.1f x %.1f m par texel"),
				Res.X, Res.Y,
				Res.X > 0 ? (InfoExtent.X / 100.0f) / Res.X : 0.0f,
				Res.Y > 0 ? (InfoExtent.Y / 100.0f) / Res.Y : 0.0f);
		}

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] eau : %d sol(s), %d primitives sur %.1f x %.1f km, intersecte=%s"),
			Sols, Primitives,
			SolBounds.GetSize().X / 100000.0f, SolBounds.GetSize().Y / 100000.0f,
			bIntersecte ? TEXT("oui") : TEXT("NON"));

		// LA RESOLUTION DU QUADTREE EST ARRONDIE A LA PUISSANCE DE DEUX
		// SUPERIEURE, a partir de la DEMI-etendue divisee par la taille de
		// tuile. Une petite carte peut donc se retrouver avec un quadtree qui
		// deborde largement d'elle — ou, si la division tombe sous un, avec un
		// quadtree d'une seule tuile.
		if (const UWaterMeshComponent* Mesh = Zone->GetWaterMeshComponent())
		{
			const FIntPoint HalfTiles = Mesh->GetExtentInTiles();
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] eau : maillage %dx%d tuiles de %.0f m, soit %.1f x %.1f km"),
				HalfTiles.X * 2, HalfTiles.Y * 2, Mesh->GetTileSize() / 100.0f,
				HalfTiles.X * 2 * Mesh->GetTileSize() / 100000.0f,
				HalfTiles.Y * 2 * Mesh->GetTileSize() / 100000.0f);

			// --- LE RELAIS LOINTAIN, ET S'IL EST ARME -----------------------
			//
			// SIGNALE EN JEU : « l'eau se coupait ». La surface s'arrete sur un
			// trait DROIT en travers d'une baie, au CENTRE du monde -- longitude
			// 12,7 E -- ce qui ecarte la couture de la carte. La distance colle
			// a la demi-largeur du maillage glissant ci-dessus : 6,1 km de cote,
			// donc un bord a trois kilometres.
			//
			// AU-DELA, C'EST LE MAILLAGE LOINTAIN QUI DOIT PRENDRE LE RELAIS, et
			// c'est tout l'objet de ces deux valeurs. Une portee nulle ou un
			// materiau absent expliquent une coupure nette ; les deux renseignes
			// deplacent le soupcon vers la transition elle-meme. Sans les lire,
			// on ne peut que supposer -- et ce depot a une regle contre cela.
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] eau : relais lointain %.1f km, materiau %s"),
				Mesh->FarDistanceMeshExtent / 100000.0f,
				Mesh->FarDistanceMaterial
					? *Mesh->FarDistanceMaterial->GetName()
					: TEXT("ABSENT"));
		}

		if (const UWaterBodyComponent* Ocean = Bodies.Ocean.IsValid()
			? Bodies.Ocean->GetWaterBodyComponent() : nullptr)
		{
			const FBox B = Ocean->Bounds.GetBox();
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] eau : ocean zone=%s  bornes %.1f x %.1f km, z [%.0f .. %.0f] m"),
				Ocean->GetWaterZone() ? TEXT("oui") : TEXT("AUCUNE"),
				B.GetSize().X / 100000.0f, B.GetSize().Y / 100000.0f,
				B.Min.Z / 100.0f, B.Max.Z / 100.0f);
		}
	}

	void Clear(FWorldseedWaterBodies& Bodies)
	{
		if (Bodies.Ocean.IsValid())
		{
			Bodies.Ocean->Destroy();
		}
		if (Bodies.Zone.IsValid())
		{
			Bodies.Zone->Destroy();
		}

		Bodies.Ocean = nullptr;
		Bodies.Zone = nullptr;
	}
}
