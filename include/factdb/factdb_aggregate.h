#ifndef FACTDB_AGGREGATE_H
#define FACTDB_AGGREGATE_H

/* Aggregates (F19), as Datomic has them. The rows are first made a set over
 * the variables the result names: the :find variables, each aggregate's
 * variable, and the :with variables. Then the :find variables that are not
 * aggregated group the set, and each aggregate is worked out over its group.
 * So (sum ?age) over two people of 30 is 30: the set has one row, ?age 30.
 * With :with ?e it has two, and the sum is 60. */

static SV *factdb_agg_num_sv(pTHX_ NV nv, int whole) {
    return factdb_num_sv(aTHX_ nv, whole);
}

typedef struct factdb_aggctx {
    factdb_run *run;
    const factdb_find *f;
} factdb_aggctx;

static void factdb_agg_err(pTHX_ const factdb_aggctx *x, const char *msg, const factdb_val *v) {
    factdb_at(aTHX_ x->run->what, x->f->at->line, x->f->at->col, "%s %s%s%" SVf, x->f->agg, msg,
              v ? ", not " : "", SVfARG(v ? factdb_quote(aTHX_ factdb_val_sv(aTHX_ v)) : sv_2mortal(newSVpvs(""))));
}

/* The values of a group's rows for the aggregate's variable. */
static factdb_val *factdb_agg_vals(pTHX_ const factdb_rows *set, const IV *members, IV n, U32 var) {
    factdb_val *vals;
    IV i;
    Newx(vals, n ? n : 1, factdb_val);
    SAVEFREEPV(vals);
    for (i = 0; i < n; i++) vals[i] = factdb_row_at(set, members[i])[var];
    return vals;
}

static const factdb_aggctx *factdb_sort_ctx;

static int factdb_sort_cmp(const void *a, const void *b) {
    dTHX;
    int c = factdb_val_cmp(aTHX_ (const factdb_val *)a, (const factdb_val *)b);
    if (c == 2) factdb_agg_err(aTHX_ factdb_sort_ctx, "compares values of one kind: numbers, or strings", (const factdb_val *)b);
    return c;
}

/* One aggregate over a group: the rows of `set` named by `members`. */
static SV *factdb_agg_one(pTHX_ factdb_run *run, const factdb_find *f, const factdb_rows *set, const IV *members, IV n) {
    factdb_aggctx x;
    const factdb_term *arg = &f->args[f->nargs - 1];
    factdb_val *vals = factdb_agg_vals(aTHX_ set, members, n, arg->var);
    const char *a = f->agg;
    IV i;
    x.run = run;
    x.f = f;
    if (strEQ(a, "count")) return sv_2mortal(newSViv(n));
    if (strEQ(a, "count-distinct") || strEQ(a, "distinct")) {
        HV *seen = (HV *)sv_2mortal((SV *)newHV());
        SV *key = sv_newmortal();
        AV *out = (AV *)sv_2mortal((SV *)newAV());
        for (i = 0; i < n; i++) {
            sv_setpvs(key, "");
            factdb_val_key(aTHX_ key, &vals[i]);
            if (hv_exists(seen, SvPVX(key), (I32)SvCUR(key))) continue;
            (void)hv_store(seen, SvPVX(key), (I32)SvCUR(key), &PL_sv_yes, 0);
            av_push(out, newSVsv(factdb_val_sv(aTHX_ &vals[i])));
        }
        if (a[0] == 'c') return sv_2mortal(newSViv(av_len(out) + 1));
        return sv_2mortal(newRV_inc((SV *)out));
    }
    if (strEQ(a, "sum") || strEQ(a, "avg")) {
        NV acc = 0, nv;
        IV iv;
        int whole = 1, is_int;
        for (i = 0; i < n; i++) {
            if (!factdb_val_num(aTHX_ &vals[i], &nv, &iv, &is_int)) factdb_agg_err(aTHX_ &x, "takes numbers", &vals[i]);
            if (!is_int) whole = 0;
            acc += nv;
        }
        if (a[0] == 's') return factdb_agg_num_sv(aTHX_ acc, whole);
        if (!n) return &PL_sv_undef;
        return sv_2mortal(newSVnv(acc / (NV)n));
    }
    if (strEQ(a, "median")) {
        NV *ns, nv;
        IV iv;
        int is_int;
        if (!n) return &PL_sv_undef;
        Newx(ns, n, NV);
        SAVEFREEPV(ns);
        for (i = 0; i < n; i++) {
            if (!factdb_val_num(aTHX_ &vals[i], &nv, &iv, &is_int)) factdb_agg_err(aTHX_ &x, "takes numbers", &vals[i]);
            ns[i] = nv;
        }
        {
            IV j, k;
            for (j = 1; j < n; j++) {
                NV t = ns[j];
                for (k = j - 1; k >= 0 && ns[k] > t; k--) ns[k + 1] = ns[k];
                ns[k + 1] = t;
            }
        }
        if (n & 1) return sv_2mortal(newSVnv(ns[n / 2]));
        return sv_2mortal(newSVnv((ns[n / 2 - 1] + ns[n / 2]) / 2));
    }
    if (strEQ(a, "min") || strEQ(a, "max")) {
        int want = a[1] == 'i' ? -1 : 1;
        if (f->nargs == 2) {
            IV take = f->args[0].c->iv, k;
            AV *out = (AV *)sv_2mortal((SV *)newAV());
            const factdb_aggctx *was = factdb_sort_ctx;
            factdb_sort_ctx = &x;
            qsort(vals, (size_t)n, sizeof(factdb_val), factdb_sort_cmp);
            factdb_sort_ctx = was;
            for (k = 0; k < take && k < n; k++) {
                IV at = want < 0 ? k : n - 1 - k;
                av_push(out, newSVsv(factdb_val_sv(aTHX_ &vals[at])));
            }
            return sv_2mortal(newRV_inc((SV *)out));
        }
        {
            const factdb_val *best = NULL;
            for (i = 0; i < n; i++) {
                int c;
                if (!best) { best = &vals[i]; continue; }
                c = factdb_val_cmp(aTHX_ &vals[i], best);
                if (c == 2) factdb_agg_err(aTHX_ &x, "compares values of one kind: numbers, or strings", &vals[i]);
                if (c == want) best = &vals[i];
            }
            return best ? factdb_val_sv(aTHX_ best) : &PL_sv_undef;
        }
    }
    factdb_agg_err(aTHX_ &x, "is not an aggregate", NULL);
    return &PL_sv_undef;
}

/* The result of a query whose :find has an aggregate. */
static SV *factdb_project_agg(pTHX_ factdb_run *run, const factdb_rows *rows) {
    const factdb_query *q = run->q;
    char *named;
    U32 *vars, nnamed = 0, i, v;
    factdb_rows set;
    HV *seen = (HV *)sv_2mortal((SV *)newHV());
    HV *groups = (HV *)sv_2mortal((SV *)newHV());
    AV *order = (AV *)sv_2mortal((SV *)newAV());
    SV *key = sv_newmortal();
    AV *out = (AV *)sv_2mortal((SV *)newAV());
    IV r, g, ngroups;
    int any_group = 0;

    Newxz(named, q->nvars ? q->nvars : 1, char);
    SAVEFREEPV(named);
    for (i = 0; i < q->nfind; i++) {
        if (q->find[i].is_agg) named[q->find[i].args[q->find[i].nargs - 1].var] = 1;
        else { named[q->find[i].var] = 1; any_group = 1; }
    }
    for (i = 0; i < q->nwith; i++) named[q->with[i].var] = 1;
    Newx(vars, q->nvars ? q->nvars : 1, U32);
    SAVEFREEPV(vars);
    for (v = 0; v < q->nvars; v++) if (named[v]) vars[nnamed++] = v;

    /* the set, over the named variables */
    factdb_rows_init(aTHX_ &set, rows->width);
    for (r = 0; r < rows->n; r++) {
        const factdb_val *row = factdb_row_at(rows, r);
        sv_setpvs(key, "");
        for (v = 0; v < nnamed; v++) factdb_val_key(aTHX_ key, &row[vars[v]]);
        if (hv_exists(seen, SvPVX(key), (I32)SvCUR(key))) continue;
        (void)hv_store(seen, SvPVX(key), (I32)SvCUR(key), &PL_sv_yes, 0);
        factdb_rows_push(aTHX_ &set, row);
    }

    /* the groups, in the order first seen: each a list of rows of the set */
    for (r = 0; r < set.n; r++) {
        const factdb_val *row = factdb_row_at(&set, r);
        SV **slot;
        sv_setpvs(key, "");
        for (i = 0; i < q->nfind; i++) if (!q->find[i].is_agg) factdb_val_key(aTHX_ key, &row[q->find[i].var]);
        slot = hv_fetch(groups, SvPVX(key), (I32)SvCUR(key), 1);
        if (!SvOK(*slot)) {
            SV *members = newSVpvs("");
            sv_setiv(*slot, av_len(order) + 1);
            av_push(order, members);
        }
        {
            SV *members = *av_fetch(order, SvIV(*slot), 0);
            sv_catpvn(members, (const char *)&r, sizeof(IV));
        }
    }
    ngroups = av_len(order) + 1;
    if (!ngroups && !any_group) {
        av_push(order, newSVpvs(""));
        ngroups = 1;
    }

    for (g = 0; g < ngroups; g++) {
        SV *members = *av_fetch(order, g, 0);
        const IV *m = (const IV *)SvPVX(members);
        IV n = (IV)(SvCUR(members) / sizeof(IV));
        AV *t = newAV();
        SV *tuple = sv_2mortal(newRV_noinc((SV *)t));
        for (i = 0; i < q->nfind; i++) {
            const factdb_find *f = &q->find[i];
            if (f->is_agg) av_push(t, newSVsv(factdb_agg_one(aTHX_ run, f, &set, m, n)));
            else av_push(t, newSVsv(factdb_val_sv(aTHX_ &factdb_row_at(&set, m[0])[f->var])));
        }
        if (q->find_kind == FACTDB_F_SCALAR) return newSVsv(*av_fetch(t, 0, 0));
        if (q->find_kind == FACTDB_F_TUPLE) return newSVsv(tuple);
        if (q->find_kind == FACTDB_F_COLL) av_push(out, newSVsv(*av_fetch(t, 0, 0)));
        else av_push(out, newSVsv(tuple));
    }
    if (q->find_kind == FACTDB_F_SCALAR || q->find_kind == FACTDB_F_TUPLE) return newSV(0);
    return newRV_inc((SV *)out);
}

#endif
