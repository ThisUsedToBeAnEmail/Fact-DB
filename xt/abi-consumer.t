#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use Config;
use Cwd qw(abs_path);
use File::Copy qw(copy);
use File::Path qw(make_path);
use File::Spec;
use File::Temp qw(tempdir);
use FindBin ();

# A second extension, written as a stranger would write one, that knows
# Fact::DB only through the header ExtUtils::Depends points it at: built
# here, loaded beside Fact::DB, and made to drive connections and values
# through the table, ones Perl made and ones it made, side by side with Perl.
# Built a second time with HAMT's include directories taken away, to show
# the header needs nothing of HAMT's.

plan skip_all => 'Author tests not required for installation' unless $ENV{RELEASE_TESTING};
plan skip_all => 'no compiler recorded in Config' unless $Config{cc} && $Config{make};

my $xt_dir       = abs_path($FindBin::Bin);
my $consumer_src = File::Spec->catdir($xt_dir, 'abi-consumer');
my $dist_root    = abs_path(File::Spec->catdir($xt_dir, File::Spec->updir));
my $blib_arch    = File::Spec->catdir($dist_root, 'blib', 'arch');
my $blib_lib     = File::Spec->catdir($dist_root, 'blib', 'lib');

plan skip_all => "no consumer source at $consumer_src" unless -f File::Spec->catfile($consumer_src, 'Consumer.xs');
plan skip_all => 'Fact::DB is not built: run make first'
    unless -f File::Spec->catfile($blib_arch, 'Fact', 'DB', 'Install', 'Files.pm');

local $ENV{PERL5LIB} = join $Config{path_sep}, $blib_arch, $blib_lib, grep { defined && length } $ENV{PERL5LIB};

sub build_consumer {
    my ($no_hamt) = @_;
    my $build = tempdir(CLEANUP => 1);
    make_path(File::Spec->catdir($build, qw(lib Fact DB ABI)));
    copy(File::Spec->catfile($consumer_src, $_), File::Spec->catfile($build, $_)) or die "copy $_: $!"
        for 'Makefile.PL', 'Consumer.xs';
    copy(File::Spec->catfile($consumer_src, qw(lib Fact DB ABI Consumer.pm)),
         File::Spec->catfile($build, qw(lib Fact DB ABI Consumer.pm))) or die "copy: $!";
    local $ENV{FACT_DB_CONSUMER_NO_HAMT} = $no_hamt ? 1 : 0;
    my $log = `cd "$build" && "$^X" Makefile.PL 2>&1 && "$Config{make}" 2>&1`;
    return ($build, $?, $log);
}

{
    my (undef, $rc, $log) = build_consumer(1);
    is($rc, 0, 'a consumer with none of HAMT\'s include directories builds: the header needs nothing of HAMT\'s')
        or diag($log);
    unlike($log, qr/^INC: .*HAMT/m, 'and HAMT\'s directory really was taken away') or diag($log);
}

my ($build, $rc, $log) = build_consumer(0);
is($rc, 0, 'a consumer that knows only fact_db_abi.h configures and builds') or do {
    diag($log);
    done_testing;
    exit;
};
unlike($log, qr/warning:/, 'without a compiler warning') or diag($log);

unshift @INC, File::Spec->catdir($build, 'blib', 'arch'), File::Spec->catdir($build, 'blib', 'lib');
require Fact::DB;
ok(eval { require Fact::DB::ABI::Consumer; 1 }, 'and loads beside Fact::DB') or do { diag($@); done_testing; exit };

my $C = 'Fact::DB::ABI::Consumer';
no strict 'refs';
my %c = map { ($_ => \&{"${C}::$_"}) } qw(abi_version make add_attr is_conn is_db db versions transact with
    basis_tx count travel entity datoms q q_stop explain);
use strict 'refs';

is($c{abi_version}->(), Fact::DB::_abi_version(), 'it resolved the table, at the version Fact::DB reports');

my $schema = {
    'person/name'   => { type => 'string', unique => 'identity' },
    'person/age'    => { type => 'int', index => 1 },
    'person/friend' => { type => 'ref', cardinality => 'many' },
};
my $names = '[:find ?n :where [_ :person/name ?n]]';
sub sorted { [ sort map { ref $_ ? $_->[0] : $_ } @{ $_[0] } ] }

# ---- a connection made in C, used from Perl, and the other way ---------------
sub everything {
    my $conn = $c{make}->();
    isa_ok($conn, 'Fact::DB', 'a connection made in C');
    ok($c{is_conn}->($conn), 'is_conn says yes to it');
    ok(!$c{is_conn}->({}), 'and no to a hash');
    ok(!$c{is_conn}->(bless {}, 'Fact::DB'), 'and to a forged one');
    ok(!$c{is_db}->($conn), 'is_db says no to a connection');
    is($conn->schema->{'person/age'}{index}, 1, 'Perl reads the schema C gave it');

    my ($code, $db, $tx, $ids) = $c{transact}->($conn, [
        { 'db/id' => 'ann', 'person/name' => 'Ann', 'person/age' => 61 },
        [ add => 'bob', 'person/name', 'Bob' ],
        [ add => 'bob', 'person/friend', 'ann' ],
    ]);
    is($code, 0, 'C transacts: OK');
    ok($c{is_db}->($db), 'making a value');
    isa_ok($db, 'Fact::DB::Db', 'that Perl sees as one');
    is($db->basis_tx, $tx, 'at the transaction C was told');
    is_deeply([ sort keys %$ids ], [qw(ann bob)], 'with both tempids');
    is($db, $conn->db, 'and it is the connection\'s value, the same object');
    is_deeply(sorted($db->q($names)), [qw(Ann Bob)], 'Perl queries what C wrote');
    is($db->q('[:find ?e . :where [?e :person/friend ?f] [?f :person/name "Ann"]]'), $ids->{bob}, 'refs and all');

    $conn->transact([{ 'person/name' => 'Cy', 'person/age' => 3 }]);
    my ($qc, $rows) = $c{q}->($conn->db, $names);
    is($qc, 0, 'C queries what Perl wrote');
    is_deeply(sorted($rows), [qw(Ann Bob Cy)], 'and sees it');
    is($c{versions}->($conn), 3, 'one version a transaction, whoever ran it');

    my $p = Fact::DB->new(schema => $schema);
    ($code) = $c{transact}->($p, [[ add => 'x', 'person/name', 'Xi' ]]);
    is($code, 0, 'C transacts on a connection Perl made');
    is($p->db->q('[:find ?n . :where [_ :person/name ?n]]'), 'Xi', 'and Perl sees it');

    # ---- refusals ----------------------------------------------------------------
    {
        local $@ = 'kept';
        my $before = $c{versions}->($conn);
        my ($rc, $err) = $c{transact}->($conn, [[ add => $ids->{ann}, 'person/age', 62 ], [ add => $ids->{ann}, 'no/such', 1 ]]);
        is($rc, -4, 'an attribute not in the schema: REFUSED');
        like($err, qr/^fact_db transact: \[1\] no\/such is not in the schema/, 'with the words Perl would say');
        eval { $conn->transact([[ 'db/add', $ids->{ann}, 'no/such', 1 ]]) };
        like($@, qr/no\/such is not in the schema/, 'as Perl does say');
        $@ = 'kept';
        is($c{versions}->($conn), $before, 'and nothing changed');
        is($conn->db->q('[:find ?a . :where [?e :person/name "Ann"] [?e :person/age ?a]]'), 61, 'not even the half that was fine');
        ($rc, $err) = $c{transact}->($conn, [[ add => 'z', 'person/age', 'old' ]]);
        is($rc, -4, 'a value its type refuses: REFUSED');
        ($rc, $err) = $c{transact}->($conn, [[ add => 999999, 'person/age', 1 ]]);
        is($rc, -4, 'an id not given out: REFUSED');
        ($rc, $err) = $c{add_attr}->($conn, 'person/x', 0, 0, 0, 0);
        is($rc, -4, 'an attribute of no type: REFUSED');
        ($rc, $err) = $c{add_attr}->($conn, 'person/age', 1, 0, 0, 0);
        is($rc, -4, 'one already there as another type: REFUSED');
        is($conn->schema->{'person/age'}{type}, 'int', 'and it is as it was');
        is(($c{add_attr}->({}, 'a/b', 1, 0, 0, 0))[0], -2, 'add_attr on a hash: NOTDB');
        is(($c{transact}->([], []))[0], -2, 'transact on an array: NOTDB');
        is($c{db}->([]), undef, 'and db: NULL');
        is($c{versions}->($db), -2, 'versions of a value, not a connection: NOTDB');
        is($c{count}->($conn), -2, 'count of a connection, not a value: NOTDB');
        is($@, 'kept', '$@ is as it was through every refusal');
    }

    # ---- reads -----------------------------------------------------------------
    my $now = $conn->db;
    my ($ec, $pairs) = $c{entity}->($now, $ids->{bob});
    is($ec, 0, 'entity: OK');
    is_deeply([ sort map { "$_->[0]=$_->[1]" } @$pairs ], [ "person/friend=$ids->{ann}", 'person/name=Bob' ],
              'every fact of it, db/id left out');
    ($ec, $pairs) = $c{entity}->($now, [ 'person/name', 'Ann' ]);
    is(scalar @$pairs, 2, 'by a lookup ref too');
    my ($xc, $xerr) = $c{entity}->($now, $ids->{bob}, 1);
    is($xc, -4, 'a callback that croaks: REFUSED');
    like($xerr, qr/^a consumer callback croaked at person\//, 'with its words');
    ($xc, $xerr) = $c{entity}->($now, [ 'person/name', 'Nobody' ]);
    is($xc, -4, 'a lookup ref to nobody: REFUSED');

    my ($dc, $dat) = $c{datoms}->($now, 3, 'person/age');
    is($dc, 0, 'datoms of ave: OK');
    is_deeply([ sort { $a->[0] <=> $b->[0] } map { [ @$_[0 .. 3] ] } @$dat ],
              [ sort { $a->[0] <=> $b->[0] } @{ $now->datoms(ave => 'person/age') } ], 'what Perl\'s datoms gives');
    is($dat->[0][4], -1, 'with -1 for "not a history value"');
    ($dc, $dat) = $c{datoms}->($now, 4, $ids->{ann});
    is_deeply([ map { $_->[0] } @$dat ], [ $ids->{bob} ], 'vae: who refers to Ann');
    is(($c{add_attr}->($conn, 'person/nick', 1, 0, 0, 0))[0], 0, 'C adds an attribute with no index');
    ($dc, $dat) = $c{datoms}->($now, 3, 'person/nick');
    is($dc, -4, 'ave of an attribute not indexed: REFUSED');
    like($dat, qr/is not indexed/, 'as Perl says');
    ($dc) = $c{datoms}->($now, 9);
    is($dc, -4, 'an index of no number: REFUSED');

    # ---- queries -----------------------------------------------------------------
    my $older = '[:find ?n :in $ ?min :where [?e :person/age ?a] [(> ?a ?min)] [?e :person/name ?n]]';
    my ($qr, $out) = $c{q}->($now, $older, 40);
    is_deeply($out, ['Ann'], 'a query with an input');
    is_deeply([ $c{q}->($now, '[:find ?n ?a :where [?e :person/name ?n] [?e :person/age ?a] [(< ?a 10)]]') ],
              [ 0, [ [ 'Cy', 3 ] ] ], 'a relation of two: a row a tuple');
    is_deeply([ $c{q}->($now, '[:find (count ?e) . :where [?e :person/name]]') ], [ 0, [3] ], 'a scalar: one row');
    is_deeply([ $c{q}->($now, '[:find ?n . :where [?e :person/name ?n] [(= ?n "Zed")]]') ], [ 0, [] ],
              'a scalar of nothing: no row');
    is_deeply([ $c{q}->($now, '[:find [?n ...] :where [_ :person/name ?n] [(= ?n "Bob")]]') ], [ 0, ['Bob'] ],
              'a collection: a row a value');
    is_deeply([ $c{q}->($now, '[:find [?n ?a] :where [?e :person/name ?n] [?e :person/age ?a] [(> ?a 50)]]') ],
              [ 0, [ [ 'Ann', 61 ] ] ], 'a tuple: one row');
    my $rules = '[[(knows ?a ?b) [?a :person/friend ?b]] [(knows ?a ?b) [?a :person/friend ?c] (knows ?c ?b)]]';
    ($qr, $out) = $c{q}->($now, '[:find ?n :in $ % :where [?b :person/name "Bob"] (knows ?b ?f) [?f :person/name ?n]]', $rules);
    is_deeply([ $qr, $out ], [ 0, ['Ann'] ], 'rules, as an input');
    my $calls = 0;
    ($qr, $out) = $c{q}->($now, '[:find ?d :where [_ :person/age ?a] [(my/double ?a) ?d] [(> ?a 50)]]',
                         { 'my/double' => sub { $calls++; $_[0] * 2 } });
    is_deeply([ $qr, $out ], [ 0, [122] ], 'a Perl function, given in functions');
    ok($calls, 'and called');
    {
        local $@ = 'kept';
        ($qr, $out) = $c{q}->($now, '[:find ?d :where [_ :person/age ?a] [(my/bad ?a) ?d]]',
                             { 'my/bad' => sub { die "bad function\n" } });
        is($qr, -4, 'a Perl function that dies: REFUSED');
        like($out, qr/bad function/, 'with its words');
        is($@, 'kept', 'and $@ is as it was');
    }
    ($qr, $out) = $c{q}->($now, '[:find ?x :where');
    is($qr, -4, 'a query that does not parse: REFUSED');
    eval { $now->q('[:find ?x :where') };
    (my $perl_err = $@) =~ s/^Fact::DB::Db::q: //;
    $perl_err =~ s/ at .*//s;
    like($out, qr/^fact_db query_parse: \Q$perl_err\E/, 'with the words Perl says');
    ($qr) = $c{q}->($now, $older);
    is($qr, -4, 'an input missing: REFUSED');
    ($qr) = $c{q}->($conn, $names);
    is($qr, -2, 'a query of a connection, not a value: NOTDB');
    is($c{q_stop}->($now, $names, 2), -3, 'a callback that stops: STOPPED');
    is($c{explain}->($now, $older), $now->explain($older), 'explain says what Perl\'s says');

    my (undef, $hits) = Fact::DB::_cache_stats();
    $now->q($older, 40);
    my (undef, $hits2) = Fact::DB::_cache_stats();
    is($hits2, $hits + 1, 'a query C parsed is the one Perl finds in the cache');

    # ---- time ----------------------------------------------------------------------
    is($c{count}->($c{travel}->($now, 'as_of', $tx)), $now->as_of($tx)->count, 'as_of from C counts what Perl\'s does');
    is($c{count}->($c{travel}->($now, 'since', $tx)), $now->since($tx)->count, 'and since');
    my $h = $c{travel}->($now, 'history');
    isa_ok($h, 'Fact::DB::Db', 'a history value');
    ok($h->is_history, 'that is one');
    my ($hv, $herr) = $c{travel}->($now->since($tx), 'as_of', 0);
    is($hv, undef, 'as_of of a since value: NULL');
    like($herr, qr/since/, 'saying why');
    ($hv) = $c{travel}->($now, 'as_of', -1);
    is($hv, undef, 'as_of a negative transaction: NULL');

    # ---- with, cas, retractEntity -------------------------------------------------
    my $before = $c{versions}->($conn);
    my ($wc, $wdb) = $c{with}->($now, [[ cas => $ids->{ann}, 'person/age', 61, 62 ]]);
    is($wc, 0, 'with: OK');
    is($wdb->q('[:find ?a . :where [?e :person/name "Ann"] [?e :person/age ?a]]'), 62, 'its value has the change');
    is($c{versions}->($conn), $before, 'and the connection does not');
    is(($c{transact}->($conn, [[ cas => $ids->{ann}, 'person/age', 61, 62 ]], 2))[0], -4,
       'one transaction run twice: the second cas is REFUSED');
    is($c{versions}->($conn), $before + 1, 'and only the first is kept');
    is(($c{transact}->($conn, [[ retractEntity => $ids->{bob} ]]))[0], 0, 'retractEntity');
    is_deeply(sorted($conn->db->q($names)), [qw(Ann Cy)], 'Bob is gone');
    ok(eval { $conn->db->_check; 1 }, 'and the indexes agree with the records') or diag($@);
}

everything();
my $blocks = Fact::DB::_live_blocks();
my $hamt   = HAMT::_live_blocks();
everything();
is(Fact::DB::_live_blocks(), $blocks, 'when everything it made is gone, nothing is left allocated');
is(HAMT::_live_blocks(), $hamt, 'by HAMT either');

done_testing;
