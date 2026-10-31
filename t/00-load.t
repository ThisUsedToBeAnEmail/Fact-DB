#!perl
use 5.010;
use strict;
use warnings;
use Test::More;

# The module loads, and it has found HAMT twice over: the header when it was
# built and the table when it was loaded. Everything later leans on both.

use_ok('Fact::DB') or BAIL_OUT('Fact::DB does not load');
use_ok('Fact::DB::Db') or BAIL_OUT('Fact::DB::Db does not load');

for my $m (qw(Fact::DB Fact::DB::Db)) {
    no strict 'refs';
    my $v = ${"${m}::VERSION"};
    like($v, qr/^\d+\.\d+$/, "$m has a version, a plain number");
}
is($Fact::DB::Db::VERSION, $Fact::DB::VERSION, 'the same version in both');

ok($INC{'HAMT.pm'}, 'loading it loads HAMT');

can_ok('Fact::DB', '_hamt_abi_version');

my $built = Fact::DB::_hamt_abi_built();
my $found = Fact::DB::_hamt_abi_version();
cmp_ok($built, '>=', 1, 'it was compiled against a version of HAMT\'s table');
cmp_ok($found, '>=', $built, 'and the table it found is that version or a later one');
is($found, HAMT::_abi_version(), 'which is the table HAMT says it has');

# A table that resolved is one that can be called.
{
    my ($n, $last, $bad) = Fact::DB::_hamt_roundtrip();
    is($n, 0, 'a map made through the table starts empty');
    is($last, undef, 'with nothing under any key');
    is($bad, 0, 'and well formed');

    ($n, $last, $bad) = Fact::DB::_hamt_roundtrip(map { "v$_" } 1 .. 100);
    is($n, 100, 'a hundred stores through the table are a hundred keys');
    is($last, 'v100', 'the last key holding the last value stored');
    is($bad, 0, 'in a map HAMT calls well formed');

    my $blocks = HAMT::_live_blocks();
    Fact::DB::_hamt_roundtrip(map { "v$_" } 1 .. 5000) for 1 .. 3;
    is(HAMT::_live_blocks(), $blocks, 'and given up, it leaves nothing allocated in HAMT');

    my $ref = [1, 2, 3];
    ($n, $last) = Fact::DB::_hamt_roundtrip('a', $ref);
    is($last, $ref, 'a reference goes in and comes out the same reference');
}

is(Fact::DB::_live_blocks(), 0, 'the library holds no blocks of its own');
is(Fact::DB::_alloc_roundtrip(1000), 1000, 'its allocator counts the blocks it hands out');
is(Fact::DB::_live_blocks(), 0, 'and the ones given back');
is(Fact::DB::_alloc_roundtrip(0), 0, 'none is none');

diag("Testing Fact::DB $Fact::DB::VERSION on HAMT $HAMT::VERSION (C ABI $found), Perl $], $^X");

done_testing;
