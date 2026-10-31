#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(people);

# The query cache: a query's text parsed once, 256 kept, the oldest out.

my $c = people();
$c->transact([{ 'person/name' => 'Ann', 'person/age' => 30 }]);
my $db = $c->db;
my ($held0, $hits0, $misses0) = Fact::DB::_cache_stats();
my $q = '[:find ?n :where [?e :person/name ?n]]';
$db->q($q) for 1 .. 5;
my ($held, $hits, $misses) = Fact::DB::_cache_stats();
is($misses - $misses0, 1, 'a query is parsed the first time');
is($hits - $hits0, 4, 'and found the other times');
is($held, $held0 + 1, 'and kept');

my $old = $db;
$c->transact([{ 'person/name' => 'Bob' }]);
is_deeply([ sort map { $_->[0] } @{ $c->db->q($q) } ], ['Ann', 'Bob'], 'one cached query against a new value');
is_deeply($old->q($q), [['Ann']], 'and against the old one');

$db->q("[:find ?n :where [?e :person/name ?n] [(ground $_) ?x]]") for 1 .. 300;
($held) = Fact::DB::_cache_stats();
is($held, 256, 'at most 256 are kept');
my (undef, $h1, $m1) = Fact::DB::_cache_stats();
$db->q('[:find ?n :where [?e :person/name ?n] [(ground 300) ?x]]');
my (undef, $h2) = Fact::DB::_cache_stats();
is($h2, $h1 + 1, 'the newest are kept');
$db->q('[:find ?n :where [?e :person/name ?n] [(ground 1) ?x]]');
my (undef, undef, $m2) = Fact::DB::_cache_stats();
is($m2, $m1 + 1, 'and the oldest went first');

{
    my $evict = sub { $db->q("[:find ?n :where [?e :person/name ?n] [(ground \"x$_\") ?x]]") for 1 .. 300; 1 };
    my $text = '[:find ?n :where [?e :person/name ?n] [(my/evict) ?ok]]';
    is_deeply($db->q($text, { functions => { 'my/evict' => sub { 1 } } }), [['Ann']], 'a query, put in the cache');
    is_deeply($db->q($text, { functions => { 'my/evict' => $evict } }), [['Ann']],
              'found there, and pushed out of it by its own function, it still finishes');
}

done_testing;
