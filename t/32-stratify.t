#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(people dies_like);

# A rule may not depend on its own negation.

my $c = people();
$c->transact([ map { { 'person/name' => "p$_", 'person/age' => $_ } } 1 .. 4 ]);
my $db = $c->db;
my $q = '[:find ?x :in $ % :where (r ?x)]';

dies_like(sub { $db->q($q, q{[[(r ?x) [?x :person/age _] (not (r ?x))]]}) },
          qr/^Fact::DB::Db::q rules: line 1, column 3: rule r negates r, which depends on r: a rule cannot depend on its own negation/,
          'a rule that negates itself');
dies_like(sub { $db->q($q, q{[[(r ?x) [?x :person/age _] (not (s ?x))] [(s ?x) (r ?x)]]}) },
          qr/rule r negates s, which depends on r/, 'through another rule');
dies_like(sub { $db->q($q, q{[[(r ?x) [?x :person/age _] (or [?x :person/age 1] (not (s ?x)))] [(s ?x) [?x :person/age _] (t ?x)] [(t ?x) (r ?x)]]}) },
          qr/rule r negates s, which depends on r/, 'through two, inside an or');
dies_like(sub { Fact::DB::_parse($q, q{[[(r ?x) [?x :a _] (not (r ?x))]]}) }, qr/rule r negates r/, 'and so says the parser');

is_deeply([ sort map { $_->[0] } @{ $db->q('[:find ?n :in $ % :where (r ?x) [?x :person/name ?n]]',
                                          q{[[(old ?x) [?x :person/age ?a] [(> ?a 2)]] [(r ?x) [?x :person/age _] (not (old ?x))]]}) } ],
          ['p1', 'p2'], 'a negation of a rule that does not depend on the negating one works');
is_deeply([ sort map { $_->[0] } @{ $db->q('[:find ?n :in $ % :where (r ?x) [?x :person/name ?n]]',
                                          q{[[(r ?x) [?x :person/age _] (not [?x :person/age 1])] [(r ?x) (r ?x)]]}) } ],
          [qw(p2 p3 p4)], 'and a rule that recurses with a not of no rule');

done_testing;
