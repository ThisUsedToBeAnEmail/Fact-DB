#ifndef FACTDB_CONN_H
#define FACTDB_CONN_H

/* A connection: the core (the schema), and its current database value,
 * counted, which reaches back through every value before it.
 *
 * ---- the handle --------------------------------------------------------
 *
 * A connection is a blessed reference to a scalar that carries the C struct
 * in ext magic, marked by the address of a vtable in this file, as HAMT's
 * and State::Store's handles are. Magic cannot be forged from Perl or
 * carried through a serialiser, so an object without it is not a
 * connection and every method says so; the struct is released when the
 * scalar is freed, so there is no DESTROY to call twice or to shadow in a
 * subclass; and a new thread does not get the handle, by CLONE_SKIP and,
 * failing that, by a dup hook that empties the clone's magic. */

typedef struct factdb_conn {
    factdb_core *core;
    factdb_db *current;
} factdb_conn;

static factdb_db *factdb_conn_current(const factdb_conn *c) { return c->current; }

/* Takes over a reference to `d`, which becomes the current value. */
static void factdb_conn_push(pTHX_ factdb_conn *c, factdb_db *d) {
    factdb_db *old = c->current;
    c->current = d;
    factdb_db_release(aTHX_ old);
}

static int factdb_conn_magic_free(pTHX_ SV *sv, MAGIC *mg) {
    factdb_conn *c = (factdb_conn *)mg->mg_ptr;
    PERL_UNUSED_ARG(sv);
    if (c) {
        mg->mg_ptr = NULL;
        factdb_db_release(aTHX_ c->current);
        if (c->core) factdb_core_release(aTHX_ c->core);
        factdb_free(aTHX_ c);
    }
    return 0;
}

static int factdb_conn_magic_dup(pTHX_ MAGIC *mg, CLONE_PARAMS *param) {
    PERL_UNUSED_CONTEXT;
    PERL_UNUSED_ARG(param);
    mg->mg_ptr = NULL;
    return 0;
}

static MGVTBL factdb_conn_vtbl = {
    NULL, NULL, NULL, NULL, factdb_conn_magic_free, NULL, factdb_conn_magic_dup, NULL
};

/* The struct behind an object, or a croak that names the method. */
static factdb_conn *factdb_conn_handle(pTHX_ SV *sv, const char *what) {
    if (SvROK(sv) && SvTYPE(SvRV(sv)) >= SVt_PVMG) {
        MAGIC *mg;
        for (mg = SvMAGIC(SvRV(sv)); mg; mg = mg->mg_moremagic) {
            if (mg->mg_type != PERL_MAGIC_ext || mg->mg_virtual != &factdb_conn_vtbl) continue;
            if (!mg->mg_ptr) croak("%s: this connection belongs to another thread", what);
            return (factdb_conn *)mg->mg_ptr;
        }
    }
    croak("%s: not a Fact::DB connection", what);
    return NULL;
}

/* The class to bless into: the one named, or the one an object is in. */
static HV *factdb_class_stash(pTHX_ SV *klass) {
    if (SvROK(klass) && SvOBJECT(SvRV(klass))) return SvSTASH(SvRV(klass));
    return gv_stashsv(klass, GV_ADD);
}

/* A new, empty connection of the class behind `stash`, mortal. The object
 * owns the struct from the start, so a croak afterwards frees both. */
static SV *factdb_conn_new(pTHX_ HV *stash, factdb_conn **out) {
    factdb_conn *c = (factdb_conn *)factdb_alloc(aTHX_ sizeof(factdb_conn));
    SV *inner = newSV(0);
    SV *rv;
    MAGIC *mg = sv_magicext(inner, NULL, PERL_MAGIC_ext, &factdb_conn_vtbl, (const char *)c, 0);
    mg->mg_flags |= MGf_DUP;
    rv = sv_2mortal(newRV_noinc(inner));
    sv_bless(rv, stash);
    c->core = factdb_core_new(aTHX);
    factdb_conn_push(aTHX_ c, factdb_db_empty(aTHX_ c->core));
    *out = c;
    return rv;
}

#endif
