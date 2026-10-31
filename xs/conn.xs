# Fact::DB: the connection. From Phase 03, the schema only.

MODULE = Fact::DB    PACKAGE = Fact::DB

# new(%options). The only option is `schema`.
void
new(klass, ...)
        SV *klass
    PREINIT:
        factdb_conn *c;
        SV *obj, *schema = NULL;
        I32 i;
    CODE:
        if (!(items & 1))
            croak("Fact::DB::new: odd number of options");
        for (i = 1; i < items; i += 2) {
            const char *name = SvPV_nolen(ST(i));
            if (strEQ(name, "schema")) schema = ST(i + 1);
            else croak("Fact::DB::new: unknown option \"%" SVf "\"", SVfARG(ST(i)));
        }
        obj = factdb_conn_new(aTHX_ factdb_class_stash(aTHX_ klass), &c);
        if (schema) factdb_schema_add(aTHX_ &c->core->schema, schema, "Fact::DB::new");
        ST(0) = obj;
        XSRETURN(1);

void
add_schema(self, defs)
        SV *self
        SV *defs
    CODE:
        factdb_schema_add(aTHX_ &factdb_conn_handle(aTHX_ self, "Fact::DB::add_schema")->core->schema,
                          defs, "Fact::DB::add_schema");
        XSRETURN(1);

SV *
schema(self)
        SV *self
    CODE:
        RETVAL = newRV_noinc((SV *)factdb_schema_hv(aTHX_ &factdb_conn_handle(aTHX_ self, "Fact::DB::schema")->core->schema));
    OUTPUT:
        RETVAL

void
CLONE_SKIP(...)
    CODE:
        ST(0) = &PL_sv_yes;
        XSRETURN(1);
