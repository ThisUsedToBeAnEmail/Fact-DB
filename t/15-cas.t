#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(people dies_like consistent);

# db/cas: a new value if the current one is the one expected.

my $c = people();
$c->transact([{ 'person/name' => 'Ann', 'person/age' => 61 }, { 'person/name' => 'Bob' }]);
my ($ann, $bob) = (2, 3);

my $r = $c->transact([[ 'db/cas', $ann, 'person/age', 61, 62 ]]);
is($c->db->entity($ann)->{'person/age'}, 62, 'the value expected: the new one is asserted');
is_deeply([ map { [ @$_[1, 2, 4] ] } @{ $r->{tx_data} }[1, 2] ], [ ['person/age', 61, 0], ['person/age', 62, 1] ],
          'retracting the old as any new value does');
$c->transact([[ 'db/cas', ['person/name', 'Ann'], 'person/age', '062', 63 ]]);
is($c->db->entity($ann)->{'person/age'}, 63, 'the expected value judged by its type; the entity by a lookup ref');
$c->transact([[ 'db/cas', $bob, 'person/age', undef, 40 ]]);
is($c->db->entity($bob)->{'person/age'}, 40, 'undef expects no value');
$c->transact([[ 'db/cas', $bob, 'person/boss', undef, ['person/name', 'Ann'] ]]);
$c->transact([[ 'db/cas', $bob, 'person/boss', ['person/name', 'Ann'], $bob ]]);
is($c->db->entity($bob)->{'person/boss'}, $bob, 'a ref expected by a lookup ref');
$r = $c->transact([[ 'db/cas', $ann, 'person/age', 63, 63 ]]);
is(scalar @{ $r->{tx_data} }, 1, 'the new value the same as the old: no datom');

$c->transact([[ 'db/add', $bob, 'person/name', "\x{263a}" ]]);
{
    my $bytes = "\x{263a}";
    utf8::encode($bytes);
    dies_like(sub { $c->transact([[ 'db/cas', $bob, 'person/name', $bytes, 'Bobby' ]]) },
              qr/db\/cas: person\/name of entity $bob is/,
              'a string expected is compared as characters: the same bytes, read as Latin-1, are another string');
    $c->transact([[ 'db/cas', $bob, 'person/name', "\x{263a}", 'Bob' ]]);
    is($c->db->entity($bob)->{'person/name'}, 'Bob', 'and the same characters match');
}

my $before = $c->db;
dies_like(sub { $c->transact([[ 'db/cas', $ann, 'person/age', 60, 64 ]]) },
          qr/^Fact::DB::transact: \[0\] db\/cas: person\/age of entity $ann is "63", not "60"/, 'another value expected');
dies_like(sub { $c->transact([[ 'db/cas', $ann, 'person/age', undef, 64 ]]) },
          qr/db\/cas: person\/age of entity $ann is "63", not nothing/, 'none expected, one there');
dies_like(sub { $c->transact([[ 'db/cas', $ann, 'person/score', 1, 2 ]]) },
          qr/db\/cas: person\/score of entity $ann is nothing, not "1"/, 'one expected, none there');
dies_like(sub { $c->transact([[ 'db/cas', $ann, 'person/tags', 'a', 'b' ]]) },
          qr/person\/tags: db\/cas is for a cardinality one attribute/, 'cardinality many');
dies_like(sub { $c->transact([[ 'db/cas', 'x', 'person/age', undef, 1 ]]) },
          qr/db\/cas names an entity id or a lookup ref, not the tempid "x"/, 'a tempid');
dies_like(sub { $c->transact([[ 'db/cas', $ann, 'person/age', 63 ]]) },
          qr/db\/cas is \[db\/cas, entity, attribute, old, new\]/, 'four elements');
dies_like(sub { $c->transact([[ 'db/cas', $ann, 'person/age', 'x', 1 ]]) },
          qr/person\/age: "x" is not a whole number/, 'an expected value its type refuses');
dies_like(sub { $c->transact([[ 'db/cas', $ann, 'person/age', 63, undef ]]) },
          qr/person\/age: undef is not a value/, 'no new value');
dies_like(sub { $c->transact([[ 'db/cas', $ann, 'person/age', 63, 64 ], [ 'db/cas', $ann, 'person/age', 63, 65 ]]) },
          qr/two values of a cardinality one attribute/, 'two in one transaction, each judged against the value it started from');
is($c->db, $before, 'none of them changed anything');

consistent($c->db);
undef $c; undef $r; undef $before;
is(Fact::DB::_live_blocks(), 0, 'nothing is left allocated');

done_testing;
