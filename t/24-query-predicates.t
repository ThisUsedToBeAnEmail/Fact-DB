#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(people dies_like);

# The built-in predicates and functions.

my $c = people('person/born' => { type => 'instant' });
$c->transact([ { 'db/id' => 'ann', 'person/name' => 'Ann', 'person/age' => 61, 'person/score' => 2.5, 'person/born' => 100 },
               { 'person/name' => 'Bob', 'person/age' => 40, 'person/boss' => 'ann' } ]);
my $db = $c->db;
sub one { my ($w, @in) = @_; $db->q("[:find ?r . :in \$ ?x ?y :where [(ground 1) ?one] $w]", @in) }
sub truth { my ($p, @in) = @_; my $r = $db->q("[:find ?t . :in \$ ?x ?y :where [(ground 1) ?t] [$p]]", @in); defined $r ? 1 : 0 }

is(truth('(< ?x ?y)', 1, 2), 1, '<');
is(truth('(< ?x ?y)', 2, 2), 0, '< not when equal');
is(truth('(<= ?x ?y)', 2, 2), 1, '<=');
is(truth('(> ?x ?y)', 3, 2), 1, '>');
is(truth('(>= ?x ?y)', 2, 3), 0, '>= not when less');
is(truth('(= ?x ?y)', 2, '2.0'), 1, '= of numbers by value');
is(truth('(= ?x ?y)', 'a', 'a'), 1, '= of strings');
is(truth('(!= ?x ?y)', 'a', 'b'), 1, '!=');
is(truth('(< ?x ?y)', 'apple', 'banana'), 1, 'strings ordered as strings');
is(truth('(< ?x ?y)', 1, 'banana'), 0, 'a number and a string are not ordered');
is(truth('(> ?x ?y)', 1, 'banana'), 0, 'either way');
is(truth('(= ?x ?y)', 1, 'one'), 0, 'nor equal');
is(truth('(!= ?x ?y)', 1, 'one'), 1, 'so they are not the same');
is(truth('(< 1 ?x ?y)', 2, 3), 1, 'a chain of three');
is(truth('(< 1 ?x ?y)', 3, 2), 0, 'broken anywhere');

is(one('[(+ ?x ?y 1) ?r]', 2, 3), 6, '+');
is(one('[(- ?x ?y) ?r]', 2, 3), -1, '-');
is(one('[(- ?x) ?r]', 2, 0), -2, '- of one');
is(one('[(* ?x ?y) ?r]', 2.5, 4), 10, '*');
is(one('[(/ ?x ?y) ?r]', 7, 2), 3.5, '/');
is(one('[(quot ?x ?y) ?r]', -7, 2), -3, 'quot truncates');
is(one('[(rem ?x ?y) ?r]', -7, 2), -1, 'rem has the sign of the dividend');
is(one('[(mod ?x ?y) ?r]', -7, 2), 1, 'mod the sign of the divisor');
is(one('[(inc ?x) ?r]', 41, 0), 42, 'inc');
is(one('[(dec ?x) ?r]', 1.5, 0), 0.5, 'dec');
is(one('[(str ?x "-" ?y) ?r]', 'a', 7), 'a-7', 'str');
is(one('[(subs ?x 1 4) ?r]', "caf\x{e9}s", 0), "af\x{e9}", 'subs, by characters, its end not included');
is(one('[(subs ?x 1 3) ?r]', "\x{263a}\x{263a}\x{263a}x", 0), "\x{263a}\x{263a}", 'subs of wide characters');
is(one('[(subs ?x 2) ?r]', 'hello', 0), 'llo', 'subs to the end');
is(one('[(count ?x) ?r]', "\x{263a}ab", 0), 3, 'count of a string, in characters');
is(one('[(ground ?x) ?r]', 'g', 0), 'g', 'ground');

is_deeply([ sort map { $_->[0] } @{ $db->q('[:find ?n :where [?e :person/name ?n] [(missing? $ ?e :person/boss)]]') } ], ['Ann'], 'missing?');
is_deeply([ sort { $a->[0] cmp $b->[0] } @{ $db->q('[:find ?n ?b :where [?e :person/name ?n] [(get-else $ ?e :person/age 0) ?b]]') } ],
          [['Ann', 61], ['Bob', 40]], 'get-else with a value');
is_deeply($db->q('[:find ?n ?b :where [?e :person/name ?n] [(get-else $ ?e :person/score -1) ?b] [(< ?b 0)]]'), [['Bob', -1]], 'and with the default');

is_deeply($db->q('[:find ?n :where [?e :person/age ?a] [?e :person/score ?s] [(> ?a ?s)] [?e :person/name ?n]]'), [['Ann']],
          'an int and a num compare as numbers');
is_deeply($db->q('[:find ?n :where [?e :person/born ?b] [(= ?b 100)] [?e :person/name ?n]]'), [['Ann']], 'an instant and a constant');
is_deeply($db->q('[:find ?n :where [?e :person/boss ?b] [(= ?b ?b)] [?b :person/name ?n]]'), [['Ann']], 'a ref compares as its id');

is_deeply($db->q('[:find ?n :where [?e :person/name ?n] [(ground "Ann") ?n]]'), [['Ann']],
          'a function binding a variable already bound keeps the rows where they agree');
dies_like(sub { one('[(+ ?x ?y) ?r]', 1, 'x') }, qr/line 1, column \d+: \+ takes numbers, and "x" is not one/, 'arithmetic on a string');
dies_like(sub { one('[(/ ?x ?y) ?r]', 1, 0) }, qr/\/ by zero/, 'dividing by zero');
dies_like(sub { one('[(quot ?x ?y) ?r]', 1, 0) }, qr/quot by zero/, 'quot by zero');
dies_like(sub { one('[(quot ?x ?y) ?r]', 1.5, 1) }, qr/quot takes whole numbers/, 'quot of a fraction');
dies_like(sub { one('[(inc ?x ?y) ?r]', 1, 1) }, qr/inc takes 1 arguments, not 2/, 'inc of two');
dies_like(sub { one('[(subs ?x 9) ?r]', 'abc', 0) }, qr/subs: 9 to 3 is not within a string of 3/, 'subs past the end');
dies_like(sub { one('[(nosuch ?x) ?r]', 1, 0) }, qr/there is no function nosuch, built in or given in functions/, 'an unknown function');
dies_like(sub { $db->q('[:find ?e :where [?e :person/name ?n] [(missing? ?e :person/boss)]]') },
          qr/missing\? takes 3 arguments, not 2/, 'missing? without its source');
dies_like(sub { $db->q('[:find ?e :where [?e :person/name ?n] [(get-else $ ?e :person/tags 0) ?t]]') },
          qr/get-else is for a cardinality one attribute/, 'get-else on many');
dies_like(sub { $db->q('[:find ?e :where [?e :person/nope ?n]]') }, qr/line 1, column 22: :person\/nope is not in the schema/,
          'an attribute not in the schema');

done_testing;
