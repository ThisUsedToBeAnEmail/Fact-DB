#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(people);

# The find forms, with results and without.

my $c = people();
$c->transact([ { 'person/name' => 'Ann', 'person/age' => 30, 'person/tags' => ['x', 'y'] },
               { 'person/name' => 'Bob', 'person/age' => 30 } ]);
my $db = $c->db;

is_deeply([ sort { $a->[0] cmp $b->[0] } @{ $db->q('[:find ?n ?a :where [?e :person/name ?n] [?e :person/age ?a]]') } ],
          [['Ann', 30], ['Bob', 30]], 'a relation: a list of tuples');
is_deeply($db->q('[:find ?a :where [?e :person/age ?a]]'), [[30]], 'duplicates removed');
is($db->q('[:find ?n . :where [?e :person/name ?n] [?e :person/tags "x"]]'), 'Ann', 'one value');
is_deeply([ sort @{ $db->q('[:find [?t ...] :where [?e :person/tags ?t]]') } ], ['x', 'y'], 'a list of values');
is_deeply($db->q('[:find [?a ...] :where [?e :person/age ?a]]'), [30], 'a list, duplicates removed');
is_deeply($db->q('[:find [?n ?a] :where [?e :person/name ?n] [?e :person/age ?a] [(= ?n "Bob")]]'), ['Bob', 30], 'one tuple');

is_deeply($db->q('[:find ?n :where [?e :person/name ?n] [?e :person/age 99]]'), [], 'a relation of nothing is empty');
is($db->q('[:find ?n . :where [?e :person/name ?n] [?e :person/age 99]]'), undef, 'one value of nothing is undef');
is_deeply($db->q('[:find [?n ...] :where [?e :person/name ?n] [?e :person/age 99]]'), [], 'a list of nothing is empty');
is($db->q('[:find [?n ?a] :where [?e :person/name ?n] [?e :person/age 99] [?e :person/age ?a]]'), undef, 'a tuple of nothing is undef');

is_deeply($db->q('{:find [?n] :where [[?e :person/tags "y"] [?e :person/name ?n]]}'), [['Ann']], 'the map form');
is_deeply([ sort map { $_->[0] } @{ $db->q('[:find ?a :where [_ ?a "Ann"]]') } ], ['person/name'], 'an attribute found is its name');

done_testing;
