package Fact::DB::Db;

use 5.010;
use strict;
use warnings;

use Fact::DB ();

our $VERSION = '0.01';

1;

__END__

=head1 NAME

Fact::DB::Db - one version of a Fact::DB database

=head1 VERSION

Version 0.01

=head1 DESCRIPTION

A database value: what a L<Fact::DB> connection held after one transaction.
It never changes. Reads and queries are made on it.

Every version is kept, and each transaction keeps only what it changed: the
facts it touched and the few index nodes on their paths. Everything else is
shared with the version before.

=head1 METHODS

=head2 entity

    my $e = $value->entity($id);
    # { 'db/id' => 2, 'person/name' => 'Ann', 'person/tags' => ['a', 'b'] }

An entity as a hash reference: C<db/id>, then each attribute it holds and
its value. A cardinality many attribute's values are a list, in no promised
order, even when there is one. A ref is the id of the entity it refers to.
The hash is a copy, the caller's to change.

Any id the database has given out is an entity: one with no facts is just
C<< { 'db/id' => $id } >>. An id it has not given out croaks. A lookup ref,
C<[$attribute, $value]> of a unique attribute, names the entity holding the
value, here and wherever C<datoms> takes an entity.

=head2 datoms

    my $all  = $value->datoms('eav');
    my $mine = $value->datoms(eav => $id);
    my $ages = $value->datoms(aev => 'person/age');
    my $anns = $value->datoms(ave => 'person/name', 'Ann');
    my $refs = $value->datoms(vae => $id);

The datoms of this value that match what is given, as an array reference of
array references: the entity id, the attribute's name, the value, and the
id of the transaction that asserted it. In no promised order.

The first argument names an index; after it come up to three of its
components, in its order, the rest left open:

=over 4

=item eav

entity, attribute, value. With nothing after it, every datom.

=item aev

attribute, entity, value. With nothing after it, every datom.

=item ave

attribute, value, entity. Only for an indexed attribute: one asked to be,
a unique one, or a ref.

=item vae

the entity referred to, the ref attribute, the entity referring: every ref
to an entity, through any ref attribute or one.

=back

Every attribute, indexed or not, can be read through C<aev>. For one that is
not indexed, that reads every entity.

=head2 count

    my $n = $value->count;

How many datoms the value holds, each transaction's own C<db/txInstant>
among them.

=head2 basis_tx

    my $tx = $value->basis_tx;

The id of the last transaction in this value; 0 for the empty one.

=head2 as_of

    my $then = $value->as_of($tx);

The value as it was after transaction C<$tx>: the latest on this value's
line of versions at or before it. Any whole number, 0 or more, will do; one
between two transactions gives the value of the earlier, one after this
value's own transaction gives this value. Asked again while it is held, it
is the same object.

The line is the values this one was made from, back to the empty one, so
C<as_of> works on any value: an old one looks further back, a C<with> value
looks back through the value it was made from, and none of them needs the
connection, which may be gone. Finding the value costs a number of steps
that grows with the logarithm of the number of versions.

=head2 since

    my $new = $value->since($tx);

A value holding only the facts this value holds that were asserted after
transaction C<$tx>: the ones still true, each from its latest assertion. It
can be read as any value is. It costs the number of datoms since C<$tx>, and
is kept: asked again for the same transaction, it is the same value.

=head2 history

    my $all = $value->history;

A value holding every assertion and retraction on this value's line, read by
C<datoms>, whose rows have a fifth element: 1 for an assertion, 0 for a
retraction. They come in the order they were made. Every C<datoms> on it
reads the whole log. C<entity> on it croaks: it holds datoms, not entities.

=head2 is_history

True for a value C<history> made.

=head2 q

    my $rows = $value->q($query, @inputs);
    my $rows = $value->q($query, @inputs, { functions => \%subs });

Runs a query, written in Datalog (see L</QUERIES>), against this value, and
returns what its C<:find> asks for. The first C<$> of the query's C<:in> is
this value; each input after it takes the next argument, in order. A hash
reference last is options; its one option, C<functions>, maps names to subs
the query may call (see L</Functions>).

A query's text is parsed once and kept, so running one query many times
costs its parse once.

=head2 explain

    print $value->explain($query);

How C<q> would run a query against this value, as text: for each clause in
turn, what it reads or does. A pattern says which way it reads, by the
entity's record, by an index, or through every record, and says when it
shares no variable with the clauses before it, which makes it join every
row so far with every match.

=head2 with

    my $r = $value->with(\@data);

A transaction run against this value and kept by nothing: the connection
does not move. It takes the same data as L<Fact::DB/transact> and returns
the same hash reference, with this value as C<db_before>. It can be run
against any value, an old one or one another C<with> made, and numbers its
entities on from that value.

=head1 QUERIES

    my $names = $value->q(q{
        [:find ?n
         :in $ ?min
         :where [?e :person/age ?a]
                [(> ?a ?min)]
                [?e :person/name ?n]]
    }, 40);

A query is Datalog, as Datomic writes it: a vector of C<:find>, then
optionally C<:with> and C<:in>, then C<:where>. A map of the same keys, each
to a vector, is the same query.

=head2 Find

=over 4

=item C<?a ?b>

A relation: an array reference of array references, one a match, with no
two the same.

=item C<?x .>

One value: the first match's, or undef with none.

=item C<[?x ...]>

A list: an array reference of the values, with no two the same.

=item C<[?a ?b]>

One tuple: an array reference, or undef with none.

=back

=head2 Aggregates

    $value->q('[:find ?city (avg ?age) :where [?e :person/city ?city] [?e :person/age ?age]]');

An element of C<:find> may be an aggregate: C<(count ?x)>,
C<(count-distinct ?x)>, C<(sum ?x)>, C<(avg ?x)>, C<(min ?x)>, C<(max ?x)>,
C<(median ?x)>, C<(distinct ?x)> (a list of its values), and C<(min n ?x)>
and C<(max n ?x)> (lists of the n least and the n greatest). The other
elements of C<:find> group the matches: an aggregate is worked out over each
group, and each group is one result.

What an aggregate is worked out over is a set: of the variables the result
names, the C<:find> elements and the aggregated variables, and those named
in C<:with>. So two people aged 30 are one 30 unless each person is named:

    $value->q('[:find (sum ?a) . :where [?e :person/age ?a]]');           # 30
    $value->q('[:find (sum ?a) . :with ?e :where [?e :person/age ?a]]');  # 60

C<sum>, C<avg> and C<median> take numbers; C<min> and C<max> compare as
predicates do, and croak at values of two kinds. Over nothing, C<count> and
C<sum> are 0 and the rest undef; with nothing to group by, that is still one
result.

=head2 Inputs

C<$> is the value C<q> is called on; C<$name> is another value, given as an
argument, read by patterns that name it first, C<[$old ?e :person/age ?a]>,
which is how one query compares two versions. C<?x> is a scalar,
C<[?x ...]> each value of an array reference, C<[?a ?b]> one array
reference's values, and C<[[?a ?b]]> each array reference of an array
reference. C<_> in a tuple or a relation is a value not bound.

=head2 Clauses

=over 4

=item C<[e a v tx added]>

A pattern: from one to five places, each a variable, C<_> for anything, or a
constant. The attribute's place is a keyword, a variable or C<_>. The
entity's place may be a lookup ref, C<[:person/name "Ann"]>. C<tx> binds
the transaction that asserted the fact; C<added> means something only on a
L</history> value, where it is 1 for an assertion and 0 for a retraction.

=item C<[(pred args...)]>

A predicate: keeps the rows for which it is true.

=item C<[(fn args...) binding]>

A function: binds what it returns, as C<?x>, C<[?x ...]> (a list),
C<[?a ?b]> (a tuple) or C<[[?a ?b]]> (a relation). A row for which it
returns undef is dropped.

=item C<(not clause...)>, C<(not-join [?v ...] clause...)>

Keeps a row when its clauses find nothing for it. Every variable in a
C<not> must be bound before it; a C<not-join> names the ones that join,
and the rest are its own.

=item C<(or branch...)>, C<(or-join [?v ...] branch...)>

The rows any branch makes. A branch is one clause, or C<(and clause...)>.
In an C<or>, every branch binds the same variables; an C<or-join> names the
ones that join, and the rest stay in the branch.

=item C<(rule args...)>

A call of a rule, given in C<%> (see L</Rules>).

=back

Clauses run in the order they are written, each joining with the rows the
clauses before it made, so that order decides the cost: put a clause that
names few entities first. A variable must be bound by an earlier clause
before a predicate or function uses it, and a query that does otherwise is
refused before it runs, saying where.

=head2 Rules

    my $rules = q{
        [[(anc ?a ?b) [?b :person/parent ?a]]
         [(anc ?a ?b) [?b :person/parent ?c] (anc ?a ?c)]]
    };
    $value->q('[:find ?n :in $ % ?b :where (anc ?a ?b) [?a :person/name ?n]]', $rules, $bob);

Rules are given as an input, C<%>, a vector of rules, each
C<[(name ?a ?b) clause...]>. Several with one name are alternatives: a call
matches if any does. A rule may call itself, and others, and they it. Every
variable of a rule's head must be bound by its body.

A call is worked out once for each set of arguments it has bound, and only
as far as those reach: the ancestors of one person are found without
finding everyone's. A cycle in the data ends: answers stop growing.

These are refused: a rule that depends on its own negation, through any
number of others (there is no order to work them out in); a call of a rule
not given, or with the wrong number of arguments; recursion more than 1,000
calls deep; a rule that does not settle in 1,000 rounds or makes more than
a million answers, which is one that grows without end, as through a
function.

=head2 Values

Numbers compare as numbers whatever their type: an int, a num, an
instant, an entity id. Strings compare as strings. A number and a string are
never equal and never ordered: C<< (< 1 "a") >> and C<< (> 1 "a") >> are
both false.

=head2 Built in

C<< = != < <= > >= >>, which take two or more; C<+ - * />; C<quot rem mod>
of whole numbers; C<inc dec>; C<str>, which joins its arguments as strings;
C<(subs s from to)>, by characters, C<to> not included and optional;
C<count> of a string's characters; C<ground>, which is its argument;
C<(missing? $ ?e :attr)>, true when the entity has no value of the
attribute; C<(get-else $ ?e :attr default)>, its value or the default.

=head2 Functions

    $value->q('[:find ?n :where [_ :person/name ?n] [(my/short ?n)]]',
              { functions => { 'my/short' => sub { length $_[0] < 4 } } });

A function given in C<functions> is called with the values of its
arguments, in scalar context. As a predicate, its truth keeps the row; as a
function, its value is bound, an array reference for a list, a tuple or a
relation. It may run queries of its own. A die in it is the query's, as it
was; C<last> and C<next> cannot leave it, and are errors. A function given
under a built-in's name is used in its place.

=head1 AUTHOR

LNATION <email@lnation.org>

=head1 LICENSE AND COPYRIGHT

This software is Copyright (c) 2026 by LNATION <email@lnation.org>.

This is free software, licensed under:

  The Artistic License 2.0 (GPL Compatible)

=cut
