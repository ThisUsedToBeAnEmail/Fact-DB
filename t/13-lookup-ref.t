#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(facts people dies_like consistent);

# A lookup ref, [attribute, value] of a unique attribute, stands for the
# entity holding that value wherever an entity id can go.

my $c = people('person/friends' => { type => 'ref', cardinality => 'many' });
$c->transact([{ 'person/name' => 'Ann', 'person/email' => 'a@x', 'person/age' => 61 },
              { 'person/name' => 'Bob' }]);
my ($ann, $bob) = (2, 3);
my $db = $c->db;

is_deeply($db->entity(['person/name', 'Ann']), $db->entity($ann), 'entity by a lookup ref');
is_deeply($db->entity(['person/email', 'a@x']), $db->entity($ann), 'through a unique value attribute too');
is_deeply($db->datoms(eav => ['person/name', 'Ann'], 'person/age'), [[ $ann, 'person/age', 61, 1 ]], 'datoms by one');

$c->transact([[ 'db/add', ['person/name', 'Ann'], 'person/age', 62 ]]);
is($c->db->entity($ann)->{'person/age'}, 62, 'an op\'s entity');
$c->transact([{ 'db/id' => ['person/name', 'Bob'], 'person/age' => 40, 'person/boss' => ['person/name', 'Ann'],
                'person/friends' => [['person/email', 'a@x']] }]);
is_deeply([ grep { /^$bob / } @{ facts($c->db) } ],
          [ "$bob person/age 40", "$bob person/boss $ann", "$bob person/friends $ann", "$bob person/name Bob" ],
          'a hash\'s db/id, a ref value, and a cardinality many ref\'s values');
is_deeply([ map { $_->[0] } @{ $c->db->datoms(vae => ['person/name', 'Ann'], 'person/boss') } ], [ $bob ], 'vae by one');
$c->transact([[ 'db/retract', ['person/name', 'Bob'], 'person/friends', ['person/name', 'Ann'] ]]);
ok(!grep({ /friends/ } @{ facts($c->db) }), 'a retraction may use them for the entity and a ref value');

{
    my $w = $c->db->with([{ 'db/id' => 'n', 'person/name' => 'Cy' }, [ 'db/add', 'n', 'person/boss', ['person/name', 'Bob'] ]]);
    is($w->{db_after}->entity($w->{tempids}{n})->{'person/boss'}, $bob, 'in with too');
}

my @refusals = (
    [ 'a non-unique attribute', [['db/add', ['person/age', 62], 'person/score', 1]], qr/\[0\] person\/age is not unique, so a value of it does not name one entity/ ],
    [ 'a value nobody holds',   [['db/add', ['person/name', 'Zed'], 'person/age', 1]], qr/\[0\] no entity has person\/name "Zed"/ ],
    [ 'one element',            [['db/add', ['person/name'], 'person/age', 1]],        qr/\[0\] a lookup ref is \[attribute, value\]/ ],
    [ 'three elements',         [['db/add', ['person/name', 'Ann', 1], 'person/age', 1]], qr/a lookup ref is \[attribute, value\]/ ],
    [ 'an unknown attribute',   [['db/add', ['person/nope', 'x'], 'person/age', 1]],   qr/person\/nope is not in the schema/ ],
    [ 'a bad value',            [['db/add', ['person/email', undef], 'person/age', 1]], qr/person\/email: undef is not a value/ ],
    [ 'as a ref value',         [{ 'person/name' => 'Cy', 'person/boss' => ['person/name', 'Zed'] }], qr/\[0\] no entity has person\/name "Zed"/ ],
    [ 'one made in the same transaction', [{ 'person/name' => 'Cy' }, ['db/add', ['person/name', 'Cy'], 'person/age', 3]],
                                qr/\[1\] no entity has person\/name "Cy"/ ],
);
for my $case (@refusals) {
    my ($name, $data, $re) = @$case;
    my $before = $c->db;
    dies_like(sub { $c->transact($data) }, qr/^Fact::DB::transact: .*$re/s, $name);
    is($c->db, $before, "$name: the connection is as it was");
}
dies_like(sub { $c->db->entity(['person/name', 'Zed']) }, qr/^Fact::DB::Db::entity: no entity has person\/name "Zed"/, 'a read whose lookup finds no one');
dies_like(sub { $c->db->entity(['person/age', 1]) }, qr/^Fact::DB::Db::entity: person\/age is not unique/, 'a read through a non-unique attribute');

consistent($c->db);
undef $c; undef $db;
is(Fact::DB::_live_blocks(), 0, 'nothing is left allocated');

done_testing;
