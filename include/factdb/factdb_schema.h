#ifndef FACTDB_SCHEMA_H
#define FACTDB_SCHEMA_H

/* The schema: what each attribute is. It only grows. An attribute is never
 * changed or removed, so an old database value reads with the schema as it
 * is now: an attribute added since simply has no facts in it.
 *
 * Each attribute has a small id, its place in `attrs`. A record keeps an
 * attribute by id and the per-attribute indexes are arrays by id. Id 0 is
 * db/txInstant. The ids of the attributes one call adds are given in the
 * order of their names, so the same schema always gives the same ids. */

#define FACTDB_UNIQUE_NONE     0
#define FACTDB_UNIQUE_IDENTITY 1
#define FACTDB_UNIQUE_VALUE    2

#define FACTDB_MAX_ATTRS 65535

typedef struct factdb_attr {
    SV *name;
    U8  type;
    U8  many;
    U8  unique;
    U8  indexed;
} factdb_attr;

typedef struct factdb_schema {
    factdb_attr *attrs;
    U32 n, cap;
    hamt_map *by_name;
} factdb_schema;

/* Why a name is not an attribute name, or NULL. */
static const char *factdb_name_problem(const char *p, STRLEN len) {
    STRLEN i, slash = 0, slashes = 0;
    for (i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)p[i];
        if (ch == '/') { slash = i; slashes++; continue; }
        if (ch <= ' ' || ch == 0x7f || strchr("[](){}\",;:'\\#`", ch))
            return "an attribute name has no spaces and none of [ ] ( ) { } \" , ; : ' \\ # `";
    }
    if (slashes != 1 || slash == 0 || slash == len - 1)
        return "an attribute name is a namespace, a slash and a name, as person/name";
    if (slash == 2 && memEQ(p, "db", 2))
        return "the db/ namespace is reserved";
    return NULL;
}

static IV factdb_attr_id(pTHX_ const factdb_schema *s, SV *name) {
    STRLEN len;
    const char *p = SvPV(name, len);
    SV *id = (FACTDB_HAMT->map_fetch)(aTHX_ s->by_name, p, len, SvUTF8(name) ? 1 : 0);
    return id ? SvIV(id) : -1;
}

static void factdb_schema_append(pTHX_ factdb_schema *s, const factdb_attr *a) {
    STRLEN len;
    const char *p;
    SV *id;
    if (s->n == s->cap) {
        U32 cap = s->cap ? s->cap * 2 : 16;
        factdb_attr *bigger = (factdb_attr *)factdb_alloc(aTHX_ cap * sizeof(factdb_attr));
        if (s->attrs) {
            Copy(s->attrs, bigger, s->n, factdb_attr);
            factdb_free(aTHX_ s->attrs);
        }
        s->attrs = bigger;
        s->cap = cap;
    }
    p = SvPV(a->name, len);
    id = sv_2mortal(newSViv((IV)s->n));
    if ((FACTDB_HAMT->map_store)(aTHX_ s->by_name, p, len, SvUTF8(a->name) ? 1 : 0, id) != HAMT_ABI_OK)
        croak("Fact::DB: out of memory");
    s->attrs[s->n++] = *a;
}

static void factdb_schema_init(pTHX_ factdb_schema *s) {
    factdb_attr tx;
    s->by_name = (FACTDB_HAMT->map_new)(aTHX);
    if (!s->by_name) croak("Fact::DB: out of memory");
    tx.name = newSVpvs("db/txInstant");
    SvREADONLY_on(tx.name);
    tx.type = FACTDB_T_INSTANT;
    tx.many = 0;
    tx.unique = FACTDB_UNIQUE_NONE;
    tx.indexed = 0;
    factdb_schema_append(aTHX_ s, &tx);
}

static void factdb_schema_free(pTHX_ factdb_schema *s) {
    U32 i;
    if (!PL_dirty)
        for (i = 0; i < s->n; i++) SvREFCNT_dec(s->attrs[i].name);
    if (s->attrs) factdb_free(aTHX_ s->attrs);
    if (s->by_name) (FACTDB_HAMT->map_release)(aTHX_ s->by_name);
    s->attrs = NULL;
    s->by_name = NULL;
    s->n = s->cap = 0;
}

/* One definition read into `out`. Croaks naming the attribute. */
static void factdb_parse_def(pTHX_ SV *name, SV *def, factdb_attr *out, const char *what) {
    HV *hv;
    HE *he;
    SV *type = NULL, *card = NULL, *uniq = NULL, *index = NULL;

    if (!SvROK(def) || SvTYPE(SvRV(def)) != SVt_PVHV || SvOBJECT(SvRV(def)))
        croak("%s: attribute %" SVf ": a definition is a hash reference", what, SVfARG(name));
    hv = (HV *)SvRV(def);
    hv_iterinit(hv);
    while ((he = hv_iternext(hv))) {
        SV *k = hv_iterkeysv(he);
        SV *v = hv_iterval(hv, he);
        const char *kp = SvPV_nolen(k);
        if      (strEQ(kp, "type"))        type = v;
        else if (strEQ(kp, "cardinality")) card = v;
        else if (strEQ(kp, "unique"))      uniq = v;
        else if (strEQ(kp, "index"))       index = v;
        else croak("%s: attribute %" SVf ": unknown key \"%" SVf "\"; the keys are type, cardinality, unique and index",
                   what, SVfARG(name), SVfARG(k));
    }

    out->name = name;
    out->many = 0;
    out->unique = FACTDB_UNIQUE_NONE;
    out->indexed = 0;

    if (!type || !SvOK(type) || SvROK(type))
        croak("%s: attribute %" SVf ": a type is needed: string, int, num, bool, ref or instant", what, SVfARG(name));
    {
        STRLEN len;
        const char *p = SvPV(type, len);
        int t = factdb_type_by_name(p, len);
        if (!t) croak("%s: attribute %" SVf ": unknown type \"%" SVf "\"; the types are string, int, num, bool, ref and instant",
                      what, SVfARG(name), SVfARG(type));
        out->type = (U8)t;
    }
    if (card) {
        const char *p = SvOK(card) && !SvROK(card) ? SvPV_nolen(card) : "";
        if      (strEQ(p, "one"))  out->many = 0;
        else if (strEQ(p, "many")) out->many = 1;
        else croak("%s: attribute %" SVf ": cardinality is one or many", what, SVfARG(name));
    }
    if (uniq) {
        const char *p = SvOK(uniq) && !SvROK(uniq) ? SvPV_nolen(uniq) : "";
        if      (strEQ(p, "identity")) out->unique = FACTDB_UNIQUE_IDENTITY;
        else if (strEQ(p, "value"))    out->unique = FACTDB_UNIQUE_VALUE;
        else croak("%s: attribute %" SVf ": unique is identity or value", what, SVfARG(name));
        if (out->many)
            croak("%s: attribute %" SVf ": a cardinality many attribute cannot be unique", what, SVfARG(name));
    }
    if (index && SvROK(index))
        croak("%s: attribute %" SVf ": index is true or false", what, SVfARG(name));
    out->indexed = (index && SvTRUE(index)) || out->unique || out->type == FACTDB_T_REF;
}

static int factdb_cmp_names(const void *a, const void *b) {
    const factdb_attr *x = (const factdb_attr *)a, *y = (const factdb_attr *)b;
    STRLEN xl = SvCUR(x->name), yl = SvCUR(y->name);
    int c = memcmp(SvPVX(x->name), SvPVX(y->name), xl < yl ? xl : yl);
    return c ? c : (xl < yl ? -1 : xl > yl);
}

/* Adds every attribute in `defs`, a hash reference, or none of them. */
static void factdb_schema_add(pTHX_ factdb_schema *s, SV *defs, const char *what) {
    HV *hv;
    HE *he;
    factdb_attr *add;
    I32 n = 0, i, keys;

    if (!SvROK(defs) || SvTYPE(SvRV(defs)) != SVt_PVHV || SvOBJECT(SvRV(defs)))
        croak("%s: a schema is a hash reference of attribute names to definitions", what);
    hv = (HV *)SvRV(defs);
    keys = (I32)HvUSEDKEYS(hv);
    if (!keys) return;
    if ((U32)keys > FACTDB_MAX_ATTRS - s->n)
        croak("%s: a schema holds at most %d attributes", what, FACTDB_MAX_ATTRS);

    Newxz(add, keys, factdb_attr);
    SAVEFREEPV(add);
    hv_iterinit(hv);
    while ((he = hv_iternext(hv))) {
        SV *name = sv_2mortal(newSVsv(hv_iterkeysv(he)));
        STRLEN len;
        const char *p = SvPV(name, len);
        const char *why = factdb_name_problem(p, len);
        if (why) croak("%s: attribute %" SVf ": %s", what, SVfARG(name), why);
        if (factdb_attr_id(aTHX_ s, name) >= 0)
            croak("%s: attribute %" SVf " is already in the schema", what, SVfARG(name));
        factdb_parse_def(aTHX_ name, hv_iterval(hv, he), &add[n++], what);
    }
    qsort(add, (size_t)n, sizeof(factdb_attr), factdb_cmp_names);
    for (i = 0; i < n; i++) {
        add[i].name = newSVsv(add[i].name);
        SvREADONLY_on(add[i].name);
        factdb_schema_append(aTHX_ s, &add[i]);
    }
}

/* The schema as a new hash of name to definition. */
static HV *factdb_schema_hv(pTHX_ const factdb_schema *s) {
    HV *out = newHV();
    U32 i;
    for (i = 0; i < s->n; i++) {
        const factdb_attr *a = &s->attrs[i];
        HV *def = newHV();
        (void)hv_stores(def, "type", newSVpv(factdb_type_names[a->type], 0));
        (void)hv_stores(def, "cardinality", newSVpv(a->many ? "many" : "one", 0));
        (void)hv_stores(def, "index", newSViv(a->indexed ? 1 : 0));
        if (a->unique)
            (void)hv_stores(def, "unique", newSVpv(a->unique == FACTDB_UNIQUE_IDENTITY ? "identity" : "value", 0));
        (void)hv_store_ent(out, a->name, newRV_noinc((SV *)def), 0);
    }
    return out;
}

#endif
