/*
 * swigutil_py.h :  utility functions and stuff for the SWIG Python bindings
 *
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
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* Initialize the libsvn_swig_py library. */
apr_status_t svn_swig_py_initialize(void);


/* Returns the provided python object as a FILE *object.
 * Return the underlying FILE or NULL if the object is not a File instance.
 */
FILE *svn_swig_py_as_file(PyObject *pyfile);



/* Functions to manage python's global interpreter lock */
void svn_swig_py_release_py_lock(void);

void svn_swig_py_acquire_py_lock(void);


/*** Automatic Pool Management Functions ***/

/* Set the application pool */
void svn_swig_py_set_application_pool(PyObject *py_pool, apr_pool_t *pool);

/* Clear the application pool */
void svn_swig_py_clear_application_pool(void);

/* Get the pool argument from the last element of tuple args.
 * If the last element of args is not a pool, create a new
 * subpool. Return 0 if successful. Return 1 if an error
 * occurs.
 */
int svn_swig_py_get_pool_arg(PyObject *args, swig_type_info *type,
    PyObject **py_pool, apr_pool_t **pool);

/* Get the parent pool of the first argument in the specified
 * argument list. Return 0 if successful. Return 1 if an error
 * occurs.
 */
int svn_swig_py_get_parent_pool(PyObject *args, swig_type_info *type,
    PyObject **py_pool, apr_pool_t **pool);


/*** SWIG Wrappers ***/

/* Wrapper for SWIG_NewPointerObj */
PyObject *svn_swig_py_new_pointer_obj(void *obj, swig_type_info *type,
                                 PyObject *pool, PyObject *args);

/* Wrapper for SWIG_ConvertPtr */
int svn_swig_py_convert_ptr(PyObject *input, void **obj, swig_type_info *type);

/* Wrapper for SWIG_MustGetPtr */
void *svn_swig_py_must_get_ptr(void *input, swig_type_info *type, int argnum);


/*** Functions to expose a custom SubversionException ***/

/* Get a SubversionException class object and its instance built from
   error_chain, but do not raise it immediately.  Consume the
   error_chain.  */
void svn_swig_py_build_svn_exception(
    PyObject **exc_class, PyObject **exc_ob, svn_error_t *error_chain);

/* Raise a SubversionException, created from a normal subversion
   error.  Consume the error.  */
void svn_swig_py_svn_exception(svn_error_t *err);



/* Function to get char * representation of bytes/str object. This is
   the replacement of typemap(in, parse="s") and typemap(in, parse="z")
   to accept both of bytes object and str object, and it assumes to be
   used from those typemaps only.
   Note: type of return value should be char const *, however, as SWIG
   produces variables for C function without 'const' modifier, to avoid
   ton of cast in SWIG produced C code we drop it from return value
   types as well  */
char *svn_swig_py_string_to_cstring(PyObject *input, int maybe_null,
                                    const char * funcsym, const char * argsym);

/* helper function to convert an apr_hash_t* (char* -> svnstring_t*) to
   a Python dict */
PyObject *svn_swig_py_prophash_to_dict(apr_hash_t *hash);

/* helper function to convert an apr_hash_t* (svn_revnum_t* -> const
   char *) to a Python dict */
PyObject *svn_swig_py_locationhash_to_dict(apr_hash_t *hash);

/* helper function to convert an apr_array_header_t* (of
   svn_merge_range_t *) to a Python list */
PyObject *svn_swig_py_pointerlist_to_list(apr_array_header_t *list,
                                          swig_type_info *type,
                                          PyObject *py_pool);

/* helper function to convert an apr_hash_t* (const char *->array of
   svn_merge_range_t *) to a Python dict */
PyObject *svn_swig_py_mergeinfo_to_dict(apr_hash_t *hash,
                                        swig_type_info *type,
                                        PyObject *py_pool);

/* helper function to convert an apr_hash_t* (const char *->hash of
   mergeinfo hashes) to a Python dict */
PyObject *svn_swig_py_mergeinfo_catalog_to_dict(apr_hash_t *hash,
                                                swig_type_info *type,
                                                PyObject *py_pool);

/* helper function to convert an apr_hash_t *(const char *->const char
 *) to a Python dict */

PyObject *svn_swig_py_stringhash_to_dict(apr_hash_t *hash);

/* convert a hash of 'const char *' -> TYPE into a Python dict */
PyObject *svn_swig_py_convert_hash(apr_hash_t *hash, swig_type_info *type,
                                   PyObject *py_pool);

/* helper function to convert a 'char **' into a Python list of string
   objects */
PyObject *svn_swig_py_c_strings_to_list(char **strings);

/* helper function to convert an array of 'const char *' to a Python list
   of string objects */
PyObject *svn_swig_py_array_to_list(const apr_array_header_t *strings);

/* helper function to convert a hash mapping char * to
 * svn_log_changed_path_t * to a Python dict mapping str to str. */
PyObject *svn_swig_py_changed_path_hash_to_dict(apr_hash_t *hash);

/* helper function to convert a hash mapping char * to
 * svn_log_changed_path2_t * to a Python dict mapping str to str. */
PyObject *svn_swig_py_changed_path2_hash_to_dict(apr_hash_t *hash);

/* helper function to convert an array of 'svn_revnum_t' to a Python list
   of int objects */
PyObject *svn_swig_py_revarray_to_list(const apr_array_header_t *revs);

/* helper function to convert a Python dictionary mapping strings to
   strings into an apr_hash_t mapping const char *'s to const char *'s,
   allocated in POOL. */
apr_hash_t *svn_swig_py_stringhash_from_dict(PyObject *dict,
                                             apr_pool_t *pool);

/* helper function to convert a Python dictionary mapping strings to
   rangelists into an apr_hash_t mapping const char *'s to rangelists,
   allocated in POOL. */
apr_hash_t *svn_swig_py_mergeinfo_from_dict(PyObject *dict,
                                             apr_pool_t *pool);

/* helper function to convert a Python dictionary mapping strings to
   strings into an 'apr_array_header_t *' of svn_prop_t *
   allocated in POOL. */
apr_array_header_t *svn_swig_py_proparray_from_dict(PyObject *dict,
                                                    apr_pool_t *pool);

/* helper function to convert a 'apr_array_header_t *' of 'svn_prop_t
   to a Python dictionary mapping strings to strings. */
PyObject *svn_swig_py_proparray_to_dict(const apr_array_header_t *array);

/* helper function to convert a 'apr_array_header_t *' of
   'svn_prop_inherited_item_t' to a Python dictionary mapping strings
   to dictionary. */
PyObject *
svn_swig_py_propinheriteditemarray_to_dict(const apr_array_header_t *array);

/* helper function to convert a Python dictionary mapping strings to
   strings into an apr_hash_t mapping const char *'s to svn_string_t's,
   allocated in POOL. */
apr_hash_t *svn_swig_py_prophash_from_dict(PyObject *dict,
                                           apr_pool_t *pool);

/* helper function to convert a Python dictionary mapping strings to
   integers into an apr_hash_t mapping const char *'s to revnums,
   allocated in POOL. */
apr_hash_t *svn_swig_py_path_revs_hash_from_dict(PyObject *dict,
                                                 apr_pool_t *pool);

/* helper function to convert a Python dictionary mapping strings to
   SWIG wrappers described by type into an apr_hash_t mapping const char *'s to
   struct pointers, allocated in POOL. */
apr_hash_t *svn_swig_py_struct_ptr_hash_from_dict(PyObject *dict,
                                                  swig_type_info *type,
                                                  apr_pool_t *pool);

/* Callback function for use in data structure conversion routines. It is
   supposed to extract a C value of a certain type from object, write it into
   the location given by destination, and return zero. If that turns out to be
   infeasible, it shall raise a Python exception and return a negative value. */
typedef int (*svn_swig_py_object_unwrap_t)(PyObject *source,
                                           void *destination,
                                           void *baton);

/* Helper function to convert a Python sequence into an immutable APR array. The
   resulting array's elements will be presumed to be of size element_size and
   will be obtained by applying unwrap_func/unwrap_baton to elements from seq.
   If seq is None, returns NULL.
   In case of failure, raises a Python exception, presuming that seq was the
   function argument #argnum.
   pool is used to allocate the array. */
const apr_array_header_t *
svn_swig_py_seq_to_array(PyObject *seq,
                         int element_size,
                         svn_swig_py_object_unwrap_t unwrap_func,
                         void *unwrap_baton,
                         apr_pool_t *pool);

/* An svn_swig_py_object_unwrap_t that extracts a char pointer from a Python
   string.

   Note the lifetime of the returned string is tied to the provided Python
   object. */
int
svn_swig_py_unwrap_string(PyObject *source,
                          void *destination,
                          void *baton);

/* An svn_swig_py_object_unwrap_t that extracts an svn_revnum_t from a Python
   integer. */
int
svn_swig_py_unwrap_revnum(PyObject *source,
                          void *destination,
                          void *baton);

/* An svn_swig_py_object_unwrap_t that extracts a struct pointer from a SWIG
   wrapper. baton is expected to be a swig_type_info* describing the struct. */
int
svn_swig_py_unwrap_struct_ptr(PyObject *source,
                          void *destination,
                          void *baton);

/* make an editor that "thunks" from C callbacks up to Python */
void svn_swig_py_make_editor(const svn_delta_editor_t **editor,
                             apr_pool_t *pool);

/* make a parse vtable that "thunks" from C callbacks up to Python */
void svn_swig_py_make_parse_fns3(const svn_repos_parse_fns3_t **parse_fns3,
                                 apr_pool_t *pool);

apr_file_t *svn_swig_py_make_file(PyObject *py_file,
                                  apr_pool_t *pool);

svn_stream_t *svn_swig_py_make_stream(PyObject *py_io,
                                      apr_pool_t *pool);

/* Convert ops, a C array of num_ops elements, to a Python list of SWIG
   objects with descriptor op_type_info and pool set to parent_pool. */
PyObject *
svn_swig_py_convert_txdelta_op_c_array(int num_ops,
                                       svn_txdelta_op_t *ops,
                                       swig_type_info * op_type_info,
                                       PyObject *parent_pool);

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

void svn_swig_py_notify_func2(void *baton,
                              const svn_wc_notify_t *notify,
                              apr_pool_t *pool);

/* a status function that executes a Python function that is passed in
   via the baton argument */
void svn_swig_py_status_func(void *baton,
                             const char *path,
                             svn_wc_status_t *status);

/* a client status function that executes a Python function that is passed in
   via the baton argument */
svn_error_t *svn_swig_py_client_status_func(void *baton,
                                            const char *path,
                                            const svn_client_status_t *status,
                                            apr_pool_t *scratch_pool);

/* a svn_delta_path_driver callback that executes a Python function
  that is passed in via the baton argument */
svn_error_t *svn_swig_py_delta_path_driver_cb_func(void **dir_baton,
                                                   void *parent_baton,
                                                   void *callback_baton,
                                                   const char *path,
                                                   apr_pool_t *pool);

/* a status function that executes a Python function that is passed in
   via the baton argument */
void svn_swig_py_status_func2(void *baton,
                              const char *path,
                              svn_wc_status2_t *status);

/* a cancel function that executes a Python function passed in via the
   cancel_baton argument. */
svn_error_t *svn_swig_py_cancel_func(void *cancel_baton);

/* thunked fs get_locks function */
svn_error_t *svn_swig_py_fs_get_locks_func(void *baton,
                                           svn_lock_t *lock,
                                           apr_pool_t *pool);

svn_error_t *svn_swig_py_fs_lock_callback(
                    void *baton,
                    const char *path,
                    const svn_lock_t *lock,
                    svn_error_t *ra_err,
                    apr_pool_t *pool);

/* thunked commit log fetcher */
svn_error_t *svn_swig_py_get_commit_log_func(const char **log_msg,
                                             const char **tmp_file,
                                             const apr_array_header_t *
                                             commit_items,
                                             void *baton,
                                             apr_pool_t *pool);

/* thunked repos authz callback function */
svn_error_t *svn_swig_py_repos_authz_func(svn_boolean_t *allowed,
                                          svn_fs_root_t *root,
                                          const char *path,
                                          void *baton,
                                          apr_pool_t *pool);

/* thunked history callback function */
svn_error_t *svn_swig_py_repos_history_func(void *baton,
                                            const char *path,
                                            svn_revnum_t revision,
                                            apr_pool_t *pool);

/* thunked log receiver function */
svn_error_t *svn_swig_py_log_receiver(void *py_receiver,
                                      apr_hash_t *changed_paths,
                                      svn_revnum_t rev,
                                      const char *author,
                                      const char *date,
                                      const char *msg,
                                      apr_pool_t *pool);

/* thunked log receiver2 function */
svn_error_t *svn_swig_py_log_entry_receiver(void *baton,
                                            svn_log_entry_t *log_entry,
                                            apr_pool_t *pool);

/* thunked repos freeze function */
svn_error_t *svn_swig_py_repos_freeze_func(void *baton,
                                           apr_pool_t *pool);

/* thunked fs freeze function */
svn_error_t *svn_swig_py_fs_freeze_func(void *baton,
                                        apr_pool_t *pool);

/* thunked proplist receiver2 function */
svn_error_t *svn_swig_py_proplist_receiver2(void *baton,
                                            const char *path,
                                            apr_hash_t *prop_hash,
                                            apr_array_header_t *inherited_props,
                                            apr_pool_t *pool);

/* thunked info receiver function */
svn_error_t *svn_swig_py_info_receiver_func(void *py_receiver,
                                            const char *path,
                                            const svn_info_t *info,
                                            apr_pool_t *pool);

/* thunked location segments receiver function */
svn_error_t *
svn_swig_py_location_segment_receiver_func(svn_location_segment_t *segment,
                                           void *baton,
                                           apr_pool_t *pool);

/* thunked blame receiver function */
svn_error_t *svn_swig_py_client_blame_receiver_func(void *baton,
                                                    apr_int64_t line_no,
                                                    svn_revnum_t revision,
                                                    const char *author,
                                                    const char *date,
                                                    const char *line,
                                                    apr_pool_t *pool);

/* thunked changelist receiver function */
svn_error_t *svn_swig_py_changelist_receiver_func(void *baton,
                                                  const char *path,
                                                  const char *changelist,
                                                  apr_pool_t *pool);

/* auth provider callbacks */
svn_error_t * svn_swig_py_auth_gnome_keyring_unlock_prompt_func(
        char **keyring_passwd,
        const char *keyring_name,
        void *baton,
        apr_pool_t *pool);

svn_error_t *svn_swig_py_auth_simple_prompt_func(
    svn_auth_cred_simple_t **cred,
    void *baton,
    const char *realm,
    const char *username,
    svn_boolean_t may_save,
    apr_pool_t *pool);

svn_error_t *svn_swig_py_auth_username_prompt_func(
    svn_auth_cred_username_t **cred,
    void *baton,
    const char *realm,
    svn_boolean_t may_save,
    apr_pool_t *pool);

svn_error_t *svn_swig_py_auth_ssl_server_trust_prompt_func(
    svn_auth_cred_ssl_server_trust_t **cred,
    void *baton,
    const char *realm,
    apr_uint32_t failures,
    const svn_auth_ssl_server_cert_info_t *cert_info,
    svn_boolean_t may_save,
    apr_pool_t *pool);

svn_error_t *svn_swig_py_auth_ssl_client_cert_prompt_func(
    svn_auth_cred_ssl_client_cert_t **cred,
    void *baton,
    const char *realm,
    svn_boolean_t may_save,
    apr_pool_t *pool);

svn_error_t *svn_swig_py_auth_ssl_client_cert_pw_prompt_func(
    svn_auth_cred_ssl_client_cert_pw_t **cred,
    void *baton,
    const char *realm,
    svn_boolean_t may_save,
    apr_pool_t *pool);

/* auth cleanup callback */
svn_error_t *svn_swig_py_config_auth_walk_func(svn_boolean_t *delete_cred,
                                               void *walk_baton,
                                               const char *cred_kind,
                                               const char *realmstring,
                                               apr_hash_t *hash,
                                               apr_pool_t *scratch_pool);

void
svn_swig_py_setup_ra_callbacks(svn_ra_callbacks2_t **callbacks,
                               void **baton,
                               PyObject *py_callbacks,
                               apr_pool_t *pool);

svn_wc_diff_callbacks2_t *
svn_swig_py_setup_wc_diff_callbacks2(void **baton,
                                     PyObject *py_callbacks,
                                     apr_pool_t *pool);

svn_error_t *svn_swig_py_commit_callback2(const svn_commit_info_t *commit_info,
                                          void *baton,
                                          apr_pool_t *pool);

svn_error_t *svn_swig_py_commit_callback(svn_revnum_t new_revision,
                                         const char *date,
                                         const char *author,
                                         void *baton);


svn_error_t *svn_swig_py_ra_file_rev_handler_func(
                    void *baton,
                    const char *path,
                    svn_revnum_t rev,
                    apr_hash_t *rev_props,
                    svn_txdelta_window_handler_t *delta_handler,
                    void **delta_baton,
                    apr_array_header_t *prop_diffs,
                    apr_pool_t *pool);

svn_error_t *svn_swig_py_ra_lock_callback(
                    void *baton,
                    const char *path,
                    svn_boolean_t do_lock,
                    const svn_lock_t *lock,
                    svn_error_t *ra_err,
                    apr_pool_t *pool);

const svn_ra_reporter2_t *svn_swig_py_get_ra_reporter2(void);

svn_boolean_t
svn_swig_py_config_enumerator2(const char *name,
                               const char *value,
                               void *baton,
                               apr_pool_t *pool);

svn_boolean_t
svn_swig_py_config_section_enumerator2(const char *name,
                                       void *baton,
                                       apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_SWIG_SWIGUTIL_PY_H */
