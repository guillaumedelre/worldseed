# Quelles ARETES une cellule de transition porte-t-elle reellement ?
#
# POURQUOI LA QUESTION SE POSE. La cellule a treize coins : neuf sur la face
# pleine resolution (0 a 8) et quatre sur la face demi-resolution (9, A, B, C).
# Savoir QUELLES PAIRES de coins peuvent porter un sommet decide de tout le
# mailleur -- notamment s'il existe des aretes LATERALES reliant les deux faces,
# donc si les deux nappes partagent des sommets ou sont cousues par des
# triangles.
#
# La figure 4.18 de Lengyel le dit, mais c'est une IMAGE. Les tables, elles,
# l'encodent : transitionVertexData ne cite que des aretes existantes. On les
# enumere donc, plutot que de lire un dessin qu'on n'a pas.

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

my @classe = map { hex } corps('transitionCellClass[512]') =~ /0x([0-9A-Fa-f]{2})/g;

my @dataBrut = split /\},\s*\{/, corps('transitionCellData[56]');
my @nbSommets = map { /0x([0-9A-Fa-f]{2})/ ? (hex($1) >> 4) : 0 } @dataBrut;

my $vd = corps('transitionVertexData[512][12]');
$vd =~ s/^\s*\{//s; $vd =~ s/\}\s*$//s;
my @lignes;
while ($vd =~ /\{([^{}]*)\}/g)
{
    push @lignes, [ map { hex } $1 =~ /0x([0-9A-Fa-f]+)/g ];
}

my %paires;
my %coinsVus;
for my $cas (0 .. 511)
{
    my $n = $nbSommets[ $classe[$cas] & 0x7F ];
    for my $s (0 .. $n - 1)
    {
        last unless defined $lignes[$cas][$s];
        my $code = $lignes[$cas][$s];
        my $c0 = ($code >> 4) & 0x0F;
        my $c1 = $code & 0x0F;
        my ($a, $b) = $c0 < $c1 ? ($c0, $c1) : ($c1, $c0);
        ++$paires{"$a-$b"};
        ++$coinsVus{$a}; ++$coinsVus{$b};
    }
}

printf("coins cites : %s\n\n",
    join(' ', map { sprintf('%X', $_) } sort { $a <=> $b } keys %coinsVus));

printf("%-8s %8s  %s\n", 'arete', 'citations', 'nature');
for my $p (sort { my @x = split /-/, $a; my @y = split /-/, $b;
                  $x[0] <=> $y[0] || $x[1] <=> $y[1] } keys %paires)
{
    my ($a, $b) = split /-/, $p;
    my $nature =
        ($a <= 8 && $b <= 8)  ? 'face PLEINE resolution' :
        ($a >= 9 && $b >= 9)  ? 'face DEMI resolution'   :
                                'LATERALE (relie les deux faces)';
    printf("%X - %X  %8d  %s\n", $a, $b, $paires{$p}, $nature);
}

my $lat = grep { my ($a, $b) = split /-/, $_; $a <= 8 && $b >= 9 } keys %paires;
print "\n";
print $lat ? "$lat arete(s) LATERALE(s)\n"
           : "AUCUNE arete laterale : les deux nappes ne partagent aucun sommet.\n";
