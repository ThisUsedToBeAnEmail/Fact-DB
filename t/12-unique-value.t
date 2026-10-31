#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(people dies_like consistent);

# unique => 'value': no two entities hold the same value, whichever way
# they try, and a transaction is judged by where it ends.

my $c = people();
$c->transact([{ 'db/id' => 'a', 'person/name' => 'Ann', 'person/email' => 'a@x' },
              { 'db/id' => 'b', 'person/name' => 'Bob', 'person/email' => 'b@x' }]);
my ($ann, $bob) = (2, 3);

dies_like(sub { $c->transact([[ 'db/add', $bob, 'person/email', 'a@x' ]]) },
          qr/^Fact::DB::transact: \[0\] person\/email: "a\@x" is already held by entity $ann/,
          'a value another entity holds');
dies_like(sub { $c->transact([{ 'person/name' => 'Cy', 'person/email' => 'c@x' }, { 'person/name' => 'Dan', 'person/email' => 'c@x' }]) },
          qr/\[1\] person\/email: "c\@x" is already held by entity \d+/,
          'one new value given to two new entities in one transaction');
my $r = $c->transact([[ 'db/add', $ann, 'person/email', 'a@x' ]]);
is(scalar @{ $r->{tx_data} }, 1, 'the holder asserting it again is no change');

$r = $c->transact([[ 'db/add', $ann, 'person/email', 'b@x' ], [ 'db/add', $bob, 'person/email', 'a@x' ]]);
is_deeply([ map { $c->db->entity($_)->{'person/email'} } $ann, $bob ], [ 'b@x', 'a@x' ],
          'two entities may swap values in one transaction');
is_deeply([ map { $_->[0] } @{ $c->db->datoms(ave => 'person/email', 'a@x') } ], [ $bob ], 'the index follows');
is_deeply([ map { $_->[0] } @{ $c->db->datoms(ave => 'person/email', 'b@x') } ], [ $ann ], 'both ways');
consistent($c->db, 'and agrees with the records');

dies_like(sub { $c->transact([[ 'db/add', $ann, 'person/email', 'a@x' ], [ 'db/add', $bob, 'person/email', 'z@x' ],
                              [ 'db/add', $bob, 'person/name', 'Ann' ]]) },
          qr/\[2\] person\/name: "Ann" is already held by entity $ann/,
          'a held value croaks even after others in the same transaction moved fine');
is($c->db->entity($ann)->{'person/email'}, 'b@x', 'and none of them moved');
consistent($c->db);

{
    my $c2 = people();
    $c2->transact([{ 'person/name' => 'Ann', 'person/email' => 'a@x' }]);
    my $r2 = $c2->transact([[ 'db/retract', 2, 'person/email', 'a@x' ], { 'person/name' => 'Eve', 'person/email' => 'a@x' }]);
    my $eve = (grep { $_->[1] eq 'person/name' } @{ $r2->{tx_data} })[0][0];
    is($c2->db->entity($eve)->{'person/email'}, 'a@x', 'a value given up may go to a new entity');
    consistent($c2->db);
}

undef $c; undef $r;
is(Fact::DB::_live_blocks(), 0, 'nothing is left allocated');

done_testing;
