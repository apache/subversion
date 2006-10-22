/*
 * svn_fs.i :  SWIG interface file for svn_fs.h
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
%module "SVN::_Fs"
#elif defined(SWIGRUBY)
%module "svn::ext::fs"
#else
%module fs
#endif

%include typemaps.i

%include svn_global.swg
%import apr.swg
%import core.i
%import svn_types.swg
%import svn_string.swg
%import svn_delta.i

/* -----------------------------------------------------------------------
   do not generate any constructors or destructors (of structures) -- all
   structures are going to come /out/ of the FS (so we don't need to
   construct the things) and will live in a pool (so we don't need to
   destroy the things).
*/
%nodefault;

/* Redundant from 1.1 onwards, so not worth manually wrapping the callback. */
%ignore svn_fs_set_berkeley_errcall;

/* -----------------------------------------------------------------------
   %apply-ing of typemaps defined elsewhere
*/
%apply SWIGTYPE **OUTPARAM {
    svn_fs_root_t **,
    svn_fs_txn_t **,
    void **,
    svn_fs_history_t **,
    svn_fs_id_t **,
    svn_fs_access_t **,
    svn_lock_t **,
    svn_fs_t **
};

%apply const char **OUTPUT { const char ** };

/* svn_fs_*_proplist() */
%apply apr_hash_t **PROPHASH { apr_hash_t **table_p };

/* ### need to deal with IN params which have "const" and OUT params which
   ### return non-const type. SWIG's type checking may see these as
   ### incompatible. */

%apply const char *MAY_BE_NULL {
    const char *base_checksum,
    const char *result_checksum,
    const char *token,
    const char *comment
};

%apply apr_hash_t *STRING_TO_STRING { apr_hash_t *fs_config };

/* svn_fs_parse_id() */
%apply (const char *PTR, apr_size_t LEN) {
    (const char *data, apr_size_t len)
}

/* svn_fs_berkeley_logfiles(), svn_fs_list_transactions() */
%apply apr_array_header_t **OUTPUT_OF_CONST_CHAR_P {
    apr_array_header_t **logfiles,
    apr_array_header_t **names_p
}

#ifdef SWIGPYTHON
%apply svn_stream_t *WRAPPED_STREAM { svn_stream_t * };
#endif

/* -----------------------------------------------------------------------
   except for svn_fs_dir_entries, which returns svn_fs_dirent_t structures
*/

%typemap(python,in,numinputs=0) apr_hash_t **entries_p = apr_hash_t **OUTPUT;
%typemap(python,argout,fragment="t_output_helper") apr_hash_t **entries_p {
    $result = t_output_helper(
        $result,
        svn_swig_py_convert_hash(*$1, $descriptor(svn_fs_dirent_t *),
          _global_svn_swig_py_pool));
}
%typemap(perl5,in,numinputs=0) apr_hash_t **entries_p = apr_hash_t **OUTPUT;
%typemap(perl5,argout) apr_hash_t **entries_p {
    ST(argvi++) = svn_swig_pl_convert_hash(*$1, 
      $descriptor(svn_fs_dirent_t *));
}
%typemap(ruby,in,numinputs=0) apr_hash_t **entries_p = apr_hash_t **OUTPUT;
%typemap(ruby,argout) apr_hash_t **entries_p {
  $result = svn_swig_rb_apr_hash_to_hash_swig_type(*$1, "svn_fs_dirent_t *");
}

/* -----------------------------------------------------------------------
   and except for svn_fs_paths_changed, which returns svn_fs_path_change_t
   structures
*/

%typemap(python, in,numinputs=0) apr_hash_t **changed_paths_p = apr_hash_t **OUTPUT;
%typemap(python, argout, fragment="t_output_helper") apr_hash_t **changed_paths_p {
    $result = t_output_helper(
        $result,
        svn_swig_py_convert_hash(*$1, $descriptor(svn_fs_path_change_t *),
          _global_svn_swig_py_pool));
}

%typemap(perl5, in,numinputs=0) apr_hash_t **changed_paths_p = apr_hash_t **OUTPUT;
%typemap(perl5, argout) apr_hash_t **changed_paths_p {
    ST(argvi++) = svn_swig_pl_convert_hash(*$1, 
      $descriptor(svn_fs_path_change_t *));
}

%typemap(ruby, in, numinputs=0) apr_hash_t **changed_paths_p = apr_hash_t **OUTPUT;
%typemap(ruby, argout) apr_hash_t **changed_paths_p
{
  $result = svn_swig_rb_apr_hash_to_hash_swig_type(*$1,
                                                   "svn_fs_path_change_t *");
}

/* -----------------------------------------------------------------------
   handle get_locks_func/get_locks_baton pairs.
*/
%typemap(python, in) (svn_fs_get_locks_callback_t get_locks_func, void *get_locks_baton) {
  $1 = svn_swig_py_fs_get_locks_func;
  $2 = $input; /* our function is the baton. */
}

%typemap(ruby, in) (svn_fs_get_locks_callback_t get_locks_func, void *get_locks_baton)
{
  $1 = svn_swig_rb_fs_get_locks_callback;
  $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
}

/* -----------------------------------------------------------------------
   Fix the return value for svn_fs_commit_txn(). If the conflict result is
   NULL, then t_output_helper() is passed Py_None, but that goofs up
   because that is *also* the marker for "I haven't started assembling a
   multi-valued return yet" which means the second return value (new_rev)
   will not cause a 2-tuple to be manufactured.

   The answer is to explicitly create a 2-tuple return value.
*/
%typemap(python, argout) (const char **conflict_p, svn_revnum_t *new_rev) {
    /* this is always Py_None */
    Py_DECREF($result);
    /* build the result tuple */
    $result = Py_BuildValue("zi", *$1, (long)*$2);
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

#ifdef SWIGRUBY
%ignore svn_fs_set_warning_func;
%ignore svn_fs_root_fs;
#endif

%include svn_fs_h.swg

#ifdef SWIGRUBY
%inline %{
static void
svn_fs_set_warning_func_wrapper(svn_fs_t *fs,
                                svn_fs_warning_callback_t warning,
                                void *warning_baton,
                                apr_pool_t *pool)
{
  svn_fs_set_warning_func(fs, warning, warning_baton);
}

static svn_fs_t *
svn_fs_root_fs_wrapper(svn_fs_root_t *root, apr_pool_t *pool)
{
  return svn_fs_root_fs(root);
}
%}
#endif
