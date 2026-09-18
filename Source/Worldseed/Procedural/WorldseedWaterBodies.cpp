// Worldseed - ocean et lacs confies au plugin Water d'Unreal.

#include "Procedural/WorldseedWaterBodies.h"

#include "Procedural/WorldseedPolyline.h"
#include "Procedural/WorldseedRiverReaches.h"
#include "Procedural/WorldseedRivers.h"

#include "Algo/Reverse.h"

#include "Components/SceneComponent.h"
#include "UObject/UnrealType.h"
#include "Components/SplineComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "WaterBodyComponent.h"
#include "WaterBodyLakeActor.h"
#include "WaterBodyOceanActor.h"
#include "WaterBodyOceanComponent.h"
#include "WaterBodyRiverActor.h"
#include "WaterBodyRiverComponent.h"
#include "WaterSplineMetadata.h"
#include "WaterSplineComponent.h"
#include "WaterZoneActor.h"
#include "WaterMeshComponent.h"
#include "WaterSubsystem.h"
#include "WaterTerrainComponent.h"

namespace WorldseedWaterBodies
{
	namespace
	{
		/** Le monde raisonne en metres, la scene en centimetres. */
		constexpr float MetersToCm = 100.0f;

		/**
		 * Points conserves par rivage : un budget PROPORTIONNEL, pas fixe.
		 *
		 * Une spline n'est pas une polyligne, elle interpole — mais le nombre
		 * de points qu'il lui faut depend de la LONGUEUR du rivage, pas d'une
		 * constante. Trente-deux suffisent a une mare ronde de trois cents
		 * metres de tour ; sur un ruban sinueux de sept kilometres, cela fait
		 * un point tous les deux cent trente metres, et la reduction ecrase les
		 * deux berges l'une sur l'autre. Le polygone garde son perimetre et
		 * perd son aire : il se triangule encore, mais ne se DILATE plus, et
		 * l'eau s'arrete net au bord de la nappe.
		 *
		 * Ce cas n'a rien d'exotique — une vallee comblee par le routage est
		 * exactement cela : longue, etroite, et classee comme nappe.
		 */
		constexpr float MetresPerShorePoint = 60.0f;
		constexpr int32 MinLakeSplinePoints = 8;
		constexpr int32 MaxLakeSplinePoints = 192;

		/**
		 * Ecart minimal entre deux points de rivage, en cellules.
		 *
		 * Un segment de longueur nulle n'a pas de direction : l'offset qui
		 * dilate la nappe ne saurait pas de quel cote le pousser.
		 */
		constexpr float MinShoreSpacingCells = 0.25f;

		/**
		 * Marge de l'enveloppe vers l'exterieur, en cellules.
		 *
		 * Elle doit couvrir l'ecart entre le relief de simulation, sur lequel
		 * le niveau du lac est calcule, et le relief AFFICHE, qui recoit son
		 * detail fractal apres l'hydrologie. Mesure : jusqu'a 6,7 m d'ecart
		 * vertical, soit quelques cellules au sol sur une berge douce.
		 *
		 * Trop serree, le mur d'eau revient. Trop large, une depression voisine
		 * plus basse que la nappe entre dans l'enveloppe et se remplit a tort.
		 */
		constexpr float ShoreMarginCells = 3.0f;

		/**
		 * Distance a laquelle deux aretes non voisines se genent, en cellules.
		 *
		 * TRES PETIT, ET C'EST TOUT L'ENJEU. Sur une nappe etroite les deux
		 * berges sont proches par nature : c'est sa forme. Un seuil genereux —
		 * un quart de cellule a suffi — les prend pour un defaut, recoud le lac
		 * en travers et le fait disparaitre. Une nappe perdue coute plus cher
		 * qu'un rivage un peu raide.
		 */
		constexpr float ShoreTouchEpsilonCells = 0.02f;

		/**
		 * Epaisseur plancher d'une nappe, en centimetres.
		 *
		 * L'hydrologie retient des lacs d'a peine quelques decimetres. Leur
		 * volume de collision serait alors si mince que la sphere de detection
		 * du plugin le traverserait entre deux images : le voile sous-marin
		 * clignoterait au lieu de s'installer. Deux metres coutent moins cher
		 * qu'un clignotement.
		 */
		constexpr float MinLakeDepthCm = 200.0f;

		/**
		 * Epaisseur plafond d'une nappe, en centimetres.
		 *
		 * Cent metres : aucune plongee de joueur n'ira l'eprouver, et cela
		 * garde le plancher de l'intervalle de hauteurs d'eau a une echelle
		 * comparable au relief plutot qu'a celle de la carte.
		 */
		constexpr float MaxLakeDepthCm = 10000.0f;

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
		 * Pente au-dela de laquelle une spline ne suit plus, en degres.
		 *
		 * Ce n'est pas une preference de rendu : au-dela, la courbe quitte le
		 * lit et l'eau flotte dans le vide. Trente degres laisse passer les
		 * rapides et coupe aux ressauts.
		 */
		constexpr float MaxSplineSlopeDeg = 30.0f;

		/** En deca, un bief ne vaut pas un acteur. */
		constexpr int32 MinReachPoints = 6;

		/**
		 * Nombre de corps de riviere au plus.
		 *
		 * Chacun porte sa spline, son maillage et ses composants de collision.
		 * Ce monde compte jusqu'a soixante cours, decoupes en biefs : sans
		 * quota, la pose se compterait en centaines d'acteurs.
		 */
		constexpr int32 MaxRiverBodies = 96;

		/** Planchers de lit, en metres. Une largeur nulle ne dessine rien. */
		constexpr float MinRiverWidthM = 2.0f;
		constexpr float MinRiverDepthM = 0.5f;

		/**
		 * Rayon dont l'eau deborde de son lit pour aller chercher la berge.
		 *
		 * LA SPLINE NE CONNAIT PAS LE RELIEF, et c'est tres bien ainsi : ce
		 * n'est pas elle qui decide ou l'eau s'arrete. La texture
		 * d'information compare, pixel par pixel, la hauteur d'eau a celle du
		 * sol, et l'eau se dessine partout ou la premiere depasse la seconde.
		 *
		 * Encore faut-il que le corps ait INSCRIT une hauteur d'eau a cet
		 * endroit. Hors de son lit il n'inscrit rien, et l'eau s'arrete net au
		 * bord du ruban — un cordon pose sur le terrain au lieu d'un cours
		 * loge dans sa vallee. ShapeDilation est ce qui repousse cette
		 * inscription vers l'exterieur.
		 *
		 * Le defaut du plugin, quarante metres, vise des rivieres posees a la
		 * main dans un terrain sculpte pour elles. Ici le relief est genere
		 * avant le cours : il faut de quoi traverser une vallee.
		 */
		constexpr float RiverDilationPerWidth = 20.0f;
		constexpr float MinRiverDilationCm = 20000.0f;   // deux cents metres
		constexpr float MaxRiverDilationCm = 60000.0f;   // six cents metres

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
		 * Remplit une courbe de metadonnees de spline, un point par sommet.
		 *
		 * SANS CELA LA RIVIERE EST INVISIBLE, et rien ne le signale.
		 *
		 * Le maillage lit sa largeur dans RiverWidth, indexee par la CLE
		 * D'ENTREE de la spline — soit le rang du point. Or SetSplinePoints ne
		 * touche pas aux metadonnees, et le seul code du plugin qui les
		 * redimensionne, UWaterSplineMetadata::Fixup, tient tout entier dans un
		 * #if WITH_EDITORONLY_DATA. Une courbe vide s'evalue a zero : la
		 * riviere se construit, s'enregistre, se declare en bonne sante, et
		 * n'a pas un pixel de large.
		 */
		void FillCurve(FInterpCurveFloat& Curve, const TArray<float>& Values)
		{
			Curve.Points.Reset(Values.Num());
			for (int32 I = 0; I < Values.Num(); ++I)
			{
				Curve.Points.Emplace(static_cast<float>(I), Values[I],
					0.0f, 0.0f, CIM_Linear);
			}
		}

		/** Largeur, profondeur, courant et son, le long du bief. */
		void ApplyRiverProfile(UWaterBodyRiverComponent* Component,
			const TArray<float>& WidthsCm, const TArray<float>& DepthsCm)
		{
			UWaterSplineMetadata* const Meta =
				Component ? Component->GetWaterSplineMetadata() : nullptr;
			if (!Meta)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[Worldseed] riviere sans metadonnees : largeur nulle"));
				return;
			}

			FillCurve(Meta->RiverWidth, WidthsCm);
			FillCurve(Meta->Depth, DepthsCm);

			// Le courant et le son ne changent pas le long d'un bief calme :
			// une seule valeur suffit, mais la courbe doit exister.
			TArray<float> Uniform;
			Uniform.Init(1.0f, WidthsCm.Num());
			FillCurve(Meta->WaterVelocityScalar, Uniform);
			FillCurve(Meta->AudioIntensity, Uniform);
		}

		/**
		 * Les deux materiaux de raccord aux autres nappes.
		 *
		 * Une riviere qui se jette dans l'ocean ou dans un lac n'y entre pas
		 * franche : le plugin fond les deux surfaces l'une dans l'autre, et ces
		 * materiaux-la sont ce avec quoi il le fait. Absents, la jonction reste
		 * une arete nette — le meme genre de defaut que les rivages droits
		 * qu'on a deja corriges.
		 */
		void ApplyRiverTransitions(UWaterBodyRiverComponent* Component)
		{
			if (!Component)
			{
				return;
			}

			if (UMaterialInterface* M = Load(TEXT("/Water/Materials/WaterSurface/Transitions/")
				TEXT("Water_Material_River_To_Ocean_Transition.Water_Material_River_To_Ocean_Transition")))
			{
				Component->SetOceanTransitionMaterial(M);
			}
			if (UMaterialInterface* M = Load(TEXT("/Water/Materials/WaterSurface/Transitions/")
				TEXT("Water_Material_River_To_Lake_Transition.Water_Material_River_To_Lake_Transition")))
			{
				Component->SetLakeTransitionMaterial(M);
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

	bool Build(UWorld* World, const FWorldseedGeometry& Geometry,
		const FWorldseedHydrology& Hydrology, float HeightExaggeration,
		float SeabedM, FWorldseedWaterBodies& Out)
	{
		Clear(Out);

		if (!World || Geometry.NX < 2)
		{
			return false;
		}

		const double StartTime = FPlatformTime::Seconds();

		const float WidthCm = Geometry.WidthM() * MetersToCm;
		const float HeightCm = Geometry.HeightM * MetersToCm;
		const float CellCm = Geometry.MetersPerPixel() * MetersToCm;
		const FVector2D OriginCm(-WidthCm * 0.5f, -HeightCm * 0.5f);

		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

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
		constexpr float LocalWindowCm = 400000.0f;   // quatre kilometres

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
				const float OceanDepthCm = FMath::Clamp(
					-SeabedM * MetersToCm * HeightExaggeration,
					MinOceanDepthCm, MaxOceanDepthCm);

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

		// --- les lacs -------------------------------------------------------
		int32 ReversedShores = 0;
		int32 UndoneCrossings = 0;
		TArray<FVector> Points;
		for (const FWorldseedLake& Lake : Hydrology.Lakes)
		{
			if (Lake.OutlinePx.Num() < 3)
			{
				continue;
			}

			// LE Z DE L'ACTEUR EST LE NIVEAU DE L'EAU.
			//
			// UpdateWaterHeight aplatit toute la spline sur la hauteur de
			// l'acteur, et le maillage se construit en 2D locale : les points
			// que j'y mets ne portent pas leur altitude. Un lac ne a zero se
			// dessinait donc au niveau de la mer — enterre sous la montagne qui
			// le contient, et parfaitement invisible.
			const float SurfaceCm = Lake.SurfaceM * MetersToCm * HeightExaggeration;


			// LA BORNE, ET NON LE RIVAGE.
			//
			// Un ocean n'a pas de forme : c'est un plan a une altitude, et le
			// relief decoupe son trait de cote. Un lac est le meme phenomene a
			// une autre altitude — son polygone ne dessine pas son rivage, il
			// BORNE la zone ou son niveau s'applique.
			//
			// Mesure sur la graine 20260909 : sur la moitie des points du
			// rivage extrait, le terrain affiche est DEJA sous la surface du
			// lac, jusqu'a 6,7 m. Ce contour n'est donc pas la limite de l'eau,
			// et le poursuivre au point pres ne dessinait rien.
			//
			// Une enveloppe convexe elargie fait la borne : simple par
			// construction, sans pincement ni auto-intersection, quelques
			// dizaines de points — et assez large pour que la dilatation du
			// plugin ne soit plus necessaire. C'est elle qui echouait encore
			// sur une nappe et laissait ce mur d'eau vertical au bord.
			TArray<FVector2D> Shore = WorldseedPolyline::ConvexHull(Lake.OutlinePx);
			if (Shore.Num() < 3)
			{
				continue;
			}

			// Marge vers l'exterieur depuis le centroide. Sur un convexe, cette
			// operation ne peut pas creer de croisement — ce qui n'etait pas le
			// cas du rentrage qu'elle remplace.
			{
				FVector2D Centre = FVector2D::ZeroVector;
				for (const FVector2D& P : Shore) { Centre += P; }
				Centre /= Shore.Num();

				for (FVector2D& P : Shore)
				{
					P += (P - Centre).GetSafeNormal() * ShoreMarginCells;
				}
			}

			const FWorldseedRing RingReduit = WorldseedPolyline::Measure(Shore);
			const FWorldseedRing RingBrut = WorldseedPolyline::Measure(Lake.OutlinePx);

			// LE CONTOUR DOIT ETRE SIMPLE, PAS SEULEMENT FERME.
			//
			// Une corde tendue par la reduction peut traverser une anse, et le
			// rentrage peut pincer un passage etroit. Le resultat se triangule
			// quand meme — la regle par enroulement l'encaisse — mais il ne se
			// DILATE pas, et c'est la dilatation qui laisse l'eau rejoindre la
			// berge.
			UndoneCrossings += WorldseedPolyline::MakeSimple(
				Shore, MinShoreSpacingCells, ShoreTouchEpsilonCells);
			if (Shore.Num() < 4)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[Worldseed] nappe de %.0f ha ecartee : rivage reduit a %d points"),
					Lake.AreaHa, Shore.Num());
				continue;
			}

			// LES QUATRE ETAPES, COTE A COTE.
			//
			// Tout est ici en CELLULES converties en metres par le meme
			// facteur : c'est la comparaison entre colonnes qui porte
			// l'information, pas la valeur absolue de l'une d'elles.
			{
				const FWorldseedRing RingSimple = WorldseedPolyline::Measure(Shore);
				const float M = Geometry.MetersPerPixel();
				const float HaPerCell2 = M * M / 10000.0f;

				auto Ligne = [&](const TCHAR* Etape, const FWorldseedRing& R)
				{
					UE_LOG(LogTemp, Log,
						TEXT("[Worldseed]     %-8s %4d pts  perimetre %6.0f m  aire %6.1f ha  ")
						TEXT("ecart mini %5.1f m  %d alignes"),
						Etape, R.Points, R.Perimeter * M,
						FMath::Abs(R.SignedArea) * HaPerCell2,
						R.MinVertexGap * M, R.Collinear);
				};

				UE_LOG(LogTemp, Log,
					TEXT("[Worldseed]   rivage %d : masque %.0f ha, marge %.0f m"),
					Out.Lakes.Num(), Lake.AreaHa,
					ShoreMarginCells * Geometry.MetersPerPixel());
				Ligne(TEXT("brut"), RingBrut);
				Ligne(TEXT("enveloppe"), RingReduit);
				Ligne(TEXT("simple"), RingSimple);
			}

			Points.Reset(Shore.Num());
			for (const FVector2D& Cell : Shore)
			{
				Points.Emplace(
					OriginCm.X + Cell.X * CellCm,
					OriginCm.Y + Cell.Y * CellCm,
					SurfaceCm);
			}

			// L'ACTEUR NAIT AU CENTRE DE SA NAPPE, ET NON A L'ORIGINE.
			//
			// La spline se stocke en coordonnees LOCALES, et c'est sur elles
			// que le plugin dilate le contour. Un lac ne a l'origine porte donc
			// une spline aux coordonnees du monde — jusqu'a huit cent mille
			// centimetres sur une carte de seize kilometres. L'offset qui
			// dilate travaille en virgule fixe : a cette echelle il ne rend
			// plus rien, la triangulation du maillage dilate echoue faute
			// d'entree, et l'eau s'arrete net au bord de la nappe.
			//
			// Centre sur son lac, le meme contour tient dans quelques
			// centaines de metres.
			FVector Middle(0.0f, 0.0f, SurfaceCm);
			for (const FVector& P : Points)
			{
				Middle.X += P.X;
				Middle.Y += P.Y;
			}
			Middle.X /= Points.Num();
			Middle.Y /= Points.Num();

			const FTransform At(FRotator::ZeroRotator, Middle);

			AWaterBodyLake* Body = World->SpawnActorDeferred<AWaterBodyLake>(
				AWaterBodyLake::StaticClass(), At, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Body)
			{
				continue;
			}

			MakeMovable(Body);
			Body->FinishSpawning(At);
			MakeMovable(Body);

			UWaterBodyComponent* Component = Body->GetWaterBodyComponent();
			UWaterSplineComponent* Spline = Component ? Component->GetWaterSpline() : nullptr;
			if (!Spline)
			{
				Body->Destroy();
				continue;
			}

			// LA MESURE DOIT ETRE COHERENTE AVEC ELLE-MEME.
			//
			// Emprise, perimetre et aire sont pris sur LE MEME tableau, celui
			// qu'on s'apprete a donner au plugin. Un contour ferme ne peut pas
			// avoir un perimetre inferieur au double de sa plus grande
			// dimension : si ces trois nombres se contredisent, c'est le
			// nettoyage en amont qui a recousu le contour de travers, et non le
			// plugin qui refuse un polygone sain.
			{
				FVector2D MinLocal(TNumericLimits<float>::Max());
				FVector2D MaxLocal(TNumericLimits<float>::Lowest());
				float ShortestCm = TNumericLimits<float>::Max();
				double PerimeterCm = 0.0;
				double TwiceAreaCm2 = 0.0;

				for (int32 I = 0, N = Points.Num(); I < N; ++I)
				{
					const FVector2D Here(Points[I].X, Points[I].Y);
					const FVector2D Next(Points[(I + 1) % N].X, Points[(I + 1) % N].Y);

					MinLocal = FVector2D::Min(MinLocal, Here);
					MaxLocal = FVector2D::Max(MaxLocal, Here);

					const float EdgeCm = static_cast<float>((Next - Here).Size());
					ShortestCm = FMath::Min(ShortestCm, EdgeCm);
					PerimeterCm += EdgeCm;
					TwiceAreaCm2 += (Here.X * Next.Y) - (Next.X * Here.Y);
				}

				const FVector2D Span = MaxLocal - MinLocal;
				const double AreaHa = FMath::Abs(TwiceAreaCm2) * 0.5 / 1.0e8;

				UE_LOG(LogTemp, Log,
					TEXT("[Worldseed]   rivage %d : %d points, emprise %.0f x %.0f m, ")
					TEXT("perimetre %.0f m, aire %.1f ha (masque %.0f ha), arete mini %.1f m"),
					Out.Lakes.Num(), Points.Num(),
					Span.X / MetersToCm, Span.Y / MetersToCm,
					PerimeterCm / MetersToCm, AreaHa, Lake.AreaHa,
					ShortestCm / MetersToCm);
			}
			Spline->SetSplinePoints(Points, ESplineCoordinateSpace::World, true);

			// Un rivage est une boucle, pas un trajet.
			Spline->SetClosedLoop(true, true);

			// Des tangentes lineaires : une spline de Bezier sur un contour
			// mesure au pixel deborderait dans les baies etroites.
			for (int32 I = 0; I < Spline->GetNumberOfSplinePoints(); ++I)
			{
				Spline->SetSplinePointType(I, ESplinePointType::Linear, false);
			}
			Spline->UpdateSpline();

			ApplyMaterials(Component,
				TEXT("/Water/Materials/WaterSurface/Water_Material_Lake.Water_Material_Lake"),
				TEXT("/Water/Materials/WaterSurface/LODs/Water_Material_Lake_LOD.Water_Material_Lake_LOD"));

			// EPAISSEUR DE LA NAPPE, ET NON DECOR.
			//
			// Elle donne sa hauteur au volume de collision du lac, qui descend
			// de ChannelDepth sous la surface. Le plugin la laisse a ZERO tant
			// qu'on ne sculpte pas de paysage avec : le volume devient une
			// feuille posee sur l'eau, et tout ce qui demande d'etre DEDANS —
			// le voile sous-marin, la nage, la flottabilite — s'eteint des le
			// premier metre de plongee.
			//
			// Elle se paie en PRECISION, en revanche : elle descend les bornes
			// du corps, donc le plancher de l'intervalle ou la texture
			// d'information normalise toutes les hauteurs d'eau (voir l'ocean
			// plus haut). La profondeur reelle d'un lac reste sans commune
			// mesure avec l'etendue de la carte, mais un bassin aberrant ne
			// doit pas pouvoir elargir seul l'intervalle commun : d'ou le
			// plafond.
			Component->CurveSettings.ChannelDepth = FMath::Clamp(
				Lake.MaxDepthM * MetersToCm * HeightExaggeration,
				MinLakeDepthCm, MaxLakeDepthCm);

			PushShape(Component);
			MakeMovable(Body);
			Out.Lakes.Add(Body);
		}



		if (ReversedShores > 0 || UndoneCrossings > 0)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] lacs : %d rivages sur %d retournes, %d croisements defaits"),
				ReversedShores, Out.Lakes.Num(), UndoneCrossings);
		}
		// --- les biefs calmes, confies au plugin ---------------------------
		{
			TArray<FWorldseedReach> Reaches;
			WorldseedRiverReaches::Split(Hydrology.Rivers, Geometry,
				MaxSplineSlopeDeg, MinReachPoints, Reaches);

			// Les plus longs d'abord : si le quota tranche, il doit garder ce
			// qui se parcourt.
			Reaches.Sort([](const FWorldseedReach& A, const FWorldseedReach& B)
				{ return A.Num() > B.Num(); });
			if (Reaches.Num() > MaxRiverBodies)
			{
				Reaches.SetNum(MaxRiverBodies);
			}

			TArray<FVector> ReachPoints;
			TArray<float> Widths;
			TArray<float> Depths;

			for (const FWorldseedReach& Reach : Reaches)
			{
				const FWorldseedRiver& River = Hydrology.Rivers[Reach.RiverIndex];

				// UNE RIVIERE N'EST PAS PLATE : le composant l'affirme par un
				// check(!IsFlatSurface()). Sa spline garde donc le Z de chaque
				// point, et l'acteur peut rester a l'origine — la ou un lac
				// devait naitre a la hauteur de sa nappe.
				AWaterBodyRiver* const Body = World->SpawnActorDeferred<AWaterBodyRiver>(
					AWaterBodyRiver::StaticClass(), FTransform::Identity, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (!Body)
				{
					continue;
				}

				MakeMovable(Body);
				Body->FinishSpawning(FTransform::Identity);
				MakeMovable(Body);

				UWaterBodyRiverComponent* const Component =
					Cast<UWaterBodyRiverComponent>(Body->GetWaterBodyComponent());
				UWaterSplineComponent* const Spline =
					Component ? Component->GetWaterSpline() : nullptr;
				if (!Spline)
				{
					Body->Destroy();
					continue;
				}

				ReachPoints.Reset(Reach.Num());
				Widths.Reset(Reach.Num());
				Depths.Reset(Reach.Num());

				float DeepestM = 0.0f;
				for (int32 I = Reach.First; I <= Reach.Last; ++I)
				{
					const FVector2D& P = River.PointsPx[I];
					ReachPoints.Emplace(
						OriginCm.X + P.X * CellCm,
						OriginCm.Y + P.Y * CellCm,
						River.SurfaceM[I] * MetersToCm * HeightExaggeration);

					// L'EMPRISE, ET NON LE LIT.
					//
					// Le corps d'eau ne dessine de l'eau que dans son emprise.
					// La dilatation ne l'etend pas : son shader enfonce sous le
					// sol toute eau dilatee qui passerait au-dessus du terrain.
					// Pour qu'un cours remplisse la cuvette qu'il traverse, c'est
					// donc sa LARGEUR qui doit valoir celle de la cuvette.
					//
					// Largeur TOTALE : l'en-tete du plugin annonce "from center
					// in each direction", mais son maillage divise par deux ce
					// qu'il lit. On suit le code.
					const float BasinM = River.BasinWidthM.IsValidIndex(I)
						? River.BasinWidthM[I] : 0.0f;
					Widths.Add(FMath::Max3(River.WidthM[I], BasinM, MinRiverWidthM)
						* MetersToCm);

					const float DepthM = River.DepthM.IsValidIndex(I)
						? River.DepthM[I] : MinRiverDepthM;
					Depths.Add(FMath::Max(DepthM, MinRiverDepthM) * MetersToCm);
					DeepestM = FMath::Max(DeepestM, DepthM);
				}

				Spline->SetSplinePoints(ReachPoints, ESplineCoordinateSpace::World, false);
				Spline->SetClosedLoop(false, false);

				// DES TANGENTES BRIDEES, ET NON LIBRES. Une Bezier libre
				// depasse dans les meandres serres : le lit sortirait de son
				// fond a chaque coude. La variante bridee garde la courbe
				// dans l'enveloppe de ses points tout en restant lisse.
				for (int32 I = 0; I < Spline->GetNumberOfSplinePoints(); ++I)
				{
					Spline->SetSplinePointType(I, ESplinePointType::CurveClamped, false);
				}
				Spline->UpdateSpline();

				ApplyRiverProfile(Component, Widths, Depths);

				// L'eau va chercher la berge : voir RiverDilationPerWidth.
				// Un lit large draine une vallee large, d'ou la proportion.
				float MeanWidthM = 0.0f;
				for (const float WidthCmValue : Widths)
				{
					MeanWidthM += WidthCmValue / MetersToCm;
				}
				MeanWidthM /= FMath::Max(Widths.Num(), 1);

				Component->ShapeDilation = FMath::Clamp(
					MeanWidthM * RiverDilationPerWidth * MetersToCm,
					MinRiverDilationCm, MaxRiverDilationCm);

				ApplyMaterials(Component,
					TEXT("/Water/Materials/WaterSurface/Water_Material_River.Water_Material_River"),
					TEXT("/Water/Materials/WaterSurface/LODs/Water_Material_River_LOD.Water_Material_River_LOD"));
				ApplyRiverTransitions(Component);

				// Epaisseur du lit : elle donne sa hauteur aux bornes du corps,
				// et participe donc a l'intervalle commun. Bornee pour les
				// memes raisons que l'ocean et les lacs.
				Component->CurveSettings.ChannelDepth = FMath::Clamp(
					DeepestM * MetersToCm * HeightExaggeration,
					MinLakeDepthCm, MaxLakeDepthCm);

				PushShape(Component);
				MakeMovable(Body);
				Out.Rivers.Add(Body);
			}

			WorldseedRiverReaches::MarkTakenSegments(
				Hydrology.Rivers, Reaches, Out.TakenRiverSegments);
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
				Component->ShapeDilation / MetersToCm, *Maillages);
		};

		if (Out.Ocean.IsValid())
		{
			Report(TEXT("ocean"), Out.Ocean.Get(), Out.Ocean->GetWaterBodyComponent());
		}
		for (int32 I = 0; I < FMath::Min(Out.Lakes.Num(), 3); ++I)
		{
			if (Out.Lakes[I].IsValid())
			{
				Report(*FString::Printf(TEXT("lac %d"), I), Out.Lakes[I].Get(),
					Out.Lakes[I]->GetWaterBodyComponent());
			}
		}
		for (int32 I = 0; I < FMath::Min(Out.Rivers.Num(), 3); ++I)
		{
			if (!Out.Rivers[I].IsValid())
			{
				continue;
			}

			UWaterBodyRiverComponent* const Component =
				Cast<UWaterBodyRiverComponent>(Out.Rivers[I]->GetWaterBodyComponent());
			Report(*FString::Printf(TEXT("bief %d"), I), Out.Rivers[I].Get(), Component);

			// LA CONDITION PROPRE A LA RIVIERE, et la seule qu'un rapport
			// generique ne verrait pas : une courbe de largeur vide donne un
			// lit de zero metre. Le corps est alors parfaitement sain sur tous
			// les autres criteres, et invisible.
			if (const UWaterSplineMetadata* Meta =
				Component ? Component->GetWaterSplineMetadata() : nullptr)
			{
				UE_LOG(LogTemp, Log,
					TEXT("[Worldseed]     profil : %d largeurs, %d profondeurs, ")
					TEXT("lit %.1f m au premier point"),
					Meta->RiverWidth.Points.Num(), Meta->Depth.Points.Num(),
					Component->GetRiverWidthAtSplineInputKey(0.0f) / MetersToCm);
			}
		}


		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] plugin Water : zone %.1f x %.1f km, fenetre=%s, ocean=%d, %d lacs, %d biefs  (%.0f ms)"),
			Geometry.WidthM() / 1000.0f, Geometry.HeightM / 1000.0f,
			bLocalWindow ? TEXT("glissante 4 km") : TEXT("globale"),
			Out.Ocean.IsValid() ? 1 : 0, Out.Lakes.Num(), Out.Rivers.Num(),
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
		for (TWeakObjectPtr<AWaterBodyRiver>& River : Bodies.Rivers)
		{
			if (River.IsValid())
			{
				River->Destroy();
			}
		}
		Bodies.Rivers.Reset();
		Bodies.TakenRiverSegments.Reset();

		for (TWeakObjectPtr<AWaterBodyLake>& Lake : Bodies.Lakes)
		{
			if (Lake.IsValid())
			{
				Lake->Destroy();
			}
		}
		Bodies.Lakes.Reset();

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
