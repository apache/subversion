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
 * svn_wc.i: SWIG interface file for svn_wc.h
 */

#if defined(SWIGPYTHON)
%module(package="libsvn") wc
#elif defined(SWIGPERL)
%module "SVN::_Wc"
#elif defined(SWIGRUBY)
%module "svn::ext::wc"
#endif

%include svn_global.swg
%import core.i
%import svn_delta.i
%import svn_ra.i

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

/* svn_wc_get_prop_diffs() */
%apply apr_array_header_t **OUTPUT_OF_PROP {
  apr_array_header_t **propchanges
};

%apply apr_hash_t *PROPHASH {
  apr_hash_t *baseprops,
  apr_hash_t *new_base_props,
  apr_hash_t *new_props
};


/* svn_wc_cleanup2() */
%apply const char *MAY_BE_NULL {
    const char *diff3_cmd,
    const char *uuid,
    const char *repos,
    const char *new_text_path,
    const char *copyfrom_url,
    const char *rev_date,
    const char *rev_author,
    const char *trail_url
}


%hash_argout_typemap(entries, svn_wc_entry_t *)
%hash_argout_typemap(externals_p, svn_wc_external_item_t *)

#ifdef SWIGPYTHON
%callback_typemap(svn_wc_notify_func_t notify_func, void *notify_baton,
                  svn_swig_py_notify_func,
                  ,
                  )
#endif

#ifndef SWIGPERL
%callback_typemap(svn_wc_notify_func2_t notify_func, void *notify_baton,
                  svn_swig_py_notify_func2,
                  ,
                  svn_swig_rb_notify_func2)
#endif

#ifdef SWIGRUBY
%callback_typemap(const svn_wc_entry_callbacks2_t *walk_callbacks,
                  void *walk_baton,
                  ,
                  ,
                  svn_swig_rb_wc_entry_callbacks2())
#endif

#ifndef SWIGRUBY
%callback_typemap(svn_wc_status_func_t status_func, void *status_baton,
                  svn_swig_py_status_func,
                  svn_swig_pl_status_func,
                  )
#endif

#ifdef SWIGRUBY
%callback_typemap(svn_wc_status_func2_t status_func, void *status_baton,
                  ,
                  ,
                  svn_swig_rb_wc_status_func)

%callback_typemap(const svn_wc_diff_callbacks2_t *callbacks,
                  void *callback_baton,
                  ,
                  ,
                  svn_swig_rb_wc_diff_callbacks2())

%callback_typemap(svn_wc_relocation_validator2_t validator,
                  void *validator_baton,
                  ,
                  ,
                  svn_swig_rb_wc_relocation_validator2)
#endif


/* svn_wc_translated2() */
#ifdef SWIGRUBY
%apply const char **TO_TEMP_FILE {
    const char **xlated_path
};
#endif
/* ----------------------------------------------------------------------- */

%{
#include "svn_md5.h"
%}

%include svn_wc_h.swg

%inline %{
static svn_error_t *
svn_wc_swig_init_asp_dot_net_hack (apr_pool_t *pool)
{
#if defined(WIN32) || defined(__CYGWIN__)
  if (getenv ("SVN_ASP_DOT_NET_HACK"))
    SVN_ERR (svn_wc_set_adm_dir("_svn", pool));
#endif /* WIN32 */
  return SVN_NO_ERROR;
}
%}

#if defined(SWIGPYTHON)
%pythoncode %{ svn_wc_swig_init_asp_dot_net_hack() %}
#endif
