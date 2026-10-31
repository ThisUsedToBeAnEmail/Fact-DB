#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(people dies_like);

# A rule called with an argument bound works out only what that argument
# reaches: the ancestors of one person, not of everyone.

my $c = people();
my @data;
for my $t (1 .. 20) {
    push @data, { 'db/id' => "r$t", 'person/name' => "r$t" };
    push @data, map { { 'db/id' => "t${t}_$_", 'person/name' => "t${t}_$_", 'person/boss' => ($_ == 1 ? "r$t" : "t${t}_" . ($_ - 1)) } } 1 .. 10;
}
$c->transact(\@data);
my $db = $c->db;
my $anc = q{[[(anc ?a ?b) [?b :person/boss ?a]] [(anc ?a ?b) [?b :person/boss ?c] (anc ?a ?c)]]};

Fact::DB::_tried();
my $one = $db->q('[:find ?n :in $ % ?b :where [?e :person/name ?b] (anc ?a ?e) [?a :person/name ?n]]', $anc, 't3_10');
my $bound = Fact::DB::_tried();
is(scalar @$one, 10, 'ancestors of the deepest of one chain of 20');
my $all = $db->q('[:find ?a ?b :in $ % :where (anc ?a ?b)]', $anc);
my $free = Fact::DB::_tried();
is(scalar @$all, 20 * 55, 'every pair, with nothing bound');
cmp_ok($bound * 10, '<', $free, "bound, it tries $bound datoms; free, $free: the one chain, not all twenty");

{
    my $once = $db->q('[:find ?n :in $ % [?b ...] :where [?e :person/name ?b] (anc ?a ?e) [?a :person/name ?n]]', $anc, ['t3_10']);
    my $t1 = Fact::DB::_tried();
    my $thrice = $db->q('[:find ?n :in $ % [?b ...] :where [?e :person/name ?b] (anc ?a ?e) [?a :person/name ?n]]', $anc, ['t3_10', 't3_10', 't3_10']);
    my $t3 = Fact::DB::_tried();
    is_deeply($thrice, $once, 'one goal called from three rows');
    cmp_ok($t3, '<', $t1 * 1.5, "is worked out once, and its answers kept for the rest ($t1 tried once, $t3 for three)");
}

{
    my $c2 = people();
    $c2->transact([ map { { 'db/id' => "t$_", 'person/name' => "p$_", ($_ > 1 ? ('person/boss' => 't' . ($_ - 1)) : ()) } } 1 .. 1100 ]);
    dies_like(sub { $c2->db->q('[:find ?a :in $ % ?b :where [?e :person/name ?b] (anc ?a ?e)]', $anc, 'p1100') },
              qr/^Fact::DB::Db::q: line 1, column \d+: rule anc calls itself more than 1000 deep/, 'recursion past 1,000 deep is refused, not a crash');
    is(scalar @{ $c2->db->q('[:find ?a :in $ % ?b :where [?e :person/name ?b] (anc ?a ?e)]', $anc, 'p900') }, 899, 'and 900 deep works');
}
{
    my $c3 = people();
    $c3->transact([ map { { 'person/name' => "p$_", 'person/age' => $_ } } 1 .. 3 ]);
    my $grow = q{[[(n ?x) [?e :person/age ?x]] [(n ?y) (n ?x) [(inc ?x) ?y]]]};
    dies_like(sub { $c3->db->q('[:find ?x :in $ % :where (n ?x)]', $grow) },
              qr/rule n did not settle in 1000 rounds; it may grow without end/,
              'a rule that grows without end through a function is stopped');
}

done_testing;
