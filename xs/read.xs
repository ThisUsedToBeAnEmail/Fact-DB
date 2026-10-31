# Fact::DB::Db: reading a database value.

MODULE = Fact::DB    PACKAGE = Fact::DB::Db

# An entity as a hash reference: db/id, then each attribute it holds.
SV *
entity(self, e)
        SV *self
        SV *e
    PREINIT:
        factdb_db *d;
    CODE:
        d = factdb_db_handle(aTHX_ self, "Fact::DB::Db::entity");
        if (d->kind == FACTDB_KIND_HISTORY)
            croak("Fact::DB::Db::entity: a history value holds every assertion and retraction, not entities; ask datoms");
        RETVAL = newRV_noinc((SV *)factdb_entity_hv(aTHX_ d, factdb_read_entity(aTHX_ d, e, "Fact::DB::Db::entity")));
    OUTPUT:
        RETVAL

# How many datoms the value holds.
IV
count(self)
        SV *self
    CODE:
        RETVAL = factdb_db_handle(aTHX_ self, "Fact::DB::Db::count")->ndatoms;
    OUTPUT:
        RETVAL

# datoms($index, @components): eav, aev, ave or vae, then what is bound, in
# that index's order. eav or aev with nothing bound is every datom.
SV *
datoms(self, index, ...)
        SV *self
        SV *index
    CODE:
        RETVAL = newRV_inc((SV *)factdb_datoms_av(aTHX_ factdb_db_handle(aTHX_ self, "Fact::DB::Db::datoms"), index, &ST(2), items - 2, "Fact::DB::Db::datoms"));
    OUTPUT:
        RETVAL
