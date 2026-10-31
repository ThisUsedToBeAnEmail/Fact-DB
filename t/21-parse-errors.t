#!perl
use 5.010;
use strict;
use warnings;
use Test::More;
use Fact::DB;

# Every refusal, by its message and the line and column it names.

sub refused {
    my ($text, $re, $name, $rules) = @_;
    my $ok = eval { Fact::DB::_parse($text, $rules); 1 };
    ok(!$ok, "$name: refused") or return;
    like($@, qr/^Fact::DB::_parse(?: rules)?: (?:line \d+, column \d+: )?$re/, "$name: saying where and why") or diag $@;
}

my $q = '[:find ?n :where [?e :a ?n]]';

refused('', qr/line 1, column 1: the text is empty/, 'nothing');
refused('   ', qr/line 1, column 1: the text is empty/, 'only spaces');
refused('[:find ?n :where [?e :a ?n]', qr/line 1, column 1: this \[ is never closed by a \]/, 'an unclosed vector');
refused('[:find ?n :where (not [?e :a ?n]]', qr/line 1, column 33: a \] here, where the \( at line 1, column 18 wants a \)/, 'the wrong closer');
refused('[:find ?n :where [?e :a ?n]]]', qr/line 1, column 29: more after the end of the query/, 'one closer too many');
refused(']', qr/line 1, column 1: a \] with nothing open for it to close/, 'a closer first');
refused('[:find ?n :where [?e :a "abc]]', qr/line 1, column 25: this string is never closed by a "/, 'an unclosed string');
refused('[:find ?n :where [?e :a "a\q"]]', qr/line 1, column 27: unknown escape \\q in a string/, 'an unknown escape');
my $bs = chr 92;
refused(qq{[:find ?n :where [?e :a "${bs}u12"]]}, qr/line 1, column 26: \\u needs four hex digits/, 'a short \\u');
refused(qq{[:find ?n :where [?e :a "${bs}uD83D"]]}, qr/line 1, column 26: \\uD83D is half a surrogate pair, with no second half/, 'half a pair');
refused(qq{[:find ?n :where [?e :a "${bs}uDE00"]]}, qr/line 1, column 26: \\uDE00 is the second half of a surrogate pair, with no first/, 'the other half');
refused('[:find ?n :where [?e :a 99999999999999999999]]', qr/line 1, column 25: 99999999999999999999 is outside the range/, 'a number out of range');
refused('[:find ?n :where [?e :a 1e99999]]', qr/1e99999 is too large a number/, 'a number too large, even for long doubles');
refused('[:find ?n :where [?e :a 1.]]', qr/a number needs digits after its point/, 'a point with nothing after');
refused('[:find ?n :where [?e :a 3x]]', qr/3x is not a number/, 'digits and letters');
refused('[:find ?n :where [?e :a @x]]', qr/line 1, column 25: unexpected '\@'/, 'a character no form begins with');
refused('[:find ?n :where [?e : ?n]]', qr/a keyword needs a name after the colon/, 'a bare colon');
refused('{:find [?n] :where}', qr/a \{ \} holds pairs, and this one has a key with no value/, 'a map with an odd count');
refused(('[' x 257) . (']' x 257), qr/line 1, column 257: nested more than 256 deep/, 'nested too deep');
refused("[:find ?n\n :where\n [?e :a \"abc]]", qr/line 3, column 9: this string is never closed/, 'across lines');
refused(qq{[:find ?n :where [?e :a "\x{263a}\x{263a}" @]]}, qr/line 1, column 30: unexpected '\@'/, 'columns in characters');

refused('42', qr/a query is a vector or a map, beginning :find; this is a number/, 'not a query');
refused('[?n :find]', qr/a query begins :find/, 'not beginning with a keyword');
refused('[:where [?e :a ?n]]', qr/a query needs :find/, 'no :find');
refused('[:find ?n]', qr/a query needs :where/, 'no :where');
refused('[:find ?n :where]', qr/line 1, column 11: :where needs at least one clause/, 'an empty :where');
refused('[:find :where [?e :a ?n]]', qr/:find needs at least one variable or aggregate/, 'an empty :find');
refused('[:find ?n :select ?n :where [?e :a ?n]]', qr/unknown section :select/, 'an unknown section');
refused('[:find ?n :find ?e :where [?e :a ?n]]', qr/:find is given twice/, 'a section twice');
refused('{:find ?n :where [[?e :a ?n]]}', qr/in a query map, :find takes a vector/, 'a map section not a vector');
refused('[:find "x" :where [?e :a ?n]]', qr/:find takes variables and aggregates, not a string/, 'a string in :find');
refused('[:find (total ?n) :where [?e :a ?n]]', qr/unknown aggregate total/, 'an unknown aggregate');
refused('[:find (count ?n ?e) :where [?e :a ?n]]', qr/count takes one variable/, 'an aggregate given two');
refused('[:find (min x ?n) :where [?e :a ?n]]', qr/\(min n \?x\) takes a whole number n/, 'min with no count');
refused('[:find (min 0 ?n) :where [?e :a ?n]]', qr/\(min n \?x\) takes n of 1 or more/, 'min of none');
refused('[:find ?n ... :where [?e :a ?n]]', qr/\.\.\. goes after the one element it qualifies/, '... in the wrong place');
refused('[:find ?x :where [?e :a ?n]]', qr/line 1, column 8: \?x in :find is bound by no clause/, 'a find variable nothing binds');
refused('[:find ?n :with ?z :where [?e :a ?n]]', qr/\?z in :with is bound by no clause/, 'a :with variable nothing binds');
refused('[:find (count ?z) :where [?e :a ?n]]', qr/\?z in :find is bound by no clause/, 'an aggregate of nothing bound');
refused('[:find ?n :in ?x ?x :where [?e :a ?n]]', qr/\?x is bound by two inputs/, 'an input variable twice');
refused('[:find ?n :in $ $ :where [?e :a ?n]]', qr/\$ is named twice in :in/, 'a source twice');
refused('[:find ?n :in % % :where [?e :a ?n]]', qr/% is named twice in :in/, 'rules twice');
refused('[:find ?n :in 3 :where [?e :a ?n]]', qr/an input is \$, \$name, %, \?x/, 'a number as an input');
refused('[:find ?n :in ?x :where [?e :a ?n]]', qr/a pattern reads a source, and :in names no \$/, 'no $ for a pattern');
refused('[:find ?n :where [$old ?e :a ?n]]', qr/\$old is not a source named in :in/, 'an unknown source');

refused('[:find ?n :where []]', qr/an empty clause/, 'an empty clause');
refused('[:find ?n :where [?e :a ?n 1 true 2]]', qr/a pattern has at most five places/, 'six places');
refused('[:find ?n :where [?e "a" ?n]]', qr/a pattern's second place is an attribute: a keyword, a variable or _, not a string/, 'a string attribute');
refused('[:find ?n :where [?e :a :b]]', qr/a keyword names an attribute, and goes in a pattern's second place/, 'a keyword value');
refused('[:find ?n :where [?e :a nil]]', qr/nil is not a value/, 'nil');
refused('[:find ?n :where [?e :a x]]', qr/x is not a variable \(they begin with \?\), _ or a constant/, 'a bare symbol');
refused('[:find ?n :where [[:a] :b ?n]]', qr/a lookup ref is \[:attribute value\]/, 'a short lookup ref');
refused('[:find ?n :where [?e :a ?n] [(> ?n 1) ?x ?y]]', qr/a predicate is \[\(name args\.\.\.\)\] and a function/, 'a predicate with too much');
refused('[:find ?n :where [?e :a ?n] [(1 ?n)]]', qr/a predicate begins with its name/, 'a predicate with no name');
refused('[:find ?n :where [?e :a ?n] [(> ?n [1])]]', qr/an argument is a variable or a constant, not a vector/, 'a vector argument');
refused('[:find ?n :where [?e :a ?n] [(> ?n _)]]', qr/_ stands for any value in a pattern, not for an argument/, '_ as an argument');
refused('[:find ?n :where [?e :a ?n] [(f ?n) "x"]]', qr/a function binds \?x, \[\?x \.\.\.\], \[\?a \?b\] or \[\[\?a \?b\]\], not a string/, 'a bad binding');
refused('[:find ?n :where [?e :a ?n] [(f ?n) []]]', qr/a function's binding needs at least one variable/, 'an empty binding');
refused('[:find ?n :where "x"]', qr/a clause is a pattern \[e a v\]/, 'a string clause');
refused('[:find ?n :where ()]', qr/a list clause begins with not, not-join, or, or-join or a rule's name/, 'an empty list');
refused('[:find ?n :where (?r ?n)]', qr/a rule's name is a plain symbol, not \?r/, 'a variable as a rule name');
refused('[:find ?n :where [?e :a ?n] (and [?e :b 1])]', qr/and is a branch of or, and stands nowhere else/, 'and alone');
refused('[:find ?n :where [?e :a ?n] (not)]', qr/not needs at least one clause/, 'an empty not');
refused('[:find ?n :where [?e :a ?n] (not-join ?e [?e :b 1])]', qr/not-join names the variables that join in a vector/, 'not-join without a vector');
refused('[:find ?n :where [?e :a ?n] (or)]', qr/or needs at least one branch/, 'an empty or');
refused('[:find ?n :where [?e :a ?n] (or-join [1] [?e :b 1])]', qr/or-join joins on variables, and a number is not one/, 'or-join on a constant');

refused('[:find ?n :where [(> ?a 1)] [?e :a ?a] [?e :b ?n]]', qr/line 1, column 22: \?a in this predicate is bound by no clause before it/,
        'a predicate before what binds its variable');
refused('[:find ?n :where [(str ?a) ?n] [?e :a ?a]]', qr/\?a in this function is bound by no clause before it/, 'a function likewise');
refused('[:find ?e :where (not [?e :a ?v]) [?e :b ?v]]', qr/\?e in this not is bound by no clause before it; not-join names/,
        'not over variables nothing has bound');
refused('[:find ?e :where (not-join [?e] [?e :a 1]) [?e :b 1]]', qr/\?e in this not-join is bound by no clause before it/,
        'not-join likewise');
refused('[:find ?e :where [?e :n 1] (or [?e :a ?x] [?e :b ?y])]', qr/the branches of this or bind different variables: \?x in the first and not in this one/,
        'or branches binding different variables');
refused('[:find ?e :where [?e :n 1] (or-join [?e ?z] [?e :a ?z] [?e :b 1])]', qr/\?z of this or-join is bound by no clause of this branch/,
        'or-join with a branch that does not bind a join variable');
refused('[:find ?n :where [?e :a ?n] (anc ?n)]', qr/\(anc \.\.\.\) calls a rule, and :in names no % to give the rules/, 'a rule call with no rules');
refused('[:find ?n :in $ % :where [?e :a ?n] (anc ?n)]', qr/there is no rule anc/, 'an unknown rule', '[[(other ?x) [?x :a 1]]]');
refused('[:find ?n :in $ % :where [?e :a ?n] (anc ?n)]', qr/rule anc takes 2 arguments, not 1/, 'a rule called with too few',
        '[[(anc ?a ?b) [?b :p ?a]]]');

refused($q, qr/rules are a vector of rules/, 'rules not a vector', '42');
refused($q, qr/a rule is \[\(name \?a \.\.\.\) clause \.\.\.\], with at least one clause/, 'a rule with no body', '[[(r ?a)]]');
refused($q, qr/a rule's head begins with its name/, 'a head with no name', '[[(?r ?a) [?a :b 1]]]');
refused($q, qr/a rule's head names variables, and a number is not one/, 'a constant in a head', '[[(r 1) [?a :b 1]]]');
refused($q, qr/rule r is defined with 2 arguments here and 1 before/, 'two arities', '[[(r ?a) [?a :b 1]] [(r ?a ?b) [?a :b ?b]]]');
refused($q, qr/rule r: \?b in its head is bound by no clause of its body/, 'a head variable its body does not bind', '[[(r ?a ?b) [?a :b 1]]]');
refused($q, qr/there is no rule q/, 'a rule calling an unknown rule', '[[(r ?a) [?a :b 1] (q ?a)]]');

done_testing;
