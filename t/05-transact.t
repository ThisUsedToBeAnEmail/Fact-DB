#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(facts index_of log_of people);

# transact: the ops, the forms, tempids, and what a transaction reports.

my $c = people();
my $db0 = $c->db;
isa_ok($db0, 'Fact::DB::Db');
is($c->db, $db0, 'db hands out the same object while it lives');
is_deeply(facts($db0), [], 'a new connection holds no facts');
is(Fact::DB::_versions($c), 1, 'and one database value, the empty one');
is($db0->_dump->{next_id}, 1, 'whose next id is 1');

my $r = $c->transact([
    { 'db/id' => 'ann', 'person/name' => 'Ann', 'person/age' => 61, 'person/tags' => ['a', 'b'] },
    { 'person/name' => 'Bob', 'person/boss' => 'ann' },
]);
is($r->{tx}, 1, 'the transaction takes the first id');
is_deeply($r->{tempids}, { ann => 2 }, 'then each tempid in the order seen; a hash with no db/id is not listed');
is($r->{db_before}, $db0, 'db_before is the value it started from');
is($r->{db_after}, $c->db, 'db_after is the connection\'s value now');
isnt($r->{db_after}, $db0, 'a new one');
is(Fact::DB::_versions($c), 2, 'which the connection keeps beside the old');
is_deeply(facts($c->db), [
    '2 person/age 61', '2 person/name Ann', '2 person/tags a', '2 person/tags b',
    '3 person/boss 2', '3 person/name Bob',
], 'the facts, a tempid as a ref value resolved, the hash without db/id given id 3');
is_deeply(facts($db0), [], 'and the value it started from holds none still');

{
    my @txi = grep { $_->[1] eq 'db/txInstant' } @{ $r->{tx_data} };
    is(scalar @txi, 1, 'tx_data has the transaction\'s own datom');
    is($r->{tx_data}[0], $txi[0], 'first');
    is($txi[0][0], 1, 'on the transaction entity');
    cmp_ok(abs($txi[0][2] - time), '<', 5, 'with the time it was made');
    is_deeply([ map { [ @$_[0, 1, 2, 3, 4] ] } @{ $r->{tx_data} }[1 .. $#{ $r->{tx_data} }] ], [
        [2, 'person/age', 61, 1, 1], [2, 'person/name', 'Ann', 1, 1],
        [2, 'person/tags', 'a', 1, 1], [2, 'person/tags', 'b', 1, 1],
        [3, 'person/boss', 2, 1, 1], [3, 'person/name', 'Bob', 1, 1],
    ], 'and every datom made, in the order made: attributes of a hash in the order of their names');
}

my $db1 = $c->db;
my $r2 = $c->transact([[ 'db/add', 2, 'person/age', 62 ]]);
is($r2->{tx}, 4, 'the next transaction takes the next id');
is_deeply([ map { [ @$_[0, 1, 2, 3, 4] ] } @{ $r2->{tx_data} }[1, 2] ],
          [ [2, 'person/age', 61, 4, 0], [2, 'person/age', 62, 4, 1] ],
          'a new value of a cardinality one attribute retracts the old one, then asserts the new');
is_deeply(index_of($c->db, 'person/age'), { 62 => [2] }, 'the index moves with it');
is_deeply(index_of($db1, 'person/age'), { 61 => [2] }, 'and the old value\'s index does not');

my $r3 = $c->transact([[ 'db/add', 2, 'person/age', 62 ], [ 'db/add', 2, 'person/tags', 'a' ]]);
is(scalar @{ $r3->{tx_data} }, 1, 'asserting what is already true makes no datom');
is($r3->{tx}, 5, 'but the transaction still happens');
is_deeply(facts($c->db), facts($r3->{db_before}), 'and changes no fact');

my $r4 = $c->transact([[ 'db/retract', 2, 'person/tags', 'a' ], [ 'db/retract', 2, 'person/tags', 'zzz' ],
                       [ 'db/retract', 3, 'person/age', 1 ]]);
is_deeply([ map { [ @$_[0, 1, 2, 4] ] } @{ $r4->{tx_data} }[1 .. $#{ $r4->{tx_data} }] ],
          [ [2, 'person/tags', 'a', 0] ],
          'a retraction of a fact that is true makes a datom; of one that is not, nothing');
is_deeply(facts($c->db), [
    '2 person/age 62', '2 person/name Ann', '2 person/tags b', '3 person/boss 2', '3 person/name Bob',
], 'the facts after it');

{
    my $r5 = $c->transact([[ 'db/retract', 3, 'person/name', 'Bob' ], [ 'db/retract', 3, 'person/boss', 2 ]]);
    ok(!exists $c->db->_dump->{eav}{3}, 'an entity with no facts left has no record');
    ok(exists $r5->{db_before}->_dump->{eav}{3}, 'though the value before still has it');
    ok(!exists index_of($c->db, 'person/name')->{Bob}, 'a retracted unique value leaves its index');
    is_deeply(index_of($c->db, 'person/boss'), {}, 'and a retracted ref leaves the index of what points where');
    is(index_of($r5->{db_before}, 'person/name')->{Bob}, 3, 'which the value before still has');
}

{
    my $r6 = $c->transact([]);
    is(scalar @{ $r6->{tx_data} }, 1, 'an empty transaction makes only its own datom');
    is_deeply($r6->{tempids}, {}, 'and no tempids');
}

{
    my $r7 = $c->transact([
        [ 'db/add', 'x', 'person/name', 'Xia' ],
        { 'db/id' => 'y', 'person/boss' => 'x', 'person/name' => 'Yu' },
        [ 'db/add', 'x', 'person/boss', 'y' ],
    ]);
    my ($x, $y) = @{ $r7->{tempids} }{qw(x y)};
    is($y, $x + 1, 'tempids get ids in the order first seen, across ops and hashes');
    is($x, $r7->{tx} + 1, 'after the transaction\'s own');
    is_deeply([ grep { /^(\Q$x\E|\Q$y\E) / } @{ facts($c->db) } ],
              [ "$x person/boss $y", "$x person/name Xia", "$y person/boss $x", "$y person/name Yu" ],
              'one tempid is one entity wherever it appears, as an entity or a ref');
}

{
    my $r8 = $c->transact([{ 'db/id' => 2 }, { 'db/id' => 'nobody' }]);
    is_deeply($r8->{tempids}, { nobody => $r8->{tx} + 1 }, 'a hash of only db/id names its entity and asserts nothing');
    is(scalar @{ $r8->{tx_data} }, 1, 'so makes no datom');
}

{
    my $r9 = $c->transact([{ 'person/name' => "Zo\x{eb}", 'person/score' => '1e3', 'person/age' => '042' }]);
    my ($e) = map { $_->[0] } grep { $_->[1] eq 'person/name' } @{ $r9->{tx_data} };
    my %got = map { ($_->[1] => $_->[2]) } grep { $_->[0] == $e } @{ $r9->{tx_data} };
    is_deeply(\%got, { 'person/name' => "Zo\x{eb}", 'person/score' => 1000, 'person/age' => 42 },
              'values are kept as their types have them');
    is_deeply(index_of($c->db, 'person/age')->{42}, [$e], 'and indexed so');
}

is_deeply(log_of($c->db)->[1], [4, ['2 person/age 61 0', '2 person/age 62 1']],
          'the log has every transaction\'s datoms, in the order made');
is(scalar @{ log_of($c->db) }, Fact::DB::_versions($c) - 1, 'one entry a transaction');
is_deeply(log_of($db1), [ log_of($c->db)->[0] ], 'and an old value\'s log ends where it did');

{
    my $held = $c->db;
    my $facts = facts($held);
    $c->transact([[ 'db/add', 2, 'person/age', 100 + $_ ]]) for 1 .. 1000;
    is_deeply(facts($held), $facts, 'a value held through 1,000 more transactions reads as it did');
    is_deeply([ grep { /^2 person\/age/ } @{ facts($c->db) } ], ['2 person/age 1100'], 'while the latest has moved on');
}

{
    my $c2 = people();
    $c2->transact([{ 'person/name' => 'Ann' }]);
    my $before = $c2->db;
    $c2->add_schema({ 'pet/name' => { type => 'string', index => 1 } });
    is_deeply(facts($before), ['2 person/name Ann'], 'a value made before add_schema reads as before');
    $c2->transact([{ 'db/id' => 2, 'pet/name' => 'Rex' }]);
    is_deeply(facts($c2->db), ['2 person/name Ann', '2 pet/name Rex'], 'and a new attribute takes facts at once');
    is_deeply(index_of($c2->db, 'pet/name'), { Rex => [2] }, 'indexed');
}

{
    my $c3 = people();
    my $r = $c3->transact([{ 'person/name' => 'Ann' }]);
    undef $c3;
    is_deeply(facts($r->{db_after}), ['2 person/name Ann'], 'a value outlives its connection');
    undef $r;
}

undef $c; undef $db0; undef $db1; undef $r; undef $r2; undef $r3; undef $r4;
is(Fact::DB::_live_blocks(), 0, 'and every value made is freed when nothing holds it');

done_testing;
