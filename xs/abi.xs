# The C ABI's Perl-visible ends: the table's address, its version, the
# selftest that walks it, and the frame its entries run their bodies in.

MODULE = Fact::DB    PACKAGE = Fact::DB

# Unsigned, not IV: where the loader maps this object decides the sign bit.
UV
_abi_ptr()
    CODE:
        RETVAL = PTR2UV(&FACTDB_ABI);
    OUTPUT:
        RETVAL

IV
_abi_version()
    CODE:
        RETVAL = FACTDB_ABI.abi_version;
    OUTPUT:
        RETVAL

# The number of the check that failed, or 0, and then the connection it built.
void
_abi_selftest()
    PREINIT:
        SV *conn = NULL;
        int step;
    PPCODE:
        step = factdb_abi_selftest(aTHX_ &conn);
        EXTEND(SP, 2);
        PUSHs(sv_2mortal(newSViv(step)));
        PUSHs(conn ? sv_2mortal(conn) : &PL_sv_undef);

# What an entry's body runs in: called by the entry under G_EVAL, never from
# Perl, where there is nothing pending and it does nothing.
void
_abi_call()
    PREINIT:
        dMY_CXT;
    CODE:
        if (MY_CXT.pending) {
            void (*fn)(pTHX_ void *) = MY_CXT.pending;
            void *ud = MY_CXT.pending_ud;
            MY_CXT.pending = NULL;
            fn(aTHX_ ud);
        }
        XSRETURN_EMPTY;
