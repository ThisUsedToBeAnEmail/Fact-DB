use 5.010;
use strict;
use warnings;
use ExtUtils::MakeMaker;
use ExtUtils::Depends;

# A consumer of Fact::DB's C ABI, as a third party would write one: it finds
# the header through ExtUtils::Depends and nothing else. Built and run by
# xt/abi-consumer.t; it is not installed and not shipped as a module.
#
# With FACT_DB_CONSUMER_NO_HAMT set, every include directory of HAMT's is
# dropped, to show fact_db_abi.h needs nothing of HAMT's.
my $pkg = ExtUtils::Depends->new('Fact::DB::ABI::Consumer', 'Fact::DB');
my %vars = $pkg->get_makefile_vars;
if ($ENV{FACT_DB_CONSUMER_NO_HAMT}) {
    my @kept = grep { !m{HAMT} } split ' ', $vars{INC} // '';
    $vars{INC} = join ' ', @kept;
    print "INC: $vars{INC}\n";
}

WriteMakefile(
    NAME         => 'Fact::DB::ABI::Consumer',
    VERSION_FROM => 'lib/Fact/DB/ABI/Consumer.pm',
    XS           => { 'Consumer.xs' => 'Consumer.c' },
    OBJECT       => 'Consumer$(OBJ_EXT)',
    %vars,
);
