#ifndef FACT_DB_ABI_H
#define FACT_DB_ABI_H

/* Public C ABI for Fact::DB: connections, transactions, database values,
 * reads and queries, callable from another extension's C.
 *
 * It is resolved at RUNTIME through Fact::DB::_abi_ptr - a versioned table
 * of function pointers - so there is no link-time coupling: each dist
 * builds and upgrades by itself. A consumer reaches this header through
 * ExtUtils::Depends, or vendors a copy pinned at FACT_DB_ABI_VERSION, and
 * checks abi_version once when it loads.
 *
 * The table only ever grows at the end. FACT_DB_ABI_VERSION bumps on any
 * append, and a consumer requires abi_version >= the version it was written
 * against, treating a later table as a superset it uses a prefix of. NOT
 * ==: an equality check turns every release of this dist into a breaking
 * change for every consumer.
 *
 * ---- THIS HEADER NEEDS PERL -------------------------------------------
 *
 * Every entry takes pTHX_, and connections and database values are Perl
 * objects. Include perl's headers first. Nothing here needs HAMT's header.
 *
 * ---- resolving the table ----------------------------------------------
 *
 * At BOOT, once, never per call:
 *
 *     static const fact_db_abi *FDB = NULL;
 *
 *     BOOT:
 *     {
 *         SV *err;
 *         SV *sv = eval_pv("require Fact::DB; Fact::DB::_abi_ptr()", 0);
 *         err = get_sv("@", 0);
 *         if (sv && SvOK(sv) && (!err || !SvTRUE(err))) {
 *             const fact_db_abi *t = INT2PTR(const fact_db_abi *, SvUV(sv));
 *             if (t && t->abi_version >= FACT_DB_ABI_VERSION) FDB = t;
 *         }
 *     }
 *
 * The VALUE eval_pv returns, not the top of the stack. A NULL table means
 * Fact::DB is absent or too old.
 *
 * Calls are written (FDB->transact)(aTHX_ conn, t, &r). The parentheses
 * keep a member's name from ever being read as a function-like macro, which
 * is how XSUB.h redefines a number of ordinary names on a perl built with
 * PERL_IMPLICIT_SYS.
 *
 * ---- WHAT IS WHAT HERE ------------------------------------------------
 *
 * A connection and a database value are the same objects Perl sees: a
 * reference to a Fact::DB, or to a Fact::DB::Db. Entries take them as SV *.
 * One an entry makes has a reference count of one, which the caller owns;
 * one from Perl is borrowed for as long as Perl holds it. Either can go to
 * the other side freely. A database value never changes.
 *
 * A transaction being built (fact_db_txn) and a parsed query
 * (fact_db_query) are the caller's: made by txn_new and query_parse, given
 * up exactly once to txn_free and query_release. A query parsed here and one
 * Perl's q parsed from the same text are one parse, kept once.
 *
 * Values given to an entry (entities, attributes, values, inputs) are read
 * and not kept, except that a transaction keeps a copy of what it is given.
 * SVs handed to a callback are borrowed for the length of that call.
 *
 * ---- results and failure ----------------------------------------------
 *
 * An entry that returns int returns FACT_DB_OK or a negative code. One that
 * returns a pointer returns NULL on failure, with the code's meaning in
 * last_error.
 *
 * FACT_DB_REFUSED is everything the Perl method would croak on: a value its
 * type refuses, an attribute not in the schema, a query that does not parse,
 * an entity id not given out. The message the Perl method would have given
 * is in last_error, and NOTHING CHANGED: a refused transaction leaves the
 * connection as it was. Entries that can refuse run under a Perl eval frame
 * of their own (no Perl code runs in it, unless a query calls a Perl
 * function), so a croak becomes the code, and $@ is left as it was.
 *
 * A callback may stop the walk it is called from by returning non-zero: the
 * entry returns FACT_DB_STOPPED. A callback that croaks makes the entry
 * return FACT_DB_REFUSED, with its message in last_error.
 *
 * Which entries can run Perl code: query_run, when its query calls a
 * function given in `functions`. Every callback given here is C. */

#define FACT_DB_ABI_VERSION 1

#define FACT_DB_OK        0
#define FACT_DB_NOMEM   (-1)   /* reserved; perl's allocator croaks instead */
#define FACT_DB_NOTDB   (-2)   /* not a connection, or not a database value */
#define FACT_DB_STOPPED (-3)   /* a callback asked to stop */
#define FACT_DB_REFUSED (-4)   /* what the Perl method croaks on: see last_error */

/* Attribute types, cardinality, uniqueness, for add_attr. */
#define FACT_DB_T_STRING  1
#define FACT_DB_T_INT     2
#define FACT_DB_T_NUM     3
#define FACT_DB_T_BOOL    4
#define FACT_DB_T_REF     5
#define FACT_DB_T_INSTANT 6

#define FACT_DB_UNIQUE_NONE     0
#define FACT_DB_UNIQUE_IDENTITY 1
#define FACT_DB_UNIQUE_VALUE    2

/* Ops of a transaction, for txn_add. */
#define FACT_DB_OP_ADD     1
#define FACT_DB_OP_RETRACT 2

/* Indexes, for datoms. */
#define FACT_DB_EAV 1
#define FACT_DB_AEV 2
#define FACT_DB_AVE 3
#define FACT_DB_VAE 4

#ifndef FACT_DB_TXN_FWD
#define FACT_DB_TXN_FWD
typedef struct fact_db_txn fact_db_txn;       /* a transaction being built */
typedef struct fact_db_query fact_db_query;   /* a parsed query */
#endif

/* What a transaction made. The caller owns `db` and `tempids` and gives
 * them up with result_release. */
typedef struct fact_db_result {
    SV *db;        /* the value after it: a Fact::DB::Db */
    IV  tx;        /* its id */
    HV *tempids;   /* each tempid named to the id it was given */
} fact_db_result;

/* ---- callbacks ---------------------------------------------------------- */

/* One fact of an entity: its attribute's name and its value. A cardinality
 * many attribute is one call a value. Non-zero to stop. */
typedef int (*fact_db_pair_fn)(pTHX_ void *ud, SV *attr, SV *value);

/* One datom: entity, attribute name, value, the transaction that made it,
 * and, from a history value, 1 for an assertion and 0 for a retraction (-1
 * from any other value). Non-zero to stop. */
typedef int (*fact_db_datom_fn)(pTHX_ void *ud, IV e, SV *attr, SV *value, IV tx, int added);

/* One row of a query's result: its values, as many as :find names. A
 * relation is a row a tuple; one value, or a list, a row a value; one tuple,
 * one row. Non-zero to stop. */
typedef int (*fact_db_row_fn)(pTHX_ void *ud, SV **values, U32 n);

typedef struct fact_db_abi {
    int abi_version;

    /* What the last entry that returned FACT_DB_REFUSED (or NULL for
     * refused) said, as the Perl method would have croaked it: borrowed,
     * valid until the next refusal. */
    SV  *(*last_error)(pTHX);

    /* ================================================================ *
     *  connections
     * ================================================================ */

    /* A new connection with no attributes and no facts. */
    SV  *(*conn_new)(pTHX);
    /* 1 when `sv` is a connection this interpreter can use, 0 when not. */
    int  (*is_conn)(pTHX_ SV *sv);
    /* Adds one attribute: a name, a FACT_DB_T_ type, cardinality many when
     * `many`, a FACT_DB_UNIQUE_, an index when `indexed`. */
    int  (*add_attr)(pTHX_ SV *conn, SV *name, int type, int many, int unique, int indexed);
    /* The connection's current value. NULL when `conn` is not one. */
    SV  *(*db)(pTHX_ SV *conn);
    /* How many values its line holds; FACT_DB_NOTDB when it is not one. */
    IV   (*versions)(pTHX_ SV *conn);

    /* ================================================================ *
     *  transactions
     * ================================================================ */

    fact_db_txn *(*txn_new)(pTHX);
    void (*txn_free)(pTHX_ fact_db_txn *t);
    /* [db/add or db/retract, entity, attribute, value]: an entity is an id,
     * a tempid (a string), or a lookup ref (an array reference). */
    void (*txn_add)(pTHX_ fact_db_txn *t, int op, SV *entity, SV *attr, SV *value);
    void (*txn_retract_entity)(pTHX_ fact_db_txn *t, SV *entity);
    void (*txn_cas)(pTHX_ fact_db_txn *t, SV *entity, SV *attr, SV *was, SV *now);
    /* An entity as a hash: attribute names to values, db/id for its id or
     * tempid. The hash is copied. */
    void (*txn_entity)(pTHX_ fact_db_txn *t, HV *entity);
    /* Runs it against the connection's current value, which becomes the
     * value it makes. The transaction can be run again, or freed. */
    int  (*transact)(pTHX_ SV *conn, fact_db_txn *t, fact_db_result *out);
    /* Runs it against `db`, keeping nothing. */
    int  (*with)(pTHX_ SV *db, fact_db_txn *t, fact_db_result *out);
    /* The id a tempid was given, or 0. */
    IV   (*tempid)(pTHX_ const fact_db_result *r, SV *name);
    void (*result_release)(pTHX_ fact_db_result *r);

    /* ================================================================ *
     *  database values
     * ================================================================ */

    int  (*is_db)(pTHX_ SV *sv);
    /* FACT_DB_NOTDB when `db` is not one. */
    IV   (*basis_tx)(pTHX_ SV *db);
    IV   (*count)(pTHX_ SV *db);
    /* New values, as the Perl methods of the same names make. */
    SV  *(*as_of)(pTHX_ SV *db, IV tx);
    SV  *(*since)(pTHX_ SV *db, IV tx);
    SV  *(*history)(pTHX_ SV *db);

    /* ================================================================ *
     *  reads
     * ================================================================ */

    /* Every fact of an entity (an id or a lookup ref). */
    int  (*entity)(pTHX_ SV *db, SV *e, fact_db_pair_fn fn, void *ud);
    /* The datoms of an index (FACT_DB_EAV ...) and up to three of its
     * components, as Perl's datoms takes them. */
    int  (*datoms)(pTHX_ SV *db, int index, SV **parts, int nparts, fact_db_datom_fn fn, void *ud);

    /* ================================================================ *
     *  queries
     * ================================================================ */

    /* A parsed query: NULL when it does not parse. */
    fact_db_query *(*query_parse)(pTHX_ SV *text);
    void (*query_release)(pTHX_ fact_db_query *q);
    /* Runs it against `db`: `inputs` are the inputs after the first $, in
     * the order of :in; `functions`, which may be NULL, is q's option. */
    int  (*query_run)(pTHX_ SV *db, fact_db_query *q, SV **inputs, I32 ninputs, HV *functions,
                      fact_db_row_fn fn, void *ud);
    /* The plan, as text. */
    SV  *(*explain)(pTHX_ SV *db, fact_db_query *q);
} fact_db_abi;

#endif
