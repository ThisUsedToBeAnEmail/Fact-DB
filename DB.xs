#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#ifdef __APPLE__
#  include <malloc/malloc.h>
#endif

/* All this dist knows of HAMT: its public header, found through
 * ExtUtils::Depends. */
#include "hamt_abi.h"

#include "factdb/factdb_alloc.h"
#include "factdb/factdb_hamt.h"
#include "factdb/factdb_call.h"
#include "factdb/factdb_value.h"
#include "factdb/factdb_schema.h"
#include "factdb/factdb_record.h"
#include "factdb/factdb_db.h"
#include "factdb/factdb_conn.h"
#include "factdb/factdb_read.h"
#include "factdb/factdb_tx.h"
#include "factdb/factdb_time.h"
#include "factdb/factdb_arena.h"
#include "factdb/factdb_edn.h"
#include "factdb/factdb_query.h"
#include "factdb/factdb_qdump.h"
#include "factdb/factdb_engine.h"
#include "factdb/factdb_explain.h"
#include "factdb/factdb_aggregate.h"

#define MY_CXT_KEY "Fact::DB::_guts" XS_VERSION
typedef struct {
    HV *cache;
    AV *order;
    IV hits, misses;
    SV *last_error;
    CV *trampoline;
    void (*pending)(pTHX_ void *);
    void *pending_ud;
} my_cxt_t;
START_MY_CXT

#include "factdb/factdb_cache.h"
#include "factdb/factdb_run.h"
#include "fact_db_abi.h"
#include "factdb/factdb_abi_impl.h"
#include "factdb/factdb_abi_selftest.h"

MODULE = Fact::DB    PACKAGE = Fact::DB

PROTOTYPES: DISABLE

BOOT:
{
    MY_CXT_INIT;
    factdb_hamt_boot(aTHX);
    factdb_cache_init(aTHX);
    factdb_abi_init(aTHX);
}

INCLUDE: xs/conn.xs
INCLUDE: xs/db.xs
INCLUDE: xs/read.xs
INCLUDE: xs/time.xs
INCLUDE: xs/query.xs
INCLUDE: xs/q.xs
INCLUDE: xs/test.xs
INCLUDE: xs/abi.xs
