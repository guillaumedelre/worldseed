// Worldseed - tables de l'algorithme Transvoxel (Eric Lengyel).
//
// ============================================================================
//  CE FICHIER ET SON .cpp PORTENT DE LA DONNEE TIERCE, SOUS LICENCE MIT.
//
//  Transvoxel Algorithm Data Tables -- https://transvoxel.org/
//
//  MIT License
//
//  Copyright (c) 2009 Eric Lengyel
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in all
//  copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//  SOFTWARE.
//
//  La licence complete est conservee telle quelle dans
//  Source/Worldseed/ThirdParty/Transvoxel/LICENSE, avec le fichier d'origine.
//  L'algorithme lui-meme est declare LIBRE DE TOUT BREVET par son auteur
//  (transvoxel.org) -- verifie avant d'ecrire une ligne, comme le plan du
//  chantier l'exigeait.
// ============================================================================
//
// POURQUOI LES NOMS RESTENT EN ANGLAIS, seule entorse a la convention du depot.
// Ces sept tables sont de la DONNEE PUBLIEE, et toute leur valeur tient a ce
// qu'on puisse les confronter au texte de Lengyel -- les numeros de section et
// de figure cites en commentaire renvoient a son article. Les renommer les
// rendrait invérifiables. Le code qui les EMPLOIE, lui, suit la convention du
// projet.
//
// Les tables sont reprises A L'OCTET PRES du fichier d'origine, par
// transformation scriptee et non par recopie : une table de correspondance
// fausse produirait du maillage silencieusement faux, et rien ne le signalerait.

#pragma once

#include "CoreMinimal.h"

namespace WorldseedTransvoxel
{
	/**
	 * Triangulation d'une classe d'equivalence de cellule REGULIERE (section 3.2).
	 *
	 * Le quartet haut de Compteurs donne le nombre de sommets, le bas le nombre
	 * de triangles. VertexIndex se lit par groupes de trois.
	 */
	struct RegularCellData
	{
		unsigned char geometryCounts;
		unsigned char vertexIndex[15];

		int32 GetVertexCount() const { return geometryCounts >> 4; }
		int32 GetTriangleCount() const { return geometryCounts & 0x0F; }
	};

	/**
	 * Triangulation d'une classe d'equivalence de cellule de TRANSITION (4.3).
	 *
	 * C'est ce que le marching cubes du moteur ne sait pas produire, et c'est
	 * toute la raison de ce fichier : une cellule de transition raccorde une
	 * face fine a un voisin deux fois plus grossier, sans fissure.
	 */
	struct TransitionCellData
	{
		long geometryCounts;
		unsigned char vertexIndex[36];

		int32 GetVertexCount() const { return static_cast<int32>(geometryCounts >> 4); }
		int32 GetTriangleCount() const { return static_cast<int32>(geometryCounts & 0x0F); }
	};

	// LES TYPES SONT CEUX DE L'ORIGINAL, A DESSEIN. Declarer `uint8` la ou le
	// .cpp definit `unsigned char` marcherait par chance sur cette plateforme ;
	// sur `unsigned short` contre `uint16` aussi. Mais une declaration qui ne
	// correspond pas EXACTEMENT a sa definition est une faute qui ne se voit
	// qu'a l'edition de liens, et parfois pas du tout.

	/** 256 cas de marching cubes -> 16 classes d'equivalence (section 3.2). */
	extern const unsigned char regularCellClass[256];
	extern const RegularCellData regularCellData[16];

	/**
	 * Pour chacun des 256 cas, l'arete portant chaque sommet (section 3.3).
	 *
	 * Octet bas : les deux extremites de l'arete, numerotees figure 3.7.
	 * Octet haut : de quelle cellule voisine le sommet peut etre REUTILISE,
	 * figure 3.8. C'est ce partage qui donne un maillage soude.
	 */
	extern const unsigned short regularVertexData[256][12];

	/**
	 * L'ORDRE DES BITS DU CODE DE CAS D'UNE CELLULE DE TRANSITION.
	 *
	 * Il n'est NULLE PART en clair : ni dans la source des tables, ni dans le
	 * texte de la these -- il est dans la figure 4.17, qui est une IMAGE. Le
	 * deduire d'une phrase serait un pari, et un pari sur un ordre de bits
	 * produit du maillage silencieusement faux.
	 *
	 * IL A DONC ETE DERIVE DES TABLES, QUI L'ENCODENT. `transitionVertexData`
	 * liste les aretes portant un sommet ; or une arete ne porte un sommet que
	 * si ses deux extremites sont de SIGNES OPPOSES. Pour le bon ordre, et pour
	 * lui seul, les 512 cas sont coherents. Mesure exhaustive, 4 096 aretes :
	 *
	 *     perimetre     0 faute     0,00 %     <- retenu
	 *     sequentiel    1 536      37,50 %
	 *     perimetre2    2 048      50,00 %
	 *
	 * Rejouable : `perl Tools/Transvoxel/ordre_bits.pl`.
	 *
	 * Echantillons de la face PLEINE RESOLUTION, en ligne (figure 4.16) :
	 *
	 *     0 1 2
	 *     3 4 5
	 *     6 7 8
	 *
	 * et le code de cas parcourt le PERIMETRE, le centre en poids fort :
	 *
	 *     bit 0x001 -> 0     bit 0x008 -> 5     bit 0x040 -> 6
	 *     bit 0x002 -> 1     bit 0x010 -> 8     bit 0x080 -> 3
	 *     bit 0x004 -> 2     bit 0x020 -> 7     bit 0x100 -> 4
	 *
	 * ET LES QUATRE ECHANTILLONS DEMI-RESOLUTION NE SONT PAS DES INCONNUES.
	 * Section 4.5 : « the voxel values for locations 0 and 9 are the same, as
	 * are those for locations 2 and A, 6 and B, and 8 and C ». C'est ce qui
	 * ramene treize echantillons a neuf bits.
	 */
	/** 512 cas de transition -> 56 classes. Bit haut : enroulement inverse. */
	extern const unsigned char transitionCellClass[512];
	extern const TransitionCellData transitionCellData[56];

	/** Reutilisation des 13 coins d'une cellule de transition (figure 4.18). */
	extern const unsigned char transitionCornerData[13];

	/** Aretes et reutilisation pour les 512 cas de transition (figures 4.16/4.17). */
	extern const unsigned short transitionVertexData[512][12];
}
