# Fact::DB::Db: a value at other times.

MODULE = Fact::DB    PACKAGE = Fact::DB

# How many values the connection's line holds: the empty one and one a
# transaction.
IV
versions(self)
        SV *self
    CODE:
        RETVAL = factdb_conn_handle(aTHX_ self, "Fact::DB::versions")->current->depth + 1;
    OUTPUT:
        RETVAL

MODULE = Fact::DB    PACKAGE = Fact::DB::Db

# The last transaction in this value.
IV
basis_tx(self)
        SV *self
    CODE:
        RETVAL = factdb_db_handle(aTHX_ self, "Fact::DB::Db::basis_tx")->basis_tx;
    OUTPUT:
        RETVAL

# The latest value at or before transaction $t on this value's line.
void
as_of(self, t)
        SV *self
        SV *t
    PREINIT:
        const char *what = "Fact::DB::Db::as_of";
        factdb_db *d, *found;
    CODE:
        d = factdb_db_handle(aTHX_ self, what);
        if (d->kind != FACTDB_KIND_VALUE)
            croak("%s: a %s value has no line of versions; as_of is for a value a transaction made",
                  what, d->kind == FACTDB_KIND_SINCE ? "since" : "history");
        found = factdb_db_as_of(d, factdb_tx_arg(aTHX_ t, what), NULL);
        ST(0) = factdb_db_sv(aTHX_ found);
        XSRETURN(1);

# The facts this value holds that were asserted after transaction $t.
void
since(self, t)
        SV *self
        SV *t
    PREINIT:
        const char *what = "Fact::DB::Db::since";
        factdb_db *d, *made;
        IV after;
        SV *obj;
    CODE:
        d = factdb_db_handle(aTHX_ self, what);
        if (d->kind != FACTDB_KIND_VALUE)
            croak("%s: since is for a value a transaction made, not a %s value",
                  what, d->kind == FACTDB_KIND_SINCE ? "since" : "history");
        after = factdb_tx_arg(aTHX_ t, what);
        if (d->since && d->since_tx == after) {
            ST(0) = factdb_db_sv(aTHX_ d->since);
            XSRETURN(1);
        }
        obj = factdb_since_build(aTHX_ d, after, what, &made);
        factdb_db_retain(made);
        if (d->since) factdb_db_release(aTHX_ d->since);
        d->since = made;
        d->since_tx = after;
        ST(0) = obj;
        XSRETURN(1);

# Every assertion and retraction on this value's line.
void
history(self)
        SV *self
    PREINIT:
        const char *what = "Fact::DB::Db::history";
        factdb_db *d;
    CODE:
        d = factdb_db_handle(aTHX_ self, what);
        if (d->kind == FACTDB_KIND_HISTORY) { ST(0) = self; XSRETURN(1); }
        if (d->kind != FACTDB_KIND_VALUE)
            croak("%s: history is for a value a transaction made, not a since value", what);
        ST(0) = factdb_history_build(aTHX_ d, what);
        XSRETURN(1);

# True for a value history made.
int
is_history(self)
        SV *self
    CODE:
        RETVAL = factdb_db_handle(aTHX_ self, "Fact::DB::Db::is_history")->kind == FACTDB_KIND_HISTORY;
    OUTPUT:
        RETVAL
