#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(people dies_like);

# history: every assertion and retraction on a value's line, in log order.

my $c = people('person/friends' => { type => 'ref', cardinality => 'many' });
my (@all, @tx);
my @data = (
    [{ 'db/id' => 'a', 'person/name' => 'Ann', 'person/age' => 61 }, { 'db/id' => 'b', 'person/name' => 'Bob', 'person/boss' => 'a' }],
    [[ 'db/add', 2, 'person/age', 62 ]],
    [[ 'db/add', 3, 'person/friends', 2 ], [ 'db/add', 3, 'person/tags', 't' ], [ 'db/add', 2, 'person/friends', 3 ]],
    [[ 'db/retractEntity', 2 ]],
    [[ 'db/add', 3, 'person/age', 30 ], [ 'db/add', 3, 'person/score', 2 ]],
);
for my $d (@data) {
    my $r = $c->transact($d);
    push @all, map { [ @$_ ] } @{ $r->{tx_data} };
    push @tx, $r->{tx};
}
my $h = $c->db->history;
ok($h->is_history, 'a history value says so');
ok(!$c->db->is_history, 'and a plain one does not');
is($h->history, $h, 'history of a history value is itself');
is_deeply($h->datoms('eav'), \@all, 'every datom of every transaction\'s tx_data, in log order, with added');
is($h->count, scalar @all, 'counted');
is_deeply($h->datoms(eav => 2), [ grep { $_->[0] == 2 } @all ], 'by entity');
is_deeply($h->datoms(eav => 2, 'person/age'), [ grep { $_->[0] == 2 && $_->[1] eq 'person/age' } @all ], 'by entity and attribute');
is_deeply($h->datoms(eav => 2, 'person/age', 61), [ grep { $_->[0] == 2 && $_->[1] eq 'person/age' && $_->[2] == 61 } @all ],
          'and value: its assertion and its retraction');
is_deeply($h->datoms(aev => 'person/age'), [ grep { $_->[1] eq 'person/age' } @all ], 'by attribute');
is_deeply($h->datoms(aev => 'person/age', 3), [ grep { $_->[1] eq 'person/age' && $_->[0] == 3 } @all ], 'and entity');
is_deeply($h->datoms(ave => 'person/name', 'Ann'), [ grep { $_->[1] eq 'person/name' && $_->[2] eq 'Ann' } @all ], 'by value');
is_deeply($h->datoms(vae => 2), [ grep { ($_->[1] eq 'person/boss' || $_->[1] eq 'person/friends') && $_->[2] == 2 } @all ],
          'every ref to an entity, made and retracted');
is_deeply($h->datoms(vae => 2, 'person/friends'), [ grep { $_->[1] eq 'person/friends' && $_->[2] == 2 } @all ], 'through one ref');
is(scalar(grep { !$_->[4] && $_->[3] == $tx[3] } @{ $h->datoms('eav') }), 5,
   'retractEntity\'s retractions are there: three of its own facts, two refs to it');

{
    my $then = $c->db->as_of(4)->history;
    is_deeply($then->datoms('eav'), [ grep { $_->[3] <= 4 } @all ], 'history of an old value ends where it does');
}

dies_like(sub { $h->entity(2) }, qr/^Fact::DB::Db::entity: a history value holds every assertion and retraction, not entities/, 'entity');
dies_like(sub { $h->since(1) }, qr/since is for a value a transaction made, not a history value/, 'since');
dies_like(sub { $h->with([]) }, qr/with is for a value a transaction made, not a history value/, 'with');
dies_like(sub { $c->db->since(1)->history }, qr/history is for a value a transaction made, not a since value/, 'history of a since value');

undef $c; undef $h;
is(Fact::DB::_live_blocks(), 0, 'nothing is left allocated');

done_testing;
