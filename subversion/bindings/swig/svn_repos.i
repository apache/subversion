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
 * svn_repos.i: SWIG interface file for svn_repos.h
 */

#if defined(SWIGPERL)
%module "SVN::_Repos"
#elif defined(SWIGRUBY)
%module "svn::ext::repos"
#else
%module repos
#endif

%include svn_global.swg
%import core.i
%import svn_delta.i
%import svn_fs.i

/* -----------------------------------------------------------------------
   %apply-ing of typemaps defined elsewhere
*/
%apply const char *MAY_BE_NULL {
    const char *src_entry,
    const char *unused_1,
    const char *unused_2,
    const char *token,
    const char *user,
    const char *log_msg,
    const char *lock_token,
    const char *tgt_path
};

%apply apr_hash_t *STRING_TO_STRING { apr_hash_t *fs_config };

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
   XXX: for some reasons svn_delta_editor doesn't get typemapped even
   if svn_delta.i is imported. so we redeclare here.
*/

#ifdef SWIGPERL
%typemap(in) (const svn_delta_editor_t *editor, void *edit_baton) {
    svn_delta_make_editor(&$1, &$2, $input, _global_pool);
}
#endif

/* -----------------------------------------------------------------------
   handle svn_repos_history_func_t/baton pairs
*/
#ifdef SWIGPYTHON
%typemap(in) (svn_repos_history_func_t history_func, void *history_baton) {

  $1 = svn_swig_py_repos_history_func;
  $2 = $input; /* our function is the baton. */
}
#endif

#ifdef SWIGPERL
%typemap(in) (svn_repos_history_func_t history_func, void *history_baton) {

  $1 = svn_swig_pl_thunk_history_func;
  $2 = $input; /* our function is the baton. */
}
#endif

#ifdef SWIGRUBY
%typemap(in) (svn_repos_history_func_t history_func, void *history_baton)
{
  $1 = svn_swig_rb_repos_history_func;
  $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
}
#endif

/* -----------------------------------------------------------------------
   handle svn_repos_fs_get_locks
*/
#ifdef SWIGPYTHON
%typemap(argout) apr_hash_t **locks {
  %append_output(svn_swig_py_convert_hash(*$1, $descriptor(svn_lock_t *),
                                          _global_svn_swig_py_pool));
}
#endif


/* -----------------------------------------------------------------------
   handle svn_repos_authz_read_func_t/baton pairs
*/

#ifdef SWIGPERL
%typemap(in) (svn_repos_authz_func_t authz_read_func, void *authz_read_baton) {
  if (SvOK ($input)) {
    $1 = svn_swig_pl_thunk_authz_func;
    $2 = $input; /* our function is the baton */
  }
  else {
    $1 = NULL;
    $2 = NULL;
  }
}
#endif

#ifdef SWIGPYTHON
%typemap(in) (svn_repos_authz_func_t authz_read_func, void *authz_read_baton) {
  $1 = svn_swig_py_repos_authz_func;
  $2 = $input; /* our function is the baton. */
}
#endif

#ifdef SWIGRUBY
%typemap(in) (svn_repos_authz_func_t authz_read_func, void *authz_read_baton)
{
  if (NIL_P($input)) {
    $1 = NULL;
    $2 = NULL;
  } else {
    $1 = svn_swig_rb_repos_authz_func;
    $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
  }
}
#endif

/* -----------------------------------------------------------------------
   handle start_callback of svn_repos_recover2().
*/

/* cause SWIG syntax error.
#ifdef SWIGRUBY
%typemap(in) (svn_error_t *(*)(void *baton) start_callback, void *start_callback_baton)
{
  $1 = svn_swig_rb_just_call;
  $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
}
#endif
*/

/* -----------------------------------------------------------------------
   handle svn_repos_file_rev_handler_t/baton pairs
*/

#ifdef SWIGRUBY
%typemap(in) (svn_repos_file_rev_handler_t handler,
                    void *handler_baton)
{
  $1 = svn_swig_rb_repos_file_rev_handler;
  $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
}
#endif

/* -----------------------------------------------------------------------
   handle svn_repos_authz_func_t/baton pairs
*/

#ifdef SWIGRUBY
%typemap(in) (svn_repos_authz_func_t authz_read_func,
                    void *authz_read_baton)
{
  $1 = svn_swig_rb_repos_authz_func;
  $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
}
#endif

/* -----------------------------------------------------------------------
   handle svn_repos_authz_callback_t/baton pairs
*/

#ifdef SWIGRUBY
%typemap(in) (svn_repos_authz_callback_t authz_callback,
                    void *authz_baton)
{
  $1 = svn_swig_rb_repos_authz_callback;
  $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
}
#endif

/* -----------------------------------------------------------------------
   handle svn_repos_get_committed_info().
*/
#ifdef SWIGRUBY
%typemap(argout) const char **committed_date {
  %append_output(svn_swig_rb_svn_date_string_to_time(*$1));
}
#endif

/* -----------------------------------------------------------------------
   handle config in svn_repos_create
*/

/* ### TODO: %typemap(python, in) apr_hash_t *config {} */

#ifdef SWIGPERL
%typemap(in) apr_hash_t *config {
    $1 = svn_swig_pl_objs_to_hash_by_name ($input, "svn_config_t *",
                                           _global_pool);
}
#endif

/* ----------------------------------------------------------------------- */

#ifdef SWIGRUBY
%ignore svn_repos_fs;
#endif

%include svn_repos_h.swg

#ifdef SWIGRUBY
%inline %{
static svn_fs_t *
svn_repos_fs_wrapper(svn_repos_t *fs, apr_pool_t *pool)
{
  return svn_repos_fs(fs);
}
%}
#endif
