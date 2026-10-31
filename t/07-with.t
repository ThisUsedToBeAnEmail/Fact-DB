#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(facts people);

# with: a transaction against any database value, kept by nothing.

my $c = people();
$c->transact([{ 'db/id' => 'ann', 'person/name' => 'Ann', 'person/age' => 61 }]);
my $old = $c->db;
$c->transact([[ 'db/add', 2, 'person/age', 62 ]]);
my $now = $c->db;
my $versions = Fact::DB::_versions($c);

my $w = $now->with([[ 'db/add', 2, 'person/age', 63 ], { 'db/id' => 'cy', 'person/name' => 'Cy' }]);
is($w->{db_before}, $now, 'with starts from the value it is called on');
is_deeply(facts($w->{db_after}), ['2 person/age 63', '2 person/name Ann', '5 person/name Cy'],
          'and holds the transaction\'s facts');
is($w->{tx}, 4, 'its transaction takes the value\'s next id');
is_deeply($w->{tempids}, { cy => 5 }, 'and its tempids the ones after');
is($c->db, $now, 'the connection\'s value does not move');
is(Fact::DB::_versions($c), $versions, 'nor does the number it holds');
is_deeply(facts($now), ['2 person/age 62', '2 person/name Ann'], 'and the value it was called on is unchanged');

my $w2 = $w->{db_after}->with([[ 'db/retract', 5, 'person/name', 'Cy' ]]);
is_deeply(facts($w2->{db_after}), ['2 person/age 63', '2 person/name Ann'], 'with on a with value');
is($w2->{tx}, 6, 'numbering on from it');

my $wo = $old->with([[ 'db/add', 2, 'person/age', 70 ]]);
is_deeply(facts($wo->{db_after}), ['2 person/age 70', '2 person/name Ann'], 'with on an older value');
is($wo->{tx}, 3, 'whose next id is the one it had');
is_deeply([ map { [ @$_[0, 1, 2, 4] ] } @{ $wo->{tx_data} }[1, 2] ],
          [ [2, 'person/age', 61, 0], [2, 'person/age', 70, 1] ],
          'and whose facts are the ones it holds, not the latest');

my $r = $c->transact([[ 'db/add', 2, 'person/age', 64 ]]);
is($r->{tx}, 4, 'transact is not moved by a with');
is_deeply(facts($w->{db_after}), ['2 person/age 63', '2 person/name Ann', '5 person/name Cy'],
          'and a with value still reads as it did');

undef $c; undef $old; undef $now; undef $w; undef $w2; undef $wo; undef $r;
is(Fact::DB::_live_blocks(), 0, 'nothing is left allocated');

done_testing;
