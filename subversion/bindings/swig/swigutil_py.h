/*
 * swigutil_py.h :  utility functions and stuff for the SWIG Python bindings
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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


#ifndef SVN_SWIG_SWIGUTIL_PY_H
#define SVN_SWIG_SWIGUTIL_PY_H

#include <Python.h>

#include <apr.h>
#include <apr_pools.h>
#include <apr_strings.h>
#include <apr_hash.h>
#include <apr_tables.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_delta.h"
#include "svn_client.h"
#include "svn_repos.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* If this file is being included outside of a wrapper file, then need to
   create stubs for some of the SWIG types. */

/* if SWIGEXPORT is defined, then we're in a wrapper. otherwise, we need
   the prototypes and type definitions. */
#ifndef SWIGEXPORT
#define SVN_NEED_SWIG_TYPES
#endif

#ifdef SVN_NEED_SWIG_TYPES

typedef struct _unnamed swig_type_info;
PyObject *SWIG_NewPointerObj(void *, swig_type_info *, int own);
swig_type_info *SWIG_TypeQuery(const char *name);

#endif /* SVN_NEED_SWIG_TYPES */


/* helper function to convert an apr_hash_t* (char* -> svnstring_t*) to
   a Python dict */
PyObject *svn_swig_py_prophash_to_dict(apr_hash_t *hash);

/* convert a hash of 'const char *' -> TYPE into a Python dict */
PyObject *svn_swig_py_convert_hash(apr_hash_t *hash, swig_type_info *type);

/* helper function to convert a 'char **' into a Python list of string
   objects */
PyObject *svn_swig_py_c_strings_to_list(char **strings);

/* helper function to convert an array of 'const char *' to a Python list
   of string objects */
PyObject *svn_swig_py_array_to_list(const apr_array_header_t *strings);

/* helper function to convert an array of 'svn_revnum_t' to a Python list
   of int objects */
PyObject *svn_swig_py_revarray_to_list(const apr_array_header_t *revs);

/* helper function to convert a Python sequence of strings into an
   'apr_array_header_t *' of 'const char *' objects.  Note that the
   objects must remain alive -- the values are not copied. This is
   appropriate for incoming arguments which are defined to last the
   duration of the function's execution.  */
const apr_array_header_t *svn_swig_py_strings_to_array(PyObject *source,
                                                       apr_pool_t *pool);

/* make a editor that "thunks" from C callbacks up to Python */
void svn_swig_py_make_editor(const svn_delta_editor_t **editor,
                             void **edit_baton,
                             PyObject *py_editor,
                             apr_pool_t *pool);

apr_file_t *svn_swig_py_make_file (PyObject *py_file,
                                   apr_pool_t *pool);

svn_opt_revision_t *svn_swig_py_make_opt_revision (PyObject *py_rev,
                                                   apr_pool_t *pool);

/* a notify function that executes a Python function that is passed in
   via the baton argument */
void svn_swig_py_notify_func(void *baton,
                             const char *path,
                             svn_wc_notify_action_t action,
                             svn_node_kind_t kind,
                             const char *mime_type,
                             svn_wc_notify_state_t content_state,
                             svn_wc_notify_state_t prop_state,
                             svn_revnum_t revision);

/* thunked log receiver function.  */
svn_error_t * svn_swig_py_thunk_log_receiver(void *py_receiver,
                                             apr_hash_t *changed_paths,
                                             svn_revnum_t rev,
                                             const char *author,
                                             const char *date,
                                             const char *msg,
                                             apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_SWIG_SWIGUTIL_PY_H */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end:
 */
