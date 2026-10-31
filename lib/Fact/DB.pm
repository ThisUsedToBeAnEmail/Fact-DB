package Fact::DB;

use 5.010;
use strict;
use warnings;

use HAMT ();

our $VERSION = '0.01';

require XSLoader;
XSLoader::load('Fact::DB', $VERSION);

1;

__END__

=head1 NAME

Fact::DB - a database of facts that keeps every version

=head1 VERSION

Version 0.01

=head1 DESCRIPTION

Fact::DB holds facts about entities, an attribute and a value each, in
memory. Every transaction makes a new database value and leaves the old one
as it was, so any version can be read and queried, and a query runs against
a value that cannot change under it. Queries are written in Datalog, with
rules.

This version stores facts, keeps every version of them, reads them back by
entity, by index and by query, rules and aggregates included, and reads any
version as of a transaction.

=head1 SYNOPSIS

    use Fact::DB;

    my $conn = Fact::DB->new(schema => {
        'person/name'  => { type => 'string', unique => 'identity' },
        'person/email' => { type => 'string', unique => 'value' },
        'person/age'   => { type => 'int',    index  => 1 },
        'person/tags'  => { type => 'string', cardinality => 'many' },
        'person/boss'  => { type => 'ref' },
    });

    $conn->add_schema({ 'person/joined' => { type => 'instant' } });

    my $r = $conn->transact([
        { 'db/id' => 'ann', 'person/name' => 'Ann', 'person/tags' => ['a', 'b'] },
        { 'person/name' => 'Bob', 'person/boss' => 'ann' },
        [ 'db/add', 'ann', 'person/age', 61 ],
    ]);
    my $ann = $r->{tempids}{ann};

    my $now  = $conn->db;
    my $then = $r->{db_before};

    $now->entity($ann);                       # { 'db/id' => ..., 'person/name' => 'Ann', ... }
    $now->datoms(ave => 'person/name', 'Bob');

    $now->q('[:find ?n :in $ ?min :where [?e :person/age ?a] [(> ?a ?min)] [?e :person/name ?n]]', 40);

    $now->as_of($r->{tx});                    # the value just after that transaction
    $now->since($r->{tx});                    # only the facts asserted after it
    $now->history->datoms(eav => $ann);       # every assertion and retraction

=head1 METHODS

=head2 new

    my $conn = Fact::DB->new(schema => \%definitions);

A new connection, holding no facts. C<schema> is optional and is what
L</add_schema> takes.

=head2 add_schema

    $conn->add_schema(\%definitions);

Adds attributes to the schema and returns the connection. Each key is an
attribute's name and each value its definition. If any name or definition
is refused, none of them is added.

An attribute, once added, is never changed or removed. Naming one that is
already in the schema croaks, even with the same definition.

=head2 db

    my $value = $conn->db;

The current database value, a L<Fact::DB::Db>. It never changes: a
transaction makes a new one. Asked for again while it is held, it is the
same object.

=head2 transact

    my $r = $conn->transact(\@data);

Runs a transaction and makes its result the current database value. If it
croaks, nothing has changed. It returns a hash reference:

=over 4

=item tx

The transaction's id. A transaction is an entity of its own, with a
C<db/txInstant>.

=item tempids

A hash reference of each tempid named in the data to the entity id it was
given.

=item db_before, db_after

The database values before and after.

=item tx_data

Every datom the transaction made, in the order made, each an array
reference of the entity id, the attribute's name, the value, the
transaction's id, and 1 for an assertion or 0 for a retraction. The first is
the transaction's own C<db/txInstant>.

=back

=head2 versions

    my $n = $conn->versions;

How many values the connection's line holds: the empty one, and one a
transaction since.

=head2 schema

    my $schema = $conn->schema;

The schema as a hash reference of names to definitions, every attribute
included, C<db/txInstant> among them. It is a copy: changing it changes
nothing.

Each definition has C<type>, C<cardinality> and C<index>, and C<unique> when
the attribute is unique. C<index> is 1 when the attribute is indexed for any
reason: asked for, unique, or a ref.

=head1 THE SCHEMA

=head2 Names

An attribute's name is a namespace, a slash and a name: C<person/name>. It
has no spaces and none of C<[ ] ( ) { } " , ; : ' \ # `>, which the query
language uses. The C<db> namespace is reserved for the database's own
attributes.

=head2 Definitions

=over 4

=item type

Required. One of C<string>, C<int>, C<num>, C<bool>, C<ref> and C<instant>;
see L</VALUES>.

=item cardinality

C<one>, the default, or C<many>. An entity holds at most one value of a
cardinality one attribute, and any number of a cardinality many one.

=item unique

C<identity> or C<value>. No two entities hold the same value of a unique
attribute. A cardinality many attribute cannot be unique.

=item index

True to keep an index from the attribute's values to the entities holding
them. A unique attribute and a ref are always indexed.

=back

=head2 Built-in attributes

C<db/txInstant>, an instant: when a transaction was made.

=head1 TRANSACTIONS

The data is an array reference of items, each one of these:

=over 4

=item an entity as a hash reference

    { 'db/id' => 'ann', 'person/name' => 'Ann', 'person/tags' => ['a', 'b'] }

Asserts each attribute's value of one entity. C<db/id> names the entity: an
entity id, or a tempid. Without it the hash is a new entity of its own. A
cardinality many attribute may be given a list of values. The attributes are
taken in the order of their names.

=item an op as an array reference

    [ 'db/add',           $entity, $attribute, $value ]
    [ 'db/retract',       $entity, $attribute, $value ]
    [ 'db/retractEntity', $entity ]
    [ 'db/cas',           $entity, $attribute, $old, $new ]

C<db/retractEntity> retracts every fact the entity holds and every ref to it
from another entity. A transaction entity cannot be retracted.

C<db/cas> asserts C<$new> if the attribute's value is C<$old> now, and
croaks naming both if it is not. C<$old> undef expects no value. It is for a
cardinality one attribute.

=back

=head2 Entities, tempids and lookup refs

An entity id is a positive whole number the database has given out. A tempid
is any other string: it stands for a new entity, the same one wherever it
appears in the transaction, as an entity or as the value of a ref. The
transaction takes the next id, then each new entity in the order its tempid
first appears. A tempid that appears only as a value croaks: nothing says
what that entity is.

A lookup ref, C<[$attribute, $value]> of a unique attribute, is the entity
that holds that value, as the database is before the transaction. It can go
anywhere an entity id can: an op's entity, C<db/id>, a ref's value, and
C<entity> and C<datoms>. For a cardinality many ref, a lookup ref goes
inside the list of values.

A retraction, C<db/retractEntity> and C<db/cas> name their entity by id or
lookup ref, not by tempid.

=head2 Upsert

A tempid, or a hash without C<db/id>, that is given the value of a
C<< unique => 'identity' >> attribute some entity already holds is that
entity. Tempids given the same new identity value in one transaction are one
new entity. If one entity's identity values are held by two entities, the
transaction croaks. An entity named by its id is never upserted: giving it a
value another holds is a conflict like any other.

=head2 What a transaction does

Asserting a fact that is already true makes no datom. Asserting a new value
of a cardinality one attribute retracts the old one first. Retracting a fact
that is not true makes no datom. A transaction with no datoms of its own
still happens, and has its C<db/txInstant>.

Uniqueness is judged where the transaction ends: a unique value may pass from
one entity to another in one transaction, whichever comes first, and two
entities may swap values.

These croak: an attribute not in the schema; one of the C<db> namespace's;
a value its type refuses; a list for a cardinality one attribute; an entity
id the database has not given out; a lookup ref that names no one; one
transaction both asserting and retracting a fact; one transaction giving an
entity two values of a cardinality one attribute; and, at the end, a unique
value two entities hold.

=head1 VALUES

Every value is checked against its attribute's type when it is given, and
what is kept is the value as the type has it, not as it was given: an int
given as C<'042'> is kept as 42, and a number given as C<'1e3'> as 1000.
Two values are the same value when they are the same as their type has
them, so C<'0.1'> and C<0.1> are one num, and a string is the same string
however perl happens to hold it.

undef is never a value: retracting a fact is how it goes away. A reference
is a value of no type but C<ref>, and only as a ref's own forms.

=over 4

=item string

Any defined scalar, as its string.

=item int

A whole number in the range of perl's integers, given as a number or as a
string perl reads as one. C<4.5>, C<'abc'>, C<'0x10'> and infinity are
refused.

=item num

Any finite number, given as a number or as a string perl reads as one. NaN
and infinity are refused. C<-0> is kept as C<0>. A num reads back as
exactly the number given.

=item bool

Any defined scalar, by perl's truth. It reads back as 1 or 0.

=item ref

An entity id: a positive whole number.

=item instant

A point in time, as a num of seconds since the epoch.

=back

=head1 C ABI

Everything this module does from Perl can be done from another extension's
C: making a connection and adding attributes, building and running
transactions (C<with> too), reading the time-travel values, walking an
entity or an index, and parsing, running and explaining queries. A
connection and a database value are the same objects either way, so one
made in C can be handed to Perl and one from Perl used in C.

The interface is one header, F<fact_db_abi.h>, installed with the module. A
consuming distribution finds it with L<ExtUtils::Depends>:

    my $pkg = ExtUtils::Depends->new('My::Module', 'Fact::DB');

and resolves the table once, when it loads; the header shows how. Nothing
is linked against, and the table is versioned, so the two distributions can
be upgraded independently. It is at version 1.

What a method croaks on, an entry refuses: it returns a code, says what the
method would have said, and changes nothing. C<$@> is left as it was. A
query parsed in C and one parsed by L<Fact::DB::Db/q> from the same text
are one parse, held once.

=head1 THREADS

A connection belongs to the thread that made it. A new thread does not get
it.

=head1 SEE ALSO

L<HAMT>, which holds every index.

=head1 AUTHOR

LNATION <email@lnation.org>

=head1 BUGS

Please report any bugs or feature requests to C<bug-fact-db at rt.cpan.org>,
or through the web interface at
L<https://rt.cpan.org/NoAuth/ReportBug.html?Queue=Fact-DB>.

=head1 LICENSE AND COPYRIGHT

This software is Copyright (c) 2026 by LNATION <email@lnation.org>.

This is free software, licensed under:

  The Artistic License 2.0 (GPL Compatible)

=cut
