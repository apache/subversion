/*
 * svn_wc.i :  SWIG interface file for svn_wc.h
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

#ifdef SWIGPERL
%module "SVN::_Wc"
#else
%module wc
#endif

%include typemaps.i

%import apr.i
%import svn_types.i
%import svn_string.i
%import svn_delta.i

/* -----------------------------------------------------------------------
   ### these functions require a pool, which we don't have immediately
   ### handy. just eliminate these funcs for now.
*/
%ignore svn_wc_set_auth_file;

/* ### ignore this structure because the accessors will need a pool */
%ignore svn_wc_keywords_t;

#ifdef SWIGJAVA
/* Ignore these function pointer members because swig's string
   representations of their types approach the maximum path
   length on windows, causing swig to crash when it outputs 
   java wrapper classes for them. */
%ignore svn_wc_diff_callbacks_t::file_added;
%ignore svn_wc_diff_callbacks_t::file_changed;
%ignore svn_wc_diff_callbacks_t::file_deleted;
#endif

/* -----------------------------------------------------------------------
   %apply-ing of typemaps defined elsewhere
*/

%apply SWIGTYPE **OUTPARAM {
    svn_wc_entry_t **,
    svn_wc_adm_access_t **,
    svn_wc_status_t **
};

/* svn_wc_check_wc(wc_format) */
%apply int *OUTPUT { int * };

/* svn_wc_prop_list() */
%apply apr_hash_t **PROPHASH { apr_hash_t **props };

/* -----------------------------------------------------------------------
   apr_hash_t ** <const char *, const svn_wc_entry_t *>
   svn_wc_entries_read()
*/

%typemap(python, in, numinputs=0) apr_hash_t **entries = apr_hash_t **OUTPUT;
%typemap(python, argout, fragment="t_output_helper") apr_hash_t **entries {
    $result = t_output_helper(
        $result,
        svn_swig_py_convert_hash(*$1, SWIGTYPE_p_svn_wc_entry_t));
}

/* -----------------------------------------------------------------------
   Callback: svn_wc_notify_func_t
   svn_client_ctx_t
   svn_wc many
*/

%typemap(python,in) (svn_wc_notify_func_t notify_func, void *notify_baton) {
  $1 = svn_swig_py_notify_func;
  $2 = $input; /* our function is the baton. */
}

%typemap(java,in) (svn_wc_notify_func_t notify_func, void *notify_baton) {
  $1 = svn_swig_java_notify_func;
  $2 = (void*)$input; /* our function is the baton. */
}

%typemap(jni) svn_wc_notify_func_t "jobject"
%typemap(jtype) svn_wc_notify_func_t "org.tigris.subversion.wc.Notifier"
%typemap(jstype) svn_wc_notify_func_t "org.tigris.subversion.wc.Notifier"
%typemap(javain) svn_wc_notify_func_t "$javainput"
%typemap(javaout) svn_wc_notify_func_t {
    return $jnicall;
  }

/* -----------------------------------------------------------------------
   Callback: svn_wc_status_func_t
   svn_client_status()
   svn_wc_get_status_editor()
*/

%typemap(python,in) (svn_wc_status_func_t status_func, void *status_baton) {
  $1 = svn_swig_py_status_func;
  $2 = $input; /* our function is the baton. */
}

%typemap(perl5,in) (svn_wc_status_func_t status_func, void *status_baton) {
  $1 = svn_swig_pl_status_func;
  $2 = $input; /* our function is the baton. */
}

/* ----------------------------------------------------------------------- */

%include svn_wc.h
%{
#include "svn_wc.h"

#ifdef SWIGPYTHON
#include "swigutil_py.h"
#endif

#ifdef SWIGJAVA
#include "swigutil_java.h"
#endif

#ifdef SWIGPERL
#include "swigutil_pl.h"
#endif
%}
