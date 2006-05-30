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

%include svn_global.swg
%import apr.swg
%import core.i
%import svn_types.swg
%import svn_string.swg
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

%apply SWIGTYPE **OUTPARAM {
    svn_wc_entry_t **,
    svn_wc_adm_access_t **,
    svn_wc_status_t **,
    svn_wc_status2_t **,
    svn_wc_revision_status_t **
};

/*
   svn_wc_check_wc(wc_format)
   svn_wc_merge(wc_format)
*/
%apply int *OUTPUT {
  int *,
  enum svn_wc_merge_outcome_t *
};

/*
   svn_wc_prop_list()
   svn_wc_get_prop_diffs()
*/
%apply apr_hash_t **PROPHASH {
  apr_hash_t **props,
  apr_hash_t **original_props
};

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

/* svn_wc_transmit_text_deltas2() */
%apply (const svn_delta_editor_t *EDITOR, void *BATON)
{
  (const svn_delta_editor_t *editor, void *file_baton),
  (const svn_delta_editor_t *editor, void *baton)
}


%apply const char **OUTPUT { char **url };


/* -----------------------------------------------------------------------
   apr_hash_t ** <const char *, const svn_wc_entry_t *>
   svn_wc_entries_read()
*/

%typemap(python, in, numinputs=0) apr_hash_t **entries = apr_hash_t **OUTPUT;
%typemap(python, argout, fragment="t_output_helper") apr_hash_t **entries {
    $result = t_output_helper(
        $result,
        svn_swig_py_convert_hash(*$1, $descriptor(svn_wc_entry_t *),
          _global_svn_swig_py_pool));
}

%typemap(ruby, in, numinputs=0) apr_hash_t **entries = apr_hash_t **OUTPUT;
%typemap(ruby, argout, fragment="output_helper") apr_hash_t **entries
{
  $result = output_helper($result,
                          svn_swig_rb_apr_hash_to_hash_swig_type
                            (*$1, "svn_wc_entry_t *"));
}

/* -----------------------------------------------------------------------
   apr_hash_t **externals_old
   apr_hash_t **externals_new
   svn_wc_edited_externals()
*/

%typemap(ruby, in, numinputs=0) apr_hash_t **externals_old = apr_hash_t **OUTPUT;
%typemap(ruby, argout, fragment="output_helper") apr_hash_t **externals_old
{
  $result = output_helper($result,
                          svn_swig_rb_apr_hash_to_hash_string(*$1));
}

%typemap(ruby, in, numinputs=0) apr_hash_t **externals_new = apr_hash_t **externals_old;
%typemap(ruby, argout, fragment="output_helper") apr_hash_t **externals_new = apr_hash_t **externals_old;


/* -----------------------------------------------------------------------
   apr_array_header_t **externals_p
   svn_wc_parse_externals_description2()
*/

%typemap(ruby, in, numinputs=0)
     apr_array_header_t **externals_p (apr_array_header_t *temp)
{
  $1 = &temp;
}
%typemap(ruby, argout, fragment="output_helper")
     apr_array_header_t **externals_p
{
  $result = output_helper($result,
                          svn_swig_rb_apr_array_to_array_external_item(*$1));
}

/* -----------------------------------------------------------------------
   apr_array_header_t **patterns
   svn_wc_get_default_ignores()
*/

%typemap(ruby, in, numinputs=0)
     apr_array_header_t **patterns (apr_array_header_t *temp)
{
  $1 = &temp;
}
%typemap(ruby, argout, fragment="output_helper")
     apr_array_header_t **patterns
{
  $result = output_helper($result,
                          svn_swig_rb_apr_array_to_array_string(*$1));
}

/* -----------------------------------------------------------------------
   apr_array_header_t *wcprop_changes
   svn_wc_process_committed2()
*/

%typemap(ruby, in) apr_array_header_t *wcprop_changes
{
  VALUE rb_pool;
  apr_pool_t *pool;

  svn_swig_rb_get_pool(argc, argv, self, &rb_pool, &pool);
        
  $1 = svn_swig_rb_array_to_apr_array_prop($input, pool);
}

/* -----------------------------------------------------------------------
   apr_array_header_t *propchanges
   svn_wc_merge_props()
*/
%typemap(ruby, in) apr_array_header_t *propchanges
{
  VALUE rb_pool;
  apr_pool_t *pool;

  svn_swig_rb_get_pool(argc, argv, self, &rb_pool, &pool);
        
  $1 = svn_swig_rb_array_to_apr_array_prop($input, pool);
}

/* -----------------------------------------------------------------------
   apr_array_header_t *merge_options
   svn_wc_merge2()
*/
%typemap(ruby, in) apr_array_header_t *merge_options
{
  if (NIL_P($input)) {
    $1 = NULL;
  } else {
    VALUE rb_pool;
    apr_pool_t *pool;

    svn_swig_rb_get_pool(argc, argv, self, &rb_pool, &pool);
    $1 = svn_swig_rb_array_to_apr_array_prop($input, pool);
  }
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

%typemap(ruby, in) (svn_wc_notify_func2_t notify_func, void *notify_baton)
{
  $1 = svn_swig_rb_notify_func2;
  $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
}

/* -----------------------------------------------------------------------
   Callback: svn_wc_entry_callbacks_t
   svn_wc_walk_entries2()
*/

%typemap(ruby, in) (const svn_wc_entry_callbacks_t *walk_callbacks,
                    void *walk_baton)
{
  $1 = svn_swig_rb_wc_entry_callbacks();
  $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
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
  $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
}

/* -----------------------------------------------------------------------
   Callback: svn_wc_callbacks2_t
   svn_wc_get_diff_editor3()
*/

%typemap(ruby, in) (const svn_wc_diff_callbacks2_t *callbacks,
                    void *callback_baton)
{
  $1 = svn_swig_rb_wc_diff_callbacks2();
  $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
}

/* -----------------------------------------------------------------------
   Callback: svn_wc_relocation_validator2_t
   svn_wc_relocate2()
*/

%typemap(ruby, in) (svn_wc_relocation_validator2_t validator,
                    void *validator_baton)
{
  $1 = svn_swig_rb_wc_relocation_validator2;
  $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
}

/* ----------------------------------------------------------------------- */

%{
#include "svn_md5.h"

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
