# Fact::DB::Db: queries.

MODULE = Fact::DB    PACKAGE = Fact::DB

# A new interpreter (a thread) gets a cache of its own.
void
CLONE(...)
    CODE:
        MY_CXT_CLONE;
        factdb_cache_init(aTHX);
        factdb_abi_init(aTHX);

MODULE = Fact::DB    PACKAGE = Fact::DB::Db

# q($text, @inputs, \%options). The first $ of :in is this value; each
# input after it takes the next argument; a hash reference last is options.
void
q(self, text, ...)
        SV *self
        SV *text
    PREINIT:
        const char *what = "Fact::DB::Db::q";
        factdb_query *q;
        SV *owner;
        HV *functions = NULL;
        I32 nargs = items;
    CODE:
        if (nargs > 2 && SvROK(ST(nargs - 1)) && SvTYPE(SvRV(ST(nargs - 1))) == SVt_PVHV && !SvOBJECT(SvRV(ST(nargs - 1)))) {
            functions = factdb_query_opts(aTHX_ ST(nargs - 1), what);
            nargs--;
        }
        q = factdb_cached_query(aTHX_ text, what, &owner);
        PERL_UNUSED_VAR(owner);
        ST(0) = factdb_query_run(aTHX_ self, q, &ST(2), nargs - 2, functions, what);
        XSRETURN(1);

# The plan of a query against this value, as text.
SV *
explain(self, text)
        SV *self
        SV *text
    PREINIT:
        const char *what = "Fact::DB::Db::explain";
        factdb_query *q;
        SV *owner;
    CODE:
        q = factdb_cached_query(aTHX_ text, what, &owner);
        PERL_UNUSED_VAR(owner);
        RETVAL = newSVsv(factdb_query_explain(aTHX_ factdb_db_handle(aTHX_ self, what), q, what));
    OUTPUT:
        RETVAL

MODULE = Fact::DB    PACKAGE = Fact::DB

# How the query cache stands: held, hits, misses.
void
_cache_stats()
    PPCODE:
    {
        dMY_CXT;
        EXTEND(SP, 3);
        PUSHs(sv_2mortal(newSViv(MY_CXT.cache ? (IV)HvUSEDKEYS(MY_CXT.cache) : 0)));
        PUSHs(sv_2mortal(newSViv(MY_CXT.hits)));
        PUSHs(sv_2mortal(newSViv(MY_CXT.misses)));
    }

# How many datoms patterns have tried, since the last call.
IV
_tried()
    CODE:
        RETVAL = factdb_tried;
        factdb_tried = 0;
    OUTPUT:
        RETVAL
