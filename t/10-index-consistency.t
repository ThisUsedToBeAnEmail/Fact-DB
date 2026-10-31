#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(people);

# Every index agrees with every other, at every 10th version of a random
# run, and HAMT finds nothing wrong with any map in them.

my $c = people('person/likes' => { type => 'string', cardinality => 'many', index => 1 },
               'person/friends' => { type => 'ref', cardinality => 'many' });
is_deeply($c->db->_check, [], 'the empty value is consistent');

srand 7;
my @ents;
my @held;
my $n = 0;
for my $i (1 .. 400) {
    my @data;
    for (1 .. 1 + int rand 6) {
        if (!@ents || rand() < 0.3) {
            $n++;
            push @data, { 'person/name' => "n$n", 'person/age' => int rand 5, 'person/likes' => [ grep { rand > 0.5 } qw(x y z) ] || ['x'],
                          (@ents ? ('person/friends' => [ $ents[rand @ents] ], 'person/boss' => $ents[rand @ents]) : ()) };
            $data[-1]{'person/likes'} = ['x'] unless @{ $data[-1]{'person/likes'} };
            next;
        }
        my $e = $ents[rand @ents];
        my $k = int rand 4;
        push @data, $k == 0 ? [ 'db/add', $e, 'person/likes', (qw(x y z))[rand 3] ]
                  : $k == 1 ? [ 'db/retract', $e, 'person/likes', (qw(x y z))[rand 3] ]
                  : $k == 2 ? [ 'db/add', $e, 'person/friends', $ents[rand @ents] ]
                  :           [ 'db/retract', $e, 'person/friends', $ents[rand @ents] ];
    }
    my %seen;
    @data = grep { !ref($_) || ref($_) eq 'HASH' || !$seen{"$_->[1] $_->[2] $_->[3]"}++ } @data;
    my $r = eval { $c->transact(\@data) } or next;
    push @ents, values %{ $r->{tempids} };
    my %tx = map { $_ => 1 } $r->{tx};
    push @ents, grep { !$tx{$_} } map { $_->[0] } grep { $_->[1] eq 'person/name' } @{ $r->{tx_data} };
    my %u; @ents = grep { !$u{$_}++ } @ents;
    if ($i % 10 == 0) {
        my $bad = $c->db->_check;
        is_deeply($bad, [], "after $i transactions every index agrees") or diag explain $bad;
        push @held, $c->db;
    }
}
is_deeply([ map { @{ $_->_check } } @held ], [], 'and every value held along the way still does');
undef $c; @held = ();
is(Fact::DB::_live_blocks(), 0, 'nothing is left allocated');

done_testing;
