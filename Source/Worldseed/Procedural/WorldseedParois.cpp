// Worldseed - poser des pans de falaise le long des rebords du relief.

#include "Procedural/WorldseedParois.h"

#include "Procedural/WorldseedVoxelChunk.h"

namespace
{
	/**
	 * Un melange entier, le meme esprit que partout ailleurs dans ce projet :
	 * deterministe, sans etat, et independant d'un appel a l'autre. Une falaise
	 * doit tomber au meme endroit a chaque chargement du meme monde.
	 */
	uint32 Melanger(uint32 V)
	{
		V ^= V >> 16;
		V *= 0x7feb352du;
		V ^= V >> 15;
		V *= 0x846ca68bu;
		V ^= V >> 16;
		return V;
	}

	/** Nombre de rayons echantillonnes autour d'un point pour lire le rebord. */
	constexpr int32 NbRayons = 12;
}

TArray<FSoftObjectPath> WorldseedParois::MaillagesParDefaut()
{
	return {
		FSoftObjectPath(TEXT("/Game/Orasot_Bundle/Stylized_Landscape_5_Bioms/"
			"Biom_Green/StaticMeshes/SM_Cliff_2.SM_Cliff_2")),
	};
}

float WorldseedParois::Tirage(int32 MailleX, int32 MailleY, int32 Canal, int32 Graine)
{
	uint32 H = Melanger(static_cast<uint32>(MailleX) * 0x9e3779b9u
		^ Melanger(static_cast<uint32>(MailleY) * 0x85ebca6bu
			^ Melanger(static_cast<uint32>(Canal) * 0xc2b2ae35u
				^ static_cast<uint32>(Graine))));
	return static_cast<float>(H & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

bool WorldseedParois::LireRebord(double XM, double YM,
	const FWorldseedParoiRegles& Regles,
	TFunctionRef<double(double, double)> ReliefM,
	FWorldseedRebord& Out)
{
	const double R = FMath::Max(1.0f, Regles.RayonMesureM);
	const double H0 = ReliefM(XM, YM);

	double PlusHaut = H0;
	double PlusBas = H0;
	int32 IdxBas = INDEX_NONE;

	for (int32 K = 0; K < NbRayons; ++K)
	{
		const double A = (2.0 * PI * K) / NbRayons;
		const double H = ReliefM(XM + R * FMath::Cos(A), YM + R * FMath::Sin(A));
		if (H > PlusHaut)
		{
			PlusHaut = H;
		}
		if (H < PlusBas)
		{
			PlusBas = H;
			IdxBas = K;
		}
	}

	const double Chute = H0 - PlusBas;
	const double Montee = PlusHaut - H0;

	// --- LES DEUX CONDITIONS D'UN REBORD, ET LA SECONDE FAIT TOUT -----------
	//
	// La premiere est evidente : il faut que ca TOMBE. La seconde est celle qui
	// distingue le HAUT d'une marche du MILIEU d'une paroi -- et c'est elle qui
	// transforme un semis de surface en semis de ligne.
	//
	// Au milieu d'une paroi, il y a autant de roche au-dessus qu'en dessous :
	// montee et chute se valent. Sur le rebord, rien ne domine -- on est sur le
	// plateau, le vide est devant. Exiger `montee << chute` ne retient donc que
	// le trait ou le plateau bascule, et laisse toute la face en dessous.
	//
	// SANS ELLE, TOUTE FACE RAIDE EST RETENUE, et un canyon en a partout :
	// mesure du defaut, 911 pans formant un chaos de dalles qui ensevelissait
	// le canyon au lieu d'en habiller le bord. Aucun reglage de densite ne
	// corrige cela -- c'est la FORME du critere qui etait fausse, pas sa
	// valeur.
	if (Chute < static_cast<double>(Regles.DeniveleMinM))
	{
		return false;
	}
	if (Montee > Chute * static_cast<double>(Regles.MonteeMaxFrac))
	{
		return false;
	}
	if (IdxBas == INDEX_NONE)
	{
		return false;
	}

	const double A = (2.0 * PI * IdxBas) / NbRayons;
	Out.VersLeVide = FVector2D(FMath::Cos(A), FMath::Sin(A));
	Out.AltitudeM = H0;
	Out.ChuteM = Chute;
	return true;
}

void WorldseedParois::Semer(const FWorldseedVoxelMesh& Mesh,
	const FVector& OrigineChunkCm, double CoteChunkCm,
	TArrayView<const FWorldseedParoiModele> Catalogue,
	const FWorldseedParoiRegles& Regles, int32 Graine,
	TFunctionRef<double(double, double)> ReliefM,
	TArray<FWorldseedParoiInstance>& Out)
{
	if (Catalogue.Num() == 0 || Regles.Densite <= 0.0f || Regles.PasM <= 0.0f)
	{
		return;
	}
	if (Mesh.Positions.Num() == 0)
	{
		return;
	}

	const double PasCm = static_cast<double>(Regles.PasM) * 100.0;

	// --- LES MAILLES QUE CE CHUNK DOIT VISITER ------------------------------
	//
	// La grille est celle du MONDE, pas du chunk : on prend les mailles dont le
	// centre tombe dans ce chunk. Un chunk ne peut donc pas semer chez son
	// voisin, et aucune maille ne peut etre semee deux fois.
	const int32 MailleX0 = FMath::FloorToInt32(OrigineChunkCm.X / PasCm);
	const int32 MailleY0 = FMath::FloorToInt32(OrigineChunkCm.Y / PasCm);
	const int32 MailleX1 = FMath::CeilToInt32((OrigineChunkCm.X + CoteChunkCm) / PasCm);
	const int32 MailleY1 = FMath::CeilToInt32((OrigineChunkCm.Y + CoteChunkCm) / PasCm);

	for (int32 MX = MailleX0; MX <= MailleX1; ++MX)
	{
		for (int32 MY = MailleY0; MY <= MailleY1; ++MY)
		{
			// Le centre de la maille, decale par un tirage pour que le chapelet
			// ne se lise pas comme une grille. Le decalage reste modeste : on
			// veut une LIGNE continue, pas un nuage.
			const double CX = (static_cast<double>(MX) + 0.35
				+ 0.3 * Tirage(MX, MY, 0, Graine)) * PasCm;
			const double CY = (static_cast<double>(MY) + 0.35
				+ 0.3 * Tirage(MX, MY, 1, Graine)) * PasCm;

			if (CX < OrigineChunkCm.X || CX >= OrigineChunkCm.X + CoteChunkCm
				|| CY < OrigineChunkCm.Y || CY >= OrigineChunkCm.Y + CoteChunkCm)
			{
				continue;
			}

			if (Tirage(MX, MY, 2, Graine) > Regles.Probabilite * Regles.Densite)
			{
				continue;
			}

			FWorldseedRebord Rebord;
			if (!LireRebord(CX / 100.0, CY / 100.0, Regles, ReliefM, Rebord))
			{
				continue;
			}

			// --- UNE SEULE TRANCHE DE CHUNK SEME CE REBORD --------------------
			//
			// LES CHUNKS SONT EN TROIS DIMENSIONS, et une colonne en empile
			// plusieurs sur la meme emprise XY. Sans ce test, chaque chunk de la
			// colonne poserait le meme pan : autant de copies superposees que
			// d'etages charges, pour un defaut qui ne se voit qu'au compteur.
			// On ne seme que depuis le chunk dont la tranche verticale contient
			// l'altitude du rebord.
			const double ZRebordCm = Rebord.AltitudeM * 100.0;
			if (ZRebordCm < OrigineChunkCm.Z
				|| ZRebordCm >= OrigineChunkCm.Z + CoteChunkCm)
			{
				continue;
			}

			const int32 IdxModele = FMath::Clamp(
				FMath::FloorToInt32(Tirage(MX, MY, 3, Graine) * Catalogue.Num()),
				0, Catalogue.Num() - 1);
			const FWorldseedParoiModele& Modele = Catalogue[IdxModele];

			// --- L'ECHELLE SUIT LA CHUTE MESUREE ------------------------------
			//
			// Le pan doit couvrir la paroi qu'il habille : on cale sa hauteur
			// sur la chute lue au rebord. C'est une MESURE et non un tirage --
			// une plage fixe recopiee du pack donne des blocs plus grands que le
			// relief des qu'on change d'echelle de monde, et ce depot a deja
			// paye cette lecon sur le `Sand UV` d'Orasot.
			const double HauteurModeleCm = FMath::Max(1.0, 2.0 * Modele.DemiTaille.Z);
			const float Echelle = FMath::Clamp(
				static_cast<float>(Rebord.ChuteM * 100.0 / HauteurModeleCm),
				Regles.EchelleMin, Regles.EchelleMax);

			// --- L'ORIENTATION SUIT LE TRAIT DU REBORD ------------------------
			//
			// LE PAN EST TANGENT, PAS PERPENDICULAIRE. Sa plus grande dimension
			// (76,8 m contre 54,9) doit courir LE LONG de la falaise, et sa
			// face large regarder le vide : c'est ainsi qu'une suite de pans
			// forme une paroi continue plutot qu'une rangee de dents.
			//
			// `YawOffsetDeg` dit quel axe local regarde le vide, et il se REGLE
			// A L'IMAGE : un modele quelconque n'a aucune raison d'orienter sa
			// face large selon un axe plutot qu'un autre, et se tromper de
			// quatre-vingt-dix degres ne casse rien -- cela met simplement les
			// pans de profil, ce qui ne se voit qu'en regardant.
			const double AzimutVide = FMath::RadiansToDegrees(
				FMath::Atan2(Rebord.VersLeVide.Y, Rebord.VersLeVide.X));

			const float Bascule = Regles.BasculeMaxDeg;
			const FRotator Rot(
				(Tirage(MX, MY, 5, Graine) * 2.0f - 1.0f) * Bascule,
				static_cast<float>(AzimutVide) + Regles.YawOffsetDeg,
				(Tirage(MX, MY, 6, Graine) * 2.0f - 1.0f) * Bascule);

			// --- L'ANCRAGE ----------------------------------------------------
			//
			// LE SOMMET DU PAN SE POSE SUR LE REBORD, et le reste pend dans la
			// paroi. Ancrer son CENTRE sur le rebord le ferait depasser d'une
			// demi-hauteur au-dessus du plateau -- un mur planté sur la crete au
			// lieu d'une falaise.
			//
			// Et l'on s'enfonce vers l'INTERIEUR : un pan pose pile sur le trait
			// laisse voir son dos depuis le plateau. La majeure partie du bloc
			// doit etre dans la roche, comme la showcase l'emploie -- on n'en
			// voit que la face.
			const FVector2D VersLInterieur = -Rebord.VersLeVide;
			const double DemiLargeur = 0.5 * (Modele.DemiTaille.X + Modele.DemiTaille.Y);
			const double Enfoncement = DemiLargeur * Echelle
				* static_cast<double>(Regles.EnfoncementFrac);

			// L'ALTITUDE SE PREND SUR LA GEOMETRIE REELLE QUAND ON L'A. Le champ
			// voxel deplace la surface de plusieurs metres par rapport au relief
			// macro -- bruit de surplomb plus detail -- et un pan cale sur le
			// macro flotterait ou s'enterrerait d'autant. On cherche donc le
			// sommet du maillage le plus proche en XY ; a defaut, le macro fait
			// un repli honnete.
			double ZSommetCm = ZRebordCm;
			{
				double MeilleureD2 = TNumericLimits<double>::Max();
				const double RayonAccroche2 = PasCm * PasCm * 0.25;
				for (int32 I = 0; I < Mesh.Positions.Num(); ++I)
				{
					const FVector& P = Mesh.Positions[I];
					const double DX = P.X - CX;
					const double DY = P.Y - CY;
					const double D2 = DX * DX + DY * DY;
					if (D2 < MeilleureD2 && D2 < RayonAccroche2)
					{
						MeilleureD2 = D2;
						ZSommetCm = P.Z;
					}
				}
			}

			const FVector CentreVoulu(
				CX + VersLInterieur.X * Enfoncement,
				CY + VersLInterieur.Y * Enfoncement,
				ZSommetCm - Modele.DemiTaille.Z * Echelle);

			const FVector Decalage = Rot.RotateVector(Modele.Origine * Echelle);

			FWorldseedParoiInstance Inst;
			Inst.Modele = IdxModele;
			Inst.Transform = FTransform(Rot, CentreVoulu - Decalage,
				FVector(Echelle, Echelle, Echelle));
			Out.Add(Inst);
		}
	}
}
