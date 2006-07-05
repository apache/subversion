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
 * svn_containers.swg: This is a child file of svn_types.swg, and
 *   should not be included directly.  This file should contain
 *   typemaps that deal container types
 *   - i.e. apr_hash_t, apr_array_header_t .
 */

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
