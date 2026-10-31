#ifndef FACTDB_READ_H
#define FACTDB_READ_H

/* The ways into a database value: one function for each shape of question,
 * each calling back once a datom, so nothing is collected that the caller
 * does not keep. These are what a query pattern is answered by.
 *
 *   bound          read from                     function
 *   e [a [v]]      the entity's record            factdb_scan_e
 *   a              the attribute's AVE if it has  factdb_scan_a
 *                  one, else every record
 *   a v            AVE if it has one, else every  factdb_scan_av
 *                  record
 *   v, a ref       the AVE of each ref attribute  factdb_scan_v
 *   nothing        every record                   factdb_scan_all
 *
 * Whichever way it is read, a datom comes from the entity's record: that is
 * where the transaction that asserted it is kept. */

typedef int (*factdb_datom_fn)(pTHX_ void *ud, IV e, const factdb_entry *x);

static SV *factdb_record_of(pTHX_ const factdb_db *d, IV e) {
    char k[24];
    STRLEN kl = factdb_ekey(e, k);
    return (FACTDB_HAMT->map_fetch)(aTHX_ d->eav, k, kl, 0);
}

static hamt_map *factdb_ave_of(const factdb_db *d, U16 a) {
    return a < d->nave && d->ave[a] ? d->ave[a]->m : NULL;
}

/* Every datom of `e`; of one attribute when `a` >= 0; of one value of it
 * when `v` is not NULL. 1 when `fn` stopped it. */
static int factdb_scan_e(pTHX_ const factdb_db *d, IV e, int a, const char *v, STRLEN vl, U8 flags,
                         factdb_datom_fn fn, void *ud) {
    SV *rec = factdb_record_of(aTHX_ d, e);
    STRLEN len;
    const char *p, *end;
    factdb_entry x, want;
    if (!rec) return 0;
    p = SvPV(rec, len);
    end = p + len;
    want.a = (U16)a; want.flags = flags; want.v = v; want.vlen = vl;
    while (factdb_rec_next(&p, end, &x)) {
        if (a >= 0) {
            if (x.a < (U16)a) continue;
            if (x.a > (U16)a) break;
            if (v && factdb_entry_cmp(&x, &want) != 0) continue;
        }
        if (fn(aTHX_ ud, e, &x)) return 1;
    }
    return 0;
}

typedef struct factdb_filter {
    int a;
    const char *v;
    STRLEN vl;
    U8 flags;
    factdb_datom_fn fn;
    void *ud;
    int stopped;
} factdb_filter;

static int factdb_filter_record(pTHX_ void *ud, const char *key, STRLEN klen, int utf8, SV *rec) {
    factdb_filter *f = (factdb_filter *)ud;
    IV e = factdb_ekey_iv(key, klen);
    PERL_UNUSED_ARG(utf8);
    {
        STRLEN len;
        const char *p = SvPV(rec, len), *end = p + len;
        factdb_entry x, want;
        want.a = (U16)f->a; want.flags = f->flags; want.v = f->v; want.vlen = f->vl;
        while (factdb_rec_next(&p, end, &x)) {
            if (f->a >= 0) {
                if (x.a < (U16)f->a) continue;
                if (x.a > (U16)f->a) break;
                if (f->v && factdb_entry_cmp(&x, &want) != 0) continue;
            }
            if (f->fn(aTHX_ f->ud, e, &x)) { f->stopped = 1; return 1; }
        }
    }
    return 0;
}

/* Every datom, or every one of attribute `a`, or of `a` holding `v`, read
 * from every record. */
static int factdb_scan_records(pTHX_ const factdb_db *d, int a, const char *v, STRLEN vl, U8 flags,
                               factdb_datom_fn fn, void *ud) {
    factdb_filter f;
    f.a = a; f.v = v; f.vl = vl; f.flags = flags; f.fn = fn; f.ud = ud; f.stopped = 0;
    (FACTDB_HAMT->map_walk)(aTHX_ d->eav, factdb_filter_record, &f);
    return f.stopped;
}

static int factdb_scan_all(pTHX_ const factdb_db *d, factdb_datom_fn fn, void *ud) {
    return factdb_scan_records(aTHX_ d, -1, NULL, 0, 0, fn, ud);
}

/* The entities an AVE map holds under one value: one id, or a set's. */
typedef struct factdb_members {
    const factdb_db *d;
    U16 a;
    const char *v;
    STRLEN vl;
    U8 flags;
    factdb_datom_fn fn;
    void *ud;
    int stopped;
} factdb_members;

static int factdb_member(pTHX_ void *ud, const char *key, STRLEN klen, int utf8, SV *value) {
    factdb_members *m = (factdb_members *)ud;
    IV e = factdb_ekey_iv(key, klen);
    PERL_UNUSED_ARG(utf8); PERL_UNUSED_ARG(value);
    if (factdb_scan_e(aTHX_ m->d, e, m->a, m->v, m->vl, m->flags, m->fn, m->ud)) {
        m->stopped = 1;
        return 1;
    }
    return 0;
}

static int factdb_scan_held(pTHX_ factdb_members *m, SV *held) {
    if (!SvROK(held))
        return factdb_scan_e(aTHX_ m->d, SvIV(held), m->a, m->v, m->vl, m->flags, m->fn, m->ud);
    (FACTDB_HAMT->set_walk)(aTHX_ (FACTDB_HAMT->set_from_sv)(aTHX_ held), factdb_member, m);
    return m->stopped;
}

static int factdb_scan_av(pTHX_ const factdb_db *d, U16 a, const char *v, STRLEN vl, U8 flags,
                          factdb_datom_fn fn, void *ud) {
    hamt_map *ave = factdb_ave_of(d, a);
    factdb_members m;
    SV *held;
    if (!d->core->schema.attrs[a].indexed)
        return factdb_scan_records(aTHX_ d, a, v, vl, flags, fn, ud);
    if (!ave) return 0;
    held = (FACTDB_HAMT->map_fetch)(aTHX_ ave, v, vl, flags & FACTDB_F_UTF8);
    if (!held) return 0;
    m.d = d; m.a = a; m.v = v; m.vl = vl; m.flags = flags; m.fn = fn; m.ud = ud; m.stopped = 0;
    return factdb_scan_held(aTHX_ &m, held);
}

static int factdb_each_value(pTHX_ void *ud, const char *key, STRLEN klen, int utf8, SV *held) {
    factdb_members *m = (factdb_members *)ud;
    m->v = key;
    m->vl = klen;
    m->flags = utf8 ? FACTDB_F_UTF8 : 0;
    return factdb_scan_held(aTHX_ m, held);
}

static int factdb_scan_a(pTHX_ const factdb_db *d, U16 a, factdb_datom_fn fn, void *ud) {
    hamt_map *ave = factdb_ave_of(d, a);
    factdb_members m;
    if (!d->core->schema.attrs[a].indexed)
        return factdb_scan_records(aTHX_ d, a, NULL, 0, 0, fn, ud);
    if (!ave) return 0;
    m.d = d; m.a = a; m.fn = fn; m.ud = ud; m.stopped = 0;
    (FACTDB_HAMT->map_walk)(aTHX_ ave, factdb_each_value, &m);
    return m.stopped;
}

/* Every datom whose value is a ref to `target`; of one ref attribute when
 * `a` >= 0. */
static int factdb_scan_v(pTHX_ const factdb_db *d, IV target, int a, factdb_datom_fn fn, void *ud) {
    const factdb_schema *s = &d->core->schema;
    char v[24];
    STRLEN vl = factdb_ekey(target, v);
    U32 i;
    for (i = 0; i < s->n; i++) {
        if (s->attrs[i].type != FACTDB_T_REF) continue;
        if (a >= 0 && (U32)a != i) continue;
        if (factdb_scan_av(aTHX_ d, (U16)i, v, vl, 0, fn, ud)) return 1;
    }
    return 0;
}

/* ---- to Perl ---------------------------------------------------------- */

static int factdb_is_list(SV *sv) {
    return SvROK(sv) && SvTYPE(SvRV(sv)) == SVt_PVAV && !SvOBJECT(SvRV(sv));
}

/* The entity a lookup ref, [attribute, value], names in `d`: the one that
 * holds that value of that unique attribute. `where` goes after `what` in
 * a message: "" or "[2] ". */
static IV factdb_lookup(pTHX_ const factdb_db *d, SV *ref, const char *what, const char *where) {
    AV *av = (AV *)SvRV(ref);
    SV **f = AvARRAY(av);
    const factdb_attr *at;
    SV *enc, *why, *held, *val;
    hamt_map *ave;
    IV a;
    int utf8;
    STRLEN vl;
    const char *v;
    if (av_len(av) != 1 || !f[0] || !SvOK(f[0]) || SvROK(f[0]))
        croak("%s: %sa lookup ref is [attribute, value]", what, where);
    a = factdb_attr_id(aTHX_ &d->core->schema, f[0]);
    if (a < 0) croak("%s: %s%" SVf " is not in the schema", what, where, SVfARG(f[0]));
    at = &d->core->schema.attrs[a];
    if (!at->unique)
        croak("%s: %s%" SVf " is not unique, so a value of it does not name one entity", what, where, SVfARG(at->name));
    val = f[1] ? f[1] : &PL_sv_undef;
    enc = sv_newmortal();
    why = factdb_encode(aTHX_ at->type, val, enc, &utf8);
    if (why) croak("%s: %s%" SVf ": %" SVf, what, where, SVfARG(at->name), SVfARG(why));
    v = SvPV(enc, vl);
    ave = factdb_ave_of(d, (U16)a);
    held = ave ? (FACTDB_HAMT->map_fetch)(aTHX_ ave, v, vl, utf8) : NULL;
    if (!held)
        croak("%s: %sno entity has %" SVf " %" SVf, what, where, SVfARG(at->name),
              SVfARG(factdb_quote(aTHX_ sv_2mortal(factdb_decode(aTHX_ at->type, v, vl, utf8)))));
    return SvIV(held);
}

/* An entity given to a read: an id the value has given out, or a lookup
 * ref. */
static IV factdb_read_entity(pTHX_ const factdb_db *d, SV *sv, const char *what) {
    IV iv = 0;
    UV uv = 0;
    int is_uv;
    if (factdb_is_list(sv)) return factdb_lookup(aTHX_ d, sv, what, "");
    if (!SvOK(sv) || SvROK(sv) || !looks_like_number(sv)
        || factdb_whole(aTHX_ sv, &iv, &uv, &is_uv) != 1 || is_uv || iv <= 0)
        croak("%s: an entity id is a positive whole number, not %" SVf, what, SVfARG(factdb_quote(aTHX_ sv)));
    if (iv >= d->next_id) croak("%s: there is no entity %" IVdf, what, iv);
    return iv;
}

/* An attribute given to a read, by name. */
static U16 factdb_read_attr(pTHX_ const factdb_db *d, SV *sv, const char *what) {
    IV a;
    if (!SvOK(sv) || SvROK(sv)) croak("%s: an attribute is named by a string", what);
    a = factdb_attr_id(aTHX_ &d->core->schema, sv);
    if (a < 0) croak("%s: %" SVf " is not in the schema", what, SVfARG(sv));
    return (U16)a;
}

typedef struct factdb_collect {
    const factdb_schema *s;
    AV *out;
} factdb_collect;

/* A datom as [e, attribute, value, tx]. */
static int factdb_collect_datom(pTHX_ void *ud, IV e, const factdb_entry *x) {
    factdb_collect *c = (factdb_collect *)ud;
    const factdb_attr *at = &c->s->attrs[x->a];
    AV *row = newAV();
    av_extend(row, 3);
    av_push(row, newSViv(e));
    av_push(row, newSVsv(at->name));
    av_push(row, factdb_decode(aTHX_ at->type, x->v, x->vlen, x->flags & FACTDB_F_UTF8));
    av_push(row, newSVuv(x->tx));
    av_push(c->out, newRV_noinc((SV *)row));
    return 0;
}

/* An entity as a new hash: db/id, then each attribute's value, a list for
 * a cardinality many one. */
static HV *factdb_entity_hv(pTHX_ const factdb_db *d, IV e) {
    const factdb_schema *s = &d->core->schema;
    HV *hv = newHV();
    SV *rec = factdb_record_of(aTHX_ d, e);
    (void)hv_stores(hv, "db/id", newSViv(e));
    if (rec) {
        STRLEN len;
        const char *p = SvPV(rec, len), *end = p + len;
        factdb_entry x;
        AV *many = NULL;
        int many_a = -1;
        while (factdb_rec_next(&p, end, &x)) {
            const factdb_attr *at = &s->attrs[x.a];
            SV *v = factdb_decode(aTHX_ at->type, x.v, x.vlen, x.flags & FACTDB_F_UTF8);
            if (at->many) {
                if (many_a != (int)x.a) {
                    many = newAV();
                    many_a = (int)x.a;
                    (void)hv_store_ent(hv, at->name, newRV_noinc((SV *)many), 0);
                }
                av_push(many, v);
            }
            else (void)hv_store_ent(hv, at->name, v, 0);
        }
    }
    return hv;
}

#endif
