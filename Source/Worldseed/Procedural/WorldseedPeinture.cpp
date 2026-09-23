#include "Procedural/WorldseedPeinture.h"

#include "Procedural/WorldseedBiomes.h"
#include "Procedural/WorldseedDensity.h"
#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedTrace.h"
#include "Procedural/WorldseedVoxelChunk.h"

const TCHAR* WorldseedPeinture::NomDeCause(ECause C)
{
	switch (C)
	{
	case ECause::Repli:   return TEXT("repli");
	case ECause::Biome:   return TEXT("biome");
	case ECause::Roche2D: return TEXT("roche 2D");
	default:              return TEXT("banc");
	}
}

FLinearColor WorldseedPeinture::Aplat(ECause C, int32 Banc, int32 NbBancs)
{
	switch (C)
	{
	case ECause::Repli:   return FLinearColor(1.0f, 0.0f, 1.0f);    // magenta
	case ECause::Biome:   return FLinearColor(0.10f, 0.85f, 0.20f); // vert
	case ECause::Roche2D: return FLinearColor(0.15f, 0.45f, 1.00f); // bleu
	default:
		// UNE TEINTE VIVE PAR BANC, et le tour de roue les separe : dix bancs
		// sur 255 de saturation donnent dix couleurs qu'on distingue a l'oeil,
		// ce qu'un degrade de gris ne ferait pas.
		return FLinearColor::MakeFromHSV8(
			static_cast<uint8>((Banc * 255) / FMath::Max(1, NbBancs)),
			200, 255);
	}
}

void WorldseedPeinture::Sommets(FWorldseedVoxelMesh& Mesh,
	const FWorldseedPeintureContexte& C, FWorldseedPeintureReleve& Releve)
{
	WORLDSEED_TRACE(PeindreSommets);

	const int32 Count = Mesh.Positions.Num();
	Mesh.Colours.SetNumUninitialized(Count);

	// LE SEUIL DE NOIR VIENT DE LA MESURE A L'IMAGE, PAS D'UNE INTUITION.
	// Soixante-dix sur 255 en sRGB est le seuil que le script de comparaison
	// emploie sur les captures ; le convertir par la table sRGB -- la meme que
	// le catalogue de roches -- le rend exactement comparable.
	static const float SeuilSombre =
		FLinearColor(FColor(70, 70, 70, 255)).GetLuminance();

	// LA CARTE EST PRISE UNE FOIS, PAS PAR SOMMET. L'accesseur passe par le
	// monde partage : un test de validite et un dereferencement, donc presque
	// rien -- mais cette boucle tourne sur 1,67 million de sommets, et la lier
	// une fois dit aussi ce qui est vrai : la carte ne change pas pendant la
	// peinture.
	const FWorldseedBiomeMap& Carte = C.Biomes;

	const bool bHasBiomes = (Carte.Index.Num() == C.Geo.CellCount());
	const bool bHasCover = (Carte.Cover.Num() == C.Geo.CellCount());
	const bool bAvecRoche = C.Litho.IsValid(C.Geo.CellCount())
		&& C.CouleurParRoche.Num() > 0;

	const double WidthM = C.Geo.WidthM();
	const double HeightM = C.Geo.HeightM;

	for (int32 I = 0; I < Count; ++I)
	{
		// LA BRANCHE QUI DECIDE EST SUIVIE JUSQU'AU BOUT. Sans elle, un sommet
		// sombre ne dit pas D'OU il vient, et l'on remonte la piste a l'envers.
		WorldseedPeinture::ECause Cause = WorldseedPeinture::ECause::Biome;
		int32 BancPeint = 0;

		if (!bHasBiomes)
		{
			Cause = WorldseedPeinture::ECause::Repli;
			const FLinearColor Repli = C.bCarteDesCauses
				? WorldseedPeinture::Aplat(Cause, 0, 1) : FLinearColor::Gray;
			Mesh.Colours[I] = Repli;
			++Releve.ParCause[static_cast<int32>(Cause)];
			if (Repli.GetLuminance() < SeuilSombre)
			{
				++Releve.SombresEcrits;
				++Releve.SombresParCause[static_cast<int32>(Cause)];
			}
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
			FMath::FloorToInt(U * C.Geo.NX), 0, C.Geo.NX - 1);
		const int32 Row = FMath::Clamp(
			FMath::FloorToInt(V * C.Geo.NY), 0, C.Geo.NY - 1);

		// LA COULEUR SUIT LE SUBSTRAT QUAND IL Y EN A UN. Depuis que la roche
		// a nu et l'estran ont quitte l'axe des biomes, l'index porte le climat
		// meme sur une paroi : le lire seul peindrait la falaise en vert.
		const int32 Cell = Row * C.Geo.NX + Col;
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
		++Releve.Sommets;

		if (bAvecRoche && C.FonduRocheM > 0.0f)
		{
			const double Z = Mesh.Positions[I].Z / WorldseedMetersToCm;
			const double Profondeur = C.Champ.SurfaceHeightM(X, Y) - Z;

			Releve.ProfondeurSomme += Profondeur;
			Releve.ProfondeurMax = FMath::Max(Releve.ProfondeurMax, Profondeur);
			Releve.ProfondeurMin = FMath::Min(Releve.ProfondeurMin, Profondeur);

			if (Profondeur > 0.0)
			{
				++Releve.SousLaSurface;
				Cause = WorldseedPeinture::ECause::Roche2D;
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
				uint8 Id = C.Litho.Id[Cell];
				if (C.Strates.IsActive())
				{
					const float DureteSocle = C.DureteParId.IsValidIndex(Id)
						? C.DureteParId[Id] : 1.0f;
					if (DureteSocle >= C.Strates.SocleHardnessMin
						&& DureteSocle <= C.Strates.SocleHardnessMax)
					{
						++Releve.SerieActive;

						const int32 Banc = WorldseedStrata::BancAt(
							X, Y, Z, C.Strates, C.Seed);
						if (C.Strates.Serie.IsValidIndex(Banc))
						{
							Id = C.Strates.Serie[Banc].RockId;
							BancPeint = Banc;
							Cause = WorldseedPeinture::ECause::Banc;

							if (Releve.ParBanc.Num() < C.Strates.Serie.Num())
							{
								Releve.ParBanc.SetNumZeroed(C.Strates.Serie.Num());
							}
							++Releve.ParBanc[Banc];
						}
					}
				}
				if (C.CouleurParRoche.IsValidIndex(Id))
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
					const double Marge = static_cast<double>(C.MargeDeplacementM);

					const float T = FMath::Clamp(
						static_cast<float>(Profondeur - Marge) / C.FonduRocheM,
						0.0f, 1.0f);
					if (T > 0.01f) { ++Releve.Teintee; }
					else
					{
						// LE FONDU NE MORD PAS ENCORE : ce sommet est peint en
						// BIOME, quoi qu'en dise la branche qu'on a traversee.
						// Compter la branche PARCOURUE plutot que celle qui a
						// ECRIT donnerait un entonnoir juste et une carte des
						// causes fausse.
						Cause = WorldseedPeinture::ECause::Biome;
					}
					Teinte = FMath::Lerp(Teinte, C.CouleurParRoche[Id], T);
				}
			}
		}

		// --- L'HISTOGRAMME PORTE SUR CE QUI EST ECRIT --------------------
		//
		// On mesure la couleur FINALE, apres toutes les branches, et jamais
		// ce qu'on croit avoir mis. C'est la seule facon de repondre a la
		// question posee : la peinture ecrit-elle du noir, oui ou non ?
		const double Lum = static_cast<double>(Teinte.GetLuminance());
		Releve.LumMin = FMath::Min(Releve.LumMin, Lum);
		Releve.LumMax = FMath::Max(Releve.LumMax, Lum);
		++Releve.ParCause[static_cast<int32>(Cause)];
		if (Lum < SeuilSombre)
		{
			++Releve.SombresEcrits;
			++Releve.SombresParCause[static_cast<int32>(Cause)];
		}

		if (C.bCarteDesCauses)
		{
			Teinte = WorldseedPeinture::Aplat(
				Cause, BancPeint, FMath::Max(1, C.Strates.Serie.Num()));
		}

		Mesh.Colours[I] = Teinte;
	}
}
