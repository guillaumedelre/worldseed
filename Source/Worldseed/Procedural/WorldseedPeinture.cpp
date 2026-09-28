#include "Procedural/WorldseedPeinture.h"

#include "Procedural/WorldseedApparence.h"
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

	// LES CANAUX DE TEINTE NE SE REMPLISSENT QUE SI QUELQU'UN LES LIT. En mode
	// couleur de biome, RGBA porte deja tout et deux tableaux de plus par chunk
	// seraient payes pour rien -- 1,67 million de sommets par releve.
	// LE MATERIAU NE LIT PAS UV0, ET C'EST MESURE -- donc on ne le remplit pas.
	//
	// Un placage planaire en XY a ete pose ici, puis retire. L'A/B qui a
	// tranche tenait en une surcharge : meme monde, meme depart, ciel fige,
	// seul UV0 mis a zero. Sur la paroi carrelee, 0,58 % de pixels changes et
	// 3,67/255 d'ecart moyen -- rien. Et surtout les stries verticales comme le
	// carrelage rectangulaire sont presents DES DEUX COTES, a la meme place et
	// de la meme forme : `M_WorldseedGround` carrele par la POSITION DU MONDE,
	// pas par les coordonnees du maillage.
	//
	// Ces stries sont donc ANTERIEURES a ce chantier et restent a corriger --
	// dans le MATERIAU, par une projection triplanaire : toute projection
	// planaire etire la texture a l'infini sur une paroi verticale, puisque les
	// deux coordonnees n'y bougent pas pendant que Z descend.
	//
	// `ProceduralMeshComponent.cpp:573` autorise ce vide : chaque canal est
	// rempli si sa taille vaut le nombre de sommets, et vaut zero sinon. UV1 et
	// UV2 portent donc la teinte meme avec UV0 absent.
	if (C.bPoidsDeMatiere)
	{
		Mesh.TintRG.SetNumUninitialized(Count);
		Mesh.TintB.SetNumUninitialized(Count);
		// UV3.Y PORTE LA PART DE NEIGE PERMANENTE, que DLWE lit par son entree
		// `Offset Coverage`. Le canal n'existe qu'en mode pack de textures : en
		// couleur de biome le materiau lit tout dans RGBA et n'a que faire
		// d'une neige, la calotte y etant deja peinte en blanc.
		Mesh.Neige.SetNumUninitialized(Count);
	}
	else
	{
		Mesh.TintRG.Reset();
		Mesh.TintB.Reset();
		Mesh.Neige.Reset();
	}

	// CE QU'UNE PAROI PORTE QUAND LE SOL EST HABILLE PAR UN PACK : de la
	// roche, pleine. La couleur de la roche part alors dans la TEINTE, si bien
	// que calcaire creme et basalte sombre restent distincts sans qu'aucune
	// herbe ne vienne se melanger a une falaise.
	static const FLinearColor PoidsRoche(0.0f, 0.0f, 1.0f, 0.0f);

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
			if (C.bPoidsDeMatiere)
			{
				// SANS CARTE DES BIOMES, ON NE SAIT RIEN : de la roche, sans
				// teinte. C'est le gris de secours transpose au mode pack.
				Mesh.Colours[I] = PoidsRoche;
				Mesh.TintRG[I] = FVector2D(1.0, 1.0);
				Mesh.TintB[I] = FVector2D(1.0, 0.0);
				Mesh.Neige[I] = FVector2D(0.0, 0.0);
			}
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

		// --- LA FRONTIERE DE DEUX BIOMES NE DOIT PAS SUIVRE UN TRIANGLE ------
		//
		// SIGNALE EN JEU : « on voit des coupures polygonales, comme si le sol
		// portait encore la teinte du biome ». C'etait exact, et la cause est
		// une DIFFERENCE D'ECHELLE. La carte des biomes a une maille de 15,6 m
		// et se lisait au PLUS PROCHE VOISIN ; le maillage voxel, lui, pose un
		// sommet au metre. Deux sommets voisins tombant de part et d'autre
		// d'une limite de cellule recevaient donc deux teintes franchement
		// differentes, et la transition se faisait sur UNE arete de triangle --
		// d'ou un escalier qui epouse la triangulation au lieu du terrain.
		//
		// ON N'INTERPOLE JAMAIS UN IDENTIFIANT. La regle est ecrite dans ce
		// depot et elle a ete payee : un filtre bilineaire sur `biome_index`
		// rendait des biomes qui n'existent pas la, 307 points faux sur 17956.
		// `Biome` reste donc celui de la cellule la plus proche -- c'est lui qui
		// nomme la cause et le banc. Ce qu'on melange, ce sont les GRANDEURS
		// CONTINUES qui en derivent : la couleur et les quatre poids de
		// matiere. Les melanger est licite, les moyenner a du sens, et la
		// transition s'etale alors sur toute la maille au lieu d'un triangle.
		//
		// L'OCEAN EST ECARTE DU MELANGE, et il le faut : un sommet de rivage a
		// des voisins marins, et les inclure tirerait la teinte du sable vers
		// le bleu sur toute la cote. Meme parade que la grille de sol du semis,
		// qui saute ses cases vides et renormalise.
		FLinearColor Teinte = WorldseedBiomes::Colour(Biome);
		FLinearColor PoidsBiome = WorldseedBiomes::SlotWeights(Biome);

		// LA PART D'ESTRAN SUIT LE MEME CHEMIN QUE LA TEINTE ET LES POIDS, et
		// il le faut : elle est une grandeur CONTINUE derivee du biome, donc
		// elle se moyenne. La laisser franche pendant que les poids se lissent
		// ferait sauter la texture de sable la ou la matiere, elle, change en
		// douceur -- soit exactement l'escalier que le melange ci-dessous
		// corrige, transpose d'un canal a l'autre.
		float Estran = WorldseedApparence::PartEstran(Biome);

		// LA NEIGE SE MOYENNE POUR LA MEME RAISON QUE L'ESTRAN. Laissee
		// franche pendant que les poids de matiere se lissent, elle ferait
		// sauter le manteau blanc d'une cellule a l'autre au bord de la
		// calotte -- le meme escalier, transpose d'un canal a l'autre.
		float Neige = WorldseedApparence::PartNeige(Biome);

		if (C.bMelangerLesBiomes)
		{
			const double FX = U * C.Geo.NX - 0.5;
			const double FY = V * C.Geo.NY - 0.5;
			const int32 X0 = FMath::FloorToInt(FX);
			const int32 Y0 = FMath::FloorToInt(FY);
			const double TX = FX - X0;
			const double TY = FY - Y0;

			FLinearColor SommeT(0.0f, 0.0f, 0.0f, 0.0f);
			FLinearColor SommeP(0.0f, 0.0f, 0.0f, 0.0f);
			double SommeE = 0.0;
			double SommeN = 0.0;
			double SommePoids = 0.0;

			for (int32 DY = 0; DY <= 1; ++DY)
			{
				for (int32 DX = 0; DX <= 1; ++DX)
				{
					// LA LONGITUDE BOUCLE, LA LATITUDE NON. Un modulo sur X et
					// une borne sur Y : c'est la meme convention que la lecture
					// principale, et s'en ecarter ferait apparaitre une couture
					// au meridien de bordure.
					const int32 CX = ((X0 + DX) % C.Geo.NX + C.Geo.NX) % C.Geo.NX;
					const int32 CY = FMath::Clamp(Y0 + DY, 0, C.Geo.NY - 1);
					const int32 CI = CY * C.Geo.NX + CX;

					const EWorldseedBiome BV = bHasCover
						? WorldseedBiomes::AppearanceBiome(Carte.Index[CI], Carte.Cover[CI])
						: static_cast<EWorldseedBiome>(Carte.Index[CI]);
					if (BV == EWorldseedBiome::Ocean)
					{
						continue;
					}

					const double Poids =
						(DX ? TX : 1.0 - TX) * (DY ? TY : 1.0 - TY);
					if (Poids <= 0.0)
					{
						continue;
					}

					SommeT += WorldseedBiomes::Colour(BV) * static_cast<float>(Poids);
					SommeP += WorldseedBiomes::SlotWeights(BV) * static_cast<float>(Poids);
					SommeE += WorldseedApparence::PartEstran(BV) * Poids;
					SommeN += WorldseedApparence::PartNeige(BV) * Poids;
					SommePoids += Poids;
				}
			}

			// TOUS LES VOISINS MARINS, OU UN SOMMET PILE SUR UN CENTRE DE
			// CELLULE : on garde ce que la lecture au plus proche a deja rendu.
			if (SommePoids > UE_DOUBLE_SMALL_NUMBER)
			{
				const float Inv = static_cast<float>(1.0 / SommePoids);
				Teinte = SommeT * Inv;
				PoidsBiome = SommeP * Inv;
				Estran = static_cast<float>(SommeE) * Inv;
				Neige = static_cast<float>(SommeN) * Inv;
			}
		}

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

		if (C.bPoidsDeMatiere)
		{
			// LA MATIERE SUIT LA CAUSE, ET C'EST LE MEME ARBITRAGE QUE POUR LA
			// COULEUR. Au-dessus du sol c'est le biome qui dit de quoi la
			// surface est faite ; sous terre, c'est de la roche, quelle que
			// soit la prairie qui pousse au-dessus. Lire le biome sous terre
			// couvrirait d'herbe la paroi d'une grotte -- exactement le defaut
			// que la couleur a deja corrige plus haut.
			Mesh.Colours[I] = (Cause == WorldseedPeinture::ECause::Biome)
				? PoidsBiome
				: PoidsRoche;

			// ET LA COULEUR DEVIENT LA TEINTE. Rien n'est perdu : ce que la
			// branche precedente a calcule -- couleur de biome, de roche ou de
			// banc -- part dans les canaux de teinte, ou le materiau la
			// multiplie a la texture melangee. C'est ce qui garde un calcaire
			// creme distinct d'un basalte sombre sur la meme texture de roche.
			const FLinearColor Normalisee =
				WorldseedApparence::TeinteNormalisee(Teinte);
			// UV2.Y PORTE LA PART D'ESTRAN, ET SEULEMENT AU-DESSUS DU SOL.
			// Meme arbitrage que la ligne du dessus : sous terre c'est de la
			// roche, et une paroi de grotte creusee sous une plage n'est pas
			// du sable de plage. Lire l'estran sous terre poserait la texture
			// du rivage au plafond d'une galerie.
			Mesh.TintRG[I] = FVector2D(Normalisee.R, Normalisee.G);
			Mesh.TintB[I] = FVector2D(Normalisee.B,
				(Cause == WorldseedPeinture::ECause::Biome) ? Estran : 0.0f);
			// LA NEIGE SUIT LA MEME REGLE QUE L'ESTRAN, ET IL LE FAUT : sous
			// terre c'est de la roche. Une galerie creusee sous la calotte
			// glaciaire n'a pas de neige a son plafond, pas plus qu'une grotte
			// sous une plage n'a de sable de rivage.
			Mesh.Neige[I] = FVector2D(0.0,
				(Cause == WorldseedPeinture::ECause::Biome) ? Neige : 0.0f);
			continue;
		}

		Mesh.Colours[I] = Teinte;
	}
}
