/*
 * svn_repos.i :  SWIG interface file for svn_repos.h
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
%module "SVN::_Repos"
#elif defined(SWIGRUBY)
%module "svn::ext::repos"
#else
%module repos
#endif

%include typemaps.i

%import apr.i
%import svn_types.i
%import svn_string.i
%import svn_delta.i
%import svn_fs.i

/* -----------------------------------------------------------------------
   %apply-ing of typemaps defined elsewhere
*/
%apply SWIGTYPE **OUTPARAM {
    svn_repos_t **,
    const svn_delta_editor_t **editor,
    void **edit_baton
};

%apply const char *MAY_BE_NULL {
    const char *src_entry,
    const char *unused_1,
    const char *unused_2,
    const char *token
};

/* svn_repos_db_logfiles() */
%apply apr_array_header_t **OUTPUT_OF_CONST_CHAR_P {
    apr_array_header_t **logfiles
}

/* svn_repos_get_logs() */
%apply const apr_array_header_t *STRINGLIST {
    const apr_array_header_t *paths
};

#ifdef SWIGPYTHON
%apply svn_stream_t *WRAPPED_STREAM { svn_stream_t * };
#endif

/* -----------------------------------------------------------------------
   handle the 'location_revisions' parameter appropriately
*/
%typemap(python,in) apr_array_header_t *location_revisions {
    $1 = (apr_array_header_t *) svn_swig_py_revnums_to_array($input,
                                                             _global_pool);
    if ($1 == NULL)
        return NULL;
}

/* -----------------------------------------------------------------------
   XXX: for some reasons svn_delta_editor doesn't get typemapped even
   if svn_delta.i is imported. so we redeclare here.
*/

%typemap(perl5, in) (const svn_delta_editor_t *editor, void *edit_baton) {
    svn_delta_make_editor(&$1, &$2, $input, _global_pool);
}

/* -----------------------------------------------------------------------
   handle svn_repos_history_func_t/baton pairs
*/
%typemap(python,in) (svn_repos_history_func_t history_func, void *history_baton) {

  $1 = svn_swig_py_repos_history_func;
  $2 = $input; /* our function is the baton. */
}

%typemap(perl5,in) (svn_repos_history_func_t history_func, void *history_baton) {

  $1 = svn_swig_pl_thunk_history_func;
  $2 = $input; /* our function is the baton. */
}

/* -----------------------------------------------------------------------
   handle svn_repos_fs_get_locks
*/
%typemap(python,in,numinputs=0) apr_hash_t **locks = apr_hash_t **OUTPUT;
%typemap(python,argout,fragment="t_output_helper") apr_hash_t **locks {
    $result = t_output_helper(
        $result,
        svn_swig_py_convert_hash(*$1, SWIGTYPE_p_svn_lock_t));
}


/* -----------------------------------------------------------------------
   handle svn_repos_authz_read_func_t/baton pairs
*/

%typemap(perl5, in) (svn_repos_authz_func_t authz_read_func, void *authz_read_baton) {
  if (SvOK ($input)) {
    $1 = svn_swig_pl_thunk_authz_func;
    $2 = $input; /* our function is the baton */
  }
  else {
    $1 = NULL;
    $2 = NULL;
  }
}

%typemap(python, in) (svn_repos_authz_func_t authz_read_func, void *authz_read_baton) {
  $1 = svn_swig_py_repos_authz_func;
  $2 = $input; /* our function is the baton. */
}

%typemap(ruby, in) (svn_repos_authz_func_t authz_read_func, void *authz_read_baton) {
  $1 = svn_swig_rb_repos_authz_func;
  $2 = (void *)$input;
}

/* -----------------------------------------------------------------------
   handle config and fs_config in svn_repos_create
*/

/* ### TODO: %typemap(python, in) apr_hash_t *config {} */

%typemap(python, in) apr_hash_t *fs_config {
    $1 = svn_swig_py_stringhash_from_dict ($input, _global_pool);
}
    
%typemap(perl5, in) apr_hash_t *config {
    $1 = svn_swig_pl_objs_to_hash_by_name ($input, "svn_config_t *",
					   _global_pool);
}

%typemap(perl5, in) apr_hash_t *fs_config {
    $1 = svn_swig_pl_strings_to_hash ($input, _global_pool);
}

/* -----------------------------------------------------------------------
   handle the output from svn_repos_trace_node_locations()
*/
%typemap(python,in,numinputs=0) apr_hash_t **locations = apr_hash_t **OUTPUT;
%typemap(python,argout,fragment="t_output_helper") apr_hash_t **locations {
    $result = t_output_helper($result, svn_swig_py_locationhash_to_dict(*$1));
}

/* ----------------------------------------------------------------------- */

%{
#include "svn_repos.h"

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

%include svn_repos.h
