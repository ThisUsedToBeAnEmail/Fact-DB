#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(people);

# :with: the set an aggregate is worked out over is a set of the variables
# the result names. Two people of 30: their ages are one 30, unless each
# person is named too.

my $c = people();
$c->transact([ { 'person/name' => 'Ann', 'person/age' => 30, 'person/tags' => ['a'] },
               { 'person/name' => 'Bob', 'person/age' => 30, 'person/tags' => ['a'] },
               { 'person/name' => 'Cy',  'person/age' => 40, 'person/tags' => ['b'] } ]);
my $db = $c->db;

is($db->q('[:find (sum ?a) . :where [?e :person/age ?a]]'), 70, 'without :with, two people of 30 are one 30: 30 + 40');
is($db->q('[:find (sum ?a) . :with ?e :where [?e :person/age ?a]]'), 100, 'with :with ?e, each person counts: 30 + 30 + 40');
is($db->q('[:find (count ?a) . :where [?e :person/age ?a]]'), 2, 'count without :with counts the ages');
is($db->q('[:find (count ?a) . :with ?e :where [?e :person/age ?a]]'), 3, 'and with it, the people');
is($db->q('[:find (count ?e) . :where [?e :person/age ?a]]'), 3, 'count of the entities needs no :with: ?e is named');
is_deeply([ sort { "@$a" cmp "@$b" } @{ $db->q('[:find ?t (sum ?a) :where [?e :person/tags ?t] [?e :person/age ?a]]') } ],
          [['a', 30], ['b', 40]], 'grouped, without :with');
is_deeply([ sort { "@$a" cmp "@$b" } @{ $db->q('[:find ?t (sum ?a) :with ?e :where [?e :person/tags ?t] [?e :person/age ?a]]') } ],
          [['a', 60], ['b', 40]], 'grouped, with');
is($db->q('[:find (avg ?a) . :where [?e :person/age ?a]]'), 35, 'avg without :with: of 30 and 40');
is(sprintf('%.4f', $db->q('[:find (avg ?a) . :with ?e :where [?e :person/age ?a]]')), '33.3333', 'avg with: of 30, 30 and 40');

done_testing;
