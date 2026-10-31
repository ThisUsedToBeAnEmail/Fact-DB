#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use FindBin ();
use lib "$FindBin::Bin/lib";
use Fact::DB;
use Facts qw(people);

# explain names the way every pattern reads, for the queries of the POD.

my $c = people();
my $db = $c->db;

{
    my $x = $db->explain('[:find ?n :in $ ?min :where [?e :person/age ?a] [(> ?a ?min)] [?e :person/name ?n]]');
    like($x, qr/^1\. \[\?e :person\/age \?a\]\n   reads every value in the index of :person\/age \(ave\)\n/, 'an indexed attribute alone: its index');
    unlike($x, qr/joins every row/, 'a scalar input makes no cross product');
    like($x, qr/^2\. \[\(> \?a \?min\)\]\n   keeps the rows for which > is true\n/m, 'a predicate');
    like($x, qr/^3\. \[\?e :person\/name \?n\]\n   reads the entity's record \(eav\)\n/m, 'a bound entity: its record');
}
like($db->explain('[:find ?e :where [?e :person/name "Ann"]]'), qr/looks up the value in the index of :person\/name \(ave\)/,
     'an indexed attribute and a value: a lookup');
like($db->explain('[:find ?e :where [?e :person/tags "x"]]'), qr/reads every entity's record: :person\/tags is not indexed/,
     'an unindexed attribute: every record, and why');
like($db->explain('[:find ?e :where [?e ?a ?v]]'), qr/reads every datom/, 'nothing bound: every datom');
like($db->explain('[:find ?e :where [?e :person/nope ?v]]'), qr/reads nothing: :person\/nope is not in the schema/, 'an attribute not in the schema');
like($db->explain('[:find ?e ?f :where [?e :person/name ?n] [?f :person/age ?a]]'),
     qr/2\. .*\n   .*shares no variable with the clauses before it, so joins every row with every match/,
     'a cross product said so');
like($db->explain('[:find ?n :where [?e :person/name ?n] [(str ?n "!") ?x]]'), qr/binds from str/, 'a function');
like($db->history->explain('[:find ?e :where [?e :person/name ?n]]'), qr/reads the whole log: a history value/, 'a history value');
like($db->explain('[:find ?n :in $ [?a ...] :where [?e :person/age ?a] [?e :person/name ?n]]'), qr/^1\. .*\n   looks up the value in the index of :person\/age \(ave\)/m,
     'a variable a collection input binds is a value to look up');

{
    my $c2 = people();
    $c2->transact([ map { { 'person/name' => "n$_", 'person/age' => $_ % 10, 'person/tags' => ['t'] } } 1 .. 100 ]);
    my $d = $c2->db;
    Fact::DB::_tried();
    $d->q('[:find ?e :where [?e :person/name "n5"]]');
    is(Fact::DB::_tried(), 1, 'a unique attribute and a value: the one datom tried');
    $d->q('[:find ?e :where [?e :person/age 3]]');
    is(Fact::DB::_tried(), 10, 'an indexed attribute and a value: only the entities holding it');
    $d->q('[:find ?e :where [?e :person/age _]]');
    is(Fact::DB::_tried(), 100, 'an indexed attribute alone: each of its datoms');
    $d->q('[:find ?n :where [?e :person/age 3] [?e :person/name ?n]]');
    is(Fact::DB::_tried(), 20, 'a bound entity: its own record, once a row');
    $d->q('[:find ?e :where [?e :person/tags "t"]]');
    is(Fact::DB::_tried(), 100, 'an unindexed attribute: every record read, and only its own datoms tried');
}

done_testing;
