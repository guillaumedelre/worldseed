"""Worldseed - generateur de monde climatique.

Chaine complete : tectonique -> temperature -> circulation atmospherique ->
erosion -> biomes -> surfaces -> export.

Aucune dependance a Unreal : le module tourne dans son propre venv et n'ecrit
que des PNG / JSON, consommes ensuite par les scripts de Tools/UE.

Convention d'axes (valable dans TOUT le paquet) :
    tableau[j, i]   j = ligne -> axe Y monde -> LATITUDE, j=0 au pole SUD
                    i = colonne -> axe X monde -> longitude, i=0 a l'OUEST
Les PNG sont ecrits dans cet ordre exact (ligne 0 = sud). Le rapport HTML
effectue la symetrie verticale pour afficher le nord en haut.
"""

__all__ = [
    "config", "noise", "tectonics", "climate", "erosion",
    "flow", "biomes", "surfaces", "export", "pipeline",
]

__version__ = "1.0.0"
