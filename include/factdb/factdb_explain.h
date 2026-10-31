#ifndef FACTDB_EXPLAIN_H
#define FACTDB_EXPLAIN_H

/* explain: how a query would run against a value, clause by clause, for
 * people. It follows the engine's choices: which variables each clause
 * finds bound, and so which way each pattern reads. */

static void factdb_ex_form(pTHX_ SV *out, const factdb_edn *f) {
    U32 i;
    switch (f->type) {
    case FACTDB_EDN_KEYWORD: sv_catpvf(out, ":%s", f->s); return;
    case FACTDB_EDN_SYMBOL: sv_catpv(out, f->s); return;
    case FACTDB_EDN_STRING: {
        SV *q = sv_2mortal(newSVpvn(f->s, f->len));
        if (f->utf8) SvUTF8_on(q);
        sv_catsv(out, factdb_quote(aTHX_ q));
        return;
    }
    case FACTDB_EDN_INT: sv_catpvf(out, "%" IVdf, f->iv); return;
    case FACTDB_EDN_NUM: sv_catpvf(out, "%" NVgf, f->nv); return;
    case FACTDB_EDN_TRUE: sv_catpvs(out, "true"); return;
    case FACTDB_EDN_FALSE: sv_catpvs(out, "false"); return;
    case FACTDB_EDN_NIL: sv_catpvs(out, "nil"); return;
    }
    sv_catpv(out, f->type == FACTDB_EDN_VEC ? "[" : f->type == FACTDB_EDN_LIST ? "(" : "{");
    for (i = 0; i < f->n; i++) {
        if (i) sv_catpvs(out, " ");
        factdb_ex_form(aTHX_ out, f->items[i]);
    }
    sv_catpv(out, f->type == FACTDB_EDN_VEC ? "]" : f->type == FACTDB_EDN_LIST ? ")" : "}");
}

static int factdb_ex_bound(const factdb_term *t, const char *bound) {
    return t->kind == FACTDB_Q_CONST || (t->kind == FACTDB_Q_VAR && bound[t->var]);
}

static const char *factdb_ex_attr(pTHX_ const factdb_db *d, const factdb_term *t, IV *a) {
    SV *name;
    *a = -1;
    if (t->kind != FACTDB_Q_CONST) return NULL;
    name = sv_2mortal(newSVpvn(t->c->s, t->c->len));
    *a = factdb_attr_id(aTHX_ &d->core->schema, name);
    return t->c->s;
}

static SV *factdb_explain(pTHX_ factdb_run *run) {
    const factdb_query *q = run->q;
    SV *out = sv_2mortal(newSVpvs(""));
    char *bound;
    U32 i, v;
    int any = 0;
    Newxz(bound, q->nvars ? q->nvars : 1, char);
    SAVEFREEPV(bound);
    for (i = 0; i < q->nin; i++) {
        for (v = 0; v < q->in[i].nvars; v++)
            if (q->in[i].vars[v].kind == FACTDB_Q_VAR) bound[q->in[i].vars[v].var] = 1;
        if (q->in[i].kind == FACTDB_I_COLL || q->in[i].kind == FACTDB_I_REL) any = 1;
    }
    for (i = 0; i < q->nwhere; i++) {
        const factdb_clause *c = &q->where[i];
        int shares = 0, binds = 0;
        sv_catpvf(out, "%u. ", (unsigned)(i + 1));
        factdb_ex_form(aTHX_ out, c->at);
        sv_catpvs(out, "\n   ");
        for (v = 0; v < c->nterms; v++) if (c->terms[v].kind == FACTDB_Q_VAR) { binds = 1; if (bound[c->terms[v].var]) shares = 1; }
        if (c->kind == FACTDB_C_PATTERN) {
            const factdb_db *d = run->src[c->src];
            IV a;
            const char *aname = c->nterms > 1 ? factdb_ex_attr(aTHX_ d, &c->terms[1], &a) : NULL;
            int e_b = factdb_ex_bound(&c->terms[0], bound);
            int a_b = c->nterms > 1 && factdb_ex_bound(&c->terms[1], bound);
            int v_b = c->nterms > 2 && factdb_ex_bound(&c->terms[2], bound);
            if (aname && a < 0) sv_catpvf(out, "reads nothing: :%s is not in the schema", aname);
            else if (d->kind == FACTDB_KIND_HISTORY) sv_catpvs(out, "reads the whole log: a history value");
            else if (e_b) sv_catpvs(out, "reads the entity's record (eav)");
            else if (aname && v_b && d->core->schema.attrs[a].indexed) sv_catpvf(out, "looks up the value in the index of :%s (ave)", aname);
            else if (aname && d->core->schema.attrs[a].indexed) sv_catpvf(out, "reads every value in the index of :%s (ave)", aname);
            else if (aname) sv_catpvf(out, "reads every entity's record: :%s is not indexed", aname);
            else if (a_b) sv_catpvs(out, "reads by the attribute each row holds");
            else sv_catpvs(out, "reads every datom");
            if (any && !shares && binds) sv_catpvs(out, "; shares no variable with the clauses before it, so joins every row with every match");
            for (v = 0; v < c->nterms; v++) if (c->terms[v].kind == FACTDB_Q_VAR) bound[c->terms[v].var] = 1;
        }
        else if (c->kind == FACTDB_C_PRED) sv_catpvf(out, "keeps the rows for which %s is true", c->name);
        else if (c->kind == FACTDB_C_FN) {
            sv_catpvf(out, "binds from %s", c->name);
            for (v = 0; v < c->nbind; v++) if (c->bind[v].kind == FACTDB_Q_VAR) bound[c->bind[v].var] = 1;
        }
        else if (c->kind == FACTDB_C_RULE) {
            sv_catpvf(out, "calls rule %s, its answers worked out once for each set of arguments bound", c->name);
            for (v = 0; v < c->nterms; v++) if (c->terms[v].kind == FACTDB_Q_VAR) bound[c->terms[v].var] = 1;
        }
        else if (c->kind == FACTDB_C_NOT) sv_catpvs(out, "keeps the rows for which its clauses find nothing");
        else {
            sv_catpvf(out, "joins the rows its %u branches make", (unsigned)c->nbranches);
            if (c->has_join) for (v = 0; v < c->njoin; v++) bound[c->join[v].var] = 1;
            else {
                char *seen;
                Newxz(seen, q->nvars ? q->nvars : 1, char);
                SAVEFREEPV(seen);
                factdb_vars_in(c->branches[0].clauses, c->branches[0].n, seen);
                for (v = 0; v < q->nvars; v++) if (seen[v]) bound[v] = 1;
            }
        }
        if (c->kind == FACTDB_C_PATTERN || c->kind == FACTDB_C_FN || c->kind == FACTDB_C_RULE || c->kind == FACTDB_C_OR) any = 1;
        sv_catpvs(out, "\n");
    }
    return out;
}

#endif
