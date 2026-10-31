#ifndef FACTDB_TIME_H
#define FACTDB_TIME_H

/* Reading a value at other times: the log, `since` and `history`.
 *
 * A log entry is one transaction: its id, how many datoms, then each
 * datom's entity, attribute, flags (FACTDB_F_UTF8, FACTDB_F_ADDED) and
 * encoding. The entries of a value's log are in the order of its line,
 * so their transaction ids rise. */

/* A transaction id given to as_of or since: a whole number, 0 or more. */
static IV factdb_tx_arg(pTHX_ SV *sv, const char *what) {
    IV iv = 0;
    UV uv = 0;
    int is_uv;
    if (!SvOK(sv) || SvROK(sv) || !looks_like_number(sv)
        || factdb_whole(aTHX_ sv, &iv, &uv, &is_uv) != 1 || is_uv || iv < 0)
        croak("%s: a transaction is a whole number, 0 or more, not %" SVf, what, SVfARG(factdb_quote(aTHX_ sv)));
    return iv;
}

typedef int (*factdb_log_fn)(pTHX_ void *ud, IV tx, IV e, U16 a, U8 flags, const char *v, STRLEN vl);

static IV factdb_log_tx(pTHX_ const factdb_db *d, size_t i) {
    SV *rec = (FACTDB_HAMT->vec_fetch)(aTHX_ d->log, i);
    STRLEN len;
    const char *p = SvPV(rec, len), *end = p + len;
    UV tx = 0;
    (void)factdb_get_varint(&p, end, &tx);
    return (IV)tx;
}

/* The index of the first entry of `d`'s log whose transaction is after `t`. */
static size_t factdb_log_after(pTHX_ const factdb_db *d, IV t) {
    size_t lo = 0, hi = (FACTDB_HAMT->vec_count)(aTHX_ d->log);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (factdb_log_tx(aTHX_ d, mid) <= t) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

/* Every datom of the log from entry `from` on, in order. 1 if `fn` stopped. */
static int factdb_log_each(pTHX_ const factdb_db *d, size_t from, factdb_log_fn fn, void *ud) {
    size_t i, n = (FACTDB_HAMT->vec_count)(aTHX_ d->log);
    for (i = from; i < n; i++) {
        SV *rec = (FACTDB_HAMT->vec_fetch)(aTHX_ d->log, i);
        STRLEN len;
        const char *p = SvPV(rec, len), *end = p + len;
        UV tx = 0, count = 0, k;
        (void)factdb_get_varint(&p, end, &tx);
        (void)factdb_get_varint(&p, end, &count);
        for (k = 0; k < count; k++) {
            UV e = 0, vl = 0;
            U16 a;
            U8 flags;
            (void)factdb_get_varint(&p, end, &e);
            a = (U16)((U8)p[0] | ((U8)p[1] << 8));
            flags = (U8)p[2];
            p += 3;
            (void)factdb_get_varint(&p, end, &vl);
            if (fn(aTHX_ ud, (IV)tx, (IV)e, a, flags, p, (STRLEN)vl)) return 1;
            p += vl;
        }
    }
    return 0;
}

static int factdb_log_count(pTHX_ void *ud, IV tx, IV e, U16 a, U8 flags, const char *v, STRLEN vl) {
    PERL_UNUSED_ARG(tx); PERL_UNUSED_ARG(e); PERL_UNUSED_ARG(a);
    PERL_UNUSED_ARG(flags); PERL_UNUSED_ARG(v); PERL_UNUSED_ARG(vl);
    (*(IV *)ud)++;
    return 0;
}

/* A new value on `src`'s core, of `kind`, with no parent. */
static factdb_db *factdb_db_derived(pTHX_ const factdb_db *src, int kind) {
    factdb_db *d = (factdb_db *)factdb_alloc(aTHX_ sizeof(factdb_db));
    d->refcnt = 1;
    d->core = src->core;
    d->core->refcnt++;
    d->kind = kind;
    d->basis_tx = src->basis_tx;
    d->next_id = src->next_id;
    d->jump = d;
    d->depth = 0;
    return d;
}

/* ---- since ------------------------------------------------------------- */

typedef struct factdb_since_b {
    const factdb_db *src;
    factdb_txa *t;
} factdb_since_b;

/* An assertion from the log is kept when the value still holds that fact
 * and holds it from that transaction: then it is the assertion that made
 * the fact as it is now, and each fact is kept once. */
static int factdb_since_one(pTHX_ void *ud, IV tx, IV e, U16 a, U8 flags, const char *v, STRLEN vl) {
    factdb_since_b *sb = (factdb_since_b *)ud;
    factdb_txa *t = sb->t;
    factdb_entry cur, add;
    char k[24];
    STRLEN kl;
    SV *rec, *edited;
    if (!(flags & FACTDB_F_ADDED)) return 0;
    if (!factdb_rec_find(aTHX_ factdb_record_of(aTHX_ sb->src, e), a, flags & FACTDB_F_UTF8, v, vl, &cur)) return 0;
    if ((IV)cur.tx != tx) return 0;
    kl = factdb_ekey(e, k);
    rec = (FACTDB_HAMT->map_fetch)(aTHX_ t->db->eav, k, kl, 0);
    add = cur;
    edited = sv_2mortal(factdb_rec_edit(aTHX_ rec, &add, NULL));
    FACTDB_CK(t, (FACTDB_HAMT->map_store)(aTHX_ t->db->eav, k, kl, 0, edited));
    factdb_txa_index(aTHX_ t, &t->schema->attrs[a], a, e, v, vl, flags & FACTDB_F_UTF8, 1);
    t->db->ndatoms++;
    return 0;
}

/* The value of the facts `src` holds that were asserted after `t`. Its
 * object, mortal; the value is in *made. */
static SV *factdb_since_build(pTHX_ factdb_db *src, IV t_after, const char *what, factdb_db **made) {
    const factdb_schema *schema = &src->core->schema;
    factdb_db *nd = factdb_db_derived(aTHX_ src, FACTDB_KIND_SINCE);
    SV *obj = factdb_db_sv(aTHX_ nd);
    factdb_txa t;
    factdb_since_b sb;
    size_t from, n;
    int status = 0;
    factdb_db_release(aTHX_ nd);
    nd->eav = (FACTDB_HAMT->map_new)(aTHX);
    nd->nave = schema->n;
    nd->ave = (factdb_idx **)factdb_alloc(aTHX_ (size_t)(nd->nave ? nd->nave : 1) * sizeof(factdb_idx *));
    from = factdb_log_after(aTHX_ src, t_after);
    n = (FACTDB_HAMT->vec_count)(aTHX_ src->log);
    nd->log = (FACTDB_HAMT->vec_slice)(aTHX_ src->log, from, n - from, &status);
    if (!nd->eav || !nd->log) croak("%s: out of memory", what);
    Zero(&t, 1, factdb_txa);
    t.what = what;
    t.schema = schema;
    t.db = nd;
    Newxz(t.touched, nd->nave ? nd->nave : 1, U8);
    SAVEFREEPV(t.touched);
    sb.src = src;
    sb.t = &t;
    factdb_log_each(aTHX_ src, from, factdb_since_one, &sb);
    *made = nd;
    return obj;
}

/* ---- history ----------------------------------------------------------- */

static SV *factdb_history_build(pTHX_ factdb_db *src, const char *what) {
    factdb_db *nd = factdb_db_derived(aTHX_ src, FACTDB_KIND_HISTORY);
    SV *obj = factdb_db_sv(aTHX_ nd);
    U32 i;
    factdb_db_release(aTHX_ nd);
    nd->eav = (FACTDB_HAMT->map_copy)(aTHX_ src->eav);
    nd->log = (FACTDB_HAMT->vec_copy)(aTHX_ src->log);
    if (!nd->eav || !nd->log) croak("%s: out of memory", what);
    nd->nave = src->nave;
    nd->ave = (factdb_idx **)factdb_alloc(aTHX_ (size_t)(nd->nave ? nd->nave : 1) * sizeof(factdb_idx *));
    for (i = 0; i < nd->nave; i++)
        if (src->ave[i]) { nd->ave[i] = src->ave[i]; nd->ave[i]->refcnt++; }
    factdb_log_each(aTHX_ nd, 0, factdb_log_count, &nd->ndatoms);
    return obj;
}

/* The datoms of a history value that match: [e, a, v, tx, added]. Any of
 * `e`, `a` and `v` may be left open (e 0, a -1, v NULL); `refs` keeps only
 * datoms of ref attributes. */
typedef struct factdb_hfilter {
    const factdb_schema *s;
    IV e;
    int a;
    const char *v;
    STRLEN vl;
    U8 vflags;
    int refs;
    AV *out;
} factdb_hfilter;

static int factdb_history_one(pTHX_ void *ud, IV tx, IV e, U16 a, U8 flags, const char *v, STRLEN vl) {
    factdb_hfilter *f = (factdb_hfilter *)ud;
    const factdb_attr *at;
    AV *row;
    if (f->e && e != f->e) return 0;
    if (f->a >= 0 && a != (U16)f->a) return 0;
    at = &f->s->attrs[a];
    if (f->refs && at->type != FACTDB_T_REF) return 0;
    if (f->v && (vl != f->vl || (flags & FACTDB_F_UTF8) != f->vflags || memNE(v, f->v, vl))) return 0;
    row = newAV();
    av_extend(row, 4);
    av_push(row, newSViv(e));
    av_push(row, newSVsv(at->name));
    av_push(row, factdb_decode(aTHX_ at->type, v, vl, flags & FACTDB_F_UTF8));
    av_push(row, newSViv(tx));
    av_push(row, newSViv(flags & FACTDB_F_ADDED ? 1 : 0));
    av_push(f->out, newRV_noinc((SV *)row));
    return 0;
}

#endif
