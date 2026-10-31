#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/../t/lib";
use Fact::DB;
use Facts qw(people);

# 100,000 queries: nothing left allocated, and resident memory flat.

sub rss { my $k = `ps -o rss= -p $$`; $k =~ s/\s//g; $k }
my $c = people();
$c->transact([ map { { 'person/name' => "n$_", 'person/age' => $_ % 7 } } 1 .. 200 ]);
my $db = $c->db;
my @q = (['[:find ?n :in $ ?a :where [?e :person/age ?a] [?e :person/name ?n]]', 3],
         ['[:find ?n . :where [?e :person/name ?n] [(str ?n "x") ?y] [(= ?y "n7x")]]'],
         ['[:find ?e :where [?e :person/age 99]]']);
$db->q(@{ $q[$_ % 3] }) for 1 .. 10_000;
my $blocks = Fact::DB::_live_blocks();
my $before = rss();
$db->q(@{ $q[$_ % 3] }) for 1 .. 100_000;
my $after = rss();
is(Fact::DB::_live_blocks(), $blocks, 'nothing left allocated beyond the three queries the cache keeps');
cmp_ok($after - $before, '<', 2048, "resident memory flat (grew ${\ ($after - $before)} KB)");

done_testing;
