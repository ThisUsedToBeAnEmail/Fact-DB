#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(people dies_like);

# Functions written in Perl.

my $c = people();
$c->transact([ map { { 'person/name' => $_, 'person/age' => length $_ } } qw(Ann Bobby Cy) ]);
my $db = $c->db;
my %f = (
    'my/long'  => sub { length($_[0]) > 2 },
    'my/upper' => sub { uc $_[0] },
    'my/chars' => sub { [ split //, $_[0] ] },
    'my/pair'  => sub { [ $_[0], length $_[0] ] },
    'my/rel'   => sub { [ map { [ $_[0], $_ ] } 1 .. $_[1] ] },
    'my/none'  => sub { undef },
    'my/die'   => sub { die "no thanks\n" },
    'my/obj'   => sub { die bless({ why => 'object' }, 'My::Err') },
    'my/last'  => sub { no warnings 'exiting'; last },
    'str'      => sub { 'mine' },
);
my $o = { functions => \%f };
sub srt { [ sort { "@$a" cmp "@$b" } @{ $_[0] } ] }

is_deeply(srt($db->q('[:find ?n :where [_ :person/name ?n] [(my/long ?n)]]', $o)), [['Ann'], ['Bobby']], 'a predicate');
is_deeply(srt($db->q('[:find ?u :where [_ :person/name ?n] [(my/upper ?n) ?u]]', $o)), [['ANN'], ['BOBBY'], ['CY']], 'a function');
is_deeply(srt($db->q('[:find ?c :where [_ :person/name "Cy"] [(my/chars "Cy") [?c ...]]]', $o)), [['C'], ['y']], 'a list binding');
is_deeply($db->q('[:find ?a ?b :where [_ :person/name "Cy"] [(my/pair "Cy") [?a ?b]]]', $o), [['Cy', 2]], 'a tuple binding');
is_deeply(srt($db->q('[:find ?x ?i :where [_ :person/name "Cy"] [(my/rel "x" 2) [[?x ?i]]]]', $o)), [['x', 1], ['x', 2]], 'a relation binding');
is_deeply($db->q('[:find ?u :where [_ :person/name ?n] [(my/none ?n) ?u]]', $o), [], 'undef binds nothing: the row goes');
is_deeply($db->q('[:find ?u . :where [_ :person/name "Ann"] [(str "a") ?u]]', $o), 'mine', 'a function given wins over a built-in of its name');
is_deeply(srt($db->q('[:find ?n :where [?e :person/name ?n] [?e :person/age ?a] [(> ?a 1000)]]', { functions => { '>' => sub { 1 } } })),
          [['Ann'], ['Bobby'], ['Cy']], 'even a comparison: a > given is used for [(> ...)]');
is_deeply(srt($db->q('[:find ?n :where [?e :person/name ?n] [?e :person/age ?a] [(my/upper ?n) ?u] [(= ?u "ANN")]]', $o)), [['Ann']],
          'its result used by a later clause');
{
    my $inner = sub { scalar @{ $db->q('[:find ?n :where [_ :person/name ?n]]') } };
    is($db->q('[:find ?k . :where [_ :person/name "Ann"] [(my/count) ?k]]', { functions => { 'my/count' => $inner } }), 3,
       'a function may run a query of its own on the same value');
}

dies_like(sub { $db->q('[:find ?n :where [_ :person/name ?n] [(my/die ?n)]]', $o) }, qr/^no thanks\n\z/,
          'a die in a function is the query\'s, as it was');
{
    eval { $db->q('[:find ?n :where [_ :person/name ?n] [(my/obj ?n)]]', $o) };
    isa_ok($@, 'My::Err', 'an exception object comes through as itself');
}
{
    my @seen;
    for my $i (1 .. 2) {
        eval { $db->q('[:find ?n :where [_ :person/name ?n] [(my/last ?n)]]', $o) };
        push @seen, $@ =~ /Can't "last" outside a loop block/ ? 'error' : "other: $@";
    }
    is_deeply(\@seen, [qw(error error)], 'last in a function is an error, and the loop around the query runs on');
}
{
    local $@ = 'kept';
    $db->q('[:find ?n :where [_ :person/name ?n] [(my/long ?n)]]', $o);
    is($@, 'kept', 'a query whose functions work leaves $@ alone');
}
dies_like(sub { $db->q('[:find ?c :where [_ :person/name ?n] [(my/upper ?n) [?c ...]]]', $o) },
          qr/my\/upper binds a list, and needs an array reference from my\/upper/, 'a list binding given a scalar');
dies_like(sub { $db->q('[:find ?a ?b :where [_ :person/name ?n] [(my/chars ?n) [?a ?b]]]', $o) },
          qr/my\/chars gave a tuple of \d for a binding of 2/, 'a tuple of the wrong size');
dies_like(sub { $db->q('[:find ?n :where [_ :person/name ?n]]', { functions => [] }) },
          qr/functions is a hash reference of names to subs/, 'functions that are not a hash');
{
    my $blocks = Fact::DB::_live_blocks();
    for (1 .. 50) {
        eval { $db->q('[:find ?n :where [_ :person/name ?n] [(my/die ?n)]]', $o) };
        eval { $db->q('[:find ?c :where [_ :person/name ?n] [(my/upper ?n) [?c ...]]]', $o) };
    }
    is(Fact::DB::_live_blocks(), $blocks, 'queries that die, once parsed and kept, leave nothing more allocated');
}

done_testing;
