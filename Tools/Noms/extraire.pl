#!/usr/bin/perl
# Extrait les bases de noms d'Azgaar (src/data/name-bases.ts) en JSON.
#
# LA CONVERSION EST MECANIQUE ET REJOUABLE, comme celle des tables Transvoxel :
# une table recopiee a la main est une table fausse, et 43 bases de plusieurs
# centaines de mots ne se relisent pas a l'oeil.
use strict;
use warnings;
use open qw(:std :encoding(UTF-8));
use JSON::PP;

local $/;
open my $f, '<:encoding(UTF-8)', 'nb.ts' or die "nb.ts: $!";
my $t = <$f>;
close $f;

my @out;
my $re = qr{
    name: \s* " ( (?: [^"\\] | \\. )* ) " \s* , \s*
    i:    \s* (\d+)            \s* , \s*
    min:  \s* (\d+)            \s* , \s*
    max:  \s* (\d+)            \s* , \s*
    d:    \s* " ( (?: [^"\\] | \\. )* ) " \s* , \s*
    m:    \s* ([\d.]+)         \s* , \s*
    b:    \s* " ( (?: [^"\\] | \\. )* ) "
}xs;

while ($t =~ /$re/g) {
    my ($n, $i, $min, $max, $d, $m, $b) = ($1, $2, $3, $4, $5, $6, $7);
    for ($n, $d, $b) { s/\\"/"/g; s/\\\\/\\/g; }
    my @mots;
    for my $w (split /,/, $b) {
        $w =~ s/^\s+|\s+$//g;
        push @mots, $w if length $w;
    }
    push @out, {
        nom  => $n,
        i    => $i + 0,
        min  => $min + 0,
        max  => $max + 0,
        dupl => $d,
        mots => \@mots,
    };
}

printf("bases extraites : %d\n", scalar @out);
my $tot = 0;
$tot += scalar @{ $_->{mots} } for @out;
printf("mots au total   : %d\n", $tot);
for my $b (@out) {
    printf("  %-16s i=%-2d min=%-2d max=%-2d d=%-6s %5d mots\n",
        $b->{nom}, $b->{i}, $b->{min}, $b->{max}, '"' . $b->{dupl} . '"',
        scalar @{ $b->{mots} });
}

open my $o, '>:raw', 'azgaar_bases.json' or die $!;
print $o JSON::PP->new->utf8->canonical->pretty->encode(\@out);
close $o;
