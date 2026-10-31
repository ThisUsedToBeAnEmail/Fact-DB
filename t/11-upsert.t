#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(facts people dies_like consistent);

# Upsert: a tempid whose entity is given a unique identity value someone
# holds is that someone.

my $c = people('person/code' => { type => 'string', unique => 'identity' },
               'person/friends' => { type => 'ref', cardinality => 'many' });
my $r = $c->transact([{ 'person/name' => 'Ann', 'person/age' => 61, 'person/code' => 'A1' }]);
my $ann = 2;
is($r->{tx_data}[1][0], $ann, 'Ann is entity 2');

$r = $c->transact([{ 'person/name' => 'Ann', 'person/age' => 62 }]);
is_deeply([ map { [ @$_[0, 1, 2, 4] ] } @{ $r->{tx_data} }[1, 2] ],
          [ [$ann, 'person/age', 61, 0], [$ann, 'person/age', 62, 1] ],
          'a hash naming Ann by her name is Ann');
is($c->db->_dump->{next_id}, $r->{tx} + 1, 'and gives out no id but the transaction\'s');

$r = $c->transact([{ 'db/id' => 'a', 'person/name' => 'Ann' }, [ 'db/add', 'a', 'person/tags', 'x' ],
                   { 'db/id' => 'b', 'person/name' => 'Bob', 'person/boss' => 'a', 'person/friends' => ['a'] }]);
is($r->{tempids}{a}, $ann, 'a tempid that upserts is the existing id');
my $bob = $r->{tempids}{b};
is($bob, $r->{tx} + 1, 'and new entities take the ids after the transaction\'s');
is_deeply([ grep { /^$ann person\/tags|^$bob person\/(boss|friends)/ } @{ facts($c->db) } ],
          [ "$ann person/tags x", "$bob person/boss $ann", "$bob person/friends $ann" ],
          'wherever the tempid is used, as an entity or a ref');

$r = $c->transact([{ 'db/id' => 'x', 'person/name' => 'Cy', 'person/age' => 30 },
                   { 'db/id' => 'y', 'person/name' => 'Cy', 'person/tags' => ['t'] },
                   { 'person/name' => 'Cy', 'person/score' => 1.5 }]);
is($r->{tempids}{x}, $r->{tempids}{y}, 'tempids given one new identity value in one transaction are one entity');
my $cy = $r->{tempids}{x};
is($c->db->_dump->{next_id}, $cy + 1, 'given one id between them');
is_deeply($c->db->entity($cy), { 'db/id' => $cy, 'person/name' => 'Cy', 'person/age' => 30,
                                 'person/tags' => ['t'], 'person/score' => 1.5 },
          'which holds what all three said');

$r = $c->transact([{ 'db/id' => 'z', 'person/code' => 'A1', 'person/email' => 'ann@x' }]);
is($r->{tempids}{z}, $ann, 'any identity attribute upserts, not only the first');

dies_like(sub { $c->transact([{ 'person/name' => 'Ann', 'person/code' => 'B9' }, { 'person/name' => 'Bob', 'person/code' => 'B9' }]) },
          qr/person\/(code|name): one entity's identity values are held by two entities, \d+ and \d+/,
          'two identity values held by two entities, joined in one transaction');
$c->transact([{ 'person/name' => 'Bob', 'person/code' => 'B1' }]);
dies_like(sub { $c->transact([{ 'person/name' => 'Ann', 'person/code' => 'B1' }]) },
          qr/^Fact::DB::transact: \[0\] person\/(name|code): one entity's identity values are held by two entities, $ann and $bob|$bob and $ann/,
          'one hash whose identity values are two entities\'');

dies_like(sub { $c->transact([{ 'db/id' => $bob, 'person/name' => 'Ann' }]) },
          qr/person\/name: "Ann" is already held by entity $ann/,
          'an entity named by its id is not upserted: Ann\'s name for Bob croaks');
dies_like(sub { $c->transact([{ 'person/email' => 'ann@x' }]) },
          qr/person\/email: "ann\@x" is already held by entity $ann/,
          'a unique value attribute does not upsert');

{
    my $w = $c->db->with([{ 'db/id' => 'n', 'person/name' => 'Ann', 'person/age' => 1 }]);
    is($w->{tempids}{n}, $ann, 'with upserts as transact does');
}

{
    my $r2 = $c->transact([[ 'db/add', $bob, 'person/email', 'ann@x' ], [ 'db/retract', $ann, 'person/email', 'ann@x' ]]);
    is($c->db->entity($bob)->{'person/email'}, 'ann@x', 'a unique value may pass to another entity: given first, then given up');
    my $r3 = $c->transact([[ 'db/retract', $bob, 'person/email', 'ann@x' ], [ 'db/add', $ann, 'person/email', 'ann@x' ]]);
    is($c->db->entity($ann)->{'person/email'}, 'ann@x', 'or given up first');
    my $r4 = $c->transact([[ 'db/add', $bob, 'person/email', 'ann@x' ], [ 'db/add', $ann, 'person/email', 'new@x' ]]);
    is_deeply([ $c->db->entity($ann)->{'person/email'}, $c->db->entity($bob)->{'person/email'} ], [ 'new@x', 'ann@x' ],
              'or left behind by a new value of a cardinality one attribute');
}

$c->transact([{ 'person/name' => 'Cy', 'person/code' => 'C1' }]);
is($c->db->entity($cy)->{'person/code'}, 'C1', 'a new identity value for an upserted entity');
$c->transact([[ 'db/add', $cy, 'person/name', 'Cyrus' ]]);
is_deeply($c->db->datoms(ave => 'person/name', 'Cyrus')->[0][0], $cy, 'a new name moves the index');
is_deeply($c->db->datoms(ave => 'person/name', 'Cy'), [], 'and frees the old one');
{
    my $rq = $c->transact([{ 'db/id' => 'q', 'person/name' => 'Cy' }]);
    isnt($rq->{tempids}{q}, $cy, 'so the old name, held by no one now, names a new entity');
    is($rq->{tempids}{q}, $rq->{tx} + 1, 'given the next id');
}

consistent($c->db);
undef $c; undef $r;
is(Fact::DB::_live_blocks(), 0, 'nothing is left allocated');

done_testing;
