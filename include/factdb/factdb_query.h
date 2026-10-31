#ifndef FACTDB_QUERY_H
#define FACTDB_QUERY_H

/* The grammar: a form the reader made, read as a query or as rules. The
 * dialect is Datomic's (F12):
 *
 *   [:find <find> :with ?v ... :in <input> ... :where <clause> ...]
 *   {:find [...] :with [...] :in [...] :where [...]}
 *
 *   find     ?a ?b (a relation) | ?x . (one value) | [?x ...] (a list)
 *            | [?a ?b] (one tuple); each element a variable or (agg ...)
 *   input    $ | $name | % | ?x | [?x ...] | [?a ?b] | [[?a ?b]]
 *   clause   [$src? e a v tx added]   a pattern, from one to five terms
 *            [(pred args...)]          a predicate
 *            [(fn args...) binding]    a function, binding ?x, [?x ...],
 *                                      [?a ?b] or [[?a ?b]]
 *            (rule args...)            a rule call
 *            (not clause...)  (not-join [?v ...] clause...)
 *            (or branch...)   (or-join [?v ...] branch...), a branch being
 *                             one clause or (and clause...)
 *   rules    [[(name ?a ?b) clause...] ...]
 *
 * Variables are numbered as they are first seen, one numbering a query and
 * one a rule. Every check that needs no schema and no data is made here,
 * with the line and column of what is wrong: clause order is join order
 * (F13), so a variable used before anything binds it is the user's to move,
 * and is said so now rather than when the query runs. */

#define FACTDB_Q_VAR   1
#define FACTDB_Q_BLANK 2
#define FACTDB_Q_CONST 3
#define FACTDB_Q_SRC   4

#define FACTDB_C_PATTERN 1
#define FACTDB_C_PRED    2
#define FACTDB_C_FN      3
#define FACTDB_C_RULE    4
#define FACTDB_C_NOT     5
#define FACTDB_C_OR      6

#define FACTDB_B_SCALAR 1
#define FACTDB_B_COLL   2
#define FACTDB_B_TUPLE  3
#define FACTDB_B_REL    4

#define FACTDB_F_REL    1
#define FACTDB_F_SCALAR 2
#define FACTDB_F_COLL   3
#define FACTDB_F_TUPLE  4

#define FACTDB_I_SRC    1
#define FACTDB_I_RULES  2
#define FACTDB_I_SCALAR 3
#define FACTDB_I_COLL   4
#define FACTDB_I_TUPLE  5
#define FACTDB_I_REL    6

typedef struct factdb_term {
    U8 kind;
    U32 var;
    const factdb_edn *c;
    const factdb_edn *at;
} factdb_term;

struct factdb_branch;

typedef struct factdb_clause {
    U8 kind;
    const factdb_edn *at;
    U32 src;
    const char *name;
    factdb_term *terms;
    U32 nterms;
    U8 bkind;
    factdb_term *bind;
    U32 nbind;
    int has_join;
    factdb_term *join;
    U32 njoin;
    struct factdb_clause *body;
    U32 nbody;
    struct factdb_branch *branches;
    U32 nbranches;
} factdb_clause;

typedef struct factdb_branch {
    factdb_clause *clauses;
    U32 n;
} factdb_branch;

typedef struct factdb_find {
    U8 is_agg;
    U32 var;
    const char *agg;
    factdb_term *args;
    U32 nargs;
    const factdb_edn *at;
} factdb_find;

typedef struct factdb_input {
    U8 kind;
    const char *name;
    factdb_term *vars;
    U32 nvars;
    const factdb_edn *at;
} factdb_input;

typedef struct factdb_query {
    U8 find_kind;
    factdb_find *find;
    U32 nfind;
    factdb_term *with;
    U32 nwith;
    factdb_input *in;
    U32 nin;
    factdb_clause *where;
    U32 nwhere;
    const char **vars;
    U32 nvars;
    U32 nsrc;
    int has_rules;
} factdb_query;

typedef struct factdb_rule {
    const char *name;
    const factdb_edn *at;
    factdb_term *params;
    U32 nparams;
    factdb_clause *body;
    U32 nbody;
    const char **vars;
    U32 nvars;
} factdb_rule;

typedef struct factdb_ruleset {
    factdb_rule *rules;
    U32 n;
} factdb_ruleset;

/* ---- the context of one parse ---------------------------------------- */

typedef struct factdb_gram {
    const char *what;
    factdb_arena *arena;
    HV *var_ids;
    AV *var_names;
    HV *src_ids;
    U32 nsrc;
} factdb_gram;

#define FACTDB_ERR(g, at, ...) factdb_at(aTHX_ (g)->what, (at)->line, (at)->col, __VA_ARGS__)

static void factdb_gram_scope(pTHX_ factdb_gram *g) {
    g->var_ids = (HV *)sv_2mortal((SV *)newHV());
    g->var_names = (AV *)sv_2mortal((SV *)newAV());
}

static int factdb_is_sym(const factdb_edn *f, const char *s) {
    return f && f->type == FACTDB_EDN_SYMBOL && strEQ(f->s, s);
}

static int factdb_is_var(const factdb_edn *f) {
    return f && f->type == FACTDB_EDN_SYMBOL && f->s[0] == '?' && f->len > 1;
}

static int factdb_is_src(const factdb_edn *f) {
    return f && f->type == FACTDB_EDN_SYMBOL && f->s[0] == '$';
}

static U32 factdb_var_slot(pTHX_ factdb_gram *g, const factdb_edn *f) {
    SV **slot = hv_fetch(g->var_ids, f->s, f->utf8 ? -(I32)f->len : (I32)f->len, 0);
    U32 id;
    if (slot) return (U32)SvUV(*slot);
    id = (U32)(av_len(g->var_names) + 1);
    av_push(g->var_names, newSVpvn(f->s, f->len));
    (void)hv_store(g->var_ids, f->s, f->utf8 ? -(I32)f->len : (I32)f->len, newSVuv(id), 0);
    return id;
}

static const char **factdb_gram_names(pTHX_ factdb_gram *g, U32 *n) {
    U32 i, count = (U32)(av_len(g->var_names) + 1);
    const char **names = (const char **)factdb_arena_get(aTHX_ g->arena, (count ? count : 1) * sizeof(char *));
    for (i = 0; i < count; i++) {
        STRLEN len;
        const char *p = SvPV(AvARRAY(g->var_names)[i], len);
        names[i] = factdb_arena_str(aTHX_ g->arena, p, len);
    }
    *n = count;
    return names;
}

static const char *factdb_form_name(const factdb_edn *f) {
    switch (f->type) {
    case FACTDB_EDN_VEC: return "a vector";
    case FACTDB_EDN_LIST: return "a list";
    case FACTDB_EDN_MAP: return "a map";
    case FACTDB_EDN_KEYWORD: return "a keyword";
    case FACTDB_EDN_SYMBOL: return "a symbol";
    case FACTDB_EDN_STRING: return "a string";
    case FACTDB_EDN_INT: case FACTDB_EDN_NUM: return "a number";
    case FACTDB_EDN_TRUE: case FACTDB_EDN_FALSE: return "a boolean";
    }
    return "nil";
}

static void *factdb_gram_array(pTHX_ factdb_gram *g, U32 n, size_t each) {
    void *p = factdb_arena_get(aTHX_ g->arena, (n ? n : 1) * each);
    Zero(p, (n ? n : 1) * each, char);
    return p;
}

/* ---- terms ------------------------------------------------------------ */

#define FACTDB_POS_E     0
#define FACTDB_POS_A     1
#define FACTDB_POS_V     2
#define FACTDB_POS_TX    3
#define FACTDB_POS_ADDED 4
#define FACTDB_POS_ARG   5

static void factdb_term_of(pTHX_ factdb_gram *g, const factdb_edn *f, factdb_term *t, int pos) {
    t->at = f;
    t->c = NULL;
    if (factdb_is_var(f)) { t->kind = FACTDB_Q_VAR; t->var = factdb_var_slot(aTHX_ g, f); return; }
    if (pos == FACTDB_POS_ARG && factdb_is_src(f)) {
        SV **id = hv_fetch(g->src_ids, f->s, (I32)f->len, 0);
        if (!id) FACTDB_ERR(g, f, "%s is not a source named in :in", f->s);
        t->kind = FACTDB_Q_SRC;
        t->var = (U32)SvUV(*id);
        return;
    }
    if (factdb_is_sym(f, "_")) {
        if (pos == FACTDB_POS_ARG) FACTDB_ERR(g, f, "_ stands for any value in a pattern, not for an argument");
        t->kind = FACTDB_Q_BLANK;
        return;
    }
    t->kind = FACTDB_Q_CONST;
    t->c = f;
    switch (f->type) {
    case FACTDB_EDN_KEYWORD:
        if (pos != FACTDB_POS_A && pos != FACTDB_POS_ARG)
            FACTDB_ERR(g, f, "a keyword names an attribute, and goes in a pattern's second place");
        return;
    case FACTDB_EDN_INT: case FACTDB_EDN_NUM: case FACTDB_EDN_STRING:
    case FACTDB_EDN_TRUE: case FACTDB_EDN_FALSE:
        if (pos == FACTDB_POS_A)
            FACTDB_ERR(g, f, "a pattern's second place is an attribute: a keyword, a variable or _, not %s", factdb_form_name(f));
        return;
    case FACTDB_EDN_VEC:
        if (pos == FACTDB_POS_E && f->n == 2 && f->items[0]->type == FACTDB_EDN_KEYWORD) return;
        if (pos == FACTDB_POS_E) FACTDB_ERR(g, f, "a lookup ref is [:attribute value]");
        break;
    case FACTDB_EDN_SYMBOL:
        FACTDB_ERR(g, f, "%s is not a variable (they begin with ?), _ or a constant", f->s);
    case FACTDB_EDN_NIL:
        FACTDB_ERR(g, f, "nil is not a value; a pattern that leaves a place open has _ there");
    }
    FACTDB_ERR(g, f, "%s cannot go here", factdb_form_name(f));
}

/* ---- clauses ---------------------------------------------------------- */

static void factdb_clauses_of(pTHX_ factdb_gram *g, const factdb_edn *const *forms, U32 n, factdb_clause **out, U32 *nout, int in_or);

static void factdb_join_vars(pTHX_ factdb_gram *g, const factdb_edn *f, factdb_clause *c, const char *which) {
    U32 i;
    if (!f || f->type != FACTDB_EDN_VEC || !f->n)
        FACTDB_ERR(g, f ? f : c->at, "%s names the variables that join in a vector: [?a ?b]", which);
    c->has_join = 1;
    c->njoin = f->n;
    c->join = (factdb_term *)factdb_gram_array(aTHX_ g, f->n, sizeof(factdb_term));
    for (i = 0; i < f->n; i++) {
        if (!factdb_is_var(f->items[i])) FACTDB_ERR(g, f->items[i], "%s joins on variables, and %s is not one", which, factdb_form_name(f->items[i]));
        factdb_term_of(aTHX_ g, f->items[i], &c->join[i], FACTDB_POS_ARG);
    }
}

static void factdb_binding_of(pTHX_ factdb_gram *g, const factdb_edn *f, factdb_clause *c) {
    U32 i;
    if (factdb_is_var(f)) {
        c->bkind = FACTDB_B_SCALAR;
        c->nbind = 1;
        c->bind = (factdb_term *)factdb_gram_array(aTHX_ g, 1, sizeof(factdb_term));
        factdb_term_of(aTHX_ g, f, &c->bind[0], FACTDB_POS_ARG);
        return;
    }
    if (f->type == FACTDB_EDN_VEC && f->n == 2 && factdb_is_var(f->items[0]) && factdb_is_sym(f->items[1], "...")) {
        c->bkind = FACTDB_B_COLL;
        c->nbind = 1;
        c->bind = (factdb_term *)factdb_gram_array(aTHX_ g, 1, sizeof(factdb_term));
        factdb_term_of(aTHX_ g, f->items[0], &c->bind[0], FACTDB_POS_ARG);
        return;
    }
    if (f->type == FACTDB_EDN_VEC && f->n == 1 && f->items[0]->type == FACTDB_EDN_VEC) {
        c->bkind = FACTDB_B_REL;
        f = f->items[0];
    }
    else if (f->type == FACTDB_EDN_VEC) c->bkind = FACTDB_B_TUPLE;
    else FACTDB_ERR(g, f, "a function binds ?x, [?x ...], [?a ?b] or [[?a ?b]], not %s", factdb_form_name(f));
    if (!f->n) FACTDB_ERR(g, f, "a function's binding needs at least one variable");
    c->nbind = f->n;
    c->bind = (factdb_term *)factdb_gram_array(aTHX_ g, f->n, sizeof(factdb_term));
    for (i = 0; i < f->n; i++) {
        const factdb_edn *v = f->items[i];
        if (factdb_is_sym(v, "_")) { c->bind[i].kind = FACTDB_Q_BLANK; c->bind[i].at = v; continue; }
        if (!factdb_is_var(v)) FACTDB_ERR(g, v, "a function binds variables or _, and %s is not one", factdb_form_name(v));
        factdb_term_of(aTHX_ g, v, &c->bind[i], FACTDB_POS_ARG);
    }
}

static void factdb_call_of(pTHX_ factdb_gram *g, const factdb_edn *list, factdb_clause *c, const char *what_kind) {
    U32 i;
    if (!list->n || list->items[0]->type != FACTDB_EDN_SYMBOL)
        FACTDB_ERR(g, list, "%s begins with its name", what_kind);
    c->name = list->items[0]->s;
    c->nterms = list->n - 1;
    c->terms = (factdb_term *)factdb_gram_array(aTHX_ g, c->nterms, sizeof(factdb_term));
    for (i = 1; i < list->n; i++) {
        const factdb_edn *a = list->items[i];
        if (a->type == FACTDB_EDN_VEC || a->type == FACTDB_EDN_LIST || a->type == FACTDB_EDN_MAP)
            FACTDB_ERR(g, a, "an argument is a variable or a constant, not %s", factdb_form_name(a));
        factdb_term_of(aTHX_ g, a, &c->terms[i - 1], FACTDB_POS_ARG);
    }
}

static void factdb_branch_of(pTHX_ factdb_gram *g, const factdb_edn *f, factdb_branch *b) {
    if (f->type == FACTDB_EDN_LIST && f->n && factdb_is_sym(f->items[0], "and")) {
        if (f->n < 2) FACTDB_ERR(g, f, "and needs at least one clause");
        factdb_clauses_of(aTHX_ g, (const factdb_edn *const *)f->items + 1, f->n - 1, &b->clauses, &b->n, 0);
    }
    else factdb_clauses_of(aTHX_ g, &f, 1, &b->clauses, &b->n, 1);
}

static void factdb_clause_of(pTHX_ factdb_gram *g, const factdb_edn *f, factdb_clause *c, int in_or) {
    U32 i;
    Zero(c, 1, factdb_clause);
    c->at = f;
    if (f->type == FACTDB_EDN_VEC) {
        U32 first = 0;
        if (!f->n) FACTDB_ERR(g, f, "an empty clause");
        if (f->items[0]->type == FACTDB_EDN_LIST) {
            const factdb_edn *call = f->items[0];
            if (f->n == 1) {
                c->kind = FACTDB_C_PRED;
                factdb_call_of(aTHX_ g, call, c, "a predicate");
            }
            else if (f->n == 2) {
                c->kind = FACTDB_C_FN;
                factdb_call_of(aTHX_ g, call, c, "a function");
                factdb_binding_of(aTHX_ g, f->items[1], c);
            }
            else FACTDB_ERR(g, f, "a predicate is [(name args...)] and a function [(name args...) binding]");
            return;
        }
        c->kind = FACTDB_C_PATTERN;
        if (factdb_is_src(f->items[0])) {
            SV **id = hv_fetch(g->src_ids, f->items[0]->s, (I32)f->items[0]->len, 0);
            if (!id) FACTDB_ERR(g, f->items[0], "%s is not a source named in :in", f->items[0]->s);
            c->src = (U32)SvUV(*id);
            first = 1;
        }
        else {
            SV **id = hv_fetch(g->src_ids, "$", 1, 0);
            if (!id) FACTDB_ERR(g, f, "a pattern reads a source, and :in names no $");
            c->src = (U32)SvUV(*id);
        }
        if (f->n - first < 1) FACTDB_ERR(g, f, "a pattern needs at least an entity");
        if (f->n - first > 5) FACTDB_ERR(g, f->items[first + 5], "a pattern has at most five places: entity, attribute, value, transaction, added");
        c->nterms = f->n - first;
        c->terms = (factdb_term *)factdb_gram_array(aTHX_ g, c->nterms, sizeof(factdb_term));
        for (i = first; i < f->n; i++) factdb_term_of(aTHX_ g, f->items[i], &c->terms[i - first], (int)(i - first));
        return;
    }
    if (f->type == FACTDB_EDN_LIST) {
        const factdb_edn *head = f->n ? f->items[0] : NULL;
        if (!head || head->type != FACTDB_EDN_SYMBOL)
            FACTDB_ERR(g, f, "a list clause begins with not, not-join, or, or-join or a rule's name");
        if (strEQ(head->s, "not") || strEQ(head->s, "not-join")) {
            U32 from = 1;
            c->kind = FACTDB_C_NOT;
            if (head->s[3]) { factdb_join_vars(aTHX_ g, f->n > 1 ? f->items[1] : NULL, c, "not-join"); from = 2; }
            if (f->n <= from) FACTDB_ERR(g, f, "%s needs at least one clause", head->s);
            factdb_clauses_of(aTHX_ g, (const factdb_edn *const *)f->items + from, f->n - from, &c->body, &c->nbody, 0);
            return;
        }
        if (strEQ(head->s, "or") || strEQ(head->s, "or-join")) {
            U32 from = 1;
            c->kind = FACTDB_C_OR;
            if (head->s[2]) { factdb_join_vars(aTHX_ g, f->n > 1 ? f->items[1] : NULL, c, "or-join"); from = 2; }
            if (f->n <= from) FACTDB_ERR(g, f, "%s needs at least one branch", head->s);
            c->nbranches = f->n - from;
            c->branches = (factdb_branch *)factdb_gram_array(aTHX_ g, c->nbranches, sizeof(factdb_branch));
            for (i = from; i < f->n; i++) factdb_branch_of(aTHX_ g, f->items[i], &c->branches[i - from]);
            return;
        }
        if (strEQ(head->s, "and"))
            FACTDB_ERR(g, f, in_or ? "and is a branch of or, not inside one" : "and is a branch of or, and stands nowhere else");
        if (head->s[0] == '?' || head->s[0] == '$')
            FACTDB_ERR(g, head, "a rule's name is a plain symbol, not %s", head->s);
        c->kind = FACTDB_C_RULE;
        factdb_call_of(aTHX_ g, f, c, "a rule call");
        return;
    }
    FACTDB_ERR(g, f, "a clause is a pattern [e a v], a predicate [(f ...)], a function [(f ...) binding], "
                     "a rule call (name ...), or not, not-join, or, or-join; not %s", factdb_form_name(f));
}

static void factdb_clauses_of(pTHX_ factdb_gram *g, const factdb_edn *const *forms, U32 n, factdb_clause **out, U32 *nout, int in_or) {
    U32 i;
    *out = (factdb_clause *)factdb_gram_array(aTHX_ g, n, sizeof(factdb_clause));
    *nout = n;
    for (i = 0; i < n; i++) factdb_clause_of(aTHX_ g, forms[i], &(*out)[i], in_or);
}

/* ---- binding ---------------------------------------------------------- */

static void factdb_vars_in(const factdb_clause *cs, U32 n, char *seen);

static void factdb_vars_terms(const factdb_term *ts, U32 n, char *seen) {
    U32 i;
    for (i = 0; i < n; i++) if (ts[i].kind == FACTDB_Q_VAR) seen[ts[i].var] = 1;
}

static void factdb_vars_in(const factdb_clause *cs, U32 n, char *seen) {
    U32 i, b;
    for (i = 0; i < n; i++) {
        const factdb_clause *c = &cs[i];
        factdb_vars_terms(c->terms, c->nterms, seen);
        factdb_vars_terms(c->bind, c->nbind, seen);
        factdb_vars_terms(c->join, c->njoin, seen);
        factdb_vars_in(c->body, c->nbody, seen);
        for (b = 0; b < c->nbranches; b++) factdb_vars_in(c->branches[b].clauses, c->branches[b].n, seen);
    }
}

typedef struct factdb_bind {
    const char *what;
    const char **names;
    U32 nvars;
} factdb_bind;

static void factdb_need_bound(pTHX_ const factdb_bind *k, const factdb_term *ts, U32 n, const char *bound, const char *where) {
    U32 i;
    for (i = 0; i < n; i++)
        if (ts[i].kind == FACTDB_Q_VAR && !bound[ts[i].var])
            factdb_at(aTHX_ k->what, ts[i].at->line, ts[i].at->col,
                      "%s in this %s is bound by no clause before it", k->names[ts[i].var], where);
}

/* Walks clauses in order, marking in `bound` what each binds, and croaks
 * at the first variable used before anything binds it. */
static void factdb_bind_check(pTHX_ const factdb_bind *k, const factdb_clause *cs, U32 n, char *bound) {
    U32 i, b, v;
    for (i = 0; i < n; i++) {
        const factdb_clause *c = &cs[i];
        switch (c->kind) {
        case FACTDB_C_PATTERN:
        case FACTDB_C_RULE:
            for (v = 0; v < c->nterms; v++) if (c->terms[v].kind == FACTDB_Q_VAR) bound[c->terms[v].var] = 1;
            break;
        case FACTDB_C_PRED:
            factdb_need_bound(aTHX_ k, c->terms, c->nterms, bound, "predicate");
            break;
        case FACTDB_C_FN:
            factdb_need_bound(aTHX_ k, c->terms, c->nterms, bound, "function");
            for (v = 0; v < c->nbind; v++) if (c->bind[v].kind == FACTDB_Q_VAR) bound[c->bind[v].var] = 1;
            break;
        case FACTDB_C_NOT: {
            char *inner;
            Newx(inner, k->nvars ? k->nvars : 1, char);
            SAVEFREEPV(inner);
            if (c->has_join) {
                factdb_need_bound(aTHX_ k, c->join, c->njoin, bound, "not-join");
                Zero(inner, k->nvars ? k->nvars : 1, char);
                for (v = 0; v < c->njoin; v++) inner[c->join[v].var] = 1;
            }
            else {
                char *used;
                Newxz(used, k->nvars ? k->nvars : 1, char);
                SAVEFREEPV(used);
                factdb_vars_in(c->body, c->nbody, used);
                for (v = 0; v < k->nvars; v++)
                    if (used[v] && !bound[v])
                        factdb_at(aTHX_ k->what, c->at->line, c->at->col,
                                  "%s in this not is bound by no clause before it; not-join names the variables that join",
                                  k->names[v]);
                Copy(bound, inner, k->nvars, char);
            }
            factdb_bind_check(aTHX_ k, c->body, c->nbody, inner);
            break;
        }
        case FACTDB_C_OR: {
            char *first = NULL, *inner;
            Newx(inner, k->nvars ? k->nvars : 1, char);
            SAVEFREEPV(inner);
            if (c->has_join) {
                for (b = 0; b < c->nbranches; b++) {
                    Copy(bound, inner, k->nvars, char);
                    factdb_bind_check(aTHX_ k, c->branches[b].clauses, c->branches[b].n, inner);
                    for (v = 0; v < c->njoin; v++)
                        if (!inner[c->join[v].var])
                            factdb_at(aTHX_ k->what, c->branches[b].clauses[0].at->line, c->branches[b].clauses[0].at->col,
                                      "%s of this or-join is bound by no clause of this branch", k->names[c->join[v].var]);
                }
                for (v = 0; v < c->njoin; v++) bound[c->join[v].var] = 1;
                break;
            }
            Newx(first, k->nvars ? k->nvars : 1, char);
            SAVEFREEPV(first);
            for (b = 0; b < c->nbranches; b++) {
                Copy(bound, inner, k->nvars, char);
                factdb_bind_check(aTHX_ k, c->branches[b].clauses, c->branches[b].n, inner);
                if (b == 0) { Copy(inner, first, k->nvars, char); continue; }
                for (v = 0; v < k->nvars; v++)
                    if (inner[v] != first[v])
                        factdb_at(aTHX_ k->what, c->branches[b].clauses[0].at->line, c->branches[b].clauses[0].at->col,
                                  "the branches of this or bind different variables: %s in %s and not in %s; "
                                  "or-join names the variables that join",
                                  k->names[v], inner[v] ? "this branch" : "the first", inner[v] ? "the first" : "this one");
            }
            Copy(first, bound, k->nvars, char);
            break;
        }
        }
    }
}

/* ---- the query -------------------------------------------------------- */

static int factdb_agg_known(const char *name, U32 nargs) {
    static const char *const one[] = { "count", "count-distinct", "sum", "avg", "min", "max", "distinct", "median", NULL };
    int i;
    for (i = 0; one[i]; i++) if (strEQ(name, one[i])) return nargs == 1 || (nargs == 2 && (strEQ(name, "min") || strEQ(name, "max")));
    return -1;
}

static void factdb_find_elem(pTHX_ factdb_gram *g, const factdb_edn *f, factdb_find *out) {
    U32 i;
    Zero(out, 1, factdb_find);
    out->at = f;
    if (factdb_is_var(f)) { out->var = factdb_var_slot(aTHX_ g, f); return; }
    if (f->type == FACTDB_EDN_LIST && f->n && f->items[0]->type == FACTDB_EDN_SYMBOL) {
        int known;
        out->is_agg = 1;
        out->agg = f->items[0]->s;
        out->nargs = f->n - 1;
        known = factdb_agg_known(out->agg, out->nargs);
        if (known < 0)
            FACTDB_ERR(g, f, "unknown aggregate %s; the aggregates are count, count-distinct, sum, avg, min, max, distinct and median", out->agg);
        if (!known)
            FACTDB_ERR(g, f, "%s takes one variable%s", out->agg, (strEQ(out->agg, "min") || strEQ(out->agg, "max")) ? ", or a count and a variable" : "");
        out->args = (factdb_term *)factdb_gram_array(aTHX_ g, out->nargs, sizeof(factdb_term));
        for (i = 1; i < f->n; i++) {
            const factdb_edn *a = f->items[i];
            int last = i == f->n - 1;
            if (last && !factdb_is_var(a)) FACTDB_ERR(g, a, "an aggregate's last argument is the variable it aggregates");
            if (!last && a->type != FACTDB_EDN_INT) FACTDB_ERR(g, a, "(%s n ?x) takes a whole number n", out->agg);
            if (!last && a->iv < 1) FACTDB_ERR(g, a, "(%s n ?x) takes n of 1 or more", out->agg);
            factdb_term_of(aTHX_ g, a, &out->args[i - 1], FACTDB_POS_ARG);
        }
        return;
    }
    FACTDB_ERR(g, f, ":find takes variables and aggregates, not %s", factdb_form_name(f));
}

static void factdb_find_of(pTHX_ factdb_gram *g, factdb_query *q, const factdb_edn *const *fs, U32 n, const factdb_edn *at) {
    U32 i;
    if (!n) FACTDB_ERR(g, at, ":find needs at least one variable or aggregate");
    if (n == 1 && fs[0]->type == FACTDB_EDN_VEC) {
        const factdb_edn *v = fs[0];
        if (v->n == 2 && factdb_is_sym(v->items[1], "...")) {
            q->find_kind = FACTDB_F_COLL;
            q->nfind = 1;
            q->find = (factdb_find *)factdb_gram_array(aTHX_ g, 1, sizeof(factdb_find));
            factdb_find_elem(aTHX_ g, v->items[0], &q->find[0]);
            return;
        }
        if (!v->n) FACTDB_ERR(g, v, ":find [...] needs at least one variable");
        q->find_kind = FACTDB_F_TUPLE;
        q->nfind = v->n;
        q->find = (factdb_find *)factdb_gram_array(aTHX_ g, v->n, sizeof(factdb_find));
        for (i = 0; i < v->n; i++) factdb_find_elem(aTHX_ g, v->items[i], &q->find[i]);
        return;
    }
    if (n == 2 && factdb_is_sym(fs[1], ".")) {
        q->find_kind = FACTDB_F_SCALAR;
        q->nfind = 1;
        q->find = (factdb_find *)factdb_gram_array(aTHX_ g, 1, sizeof(factdb_find));
        factdb_find_elem(aTHX_ g, fs[0], &q->find[0]);
        return;
    }
    q->find_kind = FACTDB_F_REL;
    q->nfind = n;
    q->find = (factdb_find *)factdb_gram_array(aTHX_ g, n, sizeof(factdb_find));
    for (i = 0; i < n; i++) {
        if (factdb_is_sym(fs[i], ".") || factdb_is_sym(fs[i], "..."))
            FACTDB_ERR(g, fs[i], "%s goes after the one element it qualifies: ?x . for one value, [?x ...] for a list", fs[i]->s);
        factdb_find_elem(aTHX_ g, fs[i], &q->find[i]);
    }
}

static void factdb_in_vars(pTHX_ factdb_gram *g, const factdb_edn *v, factdb_input *in, HV *seen) {
    U32 i;
    in->nvars = v->n;
    in->vars = (factdb_term *)factdb_gram_array(aTHX_ g, v->n, sizeof(factdb_term));
    if (!v->n) FACTDB_ERR(g, v, "an input binds at least one variable");
    for (i = 0; i < v->n; i++) {
        const factdb_edn *x = v->items[i];
        if (factdb_is_sym(x, "_")) { in->vars[i].kind = FACTDB_Q_BLANK; in->vars[i].at = x; continue; }
        if (!factdb_is_var(x)) FACTDB_ERR(g, x, "an input binds variables or _, and %s is not one", factdb_form_name(x));
        if (hv_exists(seen, x->s, (I32)x->len)) FACTDB_ERR(g, x, "%s is bound by two inputs", x->s);
        (void)hv_store(seen, x->s, (I32)x->len, newSViv(1), 0);
        factdb_term_of(aTHX_ g, x, &in->vars[i], FACTDB_POS_ARG);
    }
}

static void factdb_in_of(pTHX_ factdb_gram *g, factdb_query *q, const factdb_edn *const *fs, U32 n, int given) {
    U32 i;
    HV *seen = (HV *)sv_2mortal((SV *)newHV());
    if (!given) {
        q->nin = 1;
        q->in = (factdb_input *)factdb_gram_array(aTHX_ g, 1, sizeof(factdb_input));
        q->in[0].kind = FACTDB_I_SRC;
        q->in[0].name = "$";
        (void)hv_store(g->src_ids, "$", 1, newSVuv(0), 0);
        q->nsrc = 1;
        return;
    }
    q->nin = n;
    q->in = (factdb_input *)factdb_gram_array(aTHX_ g, n, sizeof(factdb_input));
    for (i = 0; i < n; i++) {
        const factdb_edn *f = fs[i];
        factdb_input *in = &q->in[i];
        in->at = f;
        if (factdb_is_src(f)) {
            if (hv_exists(g->src_ids, f->s, (I32)f->len)) FACTDB_ERR(g, f, "%s is named twice in :in", f->s);
            in->kind = FACTDB_I_SRC;
            in->name = f->s;
            (void)hv_store(g->src_ids, f->s, (I32)f->len, newSVuv(q->nsrc), 0);
            q->nsrc++;
        }
        else if (factdb_is_sym(f, "%")) {
            if (q->has_rules) FACTDB_ERR(g, f, "%% is named twice in :in; one rule set takes every rule");
            in->kind = FACTDB_I_RULES;
            q->has_rules = 1;
        }
        else if (factdb_is_var(f)) {
            const factdb_edn *one[1];
            factdb_edn wrap;
            Zero(&wrap, 1, factdb_edn);
            one[0] = f;
            wrap.items = (factdb_edn **)one;
            wrap.n = 1;
            wrap.line = f->line;
            wrap.col = f->col;
            in->kind = FACTDB_I_SCALAR;
            factdb_in_vars(aTHX_ g, &wrap, in, seen);
        }
        else if (f->type == FACTDB_EDN_VEC && f->n == 2 && factdb_is_var(f->items[0]) && factdb_is_sym(f->items[1], "...")) {
            factdb_edn wrap = *f;
            wrap.n = 1;
            in->kind = FACTDB_I_COLL;
            factdb_in_vars(aTHX_ g, &wrap, in, seen);
        }
        else if (f->type == FACTDB_EDN_VEC && f->n == 1 && f->items[0]->type == FACTDB_EDN_VEC) {
            in->kind = FACTDB_I_REL;
            factdb_in_vars(aTHX_ g, f->items[0], in, seen);
        }
        else if (f->type == FACTDB_EDN_VEC) {
            in->kind = FACTDB_I_TUPLE;
            factdb_in_vars(aTHX_ g, f, in, seen);
        }
        else FACTDB_ERR(g, f, "an input is $, $name, %%, ?x, [?x ...], [?a ?b] or [[?a ?b]], not %s", factdb_form_name(f));
    }
}

/* The sections of a query, in either form: name -> items. */
typedef struct factdb_sections {
    const factdb_edn *const *items[4];
    U32 n[4];
    int given[4];
    const factdb_edn *at[4];
} factdb_sections;

static void factdb_section(pTHX_ factdb_gram *g, factdb_sections *s, const factdb_edn *kw,
                           const factdb_edn *const *items, U32 n) {
    static const char *const names[] = { "find", "with", "in", "where" };
    int i;
    for (i = 0; i < 4; i++) {
        if (!strEQ(kw->s, names[i])) continue;
        if (s->given[i]) FACTDB_ERR(g, kw, ":%s is given twice", names[i]);
        s->given[i] = 1;
        s->items[i] = items;
        s->n[i] = n;
        s->at[i] = kw;
        return;
    }
    FACTDB_ERR(g, kw, "unknown section :%s; the sections are :find, :with, :in and :where", kw->s);
}

static factdb_query *factdb_parse_query(pTHX_ const factdb_edn *top, factdb_arena *arena, const char *what) {
    factdb_gram g;
    factdb_sections s;
    factdb_query *q;
    factdb_bind k;
    char *bound;
    U32 i, v;

    Zero(&s, 1, factdb_sections);
    g.what = what;
    g.arena = arena;
    g.src_ids = (HV *)sv_2mortal((SV *)newHV());
    g.nsrc = 0;
    factdb_gram_scope(aTHX_ &g);

    if (top->type == FACTDB_EDN_VEC) {
        i = 0;
        if (!top->n || top->items[0]->type != FACTDB_EDN_KEYWORD)
            FACTDB_ERR(&g, top, "a query begins :find");
        while (i < top->n) {
            const factdb_edn *kw = top->items[i++];
            U32 start = i;
            while (i < top->n && top->items[i]->type != FACTDB_EDN_KEYWORD) i++;
            factdb_section(aTHX_ &g, &s, kw, (const factdb_edn *const *)top->items + start, i - start);
        }
    }
    else if (top->type == FACTDB_EDN_MAP) {
        for (i = 0; i < top->n; i += 2) {
            const factdb_edn *kw = top->items[i], *val = top->items[i + 1];
            if (kw->type != FACTDB_EDN_KEYWORD) FACTDB_ERR(&g, kw, "a query map's keys are :find, :with, :in and :where");
            if (val->type != FACTDB_EDN_VEC) FACTDB_ERR(&g, val, "in a query map, :%s takes a vector", kw->s);
            factdb_section(aTHX_ &g, &s, kw, (const factdb_edn *const *)val->items, val->n);
        }
    }
    else FACTDB_ERR(&g, top, "a query is a vector or a map, beginning :find; this is %s", factdb_form_name(top));

    if (!s.given[0]) FACTDB_ERR(&g, top, "a query needs :find");
    if (!s.given[3]) FACTDB_ERR(&g, top, "a query needs :where");
    if (!s.n[3]) FACTDB_ERR(&g, s.at[3], ":where needs at least one clause");

    q = (factdb_query *)factdb_gram_array(aTHX_ &g, 1, sizeof(factdb_query));
    factdb_in_of(aTHX_ &g, q, s.items[2], s.n[2], s.given[2]);
    factdb_find_of(aTHX_ &g, q, s.items[0], s.n[0], s.at[0]);
    q->nwith = s.n[1];
    q->with = (factdb_term *)factdb_gram_array(aTHX_ &g, q->nwith, sizeof(factdb_term));
    for (i = 0; i < q->nwith; i++) {
        if (!factdb_is_var(s.items[1][i])) FACTDB_ERR(&g, s.items[1][i], ":with takes variables, not %s", factdb_form_name(s.items[1][i]));
        factdb_term_of(aTHX_ &g, s.items[1][i], &q->with[i], FACTDB_POS_ARG);
    }
    factdb_clauses_of(aTHX_ &g, s.items[3], s.n[3], &q->where, &q->nwhere, 0);
    q->vars = factdb_gram_names(aTHX_ &g, &q->nvars);

    k.what = what;
    k.names = q->vars;
    k.nvars = q->nvars;
    Newxz(bound, q->nvars ? q->nvars : 1, char);
    SAVEFREEPV(bound);
    for (i = 0; i < q->nin; i++)
        for (v = 0; v < q->in[i].nvars; v++)
            if (q->in[i].vars[v].kind == FACTDB_Q_VAR) bound[q->in[i].vars[v].var] = 1;
    factdb_bind_check(aTHX_ &k, q->where, q->nwhere, bound);
    for (i = 0; i < q->nfind; i++) {
        const factdb_find *f = &q->find[i];
        if (!f->is_agg && !bound[f->var])
            factdb_at(aTHX_ what, f->at->line, f->at->col, "%s in :find is bound by no clause", q->vars[f->var]);
        if (f->is_agg) {
            const factdb_term *a = &f->args[f->nargs - 1];
            if (!bound[a->var])
                factdb_at(aTHX_ what, a->at->line, a->at->col, "%s in :find is bound by no clause", q->vars[a->var]);
        }
    }
    for (i = 0; i < q->nwith; i++)
        if (!bound[q->with[i].var])
            factdb_at(aTHX_ what, q->with[i].at->line, q->with[i].at->col, "%s in :with is bound by no clause", q->vars[q->with[i].var]);
    return q;
}

/* ---- rules ------------------------------------------------------------ */

static factdb_ruleset *factdb_parse_rules(pTHX_ const factdb_edn *top, factdb_arena *arena, const char *what) {
    factdb_gram g;
    factdb_ruleset *rs;
    HV *arity = (HV *)sv_2mortal((SV *)newHV());
    U32 i, p;
    g.what = what;
    g.arena = arena;
    g.src_ids = (HV *)sv_2mortal((SV *)newHV());
    (void)hv_store(g.src_ids, "$", 1, newSVuv(0), 0);
    if (top->type != FACTDB_EDN_VEC) FACTDB_ERR(&g, top, "rules are a vector of rules, each [(name ?a ...) clause ...]");
    rs = (factdb_ruleset *)factdb_gram_array(aTHX_ &g, 1, sizeof(factdb_ruleset));
    rs->n = top->n;
    rs->rules = (factdb_rule *)factdb_gram_array(aTHX_ &g, top->n, sizeof(factdb_rule));
    for (i = 0; i < top->n; i++) {
        const factdb_edn *rf = top->items[i], *head;
        factdb_rule *r = &rs->rules[i];
        factdb_bind k;
        char *bound;
        SV **known;
        if (rf->type != FACTDB_EDN_VEC || rf->n < 2 || rf->items[0]->type != FACTDB_EDN_LIST)
            FACTDB_ERR(&g, rf, "a rule is [(name ?a ...) clause ...], with at least one clause");
        head = rf->items[0];
        if (!head->n || head->items[0]->type != FACTDB_EDN_SYMBOL || head->items[0]->s[0] == '?')
            FACTDB_ERR(&g, head, "a rule's head begins with its name");
        factdb_gram_scope(aTHX_ &g);
        r->name = head->items[0]->s;
        r->at = head;
        r->nparams = head->n - 1;
        r->params = (factdb_term *)factdb_gram_array(aTHX_ &g, r->nparams, sizeof(factdb_term));
        for (p = 1; p < head->n; p++) {
            if (!factdb_is_var(head->items[p]))
                FACTDB_ERR(&g, head->items[p], "a rule's head names variables, and %s is not one", factdb_form_name(head->items[p]));
            factdb_term_of(aTHX_ &g, head->items[p], &r->params[p - 1], FACTDB_POS_ARG);
        }
        known = hv_fetch(arity, r->name, (I32)strlen(r->name), 0);
        if (known && SvUV(*known) != r->nparams)
            FACTDB_ERR(&g, head, "rule %s is defined with %" UVuf " arguments here and %" UVuf " before",
                       r->name, (UV)r->nparams, SvUV(*known));
        if (!known) (void)hv_store(arity, r->name, (I32)strlen(r->name), newSVuv(r->nparams), 0);
        factdb_clauses_of(aTHX_ &g, (const factdb_edn *const *)rf->items + 1, rf->n - 1, &r->body, &r->nbody, 0);
        r->vars = factdb_gram_names(aTHX_ &g, &r->nvars);
        k.what = what;
        k.names = r->vars;
        k.nvars = r->nvars;
        Newxz(bound, r->nvars ? r->nvars : 1, char);
        SAVEFREEPV(bound);
        factdb_bind_check(aTHX_ &k, r->body, r->nbody, bound);
        for (p = 0; p < r->nparams; p++)
            if (!bound[r->params[p].var])
                factdb_at(aTHX_ what, r->params[p].at->line, r->params[p].at->col,
                          "rule %s: %s in its head is bound by no clause of its body", r->name, r->vars[r->params[p].var]);
    }
    return rs;
}

/* ---- stratification -------------------------------------------------- */

/* The calls a rule's body makes, each marked whether it is inside a not. */
static void factdb_calls_of(pTHX_ const factdb_clause *cs, U32 n, int neg, AV *out) {
    U32 i, b;
    for (i = 0; i < n; i++) {
        const factdb_clause *c = &cs[i];
        if (c->kind == FACTDB_C_RULE) {
            av_push(out, newSVpv(c->name, 0));
            av_push(out, newSViv(neg));
        }
        factdb_calls_of(aTHX_ c->body, c->nbody, neg || c->kind == FACTDB_C_NOT, out);
        for (b = 0; b < c->nbranches; b++) factdb_calls_of(aTHX_ c->branches[b].clauses, c->branches[b].n, neg, out);
    }
}

static int factdb_reaches(pTHX_ HV *edges, const char *from, const char *to, HV *seen) {
    SV **list;
    I32 i, n;
    if (strEQ(from, to)) return 1;
    if (hv_exists(seen, from, (I32)strlen(from))) return 0;
    (void)hv_store(seen, from, (I32)strlen(from), &PL_sv_yes, 0);
    list = hv_fetch(edges, from, (I32)strlen(from), 0);
    if (!list) return 0;
    n = av_len((AV *)SvRV(*list)) + 1;
    for (i = 0; i < n; i += 2)
        if (factdb_reaches(aTHX_ edges, SvPV_nolen(*av_fetch((AV *)SvRV(*list), i, 0)), to, seen)) return 1;
    return 0;
}

/* A rule may not depend on its own negation: then there is no order in
 * which to work the rules out (they are not stratified). Croaks naming the
 * rule and the one it negates. */
static void factdb_stratify(pTHX_ const factdb_ruleset *rs, const char *what) {
    HV *edges = (HV *)sv_2mortal((SV *)newHV());
    U32 i;
    for (i = 0; i < rs->n; i++) {
        const factdb_rule *r = &rs->rules[i];
        SV **slot = hv_fetch(edges, r->name, (I32)strlen(r->name), 1);
        if (!SvOK(*slot)) sv_setsv(*slot, sv_2mortal(newRV_noinc((SV *)newAV())));
        factdb_calls_of(aTHX_ r->body, r->nbody, 0, (AV *)SvRV(*slot));
    }
    for (i = 0; i < rs->n; i++) {
        const factdb_rule *r = &rs->rules[i];
        AV *calls = (AV *)SvRV(*hv_fetch(edges, r->name, (I32)strlen(r->name), 0));
        I32 k, n = av_len(calls) + 1;
        for (k = 0; k < n; k += 2) {
            const char *callee = SvPV_nolen(*av_fetch(calls, k, 0));
            if (!SvIV(*av_fetch(calls, k + 1, 0))) continue;
            if (factdb_reaches(aTHX_ edges, callee, r->name, (HV *)sv_2mortal((SV *)newHV())))
                factdb_at(aTHX_ what, r->at->line, r->at->col,
                          "rule %s negates %s, which depends on %s: a rule cannot depend on its own negation",
                          r->name, callee, r->name);
        }
    }
}

/* Every rule call names a rule with that many arguments: in the query, in
 * the rules' bodies, and inside not and or. */
static void factdb_calls_check(pTHX_ const factdb_clause *cs, U32 n, const factdb_ruleset *rs, int have_rules, const char *what) {
    U32 i, b, r;
    for (i = 0; i < n; i++) {
        const factdb_clause *c = &cs[i];
        if (c->kind == FACTDB_C_RULE) {
            int found = 0;
            if (!have_rules)
                factdb_at(aTHX_ what, c->at->line, c->at->col, "(%s ...) calls a rule, and :in names no %% to give the rules", c->name);
            for (r = 0; rs && r < rs->n; r++) {
                if (!strEQ(rs->rules[r].name, c->name)) continue;
                if (rs->rules[r].nparams != c->nterms)
                    factdb_at(aTHX_ what, c->at->line, c->at->col, "rule %s takes %" UVuf " arguments, not %" UVuf,
                              c->name, (UV)rs->rules[r].nparams, (UV)c->nterms);
                found = 1;
                break;
            }
            if (rs && !found) factdb_at(aTHX_ what, c->at->line, c->at->col, "there is no rule %s", c->name);
        }
        factdb_calls_check(aTHX_ c->body, c->nbody, rs, have_rules, what);
        for (b = 0; b < c->nbranches; b++) factdb_calls_check(aTHX_ c->branches[b].clauses, c->branches[b].n, rs, have_rules, what);
    }
}

#endif
