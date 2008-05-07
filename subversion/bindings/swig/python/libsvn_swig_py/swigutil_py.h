/*
 * swigutil_py.h :  utility functions and stuff for the SWIG Python bindings
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

/* Define DLL export magic on Windows. */
#ifdef WIN32
#  ifdef SVN_SWIG_SWIGUTIL_PY_C
#    define SVN_SWIG_SWIGUTIL_EXPORT __declspec(dllexport)
#  else
#    define SVN_SWIG_SWIGUTIL_EXPORT __declspec(dllimport)
#  endif
#else
#  define SVN_SWIG_SWIGUTIL_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* Initialize the libsvn_swig_py library. */
SVN_SWIG_SWIGUTIL_EXPORT
apr_status_t svn_swig_py_initialize(void);



/* Functions to manage python's global interpreter lock */
SVN_SWIG_SWIGUTIL_EXPORT
void svn_swig_py_release_py_lock(void);

SVN_SWIG_SWIGUTIL_EXPORT
void svn_swig_py_acquire_py_lock(void);


/*** Automatic Pool Management Functions ***/

/* Set the application pool */
SVN_SWIG_SWIGUTIL_EXPORT
void svn_swig_py_set_application_pool(PyObject *py_pool, apr_pool_t *pool);

/* Clear the application pool */
SVN_SWIG_SWIGUTIL_EXPORT
void svn_swig_py_clear_application_pool(void);

/* Get the pool argument from the last element of tuple args.
 * If the last element of args is not a pool, create a new
 * subpool. Return 0 if successful. Return 1 if an error
 * occurs.
 */
SVN_SWIG_SWIGUTIL_EXPORT
int svn_swig_py_get_pool_arg(PyObject *args, swig_type_info *type,
    PyObject **py_pool, apr_pool_t **pool);

/* Get the parent pool of the first argument in the specified
 * argument list. Return 0 if successful. Return 1 if an error
 * occurs.
 */
SVN_SWIG_SWIGUTIL_EXPORT
int svn_swig_py_get_parent_pool(PyObject *args, swig_type_info *type,
    PyObject **py_pool, apr_pool_t **pool);


/*** SWIG Wrappers ***/

/* Wrapper for SWIG_NewPointerObj */
SVN_SWIG_SWIGUTIL_EXPORT
PyObject *svn_swig_NewPointerObj(void *obj, swig_type_info *type,
                                 PyObject *pool, PyObject *args);

/* Wrapper for SWIG_ConvertPtr */
SVN_SWIG_SWIGUTIL_EXPORT
int svn_swig_ConvertPtr(PyObject *input, void **obj, swig_type_info *type);

/* Wrapper for SWIG_MustGetPtr */
SVN_SWIG_SWIGUTIL_EXPORT
void *svn_swig_MustGetPtr(void *input, swig_type_info *type, int argnum);

/*** Functions to expose a custom SubversionException ***/

/* raise a subversion exception, created from a normal subversion
   error.  consume the error.  */
SVN_SWIG_SWIGUTIL_EXPORT
void svn_swig_py_svn_exception(svn_error_t *err);



/* helper function to convert an apr_hash_t* (char* -> svnstring_t*) to
   a Python dict */
SVN_SWIG_SWIGUTIL_EXPORT
PyObject *svn_swig_py_prophash_to_dict(apr_hash_t *hash);

/* helper function to convert an apr_hash_t* (svn_revnum_t* -> const
   char *) to a Python dict */
SVN_SWIG_SWIGUTIL_EXPORT
PyObject *svn_swig_py_locationhash_to_dict(apr_hash_t *hash);

/* helper function to convert an apr_array_header_t* (of
   svn_merge_range_t *) to a Python list */
SVN_SWIG_SWIGUTIL_EXPORT
PyObject *svn_swig_py_rangelist_to_list(apr_array_header_t *rangelist,
                                        swig_type_info *type,
                                        PyObject *py_pool);

/* helper function to convert an apr_hash_t* (const char *->array of
   svn_merge_range_t *) to a Python dict */
SVN_SWIG_SWIGUTIL_EXPORT
PyObject *svn_swig_py_mergeinfo_to_dict(apr_hash_t *hash,
                                        swig_type_info *type,
                                        PyObject *py_pool);

/* helper function to convert an apr_hash_t* (const char *->hash of
   mergeinfo hashes) to a Python dict */
SVN_SWIG_SWIGUTIL_EXPORT
PyObject *svn_swig_py_mergeinfo_catalog_to_dict(apr_hash_t *hash,
                                                swig_type_info *type,
                                                PyObject *py_pool);

/* helper function to convert an apr_hash_t *(const char *->const char
 *) to a Python dict */

SVN_SWIG_SWIGUTIL_EXPORT
PyObject *svn_swig_py_stringhash_to_dict(apr_hash_t *hash);

/* convert a hash of 'const char *' -> TYPE into a Python dict */
SVN_SWIG_SWIGUTIL_EXPORT
PyObject *svn_swig_py_convert_hash(apr_hash_t *hash, swig_type_info *type,
                                   PyObject *py_pool);

/* helper function to convert a 'char **' into a Python list of string
   objects */
SVN_SWIG_SWIGUTIL_EXPORT
PyObject *svn_swig_py_c_strings_to_list(char **strings);

/* helper function to convert an array of 'const char *' to a Python list
   of string objects */
SVN_SWIG_SWIGUTIL_EXPORT
PyObject *svn_swig_py_array_to_list(const apr_array_header_t *strings);

/* helper function to convert a hash mapping char * to svn_string_t * to a
 * Python dict mapping str to str. */
SVN_SWIG_SWIGUTIL_EXPORT
PyObject *svn_swig_py_changed_path_hash_to_dict(apr_hash_t *hash);

SVN_SWIG_SWIGUTIL_EXPORT
apr_array_header_t *svn_swig_py_rangelist_to_array(PyObject *list,
                                                   apr_pool_t *pool);

/* helper function to convert an array of 'svn_revnum_t' to a Python list
   of int objects */
SVN_SWIG_SWIGUTIL_EXPORT
PyObject *svn_swig_py_revarray_to_list(const apr_array_header_t *revs);

/* helper function to convert a Python dictionary mapping strings to
   strings into an apr_hash_t mapping const char *'s to const char *'s,
   allocated in POOL. */
SVN_SWIG_SWIGUTIL_EXPORT
apr_hash_t *svn_swig_py_stringhash_from_dict(PyObject *dict,
                                             apr_pool_t *pool);

/* helper function to convert a Python dictionary mapping strings to
   rangelists into an apr_hash_t mapping const char *'s to rangelists,
   allocated in POOL. */
SVN_SWIG_SWIGUTIL_EXPORT
apr_hash_t *svn_swig_py_mergeinfo_from_dict(PyObject *dict,
                                             apr_pool_t *pool);

/* helper function to convert a Python dictionary mapping strings to
   strings into an 'apr_array_header_t *' of svn_prop_t *
   allocated in POOL. */
SVN_SWIG_SWIGUTIL_EXPORT
apr_array_header_t *svn_swig_py_proparray_from_dict(PyObject *dict,
                                                    apr_pool_t *pool);

/* helper function to convert a 'apr_array_header_t *' of 'svn_prop_t
   to a Python dictionary mapping strings to strings. */
SVN_SWIG_SWIGUTIL_EXPORT
PyObject *svn_swig_py_proparray_to_dict(const apr_array_header_t *array);

/* helper function to convert a Python dictionary mapping strings to
   strings into an apr_hash_t mapping const char *'s to svn_string_t's,
   allocated in POOL. */
SVN_SWIG_SWIGUTIL_EXPORT
apr_hash_t *svn_swig_py_prophash_from_dict(PyObject *dict,
                                           apr_pool_t *pool);

/* helper function to convert a Python dictionary mapping strings to
   integers into an apr_hash_t mapping const char *'s to revnums,
   allocated in POOL. */
SVN_SWIG_SWIGUTIL_EXPORT
apr_hash_t *svn_swig_py_path_revs_hash_from_dict(PyObject *dict,
                                                 apr_pool_t *pool);

/* helper function to convert a Python dictionary mapping strings to
   strings into an apr_hash_t mapping const char *'s to svn_string_t *'s,
   allocated in POOL. */
SVN_SWIG_SWIGUTIL_EXPORT
apr_hash_t *svn_swig_py_changed_path_hash_from_dict(PyObject *dict,
                                                    apr_pool_t *pool);

/* helper function to convert a Python sequence of strings into an
   'apr_array_header_t *' of 'const char *' objects.  Note that the
   objects must remain alive -- the values are not copied. This is
   appropriate for incoming arguments which are defined to last the
   duration of the function's execution.  */
SVN_SWIG_SWIGUTIL_EXPORT
const apr_array_header_t *svn_swig_py_strings_to_array(PyObject *source,
                                                       apr_pool_t *pool);

/* like svn_swig_py_strings_to_array(), but for array's of 'svn_revnum_t's. */
SVN_SWIG_SWIGUTIL_EXPORT
const apr_array_header_t *svn_swig_py_revnums_to_array(PyObject *source,
                                                       apr_pool_t *pool);

/* make an editor that "thunks" from C callbacks up to Python */
SVN_SWIG_SWIGUTIL_EXPORT
void svn_swig_py_make_editor(const svn_delta_editor_t **editor,
                             void **edit_baton,
                             PyObject *py_editor,
                             apr_pool_t *pool);

SVN_SWIG_SWIGUTIL_EXPORT
apr_file_t *svn_swig_py_make_file(PyObject *py_file,
                                  apr_pool_t *pool);

SVN_SWIG_SWIGUTIL_EXPORT
svn_stream_t *svn_swig_py_make_stream(PyObject *py_io,
                                      apr_pool_t *pool);

/* a notify function that executes a Python function that is passed in
   via the baton argument */
SVN_SWIG_SWIGUTIL_EXPORT
void svn_swig_py_notify_func(void *baton,
                             const char *path,
                             svn_wc_notify_action_t action,
                             svn_node_kind_t kind,
                             const char *mime_type,
                             svn_wc_notify_state_t content_state,
                             svn_wc_notify_state_t prop_state,
                             svn_revnum_t revision);

SVN_SWIG_SWIGUTIL_EXPORT
void svn_swig_py_notify_func2(void *baton,
                              const svn_wc_notify_t *notify,
                              apr_pool_t *pool);

/* a status function that executes a Python function that is passed in
   via the baton argument */
SVN_SWIG_SWIGUTIL_EXPORT
void svn_swig_py_status_func(void *baton,
                             const char *path,
                             svn_wc_status_t *status);

/* a svn_delta_path_driver callback that executes a Python function
  that is passed in via the baton argument */
SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *svn_swig_py_delta_path_driver_cb_func(void **dir_baton,
                                                   void *parent_baton,
                                                   void *callback_baton,
                                                   const char *path,
                                                   apr_pool_t *pool);

/* a status function that executes a Python function that is passed in
   via the baton argument */
SVN_SWIG_SWIGUTIL_EXPORT
void svn_swig_py_status_func2(void *baton,
                              const char *path,
                              svn_wc_status2_t *status);

/* a cancel function that executes a Python function passed in via the
   cancel_baton argument. */
SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *svn_swig_py_cancel_func(void *cancel_baton);

/* thunked fs get_locks function */
SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *svn_swig_py_fs_get_locks_func(void *baton,
                                           svn_lock_t *lock,
                                           apr_pool_t *pool);

/* thunked commit log fetcher */
SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *svn_swig_py_get_commit_log_func(const char **log_msg,
                                             const char **tmp_file,
                                             const apr_array_header_t *
                                             commit_items,
                                             void *baton,
                                             apr_pool_t *pool);

/* thunked repos authz callback function */
SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *svn_swig_py_repos_authz_func(svn_boolean_t *allowed,
                                          svn_fs_root_t *root,
                                          const char *path,
                                          void *baton,
                                          apr_pool_t *pool);

/* thunked history callback function */
SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *svn_swig_py_repos_history_func(void *baton,
                                            const char *path,
                                            svn_revnum_t revision,
                                            apr_pool_t *pool);

/* thunked log receiver function */
SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *svn_swig_py_log_receiver(void *py_receiver,
                                      apr_hash_t *changed_paths,
                                      svn_revnum_t rev,
                                      const char *author,
                                      const char *date,
                                      const char *msg,
                                      apr_pool_t *pool);

/* thunked log receiver2 function */
SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *svn_swig_py_log_entry_receiver(void *baton,
                                            svn_log_entry_t *log_entry,
                                            apr_pool_t *pool);

/* thunked info receiver function */
SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *svn_swig_py_info_receiver_func(void *py_receiver,
                                            const char *path,
                                            const svn_info_t *info,
                                            apr_pool_t *pool);

/* thunked location segments receiver function */
SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *
svn_swig_py_location_segment_receiver_func(svn_location_segment_t *segment,
                                           void *baton,
                                           apr_pool_t *pool);

/* thunked blame receiver function */
SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *svn_swig_py_client_blame_receiver_func(void *baton,
                                                    apr_int64_t line_no,
                                                    svn_revnum_t revision,
                                                    const char *author,
                                                    const char *date,
                                                    const char *line,
                                                    apr_pool_t *pool);

/* thunked changelist receiver function */
SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *svn_swig_py_changelist_receiver_func(void *baton,
                                                  const char *path,
                                                  const char *changelist,
                                                  apr_pool_t *pool);

/* auth provider callbacks */
SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *svn_swig_py_auth_simple_prompt_func(
    svn_auth_cred_simple_t **cred,
    void *baton,
    const char *realm,
    const char *username,
    svn_boolean_t may_save,
    apr_pool_t *pool);

SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *svn_swig_py_auth_username_prompt_func(
    svn_auth_cred_username_t **cred,
    void *baton,
    const char *realm,
    svn_boolean_t may_save,
    apr_pool_t *pool);

SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *svn_swig_py_auth_ssl_server_trust_prompt_func(
    svn_auth_cred_ssl_server_trust_t **cred,
    void *baton,
    const char *realm,
    apr_uint32_t failures,
    const svn_auth_ssl_server_cert_info_t *cert_info,
    svn_boolean_t may_save,
    apr_pool_t *pool);

SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *svn_swig_py_auth_ssl_client_cert_prompt_func(
    svn_auth_cred_ssl_client_cert_t **cred,
    void *baton,
    const char *realm,
    svn_boolean_t may_save,
    apr_pool_t *pool);

SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *svn_swig_py_auth_ssl_client_cert_pw_prompt_func(
    svn_auth_cred_ssl_client_cert_pw_t **cred,
    void *baton,
    const char *realm,
    svn_boolean_t may_save,
    apr_pool_t *pool);

SVN_SWIG_SWIGUTIL_EXPORT
void
svn_swig_py_setup_ra_callbacks(svn_ra_callbacks2_t **callbacks,
                               void **baton,
                               PyObject *py_callbacks,
                               apr_pool_t *pool);

SVN_SWIG_SWIGUTIL_EXPORT
svn_wc_diff_callbacks2_t *
svn_swig_py_setup_wc_diff_callbacks2(void **baton,
                                     PyObject *py_callbacks,
                                     apr_pool_t *pool);

SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *svn_swig_py_commit_callback2(const svn_commit_info_t *commit_info,
                                          void *baton,
                                          apr_pool_t *pool);

SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *svn_swig_py_commit_callback(svn_revnum_t new_revision,
                                         const char *date,
                                         const char *author,
                                         void *baton);


SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *svn_swig_py_ra_file_rev_handler_func(
                    void *baton,
                    const char *path,
                    svn_revnum_t rev,
                    apr_hash_t *rev_props,
                    svn_txdelta_window_handler_t *delta_handler,
                    void **delta_baton,
                    apr_array_header_t *prop_diffs,
                    apr_pool_t *pool);

SVN_SWIG_SWIGUTIL_EXPORT
svn_error_t *svn_swig_py_ra_lock_callback(
                    void *baton,
                    const char *path,
                    svn_boolean_t do_lock,
                    const svn_lock_t *lock,
                    svn_error_t *ra_err,
                    apr_pool_t *pool);

SVN_SWIG_SWIGUTIL_EXPORT
extern const svn_ra_reporter2_t swig_py_ra_reporter2;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_SWIG_SWIGUTIL_PY_H */
