/*
 * apr.i :  SWIG interface file for selected APR types
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

#if SVN_SWIG_VERSION <= 103024
/* for SWIG bug */
%typemap(ruby, argout, fragment="output_helper") long long *OUTPUT, long long &OUTPUT
{
  $result = output_helper($result, LL2NUM(*$1));
}
%typemap(ruby, argout, fragment="output_helper") unsigned long long *OUTPUT, unsigned long long &OUTPUT
{
  $result = output_helper($result, ULL2NUM(*$1));
}
#endif

/* ----------------------------------------------------------------------- */

%include apr.h

/* ### be nice to have all the error values and macros. there are some
   ### problems including this file, tho. SWIG isn't smart enough with some
   ### of the preprocessing and thinks there is a macro redefinition */
//%include apr_errno.h
typedef int apr_status_t;

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

#if APR_INT64_T_FMT == "ld"
    %apply long *OUTPUT { apr_time_t * };
#else
    %apply long long *OUTPUT { apr_time_t * };
#endif

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
%typemap(ruby,argout) apr_hash_t **PROPHASH {
    $result = svn_swig_rb_apr_hash_to_hash_svn_string(*$1);
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
  $result = $1; /* ### WRONG! */
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
        SWIG_NewPointerObj(*$1, $*1_descriptor, 0));";

%typemap(perl5, argout) apr_file_t ** {
    ST(argvi) = sv_newmortal();
    SWIG_MakePtr(ST(argvi++), (void *)*$1, $*1_descriptor,0);
}

%typemap(ruby, argout, fragment="output_helper") apr_file_t ** {
    $result = output_helper($result,
                            SWIG_NewPointerObj((void *)*$1, $*1_descriptor, 1));
}

/* ----------------------------------------------------------------------- */
