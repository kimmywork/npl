/* SRFI 89 keyword-argument parser.
 *
 * The expander passes a spec list in formal order:
 *   (0 . #f)          required positional
 *   (1 . #f)          optional positional
 *   (2 . keyword)     optional named
 *   (4 . keyword)     required named
 *   (3 . #f)          rest
 *   (5 . #f)          allow other keywords; always first, not a slot
 *
 * The result is a vector with one (present . value) pair per spec entry,
 * excluding the leading allow-other-keys marker. Defaults and required-named
 * errors stay in generated Scheme code, where they can be evaluated in the
 * scope of the preceding parameters.
 */

#include "npl.h"
#include <stdlib.h>

static int keyword_value_p(NplVal value)
{
    return npl_is_ptr(value) && npl_header(value)->tag == BOX_KEYWORD;
}

static NplVal missing_pair(void)
{
    return npl_cons(npl_false(), npl_false());
}

static int64_t spec_count(NplVal spec, const char *who)
{
    int64_t count = 0;
    while (!npl_is_nil(spec)) {
        if (!npl_is_ptr(spec) || npl_header(spec)->tag != BOX_PAIR)
            npl_primitive_error(who, "malformed parameter specification", spec);
        count++;
        spec = npl_cdr(spec);
    }
    return count;
}

static int keyword_name_equal(NplVal left, NplVal right)
{
    if (!npl_is_ptr(left) || !npl_is_ptr(right)) return 0;
    uint32_t left_tag = npl_header(left)->tag;
    uint32_t right_tag = npl_header(right)->tag;
    if ((left_tag != BOX_KEYWORD && left_tag != BOX_SYMBOL) ||
        (right_tag != BOX_KEYWORD && right_tag != BOX_SYMBOL))
        return 0;
    return npl_string_same_chars_internal(
        (const NplString *)npl_header(left),
        (const NplString *)npl_header(right));
}

static int64_t named_slot(NplVal spec, NplVal keyword)
{
    int64_t index = 0;
    while (!npl_is_nil(spec)) {
        NplVal entry = npl_car(spec);
        int64_t kind = npl_fixnum_value(npl_car(entry));
        if ((kind == 2 || kind == 4) &&
            keyword_name_equal(npl_cdr(entry), keyword))
            return index;
        index++;
        spec = npl_cdr(spec);
    }
    return -1;
}

/* A leading (5 . #f) entry tells the parser to skip unknown keywords. */
static int spec_allows_other_keys(NplVal spec)
{
    if (!npl_is_ptr(spec) || npl_header(spec)->tag != BOX_PAIR) return 0;
    NplVal entry = npl_car(spec);
    if (!npl_is_ptr(entry) || npl_header(entry)->tag != BOX_PAIR) return 0;
    return npl_fixnum_value(npl_car(entry)) == 5;
}

static int other_key_seen(NplVal keys, NplVal key)
{
    while (!npl_is_nil(keys)) {
        if (keyword_name_equal(npl_car(keys), key)) return 1;
        keys = npl_cdr(keys);
    }
    return 0;
}

static void note_other_key(NplVal *keys, NplVal key, const char *who)
{
    if (other_key_seen(*keys, key))
        npl_primitive_error(who, "duplicate keyword argument", key);
    *keys = npl_cons(key, *keys);
}

NplVal npl_keyword_parse(NplVal spec, NplVal args)
{
    const char *who = "keyword-arguments";
    int allow_other_keys = spec_allows_other_keys(spec);
    if (allow_other_keys) spec = npl_cdr(spec);
    int64_t count = spec_count(spec, who);
    NplVal full_spec = spec;
    NplVal *initial = (NplVal *)calloc((size_t)count, sizeof(NplVal));
    if (count && !initial) npl_runtime_oom_internal();
    NplVal result = npl_make_vector(count, initial);
    free(initial);
    NplVal other_keys = npl_nil();
    npl_gc_root(&spec);
    npl_gc_root(&args);
    npl_gc_root(&result);
    npl_gc_root(&other_keys);

    for (int64_t i = 0; i < count; i++)
        npl_vector_set(result, npl_fixnum(i), missing_pair());

    int64_t index = 0;
    int named_done = 0;
    while (!npl_is_nil(spec)) {
        NplVal entry = npl_car(spec);
        int64_t kind = npl_fixnum_value(npl_car(entry));
        if (kind == 0 || kind == 1) {
            if (!npl_is_nil(args) && !keyword_value_p(npl_car(args))) {
                NplVal value = npl_car(args);
                args = npl_cdr(args);
                npl_vector_set(result, npl_fixnum(index),
                               npl_cons(npl_true(), value));
            }
        } else if (kind == 2 || kind == 4) {
            if (!named_done) {
                named_done = 1;
                while (!npl_is_nil(args) && keyword_value_p(npl_car(args))) {
                    NplVal key = npl_car(args);
                    if (npl_is_nil(npl_cdr(args)))
                        npl_primitive_error(who,
                            "keyword argument has no value", key);
                    NplVal value = npl_car(npl_cdr(args));
                    int64_t slot = named_slot(full_spec, key);
                    if (slot < 0) {
                        if (!allow_other_keys)
                            npl_primitive_error(who,
                                "unknown keyword argument", key);
                        note_other_key(&other_keys, key, who);
                    } else {
                        NplVal present = npl_car(npl_vector_ref(
                            result, npl_fixnum(slot)));
                        if (!npl_is_false(present))
                            npl_primitive_error(who,
                                "duplicate keyword argument", key);
                        npl_vector_set(result, npl_fixnum(slot),
                                       npl_cons(npl_true(), value));
                    }
                    args = npl_cdr(npl_cdr(args));
                }
            }
        } else if (kind == 3) {
            npl_vector_set(result, npl_fixnum(index),
                           npl_cons(npl_true(), args));
            args = npl_nil();
        } else {
            npl_primitive_error(who, "unknown parameter kind",
                                npl_fixnum(kind));
        }
        index++;
        spec = npl_cdr(spec);
    }

    /* #:allow-other-keys without any #:key parameters still accepts and
     * ignores trailing keyword pairs. */
    if (allow_other_keys) {
        while (!npl_is_nil(args) && keyword_value_p(npl_car(args))) {
            NplVal key = npl_car(args);
            if (npl_is_nil(npl_cdr(args)))
                npl_primitive_error(who, "keyword argument has no value",
                                    key);
            note_other_key(&other_keys, key, who);
            args = npl_cdr(npl_cdr(args));
        }
    }

    if (!npl_is_nil(args))
        npl_primitive_error(who, "too many arguments", args);

    npl_gc_unroot(&other_keys);
    npl_gc_unroot(&result);
    npl_gc_unroot(&args);
    npl_gc_unroot(&spec);
    return result;
}

/* Generated lambda* code reports a missing parameter without depending on
 * the caller's (possibly renamed or shadowed) error binding. */
NplVal npl_keyword_missing(NplVal key, NplVal named)
{
    const char *message = named == npl_false()
        ? "missing required argument"
        : "missing required keyword argument";
    npl_primitive_error("keyword-arguments", message, key);
    return npl_nil();
}
