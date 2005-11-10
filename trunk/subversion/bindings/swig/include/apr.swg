/*
 * apr.swg :  SWIG include file for selected APR types
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

/* This is the interface for the APR headers. This is not built as a module
   because we aren't going to wrap the APR functions. Thus, we only define
   the various types in here, as necessary.

   Actually, core.i wraps a few, key functions.
*/

#ifdef SWIGRUBY
/* Inhibit incorrect class name warning. */
#pragma SWIG nowarn=801
#endif

%include typemaps.i

#ifdef SWIGRUBY
#if SWIG_VERSION <= 0x010324
/* for SWIG bug */
%define OUTPUT_TYPEMAP(type, converter, convtype)
%typemap(in,numinputs=0) type *OUTPUT($*1_ltype temp), type &OUTPUT($*1_ltype temp) "$1 = &temp;";
%typemap(argout, fragment="output_helper") type *OUTPUT, type &OUTPUT {
   VALUE o = converter(convtype (*$1));
   $result = output_helper($result, o);
}
%enddef

OUTPUT_TYPEMAP(long, INT2NUM, (long));
OUTPUT_TYPEMAP(long long, LL2NUM, (apr_int64_t));
OUTPUT_TYPEMAP(unsigned long, UINT2NUM, (unsigned long));
OUTPUT_TYPEMAP(unsigned long long, ULL2NUM, (apr_uint64_t));

#undef OUTPUT_TYPEMAP
#endif

#endif

/* ----------------------------------------------------------------------- */

/* ### be nice to have all the error values and macros. there are some
   ### problems including this file, tho. SWIG isn't smart enough with some
   ### of the preprocessing and thinks there is a macro redefinition */
//%include apr_errno_h.swg
typedef int apr_status_t;

/* -----------------------------------------------------------------------
   Create perl5 typemaps for long long datatypes
*/
%typemap(perl5, out) long long {
    char temp[256];
    sprintf(temp, "%" APR_INT64_T_FMT, (apr_int64_t) $1);
    ST(argvi) = sv_newmortal();
    sv_setpv((SV*)ST(argvi++), temp);
}

%typemap(perl5, out) unsigned long long {
    char temp[256];
    sprintf(temp, "%" APR_UINT64_T_FMT, (apr_uint64_t) $1);
    ST(argvi) = sv_newmortal();
    sv_setpv((SV*)ST(argvi++), temp);
}

%typemap(perl5, in, numinputs=0) long long *OUTPUT (apr_int64_t temp)
    "$1 = &temp;";
%typemap(perl5, argout) long long *OUTPUT {
  char temp[256];
  if (argvi >= items) {
    EXTEND(sp,1);
  }
  sprintf(temp, "%" APR_INT64_T_FMT, (apr_int64_t)*($1));
  $result = sv_newmortal();
  sv_setpv($result,temp);
  argvi++;
}

%typemap(perl5, in, numinputs=0) unsigned long long *OUTPUT (apr_uint64_t temp)
    "$1 = &temp;";
%typemap(perl5, argout) unsigned long long *OUTPUT {
  char temp[256];
  if (argvi >= items) {
    EXTEND(sp,1);
  }
  sprintf(temp, "%" APR_UINT64_T_FMT, (apr_uint64_t)*($1));
  $result = sv_newmortal();
  sv_setpv($result,temp);
  argvi++;
}

/* -----------------------------------------------------------------------
 * APR datatypes (from apr.h)
 */

/* Integers for which sizeof(type) <= sizeof(long) */
%apply long { apr_int16_t, apr_int32_t, apr_ssize_t }
%apply unsigned long { apr_byte_t, apr_uint16_t, apr_uint32_t, apr_size_t }
%apply unsigned long *OUTPUT { apr_uint32_t *, apr_size_t * }

/* If sizeof(apr_off_t) > sizeof(apr_int64_t), apr_off_t will get truncated
 * to a 64-bit integer */ 
%apply long long { apr_off_t, apr_int64_t }
%apply unsigned long long { apr_uint64_t }

/* -----------------------------------------------------------------------
   apr_time_t
*/

/* Define the time type (rather than picking up all of apr_time.h) */
typedef apr_int64_t apr_time_t;

/* For apr_time_ansi_put().
   We guess, because only the system C compiler can successfully parse
   system headers if they incorporate weird include paths
   (e.g. /usr/lib/gcc-lib/plat/ver/include). */
typedef apr_int32_t time_t;

%apply long long *OUTPUT { apr_time_t * };

/* -----------------------------------------------------------------------
   create an OUTPUT argument typemap for an apr_hash_t **
*/

%typemap(python,in,numinputs=0) apr_hash_t **OUTPUT (apr_hash_t *temp)
    "$1 = &temp;";

%typemap(perl5,in,numinputs=0) apr_hash_t **OUTPUT (apr_hash_t *temp)
    "$1 = &temp;";

%typemap(ruby,in,numinputs=0) apr_hash_t **OUTPUT (apr_hash_t *temp)
    "$1 = &temp;";

/* -----------------------------------------------------------------------
   create an OUTPUT argument defn for an apr_hash_t ** which is storing
   property values
*/

%typemap(python,in,numinputs=0) apr_hash_t **PROPHASH = apr_hash_t **OUTPUT;
%typemap(python,argout) apr_hash_t **PROPHASH {
    /* toss prior result, get new result from the hash */
    Py_DECREF($result);
    $result = svn_swig_py_prophash_to_dict(*$1);
}

%typemap(perl5,in,numinputs=0) apr_hash_t **PROPHASH = apr_hash_t **OUTPUT;
%typemap(perl5,argout) apr_hash_t **PROPHASH {
    $result = svn_swig_pl_prophash_to_hash(*$1);
    argvi++;
}

%typemap(ruby,in,numinputs=0) apr_hash_t **PROPHASH = apr_hash_t **OUTPUT;
%typemap(ruby, argout, fragment="output_helper") apr_hash_t **PROPHASH
{
  $result = output_helper($result,
                          svn_swig_rb_apr_hash_to_hash_svn_string(*$1));
}

/* -----------------------------------------------------------------------
   create an INPUT argument defn for an apr_hash_t * which is storing
   property values
*/
%typemap(ruby, in) apr_hash_t *PROPHASH
{
  $1 = svn_swig_rb_hash_to_apr_hash_svn_string($input, _global_pool);
}

%typemap(python, in) apr_hash_t *PROPHASH
{
  $1 = svn_swig_py_prophash_from_dict($input, _global_pool);
}

/* -----------------------------------------------------------------------
   create an OUTPUT argument defn for an apr_array_header_t ** which is
   storing svn_prop_t * property values
*/
%typemap(python, in, numinputs=0) 
     apr_array_header_t **OUTPUT_OF_PROP (apr_array_header_t *temp)
{
  $1 = &temp;
}
%typemap(python, argout, fragment="output_helper")
     apr_array_header_t **OUTPUT_OF_PROP
{
  /* FIXME: Actually do something here. */
}

%typemap(ruby, in, numinputs=0)
     apr_array_header_t **OUTPUT_OF_PROP (apr_array_header_t *temp)
{
  $1 = &temp;
}
%typemap(ruby, argout, fragment="output_helper")
     apr_array_header_t **OUTPUT_OF_PROP
{
  $result = output_helper($result, svn_swig_rb_apr_array_to_array_prop(*$1));
}


/* -----------------------------------------------------------------------
   apr_array_header_t ** <const char *>
*/

%typemap(in, numinputs=0) apr_array_header_t **OUTPUT_OF_CONST_CHAR_P
(apr_array_header_t *temp) {
    $1 = &temp;
}
%typemap(python, argout, fragment="t_output_helper")
apr_array_header_t **OUTPUT_OF_CONST_CHAR_P {
    $result = t_output_helper($result, svn_swig_py_array_to_list(*$1));
}
%typemap(perl5, argout) apr_array_header_t **OUTPUT_OF_CONST_CHAR_P {
    $result = svn_swig_pl_array_to_list(*$1);
    ++argvi;
}
%typemap(ruby, argout, fragment="output_helper")
     apr_array_header_t **OUTPUT_OF_CONST_CHAR_P
{
  $result = output_helper($result, svn_swig_rb_apr_array_to_array_string(*$1));
}

/* -----------------------------------------------------------------------
  handle apr_file_t *
*/

%typemap(python, in) apr_file_t * {
  $1 = svn_swig_py_make_file($input, _global_pool);
  if (!$1) SWIG_fail;
}

%typemap(perl5, in) apr_file_t * {
  $1 = svn_swig_pl_make_file($input, _global_pool);
}

%typemap(ruby, in) apr_file_t * {
  $1 = svn_swig_rb_make_file($input, _global_pool);
}

/* -----------------------------------------------------------------------
   apr_file_t ** is always an OUT param
*/

%typemap(in, numinputs=0) apr_file_t ** (apr_file_t *temp)
    "$1 = &temp;";

%typemap(python,argout,fragment="t_output_helper") apr_file_t **
    "$result = t_output_helper(
        $result,
        svn_swig_NewPointerObj(*$1, $*1_descriptor, 
           _global_svn_swig_py_pool));";

%typemap(perl5, argout) apr_file_t ** {
    ST(argvi) = sv_newmortal();
    SWIG_MakePtr(ST(argvi++), (void *)*$1, $*1_descriptor,0);
}

%typemap(ruby, argout, fragment="output_helper") apr_file_t ** {
    $result = output_helper($result,
                            SWIG_NewPointerObj((void *)*$1, $*1_descriptor, 0));
}

/* ----------------------------------------------------------------------- */
