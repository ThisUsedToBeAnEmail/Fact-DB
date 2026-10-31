#ifndef FACTDB_ABI_SELFTEST_H
#define FACTDB_ABI_SELFTEST_H

/* Walks the table. Every entry is called THROUGH the table, with its answer
 * checked, so an entry that points at the wrong function, or sits in the
 * wrong place, fails here by number and not in some consumer's hands.
 * 0 when every check passed, or the number of the first that failed. */

#define FACTDB_ST(n, cond) do { if (!(cond)) { step = (n); goto done; } } while (0)

typedef struct factdb_st_tally { int calls, stop_at, retracted; IV e, tx; int added; SV *last; } factdb_st_tally;

static int factdb_st_pair(pTHX_ void *ud, SV *attr, SV *value) {
    factdb_st_tally *t = (factdb_st_tally *)ud;
    PERL_UNUSED_CONTEXT;
    PERL_UNUSED_ARG(attr);
    PERL_UNUSED_ARG(value);
    return ++t->calls == t->stop_at;
}

static int factdb_st_datom(pTHX_ void *ud, IV e, SV *attr, SV *value, IV tx, int added) {
    factdb_st_tally *t = (factdb_st_tally *)ud;
    PERL_UNUSED_CONTEXT;
    PERL_UNUSED_ARG(attr);
    PERL_UNUSED_ARG(value);
    t->e = e;
    t->tx = tx;
    t->added = added;
    if (added == 0) t->retracted++;
    return ++t->calls == t->stop_at;
}

static int factdb_st_row(pTHX_ void *ud, SV **values, U32 n) {
    factdb_st_tally *t = (factdb_st_tally *)ud;
    if (n == 1) sv_setsv(t->last, values[0]);
    return ++t->calls == t->stop_at;
}

static int factdb_st_croak(pTHX_ void *ud, SV *attr, SV *value) {
    PERL_UNUSED_ARG(ud);
    PERL_UNUSED_ARG(attr);
    PERL_UNUSED_ARG(value);
    croak("the callback croaked\n");
    return 0;
}

static int factdb_st_says(pTHX_ const fact_db_abi *T, const char *part) {
    SV *e = (T->last_error)(aTHX);
    return e && SvOK(e) && instr(SvPV_nolen(e), part) != NULL;
}

#define FACTDB_STR(s) sv_2mortal(newSVpvs(s))
#define FACTDB_INT(i) sv_2mortal(newSViv(i))

/* `out`, when the test passes, gets the connection it built: for the Perl
 * side to read back. */
static int factdb_abi_selftest(pTHX_ SV **out) {
    const fact_db_abi *T = &FACTDB_ABI;
    SV *conn = NULL, *db = NULL, *v = NULL, *errsv = get_sv("@", GV_ADD);
    fact_db_txn *t = NULL;
    fact_db_query *q = NULL;
    fact_db_result r, r2;
    factdb_st_tally tl;
    SV *parts[2], *in[1];
    IV ann = 0, bob = 0, tx1 = 0;
    int step = 0;

    *out = NULL;
    Zero(&r, 1, fact_db_result);
    Zero(&r2, 1, fact_db_result);
    FACTDB_ST(1, T->abi_version == FACT_DB_ABI_VERSION);

    conn = (T->conn_new)(aTHX);
    FACTDB_ST(2, conn && (T->is_conn)(aTHX_ conn) && !(T->is_conn)(aTHX_ &PL_sv_undef));
    FACTDB_ST(3, (T->add_attr)(aTHX_ conn, FACTDB_STR("person/name"), FACT_DB_T_STRING, 0, FACT_DB_UNIQUE_IDENTITY, 0) == FACT_DB_OK
                 && (T->add_attr)(aTHX_ conn, FACTDB_STR("person/age"), FACT_DB_T_INT, 0, FACT_DB_UNIQUE_NONE, 1) == FACT_DB_OK
                 && (T->add_attr)(aTHX_ conn, FACTDB_STR("person/friend"), FACT_DB_T_REF, 1, FACT_DB_UNIQUE_NONE, 0) == FACT_DB_OK);
    FACTDB_ST(4, (T->add_attr)(aTHX_ conn, FACTDB_STR("person/x"), 99, 0, 0, 0) == FACT_DB_REFUSED
                 && factdb_st_says(aTHX_ T, "is not a type"));
    FACTDB_ST(5, (T->add_attr)(aTHX_ &PL_sv_undef, FACTDB_STR("a/b"), 1, 0, 0, 0) == FACT_DB_NOTDB);

    db = (T->db)(aTHX_ conn);
    FACTDB_ST(6, db && (T->is_db)(aTHX_ db) && !(T->is_db)(aTHX_ conn)
                 && (T->basis_tx)(aTHX_ db) == 0 && (T->count)(aTHX_ db) == 0
                 && (T->versions)(aTHX_ conn) == 1 && (T->versions)(aTHX_ db) == FACT_DB_NOTDB);
    SvREFCNT_dec(db);
    db = NULL;

    t = (T->txn_new)(aTHX);
    {
        HV *ent = (HV *)sv_2mortal((SV *)newHV());
        (void)hv_stores(ent, "db/id", newSVpvs("ann"));
        (void)hv_stores(ent, "person/name", newSVpvs("Ann"));
        (void)hv_stores(ent, "person/age", newSViv(61));
        (T->txn_entity)(aTHX_ t, ent);
        (void)hv_stores(ent, "person/name", newSVpvs("not kept"));
    }
    (T->txn_add)(aTHX_ t, FACT_DB_OP_ADD, FACTDB_STR("bob"), FACTDB_STR("person/name"), FACTDB_STR("Bob"));
    (T->txn_add)(aTHX_ t, FACT_DB_OP_ADD, FACTDB_STR("bob"), FACTDB_STR("person/friend"), FACTDB_STR("ann"));
    FACTDB_ST(7, (T->transact)(aTHX_ conn, t, &r) == FACT_DB_OK && r.db && r.tx > 0);
    ann = (T->tempid)(aTHX_ &r, FACTDB_STR("ann"));
    bob = (T->tempid)(aTHX_ &r, FACTDB_STR("bob"));
    tx1 = r.tx;
    FACTDB_ST(8, ann > 0 && bob > 0 && ann != bob && (T->tempid)(aTHX_ &r, FACTDB_STR("carol")) == 0);
    FACTDB_ST(9, (T->basis_tx)(aTHX_ r.db) == tx1 && (T->count)(aTHX_ r.db) == 5 && (T->versions)(aTHX_ conn) == 2);
    (T->txn_free)(aTHX_ t);
    t = NULL;

    sv_setpvs(errsv, "kept");
    t = (T->txn_new)(aTHX);
    (T->txn_add)(aTHX_ t, FACT_DB_OP_ADD, FACTDB_INT(ann), FACTDB_STR("person/age"), FACTDB_INT(62));
    (T->txn_add)(aTHX_ t, FACT_DB_OP_ADD, FACTDB_INT(ann), FACTDB_STR("no/such"), FACTDB_INT(1));
    FACTDB_ST(10, (T->transact)(aTHX_ conn, t, &r2) == FACT_DB_REFUSED && r2.db == NULL
                  && factdb_st_says(aTHX_ T, "no/such") && (T->versions)(aTHX_ conn) == 2);
    FACTDB_ST(11, strEQ(SvPV_nolen(errsv), "kept"));
    (T->txn_free)(aTHX_ t);
    t = NULL;

    Zero(&tl, 1, factdb_st_tally);
    FACTDB_ST(12, (T->entity)(aTHX_ r.db, FACTDB_INT(ann), factdb_st_pair, &tl) == FACT_DB_OK && tl.calls == 2);
    Zero(&tl, 1, factdb_st_tally);
    tl.stop_at = 1;
    FACTDB_ST(13, (T->entity)(aTHX_ r.db, FACTDB_INT(ann), factdb_st_pair, &tl) == FACT_DB_STOPPED && tl.calls == 1);
    FACTDB_ST(14, (T->entity)(aTHX_ r.db, FACTDB_INT(ann), factdb_st_croak, NULL) == FACT_DB_REFUSED
                  && factdb_st_says(aTHX_ T, "the callback croaked") && strEQ(SvPV_nolen(errsv), "kept"));
    FACTDB_ST(15, (T->entity)(aTHX_ conn, FACTDB_INT(ann), factdb_st_pair, &tl) == FACT_DB_NOTDB);

    Zero(&tl, 1, factdb_st_tally);
    parts[0] = FACTDB_STR("person/age");
    FACTDB_ST(16, (T->datoms)(aTHX_ r.db, FACT_DB_AVE, parts, 1, factdb_st_datom, &tl) == FACT_DB_OK
                  && tl.calls == 1 && tl.e == ann && tl.tx == tx1 && tl.added == -1);
    Zero(&tl, 1, factdb_st_tally);
    parts[0] = FACTDB_INT(ann);
    FACTDB_ST(17, (T->datoms)(aTHX_ r.db, FACT_DB_VAE, parts, 1, factdb_st_datom, &tl) == FACT_DB_OK
                  && tl.calls == 1 && tl.e == bob);
    FACTDB_ST(18, (T->datoms)(aTHX_ r.db, 7, parts, 0, factdb_st_datom, &tl) == FACT_DB_REFUSED);

    q = (T->query_parse)(aTHX_ FACTDB_STR("[:find ?n :in $ ?min :where [?e :person/age ?a] [(> ?a ?min)] [?e :person/name ?n]]"));
    FACTDB_ST(19, q != NULL);
    Zero(&tl, 1, factdb_st_tally);
    tl.last = sv_newmortal();
    in[0] = FACTDB_INT(40);
    FACTDB_ST(20, (T->query_run)(aTHX_ r.db, q, in, 1, NULL, factdb_st_row, &tl) == FACT_DB_OK
                  && tl.calls == 1 && strEQ(SvPV_nolen(tl.last), "Ann"));
    in[0] = FACTDB_INT(70);
    Zero(&tl, 1, factdb_st_tally);
    tl.last = sv_newmortal();
    FACTDB_ST(21, (T->query_run)(aTHX_ r.db, q, in, 1, NULL, factdb_st_row, &tl) == FACT_DB_OK && tl.calls == 0);
    FACTDB_ST(22, (T->query_run)(aTHX_ r.db, q, in, 0, NULL, factdb_st_row, &tl) == FACT_DB_REFUSED);
    v = (T->explain)(aTHX_ r.db, q);
    FACTDB_ST(23, v && SvCUR(v) > 0);
    SvREFCNT_dec(v);
    v = NULL;
    (T->query_release)(aTHX_ q);
    q = NULL;
    FACTDB_ST(24, (T->query_parse)(aTHX_ FACTDB_STR("[:find ?x :where")) == NULL && factdb_st_says(aTHX_ T, "line 1"));

    t = (T->txn_new)(aTHX);
    (T->txn_cas)(aTHX_ t, FACTDB_INT(ann), FACTDB_STR("person/age"), FACTDB_INT(61), FACTDB_INT(62));
    FACTDB_ST(25, (T->with)(aTHX_ r.db, t, &r2) == FACT_DB_OK && (T->versions)(aTHX_ conn) == 2
                  && (T->basis_tx)(aTHX_ r2.db) > tx1);
    (T->result_release)(aTHX_ &r2);
    FACTDB_ST(26, (T->transact)(aTHX_ conn, t, &r2) == FACT_DB_OK && (T->versions)(aTHX_ conn) == 3);
    (T->result_release)(aTHX_ &r2);
    FACTDB_ST(27, (T->transact)(aTHX_ conn, t, &r2) == FACT_DB_REFUSED && (T->versions)(aTHX_ conn) == 3);
    (T->txn_free)(aTHX_ t);
    t = (T->txn_new)(aTHX);
    (T->txn_add)(aTHX_ t, FACT_DB_OP_RETRACT, FACTDB_INT(bob), FACTDB_STR("person/friend"), FACTDB_INT(ann));
    FACTDB_ST(37, (T->with)(aTHX_ r.db, t, &r2) == FACT_DB_OK && (T->count)(aTHX_ r2.db) == (T->count)(aTHX_ r.db));
    (T->result_release)(aTHX_ &r2);
    (T->txn_free)(aTHX_ t);
    t = (T->txn_new)(aTHX);
    (T->txn_retract_entity)(aTHX_ t, FACTDB_INT(bob));
    FACTDB_ST(28, (T->transact)(aTHX_ conn, t, &r2) == FACT_DB_OK && (T->count)(aTHX_ r2.db) == 5);
    (T->txn_free)(aTHX_ t);
    t = NULL;

    v = (T->as_of)(aTHX_ r2.db, tx1);
    FACTDB_ST(29, v && (T->basis_tx)(aTHX_ v) == tx1 && (T->count)(aTHX_ v) == 5);
    SvREFCNT_dec(v);
    v = (T->as_of)(aTHX_ r2.db, 0);
    FACTDB_ST(30, v && (T->count)(aTHX_ v) == 0);
    SvREFCNT_dec(v);
    v = (T->since)(aTHX_ r2.db, tx1);
    FACTDB_ST(31, v && (T->count)(aTHX_ v) == 3);
    FACTDB_ST(32, (T->as_of)(aTHX_ v, 0) == NULL && factdb_st_says(aTHX_ T, "since"));
    FACTDB_ST(33, (T->history)(aTHX_ v) == NULL);
    SvREFCNT_dec(v);
    v = (T->history)(aTHX_ r2.db);
    Zero(&tl, 1, factdb_st_tally);
    parts[0] = FACTDB_INT(bob);
    FACTDB_ST(34, v && (T->datoms)(aTHX_ v, FACT_DB_EAV, parts, 1, factdb_st_datom, &tl) == FACT_DB_OK
                  && tl.calls == 4 && tl.retracted == 2);
    FACTDB_ST(35, (T->as_of)(aTHX_ r2.db, -1) == NULL && (T->as_of)(aTHX_ conn, 0) == NULL);
    FACTDB_ST(36, strEQ(SvPV_nolen(errsv), "kept"));

    SvREFCNT_dec(v);
    v = (T->since)(aTHX_ r2.db, tx1);
    SvREFCNT_dec(v);
    v = (T->since)(aTHX_ r2.db, 0);
    FACTDB_ST(38, v && (T->count)(aTHX_ v) == (T->count)(aTHX_ r2.db));
    (T->result_release)(aTHX_ &r2);

    FACTDB_ST(39, (T->add_attr)(aTHX_ conn, FACTDB_STR("person/email"), FACT_DB_T_STRING, 0, FACT_DB_UNIQUE_VALUE, 0) == FACT_DB_OK);
    (T->result_release)(aTHX_ &r);
    t = (T->txn_new)(aTHX);
    (T->txn_add)(aTHX_ t, FACT_DB_OP_ADD, FACTDB_INT(ann), FACTDB_STR("person/email"), FACTDB_STR("a@x"));
    FACTDB_ST(40, (T->transact)(aTHX_ conn, t, &r) == FACT_DB_OK);
    (T->txn_free)(aTHX_ t);
    t = (T->txn_new)(aTHX);
    (T->txn_add)(aTHX_ t, FACT_DB_OP_ADD, FACTDB_STR("eve"), FACTDB_STR("person/email"), FACTDB_STR("a@x"));
    FACTDB_ST(41, (T->with)(aTHX_ r.db, t, &r2) == FACT_DB_REFUSED);

    *out = conn;
    conn = NULL;
done:
    if (t) (T->txn_free)(aTHX_ t);
    if (q) (T->query_release)(aTHX_ q);
    SvREFCNT_dec(v);
    SvREFCNT_dec(db);
    (T->result_release)(aTHX_ &r);
    (T->result_release)(aTHX_ &r2);
    SvREFCNT_dec(conn);
    return step;
}

#undef FACTDB_STR
#undef FACTDB_INT

#endif
