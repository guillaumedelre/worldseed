"""Routage de l'ecoulement : comblement des depressions, D8, accumulation.

CE MODULE N'EST PAS DE L'HYDROLOGIE. Il ne produit ni riviere, ni lac, ni
cascade : il rend le terrain TRAVERSABLE par l'ecoulement et compte l'aire
drainee par chaque cellule. C'est exactement ce dont l'erosion a besoin pour
son incision par puissance de courant, et c'est son seul client depuis que
l'hydrologie a ete retiree du projet, le 18 septembre 2026 (voir CLAUDE.md).

Il est le miroir exact de WorldseedFlow.{h,cpp} cote moteur, garde pour la
meme raison : sans lui, plus d'erosion.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy import ndimage
from skimage.morphology import reconstruction

# Voisinage D8 : (dj, di) et distance associee, en pixels.
_NEIGHBOURS = (
    (-1, -1, 1.41421356), (-1, 0, 1.0), (-1, 1, 1.41421356),
    (0, -1, 1.0),                        (0, 1, 1.0),
    (1, -1, 1.41421356),  (1, 0, 1.0),  (1, 1, 1.41421356),
)


@dataclass
class FlowState:
    filled_m: np.ndarray       # float32, MNT sans depression fermee
    receivers: np.ndarray      # int32 aplati, indice du voisin recepteur (soi-meme = exutoire)
    order: np.ndarray          # int32 aplati, cellules triees par altitude decroissante
    accumulation: np.ndarray   # float32 [n, n], debit cumule
    lake_depth_m: np.ndarray   # float32 [n, n], filled - brut, > 0 dans les cuvettes


def fill_depressions(dem: np.ndarray, sea_level: float = 0.0) -> np.ndarray:
    """Comble toute cuvette fermee (Planchon-Darboux via reconstruction morphologique).

    Le resultat moins l'entree donne directement la profondeur des lacs.
    L'ocean sert de condition au bord : on n'inonde jamais sous le niveau marin.
    """
    dem = dem.astype(np.float32, copy=False)
    seed = np.full_like(dem, float(dem.max()))
    # Bords du domaine + ocean : points d'evacuation.
    seed[0, :] = dem[0, :]
    seed[-1, :] = dem[-1, :]
    seed[:, 0] = dem[:, 0]
    seed[:, -1] = dem[:, -1]
    ocean = dem <= sea_level
    seed[ocean] = dem[ocean]
    filled = reconstruction(seed, dem, method="erosion").astype(np.float32)
    return np.maximum(filled, dem)


def d8_receivers(dem: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Direction d'ecoulement D8 : chaque cellule pointe vers son voisin le plus bas.

    Retourne (receivers, order) aplatis. `order` trie les cellules par altitude
    decroissante : en le parcourant, toute cellule est traitee AVANT son
    recepteur, ce qui rend l'accumulation exacte en un seul passage.
    """
    n_rows, n_cols = dem.shape
    dtype = dem.dtype if dem.dtype.kind == "f" else np.float64
    flat = dem.ravel()
    idx = np.arange(flat.size, dtype=np.int32).reshape(n_rows, n_cols)

    best_slope = np.zeros((n_rows, n_cols), dtype=dtype)
    best_rec = idx.copy()

    big = dtype.type(np.inf) if hasattr(dtype, "type") else np.float64(np.inf)
    for dj, di, dist in _NEIGHBOURS:
        shifted = np.full((n_rows, n_cols), big, dtype=dtype)
        shifted_idx = idx.copy()

        src_j = slice(max(0, -dj), n_rows - max(0, dj))
        dst_j = slice(max(0, dj), n_rows - max(0, -dj))
        src_i = slice(max(0, -di), n_cols - max(0, di))
        dst_i = slice(max(0, di), n_cols - max(0, -di))
        # dst = position de la cellule courante, src = position du voisin
        shifted[dst_j, dst_i] = dem[src_j, src_i]
        shifted_idx[dst_j, dst_i] = idx[src_j, src_i]

        drop = (dem - shifted) / dtype.type(dist)
        better = drop > best_slope
        best_slope = np.where(better, drop, best_slope)
        best_rec = np.where(better, shifted_idx, best_rec)

    order = np.argsort(-flat, kind="stable").astype(np.int32)
    return best_rec.ravel().astype(np.int32), order


def flow_accumulation(
    receivers: np.ndarray, order: np.ndarray, weights: np.ndarray
) -> np.ndarray:
    """Accumulation exacte, en un seul passage descendant.

    Volontairement une boucle Python sur des LISTES : l'operation est
    intrinsequement sequentielle (chaque cellule verse dans son recepteur), et
    l'acces scalaire a une liste Python est plusieurs fois plus rapide que
    l'indexation scalaire numpy. Compter ~0,6 s pour 4 M de cellules.
    """
    acc = weights.ravel().astype(np.float64).tolist()
    rec = receivers.tolist()
    for cell in order.tolist():
        r = rec[cell]
        if r != cell:
            acc[r] += acc[cell]
    return np.asarray(acc, dtype=np.float32).reshape(weights.shape)


def slope_filled_epsilon(
    dem: np.ndarray, filled: np.ndarray, epsilon: float = 1e-4
) -> np.ndarray:
    """Incline les cuvettes comblees et les plats, en garantissant l'ecoulement.

    Variante epsilon du Priority-Flood (Barnes, Lehman & Mulla, 2014). On garde
    le comblement morphologique, exact et rapide, pour obtenir les niveaux de
    deversement ; on ne recalcule ici que les ALTITUDES DE ROUTAGE des cellules
    qui n'ont aucun voisin strictement plus bas. Chacune recoit
    `max(niveau comble, altitude du parent + epsilon)` propagee par Dijkstra
    depuis la bordure de la zone plate. Par construction, toute cellule possede
    alors un voisin strictement plus bas : plus aucun puits.

    POURQUOI PAS LA VERSION PRECEDENTE. Elle ajoutait `epsilon * distance
    geodesique au deversoir`, ce qui laisse DEUX defauts mesures sur le monde de
    reference (graine 20260909) :
      - la cellule de deversement est a distance nulle, donc relevee de zero :
        elle reste a egalite avec sa voisine drainante, aucun voisin n'est
        strictement plus bas, et elle devient un puits. 21 935 des 22 757 puits
        recenses etaient exactement ces cellules-la ;
      - la rampe atteignait 0,84 m, de quoi hisser une cellule au-dessus d'une
        voisine hors cuvette et casser des pentes valides ailleurs.
    Consequence : 83 % des cellules de terre voyaient leur ecoulement mourir
    dans un puits, le reseau se fragmentait en 145 morceaux et aucune riviere
    n'atteignait la mer.

    Le retour est en float64 : en float32, a 1000 m d'altitude, le pas de
    quantification vaut 6e-5 m et un epsilon de 1e-4 disparaitrait a l'arrondi.
    """
    import heapq

    n_rows, n_cols = filled.shape
    f32 = filled.astype(np.float32, copy=False)

    def shift(a, dj, di, fill):
        out = np.full(a.shape, fill, dtype=a.dtype)
        out[max(0, dj):n_rows - max(0, -dj), max(0, di):n_cols - max(0, -di)] = \
            a[max(0, -dj):n_rows - max(0, dj), max(0, -di):n_cols - max(0, di)]
        return out

    has_lower = np.zeros(filled.shape, dtype=bool)
    for dj, di, _ in _NEIGHBOURS:
        has_lower |= shift(f32, dj, di, np.float32(np.inf)) < f32

    # A incliner : les plats, et toute cellule effectivement remontee par le
    # comblement (une cuvette peut contenir des cellules qui gardent un voisin
    # plus bas a l'interieur d'elle-meme).
    a_incliner = (~has_lower) | (filled > dem + 1e-6)
    if not a_incliner.any():
        return filled.astype(np.float64)

    # On itere : relever une cellule plate peut la hisser au-dessus d'une voisine
    # NON plate qui comptait sur elle pour s'ecouler, laquelle devient alors un
    # puits a son tour. Sur le monde de reference, une seule passe en laissait 60,
    # tous sous plus de 1000 mm de pluie -- donc de faux bassins endoreiques.
    # Deux a trois passes suffisent a converger.
    routing = filled.astype(np.float64)
    for _ in range(_MAX_PASSES_EPSILON):
        routing = _propage_epsilon(routing, filled, a_incliner, epsilon, n_rows, n_cols)
        restants = np.ones(filled.shape, dtype=bool)
        for dj, di, _d in _NEIGHBOURS:
            voisin = np.full(routing.shape, np.inf)
            voisin[max(0, dj):n_rows - max(0, -dj), max(0, di):n_cols - max(0, -di)] = \
                routing[max(0, -dj):n_rows - max(0, dj), max(0, -di):n_cols - max(0, di)]
            restants &= ~(voisin < routing)
        restants &= ~a_incliner
        if not restants.any():
            break
        a_incliner = a_incliner | restants
    return routing


_MAX_PASSES_EPSILON = 4


def _propage_epsilon(routing, filled, a_incliner, epsilon, n_rows, n_cols):
    """Une passe de Dijkstra : chaque cellule du masque recoit parent + epsilon."""
    import heapq

    assigne = np.where(a_incliner, np.inf, routing)

    # Amorce : les cellules HORS zone a incliner qui la touchent. Elles gardent
    # leur altitude et servent de sources ; inutile d'empiler tout le domaine.
    bord = ndimage.binary_dilation(a_incliner, structure=np.ones((3, 3))) & ~a_incliner
    tas = [(float(routing[j, i]), int(j) * n_cols + int(i))
           for j, i in np.argwhere(bord)]
    heapq.heapify(tas)

    plat = a_incliner.ravel()
    niveau = filled.ravel().astype(np.float64)
    aff = assigne.ravel()
    eps = float(epsilon)

    while tas:
        z, cell = heapq.heappop(tas)
        if z > aff[cell]:
            continue
        cj, ci = divmod(cell, n_cols)
        for dj, di, _ in _NEIGHBOURS:
            nj, ni = cj + dj, ci + di
            if not (0 <= nj < n_rows and 0 <= ni < n_cols):
                continue
            nb = nj * n_cols + ni
            if not plat[nb]:
                continue
            cand = niveau[nb] if niveau[nb] > z + eps else z + eps
            if cand < aff[nb]:
                aff[nb] = cand
                heapq.heappush(tas, (cand, nb))

    # Une zone plate totalement fermee (aucune bordure drainante) reste a l'infini :
    # on la ramene a son niveau comble plutot que de produire des NaN.
    np.copyto(aff, niveau, where=~np.isfinite(aff))
    return aff.reshape(filled.shape)


def resolve_flats(filled: np.ndarray, epsilon: float = 1e-3) -> np.ndarray:
    """Incline imperceptiblement les surfaces plates vers leur exutoire.

    Sans cette etape, l'hydrologie est fausse et le reste en silence : une
    depression comblee devient un plateau PARFAITEMENT plat, aucun voisin n'y est
    strictement plus bas, donc chaque cellule du lac devient un puits en D8 et
    l'ecoulement s'arrete net au bord du lac. Les bassins se fragmentent, les
    debits s'effondrent et il ne reste presque plus de rivieres -- sans qu'aucune
    erreur ne soit levee.

    Le remede est classique : on ajoute une pente infinitesimale proportionnelle
    a la distance a la premiere cellule possedant un voisin plus bas. Les plats
    s'ecoulent alors vers leur exutoire, et l'amplitude ajoutee (quelques
    centimetres au plus) ne change rien au relief.

    Le retour est en float64 A DESSEIN. En float32, a 1000 m d'altitude, le pas
    de quantification vaut deja 6e-5 m : une pente de 1e-4 m par pixel disparait
    purement et simplement a l'arrondi, et les plats restent plats.

    La distance doit etre GEODESIQUE, calculee a l'interieur du plat. Une simple
    distance euclidienne globale pointe vers la berge la plus proche et non vers
    l'exutoire du lac : la rampe descend alors vers le rivage, ou l'eau se
    retrouve bloquee, et le probleme reste entier.
    """
    n_rows, n_cols = filled.shape
    has_lower = np.zeros(filled.shape, dtype=bool)
    same_or_lower_neighbour = np.zeros(filled.shape, dtype=bool)

    def shift(a, dj, di, fill):
        out = np.full(a.shape, fill, dtype=a.dtype)
        out[max(0, dj):n_rows - max(0, -dj), max(0, di):n_cols - max(0, -di)] = \
            a[max(0, -dj):n_rows - max(0, dj), max(0, -di):n_cols - max(0, di)]
        return out

    f32 = filled.astype(np.float32, copy=False)
    for dj, di, _ in _NEIGHBOURS:
        neighbour = shift(f32, dj, di, np.float32(np.inf))
        has_lower |= neighbour < f32

    flat = ~has_lower
    if not flat.any():
        return filled.astype(np.float64)

    # Exutoires : cellules du plat touchant une cellule qui, elle, sait s'ecouler
    # et se trouve au meme niveau ou plus bas. C'est le point de deversement.
    for dj, di, _ in _NEIGHBOURS:
        drains = shift(has_lower, dj, di, False)
        level = shift(f32, dj, di, np.float32(np.inf))
        same_or_lower_neighbour |= drains & (level <= f32 + np.float32(1e-3))
    starts = np.argwhere(flat & same_or_lower_neighbour)

    if starts.size == 0:
        # Plat totalement ferme (cuvette sans exutoire, cas des bords) : on se
        # rabat sur la distance euclidienne, faute de mieux.
        distance = ndimage.distance_transform_edt(flat)
    else:
        from skimage.graph import MCP_Geometric

        costs = np.where(flat, 1.0, np.inf)
        mcp = MCP_Geometric(costs, fully_connected=True)
        distance, _ = mcp.find_costs(starts.tolist())
        distance = np.where(np.isfinite(distance), distance, 0.0)

    return filled.astype(np.float64) + float(epsilon) * distance


def perce_bassins_fermes(
    routing: np.ndarray,
    dem: np.ndarray,
    arid: np.ndarray | None,
    epsilon: float,
    sea_level: float = 0.0,
    rayon_max_px: int = 600,
) -> tuple[np.ndarray, int, int]:
    """Creuse un exutoire aux cuvettes fermees qui ne sont PAS arides.

    Combler une cuvette suppose qu'elle deborde par son seuil. Quelques plats
    restent malgre tout enclos apres la passe epsilon -- des cellules sans aucun
    voisin strictement plus bas, mesurees a 64 sur la graine 20260909. Elles
    capturent l'ecoulement et fabriquent de faux bassins endoreiques : le plus
    gros recevait 1878 mm de pluie annuelle, ce qu'aucune evaporation ne peut
    consommer.

    Un bassin ferme n'est physiquement tenable qu'en climat aride, ou
    l'evaporation equilibre l'apport (mer d'Aral, lac Tchad). Ailleurs il
    deborde, et s'il ne le fait pas c'est que le seuil est mal represente. On
    perce donc : recherche en largeur depuis le puits jusqu'a la premiere cellule
    strictement plus basse, puis creusement monotone le long de ce chemin.

    Retourne (routage corrige, nombre de puits perces, nombre laisses en place).
    """
    from collections import deque

    n_rows, n_cols = routing.shape
    has_lower = np.zeros(routing.shape, dtype=bool)
    for dj, di, _ in _NEIGHBOURS:
        voisin = np.full(routing.shape, np.inf)
        voisin[max(0, dj):n_rows - max(0, -dj), max(0, di):n_cols - max(0, -di)] = \
            routing[max(0, -dj):n_rows - max(0, dj), max(0, -di):n_cols - max(0, di)]
        has_lower |= voisin < routing

    puits = (~has_lower) & (dem > sea_level)
    if arid is not None:
        puits &= ~arid
    if not puits.any():
        return routing, 0, 0

    perces = laisses = 0
    for pj, pi in np.argwhere(puits):
        depart = int(pj) * n_cols + int(pi)
        niveau = routing[pj, pi]
        parent = {depart: -1}
        file = deque([depart])
        cible = -1
        while file and len(parent) < rayon_max_px * rayon_max_px:
            cell = file.popleft()
            cj, ci = divmod(cell, n_cols)
            for dj, di, _ in _NEIGHBOURS:
                nj, ni = cj + dj, ci + di
                if not (0 <= nj < n_rows and 0 <= ni < n_cols):
                    continue
                nb = nj * n_cols + ni
                if nb in parent:
                    continue
                parent[nb] = cell
                if routing[nj, ni] < niveau - epsilon:
                    cible = nb
                    break
                file.append(nb)
            if cible >= 0:
                break

        if cible < 0:
            laisses += 1
            continue

        # Creusement monotone du puits vers la cellule basse trouvee.
        chemin = []
        cell = cible
        while cell != -1:
            chemin.append(cell)
            cell = parent[cell]
        # En remontant les parents depuis la cible, `chemin` va DEJA de la cible
        # vers le puits : le retourner mettait le puits en tete, le creusement
        # partait du mauvais bout et n'abaissait plus rien.
        plat = routing.ravel()
        bas = float(plat[chemin[0]])
        longueur = len(chemin) - 1
        # On REPARTIT la denivelee disponible sur le chemin au lieu de monter
        # d'un epsilon par pas. La BFS s'arrete a la premiere cellule plus basse,
        # parfois d'un epsilon seulement : en montant d'un epsilon par cellule,
        # le creusement depassait le niveau du puits avant de l'atteindre, et
        # celui-ci n'etait jamais abaisse -- le percage semblait reussir tout en
        # ne changeant rien. Ici le dernier point vaut niveau - epsilon, donc le
        # puits finit strictement au-dessus de son voisin amont.
        pas = (float(niveau) - epsilon - bas) / max(longueur, 1)
        if pas <= 0.0:
            laisses += 1
            continue
        for k, cell in enumerate(chemin[1:], start=1):
            valeur = bas + k * pas
            if valeur < plat[cell]:
                plat[cell] = valeur
        perces += 1

    return routing, perces, laisses


_MAX_PASSES_PERCAGE = 6


def compute_flow(
    dem: np.ndarray,
    rain_weight: np.ndarray,
    sea_level: float = 0.0,
    fill_epsilon_m: float = 1e-4,
    arid: np.ndarray | None = None,
) -> FlowState:
    """Chaine complete : comblement -> pente epsilon -> percage -> D8 -> accumulation."""
    filled = fill_depressions(dem, sea_level)
    routing = slope_filled_epsilon(dem, filled, fill_epsilon_m)
    # Le percage se repete : creuser un exutoire abaisse des cellules et peut en
    # laisser d'autres sans voisin plus bas. Mesure sur la graine 20260909 : une
    # passe perce 50 puits et en laisse reapparaitre 63, tous cotiers. On boucle
    # jusqu'a ce qu'il n'y ait plus rien a percer.
    for _ in range(_MAX_PASSES_PERCAGE):
        routing, perces, _laisses = perce_bassins_fermes(
            routing, dem, arid, fill_epsilon_m, sea_level)
        if perces == 0:
            break
    receivers, order = d8_receivers(routing)
    acc = flow_accumulation(receivers, order, rain_weight)
    return FlowState(
        filled_m=filled,
        receivers=receivers,
        order=order,
        accumulation=acc,
        lake_depth_m=(filled - dem).astype(np.float32),
    )
