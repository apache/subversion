/*
 * swigutil_pl.c: utility functions for the SWIG Perl bindings
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include <apr.h>
#include <apr_general.h>
#include <apr_portable.h>

/* Windows hack: Allow overriding some <perl.h> defaults */
#include "swigutil_pl__pre_perl.h"

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

/* Perl defines a _ macro, but SVN uses it for translations.
 * So undefine _ after including the Perl headers. */
#undef _

#include <stdarg.h>
#ifdef WIN32
#include <io.h>
#endif

#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_opt.h"
#include "svn_time.h"
#include "svn_private_config.h"

#include "swig_perl_external_runtime.swg"

#include "swigutil_pl.h"

/* cache SWIG_TypeQuery results in a perl hash */
static HV *type_cache = NULL;

#define _SWIG_TYPE(name) _swig_perl_type_query(name, 0)
#define POOLINFO         _SWIG_TYPE("apr_pool_t *")

static swig_type_info *_swig_perl_type_query(const char *type_name, U32 klen)
{
    SV **type_info;
    swig_type_info *tinfo;

    if (!type_cache)
      type_cache = newHV();

    if (klen == 0)
      klen = strlen(type_name);

    if ((type_info = hv_fetch(type_cache, type_name, klen, 0)))
      return (swig_type_info *) (SvIV(*type_info));

    tinfo = SWIG_TypeQuery(type_name);
    hv_store(type_cache, type_name, klen, newSViv((IV)tinfo), 0);

    return tinfo;
}

/* element convertors for perl -> c */
typedef void *(*pl_element_converter_t)(SV *value, void *ctx,
                                        apr_pool_t *pool);

static void *convert_pl_string(SV *value, void *dummy, apr_pool_t *pool)
{
    void **result = apr_palloc(pool, sizeof(void *));
    *result = SvPV_nolen(value);
    return *result;
}

static void *convert_pl_obj(SV *value, swig_type_info *tinfo,
                            apr_pool_t *pool)
{
    void **result = apr_palloc(pool, sizeof(void *));
    if (SWIG_ConvertPtr(value, result, tinfo, 0) < 0) {
        croak("unable to convert from swig object");
    }
    return *result;
}

static void *convert_pl_revnum_t(SV *value, void *dummy, apr_pool_t *pool)
{
  svn_revnum_t *result = apr_palloc(pool, sizeof(svn_revnum_t));
  *result = SvIV(value);
  return (void *)result;
}

static void *convert_pl_svn_string_t(SV *value, void *dummy, apr_pool_t *pool)
{
    svn_string_t *result = apr_palloc(pool, sizeof(svn_string_t));
    /* just the in typemap for svn_string_t */
    result->data = SvPV(value, result->len);
    return (void *)result;
}

/* Convert a revision range and return a svn_opt_revision_range_t*.
 * Value can be:
 * - a _p_svn_opt_revision_range_t object
 * - a reference to a two-element array, [start, end],
 *   where start and end is anything accepted by svn_swig_pl_set_revision
 * If value is not acceptable and *(svn_boolean_t *)ctx is FALSE,
 * convert_pl_revision_range returns NULL, otherwise it croak()s.
 */
static void *convert_pl_revision_range(SV *value, void *ctx, apr_pool_t *pool)
{
    svn_boolean_t croak_on_error = *(svn_boolean_t *)ctx;

    if (sv_isobject(value) && sv_derived_from(value, "_p_svn_opt_revision_range_t")) {
        svn_opt_revision_range_t *range;
        /* this will assign to range */
        SWIG_ConvertPtr(value, (void **)&range, _SWIG_TYPE("svn_opt_revision_range_t *"), 0);
        return range;
    }

    if (SvROK(value)
        && SvTYPE(SvRV(value)) == SVt_PVAV
        && av_len((AV *)SvRV(value)) == 1) {
        /* value is a two-element ARRAY */
        AV* array = (AV *)SvRV(value);
        svn_opt_revision_t temp_start, temp_end;
        svn_opt_revision_t *start, *end;
        svn_opt_revision_range_t *range;

        /* Note: Due to how svn_swig_pl_set_revision works,
         * either the passed in svn_opt_revision_t is modified
         * (and the original pointer returned) or a different pointer
         * is returned. svn_swig_pl_set_revision may return NULL
         * only if croak_on_error is FALSE.
         */
        start = svn_swig_pl_set_revision(&temp_start,
                                         *av_fetch(array, 0, 0),
                                         croak_on_error, pool);
        if (start == NULL)
            return NULL;
        end = svn_swig_pl_set_revision(&temp_end,
                                       *av_fetch(array, 1, 0),
                                       croak_on_error, pool);
        if (end == NULL)
            return NULL;

        /* allocate a new range and copy in start and end fields */
        range = apr_palloc(pool, sizeof(*range));
        range->start = *start;
        range->end = *end;
        return range;
    }

    if (croak_on_error)
        croak("unknown revision range: "
              "must be an array of length 2 whose elements are acceptable "
              "as opt_revision_t or a _p_svn_opt_revision_range_t object");
    return NULL;
}

/* perl -> c hash convertors */
static apr_hash_t *svn_swig_pl_to_hash(SV *source,
                                       pl_element_converter_t cv,
                                       void *ctx, apr_pool_t *pool)
{
    apr_hash_t *hash;
    HV *h;
    char *key;
    I32 cnt, retlen;

    if (!(source && SvROK(source) && SvTYPE(SvRV(source)) == SVt_PVHV)) {
        return NULL;
    }

    hash = apr_hash_make(pool);
    h = (HV *)SvRV(source);
    cnt = hv_iterinit(h);
    while (cnt--) {
        SV* item = hv_iternextsv(h, &key, &retlen);
        void *val = cv(item, ctx, pool);
        svn_hash_sets(hash, apr_pstrmemdup(pool, key, retlen), val);
    }

    return hash;
}

apr_hash_t *svn_swig_pl_objs_to_hash(SV *source, swig_type_info *tinfo,
                                     apr_pool_t *pool)
{

    return svn_swig_pl_to_hash(source, (pl_element_converter_t)convert_pl_obj,
                               tinfo, pool);
}

apr_hash_t *svn_swig_pl_strings_to_hash(SV *source, apr_pool_t *pool)
{

    return svn_swig_pl_to_hash(source, convert_pl_string, NULL, pool);
}


apr_hash_t *svn_swig_pl_objs_to_hash_by_name(SV *source,
                                             const char *typename,
                                             apr_pool_t *pool)
{
    swig_type_info *tinfo = _SWIG_TYPE(typename);
    return svn_swig_pl_objs_to_hash(source, tinfo, pool);
}

apr_hash_t *svn_swig_pl_objs_to_hash_of_revnum_t(SV *source,
                                                 apr_pool_t *pool)
{

  return svn_swig_pl_to_hash(source,
                             (pl_element_converter_t)convert_pl_revnum_t,
                             NULL, pool);
}

apr_hash_t *svn_swig_pl_hash_to_prophash(SV *source, apr_pool_t *pool)
{
  return svn_swig_pl_to_hash(source, convert_pl_svn_string_t, NULL, pool);
}

/* perl -> c array convertors */
static apr_array_header_t *svn_swig_pl_to_array(SV *source,
                                                pl_element_converter_t cv,
                                                void *ctx, apr_pool_t *pool)
{
    int targlen;
    apr_array_header_t *temp;
    AV* array;

    if (SvROK(source) && SvTYPE(SvRV(source)) == SVt_PVAV) {
      array = (AV *)SvRV(source);
      targlen = av_len(array) + 1;
      temp = apr_array_make(pool, targlen, sizeof(const char *));
        temp->nelts = targlen;

        while (targlen--) {
            /* more error handling here */
          SV **item = av_fetch(array, targlen, 0);
          APR_ARRAY_IDX(temp, targlen, const char *) = cv(*item, ctx, pool);
        }
    } else if (SvOK(source)) {
        targlen = 1;
        temp = apr_array_make(pool, targlen, sizeof(const char *));
        temp->nelts = targlen;
        APR_ARRAY_IDX(temp, 0, const char *) = cv(source, ctx, pool);
    } else {
        croak("Must pass a single value or an array reference");
    }

    return temp;
}

apr_array_header_t *svn_swig_pl_strings_to_array(SV *source,
                                                       apr_pool_t *pool)
{
  return svn_swig_pl_to_array(source, convert_pl_string, NULL, pool);
}

apr_array_header_t *svn_swig_pl_objs_to_array(SV *source,
                                              swig_type_info *tinfo,
                                              apr_pool_t *pool)
{
  return svn_swig_pl_to_array(source,
                              (pl_element_converter_t)convert_pl_obj,
                              tinfo, pool);
}

/* Convert a single revision range or an array of revisions ranges
 * Note: We can't simply use svn_swig_pl_to_array() as is, since
 * it immediatley checks whether source is an array reference and then
 * proceeds to treat this as the "array of ..." case. But a revision range
 * may be specified as a (two-element) array. Hence we first try to
 * convert source as a single revision range. Failing that and if it's
 * an array we then call svn_swig_pl_to_array(). Otherwise we croak().
 */
apr_array_header_t *svn_swig_pl_array_to_apr_array_revision_range(
        SV *source, apr_pool_t *pool)
{
    svn_boolean_t croak_on_error = FALSE;
    svn_opt_revision_range_t *range;

    if ((range = convert_pl_revision_range(source, &croak_on_error, pool))) {
        apr_array_header_t *temp = apr_array_make(pool, 1,
                                                  sizeof(svn_opt_revision_range_t *));
        temp->nelts = 1;
        APR_ARRAY_IDX(temp, 0, svn_opt_revision_range_t *) = range;
        return temp;
    }

    if (SvROK(source) && SvTYPE(SvRV(source)) == SVt_PVAV) {
        croak_on_error = TRUE;
        return svn_swig_pl_to_array(source, convert_pl_revision_range,
                                    &croak_on_error, pool);
    }

    croak("must pass a single revision range or a reference to an array of revision ranges");

    /* This return is actually unreachable because of the croak above,
     * however, Visual Studio's compiler doesn't like if all paths don't have
     * a return and errors out otherwise. */
    return NULL;
}

/* element convertors for c -> perl */
typedef SV *(*element_converter_t)(void *value, void *ctx);

static SV *convert_string(const char *value, void *dummy)
{
    SV *obj = sv_2mortal(newSVpv(value, 0));
    return obj;
}

static SV *convert_svn_string_t(svn_string_t *value, void *dummy)
{
    SV *obj = sv_2mortal(newSVpv(value->data, value->len));
    return obj;
}

static SV *convert_to_swig_type(void *ptr, swig_type_info *tinfo)
{
    SV *obj = sv_newmortal();
    SWIG_MakePtr(obj, ptr, tinfo, 0);
    return obj;
}

static SV *convert_int(int value, void *dummy)
{
  return sv_2mortal(newSViv(value));
}

static SV *convert_svn_revnum_t(svn_revnum_t revnum, void *dummy)
{
  return sv_2mortal(newSViv((long int)revnum));

}

/* c -> perl hash convertors */
static SV *convert_hash(apr_hash_t *hash, element_converter_t converter_func,
                        void *ctx)
{
    apr_hash_index_t *hi;
    HV *hv;

    hv = newHV();
    for (hi = apr_hash_first(NULL, hash); hi; hi = apr_hash_next(hi)) {
        const char *key;
        void *val;
        int klen;
        SV *obj;

        apr_hash_this(hi, (void *)&key, NULL, &val);
        klen = strlen(key);

        obj = converter_func(val, ctx);
        hv_store(hv, (const char *)key, klen, obj, 0);
        SvREFCNT_inc(obj);
    }

    return sv_2mortal(newRV_noinc((SV*)hv));
}

SV *svn_swig_pl_prophash_to_hash(apr_hash_t *hash)
{
  return convert_hash(hash, (element_converter_t)convert_svn_string_t,
                      NULL);
}

SV *svn_swig_pl_convert_hash(apr_hash_t *hash, swig_type_info *tinfo)
{
  return convert_hash(hash, (element_converter_t)convert_to_swig_type,
                      tinfo);
}

/* c -> perl array convertors */
static SV *convert_array(const apr_array_header_t *array,
                  element_converter_t converter_func, void *ctx)
{
    AV *list = newAV();
    int i;

    for (i = 0; i < array->nelts; ++i) {
        void *element = APR_ARRAY_IDX(array, i, void *);
        SV *item = converter_func(element, ctx);
        av_push(list, item);
        SvREFCNT_inc(item);
    }
    return sv_2mortal(newRV_noinc((SV*)list));
}

SV *svn_swig_pl_array_to_list(const apr_array_header_t *array)
{
  return convert_array(array, (element_converter_t)convert_string, NULL);
}

/* Formerly used by pre-1.0 APIs. Now unused
SV *svn_swig_pl_ints_to_list(const apr_array_header_t *array)
{
    return convert_array (array, (element_converter_t)convert_int, NULL);
}
*/

SV *svn_swig_pl_convert_array(const apr_array_header_t *array,
                              swig_type_info *tinfo)
{
  return convert_array(array, (element_converter_t)convert_to_swig_type,
                       tinfo);
}

SV *svn_swig_pl_revnums_to_list(const apr_array_header_t *array)
{
    return convert_array(array, (element_converter_t)convert_svn_revnum_t,
                         NULL);
}

/* perl -> c svn_opt_revision_t conversion */
svn_opt_revision_t *svn_swig_pl_set_revision(svn_opt_revision_t *rev,
                                             SV *source,
                                             svn_boolean_t croak_on_error,
                                             apr_pool_t *pool)
{
#define maybe_croak(argv) do { if (croak_on_error) croak argv; \
                               else return NULL; } while (0)

    if (source == NULL || source == &PL_sv_undef || !SvOK(source)) {
        rev->kind = svn_opt_revision_unspecified;
    }
    else if (sv_isobject(source) && sv_derived_from(source, "_p_svn_opt_revision_t")) {
        /* this will assign to rev */
        SWIG_ConvertPtr(source, (void **)&rev, _SWIG_TYPE("svn_opt_revision_t *"), 0);
    }
    else if (looks_like_number(source)) {
        rev->kind = svn_opt_revision_number;
        rev->value.number = SvIV(source);
    }
    else if (SvPOK(source)) {
        char *input = SvPV_nolen(source);
        if (svn_cstring_casecmp(input, "BASE") == 0)
            rev->kind = svn_opt_revision_base;
        else if (svn_cstring_casecmp(input, "HEAD") == 0)
            rev->kind = svn_opt_revision_head;
        else if (svn_cstring_casecmp(input, "WORKING") == 0)
            rev->kind = svn_opt_revision_working;
        else if (svn_cstring_casecmp(input, "COMMITTED") == 0)
            rev->kind = svn_opt_revision_committed;
        else if (svn_cstring_casecmp(input, "PREV") == 0)
            rev->kind = svn_opt_revision_previous;
        else if (*input == '{') {
            svn_boolean_t matched;
            apr_time_t tm;
            svn_error_t *err;

            char *end = strchr(input,'}');
            char saved_end;
            if (!end)
                maybe_croak(("unknown opt_revision_t string \"%s\": "
                             "missing closing brace for \"{DATE}\"", input));
            saved_end = *end;
            *end = '\0';
            err = svn_parse_date (&matched, &tm,
                                  input + 1, apr_time_now(), pool);
            *end = saved_end;
            if (err) {
                svn_error_clear (err);
                maybe_croak(("unknown opt_revision_t string \"%s\": "
                             "internal svn_parse_date error", input));
            }
            if (!matched)
                maybe_croak(("unknown opt_revision_t string \"%s\": "
                             "svn_parse_date failed to parse it", input));

            rev->kind = svn_opt_revision_date;
            rev->value.date = tm;
        } else
            maybe_croak(("unknown opt_revision_t string \"%s\": must be one of "
                         "\"BASE\", \"HEAD\", \"WORKING\", \"COMMITTED\", "
                         "\"PREV\" or a \"{DATE}\"", input));
    } else
        maybe_croak(("unknown opt_revision_t type: must be undef, a number, "
                     "a string (one of \"BASE\", \"HEAD\", \"WORKING\", "
                     "\"COMMITTED\", \"PREV\" or a \"{DATE}\") "
                     "or a _p_svn_opt_revision_t object"));

    return rev;
#undef maybe_croak
}

/* put the va_arg in stack and invoke caller_func with func.
   fmt:
   * O: perl object
   * i: apr_int32_t
   * u: apr_uint32_t
   * L: apr_int64_t
   * U: apr_uint64_t
   * s: string
   * S: swigtype
   * r: svn_revnum_t
   * b: svn_boolean_t
   * t: svn_string_t
   * z: apr_size_t

   Please do not add C types here.  Add a new format code if needed.
   Using the underlying C types and not the APR or SVN types can break
   things if these data types change in the future or on platforms which
   use different types.

   put returned value in result if result is not NULL
*/

/* NOTE: calls back into Perl (directly) */
svn_error_t *svn_swig_pl_callback_thunk(perl_func_invoker_t caller_func,
                                        void *func,
                                        SV **result,
                                        const char *fmt, ...)
{
    const char *fp = fmt;
    va_list ap;
    int count;
    I32 call_flags = result ? G_SCALAR : (G_VOID & G_DISCARD);

    dSP ;
    ENTER ;
    SAVETMPS ;

    PUSHMARK(SP) ;

    va_start(ap, fmt);
    while (*fp) {
        char *c;
        void *o;
        SV *obj;
        swig_type_info *t;
        svn_string_t *str;

        switch (*fp++) {
        case 'O':
          XPUSHs(va_arg(ap, SV *));
            break;
        case 'S': /* swig object */
          o = va_arg(ap, void *);
          t = va_arg(ap, swig_type_info *);

          obj = sv_newmortal();
          SWIG_MakePtr(obj, o, t, 0);
            XPUSHs(obj);
            break;

        case 's': /* string */
          c = va_arg(ap, char *);
            XPUSHs(c ? sv_2mortal(newSVpv(c, 0)) : &PL_sv_undef);
            break;

        case 'i': /* apr_int32_t */
            XPUSHs(sv_2mortal(newSViv(va_arg(ap, apr_int32_t))));
            break;

        case 'u': /* apr_uint32_t */
            XPUSHs(sv_2mortal(newSViv(va_arg(ap, apr_uint32_t))));
            break;

        case 'r': /* svn_revnum_t */
            XPUSHs(sv_2mortal(newSViv(va_arg(ap, svn_revnum_t))));
            break;

        case 'b': /* svn_boolean_t */
            XPUSHs(sv_2mortal(newSViv(va_arg(ap, svn_boolean_t))));
            break;

        case 't': /* svn_string_t */
            str = va_arg(ap, svn_string_t *);
            XPUSHs(str ? sv_2mortal(newSVpv(str->data, str->len))
                   : &PL_sv_undef);
            break;

        case 'L': /* apr_int64_t */
            /* Pass into perl as a string because some implementations may
             * not be able to handle a 64-bit int.  If it's too long to
             * fit in Perl's interal IV size then perl will only make
             * it available as a string.  If not then perl will convert
             * it to an IV for us.  So this handles the problem gracefully */
            c = malloc(30);
            snprintf(c,30,"%" APR_INT64_T_FMT,va_arg(ap, apr_int64_t));
            XPUSHs(sv_2mortal(newSVpv(c, 0)));
            free(c);
            break;

        case 'U': /* apr_uint64_t */
            c = malloc(30);
            snprintf(c,30,"%" APR_UINT64_T_FMT,va_arg(ap, apr_uint64_t));
            XPUSHs(sv_2mortal(newSVpv(c, 0)));
            free(c);
            break;

        case 'z': /* apr_size_t */
            if (sizeof(apr_size_t) >= 8)
              {
                c = malloc(30);
                snprintf(c,30,"%" APR_SIZE_T_FMT,va_arg(ap, apr_size_t));
                XPUSHs(sv_2mortal(newSVpv(c, 0)));
                free(c);
              }
            else
              {
                XPUSHs(sv_2mortal(newSViv(va_arg(ap, apr_size_t))));
              }
             break;
        }
    }

    va_end(ap);

    PUTBACK;
    switch (caller_func) {
    case CALL_SV:
      count = call_sv(func, call_flags );
        break;
    case CALL_METHOD:
      count = call_method(func, call_flags );
        break;
    default:
      croak("unkonwn calling type");
        break;
    }
    SPAGAIN ;

    if (((call_flags & G_SCALAR) && count != 1) ||
        ((call_flags & G_VOID) && count != 0))
      croak("Wrong number of returns");

    if (result) {
        *result = POPs;
        SvREFCNT_inc(*result);
    }

    PUTBACK;
    FREETMPS ;
    LEAVE ;

    return SVN_NO_ERROR;
}

/*** Editor Wrapping ***/

/* this could be more perlish */
typedef struct item_baton {
    SV *editor;     /* the editor handling the callbacks */
    SV *baton;      /* the dir/file baton (or NULL for edit baton) */
} item_baton;

static item_baton * make_baton(apr_pool_t *pool,
                               SV *editor, SV *baton)
{
    item_baton *newb = apr_palloc(pool, sizeof(*newb));

    newb->editor = editor;
    newb->baton = baton;

    return newb;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
static svn_error_t * close_baton(void *baton, const char *method, apr_pool_t *pool)
{
    item_baton *ib = baton;

    if (ib->baton) {
      SVN_ERR(svn_swig_pl_callback_thunk(CALL_METHOD,
                                         (void *)method, NULL,
                                         "OOS", ib->editor, ib->baton,
                                         pool, POOLINFO));
        SvREFCNT_dec(ib->baton);
    }
    else {
      SVN_ERR(svn_swig_pl_callback_thunk(CALL_METHOD,
                                         (void *)method, NULL,
                                         "OS", ib->editor, pool, POOLINFO));
    }

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
static svn_error_t * thunk_set_target_revision(void *edit_baton,
                                               svn_revnum_t target_revision,
                                               apr_pool_t *pool)
{
    item_baton *ib = edit_baton;

    SVN_ERR(svn_swig_pl_callback_thunk(CALL_METHOD,
                                       (void *)"set_target_revision", NULL,
                                       "Or", ib->editor, target_revision));

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
static svn_error_t * thunk_open_root(void *edit_baton,
                                     svn_revnum_t base_revision,
                                     apr_pool_t *dir_pool,
                                     void **root_baton)
{
    item_baton *ib = edit_baton;
    SV *result;

    SVN_ERR(svn_swig_pl_callback_thunk(CALL_METHOD,
                                       (void *)"open_root", &result,
                                       "OrS", ib->editor, base_revision,
                                       dir_pool, POOLINFO));

    *root_baton = make_baton(dir_pool, ib->editor, result);
    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
static svn_error_t * thunk_delete_entry(const char *path,
                                        svn_revnum_t revision,
                                        void *parent_baton,
                                        apr_pool_t *pool)
{
    item_baton *ib = parent_baton;

    SVN_ERR(svn_swig_pl_callback_thunk(CALL_METHOD,
                                       (void *)"delete_entry", NULL,
                                       "OsrOS", ib->editor, path, revision,
                                       ib->baton, pool, POOLINFO));
    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
static svn_error_t * thunk_add_directory(const char *path,
                                         void *parent_baton,
                                         const char *copyfrom_path,
                                         svn_revnum_t copyfrom_revision,
                                         apr_pool_t *dir_pool,
                                         void **child_baton)
{
    item_baton *ib = parent_baton;
    SV *result;

    SVN_ERR(svn_swig_pl_callback_thunk(CALL_METHOD,
                                       (void *)"add_directory", &result,
                                       "OsOsrS", ib->editor, path, ib->baton,
                                       copyfrom_path, copyfrom_revision,
                                       dir_pool, POOLINFO));
    *child_baton = make_baton(dir_pool, ib->editor, result);
    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
static svn_error_t * thunk_open_directory(const char *path,
                                          void *parent_baton,
                                          svn_revnum_t base_revision,
                                          apr_pool_t *dir_pool,
                                          void **child_baton)
{
    item_baton *ib = parent_baton;
    SV *result;

    SVN_ERR(svn_swig_pl_callback_thunk(CALL_METHOD,
                                       (void *)"open_directory", &result,
                                       "OsOrS", ib->editor, path, ib->baton,
                                       base_revision, dir_pool, POOLINFO));

    *child_baton = make_baton(dir_pool, ib->editor, result);

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
static svn_error_t * thunk_change_dir_prop(void *dir_baton,
                                           const char *name,
                                           const svn_string_t *value,
                                           apr_pool_t *pool)
{
    item_baton *ib = dir_baton;

    SVN_ERR(svn_swig_pl_callback_thunk(CALL_METHOD,
                                       (void *)"change_dir_prop", NULL,
                                       "OOstS", ib->editor, ib->baton, name,
                                       value, pool, POOLINFO));

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling close_baton) */
static svn_error_t * thunk_close_directory(void *dir_baton,
                                           apr_pool_t *pool)
{
    return close_baton(dir_baton, "close_directory", pool);
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
static svn_error_t * thunk_absent_directory(const char *path,
                                            void *parent_baton,
                                            apr_pool_t *pool)
{
    item_baton *ib = parent_baton;

    SVN_ERR(svn_swig_pl_callback_thunk(CALL_METHOD,
                                       (void *)"absent_directory", NULL,
                                       "OsOS", ib->editor, path, ib->baton,
                                       pool, POOLINFO));

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
static svn_error_t * thunk_add_file(const char *path,
                                    void *parent_baton,
                                    const char *copyfrom_path,
                                    svn_revnum_t copyfrom_revision,
                                    apr_pool_t *file_pool,
                                    void **file_baton)
{
    item_baton *ib = parent_baton;
    SV *result;

    SVN_ERR(svn_swig_pl_callback_thunk(CALL_METHOD,
                                       (void *)"add_file", &result,
                                       "OsOsrS", ib->editor, path, ib->baton,
                                       copyfrom_path, copyfrom_revision,
                                       file_pool, POOLINFO));

    *file_baton = make_baton(file_pool, ib->editor, result);
    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
static svn_error_t * thunk_open_file(const char *path,
                                     void *parent_baton,
                                     svn_revnum_t base_revision,
                                     apr_pool_t *file_pool,
                                     void **file_baton)
{
    item_baton *ib = parent_baton;
    SV *result;

    SVN_ERR(svn_swig_pl_callback_thunk(CALL_METHOD,
                                       (void *)"open_file", &result,
                                       "OsOrS", ib->editor, path, ib->baton,
                                       base_revision, file_pool, POOLINFO));

    *file_baton = make_baton(file_pool, ib->editor, result);
    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
static svn_error_t * thunk_window_handler(svn_txdelta_window_t *window,
                                          void *baton)
{
    SV *handler = baton;

    if (window == NULL) {
      SVN_ERR(svn_swig_pl_callback_thunk(CALL_SV,
                                         handler, NULL, "O",
                                         &PL_sv_undef));
        SvREFCNT_dec(handler);
    }
    else {
        swig_type_info *tinfo = _SWIG_TYPE("svn_txdelta_window_t *");
        SVN_ERR(svn_swig_pl_callback_thunk(CALL_SV, handler,
                                           NULL, "S", window, tinfo));
    }

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
static svn_error_t *
thunk_apply_textdelta(void *file_baton,
                      const char *base_checksum,
                      apr_pool_t *pool,
                      svn_txdelta_window_handler_t *handler,
                      void **h_baton)
{
    item_baton *ib = file_baton;
    SV *result;

    SVN_ERR(svn_swig_pl_callback_thunk(CALL_METHOD,
                                       (void *)"apply_textdelta", &result,
                                       "OOsS", ib->editor, ib->baton,
                                       base_checksum, pool, POOLINFO));
    if (SvOK(result)) {
        if (SvROK(result) && SvTYPE(SvRV(result)) == SVt_PVAV) {
            swig_type_info *handler_info =
              _SWIG_TYPE("svn_txdelta_window_handler_t");
            swig_type_info *void_info = _SWIG_TYPE("void *");
            AV *array = (AV *)SvRV(result);

            if (SWIG_ConvertPtr(*av_fetch(array, 0, 0),
                                (void **)handler, handler_info,0) < 0) {
                croak("Unable to convert from SWIG Type");
            }
            if (SWIG_ConvertPtr(*av_fetch(array, 1, 0),
                                h_baton, void_info,0) < 0) {
                croak("Unable to convert from SWIG Type ");
            }
            SvREFCNT_dec(result);
        }
        else {
            *handler = thunk_window_handler;
            *h_baton = result;
        }
    }
    else {
        *handler = svn_delta_noop_window_handler;
        *h_baton = NULL;
    }

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
static svn_error_t * thunk_change_file_prop(void *file_baton,
                                            const char *name,
                                            const svn_string_t *value,
                                            apr_pool_t *pool)
{
    item_baton *ib = file_baton;

    SVN_ERR(svn_swig_pl_callback_thunk(CALL_METHOD,
                                       (void *)"change_file_prop", NULL,
                                       "OOstS", ib->editor, ib->baton, name,
                                       value, pool, POOLINFO));

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
static svn_error_t * thunk_close_file(void *file_baton,
                                      const char *text_checksum,
                                      apr_pool_t *pool)
{
    item_baton *ib = file_baton;

    SVN_ERR(svn_swig_pl_callback_thunk(CALL_METHOD,
                                       (void *)"close_file", NULL, "OOsS",
                                       ib->editor, ib->baton, text_checksum,
                                       pool, POOLINFO));

    SvREFCNT_dec(ib->baton);

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
static svn_error_t * thunk_absent_file(const char *path,
                                       void *parent_baton,
                                       apr_pool_t *pool)
{
    item_baton *ib = parent_baton;

    SVN_ERR(svn_swig_pl_callback_thunk(CALL_METHOD,
                                       (void *)"absent_file", NULL,
                                       "OsOS", ib->editor, path, ib->baton,
                                       pool, POOLINFO));

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling close_baton) */
static svn_error_t * thunk_close_edit(void *edit_baton,
                                      apr_pool_t *pool)
{
    return close_baton(edit_baton, "close_edit", pool);
}

/* NOTE: calls back into Perl (by calling close_baton) */
static svn_error_t * thunk_abort_edit(void *edit_baton,
                                      apr_pool_t *pool)
{
    return close_baton(edit_baton, "abort_edit", pool);
}


void svn_swig_pl_make_editor(svn_delta_editor_t **editor,
                             void **edit_baton,
                             SV *perl_editor,
                             apr_pool_t *pool)
{
  svn_delta_editor_t *thunk_editor = svn_delta_default_editor(pool);

    thunk_editor->set_target_revision = thunk_set_target_revision;
    thunk_editor->open_root = thunk_open_root;
    thunk_editor->delete_entry = thunk_delete_entry;
    thunk_editor->add_directory = thunk_add_directory;
    thunk_editor->open_directory = thunk_open_directory;
    thunk_editor->change_dir_prop = thunk_change_dir_prop;
    thunk_editor->close_directory = thunk_close_directory;
    thunk_editor->absent_directory = thunk_absent_directory;
    thunk_editor->add_file = thunk_add_file;
    thunk_editor->open_file = thunk_open_file;
    thunk_editor->apply_textdelta = thunk_apply_textdelta;
    thunk_editor->change_file_prop = thunk_change_file_prop;
    thunk_editor->close_file = thunk_close_file;
    thunk_editor->absent_file = thunk_absent_file;
    thunk_editor->close_edit = thunk_close_edit;
    thunk_editor->abort_edit = thunk_abort_edit;

    *editor = thunk_editor;
    *edit_baton = make_baton(pool, perl_editor, NULL);
    svn_swig_pl_hold_ref_in_pool(pool, perl_editor);
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_error_t *svn_swig_pl_thunk_log_receiver(void *baton,
                                            apr_hash_t *changed_paths,
                                            svn_revnum_t rev,
                                            const char *author,
                                            const char *date,
                                            const char *msg,
                                            apr_pool_t *pool)
{
    SV *receiver = baton;
    swig_type_info *tinfo = _SWIG_TYPE("svn_log_changed_path_t *");

    if (!SvOK(receiver))
        return SVN_NO_ERROR;

    svn_swig_pl_callback_thunk(CALL_SV,
                               receiver, NULL,
                               "OrsssS", (changed_paths) ?
                               svn_swig_pl_convert_hash(changed_paths, tinfo)
                               : &PL_sv_undef,
                               rev, author, date, msg, pool, POOLINFO);

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_error_t *svn_swig_pl_thunk_log_entry_receiver(void *baton,
                                                  svn_log_entry_t *log_entry,
                                                  apr_pool_t *pool)
{
    SV *receiver = baton;

    if (!SvOK(receiver))
        return SVN_NO_ERROR;

    svn_swig_pl_callback_thunk(CALL_SV,
                               receiver, NULL,
                               "SS",
                               log_entry, _SWIG_TYPE("svn_log_entry_t *"),
                               pool, POOLINFO);

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_error_t * svn_swig_pl_thunk_client_diff_summarize_func(
                     const svn_client_diff_summarize_t *diff,
                     void *baton,
                     apr_pool_t *pool)
{
    SV *func = baton;

    if(!SvOK(func))
    return SVN_NO_ERROR;

    svn_swig_pl_callback_thunk(CALL_SV,
                               func, NULL,
                               "SS", diff,
                               _SWIG_TYPE("svn_client_diff_summarize_t *"),
                               pool, POOLINFO);

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_error_t *svn_swig_pl_thunk_history_func(void *baton,
                                            const char *path,
                                            svn_revnum_t revision,
                                            apr_pool_t *pool)
{
    SV *func = baton;

    if (!SvOK(func))
        return SVN_NO_ERROR;

    svn_swig_pl_callback_thunk(CALL_SV,
                               func, NULL,
                               "srS", path, revision, pool, POOLINFO);

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_error_t *svn_swig_pl_thunk_authz_func(svn_boolean_t *allowed,
                                          svn_fs_root_t *root,
                                          const char *path,
                                          void *baton,
                                          apr_pool_t *pool)
{
    SV *func = baton, *result;

    if (!SvOK(func))
        return SVN_NO_ERROR;

    svn_swig_pl_callback_thunk(CALL_SV,
                               func, &result,
                               "SsS", root, _SWIG_TYPE("svn_fs_root_t *"),
                               path, pool, POOLINFO);

    *allowed = SvIV(result);
    SvREFCNT_dec(result);

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_error_t *svn_swig_pl_thunk_commit_callback(svn_revnum_t new_revision,
                                               const char *date,
                                               const char *author,
                                               void *baton)
{
    if (!SvOK((SV *)baton))
        return SVN_NO_ERROR;

    svn_swig_pl_callback_thunk(CALL_SV, baton, NULL,
                               "rss", new_revision, date, author);

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_error_t *svn_swig_pl_thunk_commit_callback2(const svn_commit_info_t *commit_info,
                                                void *baton,
                                                apr_pool_t *pool)
{
    if (!SvOK((SV *)baton))
        return SVN_NO_ERROR;

    svn_swig_pl_callback_thunk(CALL_SV, baton, NULL,
                               "SS",
                               commit_info, _SWIG_TYPE("svn_commit_info_t *"),
                               pool, POOLINFO);

    return SVN_NO_ERROR;
}


/* Wrap RA */

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
static svn_error_t * thunk_open_tmp_file(apr_file_t **fp,
                                         void *callback_baton,
                                         apr_pool_t *pool)
{
    SV *result;
    swig_type_info *tinfo = _SWIG_TYPE("apr_file_t *");

    svn_swig_pl_callback_thunk(CALL_METHOD, (void *)"open_tmp_file",
                               &result, "OS", callback_baton, pool, POOLINFO);

    if (SWIG_ConvertPtr(result, (void *)fp, tinfo,0) < 0) {
        croak("Unable to convert from SWIG Type");
    }

    SvREFCNT_dec(result);
    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
static svn_error_t *thunk_get_wc_prop(void *baton,
                                      const char *relpath,
                                      const char *name,
                                      const svn_string_t **value,
                                      apr_pool_t *pool)
{
    SV *result;
    char *data;
    STRLEN len;

    svn_swig_pl_callback_thunk(CALL_METHOD, (void *)"get_wc_prop",
                               &result, "OssS", baton, relpath, name,
                               pool, POOLINFO);

    /* this is svn_string_t * typemap in */
    if (!SvOK(result) || result == &PL_sv_undef) {
        *value = NULL;
    }
    else if (SvPOK(result)) {
        data = SvPV(result, len);
        *value = svn_string_ncreate(data, len, pool);
    }
    else {
        SvREFCNT_dec(result);
        croak("not a string");
    }

    SvREFCNT_dec(result);
    return SVN_NO_ERROR;
}


svn_error_t *svn_swig_pl_make_callbacks(svn_ra_callbacks_t **cb,
                                        void **c_baton,
                                        SV *perl_callbacks,
                                        apr_pool_t *pool)
{
    SV *auth_baton;

    *cb = apr_pcalloc(pool, sizeof(**cb));

    (*cb)->open_tmp_file = thunk_open_tmp_file;
    (*cb)->get_wc_prop = thunk_get_wc_prop;
    (*cb)->set_wc_prop = NULL;
    (*cb)->push_wc_prop = NULL;
    (*cb)->invalidate_wc_props = NULL;
    auth_baton = *hv_fetch((HV *)SvRV(perl_callbacks), "auth", 4, 0);

    if (SWIG_ConvertPtr(auth_baton,
                        (void **)&(*cb)->auth_baton, _SWIG_TYPE("svn_auth_baton_t *"),0) < 0) {
        croak("Unable to convert from SWIG Type");
    }
    *c_baton = perl_callbacks;
    svn_swig_pl_hold_ref_in_pool(pool, perl_callbacks);
    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_error_t *svn_swig_pl_thunk_gnome_keyring_unlock_prompt(char **keyring_password,
                                                           const char *keyring_name,
                                                           void *baton,
                                                           apr_pool_t *pool)
{
    SV *result;
    STRLEN len;
    /* The baton is the actual prompt function passed from perl, so we
     * call that one and process the result. */
    svn_swig_pl_callback_thunk(CALL_SV,
                               baton, &result,
                               "sS", keyring_name,
                               pool, POOLINFO);
    if (!SvOK(result) || result == &PL_sv_undef) {
        *keyring_password = NULL;
    }
    else if (SvPOK(result)) {
        *keyring_password = apr_pstrdup(pool, SvPV(result, len));
    }
    else {
        SvREFCNT_dec(result);
        croak("not a string");
    }

    SvREFCNT_dec(result);
    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_error_t *svn_swig_pl_thunk_simple_prompt(svn_auth_cred_simple_t **cred,
                                             void *baton,
                                             const char *realm,
                                             const char *username,
                                             svn_boolean_t may_save,
                                             apr_pool_t *pool)
{
    /* Be nice and allocate the memory for the cred structure before passing it
     * off to the perl space */
  *cred = apr_pcalloc(pool, sizeof(**cred));
    if (!*cred) {
      croak("Could not allocate memory for cred structure");
    }
    svn_swig_pl_callback_thunk(CALL_SV,
                               baton, NULL,
                               "SssbS", *cred, _SWIG_TYPE("svn_auth_cred_simple_t *"),
                               realm, username, may_save, pool, POOLINFO);

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_error_t *svn_swig_pl_thunk_username_prompt(svn_auth_cred_username_t **cred,
                                               void *baton,
                                               const char *realm,
                                               svn_boolean_t may_save,
                                               apr_pool_t *pool)
{
    /* Be nice and allocate the memory for the cred structure before passing it
     * off to the perl space */
  *cred = apr_pcalloc(pool, sizeof(**cred));
    if (!*cred) {
      croak("Could not allocate memory for cred structure");
    }
    svn_swig_pl_callback_thunk(CALL_SV,
                               baton, NULL,
                               "SsbS", *cred, _SWIG_TYPE("svn_auth_cred_username_t *"),
                               realm, may_save, pool, POOLINFO);

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_error_t *svn_swig_pl_thunk_ssl_server_trust_prompt(
                              svn_auth_cred_ssl_server_trust_t **cred,
                              void *baton,
                              const char *realm,
                              apr_uint32_t failures,
                              const svn_auth_ssl_server_cert_info_t *cert_info,
                              svn_boolean_t may_save,
                              apr_pool_t *pool)
{
    /* Be nice and allocate the memory for the cred structure before passing it
     * off to the perl space */
  *cred = apr_pcalloc(pool, sizeof(**cred));
    if (!*cred) {
      croak("Could not allocate memory for cred structure");
    }
    svn_swig_pl_callback_thunk(CALL_SV,
                               baton, NULL,
                               "SsiSbS", *cred, _SWIG_TYPE("svn_auth_cred_ssl_server_trust_t *"),
                               realm, failures,
                               cert_info, _SWIG_TYPE("svn_auth_ssl_server_cert_info_t *"),
                               may_save, pool, POOLINFO);

    /* Allow the perl callback to indicate failure by setting all vars to 0
     * or by simply doing nothing.  While still allowing them to indicate
     * failure by setting the cred strucutre's pointer to 0 via $$cred = 0 */
    if (*cred) {
        if ((*cred)->may_save == 0 && (*cred)->accepted_failures == 0) {
            *cred = NULL;
        }
    }

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_error_t *svn_swig_pl_thunk_ssl_client_cert_prompt(
                svn_auth_cred_ssl_client_cert_t **cred,
                void *baton,
                const char * realm,
                svn_boolean_t may_save,
                apr_pool_t *pool)
{
    /* Be nice and allocate the memory for the cred structure before passing it
     * off to the perl space */
  *cred = apr_pcalloc(pool, sizeof(**cred));
    if (!*cred) {
      croak("Could not allocate memory for cred structure");
    }
    svn_swig_pl_callback_thunk(CALL_SV,
                               baton, NULL,
                               "SsbS", *cred, _SWIG_TYPE("svn_auth_cred_ssl_client_cert_t *"),
                               realm, may_save, pool, POOLINFO);

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_error_t *svn_swig_pl_thunk_ssl_client_cert_pw_prompt(
                                     svn_auth_cred_ssl_client_cert_pw_t **cred,
                                     void *baton,
                                     const char *realm,
                                     svn_boolean_t may_save,
                                     apr_pool_t *pool)
{
    /* Be nice and allocate the memory for the cred structure before passing it
     * off to the perl space */
  *cred = apr_pcalloc(pool, sizeof(**cred));
    if (!*cred) {
      croak("Could not allocate memory for cred structure");
    }
    svn_swig_pl_callback_thunk(CALL_SV,
                               baton, NULL,
                               "SsbS", *cred, _SWIG_TYPE("svn_auth_cred_ssl_client_cert_pw_t *"),
                               realm, may_save, pool, POOLINFO);

    return SVN_NO_ERROR;
}

/* Thunked version of svn_wc_notify_func_t callback type */
/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
void svn_swig_pl_notify_func(void * baton,
                             const char *path,
                             svn_wc_notify_action_t action,
                             svn_node_kind_t kind,
                             const char *mime_type,
                             svn_wc_notify_state_t content_state,
                             svn_wc_notify_state_t prop_state,
                             svn_revnum_t revision)
{
    if (!SvOK((SV *)baton)) {
        return;
    }

    svn_swig_pl_callback_thunk(CALL_SV,
                               baton, NULL,
                               "siisiir", path, action, kind, mime_type,
                               content_state, prop_state, revision);

}

/* Thunked version of svn_client_get_commit_log3_t callback type. */
/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_error_t *svn_swig_pl_get_commit_log_func(const char **log_msg,
                                             const char **tmp_file,
                                             const apr_array_header_t *
                                             commit_items,
                                             void *baton,
                                             apr_pool_t *pool)
{
    SV *result;
    svn_error_t *ret_val = SVN_NO_ERROR;
    SV *log_msg_sv;
    SV *tmp_file_sv;
    SV *commit_items_sv;

    if (!SvOK((SV *)baton)) {
      *log_msg = apr_pstrdup(pool, "");
        *tmp_file = NULL;
        return SVN_NO_ERROR;
    }

    log_msg_sv = newRV_noinc(sv_newmortal());
    tmp_file_sv = newRV_noinc(sv_newmortal());
    commit_items_sv = svn_swig_pl_convert_array
      (commit_items, _SWIG_TYPE("svn_client_commit_item3_t *"));

    svn_swig_pl_callback_thunk(CALL_SV,
                               baton, &result,
                               "OOOS", log_msg_sv, tmp_file_sv,
                               commit_items_sv, pool, POOLINFO);

    if (!SvOK(SvRV(log_msg_sv))) {
        /* client returned undef to us */
        *log_msg = NULL;
    } else if (SvPOK(SvRV(log_msg_sv))) {
        /* client returned string so get the string and then duplicate
         * it using pool memory */
        *log_msg = apr_pstrdup(pool, SvPV_nolen(SvRV(log_msg_sv)));
    } else {
        croak("Invalid value in log_msg reference, must be undef or a string");
    }

    if (!SvOK(SvRV(tmp_file_sv))) {
        *tmp_file = NULL;
    } else if (SvPOK(SvRV(tmp_file_sv))) {
        *tmp_file = apr_pstrdup(pool, SvPV_nolen(SvRV(tmp_file_sv)));
    } else {
        croak("Invalid value in tmp_file reference, "
              "must be undef or a string");
    }

    if (sv_derived_from(result, "_p_svn_error_t")) {
        swig_type_info *errorinfo = _SWIG_TYPE("svn_error_t *");
        if (SWIG_ConvertPtr(result, (void *)&ret_val, errorinfo, 0) < 0) {
            SvREFCNT_dec(result);
            croak("Unable to convert from SWIG Type");
        }
    }

    SvREFCNT_dec(result);
    return ret_val;
}

/* Thunked version of svn_client_info_t callback type. */
/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_error_t *svn_swig_pl_info_receiver(void *baton,
                                       const char *path,
                                       const svn_info_t *info,
                                       apr_pool_t *pool)
{
    SV *result;
    svn_error_t *ret_val;
    swig_type_info *infoinfo = _SWIG_TYPE("svn_info_t *");

    if (!SvOK((SV *)baton))
        return SVN_NO_ERROR;

    svn_swig_pl_callback_thunk(CALL_SV, baton, &result, "sSS", path, info,
                               infoinfo, pool, POOLINFO);

    if (sv_derived_from(result, "_p_svn_error_t")) {
        swig_type_info *errorinfo = _SWIG_TYPE("svn_error_t *");
        if (SWIG_ConvertPtr(result, (void *)&ret_val, errorinfo, 0) < 0) {
            SvREFCNT_dec(result);
            croak("Unable to convert from SWIG Type");
        }
    }
    else
        ret_val = SVN_NO_ERROR;

    SvREFCNT_dec(result);
    return ret_val;
}


/* Thunked version of svn_wc_cancel_func_t callback type. */
/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_error_t *svn_swig_pl_cancel_func(void *cancel_baton) {
    SV *result;
    svn_error_t *ret_val;

    if (!SvOK((SV *)cancel_baton)) {
        return SVN_NO_ERROR;
    }
    svn_swig_pl_callback_thunk(CALL_SV, cancel_baton, &result, "");

    if (sv_derived_from(result,"_p_svn_error_t")) {
        swig_type_info *errorinfo = _SWIG_TYPE("svn_error_t *");
        if (SWIG_ConvertPtr(result, (void *)&ret_val, errorinfo, 0) < 0) {
            SvREFCNT_dec(result);
            croak("Unable to convert from SWIG Type");
        }
    } else if (SvIOK(result) && SvIV(result)) {
        ret_val = svn_error_create(SVN_ERR_CANCELLED, NULL,
                                   "By cancel callback");
    } else if (SvTRUE(result) && SvPOK(result)) {
        ret_val = svn_error_create(SVN_ERR_CANCELLED, NULL,
                                   SvPV_nolen(result));
    } else {
        ret_val = SVN_NO_ERROR;
    }
    SvREFCNT_dec(result);
    return ret_val;
}

/* Thunked version of svn_wc_status_func_t callback type. */
/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
void svn_swig_pl_status_func(void *baton,
                             const char *path,
                             svn_wc_status_t *status)
{
  swig_type_info *statusinfo = _SWIG_TYPE("svn_wc_status_t *");

  if (!SvOK((SV *)baton)) {
    return;
  }

  svn_swig_pl_callback_thunk(CALL_SV, baton, NULL, "sS",
                             path, status, statusinfo);

}

/* Thunked version of svn_wc_status_func2_t callback type. */
/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
void svn_swig_pl_status_func2(void *baton,
                              const char *path,
                              svn_wc_status2_t *status)
{
  swig_type_info *statusinfo = _SWIG_TYPE("svn_wc_status2 _t *");

  if (!SvOK((SV *)baton)) {
    return;
  }

  svn_swig_pl_callback_thunk(CALL_SV, baton, NULL, "sS",
                             path, status, statusinfo);

}

/* Thunked version of svn_wc_status_func3_t callback type. */
/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_error_t *svn_swig_pl_status_func3(void *baton,
                                      const char *path,
                                      svn_wc_status2_t *status,
                                      apr_pool_t *pool)
{
  SV *result;
  svn_error_t *ret_val = SVN_NO_ERROR;

  swig_type_info *statusinfo = _SWIG_TYPE("svn_wc_status2 _t *");

  if (!SvOK((SV *)baton)) {
    return ret_val;
  }

  svn_swig_pl_callback_thunk(CALL_SV, baton, &result, "sSS",
                             path, status, statusinfo,
                             pool, POOLINFO);

  if (sv_derived_from(result, "_p_svn_error_t")) {
    swig_type_info *errorinfo = _SWIG_TYPE("svn_error_t *");
    if (SWIG_ConvertPtr(result, (void *)&ret_val, errorinfo, 0) < 0) {
        SvREFCNT_dec(result);
        croak("Unable to convert from SWIG Type");
    }
  }

  SvREFCNT_dec(result);
  return ret_val;
}


/* Thunked version of svn_client_blame_receiver_t callback type. */
/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_error_t *svn_swig_pl_blame_func(void *baton,
                                    apr_int64_t line_no,
                                    svn_revnum_t revision,
                                    const char *author,
                                    const char *date,
                                    const char *line,
                                    apr_pool_t *pool)
{
    SV *result;
    svn_error_t *ret_val = SVN_NO_ERROR;

    svn_swig_pl_callback_thunk(CALL_SV, baton, &result, "LrsssS",
                               line_no, revision, author, date, line,
                               pool, POOLINFO);

    if (sv_derived_from(result, "_p_svn_error_t")) {
        swig_type_info *errorinfo = _SWIG_TYPE("svn_error_t *");
        if (SWIG_ConvertPtr(result, (void *)&ret_val, errorinfo, 0) < 0) {
            SvREFCNT_dec(result);
            croak("Unable to convert from SWIG Type");
        }
    }

    SvREFCNT_dec(result);
    return ret_val;
}

/* Thunked config enumerator */
/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_boolean_t svn_swig_pl_thunk_config_enumerator(const char *name, const char *value, void *baton)
{
    SV *result;
    if (!SvOK((SV *)baton))
        return 0;

    svn_swig_pl_callback_thunk(CALL_SV, baton, &result,
                               "ss", name, value);

    return SvOK(result);
}


/* default pool support */

static svn_swig_pl_get_current_pool_func_t get_current_pool_cb = NULL;
static svn_swig_pl_set_current_pool_func_t set_current_pool_cb = NULL;

void
svn_swig_pl__bind_current_pool_fns(svn_swig_pl_get_current_pool_func_t get,
                                   svn_swig_pl_set_current_pool_func_t set)
{
  /* This function should only be called ONCE, otherwise there are two
     global variables CURRENT_POOL */
  SVN_ERR_ASSERT_NO_RETURN(get_current_pool_cb == NULL
                           && set_current_pool_cb == NULL);

  get_current_pool_cb = get;
  set_current_pool_cb = set;
}

apr_pool_t * svn_swig_pl_get_current_pool()
{
  SVN_ERR_ASSERT_NO_RETURN(get_current_pool_cb != NULL);
  return get_current_pool_cb();
}

void svn_swig_pl_set_current_pool(apr_pool_t *pool)
{
  SVN_ERR_ASSERT_NO_RETURN(set_current_pool_cb != NULL);
  set_current_pool_cb(pool);
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
apr_pool_t *svn_swig_pl_make_pool(SV *obj)
{
    apr_pool_t *pool;

    if (obj && sv_isobject(obj)) {
      if (sv_derived_from(obj, "SVN::Pool")) {
            obj = SvRV(obj);
        }
        if (sv_derived_from(obj, "_p_apr_pool_t")) {
            SWIG_ConvertPtr(obj, (void **)&pool, POOLINFO, 0);
            return pool;
        }
    }

    if (!svn_swig_pl_get_current_pool())
      svn_swig_pl_callback_thunk(CALL_METHOD, (void *)"new_default",
                                 &obj, "s", "SVN::Pool");

    return svn_swig_pl_get_current_pool();
}

/* stream interpolability with io::handle */

typedef struct io_baton_t {
    SV *obj;
    IO *io;
} io_baton_t;

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
static svn_error_t *io_handle_read(void *baton,
                                   char *buffer,
                                   apr_size_t *len)
{
    io_baton_t *io = baton;
    MAGIC *mg;

    if ((mg = SvTIED_mg((SV*)io->io, PERL_MAGIC_tiedscalar))) {
        SV *ret;
        SV *buf = sv_newmortal();

        svn_swig_pl_callback_thunk(CALL_METHOD, (void *)"READ", &ret, "OOz",
                                   SvTIED_obj((SV*)io->io, mg),
                                   buf, *len);
        *len = SvIV(ret);
        SvREFCNT_dec(ret);
        memmove(buffer, SvPV_nolen(buf), *len);
    }
    else
      *len = PerlIO_read(IoIFP(io->io), buffer, *len);
    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
static svn_error_t *io_handle_write(void *baton,
                                    const char *data,
                                    apr_size_t *len)
{
    io_baton_t *io = baton;
    MAGIC *mg;

    if ((mg = SvTIED_mg((SV*)io->io, PERL_MAGIC_tiedscalar))) {
        SV *ret, *pv;
        pv = sv_2mortal(newSVpvn(data, *len));
        svn_swig_pl_callback_thunk(CALL_METHOD, (void *)"WRITE", &ret, "OOz",
                                   SvTIED_obj((SV*)io->io, mg), pv, *len);
        *len = SvIV(ret);
        SvREFCNT_dec(ret);
    }
    else
      *len = PerlIO_write(IoIFP(io->io), data, *len);
    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
static svn_error_t *io_handle_close(void *baton)
{
    io_baton_t *io = baton;
    MAGIC *mg;

    if ((mg = SvTIED_mg((SV*)io->io, PERL_MAGIC_tiedscalar))) {
      svn_swig_pl_callback_thunk(CALL_METHOD, (void *)"CLOSE", NULL, "O",
                                 SvTIED_obj((SV*)io->io, mg));
    }
    else {
      PerlIO_close(IoIFP(io->io));
    }

    return SVN_NO_ERROR;
}

static apr_status_t io_handle_cleanup(void *baton)
{
    io_baton_t *io = baton;
    SvREFCNT_dec(io->obj);
    return APR_SUCCESS;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_error_t *svn_swig_pl_make_stream(svn_stream_t **stream, SV *obj)
{
    IO *io;

    if (!SvOK(obj)) {
        *stream = NULL;
        return SVN_NO_ERROR;
    }

    if (obj && sv_isobject(obj)) {
      int simple_type = 1;
      if (sv_derived_from(obj, "SVN::Stream"))
        svn_swig_pl_callback_thunk(CALL_METHOD, (void *)"svn_stream",
                                   &obj, "O", obj);
        else if (!sv_derived_from(obj, "_p_svn_stream_t"))
            simple_type = 0;

        if (simple_type) {
            SWIG_ConvertPtr(obj, (void **)stream, _SWIG_TYPE("svn_stream_t *"), 0);
            return SVN_NO_ERROR;
        }
    }

    if (obj && SvROK(obj) && SvTYPE(SvRV(obj)) == SVt_PVGV &&
        (io = GvIO(SvRV(obj)))) {
        apr_pool_t *pool = svn_swig_pl_get_current_pool();
        io_baton_t *iob = apr_palloc(pool, sizeof(io_baton_t));
        SvREFCNT_inc(obj);
        iob->obj = obj;
        iob->io = io;
        *stream = svn_stream_create(iob, pool);
        svn_stream_set_read2(*stream, NULL /* only full read support */,
                             io_handle_read);
        svn_stream_set_write(*stream, io_handle_write);
        svn_stream_set_close(*stream, io_handle_close);
        apr_pool_cleanup_register(pool, iob, io_handle_cleanup,
                                  io_handle_cleanup);

    }
    else
      croak("unknown type for svn_stream_t");

    return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
svn_error_t *svn_swig_pl_ra_lock_callback(
                    void *baton,
                    const char *path,
                    svn_boolean_t do_lock,
                    const svn_lock_t *lock,
                    svn_error_t *ra_err,
                    apr_pool_t *pool)
{
  if (!SvOK((SV *)baton))
      return SVN_NO_ERROR;

  SVN_ERR(svn_swig_pl_callback_thunk(CALL_SV, baton, NULL, "sbSSS",
                                     path, do_lock,
                                     lock, _SWIG_TYPE("svn_lock_t *"),
                                     ra_err, _SWIG_TYPE("svn_error_t *"),
                                     pool, POOLINFO));
  return SVN_NO_ERROR;
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
SV *svn_swig_pl_from_stream(svn_stream_t *stream)
{
    SV *ret;

    svn_swig_pl_callback_thunk(CALL_METHOD, (void *)"new", &ret, "sS",
                               "SVN::Stream", stream, _SWIG_TYPE("svn_stream_t *"));

    return sv_2mortal(ret);
}

apr_file_t *svn_swig_pl_make_file(SV *file, apr_pool_t *pool)
{
    apr_file_t *apr_file = NULL;

    if (!SvOK(file) || file == &PL_sv_undef)
        return NULL;

    if (SvPOKp(file)) {
      apr_file_open(&apr_file, SvPV_nolen(file),
                    APR_CREATE | APR_READ | APR_WRITE,
                    APR_OS_DEFAULT,
                    pool);
    } else if (SvROK(file) && SvTYPE(SvRV(file)) == SVt_PVGV) {
        apr_status_t status;
#ifdef WIN32
        apr_os_file_t osfile = (apr_os_file_t)
          _get_osfhandle(PerlIO_fileno(IoIFP(sv_2io(file))));
#else
        apr_os_file_t osfile = PerlIO_fileno(IoIFP(sv_2io(file)));
#endif
        status = apr_os_file_put(&apr_file, &osfile,
                                 O_CREAT | O_WRONLY, pool);
        if (status)
            return NULL;
    }
    return apr_file;
}

static apr_status_t cleanup_refcnt(void *data)
{
    SV *sv = data;
    SvREFCNT_dec(sv);
    return APR_SUCCESS;
}

void svn_swig_pl_hold_ref_in_pool(apr_pool_t *pool, SV *sv)
{
    SvREFCNT_inc(sv);
    apr_pool_cleanup_register(pool, sv, cleanup_refcnt, apr_pool_cleanup_null);
}

/* NOTE: calls back into Perl (by calling svn_swig_pl_callback_thunk) */
SV *svn_swig_pl_from_md5(unsigned char *digest)
{
    SV *ret;

    svn_swig_pl_callback_thunk(CALL_METHOD, (void *)"new", &ret, "sS",
                               "SVN::MD5", digest,
                               _SWIG_TYPE("unsigned char *"));

    return sv_2mortal(ret);
}
