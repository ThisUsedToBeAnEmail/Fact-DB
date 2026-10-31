#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(facts people dies_like consistent);

# since: the facts a value holds that were asserted after a transaction.

my $c = people();
my $t1 = $c->transact([{ 'db/id' => 'a', 'person/name' => 'Ann', 'person/age' => 61, 'person/tags' => ['old'] },
                       { 'db/id' => 'b', 'person/name' => 'Bob' }])->{tx};
my ($ann, $bob) = (2, 3);
my $t2 = $c->transact([[ 'db/add', $ann, 'person/age', 62 ]])->{tx};
my $t3 = $c->transact([[ 'db/add', $bob, 'person/tags', 'x' ]])->{tx};
my $t4 = $c->transact([[ 'db/retract', $bob, 'person/tags', 'x' ]])->{tx};
my $t5 = $c->transact([[ 'db/add', $bob, 'person/tags', 'x' ], [ 'db/add', $ann, 'person/score', 1.5 ]])->{tx};
my $db = $c->db;

is_deeply(facts($db->since($t1)), [ "$ann person/age 62", "$ann person/score 1.5", "$bob person/tags x" ],
          'since the first transaction: what was asserted after it and is still true');
ok(!grep({ /person\/name|old/ } @{ facts($db->since($t1)) }), 'a fact asserted before and still true is not there');
is_deeply(facts($db->since($t3)), [ "$ann person/score 1.5", "$bob person/tags x" ],
          'a fact asserted, retracted and asserted again is there once, from its last assertion');
is_deeply($db->since($t3)->datoms(eav => $bob), [ [ $bob, 'person/tags', 'x', $t5 ] ], 'with the transaction that last asserted it');
is_deeply(facts($db->since($t2 - 1)), facts($db->since($t1)), 'the id between two transactions is the earlier one');
is_deeply(facts($db->since(0)), facts($db), 'since 0: every fact');
is_deeply(facts($db->since($t5)), [], 'since the last: nothing');
is($db->since($t5)->count, 0, 'counted as nothing');
is($db->since($t3)->count, 4, 'a since value counts its own datoms: two facts and two transactions\' own');
is_deeply($db->since($t1)->datoms(ave => 'person/age', 62), [ [ $ann, 'person/age', 62, $t2 ] ], 'its indexes hold only its facts');
is_deeply($db->since($t1)->datoms(ave => 'person/age', 61), [], 'and not the ones before');
is_deeply($db->since($t1)->entity($ann), { 'db/id' => $ann, 'person/age' => 62, 'person/score' => 1.5 }, 'entity shows only them');
is($db->since($t1), $db->since($t1), 'asked again for the same transaction, the same value');
consistent($db->since($t1), 'its indexes agree');

{
    my $old = $db->as_of($t3)->since($t1);
    is_deeply(facts($old), [ "$ann person/age 62", "$bob person/tags x" ], 'since on an old value: as it was then');
}

dies_like(sub { $db->since(-2) }, qr/^Fact::DB::Db::since: a transaction is a whole number, 0 or more/, 'a negative id');
dies_like(sub { $db->since($t1)->since($t2) }, qr/since is for a value a transaction made, not a since value/, 'since on a since value');
dies_like(sub { $db->since($t1)->with([]) }, qr/with is for a value a transaction made, not a since value/, 'with on a since value');

undef $c; undef $db;
is(Fact::DB::_live_blocks(), 0, 'nothing is left allocated');

done_testing;
