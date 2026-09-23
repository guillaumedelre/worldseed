// Worldseed - la veille : surveiller le streaming PENDANT UNE PARTIE REELLE.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "WorldseedVeille.generated.h"

class AWorldseedVoxelTerrain;

/**
 * CE QUE LE BANC NE POUVAIT PAS MESURER : LA OU LE JOUEUR VA.
 *
 * Le banc marche tout seul, en ligne droite, au cap qu'on lui donne. Ca suffit
 * pour prouver qu'un mecanisme existe -- les orphelins l'ont ete -- et c'est
 * INUTILISABLE pour juger ce qu'un joueur voit. Preuve immediate : lance au
 * cap 0 depuis un depart cotier, il est parti DROIT VERS LA MER. Mille sept
 * cent soixante-six metres parcourus, neuf cent soixante-douze feuilles
 * demandees au lieu de deux mille quatre cents, et zero trou au sondage. Le
 * releve etait vide parce que la MARCHE etait vide.
 *
 * C'est la meme faute que ce depot a payee plusieurs fois sous d'autres
 * formes -- mesurer une forme rare au mauvais endroit, chercher l'entonnoir
 * des bancs sur une plaine cotiere que la serie n'atteint jamais. L'instrument
 * etait juste ; il ne regardait pas la ou il se passe quelque chose.
 *
 * LA VEILLE RENVERSE LE PROBLEME : elle ne conduit pas, elle REGARDE. Le
 * joueur va ou il veut -- vers un canyon, vers une paroi, vers un endroit ou
 * il a DEJA VU le defaut -- et elle crie quand elle constate un trou, avec la
 * position, le cap et la vitesse. C'est la reponse a « instrumente l'apparition
 * du neant afin que tu t'en rendes compte programmatiquement ».
 *
 * ELLE SE TAIT QUAND TOUT VA BIEN, et c'est ce qui la rend lisible : une ligne
 * par evenement, plus un resume periodique pour que le SILENCE soit lui aussi
 * une information -- sans quoi on ne saurait pas distinguer « rien vu » de
 * « rien mesure ».
 *
 * Usage :
 *
 *     UnrealEditor.exe Worldseed.uproject -game -WorldseedVeille
 *       -windowed -resx=1600 -resy=900
 *
 * Elle ne s'arme que la ou un terrain voxel existe : dans le menu, elle ne
 * trouve rien et ne coute rien.
 */
UCLASS()
class WORLDSEED_API UWorldseedVeille : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(UWorldseedVeille, STATGROUP_Tickables);
	}
	virtual bool IsTickable() const override { return bArmee; }

private:
	AWorldseedVoxelTerrain* Terrain() const;
	void Resumer(const TCHAR* Quand) const;

	bool bArmee = false;

	/**
	 * Le TEMOIN, pris une fois que le monde est pose.
	 *
	 * Sur un monde stabilise le sondage doit rendre ZERO. S'il crie la, c'est
	 * l'instrument qui est faux, et le croire ferait chercher un defaut qui
	 * n'existe pas -- ce depot a deja valide une metrique nulle part, l'a crue,
	 * et pose la valeur inverse.
	 */
	bool bTemoinPris = false;
	bool bTemoinValide = false;

	/** Au-dela, on mesure meme sans temoin -- et l.on dit qu.il manque. */
	float AttenteMaxS = 20.0f;
	int32 DernierCompte = -1;
	float StableS = 0.0f;

	/** Periode d'echantillonnage, en secondes. Le sondage de vue est cher. */
	float PeriodeS = 0.35f;

	/** Periode du resume, pour que le silence soit lui aussi une information. */
	float PeriodeResumeS = 15.0f;

	double Horloge = 0.0;
	double ProchainEchantillon = 0.0;
	double ProchainResume = 0.0;

	/** Position au dernier echantillon : elle donne la vitesse REELLE. */
	FVector DernierePositionCm = FVector::ZeroVector;
	bool bPositionConnue = false;

	// --- ce qu'on a vu, cumule --------------------------------------------
	int32 Echantillons = 0;
	int32 EchantillonsEnMouvement = 0;
	double DistanceParcourueM = 0.0;

	int64 SommeOrphelins = 0;
	int64 SommeBeants = 0;
	int64 SommeRayonsTroues = 0;
	int32 PireOrphelins = 0;
	int32 PireBeants = 0;
	int32 PireRayonsTroues = 0;
	int32 EvenementsCries = 0;
	float TrouLePlusProcheM = 0.0f;

	/**
	 * Ou le pire trou a ete vu.
	 *
	 * UNE MESURE SANS SON LIEU NE SE REJOUE PAS. Le depot a deja perdu un
	 * releve de reference pour l'avoir garde en coordonnees sans dire dans quel
	 * monde -- et il pointait, quelques regenerations plus tard, trois cent
	 * trente-neuf metres sous la mer. On garde donc la position ET le cap, qui
	 * permettent d'y retourner par `-WorldseedDepartX/Y` et `-WorldseedCap`.
	 */
	FVector PireLieuCm = FVector::ZeroVector;
	float PireCapDeg = 0.0f;
};
