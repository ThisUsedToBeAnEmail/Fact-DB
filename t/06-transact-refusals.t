#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(facts people dies_like);

# Every refusal says why, and leaves the connection as it was: the same
# value, the same versions, nothing allocated left behind.

my $c = people();
$c->transact([{ 'db/id' => 'ann', 'person/name' => 'Ann', 'person/email' => 'a@x', 'person/age' => 61 },
              { 'person/name' => 'Bob' }]);

my @refusals = (
    [ 'not an array',           { a => 1 },                                         qr/the transaction is an array reference/ ],
    [ 'a plain item',           ['x'],                                              qr/\[0\] an item is an entity as a hash reference or an op/ ],
    [ 'an object item',         [bless {}, 'X'],                                    qr/\[0\] an item is/ ],
    [ 'a short op',             [['db/add', 2, 'person/age']],                      qr/\[0\] an op is \[op, entity, attribute, value\]/ ],
    [ 'a long op',              [['db/add', 2, 'person/age', 1, 2]],                qr/an op is \[op, entity/ ],
    [ 'an unknown op',          [['db/put', 2, 'person/age', 1]],                   qr/unknown op "db\/put"; the ops are db\/add, db\/retract, db\/retractEntity and db\/cas/ ],
    [ 'an unknown attribute',   [['db/add', 2, 'person/height', 1]],                qr/person\/height is not in the schema/ ],
    [ 'the database\'s own',    [['db/add', 2, 'db/txInstant', 1]],                 qr/db\/txInstant is the database's own/ ],
    [ 'an unknown hash key',    [{ 'person/name' => 'Cy', 'pet/name' => 'Rex' }],    qr/pet\/name is not in the schema/ ],
    [ 'a bad value',            [['db/add', 2, 'person/age', 'old']],               qr/\[0\] person\/age: "old" is not a whole number/ ],
    [ 'undef',                  [{ 'person/name' => undef }],                       qr/person\/name: undef is not a value/ ],
    [ 'a bad list element',     [{ 'person/tags' => ['a', undef] }],                qr/person\/tags: undef is not a value/ ],
    [ 'a list for one',         [{ 'person/age' => [1, 2] }],                       qr/person\/age: a cardinality one attribute takes one value, not a list/ ],
    [ 'an id not issued',       [['db/add', 99, 'person/age', 1]],                  qr/there is no entity 99/ ],
    [ 'id zero',                [['db/add', 0, 'person/age', 1]],                   qr/an entity id is a positive whole number, not "0"/ ],
    [ 'a fractional id',        [['db/add', '2.5', 'person/age', 1]],               qr/an entity id is a positive whole number, not "2\.5"/ ],
    [ 'an undef entity',        [['db/add', undef, 'person/age', 1]],               qr/an entity is an entity id, a tempid or a lookup ref, not undef/ ],
    [ 'a reference entity',     [['db/add', {}, 'person/age', 1]],                  qr/an entity is an entity id, a tempid or a lookup ref, not a reference/ ],
    [ 'an empty tempid',        [['db/add', '', 'person/age', 1]],                  qr/a tempid is a string with something in it/ ],
    [ 'a retraction by tempid', [['db/retract', 'ann', 'person/age', 61]],          qr/a retraction names an entity id or a lookup ref, not the tempid "ann"/ ],
    [ 'a retracted tempid ref', [['db/retract', 2, 'person/boss', 'x']],            qr/a retraction names an entity id or a lookup ref, not the tempid "x"/ ],
    [ 'a tempid only a value',  [{ 'person/name' => 'Cy', 'person/boss' => 'ghost' }], qr/the tempid "ghost" is used only as a value/ ],
    [ 'assert and retract',     [['db/add', 2, 'person/tags', 'a'], ['db/retract', 2, 'person/tags', 'a']],
                                qr/\[1\] person\/tags: one transaction both asserts and retracts "a" of entity 2/ ],
    [ 'two values for one',     [['db/add', 2, 'person/age', 1], ['db/add', 2, 'person/age', 2]],
                                qr/\[1\] person\/age: one transaction gives entity 2 two values of a cardinality one attribute/ ],
    [ 'two values by tempid',   [{ 'db/id' => 'n', 'person/age' => 1 }, ['db/add', 'n', 'person/age', 2]],
                                qr/two values of a cardinality one attribute/ ],
    [ 'a unique value held',    [{ 'person/email' => 'a@x' }],                      qr/person\/email: "a\@x" is already held by entity 2/ ],
    [ 'a unique identity held', [['db/add', 3, 'person/name', 'Ann']],              qr/person\/name: "Ann" is already held by entity 2/ ],
    [ 'a late refusal',         [{ 'person/name' => 'Cy' }, ['db/add', 2, 'person/email', 'b@x'],
                                 { 'person/email' => 'b@x' }],                      qr/\[2\] person\/email: "b\@x" is already held/ ],
);

for my $case (@refusals) {
    my ($name, $data, $re) = @$case;
    my $db = $c->db;
    my $facts = facts($db);
    my $versions = Fact::DB::_versions($c);
    my $blocks = Fact::DB::_live_blocks();
    dies_like(sub { $c->transact($data) }, qr/^Fact::DB::transact: .*$re/s, $name);
    is($c->db, $db, "$name: the connection's value is the same object");
    is(Fact::DB::_versions($c), $versions, "$name: and it holds no new version");
    is_deeply(facts($c->db), $facts, "$name: nor any new fact");
    is(Fact::DB::_live_blocks(), $blocks, "$name: and nothing is left allocated");
}

{
    my $db = $c->db;
    my $blocks = Fact::DB::_live_blocks();
    dies_like(sub { $db->with([['db/add', 99, 'person/age', 1]]) }, qr/^Fact::DB::Db::with: \[0\] there is no entity 99/,
              'with refuses as transact does, by its own name');
    is(Fact::DB::_live_blocks(), $blocks, 'and leaves nothing allocated');
}

dies_like(sub { Fact::DB::transact('Fact::DB', []) }, qr/^Fact::DB::transact: not a Fact::DB connection/, 'transact on no connection');
dies_like(sub { Fact::DB::Db::with($c, []) }, qr/^Fact::DB::Db::with: not a Fact::DB::Db/, 'with on a connection');

undef $c;
is(Fact::DB::_live_blocks(), 0, 'every refusal freed what it made');

done_testing;
