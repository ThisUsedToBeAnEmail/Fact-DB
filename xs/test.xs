# Doors for the tests. Nothing here is part of the interface.

MODULE = Fact::DB    PACKAGE = Fact::DB

# The version of the table this library resolved when it loaded.
IV
_hamt_abi_version()
    CODE:
        RETVAL = FACTDB_HAMT ? FACTDB_HAMT->abi_version : 0;
    OUTPUT:
        RETVAL

# The version of the header it was compiled against.
IV
_hamt_abi_built()
    CODE:
        RETVAL = HAMT_ABI_VERSION;
    OUTPUT:
        RETVAL

# A map made, filled, read and given up through the table: how many keys it
# held, the value under the last key stored, and whether HAMT called it well
# formed. Proof that the table resolved is one that can be called. The keys
# are the arguments' positions, the values the arguments.
void
_hamt_roundtrip(...)
    PREINIT:
        hamt_map *m;
        I32 i;
        size_t n;
        SV *last = NULL;
        int bad;
        char key[32];
        STRLEN klen;
    PPCODE:
        m = (FACTDB_HAMT->map_new)(aTHX);
        if (!m) croak("Fact::DB::_hamt_roundtrip: out of memory");
        for (i = 0; i < items; i++) {
            klen = (STRLEN)my_snprintf(key, sizeof key, "%d", (int)i);
            if ((FACTDB_HAMT->map_store)(aTHX_ m, key, klen, 0, ST(i)) != HAMT_ABI_OK) {
                (FACTDB_HAMT->map_release)(aTHX_ m);
                croak("Fact::DB::_hamt_roundtrip: out of memory");
            }
        }
        n = (FACTDB_HAMT->map_count)(aTHX_ m);
        if (items) {
            klen = (STRLEN)my_snprintf(key, sizeof key, "%d", (int)(items - 1));
            last = (FACTDB_HAMT->map_fetch)(aTHX_ m, key, klen, 0);
        }
        last = last ? newSVsv(last) : newSV(0);
        bad  = (FACTDB_HAMT->map_check)(aTHX_ m);
        (FACTDB_HAMT->map_release)(aTHX_ m);
        EXTEND(SP, 3);
        PUSHs(sv_2mortal(newSVuv((UV)n)));
        PUSHs(sv_2mortal(last));
        PUSHs(sv_2mortal(newSViv(bad)));

# How many blocks this library holds for itself.
IV
_live_blocks()
    CODE:
        RETVAL = (IV)factdb_live_blocks;
    OUTPUT:
        RETVAL

# Takes `n` blocks through the library's allocator and gives them back:
# the count while they were held. The allocator has no other caller yet.
IV
_alloc_roundtrip(n)
        IV n
    PREINIT:
        void **held;
        IV i;
    CODE:
        if (n < 0 || n > 100000) croak("Fact::DB::_alloc_roundtrip: 0 to 100000 blocks");
        Newx(held, n ? n : 1, void *);
        for (i = 0; i < n; i++) held[i] = factdb_alloc(aTHX_ 16 + (size_t)i % 64);
        RETVAL = (IV)factdb_live_blocks;
        for (i = 0; i < n; i++) factdb_free(aTHX_ held[i]);
        Safefree(held);
    OUTPUT:
        RETVAL

# Calls `code` with the rest of the arguments through factdb_call, in list
# context, and returns what it returned; an error it raised is thrown again.
# This is the path every sub the database calls will take.
void
_call(code, ...)
        SV *code
    PREINIT:
        AV *many;
        SV *err;
        SSize_t i, n;
    PPCODE:
        many = (AV *)sv_2mortal((SV *)newAV());
        err = factdb_call(aTHX_ code, &ST(1), items - 1, G_LIST, NULL, many);
        if (err) factdb_rethrow(aTHX_ err);
        n = av_len(many) + 1;
        EXTEND(SP, n);
        for (i = 0; i < n; i++) PUSHs(sv_2mortal(newSVsv(*av_fetch(many, i, 0))));

# The same in scalar context.
SV *
_call_scalar(code, ...)
        SV *code
    PREINIT:
        SV *one;
        SV *err;
    CODE:
        err = factdb_call(aTHX_ code, &ST(1), items - 1, G_SCALAR, &one, NULL);
        if (err) factdb_rethrow(aTHX_ err);
        RETVAL = newSVsv(one);
    OUTPUT:
        RETVAL

# perl's temporaries stack, for a test that says a loop of calls leaves
# nothing on it.
IV
_tmps_ix()
    CODE:
        RETVAL = (IV)PL_tmps_ix;
    OUTPUT:
        RETVAL

# A value encoded as a type named by its name: the bytes and whether they
# are UTF-8. A refusal croaks with the reason.
void
_encode(type, value)
        SV *type
        SV *value
    PREINIT:
        STRLEN len;
        const char *p;
        int t, utf8;
        SV *out, *why;
    PPCODE:
        p = SvPV(type, len);
        t = factdb_type_by_name(p, len);
        if (!t) croak("Fact::DB::_encode: no type \"%" SVf "\"", SVfARG(type));
        out = sv_2mortal(newSV(0));
        why = factdb_encode(aTHX_ t, value, out, &utf8);
        if (why) croak("Fact::DB::_encode: %" SVf, SVfARG(why));
        EXTEND(SP, 2);
        PUSHs(out);
        PUSHs(sv_2mortal(newSViv(utf8)));

# The value an encoding stands for.
SV *
_decode(type, bytes, utf8)
        SV *type
        SV *bytes
        int utf8
    PREINIT:
        STRLEN len, blen;
        const char *p, *b;
        int t;
    CODE:
        p = SvPV(type, len);
        t = factdb_type_by_name(p, len);
        if (!t) croak("Fact::DB::_decode: no type \"%" SVf "\"", SVfARG(type));
        b = SvPV(bytes, blen);
        RETVAL = factdb_decode(aTHX_ t, b, blen, utf8);
    OUTPUT:
        RETVAL

# An attribute's id, or -1.
IV
_attr_id(self, name)
        SV *self
        SV *name
    CODE:
        RETVAL = factdb_attr_id(aTHX_ &factdb_conn_handle(aTHX_ self, "Fact::DB::_attr_id")->core->schema, name);
    OUTPUT:
        RETVAL

MODULE = Fact::DB    PACKAGE = Fact::DB

# How many database values the connection holds.
IV
_versions(self)
        SV *self
    CODE:
        RETVAL = factdb_conn_handle(aTHX_ self, "Fact::DB::_versions")->current->depth + 1;
    OUTPUT:
        RETVAL

MODULE = Fact::DB    PACKAGE = Fact::DB::Db

# Everything a database value holds, as plain Perl data, for tests:
#   eav  => { e => [ [attribute, value, tx], ... ] }   in record order
#   ave  => { attribute => { encoding => e, or [e, ...] in no order } }
#   log  => [ [tx, [ [e, attribute, value, added], ... ]], ... ]
#   basis_tx, next_id
SV *
_dump(self)
        SV *self
    PREINIT:
        factdb_db *d;
        const factdb_schema *s;
        HV *out, *eav, *ave;
        AV *log;
        U32 a;
        size_t i, n;
    CODE:
        d = factdb_db_handle(aTHX_ self, "Fact::DB::Db::_dump");
        s = &d->core->schema;
        out = newHV();
        eav = newHV();
        {
            HV *raw = (FACTDB_HAMT->map_to_hv)(aTHX_ d->eav);
            HE *he;
            hv_iterinit(raw);
            while ((he = hv_iternext(raw))) {
                SV *rec = HeVAL(he);
                STRLEN len;
                const char *p = SvPV(rec, len), *end = p + len;
                factdb_entry e;
                AV *facts = newAV();
                while (factdb_rec_next(&p, end, &e)) {
                    AV *row = newAV();
                    av_push(row, newSVsv(s->attrs[e.a].name));
                    av_push(row, factdb_decode(aTHX_ s->attrs[e.a].type, e.v, e.vlen, e.flags & FACTDB_F_UTF8));
                    av_push(row, newSVuv(e.tx));
                    av_push(facts, newRV_noinc((SV *)row));
                }
                (void)hv_store_ent(eav, hv_iterkeysv(he), newRV_noinc((SV *)facts), 0);
            }
            SvREFCNT_dec((SV *)raw);
        }
        ave = newHV();
        for (a = 0; a < d->nave; a++) {
            HV *raw, *by;
            HE *he;
            if (!d->ave[a]) continue;
            raw = (FACTDB_HAMT->map_to_hv)(aTHX_ d->ave[a]->m);
            by = newHV();
            hv_iterinit(raw);
            while ((he = hv_iternext(raw))) {
                SV *v = HeVAL(he);
                const hamt_map *set = SvROK(v) ? (FACTDB_HAMT->set_from_sv)(aTHX_ v) : NULL;
                if (set) {
                    HV *members = (FACTDB_HAMT->map_to_hv)(aTHX_ set);
                    AV *ids = newAV();
                    HE *m;
                    hv_iterinit(members);
                    while ((m = hv_iternext(members))) av_push(ids, newSVsv(hv_iterkeysv(m)));
                    SvREFCNT_dec((SV *)members);
                    (void)hv_store_ent(by, hv_iterkeysv(he), newRV_noinc((SV *)ids), 0);
                }
                else (void)hv_store_ent(by, hv_iterkeysv(he), newSVsv(v), 0);
            }
            SvREFCNT_dec((SV *)raw);
            (void)hv_store_ent(ave, s->attrs[a].name, newRV_noinc((SV *)by), 0);
        }
        log = newAV();
        n = (FACTDB_HAMT->vec_count)(aTHX_ d->log);
        for (i = 0; i < n; i++) {
            SV *rec = (FACTDB_HAMT->vec_fetch)(aTHX_ d->log, i);
            STRLEN len;
            const char *p = SvPV(rec, len), *end = p + len;
            UV tx = 0, count = 0, k;
            AV *entry = newAV(), *datoms = newAV();
            (void)factdb_get_varint(&p, end, &tx);
            (void)factdb_get_varint(&p, end, &count);
            for (k = 0; k < count; k++) {
                UV e = 0, vl = 0;
                U16 at;
                U8 fl;
                AV *row = newAV();
                (void)factdb_get_varint(&p, end, &e);
                at = (U16)((U8)p[0] | ((U8)p[1] << 8));
                fl = (U8)p[2];
                p += 3;
                (void)factdb_get_varint(&p, end, &vl);
                av_push(row, newSVuv(e));
                av_push(row, newSVsv(s->attrs[at].name));
                av_push(row, factdb_decode(aTHX_ s->attrs[at].type, p, (STRLEN)vl, fl & FACTDB_F_UTF8));
                av_push(row, newSViv(fl & FACTDB_F_ADDED ? 1 : 0));
                av_push(datoms, newRV_noinc((SV *)row));
                p += vl;
            }
            av_push(entry, newSVuv(tx));
            av_push(entry, newRV_noinc((SV *)datoms));
            av_push(log, newRV_noinc((SV *)entry));
        }
        (void)hv_stores(out, "eav", newRV_noinc((SV *)eav));
        (void)hv_stores(out, "ave", newRV_noinc((SV *)ave));
        (void)hv_stores(out, "log", newRV_noinc((SV *)log));
        (void)hv_stores(out, "basis_tx", newSViv(d->basis_tx));
        (void)hv_stores(out, "next_id", newSViv(d->next_id));
        RETVAL = newRV_noinc((SV *)out);
    OUTPUT:
        RETVAL

# Every index checked against every other, for tests: a list of what is
# wrong, empty when nothing is.
#   - every record parses, its entries in order, one entry at most for a
#     cardinality one attribute, of attributes in the schema, of ids given out
#   - every entry of an indexed attribute is in its AVE, and nothing else is
#   - the count of entries is the value's count, and the log's sum
#   - HAMT's own check passes on every map and set
SV *
_check(self)
        SV *self
    PREINIT:
        factdb_db *d;
        const factdb_schema *s;
        AV *bad;
        HV *raw;
        HE *he;
        IV entries = 0, indexed_entries = 0, ave_pairs = 0, logsum = 0;
        U32 a;
        size_t i, n;
    CODE:
        d = factdb_db_handle(aTHX_ self, "Fact::DB::Db::_check");
        if (d->kind == FACTDB_KIND_HISTORY) croak("Fact::DB::Db::_check: a history value is the log; check the value it came from");
        s = &d->core->schema;
        bad = newAV();
        if ((FACTDB_HAMT->map_check)(aTHX_ d->eav))
            av_push(bad, newSVpvs("eav fails HAMT's check"));
        raw = (FACTDB_HAMT->map_to_hv)(aTHX_ d->eav);
        hv_iterinit(raw);
        while ((he = hv_iternext(raw))) {
            I32 kl;
            char *k = hv_iterkey(he, &kl);
            UV e = 0;
            STRLEN len;
            const char *p = SvPV(HeVAL(he), len), *end = p + len;
            factdb_entry x, prev;
            int have_prev = 0;
            if (!grok_number(k, (STRLEN)kl, &e) || e == 0 || (IV)e >= d->next_id)
                av_push(bad, newSVpvf("eav has a record for %s, not an id given out", k));
            if (!len) av_push(bad, newSVpvf("entity %s has an empty record", k));
            while (factdb_rec_next(&p, end, &x)) {
                entries++;
                if (x.a >= s->n) { av_push(bad, newSVpvf("entity %s has attribute %u, not in the schema", k, (unsigned)x.a)); continue; }
                if (have_prev && factdb_entry_cmp(&prev, &x) >= 0)
                    av_push(bad, newSVpvf("entity %s: entries out of order at %" SVf, k, SVfARG(s->attrs[x.a].name)));
                if (have_prev && prev.a == x.a && !s->attrs[x.a].many)
                    av_push(bad, newSVpvf("entity %s holds two values of %" SVf, k, SVfARG(s->attrs[x.a].name)));
                if (x.tx == 0 || (IV)x.tx > d->basis_tx)
                    av_push(bad, newSVpvf("entity %s: %" SVf " asserted in transaction %" UVuf ", not one of this value's",
                                          k, SVfARG(s->attrs[x.a].name), x.tx));
                if (s->attrs[x.a].indexed) {
                    hamt_map *ave = factdb_ave_of(d, x.a);
                    SV *held = ave ? (FACTDB_HAMT->map_fetch)(aTHX_ ave, x.v, x.vlen, x.flags & FACTDB_F_UTF8) : NULL;
                    indexed_entries++;
                    if (!held) av_push(bad, newSVpvf("entity %s: %" SVf " is not in its ave", k, SVfARG(s->attrs[x.a].name)));
                    else if (SvROK(held)) {
                        if (!(FACTDB_HAMT->set_has)(aTHX_ (FACTDB_HAMT->set_from_sv)(aTHX_ held), k, (STRLEN)kl, 0))
                            av_push(bad, newSVpvf("entity %s: %" SVf "'s ave set lacks it", k, SVfARG(s->attrs[x.a].name)));
                    }
                    else if ((UV)SvIV(held) != e)
                        av_push(bad, newSVpvf("entity %s: %" SVf "'s ave names %" IVdf, k, SVfARG(s->attrs[x.a].name), SvIV(held)));
                }
                prev = x;
                have_prev = 1;
            }
            if (p != end) av_push(bad, newSVpvf("entity %s: record does not parse", k));
        }
        SvREFCNT_dec((SV *)raw);

        for (a = 0; a < d->nave; a++) {
            HV *by;
            if (!d->ave[a]) continue;
            if (!s->attrs[a].indexed) { av_push(bad, newSVpvf("%" SVf " is not indexed but has an ave", SVfARG(s->attrs[a].name))); continue; }
            if ((FACTDB_HAMT->map_check)(aTHX_ d->ave[a]->m))
                av_push(bad, newSVpvf("%" SVf "'s ave fails HAMT's check", SVfARG(s->attrs[a].name)));
            by = (FACTDB_HAMT->map_to_hv)(aTHX_ d->ave[a]->m);
            hv_iterinit(by);
            while ((he = hv_iternext(by))) {
                SV *held = HeVAL(he);
                SV *ksv = hv_iterkeysv(he);
                STRLEN vl;
                const char *v = SvPV(ksv, vl);
                U8 flags = SvUTF8(ksv) ? FACTDB_F_UTF8 : 0;
                factdb_entry found;
                if (SvROK(held)) {
                    const hamt_map *set = (FACTDB_HAMT->set_from_sv)(aTHX_ held);
                    HV *members;
                    HE *m;
                    if (s->attrs[a].unique) av_push(bad, newSVpvf("%" SVf " is unique but its ave holds a set", SVfARG(s->attrs[a].name)));
                    if (!set || (FACTDB_HAMT->set_check)(aTHX_ set)) { av_push(bad, newSVpvf("%" SVf "'s ave holds a bad set", SVfARG(s->attrs[a].name))); continue; }
                    if (!(FACTDB_HAMT->set_count)(aTHX_ set)) av_push(bad, newSVpvf("%" SVf "'s ave holds an empty set", SVfARG(s->attrs[a].name)));
                    members = (FACTDB_HAMT->map_to_hv)(aTHX_ set);
                    hv_iterinit(members);
                    while ((m = hv_iternext(members))) {
                        UV e = 0;
                        I32 ml;
                        char *mk = hv_iterkey(m, &ml);
                        ave_pairs++;
                        (void)grok_number(mk, (STRLEN)ml, &e);
                        if (!factdb_rec_find(aTHX_ factdb_record_of(aTHX_ d, (IV)e), (U16)a, flags, v, vl, &found))
                            av_push(bad, newSVpvf("%" SVf "'s ave names entity %s, which does not hold that value", SVfARG(s->attrs[a].name), mk));
                    }
                    SvREFCNT_dec((SV *)members);
                }
                else {
                    ave_pairs++;
                    if (!s->attrs[a].unique) av_push(bad, newSVpvf("%" SVf " is not unique but its ave holds an id", SVfARG(s->attrs[a].name)));
                    if (!factdb_rec_find(aTHX_ factdb_record_of(aTHX_ d, SvIV(held)), (U16)a, flags, v, vl, &found))
                        av_push(bad, newSVpvf("%" SVf "'s ave names entity %" IVdf ", which does not hold that value", SVfARG(s->attrs[a].name), SvIV(held)));
                }
            }
            SvREFCNT_dec((SV *)by);
        }
        if (ave_pairs != indexed_entries)
            av_push(bad, newSVpvf("the aves hold %" IVdf " pairs and the records %" IVdf " indexed entries", ave_pairs, indexed_entries));
        if (entries != d->ndatoms)
            av_push(bad, newSVpvf("the records hold %" IVdf " entries and the count says %" IVdf, entries, d->ndatoms));

        n = (FACTDB_HAMT->vec_count)(aTHX_ d->log);
        for (i = 0; i < n; i++) {
            SV *rec = (FACTDB_HAMT->vec_fetch)(aTHX_ d->log, i);
            STRLEN len;
            const char *p = SvPV(rec, len), *end = p + len;
            UV tx = 0, count = 0, k;
            (void)factdb_get_varint(&p, end, &tx);
            (void)factdb_get_varint(&p, end, &count);
            for (k = 0; k < count; k++) {
                UV e = 0, vl = 0;
                U8 fl;
                (void)factdb_get_varint(&p, end, &e);
                fl = (U8)p[2];
                p += 3;
                (void)factdb_get_varint(&p, end, &vl);
                p += vl;
                logsum += (fl & FACTDB_F_ADDED) ? 1 : -1;
            }
        }
        if (d->kind == FACTDB_KIND_VALUE && logsum != d->ndatoms)
            av_push(bad, newSVpvf("the log adds up to %" IVdf " datoms and the count says %" IVdf, logsum, d->ndatoms));
        if ((FACTDB_HAMT->vec_check)(aTHX_ d->log)) av_push(bad, newSVpvs("the log fails HAMT's check"));
        RETVAL = newRV_noinc((SV *)bad);
    OUTPUT:
        RETVAL

# The datoms of one attribute holding one value, by factdb_scan_av directly:
# through its AVE when it has one, else from every record. datoms() never
# asks this of an unindexed attribute; a query will.
SV *
_scan_av(self, attr, value)
        SV *self
        SV *attr
        SV *value
    PREINIT:
        factdb_db *d;
        factdb_collect col;
        U16 a;
        SV *enc, *why;
        int utf8;
        STRLEN vl;
        const char *vp;
    CODE:
        d = factdb_db_handle(aTHX_ self, "Fact::DB::Db::_scan_av");
        a = factdb_read_attr(aTHX_ d, attr, "Fact::DB::Db::_scan_av");
        enc = sv_newmortal();
        why = factdb_encode(aTHX_ d->core->schema.attrs[a].type, value, enc, &utf8);
        if (why) croak("Fact::DB::Db::_scan_av: %" SVf, SVfARG(why));
        vp = SvPV(enc, vl);
        col.s = &d->core->schema;
        col.out = (AV *)sv_2mortal((SV *)newAV());
        factdb_scan_av(aTHX_ d, a, vp, vl, utf8 ? FACTDB_F_UTF8 : 0, factdb_collect_datom, &col);
        RETVAL = newRV_inc((SV *)col.out);
    OUTPUT:
        RETVAL

MODULE = Fact::DB    PACKAGE = Fact::DB::Db

# How far back this value's line goes, and where its jump link points.
void
_line(self)
        SV *self
    PREINIT:
        factdb_db *d;
    PPCODE:
        d = factdb_db_handle(aTHX_ self, "Fact::DB::Db::_line");
        EXTEND(SP, 2);
        PUSHs(sv_2mortal(newSViv(d->depth)));
        PUSHs(sv_2mortal(newSViv(d->jump ? d->jump->depth : -1)));

# Bytes malloc says are in use, on macOS; -1 elsewhere.
NV
_malloc_in_use()
    CODE:
#ifdef __APPLE__
        {
            malloc_statistics_t st;
            malloc_zone_statistics(NULL, &st);
            RETVAL = (NV)st.size_in_use;
        }
#else
        RETVAL = -1;
#endif
    OUTPUT:
        RETVAL

# How many steps as_of takes from this value to the one at or before $t.
IV
_as_of_steps(self, t)
        SV *self
        IV t
    PREINIT:
        factdb_db *d;
        IV steps = 0;
    CODE:
        d = factdb_db_handle(aTHX_ self, "Fact::DB::Db::_as_of_steps");
        (void)factdb_db_as_of(d, t, &steps);
        RETVAL = steps;
    OUTPUT:
        RETVAL
