/* py_util.h : some help for the embedded python interpreter
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

#include "fs.h"

/* Initialize the python interpreter and load the fs module */
svn_error_t *
svn_fs_py__init_python(apr_pool_t *pool);

/* Destroy the PyObject pointed to by DATA.
 * This function in mainly used to cleanup Python objects on pool cleanup. */
apr_status_t
svn_fs_py__destroy_py_object(void *data);

/* Call NAME method of P_OBJ, putting the result in *P_RESULT.  If an
 * exception is raised by the Python method, convert it to an svn_error_t. */
svn_error_t*
svn_fs_py__call_method(PyObject **p_result,
                       PyObject *p_obj,
                       const char *name,
                       const char *format,
                       ...);

svn_error_t *
svn_fs_py__convert_and_consume(void **out_p,
                               PyObject *p_obj,
                               void *(*convert_func)(PyObject *, apr_pool_t *),
                               apr_pool_t *pool);

PyObject *
svn_fs_py__convert_cstring_hash(void *object);

PyObject *
svn_fs_py__convert_proplist(void *object);

void *
svn_fs_py__prophash_from_dict(PyObject *dict, apr_pool_t *pool);

/* Load a reference to the FS Python module into the shared data. */
svn_error_t *
svn_fs_py__load_module(fs_fs_data_t *ffd);

svn_error_t *
svn_fs_py__set_int_attr(PyObject *p_obj,
                        const char *name,
                        long int val);

/* Get an attribute value from a Python object, and return it in *RESULT,
   allocated in RESULT_POOL. */
svn_error_t *
svn_fs_py__get_string_attr(const char **result,
                           PyObject *p_obj,
                           const char *name,
                           apr_pool_t *result_pool);

svn_error_t *
svn_fs_py__get_int_attr(int *result,
                        PyObject *p_obj,
                        const char *name);

PyObject *
svn_fs_py__wrap_pack_notify_func(svn_fs_pack_notify_t notify_func,
                                 void *notify_baton);

PyObject *
svn_fs_py__wrap_cancel_func(svn_cancel_func_t cancel_func,
                            void *cancel_baton);
