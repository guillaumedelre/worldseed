# Determiner l'ORDRE DES BITS du code de cas d'une cellule de transition,
# en interrogeant les tables plutot qu'en lisant une figure.
#
# POURQUOI. L'ordre des bits est dans la figure 4.17 de Lengyel, qui est une
# IMAGE : ni le texte de la these ni la source des tables ne le donnent en
# clair. Le deduire d'une phrase tronquee par l'extraction serait exactement le
# genre de pari qui produit du maillage silencieusement faux.
#
# MAIS LES TABLES L'ENCODENT. transitionVertexData[cas] liste les aretes qui
# portent un sommet ; une arete ne porte un sommet que si ses deux extremites
# sont de SIGNES OPPOSES. Donc pour le bon ordre des bits, et pour lui seul, les
# 512 cas sont tous coherents. Un mauvais ordre fait apparaitre des sommets sur
# des aretes dont les deux bouts sont du meme cote : il se trahit.
#
# Rappel etabli par le texte (section 4.5) : les valeurs des echantillons 9, A,
# B, C sont IDENTIQUES a celles de 0, 2, 6, 8. C'est ce qui ramene 13
# echantillons a 9 bits.

use strict;
use warnings;

my $src = shift || 'D:/UE/Worldseed/ThirdParty/Transvoxel/Transvoxel.cpp';
open(my $fh, '<', $src) or die "ouverture impossible : $!";
my $txt = do { local $/; <$fh> };
close($fh);

sub corps
{
    my ($nom) = @_;
    my $i = index($txt, $nom);
    die "table introuvable : $nom" if $i < 0;
    my $j = index($txt, '{', $i);
    my $p = 0;
    for (my $k = $j; $k < length($txt); ++$k)
    {
        my $c = substr($txt, $k, 1);
        ++$p if $c eq '{';
        if ($c eq '}') { --$p; return substr($txt, $j, $k - $j + 1) if $p == 0; }
    }
    die "accolade non fermee : $nom";
}

# --- transitionCellClass[512] ---
my @classe = corps('transitionCellClass[512]') =~ /0x([0-9A-Fa-f]{2})/g;
@classe = map { hex } @classe;
die "classes : " . scalar(@classe) unless @classe == 512;

# --- transitionCellData[56] : compteurs + indices de sommets ---
my @dataBrut = split /\},\s*\{/, corps('transitionCellData[56]');
my @nbSommets;
for my $e (@dataBrut)
{
    if ($e =~ /0x([0-9A-Fa-f]{2})/) { push @nbSommets, (hex($1) >> 4); }
    else { push @nbSommets, 0; }   # l'entree vide {0x00, {}}
}
die "donnees : " . scalar(@nbSommets) unless @nbSommets == 56;

# --- transitionVertexData[512][12] : une LIGNE par cas ---
my $vd = corps('transitionVertexData[512][12]');
# On retire l'accolade englobante, puis on decoupe ligne a ligne.
$vd =~ s/^\s*\{//s; $vd =~ s/\}\s*$//s;
my @lignes;
while ($vd =~ /\{([^{}]*)\}/g)
{
    my @v = $1 =~ /0x([0-9A-Fa-f]+)/g;
    push @lignes, [ map { hex } @v ];
}
die "lignes de sommets : " . scalar(@lignes) unless @lignes == 512;

# --- les candidats ----------------------------------------------------------
# Disposition de la face pleine resolution, indices de tableau en ligne :
#     0 1 2
#     3 4 5
#     6 7 8
my %candidats = (
    # bit i pour l'echantillon i, tout simplement
    'sequentiel'  => [ 0, 1, 2, 3, 4, 5, 6, 7, 8 ],
    # tour du perimetre puis le centre en bit de poids fort
    'perimetre'   => [ 0, 1, 2, 5, 8, 7, 6, 3, 4 ],
    # tour du perimetre dans l'autre sens
    'perimetre2'  => [ 0, 3, 6, 7, 8, 5, 2, 1, 4 ],
);

sub eprouver
{
    my ($ordre) = @_;   # $ordre->[b] = indice d'echantillon portant le bit b
    my $fautes = 0;
    my $aretes = 0;

    for my $cas (0 .. 511)
    {
        next if ($cas == 0 || $cas == 511);

        # Signes des neuf echantillons de la face pleine resolution.
        my @dedans;
        for my $b (0 .. 8)
        {
            $dedans[ $ordre->[$b] ] = (($cas >> $b) & 1) ? 1 : 0;
        }
        # 9,A,B,C recopient 0,2,6,8 -- etabli par le texte, section 4.5.
        $dedans[0x9] = $dedans[0];
        $dedans[0xA] = $dedans[2];
        $dedans[0xB] = $dedans[6];
        $dedans[0xC] = $dedans[8];

        my $cl = $classe[$cas] & 0x7F;
        my $n  = $nbSommets[$cl];
        my $l  = $lignes[$cas];

        for my $s (0 .. $n - 1)
        {
            last unless defined $l->[$s];
            my $code = $l->[$s];
            my $c0 = ($code >> 4) & 0x0F;
            my $c1 = $code & 0x0F;
            ++$aretes;
            # Une arete ne porte un sommet que si ses bouts sont de signes
            # OPPOSES. C'est le seul controle, et il suffit.
            ++$fautes if ($dedans[$c0] == $dedans[$c1]);
        }
    }
    return ($fautes, $aretes);
}

printf("%-14s %10s %10s %8s\n", 'ordre', 'aretes', 'fautes', 'part');
for my $nom (sort keys %candidats)
{
    my ($f, $a) = eprouver($candidats{$nom});
    printf("%-14s %10d %10d %7.2f%%\n", $nom, $a, $f, $a ? 100 * $f / $a : 0);
}
