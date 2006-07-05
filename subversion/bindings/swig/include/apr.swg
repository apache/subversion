/*
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
 *
 * apr.swg: This is a child file of svn_types.swg, and should not be
 *   included directly.  This file should contain typemaps that deal
 *   with APR functionality, not involving any parts of Subversion
 *   (that last phrase currently isn't fully obeyed).
 */

/* -----------------------------------------------------------------------
   Create perl5 typemaps for long long datatypes
   PERL-FIXME: These typemaps are decidedly odd.  They convert long long
     values to strings before returning them.  This behaviour is either
     wrong, or needful of a comment explaining it.
*/
#ifdef SWIGPERL
%typemap(out) long long {
    char temp[256];
    sprintf(temp, "%" APR_INT64_T_FMT, (apr_int64_t) $1);
    ST(argvi) = sv_newmortal();
    sv_setpv((SV*)ST(argvi++), temp);
}

%typemap(out) unsigned long long {
    char temp[256];
    sprintf(temp, "%" APR_UINT64_T_FMT, (apr_uint64_t) $1);
    ST(argvi) = sv_newmortal();
    sv_setpv((SV*)ST(argvi++), temp);
}

%typemap(in, numinputs=0) long long *OUTPUT (apr_int64_t temp)
    "$1 = &temp;";
%typemap(argout) long long *OUTPUT {
  char temp[256];
  sprintf(temp, "%" APR_INT64_T_FMT, (apr_int64_t)*($1));
  %append_output(sv_2mortal(newSVpv(temp, 0)));
}

%typemap(in, numinputs=0) unsigned long long *OUTPUT (apr_uint64_t temp)
    "$1 = &temp;";
%typemap(argout) unsigned long long *OUTPUT {
  char temp[256];
  sprintf(temp, "%" APR_UINT64_T_FMT, (apr_uint64_t)*($1));
  %append_output(sv_2mortal(newSVpv(temp, 0)));
}
#endif

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
%apply long long *OUTPUT { apr_int64_t * };
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

/* -----------------------------------------------------------------------
   Output of apr_hash_t * <svn_dirent_t *> 
*/

#ifdef SWIGPYTHON
%typemap(argout) apr_hash_t **DIRENTHASH {
  %append_output(svn_swig_py_convert_hash(*$1,
                                          $descriptor(svn_dirent_t *),
                                          NULL));
}
#endif

#ifdef SWIGRUBY
%typemap(argout) apr_hash_t **DIRENTHASH {
  %append_output(svn_swig_rb_apr_hash_to_hash_swig_type(*$1,
                                                        "svn_dirent_t *"));
}
#endif

/* -----------------------------------------------------------------------
   Output of apr_hash_t * <const char *, svn_string_t *>
   (commonly used for property name and value pairs)
*/

#ifdef SWIGPYTHON
%typemap(argout) apr_hash_t **PROPHASH {
  %append_output(svn_swig_py_prophash_to_dict(*$1));
}
#endif

#ifdef SWIGPERL
%typemap(argout) apr_hash_t **PROPHASH {
  %append_output(svn_swig_pl_prophash_to_hash(*$1));
}
#endif

#ifdef SWIGRUBY
%typemap(argout) apr_hash_t **PROPHASH {
  %append_output(svn_swig_rb_apr_hash_to_hash_svn_string(*$1));
}
#endif

/* -----------------------------------------------------------------------
   Input of apr_hash_t * <const char *, svn_string_t *>
   (commonly used for property name and value pairs)
*/

#ifdef SWIGPYTHON
%typemap(in) apr_hash_t *PROPHASH
{
  $1 = svn_swig_py_prophash_from_dict($input, _global_pool);
}
#endif

#ifdef SWIGRUBY
%typemap(in) apr_hash_t *PROPHASH
{
  $1 = svn_swig_rb_hash_to_apr_hash_svn_string($input, _global_pool);
}
#endif

/* -----------------------------------------------------------------------
   Output of apr_array_header_t * <svn_prop_t *>
*/
#ifdef SWIGRUBY
%typemap(argout) apr_array_header_t **OUTPUT_OF_PROP {
  %append_output(svn_swig_rb_apr_array_to_array_prop(*$1));
}
#endif

/* -----------------------------------------------------------------------
   Output of apr_array_header_t * <const char *>
*/

#ifdef SWIGPYTHON
%typemap(argout) apr_array_header_t **OUTPUT_OF_CONST_CHAR_P {
  %append_output(svn_swig_py_array_to_list(*$1));
}
#endif
#ifdef SWIGPERL
%typemap(argout) apr_array_header_t **OUTPUT_OF_CONST_CHAR_P {
  %append_output(svn_swig_pl_array_to_list(*$1));
}
#endif
#ifdef SWIGRUBY
%typemap(argout) apr_array_header_t **OUTPUT_OF_CONST_CHAR_P {
  %append_output(svn_swig_rb_apr_array_to_array_string(*$1));
}
#endif

/* -----------------------------------------------------------------------
   Input of apr_file_t *
*/

#ifdef SWIGPYTHON
%typemap(in) apr_file_t * {
  $1 = svn_swig_py_make_file($input, _global_pool);
  if (!$1) SWIG_fail;
}
#endif

#ifdef SWIGPERL
%typemap(in) apr_file_t * {
  $1 = svn_swig_pl_make_file($input, _global_pool);
}
#endif

#ifdef SWIGRUBY
%typemap(in) apr_file_t * {
  $1 = svn_swig_rb_make_file($input, _global_pool);
}
#endif

/* -----------------------------------------------------------------------
   Output of apr_file_t *
   ... is currently being handled by the standard OUTPARAM code in
   svn_types.swg, but should probably be more comfortably translated to
   scripting language concepts.
*/
