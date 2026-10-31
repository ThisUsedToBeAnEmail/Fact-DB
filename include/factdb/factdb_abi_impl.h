#ifndef FACTDB_ABI_IMPL_H
#define FACTDB_ABI_IMPL_H

/* The table behind fact_db_abi.h. Each entry that can refuse runs its body
 * through factdb_protect: an XSUB of this library's own, called under
 * G_EVAL with $@ localised, which runs the body and nothing else. A croak
 * in the body becomes FACT_DB_REFUSED and its message last_error. */

struct fact_db_txn {
    AV *ops;
};

struct fact_db_query {
    SV *owner;
    factdb_query *q;
};

static int factdb_protect(pTHX_ void (*fn)(pTHX_ void *), void *ud) {
    dMY_CXT;
    int rc = FACT_DB_OK;
    dSP;
    ENTER;
    SAVETMPS;
    save_scalar(PL_errgv);
    MY_CXT.pending = fn;
    MY_CXT.pending_ud = ud;
    PUSHMARK(SP);
    PUTBACK;
    call_sv((SV *)MY_CXT.trampoline, G_DISCARD | G_EVAL | G_NOARGS);
    {
        dMY_CXT;
        MY_CXT.pending = NULL;
        if (SvTRUE(ERRSV)) {
            sv_setsv(MY_CXT.last_error, ERRSV);
            rc = FACT_DB_REFUSED;
        }
    }
    FREETMPS;
    LEAVE;
    return rc;
}

/* A reference of the caller's own on `sv`, which is evaluated once:
 * SvREFCNT_inc_simple_NN(f()) calls f twice. */
static SV *factdb_ab_own(SV *sv) {
    SvREFCNT_inc_simple_void_NN(sv);
    return sv;
}

/* Each interpreter its own: called at BOOT and by CLONE. */
static void factdb_abi_init(pTHX) {
    dMY_CXT;
    MY_CXT.last_error = newSVpvs("");
    MY_CXT.trampoline = get_cv("Fact::DB::_abi_call", 0);
    MY_CXT.pending = NULL;
    MY_CXT.pending_ud = NULL;
}

/* Non-croaking lookups of the two handles. */
static factdb_conn *factdb_conn_of(pTHX_ SV *sv) {
    MAGIC *mg;
    PERL_UNUSED_CONTEXT;
    if (!sv || !SvROK(sv) || SvTYPE(SvRV(sv)) < SVt_PVMG) return NULL;
    for (mg = SvMAGIC(SvRV(sv)); mg; mg = mg->mg_moremagic)
        if (mg->mg_type == PERL_MAGIC_ext && mg->mg_virtual == &factdb_conn_vtbl) return (factdb_conn *)mg->mg_ptr;
    return NULL;
}

static factdb_db *factdb_db_of(pTHX_ SV *sv) {
    MAGIC *mg;
    PERL_UNUSED_CONTEXT;
    if (!sv || !SvROK(sv) || SvTYPE(SvRV(sv)) < SVt_PVMG) return NULL;
    for (mg = SvMAGIC(SvRV(sv)); mg; mg = mg->mg_moremagic)
        if (mg->mg_type == PERL_MAGIC_ext && mg->mg_virtual == &factdb_db_vtbl) return (factdb_db *)mg->mg_ptr;
    return NULL;
}

static void factdb_abi_notdb(pTHX_ const char *what) {
    dMY_CXT;
    sv_setpvf(MY_CXT.last_error, "%s: not a Fact::DB connection or value\n", what);
}

/* ---- errors, connections ----------------------------------------------- */

static SV *factdb_abi_last_error(pTHX) {
    dMY_CXT;
    return MY_CXT.last_error;
}

static SV *factdb_abi_conn_new(pTHX) {
    factdb_conn *c;
    return factdb_ab_own(factdb_conn_new(aTHX_ gv_stashpvs("Fact::DB", GV_ADD), &c));
}

static int factdb_abi_is_conn(pTHX_ SV *sv) { return factdb_conn_of(aTHX_ sv) != NULL; }

typedef struct { factdb_conn *c; SV *name; int type, many, unique, indexed; } factdb_ab_attr;

static void factdb_ab_attr_run(pTHX_ void *ud) {
    factdb_ab_attr *x = (factdb_ab_attr *)ud;
    HV *def = newHV(), *top = newHV();
    SV *rv;
    if (x->type < 1 || x->type > FACTDB_T_MAX) croak("fact_db add_attr: type %d is not a type", x->type);
    (void)hv_stores(def, "type", newSVpv(factdb_type_names[x->type], 0));
    if (x->many) (void)hv_stores(def, "cardinality", newSVpvs("many"));
    if (x->unique == FACT_DB_UNIQUE_IDENTITY) (void)hv_stores(def, "unique", newSVpvs("identity"));
    else if (x->unique == FACT_DB_UNIQUE_VALUE) (void)hv_stores(def, "unique", newSVpvs("value"));
    if (x->indexed) (void)hv_stores(def, "index", newSViv(1));
    (void)hv_store_ent(top, x->name, newRV_noinc((SV *)def), 0);
    rv = sv_2mortal(newRV_noinc((SV *)top));
    factdb_schema_add(aTHX_ &x->c->core->schema, rv, "fact_db add_attr");
}

static int factdb_abi_add_attr(pTHX_ SV *conn, SV *name, int type, int many, int unique, int indexed) {
    factdb_ab_attr x;
    x.c = factdb_conn_of(aTHX_ conn);
    if (!x.c) { factdb_abi_notdb(aTHX_ "fact_db add_attr"); return FACT_DB_NOTDB; }
    x.name = name; x.type = type; x.many = many; x.unique = unique; x.indexed = indexed;
    return factdb_protect(aTHX_ factdb_ab_attr_run, &x);
}

static SV *factdb_abi_db(pTHX_ SV *conn) {
    factdb_conn *c = factdb_conn_of(aTHX_ conn);
    if (!c) { factdb_abi_notdb(aTHX_ "fact_db db"); return NULL; }
    return factdb_ab_own(factdb_db_sv(aTHX_ c->current));
}

static IV factdb_abi_versions(pTHX_ SV *conn) {
    factdb_conn *c = factdb_conn_of(aTHX_ conn);
    return c ? c->current->depth + 1 : FACT_DB_NOTDB;
}

/* ---- transactions ------------------------------------------------------ */

static fact_db_txn *factdb_abi_txn_new(pTHX) {
    fact_db_txn *t = (fact_db_txn *)factdb_alloc(aTHX_ sizeof(fact_db_txn));
    t->ops = newAV();
    return t;
}

static void factdb_abi_txn_free(pTHX_ fact_db_txn *t) {
    if (!t) return;
    SvREFCNT_dec((SV *)t->ops);
    factdb_free(aTHX_ t);
}

static SV *factdb_ab_copy(pTHX_ SV *sv) { return sv ? newSVsv(sv) : newSV(0); }

static void factdb_abi_txn_add(pTHX_ fact_db_txn *t, int op, SV *entity, SV *attr, SV *value) {
    AV *o = newAV();
    av_push(o, newSVpv(op == FACT_DB_OP_RETRACT ? "db/retract" : op == FACT_DB_OP_ADD ? "db/add" : "?", 0));
    av_push(o, factdb_ab_copy(aTHX_ entity));
    av_push(o, factdb_ab_copy(aTHX_ attr));
    av_push(o, factdb_ab_copy(aTHX_ value));
    av_push(t->ops, newRV_noinc((SV *)o));
}

static void factdb_abi_txn_retract_entity(pTHX_ fact_db_txn *t, SV *entity) {
    AV *o = newAV();
    av_push(o, newSVpvs("db/retractEntity"));
    av_push(o, factdb_ab_copy(aTHX_ entity));
    av_push(t->ops, newRV_noinc((SV *)o));
}

static void factdb_abi_txn_cas(pTHX_ fact_db_txn *t, SV *entity, SV *attr, SV *was, SV *now) {
    AV *o = newAV();
    av_push(o, newSVpvs("db/cas"));
    av_push(o, factdb_ab_copy(aTHX_ entity));
    av_push(o, factdb_ab_copy(aTHX_ attr));
    av_push(o, factdb_ab_copy(aTHX_ was));
    av_push(o, factdb_ab_copy(aTHX_ now));
    av_push(t->ops, newRV_noinc((SV *)o));
}

static void factdb_abi_txn_entity(pTHX_ fact_db_txn *t, HV *entity) {
    av_push(t->ops, newRV_noinc((SV *)newHVhv(entity)));
}

typedef struct { factdb_conn *c; factdb_db *d; fact_db_txn *t; fact_db_result r; } factdb_ab_tx;

static void factdb_ab_tx_fill(pTHX_ factdb_ab_tx *x, HV *res, factdb_db *made) {
    SV **tx = hv_fetchs(res, "tx", 0), **tempids = hv_fetchs(res, "tempids", 0);
    x->r.db = factdb_ab_own(factdb_db_sv(aTHX_ made));
    x->r.tx = tx ? SvIV(*tx) : 0;
    x->r.tempids = tempids ? (HV *)factdb_ab_own(SvRV(*tempids)) : newHV();
}

static void factdb_ab_transact_run(pTHX_ void *ud) {
    factdb_ab_tx *x = (factdb_ab_tx *)ud;
    factdb_db *made;
    HV *res = factdb_tx_run(aTHX_ factdb_conn_current(x->c), sv_2mortal(newRV_inc((SV *)x->t->ops)), "fact_db transact", &made);
    factdb_db_retain(made);
    factdb_conn_push(aTHX_ x->c, made);
    factdb_ab_tx_fill(aTHX_ x, res, made);
}

static void factdb_ab_with_run(pTHX_ void *ud) {
    factdb_ab_tx *x = (factdb_ab_tx *)ud;
    factdb_db *made;
    HV *res;
    if (x->d->kind != FACTDB_KIND_VALUE) croak("fact_db with: with is for a value a transaction made");
    res = factdb_tx_run(aTHX_ x->d, sv_2mortal(newRV_inc((SV *)x->t->ops)), "fact_db with", &made);
    factdb_ab_tx_fill(aTHX_ x, res, made);
}

static int factdb_abi_transact(pTHX_ SV *conn, fact_db_txn *t, fact_db_result *out) {
    factdb_ab_tx x;
    int rc;
    Zero(&x, 1, factdb_ab_tx);
    Zero(out, 1, fact_db_result);
    x.c = factdb_conn_of(aTHX_ conn);
    if (!x.c) { factdb_abi_notdb(aTHX_ "fact_db transact"); return FACT_DB_NOTDB; }
    x.t = t;
    rc = factdb_protect(aTHX_ factdb_ab_transact_run, &x);
    if (rc == FACT_DB_OK) *out = x.r;
    return rc;
}

static int factdb_abi_with(pTHX_ SV *db, fact_db_txn *t, fact_db_result *out) {
    factdb_ab_tx x;
    int rc;
    Zero(&x, 1, factdb_ab_tx);
    Zero(out, 1, fact_db_result);
    x.d = factdb_db_of(aTHX_ db);
    if (!x.d) { factdb_abi_notdb(aTHX_ "fact_db with"); return FACT_DB_NOTDB; }
    x.t = t;
    rc = factdb_protect(aTHX_ factdb_ab_with_run, &x);
    if (rc == FACT_DB_OK) *out = x.r;
    return rc;
}

static IV factdb_abi_tempid(pTHX_ const fact_db_result *r, SV *name) {
    HE *he;
    if (!r || !r->tempids) return 0;
    he = hv_fetch_ent(r->tempids, name, 0, 0);
    return he ? SvIV(HeVAL(he)) : 0;
}

static void factdb_abi_result_release(pTHX_ fact_db_result *r) {
    if (!r) return;
    SvREFCNT_dec(r->db);
    SvREFCNT_dec((SV *)r->tempids);
    Zero(r, 1, fact_db_result);
}

/* ---- values ------------------------------------------------------------ */

static int factdb_abi_is_db(pTHX_ SV *sv) { return factdb_db_of(aTHX_ sv) != NULL; }

static IV factdb_abi_basis_tx(pTHX_ SV *db) {
    factdb_db *d = factdb_db_of(aTHX_ db);
    return d ? d->basis_tx : FACT_DB_NOTDB;
}

static IV factdb_abi_count(pTHX_ SV *db) {
    factdb_db *d = factdb_db_of(aTHX_ db);
    return d ? d->ndatoms : FACT_DB_NOTDB;
}

typedef struct { factdb_db *d; IV tx; int which; SV *out; } factdb_ab_time;

static void factdb_ab_time_run(pTHX_ void *ud) {
    factdb_ab_time *x = (factdb_ab_time *)ud;
    factdb_db *d = x->d;
    if (x->which == 2) {
        if (d->kind == FACTDB_KIND_SINCE) croak("fact_db history: history is for a value a transaction made, not a since value");
        x->out = factdb_ab_own(d->kind == FACTDB_KIND_HISTORY ? factdb_db_sv(aTHX_ d) : factdb_history_build(aTHX_ d, "fact_db history"));
        return;
    }
    if (d->kind != FACTDB_KIND_VALUE) croak("fact_db %s: %s is for a value a transaction made, not a %s value", x->which ? "since" : "as_of", x->which ? "since" : "as_of", d->kind == FACTDB_KIND_SINCE ? "since" : "history");
    if (x->tx < 0) croak("fact_db %s: a transaction is a whole number, 0 or more", x->which ? "since" : "as_of");
    if (x->which == 0) x->out = factdb_ab_own(factdb_db_sv(aTHX_ factdb_db_as_of(d, x->tx, NULL)));
    else {
        factdb_db *made;
        SV *obj;
        if (d->since && d->since_tx == x->tx) { x->out = factdb_ab_own(factdb_db_sv(aTHX_ d->since)); return; }
        obj = factdb_since_build(aTHX_ d, x->tx, "fact_db since", &made);
        factdb_db_retain(made);
        if (d->since) factdb_db_release(aTHX_ d->since);
        d->since = made;
        d->since_tx = x->tx;
        x->out = factdb_ab_own(obj);
    }
}

static SV *factdb_ab_timed(pTHX_ SV *db, IV tx, int which) {
    factdb_ab_time x;
    x.d = factdb_db_of(aTHX_ db);
    x.tx = tx;
    x.which = which;
    x.out = NULL;
    if (!x.d) { factdb_abi_notdb(aTHX_ "fact_db value"); return NULL; }
    if (factdb_protect(aTHX_ factdb_ab_time_run, &x) != FACT_DB_OK) {
        if (x.out) SvREFCNT_dec(x.out);
        return NULL;
    }
    return x.out;
}

static SV *factdb_abi_as_of(pTHX_ SV *db, IV tx) { return factdb_ab_timed(aTHX_ db, tx, 0); }
static SV *factdb_abi_since(pTHX_ SV *db, IV tx) { return factdb_ab_timed(aTHX_ db, tx, 1); }
static SV *factdb_abi_history(pTHX_ SV *db) { return factdb_ab_timed(aTHX_ db, 0, 2); }

/* ---- reads ------------------------------------------------------------- */

typedef struct { factdb_db *d; SV *e; fact_db_pair_fn fn; void *ud; int stopped; } factdb_ab_ent;

static void factdb_ab_entity_run(pTHX_ void *ud) {
    factdb_ab_ent *x = (factdb_ab_ent *)ud;
    HV *hv;
    HE *he;
    if (x->d->kind == FACTDB_KIND_HISTORY) croak("fact_db entity: a history value holds no entities");
    hv = (HV *)sv_2mortal((SV *)factdb_entity_hv(aTHX_ x->d, factdb_read_entity(aTHX_ x->d, x->e, "fact_db entity")));
    hv_iterinit(hv);
    while ((he = hv_iternext(hv)) && !x->stopped) {
        SV *k = hv_iterkeysv(he), *v = HeVAL(he);
        if (SvCUR(k) == 5 && memEQ(SvPVX(k), "db/id", 5)) continue;
        if (SvROK(v) && SvTYPE(SvRV(v)) == SVt_PVAV) {
            AV *list = (AV *)SvRV(v);
            I32 i, n = av_len(list) + 1;
            for (i = 0; i < n && !x->stopped; i++)
                if (x->fn(aTHX_ x->ud, k, *av_fetch(list, i, 0))) x->stopped = 1;
        }
        else if (x->fn(aTHX_ x->ud, k, v)) x->stopped = 1;
    }
}

static int factdb_abi_entity(pTHX_ SV *db, SV *e, fact_db_pair_fn fn, void *ud) {
    factdb_ab_ent x;
    int rc;
    x.d = factdb_db_of(aTHX_ db);
    if (!x.d) { factdb_abi_notdb(aTHX_ "fact_db entity"); return FACT_DB_NOTDB; }
    x.e = e; x.fn = fn; x.ud = ud; x.stopped = 0;
    rc = factdb_protect(aTHX_ factdb_ab_entity_run, &x);
    return rc == FACT_DB_OK && x.stopped ? FACT_DB_STOPPED : rc;
}

typedef struct { factdb_db *d; int index; SV **parts; int nparts; fact_db_datom_fn fn; void *ud; int stopped; } factdb_ab_dat;

static void factdb_ab_datoms_run(pTHX_ void *ud) {
    factdb_ab_dat *x = (factdb_ab_dat *)ud;
    static const char *const names[] = { NULL, "eav", "aev", "ave", "vae" };
    AV *rows;
    I32 i, n;
    if (x->index < 1 || x->index > 4) croak("fact_db datoms: index %d is not one of FACT_DB_EAV, AEV, AVE and VAE", x->index);
    rows = factdb_datoms_av(aTHX_ x->d, sv_2mortal(newSVpv(names[x->index], 0)), x->parts, x->nparts, "fact_db datoms");
    n = av_len(rows) + 1;
    for (i = 0; i < n && !x->stopped; i++) {
        AV *row = (AV *)SvRV(*av_fetch(rows, i, 0));
        SV **added = av_fetch(row, 4, 0);
        if (x->fn(aTHX_ x->ud, SvIV(*av_fetch(row, 0, 0)), *av_fetch(row, 1, 0), *av_fetch(row, 2, 0),
                  SvIV(*av_fetch(row, 3, 0)), added ? (int)SvIV(*added) : -1)) x->stopped = 1;
    }
}

static int factdb_abi_datoms(pTHX_ SV *db, int index, SV **parts, int nparts, fact_db_datom_fn fn, void *ud) {
    factdb_ab_dat x;
    int rc;
    x.d = factdb_db_of(aTHX_ db);
    if (!x.d) { factdb_abi_notdb(aTHX_ "fact_db datoms"); return FACT_DB_NOTDB; }
    x.index = index; x.parts = parts; x.nparts = nparts; x.fn = fn; x.ud = ud; x.stopped = 0;
    rc = factdb_protect(aTHX_ factdb_ab_datoms_run, &x);
    return rc == FACT_DB_OK && x.stopped ? FACT_DB_STOPPED : rc;
}

/* ---- queries ----------------------------------------------------------- */

typedef struct { SV *text; fact_db_query *out; } factdb_ab_parse;

static void factdb_ab_parse_run(pTHX_ void *ud) {
    factdb_ab_parse *x = (factdb_ab_parse *)ud;
    SV *owner;
    factdb_query *q = factdb_cached_query(aTHX_ x->text, "fact_db query_parse", &owner);
    fact_db_query *h = (fact_db_query *)factdb_alloc(aTHX_ sizeof(fact_db_query));
    h->owner = factdb_ab_own(owner);
    h->q = q;
    x->out = h;
}

static fact_db_query *factdb_abi_query_parse(pTHX_ SV *text) {
    factdb_ab_parse x;
    x.text = text;
    x.out = NULL;
    if (factdb_protect(aTHX_ factdb_ab_parse_run, &x) != FACT_DB_OK) return NULL;
    return x.out;
}

static void factdb_abi_query_release(pTHX_ fact_db_query *q) {
    if (!q) return;
    SvREFCNT_dec(q->owner);
    factdb_free(aTHX_ q);
}

typedef struct { SV *db; fact_db_query *q; SV **inputs; I32 n; HV *functions; fact_db_row_fn fn; void *ud; int stopped; } factdb_ab_q;

static void factdb_ab_q_run(pTHX_ void *ud) {
    factdb_ab_q *x = (factdb_ab_q *)ud;
    const factdb_query *q = x->q->q;
    SV *res = factdb_query_run(aTHX_ x->db, x->q->q, x->inputs, x->n, x->functions, "fact_db query_run");
    SV *vals[64];
    U32 k;
    if (q->find_kind == FACTDB_F_SCALAR) {
        if (SvOK(res)) { vals[0] = res; if (x->fn(aTHX_ x->ud, vals, 1)) x->stopped = 1; }
        return;
    }
    if (q->find_kind == FACTDB_F_TUPLE) {
        AV *t;
        if (!SvOK(res)) return;
        t = (AV *)SvRV(res);
        for (k = 0; k < q->nfind && k < 64; k++) vals[k] = *av_fetch(t, k, 0);
        if (x->fn(aTHX_ x->ud, vals, q->nfind)) x->stopped = 1;
        return;
    }
    {
        AV *rows = (AV *)SvRV(res);
        I32 i, n = av_len(rows) + 1;
        for (i = 0; i < n && !x->stopped; i++) {
            SV *row = *av_fetch(rows, i, 0);
            if (q->find_kind == FACTDB_F_COLL) {
                vals[0] = row;
                if (x->fn(aTHX_ x->ud, vals, 1)) x->stopped = 1;
                continue;
            }
            for (k = 0; k < q->nfind && k < 64; k++) vals[k] = *av_fetch((AV *)SvRV(row), k, 0);
            if (x->fn(aTHX_ x->ud, vals, q->nfind)) x->stopped = 1;
        }
    }
}

static int factdb_abi_query_run(pTHX_ SV *db, fact_db_query *q, SV **inputs, I32 ninputs, HV *functions,
                                fact_db_row_fn fn, void *ud) {
    factdb_ab_q x;
    int rc;
    if (!factdb_db_of(aTHX_ db)) { factdb_abi_notdb(aTHX_ "fact_db query_run"); return FACT_DB_NOTDB; }
    if (!q) {
        dMY_CXT;
        sv_setpvs(MY_CXT.last_error, "fact_db query_run: no query\n");
        return FACT_DB_REFUSED;
    }
    if (q->q->nfind > 64) {
        dMY_CXT;
        sv_setpvs(MY_CXT.last_error, "fact_db query_run: a :find of more than 64 is not supported\n");
        return FACT_DB_REFUSED;
    }
    x.db = db; x.q = q; x.inputs = inputs; x.n = ninputs; x.functions = functions; x.fn = fn; x.ud = ud; x.stopped = 0;
    rc = factdb_protect(aTHX_ factdb_ab_q_run, &x);
    return rc == FACT_DB_OK && x.stopped ? FACT_DB_STOPPED : rc;
}

typedef struct { factdb_db *d; fact_db_query *q; SV *out; } factdb_ab_ex;

static void factdb_ab_ex_run(pTHX_ void *ud) {
    factdb_ab_ex *x = (factdb_ab_ex *)ud;
    x->out = newSVsv(factdb_query_explain(aTHX_ x->d, x->q->q, "fact_db explain"));
}

static SV *factdb_abi_explain(pTHX_ SV *db, fact_db_query *q) {
    factdb_ab_ex x;
    x.d = factdb_db_of(aTHX_ db);
    x.q = q;
    x.out = NULL;
    if (!x.d) { factdb_abi_notdb(aTHX_ "fact_db explain"); return NULL; }
    if (!q) {
        dMY_CXT;
        sv_setpvs(MY_CXT.last_error, "fact_db explain: no query\n");
        return NULL;
    }
    if (factdb_protect(aTHX_ factdb_ab_ex_run, &x) != FACT_DB_OK) return NULL;
    return x.out;
}

/* ---- the table --------------------------------------------------------- */

static const fact_db_abi FACTDB_ABI = {
    FACT_DB_ABI_VERSION,
    factdb_abi_last_error,
    factdb_abi_conn_new,
    factdb_abi_is_conn,
    factdb_abi_add_attr,
    factdb_abi_db,
    factdb_abi_versions,
    factdb_abi_txn_new,
    factdb_abi_txn_free,
    factdb_abi_txn_add,
    factdb_abi_txn_retract_entity,
    factdb_abi_txn_cas,
    factdb_abi_txn_entity,
    factdb_abi_transact,
    factdb_abi_with,
    factdb_abi_tempid,
    factdb_abi_result_release,
    factdb_abi_is_db,
    factdb_abi_basis_tx,
    factdb_abi_count,
    factdb_abi_as_of,
    factdb_abi_since,
    factdb_abi_history,
    factdb_abi_entity,
    factdb_abi_datoms,
    factdb_abi_query_parse,
    factdb_abi_query_release,
    factdb_abi_query_run,
    factdb_abi_explain,
};

#endif
