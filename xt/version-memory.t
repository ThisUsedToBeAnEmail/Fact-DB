#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use Fact::DB;

# Each transaction keeps only what it changed: 10,000 transactions of 10
# changes, every version kept, as the Phase 02 spike measured it: 83,334
# entities, about 1M datoms, each change to one of seven attributes of a
# random entity, two of them indexed. The mark is the spike's figure for
# its layout, 14.2 KB, plus 20%.

plan skip_all => 'malloc\'s count is read only on macOS' if Fact::DB::Db::_malloc_in_use() < 0;

my $c = Fact::DB->new(schema => {
    'p/name'   => { type => 'string', unique => 'identity' },
    'p/email'  => { type => 'string', unique => 'identity' },
    'p/age'    => { type => 'int', index => 1 },
    'p/city'   => { type => 'string' },
    'p/parent' => { type => 'ref' },
    'p/tags'   => { type => 'string', cardinality => 'many' },
    'p/score'  => { type => 'num' },
    'p/active' => { type => 'bool' },
    'p/joined' => { type => 'int' },
    'p/bio'    => { type => 'string' },
});
my $n = 83_334;
srand 2;
$c->transact([ map { {
    'p/name' => "p$_", 'p/email' => "p$_\@example.org", 'p/age' => int rand 90, 'p/city' => 'c' . int(rand 50),
    'p/tags' => [ map { 't' . (($_ * 17 + $_) % 50) } 1 .. 3 ], 'p/score' => int(rand 100000) / 1000,
    'p/active' => int rand 2, 'p/joined' => 1600000000 + int rand 1e8, 'p/bio' => "biography of person $_, version 0",
    ($_ > 1 ? ('p/parent' => 1 + int rand($_ - 1)) : ()),
} } 1 .. $n ]);
cmp_ok($c->db->count, '>=', 900_000, 'a base of about 1M datoms (' . $c->db->count . ')');

my %val = (
    'p/age'    => sub { int rand 90 },
    'p/city'   => sub { 'c' . int rand 50 },
    'p/score'  => sub { int(rand 100000) / 1000 },
    'p/active' => sub { int rand 2 },
    'p/joined' => sub { 1600000000 + int rand 1e8 },
    'p/bio'    => sub { 'biography, version ' . int rand 1000 },
    'p/parent' => sub { 2 + int rand($n - 2) },
);
my @attrs = sort keys %val;
my @tx = map { my %s; [ map { my $e; do { $e = 3 + int rand($n - 2) } while $s{$e}++; my $a = $attrs[rand @attrs]; [ 'db/add', $e, $a, $val{$a}->() ] } 1 .. 10 ] } 1 .. 10_000;
my $before = Fact::DB::Db::_malloc_in_use();
$c->transact($_) for @tx;
my $per = (Fact::DB::Db::_malloc_in_use() - $before) / 10_000 / 1024;
diag(sprintf 'memory a version: %.1f KB', $per);
cmp_ok($per, '<=', 14.2 * 1.2, 'each version keeps only what it changed, within the mark');

done_testing;
