#ifndef FACTDB_CACHE_H
#define FACTDB_CACHE_H

/* The query cache (F15): parsed queries kept by their exact text, 256 at
 * most, the oldest put in going first. One a interpreter, in MY_CXT, and
 * let go at the interpreter's exit (Perl_call_atexit), not in global
 * destruction, by which time perl frees scalars in an order of its own. */

#define FACTDB_CACHE_MAX 256

static void factdb_cache_free(pTHX_ void *unused) {
    dMY_CXT;
    PERL_UNUSED_ARG(unused);
    SvREFCNT_dec((SV *)MY_CXT.cache);
    SvREFCNT_dec((SV *)MY_CXT.order);
    MY_CXT.cache = NULL;
    MY_CXT.order = NULL;
}

static void factdb_cache_init(pTHX) {
    dMY_CXT;
    MY_CXT.cache = newHV();
    MY_CXT.order = newAV();
    MY_CXT.hits = 0;
    MY_CXT.misses = 0;
    Perl_call_atexit(aTHX_ factdb_cache_free, NULL);
}

/* The parsed query for `text`, from the cache or parsed and put there. The
 * scalar holding it is in *owner, with a reference of the caller's own,
 * mortal: a Perl function the query calls may run queries enough to push
 * this one out of the cache, and it must outlive the call. */
static factdb_query *factdb_cached_query(pTHX_ SV *text, const char *what, SV **owner_out) {
    dMY_CXT;
    HE *he;
    factdb_owned *o;
    SV *owner, *key;
    factdb_query *q;
    if (!SvOK(text) || SvROK(text)) croak("%s: a query is text", what);
    key = sv_2mortal(newSVsv(text));
    if (MY_CXT.cache && (he = hv_fetch_ent(MY_CXT.cache, key, 0, 0))) {
        MY_CXT.hits++;
        *owner_out = sv_2mortal(SvREFCNT_inc_simple_NN(HeVAL(he)));
        return (factdb_query *)factdb_owned_of(HeVAL(he))->root;
    }
    MY_CXT.misses++;
    owner = factdb_owned_new(aTHX_ &o);
    q = factdb_parse_query(aTHX_ factdb_read(aTHX_ text, &o->arena, what), &o->arena, what);
    o->root = q;
    if (MY_CXT.cache) {
        (void)hv_store_ent(MY_CXT.cache, key, SvREFCNT_inc_simple_NN(owner), 0);
        av_push(MY_CXT.order, newSVsv(key));
        while (av_len(MY_CXT.order) + 1 > FACTDB_CACHE_MAX) {
            SV *old = av_shift(MY_CXT.order);
            (void)hv_delete_ent(MY_CXT.cache, old, G_DISCARD, 0);
            SvREFCNT_dec(old);
        }
    }
    *owner_out = owner;
    return q;
}

#endif
