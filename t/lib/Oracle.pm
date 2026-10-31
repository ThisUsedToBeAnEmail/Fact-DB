package Oracle;
use 5.010;
use strict;
use warnings;
use Scalar::Util qw(looks_like_number);
use Exporter 'import';

our @EXPORT_OK = qw(evaluate render render_rules);

sub _num { defined $_[0] && !ref $_[0] && looks_like_number($_[0]) }

sub same {
    my ($x, $y) = @_;
    return $x == $y if _num($x) && _num($y);
    return $x eq $y if !_num($x) && !_num($y);
    return 0;
}

sub cmp3 {
    my ($x, $y) = @_;
    return $x <=> $y if _num($x) && _num($y);
    return $x cmp $y if !_num($x) && !_num($y);
    return undef;
}

sub render_term {
    my ($t) = @_;
    return $t->[1] if $t->[0] eq 'var';
    return '_' if $t->[0] eq '_';
    return ":$t->[1]" if $t->[0] eq 'kw';
    return $t->[1] if $t->[0] eq 'int';
    return '"' . $t->[1] . '"';
}

sub render_clause {
    my ($c) = @_;
    return '[' . join(' ', map { render_term($_) } @{ $c->[1] }) . ']' if $c->[0] eq 'pattern';
    return "($c->[1] " . join(' ', map { render_term($_) } @{ $c->[2] }) . ')' if $c->[0] eq 'rule';
    return '(not ' . join(' ', map { render_clause($_) } @{ $c->[1] }) . ')' if $c->[0] eq 'not';
    return "[($c->[1] " . join(' ', map { render_term($_) } @{ $c->[2] }) . ')]';
}

sub render {
    my ($q, $with_rules) = @_;
    my $in = $with_rules ? ' :in $ %' : '';
    return '[:find ' . join(' ', @{ $q->{find} }) . $in . ' :where ' . join(' ', map { render_clause($_) } @{ $q->{where} }) . ']';
}

sub render_rules {
    my ($rules) = @_;
    my @out;
    for my $name (sort keys %$rules) {
        for my $alt (@{ $rules->{$name} }) {
            push @out, "[($name " . join(' ', @{ $alt->[0] }) . ') ' . join(' ', map { render_clause($_) } @{ $alt->[1] }) . ']';
        }
    }
    return '[' . join(' ', @out) . ']';
}

sub term_value {
    my ($t, $row) = @_;
    return $row->{ $t->[1] } if $t->[0] eq 'var';
    return $t->[1];
}

sub _key { join "\0", map { _num($_) ? sprintf('%.17g', $_) : "s$_" } @_ }

# Rules worked out bottom up, naively: every body run against the relations
# so far, round after round, until a round adds nothing. $rules is
# { name => [ [ [params], [clauses] ], ... ] }.
sub relations {
    my ($rules, $datoms, $cap) = @_;
    my %rel = map { $_ => {} } keys %$rules;
    for (1 .. 1000) {
        my $grew = 0;
        for my $name (sort keys %$rules) {
            for my $alt (@{ $rules->{$name} }) {
                my ($params, $body) = @$alt;
                my $rows = _rows($body, $datoms, \%rel, $cap) or return undef;
                for my $row (@$rows) {
                    my @t = map { $row->{$_} } @$params;
                    my $k = _key(@t);
                    next if $rel{$name}{$k};
                    $rel{$name}{$k} = \@t;
                    $grew++;
                }
            }
        }
        return \%rel unless $grew;
    }
    return undef;
}

# The query's result as a sorted list of tuples, or undef when the rows
# would pass $cap.
sub evaluate {
    my ($q, $datoms, $cap, $rules) = @_;
    my $rel = $rules ? relations($rules, $datoms, $cap) : {};
    return undef unless $rel;
    my $rows = _rows($q->{where}, $datoms, $rel, $cap) or return undef;
    my %seen;
    my @out;
    for my $row (@$rows) {
        my @t = map { $row->{$_} } @{ $q->{find} };
        push @out, \@t unless $seen{ _key(@t) }++;
    }
    return [ sort { join("\0", @$a) cmp join("\0", @$b) } @out ];
}

sub _rows {
    my ($where, $datoms, $rel, $cap) = @_;
    my (%by_e, %by_a);
    for my $d (@$datoms) { push @{ $by_e{ $d->[0] } }, $d; push @{ $by_a{ $d->[1] } }, $d }
    my @rows = ({});
    for my $c (@$where) {
        my @next;
        if ($c->[0] eq 'rule') {
            my @t = @{ $c->[2] };
            for my $row (@rows) {
                T: for my $tuple (values %{ $rel->{ $c->[1] } || {} }) {
                    my %new = %$row;
                    for my $i (0 .. $#t) {
                        my $t = $t[$i];
                        if ($t->[0] eq 'var') {
                            if (exists $new{ $t->[1] }) { next T unless same($new{ $t->[1] }, $tuple->[$i]) }
                            else { $new{ $t->[1] } = $tuple->[$i] }
                        }
                        else { next T unless same($t->[1], $tuple->[$i]) }
                    }
                    push @next, \%new;
                    return undef if @next > $cap;
                }
            }
            @rows = @next;
            next;
        }
        if ($c->[0] eq 'not') {
            @rows = grep { my $sub = _rows([ ['bind', $_], @{ $c->[1] } ], $datoms, $rel, $cap); $sub && !@$sub } @rows;
            next;
        }
        if ($c->[0] eq 'bind') { @rows = ({ %{ $c->[1] } }); next }
        if ($c->[0] eq 'pattern') {
            my @t = @{ $c->[1] };
            for my $row (@rows) {
                my $cands = $datoms;
                my ($te, $ta) = @t;
                my $ev = $te->[0] eq 'var' ? $row->{ $te->[1] } : $te->[0] eq '_' ? undef : $te->[1];
                my $av = $ta ? ($ta->[0] eq 'var' ? $row->{ $ta->[1] } : $ta->[0] eq '_' ? undef : $ta->[1]) : undef;
                if (defined $ev && _num($ev)) { $cands = $by_e{ 0 + $ev } || [] }
                elsif (defined $ev) { $cands = [] }
                elsif (defined $av) { $cands = $by_a{$av} || [] }
                D: for my $d (@$cands) {
                    my %new = %$row;
                    for my $i (0 .. $#t) {
                        my $t = $t[$i];
                        next if $t->[0] eq '_';
                        my $val = $d->[$i];
                        if ($t->[0] eq 'var') {
                            if (exists $new{ $t->[1] }) { next D unless same($new{ $t->[1] }, $val) }
                            else { $new{ $t->[1] } = $val }
                        }
                        else { next D unless same($t->[1], $val) }
                    }
                    push @next, \%new;
                    return undef if @next > $cap;
                }
            }
        }
        else {
            my ($name, $args) = @$c[1, 2];
            for my $row (@rows) {
                my ($x, $y) = map { term_value($_, $row) } @$args;
                my $c3 = cmp3($x, $y);
                my $ok = $name eq '=' ? (defined $c3 && $c3 == 0)
                       : $name eq '!=' ? !(defined $c3 && $c3 == 0)
                       : !defined $c3 ? 0
                       : $name eq '<' ? $c3 < 0 : $name eq '>' ? $c3 > 0
                       : $name eq '<=' ? $c3 <= 0 : $c3 >= 0;
                push @next, $row if $ok;
            }
        }
        @rows = @next;
    }
    return \@rows;
}

1;
