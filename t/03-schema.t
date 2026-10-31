#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use Fact::DB;

# The schema: what each attribute is. Every refusal names the attribute and
# leaves the schema as it was.

sub dies_like {
    my ($code, $re, $name) = @_;
    my $ok = eval { $code->(); 1 };
    ok(!$ok, "$name: refused") or return;
    like($@, $re, "$name: saying why") or diag $@;
}

my %BUILTIN = ('db/txInstant' => { type => 'instant', cardinality => 'one', index => 0 });

{
    my $c = Fact::DB->new;
    isa_ok($c, 'Fact::DB');
    is_deeply($c->schema, \%BUILTIN, 'a new connection knows only db/txInstant');
    is(Fact::DB::_attr_id($c, 'db/txInstant'), 0, 'which is attribute 0');
}

{
    my $c = Fact::DB->new(schema => {
        'person/name'  => { type => 'string', unique => 'identity' },
        'person/email' => { type => 'string', unique => 'value' },
        'person/age'   => { type => 'int', index => 1 },
        'person/tags'  => { type => 'string', cardinality => 'many' },
        'person/boss'  => { type => 'ref' },
        'person/score' => { type => 'num', index => 0 },
        'person/ok'    => { type => 'bool', cardinality => 'one' },
        'person/born'  => { type => 'instant' },
    });
    is_deeply($c->schema, {
        %BUILTIN,
        'person/name'  => { type => 'string',  cardinality => 'one',  index => 1, unique => 'identity' },
        'person/email' => { type => 'string',  cardinality => 'one',  index => 1, unique => 'value' },
        'person/age'   => { type => 'int',     cardinality => 'one',  index => 1 },
        'person/tags'  => { type => 'string',  cardinality => 'many', index => 0 },
        'person/boss'  => { type => 'ref',     cardinality => 'one',  index => 1 },
        'person/score' => { type => 'num',     cardinality => 'one',  index => 0 },
        'person/ok'    => { type => 'bool',    cardinality => 'one',  index => 0 },
        'person/born'  => { type => 'instant', cardinality => 'one',  index => 0 },
    }, 'every type and option, read back; unique and ref attributes are indexed');

    is_deeply([ map { Fact::DB::_attr_id($c, $_) } sort keys %{ $c->schema } ],
              [ 0, 1, 2, 3, 4, 5, 6, 7, 8 ],
              'ids follow the order of the names, after db/txInstant');
    is(Fact::DB::_attr_id($c, 'person/nobody'), -1, 'a name not in the schema has no id');

    my $copy = $c->schema;
    $copy->{'person/age'}{type} = 'string';
    delete $copy->{'person/name'};
    is($c->schema->{'person/age'}{type}, 'int', 'schema hands out a copy');
    ok($c->schema->{'person/name'}, 'and changing it changes nothing');

    is($c->add_schema({ 'pet/name' => { type => 'string' }, 'pet/age' => { type => 'int' } }), $c,
       'add_schema returns the connection');
    is(Fact::DB::_attr_id($c, 'pet/age'), 9, 'an added attribute takes the next id');
    is(Fact::DB::_attr_id($c, 'pet/name'), 10, 'in the order of the names added');
    is(Fact::DB::_attr_id($c, 'person/age'), 1, 'and the old ids stay');

    dies_like(sub { $c->add_schema({ 'person/age' => { type => 'int', index => 1 } }) },
              qr/^Fact::DB::add_schema: attribute person\/age is already in the schema/,
              'naming an attribute again, even as it was');
    dies_like(sub { $c->add_schema({ 'zoo/a' => { type => 'int' }, 'zoo/b' => { type => 'nope' } }) },
              qr/attribute zoo\/b: unknown type "nope"/, 'one bad definition among several');
    is(Fact::DB::_attr_id($c, 'zoo/a'), -1, 'adds none of them');
    is(scalar keys %{ $c->schema }, 11, 'the schema is as it was');
    $c->add_schema({});
    is(scalar keys %{ $c->schema }, 11, 'an empty schema adds nothing');
}

{
    my $a = Fact::DB->new(schema => { map { ("n/a$_" => { type => 'int' }) } 1 .. 50 });
    my $b = Fact::DB->new(schema => { map { ("n/a$_" => { type => 'int' }) } reverse 1 .. 50 });
    is_deeply([ map { Fact::DB::_attr_id($a, "n/a$_") } 1 .. 50 ],
              [ map { Fact::DB::_attr_id($b, "n/a$_") } 1 .. 50 ],
              'the same schema gives the same ids, whatever order the hash keeps');
}

{
    my $c = Fact::DB->new(schema => { "caf\x{e9}/nom" => { type => 'string' }, "\x{4eba}/\x{540d}" => { type => 'int' } });
    ok(Fact::DB::_attr_id($c, "caf\x{e9}/nom") > 0, 'a name in Latin-1 characters');
    ok(Fact::DB::_attr_id($c, "\x{4eba}/\x{540d}") > 0, 'a name in wide characters');
    my $up = "caf\x{e9}/nom";
    utf8::upgrade($up);
    is(Fact::DB::_attr_id($c, $up), Fact::DB::_attr_id($c, "caf\x{e9}/nom"), 'one attribute however its name is held');
}

my @bad_names = (
    [ 'name',        qr/a namespace, a slash and a name/ ],
    [ '/name',       qr/a namespace, a slash and a name/ ],
    [ 'person/',     qr/a namespace, a slash and a name/ ],
    [ 'a/b/c',       qr/a namespace, a slash and a name/ ],
    [ '',            qr/a namespace, a slash and a name/ ],
    [ 'per son/x',   qr/no spaces/ ],
    [ "a/b\t",       qr/no spaces/ ],
    [ ':person/name', qr/no spaces and none of/ ],
    [ 'person/na[me', qr/no spaces and none of/ ],
    [ 'person/n"x',  qr/no spaces and none of/ ],
    [ 'a,b/c',       qr/no spaces and none of/ ],
    [ "a/b\x{7f}",   qr/no spaces/ ],
    [ 'db/thing',    qr/the db\/ namespace is reserved/ ],
    [ 'db/txInstant', qr/the db\/ namespace is reserved/ ],
);
for my $b (@bad_names) {
    my ($name, $re) = @$b;
    (my $label = $name) =~ s/([^ -~])/sprintf '\\x%02x', ord $1/ge;
    dies_like(sub { Fact::DB->new(schema => { $name => { type => 'int' } }) },
              qr/^Fact::DB::new: attribute .*$re/s, "the name '$label'");
}
ok(Fact::DB->new(schema => { 'dbx/a' => { type => 'int' }, 'a/db' => { type => 'int' } }),
   'db is reserved only as a whole namespace');

my @bad_defs = (
    [ 'not a hash',      'x',                                     qr/a definition is a hash reference/ ],
    [ 'an array',        [],                                      qr/a definition is a hash reference/ ],
    [ 'an object',       bless({ type => 'int' }, 'X'),           qr/a definition is a hash reference/ ],
    [ 'no type',         {},                                      qr/a type is needed/ ],
    [ 'an undef type',   { type => undef },                       qr/a type is needed/ ],
    [ 'unknown type',    { type => 'integer' },                   qr/unknown type "integer"/ ],
    [ 'unknown key',     { type => 'int', indexed => 1 },         qr/unknown key "indexed"/ ],
    [ 'bad cardinality', { type => 'int', cardinality => 'some' }, qr/cardinality is one or many/ ],
    [ 'bad unique',      { type => 'int', unique => 1 },          qr/unique is identity or value/ ],
    [ 'unique and many', { type => 'int', unique => 'value', cardinality => 'many' }, qr/a cardinality many attribute cannot be unique/ ],
    [ 'index a ref',     { type => 'int', index => [] },          qr/index is true or false/ ],
);
for my $b (@bad_defs) {
    my ($label, $def, $re) = @$b;
    dies_like(sub { Fact::DB->new(schema => { 'a/b' => $def }) },
              qr/^Fact::DB::new: attribute a\/b: $re/, "a definition that is $label");
}

dies_like(sub { Fact::DB->new(schema => []) }, qr/a schema is a hash reference/, 'a schema that is not a hash');
dies_like(sub { Fact::DB->new('schema') }, qr/odd number of options/, 'an odd list of options');
dies_like(sub { Fact::DB->new(scheme => {}) }, qr/unknown option "scheme"/, 'an unknown option');
dies_like(sub { Fact::DB::schema('Fact::DB') }, qr/^Fact::DB::schema: not a Fact::DB connection/, 'a class name for a connection');
dies_like(sub { bless({}, 'Fact::DB')->add_schema({}) }, qr/^Fact::DB::add_schema: not a Fact::DB connection/,
          'an object of the class made by hand');
dies_like(sub { require HAMT::Map; Fact::DB::schema(HAMT::Map->new) }, qr/^Fact::DB::schema: not a Fact::DB connection/,
          "an object carrying another library's magic");

{
    package My::DB;
    our @ISA = ('Fact::DB');
}
isa_ok(My::DB->new, 'My::DB', 'a subclass');

{
    my $c = Fact::DB->new;
    my $d = $c->new;
    isa_ok($d, 'Fact::DB', 'new called on a connection makes another');
    is_deeply($d->schema, \%BUILTIN, 'with an empty schema of its own');
}

is(Fact::DB::_live_blocks(), 0, 'every connection made and refused is freed');

done_testing;
