#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(facts people dies_like consistent);

# db/retractEntity: every fact of an entity, and every ref to it, go.

my $c = people('person/friends' => { type => 'ref', cardinality => 'many' });
my $r = $c->transact([
    { 'db/id' => 'a', 'person/name' => 'Ann', 'person/email' => 'a@x', 'person/age' => 61, 'person/tags' => ['p', 'q'] },
    { 'db/id' => 'b', 'person/name' => 'Bob', 'person/boss' => 'a' },
    { 'db/id' => 'c', 'person/name' => 'Cy', 'person/friends' => ['a', 'b'] },
    { 'db/id' => 's', 'person/name' => 'Sam', 'person/boss' => 's' },
]);
my ($ann, $bob, $cy, $sam) = @{ $r->{tempids} }{qw(a b c s)};
my $before = $c->db;

$r = $c->transact([[ 'db/retractEntity', $ann ]]);
is_deeply($c->db->entity($ann), { 'db/id' => $ann }, 'the entity holds nothing');
ok(!exists $c->db->entity($bob)->{'person/boss'}, 'a cardinality one ref to it is gone');
is_deeply($c->db->entity($cy)->{'person/friends'}, [ $bob ], 'a cardinality many ref to it is gone, and the others stay');
is_deeply($c->db->datoms(vae => $ann), [], 'nothing refers to it');
is_deeply($c->db->datoms(ave => 'person/name', 'Ann'), [], 'its unique values are free');
is(scalar(grep { !$_->[4] } @{ $r->{tx_data} }), 7, 'one retraction a datom: five of its own, two refs to it');
is_deeply(facts($before), facts($r->{db_before}), 'the value before still holds it all');
consistent($c->db, 'every index agrees after it');

$c->transact([[ 'db/retractEntity', ['person/name', 'Sam'] ]]);
is_deeply($c->db->entity($sam), { 'db/id' => $sam }, 'by a lookup ref, an entity that refers to itself');
consistent($c->db);

$r = $c->transact([[ 'db/retractEntity', $ann ]]);
is(scalar @{ $r->{tx_data} }, 1, 'an entity with nothing left: no datoms');

$r = $c->transact([[ 'db/retractEntity', $bob ], { 'db/id' => $bob, 'person/name' => 'Bobby' }]);
is_deeply($c->db->entity($bob), { 'db/id' => $bob, 'person/name' => 'Bobby' }, 'facts given in the same transaction stay');

dies_like(sub { $c->transact([[ 'db/retractEntity', $bob ], [ 'db/add', $bob, 'person/name', 'Bobby' ]]) },
          qr/one transaction both asserts and retracts "Bobby" of entity $bob/, 'but not one it also retracts');
dies_like(sub { $c->transact([[ 'db/retractEntity', 'x' ]]) },
          qr/\[0\] db\/retractEntity names an entity id or a lookup ref, not the tempid "x"/, 'a tempid');
dies_like(sub { $c->transact([[ 'db/retractEntity', 1 ]]) },
          qr/\[0\] entity 1 is a transaction, which cannot be retracted/, 'a transaction');
dies_like(sub { $c->transact([[ 'db/retractEntity' ]]) }, qr/db\/retractEntity is \[db\/retractEntity, entity\]/, 'no entity');
dies_like(sub { $c->transact([[ 'db/retractEntity', $bob, 1 ]]) }, qr/db\/retractEntity is \[db\/retractEntity, entity\]/, 'too much');

consistent($c->db);
undef $c; undef $r; undef $before;
is(Fact::DB::_live_blocks(), 0, 'nothing is left allocated');

done_testing;
