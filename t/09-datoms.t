#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(people dies_like);

# Every way of reading datoms, against a model kept in Perl, over 500 random
# transactions: checked at every 50th version, and the values held at those
# versions checked again at the end, after everything since.

my %EXTRA = (
    'person/likes'   => { type => 'string', cardinality => 'many', index => 1 },
    'person/friends' => { type => 'ref', cardinality => 'many' },
);
my $c = people(%EXTRA);
my $schema = $c->schema;
my @ATTRS = grep { !/^db\// } sort keys %$schema;
my @INDEXED = grep { $schema->{$_}{index} } @ATTRS;
my @REFS = grep { $schema->{$_}{type} eq 'ref' } @ATTRS;

my %m;
my @ents;
my $ntx = 0;
my $fresh = 0;

sub mvalues { my ($e, $a) = @_; return $m{$e} && $m{$e}{$a} ? values %{ $m{$e}{$a} } : () }

srand 20261014;
sub pick { $_[int rand @_] }

sub random_tx {
    my (@data, %one, %fact);
    my $ops = 1 + int rand 8;
    for (1 .. $ops) {
        my $kind = @ents ? int rand 10 : 0;
        if ($kind == 0) {
            my $n = ++$fresh;
            my %h = ('db/id' => "t$n", 'person/name' => "n$n");
            $h{'person/email'} = "e$n" if rand > 0.5;
            $h{'person/age'} = int rand 10 if rand > 0.3;
            $h{'person/tags'} = [ grep { rand > 0.5 } qw(a b c d e) ];
            delete $h{'person/tags'} unless @{ $h{'person/tags'} };
            $h{'person/boss'} = pick(@ents) if @ents && rand > 0.5;
            $h{'person/score'} = int(rand 40) / 4 if rand > 0.5;
            $h{'person/likes'} = [ grep { rand > 0.6 } qw(tea cake jam) ];
            delete $h{'person/likes'} unless @{ $h{'person/likes'} };
            push @data, \%h;
            next;
        }
        my $e = pick(@ents);
        my ($op, $a, $v);
        if    ($kind == 1) { ($op, $a, $v) = ('db/add', 'person/age', int rand 10) }
        elsif ($kind == 2) { ($op, $a, $v) = ('db/add', pick('person/tags', 'person/likes'), pick(qw(a b c d e tea cake jam))) }
        elsif ($kind == 3) { my $at = pick('person/tags', 'person/likes'); my @have = mvalues($e, $at);
                             ($op, $a, $v) = ('db/retract', $at, @have && rand > 0.2 ? pick(@have)->[0] : 'zz') }
        elsif ($kind == 4) { my @have = mvalues($e, 'person/age');
                             ($op, $a, $v) = ('db/retract', 'person/age', @have ? $have[0][0] : 3) }
        elsif ($kind == 5) { ($op, $a, $v) = ('db/add', 'person/email', 'e' . ++$fresh) }
        elsif ($kind == 6) { ($op, $a, $v) = ('db/add', 'person/boss', pick(@ents)) }
        elsif ($kind == 7) { ($op, $a, $v) = ('db/add', 'person/friends', pick(@ents)) }
        elsif ($kind == 8) { my @have = mvalues($e, 'person/friends');
                             next unless @have; ($op, $a, $v) = ('db/retract', 'person/friends', pick(@have)->[0]) }
        else               { my @have = mvalues($e, 'person/name');
                             next unless @have && rand > 0.7; ($op, $a, $v) = ('db/retract', 'person/name', $have[0][0]) }
        next if $schema->{$a}{cardinality} eq 'one' && $op eq 'db/add' && $one{"$e $a"}++;
        next if $fact{"$e $a $v"} && $fact{"$e $a $v"} ne $op;
        next if $op eq 'db/retract' && $schema->{$a}{cardinality} eq 'one' && $one{"$e $a"};
        $one{"$e $a"}++ if $schema->{$a}{cardinality} eq 'one' && $op eq 'db/retract';
        $fact{"$e $a $v"} = $op;
        push @data, [ $op, $e, $a, $v ];
    }
    return \@data;
}

sub apply_model {
    my ($r) = @_;
    for my $d (@{ $r->{tx_data} }) {
        my ($e, $a, $v, $tx, $added) = @$d;
        next if $a eq 'db/txInstant';
        if ($added) { $m{$e}{$a}{$v} = [ $v, $tx ] }
        else {
            delete $m{$e}{$a}{$v};
            delete $m{$e}{$a} unless %{ $m{$e}{$a} };
            delete $m{$e} unless %{ $m{$e} };
        }
    }
    my %seen = map { $_ => 1 } @ents;
    push @ents, grep { !$seen{$_}++ } sort { $a <=> $b } values %{ $r->{tempids} };
    $ntx++;
}

sub rows { [ sort { $a->[0] <=> $b->[0] || $a->[1] cmp $b->[1] || "$a->[2]" cmp "$b->[2]" } @_ ] }
sub got { my ($db, @args) = @_; rows(grep { $_->[1] ne 'db/txInstant' } @{ $db->datoms(@args) }) }
sub want {
    my ($model, %f) = @_;
    my @out;
    for my $e (keys %$model) {
        next if defined $f{e} && $e != $f{e};
        for my $a (keys %{ $model->{$e} }) {
            next if defined $f{a} && $a ne $f{a};
            next if $f{refs} && $schema->{$a}{type} ne 'ref';
            for my $vt (values %{ $model->{$e}{$a} }) {
                next if defined $f{v} && "$vt->[0]" ne "$f{v}";
                push @out, [ $e + 0, $a, $vt->[0], $vt->[1] ];
            }
        }
    }
    return rows(@out);
}
sub entity_want {
    my ($model, $e) = @_;
    my %h = ('db/id' => $e);
    for my $a (keys %{ $model->{$e} || {} }) {
        my @v = sort map { "$_->[0]" } values %{ $model->{$e}{$a} };
        $h{$a} = $schema->{$a}{cardinality} eq 'many' ? \@v : $v[0];
    }
    return \%h;
}
sub entity_got {
    my ($db, $e) = @_;
    my $h = $db->entity($e);
    delete $h->{'db/txInstant'};
    ref $h->{$_} and $h->{$_} = [ sort map { "$_" } @{ $h->{$_} } ] for keys %$h;
    return $h;
}

sub deep { my ($m) = @_; return { map { my $e = $_; ($e => { map { my $a = $_; ($a => { %{ $m->{$e}{$a} } }) } keys %{ $m->{$e} } }) } keys %$m } }

sub compare {
    my ($db, $model, $label, $full, $ents) = @_;
    my $bad = 0;
    my @es = sort { $a <=> $b } @{ $ents || \@ents };
    @es = grep { rand() < 0.2 } @es unless $full;
    for my $e (@es) {
        $bad++ unless is_deeply(entity_got($db, $e), entity_want($model, $e), "$label: entity $e") || 0;
        $bad++ unless is_deeply(got($db, eav => $e), want($model, e => $e), "$label: eav $e");
        my $a = pick(@ATTRS);
        $bad++ unless is_deeply(got($db, eav => $e, $a), want($model, e => $e, a => $a), "$label: eav $e $a");
        my @vs = map { $_->[0] } values %{ $model->{$e}{$a} || {} };
        if (@vs) {
            $bad++ unless is_deeply(got($db, eav => $e, $a, $vs[0]), want($model, e => $e, a => $a, v => $vs[0]), "$label: eav $e $a $vs[0]");
            $bad++ unless is_deeply(got($db, aev => $a, $e, $vs[0]), want($model, e => $e, a => $a, v => $vs[0]), "$label: aev $a $e $vs[0]");
        }
        $bad++ unless is_deeply(got($db, vae => $e), want($model, v => $e, refs => 1), "$label: vae $e");
        for my $ref (@REFS) {
            my $w = want($model, v => $e, a => $ref);
            $bad++ unless is_deeply(got($db, vae => $e, $ref), $w, "$label: vae $e $ref");
            if (@$w) {
                my $from = $w->[0][0];
                $bad++ unless is_deeply(got($db, vae => $e, $ref, $from), want($model, v => $e, a => $ref, e => $from),
                                        "$label: vae $e $ref $from");
            }
        }
    }
    for my $a (@ATTRS) {
        $bad++ unless is_deeply(got($db, aev => $a), want($model, a => $a), "$label: aev $a");
    }
    for my $a (@INDEXED) {
        $bad++ unless is_deeply(got($db, ave => $a), want($model, a => $a), "$label: ave $a");
        my %vals = map { ("$_->[2]" => $_->[2]) } @{ want($model, a => $a) };
        for my $v ((values %vals)[0 .. 2]) {
            next unless defined $v;
            $bad++ unless is_deeply(got($db, ave => $a, $v), want($model, a => $a, v => $v), "$label: ave $a $v");
        }
    }
    for my $a (@ATTRS) {
        my %vals = map { ("$_->[2]" => $_->[2]) } @{ want($model, a => $a) };
        for my $v ((sort keys %vals)[0 .. 1]) {
            next unless defined $v;
            $bad++ unless is_deeply(rows(@{ $db->_scan_av($a, $vals{$v}) }), want($model, a => $a, v => $vals{$v}),
                                    "$label: $a = $v, indexed or not");
        }
    }
    for my $a ('person/age', 'person/tags') {
        my $v = $a eq 'person/age' ? 4 : 'b';
        my $d = $db->datoms(aev => $a);
        $bad++ unless is_deeply(rows(grep { "$_->[2]" eq "$v" } @$d), want($model, a => $a, v => $v), "$label: aev $a filtered to $v");
    }
    $bad++ unless is_deeply(got($db, 'eav'), want($model), "$label: every datom");
    return $bad;
}

my @held;
for my $i (1 .. 500) {
    my $r = $c->transact(random_tx());
    apply_model($r);
    if ($i % 50 == 0) {
        my $n = 0;
        $n += scalar map { values %$_ } map { values %$_ } values %m;
        is($c->db->count, $n + $ntx, "after $i transactions: count is every fact and every transaction's own");
        compare($c->db, \%m, "after $i", $i % 250 == 0);
        push @held, [ $c->db, deep(\%m), $i, [@ents] ];
    }
}
cmp_ok(scalar(@ents), '>', 100, 'the run made a fair number of entities (' . scalar(@ents) . ')');
for my $h (@held) {
    my ($db, $model, $i, $ents) = @$h;
    compare($db, $model, "version $i, read at the end", 0, $ents);
}

dies_like(sub { $c->db->datoms('xyz') }, qr/^Fact::DB::Db::datoms: unknown index "xyz"; the indexes are eav, aev, ave and vae/, 'an unknown index');
dies_like(sub { $c->db->datoms(ave => 'person/tags') }, qr/person\/tags is not indexed, so it has no ave/, 'ave of an unindexed attribute');
dies_like(sub { $c->db->datoms(ave => 'person/nope') }, qr/person\/nope is not in the schema/, 'an unknown attribute');
dies_like(sub { $c->db->datoms(vae => 2, 'person/age') }, qr/person\/age is not a ref/, 'vae through a non-ref');
dies_like(sub { $c->db->datoms(eav => 2, 'person/age', 'x') }, qr/person\/age: "x" is not a whole number/, 'a value its type refuses');
dies_like(sub { $c->db->datoms(eav => 1, 'person/age', 1, 2) }, qr/at most three components/, 'four components');
dies_like(sub { $c->db->datoms(ave => ) }, qr/ave needs an attribute/, 'ave with nothing');
dies_like(sub { $c->db->datoms(vae => ) }, qr/vae needs the entity referred to/, 'vae with nothing');
dies_like(sub { $c->db->datoms(eav => 99_999) }, qr/there is no entity 99999/, 'an id not given out');

undef $c; @held = ();
is(Fact::DB::_live_blocks(), 0, 'nothing is left allocated');

done_testing;
