#ifndef FACTDB_VALUE_H
#define FACTDB_VALUE_H

/* The value encoding: one canonical byte string per value, fixed by the
 * attribute's type. It is the key a value is indexed under and the form a
 * record keeps it in, so it must give equal values one string and read back
 * as exactly the value encoded. The stored value is what the encoding reads
 * back as, not what was given: an int given as '042' is 42 from then on. */

#ifdef USE_QUADMATH
#  include <quadmath.h>
#endif
#include <locale.h>

#define FACTDB_T_STRING  1
#define FACTDB_T_INT     2
#define FACTDB_T_NUM     3
#define FACTDB_T_BOOL    4
#define FACTDB_T_REF     5
#define FACTDB_T_INSTANT 6
#define FACTDB_T_MAX     6

static const char *const factdb_type_names[FACTDB_T_MAX + 1] = {
    NULL, "string", "int", "num", "bool", "ref", "instant"
};

/* A type by name, or 0. */
static int factdb_type_by_name(const char *name, STRLEN len) {
    int t;
    for (t = 1; t <= FACTDB_T_MAX; t++)
        if (strlen(factdb_type_names[t]) == len && memEQ(factdb_type_names[t], name, len)) return t;
    return 0;
}

/* A value, quoted and cut short, for a message. */
static SV *factdb_quote(pTHX_ SV *sv) {
    SV *q = sv_2mortal(newSVpvs(""));
    STRLEN len;
    const char *p;
    if (!SvOK(sv)) { sv_setpvs(q, "undef"); return q; }
    if (SvROK(sv)) { sv_setpvs(q, "a reference"); return q; }
    p = SvPV(sv, len);
    pv_pretty(q, p, len, 40, NULL, NULL,
              PERL_PV_PRETTY_QUOTE | PERL_PV_PRETTY_ELLIPSES | (SvUTF8(sv) ? PERL_PV_ESCAPE_UNI : 0));
    return q;
}

/* ---- whole numbers ---------------------------------------------------- */

/* A scalar as a whole number. 1 with *iv or *uv set (*is_uv saying which;
 * a UV only above IV_MAX), 0 if it is not a whole number perl can read, -1
 * if it is one but outside the perl's integer range. */
static int factdb_whole(pTHX_ SV *sv, IV *iv, UV *uv, int *is_uv) {
    NV nv;
    *is_uv = 0;
    if (SvIOKp(sv) && !SvNOKp(sv) && !SvPOKp(sv)) {
        if (SvIsUV(sv) && SvUVX(sv) > (UV)IV_MAX) { *uv = SvUVX(sv); *is_uv = 1; }
        else *iv = SvIVX(sv);
        return 1;
    }
    if (SvPOKp(sv)) {
        STRLEN len;
        const char *p = SvPV(sv, len);
        UV val = 0;
        int flags = grok_number(p, len, &val);
        if (!flags || (flags & (IS_NUMBER_INFINITY | IS_NUMBER_NAN))) return 0;
        if ((flags & IS_NUMBER_IN_UV) && !(flags & IS_NUMBER_NOT_INT)) {
            if (flags & IS_NUMBER_NEG) {
                if (val > (UV)IV_MAX + 1) return -1;
                *iv = val == (UV)IV_MAX + 1 ? IV_MIN : -(IV)val;
            }
            else if (val > (UV)IV_MAX) { *uv = val; *is_uv = 1; }
            else *iv = (IV)val;
            return 1;
        }
        if (!(flags & IS_NUMBER_NOT_INT) && !(flags & IS_NUMBER_GREATER_THAN_UV_MAX)) return 0;
        nv = SvNV(sv);
    }
    else if (SvNOKp(sv)) nv = SvNVX(sv);
    else if (SvIOKp(sv)) {
        if (SvIsUV(sv) && SvUVX(sv) > (UV)IV_MAX) { *uv = SvUVX(sv); *is_uv = 1; }
        else *iv = SvIVX(sv);
        return 1;
    }
    else return 0;

    if (Perl_isnan(nv) || Perl_isinf(nv)) return 0;
    if (nv != Perl_floor(nv)) return 0;
    if (nv < 0) {
        if (nv < (NV)IV_MIN) return -1;
        *iv = (IV)nv;
        return (NV)*iv == nv ? 1 : -1;
    }
    if (nv >= (NV)UV_MAX) return -1;
    if (nv <= (NV)IV_MAX) {
        *iv = (IV)nv;
        if ((NV)*iv == nv) return 1;
    }
    *uv = (UV)nv;
    *is_uv = 1;
    return (NV)*uv == nv ? 1 : -1;
}

/* ---- floating point --------------------------------------------------- */

#ifdef USE_QUADMATH
#  define FACTDB_NV_DIG 33
#else
#  define FACTDB_NV_DIG NV_DIG
#endif

/* An encoding read back, by the C library: perl's own my_atof is not
 * correctly rounded before 5.30, and a num must read back as exactly the
 * number it encodes. The C library reads the radix of the process's
 * LC_NUMERIC, so a '.' is given to it as that. */
static NV factdb_read_nv(pTHX_ const char *p, STRLEN len) {
    char buf[64];
    STRLEN i;
    char radix = '.';
#ifdef HAS_LOCALECONV
    {
        const struct lconv *lc = localeconv();
        if (lc && lc->decimal_point && lc->decimal_point[0] && !lc->decimal_point[1])
            radix = lc->decimal_point[0];
    }
#endif
    PERL_UNUSED_CONTEXT;
    if (len >= sizeof buf) len = sizeof buf - 1;
    for (i = 0; i < len; i++) buf[i] = p[i] == '.' ? radix : p[i];
    buf[len] = '\0';
#if defined(USE_QUADMATH)
    return strtoflt128(buf, NULL);
#elif defined(USE_LONG_DOUBLE) && defined(HAS_STRTOLD)
    return (NV)strtold(buf, NULL);
#else
    return (NV)strtod(buf, NULL);
#endif
}

/* `nv` printed with `prec` significant digits, the radix always '.'. */
static STRLEN factdb_print_nv(pTHX_ char *buf, size_t size, int prec, NV nv) {
    int n;
    char *c;
#ifdef USE_QUADMATH
    n = quadmath_snprintf(buf, size, "%.*Qg", prec, nv);
#else
    n = my_snprintf(buf, size, "%.*" NVgf, prec, nv);
#endif
    if (n < 0 || (size_t)n >= size) return 0;
    for (c = buf; *c; c++)
        if (!isDIGIT(*c) && *c != '-' && *c != '+' && *c != 'e' && *c != 'E') *c = '.';
    return (STRLEN)n;
}

/* The fewest significant digits that read back as exactly `nv`. */
static STRLEN factdb_canonical_nv(pTHX_ char *buf, size_t size, NV nv) {
    int prec;
    STRLEN n = 0;
    for (prec = FACTDB_NV_DIG; prec <= FACTDB_NV_DIG + 6; prec++) {
        n = factdb_print_nv(aTHX_ buf, size, prec, nv);
        if (n && factdb_read_nv(aTHX_ buf, n) == nv) return n;
    }
    return n;
}

/* A scalar as a number. 1 with *nv set, 0 if it is not a number, -1 if it
 * is not a finite one. */
static int factdb_number(pTHX_ SV *sv, NV *nv) {
    if (SvPOKp(sv)) {
        STRLEN len;
        const char *p = SvPV(sv, len);
        UV val;
        int flags = grok_number(p, len, &val);
        if (!flags) return 0;
        if (flags & (IS_NUMBER_INFINITY | IS_NUMBER_NAN)) return -1;
        *nv = SvNV(sv);
    }
    else if (SvNOKp(sv)) *nv = SvNVX(sv);
    else if (SvIOKp(sv)) *nv = SvIsUV(sv) ? (NV)SvUVX(sv) : (NV)SvIVX(sv);
    else return 0;
    if (Perl_isnan(*nv) || Perl_isinf(*nv)) return -1;
    if (*nv == 0) *nv = 0;
    return 1;
}

/* ---- encode and decode ------------------------------------------------ */

/* Encodes `in` as type `type` into `out` (made a plain byte string), with
 * *utf8 saying whether the bytes are UTF-8. NULL when it worked, else why
 * not, mortal, for the caller to put a name in front of. */
static SV *factdb_encode(pTHX_ int type, SV *in, SV *out, int *utf8) {
    *utf8 = 0;
    if (SvGMAGICAL(in)) {
        SV *c = sv_newmortal();
        sv_setsv_flags(c, in, SV_GMAGIC | SV_NOSTEAL);
        in = c;
    }
    if (!SvOK(in))
        return sv_2mortal(newSVpvs("undef is not a value; retracting a fact is how it goes away"));
    if (SvROK(in) && type != FACTDB_T_REF)
        return sv_2mortal(newSVpvf("a reference is not a value of type %s", factdb_type_names[type]));

    switch (type) {
    case FACTDB_T_STRING: {
        STRLEN len;
        const char *p = SvPV(in, len);
        sv_setpvn(out, p, len);
        SvUTF8_off(out);
        if (SvUTF8(in)) {
            SvUTF8_on(out);
            if (!sv_utf8_downgrade(out, TRUE)) *utf8 = 1;
            SvUTF8_off(out);
        }
        return NULL;
    }
    case FACTDB_T_INT:
    case FACTDB_T_REF: {
        IV iv = 0;
        UV uv = 0;
        int is_uv, r;
        if (SvROK(in))
            return sv_2mortal(newSVpvs("a ref is an entity id, a positive whole number, not a reference"));
        r = factdb_whole(aTHX_ in, &iv, &uv, &is_uv);
        if (r == 0)
            return sv_2mortal(newSVpvf("%s is not a whole number", SvPVX(factdb_quote(aTHX_ in))));
        if (r < 0)
            return sv_2mortal(newSVpvf("%s is outside the range of a whole number here", SvPVX(factdb_quote(aTHX_ in))));
        if (type == FACTDB_T_REF && !is_uv && iv <= 0)
            return sv_2mortal(newSVpvf("a ref is an entity id, a positive whole number, not %s", SvPVX(factdb_quote(aTHX_ in))));
        if (is_uv) sv_setpvf(out, "%" UVuf, uv);
        else sv_setpvf(out, "%" IVdf, iv);
        SvUTF8_off(out);
        return NULL;
    }
    case FACTDB_T_NUM:
    case FACTDB_T_INSTANT: {
        NV nv = 0;
        char buf[64];
        STRLEN n;
        int r = factdb_number(aTHX_ in, &nv);
        if (r == 0)
            return sv_2mortal(newSVpvf("%s is not a number", SvPVX(factdb_quote(aTHX_ in))));
        if (r < 0)
            return sv_2mortal(newSVpvf("%s is not a finite number", SvPVX(factdb_quote(aTHX_ in))));
        n = factdb_canonical_nv(aTHX_ buf, sizeof buf, nv);
        sv_setpvn(out, buf, n);
        SvUTF8_off(out);
        return NULL;
    }
    case FACTDB_T_BOOL:
        sv_setpvn(out, SvTRUE(in) ? "1" : "0", 1);
        SvUTF8_off(out);
        return NULL;
    }
    return sv_2mortal(newSVpvs("unknown type"));
}

/* The value an encoding stands for, new. */
static SV *factdb_decode(pTHX_ int type, const char *p, STRLEN len, int utf8) {
    switch (type) {
    case FACTDB_T_STRING: {
        SV *sv = newSVpvn(p, len);
        if (utf8) SvUTF8_on(sv);
        return sv;
    }
    case FACTDB_T_INT:
    case FACTDB_T_REF: {
        UV val = 0;
        int flags = grok_number(p, len, &val);
        if (flags & IS_NUMBER_NEG)
            return newSViv(val == (UV)IV_MAX + 1 ? IV_MIN : -(IV)val);
        return val > (UV)IV_MAX ? newSVuv(val) : newSViv((IV)val);
    }
    case FACTDB_T_NUM:
    case FACTDB_T_INSTANT:
        return newSVnv(factdb_read_nv(aTHX_ p, len));
    case FACTDB_T_BOOL:
        return newSViv(len && p[0] == '1');
    }
    return newSV(0);
}

#endif
