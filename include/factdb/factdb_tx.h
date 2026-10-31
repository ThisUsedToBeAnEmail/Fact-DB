#ifndef FACTDB_TX_H
#define FACTDB_TX_H

/* A transaction: data in, a new database value out. The value it starts
 * from is only read. Everything is checked before anything is applied, and
 * what is applied goes into copies held by the new value, which a mortal
 * Perl object owns from the start: a croak at any point frees it and leaves
 * every other value as it was.
 *
 *   1. read    each item becomes ops; tempids get slots, in the order seen;
 *              lookup refs, db/retractEntity and db/cas read the value the
 *              transaction starts from and become plain ops
 *   2. upsert  tempids that share a unique identity value are one entity,
 *              and that entity is the one already holding it, if one does
 *   3. ids     the transaction takes the next id, then each new entity in
 *              the order its first tempid was seen
 *   4. check   two ops that contradict each other croak
 *   5. apply   op by op, to the copies, making the datoms; then a unique
 *              value given to an entity while another held it croaks if
 *              the other still holds it at the end
 *
 * An entity id is a positive whole number below the next id to be given:
 * one the database has issued. A tempid is any other string. A lookup ref
 * is [attribute, value] of a unique attribute. */

#ifdef I_SYS_TIME
#  include <sys/time.h>
#endif

#define FACTDB_OP_ADD     1
#define FACTDB_OP_RETRACT 2

typedef struct factdb_op {
    U8  op;
    U16 a;
    IV  e;          /* > 0 an entity id; < 0 the tempid in slot -e - 1 */
    IV  vslot;      /* a ref value given as a tempid: its slot; else -1 */
    SV *enc;        /* the value's encoding, mortal */
    int utf8;
    IV  item;
} factdb_op;

typedef struct factdb_datom {
    IV  e;
    U16 a;
    U8  flags;      /* FACTDB_F_UTF8, FACTDB_F_ADDED */
    SV *enc;
} factdb_datom;

typedef struct factdb_txb {
    const char *what;
    const factdb_schema *schema;
    const factdb_db *base;
    IV next_id;
    HV *tempids;    /* name -> slot */
    AV *slots;      /* slot -> name, or undef for a hash with no db/id */
    SV *used;       /* slot -> 1 once it has stood for an entity */
    SV *ops;        /* a buffer of factdb_op */
    IV nops;
} factdb_txb;

static void factdb_txb_push(pTHX_ factdb_txb *b, const factdb_op *op) {
    SvGROW(b->ops, (STRLEN)(b->nops + 1) * sizeof(factdb_op));
    ((factdb_op *)SvPVX(b->ops))[b->nops++] = *op;
    SvCUR_set(b->ops, (STRLEN)b->nops * sizeof(factdb_op));
}

static IV factdb_txb_slot(pTHX_ factdb_txb *b, SV *name, int entity) {
    IV slot;
    if (name) {
        HE *he = hv_fetch_ent(b->tempids, name, 0, 0);
        if (he) slot = SvIV(HeVAL(he));
        else {
            slot = av_len(b->slots) + 1;
            av_push(b->slots, newSVsv(name));
            (void)hv_store_ent(b->tempids, name, newSViv(slot), 0);
        }
    }
    else {
        slot = av_len(b->slots) + 1;
        av_push(b->slots, newSV(0));
    }
    if ((STRLEN)slot >= SvCUR(b->used)) {
        STRLEN old = SvCUR(b->used);
        SvGROW(b->used, (STRLEN)slot + 2);
        Zero(SvPVX(b->used) + old, (STRLEN)slot + 1 - old, char);
        SvCUR_set(b->used, (STRLEN)slot + 1);
    }
    if (entity) SvPVX(b->used)[slot] = 1;
    return slot;
}

static const char *factdb_where(IV item, char *buf) {
    my_snprintf(buf, 32, "[%" IVdf "] ", item);
    return buf;
}

/* The entity an item names, as an op holds it. `no_tempid`, when not NULL,
 * says what refuses a tempid here. */
static IV factdb_tx_entity(pTHX_ factdb_txb *b, SV *sv, const char *no_tempid, IV item) {
    IV iv = 0;
    UV uv = 0;
    int is_uv;
    char where[32];
    if (!SvOK(sv)) croak("%s: [%" IVdf "] an entity is an entity id, a tempid or a lookup ref, not undef", b->what, item);
    if (factdb_is_list(sv)) return factdb_lookup(aTHX_ b->base, sv, b->what, factdb_where(item, where));
    if (SvROK(sv)) croak("%s: [%" IVdf "] an entity is an entity id, a tempid or a lookup ref, not a reference", b->what, item);
    if (looks_like_number(sv)) {
        if (factdb_whole(aTHX_ sv, &iv, &uv, &is_uv) != 1 || is_uv || iv <= 0)
            croak("%s: [%" IVdf "] an entity id is a positive whole number, not %" SVf,
                  b->what, item, SVfARG(factdb_quote(aTHX_ sv)));
        if (iv >= b->next_id)
            croak("%s: [%" IVdf "] there is no entity %" IVdf, b->what, item, iv);
        return iv;
    }
    if (no_tempid)
        croak("%s: [%" IVdf "] %s names an entity id or a lookup ref, not the tempid %" SVf,
              b->what, item, no_tempid, SVfARG(factdb_quote(aTHX_ sv)));
    if (!sv_len(sv)) croak("%s: [%" IVdf "] a tempid is a string with something in it", b->what, item);
    return -factdb_txb_slot(aTHX_ b, sv, 1) - 1;
}

static const factdb_attr *factdb_tx_attr(pTHX_ factdb_txb *b, SV *name, IV item, U16 *id) {
    IV a;
    STRLEN len;
    const char *p;
    if (!SvOK(name) || SvROK(name))
        croak("%s: [%" IVdf "] an attribute is named by a string", b->what, item);
    a = factdb_attr_id(aTHX_ b->schema, name);
    if (a < 0) croak("%s: [%" IVdf "] %" SVf " is not in the schema", b->what, item, SVfARG(name));
    p = SvPV(name, len);
    if (len > 3 && memEQ(p, "db/", 3))
        croak("%s: [%" IVdf "] %" SVf " is the database's own", b->what, item, SVfARG(name));
    *id = (U16)a;
    return &b->schema->attrs[a];
}

static void factdb_tx_op(pTHX_ factdb_txb *b, U8 kind, IV e, U16 a, const factdb_attr *at, SV *val, IV item) {
    factdb_op op;
    op.op = kind;
    op.a = a;
    op.e = e;
    op.vslot = -1;
    op.enc = NULL;
    op.utf8 = 0;
    op.item = item;
    if (at->type == FACTDB_T_REF && factdb_is_list(val)) {
        char where[32];
        IV target = factdb_lookup(aTHX_ b->base, val, b->what, factdb_where(item, where));
        op.enc = sv_2mortal(newSVpvf("%" IVdf, target));
    }
    else if (at->type == FACTDB_T_REF && SvOK(val) && !SvROK(val) && !looks_like_number(val)) {
        if (kind == FACTDB_OP_RETRACT)
            croak("%s: [%" IVdf "] %" SVf ": a retraction names an entity id or a lookup ref, not the tempid %" SVf,
                  b->what, item, SVfARG(at->name), SVfARG(factdb_quote(aTHX_ val)));
        op.vslot = factdb_txb_slot(aTHX_ b, val, 0);
    }
    else {
        SV *why;
        op.enc = sv_newmortal();
        why = factdb_encode(aTHX_ at->type, val, op.enc, &op.utf8);
        if (why) croak("%s: [%" IVdf "] %" SVf ": %" SVf, b->what, item, SVfARG(at->name), SVfARG(why));
    }
    factdb_txb_push(aTHX_ b, &op);
}

static void factdb_tx_read_hash(pTHX_ factdb_txb *b, HV *hv, IV item) {
    SV **id = hv_fetchs(hv, "db/id", 0);
    AV *keys = (AV *)sv_2mortal((SV *)newAV());
    HE *he;
    IV e = 0, i, n;
    int have_e = 0;

    if (id) {
        e = factdb_tx_entity(aTHX_ b, *id, NULL, item);
        have_e = 1;
    }
    hv_iterinit(hv);
    while ((he = hv_iternext(hv))) {
        SV *k = hv_iterkeysv(he);
        if (SvCUR(k) == 5 && memEQ(SvPVX(k), "db/id", 5)) continue;
        av_push(keys, newSVsv(k));
    }
    n = av_len(keys) + 1;
    if (!n) return;
    sortsv(AvARRAY(keys), (size_t)n, Perl_sv_cmp);
    if (!have_e) e = -factdb_txb_slot(aTHX_ b, NULL, 1) - 1;
    for (i = 0; i < n; i++) {
        SV *k = AvARRAY(keys)[i];
        U16 a;
        const factdb_attr *at = factdb_tx_attr(aTHX_ b, k, item, &a);
        SV *v = HeVAL(hv_fetch_ent(hv, k, 0, 0));
        if (factdb_is_list(v) && (at->many || at->type != FACTDB_T_REF)) {
            AV *list = (AV *)SvRV(v);
            IV j, m = av_len(list) + 1;
            if (!at->many)
                croak("%s: [%" IVdf "] %" SVf ": a cardinality one attribute takes one value, not a list",
                      b->what, item, SVfARG(at->name));
            for (j = 0; j < m; j++) {
                SV **el = av_fetch(list, j, 0);
                factdb_tx_op(aTHX_ b, FACTDB_OP_ADD, e, a, at, el ? *el : &PL_sv_undef, item);
            }
        }
        else factdb_tx_op(aTHX_ b, FACTDB_OP_ADD, e, a, at, v, item);
    }
}

/* db/retractEntity: a retraction of every datom of the entity, and of
 * every ref to it, as the value the transaction starts from holds them. */
typedef struct factdb_re {
    factdb_txb *b;
    IV item;
} factdb_re;

static int factdb_re_push(pTHX_ void *ud, IV e, const factdb_entry *x) {
    factdb_re *r = (factdb_re *)ud;
    factdb_op op;
    op.op = FACTDB_OP_RETRACT;
    op.a = x->a;
    op.e = e;
    op.vslot = -1;
    op.enc = sv_2mortal(newSVpvn(x->v, x->vlen));
    op.utf8 = (x->flags & FACTDB_F_UTF8) ? 1 : 0;
    op.item = r->item;
    factdb_txb_push(aTHX_ r->b, &op);
    return 0;
}

static void factdb_tx_retract_entity(pTHX_ factdb_txb *b, IV e, IV item) {
    factdb_re r;
    factdb_entry tx;
    if (factdb_rec_find(aTHX_ factdb_record_of(aTHX_ b->base, e), 0, 0, NULL, 0, &tx))
        croak("%s: [%" IVdf "] entity %" IVdf " is a transaction, which cannot be retracted", b->what, item, e);
    r.b = b;
    r.item = item;
    factdb_scan_e(aTHX_ b->base, e, -1, NULL, 0, 0, factdb_re_push, &r);
    factdb_scan_v(aTHX_ b->base, e, -1, factdb_re_push, &r);
}

/* db/cas: `new` if the attribute's value is `old` now (undef: none). */
static void factdb_tx_cas(pTHX_ factdb_txb *b, SV **f, IV item) {
    U16 a;
    const factdb_attr *at;
    IV e = factdb_tx_entity(aTHX_ b, f[1] ? f[1] : &PL_sv_undef, "db/cas", item);
    SV *old = f[3] ? f[3] : &PL_sv_undef;
    factdb_entry cur;
    int have;
    at = factdb_tx_attr(aTHX_ b, f[2] ? f[2] : &PL_sv_undef, item, &a);
    if (at->many)
        croak("%s: [%" IVdf "] %" SVf ": db/cas is for a cardinality one attribute", b->what, item, SVfARG(at->name));
    have = factdb_rec_find(aTHX_ factdb_record_of(aTHX_ b->base, e), a, 0, NULL, 0, &cur);
    {
        SV *now = have ? sv_2mortal(factdb_decode(aTHX_ at->type, cur.v, cur.vlen, cur.flags & FACTDB_F_UTF8)) : NULL;
        int same;
        if (!SvOK(old)) same = !have;
        else {
            SV *enc = sv_newmortal(), *why;
            int utf8 = 0;
            if (at->type == FACTDB_T_REF && factdb_is_list(old)) {
                char where[32];
                sv_setiv(enc, factdb_lookup(aTHX_ b->base, old, b->what, factdb_where(item, where)));
                (void)SvPV_nolen(enc);
            }
            else if ((why = factdb_encode(aTHX_ at->type, old, enc, &utf8)))
                croak("%s: [%" IVdf "] %" SVf ": %" SVf, b->what, item, SVfARG(at->name), SVfARG(why));
            same = have && (cur.flags & FACTDB_F_UTF8 ? 1 : 0) == utf8
                   && cur.vlen == SvCUR(enc) && memEQ(cur.v, SvPVX(enc), cur.vlen);
        }
        if (!same)
            croak("%s: [%" IVdf "] db/cas: %" SVf " of entity %" IVdf " is %" SVf ", not %" SVf,
                  b->what, item, SVfARG(at->name), e,
                  SVfARG(now ? factdb_quote(aTHX_ now) : sv_2mortal(newSVpvs("nothing"))),
                  SVfARG(SvOK(old) ? factdb_quote(aTHX_ old) : sv_2mortal(newSVpvs("nothing"))));
    }
    factdb_tx_op(aTHX_ b, FACTDB_OP_ADD, e, a, at, f[4] ? f[4] : &PL_sv_undef, item);
}

static void factdb_tx_read_op(pTHX_ factdb_txb *b, AV *av, IV item) {
    SV **f = AvARRAY(av);
    I32 len = av_len(av) + 1;
    const char *name;
    U8 kind;
    IV e;
    U16 a;
    const factdb_attr *at;
    name = len && f[0] && SvOK(f[0]) && !SvROK(f[0]) ? SvPV_nolen(f[0]) : "";
    if (strEQ(name, "db/retractEntity")) {
        if (len != 2) croak("%s: [%" IVdf "] db/retractEntity is [db/retractEntity, entity]", b->what, item);
        e = factdb_tx_entity(aTHX_ b, f[1] ? f[1] : &PL_sv_undef, "db/retractEntity", item);
        factdb_tx_retract_entity(aTHX_ b, e, item);
        return;
    }
    if (strEQ(name, "db/cas")) {
        if (len != 5) croak("%s: [%" IVdf "] db/cas is [db/cas, entity, attribute, old, new]", b->what, item);
        factdb_tx_cas(aTHX_ b, f, item);
        return;
    }
    if      (strEQ(name, "db/add"))     kind = FACTDB_OP_ADD;
    else if (strEQ(name, "db/retract")) kind = FACTDB_OP_RETRACT;
    else croak("%s: [%" IVdf "] unknown op %" SVf "; the ops are db/add, db/retract, db/retractEntity and db/cas",
               b->what, item, SVfARG(factdb_quote(aTHX_ len && f[0] ? f[0] : &PL_sv_undef)));
    if (len != 4)
        croak("%s: [%" IVdf "] an op is [op, entity, attribute, value]", b->what, item);
    e = factdb_tx_entity(aTHX_ b, f[1] ? f[1] : &PL_sv_undef, kind == FACTDB_OP_ADD ? NULL : "a retraction", item);
    at = factdb_tx_attr(aTHX_ b, f[2] ? f[2] : &PL_sv_undef, item, &a);
    factdb_tx_op(aTHX_ b, kind, e, a, at, f[3] ? f[3] : &PL_sv_undef, item);
}

/* ---- applying -------------------------------------------------------- */

typedef struct factdb_txa {
    const char *what;
    const factdb_schema *schema;
    factdb_db *db;
    U8 *touched;
    IV tx;
    SV *datoms;
    IV ndatoms;
    SV *conflicts;  /* a buffer of factdb_conflict */
    IV nconflicts;
} factdb_txa;

/* A unique value given to an entity while another held it. */
typedef struct factdb_conflict {
    IV  item;
    U16 a;
    IV  holder;
    SV *v;
    int utf8;
} factdb_conflict;

static void factdb_txa_datom(pTHX_ factdb_txa *t, IV e, U16 a, U8 flags, SV *enc) {
    factdb_datom d;
    d.e = e;
    d.a = a;
    d.flags = flags;
    d.enc = enc;
    t->db->ndatoms += (flags & FACTDB_F_ADDED) ? 1 : -1;
    SvGROW(t->datoms, (STRLEN)(t->ndatoms + 1) * sizeof(factdb_datom));
    ((factdb_datom *)SvPVX(t->datoms))[t->ndatoms++] = d;
    SvCUR_set(t->datoms, (STRLEN)t->ndatoms * sizeof(factdb_datom));
}

static hamt_map *factdb_txa_ave(pTHX_ factdb_txa *t, U16 a) {
    factdb_db *d = t->db;
    if (!t->touched[a]) {
        factdb_idx *old = d->ave[a];
        hamt_map *m = old ? (FACTDB_HAMT->map_copy)(aTHX_ old->m) : (FACTDB_HAMT->map_new)(aTHX);
        if (!m) croak("%s: out of memory", t->what);
        d->ave[a] = factdb_idx_new(aTHX_ m);
        factdb_idx_release(aTHX_ old);
        t->touched[a] = 1;
    }
    return d->ave[a]->m;
}

#define FACTDB_CK(t, x) STMT_START { if ((x) != HAMT_ABI_OK) croak("%s: out of memory", (t)->what); } STMT_END

static void factdb_txa_index(pTHX_ factdb_txa *t, const factdb_attr *at, U16 a, IV e,
                             const char *v, STRLEN vl, int utf8, int add) {
    hamt_map *m;
    char k[24];
    STRLEN kl;
    SV *sv;
    if (!at->indexed) return;
    m = factdb_txa_ave(aTHX_ t, a);
    if (at->unique) {
        if (add) {
            SV *ev = sv_2mortal(newSViv(e));
            FACTDB_CK(t, (FACTDB_HAMT->map_store)(aTHX_ m, v, vl, utf8, ev));
        }
        else {
            SV *held = (FACTDB_HAMT->map_fetch)(aTHX_ m, v, vl, utf8);
            if (held && SvIV(held) == e) FACTDB_CK(t, (FACTDB_HAMT->map_remove)(aTHX_ m, v, vl, utf8));
        }
        return;
    }
    kl = factdb_ekey(e, k);
    sv = (FACTDB_HAMT->map_fetch)(aTHX_ m, v, vl, utf8);
    if (add) {
        hamt_map *s = sv ? (FACTDB_HAMT->set_copy)(aTHX_ (FACTDB_HAMT->set_from_sv)(aTHX_ sv))
                         : (FACTDB_HAMT->set_new)(aTHX);
        SV *ssv;
        if (!s) croak("%s: out of memory", t->what);
        if ((FACTDB_HAMT->set_add)(aTHX_ s, k, kl, 0) != HAMT_ABI_OK) {
            (FACTDB_HAMT->set_release)(aTHX_ s);
            croak("%s: out of memory", t->what);
        }
        ssv = sv_2mortal((FACTDB_HAMT->set_to_sv)(aTHX_ s, NULL));
        FACTDB_CK(t, (FACTDB_HAMT->map_store)(aTHX_ m, v, vl, utf8, ssv));
    }
    else if (sv) {
        hamt_map *s = (FACTDB_HAMT->set_copy)(aTHX_ (FACTDB_HAMT->set_from_sv)(aTHX_ sv));
        if (!s) croak("%s: out of memory", t->what);
        if ((FACTDB_HAMT->set_remove)(aTHX_ s, k, kl, 0) != HAMT_ABI_OK) {
            (FACTDB_HAMT->set_release)(aTHX_ s);
            croak("%s: out of memory", t->what);
        }
        if ((FACTDB_HAMT->set_count)(aTHX_ s) == 0) {
            (FACTDB_HAMT->set_release)(aTHX_ s);
            FACTDB_CK(t, (FACTDB_HAMT->map_remove)(aTHX_ m, v, vl, utf8));
        }
        else {
            SV *ssv = sv_2mortal((FACTDB_HAMT->set_to_sv)(aTHX_ s, NULL));
            FACTDB_CK(t, (FACTDB_HAMT->map_store)(aTHX_ m, v, vl, utf8, ssv));
        }
    }
}

/* Notes a unique value about to be given to `e` while another entity holds
 * it. Whether that is a conflict is known only at the end: the other may
 * give it up later in the same transaction. */
static void factdb_txa_unique(pTHX_ factdb_txa *t, const factdb_attr *at, U16 a, IV e,
                              const char *v, STRLEN vl, int utf8, IV item) {
    SV *held;
    factdb_conflict c;
    hamt_map *m;
    if (!at->unique) return;
    m = factdb_ave_of(t->db, a);
    held = m ? (FACTDB_HAMT->map_fetch)(aTHX_ m, v, vl, utf8) : NULL;
    if (!held || SvIV(held) == e) return;
    c.item = item;
    c.a = a;
    c.holder = SvIV(held);
    c.v = sv_2mortal(newSVpvn(v, vl));
    c.utf8 = utf8;
    SvGROW(t->conflicts, (STRLEN)(t->nconflicts + 1) * sizeof(factdb_conflict));
    ((factdb_conflict *)SvPVX(t->conflicts))[t->nconflicts++] = c;
    SvCUR_set(t->conflicts, (STRLEN)t->nconflicts * sizeof(factdb_conflict));
}

static void factdb_txa_conflicts(pTHX_ factdb_txa *t) {
    factdb_conflict *cs = (factdb_conflict *)SvPVX(t->conflicts);
    IV i;
    for (i = 0; i < t->nconflicts; i++) {
        factdb_conflict *c = &cs[i];
        const factdb_attr *at = &t->schema->attrs[c->a];
        factdb_entry found;
        STRLEN vl;
        const char *v = SvPV(c->v, vl);
        if (factdb_rec_find(aTHX_ factdb_record_of(aTHX_ t->db, c->holder), c->a,
                            c->utf8 ? FACTDB_F_UTF8 : 0, v, vl, &found)) {
            SV *shown = sv_2mortal(factdb_decode(aTHX_ at->type, v, vl, c->utf8));
            croak("%s: [%" IVdf "] %" SVf ": %" SVf " is already held by entity %" IVdf,
                  t->what, c->item, SVfARG(at->name), SVfARG(factdb_quote(aTHX_ shown)), c->holder);
        }
    }
}

static void factdb_txa_apply(pTHX_ factdb_txa *t, const factdb_op *op) {
    const factdb_attr *at = &t->schema->attrs[op->a];
    factdb_db *d = t->db;
    char k[24];
    STRLEN kl = factdb_ekey(op->e, k), vl;
    const char *v = SvPV(op->enc, vl);
    U8 flags = op->utf8 ? FACTDB_F_UTF8 : 0;
    SV *rec = (FACTDB_HAMT->map_fetch)(aTHX_ d->eav, k, kl, 0);
    SV *edited;
    factdb_entry want, cur;
    int have;

    want.a = op->a;
    want.flags = flags;
    want.tx = (UV)t->tx;
    want.v = v;
    want.vlen = vl;

    if (op->op == FACTDB_OP_ADD) {
        if (!at->many && factdb_rec_find(aTHX_ rec, op->a, 0, NULL, 0, &cur)) {
            SV *old;
            if (factdb_entry_cmp(&cur, &want) == 0) return;
            factdb_txa_unique(aTHX_ t, at, op->a, op->e, v, vl, op->utf8, op->item);
            old = sv_2mortal(newSVpvn(cur.v, cur.vlen));
            edited = factdb_rec_edit(aTHX_ rec, &want, &cur);
            factdb_txa_datom(aTHX_ t, op->e, op->a, cur.flags & FACTDB_F_UTF8, old);
            factdb_txa_index(aTHX_ t, at, op->a, op->e, SvPVX(old), cur.vlen, cur.flags & FACTDB_F_UTF8, 0);
        }
        else {
            have = at->many && factdb_rec_find(aTHX_ rec, op->a, flags, v, vl, &cur);
            if (have) return;
            factdb_txa_unique(aTHX_ t, at, op->a, op->e, v, vl, op->utf8, op->item);
            edited = factdb_rec_edit(aTHX_ rec, &want, NULL);
        }
        factdb_txa_datom(aTHX_ t, op->e, op->a, flags | FACTDB_F_ADDED, op->enc);
        factdb_txa_index(aTHX_ t, at, op->a, op->e, v, vl, op->utf8, 1);
    }
    else {
        if (!factdb_rec_find(aTHX_ rec, op->a, flags, v, vl, &cur)) return;
        edited = factdb_rec_edit(aTHX_ rec, NULL, &cur);
        factdb_txa_datom(aTHX_ t, op->e, op->a, flags, op->enc);
        factdb_txa_index(aTHX_ t, at, op->a, op->e, v, vl, op->utf8, 0);
    }
    if (edited) {
        sv_2mortal(edited);
        FACTDB_CK(t, (FACTDB_HAMT->map_store)(aTHX_ d->eav, k, kl, 0, edited));
    }
    else FACTDB_CK(t, (FACTDB_HAMT->map_remove)(aTHX_ d->eav, k, kl, 0));
}

static NV factdb_now(void) {
#ifdef HAS_GETTIMEOFDAY
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0) return (NV)tv.tv_sec + (NV)tv.tv_usec / 1e6;
#endif
    return (NV)time(NULL);
}

/* Runs `data` against `base`. Returns the result hash, mortal, and the new
 * value in *made, which its object in the result holds. */
static HV *factdb_tx_run(pTHX_ factdb_db *base, SV *data, const char *what, factdb_db **made) {
    const factdb_schema *schema = &base->core->schema;
    factdb_txb b;
    factdb_txa t;
    factdb_db *nd;
    AV *list, *tx_data;
    HV *seen, *one, *result, *tempids = NULL;
    IV i, n, slots, tx;
    U32 j;
    SV *dsv, *buf, *logrec;
    factdb_op *ops;

    if (!SvROK(data) || SvTYPE(SvRV(data)) != SVt_PVAV || SvOBJECT(SvRV(data)))
        croak("%s: the transaction is an array reference", what);
    list = (AV *)SvRV(data);

    /* 1. read */
    b.what = what;
    b.schema = schema;
    b.base = base;
    b.next_id = base->next_id;
    b.tempids = (HV *)sv_2mortal((SV *)newHV());
    b.slots = (AV *)sv_2mortal((SV *)newAV());
    b.used = sv_2mortal(newSVpvs(""));
    b.ops = sv_2mortal(newSVpvs(""));
    b.nops = 0;
    {
        factdb_op op;
        op.op = FACTDB_OP_ADD;
        op.a = 0;
        op.e = 0;
        op.vslot = -1;
        op.utf8 = 0;
        op.item = -1;
        op.enc = sv_newmortal();
        (void)factdb_encode(aTHX_ FACTDB_T_INSTANT, sv_2mortal(newSVnv(factdb_now())), op.enc, &op.utf8);
        factdb_txb_push(aTHX_ &b, &op);
    }
    n = av_len(list) + 1;
    for (i = 0; i < n; i++) {
        SV **it = av_fetch(list, i, 0);
        SV *item = it ? *it : &PL_sv_undef;
        if (SvROK(item) && !SvOBJECT(SvRV(item)) && SvTYPE(SvRV(item)) == SVt_PVHV)
            factdb_tx_read_hash(aTHX_ &b, (HV *)SvRV(item), i);
        else if (SvROK(item) && !SvOBJECT(SvRV(item)) && SvTYPE(SvRV(item)) == SVt_PVAV)
            factdb_tx_read_op(aTHX_ &b, (AV *)SvRV(item), i);
        else
            croak("%s: [%" IVdf "] an item is an entity as a hash reference or an op as an array reference",
                  what, i);
    }

    /* 2. upsert: slots that share an identity value are one group, whose
     * entity is the one holding that value now, if any */
    ops = (factdb_op *)SvPVX(b.ops);
    slots = av_len(b.slots) + 1;
    {
        IV *parent, *holder;
        HV *by_value = (HV *)sv_2mortal((SV *)newHV());
        SV *key = sv_newmortal();
        Newx(parent, slots ? slots : 1, IV);
        SAVEFREEPV(parent);
        Newxz(holder, slots ? slots : 1, IV);
        SAVEFREEPV(holder);
        for (i = 0; i < slots; i++) parent[i] = i;
        for (i = 0; i < b.nops; i++) {
            factdb_op *op = &ops[i];
            SV **seen_slot;
            IV s, r1, r2;
            STRLEN vl;
            const char *v;
            if (op->op != FACTDB_OP_ADD || op->e >= 0 || op->vslot >= 0) continue;
            if (schema->attrs[op->a].unique != FACTDB_UNIQUE_IDENTITY) continue;
            s = -op->e - 1;
            v = SvPV(op->enc, vl);
            sv_setpvf(key, "%u:%d:", (unsigned)op->a, op->utf8);
            sv_catpvn(key, v, vl);
            seen_slot = hv_fetch(by_value, SvPVX(key), SvCUR(key), 0);
            if (!seen_slot) { (void)hv_store(by_value, SvPVX(key), SvCUR(key), newSViv(s), 0); continue; }
            for (r1 = SvIV(*seen_slot); parent[r1] != r1; r1 = parent[r1]) ;
            for (r2 = s; parent[r2] != r2; r2 = parent[r2]) ;
            if (r1 != r2) parent[r1 > r2 ? r1 : r2] = r1 < r2 ? r1 : r2;
        }
        for (i = 0; i < b.nops; i++) {
            factdb_op *op = &ops[i];
            const factdb_attr *at = &schema->attrs[op->a];
            hamt_map *ave;
            SV *held;
            IV r;
            STRLEN vl;
            const char *v;
            if (op->op != FACTDB_OP_ADD || op->e >= 0 || op->vslot >= 0) continue;
            if (at->unique != FACTDB_UNIQUE_IDENTITY) continue;
            ave = factdb_ave_of(base, op->a);
            v = SvPV(op->enc, vl);
            held = ave ? (FACTDB_HAMT->map_fetch)(aTHX_ ave, v, vl, op->utf8) : NULL;
            if (!held) continue;
            for (r = -op->e - 1; parent[r] != r; r = parent[r]) ;
            if (holder[r] && holder[r] != SvIV(held))
                croak("%s: [%" IVdf "] %" SVf ": one entity's identity values are held by two entities, %" IVdf " and %" IVdf,
                      what, op->item, SVfARG(at->name), holder[r], SvIV(held));
            holder[r] = SvIV(held);
        }

        /* 3. ids */
        tx = b.next_id++;
        tempids = (HV *)sv_2mortal((SV *)newHV());
        {
            IV *ids, *group;
            char *used = SvPVX(b.used);
            Newx(ids, slots ? slots : 1, IV);
            SAVEFREEPV(ids);
            Newxz(group, slots ? slots : 1, IV);
            SAVEFREEPV(group);
            for (i = 0; i < slots; i++) {
                IV r;
                for (r = i; parent[r] != r; r = parent[r]) ;
                parent[i] = r;
                if (used[i]) group[r] = 1;
            }
            for (i = 0; i < slots; i++) {
                IV r = parent[i];
                SV *name = AvARRAY(b.slots)[i];
                if (!group[r])
                    croak("%s: the tempid %" SVf " is used only as a value; nothing names it as an entity",
                          what, SVfARG(factdb_quote(aTHX_ name)));
                if (!holder[r]) holder[r] = -(b.next_id++);
                ids[i] = holder[r] < 0 ? -holder[r] : holder[r];
                if (SvOK(name)) (void)hv_store_ent(tempids, name, newSViv(ids[i]), 0);
            }
            for (i = 0; i < b.nops; i++) {
                factdb_op *op = &ops[i];
                if (op->item < 0) op->e = tx;
                else if (op->e < 0) op->e = ids[-op->e - 1];
                if (op->vslot >= 0) {
                    op->enc = sv_2mortal(newSVpvf("%" IVdf, ids[op->vslot]));
                    op->utf8 = 0;
                }
            }
        }
    }

    /* 4. check */
    seen = (HV *)sv_2mortal((SV *)newHV());
    one = (HV *)sv_2mortal((SV *)newHV());
    buf = sv_newmortal();
    for (i = 0; i < b.nops; i++) {
        factdb_op *op = &ops[i];
        const factdb_attr *at = &schema->attrs[op->a];
        STRLEN vl;
        const char *v = SvPV(op->enc, vl);
        SV **prev;
        sv_setpvf(buf, "%" IVdf ":%u:%d:", op->e, (unsigned)op->a, op->utf8);
        sv_catpvn(buf, v, vl);
        prev = hv_fetch(seen, SvPVX(buf), SvCUR(buf), 0);
        if (prev && SvIV(*prev) != op->op)
            croak("%s: [%" IVdf "] %" SVf ": one transaction both asserts and retracts %" SVf " of entity %" IVdf,
                  what, op->item, SVfARG(at->name),
                  SVfARG(factdb_quote(aTHX_ sv_2mortal(factdb_decode(aTHX_ at->type, v, vl, op->utf8)))), op->e);
        if (!prev) (void)hv_store(seen, SvPVX(buf), SvCUR(buf), newSViv(op->op), 0);
        if (op->op == FACTDB_OP_ADD && !at->many) {
            sv_setpvf(buf, "%" IVdf ":%u", op->e, (unsigned)op->a);
            prev = hv_fetch(one, SvPVX(buf), SvCUR(buf), 0);
            if (prev && !sv_eq(*prev, op->enc))
                croak("%s: [%" IVdf "] %" SVf ": one transaction gives entity %" IVdf
                      " two values of a cardinality one attribute", what, op->item, SVfARG(at->name), op->e);
            if (!prev) (void)hv_store(one, SvPVX(buf), SvCUR(buf), newSVsv(op->enc), 0);
        }
    }

    /* 5. apply */
    nd = (factdb_db *)factdb_alloc(aTHX_ sizeof(factdb_db));
    nd->refcnt = 1;
    nd->core = base->core;
    nd->core->refcnt++;
    dsv = factdb_db_sv(aTHX_ nd);
    factdb_db_release(aTHX_ nd);
    nd->eav = (FACTDB_HAMT->map_copy)(aTHX_ base->eav);
    nd->log = (FACTDB_HAMT->vec_copy)(aTHX_ base->log);
    nd->nave = schema->n;
    nd->ave = (factdb_idx **)factdb_alloc(aTHX_ (size_t)(nd->nave ? nd->nave : 1) * sizeof(factdb_idx *));
    nd->basis_tx = tx;
    nd->next_id = b.next_id;
    nd->ndatoms = base->ndatoms;
    factdb_db_link(nd, base);
    if (!nd->eav || !nd->log) croak("%s: out of memory", what);
    for (j = 0; j < nd->nave && j < base->nave; j++)
        if (base->ave[j]) { nd->ave[j] = base->ave[j]; nd->ave[j]->refcnt++; }

    t.what = what;
    t.schema = schema;
    t.db = nd;
    t.tx = tx;
    t.datoms = sv_2mortal(newSVpvs(""));
    t.ndatoms = 0;
    t.conflicts = sv_2mortal(newSVpvs(""));
    t.nconflicts = 0;
    Newxz(t.touched, nd->nave ? nd->nave : 1, U8);
    SAVEFREEPV(t.touched);
    for (i = 0; i < b.nops; i++) factdb_txa_apply(aTHX_ &t, &ops[i]);
    factdb_txa_conflicts(aTHX_ &t);

    /* the log, and the result */
    logrec = sv_2mortal(newSVpvs(""));
    tx_data = (AV *)sv_2mortal((SV *)newAV());
    {
        char head[24];
        factdb_datom *ds = (factdb_datom *)SvPVX(t.datoms);
        sv_catpvn(logrec, head, factdb_put_varint(head, (UV)tx));
        sv_catpvn(logrec, head, factdb_put_varint(head, (UV)t.ndatoms));
        for (i = 0; i < t.ndatoms; i++) {
            factdb_datom *dm = &ds[i];
            const factdb_attr *at = &schema->attrs[dm->a];
            STRLEN vl;
            const char *v = SvPV(dm->enc, vl);
            AV *row = newAV();
            STRLEN hn = factdb_put_varint(head, (UV)dm->e);
            head[hn++] = (char)(dm->a & 0xff);
            head[hn++] = (char)(dm->a >> 8);
            head[hn++] = (char)dm->flags;
            hn += factdb_put_varint(head + hn, (UV)vl);
            sv_catpvn(logrec, head, hn);
            sv_catpvn(logrec, v, vl);
            av_push(row, newSViv(dm->e));
            av_push(row, newSVsv(at->name));
            av_push(row, factdb_decode(aTHX_ at->type, v, vl, dm->flags & FACTDB_F_UTF8));
            av_push(row, newSViv(tx));
            av_push(row, newSViv(dm->flags & FACTDB_F_ADDED ? 1 : 0));
            av_push(tx_data, newRV_noinc((SV *)row));
        }
    }
    FACTDB_CK(&t, (FACTDB_HAMT->vec_push)(aTHX_ nd->log, logrec));

    result = (HV *)sv_2mortal((SV *)newHV());
    (void)hv_stores(result, "tx", newSViv(tx));
    (void)hv_stores(result, "tempids", newRV_inc((SV *)tempids));
    (void)hv_stores(result, "db_before", newSVsv(factdb_db_sv(aTHX_ base)));
    (void)hv_stores(result, "db_after", newSVsv(dsv));
    (void)hv_stores(result, "tx_data", newRV_inc((SV *)tx_data));
    *made = nd;
    return result;
}

#endif
