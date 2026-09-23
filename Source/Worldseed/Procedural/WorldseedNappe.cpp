#include "Procedural/WorldseedNappe.h"

#include "Procedural/WorldseedGrid.h"
#include "Procedural/WorldseedTrace.h"
#include "Procedural/WorldseedRules.h"
#include "Procedural/WorldseedWorldData.h"

void WorldseedNappe::Batir(const FWorldseedWorldData& Monde,
	const FWorldseedGeometry& Geo, const TArray<float>& Heights,
	const FWorldseedSurfaceRegles& ReglesSurf,
	const FWorldseedAppearance& Mode, const FWorldseedNappeRegles& R,
	FWorldseedNappeMaillage& Out, FWorldseedNappeReleve& Releve)
{
	WORLDSEED_TRACE(SolDeFond);

	// LES TROIS GRANDEURS METRIQUES ARRIVENT RESOLUES. L'appelant a confronte
	// ses reglages au champ de densite -- le retrait doit couvrir le
	// deplacement que le voxel applique a la surface, l'enfoncement doit
	// passer sous tout ce qu'il peut creuser -- et la nappe n'a pas a relire
	// les regles pour cela. Une seule source, pas deux copies qui divergent.
	const float DropCm = R.RetraitM * WorldseedMetersToCm * R.ExagerationZ;
	const double SurEnfoncementM = R.SurEnfoncementM;
	const double MargeMerM = R.MargeMerM;

	// Jamais plus fin que la grille elle-meme : au-dela on interpolerait du
	// detail qui n'existe pas.
	const int32 CountX = FMath::Clamp(R.Largeur, 32, Geo.NX);
	// La hauteur suit la FORME DE LA GRILLE, et non un demi arbitraire : le
	// monde n'est pas toujours deux fois plus large que haut, et un maillage
	// qui ne respecterait pas son rapport donnerait des mailles etirees.
	const int32 CountY = FMath::Clamp(
		FMath::RoundToInt(CountX * static_cast<float>(Geo.NY)
			/ static_cast<float>(FMath::Max(Geo.NX, 1))),
		16, Geo.NY);

	const float CellCm = Geo.MetersPerPixel() * WorldseedMetersToCm;
	const float OriginX = -Geo.WidthM() * WorldseedMetersToCm * 0.5f;
	const float OriginY = -Geo.HeightM * WorldseedMetersToCm * 0.5f;
	// --- LE RETRAIT SUIT LE CHAMP, PAS UN NOMBRE EN DUR --------------------
	//
	// LA PREMISSE DU REGLAGE ETAIT FAUSSE. Il valait UN METRE, avec ce motif :
	// « les deux maillages decrivent le meme relief, sans ce retrait ils se
	// disputeraient le meme plan et scintilleraient ». Or ils ne decrivent PAS
	// le meme relief : la nappe est batie sur la grille macro, le terrain voxel
	// maille l'isovaleur zero du CHAMP, qui ajoute a cette grille un
	// deplacement vertical 3D -- jusqu'a overhangAmplitudeM plus
	// detailAmplitudeM vers le bas.
	//
	// MESURE, graine 20260909, 10 637 colonnes de terre : la nappe flotte
	// AU-DESSUS du sol reel sur 38,8 % d'entre elles. Ecart moyen 0,44 m,
	// mediane 0,50, p90 2,50, p99 7,00. Un metre couvrait donc la mediane et
	// rien de plus, et le joueur -- qui marche sur le voxel et traverse la
	// nappe, laquelle est dessinee mais sans collision -- paraissait enfonce
	// dedans jusqu'a la taille. Signale sur capture par le proprietaire.
	//
	// C'est exactement l'angle mort que `archeMargeSommetM` corrige deja
	// ailleurs : « le champ de densite deplace la surface et la passe des
	// cavites ne le sait pas ». La nappe avait la meme cecite.
	//
	// On lit donc les deux amplitudes plutot que d'ecrire douze : un reglage
	// qui change ne doit pas laisser ce retrait en arriere.

	const int32 Verts = CountX * CountY;

	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FVector2D> TintRG;
	TArray<FVector2D> TintB;
	TArray<FProcMeshTangent> Tangents;
	TArray<FLinearColor> Colors;
	TArray<int32> Triangles;

	Vertices.SetNumUninitialized(Verts);
	Normals.SetNumUninitialized(Verts);
	UVs.SetNumUninitialized(Verts);
	TintRG.SetNumUninitialized(Verts);
	TintB.SetNumUninitialized(Verts);
	Tangents.SetNumUninitialized(Verts);
	Colors.SetNumUninitialized(Verts);


	// COMPTER CE QU'ON PEINT. Une couleur qui ne se voit pas a deux causes
	// opposees -- le terme ne s'evalue jamais, ou il s'evalue et rien ne
	// l'affiche -- et elles n'appellent pas du tout le meme remede. Le compte
	// separe les deux avant meme de regarder l'image.
	int32 SommetsSousZero = 0;

	// --- CE QUE L'ENFONCEMENT DE LA NAPPE VUE VA NOYER -------------------
	//
	// LE CALCUL EST HISSE ICI POUR AVOIR UN SEUL PROPRIETAIRE. Il servait plus
	// bas a enfoncer la nappe de l'IMAGE ; il sert aussi a compter ce que cet
	// enfoncement coute, et deux copies d'un meme seuil divergent a la premiere
	// retouche.
	//
	// POURQUOI CE COMPTE EXISTE. La nappe vue passe sous la bande creusable
	// pour ne boucher aucune cavite -- cent vingt-cinq metres. Mais elle porte
	// le relief du monde ENTIER, et tout ce qui culmine sous cette valeur passe
	// alors SOUS LE NIVEAU DE LA MER : l'ocean, qui est un plan a l'altitude
	// zero, le recouvre. Au-dela du rayon de chargement, une plaine cotiere ou
	// une vallee basse ne se lit donc plus comme une terre un peu affaissee,
	// elle DISPARAIT sous l'eau. Vu depuis un sommet, une vallee verte devient
	// une baie.
	//
	// C'est le vrai cout de l'enfoncement, et il ne se voyait nulle part : le
	// registre ne parlait que d'« une marche de 125 m ».

	int32 SommetsEmerges = 0;
	int32 SommetsNoyesParLaVue = 0;

	// ET LA COURBE, PAS SEULEMENT LE POINT. « 16 % des terres noyees a 125 m »
	// ne dit pas ce qu'on regagnerait en enfoncant moins : la distribution des
	// altitudes basses n'est pas uniforme, et c'est elle qui decide si ramener
	// l'enfoncement a quatre-vingts metres rend beaucoup ou presque rien.
	constexpr int32 NbPaliers = 6;
	constexpr double Paliers[NbPaliers] = { 25.0, 50.0, 75.0, 100.0, 125.0, 150.0 };
	int32 ParPalier[NbPaliers] = {};

	// LE MARQUAGE VOYAGE AVEC LE SOMMET, il ne se rededuit pas. La nappe vue
	// est ENFONCEE sous le niveau de la mer, donc son Z ne dit plus rien de
	// l'altitude reelle du terrain : relire « ce sommet est-il submerge » sur
	// la geometrie decimee donnerait « tout est submerge ». On retient donc le
	// verdict ici, ou `HereM` est l'altitude vraie.
	TArray<uint8> SousZero;
	SousZero.SetNumUninitialized(Verts);

	// LE PAS SE PREND EN REEL, ET LE DERNIER SOMMET TOMBE SUR LA DERNIERE
	// CELLULE.
	//
	// Un pas ENTIER tronque le sol de fond sans le dire : NX / CountX vaut 1
	// des que la grille fait moins du double de la largeur demandee, et le
	// maillage s'arrete alors a la colonne CountX au lieu de NX. A 512 sommets
	// sur une grille de 1000, il ne couvrait plus que la moitie de la carte —
	// le paysage se terminait par une arete parfaitement droite a vingt metres
	// du joueur, et le ciel commencait derriere.
	const float StepX = (CountX > 1)
		? static_cast<float>(Geo.NX - 1) / static_cast<float>(CountX - 1) : 0.0f;
	const float StepY = (CountY > 1)
		? static_cast<float>(Geo.NY - 1) / static_cast<float>(CountY - 1) : 0.0f;

	// Voisins pour la normale : a un pas du sol de fond, pas a une cellule.
	const int32 NeighbourX = FMath::Max(FMath::RoundToInt(StepX), 1);
	const int32 NeighbourY = FMath::Max(FMath::RoundToInt(StepY), 1);

	// LE SOL DE FOND DOIT PASSER SOUS LE RELIEF, PAS AU TRAVERS.
	//
	// Un sommet pris au POINT ne dit rien de ce qui se passe entre deux
	// sommets. Sur cette carte l'ecart entre eux atteint trente metres : le
	// maillage y tend une corde droite, qui monte au-dessus du terrain dans le
	// moindre creux. Les chunks detailles le traversent alors par en dessous,
	// et l'oeil voit deux reliefs superposes — exactement ce qu'on croit etre
	// un bug de maillage. L'enfoncement d'un metre n'y peut rien : il est deux
	// ordres de grandeur trop petit.
	//
	// On prend donc le MINIMUM sur l'empreinte du sommet. Une corde tendue
	// entre deux minima passe sous le relief au lieu de le couper, et
	// l'enfoncement ne sert plus que de marge.
	const int32 FootX = FMath::Max(NeighbourX / 2, 1);
	const int32 FootY = FMath::Max(NeighbourY / 2, 1);

	auto FloorAt = [&](int32 CX, int32 CY) -> float
	{
		const int32 X0 = FMath::Max(CX - FootX, 0);
		const int32 X1 = FMath::Min(CX + FootX, Geo.NX - 1);
		const int32 Y0 = FMath::Max(CY - FootY, 0);
		const int32 Y1 = FMath::Min(CY + FootY, Geo.NY - 1);

		float Lowest = TNumericLimits<float>::Max();
		for (int32 Row = Y0; Row <= Y1; ++Row)
		{
			const int32 Base = Row * Geo.NX;
			for (int32 Col = X0; Col <= X1; ++Col)
			{
				Lowest = FMath::Min(Lowest, Heights[Base + Col]);
			}
		}
		return Lowest;
	};

	// De combien le point depassait-il son empreinte ? C'est la hauteur dont le
	// sol de fond sortait du relief, et elle n'avait aucune raison d'etre
	// visible autrement qu'a l'ecran.
	float WorstOvershootM = 0.0f;

	for (int32 Y = 0; Y < CountY; ++Y)
	{
		const int32 SY = FMath::Min(FMath::RoundToInt(Y * StepY), Geo.NY - 1);
		for (int32 X = 0; X < CountX; ++X)
		{
			const int32 SX = FMath::Min(FMath::RoundToInt(X * StepX), Geo.NX - 1);
			const int32 Index = Y * CountX + X;
			const int32 Cell = SY * Geo.NX + SX;

			const float HereM = Heights[Cell];

			// La GEOMETRIE prend le plancher ; l'APPARENCE garde la hauteur du
			// point, qui est celle du biome reellement present ici.
			const float FloorM = FloorAt(SX, SY);
			WorstOvershootM = FMath::Max(WorstOvershootM, HereM - FloorM);

			Vertices[Index] = FVector(
				OriginX + SX * CellCm,
				OriginY + SY * CellCm,
				FloorM * WorldseedMetersToCm * R.ExagerationZ - DropCm);


			// Normale au PAS DU SOL DE FOND, pas a celui de la grille : prise
			// sur des voisins immediats, elle porterait un detail que ce
			// maillage ne represente pas, et l'ombrage jurerait avec sa forme.
			const int32 PX = FMath::Min(SX + NeighbourX, Geo.NX - 1);
			const int32 MX = FMath::Max(SX - NeighbourX, 0);
			const int32 PY = FMath::Min(SY + NeighbourY, Geo.NY - 1);
			const int32 MY = FMath::Max(SY - NeighbourY, 0);

			const float SpanX = FMath::Max((PX - MX), 1) * Geo.MetersPerPixel();
			const float SpanY = FMath::Max((PY - MY), 1) * Geo.MetersPerPixel();

			const float DZDX =
				(Heights[SY * Geo.NX + PX] - Heights[SY * Geo.NX + MX]) / SpanX;
			const float DZDY =
				(Heights[PY * Geo.NX + SX] - Heights[MY * Geo.NX + SX]) / SpanY;

			const FVector N = FVector(-DZDX, -DZDY, 1.0f).GetSafeNormal();
			Normals[Index] = N;
			Tangents[Index] = FProcMeshTangent(
				FVector(1.0f, 0.0f, DZDX).GetSafeNormal(), false);

			UVs[Index] = FVector2D(
				static_cast<float>(SX) / static_cast<float>(Geo.NX),
				static_cast<float>(SY) / static_cast<float>(Geo.NY));

			WorldseedApparence::Sommet(Monde, ReglesSurf, Cell, HereM, N,
				Mode, Colors[Index], TintRG[Index], TintB[Index]);

			SousZero[Index] = (HereM < 0.0f) ? 1 : 0;
			if (HereM < 0.0f) { ++SommetsSousZero; }
			else
			{
				++SommetsEmerges;
				if (HereM < SurEnfoncementM) { ++SommetsNoyesParLaVue; }
				for (int32 P = 0; P < NbPaliers; ++P)
				{
					if (HereM < Paliers[P]) { ++ParPalier[P]; }
				}
			}
		}
	}

	// LE MODULE MESURE, L APPELANT RAPPORTE. Les trois comptes partent dans
	// `Releve` et c est l acteur qui les journalise : une passe qui ecrit
	// elle-meme au journal le fait DEUX fois des qu on la reutilise, et c est
	// arrive ici a la premiere execution apres l extraction.
	Triangles.Reserve((CountX - 1) * (CountY - 1) * 6);
	for (int32 Y = 0; Y < CountY - 1; ++Y)
	{
		for (int32 X = 0; X < CountX - 1; ++X)
		{
			const int32 A = Y * CountX + X;
			const int32 C = A + CountX;

			if (R.bInverserEnroulement)
			{
				Triangles.Add(A); Triangles.Add(A + 1); Triangles.Add(C);
				Triangles.Add(A + 1); Triangles.Add(C + 1); Triangles.Add(C);
			}
			else
			{
				Triangles.Add(A); Triangles.Add(C); Triangles.Add(A + 1);
				Triangles.Add(A + 1); Triangles.Add(C); Triangles.Add(C + 1);
			}
		}
	}

	Out.CountX = CountX;
	Out.CountY = CountY;
	Out.Positions = MoveTemp(Vertices);
	Out.Normales = MoveTemp(Normals);
	Out.UV0 = MoveTemp(UVs);
	Out.TeinteRG = MoveTemp(TintRG);
	Out.TeinteB = MoveTemp(TintB);
	Out.Tangentes = MoveTemp(Tangents);
	Out.Couleurs = MoveTemp(Colors);
	Out.Triangles = MoveTemp(Triangles);
	Out.SousZero = MoveTemp(SousZero);

	Releve.Sommets = Verts;
	Releve.SommetsSousZero = SommetsSousZero;
	Releve.SommetsEmerges = SommetsEmerges;
	Releve.SommetsNoyesParLaVue = SommetsNoyesParLaVue;
	Releve.PireDepassementM = WorstOvershootM;
	for (int32 P = 0; P < FWorldseedNappeReleve::NbPaliers; ++P)
	{
		Releve.ParPalier[P] = ParPalier[P];
	}
}

void WorldseedNappe::Decimer(const FWorldseedNappeMaillage& Source,
	const FWorldseedNappeRegles& R, FWorldseedNappeMaillage& Out,
	TArray<FVector2D>& OutPlafondCm,
	TArray<int32>& OutTrianglesTerre, TArray<int32>& OutTrianglesMer)
{
	WORLDSEED_TRACE(NappeVue);

	// Le meme retrait que la nappe pleine : on remonte les sommets a leur
	// altitude vraie pour calculer leur plafond, puis on les redescend.
	const float DropCm = R.RetraitM * WorldseedMetersToCm * R.ExagerationZ;

		const int32 Pas = FMath::Clamp(R.Pas, 1, 8);
		const int32 HX = (Source.CountX - 1) / Pas + 1;
		const int32 HY = (Source.CountY - 1) / Pas + 1;
		const int32 Verts = HX * HY;

		// --- LE DECOR DOIT PASSER SOUS TOUT CE QUE LE VOXEL PEUT CREUSER ----
		//
		// L'ENFONCEMENT NE SE CHOISIT PAS, IL SE DEDUIT. Le terrain voxel ne
		// creuse que dans une bande de `bandeM` sous la surface : rien, ni
		// galerie, ni chambre, ni arche, ni gouffre, n'existe plus bas. Un
		// decor de fond pose SOUS cette bande ne peut donc boucher aucune
		// ouverture, ou qu'on soit.
		//
		// ET C'EST CE QUI FAIT DISPARAITRE L'APPARITION BRUTALE. Signale en
		// jeu : « quand je sors de l'arche le paysage en fond apparait d'un
		// coup ». C'etait la bascule qui se voyait enfin -- avant, le gel de
		// 520 ms la masquait. Sous la bande, la nappe vue n'est visible de
		// NULLE PART ou l'on pourrait se trouver sous un plafond : la cacher
		// puis la rendre ne change plus rien a l'image, et il n'y a plus rien
		// a faire apparaitre.
		//
		// La bascule est GARDEE malgre tout -- elle ne coute rien et reste un
		// filet si un creusement futur sortait de la bande.
		//
		// LA VALEUR EST CALCULEE PLUS HAUT, avec le compte des terres qu'elle
		// noie : c'est la meme grandeur, et la recopier ici l'aurait fait
		// diverger de ce que le releve annonce.
		//
		// `-WorldseedNappeVue=` la surcharge, parce que c'est un arbitrage A
		// L'IMAGE. Plus le decor est bas, moins il bouche -- mais plus il noie
		// de terres basses. Aucun calcul ne tranche cela, et un A/B qui
		// demanderait de rouvrir le fichier de regles en changerait
		// l'empreinte, donc regenererait le monde entre les deux moities.

		// L'ENFONCEMENT SUIT L'EXAGERATION VERTICALE, comme celui de la nappe :
		// un monde etire verticalement etire aussi la hauteur des ouvertures
		// qu'il s'agit de ne plus boucher. Il s'applique SOMMET PAR SOMMET
		// depuis que la mer le plafonne -- voir la boucle ci-dessous.

		TArray<FVector> Positions;
		TArray<FVector> Normales;
		TArray<FVector2D> UV0;
		TArray<FVector2D> TeinteRG;
		TArray<FVector2D> TeinteB;
		TArray<FVector2D> PlafondCm;
		TArray<FProcMeshTangent> Tangentes;
		TArray<FLinearColor> Couleurs;
		TArray<int32> Triangles;

		Positions.SetNumUninitialized(Verts);
		Normales.SetNumUninitialized(Verts);
		UV0.SetNumUninitialized(Verts);
		TeinteRG.SetNumUninitialized(Verts);
		TeinteB.SetNumUninitialized(Verts);
		PlafondCm.SetNumUninitialized(Verts);
		Tangentes.SetNumUninitialized(Verts);
		Couleurs.SetNumUninitialized(Verts);

		TArray<uint8> SousZero;
		SousZero.SetNumUninitialized(Verts);

		for (int32 Y = 0; Y < HY; ++Y)
		{
			// LE DERNIER SOMMET TOMBE SUR LE DERNIER, et pas sur un multiple
			// du pas : sans ce bornage la nappe vue s'arreterait avant le bord
			// du monde et le paysage se terminerait par une arete droite --
			// defaut deja rencontre sur la nappe elle-meme.
			const int32 SY = FMath::Min(Y * Pas, Source.CountY - 1);
			for (int32 X = 0; X < HX; ++X)
			{
				const int32 SX = FMath::Min(X * Pas, Source.CountX - 1);
				const int32 Src = SY * Source.CountX + SX;
				const int32 Dst = Y * HX + X;

				Positions[Dst] = Source.Positions[Src];

				// --- LE SOMMET RESTE A SON ALTITUDE, IL PORTE SON PLAFOND ---
				//
				// L'ENFONCEMENT NE SE CUIT PLUS DANS LA GEOMETRIE. Il depend
				// desormais de la distance a la CAMERA -- entier pres du
				// joueur, nul au loin -- donc il change a chaque image et ne
				// peut pas vivre dans des sommets qu'on ne rebatit jamais. Il
				// est applique par deplacement de sommets dans le materiau, et
				// ce canal lui porte la seule chose que le materiau ne sait pas
				// calculer : de combien CE sommet a le droit de descendre.
				//
				// --- L'ENFONCEMENT NE CREUSE JAMAIS SOUS LA MER -------------
				//
				// SANS CE PLAFOND, IL NOIE UN SIXIEME DES TERRES. Mesure sur le
				// monde de reference : 398 839 sommets de terre sur 2 449 474,
				// soit 16,3 %, passaient sous l'altitude zero -- et l'ocean,
				// qui est un plan a zero, les recouvrait. Au-dela du rayon de
				// chargement, une vallee verte avec sa plage se lisait comme
				// une BAIE. Vu a l'image depuis le massif de l'est, et confirme
				// par un temoin a `-WorldseedNappeVue=0` ou la meme vallee est
				// verte : 28 a 30 % du cadre changeait entre les deux moities.
				//
				// ET LE PLAFOND NE ROUVRE RIEN, par construction. L'enfoncement
				// existe pour que la nappe passe sous toute cavite ; or aucune
				// chambre n'existe sous `profondeurMax + rayonMax + margeMer`
				// d'altitude -- soixante-six metres ici -- donc tout plancher de
				// cavite se trouve AU-DESSUS de la marge de mer, partout. Une
				// nappe posee a cette marge reste dessous sans avoir a creuser
				// davantage. Les deux contraintes se rejoignent exactement.
				//
				// CE QU'ON ECHANGE, ET IL FAUT LE DIRE : les terres qui
				// culminent sous l'enfoncement ne sont plus noyees, elles sont
				// APLATIES -- elles se lisent au loin comme une plate-forme a
				// hauteur de rivage au lieu d'un relief. On troque une mer
				// fausse contre une plaine faussement plate, ce qui est le bon
				// sens de l'echange : une terre reste une terre.
				const double FloorM =
					(Positions[Dst].Z + DropCm)
					/ (WorldseedMetersToCm * R.ExagerationZ);
				const double EnfonceM = WorldseedNappe::PlafondDEnfoncement(
					FloorM, R.MargeMerM, R.SurEnfoncementM);

				// ET LE PLAFOND DE MER EPINGLE LE RIVAGE, ce qui supprime le
				// seul risque serieux de la rampe. Un enfoncement qui suit la
				// camera fait « respirer » le relief lointain : un point voit
				// son altitude dessinee changer d'environ un metre par dix
				// metres parcourus. Sur une crete c'est invisible ; sur un
				// TRAIT DE COTE cela se verrait, le rivage avancant et reculant
				// au rythme des pas. Or le plafond vaut zero des qu'on approche
				// du niveau de la mer : le rivage ne bouge pas, par
				// construction.
				PlafondCm[Dst] = FVector2D(
					EnfonceM * WorldseedMetersToCm * R.ExagerationZ, 0.0);
				Normales[Dst] = Source.Normales[Src];
				UV0[Dst] = Source.UV0[Src];
				TeinteRG[Dst] = Source.TeinteRG[Src];
				TeinteB[Dst] = Source.TeinteB[Src];
				Tangentes[Dst] = Source.Tangentes[Src];
				Couleurs[Dst] = Source.Couleurs[Src];
				SousZero[Dst] = Source.SousZero[Src];
			}
		}

		Triangles.Reserve((HX - 1) * (HY - 1) * 6);
		for (int32 Y = 0; Y < HY - 1; ++Y)
		{
			for (int32 X = 0; X < HX - 1; ++X)
			{
				const int32 A = Y * HX + X;
				const int32 C = A + HX;

				if (R.bInverserEnroulement)
				{
					Triangles.Add(A); Triangles.Add(A + 1); Triangles.Add(C);
					Triangles.Add(A + 1); Triangles.Add(C + 1); Triangles.Add(C);
				}
				else
				{
					Triangles.Add(A); Triangles.Add(C); Triangles.Add(A + 1);
					Triangles.Add(A + 1); Triangles.Add(C); Triangles.Add(C + 1);
				}
			}
		}

		// --- LA MER DU DECOR PREND SA PROPRE SECTION ------------------------
		//
		// SIGNALE EN JEU DEPUIS 930 M : « on voit le shader de la mer detaille
		// sur la moitie gauche, une coupure nette, puis du bleu plat a
		// droite ». Le bleu plat est CE maillage-ci : il joue deja le role du
		// `Water_FarMesh` du plugin -- dont la jupe, elle, s'accroche aux
		// bornes de la ZONE (64 x 32 km) et reste donc hors d'atteinte depuis
		// l'interieur du monde. Il porte simplement le materiau de TERRAIN,
		// mat, sans speculaire ni reflet de ciel.
		//
		// POURQUOI CELA NE SE VOYAIT PAS PLUS TOT, et c'est de la geometrie
		// pure : le bord de la fenetre est a 6 144 m, donc son angle sous
		// l'horizontale vaut `atan(altitude / 6144)` -- 0,6 degre a 69 m,
		// 2,9 a 307, mais 8,6 a 930. Mes trois points de vue etaient sous
		// l'horizon. **Une couture a distance fixe se juge a l'ALTITUDE, pas
		// a la distance.**
		//
		// LE PARTAGE EST CONSERVATEUR : un triangle ne passe a la mer que si
		// ses TROIS sommets sont submerges. La bande du rivage reste donc avec
		// la terre, et aucun materiau de mer ne deborde sur une plage.
		//
		// ET LES SOMMETS SONT REMAPPES, PAS DUPLIQUES. Donner le tableau
		// complet aux deux sections doublerait deux millions de sommets pour
		// n'en dessiner qu'une part dans chacune.
		TArray<int32> TriTerre;
		TArray<int32> TriMer;
		TriTerre.Reserve(Triangles.Num());
		TriMer.Reserve(Triangles.Num());
		for (int32 I = 0; I + 2 < Triangles.Num(); I += 3)
		{
			const int32 A = Triangles[I];
			const int32 B = Triangles[I + 1];
			const int32 C = Triangles[I + 2];
			TArray<int32>& Cible =
				(SousZero[A] && SousZero[B] && SousZero[C]) ? TriMer : TriTerre;
			Cible.Add(A); Cible.Add(B); Cible.Add(C);
		}

	Out.CountX = HX;
	Out.CountY = HY;
	Out.Positions = MoveTemp(Positions);
	Out.Normales = MoveTemp(Normales);
	Out.UV0 = MoveTemp(UV0);
	Out.TeinteRG = MoveTemp(TeinteRG);
	Out.TeinteB = MoveTemp(TeinteB);
	Out.Tangentes = MoveTemp(Tangentes);
	Out.Couleurs = MoveTemp(Couleurs);
	Out.Triangles = MoveTemp(Triangles);
	Out.SousZero = MoveTemp(SousZero);
	OutPlafondCm = MoveTemp(PlafondCm);
	OutTrianglesTerre = MoveTemp(TriTerre);
	OutTrianglesMer = MoveTemp(TriMer);
}
