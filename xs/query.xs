# Fact::DB: the Datalog reader and parser. Queries run from Phase 09; until
# then the parser is reached through these test doors.

MODULE = Fact::DB    PACKAGE = Fact::DB

# The forms of a text, as [type, line, column, value].
SV *
_read(text)
        SV *text
    PREINIT:
        factdb_owned *o;
    CODE:
        (void)factdb_owned_new(aTHX_ &o);
        RETVAL = factdb_dump_edn(aTHX_ factdb_read(aTHX_ text, &o->arena, "Fact::DB::_read"));
    OUTPUT:
        RETVAL

# A query parsed, and its rules when given, as plain data; every check a
# parse makes is made.
SV *
_parse(text, ...)
        SV *text
    PREINIT:
        const char *what = "Fact::DB::_parse";
        factdb_owned *o;
        factdb_query *q;
        factdb_ruleset *rs = NULL;
        HV *out;
    CODE:
        (void)factdb_owned_new(aTHX_ &o);
        q = factdb_parse_query(aTHX_ factdb_read(aTHX_ text, &o->arena, what), &o->arena, what);
        if (items > 1 && SvOK(ST(1))) {
            U32 r;
            rs = factdb_parse_rules(aTHX_ factdb_read(aTHX_ ST(1), &o->arena, "Fact::DB::_parse rules"), &o->arena, "Fact::DB::_parse rules");
            for (r = 0; r < rs->n; r++)
                factdb_calls_check(aTHX_ rs->rules[r].body, rs->rules[r].nbody, rs, 1, "Fact::DB::_parse rules");
            factdb_stratify(aTHX_ rs, "Fact::DB::_parse rules");
        }
        factdb_calls_check(aTHX_ q->where, q->nwhere, rs, q->has_rules, what);
        out = (HV *)SvRV(sv_2mortal(factdb_dump_query(aTHX_ q)));
        if (rs) (void)hv_stores(out, "rules", factdb_dump_rules(aTHX_ rs));
        RETVAL = newRV_inc((SV *)out);
    OUTPUT:
        RETVAL
