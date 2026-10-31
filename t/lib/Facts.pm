package Facts;
use 5.010;
use strict;
use warnings;
use Exporter 'import';

our @EXPORT_OK = qw(facts index_of log_of people dies_like consistent);

sub facts {
    my ($db) = @_;
    my $eav = $db->_dump->{eav};
    my @out;
    for my $e (sort { $a <=> $b } keys %$eav) {
        for my $f (@{ $eav->{$e} }) {
            next if $f->[0] eq 'db/txInstant';
            push @out, "$e $f->[0] $f->[1]";
        }
    }
    return \@out;
}

sub index_of {
    my ($db, $attr) = @_;
    my $by = $db->_dump->{ave}{$attr} || {};
    return { map { $_ => ref $by->{$_} ? [ sort { $a <=> $b } @{ $by->{$_} } ] : $by->{$_} } keys %$by };
}

sub log_of {
    my ($db) = @_;
    return [ map {
        my ($tx, $datoms) = @$_;
        [ $tx, [ map { join ' ', @$_ } grep { $_->[1] ne 'db/txInstant' } @$datoms ] ];
    } @{ $db->_dump->{log} } ];
}

sub people {
    return Fact::DB->new(schema => {
        'person/name'  => { type => 'string', unique => 'identity' },
        'person/email' => { type => 'string', unique => 'value' },
        'person/age'   => { type => 'int', index => 1 },
        'person/tags'  => { type => 'string', cardinality => 'many' },
        'person/boss'  => { type => 'ref' },
        'person/score' => { type => 'num' },
        @_,
    });
}

sub consistent {
    my ($db, $name) = @_;
    my $bad = $db->_check;
    Test::More::is_deeply($bad, [], $name || 'every index agrees') or Test::More::diag(Test::More::explain($bad));
}

sub dies_like {
    my ($code, $re, $name) = @_;
    my $ok = eval { $code->(); 1 };
    Test::More::ok(!$ok, "$name: refused") or return;
    Test::More::like($@, $re, "$name: saying why") or Test::More::diag($@);
}

1;
