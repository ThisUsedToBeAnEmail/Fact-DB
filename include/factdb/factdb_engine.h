#ifndef FACTDB_ENGINE_H
#define FACTDB_ENGINE_H

/* The query engine: a parsed query run against database values.
 *
 * ---- values -----------------------------------------------------------
 *
 * A variable's value in a row is one of:
 *
 *   ENT    an entity id (from an entity place, a transaction place, a ref)
 *   ATTR   an attribute, by id in the schema it came from
 *   ENC    a value as its attribute's type keeps it: the type and its
 *          encoding, pointing into the record or log entry it was read
 *          from, which the database value holds and never changes
 *   SV     a Perl scalar: an input, or what a function returned
 *   INT, NUM, STR   a constant of the query, as it was read: no scalar is
 *          made for it unless one is asked for
 *
 * Two values are equal when they are the same as numbers (ints, nums,
 * instants, entity ids, bools and Perl numbers all compare so), or the same
 * as strings; a number and a string are never equal, and are not ordered
 * (F16). An attribute is equal to its name.
 *
 * ---- running ----------------------------------------------------------
 *
 * Rows are arrays of values, one a variable. The inputs make the first
 * rows; each clause, in order (F13), turns the rows so far into the rows
 * after it. A pattern, for each row, reads the way what that row has bound
 * picks: the entity's record, an attribute's AVE, or every record. */

#define FACTDB_V_NONE 0
#define FACTDB_V_ENT  1
#define FACTDB_V_ATTR 2
#define FACTDB_V_ENC  3
#define FACTDB_V_SV   4
#define FACTDB_V_INT  5
#define FACTDB_V_NUM  6
#define FACTDB_V_STR  7

typedef struct factdb_val {
    U8 k;
    U8 type;
    U8 utf8;
    IV iv;
    NV nv;
    const char *p;
    STRLEN len;
    SV *sv;
    const factdb_schema *sch;
} factdb_val;

typedef struct factdb_rows {
    SV *buf;
    IV n;
    U32 width;
} factdb_rows;

static void factdb_rows_init(pTHX_ factdb_rows *r, U32 width) {
    r->buf = sv_2mortal(newSVpvs(""));
    r->n = 0;
    r->width = width ? width : 1;
}

static factdb_val *factdb_row_at(const factdb_rows *r, IV i) {
    return (factdb_val *)SvPVX(r->buf) + i * r->width;
}

/* A new row at the end, a copy of `from` (or empty). Row pointers into `r`
 * taken before this are stale after it. */
static factdb_val *factdb_rows_push(pTHX_ factdb_rows *r, const factdb_val *from) {
    STRLEN need = (STRLEN)(r->n + 1) * r->width * sizeof(factdb_val);
    factdb_val *row;
    if (SvLEN(r->buf) < need + 1) SvGROW(r->buf, need * 2 + 1);
    row = (factdb_val *)SvPVX(r->buf) + r->n * r->width;
    if (from) Copy(from, row, r->width, factdb_val);
    else Zero(row, r->width, factdb_val);
    r->n++;
    SvCUR_set(r->buf, need);
    return row;
}

typedef struct factdb_run {
    const char *what;
    const factdb_query *q;
    factdb_db **src;
    U32 nsrc;
    HV *functions;
    factdb_val *consts;
    const factdb_ruleset *rules;
    HV *goals;           /* goal key -> the goal's address */
    AV *held;            /* the scalars the goals live in */
    IV answers;
    int solving;
    int depth;
} factdb_run;

static int factdb_numeric_type(int t) {
    return t == FACTDB_T_INT || t == FACTDB_T_NUM || t == FACTDB_T_INSTANT || t == FACTDB_T_REF || t == FACTDB_T_BOOL;
}

static int factdb_int_type(int t) {
    return t == FACTDB_T_INT || t == FACTDB_T_REF || t == FACTDB_T_BOOL;
}

/* ---- values to and from Perl ------------------------------------------ */

static SV *factdb_val_sv(pTHX_ const factdb_val *v) {
    switch (v->k) {
    case FACTDB_V_ENT: return sv_2mortal(newSViv(v->iv));
    case FACTDB_V_ATTR: return sv_2mortal(newSVsv(v->sch->attrs[v->iv].name));
    case FACTDB_V_ENC: return sv_2mortal(factdb_decode(aTHX_ v->type, v->p, v->len, v->utf8));
    case FACTDB_V_SV: return v->sv;
    case FACTDB_V_INT: return sv_2mortal(newSViv(v->iv));
    case FACTDB_V_NUM: return sv_2mortal(newSVnv(v->nv));
    case FACTDB_V_STR: {
        SV *sv = sv_2mortal(newSVpvn(v->p, v->len));
        if (v->utf8) SvUTF8_on(sv);
        return sv;
    }
    }
    return &PL_sv_undef;
}

static void factdb_val_of_sv(factdb_val *v, SV *sv) {
    Zero(v, 1, factdb_val);
    v->k = FACTDB_V_SV;
    v->sv = sv;
}

/* As a number: 1 with *nv (and *iv, *is_int for a whole number). */
static int factdb_val_num(pTHX_ const factdb_val *v, NV *nv, IV *iv, int *is_int) {
    *is_int = 0;
    switch (v->k) {
    case FACTDB_V_ENT: *iv = v->iv; *nv = (NV)v->iv; *is_int = 1; return 1;
    case FACTDB_V_INT: *iv = v->iv; *nv = (NV)v->iv; *is_int = 1; return 1;
    case FACTDB_V_NUM: *nv = v->nv; return 1;
    case FACTDB_V_ENC:
        if (!factdb_numeric_type(v->type)) return 0;
        if (factdb_int_type(v->type)) {
            UV uv = 0;
            int flags;
            if (v->type == FACTDB_T_BOOL) { *iv = v->len && v->p[0] == '1'; *nv = (NV)*iv; *is_int = 1; return 1; }
            flags = grok_number(v->p, v->len, &uv);
            *iv = (flags & IS_NUMBER_NEG) ? (uv == (UV)IV_MAX + 1 ? IV_MIN : -(IV)uv) : (IV)uv;
            *nv = (NV)*iv;
            *is_int = 1;
            return 1;
        }
        *nv = factdb_read_nv(aTHX_ v->p, v->len);
        return 1;
    case FACTDB_V_SV: {
        SV *sv = v->sv;
        UV uv;
        int is_uv;
        if (!SvOK(sv) || SvROK(sv) || !looks_like_number(sv)) return 0;
        if (factdb_whole(aTHX_ sv, iv, &uv, &is_uv) == 1 && !is_uv) { *nv = (NV)*iv; *is_int = 1; return 1; }
        *nv = SvNV(sv);
        return 1;
    }
    }
    return 0;
}

/* As a string, when it is one: a string attribute's value, or a Perl
 * scalar that is not a number. An attribute is its name. */
static SV *factdb_val_str(pTHX_ const factdb_val *v) {
    if (v->k == FACTDB_V_ENC && v->type == FACTDB_T_STRING) return factdb_val_sv(aTHX_ v);
    if (v->k == FACTDB_V_STR) return factdb_val_sv(aTHX_ v);
    if (v->k == FACTDB_V_ATTR) return factdb_val_sv(aTHX_ v);
    if (v->k == FACTDB_V_SV && SvOK(v->sv) && !SvROK(v->sv) && !looks_like_number(v->sv)) return v->sv;
    return NULL;
}

/* A string's bytes and flag, without a scalar, when it is one of those. */
static int factdb_val_bytes(const factdb_val *v, const char **p, STRLEN *len, int *utf8) {
    if ((v->k == FACTDB_V_ENC && v->type == FACTDB_T_STRING) || v->k == FACTDB_V_STR) {
        *p = v->p; *len = v->len; *utf8 = v->utf8;
        return 1;
    }
    return 0;
}

/* -1, 0, 1, or 2 when the two are not of one kind (F16). */
static int factdb_val_cmp(pTHX_ const factdb_val *a, const factdb_val *b) {
    NV na, nb;
    IV ia = 0, ib = 0;
    int ai, bi;
    SV *sa, *sb;
    const char *pa, *pb;
    STRLEN la, lb;
    int ua, ub;
    if (a->k == FACTDB_V_ENT && b->k == FACTDB_V_ENT) return a->iv < b->iv ? -1 : a->iv > b->iv;
    if (factdb_val_bytes(a, &pa, &la, &ua) && factdb_val_bytes(b, &pb, &lb, &ub) && ua == ub) {
        int c = memcmp(pa, pb, la < lb ? la : lb);
        if (c) return c < 0 ? -1 : 1;
        return la == lb ? 0 : (la < lb ? -1 : 1);
    }
    if (a->k == FACTDB_V_ENC && b->k == FACTDB_V_ENC && a->type == b->type && !factdb_numeric_type(a->type)) {
        if (a->len == b->len && a->utf8 == b->utf8 && memEQ(a->p, b->p, a->len)) return 0;
    }
    if (factdb_val_num(aTHX_ a, &na, &ia, &ai) && factdb_val_num(aTHX_ b, &nb, &ib, &bi)) {
        if (ai && bi) return ia < ib ? -1 : ia > ib;
        return na < nb ? -1 : na > nb;
    }
    sa = factdb_val_str(aTHX_ a);
    sb = factdb_val_str(aTHX_ b);
    if (sa && sb) {
        int c = sv_cmp(sa, sb);
        return c < 0 ? -1 : c > 0;
    }
    return 2;
}

static int factdb_val_eq(pTHX_ const factdb_val *a, const factdb_val *b) {
    return factdb_val_cmp(aTHX_ a, b) == 0;
}

/* As an entity id: any value that is a positive whole number, since
 * numbers compare as numbers whatever their type (F16). */
static int factdb_val_entity(pTHX_ const factdb_val *v, IV *e) {
    NV nv;
    int is_int;
    if (v->k == FACTDB_V_ATTR) return 0;
    if (v->k == FACTDB_V_ENC && v->type == FACTDB_T_BOOL) return 0;
    if (!factdb_val_num(aTHX_ v, &nv, e, &is_int)) return 0;
    if (!is_int) {
        if (nv != Perl_floor(nv) || nv < 1 || nv >= (NV)IV_MAX) return 0;
        *e = (IV)nv;
    }
    return *e > 0;
}

/* As an attribute of schema `s`. */
static int factdb_val_attr(pTHX_ const factdb_val *v, const factdb_schema *s, IV *a) {
    SV *name;
    if (v->k == FACTDB_V_ATTR && v->sch == s) { *a = v->iv; return 1; }
    name = factdb_val_str(aTHX_ v);
    if (!name) return 0;
    {
        STRLEN len;
        const char *p = SvPV(name, len);
        if (len && *p == ':') name = sv_2mortal(newSVpvn_flags(p + 1, len - 1, SvUTF8(name)));
    }
    *a = factdb_attr_id(aTHX_ s, name);
    return *a >= 0;
}

/* As attribute type `type` keeps it: the encoding, borrowed or mortal. */
static int factdb_val_enc(pTHX_ const factdb_val *v, int type, const char **p, STRLEN *len, int *utf8) {
    SV *out, *why, *in;
    if (v->k == FACTDB_V_ENC && v->type == type) { *p = v->p; *len = v->len; *utf8 = v->utf8; return 1; }
    if (v->k == FACTDB_V_ATTR) return 0;
    if (v->k == FACTDB_V_ENC && factdb_numeric_type(v->type) != factdb_numeric_type(type)) return 0;
    if (v->k == FACTDB_V_ENT && !factdb_numeric_type(type)) return 0;
    in = factdb_val_sv(aTHX_ v);
    if (v->k == FACTDB_V_SV && type == FACTDB_T_REF && SvROK(in)) return 0;
    out = sv_newmortal();
    why = factdb_encode(aTHX_ type, in, out, utf8);
    if (why) return 0;
    *p = SvPV(out, *len);
    return 1;
}

/* ---- the constants of a query ----------------------------------------- */

static SV *factdb_const_sv(pTHX_ const factdb_edn *c) {
    SV *sv;
    switch (c->type) {
    case FACTDB_EDN_INT: return sv_2mortal(newSViv(c->iv));
    case FACTDB_EDN_NUM: return sv_2mortal(newSVnv(c->nv));
    case FACTDB_EDN_TRUE: return sv_2mortal(newSViv(1));
    case FACTDB_EDN_FALSE: return sv_2mortal(newSViv(0));
    case FACTDB_EDN_STRING: case FACTDB_EDN_KEYWORD:
        sv = sv_2mortal(newSVpvn(c->s, c->len));
        if (c->utf8) SvUTF8_on(sv);
        return sv;
    }
    return &PL_sv_undef;
}

/* A constant of the query as a value, no scalar made. */
static void factdb_const_val(const factdb_edn *c, factdb_val *out) {
    Zero(out, 1, factdb_val);
    switch (c->type) {
    case FACTDB_EDN_INT: out->k = FACTDB_V_INT; out->iv = c->iv; return;
    case FACTDB_EDN_TRUE: out->k = FACTDB_V_INT; out->iv = 1; return;
    case FACTDB_EDN_FALSE: out->k = FACTDB_V_INT; out->iv = 0; return;
    case FACTDB_EDN_NUM: out->k = FACTDB_V_NUM; out->nv = c->nv; return;
    case FACTDB_EDN_STRING: case FACTDB_EDN_KEYWORD:
        out->k = FACTDB_V_STR; out->p = c->s; out->len = c->len; out->utf8 = c->utf8; return;
    }
}

static void factdb_term_val(pTHX_ const factdb_term *t, const factdb_val *row, factdb_val *out) {
    PERL_UNUSED_CONTEXT;
    if (t->kind == FACTDB_Q_VAR) *out = row[t->var];
    else if (t->kind == FACTDB_Q_CONST) factdb_const_val(t->c, out);
    else Zero(out, 1, factdb_val);
}

/* ---- patterns --------------------------------------------------------- */

#define FACTDB_S_BLANK 0
#define FACTDB_S_BOUND 1
#define FACTDB_S_FREE  2
#define FACTDB_S_READ  3     /* bound, and the way read already holds to it */

typedef struct factdb_pm {
    const factdb_db *d;
    const factdb_schema *s;
    const factdb_val *row;
    U32 nterms;
    U8 state[5];
    factdb_val bound[5];
    U32 slot[5];
    factdb_rows *out;
} factdb_pm;

/* Datoms tried by patterns, for the tests that say which way one read. */
static IV factdb_tried = 0;

static int factdb_pm_try(pTHX_ factdb_pm *m, IV e, U16 a, U8 flags, UV tx, const char *v, STRLEN vl, int added) {
    factdb_val cand[5];
    factdb_val *row;
    U32 i, j;
    factdb_tried++;
    Zero(cand, 5, factdb_val);
    cand[0].k = FACTDB_V_ENT; cand[0].iv = e;
    cand[1].k = FACTDB_V_ATTR; cand[1].iv = a; cand[1].sch = m->s;
    cand[2].k = FACTDB_V_ENC; cand[2].type = m->s->attrs[a].type; cand[2].p = v; cand[2].len = vl;
    cand[2].utf8 = (flags & FACTDB_F_UTF8) ? 1 : 0; cand[2].sch = m->s;
    if (cand[2].type == FACTDB_T_REF) {
        cand[2].k = FACTDB_V_ENT;
        cand[2].iv = factdb_ekey_iv(v, vl);
    }
    cand[3].k = FACTDB_V_ENT; cand[3].iv = (IV)tx;
    cand[4].k = FACTDB_V_ENC; cand[4].type = FACTDB_T_BOOL; cand[4].p = added ? "1" : "0"; cand[4].len = 1;
    for (i = 0; i < m->nterms; i++) {
        if (m->state[i] == FACTDB_S_BOUND && !factdb_val_eq(aTHX_ &m->bound[i], &cand[i])) return 0;
        if (m->state[i] == FACTDB_S_FREE)
            for (j = 0; j < i; j++)
                if (m->state[j] == FACTDB_S_FREE && m->slot[j] == m->slot[i] && !factdb_val_eq(aTHX_ &cand[j], &cand[i])) return 0;
    }
    row = factdb_rows_push(aTHX_ m->out, m->row);
    for (i = 0; i < m->nterms; i++) if (m->state[i] == FACTDB_S_FREE) row[m->slot[i]] = cand[i];
    return 0;
}

static int factdb_pm_datom(pTHX_ void *ud, IV e, const factdb_entry *x) {
    factdb_pm *m = (factdb_pm *)ud;
    return factdb_pm_try(aTHX_ m, e, x->a, x->flags, x->tx, x->v, x->vlen, 1);
}

/* From an AVE entry straight, with no record read: for a pattern that
 * names no transaction, the index holds all it needs. */
typedef struct factdb_pm_ave {
    factdb_pm *m;
    U16 a;
    const char *v;
    STRLEN vl;
    U8 flags;
} factdb_pm_ave;

static int factdb_pm_member(pTHX_ void *ud, const char *key, STRLEN klen, int utf8, SV *value) {
    factdb_pm_ave *x = (factdb_pm_ave *)ud;
    PERL_UNUSED_ARG(utf8); PERL_UNUSED_ARG(value);
    return factdb_pm_try(aTHX_ x->m, factdb_ekey_iv(key, klen), x->a, x->flags, 0, x->v, x->vl, 1);
}

static void factdb_pm_from_ave(pTHX_ factdb_pm *m, U16 a, const char *v, STRLEN vl, U8 flags) {
    hamt_map *ave = factdb_ave_of(m->d, a);
    SV *held = ave ? (FACTDB_HAMT->map_fetch)(aTHX_ ave, v, vl, flags & FACTDB_F_UTF8) : NULL;
    factdb_pm_ave x;
    if (!held) return;
    if (!SvROK(held)) { factdb_pm_try(aTHX_ m, SvIV(held), a, flags, 0, v, vl, 1); return; }
    x.m = m; x.a = a; x.v = v; x.vl = vl; x.flags = flags;
    (FACTDB_HAMT->set_walk)(aTHX_ (FACTDB_HAMT->set_from_sv)(aTHX_ held), factdb_pm_member, &x);
}

static int factdb_pm_log(pTHX_ void *ud, IV tx, IV e, U16 a, U8 flags, const char *v, STRLEN vl) {
    factdb_pm *m = (factdb_pm *)ud;
    return factdb_pm_try(aTHX_ m, e, a, flags, (UV)tx, v, vl, (flags & FACTDB_F_ADDED) ? 1 : 0);
}

/* Resolves a constant of a pattern's entity place: an id, or a lookup ref
 * (to none, when nobody holds the value). */
static int factdb_pm_const_entity(pTHX_ factdb_run *run, const factdb_db *d, const factdb_term *t, factdb_val *out) {
    const factdb_edn *c = t->c;
    Zero(out, 1, factdb_val);
    if (c->type == FACTDB_EDN_VEC) {
        SV *name = sv_2mortal(newSVpvn(c->items[0]->s, c->items[0]->len));
        IV a = factdb_attr_id(aTHX_ &d->core->schema, name);
        const factdb_attr *at;
        hamt_map *ave;
        SV *enc, *held;
        int utf8;
        STRLEN vl;
        const char *v;
        if (a < 0) factdb_at(aTHX_ run->what, c->line, c->col, ":%s is not in the schema", c->items[0]->s);
        at = &d->core->schema.attrs[a];
        if (!at->unique)
            factdb_at(aTHX_ run->what, c->line, c->col, ":%s is not unique, so a value of it does not name one entity", c->items[0]->s);
        enc = sv_newmortal();
        if (factdb_encode(aTHX_ at->type, factdb_const_sv(aTHX_ c->items[1]), enc, &utf8)) return 0;
        v = SvPV(enc, vl);
        ave = factdb_ave_of(d, (U16)a);
        held = ave ? (FACTDB_HAMT->map_fetch)(aTHX_ ave, v, vl, utf8) : NULL;
        if (!held) return 0;
        out->k = FACTDB_V_ENT;
        out->iv = SvIV(held);
        return 1;
    }
    factdb_const_val(c, out);
    return 1;
}

static void factdb_run_pattern(pTHX_ factdb_run *run, const factdb_clause *c, const factdb_rows *in, factdb_rows *out) {
    const factdb_db *d = run->src[c->src];
    const factdb_schema *s = &d->core->schema;
    IV r;
    U32 i;
    factdb_pm m;
    IV attr_const = -1;
    if (c->nterms > 1 && c->terms[1].kind == FACTDB_Q_CONST) {
        const factdb_edn *k = c->terms[1].c;
        SV *name = sv_2mortal(newSVpvn(k->s, k->len));
        if (k->utf8) SvUTF8_on(name);
        attr_const = factdb_attr_id(aTHX_ s, name);
        if (attr_const < 0) factdb_at(aTHX_ run->what, k->line, k->col, ":%s is not in the schema", k->s);
    }
    m.d = d;
    m.s = s;
    m.out = out;
    m.nterms = c->nterms;
    {
        /* The constants are the same in every row: read them once. */
        factdb_val cval[5];
        int cskip = 0;
        for (i = 0; i < c->nterms; i++) {
            const factdb_term *t = &c->terms[i];
            if (t->kind != FACTDB_Q_CONST) continue;
            if (i == 0) { if (!factdb_pm_const_entity(aTHX_ run, d, t, &cval[0])) cskip = 1; }
            else if (i == 1) { Zero(&cval[1], 1, factdb_val); cval[1].k = FACTDB_V_ATTR; cval[1].iv = attr_const; cval[1].sch = s; }
            else factdb_const_val(t->c, &cval[i]);
        }
        if (cskip) return;
    for (r = 0; r < in->n; r++) {
        const factdb_val *row = factdb_row_at(in, r);
        IV e = 0, a = -1;
        int have_e = 0, have_v = 0, utf8 = 0;
        const char *vp = NULL;
        STRLEN vl = 0;
        m.row = row;
        for (i = 0; i < c->nterms; i++) {
            const factdb_term *t = &c->terms[i];
            m.slot[i] = t->var;
            if (t->kind == FACTDB_Q_BLANK) m.state[i] = FACTDB_S_BLANK;
            else if (t->kind == FACTDB_Q_VAR && row[t->var].k == FACTDB_V_NONE) m.state[i] = FACTDB_S_FREE;
            else {
                m.state[i] = FACTDB_S_BOUND;
                m.bound[i] = t->kind == FACTDB_Q_VAR ? row[t->var] : cval[i];
            }
        }
        if (m.state[0] == FACTDB_S_BOUND) {
            if (!factdb_val_entity(aTHX_ &m.bound[0], &e)) continue;
            have_e = 1;
        }
        if (c->nterms > 1 && m.state[1] == FACTDB_S_BOUND) {
            if (c->terms[1].kind == FACTDB_Q_CONST) a = attr_const;
            else if (!factdb_val_attr(aTHX_ &m.bound[1], s, &a)) continue;
        }
        if (c->nterms > 2 && m.state[2] == FACTDB_S_BOUND && a >= 0) {
            if (!factdb_val_enc(aTHX_ &m.bound[2], s->attrs[a].type, &vp, &vl, &utf8)) continue;
            have_v = 1;
        }
        if (d->kind != FACTDB_KIND_HISTORY) {
            if (have_e) m.state[0] = FACTDB_S_READ;
            if (a >= 0 && c->nterms > 1 && m.state[1] == FACTDB_S_BOUND) m.state[1] = FACTDB_S_READ;
            if (have_v && (have_e || a >= 0)) m.state[2] = FACTDB_S_READ;
        }
        if (d->kind == FACTDB_KIND_HISTORY)
            factdb_log_each(aTHX_ d, 0, factdb_pm_log, &m);
        else if (have_e)
            factdb_scan_e(aTHX_ d, e, (int)a, have_v ? vp : NULL, vl, utf8 ? FACTDB_F_UTF8 : 0, factdb_pm_datom, &m);
        else if (a >= 0 && have_v && c->nterms <= 3 && s->attrs[a].indexed)
            factdb_pm_from_ave(aTHX_ &m, (U16)a, vp, vl, utf8 ? FACTDB_F_UTF8 : 0);
        else if (a >= 0 && have_v)
            factdb_scan_av(aTHX_ d, (U16)a, vp, vl, utf8 ? FACTDB_F_UTF8 : 0, factdb_pm_datom, &m);
        else if (a >= 0)
            factdb_scan_a(aTHX_ d, (U16)a, factdb_pm_datom, &m);
        else
            factdb_scan_all(aTHX_ d, factdb_pm_datom, &m);
    }
    }
}

/* ---- built-ins -------------------------------------------------------- */

typedef struct factdb_args {
    factdb_val v[8];
    U32 n;
    const factdb_clause *c;
    factdb_run *run;
} factdb_args;

static void factdb_args_need(pTHX_ const factdb_args *x, U32 lo, U32 hi) {
    if (x->n < lo || x->n > hi) {
        if (lo == hi) factdb_at(aTHX_ x->run->what, x->c->at->line, x->c->at->col, "%s takes %u arguments, not %u", x->c->name, (unsigned)lo, (unsigned)x->n);
        factdb_at(aTHX_ x->run->what, x->c->at->line, x->c->at->col, "%s takes %u to %u arguments, not %u", x->c->name, (unsigned)lo, (unsigned)hi, (unsigned)x->n);
    }
}

static void factdb_args_num(pTHX_ const factdb_args *x, U32 i, NV *nv, IV *iv, int *is_int) {
    if (!factdb_val_num(aTHX_ &x->v[i], nv, iv, is_int))
        factdb_at(aTHX_ x->run->what, x->c->at->line, x->c->at->col, "%s takes numbers, and %" SVf " is not one",
                  x->c->name, SVfARG(factdb_quote(aTHX_ factdb_val_sv(aTHX_ &x->v[i]))));
}

static SV *factdb_num_sv(pTHX_ NV nv, int whole) {
    if (whole && nv >= (NV)IV_MIN && nv <= (NV)IV_MAX && nv == Perl_floor(nv)) return sv_2mortal(newSViv((IV)nv));
    return sv_2mortal(newSVnv(nv));
}

/* A string's length in characters. (sv_len_utf8 reads a byte string as
 * UTF-8 before 5.16.) */
static STRLEN factdb_chars(pTHX_ SV *s) {
    STRLEN len;
    const char *p = SvPV(s, len);
    return SvUTF8(s) ? (STRLEN)utf8_length((const U8 *)p, (const U8 *)p + len) : len;
}

/* A built-in's result; NULL when the name is not a built-in. A comparison
 * of values of two kinds is false (F16). */
static SV *factdb_builtin(pTHX_ factdb_args *x, int *known) {
    const char *f = x->c->name;
    U32 i;
    *known = 1;
    if (strEQ(f, "=") || strEQ(f, "!=") || strEQ(f, "<") || strEQ(f, "<=") || strEQ(f, ">") || strEQ(f, ">=")) {
        int ok = 1;
        factdb_args_need(aTHX_ x, 2, 8);
        for (i = 0; i + 1 < x->n && ok; i++) {
            int c = factdb_val_cmp(aTHX_ &x->v[i], &x->v[i + 1]);
            if      (f[0] == '=') ok = c == 0;
            else if (f[0] == '!') ok = c != 0;
            else if (c == 2) ok = 0;
            else if (f[0] == '<') ok = f[1] ? c <= 0 : c < 0;
            else ok = f[1] ? c >= 0 : c > 0;
        }
        return sv_2mortal(newSViv(ok));
    }
    if (strEQ(f, "+") || strEQ(f, "*") || strEQ(f, "-") || strEQ(f, "/")) {
        NV acc = 0, nv;
        IV iv;
        int whole = 1, is_int;
        factdb_args_need(aTHX_ x, f[0] == '-' || f[0] == '/' ? 1 : 0, 8);
        if (f[0] == '*') acc = 1;
        for (i = 0; i < x->n; i++) {
            factdb_args_num(aTHX_ x, i, &nv, &iv, &is_int);
            if (!is_int) whole = 0;
            if (i == 0 && (f[0] == '-' || f[0] == '/')) {
                acc = nv;
                if (x->n == 1) acc = f[0] == '-' ? -nv : (nv == 0 ? 0 : 1 / nv);
                if (x->n == 1 && f[0] == '/' && nv == 0)
                    factdb_at(aTHX_ x->run->what, x->c->at->line, x->c->at->col, "/ by zero");
                continue;
            }
            if      (f[0] == '+') acc += nv;
            else if (f[0] == '*') acc *= nv;
            else if (f[0] == '-') acc -= nv;
            else {
                if (nv == 0) factdb_at(aTHX_ x->run->what, x->c->at->line, x->c->at->col, "/ by zero");
                acc /= nv;
            }
        }
        return factdb_num_sv(aTHX_ acc, whole && f[0] != '/');
    }
    if (strEQ(f, "quot") || strEQ(f, "rem") || strEQ(f, "mod")) {
        NV nv;
        IV a, b, r;
        int ia, ib;
        factdb_args_need(aTHX_ x, 2, 2);
        factdb_args_num(aTHX_ x, 0, &nv, &a, &ia);
        factdb_args_num(aTHX_ x, 1, &nv, &b, &ib);
        if (!ia || !ib) factdb_at(aTHX_ x->run->what, x->c->at->line, x->c->at->col, "%s takes whole numbers", f);
        if (b == 0) factdb_at(aTHX_ x->run->what, x->c->at->line, x->c->at->col, "%s by zero", f);
        if (f[0] == 'q') r = a / b;
        else {
            r = a % b;
            if (f[0] == 'm' && r != 0 && ((r < 0) != (b < 0))) r += b;
        }
        return sv_2mortal(newSViv(r));
    }
    if (strEQ(f, "inc") || strEQ(f, "dec")) {
        NV nv;
        IV iv;
        int is_int;
        factdb_args_need(aTHX_ x, 1, 1);
        factdb_args_num(aTHX_ x, 0, &nv, &iv, &is_int);
        return factdb_num_sv(aTHX_ f[0] == 'i' ? nv + 1 : nv - 1, is_int);
    }
    if (strEQ(f, "str")) {
        SV *out = sv_2mortal(newSVpvs(""));
        for (i = 0; i < x->n; i++) {
            SV *p = factdb_val_sv(aTHX_ &x->v[i]);
            if (SvOK(p)) sv_catsv(out, p);
        }
        return out;
    }
    if (strEQ(f, "subs")) {
        NV nv;
        IV from, to;
        int is_int;
        SV *s;
        STRLEN chars;
        factdb_args_need(aTHX_ x, 2, 3);
        s = factdb_val_str(aTHX_ &x->v[0]);
        if (!s) factdb_at(aTHX_ x->run->what, x->c->at->line, x->c->at->col, "subs takes a string first");
        chars = factdb_chars(aTHX_ s);
        factdb_args_num(aTHX_ x, 1, &nv, &from, &is_int);
        to = (IV)chars;
        if (x->n == 3) factdb_args_num(aTHX_ x, 2, &nv, &to, &is_int);
        if (from < 0 || to < from || (STRLEN)to > chars)
            factdb_at(aTHX_ x->run->what, x->c->at->line, x->c->at->col, "subs: %" IVdf " to %" IVdf " is not within a string of %" UVuf,
                      from, to, (UV)chars);
        {
            SV *copy = sv_2mortal(newSVsv(s));
            STRLEN b0 = (STRLEN)from, b1 = (STRLEN)to, len;
            const char *p;
            if (SvUTF8(copy)) {
                p = SvPV(copy, len);
                b0 = (STRLEN)(utf8_hop((U8 *)p, from) - (U8 *)p);
                b1 = (STRLEN)(utf8_hop((U8 *)p, to) - (U8 *)p);
            }
            else p = SvPV(copy, len);
            return sv_2mortal(newSVpvn_flags(p + b0, b1 - b0, SvUTF8(copy)));
        }
    }
    if (strEQ(f, "count")) {
        SV *s;
        factdb_args_need(aTHX_ x, 1, 1);
        if (x->v[0].k == FACTDB_V_SV && SvROK(x->v[0].sv) && SvTYPE(SvRV(x->v[0].sv)) == SVt_PVAV)
            return sv_2mortal(newSViv(av_len((AV *)SvRV(x->v[0].sv)) + 1));
        s = factdb_val_str(aTHX_ &x->v[0]);
        if (!s) factdb_at(aTHX_ x->run->what, x->c->at->line, x->c->at->col, "count takes a string or a list");
        return sv_2mortal(newSViv((IV)factdb_chars(aTHX_ s)));
    }
    if (strEQ(f, "ground")) {
        factdb_args_need(aTHX_ x, 1, 1);
        return factdb_val_sv(aTHX_ &x->v[0]);
    }
    if (strEQ(f, "missing?") || strEQ(f, "get-else")) {
        const factdb_term *ts = x->c->terms;
        const factdb_db *d;
        IV e, a;
        factdb_entry cur;
        int have;
        factdb_args_need(aTHX_ x, f[0] == 'm' ? 3 : 4, f[0] == 'm' ? 3 : 4);
        if (ts[0].kind != FACTDB_Q_SRC)
            factdb_at(aTHX_ x->run->what, x->c->at->line, x->c->at->col, "%s takes a source first: (%s $ ?e :attr%s)", f, f, f[0] == 'm' ? "" : " default");
        d = x->run->src[ts[0].var];
        if (d->kind == FACTDB_KIND_HISTORY)
            factdb_at(aTHX_ x->run->what, x->c->at->line, x->c->at->col, "%s reads an entity, and a history value holds no entities", f);
        if (!factdb_val_attr(aTHX_ &x->v[2], &d->core->schema, &a))
            factdb_at(aTHX_ x->run->what, x->c->at->line, x->c->at->col, "%s: %" SVf " is not an attribute in the schema", f,
                      SVfARG(factdb_quote(aTHX_ factdb_val_sv(aTHX_ &x->v[2]))));
        if (f[0] == 'g' && d->core->schema.attrs[a].many)
            factdb_at(aTHX_ x->run->what, x->c->at->line, x->c->at->col, "get-else is for a cardinality one attribute");
        have = factdb_val_entity(aTHX_ &x->v[1], &e)
               && factdb_rec_find(aTHX_ factdb_record_of(aTHX_ d, e), (U16)a, 0, NULL, 0, &cur);
        if (f[0] == 'm') return sv_2mortal(newSViv(!have));
        if (!have) return factdb_val_sv(aTHX_ &x->v[3]);
        {
            const factdb_attr *at = &d->core->schema.attrs[a];
            return sv_2mortal(factdb_decode(aTHX_ at->type, cur.v, cur.vlen, cur.flags & FACTDB_F_UTF8));
        }
    }
    *known = 0;
    return NULL;
}

/* A predicate's or function's value for one row: a built-in's, or a Perl
 * function's from `functions`, which wins over a built-in of its name. */
static SV *factdb_call_fn(pTHX_ factdb_run *run, const factdb_clause *c, const factdb_val *row) {
    factdb_args x;
    U32 i;
    SV *code = NULL;
    if (c->nterms > 8)
        factdb_at(aTHX_ run->what, c->at->line, c->at->col, "%s is given %u arguments, and 8 is the most", c->name, (unsigned)c->nterms);
    x.n = c->nterms;
    x.c = c;
    x.run = run;
    for (i = 0; i < c->nterms; i++) factdb_term_val(aTHX_ &c->terms[i], row, &x.v[i]);
    if (run->functions) {
        HE *he = hv_fetch_ent(run->functions, sv_2mortal(newSVpv(c->name, 0)), 0, 0);
        if (he) code = HeVAL(he);
    }
    if (code) {
        SV *args[8], *one, *err;
        for (i = 0; i < x.n; i++) args[i] = factdb_val_sv(aTHX_ &x.v[i]);
        err = factdb_call(aTHX_ code, args, (I32)x.n, G_SCALAR, &one, NULL);
        if (err) factdb_rethrow(aTHX_ err);
        return one;
    }
    {
        int known;
        SV *r = factdb_builtin(aTHX_ &x, &known);
        if (!known)
            factdb_at(aTHX_ run->what, c->at->line, c->at->col, "there is no function %s, built in or given in functions", c->name);
        return r;
    }
}

/* A comparison predicate's operator, when it is one and no function given
 * takes its name: then it runs without making a scalar a row. */
static int factdb_cmp_op(pTHX_ factdb_run *run, const factdb_clause *c) {
    const char *f = c->name;
    if (c->nterms != 2) return 0;
    if (run->functions && hv_exists(run->functions, f, (I32)strlen(f))) return 0;
    if (strEQ(f, "=")) return 1;
    if (strEQ(f, "!=")) return 2;
    if (strEQ(f, "<")) return 3;
    if (strEQ(f, "<=")) return 4;
    if (strEQ(f, ">")) return 5;
    if (strEQ(f, ">=")) return 6;
    return 0;
}

static void factdb_run_pred(pTHX_ factdb_run *run, const factdb_clause *c, const factdb_rows *in, factdb_rows *out) {
    IV r;
    int op = factdb_cmp_op(aTHX_ run, c);
    if (op) {
        factdb_val a, b;
        for (r = 0; r < in->n; r++) {
            const factdb_val *row = factdb_row_at(in, r);
            int cmp, ok;
            factdb_term_val(aTHX_ &c->terms[0], row, &a);
            factdb_term_val(aTHX_ &c->terms[1], row, &b);
            cmp = factdb_val_cmp(aTHX_ &a, &b);
            switch (op) {
            case 1: ok = cmp == 0; break;
            case 2: ok = cmp != 0; break;
            case 3: ok = cmp == -1; break;
            case 4: ok = cmp == -1 || cmp == 0; break;
            case 5: ok = cmp == 1; break;
            default: ok = cmp == 1 || cmp == 0; break;
            }
            if (ok) factdb_rows_push(aTHX_ out, row);
        }
        return;
    }
    for (r = 0; r < in->n; r++) {
        SV *res = factdb_call_fn(aTHX_ run, c, factdb_row_at(in, r));
        if (SvTRUE(res)) factdb_rows_push(aTHX_ out, factdb_row_at(in, r));
    }
}

/* Binds slot `t` of a copy of `row` to `sv`, or keeps the row only if the
 * slot already holds a value equal to it. */
static void factdb_bind_one(pTHX_ factdb_rows *out, const factdb_val *row, const factdb_term *t, U32 nt, SV **svs) {
    factdb_val *nr;
    U32 i;
    for (i = 0; i < nt; i++) {
        factdb_val v;
        if (t[i].kind != FACTDB_Q_VAR) continue;
        if (!SvOK(svs[i])) return;
        if (row[t[i].var].k != FACTDB_V_NONE) {
            factdb_val_of_sv(&v, svs[i]);
            if (!factdb_val_eq(aTHX_ &row[t[i].var], &v)) return;
        }
    }
    nr = factdb_rows_push(aTHX_ out, row);
    for (i = 0; i < nt; i++)
        if (t[i].kind == FACTDB_Q_VAR && nr[t[i].var].k == FACTDB_V_NONE) {
            SV *keep = sv_2mortal(newSVsv(svs[i]));
            factdb_val_of_sv(&nr[t[i].var], keep);
        }
}

static AV *factdb_list_of(pTHX_ factdb_run *run, const factdb_clause *c, SV *res, const char *what_kind) {
    if (!SvROK(res) || SvTYPE(SvRV(res)) != SVt_PVAV)
        factdb_at(aTHX_ run->what, c->at->line, c->at->col, "%s binds %s, and needs an array reference from %s",
                  c->name, what_kind, c->name);
    return (AV *)SvRV(res);
}

static void factdb_run_fn(pTHX_ factdb_run *run, const factdb_clause *c, const factdb_rows *in, factdb_rows *out) {
    IV r;
    for (r = 0; r < in->n; r++) {
        const factdb_val *row = factdb_row_at(in, r);
        SV *res = factdb_call_fn(aTHX_ run, c, row);
        SV *one[8];
        if (!SvOK(res)) continue;
        if (c->bkind == FACTDB_B_SCALAR) {
            one[0] = res;
            factdb_bind_one(aTHX_ out, factdb_row_at(in, r), c->bind, 1, one);
        }
        else if (c->bkind == FACTDB_B_COLL) {
            AV *list = factdb_list_of(aTHX_ run, c, res, "a list");
            IV i, n = av_len(list) + 1;
            for (i = 0; i < n; i++) {
                SV **el = av_fetch(list, i, 0);
                one[0] = el ? *el : &PL_sv_undef;
                factdb_bind_one(aTHX_ out, factdb_row_at(in, r), c->bind, 1, one);
            }
        }
        else {
            AV *list = factdb_list_of(aTHX_ run, c, res, c->bkind == FACTDB_B_TUPLE ? "a tuple" : "a relation");
            IV t, nt = c->bkind == FACTDB_B_TUPLE ? 1 : av_len(list) + 1;
            for (t = 0; t < nt; t++) {
                AV *tuple = list;
                U32 i;
                if (c->bkind == FACTDB_B_REL) {
                    SV **el = av_fetch(list, t, 0);
                    if (!el || !SvROK(*el) || SvTYPE(SvRV(*el)) != SVt_PVAV)
                        factdb_at(aTHX_ run->what, c->at->line, c->at->col, "%s binds a relation, and needs an array reference of array references", c->name);
                    tuple = (AV *)SvRV(*el);
                }
                if ((U32)(av_len(tuple) + 1) != c->nbind)
                    factdb_at(aTHX_ run->what, c->at->line, c->at->col, "%s gave a tuple of %" IVdf " for a binding of %u",
                              c->name, (IV)(av_len(tuple) + 1), (unsigned)c->nbind);
                for (i = 0; i < c->nbind && i < 8; i++) {
                    SV **el = av_fetch(tuple, i, 0);
                    one[i] = el ? *el : &PL_sv_undef;
                }
                factdb_bind_one(aTHX_ out, factdb_row_at(in, r), c->bind, c->nbind, one);
            }
        }
    }
}

/* ---- not, or, rules ---------------------------------------------------- */

static void factdb_run_clauses(pTHX_ factdb_run *run, const factdb_clause *cs, U32 n, factdb_rows *rows);

/* The rows a group of clauses makes from one row: the row itself, or a row
 * of only `join`'s variables when the clause names them. */
static void factdb_run_from(pTHX_ factdb_run *run, const factdb_clause *c, const factdb_val *row, U32 width,
                            const factdb_clause *cs, U32 n, factdb_rows *sub) {
    factdb_val *start;
    U32 v;
    factdb_rows_init(aTHX_ sub, width);
    start = factdb_rows_push(aTHX_ sub, c->has_join ? NULL : row);
    if (c->has_join)
        for (v = 0; v < c->njoin; v++) start[c->join[v].var] = row[c->join[v].var];
    factdb_run_clauses(aTHX_ run, cs, n, sub);
}

/* not: a row stays when its clauses find nothing for it. */
static void factdb_run_not(pTHX_ factdb_run *run, const factdb_clause *c, const factdb_rows *in, factdb_rows *out) {
    IV r;
    for (r = 0; r < in->n; r++) {
        factdb_rows sub;
        factdb_run_from(aTHX_ run, c, factdb_row_at(in, r), in->width, c->body, c->nbody, &sub);
        if (!sub.n) factdb_rows_push(aTHX_ out, factdb_row_at(in, r));
    }
}

/* or: the rows each branch makes, together. An or-join takes from a branch
 * only its join variables; the rest of what the branch bound stays in it. */
static void factdb_run_or(pTHX_ factdb_run *run, const factdb_clause *c, const factdb_rows *in, factdb_rows *out) {
    IV r, k;
    U32 b, v;
    for (r = 0; r < in->n; r++)
        for (b = 0; b < c->nbranches; b++) {
            factdb_rows sub;
            factdb_run_from(aTHX_ run, c, factdb_row_at(in, r), in->width, c->branches[b].clauses, c->branches[b].n, &sub);
            for (k = 0; k < sub.n; k++) {
                const factdb_val *got = factdb_row_at(&sub, k);
                if (c->has_join) {
                    factdb_val *nr = factdb_rows_push(aTHX_ out, factdb_row_at(in, r));
                    for (v = 0; v < c->njoin; v++)
                        if (nr[c->join[v].var].k == FACTDB_V_NONE) nr[c->join[v].var] = got[c->join[v].var];
                }
                else factdb_rows_push(aTHX_ out, got);
            }
        }
}

/* ---- rules: tabled, top down -------------------------------------------
 *
 * A call of a rule is a goal: the rule, which arguments it has bound, and
 * their values. A goal's answers are kept in a table for the whole query,
 * so each goal is worked out once, and only the goals a query reaches are:
 * ancestors of one person never computes ancestors of everyone.
 *
 * A goal being worked out when it is called again (recursion) gives the
 * answers it has so far. The outermost call works its goal out again and
 * again until no goal anywhere gained an answer; then every goal it reached
 * is complete. A cycle in the data ends the same way: the answers stop
 * growing. */

static void factdb_val_key(pTHX_ SV *key, const factdb_val *v);

#define FACTDB_RULE_DEPTH    1000
#define FACTDB_RULE_ANSWERS  1000000
#define FACTDB_RULE_ROUNDS   1000

typedef struct factdb_goal {
    const char *name;
    U32 arity;
    factdb_val *bound;   /* the bound arguments, NONE for a free one */
    factdb_rows answers;
    HV *seen;
    int active, complete;
} factdb_goal;

static void factdb_eval_goal(pTHX_ factdb_run *run, factdb_goal *g, const factdb_clause *at);

static void factdb_solve(pTHX_ factdb_run *run, factdb_goal *g, const factdb_clause *at) {
    int top;
    if (g->complete || g->active) return;
    top = !run->solving;
    if (top) {
        IV before;
        int rounds = 0;
        run->solving = 1;
        do {
            if (++rounds > FACTDB_RULE_ROUNDS)
                factdb_at(aTHX_ run->what, at->at->line, at->at->col, "rule %s did not settle in %d rounds; it may grow without end",
                          g->name, FACTDB_RULE_ROUNDS);
            before = run->answers;
            factdb_eval_goal(aTHX_ run, g, at);
        } while (run->answers != before);
        {
            HE *he;
            hv_iterinit(run->goals);
            while ((he = hv_iternext(run->goals))) (INT2PTR(factdb_goal *, SvIV(HeVAL(he))))->complete = 1;
        }
        run->solving = 0;
    }
    else factdb_eval_goal(aTHX_ run, g, at);
}

static void factdb_eval_goal(pTHX_ factdb_run *run, factdb_goal *g, const factdb_clause *at) {
    const factdb_ruleset *rs = run->rules;
    U32 i, p;
    SV *key = sv_newmortal();
    if (++run->depth > FACTDB_RULE_DEPTH)
        factdb_at(aTHX_ run->what, at->at->line, at->at->col, "rule %s calls itself more than %d deep", g->name, FACTDB_RULE_DEPTH);
    g->active = 1;
    for (i = 0; i < rs->n; i++) {
        const factdb_rule *r = &rs->rules[i];
        factdb_rows rows;
        factdb_val *start;
        IV k;
        int clash = 0;
        if (!strEQ(r->name, g->name)) continue;
        factdb_rows_init(aTHX_ &rows, r->nvars);
        start = factdb_rows_push(aTHX_ &rows, NULL);
        for (p = 0; p < r->nparams; p++) {
            factdb_val *slot = &start[r->params[p].var];
            if (g->bound[p].k == FACTDB_V_NONE) continue;
            if (slot->k != FACTDB_V_NONE && !factdb_val_eq(aTHX_ slot, &g->bound[p])) clash = 1;
            *slot = g->bound[p];
        }
        if (clash) continue;
        factdb_run_clauses(aTHX_ run, r->body, r->nbody, &rows);
        for (k = 0; k < rows.n; k++) {
            const factdb_val *row = factdb_row_at(&rows, k);
            factdb_val *ans;
            sv_setpvs(key, "");
            for (p = 0; p < r->nparams; p++) factdb_val_key(aTHX_ key, &row[r->params[p].var]);
            if (hv_exists(g->seen, SvPVX(key), (I32)SvCUR(key))) continue;
            (void)hv_store(g->seen, SvPVX(key), (I32)SvCUR(key), &PL_sv_yes, 0);
            ans = factdb_rows_push(aTHX_ &g->answers, NULL);
            for (p = 0; p < r->nparams; p++) ans[p] = row[r->params[p].var];
            if (++run->answers > FACTDB_RULE_ANSWERS)
                factdb_at(aTHX_ run->what, at->at->line, at->at->col, "rule %s made more than %d answers; it may grow without end",
                          g->name, FACTDB_RULE_ANSWERS);
        }
    }
    g->active = 0;
    run->depth--;
}

static factdb_goal *factdb_goal_of(pTHX_ factdb_run *run, const factdb_clause *c, const factdb_val *args) {
    SV *key = sv_2mortal(newSVpv(c->name, 0));
    HE *he;
    factdb_goal *g;
    U32 i;
    sv_catpvs(key, "\0");
    for (i = 0; i < c->nterms; i++) {
        if (args[i].k == FACTDB_V_NONE) sv_catpvs(key, "_|");
        else factdb_val_key(aTHX_ key, &args[i]);
    }
    he = hv_fetch_ent(run->goals, key, 0, 0);
    if (he) return INT2PTR(factdb_goal *, SvIV(HeVAL(he)));
    {
        SV *holder = sv_2mortal(newSV(sizeof(factdb_goal) + (c->nterms + 1) * sizeof(factdb_val)));
        g = (factdb_goal *)SvPVX(holder);
        Zero(g, 1, factdb_goal);
        g->bound = (factdb_val *)(g + 1);
        av_push(run->held, SvREFCNT_inc_simple_NN(holder));
        (void)hv_store_ent(run->goals, key, newSViv(PTR2IV(g)), 0);
    }
    g->name = c->name;
    g->arity = c->nterms;
    Copy(args, g->bound, c->nterms, factdb_val);
    factdb_rows_init(aTHX_ &g->answers, c->nterms);
    g->seen = (HV *)sv_2mortal((SV *)newHV());
    return g;
}

static void factdb_run_rule(pTHX_ factdb_run *run, const factdb_clause *c, const factdb_rows *in, factdb_rows *out) {
    IV r;
    U32 i, j;
    factdb_val args[16];
    if (c->nterms > 16)
        factdb_at(aTHX_ run->what, c->at->line, c->at->col, "a rule of more than 16 arguments is not supported");
    for (r = 0; r < in->n; r++) {
        factdb_goal *g;
        IV k;
        for (i = 0; i < c->nterms; i++) factdb_term_val(aTHX_ &c->terms[i], factdb_row_at(in, r), &args[i]);
        g = factdb_goal_of(aTHX_ run, c, args);
        factdb_solve(aTHX_ run, g, c);
        for (k = 0; k < g->answers.n; k++) {
            const factdb_val *ans = factdb_row_at(&g->answers, k);
            const factdb_val *row = factdb_row_at(in, r);
            factdb_val *nr;
            int ok = 1;
            for (i = 0; i < c->nterms && ok; i++) {
                if (args[i].k != FACTDB_V_NONE) continue;
                for (j = 0; j < i; j++)
                    if (args[j].k == FACTDB_V_NONE && c->terms[j].kind == FACTDB_Q_VAR && c->terms[i].kind == FACTDB_Q_VAR
                        && c->terms[j].var == c->terms[i].var && !factdb_val_eq(aTHX_ &ans[i], &ans[j])) ok = 0;
            }
            if (!ok) continue;
            nr = factdb_rows_push(aTHX_ out, row);
            for (i = 0; i < c->nterms; i++)
                if (args[i].k == FACTDB_V_NONE && c->terms[i].kind == FACTDB_Q_VAR) nr[c->terms[i].var] = ans[i];
        }
    }
}

/* ---- the whole query -------------------------------------------------- */

static void factdb_run_clauses(pTHX_ factdb_run *run, const factdb_clause *cs, U32 n, factdb_rows *rows) {
    U32 i;
    for (i = 0; i < n; i++) {
        const factdb_clause *c = &cs[i];
        factdb_rows next;
        factdb_rows_init(aTHX_ &next, rows->width);
        switch (c->kind) {
        case FACTDB_C_PATTERN: factdb_run_pattern(aTHX_ run, c, rows, &next); break;
        case FACTDB_C_PRED: factdb_run_pred(aTHX_ run, c, rows, &next); break;
        case FACTDB_C_FN:
            if (c->nbind > 8) factdb_at(aTHX_ run->what, c->at->line, c->at->col, "a binding of more than 8 is not supported");
            factdb_run_fn(aTHX_ run, c, rows, &next);
            break;
        case FACTDB_C_RULE: factdb_run_rule(aTHX_ run, c, rows, &next); break;
        case FACTDB_C_NOT: factdb_run_not(aTHX_ run, c, rows, &next); break;
        case FACTDB_C_OR: factdb_run_or(aTHX_ run, c, rows, &next); break;
        }
        *rows = next;
        if (!rows->n) return;
    }
}

/* The first rows: one, then each input's values. `given` has one scalar
 * for each input of :in, in its order. */
static void factdb_run_inputs(pTHX_ factdb_run *run, SV **given, factdb_rows *rows) {
    const factdb_query *q = run->q;
    U32 i;
    factdb_rows_push(aTHX_ rows, NULL);
    for (i = 0; i < q->nin; i++) {
        const factdb_input *in = &q->in[i];
        SV *arg;
        factdb_rows next;
        IV r, k, nk;
        if (in->kind == FACTDB_I_SRC || in->kind == FACTDB_I_RULES) continue;
        arg = given[i];
        factdb_rows_init(aTHX_ &next, rows->width);
        if (in->kind == FACTDB_I_SCALAR) {
            if (!SvOK(arg))
                factdb_at(aTHX_ run->what, in->at->line, in->at->col, "the input for %s is undef", q->vars[in->vars[0].var]);
            for (r = 0; r < rows->n; r++) {
                factdb_val *nr = factdb_rows_push(aTHX_ &next, factdb_row_at(rows, r));
                factdb_val_of_sv(&nr[in->vars[0].var], arg);
            }
        }
        else {
            AV *list;
            if (!SvROK(arg) || SvTYPE(SvRV(arg)) != SVt_PVAV)
                factdb_at(aTHX_ run->what, in->at->line, in->at->col, "this input takes an array reference");
            list = (AV *)SvRV(arg);
            nk = in->kind == FACTDB_I_TUPLE ? 1 : av_len(list) + 1;
            for (r = 0; r < rows->n; r++)
                for (k = 0; k < nk; k++) {
                    factdb_val *nr;
                    AV *tuple = list;
                    U32 v;
                    SV **el;
                    if (in->kind == FACTDB_I_COLL) {
                        el = av_fetch(list, k, 0);
                        if (!el || !SvOK(*el)) continue;
                        nr = factdb_rows_push(aTHX_ &next, factdb_row_at(rows, r));
                        factdb_val_of_sv(&nr[in->vars[0].var], *el);
                        continue;
                    }
                    if (in->kind == FACTDB_I_REL) {
                        el = av_fetch(list, k, 0);
                        if (!el || !SvROK(*el) || SvTYPE(SvRV(*el)) != SVt_PVAV)
                            factdb_at(aTHX_ run->what, in->at->line, in->at->col, "a relation input is an array reference of array references");
                        tuple = (AV *)SvRV(*el);
                    }
                    if ((U32)(av_len(tuple) + 1) != in->nvars)
                        factdb_at(aTHX_ run->what, in->at->line, in->at->col, "this input binds %u, and was given a tuple of %" IVdf,
                                  (unsigned)in->nvars, (IV)(av_len(tuple) + 1));
                    nr = factdb_rows_push(aTHX_ &next, factdb_row_at(rows, r));
                    for (v = 0; v < in->nvars; v++) {
                        el = av_fetch(tuple, v, 0);
                        if (in->vars[v].kind != FACTDB_Q_VAR) continue;
                        if (!el || !SvOK(*el))
                            factdb_at(aTHX_ run->what, in->at->line, in->at->col, "the input for %s is undef", q->vars[in->vars[v].var]);
                        factdb_val_of_sv(&nr[in->vars[v].var], *el);
                    }
                }
        }
        *rows = next;
    }
}

/* A value as a key for removing duplicates. */
static void factdb_val_key(pTHX_ SV *key, const factdb_val *v) {
    NV nv;
    IV iv;
    int is_int;
    if (v->k != FACTDB_V_ATTR && factdb_val_num(aTHX_ v, &nv, &iv, &is_int)) {
        if (!is_int && nv == Perl_floor(nv) && nv >= (NV)IV_MIN && nv < (NV)IV_MAX) { iv = (IV)nv; is_int = 1; }
        if (is_int) sv_catpvf(key, "i%" IVdf, iv);
        else {
            char buf[64];
            STRLEN n = factdb_canonical_nv(aTHX_ buf, sizeof buf, nv);
            sv_catpvs(key, "n");
            sv_catpvn(key, buf, n);
        }
    }
    else {
        SV *s = v->k == FACTDB_V_ATTR ? factdb_val_sv(aTHX_ v) : factdb_val_str(aTHX_ v);
        if (!s) s = factdb_val_sv(aTHX_ v);
        if (SvROK(s)) sv_catpvf(key, "r%p", (void *)SvRV(s));
        else {
            STRLEN len;
            const char *p;
            SV *c = sv_2mortal(newSVsv(s));
            sv_utf8_upgrade(c);
            p = SvPV(c, len);
            sv_catpvf(key, "s%" UVuf ":", (UV)len);
            sv_catpvn(key, p, len);
        }
    }
    sv_catpvs(key, "|");
}

static SV *factdb_project_agg(pTHX_ factdb_run *run, const factdb_rows *rows);

/* The result, as the :find form asks (F14): duplicates removed, in no
 * promised order. */
static SV *factdb_project(pTHX_ factdb_run *run, const factdb_rows *rows) {
    const factdb_query *q = run->q;
    HV *seen = (HV *)sv_2mortal((SV *)newHV());
    AV *out = (AV *)sv_2mortal((SV *)newAV());
    SV *key = sv_newmortal();
    IV r;
    U32 i;
    for (i = 0; i < q->nfind; i++)
        if (q->find[i].is_agg) return factdb_project_agg(aTHX_ run, rows);
    for (r = 0; r < rows->n; r++) {
        const factdb_val *row = factdb_row_at(rows, r);
        sv_setpvs(key, "");
        for (i = 0; i < q->nfind; i++) factdb_val_key(aTHX_ key, &row[q->find[i].var]);
        if (hv_exists(seen, SvPVX(key), (I32)SvCUR(key))) continue;
        (void)hv_store(seen, SvPVX(key), (I32)SvCUR(key), &PL_sv_yes, 0);
        if (q->find_kind == FACTDB_F_COLL) av_push(out, newSVsv(factdb_val_sv(aTHX_ &row[q->find[0].var])));
        else if (q->find_kind == FACTDB_F_SCALAR) return newSVsv(factdb_val_sv(aTHX_ &row[q->find[0].var]));
        else {
            AV *t = newAV();
            for (i = 0; i < q->nfind; i++) av_push(t, newSVsv(factdb_val_sv(aTHX_ &row[q->find[i].var])));
            if (q->find_kind == FACTDB_F_TUPLE) return newRV_noinc((SV *)t);
            av_push(out, newRV_noinc((SV *)t));
        }
    }
    if (q->find_kind == FACTDB_F_SCALAR || q->find_kind == FACTDB_F_TUPLE) return newSV(0);
    return newRV_inc((SV *)out);
}

#endif
