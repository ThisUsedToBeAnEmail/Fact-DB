#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use Fact::DB;

# The grammar: every form a query and its rules can take, read into the
# structure the engine runs.

sub p { Fact::DB::_parse(@_) }
my $v = sub { ['var', $_[0]] };

{
    my $q = p('[:find ?n :where [?e :person/name ?n]]');
    is($q->{find_kind}, 'rel', 'a plain :find is a relation');
    is_deeply($q->{find}, [ $v->('?n') ], 'of its variables');
    is_deeply($q->{in}, [ ['src', '$'] ], 'with no :in, the input is $');
    is_deeply($q->{where}, [ ['pattern', '$', [ $v->('?e'), ['kw', 'person/name'], $v->('?n') ]] ], 'a pattern');
    is_deeply($q->{vars}, [qw(?n ?e)], 'variables numbered as first seen');
    is_deeply($q->{with}, [], 'no :with');
}

is(p('[:find ?n . :where [?e :a ?n]]')->{find_kind}, 'scalar', '?x . is one value');
is(p('[:find [?n ...] :where [?e :a ?n]]')->{find_kind}, 'coll', '[?x ...] is a list');
{
    my $q = p('[:find [?e ?n] :where [?e :a ?n]]');
    is($q->{find_kind}, 'tuple', '[?a ?b] is one tuple');
    is_deeply($q->{find}, [ $v->('?e'), $v->('?n') ], 'of both');
}
is_deeply(p('[:find ?a (count ?e) (min 3 ?n) :where [?e :a ?a] [?e :b ?n]]')->{find},
          [ $v->('?a'), ['agg', 'count', [ $v->('?e') ]], ['agg', 'min', [ ['int', 3], $v->('?n') ]] ],
          'aggregates, one with a count');
is_deeply(p('[:find ?a :with ?e :where [?e :a ?a]]')->{with}, [ $v->('?e') ], ':with');

{
    my $q = p('[:find ?n :in $ $old % ?x [?l ...] [?t1 ?t2] [[?r1 _]] :where [?e :a ?n] [$old ?e :b ?x]]');
    is_deeply($q->{in}, [ ['src', '$'], ['src', '$old'], ['rules'], ['scalar', [ $v->('?x') ]], ['coll', [ $v->('?l') ]],
                          ['tuple', [ $v->('?t1'), $v->('?t2') ]], ['rel', [ $v->('?r1'), ['_'] ]] ],
              'every input form');
    is($q->{where}[1][1], '$old', 'a pattern names its source');
}

is_deeply(p('[:find ?e :where [?e :a 1] [?e :b "s"] [?e :c 1.5] [?e :d true] [?e :f false] [?e _ ?x] [_ :g ?x]]')->{where},
          [ ['pattern', '$', [ $v->('?e'), ['kw', 'a'], ['int', 1] ]],
            ['pattern', '$', [ $v->('?e'), ['kw', 'b'], ['str', 's'] ]],
            ['pattern', '$', [ $v->('?e'), ['kw', 'c'], ['num', 1.5] ]],
            ['pattern', '$', [ $v->('?e'), ['kw', 'd'], ['bool', 1] ]],
            ['pattern', '$', [ $v->('?e'), ['kw', 'f'], ['bool', 0] ]],
            ['pattern', '$', [ $v->('?e'), ['_'], $v->('?x') ]],
            ['pattern', '$', [ ['_'], ['kw', 'g'], $v->('?x') ]] ],
          'constants of every kind, and _');
is_deeply(p('[:find ?e :where [?e :a ?v ?tx ?added]]')->{where}[0][2],
          [ $v->('?e'), ['kw', 'a'], $v->('?v'), $v->('?tx'), $v->('?added') ], 'all five places');
is_deeply(p('[:find ?e :where [?e]]')->{where}[0][2], [ $v->('?e') ], 'one place');
is_deeply(p('[:find ?v :where [[:person/name "Ann"] :person/age ?v]]')->{where}[0][2][0],
          ['lookup', 'person/name', ['str', 'Ann']], 'a lookup ref as the entity');
is_deeply(p('[:find ?a :where [?e ?a ?v]]')->{where}[0][2][1], $v->('?a'), 'a variable attribute');

{
    my $q = p('[:find ?n :where [?e :a ?a] [(> ?a 30)] [(str ?a "!") ?n] [(range 3) [?r ...]] [(f ?a) [?p ?q]] [(g ?a) [[?s ?t]]]]');
    is_deeply($q->{where}[1], ['pred', '>', [ $v->('?a'), ['int', 30] ]], 'a predicate');
    is_deeply($q->{where}[2], ['fn', 'str', [ $v->('?a'), ['str', '!'] ], ['scalar', [ $v->('?n') ]]], 'a function binding one variable');
    is_deeply($q->{where}[3][3], ['coll', [ $v->('?r') ]], 'a list');
    is_deeply($q->{where}[4][3], ['tuple', [ $v->('?p'), $v->('?q') ]], 'a tuple');
    is_deeply($q->{where}[5][3], ['rel', [ $v->('?s'), $v->('?t') ]], 'a relation');
}

{
    my $q = p('[:find ?e :where [?e :a ?v] (not [?e :b ?v]) (not-join [?e] [?e :c ?w] [?w :d 1])
                                   (or [?e :x 1] (and [?e :y ?v] [(> ?v 2)])) (or-join [?e] [?e :p ?z] [?e :q ?y])]');
    is_deeply($q->{where}[1], ['not', undef, [ ['pattern', '$', [ $v->('?e'), ['kw', 'b'], $v->('?v') ]] ]], 'not');
    is_deeply($q->{where}[2][1], [ $v->('?e') ], 'not-join names its join variables');
    is(scalar @{ $q->{where}[2][2] }, 2, 'and takes clauses of its own');
    is($q->{where}[3][0], 'or', 'or');
    is(scalar @{ $q->{where}[3][2] }, 2, 'of two branches');
    is(scalar @{ $q->{where}[3][2][1] }, 2, 'one an and of two clauses');
    is_deeply($q->{where}[4][1], [ $v->('?e') ], 'or-join');
}

{
    my $q = p('[:find ?who :in $ % ?b :where (anc ?a ?b) [?a :name ?who]]',
              '[[(anc ?a ?b) [?b :parent ?a]] [(anc ?a ?b) [?b :parent ?c] (anc ?a ?c)]]');
    is_deeply($q->{where}[0], ['rule', 'anc', [ $v->('?a'), $v->('?b') ]], 'a rule call');
    is(scalar @{ $q->{rules} }, 2, 'two rules');
    is_deeply($q->{rules}[1], ['anc', [ $v->('?a'), $v->('?b') ],
                               [ ['pattern', '$', [ $v->('?b'), ['kw', 'parent'], $v->('?c') ]], ['rule', 'anc', [ $v->('?a'), $v->('?c') ]] ]],
              'a recursive rule, its variables its own');
}

{
    my $vec = p('[:find ?n :in $ ?x :where [?e :a ?x] [?e :b ?n]]');
    my $map = p('{:find [?n] :in [$ ?x] :where [[?e :a ?x] [?e :b ?n]]}');
    is_deeply($map, $vec, 'the map form reads as the vector form');
}

ok(p('[:find ?x :where [?e :n 1] (or [?e :a ?x] [?e :b ?x]) [(> ?x 2)]]'),
   'a variable every branch of an or binds is bound after it, for a predicate and for :find');
ok(p('[:find ?e ?z :where [?e :n 1] (or-join [?e ?z] [?e :a ?z] (and [?e :b ?y] [(inc ?y) ?z]))]'),
   'and an or-join\'s join variables, each branch binding them its own way');
is_deeply(p("[:find ?n\n ; why\n :where [?e :a ?n]]")->{find}, [ $v->('?n') ], 'comments in a query');
is_deeply(p(qq{[:find ?n :where [?e :name "\x{263a}"] [?e :a ?n]]})->{where}[0][2][2], ['str', "\x{263a}"], 'a wide string');

done_testing;
