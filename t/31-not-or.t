#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(people);

# not, not-join, or, or-join, and, and nestings of them.

my $c = people();
$c->transact([ { 'db/id' => 'a', 'person/name' => 'A', 'person/age' => 30, 'person/tags' => ['x'] },
               { 'db/id' => 'b', 'person/name' => 'B', 'person/age' => 40, 'person/boss' => 'a', 'person/tags' => ['x', 'y'] },
               { 'person/name' => 'C', 'person/age' => 50, 'person/boss' => 'b' },
               { 'person/name' => 'D', 'person/boss' => 'b', 'person/tags' => ['y'] } ]);
my $db = $c->db;
sub names { [ sort map { $_->[0] } @{ $db->q(@_) } ] }

is_deeply(names('[:find ?n :where [?e :person/name ?n] (not [?e :person/boss _])]'), ['A'], 'not');
is_deeply(names('[:find ?n :where [?e :person/name ?n] (not [?e :person/tags "x"] [?e :person/tags "y"])]'), [qw(A C D)],
          'not of two clauses: gone only when both match');
is_deeply(names('[:find ?n :where [?e :person/name ?n] (not-join [?e] [?k :person/boss ?e])]'), [qw(C D)], 'not-join: nobody under them');
is_deeply(names('[:find ?n :where [?e :person/name ?n] (not-join [?e] [?e :person/boss ?b] [?b :person/age 40])]'), [qw(A B)],
          'not-join with its own variables');
is_deeply(names('[:find ?n :where [?e :person/name ?n] [?x :person/age 30] (not-join [?e] [?e :person/boss ?x])]'), ['A'],
          'a variable bound outside a not-join and not joined is its own inside it: any boss, not A');
is_deeply(names('[:find ?n :where [?e :person/name ?n] (or [?e :person/age 30] [?e :person/age 50])]'), [qw(A C)], 'or');
is_deeply(names('[:find ?n :where [?e :person/name ?n] (or [?e :person/tags "x"] [?e :person/tags "y"])]'), [qw(A B D)],
          'or: in both branches is once');
is_deeply(names('[:find ?n :where [?e :person/name ?n] (or-join [?e] (and [?e :person/age ?g] [(> ?g 35)]) [?e :person/tags "y"])]'), [qw(B C D)],
          'and inside or-join, a branch with a variable of its own');
is_deeply(names('[:find ?n :where [?e :person/name ?n] (or (and [?e :person/age ?g] [(> ?g 45)]) (and [?e :person/age ?g] [(< ?g 35)]))]'), [qw(A C)],
          'and inside or, both branches binding the same variables');
is_deeply(names('[:find ?n :where [?e :person/name ?n] (or-join [?e] [?e :person/boss ?x] [?e :person/tags ?x])]'), [qw(A B C D)],
          'or-join: one variable named in both branches but not joined');
{
    my $r = $db->q('[:find ?n ?x :where [?e :person/name ?n] (or-join [?e] [?e :person/boss ?x] [?e :person/age ?x]) [?e :person/age ?x]]');
    is_deeply([ sort { $a->[0] cmp $b->[0] } @$r ], [['A', 30], ['B', 40], ['C', 50]], 'or-join keeps only its join variables: ?x is bound after it');
}
is_deeply(names('[:find ?n :where [?e :person/name ?n] (not (or [?e :person/age 30] [?e :person/age 40]))]'), [qw(C D)], 'or inside not');
is_deeply(names('[:find ?n :where [?e :person/name ?n] (or (not [?e :person/boss _]) [?e :person/age 50])]'), [qw(A C)], 'not inside or');
is_deeply(names('[:find ?n :in $ % :where [?e :person/name ?n] (not (top ?e))]', q{[[(top ?x) [?x :person/name _] (not [?x :person/boss _])]]}),
          [qw(B C D)], 'not of a rule that itself negates');

done_testing;
