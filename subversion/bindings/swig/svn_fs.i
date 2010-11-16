/*
 * ====================================================================
 * Copyright (c) 2000-2006, 2009 CollabNet.  All rights reserved.
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
 * svn_fs.i: SWIG interface file for svn_fs.h
 */

#if defined(SWIGPYTHON)
%module(package="libsvn") fs
#elif defined(SWIGPERL)
%module "SVN::_Fs"
#elif defined(SWIGRUBY)
%module "svn::ext::fs"
#endif

%include svn_global.swg
%import core.i
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

/* ### need to deal with IN params which have "const" and OUT params which
   ### return non-const type. SWIG's type checking may see these as
   ### incompatible. */

%apply const char *MAY_BE_NULL {
    const char *base_checksum,
    const char *result_checksum,
    const char *token,
    const char *uuid,
    const char *comment
};

#ifdef SWIGPYTHON
%apply svn_stream_t *WRAPPED_STREAM { svn_stream_t * };
#endif

%hash_argout_typemap(entries_p, svn_fs_dirent_t *)
%hash_argout_typemap(changed_paths_p, svn_fs_path_change_t *)
%hash_argout_typemap(changed_paths2_p, svn_fs_path_change2_t *)

#ifndef SWIGPERL
%callback_typemap(svn_fs_get_locks_callback_t get_locks_func,
                  void *get_locks_baton,
                  svn_swig_py_fs_get_locks_func,
                  ,
                  svn_swig_rb_fs_get_locks_callback)
#endif
/* -----------------------------------------------------------------------
   svn_fs_get_merge_info
*/

#ifdef SWIGPYTHON
%typemap(argout) apr_hash_t **minfohash
{
  %append_output(svn_swig_py_stringhash_to_dict(*$1));
}
#endif

%apply apr_hash_t *MERGEINFO { apr_hash_t *mergeinhash };

/* -----------------------------------------------------------------------
   Fix the return value for svn_fs_commit_txn(). If the conflict result is
   NULL, then %append_output() is passed Py_None, but that goofs up
   because that is *also* the marker for "I haven't started assembling a
   multi-valued return yet" which means the second return value (new_rev)
   will not cause a 2-tuple to be manufactured.

   The answer is to explicitly create a 2-tuple return value.

   FIXME: Do the Perl and Ruby bindings need to do something similar?
*/
#ifdef SWIGPYTHON
%typemap(argout) (const char **conflict_p, svn_revnum_t *new_rev) {
    /* this is always Py_None */
    Py_DECREF($result);
    /* build the result tuple */
    $result = Py_BuildValue("zi", *$1, (long)*$2);
}
#endif

/* Ruby fixups for functions not following the pool convention. */
#ifdef SWIGRUBY
%ignore svn_fs_set_warning_func;
%ignore svn_fs_root_fs;

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

/* ----------------------------------------------------------------------- */

%{
#include <apr_md5.h>
#include "svn_md5.h"
%}

%include svn_fs_h.swg

#ifdef SWIGRUBY
%define_close_related_methods(fs);
#endif
