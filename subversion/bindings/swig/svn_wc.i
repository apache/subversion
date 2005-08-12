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

#if defined(SWIGPERL)
%module "SVN::_Wc"
#elif defined(SWIGRUBY)
%module "svn::ext::wc"
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

/* -----------------------------------------------------------------------
   Callback: svn_wc_notify_func2_t
   svn_client_ctx_t
   svn_wc many
*/

%typemap(ruby, in) (svn_wc_notify_func2_t notify_func2, void *notify_baton2)
{
  $1 = svn_swig_rb_notify_func2;
  $2 = (void *)$input;
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

/* -----------------------------------------------------------------------
   Callback: svn_wc_status_func2_t
   svn_client_status2()
   svn_wc_get_status_editor2()
*/

%typemap(ruby, in) (svn_wc_status_func2_t status_func,
                    void *status_baton)
{
  $1 = svn_swig_rb_wc_status_func;
  $2 = (void *)$input;
}

/* ----------------------------------------------------------------------- */

%{
#include "svn_wc.h"

#ifdef SWIGPYTHON
#include "swigutil_py.h"
#endif

#ifdef SWIGPERL
#include "swigutil_pl.h"
#endif

#ifdef SWIGRUBY
#include "swigutil_rb.h"
#endif
%}

%include svn_wc.h
