#ifndef FACTDB_ALLOC_H
#define FACTDB_ALLOC_H

/* Every block this dist allocates for itself, counted, so a test can say
 * that nothing was left behind. What HAMT allocates on its behalf is HAMT's
 * to count. One number for the process and nothing locks it: it means
 * something only while one thread is using the library, which is when the
 * tests read it. */
static long factdb_live_blocks = 0;

static void *factdb_alloc(pTHX_ size_t bytes) {
    void *p;
    Newxz(p, bytes, char);
    factdb_live_blocks++;
    return p;
}

static void factdb_free(pTHX_ void *p) {
    factdb_live_blocks--;
    Safefree(p);
}

#endif
