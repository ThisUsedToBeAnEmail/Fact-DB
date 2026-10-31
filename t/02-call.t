#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use Fact::DB;

# Every sub the database will call goes through one C function. Nothing in
# the interface calls a sub yet, so the test doors do: list and scalar
# context, a die, $@, and `last` and `next`, which must not jump through the
# library's C to a loop of the caller's.

is_deeply([ Fact::DB::_call(sub { @_ }, 1, 2, 3) ], [1, 2, 3], 'arguments in, the list out');
is_deeply([ Fact::DB::_call(sub { () }) ], [], 'an empty list');
is(Fact::DB::_call_scalar(sub { wantarray ? 'list' : defined wantarray ? 'scalar' : 'void' }),
   'scalar', 'scalar context is scalar');
is(Fact::DB::_call_scalar(sub { return }), undef, 'nothing returned is undef');
is(Fact::DB::_call_scalar(sub { $_[0] * 2 }, 21), 42, 'a scalar result');

{
    my $x = 'kept';
    my @r = Fact::DB::_call(sub { $_[0] = 'changed'; 1 }, $x);
    is($x, 'changed', 'arguments are aliased, as in any call');
}

{
    my $ok = eval { Fact::DB::_call(sub { die "inside\n" }); 1 };
    ok(!$ok, 'a die inside is thrown again');
    is($@, "inside\n", 'with its own message');

    my $obj = bless {}, 'My::Error';
    eval { Fact::DB::_call_scalar(sub { die $obj }) };
    is($@, $obj, 'an exception object comes back as the same object');
}

{
    local $@ = 'before';
    Fact::DB::_call(sub { eval { die "caught inside\n" }; 1 });
    is($@, 'before', 'a call that works leaves the caller\'s $@ as it was');
}

{
    no warnings 'exiting';
    my @seen;
    for my $i (1 .. 3) {
        my $ok = eval { Fact::DB::_call(sub { last }); 1 };
        push @seen, $ok ? 'ok' : ($@ =~ /Can't "last" outside a loop block/ ? 'error' : "other: $@");
    }
    is_deeply(\@seen, [qw(error error error)],
              '`last` inside is an error, and the caller\'s loop runs on');

    @seen = ();
    for my $i (1 .. 2) {
        eval { Fact::DB::_call_scalar(sub { next }) };
        push @seen, $@ =~ /Can't "next" outside a loop block/ ? 'error' : "other: $@";
    }
    is_deeply(\@seen, [qw(error error)], 'as is `next`');
}

{
    my $base = Fact::DB::_tmps_ix();
    for (1 .. 1000) {
        my @r = Fact::DB::_call(sub { (1) x 10 });
    }
    cmp_ok(Fact::DB::_tmps_ix(), '<=', $base + 20, 'a thousand calls leave the temporaries stack where it was');
}

is(Fact::DB::_live_blocks(), 0, 'and no blocks of the library\'s own');

done_testing;
