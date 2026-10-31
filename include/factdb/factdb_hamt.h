#ifndef FACTDB_HAMT_H
#define FACTDB_HAMT_H

/* HAMT's table, resolved once when this library loads and never per call.
 * Calls are written (FACTDB_HAMT->map_fetch)(aTHX_ ...): the parentheses
 * keep a member's name from being read as one of the macros XSUB.h makes of
 * ordinary names on a perl built with PERL_IMPLICIT_SYS. */
static const hamt_abi *FACTDB_HAMT = NULL;

/* hamt_abi.h tells an optional consumer to carry on without the table. This
 * one cannot: every index is a HAMT container. So a missing or older HAMT
 * stops the load, saying which it was. */
static void factdb_hamt_boot(pTHX) {
    const hamt_abi *t;
    SV *sv = eval_pv("require HAMT; HAMT::_abi_ptr()", 0);

    if (SvTRUE(ERRSV))
        croak("Fact::DB: cannot load HAMT: %" SVf, SVfARG(ERRSV));
    if (!sv || !SvOK(sv))
        croak("Fact::DB: HAMT did not hand over its C ABI");
    t = INT2PTR(const hamt_abi *, SvUV(sv));
    if (!t)
        croak("Fact::DB: HAMT did not hand over its C ABI");
    if (t->abi_version < HAMT_ABI_VERSION)
        croak("Fact::DB: HAMT's C ABI is version %d and version %d or later is needed",
              t->abi_version, HAMT_ABI_VERSION);
    FACTDB_HAMT = t;
}

#endif
