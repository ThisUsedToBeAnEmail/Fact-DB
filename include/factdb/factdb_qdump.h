#ifndef FACTDB_QDUMP_H
#define FACTDB_QDUMP_H

/* Parsed forms and queries as plain Perl data, for the tests (_read and
 * _parse). Nothing but the tests reads this. */

static SV *factdb_dump_edn(pTHX_ const factdb_edn *f) {
    static const char *const names[] = { NULL, "vec", "list", "map", "kw", "sym", "str", "int", "num", "true", "false", "nil" };
    AV *row = newAV();
    U32 i;
    av_push(row, newSVpv(names[f->type], 0));
    av_push(row, newSVuv(f->line));
    av_push(row, newSVuv(f->col));
    switch (f->type) {
    case FACTDB_EDN_VEC: case FACTDB_EDN_LIST: case FACTDB_EDN_MAP: {
        AV *items = newAV();
        for (i = 0; i < f->n; i++) av_push(items, factdb_dump_edn(aTHX_ f->items[i]));
        av_push(row, newRV_noinc((SV *)items));
        break;
    }
    case FACTDB_EDN_KEYWORD: case FACTDB_EDN_SYMBOL: case FACTDB_EDN_STRING: {
        SV *s = newSVpvn(f->s, f->len);
        if (f->utf8) SvUTF8_on(s);
        av_push(row, s);
        break;
    }
    case FACTDB_EDN_INT: av_push(row, newSViv(f->iv)); break;
    case FACTDB_EDN_NUM: av_push(row, newSVnv(f->nv)); break;
    }
    return newRV_noinc((SV *)row);
}

static SV *factdb_dump_const(pTHX_ const factdb_edn *c) {
    AV *row = newAV();
    switch (c->type) {
    case FACTDB_EDN_KEYWORD: av_push(row, newSVpvs("kw")); av_push(row, newSVpvn(c->s, c->len)); break;
    case FACTDB_EDN_STRING: { SV *s = newSVpvn(c->s, c->len); if (c->utf8) SvUTF8_on(s); av_push(row, newSVpvs("str")); av_push(row, s); break; }
    case FACTDB_EDN_INT: av_push(row, newSVpvs("int")); av_push(row, newSViv(c->iv)); break;
    case FACTDB_EDN_NUM: av_push(row, newSVpvs("num")); av_push(row, newSVnv(c->nv)); break;
    case FACTDB_EDN_TRUE: av_push(row, newSVpvs("bool")); av_push(row, newSViv(1)); break;
    case FACTDB_EDN_FALSE: av_push(row, newSVpvs("bool")); av_push(row, newSViv(0)); break;
    case FACTDB_EDN_VEC:
        av_push(row, newSVpvs("lookup"));
        av_push(row, newSVpvn(c->items[0]->s, c->items[0]->len));
        av_push(row, factdb_dump_const(aTHX_ c->items[1]));
        break;
    }
    return newRV_noinc((SV *)row);
}

static SV *factdb_dump_term(pTHX_ const factdb_term *t, const char *const *vars) {
    AV *row = newAV();
    if (t->kind == FACTDB_Q_VAR) { av_push(row, newSVpvs("var")); av_push(row, newSVpv(vars[t->var], 0)); }
    else if (t->kind == FACTDB_Q_BLANK) av_push(row, newSVpvs("_"));
    else if (t->kind == FACTDB_Q_SRC) { av_push(row, newSVpvs("src")); av_push(row, newSVuv(t->var)); }
    else { SvREFCNT_dec((SV *)row); return factdb_dump_const(aTHX_ t->c); }
    return newRV_noinc((SV *)row);
}

static SV *factdb_dump_terms(pTHX_ const factdb_term *ts, U32 n, const char *const *vars) {
    AV *out = newAV();
    U32 i;
    for (i = 0; i < n; i++) av_push(out, factdb_dump_term(aTHX_ &ts[i], vars));
    return newRV_noinc((SV *)out);
}

static SV *factdb_dump_clauses(pTHX_ const factdb_clause *cs, U32 n, const char *const *vars, const factdb_query *q);

static SV *factdb_dump_clause(pTHX_ const factdb_clause *c, const char *const *vars, const factdb_query *q) {
    AV *row = newAV();
    U32 b;
    switch (c->kind) {
    case FACTDB_C_PATTERN:
    {
        const char *src = "$";
        if (q) {
            U32 i, k = 0;
            for (i = 0; i < q->nin; i++)
                if (q->in[i].kind == FACTDB_I_SRC && k++ == c->src) src = q->in[i].name;
        }
        av_push(row, newSVpvs("pattern"));
        av_push(row, newSVpv(src, 0));
        av_push(row, factdb_dump_terms(aTHX_ c->terms, c->nterms, vars));
    }
        break;
    case FACTDB_C_PRED:
        av_push(row, newSVpvs("pred"));
        av_push(row, newSVpv(c->name, 0));
        av_push(row, factdb_dump_terms(aTHX_ c->terms, c->nterms, vars));
        break;
    case FACTDB_C_FN: {
        static const char *const bk[] = { NULL, "scalar", "coll", "tuple", "rel" };
        AV *bind = newAV();
        av_push(row, newSVpvs("fn"));
        av_push(row, newSVpv(c->name, 0));
        av_push(row, factdb_dump_terms(aTHX_ c->terms, c->nterms, vars));
        av_push(bind, newSVpv(bk[c->bkind], 0));
        av_push(bind, factdb_dump_terms(aTHX_ c->bind, c->nbind, vars));
        av_push(row, newRV_noinc((SV *)bind));
        break;
    }
    case FACTDB_C_RULE:
        av_push(row, newSVpvs("rule"));
        av_push(row, newSVpv(c->name, 0));
        av_push(row, factdb_dump_terms(aTHX_ c->terms, c->nterms, vars));
        break;
    case FACTDB_C_NOT:
        av_push(row, newSVpvs("not"));
        av_push(row, c->has_join ? factdb_dump_terms(aTHX_ c->join, c->njoin, vars) : newSV(0));
        av_push(row, factdb_dump_clauses(aTHX_ c->body, c->nbody, vars, q));
        break;
    case FACTDB_C_OR: {
        AV *branches = newAV();
        av_push(row, newSVpvs("or"));
        av_push(row, c->has_join ? factdb_dump_terms(aTHX_ c->join, c->njoin, vars) : newSV(0));
        for (b = 0; b < c->nbranches; b++)
            av_push(branches, factdb_dump_clauses(aTHX_ c->branches[b].clauses, c->branches[b].n, vars, q));
        av_push(row, newRV_noinc((SV *)branches));
        break;
    }
    }
    return newRV_noinc((SV *)row);
}

static SV *factdb_dump_clauses(pTHX_ const factdb_clause *cs, U32 n, const char *const *vars, const factdb_query *q) {
    AV *out = newAV();
    U32 i;
    for (i = 0; i < n; i++) av_push(out, factdb_dump_clause(aTHX_ &cs[i], vars, q));
    return newRV_noinc((SV *)out);
}

static SV *factdb_dump_query(pTHX_ const factdb_query *q) {
    static const char *const fk[] = { NULL, "rel", "scalar", "coll", "tuple" };
    static const char *const ik[] = { NULL, "src", "rules", "scalar", "coll", "tuple", "rel" };
    HV *out = newHV();
    AV *find = newAV(), *in = newAV(), *vars = newAV();
    U32 i;
    for (i = 0; i < q->nfind; i++) {
        const factdb_find *f = &q->find[i];
        AV *el = newAV();
        if (f->is_agg) {
            av_push(el, newSVpvs("agg"));
            av_push(el, newSVpv(f->agg, 0));
            av_push(el, factdb_dump_terms(aTHX_ f->args, f->nargs, q->vars));
        }
        else { av_push(el, newSVpvs("var")); av_push(el, newSVpv(q->vars[f->var], 0)); }
        av_push(find, newRV_noinc((SV *)el));
    }
    for (i = 0; i < q->nin; i++) {
        const factdb_input *x = &q->in[i];
        AV *el = newAV();
        av_push(el, newSVpv(ik[x->kind], 0));
        if (x->kind == FACTDB_I_SRC) av_push(el, newSVpv(x->name, 0));
        else if (x->kind != FACTDB_I_RULES) av_push(el, factdb_dump_terms(aTHX_ x->vars, x->nvars, q->vars));
        av_push(in, newRV_noinc((SV *)el));
    }
    for (i = 0; i < q->nvars; i++) av_push(vars, newSVpv(q->vars[i], 0));
    (void)hv_stores(out, "find_kind", newSVpv(fk[q->find_kind], 0));
    (void)hv_stores(out, "find", newRV_noinc((SV *)find));
    (void)hv_stores(out, "with", factdb_dump_terms(aTHX_ q->with, q->nwith, q->vars));
    (void)hv_stores(out, "in", newRV_noinc((SV *)in));
    (void)hv_stores(out, "where", factdb_dump_clauses(aTHX_ q->where, q->nwhere, q->vars, q));
    (void)hv_stores(out, "vars", newRV_noinc((SV *)vars));
    return newRV_noinc((SV *)out);
}

static SV *factdb_dump_rules(pTHX_ const factdb_ruleset *rs) {
    AV *out = newAV();
    U32 i;
    for (i = 0; i < rs->n; i++) {
        const factdb_rule *r = &rs->rules[i];
        AV *row = newAV();
        av_push(row, newSVpv(r->name, 0));
        av_push(row, factdb_dump_terms(aTHX_ r->params, r->nparams, r->vars));
        av_push(row, factdb_dump_clauses(aTHX_ r->body, r->nbody, r->vars, NULL));
        av_push(out, newRV_noinc((SV *)row));
    }
    return newRV_noinc((SV *)out);
}

#endif
