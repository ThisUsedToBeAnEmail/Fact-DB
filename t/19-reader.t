#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use Fact::DB;

# The reader: every kind of form, where each begins, and what it holds.

sub r { Fact::DB::_read($_[0]) }
sub v { my $f = r($_[0]); return $f->[3] }

is_deeply(r('[]'), ['vec', 1, 1, []], 'an empty vector');
is_deeply(r('()'), ['list', 1, 1, []], 'an empty list');
is_deeply(r('{}'), ['map', 1, 1, []], 'an empty map');
is_deeply(r(':person/name'), ['kw', 1, 1, 'person/name'], 'a keyword, without its colon');
is_deeply(r(':a'), ['kw', 1, 1, 'a'], 'a keyword with no namespace');
is_deeply(r('?x'), ['sym', 1, 1, '?x'], 'a variable is a symbol');
for my $s (qw($ $db % _ ... . > >= <= != = str missing? get-else - + a.b/c)) {
    is_deeply(r($s), ['sym', 1, 1, $s], "the symbol $s");
}
is_deeply(r('42'), ['int', 1, 1, 42], 'an integer');
is_deeply(r('-7'), ['int', 1, 1, -7], 'a negative integer');
is_deeply(r('+7'), ['int', 1, 1, 7], 'a signed one');
is(v('1.5'), 1.5, 'a decimal');
is(v('2e3'), 2000, 'an exponent');
is(v('-2.5E-1'), -0.25, 'both, negative');
is_deeply(r('true'), ['true', 1, 1], 'true');
is_deeply(r('false'), ['false', 1, 1], 'false');
is_deeply(r('nil'), ['nil', 1, 1], 'nil');
is(v('"plain"'), 'plain', 'a string');
is(v('"a\"b\\\\c\nd\te\rf\/g"'), "a\"b\\c\nd\te\rf/g", 'every escape');
my $bs = chr 92;
is(v(qq{"${bs}u00e9"}), "\x{e9}", 'a \\u escape');
is(v(qq{"${bs}u263A"}), "\x{263a}", 'a wide one');
is(v(qq{"${bs}uD83D${bs}uDE00"}), "\x{1F600}", 'a surrogate pair is one character');
ok(utf8::is_utf8(v(qq{"${bs}u00e9"})), 'a string with an escape above 127 is flagged');
{
    my $s = v(qq{"caf\x{e9} \x{263a}"});
    is($s, "caf\x{e9} \x{263a}", 'a string of characters from the text');
    ok(utf8::is_utf8($s), 'flagged');
}
{
    my $bytes = "\"caf\xe9\"";
    is(v($bytes), "caf\x{e9}", 'a byte string text is read as Latin-1');
}
is_deeply(r('[1 :a ?x "s"]'), ['vec', 1, 1, [['int', 1, 2, 1], ['kw', 1, 4, 'a'], ['sym', 1, 7, '?x'], ['str', 1, 10, 's']]],
          'a vector of forms, each with its column');
is_deeply(r("[1,2 ,3]"), ['vec', 1, 1, [['int', 1, 2, 1], ['int', 1, 4, 2], ['int', 1, 7, 3]]], 'commas are spaces');
is_deeply(r("; a comment\n[1 ; another\n 2]"), ['vec', 2, 1, [['int', 2, 2, 1], ['int', 3, 2, 2]]], 'comments to the end of a line');
is_deeply(r(qq{["\x{263a}\x{263a}" 1]}), ['vec', 1, 1, [['str', 1, 2, "\x{263a}\x{263a}"], ['int', 1, 7, 1]]],
          'columns count characters, not bytes');
is_deeply(r("{:a 1 :b [2]}"), ['map', 1, 1, [['kw', 1, 2, 'a'], ['int', 1, 5, 1], ['kw', 1, 7, 'b'], ['vec', 1, 10, [['int', 1, 11, 2]]]]],
          'a map, its pairs in order');
{
    my $deep = ('[' x 256) . (']' x 256);
    ok(r($deep), '256 deep is allowed');
}

done_testing;
