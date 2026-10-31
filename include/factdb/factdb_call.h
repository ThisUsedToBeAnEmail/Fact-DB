#ifndef FACTDB_CALL_H
#define FACTDB_CALL_H

/* G_LIST is perl 5.36's name for what older perls call G_ARRAY. */
#ifndef G_LIST
#  define G_LIST G_ARRAY
#endif

/* Every sub the database calls - a query's Perl functions, from Phase 09 -
 * is called through here, and never with a plain call_sv.
 *
 * On a stack of its own, as perl's sort calls its comparison. Without that,
 * `last` or `next` inside the sub finds the caller's loop on the context
 * stack and jumps to it straight through this library's C, which never gets
 * control back: a query's rows and arena would be left half built. On a
 * stack of its own there is no loop to find, so it is an error like any
 * other.
 *
 * Under G_EVAL, with $@ localised, so that a call that works leaves the
 * caller's $@ as it was. A die comes back as a copy of the error, mortal,
 * for the caller to throw again once it has put things right.
 *
 * `gimme` is G_VOID, G_SCALAR or G_LIST. A scalar result is put in *one;
 * list results are pushed onto `many`. Either is a temporary that lives as
 * long as the caller's temps frame. */
static SV *factdb_call(pTHX_ SV *code, SV **args, I32 nargs, I32 gimme,
                       SV **one, AV *many) {
    dSP;
    SV *err = NULL;
    I32 n, i;

    if (one) *one = &PL_sv_undef;
    ENTER;
    save_scalar(PL_errgv);
    PUSHSTACKi(PERLSI_UNKNOWN);
    {
        PUSHMARK(SP);
        EXTEND(SP, nargs);
        for (i = 0; i < nargs; i++) PUSHs(args[i]);
        PUTBACK;
        n = call_sv(code, gimme | G_EVAL);
        SPAGAIN;
        if (SvTRUE(ERRSV)) {
            err = sv_2mortal(newSVsv(ERRSV));
        }
        else if (gimme == G_SCALAR && one) {
            if (n > 0) *one = *SP;
        }
        else if (gimme == G_LIST && many) {
            for (i = n; i > 0; i--) av_push(many, SvREFCNT_inc_simple_NN(*(SP - i + 1)));
        }
        SP -= n;
        PUTBACK;
    }
    POPSTACK;
    LEAVE;
    return err;
}

/* Throw `err` again. */
static void factdb_rethrow(pTHX_ SV *err) {
    sv_setsv(ERRSV, err);
    croak(NULL);
}

#endif
