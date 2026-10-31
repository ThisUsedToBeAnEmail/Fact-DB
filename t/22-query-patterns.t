#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(people);
use Oracle qw(evaluate render);
use Time::HiRes qw(time);

# Patterns and comparisons, 1,000 random queries over random facts, each
# against a plain Perl evaluator of the same query over the same datoms.

my $c = people('person/friends' => { type => 'ref', cardinality => 'many' });
srand 909;
my @ents;
for my $i (1 .. 40) {
    my %h = ('person/name' => "n$i", 'person/age' => int rand 6, 'person/score' => int(rand 8) / 2,
             'person/tags' => [ grep { rand() > 0.5 } qw(a b c) ]);
    delete $h{'person/tags'} unless @{ $h{'person/tags'} };
    $h{'person/boss'} = $ents[rand @ents] if @ents && rand() > 0.3;
    $h{'person/friends'} = [ @ents[ map { int rand @ents } 1 .. 2 ] ] if @ents > 2 && rand() > 0.5;
    my $r = $c->transact([\%h]);
    push @ents, $r->{tx} + 1;
}
my $db = $c->db;
my $datoms = $db->datoms('eav');
my @attrs = qw(person/name person/age person/score person/tags person/boss person/friends);
my %consts = (
    'person/name' => [ map { ['str', "n$_"] } 1 .. 40 ],
    'person/age' => [ map { ['int', $_] } 0 .. 5 ],
    'person/score' => [ map { ['int', $_] } 0 .. 3 ],
    'person/tags' => [ map { ['str', $_] } qw(a b c) ],
    'person/boss' => [ map { ['int', $_] } @ents[0 .. 9] ],
    'person/friends' => [ map { ['int', $_] } @ents[0 .. 9] ],
);
my @vars = map { "?v$_" } 1 .. 5;

sub random_query {
    my @where;
    my %bound;
    for my $n (1 .. 1 + int rand 3) {
        my @t;
        my $a = $attrs[rand @attrs];
        for my $pos (0 .. 2) {
            my $r = rand;
            if ($pos == 1) {
                push @t, $r < 0.85 ? ['kw', $a] : $r < 0.95 ? ['var', $vars[rand @vars]] : ['_'];
                next;
            }
            if ($pos == 0) { push @t, $r < 0.8 ? ['var', $vars[rand @vars]] : $r < 0.9 ? ['int', $ents[rand @ents]] : ['_']; next }
            push @t, $r < 0.6 ? ['var', $vars[rand @vars]] : $r < 0.85 ? $consts{$a}[rand @{ $consts{$a} }] : ['_'];
        }
        $bound{ $_->[1] } = 1 for grep { $_->[0] eq 'var' } @t;
        push @where, ['pattern', \@t];
        if (%bound && rand() < 0.3) {
            my @b = sort keys %bound;
            my $op = (qw(< > <= >= = !=))[rand 6];
            push @where, ['pred', $op, [ ['var', $b[rand @b]], rand() < 0.5 ? ['int', int rand 6] : ['var', $b[rand @b]] ]];
        }
    }
    my @b = sort keys %bound;
    return unless @b;
    my @find = grep { rand() < 0.6 } @b;
    @find = ($b[0]) unless @find;
    return { find => \@find, where => \@where };
}

my ($agreed, $tried, $skipped, $t_engine, $t_oracle) = (0, 0, 0, 0, 0);
my @wrong;
while ($tried < 1000) {
    my $q = random_query() or next;
    my $t0 = time;
    my $want = evaluate($q, $datoms, 20_000);
    $t_oracle += time - $t0;
    if (!$want) { $skipped++; next }
    $tried++;
    my $text = render($q);
    $t0 = time;
    my $got = eval { $db->q($text) };
    $t_engine += time - $t0;
    if (!$got) { push @wrong, [$text, "died: $@"] if @wrong < 5; next }
    $got = [ sort { join("\0", @$a) cmp join("\0", @$b) } @$got ];
    if (Test::More::_deep_check($got, $want)) { $agreed++ }
    elsif (@wrong < 5) { push @wrong, [$text, $got, $want] }
}
is($agreed, $tried, "the engine and the plain evaluator agree on $tried random queries ($skipped too large to try)")
    or diag explain \@wrong;
note(sprintf 'engine %.2f s, plain evaluator %.2f s', $t_engine, $t_oracle);

is_deeply($db->q('[:find ?e :where [?e :person/name "n1"]]'), [[ $ents[0] ]], 'an entity by a value');
is_deeply($db->q(qq{[:find ?n :where [$ents[0] :person/name ?n]]}), [['n1']], 'a value of an entity');
is_deeply($db->q('[:find ?v :where [[:person/name "n1"] :person/name ?v]]'), [['n1']], 'a lookup ref as the entity');
is_deeply($db->q('[:find ?v :where [[:person/name "nobody"] :person/name ?v]]'), [], 'one that names nobody matches nothing');
is_deeply($db->q('[:find ?e :where [?e :person/name ?n] [?e :person/age 99]]'), [], 'no match is an empty result');

{
    my $r = $db->q('[:find ?tx :where [?e :person/name "n1" ?tx]]');
    is($r->[0][0], $ents[0] - 1, 'a pattern binds the transaction that asserted a fact');
    my $h = $db->history;
    $c->transact([[ 'db/add', $ents[0], 'person/age', 99 ]]);
    my $all = $c->db->history->q(qq{[:find ?v ?added :where [$ents[0] :person/age ?v _ ?added]]});
    is(scalar(grep { $_->[1] == 0 } @$all), 1, 'on a history value, the fifth place binds whether it was asserted or retracted');
}

done_testing;
