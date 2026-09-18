// Worldseed - pose de l'ocean, des lacs et des cours d'eau dans le monde.

#include "Procedural/WorldseedWaterComponent.h"

#include "Procedural/WorldseedWaterMesh.h"
#include "Procedural/WorldseedRivers.h"
#include "Procedural/WorldseedRiverSection.h"
#include "Procedural/WorldseedWaterDebug.h"

#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "TimerManager.h"

namespace
{
	/** Le monde raisonne en metres, la scene en centimetres. */
	constexpr float MetersToCm = 100.0f;

	/**
	 * Longueur minimale d'un ruban, en multiples de sa largeur.
	 *
	 * En deca, les sections transversales se replient l'une sur l'autre et le
	 * ruban devient une pile de tuiles plutot qu'un cours d'eau.
	 */
	constexpr float MinRibbonLengthPerWidth = 3.0f;

	/**
	 * Debord du plan d'ocean au-dela de la carte.
	 *
	 * L'horizon doit rester de l'eau quand on regarde vers le large : un plan
	 * s'arretant pile au bord de la carte laisserait voir le vide par-dessus.
	 */
	constexpr float OceanOversize = 3.0f;

	/** L'eau ne porte pas le joueur et n'a pas a etre testee par les rayons. */
	void MakeNonColliding(UProceduralMeshComponent* Mesh)
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->bUseAsyncCooking = false;
	}
}

UWorldseedWaterComponent::UWorldseedWaterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UProceduralMeshComponent* UWorldseedWaterComponent::EnsureMesh(FName Name,
	TObjectPtr<UProceduralMeshComponent>& Slot, UMaterialInterface* Material)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	if (!Slot)
	{
		Slot = NewObject<UProceduralMeshComponent>(Owner, Name);
		Slot->SetupAttachment(Owner->GetRootComponent());
		MakeNonColliding(Slot);
		Slot->RegisterComponent();
	}

	Slot->ClearAllMeshSections();
	if (Material)
	{
		Slot->SetMaterial(0, Material);
	}
	return Slot;
}

void UWorldseedWaterComponent::Clear()
{
	WaterfallTops.Reset();
	LastGroundRefreshTime = -BIG_NUMBER;
	WorldseedWaterBodies::Clear(WaterBodies);
	for (TObjectPtr<UProceduralMeshComponent>* Slot :
		{ &OceanMesh, &LakeMesh, &RiverMesh, &WaterfallMesh })
	{
		if (*Slot)
		{
			(*Slot)->ClearAllMeshSections();
		}
	}
	SectionCount = 0;
}

void UWorldseedWaterComponent::Build(const FWorldseedGeometry& Geometry,
	const FWorldseedHydrology& Hydrology, const TArray<float>& ElevationM,
	float HeightExaggeration)
{
	Clear();

	if (Geometry.NX < 2)
	{
		return;
	}

	const double StartTime = FPlatformTime::Seconds();

	// LE POINT LE PLUS BAS DU MONDE, qui donne son epaisseur a l'ocean. Zero
	// si le relief n'atteint jamais la mer : la pose y mettra son plancher.
	float SeabedM = 0.0f;
	for (const float M : ElevationM)
	{
		SeabedM = FMath::Min(SeabedM, M);
	}

	// --- le plugin Water d'abord ---------------------------------------------
	// S'il repond, il prend l'ocean et les lacs, et les maillages n'ont plus
	// lieu d'etre. Les rivieres et les cascades restent a nous dans tous les
	// cas : une spline se casse sur une chute.
	bool bPluginTookSurfaces = false;
	if (bUseWaterPlugin)
	{
		bPluginTookSurfaces = WorldseedWaterBodies::Build(
			GetWorld(), Geometry, Hydrology, HeightExaggeration,
			SeabedM, WaterBodies);
	}

	if (bBuildOcean && !bPluginTookSurfaces)
	{
		BuildOcean(Geometry);
	}
	if (bBuildLakes && !bPluginTookSurfaces)
	{
		BuildLakes(Geometry, Hydrology, HeightExaggeration);
	}
	if (bBuildRivers)
	{
		BuildRivers(Geometry, Hydrology, ElevationM, HeightExaggeration);
	}
	if (bBuildWaterfalls)
	{
		BuildWaterfalls(Geometry, Hydrology, ElevationM, HeightExaggeration);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Worldseed] eau posee : %d sections, plugin=%d (%d lacs, %d rivieres, %d cascades)  (%.0f ms)"),
		SectionCount, bPluginTookSurfaces ? 1 : 0, bBuildLakes ? Hydrology.Lakes.Num() : 0,
		bBuildRivers ? Hydrology.Rivers.Num() : 0, WaterfallTops.Num(),
		(FPlatformTime::Seconds() - StartTime) * 1000.0);

	// Les plus hautes d'abord : ce sont celles qui valent le detour.
	for (int32 I = 0; I < FMath::Min(WaterfallTops.Num(), 5); ++I)
	{
		UE_LOG(LogTemp, Log, TEXT("[Worldseed]   cascade %d au point %s"),
			I + 1, *WaterfallTops[I].ToCompactString());
	}


	if (bDrawRiverTrace)
	{
		WorldseedWaterDebug::DrawRivers(GetWorld(), Geometry, Hydrology,
			ElevationM, HeightExaggeration, WaterBodies.TakenRiverSegments);
		WorldseedWaterDebug::DrawLakes(GetWorld(), Geometry, Hydrology,
			ElevationM, HeightExaggeration);

		// Le balayage complet : mille cinq cents coupes plutot que sept.
		WorldseedRiverSection::Sweep(ElevationM, Geometry, Hydrology);
	}
	// LE RELEVE VIENT APRES COUP, ET C'EST TOUT L'INTERET.
	//
	// Au moment de la pose, la zone n'a encore rien rendu : ses bornes de
	// hauteur et son plancher de sol valent leurs valeurs d'usine, et un
	// releve pris ici ne dirait rien. Quelques secondes plus tard, ils disent
	// exactement ce que la texture d'information a vu.
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().SetTimer(HealthTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				WorldseedWaterBodies::LogHealth(GetWorld(), WaterBodies);
			}),
			HealthDelayS, false);
	}
}

void UWorldseedWaterComponent::NotifyGroundChanged()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// On ESPACE plutot qu'on ne compte : ce qui coute n'est pas le nombre de
	// chunks arrives, c'est la frequence a laquelle on redessine la texture.
	const float Now = World->GetTimeSeconds();
	if (Now - LastGroundRefreshTime < GroundRefreshPeriodS)
	{
		return;
	}
	LastGroundRefreshTime = Now;

	WorldseedWaterBodies::NotifyGroundChanged(WaterBodies);
}

void UWorldseedWaterComponent::BuildOcean(const FWorldseedGeometry& Geometry)
{
	UProceduralMeshComponent* Mesh = EnsureMesh(TEXT("Ocean"), OceanMesh, OceanMaterial);
	if (!Mesh)
	{
		return;
	}

	FWorldseedMeshBuffer Buffer;

	// L'ALTITUDE ZERO EST LE NIVEAU DE LA MER par construction : toute la chaine
	// de generation cale son quantile dessus. L'ocean n'a donc rien a chercher,
	// il se pose a zero.
	WorldseedWaterMesh::BuildPlane(
		Geometry.WidthM() * MetersToCm * OceanOversize,
		Geometry.HeightM * MetersToCm * OceanOversize,
		0.0f, OceanSubdivisions, Buffer);

	if (Buffer.IsEmpty())
	{
		return;
	}

	Mesh->CreateMeshSection(0, Buffer.Vertices, Buffer.Triangles, Buffer.Normals,
		Buffer.UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	++SectionCount;
}

void UWorldseedWaterComponent::BuildLakes(const FWorldseedGeometry& Geometry,
	const FWorldseedHydrology& Hydrology, float HeightExaggeration)
{
	if (Hydrology.Lakes.Num() == 0 || Hydrology.LakeLabels.Num() != Geometry.CellCount())
	{
		return;
	}

	UProceduralMeshComponent* Mesh = EnsureMesh(TEXT("Lakes"), LakeMesh, LakeMaterial);
	if (!Mesh)
	{
		return;
	}

	const float CellCm = Geometry.MetersPerPixel() * MetersToCm;
	const FVector2D OriginCm(
		-Geometry.WidthM() * MetersToCm * 0.5f,
		-Geometry.HeightM * MetersToCm * 0.5f);

	FWorldseedMeshBuffer Buffer;

	// UNE SECTION PAR LAC, et non une seule pour tous : chaque cuvette a SA
	// surface libre, et les fondre ensemble les mettrait toutes a la meme
	// altitude — un lac de montagne se retrouverait au niveau d'un lac de
	// plaine.
	int32 Section = 0;
	int32 Dropped = 0;
	for (const FWorldseedLake& Lake : Hydrology.Lakes)
	{
		WorldseedWaterMesh::BuildSurfaceFromMask(
			Hydrology.LakeLabels, Lake.Label, Geometry.NX, Geometry.NY,
			Lake.BoundsPx, CellCm,
			Lake.SurfaceM * MetersToCm * HeightExaggeration,
			OriginCm, Buffer);

		if (Buffer.IsEmpty())
		{
			continue;
		}

		// L'index de section est PROPRE A CHAQUE COMPOSANT : le partager avec
		// les autres maillages laisserait des sections vides en tete.
		Mesh->CreateMeshSection(Section, Buffer.Vertices, Buffer.Triangles,
			Buffer.Normals, Buffer.UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), false);

		if (LakeMaterial)
		{
			Mesh->SetMaterial(Section, LakeMaterial);
		}
		++Section;
		++SectionCount;
	}
}

void UWorldseedWaterComponent::BuildWaterfalls(const FWorldseedGeometry& Geometry,
	const FWorldseedHydrology& Hydrology, const TArray<float>& ElevationM,
	float HeightExaggeration)
{
	if (Hydrology.Waterfalls.Num() == 0 || ElevationM.Num() != Geometry.CellCount())
	{
		return;
	}

	UProceduralMeshComponent* Mesh =
		EnsureMesh(TEXT("Waterfalls"), WaterfallMesh, WaterfallMaterial);
	if (!Mesh)
	{
		return;
	}

	const int32 NX = Geometry.NX;
	const int32 NY = Geometry.NY;
	const float CellCm = Geometry.MetersPerPixel() * MetersToCm;
	const float OriginX = -Geometry.WidthM() * MetersToCm * 0.5f;
	const float OriginY = -Geometry.HeightM * MetersToCm * 0.5f;

	// La chute porte SES altitudes, prises sur la surface d'eau : les relire
	// dans le relief brut les ferait diverger du ruban de la riviere.
	auto ToWorld = [&](const FVector2D& Px, float AltitudeM)
	{
		return FVector(
			OriginX + Px.X * CellCm,
			OriginY + Px.Y * CellCm,
			AltitudeM * MetersToCm * HeightExaggeration);
	};

	// Les plus hautes d'abord : le quota, s'il tranche un jour, doit garder ce
	// qui se voit, et la liste rendue sert a aller les visiter.
	TArray<int32> Ranked;
	Ranked.Reserve(Hydrology.Waterfalls.Num());
	for (int32 I = 0; I < Hydrology.Waterfalls.Num(); ++I)
	{
		Ranked.Add(I);
	}
	Ranked.Sort([&Hydrology](int32 A, int32 B)
	{
		return Hydrology.Waterfalls[A].DropM > Hydrology.Waterfalls[B].DropM;
	});

	TArray<FVector> Points;
	TArray<float> HalfWidths;
	FWorldseedMeshBuffer Buffer;

	int32 Section = 0;
	int32 Dropped = 0;
	for (const int32 Index : Ranked)
	{
		const FWorldseedWaterfall& Fall = Hydrology.Waterfalls[Index];

		const FVector Top = ToWorld(Fall.TopPx, Fall.TopM);
		const FVector Bottom = ToWorld(Fall.BottomPx, Fall.BottomM);

		WaterfallTops.Add(Top);

		// UN RUBAN DE DEUX POINTS SUFFIT : la chute est un segment, et
		// BuildRibbon aplatit deja son axe pour calculer le cote — la nappe
		// reste donc verticale quelle que soit la denivelee.
		Points.Reset(2);
		Points.Add(Top);
		Points.Add(Bottom);

		const float HalfWidthCm =
			Fall.WidthM * MetersToCm * 0.5f * WaterfallWidthScale;
		HalfWidths.Reset(2);
		HalfWidths.Add(HalfWidthCm);

		// Une chute s'evase en tombant : le bas est plus large que le haut.
		HalfWidths.Add(HalfWidthCm * 1.3f);

		WorldseedWaterMesh::BuildRibbon(Points, HalfWidths, Buffer);
		if (Buffer.IsEmpty())
		{
			continue;
		}

		Mesh->CreateMeshSection(Section, Buffer.Vertices, Buffer.Triangles,
			Buffer.Normals, Buffer.UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), false);

		if (WaterfallMaterial)
		{
			Mesh->SetMaterial(Section, WaterfallMaterial);
		}
		++Section;
		++SectionCount;
	}
}

void UWorldseedWaterComponent::BuildRivers(const FWorldseedGeometry& Geometry,
	const FWorldseedHydrology& Hydrology, const TArray<float>& ElevationM,
	float HeightExaggeration)
{
	if (Hydrology.Rivers.Num() == 0 || ElevationM.Num() != Geometry.CellCount())
	{
		return;
	}

	UProceduralMeshComponent* Mesh = EnsureMesh(TEXT("Rivers"), RiverMesh, RiverMaterial);
	if (!Mesh)
	{
		return;
	}

	const int32 NX = Geometry.NX;
	const int32 NY = Geometry.NY;
	const float CellCm = Geometry.MetersPerPixel() * MetersToCm;
	const float OriginX = -Geometry.WidthM() * MetersToCm * 0.5f;
	const float OriginY = -Geometry.HeightM * MetersToCm * 0.5f;
	const float BedOffsetCm = RiverBedOffsetM * MetersToCm * HeightExaggeration;

	TArray<FVector> Points;
	TArray<float> HalfWidths;
	FWorldseedMeshBuffer Buffer;


	int32 Section = 0;
	int32 Dropped = 0;
	// ON NE DESSINE QUE CE QUE LE PLUGIN N'A PAS PRIS.
	//
	// Les biefs calmes sont devenus des AWaterBodyRiver ; il reste les
	// ressauts, et les cours que le quota a laisses de cote. Superposer les
	// deux representations ne donnerait pas une eau plus belle : deux nappes au
	// meme endroit se battent en z-fighting, et le resultat scintille.
	const TArray<TArray<bool>>& Taken = WaterBodies.TakenRiverSegments;

	for (int32 R = 0; R < Hydrology.Rivers.Num(); ++R)
	{
		const FWorldseedRiver& River = Hydrology.Rivers[R];
		const int32 Num = River.PointsPx.Num();
		if (Num < 2 || River.WidthM.Num() != Num)
		{
			continue;
		}

		const TArray<bool>* const Mask = Taken.IsValidIndex(R) ? &Taken[R] : nullptr;
		const bool bHasSurface = (River.SurfaceM.Num() == Num);

		// Un ruban par suite de segments libres.
		int32 First = 0;
		while (First < Num - 1)
		{
			if (Mask && Mask->IsValidIndex(First) && (*Mask)[First])
			{
				++First;
				continue;
			}

			int32 Last = First;
			while (Last < Num - 1
				&& !(Mask && Mask->IsValidIndex(Last) && (*Mask)[Last]))
			{
				++Last;
			}

			Points.Reset(Last - First + 1);
			HalfWidths.Reset(Last - First + 1);

			for (int32 I = First; I <= Last; ++I)
			{
				const FVector2D& P = River.PointsPx[I];

				const int32 Col = ((FMath::RoundToInt(P.X) % NX) + NX) % NX;
				const int32 Row = FMath::Clamp(FMath::RoundToInt(P.Y), 0, NY - 1);

				// LA SURFACE D'EAU, PAS LE RELIEF BRUT. Le relief brut plonge
				// dans chaque cuvette que le routage a comblee : y coller le
				// ruban ferait descendre puis REMONTER le cours, ce qui se voit
				// tout de suite.
				const float SurfaceM = bHasSurface
					? River.SurfaceM[I] : ElevationM[Row * NX + Col];

				Points.Emplace(
					OriginX + P.X * CellCm,
					OriginY + P.Y * CellCm,
					SurfaceM * MetersToCm * HeightExaggeration - BedOffsetCm);

				HalfWidths.Add(River.WidthM[I] * MetersToCm * 0.5f);
			}

			First = Last + 1;

			// UN RUBAN PLUS LARGE QUE LONG NE SE CONSTRUIT PAS.
			//
			// BuildRibbon oriente chaque section transversale par la
			// bissectrice du coude. Tant que le pas depasse la largeur, les
			// sections se suivent ; en deca elles se REPLIENT l'une sur
			// l'autre, et le ruban devient un empilement de tuiles en escalier
			// — ou, sur un segment quasi vertical, un simple pic.
			//
			// Ces bouts-la sont apparus avec la decoupe en biefs : entre deux
			// troncons confies au plugin, il ne reste parfois qu'un segment,
			// juste celui qui plonge. Il ne vaut pas la peine d'etre dessine,
			// et dessine il se voit comme un bourrelet au bord de l'eau.
			float RunCm = 0.0f;
			for (int32 I = 1; I < Points.Num(); ++I)
			{
				RunCm += FVector2D(Points[I].X - Points[I - 1].X,
					Points[I].Y - Points[I - 1].Y).Size();
			}

			float MeanWidthCm = 0.0f;
			for (const float Half : HalfWidths)
			{
				MeanWidthCm += Half * 2.0f;
			}
			MeanWidthCm /= FMath::Max(HalfWidths.Num(), 1);

			if (RunCm < MeanWidthCm * MinRibbonLengthPerWidth)
			{
				++Dropped;
				continue;
			}


			WorldseedWaterMesh::BuildRibbon(Points, HalfWidths, Buffer);
			if (Buffer.IsEmpty())
			{
				continue;
			}

			Mesh->CreateMeshSection(Section, Buffer.Vertices, Buffer.Triangles,
				Buffer.Normals, Buffer.UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), false);

			if (RiverMaterial)
			{
				Mesh->SetMaterial(Section, RiverMaterial);
			}
			++Section;
			++SectionCount;
		}
	}

	if (Dropped > 0)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] rivieres : %d sections posees, %d bouts trop courts ecartes"),
			Section, Dropped);
	}
}
