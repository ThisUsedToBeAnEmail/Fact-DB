# Fact::DB: database values, and the transactions that make them.

MODULE = Fact::DB    PACKAGE = Fact::DB

# The current database value. The same object each time while it lives.
void
db(self)
        SV *self
    CODE:
        ST(0) = factdb_db_sv(aTHX_ factdb_conn_current(factdb_conn_handle(aTHX_ self, "Fact::DB::db")));
        XSRETURN(1);

# Runs a transaction against the current value and makes the result the
# connection's current value. On a croak the connection is as it was.
void
transact(self, data)
        SV *self
        SV *data
    PREINIT:
        factdb_conn *c;
        factdb_db *made;
        HV *r;
    CODE:
        c = factdb_conn_handle(aTHX_ self, "Fact::DB::transact");
        r = factdb_tx_run(aTHX_ factdb_conn_current(c), data, "Fact::DB::transact", &made);
        factdb_db_retain(made);
        factdb_conn_push(aTHX_ c, made);
        ST(0) = sv_2mortal(newRV_inc((SV *)r));
        XSRETURN(1);

MODULE = Fact::DB    PACKAGE = Fact::DB::Db

# A transaction against this value, kept by nothing.
void
with(self, data)
        SV *self
        SV *data
    PREINIT:
        factdb_db *made;
        HV *r;
    CODE:
        {
            factdb_db *d = factdb_db_handle(aTHX_ self, "Fact::DB::Db::with");
            if (d->kind != FACTDB_KIND_VALUE)
                croak("Fact::DB::Db::with: with is for a value a transaction made, not a %s value",
                      d->kind == FACTDB_KIND_SINCE ? "since" : "history");
            r = factdb_tx_run(aTHX_ d, data, "Fact::DB::Db::with", &made);
        }
        ST(0) = sv_2mortal(newRV_inc((SV *)r));
        XSRETURN(1);

void
CLONE_SKIP(...)
    CODE:
        ST(0) = &PL_sv_yes;
        XSRETURN(1);
