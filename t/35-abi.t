#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use Fact::DB;

# The C ABI: the table has a stable address, the selftest walks every entry
# in C with every answer checked, and the connection C built is one Perl can
# use.

my $p = Fact::DB::_abi_ptr();
ok($p, '_abi_ptr returns a non-zero address');
is($p, Fact::DB::_abi_ptr(), 'and the same one every call');
like($p, qr/^\d+$/, 'and an unsigned integer');
is(Fact::DB::_abi_version(), 1, 'the table is version 1');

Fact::DB::_abi_call();
pass('_abi_call from Perl, with nothing pending, does nothing');

sub walk {
    local $@ = 'before';
    my ($step, $conn) = Fact::DB::_abi_selftest();
    is($step, 0, 'every entry in the table answered as its declaration says')
        or diag("check number $step in include/factdb/factdb_abi_selftest.h failed");
    is($@, 'kept', '$@ is what the selftest set it to, after entries that refused');

    SKIP: {
        skip 'the selftest did not finish', 8 if $step;
        isa_ok($conn, 'Fact::DB', 'the connection C built');
        my $db = $conn->db;
        is($conn->_versions, 5, 'with a value for each transaction C ran, and the empty one');
        is($db->q('[:find ?a . :where [?e :person/name "Ann"] [?e :person/age ?a]]'), 62,
           'Perl reads what the cas C ran made');
        is($db->q('[:find ?e . :where [?e :person/name "Bob"]]'), undef, 'and that C retracted Bob');
        my ($first) = sort { $a <=> $b } map { $_->[3] } @{ $db->history->datoms('eav') };
        my $before = $db->as_of($first);
        ok($before->q('[:find ?e . :where [?e :person/name "Bob"]]'), 'who is there as of before');
        $conn->transact([{ 'person/name' => 'Cy', 'person/age' => 3 }]);
        is($conn->db->q('[:find (count ?e) . :where [?e :person/name]]'), 2, 'and Perl transacts on it');
        ok(eval { $conn->db->_check; 1 }, 'its indexes agree with its records') or diag($@);
        is($conn->db->count, 10, 'and it counts what it should');
    }
}

# The first walk fills the query cache with what it parses; the second is
# the one counted.
walk();
my $blocks = Fact::DB::_live_blocks();
my $hamt   = HAMT::_live_blocks();
walk();
is(Fact::DB::_live_blocks(), $blocks, 'once that connection is gone, this library holds nothing it did not');
is(HAMT::_live_blocks(), $hamt, 'and nor does HAMT');

{
    Fact::DB::_abi_selftest() for 1 .. 50;
    is(Fact::DB::_live_blocks(), $blocks, 'nor after fifty runs of it');
}

done_testing;
