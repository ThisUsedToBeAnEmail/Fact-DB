#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(people dies_like);
use Oracle qw(evaluate);

# Every aggregate, grouped and not, with rows and without; then random
# grouped aggregates against the plain evaluator.

my $c = people('person/city' => { type => 'string' }, 'person/born' => { type => 'instant' });
$c->transact([ map { { 'person/name' => "p$_->[0]", 'person/age' => $_->[1], 'person/city' => $_->[2], 'person/score' => $_->[3] } }
               [1, 30, 'x', 1.5], [2, 30, 'x', 2], [3, 50, 'y', 3.5], [4, 20, 'y', 4], [5, 40, 'y', 0.5] ]);
my $db = $c->db;
sub q1 { $db->q(@_) }
sub rows { [ sort { "@$a" cmp "@$b" } @{ q1(@_) } ] }

is(q1('[:find (count ?e) . :where [?e :person/name]]'), 5, 'count');
is(q1('[:find (count-distinct ?a) . :with ?e :where [?e :person/age ?a]]'), 4, 'count-distinct');
is(q1('[:find (sum ?a) . :with ?e :where [?e :person/age ?a]]'), 170, 'sum of whole numbers is whole');
is(q1('[:find (sum ?s) . :with ?e :where [?e :person/score ?s]]'), 11.5, 'sum of nums');
is(q1('[:find (avg ?a) . :with ?e :where [?e :person/age ?a]]'), 34, 'avg');
is(q1('[:find (min ?a) . :where [_ :person/age ?a]]'), 20, 'min');
is(q1('[:find (max ?a) . :where [_ :person/age ?a]]'), 50, 'max');
is(q1('[:find (min ?n) . :where [_ :person/name ?n]]'), 'p1', 'min of strings, as strings');
is(q1('[:find (max ?n) . :where [_ :person/name ?n]]'), 'p5', 'max of strings');
is(q1('[:find (median ?a) . :with ?e :where [?e :person/age ?a]]'), 30, 'median of an odd count');
is(q1('[:find (median ?a) . :where [_ :person/age ?a]]'), 35, 'median of an even count: the mean of the two middle');
is_deeply([ sort { $a <=> $b } @{ q1('[:find (distinct ?a) . :where [_ :person/age ?a]]') } ], [20, 30, 40, 50], 'distinct: a list');
is_deeply(q1('[:find (min 2 ?a) . :with ?e :where [?e :person/age ?a]]'), [20, 30], '(min n ?x): the n least, least first');
is_deeply(q1('[:find (max 3 ?a) . :with ?e :where [?e :person/age ?a]]'), [50, 40, 30], '(max n ?x): the n greatest, greatest first');
is_deeply(q1('[:find (max 9 ?a) . :where [_ :person/age ?a]]'), [50, 40, 30, 20], 'more asked than there are: all of them');

is_deeply(rows('[:find ?c (count ?e) (sum ?a) :where [?e :person/city ?c] [?e :person/age ?a]]'), [['x', 2, 60], ['y', 3, 110]],
          'grouped by the variables not aggregated; count names ?e, so each person is in the set');
is_deeply(rows('[:find ?c (min ?a) (max ?a) :where [?e :person/city ?c] [?e :person/age ?a]]'), [['x', 30, 30], ['y', 20, 50]],
          'several aggregates a group');
is_deeply(q1('[:find [?c (count ?e)] :where [?e :person/city ?c] [?e :person/age 50]]'), ['y', 1], 'as one tuple');
is_deeply(q1('[:find [(count ?e) ...] :where [?e :person/city ?c]]'), [5], 'as a list: with nothing to group by, one group');

is(q1('[:find (count ?e) . :where [?e :person/age 99]]'), 0, 'count of nothing is 0');
is(q1('[:find (sum ?a) . :where [?e :person/age 99] [?e :person/age ?a]]'), 0, 'sum of nothing is 0');
is(q1('[:find (max ?a) . :where [?e :person/age 99] [?e :person/age ?a]]'), undef, 'max of nothing is undef');
is(q1('[:find (avg ?a) . :where [?e :person/age 99] [?e :person/age ?a]]'), undef, 'avg of nothing is undef');
is_deeply(q1('[:find (count ?e) :where [?e :person/age 99]]'), [[0]], 'count of nothing, as a relation: one row of 0');
is_deeply(q1('[:find ?c (count ?e) :where [?e :person/city ?c] [?e :person/age 99]]'), [], 'grouped, nothing: no groups');

dies_like(sub { q1('[:find (sum ?n) . :where [_ :person/name ?n]]') }, qr/line 1, column 8: sum takes numbers, not "p\d"/, 'sum of strings');
dies_like(sub { q1('[:find (avg ?n) . :where [_ :person/name ?n]]') }, qr/avg takes numbers/, 'avg of strings');
dies_like(sub { q1('[:find (median ?n) . :where [_ :person/name ?n]]') }, qr/median takes numbers/, 'median of strings');
dies_like(sub { q1('[:find (max ?v) . :where [?e :person/name "p1"] [?e _ ?v]]') }, qr/max compares values of one kind: numbers, or strings/,
          'max of a number and a string');
dies_like(sub { q1('[:find (min 2 ?v) . :where [?e :person/name "p1"] [?e _ ?v]]') }, qr/min compares values of one kind/,
          '(min n) likewise');

# ---- random grouped aggregates against the plain evaluator

srand 1111;
my $c2 = people();
$c2->transact([ map { { 'person/name' => "n$_", 'person/age' => int rand 6, 'person/score' => int(rand 10) / 2, 'person/tags' => [ (qw(a b c))[ rand 3 ] ] } } 1 .. 40 ]);
my $d2 = $c2->db;
my $datoms = $d2->datoms('eav');
my ($agreed, $tried) = (0, 0);
my @wrong;
for (1 .. 200) {
    my $agg = (qw(count sum min max count-distinct))[rand 5];
    my $val = (qw(person/age person/score))[rand 2];
    my $grp = (qw(person/tags person/age none))[rand 3];
    my $with = rand() < 0.5;
    my @where = (['pattern', [ ['var', '?e'], ['kw', $val], ['var', '?v'] ]]);
    push @where, ['pattern', [ ['var', '?e'], ['kw', $grp], ['var', '?g'] ]] if $grp ne 'none';
    my @named = ($grp ne 'none' ? '?g' : (), '?v', $with ? '?e' : ());
    my $set = evaluate({ find => \@named, where => \@where }, $datoms, 100_000) or next;
    my %groups;
    for my $row (@$set) { my %r; @r{@named} = @$row; push @{ $groups{ $grp ne 'none' ? $r{'?g'} : '' } }, $r{'?v'} }
    my @want;
    for my $g (keys %groups) {
        my @v = @{ $groups{$g} };
        my %u = map { $_ => 1 } @v;
        my $res = $agg eq 'count' ? scalar @v : $agg eq 'count-distinct' ? scalar keys %u
                : $agg eq 'sum' ? do { my $s = 0; $s += $_ for @v; $s }
                : $agg eq 'min' ? (sort { $a <=> $b } @v)[0] : (sort { $b <=> $a } @v)[0];
        push @want, [ $grp ne 'none' ? ($g, $res) : ($res) ];
    }
    my $text = '[:find ' . ($grp ne 'none' ? '?g ' : '') . "($agg ?v)" . ($with ? ' :with ?e' : '') . ' :where [?e :' . $val . ' ?v]'
             . ($grp ne 'none' ? " [?e :$grp ?g]" : '') . ']';
    my $got = $d2->q($text);
    my $norm = sub { [ sort { "@$a" cmp "@$b" } map { [ map { 0 + $_ eq $_ ? 0 + $_ : $_ } @$_ ] } @{ $_[0] } ] };
    $tried++;
    if (Test::More::_deep_check($norm->($got), $norm->(\@want))) { $agreed++ } elsif (@wrong < 3) { push @wrong, [$text, $got, \@want] }
}
is($agreed, $tried, "the engine and the plain evaluator agree on $tried random grouped aggregates") or diag explain \@wrong;

done_testing;
