#ifndef FACTDB_DB_H
#define FACTDB_DB_H

/* A database value, and the core it shares with its connection.
 *
 * The core holds the schema. It is counted, so a database value outlives
 * the connection that made it and still knows its attributes.
 *
 * A database value never changes once made. It holds:
 *
 *   eav      entity id -> the entity's record (factdb_record.h)
 *   ave      by attribute id, for indexed attributes: encoded value -> the
 *            entity id (a unique attribute) or a HAMT::Set of entity ids
 *   log      one entry a transaction, oldest first
 *
 * An AVE map is wrapped in a counted factdb_idx so that the values a run
 * of transactions makes share the maps they did not touch, struct and all.
 *
 * Its Perl object is made once and kept by address in `obj`, uncounted:
 * asking for it again hands out the same object while it lives.
 *
 * ---- time --------------------------------------------------------------
 *
 * A value made by a transaction holds the value it was made from, counted:
 * `parent`. So every value reaches back through every version before it,
 * and those stay alive while it does. `jump` is a second, uncounted link
 * further back, chosen as in Myers' jump pointers, so that the latest
 * ancestor at or before a transaction is found in O(log n) steps from any
 * value: `as_of` needs no list of versions, works on a `with` value, and
 * works after the connection is gone. A chain is released in a loop, never
 * by recursion, however long it is.
 *
 * `kind` is FACTDB_KIND_VALUE for a value a transaction made, _SINCE for
 * one `since` made (the facts asserted after a transaction; no parent),
 * _HISTORY for one `history` made (read from the log; no parent). */

#define FACTDB_KIND_VALUE   0
#define FACTDB_KIND_SINCE   1
#define FACTDB_KIND_HISTORY 2

typedef struct factdb_core {
    U32 refcnt;
    factdb_schema schema;
} factdb_core;

typedef struct factdb_idx {
    U32 refcnt;
    hamt_map *m;
} factdb_idx;

typedef struct factdb_db {
    U32 refcnt;
    factdb_core *core;
    hamt_map *eav;
    factdb_idx **ave;
    U32 nave;
    hamt_vec *log;
    IV basis_tx;
    IV next_id;
    IV ndatoms;
    SV *obj;
    struct factdb_db *parent;
    struct factdb_db *jump;
    IV depth;
    int kind;
    IV since_tx;
    struct factdb_db *since;
} factdb_db;

static factdb_core *factdb_core_new(pTHX) {
    factdb_core *k = (factdb_core *)factdb_alloc(aTHX_ sizeof(factdb_core));
    k->refcnt = 1;
    factdb_schema_init(aTHX_ &k->schema);
    return k;
}

static void factdb_core_release(pTHX_ factdb_core *k) {
    if (--k->refcnt) return;
    factdb_schema_free(aTHX_ &k->schema);
    factdb_free(aTHX_ k);
}

static factdb_idx *factdb_idx_new(pTHX_ hamt_map *m) {
    factdb_idx *x = (factdb_idx *)factdb_alloc(aTHX_ sizeof(factdb_idx));
    x->refcnt = 1;
    x->m = m;
    return x;
}

static void factdb_idx_release(pTHX_ factdb_idx *x) {
    if (!x || --x->refcnt) return;
    (FACTDB_HAMT->map_release)(aTHX_ x->m);
    factdb_free(aTHX_ x);
}

static void factdb_db_retain(factdb_db *d) { d->refcnt++; }

static void factdb_db_release(pTHX_ factdb_db *d) {
    while (d && --d->refcnt == 0) {
        factdb_db *parent = d->parent;
        U32 i;
        if (d->eav) (FACTDB_HAMT->map_release)(aTHX_ d->eav);
        for (i = 0; i < d->nave; i++) factdb_idx_release(aTHX_ d->ave[i]);
        if (d->ave) factdb_free(aTHX_ d->ave);
        if (d->log) (FACTDB_HAMT->vec_release)(aTHX_ d->log);
        if (d->since) factdb_db_release(aTHX_ d->since);
        factdb_core_release(aTHX_ d->core);
        factdb_free(aTHX_ d);
        d = parent;
    }
}

/* Makes `p` the value `d` was made from. */
static void factdb_db_link(factdb_db *d, factdb_db *p) {
    d->parent = p;
    p->refcnt++;
    d->depth = p->depth + 1;
    if (p->jump->jump && p->depth - p->jump->depth == p->jump->depth - p->jump->jump->depth)
        d->jump = p->jump->jump;
    else
        d->jump = p;
}

/* The latest value at or before transaction `t` on `d`'s line: `d` itself,
 * or one it came from. NULL when there is none, which happens only for a
 * `t` before the empty value's. *steps, when given, gets the steps taken. */
static factdb_db *factdb_db_as_of(factdb_db *d, IV t, IV *steps) {
    IV n = 0;
    while (d->basis_tx > t) {
        if (!d->parent) { d = NULL; break; }
        d = d->jump != d && d->jump->basis_tx > t ? d->jump : d->parent;
        n++;
    }
    if (steps) *steps = n;
    return d;
}

/* An empty database value on `core`: no facts, nothing in the log. */
static factdb_db *factdb_db_empty(pTHX_ factdb_core *core) {
    factdb_db *d = (factdb_db *)factdb_alloc(aTHX_ sizeof(factdb_db));
    d->refcnt = 1;
    d->core = core;
    core->refcnt++;
    d->eav = (FACTDB_HAMT->map_new)(aTHX);
    d->log = (FACTDB_HAMT->vec_new)(aTHX);
    if (!d->eav || !d->log) {
        factdb_db_release(aTHX_ d);
        croak("Fact::DB: out of memory");
    }
    d->basis_tx = 0;
    d->next_id = 1;
    d->jump = d;
    return d;
}

/* ---- the Perl object of a database value ------------------------------ */

static int factdb_db_magic_free(pTHX_ SV *sv, MAGIC *mg) {
    factdb_db *d = (factdb_db *)mg->mg_ptr;
    PERL_UNUSED_ARG(sv);
    if (d) {
        mg->mg_ptr = NULL;
        d->obj = NULL;
        factdb_db_release(aTHX_ d);
    }
    return 0;
}

static int factdb_db_magic_dup(pTHX_ MAGIC *mg, CLONE_PARAMS *param) {
    PERL_UNUSED_CONTEXT;
    PERL_UNUSED_ARG(param);
    mg->mg_ptr = NULL;
    return 0;
}

static MGVTBL factdb_db_vtbl = {
    NULL, NULL, NULL, NULL, factdb_db_magic_free, NULL, factdb_db_magic_dup, NULL
};

static factdb_db *factdb_db_handle(pTHX_ SV *sv, const char *what) {
    if (SvROK(sv) && SvTYPE(SvRV(sv)) >= SVt_PVMG) {
        MAGIC *mg;
        for (mg = SvMAGIC(SvRV(sv)); mg; mg = mg->mg_moremagic) {
            if (mg->mg_type != PERL_MAGIC_ext || mg->mg_virtual != &factdb_db_vtbl) continue;
            if (!mg->mg_ptr) croak("%s: this database value belongs to another thread", what);
            return (factdb_db *)mg->mg_ptr;
        }
    }
    croak("%s: not a Fact::DB::Db", what);
    return NULL;
}

/* The object of a database value, a new reference, mortal: the one it has
 * if it has one, else a new one, which then holds the value. */
static SV *factdb_db_sv(pTHX_ factdb_db *d) {
    SV *inner;
    MAGIC *mg;
    if (d->obj) return sv_2mortal(newRV_inc(d->obj));
    inner = newSV(0);
    mg = sv_magicext(inner, NULL, PERL_MAGIC_ext, &factdb_db_vtbl, (const char *)d, 0);
    mg->mg_flags |= MGf_DUP;
    factdb_db_retain(d);
    d->obj = inner;
    return sv_bless(sv_2mortal(newRV_noinc(inner)), gv_stashpvs("Fact::DB::Db", GV_ADD));
}

#endif
