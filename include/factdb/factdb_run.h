#ifndef FACTDB_RUN_H
#define FACTDB_RUN_H

/* The bodies of the methods that the C ABI calls too: a query run, a plan
 * explained, the datoms of an index. The XSUBs and the table's entries are
 * thin around these. */

/* Runs a parsed query against `self`. `args` are the inputs after `self`,
 * in the order of :in. The result, mortal, as q returns it. */
static SV *factdb_query_run(pTHX_ SV *self, factdb_query *q, SV **args, I32 nargs, HV *functions, const char *what) {
    factdb_run run;
    factdb_rows rows;
    SV **given, *rules_text = NULL;
    I32 next = 0;
    U32 i, s = 0;
    factdb_db **srcs;
    Zero(&run, 1, factdb_run);
    run.what = what;
    run.q = q;
    run.nsrc = q->nsrc;
    Newxz(srcs, q->nsrc ? q->nsrc : 1, factdb_db *);
    SAVEFREEPV(srcs);
    run.src = srcs;
    Newxz(given, q->nin ? q->nin : 1, SV *);
    SAVEFREEPV(given);
    for (i = 0; i < q->nin; i++) {
        const factdb_input *in = &q->in[i];
        SV *arg;

        if (in->kind == FACTDB_I_SRC && s == 0) {
            srcs[s++] = factdb_db_handle(aTHX_ self, what);
            continue;
        }
        if (next >= nargs)
            croak("%s: the query's :in takes %u inputs after this value, and was given %d", what,
                  (unsigned)(q->nin - (q->nsrc ? 1 : 0)), (int)nargs);
        arg = args[next++];
        given[i] = arg;
        if (in->kind == FACTDB_I_SRC) srcs[s++] = factdb_db_handle(aTHX_ arg, what);
        else if (in->kind == FACTDB_I_RULES) rules_text = arg;
    }
    if (next < nargs)
        croak("%s: the query's :in takes %u inputs after this value, and was given %d", what,
              (unsigned)(q->nin - (q->nsrc ? 1 : 0)), (int)nargs);
    if (q->has_rules) {
        factdb_owned *ro;
        factdb_ruleset *rs;
        U32 r;
        (void)factdb_owned_new(aTHX_ &ro);
        rs = factdb_parse_rules(aTHX_ factdb_read(aTHX_ rules_text, &ro->arena, "Fact::DB::Db::q rules"), &ro->arena, "Fact::DB::Db::q rules");
        for (r = 0; r < rs->n; r++)
            factdb_calls_check(aTHX_ rs->rules[r].body, rs->rules[r].nbody, rs, 1, "Fact::DB::Db::q rules");
        factdb_stratify(aTHX_ rs, "Fact::DB::Db::q rules");
        factdb_calls_check(aTHX_ q->where, q->nwhere, rs, 1, what);
        run.rules = rs;
    }
    run.goals = (HV *)sv_2mortal((SV *)newHV());
    run.held = (AV *)sv_2mortal((SV *)newAV());
    run.functions = functions;
    factdb_rows_init(aTHX_ &rows, q->nvars);
    factdb_run_inputs(aTHX_ &run, given, &rows);
    factdb_run_clauses(aTHX_ &run, q->where, q->nwhere, &rows);
    return sv_2mortal(factdb_project(aTHX_ &run, &rows));
}

/* The options hash of q: its functions, or NULL. */
static HV *factdb_query_opts(pTHX_ SV *opts, const char *what) {
    HV *o = (HV *)SvRV(opts), *functions = NULL;
    HE *he;
    hv_iterinit(o);
    while ((he = hv_iternext(o))) {
        const char *k = SvPV_nolen(hv_iterkeysv(he));
        if (strEQ(k, "functions")) {
            SV *f = HeVAL(he);
            if (!SvROK(f) || SvTYPE(SvRV(f)) != SVt_PVHV)
                croak("%s: functions is a hash reference of names to subs", what);
            functions = (HV *)SvRV(f);
        }
        else croak("%s: unknown option \"%s\"; the option is functions", what, k);
    }
    return functions;
}

/* The plan of a parsed query against `d`, as text, mortal. */
static SV *factdb_query_explain(pTHX_ factdb_db *d, factdb_query *q, const char *what) {
    factdb_run run;
    factdb_db **srcs;
    U32 i;
    Zero(&run, 1, factdb_run);
    run.what = what;
    run.q = q;
    Newxz(srcs, q->nsrc ? q->nsrc : 1, factdb_db *);
    SAVEFREEPV(srcs);
    for (i = 0; i < q->nsrc; i++) srcs[i] = d;
    run.src = srcs;
    return factdb_explain(aTHX_ &run);
}

/* The datoms of `d` an index and up to three components name, as datoms
 * returns them: a mortal list of rows. */
static AV *factdb_datoms_av(pTHX_ factdb_db *d, SV *index, SV **parts, int np, const char *what) {
    const factdb_schema *s;
    factdb_collect col;
    const char *ix;
    IV e = 0, target = 0;
    int a = -1, have_v = 0, have_e = 0, nparts, all = 0;
    SV *venc = NULL, *v = NULL;
    int utf8 = 0;
    STRLEN vl = 0;
    const char *vp = NULL;
    s = &d->core->schema;
    col.s = s;
    col.out = (AV *)sv_2mortal((SV *)newAV());
    ix = SvOK(index) ? SvPV_nolen(index) : "";
    nparts = np;
    if (nparts > 3) croak("%s: an index takes at most three components", what);
    if ((strEQ(ix, "eav") || strEQ(ix, "aev")) && nparts == 0) all = 1;
    else if (strEQ(ix, "eav")) {
        e = factdb_read_entity(aTHX_ d, parts[0], what);
        if (nparts > 1) a = factdb_read_attr(aTHX_ d, parts[1], what);
        if (nparts > 2) { v = parts[2]; have_v = 1; }
    }
    else if (strEQ(ix, "aev")) {
        a = factdb_read_attr(aTHX_ d, parts[0], what);
        if (nparts > 1) { e = factdb_read_entity(aTHX_ d, parts[1], what); have_e = 1; }
        if (nparts > 2) { v = parts[2]; have_v = 1; }
    }
    else if (strEQ(ix, "ave")) {
        if (nparts < 1) croak("%s: ave needs an attribute", what);
        a = factdb_read_attr(aTHX_ d, parts[0], what);
        if (!s->attrs[a].indexed)
            croak("%s: %" SVf " is not indexed, so it has no ave; aev reads it from every entity",
                  what, SVfARG(s->attrs[a].name));
        if (nparts > 1) { v = parts[1]; have_v = 1; }
        if (nparts > 2) { e = factdb_read_entity(aTHX_ d, parts[2], what); have_e = 1; }
    }
    else if (strEQ(ix, "vae")) {
        if (nparts < 1) croak("%s: vae needs the entity referred to", what);
        target = factdb_read_entity(aTHX_ d, parts[0], what);
        if (nparts > 1) {
            a = factdb_read_attr(aTHX_ d, parts[1], what);
            if (s->attrs[a].type != FACTDB_T_REF)
                croak("%s: %" SVf " is not a ref", what, SVfARG(s->attrs[a].name));
        }
        if (nparts > 2) { e = factdb_read_entity(aTHX_ d, parts[2], what); have_e = 1; }
    }
    else croak("%s: unknown index %" SVf "; the indexes are eav, aev, ave and vae",
               what, SVfARG(factdb_quote(aTHX_ index)));

    if (have_v) {
        SV *why;
        venc = sv_newmortal();
        why = factdb_encode(aTHX_ s->attrs[a].type, v, venc, &utf8);
        if (why) croak("%s: %" SVf ": %" SVf, what, SVfARG(s->attrs[a].name), SVfARG(why));
        vp = SvPV(venc, vl);
    }

    if (d->kind == FACTDB_KIND_HISTORY) {
        factdb_hfilter f;
        char tv[24];
        f.s = s;
        f.e = have_e || strEQ(ix, "eav") ? e : 0;
        f.a = a;
        f.v = vp;
        f.vl = vl;
        f.vflags = utf8 ? FACTDB_F_UTF8 : 0;
        f.refs = 0;
        f.out = col.out;
        if (all) { f.e = 0; f.a = -1; f.v = NULL; }
        else if (strEQ(ix, "vae")) {
            f.vl = factdb_ekey(target, tv);
            f.v = tv;
            f.vflags = 0;
            f.refs = 1;
        }
        factdb_log_each(aTHX_ d, 0, factdb_history_one, &f);
    }
    else if (all)
        factdb_scan_all(aTHX_ d, factdb_collect_datom, &col);
    else if (strEQ(ix, "vae")) {
        if (have_e) {
            char tv[24];
            STRLEN tl = factdb_ekey(target, tv);
            U32 i;
            for (i = 0; i < s->n; i++) {
                if (s->attrs[i].type != FACTDB_T_REF || (a >= 0 && (U32)a != i)) continue;
                factdb_scan_e(aTHX_ d, e, (int)i, tv, tl, 0, factdb_collect_datom, &col);
            }
        }
        else factdb_scan_v(aTHX_ d, target, a, factdb_collect_datom, &col);
    }
    else if (strEQ(ix, "eav") || have_e)
        factdb_scan_e(aTHX_ d, e, a, vp, vl, utf8 ? FACTDB_F_UTF8 : 0, factdb_collect_datom, &col);
    else if (have_v)
        factdb_scan_av(aTHX_ d, (U16)a, vp, vl, utf8 ? FACTDB_F_UTF8 : 0, factdb_collect_datom, &col);
    else
        factdb_scan_a(aTHX_ d, (U16)a, factdb_collect_datom, &col);
    return col.out;
}

#endif
