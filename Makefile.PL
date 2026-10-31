use 5.010;
use strict;
use warnings;
use ExtUtils::MakeMaker;
use ExtUtils::Depends;
use File::Find ();

# DB.xs sits at the top of the dist and the build is the plain
# single-extension one, for the reason HAMT's Makefile.PL gives: an XSMULTI
# build writes its export list where Strawberry's import-library rule does
# not look.

# ---- Fact::DB as a CONSUMER of HAMT's C ABI ----------------------------- *
# Every index, the log and the list of versions are HAMT containers, reached
# through hamt_abi.h with no Perl call. ExtUtils::Depends finds that header
# where HAMT installed it; HAMT::_abi_ptr hands over the table when this
# loads. Nothing is linked against HAMT.
#
# HAMT must be installed, or in @INC, before this file can run: that is what
# CONFIGURE_REQUIRES says below, and a missing one stops here by name.
my $pkg = eval { ExtUtils::Depends->new('Fact::DB', 'HAMT') };
unless ($pkg) {
    warn "Fact::DB needs HAMT to be installed before it is configured:\n$@";
    exit 0;
}

# ---- Fact::DB as an ABI PROVIDER ---------------------------------------- *
# fact_db_abi.h publishes connections, values and queries to other
# extensions' C, resolved at run time through Fact::DB::_abi_ptr.
# ExtUtils::Depends installs the header and records where it went. NO
# set_inc, for the reason HAMT's Makefile.PL gives: a provider's build-tree
# paths mean nothing to a consumer once installed.
$pkg->install('include/fact_db_abi.h');
mkdir 'Install' unless -d 'Install';
$pkg->save_config('Install/Files.pm');

my %dep = $pkg->get_makefile_vars;
# EU::Depends hands back an empty LIBS/LDFROM that would clobber MakeMaker's
# defaults. Its INC is the one thing wanted: where hamt_abi.h is.
delete @dep{qw(LIBS LDFROM)};
my $hamt_inc = delete $dep{INC} // '';

# Its PM map REPLACES MakeMaker's auto-detected one, so everything under lib/
# has to be re-added by hand or it silently stops being installed.
File::Find::find(sub {
    return unless -f && /\.pm\z/;
    (my $rel = $File::Find::name) =~ s{^lib/}{};
    $dep{PM}{$File::Find::name} = "\$(INST_LIB)/$rel";
}, 'lib');

# MakeMaker maps DB.xs -> DB.c and rescans neither the INCLUDEd xs/
# fragments nor the C headers, so name both. The globs run at CONFIGURE
# time - re-run Makefile.PL after adding a file.
my $xs_deps = join ' ', glob('xs/*.xs');
my $h_deps  = join ' ', 'include/fact_db_abi.h', glob('include/factdb/*.h');

my %WriteMakefileArgs = (
    NAME             => 'Fact::DB',
    AUTHOR           => ['LNATION <email@lnation.org>'],
    VERSION_FROM     => 'lib/Fact/DB.pm',
    ABSTRACT_FROM    => 'lib/Fact/DB.pm',
    LICENSE          => 'artistic_2',
    MIN_PERL_VERSION => '5.010',
    CONFIGURE_REQUIRES => {
        'ExtUtils::MakeMaker' => '0',
        'ExtUtils::Depends'   => '0.404',
        'HAMT'                => '0.02',
    },
    TEST_REQUIRES => {
        'Test::More' => '0.88',
    },
    PREREQ_PM => {
        'HAMT' => '0.02',
    },
    XS     => { 'DB.xs' => 'DB.c' },
    OBJECT => 'DB$(OBJ_EXT)',
    INC    => "-I. -Iinclude $hamt_inc",
    depend => {
        'DB.c'         => $xs_deps,
        'DB$(OBJ_EXT)' => $h_deps,
    },
    macro => { TARFLAGS => "--format=ustar -c -v -f" },
    dist  => { COMPRESS => 'gzip -9f', SUFFIX => 'gz', },
    clean => { FILES => 'Fact-DB-* Install/Files.pm' },
    %dep,
);

# Compatibility with old versions of ExtUtils::MakeMaker
unless (eval { ExtUtils::MakeMaker->VERSION('6.64'); 1 }) {
    my $test_requires = delete $WriteMakefileArgs{TEST_REQUIRES} || {};
    @{$WriteMakefileArgs{PREREQ_PM}}{keys %$test_requires} = values %$test_requires;
}

unless (eval { ExtUtils::MakeMaker->VERSION('6.55_03'); 1 }) {
    my $build_requires = delete $WriteMakefileArgs{BUILD_REQUIRES} || {};
    @{$WriteMakefileArgs{PREREQ_PM}}{keys %$build_requires} = values %$build_requires;
}

delete $WriteMakefileArgs{CONFIGURE_REQUIRES}
    unless eval { ExtUtils::MakeMaker->VERSION('6.52'); 1 };
delete $WriteMakefileArgs{MIN_PERL_VERSION}
    unless eval { ExtUtils::MakeMaker->VERSION('6.48'); 1 };
delete $WriteMakefileArgs{LICENSE}
    unless eval { ExtUtils::MakeMaker->VERSION('6.31'); 1 };

WriteMakefile(%WriteMakefileArgs);
