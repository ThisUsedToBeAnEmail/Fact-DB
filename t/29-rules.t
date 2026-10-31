#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(people dies_like);
use Oracle qw(evaluate render render_rules);

# Rules: recursion over chains, trees and cycles, alternatives, rules that
# call rules, and random rule sets against a plain evaluator.

my $anc = q{[[(anc ?a ?b) [?b :person/boss ?a]] [(anc ?a ?b) [?b :person/boss ?c] (anc ?a ?c)]]};
sub names { [ sort map { $_->[0] } @{ $_[0] } ] }

{
    my $c = people();
    $c->transact([ map { { 'db/id' => "t$_", 'person/name' => "p$_", ($_ > 1 ? ('person/boss' => 't' . ($_ - 1)) : ()) } } 1 .. 20 ]);
    my $db = $c->db;
    is_deeply(names($db->q('[:find ?n :in $ % ?b :where [?e :person/name ?b] (anc ?a ?e) [?a :person/name ?n]]', $anc, 'p20')),
              [ sort map { "p$_" } 1 .. 19 ], 'a chain: every one above');
    is_deeply(names($db->q('[:find ?n :in $ % ?a :where [?e :person/name ?a] (anc ?e ?d) [?d :person/name ?n]]', $anc, 'p18')),
              ['p19', 'p20'], 'and every one below, with the other argument bound');
    is(scalar @{ $db->q('[:find ?a ?b :in $ % :where (anc ?a ?b)]', $anc) }, 190, 'and every pair, with neither bound');
}
{
    my $c = people();
    $c->transact([ { 'db/id' => 'r', 'person/name' => 'root' },
                   (map { { 'db/id' => "k$_", 'person/name' => "k$_", 'person/boss' => 'r' } } 1 .. 3),
                   (map { my $k = $_; map { { 'person/name' => "g$k$_", 'person/boss' => "k$k" } } 1 .. 2 } 1 .. 3) ]);
    is_deeply(names($c->db->q('[:find ?n :in $ % :where [?r :person/name "root"] (anc ?r ?d) [?d :person/name ?n]]', $anc)),
              [qw(g11 g12 g21 g22 g31 g32 k1 k2 k3)], 'a tree: everyone under its root');
}
{
    my $c = people('person/friends' => { type => 'ref', cardinality => 'many' });
    $c->transact([ { 'db/id' => 'a', 'person/name' => 'a', 'person/friends' => ['b'] }, { 'db/id' => 'b', 'person/name' => 'b', 'person/friends' => ['c'] },
                   { 'db/id' => 'c', 'person/name' => 'c', 'person/friends' => ['a'] }, { 'person/name' => 'd' } ]);
    my $reach = q{[[(reach ?x ?y) [?x :person/friends ?y]] [(reach ?x ?y) [?x :person/friends ?z] (reach ?z ?y)]]};
    is_deeply(names($c->db->q('[:find ?n :in $ % :where [?a :person/name "a"] (reach ?a ?y) [?y :person/name ?n]]', $reach)),
              [qw(a b c)], 'a cycle: it ends, everyone on it reached once, itself among them');
    is(scalar @{ $c->db->q('[:find ?x ?y :in $ % :where (reach ?x ?y)]', $reach) }, 9, 'every pair on the cycle, no duplicates');
}
{
    my $c = people();
    $c->transact([ map { { 'db/id' => "t$_", 'person/name' => "p$_", 'person/age' => $_, ($_ > 1 ? ('person/boss' => 't' . ($_ - 1)) : ()) } } 1 .. 6 ]);
    my $db = $c->db;
    my $even = q{[[(even ?x) [?x :person/name "p1"]] [(even ?x) [?x :person/boss ?y] (odd ?y)]
                  [(odd ?x) [?x :person/boss ?y] (even ?y)]]};
    is_deeply(names($db->q('[:find ?n :in $ % :where (even ?x) [?x :person/name ?n]]', $even)), [qw(p1 p3 p5)], 'mutual recursion');
    my $alts = q{[[(near ?x) [?x :person/age 1]] [(near ?x) [?x :person/age 2]] [(near ?x) [?x :person/name "p6"]]]};
    is_deeply(names($db->q('[:find ?n :in $ % :where (near ?x) [?x :person/name ?n]]', $alts)), [qw(p1 p2 p6)], 'alternatives');
    my $calls = q{[[(boss2 ?a ?b) [?b :person/boss ?m] [?m :person/boss ?a]] [(skip ?a ?b) (boss2 ?a ?b)] [(skip ?a ?b) (boss2 ?a ?m) (skip ?m ?b)]]};
    is_deeply(names($db->q('[:find ?n :in $ % :where [?e :person/name "p6"] (skip ?a ?e) [?a :person/name ?n]]', $calls)), [qw(p2 p4)],
              'a rule calling a rule');
    is_deeply(names($db->q('[:find ?n :in $ % :where [?e :person/name ?n] [?e :person/age ?g] (anc ?b ?e) [(> ?g 4)]]', $anc)), [qw(p5 p6)],
              'a rule\'s answers joined on');
    is_deeply(names($db->q('[:find ?n :in $ % :where (same ?x ?x) [?x :person/name ?n]]', q{[[(same ?a ?b) [?a :person/age ?n] [?b :person/age ?n]]]})),
              [ map { "p$_" } 1 .. 6 ], 'one variable twice in a call: the answers whose two places agree');
    is_deeply($db->q('[:find ?x ?y :in $ % :where (same ?x ?y) [(!= ?x ?y)]]', q{[[(same ?a ?b) [?a :person/age ?n] [?b :person/age ?n]]]}), [],
              'and no others, ages being all different');
    is_deeply($db->q('[:find ?x :in $ % :where (over ?x ?x)]', q{[[(over ?a ?b) [?b :person/boss ?a]]]}), [],
              'a call with one variable twice: nobody is their own boss');
    my ($p1, $p2) = map { $db->q(qq{[:find ?e . :where [?e :person/name "p$_"]]}) } 1, 2;
    is_deeply($db->q(qq{[:find ?a :in \$ % :where (eq $p1 $p2) [(ground 1) ?a]]}, q{[[(eq ?a ?a) [?a :person/age _]]]}), [],
              'a head with one variable twice, called with two different values: no answer');
    is_deeply($db->q(qq{[:find ?a :in \$ % :where (eq $p1 $p1) [(ground 1) ?a]]}, q{[[(eq ?a ?a) [?a :person/age _]]]}), [[1]],
              'and with the same value twice: an answer');
}

# ---- random rule sets against the plain evaluator

my $c = people('person/friends' => { type => 'ref', cardinality => 'many' });
srand 1010;
my @ents;
for my $i (1 .. 25) {
    my %h = ('person/name' => "n$i", 'person/age' => int rand 5);
    $h{'person/boss'} = $ents[rand @ents] if @ents && rand() < 0.7;
    $h{'person/friends'} = [ @ents[ map { int rand @ents } 1 .. 2 ] ] if @ents > 2 && rand() < 0.6;
    my $r = $c->transact([\%h]);
    push @ents, $r->{tx} + 1;
}
$c->transact([[ 'db/add', $ents[0], 'person/friends', $ents[-1] ]]);
my $db = $c->db;
my $datoms = $db->datoms('eav');
my ($agreed, $tried) = (0, 0);
my @wrong;
for my $n (1 .. 300) {
    my $A = (qw(person/boss person/friends))[rand 2];
    my $B = (qw(person/boss person/friends))[rand 2];
    my %rules = (r1 => [ [ ['?x', '?y'], [ ['pattern', [ ['var', '?x'], ['kw', $A], ['var', '?y'] ]] ] ] ]);
    push @{ $rules{r1} }, rand() < 0.5
        ? [ ['?x', '?y'], [ ['pattern', [ ['var', '?x'], ['kw', $B], ['var', '?z'] ]], ['rule', 'r1', [ ['var', '?z'], ['var', '?y'] ]] ] ]
        : [ ['?x', '?y'], [ ['rule', 'r1', [ ['var', '?x'], ['var', '?z'] ]], ['pattern', [ ['var', '?z'], ['kw', $B], ['var', '?y'] ]] ] ];
    $rules{r2} = [ [ ['?x', '?y'], [ ['rule', 'r1', [ ['var', '?x'], ['var', '?y'] ]], ['pattern', [ ['var', '?y'], ['kw', 'person/age'], ['var', '?k'] ]],
                                   ['pred', '<', [ ['var', '?k'], ['int', 1 + int rand 4] ]] ] ] ] if rand() < 0.5;
    my $name = $rules{r2} && rand() < 0.5 ? 'r2' : 'r1';
    my $e = $ents[rand @ents];
    my $k = int rand 3;
    my @args = $k == 0 ? (['var', '?x'], ['var', '?y']) : $k == 1 ? (['int', $e], ['var', '?y']) : (['var', '?x'], ['int', $e]);
    my @find = grep { my $v = $_; grep { $_->[0] eq 'var' && $_->[1] eq $v } @args } qw(?x ?y);
    my $q = { find => \@find, where => [ ['rule', $name, \@args] ] };
    if (rand() < 0.3) { push @{ $q->{where} }, ['not', [ ['pattern', [ ['var', $find[0]], ['kw', 'person/age'], ['int', 0] ]] ]] }
    my $want = evaluate($q, $datoms, 50_000, \%rules) or next;
    $tried++;
    my ($text, $rtext) = (render($q, 1), render_rules(\%rules));
    my $got = eval { $db->q($text, $rtext) };
    if (!$got) { push @wrong, [$text, $rtext, "died: $@"] if @wrong < 3; next }
    $got = [ sort { join("\0", @$a) cmp join("\0", @$b) } @$got ];
    if (Test::More::_deep_check($got, $want)) { $agreed++ } elsif (@wrong < 3) { push @wrong, [$text, $rtext, $got, $want] }
}
is($agreed, $tried, "the engine and the plain evaluator agree on $tried random rule sets and calls") or diag explain \@wrong;
cmp_ok($tried, '>', 250, 'and most of them were tried');

is_deeply($db->q('[:find ?x :in $ % :where (r ?x)]', q{[[(r ?a) (r ?a)]]}), [], 'a rule that only calls itself ends, with nothing');
is_deeply(scalar @{ $db->q('[:find ?x :in $ % :where (r ?x)]', q{[[(r ?a) [?a :person/name _] (r ?a)]]}) }, 0,
          'and one that also needs itself to hold: nothing either');

done_testing;
