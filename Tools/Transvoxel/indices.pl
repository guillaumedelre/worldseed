# Les tables referencent-elles un sommet AU-DELA du compte declare ?
#
# `Locaux[12]` n-est pas initialise dans le mailleur : si vertexIndex cite un
# indice >= GetVertexCount(), on lit de la memoire non ecrite et l-on pose un
# triangle entre des sommets quelconques du chunk. C-est exactement la
# signature observee -- des rubans qui traversent le paysage.
use strict; use warnings;
my $src = shift || 'D:/UE/Worldseed/ThirdParty/Transvoxel/Transvoxel.cpp';
open(my $fh,'<',$src) or die $!; my $txt = do { local $/; <$fh> }; close $fh;
sub corps { my ($n)=@_; my $i=index($txt,$n); my $j=index($txt,'{',$i); my $p=0;
  for (my $k=$j;$k<length($txt);$k++){my $c=substr($txt,$k,1); $p++ if $c eq '{';
  if($c eq '}'){$p--; return substr($txt,$j,$k-$j+1) if !$p;}} die }
for my $t (['regularCellData[16]',15], ['transitionCellData[56]',36]) {
  my ($nom,$max) = @$t;
  my @e = split /\},\s*\{/, corps($nom);
  my ($pires,$fautes) = (0,0);
  for my $i (0..$#e) {
    next unless $e[$i] =~ /0x([0-9A-Fa-f]{2})\s*,\s*\{([^}]*)\}/;
    my $nb = hex($1) >> 4; my $tri = hex($1) & 15;
    my @v = $2 =~ /(\d+)/g;
    my @u = @v[0 .. ($tri*3 - 1)];
    for my $x (@u) { $fautes++, printf("  classe %2d : sommet %d cite pour %d declares\n",$i,$x,$nb) if $x >= $nb; }
    $pires = $nb if $nb > $pires;
  }
  printf("%-26s %d classes, %d sommets au plus, %d CITATIONS HORS BORNES\n", $nom, scalar(@e), $pires, $fautes);
}
