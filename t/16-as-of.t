#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(facts people dies_like consistent);

# as_of: every version of a random run, read back from the latest, from an
# old one, and from a with value, against a copy kept at the time.

my $c = people('person/friends' => { type => 'ref', cardinality => 'many' });
my $db0 = $c->db;
my (@ents, %at, @txs);
$at{0} = facts($db0);
srand 1015;
for my $i (1 .. 300) {
    my @data;
    if (!@ents || rand() < 0.3) {
        push @data, { 'person/name' => "n$i", 'person/age' => int rand 9, (@ents ? ('person/boss' => $ents[rand @ents]) : ()) };
    }
    else {
        my $e = $ents[rand @ents];
        my $tag = ('a' .. 'e')[rand 5];
        push @data, rand() < 0.5 ? [ 'db/add', $e, 'person/age', int rand 9 ]
                                 : [ 'db/add', $e, 'person/tags', $tag ];
        push @data, [ 'db/retract', $e, 'person/tags', ('f', grep { $_ ne $tag } 'a' .. 'e')[rand 5] ] if rand() < 0.3;
    }
    my $r = $c->transact(\@data);
    push @ents, map { $_->[0] } grep { $_->[1] eq 'person/name' } @{ $r->{tx_data} };
    push @txs, $r->{tx};
    $at{ $r->{tx} } = facts($c->db);
}
my $db = $c->db;
is($c->versions, 301, 'the connection\'s line holds the empty value and 300 more');
is($db->basis_tx, $txs[-1], 'basis_tx is the last transaction');

my ($ok, $same) = (0, 0);
for my $tx (0, @txs) {
    my $v = $db->as_of($tx);
    $ok++ if $v->basis_tx == $tx && is_deeply(facts($v), $at{$tx}, "as_of($tx)") ;
    $same++ if $db->as_of($tx) == $v;
}
is($ok, 301, 'every version reads as it did when it was made');
is($same, 301, 'and as_of hands out the same object each time');

{
    my ($between, $right) = (0, 0);
    for my $k (1 .. $#txs) {
        next unless $txs[$k] - $txs[$k - 1] > 1;
        $between++;
        $right++ if $db->as_of($txs[$k] - 1)->basis_tx == $txs[$k - 1];
    }
    cmp_ok($between, '>', 20, "a transaction id between two transactions ($between of them)");
    is($right, $between, 'gives the version of the one before');
    is($db->as_of($txs[-1] + 1000), $db, 'one after the last gives the value itself');
    is($db->as_of(0), $db0, 'zero gives the empty value');
}

{
    my $mid = $db->as_of($txs[150]);
    is($mid->as_of($txs[100]), $db->as_of($txs[100]), 'as_of on an older value looks back along its own line');
    is($mid->as_of($txs[200]), $mid, 'and never forward');
    my $w = $mid->with([[ 'db/add', $ents[0], 'person/age', 99 ]]);
    my $wv = $w->{db_after};
    is($wv->as_of($txs[100]), $db->as_of($txs[100]), 'a with value\'s line is the value it was made from, and back');
    is($wv->as_of($wv->basis_tx), $wv, 'and itself at its own transaction');
    is($wv->as_of($w->{tx} - 1), $mid, 'and the value it came from just before');
}

{
    my @l = $db->_line;
    is($l[0], 300, 'a value knows how far back its line goes');
    cmp_ok($l[1], '<', 300, 'and jumps further back than its parent');
}

consistent($db->as_of($txs[77]), 'an old value\'s indexes agree');

dies_like(sub { $db->as_of(-1) }, qr/^Fact::DB::Db::as_of: a transaction is a whole number, 0 or more, not "-1"/, 'a negative id');
dies_like(sub { $db->as_of('x') }, qr/not "x"/, 'a word');
dies_like(sub { $db->as_of(1.5) }, qr/not "1\.5"/, 'a fraction');
dies_like(sub { $db->since(3)->as_of(3) }, qr/a since value has no line of versions/, 'a since value');
dies_like(sub { $db->history->as_of(3) }, qr/a history value has no line of versions/, 'a history value');

{
    my $old = $db->as_of($txs[10]);
    my $want = $at{ $txs[10] };
    undef $c; undef $db; undef $db0;
    is_deeply(facts($old), $want, 'an old value reads after the connection and the latest value are gone');
    is_deeply(facts($old->as_of($txs[5])), $at{ $txs[5] }, 'and so does its own past');
    undef $old;
}
is(Fact::DB::_live_blocks(), 0, 'and with them gone, every version is freed');

{
    my $c2 = people();
    $c2->transact([[ 'db/add', 'x', 'person/age', $_ % 9 ]]) for 1 .. 20_000;
    my $last = $c2->db;
    my $max = 0;
    for my $t (map { int rand $last->basis_tx } 1 .. 2000) {
        my $s = $last->_as_of_steps($t);
        $max = $s if $s > $max;
    }
    cmp_ok($max, '<=', 3 * 15, "as_of on a line of 20,000 takes at most $max steps, about three times log2 of it");
    cmp_ok($last->_as_of_steps(1), '<=', 45, 'even to the oldest');
    undef $c2;
    undef $last;
    is(Fact::DB::_live_blocks(), 0, 'and the line is freed');
}

{
    my $c3 = people();
    $c3->transact([[ 'db/add', 'x', 'person/age', $_ % 9 ]]) for 1 .. 150_000;
    undef $c3;
    is(Fact::DB::_live_blocks(), 0, 'a line of 150,000 versions is freed without running out of stack');
}

done_testing;
