#ifndef FACTDB_RECORD_H
#define FACTDB_RECORD_H

/* An entity's record: every fact it holds now, in one byte string, the
 * value EAV keeps under the entity's id. Each fact is an entry:
 *
 *     u16 attribute id, little-endian
 *     u8  flags              FACTDB_F_UTF8: the value's bytes are UTF-8
 *     varint transaction     the one that asserted it
 *     varint length
 *     the value's encoding
 *
 * Entries are kept in order of attribute, then flags, then bytes, so one
 * set of facts is always one string. A cardinality many attribute has one
 * entry a value. */

#define FACTDB_F_UTF8  0x01
#define FACTDB_F_ADDED 0x02

typedef struct factdb_entry {
    U16 a;
    U8  flags;
    UV  tx;
    const char *v;
    STRLEN vlen;
} factdb_entry;

/* An entity id as the key EAV keeps its record under: its decimal digits.
 * By hand, not by snprintf, which is too slow for every lookup. */
static STRLEN factdb_ekey(IV e, char *buf) {
    char tmp[24];
    STRLEN n = 0, i = 0;
    UV u = e < 0 ? (UV)(-(e + 1)) + 1 : (UV)e;
    do { tmp[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (e < 0) buf[i++] = '-';
    while (n) buf[i++] = tmp[--n];
    buf[i] = '\0';
    return i;
}

/* The entity id an EAV key or a set member holds: its decimal digits. */
static IV factdb_ekey_iv(const char *p, STRLEN len) {
    IV v = 0;
    STRLEN i;
    for (i = 0; i < len; i++) v = v * 10 + (p[i] - '0');
    return v;
}

static STRLEN factdb_put_varint(char *out, UV v) {
    STRLEN n = 0;
    do {
        U8 b = (U8)(v & 0x7f);
        v >>= 7;
        if (v) b |= 0x80;
        out[n++] = (char)b;
    } while (v);
    return n;
}

static int factdb_get_varint(const char **p, const char *end, UV *v) {
    UV r = 0;
    int shift = 0;
    while (*p < end) {
        U8 b = (U8)*(*p)++;
        if (shift < (int)(sizeof(UV) * 8)) r |= (UV)(b & 0x7f) << shift;
        if (!(b & 0x80)) { *v = r; return 0; }
        shift += 7;
    }
    return -1;
}

/* The next entry from *p, 1 when there was one. */
static int factdb_rec_next(const char **p, const char *end, factdb_entry *e) {
    UV len;
    if (*p + 3 > end) return 0;
    e->a = (U16)((U8)(*p)[0] | ((U8)(*p)[1] << 8));
    e->flags = (U8)(*p)[2];
    *p += 3;
    if (factdb_get_varint(p, end, &e->tx) || factdb_get_varint(p, end, &len)) return 0;
    if ((UV)(end - *p) < len) return 0;
    e->v = *p;
    e->vlen = (STRLEN)len;
    *p += len;
    return 1;
}

static void factdb_rec_put(pTHX_ SV *out, const factdb_entry *e) {
    char head[3 + 2 * 10];
    STRLEN n = 3;
    head[0] = (char)(e->a & 0xff);
    head[1] = (char)(e->a >> 8);
    head[2] = (char)(e->flags & FACTDB_F_UTF8);
    n += factdb_put_varint(head + n, e->tx);
    n += factdb_put_varint(head + n, (UV)e->vlen);
    sv_catpvn(out, head, n);
    sv_catpvn(out, e->v, e->vlen);
}

/* Order of entries: attribute, then the UTF-8 flag, then the bytes. */
static int factdb_entry_cmp(const factdb_entry *x, const factdb_entry *y) {
    int c;
    STRLEN n;
    if (x->a != y->a) return x->a < y->a ? -1 : 1;
    if ((x->flags & FACTDB_F_UTF8) != (y->flags & FACTDB_F_UTF8))
        return (x->flags & FACTDB_F_UTF8) ? 1 : -1;
    n = x->vlen < y->vlen ? x->vlen : y->vlen;
    c = memcmp(x->v, y->v, n);
    if (c) return c < 0 ? -1 : 1;
    return x->vlen == y->vlen ? 0 : (x->vlen < y->vlen ? -1 : 1);
}

/* The entry of `rec` for this attribute and value, into *found: 1 if there
 * is one. With `v` NULL, the first entry of the attribute, whatever its
 * value: the one value of a cardinality one attribute. */
static int factdb_rec_find(pTHX_ SV *rec, U16 a, U8 flags, const char *v, STRLEN vlen, factdb_entry *found) {
    STRLEN len;
    const char *p, *end;
    factdb_entry e, want;
    if (!rec) return 0;
    p = SvPV(rec, len);
    end = p + len;
    want.a = a; want.flags = flags; want.v = v; want.vlen = vlen;
    while (factdb_rec_next(&p, end, &e)) {
        if (e.a < a) continue;
        if (e.a > a) return 0;
        if (!v || factdb_entry_cmp(&e, &want) == 0) { *found = e; return 1; }
    }
    return 0;
}

/* A new record: `rec` (which may be NULL) without `drop` and with `add`,
 * either of which may be NULL. NULL when nothing is left. */
static SV *factdb_rec_edit(pTHX_ SV *rec, const factdb_entry *add, const factdb_entry *drop) {
    SV *out = newSV(rec ? SvCUR(rec) + (add ? add->vlen + 24 : 0) : (add ? add->vlen + 24 : 0));
    int placed = add == NULL;
    sv_setpvs(out, "");
    if (rec) {
        STRLEN len;
        const char *p = SvPV(rec, len), *end = p + len;
        factdb_entry e;
        while (factdb_rec_next(&p, end, &e)) {
            if (drop && factdb_entry_cmp(&e, drop) == 0) continue;
            if (!placed && factdb_entry_cmp(add, &e) < 0) {
                factdb_rec_put(aTHX_ out, add);
                placed = 1;
            }
            factdb_rec_put(aTHX_ out, &e);
        }
    }
    if (!placed) factdb_rec_put(aTHX_ out, add);
    if (!SvCUR(out)) {
        SvREFCNT_dec(out);
        return NULL;
    }
    return out;
}

#endif
