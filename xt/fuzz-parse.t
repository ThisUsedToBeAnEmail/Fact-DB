#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use Fact::DB;

# 100,000 texts, mutated from good queries and made up from nothing: each
# parses or croaks with the parser's own message, and nothing is left
# allocated. Run under ASan too (see the Built section of the plan's
# Phase 08) to say nothing is read past a buffer.

my $N = $ENV{FACTDB_FUZZ} || 100_000;
my $bs = chr 92;
my @seed = (
    '[:find ?n :in $ ?min :where [?e :person/age ?a] [(> ?a ?min)] [?e :person/name ?n]]',
    '[:find ?who :in $ % ?b :where (anc ?a ?b) [?a :name ?who]]',
    '[:find [?n ...] :where [?e :a ?n] (not [?e :b 1]) (or [?e :c 2] (and [?e :d ?x] [(< ?x 3)]))]',
    '{:find [(count ?e) .] :in [$ [?x ...]] :where [[?e :a ?x] (or-join [?e] [?e :p _] [?e :q _])]}',
    qq{[:find ?n :where [?e :name "caf\x{e9} ${bs}u263A ${bs}uD83D${bs}uDE00 ${bs}n"] [(str ?e "!") ?n] [(f ?e) [[?p _]]]]},
    '[:find [?a ?b] :with ?c :where [?a :x ?b ?tx true] [$ ?c :y 1.5e3] [[:k "v"] :z ?a]]',
);
my $rules = '[[(anc ?a ?b) [?b :parent ?a]] [(anc ?a ?b) [?b :parent ?c] (anc ?a ?c)]]';
my @alphabet = (split(//, q{[](){}":?_$%.;,\ 0123456789abcdefxyz:/-+eE!<>=#&'|}), "\n", "\t", "\x{e9}", "\x{263a}", $bs . 'u', '...');

srand 15102026;
my $base = Fact::DB::_live_blocks();
my ($parsed, $refused, $wrong) = (0, 0, 0);
my @bad;
for my $i (1 .. $N) {
    my $text;
    if ($i % 10 == 0) {
        $text = join '', map { $alphabet[rand @alphabet] } 1 .. int rand 60;
    }
    elsif ($i % 97 == 0) {
        my $d = int rand 300;
        $text = ('[' x $d) . (']' x (rand() < 0.5 ? $d : int rand $d));
    }
    else {
        $text = $seed[rand @seed];
        for (1 .. 1 + int rand 4) {
            my $at = int rand(length($text) + 1);
            my $k = int rand 3;
            if ($k == 0 && length $text) { substr($text, $at, 1 + int rand 3) = '' if $at < length $text }
            elsif ($k == 1) { substr($text, $at, 0) = $alphabet[rand @alphabet] }
            else { substr($text, $at, 1) = $alphabet[rand @alphabet] if $at < length $text }
        }
    }
    my $ok = eval { Fact::DB::_parse($text, rand() < 0.5 ? $rules : undef); 1 };
    if ($ok) { $parsed++ }
    elsif ($@ =~ /^Fact::DB::_parse(?: rules)?: /) { $refused++ }
    else { $wrong++; push @bad, [$text, $@] if @bad < 5 }
}
diag("$parsed parsed, $refused refused, $wrong otherwise");
is($wrong, 0, "every one of $N texts parses or is refused with the parser's message") or diag explain \@bad;
cmp_ok($parsed, '>', $N / 100, 'and a fair number of the mutants still parse');
is(Fact::DB::_live_blocks(), $base, 'and nothing is left allocated');

done_testing;
