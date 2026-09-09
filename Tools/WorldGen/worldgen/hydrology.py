"""Etape 4b - hydrologie : depressions, ecoulement, debits, rivieres, lacs.

Le noyau numerique (comblement des depressions, D8, accumulation) est aussi
utilise par l'erosion, qui a besoin de connaitre l'aire drainee a chaque pas.

Le debit est pondere par la carte de pluie : un bassin equatorial porte un
fleuve, un bassin desertique n'atteint jamais le seuil et ne produit aucune
riviere. C'est le climat qui decide du reseau hydrographique, pas le relief seul.
"""

from __future__ import annotations

from dataclasses import dataclass, field

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


@dataclass
class River:
    points_px: list[tuple[float, float]] = field(default_factory=list)  # (j, i)
    discharge: list[float] = field(default_factory=list)
    width_m: list[float] = field(default_factory=list)
    depth_m: list[float] = field(default_factory=list)
    strahler: int = 1
    length_m: float = 0.0
    mouth: str = "ocean"        # ocean | lac | bord


@dataclass
class Lake:
    cells: int = 0
    area_ha: float = 0.0
    surface_m: float = 0.0
    centroid_px: tuple[float, float] = (0.0, 0.0)
    bbox_px: tuple[int, int, int, int] = (0, 0, 0, 0)
    outline_px: list[tuple[float, float]] = field(default_factory=list)


# --------------------------------------------------------------- noyau numerique


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


def discharge_weights(
    precip_mm: np.ndarray, geo, runoff_coefficient: float = 0.35
) -> np.ndarray:
    """Apport de chaque cellule, en m3/s.

    Ainsi l'accumulation est un VRAI debit, et la geometrie hydraulique
    (largeur = a * Q^0.5) donne des largeurs en metres directement comparables
    a des fleuves reels : 100 m3/s -> ~18 m, 10 000 m3/s -> ~180 m.
    """
    cell_area_m2 = geo.meters_per_pixel ** 2
    seconds_per_year = 365.25 * 24.0 * 3600.0
    metres_per_year = precip_mm.astype(np.float32) / np.float32(1000.0)
    return (
        metres_per_year
        * np.float32(cell_area_m2 * runoff_coefficient / seconds_per_year)
    ).astype(np.float32)


def best_donors(
    receivers: np.ndarray, accumulation: np.ndarray, valid: np.ndarray
) -> np.ndarray:
    """Pour chaque cellule, le tributaire de plus fort debit (-1 si aucun).

    Astuce : on ecrit les donneurs par debit CROISSANT, donc la derniere
    ecriture sur une cellule est son plus gros affluent. C'est ce qui permet de
    remonter le cours principal d'un fleuve depuis son embouchure.
    """
    size = receivers.size
    acc_flat = accumulation.ravel()
    valid_flat = valid.ravel()
    cells = np.flatnonzero(valid_flat & (receivers != np.arange(size, dtype=np.int32)))
    cells = cells[np.argsort(acc_flat[cells], kind="stable")]
    best = np.full(size, -1, dtype=np.int32)
    best[receivers[cells]] = cells
    return best


def strahler_orders(
    receivers: np.ndarray, order: np.ndarray, is_channel: np.ndarray
) -> np.ndarray:
    """Ordre de Strahler sur le reseau de chenaux.

    Deux affluents de meme ordre qui se rejoignent donnent l'ordre suivant ;
    sinon on garde le maximum. Sert a ne conserver que les axes principaux.
    """
    shape = is_channel.shape
    chan = is_channel.ravel()
    best = np.ones(chan.size, dtype=np.int32)
    second = np.zeros(chan.size, dtype=np.int32)

    best_l = best.tolist()
    second_l = second.tolist()
    rec_l = receivers.tolist()
    chan_l = chan.tolist()

    for cell in order.tolist():
        if not chan_l[cell]:
            continue
        r = rec_l[cell]
        if r == cell or not chan_l[r]:
            continue
        o = best_l[cell]
        if o > best_l[r]:
            second_l[r] = best_l[r]
            best_l[r] = o
        elif o > second_l[r]:
            second_l[r] = o

    result = np.asarray(best_l, dtype=np.int32)
    sec = np.asarray(second_l, dtype=np.int32)
    # Ordre + 1 la ou deux branches de meme rang confluent.
    result = np.where((sec >= result) & (sec > 0), result + 1, result)
    return result.reshape(shape)


# Nombre de cellules que l'on accepte de suivre au-dela du dernier troncon de
# chenal pour identifier l'embouchure. Genereux a dessein : la descente finale
# vers la mer traverse souvent une plaine cotiere inclinee au seul epsilon, ou
# le chemin serpente sur des centaines de cellules. Avec une borne a 64, un
# cours d'eau se jetant dans la mer restait classe endoreique.
_MAX_PAS_EMBOUCHURE = 8192


# ------------------------------------------------------------------ extraction


def extract_rivers(
    flow: FlowState,
    dem: np.ndarray,
    geo,
    hyd: dict,
) -> list[River]:
    """Remonte les chenaux en polylignes, de la source vers l'exutoire."""
    n = dem.shape[0]
    mpp = geo.meters_per_pixel
    threshold = float(hyd["riverDischargeThreshold"])
    is_land = dem > 0.0
    is_channel = (flow.accumulation >= threshold) & is_land

    if not is_channel.any():
        return []

    orders = strahler_orders(flow.receivers, flow.order, is_channel)
    min_order = int(hyd["minStrahlerOrder"])
    keep = is_channel & (orders >= min_order)
    if not keep.any():
        # Reseau trop maigre pour le seuil demande : on retombe sur les chenaux
        # bruts plutot que de ne rien produire.
        keep = is_channel

    rec = flow.receivers
    keep_flat = keep.ravel()
    acc_flat = flow.accumulation.ravel()
    lake = lake_mask(flow.lake_depth_m, is_land, geo, hyd).ravel()

    # On trace chaque fleuve depuis son EMBOUCHURE en remontant toujours le plus
    # gros affluent. Partir des sources donnerait des troncons haches a chaque
    # confluence ; partir de l'embouchure donne le cours principal entier, de la
    # source a la mer, exactement comme on nomme un fleuve.
    self_idx = np.arange(keep_flat.size, dtype=np.int32)
    receiver_is_channel = np.zeros(keep_flat.size, dtype=bool)
    receiver_is_channel[keep_flat] = keep_flat[rec[keep_flat]]
    outlets = np.flatnonzero(keep_flat & (~receiver_is_channel | (rec == self_idx)))
    outlets = outlets[np.argsort(-acc_flat[outlets])]

    donors = best_donors(rec, flow.accumulation, keep)

    a = float(hyd["widthCoefA"])
    w_exp = float(hyd["widthExponent"])
    b = float(hyd["depthCoefB"])
    d_exp = float(hyd["depthExponent"])
    w_min, w_max = float(hyd["minRiverWidthM"]), float(hyd["maxRiverWidthM"])
    tol_px = float(hyd["simplifyToleranceM"]) / mpp
    max_spacing_px = float(hyd["maxPointSpacingM"]) / mpp
    max_pts = int(hyd["maxPointsPerRiver"])
    min_len = float(hyd["minRiverLengthM"])
    max_rivers = int(hyd["maxRiverActors"])

    visited = np.zeros(keep_flat.size, dtype=bool)
    rivers: list[River] = []
    donors_l = donors.tolist()

    for outlet in outlets:
        if len(rivers) >= max_rivers:
            break
        if visited[outlet]:
            continue
        # Remontee du cours principal : embouchure -> source.
        path: list[int] = []
        cell = int(outlet)
        while cell >= 0 and not visited[cell]:
            path.append(cell)
            visited[cell] = True
            cell = donors_l[cell]

        if len(path) < 3:
            continue
        path.reverse()                     # on reordonne source -> embouchure
        # Prolonge d'une cellule pour que le trace touche vraiment l'eau.
        tail = int(rec[path[-1]])
        if tail != path[-1] and not keep_flat[tail]:
            path.append(tail)
        pts = [(float(c // n), float(c % n)) for c in path]
        length_m = _polyline_length(pts) * mpp
        if length_m < min_len:
            continue

        simplified = _simplify(pts, tol_px, max_spacing_px)
        if len(simplified) > max_pts:
            step = max(1, len(simplified) // max_pts)
            simplified = simplified[::step] + [simplified[-1]]

        disch = []
        for (j, i) in simplified:
            disch.append(float(acc_flat[int(round(j)) * n + int(round(i))]))
        widths = [float(np.clip(a * (q ** w_exp), w_min, w_max)) for q in disch]
        depths = [float(max(0.4, b * (q ** d_exp))) for q in disch]

        # Classement de l'embouchure : on SUIT l'ecoulement au-dela du dernier
        # troncon de chenal, au lieu de juger la derniere cellule de terre. Sans
        # cela un cours d'eau qui se jette dans la mer mais dont le dernier
        # pixel terrestre est a 0,75 m d'altitude n'est ni ocean, ni lac, ni
        # bord : il etait declare endoreique par elimination.
        end = path[-1]
        dem_flat = dem.ravel()
        for _ in range(_MAX_PAS_EMBOUCHURE):
            if dem_flat[end] <= 0.0:
                break
            suivant = int(rec[end])
            if suivant == end:
                break                        # puits : cuvette reellement fermee
            end = suivant
        end_j, end_i = end // n, end % n
        on_border = end_j in (0, n - 1) or end_i in (0, n - 1)
        if dem_flat[end] <= 0.0:
            mouth = "ocean"
        elif lake[end]:
            mouth = "lac"
        elif on_border:
            mouth = "bord"          # anomalie : le cours quitte le domaine
        else:
            # Cuvette fermee interieure : le cours meurt sur place. C'est un
            # bassin endoreique, exactement comme la Caspienne ou le lac Tchad.
            mouth = "endoreique"

        rivers.append(
            River(
                points_px=simplified,
                discharge=disch,
                width_m=widths,
                depth_m=depths,
                strahler=int(orders.ravel()[int(outlet)]),
                length_m=length_m,
                mouth=mouth,
            )
        )

    return rivers


def lake_mask(
    lake_depth_m: np.ndarray, is_land: np.ndarray, geo, hyd: dict
) -> np.ndarray:
    """Masque des VRAIS lacs, distinct du simple comblement numerique.

    Combler une cuvette est une operation de ROUTAGE : elle rend le terrain
    traversable par l'ecoulement. Cela ne dit rien de la presence d'eau libre.
    Traiter toute cellule comblee comme un lac -- ce que faisaient
    `extract_lakes` et `biomes.classify` avec un seuil commun de 5 cm -- couvrait
    20,5 % des terres emergees de lacs et arretait 9 rivieres sur 10 au premier
    bassin rencontre.

    Un lac n'existe donc que si la depression est a la fois assez PROFONDE sous
    son point de deversement et assez ETENDUE. Les deux seuils vivent dans
    world_rules.json. Le critere est evalue par composante connexe, pas par
    cellule : c'est la cuvette entiere qui est un lac ou ne l'est pas.
    """
    mpp = geo.meters_per_pixel
    cell_ha = (mpp * mpp) / 10000.0
    prof_min = float(hyd["minLakeDepthM"])
    aire_min = float(hyd["minLakeAreaHa"])

    comble = (lake_depth_m > 0.0) & is_land
    if not comble.any():
        return np.zeros_like(comble)

    labels, count = ndimage.label(comble)
    if count == 0:
        return np.zeros_like(comble)
    idx = np.arange(1, count + 1)
    tailles = ndimage.sum(comble, labels, index=idx)
    profondeurs = ndimage.maximum(lake_depth_m, labels, index=idx)
    garde = (profondeurs >= prof_min) & (tailles * cell_ha >= aire_min)
    table = np.zeros(count + 1, dtype=bool)
    table[1:] = garde
    return table[labels]


def extract_lakes(flow: FlowState, dem: np.ndarray, geo, hyd: dict) -> list[Lake]:
    """Les cuvettes qui satisfont le critere de lac deviennent des acteurs."""
    mpp = geo.meters_per_pixel
    cell_ha = (mpp * mpp) / 10000.0

    mask = lake_mask(flow.lake_depth_m, dem > 0.0, geo, hyd)
    if not mask.any():
        return []

    labels, count = ndimage.label(mask)
    if count == 0:
        return []
    sizes = ndimage.sum(mask, labels, index=np.arange(1, count + 1))
    keep = np.arange(1, count + 1)
    keep = keep[np.argsort(-sizes[keep - 1])][: int(hyd["maxLakeActors"])]

    lakes: list[Lake] = []
    objects = ndimage.find_objects(labels)
    for lab in keep:
        sl = objects[lab - 1]
        sub = labels[sl] == lab
        surface = float(np.median(flow.filled_m[sl][sub]))
        cj, ci = ndimage.center_of_mass(sub)
        lakes.append(
            Lake(
                cells=int(sub.sum()),
                area_ha=float(sub.sum()) * cell_ha,
                surface_m=surface,
                centroid_px=(float(cj) + sl[0].start, float(ci) + sl[1].start),
                bbox_px=(sl[0].start, sl[1].start, sl[0].stop, sl[1].stop),
                outline_px=_outline(sub, sl[0].start, sl[1].start),
            )
        )
    return lakes


# --------------------------------------------------------------------- helpers


def _polyline_length(pts: list[tuple[float, float]]) -> float:
    if len(pts) < 2:
        return 0.0
    arr = np.asarray(pts, dtype=np.float64)
    d = np.diff(arr, axis=0)
    return float(np.hypot(d[:, 0], d[:, 1]).sum())


def _simplify(
    pts: list[tuple[float, float]], tol: float, max_spacing: float = 0.0
) -> list[tuple[float, float]]:
    """Douglas-Peucker iteratif, avec densite minimale garantie.

    Douglas-Peucker seul supprime tous les points d'un troncon rectiligne : une
    riviere de 5 km peut se reduire a 4 sommets, et la spline Unreal devient un
    polygone. `max_spacing` reimpose un point tous les N pixels le long du trace
    original, ce qui garde le cours colle au fond de vallee.
    """
    if len(pts) < 3 or tol <= 0.0:
        return pts
    arr = np.asarray(pts, dtype=np.float64)
    keep = np.zeros(len(arr), dtype=bool)
    keep[0] = keep[-1] = True
    stack = [(0, len(arr) - 1)]
    while stack:
        lo, hi = stack.pop()
        if hi <= lo + 1:
            continue
        a, b = arr[lo], arr[hi]
        ab = b - a
        norm = float(np.hypot(ab[0], ab[1]))
        seg = arr[lo + 1:hi]
        if norm < 1e-9:
            dist = np.hypot(seg[:, 0] - a[0], seg[:, 1] - a[1])
        else:
            dist = np.abs(ab[0] * (a[1] - seg[:, 1]) - (a[0] - seg[:, 0]) * ab[1]) / norm
        k = int(np.argmax(dist))
        if dist[k] > tol:
            split = lo + 1 + k
            keep[split] = True
            stack.append((lo, split))
            stack.append((split, hi))

    if max_spacing > 0.0:
        step = max(1, int(round(max_spacing)))
        keep[::step] = True
        keep[-1] = True

    return [tuple(p) for p in arr[keep]]


def _outline(mask: np.ndarray, off_j: int, off_i: int, max_points: int = 64) -> list[tuple[float, float]]:
    """Contour grossier d'un lac, suffisant pour dimensionner un WaterBodyLake."""
    eroded = ndimage.binary_erosion(mask, border_value=0)
    border = mask & ~eroded
    js, iss = np.nonzero(border)
    if js.size == 0:
        js, iss = np.nonzero(mask)
    if js.size == 0:
        return []
    cj, ci = js.mean(), iss.mean()
    ang = np.arctan2(js - cj, iss - ci)
    keep = np.argsort(ang)
    if keep.size > max_points:
        keep = keep[np.linspace(0, keep.size - 1, max_points).astype(int)]
    return [(float(js[k] + off_j), float(iss[k] + off_i)) for k in keep]
