/*
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 *
 * svn_fs.i: SWIG interface file for svn_fs.h
 */

%include svn_global.swg

#if defined(SWIGPYTHON)
%module(package="libsvn", moduleimport=SVN_PYTHON_MODULEIMPORT) fs
#elif defined(SWIGPERL)
%module "SVN::_Fs"
#elif defined(SWIGRUBY)
%module "svn::ext::fs"
#endif

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

#ifdef SWIGPYTHON
%callback_typemap(svn_fs_lock_callback_t lock_callback, void *lock_baton,
                  svn_swig_py_fs_lock_callback,
                  ,
                  )
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
   Tweak a SubversionException instance when it is raised in
   svn_fs_merge(), svn_fs_commit_txn() and svn_repos_fs_commit_txn().
   Those APIs return conflicts (and revision number on svn_fs_commit_txn
   and svn_repos_fs_commit_txn) related to the conflict error when it  
   is occured.  As Python wrapper functions report errors by raising
   exceptions and don't return values, we use attributes of the exception 
   to pass these values to the caller.
*/

#ifdef SWIGPYTHON
%typemap(argout) (const char **conflict_p) (PyObject* conflict_ob) {
    if (*$1 == NULL)
      {
        Py_INCREF(Py_None);
        conflict_ob = Py_None;
      }
    else
      {
        /* Note: We can check if apr_err == SVN_ERR_FS_CONFLICT or not
           before access to *$1 */
        conflict_ob = PyBytes_FromString((const char *)*$1);
        if (conflict_ob == NULL)
          {
            Py_XDECREF(exc_ob);
            Py_XDECREF($result);
            SWIG_fail;
          }
      }
    if (exc_ob != NULL)
      {
        PyObject_SetAttrString(exc_ob, "$1_name", conflict_ob); 
        Py_DECREF(conflict_ob);
      }
    else
      {
        %append_output(conflict_ob);
      }
}

%typemap(argout) svn_revnum_t *new_rev (PyObject *rev_ob) {
    rev_ob = PyInt_FromLong((long)*$1);
    if (rev_ob == NULL)
      {
        Py_XDECREF(exc_ob);
        Py_XDECREF($result);
        SWIG_fail;
      }
    if (exc_ob != NULL)
      {
        PyObject_SetAttrString(exc_ob, "$1_name", rev_ob); 
        Py_DECREF(rev_ob);
      }
    else
      {
        %append_output(rev_ob);
      }
}

%apply svn_error_t *SVN_ERR_WITH_ATTRS  {
    svn_error_t * svn_fs_commit_txn,
    svn_error_t * svn_fs_merge
};
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
