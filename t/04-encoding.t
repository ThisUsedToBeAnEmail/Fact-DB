#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use Config;
use Fact::DB;

# The value encoding. Two values are one value exactly when their encodings
# are the same bytes, and an encoding reads back as exactly what it encodes.

sub enc { my @r = Fact::DB::_encode(@_); return wantarray ? @r : $r[0] }
sub dec { Fact::DB::_decode(@_) }
sub canon { my ($t, $v) = @_; my ($b, $u) = enc($t, $v); return dec($t, $b, $u) }

sub refused {
    my ($type, $value, $re, $name) = @_;
    my $ok = eval { enc($type, $value); 1 };
    ok(!$ok, "$type: $name is refused") or return;
    like($@, qr/^Fact::DB::_encode: (?:.*?)$re/, "$type: $name, saying why") or diag $@;
}

for my $type (qw(string int num bool ref instant)) {
    refused($type, undef, qr/undef is not a value; retracting a fact is how it goes away/, 'undef');
}
for my $type (qw(string int num bool instant)) {
    refused($type, [1], qr/a reference is not a value of type $type/, 'a reference');
}

# ---- string

is_deeply([ enc(string => 'plain') ], [ 'plain', 0 ], 'a string is its bytes');
is_deeply([ enc(string => '') ], [ '', 0 ], 'the empty string is a value');
is_deeply([ enc(string => "a\0b") ], [ "a\0b", 0 ], 'a NUL is kept');
is_deeply([ enc(string => 42) ], [ '42', 0 ], 'a number given for a string is its string');
{
    my $latin = "caf\x{e9}";
    my $up = $latin;
    utf8::upgrade($up);
    is_deeply([ enc(string => $up) ], [ enc(string => $latin) ], 'an upgraded string encodes as its downgraded spelling');
    is_deeply([ enc(string => $up) ], [ "caf\xe9", 0 ], 'as bytes, not UTF-8');
    my $wide = "smile \x{263a}";
    my ($b, $u) = enc(string => $wide);
    is($u, 1, 'a string that cannot be downgraded is UTF-8');
    is($b, do { my $x = $wide; utf8::encode($x); $x }, 'held as its UTF-8 bytes');
    my $back = dec(string => $b, $u);
    is($back, $wide, 'and reads back as the same characters');
    ok(utf8::is_utf8($back), 'flagged');
    is(canon(string => $up), $latin, 'an upgraded string reads back as the same characters');
}

# ---- int

my @ints = (
    [ 42,        '42' ],   [ '42',      '42' ],  [ '042', '42' ], [ 42.0, '42' ],
    [ '42.0',    '42' ],   [ '1e3',   '1000' ],  [ 1e3, '1000' ], [ -7, '-7' ],
    [ '-0',       '0' ],   [ -0.0,       '0' ],  [ 0, '0' ],      [ '+5', '5' ],
    [ ' 12',     '12' ],
    [ '-7',      '-7' ],   [ '-042',  '-42' ],
);
for my $c (@ints) {
    my ($in, $want) = @$c;
    is(enc(int => $in), $want, "int: '$in' encodes as $want");
}
{
    my $max = ~0 >> 1;
    my $min = -$max - 1;
    is(enc(int => $max), "$max", 'int: the largest IV');
    is(enc(int => $min), "$min", 'int: the smallest IV');
    is(enc(int => "$min"), "$min", 'int: the smallest IV, as a string');
    is(enc(int => ~0), sprintf('%u', ~0), 'int: the largest UV');
    is(dec(int => sprintf('%u', ~0), 0), ~0, 'int: which reads back as the UV');
    is(dec(int => "$min", 0), $min, 'int: the smallest IV reads back');
    refused(int => sprintf('%u', ~0) . '0', qr/outside the range of a whole number here/, 'one digit past the largest UV');
    refused(int => "-${max}00", qr/outside the range/, 'far below the smallest IV');
}
refused(int => '4.5', qr/"4\.5" is not a whole number/, 'a fraction');
refused(int => 4.5, qr/is not a whole number/, 'a fractional number');
refused(int => 'abc', qr/"abc" is not a whole number/, 'a word');
refused(int => '', qr/"" is not a whole number/, 'the empty string');
refused(int => '12abc', qr/is not a whole number/, 'digits then letters');
refused(int => '0x10', qr/is not a whole number/, 'hex');
refused(int => 'inf', qr/is not a whole number/, 'infinity');
refused(int => 9**9**9, qr/is not a whole number/, 'an infinite number');
refused(int => 'nan', qr/is not a whole number/, 'NaN');
is(dec(int => '42', 0), 42, 'int: decodes');
is(canon(int => '042') + 0, 42, 'int: the value kept is the canonical one');

# ---- ref

is(enc(ref => 7), '7', 'ref: an entity id');
is(enc(ref => '7'), '7', 'ref: as a string');
refused(ref => 0, qr/a ref is an entity id, a positive whole number, not "0"/, 'zero');
refused(ref => -3, qr/a positive whole number, not "-3"/, 'a negative number');
refused(ref => 'x', qr/"x" is not a whole number/, 'a word');
refused(ref => [1], qr/a ref is an entity id, a positive whole number, not a reference/, 'a reference');

# ---- num and instant

for my $type (qw(num instant)) {
    is(enc($type => '1e3'), enc($type => 1000), "$type: 1e3 and 1000 are one value");
    is(enc($type => '0.1'), enc($type => 0.1), "$type: '0.1' and 0.1 are one value");
    is(enc($type => -0.0), '0', "$type: -0 is 0");
    is(enc($type => '-0'), '0', "$type: '-0' is 0");
    is(enc($type => 42), '42', "$type: a whole number is written as one");
    is(enc($type => -2.5), '-2.5', "$type: a negative number");
    is(enc($type => 0.1), '0.1', "$type: 0.1 is written in the fewest digits that read back as it");
    is(enc($type => 1 / 4), '0.25', "$type: as is a quarter");
    refused($type => 'nan', qr/"nan" is not a finite number/, 'NaN');
    refused($type => 'inf', qr/"inf" is not a finite number/, 'infinity as a string');
    refused($type => 9**9**9, qr/is not a finite number/, 'an infinite number');
    refused($type => -9**9**9, qr/is not a finite number/, 'minus infinity');
    refused($type => 'abc', qr/"abc" is not a number/, 'a word');
    refused($type => '', qr/"" is not a number/, 'the empty string');
}
{
    my $third = 1 / 3;
    is(canon(num => $third), $third, 'num: a third reads back as exactly a third');
    is(canon(num => 1e308), 1e308, 'num: near the largest double');
    is(canon(num => 5e-324), 5e-324, 'num: the smallest denormal double');

    srand 42;
    my ($round, $stable, $n) = (0, 0, 2000);
    for (1 .. $n) {
        my $x = (rand() - 0.5) * 10 ** (int(rand 40) - 20);
        my ($b, $u) = enc(num => $x);
        my $back = dec(num => $b, $u);
        $round++  if $back == $x;
        $stable++ if enc(num => $back) eq $b;
    }
    is($round,  $n, "num: $n random numbers each read back as exactly themselves (nvsize $Config{nvsize})");
    is($stable, $n, 'num: and encode again as the same bytes');
}

# ---- bool

for my $c ([1, '1'], ['x', '1'], ['0.0', '1'], ['00', '1'], [0, '0'], ['0', '0'], ['', '0']) {
    my ($in, $want) = @$c;
    is(enc(bool => $in), $want, "bool: '$in' is $want");
}
is(dec(bool => '1', 0), 1, 'bool: true reads back as 1');
is(dec(bool => '0', 0), 0, 'bool: false as 0');

# ---- magic

{
    package Counting;
    sub TIESCALAR { my ($class, $v) = @_; return bless { v => $v, n => 0 }, $class }
    sub FETCH { my $s = shift; $s->{n}++; return $s->{v} }
}
for my $type (qw(string int num bool)) {
    tie my $t, 'Counting', 12;
    is(enc($type => $t), $type eq 'bool' ? '1' : '12', "$type: a tied value encodes");
    is(tied($t)->{n}, 1, "$type: read once");
}

# ---- locale

SKIP: {
    my $ok = eval { require POSIX; 1 };
    skip 'no POSIX', 2 unless $ok;
    my ($comma) = grep { defined POSIX::setlocale(POSIX::LC_NUMERIC(), $_) }
                  qw(de_DE.UTF-8 de_DE.utf8 de_DE fr_FR.UTF-8 fr_FR.utf8 fr_FR);
    skip 'no locale with a decimal comma here', 2 unless $comma;
    my ($b) = enc(num => 1.5);
    my $back = dec(num => $b, 0);
    {
        use locale;
        my ($b2) = enc(num => 1.5);
        is($b2, '1.5', "num: under use locale in $comma, 1.5 still encodes as 1.5");
    }
    POSIX::setlocale(POSIX::LC_NUMERIC(), 'C');
    is($b . '/' . ($back == 1.5 ? 'same' : $back), '1.5/same', "num: and in $comma outside use locale too");
}

done_testing;
