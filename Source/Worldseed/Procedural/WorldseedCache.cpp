// Worldseed - cache disque des mondes deja calcules.

#include "Procedural/WorldseedCache.h"

#include "HAL/FileManager.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"

namespace WorldseedCache
{
	namespace
	{
		/** Empreinte des regles, ecrite telle quelle : 32 caracteres MD5. */
		constexpr int32 HashChars = 32;

		FString CacheDir()
		{
			return FPaths::ProjectSavedDir() / TEXT("Worldseed/Cache");
		}

		/**
		 * Entete EN CLAIR, avant la partie compressee. C'est ce qui permet de
		 * dresser l'inventaire du cache sans decompresser chaque fichier.
		 */
		struct FHeader
		{
			uint32 Magic = 0;
			uint32 FormatVersion = 0;
			uint32 PipelineVersion = 0;
			uint32 RawSize = 0;
			int32 Seed = 0;
			int32 NX = 0;
			int32 NY = 0;
			float HeightM = 0.0f;
			ANSICHAR RulesHash[HashChars] = {};
		};

		bool ReadHeader(const FString& FullPath, FHeader& Out, int64& OutFileSize)
		{
			TArray<uint8> Bytes;
			if (!FFileHelper::LoadFileToArray(Bytes, *FullPath))
			{
				return false;
			}
			OutFileSize = Bytes.Num();
			if (Bytes.Num() < static_cast<int32>(sizeof(FHeader)))
			{
				return false;
			}
			FMemory::Memcpy(&Out, Bytes.GetData(), sizeof(FHeader));
			return Out.Magic == Magic;
		}

		void WriteBytes(FArchive& Ar, const TArray<uint8>& Data)
		{
			int32 Num = Data.Num();
			Ar << Num;
			if (Num > 0)
			{
				Ar.Serialize(const_cast<uint8*>(Data.GetData()), Num);
			}
		}

		/**
		 * Le reseau de cavites : les PRIMITIVES, jamais l'index.
		 *
		 * ON ECRIT CHAMP PAR CHAMP ET NON LA STRUCTURE EN BLOC. Un
		 * `Serialize(&S, sizeof(S))` sur un tableau de structures grave dans le
		 * fichier le bourrage du compilateur et l'ordre des membres : ajouter un
		 * champ, ou changer l'alignement, produirait alors un cache qui se relit
		 * SANS ERREUR et rend des chambres au mauvais endroit. Champ par champ,
		 * un decalage ne passe pas inapercu -- et l'empreinte de version le
		 * rattrape de toute facon.
		 *
		 * L'INDEX SPATIAL N'EST PAS ECRIT : il est derive, il pese plus que ce
		 * qu'il indexe -- une entree par case TOUCHEE, donc un long tunnel
		 * figure dans des dizaines de cases -- et `ReconstruireIndex` le refait
		 * en quelques millisecondes a partir de ce qui suit.
		 */
		void EcrireGrottes(FArchive& Ar, const FWorldseedCaveNetwork& N)
		{
			int32 NbChambres = N.Chambers.Num();
			int32 NbSegments = N.Segments.Num();
			int32 NbArches = N.Arches.Num();
			int32 NbPuits = N.Puits.Num();
			float CellM = N.CellM;
			FIntPoint Min = N.Min;
			FIntPoint Size = N.Size;
			Ar << NbChambres; Ar << NbSegments; Ar << NbArches; Ar << NbPuits;
			Ar << CellM; Ar << Min; Ar << Size;

			for (const FWorldseedCaveChamber& C : N.Chambers)
			{
				FVector P = C.CentreM; float R = C.RadiusM;
				Ar << P; Ar << R;
			}
			for (const FWorldseedCaveSegment& S : N.Segments)
			{
				FVector A = S.AM, B = S.BM; float RA = S.RadiusAM, RB = S.RadiusBM;
				Ar << A; Ar << B; Ar << RA; Ar << RB;
			}
			for (const FWorldseedCaveArch& A : N.Arches)
			{
				FVector P = A.CentreM; FVector2D T = A.TraversM;
				float E = A.EpaisseurM, R = A.RayonM, Pont = A.PontM;
				Ar << P; Ar << T; Ar << E; Ar << R; Ar << Pont;
			}
			for (const FWorldseedCavePuits& P : N.Puits)
			{
				FVector C = P.CentreM;
				float Sol = P.SolM, H = P.HautM, B = P.BasM;
				float RH = P.RayonHautM, RB = P.RayonBasM;
				bool bDoline = P.bDoline;
				Ar << C; Ar << Sol; Ar << H; Ar << B; Ar << RH; Ar << RB; Ar << bDoline;
			}
		}

		bool LireGrottes(FArchive& Ar, FWorldseedCaveNetwork& N)
		{
			N.Reset();

			int32 NbChambres = 0, NbSegments = 0, NbArches = 0, NbPuits = 0;
			float CellM = 64.0f;
			FIntPoint Min = FIntPoint::ZeroValue;
			FIntPoint Size = FIntPoint::ZeroValue;
			Ar << NbChambres; Ar << NbSegments; Ar << NbArches; Ar << NbPuits;
			Ar << CellM; Ar << Min; Ar << Size;

			// BORNES DE SURETE : un fichier tronque ou corrompu donnerait sinon
			// une reservation absurde avant que la lecture n'echoue.
			if (NbChambres < 0 || NbChambres > 1000000
				|| NbSegments < 0 || NbSegments > 10000000
				|| NbArches < 0 || NbArches > 1000000
				|| NbPuits < 0 || NbPuits > 1000000
				|| Size.X < 0 || Size.Y < 0
				|| static_cast<int64>(Size.X) * Size.Y > 100000000)
			{
				return false;
			}

			N.CellM = CellM;
			N.Min = Min;
			N.Size = Size;

			N.Chambers.SetNum(NbChambres);
			for (FWorldseedCaveChamber& C : N.Chambers)
			{
				FVector P; float R;
				Ar << P; Ar << R;
				C.CentreM = P; C.RadiusM = R;
			}
			N.Segments.SetNum(NbSegments);
			for (FWorldseedCaveSegment& S : N.Segments)
			{
				FVector A, B; float RA, RB;
				Ar << A; Ar << B; Ar << RA; Ar << RB;
				S.AM = A; S.BM = B; S.RadiusAM = RA; S.RadiusBM = RB;
			}
			N.Arches.SetNum(NbArches);
			for (FWorldseedCaveArch& A : N.Arches)
			{
				FVector P; FVector2D T; float E, R, Pont;
				Ar << P; Ar << T; Ar << E; Ar << R; Ar << Pont;
				A.CentreM = P; A.TraversM = T;
				A.EpaisseurM = E; A.RayonM = R; A.PontM = Pont;
			}
			N.Puits.SetNum(NbPuits);
			for (FWorldseedCavePuits& P : N.Puits)
			{
				FVector C; float Sol, H, B, RH, RB; bool bDoline;
				Ar << C; Ar << Sol; Ar << H; Ar << B; Ar << RH; Ar << RB; Ar << bDoline;
				P.CentreM = C; P.SolM = Sol; P.HautM = H; P.BasM = B;
				P.RayonHautM = RH; P.RayonBasM = RB; P.bDoline = bDoline;
			}

			// L'INDEX SE REFAIT ICI, et c'est la seule chose que la lecture
			// calcule. Sans lui, `Query` rendrait zero primitive par chunk et le
			// terrain serait plein -- sans la moindre erreur.
			N.ReconstruireIndex();
			return !Ar.IsError();
		}

		bool ReadBytes(FArchive& Ar, TArray<uint8>& Data)
		{
			int32 Num = 0;
			Ar << Num;
			if (Num < 0 || Num > 400000000)
			{
				return false;
			}
			Data.SetNumUninitialized(Num);
			if (Num > 0)
			{
				Ar.Serialize(Data.GetData(), Num);
			}
			return true;
		}

		void WriteFloats(FArchive& Ar, const TArray<float>& Data)
		{
			int32 Num = Data.Num();
			Ar << Num;
			if (Num > 0)
			{
				Ar.Serialize(const_cast<float*>(Data.GetData()), Num * sizeof(float));
			}
		}

		bool ReadFloats(FArchive& Ar, TArray<float>& Data)
		{
			int32 Num = 0;
			Ar << Num;
			if (Num < 0 || Num > 100000000)
			{
				return false;
			}
			Data.SetNumUninitialized(Num);
			if (Num > 0)
			{
				Ar.Serialize(Data.GetData(), Num * sizeof(float));
			}
			return true;
		}

		FString HashToString(const ANSICHAR* Raw)
		{
			return FString(HashChars, StringCast<TCHAR>(Raw, HashChars).Get());
		}
	}

	FString MakeKey(int32 Seed, float HeightMeters, int32 ResolutionY, const FString& RulesHash)
	{
		// La version de chaine entre dans la cle au meme titre que les regles :
		// l'une couvre le code, l'autre les reglages.
		const FString Raw = FString::Printf(TEXT("v%d|%d|%.3f|%d|%s"),
			WORLDSEED_PIPELINE_VERSION, Seed, HeightMeters, ResolutionY, *RulesHash);
		return FMD5::HashAnsiString(*Raw);
	}

	FString PathForKey(const FString& Key)
	{
		return CacheDir() / (Key + TEXT(".wsw"));
	}

	bool Load(const FString& Key, FWorldseedWorldData& Out)
	{
		const FString Path = PathForKey(Key);

		TArray<uint8> File;
		if (!FFileHelper::LoadFileToArray(File, *Path))
		{
			return false;
		}
		if (File.Num() < static_cast<int32>(sizeof(FHeader)))
		{
			return false;
		}

		FHeader Header;
		FMemory::Memcpy(&Header, File.GetData(), sizeof(FHeader));

		if (Header.Magic != Magic || Header.FormatVersion != FormatVersion)
		{
			UE_LOG(LogTemp, Log, TEXT("[Worldseed] cache d'un autre format, ignore"));
			return false;
		}
		if (Header.PipelineVersion != WORLDSEED_PIPELINE_VERSION)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Worldseed] cache produit par la chaine v%u, la chaine courante est v%d : ignore"),
				Header.PipelineVersion, WORLDSEED_PIPELINE_VERSION);
			return false;
		}

		const int32 HeaderBytes = sizeof(FHeader);
		TArray<uint8> Raw;
		Raw.SetNumUninitialized(static_cast<int32>(Header.RawSize));

		if (!FCompression::UncompressMemory(NAME_Zlib, Raw.GetData(), Raw.Num(),
			File.GetData() + HeaderBytes, File.Num() - HeaderBytes))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Worldseed] cache corrompu, ignore : %s"), *Path);
			return false;
		}

		FMemoryReader Ar(Raw, true);

		float LatSpan = 180.0f;
		float Blend = 1.0f;
		FString Mapping;
		Ar << LatSpan; Ar << Blend; Ar << Mapping;

		Out.Seed = Header.Seed;
		Out.Geometry.NX = Header.NX;
		Out.Geometry.NY = Header.NY;
		Out.Geometry.HeightM = Header.HeightM;
		Out.Geometry.LatSpanDeg = LatSpan;
		Out.Geometry.LatitudeEqualAreaBlend = Blend;
		Out.Geometry.LatitudeMapping = Mapping;

		// SeasonalAmpC est lu comme les autres : un fichier d'une version
		// anterieure n'arrive jamais jusqu'ici, l'en-tete l'a deja refuse.
		if (!ReadFloats(Ar, Out.ElevationM) || !ReadFloats(Ar, Out.TempC)
			|| !ReadFloats(Ar, Out.PrecipMm) || !ReadFloats(Ar, Out.SeasonalAmpC)
			|| !ReadFloats(Ar, Out.Continentality)
			|| !ReadBytes(Ar, Out.LithologyId)
			|| !LireGrottes(Ar, Out.Caves))
		{
			return false;
		}

		const int32 Expected = Header.NX * Header.NY;
		if (Out.ElevationM.Num() != Expected)
		{
			return false;
		}

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] monde charge du cache : %dx%d  %.1f Mo sur disque"),
			Header.NX, Header.NY, File.Num() / (1024.0f * 1024.0f));
		return true;
	}

	bool Save(const FString& Key, const FString& RulesHash,
		const FWorldseedWorldData& World)
	{
		const FWorldseedGeometry& Geometry = World.Geometry;
		const int32 Seed = World.Seed;
		if (Geometry.NX < 2 || World.ElevationM.Num() != Geometry.CellCount())
		{
			return false;
		}

		FBufferArchive Raw;
		float LatSpan = Geometry.LatSpanDeg;
		float Blend = Geometry.LatitudeEqualAreaBlend;
		FString Mapping = Geometry.LatitudeMapping;
		Raw << LatSpan; Raw << Blend; Raw << Mapping;

		WriteFloats(Raw, World.ElevationM);
		WriteFloats(Raw, World.TempC);
		WriteFloats(Raw, World.PrecipMm);
		WriteFloats(Raw, World.SeasonalAmpC);
		WriteFloats(Raw, World.Continentality);
		WriteBytes(Raw, World.LithologyId);
		EcrireGrottes(Raw, World.Caves);

		// Trois champs tres correles spatialement : zlib les reduit d'un facteur
		// 2 a 3. Sans compression, un monde de reference pese une centaine de Mo.
		int32 Bound = FCompression::CompressMemoryBound(NAME_Zlib, Raw.Num());
		TArray<uint8> Payload;
		Payload.SetNumUninitialized(Bound);
		if (!FCompression::CompressMemory(NAME_Zlib, Payload.GetData(), Bound,
			Raw.GetData(), Raw.Num()))
		{
			return false;
		}
		Payload.SetNum(Bound, EAllowShrinking::No);

		FHeader Header;
		Header.Magic = Magic;
		Header.FormatVersion = FormatVersion;
		Header.PipelineVersion = WORLDSEED_PIPELINE_VERSION;
		Header.RawSize = static_cast<uint32>(Raw.Num());
		Header.Seed = Seed;
		Header.NX = Geometry.NX;
		Header.NY = Geometry.NY;
		Header.HeightM = Geometry.HeightM;
		FCStringAnsi::Strncpy(Header.RulesHash, TCHAR_TO_ANSI(*RulesHash), HashChars);

		TArray<uint8> File;
		File.Reserve(sizeof(FHeader) + Payload.Num());
		File.Append(reinterpret_cast<const uint8*>(&Header), sizeof(FHeader));
		File.Append(Payload);

		const FString Path = PathForKey(Key);
		if (!FFileHelper::SaveArrayToFile(File, *Path))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Worldseed] cache non ecrit : %s"), *Path);
			return false;
		}

		UE_LOG(LogTemp, Log,
			TEXT("[Worldseed] monde mis en cache : %dx%d  %.1f Mo (brut %.1f Mo)  chaine v%d"),
			Geometry.NX, Geometry.NY, File.Num() / (1024.0f * 1024.0f),
			Raw.Num() / (1024.0f * 1024.0f), WORLDSEED_PIPELINE_VERSION);
		return true;
	}

	TArray<FWorldseedCacheEntry> ListEntries(const FString& CurrentRulesHash)
	{
		TArray<FWorldseedCacheEntry> Entries;

		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(CacheDir() / TEXT("*.wsw")), true, false);

		for (const FString& File : Files)
		{
			FHeader Header;
			int64 Size = 0;
			if (!ReadHeader(CacheDir() / File, Header, Size))
			{
				// Fichier illisible : on le signale comme incompatible pour qu'il
				// puisse etre nettoye.
				FWorldseedCacheEntry Broken;
				Broken.FileName = File;
				Broken.SizeBytes = Size;
				Broken.bCompatible = false;
				Entries.Add(Broken);
				continue;
			}

			FWorldseedCacheEntry Entry;
			Entry.FileName = File;
			Entry.Seed = Header.Seed;
			Entry.NX = Header.NX;
			Entry.NY = Header.NY;
			Entry.HeightM = Header.HeightM;
			Entry.PipelineVersion = static_cast<int32>(Header.PipelineVersion);
			Entry.RulesHash = HashToString(Header.RulesHash);
			Entry.SizeBytes = Size;
			Entry.bCompatible =
				(Header.FormatVersion == FormatVersion)
				&& (Header.PipelineVersion == WORLDSEED_PIPELINE_VERSION)
				&& (CurrentRulesHash.IsEmpty() || Entry.RulesHash == CurrentRulesHash);

			Entries.Add(Entry);
		}

		return Entries;
	}

	int32 ClearObsolete(const FString& CurrentRulesHash)
	{
		int32 Removed = 0;
		for (const FWorldseedCacheEntry& Entry : ListEntries(CurrentRulesHash))
		{
			if (!Entry.bCompatible
				&& IFileManager::Get().Delete(*(CacheDir() / Entry.FileName)))
			{
				++Removed;
			}
		}

		UE_LOG(LogTemp, Log, TEXT("[Worldseed] %d monde(s) perime(s) supprime(s)"), Removed);
		return Removed;
	}

	int32 ClearAll()
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(CacheDir() / TEXT("*.wsw")), true, false);

		int32 Removed = 0;
		for (const FString& File : Files)
		{
			if (IFileManager::Get().Delete(*(CacheDir() / File)))
			{
				++Removed;
			}
		}

		UE_LOG(LogTemp, Log, TEXT("[Worldseed] cache vide : %d monde(s)"), Removed);
		return Removed;
	}

	int64 TotalSize()
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(CacheDir() / TEXT("*.wsw")), true, false);

		int64 Total = 0;
		for (const FString& File : Files)
		{
			Total += IFileManager::Get().FileSize(*(CacheDir() / File));
		}
		return Total;
	}
}
