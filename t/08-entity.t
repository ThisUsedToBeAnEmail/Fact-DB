#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(people dies_like);

# entity: an entity as a hash reference, every type read back.

my $c = people(
    'person/ok'    => { type => 'bool' },
    'person/born'  => { type => 'instant' },
    'person/likes' => { type => 'string', cardinality => 'many', index => 1 },
);
my $r = $c->transact([
    { 'db/id' => 'ann', 'person/name' => "Ann \x{263a}", 'person/email' => "caf\x{e9}\@x", 'person/age' => '061',
      'person/tags' => ['b', 'a', 'c'], 'person/score' => '2.5', 'person/ok' => 'yes',
      'person/born' => 1e9, 'person/likes' => ['tea'] },
    { 'db/id' => 'bob', 'person/name' => 'Bob', 'person/boss' => 'ann', 'person/ok' => 0 },
]);
my ($ann, $bob) = @{ $r->{tempids} }{qw(ann bob)};
my $db = $c->db;

my $e = $db->entity($ann);
is_deeply([ sort keys %$e ], [ sort qw(db/id person/name person/email person/age person/tags person/score
                                       person/ok person/born person/likes) ],
          'every attribute it holds, and db/id');
is($e->{'db/id'}, $ann, 'db/id is its id');
is($e->{'person/name'}, "Ann \x{263a}", 'a string with wide characters');
ok(utf8::is_utf8($e->{'person/name'}), 'flagged as such');
is($e->{'person/email'}, "caf\x{e9}\@x", 'a Latin-1 string');
is($e->{'person/age'}, 61, 'an int, as its type keeps it');
is($e->{'person/score'}, 2.5, 'a num');
is($e->{'person/ok'}, 1, 'a bool, as 1');
is($e->{'person/born'}, 1e9, 'an instant');
is_deeply([ sort @{ $e->{'person/tags'} } ], [qw(a b c)], 'a cardinality many attribute, as a list');
is_deeply($e->{'person/likes'}, ['tea'], 'a list even when it holds one value');
ok(!exists $e->{'person/boss'}, 'an attribute it does not hold is not there');

my $b = $db->entity($bob);
is($b->{'person/boss'}, $ann, 'a ref is the id of the entity it refers to');
is($b->{'person/ok'}, 0, 'false is 0');

{
    my $tx = $db->entity($r->{tx});
    is_deeply([ sort keys %$tx ], ['db/id', 'db/txInstant'], 'a transaction is an entity, with its time');
    cmp_ok(abs($tx->{'db/txInstant'} - time), '<', 5, 'which is now');
}

$e->{'person/age'} = 99;
push @{ $e->{'person/tags'} }, 'z';
is($db->entity($ann)->{'person/age'}, 61, 'the hash is the caller\'s to change: the value is not');
is(scalar @{ $db->entity($ann)->{'person/tags'} }, 3, 'nor its lists');

my $r2 = $c->transact([[ 'db/retract', $bob, 'person/name', 'Bob' ], [ 'db/retract', $bob, 'person/boss', $ann ],
                       [ 'db/retract', $bob, 'person/ok', 0 ]]);
is_deeply($c->db->entity($bob), { 'db/id' => $bob }, 'an entity with no facts left is just its id');
is($r2->{db_before}->entity($bob)->{'person/name'}, 'Bob', 'and the value before still has them');

my $w = $c->db->with([{ 'db/id' => 'cy', 'person/name' => 'Cy' }]);
is($w->{db_after}->entity($w->{tempids}{cy})->{'person/name'}, 'Cy', 'a with value reads like any other');
dies_like(sub { $c->db->entity($w->{tempids}{cy}) }, qr/^Fact::DB::Db::entity: there is no entity \d+/,
          'and what it made is not in the value it was made from');

dies_like(sub { $db->entity(999) }, qr/^Fact::DB::Db::entity: there is no entity 999/, 'an id not given out');
dies_like(sub { $db->entity(0) }, qr/an entity id is a positive whole number, not "0"/, 'zero');
dies_like(sub { $db->entity('ann') }, qr/an entity id is a positive whole number, not "ann"/, 'a tempid');
dies_like(sub { $db->entity(undef) }, qr/not undef/, 'undef');
dies_like(sub { Fact::DB::Db::entity($c, 1) }, qr/^Fact::DB::Db::entity: not a Fact::DB::Db/, 'a connection');

undef $c; undef $db; undef $r; undef $r2; undef $w; undef $e; undef $b;
is(Fact::DB::_live_blocks(), 0, 'nothing is left allocated');

done_testing;
