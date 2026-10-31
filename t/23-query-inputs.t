#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(people dies_like);

# Inputs: each :in form, two databases, and the counts that must agree.

my $c = people();
my $r = $c->transact([ map { { 'person/name' => "n$_", 'person/age' => $_ } } 1 .. 5 ]);
my $old = $c->db;
$c->transact([[ 'db/add', ['person/name', 'n1'], 'person/age', 50 ]]);
my $db = $c->db;
sub srt { [ sort { "@$a" cmp "@$b" } @{ $_[0] } ] }

is_deeply(srt($db->q('[:find ?n :in $ ?min :where [?e :person/age ?a] [(>= ?a ?min)] [?e :person/name ?n]]', 4)),
          [['n1'], ['n4'], ['n5']], 'a scalar input');
is_deeply(srt($db->q('[:find ?n :in $ [?a ...] :where [?e :person/age ?a] [?e :person/name ?n]]', [2, 3, 99])),
          [['n2'], ['n3']], 'a collection input');
is_deeply($db->q('[:find ?n :in $ [?a ?b] :where [?e :person/age ?a] [?e :person/name ?b] [?e :person/name ?n]]', [2, 'n2']),
          [['n2']], 'a tuple input');
is_deeply(srt($db->q('[:find ?n ?w :in $ [[?a ?w]] :where [?e :person/age ?a] [?e :person/name ?n]]', [[2, 'two'], [3, 'three'], [9, 'none']])),
          [['n2', 'two'], ['n3', 'three']], 'a relation input');
is_deeply($db->q('[:find ?n :in $ [[?a _]] :where [?e :person/age ?a] [?e :person/name ?n]]', [[2, 'x']]), [['n2']], '_ in a relation');
is_deeply($db->q('[:find ?n :in $ [?a ...] :where [?e :person/age ?a] [?e :person/name ?n]]', []), [], 'an empty collection: no rows');
is_deeply([ sort map { $_->[0] } @{ $db->q('[:find ?x :in $ [?x ...] :where [(ground 1) ?one]]', [1, undef, 2]) } ], [1, 2],
          'an undef in a collection is skipped');

is_deeply($db->q('[:find ?a :in $now $then :where [$then ?e :person/name "n1"] [$then ?e :person/age ?a]]', $old), [[1]],
          'a second database, named in its patterns');
is_deeply($db->q('[:find ?was ?is :in $ $then :where [$then ?e :person/age ?was] [?e :person/age ?is] [(!= ?was ?is)]]', $old),
          [[1, 50]], 'joining a value with an older one: what changed');
is_deeply($old->q('[:find ?a :where [?e :person/name "n1"] [?e :person/age ?a]]'), [[1]], 'q on an as_of value');
is_deeply($db->as_of($r->{tx})->q('[:find ?a :where [?e :person/name "n1"] [?e :person/age ?a]]'), [[1]], 'q on as_of');

dies_like(sub { $db->q('[:find ?n :in $ ?x :where [?e :person/name ?n]]') },
          qr/^Fact::DB::Db::q: the query's :in takes 1 inputs after this value, and was given 0/, 'an input missing');
dies_like(sub { $db->q('[:find ?n :where [?e :person/name ?n]]', 1) },
          qr/takes 0 inputs after this value, and was given 1/, 'an input too many');
dies_like(sub { $db->q('[:find ?n :in $ ?x :where [?e :person/name ?n]]', undef) },
          qr/line 1, column 17: the input for \?x is undef/, 'an undef input');
dies_like(sub { $db->q('[:find ?n :in $ [?x ...] :where [?e :person/name ?n]]', 3) },
          qr/this input takes an array reference/, 'a collection that is not a list');
dies_like(sub { $db->q('[:find ?n :in $ [?a ?b] :where [?e :person/name ?n]]', [1]) },
          qr/this input binds 2, and was given a tuple of 1/, 'a short tuple');
dies_like(sub { $db->q('[:find ?n :in $ [[?a ?b]] :where [?e :person/name ?n]]', [1]) },
          qr/a relation input is an array reference of array references/, 'a relation of scalars');
dies_like(sub { $db->q('[:find ?n :in $ $b :where [$b ?e :person/name ?n]]', 'x') },
          qr/^Fact::DB::Db::q: not a Fact::DB::Db/, 'a source that is not a value');
dies_like(sub { $db->q('[:find ?n :where [?e :person/name ?n]]', { bogus => 1 }) },
          qr/unknown option "bogus"; the option is functions/, 'an unknown option');

done_testing;
