#ifndef FACTDB_EDN_H
#define FACTDB_EDN_H

/* The reader: query text to a tree of forms, the subset of EDN Datalog is
 * written in. Every form knows the line and column it starts at, counted in
 * characters from 1, so a message can say where. The text is read as UTF-8
 * (a byte string is upgraded first), and nesting stops at 256.
 *
 *   [ ... ]   vector        ( ... )  list        { ... }  map, in pairs
 *   :ns/name  keyword       name     symbol: ?x, $, $src, %, _, ..., ., >=
 *   "..."     string, with \" \\ \n \t \r \/ \b \f \uXXXX
 *   12 -3     integer       1.5 2e3  number
 *   true false nil
 *   ; to the end of a line is a comment, and a comma is a space */

#define FACTDB_EDN_VEC     1
#define FACTDB_EDN_LIST    2
#define FACTDB_EDN_MAP     3
#define FACTDB_EDN_KEYWORD 4
#define FACTDB_EDN_SYMBOL  5
#define FACTDB_EDN_STRING  6
#define FACTDB_EDN_INT     7
#define FACTDB_EDN_NUM     8
#define FACTDB_EDN_TRUE    9
#define FACTDB_EDN_FALSE   10
#define FACTDB_EDN_NIL     11

#define FACTDB_EDN_DEPTH 256

typedef struct factdb_edn {
    U8 type;
    U8 utf8;
    U32 line, col;
    const char *s;        /* keyword (no colon), symbol, string: NUL-ended */
    STRLEN len;
    IV iv;
    NV nv;
    struct factdb_edn **items;
    U32 n;
} factdb_edn;

typedef struct factdb_reader {
    const char *what;
    factdb_arena *arena;
    const char *p, *end;
    U32 line, col;
    int depth;
} factdb_reader;

static void factdb_at(pTHX_ const char *what, U32 line, U32 col, const char *fmt, ...)
    __attribute__format__(__printf__, pTHX_4, pTHX_5);

/* A croak that says where. */
static void factdb_at(pTHX_ const char *what, U32 line, U32 col, const char *fmt, ...) {
    va_list args;
    SV *msg = sv_2mortal(newSVpvf("%s: line %u, column %u: ", what, (unsigned)line, (unsigned)col));
    va_start(args, fmt);
    sv_vcatpvf(msg, fmt, &args);
    va_end(args);
    croak("%" SVf, SVfARG(msg));
}

static void factdb_rd_step(factdb_reader *r) {
    unsigned char ch = (unsigned char)*r->p++;
    if (ch == '\n') { r->line++; r->col = 1; }
    else if ((ch & 0xC0) != 0x80) r->col++;
}

static void factdb_rd_space(factdb_reader *r) {
    while (r->p < r->end) {
        char ch = *r->p;
        if (ch == ';') { while (r->p < r->end && *r->p != '\n') factdb_rd_step(r); }
        else if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == ',' || ch == '\f') factdb_rd_step(r);
        else break;
    }
}

static int factdb_sym_char(unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
        || ch >= 0x80 || (ch && strchr("*+!-_?<>=./$%&'#|", ch));
}

/* 1 when every byte is ASCII. (perl's is_ascii_string is not in 5.10.) */
static int factdb_ascii(const char *p, STRLEN len) {
    STRLEN i;
    for (i = 0; i < len; i++) if ((unsigned char)p[i] >= 0x80) return 0;
    return 1;
}

static factdb_edn *factdb_rd_node(pTHX_ factdb_reader *r, U8 type, U32 line, U32 col) {
    factdb_edn *n = (factdb_edn *)factdb_arena_get(aTHX_ r->arena, sizeof(factdb_edn));
    Zero(n, 1, factdb_edn);
    n->type = type;
    n->line = line;
    n->col = col;
    return n;
}

static factdb_edn *factdb_rd_form(pTHX_ factdb_reader *r);

static const char *factdb_closer_name(char close) {
    return close == ']' ? "]" : close == ')' ? ")" : "}";
}

static factdb_edn *factdb_rd_seq(pTHX_ factdb_reader *r, U8 type, char close) {
    U32 line = r->line, col = r->col;
    factdb_edn *n = factdb_rd_node(aTHX_ r, type, line, col);
    factdb_edn **items = NULL;
    U32 count = 0, cap = 0;
    if (++r->depth > FACTDB_EDN_DEPTH)
        factdb_at(aTHX_ r->what, line, col, "nested more than %d deep", FACTDB_EDN_DEPTH);
    factdb_rd_step(r);
    for (;;) {
        factdb_rd_space(r);
        if (r->p >= r->end)
            factdb_at(aTHX_ r->what, line, col, "this %c is never closed by a %s",
                      close == ']' ? '[' : close == ')' ? '(' : '{', factdb_closer_name(close));
        if (*r->p == close) { factdb_rd_step(r); break; }
        if (*r->p == ']' || *r->p == ')' || *r->p == '}')
            factdb_at(aTHX_ r->what, r->line, r->col, "a %c here, where the %c at line %u, column %u wants a %s",
                      *r->p, close == ']' ? '[' : close == ')' ? '(' : '{', (unsigned)line, (unsigned)col,
                      factdb_closer_name(close));
        if (count == cap) {
            factdb_edn **bigger;
            cap = cap ? cap * 2 : 8;
            bigger = (factdb_edn **)factdb_arena_get(aTHX_ r->arena, cap * sizeof(factdb_edn *));
            if (count) Copy(items, bigger, count, factdb_edn *);
            items = bigger;
        }
        items[count++] = factdb_rd_form(aTHX_ r);
    }
    if (type == FACTDB_EDN_MAP && (count & 1))
        factdb_at(aTHX_ r->what, line, col, "a { } holds pairs, and this one has a key with no value");
    r->depth--;
    n->items = items;
    n->n = count;
    return n;
}

static int factdb_hex(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static UV factdb_rd_u4(pTHX_ factdb_reader *r, U32 line, U32 col) {
    UV cp = 0;
    int i;
    for (i = 0; i < 4; i++) {
        int h = r->p < r->end ? factdb_hex(*r->p) : -1;
        if (h < 0) factdb_at(aTHX_ r->what, line, col, "\\u needs four hex digits");
        cp = cp * 16 + (UV)h;
        factdb_rd_step(r);
    }
    return cp;
}

/* Bytes onto the end of a plain byte buffer, as they are: never read as
 * Latin-1 and upgraded, whatever the buffer's flags. */
static void factdb_put(pTHX_ SV *buf, const char *p, STRLEN n) {
    STRLEN cur = SvCUR(buf);
    char *d = SvGROW(buf, cur + n + 1);
    Copy(p, d + cur, n, char);
    d[cur + n] = '\0';
    SvCUR_set(buf, cur + n);
}

static factdb_edn *factdb_rd_string(pTHX_ factdb_reader *r) {
    U32 line = r->line, col = r->col;
    factdb_edn *n = factdb_rd_node(aTHX_ r, FACTDB_EDN_STRING, line, col);
    SV *buf = sv_2mortal(newSVpvs(""));
    int wide = 0;
    factdb_rd_step(r);
    for (;;) {
        char ch;
        if (r->p >= r->end) factdb_at(aTHX_ r->what, line, col, "this string is never closed by a \"");
        ch = *r->p;
        if (ch == '"') { factdb_rd_step(r); break; }
        if (ch == '\\') {
            U32 el = r->line, ec = r->col;
            factdb_rd_step(r);
            if (r->p >= r->end) factdb_at(aTHX_ r->what, line, col, "this string is never closed by a \"");
            ch = *r->p;
            switch (ch) {
            case '"': case '\\': case '/': factdb_put(aTHX_ buf, &ch, 1); factdb_rd_step(r); break;
            case 'n': factdb_put(aTHX_ buf, "\n", 1); factdb_rd_step(r); break;
            case 't': factdb_put(aTHX_ buf, "\t", 1); factdb_rd_step(r); break;
            case 'r': factdb_put(aTHX_ buf, "\r", 1); factdb_rd_step(r); break;
            case 'b': factdb_put(aTHX_ buf, "\b", 1); factdb_rd_step(r); break;
            case 'f': factdb_put(aTHX_ buf, "\f", 1); factdb_rd_step(r); break;
            case 'u': {
                UV cp;
                U8 tmp[UTF8_MAXBYTES + 1], *e;
                factdb_rd_step(r);
                cp = factdb_rd_u4(aTHX_ r, el, ec);
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    UV lo;
                    if (r->end - r->p < 2 || r->p[0] != '\\' || r->p[1] != 'u')
                        factdb_at(aTHX_ r->what, el, ec, "\\u%04X is half a surrogate pair, with no second half", (unsigned)cp);
                    factdb_rd_step(r); factdb_rd_step(r);
                    lo = factdb_rd_u4(aTHX_ r, el, ec);
                    if (lo < 0xDC00 || lo > 0xDFFF)
                        factdb_at(aTHX_ r->what, el, ec, "\\u%04X is half a surrogate pair, with no second half", (unsigned)cp);
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                else if (cp >= 0xDC00 && cp <= 0xDFFF)
                    factdb_at(aTHX_ r->what, el, ec, "\\u%04X is the second half of a surrogate pair, with no first", (unsigned)cp);
                e = uvchr_to_utf8(tmp, cp);
                factdb_put(aTHX_ buf, (char *)tmp, (STRLEN)(e - tmp));
                if (cp > 0x7f) wide = 1;
                break;
            }
            default:
                factdb_at(aTHX_ r->what, el, ec, "unknown escape \\%c in a string", ch);
            }
            continue;
        }
        if ((unsigned char)ch >= 0x80) wide = 1;
        factdb_put(aTHX_ buf, &ch, 1);
        factdb_rd_step(r);
    }
    n->s = factdb_arena_str(aTHX_ r->arena, SvPVX(buf), SvCUR(buf));
    n->len = SvCUR(buf);
    n->utf8 = (U8)wide;
    return n;
}

static factdb_edn *factdb_rd_atom(pTHX_ factdb_reader *r) {
    U32 line = r->line, col = r->col;
    const char *start = r->p;
    STRLEN len;
    int keyword = *r->p == ':';
    factdb_edn *n;
    if (keyword) factdb_rd_step(r);
    while (r->p < r->end && factdb_sym_char((unsigned char)*r->p)) factdb_rd_step(r);
    len = (STRLEN)(r->p - start);
    if (keyword) {
        if (len == 1) factdb_at(aTHX_ r->what, line, col, "a keyword needs a name after the colon");
        n = factdb_rd_node(aTHX_ r, FACTDB_EDN_KEYWORD, line, col);
        n->s = factdb_arena_str(aTHX_ r->arena, start + 1, len - 1);
        n->len = len - 1;
        n->utf8 = !factdb_ascii(start, len);
        return n;
    }
    if (!len) {
        U8 ch = (U8)*r->p;
        factdb_at(aTHX_ r->what, line, col, ch >= ' ' && ch < 0x7f ? "unexpected '%c'" : "unexpected character %c",
                  ch >= ' ' && ch < 0x7f ? ch : '?');
    }
    {
        const char *q = start, *e = start + len;
        int sign = (*q == '-' || *q == '+') && len > 1;
        if (sign) q++;
        if (q < e && isDIGIT(*q)) {
            const char *d = q;
            int is_num = 0;
            while (d < e && isDIGIT(*d)) d++;
            if (d < e && *d == '.') {
                d++;
                if (d >= e || !isDIGIT(*d)) factdb_at(aTHX_ r->what, line, col, "a number needs digits after its point");
                while (d < e && isDIGIT(*d)) d++;
                is_num = 1;
            }
            if (d < e && (*d == 'e' || *d == 'E')) {
                d++;
                if (d < e && (*d == '+' || *d == '-')) d++;
                if (d >= e || !isDIGIT(*d)) factdb_at(aTHX_ r->what, line, col, "a number needs digits in its exponent");
                while (d < e && isDIGIT(*d)) d++;
                is_num = 1;
            }
            if (d != e) factdb_at(aTHX_ r->what, line, col, "%.*s is not a number", (int)len, start);
            if (is_num) {
                n = factdb_rd_node(aTHX_ r, FACTDB_EDN_NUM, line, col);
                n->nv = factdb_read_nv(aTHX_ start, len);
                if (Perl_isinf(n->nv)) factdb_at(aTHX_ r->what, line, col, "%.*s is too large a number", (int)len, start);
                return n;
            }
            else {
                UV uv = 0;
                int flags = grok_number(start, len, &uv);
                n = factdb_rd_node(aTHX_ r, FACTDB_EDN_INT, line, col);
                if (!(flags & IS_NUMBER_IN_UV) || (flags & IS_NUMBER_GREATER_THAN_UV_MAX)
                    || (!(flags & IS_NUMBER_NEG) && uv > (UV)IV_MAX) || ((flags & IS_NUMBER_NEG) && uv > (UV)IV_MAX + 1))
                    factdb_at(aTHX_ r->what, line, col, "%.*s is outside the range of a whole number here", (int)len, start);
                n->iv = (flags & IS_NUMBER_NEG) ? (uv == (UV)IV_MAX + 1 ? IV_MIN : -(IV)uv) : (IV)uv;
                return n;
            }
        }
    }
    if (len == 4 && memEQ(start, "true", 4)) return factdb_rd_node(aTHX_ r, FACTDB_EDN_TRUE, line, col);
    if (len == 5 && memEQ(start, "false", 5)) return factdb_rd_node(aTHX_ r, FACTDB_EDN_FALSE, line, col);
    if (len == 3 && memEQ(start, "nil", 3)) return factdb_rd_node(aTHX_ r, FACTDB_EDN_NIL, line, col);
    n = factdb_rd_node(aTHX_ r, FACTDB_EDN_SYMBOL, line, col);
    n->s = factdb_arena_str(aTHX_ r->arena, start, len);
    n->len = len;
    n->utf8 = !factdb_ascii(start, len);
    return n;
}

static factdb_edn *factdb_rd_form(pTHX_ factdb_reader *r) {
    factdb_rd_space(r);
    if (r->p >= r->end) factdb_at(aTHX_ r->what, r->line, r->col, "the text ends where a form was expected");
    switch (*r->p) {
    case '[': return factdb_rd_seq(aTHX_ r, FACTDB_EDN_VEC, ']');
    case '(': return factdb_rd_seq(aTHX_ r, FACTDB_EDN_LIST, ')');
    case '{': return factdb_rd_seq(aTHX_ r, FACTDB_EDN_MAP, '}');
    case ']': case ')': case '}':
        factdb_at(aTHX_ r->what, r->line, r->col, "a %c with nothing open for it to close", *r->p);
    case '"': return factdb_rd_string(aTHX_ r);
    }
    return factdb_rd_atom(aTHX_ r);
}

/* The one form `text` holds, read into `arena`. */
static factdb_edn *factdb_read(pTHX_ SV *text, factdb_arena *arena, const char *what) {
    factdb_reader r;
    factdb_edn *form;
    STRLEN len;
    const char *p;
    if (!SvOK(text) || SvROK(text)) croak("%s: a query is text", what);
    if (!SvUTF8(text)) {
        SV *up = sv_2mortal(newSVsv(text));
        sv_utf8_upgrade(up);
        text = up;
    }
    p = SvPV(text, len);
    r.what = what;
    r.arena = arena;
    r.p = p;
    r.end = p + len;
    r.line = 1;
    r.col = 1;
    r.depth = 0;
    factdb_rd_space(&r);
    if (r.p >= r.end) factdb_at(aTHX_ what, 1, 1, "the text is empty");
    form = factdb_rd_form(aTHX_ &r);
    factdb_rd_space(&r);
    if (r.p < r.end) factdb_at(aTHX_ what, r.line, r.col, "more after the end of the query");
    return form;
}

#endif
