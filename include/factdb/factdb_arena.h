#ifndef FACTDB_ARENA_H
#define FACTDB_ARENA_H

/* An arena: memory handed out in pieces and given back all at once. A
 * parsed query lives in one, so freeing a query is freeing its arena, and
 * a parse that croaks half way loses nothing: the arena belongs to a Perl
 * object from the start (factdb_arena_sv), and goes when that does. */

typedef struct factdb_block {
    struct factdb_block *next;
    size_t used, size;
} factdb_block;

typedef struct factdb_arena {
    factdb_block *head;
} factdb_arena;

#define FACTDB_ARENA_BLOCK 4096
#define FACTDB_ALIGN(n) (((n) + 15) & ~(size_t)15)

static void *factdb_arena_get(pTHX_ factdb_arena *ar, size_t bytes) {
    factdb_block *b = ar->head;
    void *p;
    bytes = FACTDB_ALIGN(bytes ? bytes : 1);
    if (!b || b->size - b->used < bytes) {
        size_t size = bytes > FACTDB_ARENA_BLOCK ? bytes : FACTDB_ARENA_BLOCK;
        b = (factdb_block *)factdb_alloc(aTHX_ FACTDB_ALIGN(sizeof(factdb_block)) + size);
        b->size = size;
        b->used = 0;
        b->next = ar->head;
        ar->head = b;
    }
    p = (char *)b + FACTDB_ALIGN(sizeof(factdb_block)) + b->used;
    b->used += bytes;
    return p;
}

static char *factdb_arena_str(pTHX_ factdb_arena *ar, const char *s, STRLEN len) {
    char *p = (char *)factdb_arena_get(aTHX_ ar, len + 1);
    Copy(s, p, len, char);
    p[len] = '\0';
    return p;
}

static void factdb_arena_free(pTHX_ factdb_arena *ar) {
    factdb_block *b = ar->head;
    while (b) {
        factdb_block *next = b->next;
        factdb_free(aTHX_ b);
        b = next;
    }
    ar->head = NULL;
}

/* ---- an arena owned by a Perl object --------------------------------- */

typedef struct factdb_owned {
    factdb_arena arena;
    void *root;
} factdb_owned;

static int factdb_owned_free(pTHX_ SV *sv, MAGIC *mg) {
    factdb_owned *o = (factdb_owned *)mg->mg_ptr;
    PERL_UNUSED_ARG(sv);
    if (o) {
        mg->mg_ptr = NULL;
        factdb_arena_free(aTHX_ &o->arena);
        factdb_free(aTHX_ o);
    }
    return 0;
}

static int factdb_owned_dup(pTHX_ MAGIC *mg, CLONE_PARAMS *param) {
    PERL_UNUSED_CONTEXT;
    PERL_UNUSED_ARG(param);
    mg->mg_ptr = NULL;
    return 0;
}

static MGVTBL factdb_owned_vtbl = {
    NULL, NULL, NULL, NULL, factdb_owned_free, NULL, factdb_owned_dup, NULL
};

/* A new arena held by a new scalar, mortal; the arena in *out. */
static SV *factdb_owned_new(pTHX_ factdb_owned **out) {
    factdb_owned *o = (factdb_owned *)factdb_alloc(aTHX_ sizeof(factdb_owned));
    SV *sv = sv_2mortal(newSV(0));
    MAGIC *mg = sv_magicext(sv, NULL, PERL_MAGIC_ext, &factdb_owned_vtbl, (const char *)o, 0);
    mg->mg_flags |= MGf_DUP;
    *out = o;
    return sv;
}

/* The arena a scalar from factdb_owned_new holds. */
static factdb_owned *factdb_owned_of(SV *sv) {
    MAGIC *mg;
    if (SvTYPE(sv) < SVt_PVMG) return NULL;
    for (mg = SvMAGIC(sv); mg; mg = mg->mg_moremagic)
        if (mg->mg_type == PERL_MAGIC_ext && mg->mg_virtual == &factdb_owned_vtbl)
            return (factdb_owned *)mg->mg_ptr;
    return NULL;
}

#endif
