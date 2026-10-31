#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

/* The only thing this file knows about Fact::DB. */
#include "fact_db_abi.h"

static const fact_db_abi *FDB = NULL;

#define NEED_ABI(what) do { if (!FDB) croak("%s: no Fact::DB ABI", what); } while (0)

/* Pushes a copy of each value it is handed onto the AV in ud. */
static int push_row(pTHX_ void *ud, SV **values, U32 n) {
    AV *row;
    U32 i;
    if (n == 1) { av_push((AV *)ud, newSVsv(values[0])); return 0; }
    row = newAV();
    for (i = 0; i < n; i++) av_push(row, newSVsv(values[i]));
    av_push((AV *)ud, newRV_noinc((SV *)row));
    return 0;
}

static int push_pair(pTHX_ void *ud, SV *attr, SV *value) {
    AV *pair = newAV();
    av_push(pair, newSVsv(attr));
    av_push(pair, newSVsv(value));
    av_push((AV *)ud, newRV_noinc((SV *)pair));
    return 0;
}

static int push_datom(pTHX_ void *ud, IV e, SV *attr, SV *value, IV tx, int added) {
    AV *d = newAV();
    av_push(d, newSViv(e));
    av_push(d, newSVsv(attr));
    av_push(d, newSVsv(value));
    av_push(d, newSViv(tx));
    av_push(d, newSViv(added));
    av_push((AV *)ud, newRV_noinc((SV *)d));
    return 0;
}

static int stop_after(pTHX_ void *ud, SV **values, U32 n) {
    PERL_UNUSED_CONTEXT;
    PERL_UNUSED_ARG(values);
    PERL_UNUSED_ARG(n);
    return --*(IV *)ud <= 0;
}

static int croaks(pTHX_ void *ud, SV *attr, SV *value) {
    PERL_UNUSED_ARG(ud);
    PERL_UNUSED_ARG(value);
    croak("a consumer callback croaked at %" SVf "\n", SVfARG(attr));
    return 0;
}

static SV *error_now(pTHX) {
    SV *e = (FDB->last_error)(aTHX);
    return e ? newSVsv(e) : newSV(0);
}

/* A transaction from a list of [op, e, a, v] array references: op "add",
 * "retract", "cas" (with a fifth, the new value), "retractEntity" (only e),
 * or a hash reference, an entity. */
static fact_db_txn *build(pTHX_ AV *ops) {
    fact_db_txn *t = (FDB->txn_new)(aTHX);
    I32 i, n = av_len(ops) + 1;
    for (i = 0; i < n; i++) {
        SV *op = *av_fetch(ops, i, 0);
        AV *o;
        const char *kind;
        if (SvROK(op) && SvTYPE(SvRV(op)) == SVt_PVHV) {
            (FDB->txn_entity)(aTHX_ t, (HV *)SvRV(op));
            continue;
        }
        o = (AV *)SvRV(op);
        kind = SvPV_nolen(*av_fetch(o, 0, 0));
        if (strEQ(kind, "retractEntity")) (FDB->txn_retract_entity)(aTHX_ t, *av_fetch(o, 1, 0));
        else if (strEQ(kind, "cas"))
            (FDB->txn_cas)(aTHX_ t, *av_fetch(o, 1, 0), *av_fetch(o, 2, 0), *av_fetch(o, 3, 0), *av_fetch(o, 4, 0));
        else
            (FDB->txn_add)(aTHX_ t, strEQ(kind, "add") ? FACT_DB_OP_ADD : FACT_DB_OP_RETRACT,
                           *av_fetch(o, 1, 0), *av_fetch(o, 2, 0), *av_fetch(o, 3, 0));
    }
    return t;
}

MODULE = Fact::DB::ABI::Consumer    PACKAGE = Fact::DB::ABI::Consumer

PROTOTYPES: DISABLE

BOOT:
{
    SV *err;
    SV *sv = eval_pv("require Fact::DB; Fact::DB::_abi_ptr()", 0);
    err = get_sv("@", 0);
    if (sv && SvOK(sv) && (!err || !SvTRUE(err))) {
        const fact_db_abi *t = INT2PTR(const fact_db_abi *, SvUV(sv));
        if (t && t->abi_version >= FACT_DB_ABI_VERSION) FDB = t;
    }
}

IV
abi_version()
    CODE:
        NEED_ABI("abi_version");
        RETVAL = FDB->abi_version;
    OUTPUT:
        RETVAL

# A connection with the people schema: name (identity), age (indexed int),
# friend (many refs).
SV *
make()
    CODE:
        NEED_ABI("make");
        RETVAL = (FDB->conn_new)(aTHX);
        (FDB->add_attr)(aTHX_ RETVAL, sv_2mortal(newSVpvs("person/name")), FACT_DB_T_STRING, 0, FACT_DB_UNIQUE_IDENTITY, 0);
        (FDB->add_attr)(aTHX_ RETVAL, sv_2mortal(newSVpvs("person/age")), FACT_DB_T_INT, 0, FACT_DB_UNIQUE_NONE, 1);
        (FDB->add_attr)(aTHX_ RETVAL, sv_2mortal(newSVpvs("person/friend")), FACT_DB_T_REF, 1, FACT_DB_UNIQUE_NONE, 0);
    OUTPUT:
        RETVAL

# (code, error) for one attribute added.
void
add_attr(conn, name, type, many, unique, indexed)
        SV *conn
        SV *name
        int type
        int many
        int unique
        int indexed
    PREINIT:
        int rc;
    PPCODE:
        NEED_ABI("add_attr");
        rc = (FDB->add_attr)(aTHX_ conn, name, type, many, unique, indexed);
        EXTEND(SP, 2);
        PUSHs(sv_2mortal(newSViv(rc)));
        PUSHs(rc == FACT_DB_OK ? &PL_sv_undef : sv_2mortal(error_now(aTHX)));

int
is_conn(sv)
        SV *sv
    CODE:
        NEED_ABI("is_conn");
        RETVAL = (FDB->is_conn)(aTHX_ sv);
    OUTPUT:
        RETVAL

int
is_db(sv)
        SV *sv
    CODE:
        NEED_ABI("is_db");
        RETVAL = (FDB->is_db)(aTHX_ sv);
    OUTPUT:
        RETVAL

SV *
db(conn)
        SV *conn
    CODE:
        NEED_ABI("db");
        RETVAL = (FDB->db)(aTHX_ conn);
        if (!RETVAL) RETVAL = newSV(0);
    OUTPUT:
        RETVAL

IV
versions(conn)
        SV *conn
    CODE:
        NEED_ABI("versions");
        RETVAL = (FDB->versions)(aTHX_ conn);
    OUTPUT:
        RETVAL

# (code, the value it made, its tx, { tempid => id }) or (code, error).
void
transact(conn, ops, ...)
        SV *conn
        AV *ops
    PREINIT:
        fact_db_txn *t;
        fact_db_result r;
        int rc, i, again;
    PPCODE:
        NEED_ABI("transact");
        again = items > 2 ? (int)SvIV(ST(2)) : 1;
        t = build(aTHX_ ops);
        rc = FACT_DB_OK;
        for (i = 0; i < again && rc == FACT_DB_OK; i++) {
            rc = (FDB->transact)(aTHX_ conn, t, &r);
            if (rc == FACT_DB_OK && i + 1 < again) (FDB->result_release)(aTHX_ &r);
        }
        (FDB->txn_free)(aTHX_ t);
        EXTEND(SP, 4);
        PUSHs(sv_2mortal(newSViv(rc)));
        if (rc != FACT_DB_OK) {
            PUSHs(sv_2mortal(error_now(aTHX)));
        }
        else {
            HV *ids = newHV();
            HE *he;
            hv_iterinit(r.tempids);
            while ((he = hv_iternext(r.tempids))) {
                SV *k = hv_iterkeysv(he);
                (void)hv_store_ent(ids, k, newSViv((FDB->tempid)(aTHX_ &r, k)), 0);
            }
            PUSHs(sv_2mortal(newSVsv(r.db)));
            PUSHs(sv_2mortal(newSViv(r.tx)));
            PUSHs(sv_2mortal(newRV_noinc((SV *)ids)));
            (FDB->result_release)(aTHX_ &r);
        }

# (code, the value it made, its tx) or (code, error), keeping nothing.
void
with(db, ops)
        SV *db
        AV *ops
    PREINIT:
        fact_db_txn *t;
        fact_db_result r;
        int rc;
    PPCODE:
        NEED_ABI("with");
        t = build(aTHX_ ops);
        rc = (FDB->with)(aTHX_ db, t, &r);
        (FDB->txn_free)(aTHX_ t);
        EXTEND(SP, 3);
        PUSHs(sv_2mortal(newSViv(rc)));
        if (rc != FACT_DB_OK) PUSHs(sv_2mortal(error_now(aTHX)));
        else {
            PUSHs(sv_2mortal(newSVsv(r.db)));
            PUSHs(sv_2mortal(newSViv(r.tx)));
            (FDB->result_release)(aTHX_ &r);
        }

IV
basis_tx(db)
        SV *db
    CODE:
        NEED_ABI("basis_tx");
        RETVAL = (FDB->basis_tx)(aTHX_ db);
    OUTPUT:
        RETVAL

IV
count(db)
        SV *db
    CODE:
        NEED_ABI("count");
        RETVAL = (FDB->count)(aTHX_ db);
    OUTPUT:
        RETVAL

# which: "as_of", "since" or "history". The value, or (undef, error).
void
travel(db, which, ...)
        SV *db
        const char *which
    PREINIT:
        SV *v;
        IV tx;
    PPCODE:
        NEED_ABI("travel");
        tx = items > 2 ? SvIV(ST(2)) : 0;
        v = strEQ(which, "as_of") ? (FDB->as_of)(aTHX_ db, tx)
          : strEQ(which, "since") ? (FDB->since)(aTHX_ db, tx)
          : (FDB->history)(aTHX_ db);
        EXTEND(SP, 2);
        if (v) PUSHs(sv_2mortal(v));
        else {
            PUSHs(&PL_sv_undef);
            PUSHs(sv_2mortal(error_now(aTHX)));
        }

# (code, [ [attr, value], ... ]) or (code, error).
void
entity(db, e, ...)
        SV *db
        SV *e
    PREINIT:
        AV *out;
        int rc;
    PPCODE:
        NEED_ABI("entity");
        out = (AV *)sv_2mortal((SV *)newAV());
        rc = items > 2 && SvTRUE(ST(2)) ? (FDB->entity)(aTHX_ db, e, croaks, NULL)
                                        : (FDB->entity)(aTHX_ db, e, push_pair, out);
        EXTEND(SP, 2);
        PUSHs(sv_2mortal(newSViv(rc)));
        PUSHs(rc == FACT_DB_OK ? sv_2mortal(newRV_inc((SV *)out)) : sv_2mortal(error_now(aTHX)));

# (code, [ [e, a, v, tx, added], ... ]) or (code, error).
void
datoms(db, index, ...)
        SV *db
        int index
    PREINIT:
        AV *out;
        SV *parts[3];
        int rc, i, n;
    PPCODE:
        NEED_ABI("datoms");
        out = (AV *)sv_2mortal((SV *)newAV());
        n = items - 2 > 3 ? 3 : items - 2;
        for (i = 0; i < n; i++) parts[i] = ST(2 + i);
        rc = (FDB->datoms)(aTHX_ db, index, parts, n, push_datom, out);
        EXTEND(SP, 2);
        PUSHs(sv_2mortal(newSViv(rc)));
        PUSHs(rc == FACT_DB_OK ? sv_2mortal(newRV_inc((SV *)out)) : sv_2mortal(error_now(aTHX)));

# (code, [ rows ]) or (code, error). A row of one value is the value. The
# last argument, when a hash reference, is functions.
void
q(db, text, ...)
        SV *db
        SV *text
    PREINIT:
        fact_db_query *q;
        AV *out;
        HV *functions = NULL;
        int rc, n;
    PPCODE:
        NEED_ABI("q");
        n = items - 2;
        if (n > 0 && SvROK(ST(items - 1)) && SvTYPE(SvRV(ST(items - 1))) == SVt_PVHV) {
            functions = (HV *)SvRV(ST(items - 1));
            n--;
        }
        q = (FDB->query_parse)(aTHX_ text);
        EXTEND(SP, 2);
        if (!q) {
            PUSHs(sv_2mortal(newSViv(FACT_DB_REFUSED)));
            PUSHs(sv_2mortal(error_now(aTHX)));
            XSRETURN(2);
        }
        out = (AV *)sv_2mortal((SV *)newAV());
        rc = (FDB->query_run)(aTHX_ db, q, &ST(2), n, functions, push_row, out);
        (FDB->query_release)(aTHX_ q);
        PUSHs(sv_2mortal(newSViv(rc)));
        PUSHs(rc == FACT_DB_OK ? sv_2mortal(newRV_inc((SV *)out)) : sv_2mortal(error_now(aTHX)));

# The code of a query whose callback stops after `n` rows.
int
q_stop(db, text, n)
        SV *db
        SV *text
        IV n
    PREINIT:
        fact_db_query *q;
    CODE:
        NEED_ABI("q_stop");
        q = (FDB->query_parse)(aTHX_ text);
        if (!q) croak("q_stop: %" SVf, SVfARG((FDB->last_error)(aTHX)));
        RETVAL = (FDB->query_run)(aTHX_ db, q, NULL, 0, NULL, stop_after, &n);
        (FDB->query_release)(aTHX_ q);
    OUTPUT:
        RETVAL

SV *
explain(db, text)
        SV *db
        SV *text
    PREINIT:
        fact_db_query *q;
    CODE:
        NEED_ABI("explain");
        q = (FDB->query_parse)(aTHX_ text);
        if (!q) croak("explain: %" SVf, SVfARG((FDB->last_error)(aTHX)));
        RETVAL = (FDB->explain)(aTHX_ db, q);
        (FDB->query_release)(aTHX_ q);
        if (!RETVAL) RETVAL = error_now(aTHX);
    OUTPUT:
        RETVAL
